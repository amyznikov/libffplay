/*
 * sendvideo.c
 *
 *  Created on: May 15, 2016
 *      Author: amyznikov
 */
#include "sendvideo.h"
#include "ffmpeg.h"
#include "pthread_wait.h"
#include "cclist.h"
#include "ffplay-java-api.h"
#include "debug.h"

#define OUTPUT_FIFO_SIZE   4


struct ff_output_stream {

  char * server;
  char * format;
  char * codec;
  char * ffopts;
  int quality;
  int gopsize;
  int bitrate;
  int cx, cy;
  enum AVPixelFormat input_pix_fmt, output_pix_fmt;

  int64_t firstpts;
  ff_output_stream_event_callback events_callback;
  void * cookie;

  pthread_t pid;
  pthread_wait_t lock;

  struct ccfifo p, q;

  ff_output_stream_state state;
  int status, reason;
  bool interrupted:1;

  struct output_stream_stats stats;
};



static void ctx_lock(ff_output_stream * ctx) {
  pthread_wait_lock(&ctx->lock);
}

static void ctx_unlock(ff_output_stream * ctx) {
  pthread_wait_unlock(&ctx->lock);
}

static int ctx_wait(ff_output_stream * ctx, int tmo) {
  return pthread_wait(&ctx->lock, tmo);
}

static void ctx_signal(ff_output_stream * ctx) {
  pthread_wait_broadcast(&ctx->lock);
}

static int output_stream_interrupt_callback(void * arg)
{
  return ((ff_output_stream * )arg)->interrupted;
}


static void set_stream_state(ff_output_stream * ctx, ff_output_stream_state state, int reason, bool lock)
{
  if ( lock ) {
    ctx_lock(ctx);
  }

  ctx->state = state;

  if ( !ctx->reason ) {
    ctx->reason = reason;
  }

  if ( ctx->events_callback.stream_state_changed ) {
    ctx->events_callback.stream_state_changed(ctx->cookie, ctx, ctx->state, reason);
  }

  ctx_signal(ctx);

  if ( lock ) {
    ctx_unlock(ctx);
  }
}


static int create_video_stream(AVCodecContext ** cctx, AVFormatContext * oc, const AVCodec * codec,
    const AVDictionary * options, const struct ff_output_stream * ffos)
{
  AVDictionary * codec_opts = NULL;
  AVDictionaryEntry * e = NULL;
  AVStream * os = NULL;

  int bitrate =  ffos->bitrate >= 1000 ? ffos->bitrate : 128000;
  int gop_size = ffos->gopsize > 0 ? ffos->gopsize : 25;
  int quality = ffos->quality > 0 && ffos->quality <= 100 ? ffos->quality : 50;
  int q;

  int status = 0;

  * cctx = NULL;

  /// Filter codec opts

  if ( (e = av_dict_get(options, "-b:v", NULL, 0)) ) {
    if ( (bitrate = (int) av_strtod(e->value, NULL)) < 1000 ) {
      PDBG("Bad output bitrate specified: %s", e->value);
      status = AVERROR(EINVAL);
      goto end;
    }
  }

  if ( (e = av_dict_get(options, "-g", NULL, 0)) ) {
    if ( sscanf(e->value, "%d", &gop_size) != 1 || gop_size < 1 ) {
      PDBG("Bad output gop size specified: %s", e->value);
      status = AVERROR(EINVAL);
      goto end;
    }
  }

  if ( (status = ffmpeg_filter_codec_opts(options, codec, AV_OPT_FLAG_ENCODING_PARAM, &codec_opts)) ) {
    PERROR("ffmpeg_filter_codec_opts('%s') fails", codec->name);
    goto end;
  }


  /// Open encoder

  if ( !(*cctx = avcodec_alloc_context3(codec)) ) {
    PDBG("avcodec_alloc_context3('%s') fails", codec->name);
    status = AVERROR(ENOMEM);
    goto end;
  }

  (*cctx)->time_base = (AVRational) { 1, 1000 };
  (*cctx)->pix_fmt = ffos->output_pix_fmt;
  (*cctx)->width = ffos->cx;
  (*cctx)->height = ffos->cy;
  (*cctx)->bit_rate = 0;// bitrate;
  (*cctx)->gop_size = gop_size;
  (*cctx)->me_range = 1;
  (*cctx)->flags |= CODEC_FLAG_GLOBAL_HEADER;
  (*cctx)->qmin = 1;
  (*cctx)->qmax = 32;


  PDBG("GOT quality=%d%%", quality);


  if ( strcmp(codec->name, "libx264") == 0 ) {
    if ( quality > 0 && quality <= 100 && !av_dict_get(options, "-crf", NULL, 0)
        && !av_dict_get(options, "-qp", NULL, 0) ) {
      if ( (q = (100 - quality) * 50 / 100 + 10) > 51 ) {
        q = 51;
      }
      PDBG("SET qp=%d", q);
      av_dict_set_int(&codec_opts, "qp", q, 0);
    }
  }
  else if ( quality > 0 && quality <= 100 && !av_dict_get(options, "-qmin", NULL, 0)
      && !av_dict_get(options, "-qmax", NULL, 0) ) {

    if ( (q = (100 - quality) * 32 / 100 + 1) > 31 ) {
      q = 31;
    }
    (*cctx)->qmin = (*cctx)->qmax = q;
    PDBG("SET qminmax=%d", q);
    av_dict_set_int(&codec_opts, "qmin", q, 0);
    av_dict_set_int(&codec_opts, "qmax", q, 0);
  }


  if ( (status = avcodec_open2(*cctx, codec, &codec_opts)) ) {
    PDBG("avcodec_open2('%s') fails: %s", codec->name, av_err2str(status));
    goto end;
  }

  /// Alloc stream

  if ( !(os = avformat_new_stream(oc, codec)) ) {
    status = AVERROR(ENOMEM);
    goto end;
  }

  if ( (status = avcodec_parameters_from_context(os->codecpar, *cctx)) < 0 ) {
    PDBG("avcodec_parameters_from_context('%s') fails: %s", codec->name, av_err2str(status));
    goto end;
  }

  os->time_base = (*cctx)->time_base;

end:

  av_dict_free(&codec_opts);

  return status;
}


static bool is_io_error(int status)
{
  switch ( status ) {
    case AVERROR(EIO) :
      case AVERROR(EREMOTEIO) :
      case AVERROR(ETIMEDOUT) :
      case AVERROR(EPIPE) :
      case AVERROR(ENETDOWN) :
      case AVERROR(ENETUNREACH) :
      case AVERROR(ENETRESET) :
      case AVERROR(ECONNREFUSED) :
      case AVERROR(ECONNRESET) :
      case AVERROR(ECONNABORTED) :
      // case AVERROR(EHOSTUNREACH):
      return true;
  }
  return false;
}



static int output_loop(struct ff_output_stream * ctx)
{
  AVDictionary * opts = NULL;
  AVDictionaryEntry * e = NULL;

  const char * format_name = ctx->format ? ctx->format : "matroska";
  AVOutputFormat * oformat = NULL;

  const char * vcodec_name =  ctx->codec ?  ctx->codec : "libx264";
  const AVCodec * vcodec = NULL;
  AVCodecContext * vcodec_ctx = NULL;

  AVFormatContext * oc = NULL;
  AVStream * os = NULL;

  struct frm * frm;

  AVFrame * output_frame = NULL;
  AVFrame * input_frame = NULL;
  struct SwsContext * sws = NULL;
  int cx, cy;

  AVPacket pkt;
  int gotpkt;

  bool write_header_ok = false;

  int status;


  PDBG("ENTER");





  cx = ctx->cx;
  cy = ctx->cy;

  av_init_packet(&pkt);
  pkt.data = NULL, pkt.size = 0;


  /// Parse command line

  if ( !ctx->ffopts && strcmp(vcodec_name, "libx264") == 0 ) {
    ctx->ffopts = strdup("-preset veryfast -tune zerolatency -rc-lookahead 3 -profile Main -level 42"); //
  }

  if ( (status = av_dict_parse_string(&opts, ctx->ffopts, " \t", " \t", 0)) ) {
    PERROR("av_dict_parse_string() fails: %s", av_err2str(status));
    goto end;
  }



  /// Get output format

  if ( (e = av_dict_get(opts, "-f", NULL, 0)) ) {
    format_name = e->value;
  }
  if ( !(oformat = av_guess_format(format_name, NULL, NULL)) ) {
    status = AVERROR_MUXER_NOT_FOUND;
    PERROR("av_guess_format('%s') fails: %s", format_name, av_err2str(status));
    goto end;
  }



  /// Get output codec

  if ( (e = av_dict_get(opts, "-c:v", NULL, 0)) ) {
    vcodec_name = e->value;
  }
  if ( !(vcodec = avcodec_find_encoder_by_name(vcodec_name)) ) {
    status = AVERROR_ENCODER_NOT_FOUND;
    PERROR("avcodec_find_encoder_by_name('%s') fails: %s", vcodec_name, av_err2str(status));
    goto end;
  }




  /// Alloc frame buffers

  if ( !(e = av_dict_get(opts, "-pix_fmt", NULL, 0)) ) {
    ctx->output_pix_fmt = vcodec->pix_fmts ? vcodec->pix_fmts[0] : ctx->input_pix_fmt;
    PDBG("PIXFMT ctx->output_pix_fmt=%d SELECTED", ctx->output_pix_fmt);
  }
  else if ( (ctx->output_pix_fmt = av_get_pix_fmt(e->value)) == AV_PIX_FMT_NONE ) {
    PDBG("Bad pixel format specified: %s", e->value);
    status = AVERROR(EINVAL);
    goto end;
  }


  if ( (status = ffmpeg_create_video_frame(&input_frame, ctx->input_pix_fmt, ctx->cx, ctx->cy, 0)) ) {
    PERROR("ffmpeg_create_video_frame(input_frame) fails: %s", av_err2str(status));
    goto end;
  }

  if ( (status = ffmpeg_create_video_frame(&output_frame, ctx->output_pix_fmt, ctx->cx, ctx->cy, 32)) ) {
    PERROR("ffmpeg_create_video_frame(output_frame) fails: %s", av_err2str(status));
    goto end;
  }

  if ( !(sws = sws_getContext(cx, cy, ctx->input_pix_fmt, cx, cy, ctx->output_pix_fmt, SWS_FAST_BILINEAR, NULL, NULL, NULL)) ) {
    PERROR("sws_getContext() fails");
    goto end;
  }




  /// Alloc output context

  if ( (status = avformat_alloc_output_context2(&oc, oformat, NULL, ctx->server)) < 0 ) {
    PERROR("avformat_alloc_output_context2() fails: %s", av_err2str(status));
    goto end;
  }

  oc->interrupt_callback.callback = output_stream_interrupt_callback;
  oc->interrupt_callback.opaque = ctx;



  /// Create video stream
  if ( (status = create_video_stream(&vcodec_ctx, oc, vcodec, opts, ctx)) ) {
    PERROR("create_video_stream('%s') fails: %s", vcodec->name, av_err2str(status));
    goto end;
  }


  /// Start server connection

  set_stream_state(ctx, ff_output_stream_connecting, 0, true);


  PDBG("C avio_open2('%s')", ctx->server);

  if ( (status = avio_open2(&oc->pb, ctx->server, AVIO_FLAG_WRITE, &oc->interrupt_callback, NULL)) < 0 ) {
    PCRITICAL("avio_open(%s) fails: %s", ctx->server, av_err2str(status));
    goto end;
  }

  PDBG("R avio_open('%s')", ctx->server);

  set_stream_state(ctx, ff_output_stream_established, 0, true);

  /* Write the stream header */
  PDBG("C avformat_write_header('%s')", ctx->server);

  if ( (status = avformat_write_header(oc, NULL)) < 0 ) {
    PERROR("avformat_write_header() fails: %s", av_err2str(status));
    goto end;
  }

  PDBG("R avformat_write_header('%s')", ctx->server);

  write_header_ok = true;

  ctx_lock(ctx);

  while ( status >= 0 ) {

    frm = NULL;

    while ( !ctx->interrupted && !(frm = ccfifo_ppop(&ctx->q)) ) {
      ctx_wait(ctx, -1);
    }

    if ( ctx->interrupted ) {
      status = AVERROR_EXIT;
      if ( frm ) {
        ccfifo_ppush(&ctx->p, frm);
      }
      break;
    }

    ctx_unlock(ctx);

    output_frame->pts = frm->pts;
    gotpkt = false;

    if ( (status = av_image_fill_arrays(input_frame->data, input_frame->linesize, frm->data, input_frame->format, cx, cy, 1)) <= 0 ) {
      PERROR("av_image_fill_arrays() fails: %s", av_err2str(status));
    }
    else if ( (status = sws_scale(sws, (const uint8_t * const*)input_frame->data, input_frame->linesize, 0, cy, output_frame->data, output_frame->linesize)) < 0 ) {
      PERROR("sws_scale() fails: %s", av_err2str(status));
    }
    else if ( (status = avcodec_encode_video2(vcodec_ctx, &pkt, output_frame, &gotpkt)) < 0 ) {
      PERROR("avcodec_encode_video2() fails: %s", av_err2str(status));
    }
    else if ( gotpkt ) {

      pkt.stream_index = 0;

      os = oc->streams[0];

      if ( vcodec_ctx->time_base.num != os->time_base.num || vcodec_ctx->time_base.den != os->time_base.den ) {
        av_packet_rescale_ts(&pkt, vcodec_ctx->time_base, os->time_base);
      }

    //  PDBG("C av_write_frame('%s')", ctx->server);
      if ( (status = av_write_frame(oc, &pkt)) < 0 ) {
        PERROR("av_write_frame() fails: status=%d %s", status, av_err2str(status));
      }
      else {
        ++ctx->stats.framesSent;
        ctx->stats.bytesSent += pkt.size;
      }

//      PDBG("R av_write_frame('%s')", ctx->server);

      av_packet_unref(&pkt);
    }

    ctx_lock(ctx);

    if ( frm ) {
      ccfifo_ppush(&ctx->p, frm);
    }
  }

  ctx_unlock(ctx);

end:

  PDBG("C set_output_stream_state(disconnecting, status=%d)", status);
  set_stream_state(ctx, ff_output_stream_disconnecting, status, true);

  if ( write_header_ok && !is_io_error(status) ) {
    int status2 = av_write_trailer(oc);
    if ( status2 ) {
      PERROR("av_write_trailer() fails: %s", av_err2str(status2));
      if ( status == 0 ) {
        status = status2;
      }
    }
  }

  if ( vcodec_ctx ) {
    if ( avcodec_is_open(vcodec_ctx) ) {
      avcodec_close(vcodec_ctx);
    }
    avcodec_free_context(&vcodec_ctx);
  }

  if ( oc ) {
    avio_closep(&oc->pb);
    avformat_free_context(oc);
  }

  if ( sws ) {
    sws_freeContext(sws);
  }

  av_dict_free(&opts);


  PDBG("LEAVE");

  return status;
}


static void * output_stream_thread(void * arg)
{
  struct ff_output_stream * ctx = arg;
  JNIEnv * env = NULL;

  int64_t tmo;


  int status = 0;

  PDBG("ENTER");

  java_attach_current_thread(&env);

  ctx_lock(ctx);

  while ( !ctx->interrupted && status >= 0 ) {

    ctx_unlock(ctx);

    set_stream_state(ctx, ff_output_stream_starting, 0, false);

    status = output_loop(ctx);
    PDBG("output_loop() finished with status=%d (%s): errno=%d (%s)", status, av_err2str(status), errno, strerror(errno));

    ctx_lock(ctx);

    if ( !ctx->interrupted && is_io_error(status) ) {

      //PDEBUG("Make a short delay");

      set_stream_state(ctx, ff_output_stream_paused, 0, false);

      tmo = ffmpeg_gettime_ms() + 2 * 1000;
      while ( !ctx->interrupted && ffmpeg_gettime_ms() < tmo ) {
        ctx_wait(ctx, 500);
      }

      //PDEBUG("Delay finished");
      status = 0;
    }
  }

  set_stream_state(ctx, ff_output_stream_idle, status, false);

  ctx_unlock(ctx);

  java_deatach_current_thread();

  PDBG("LEAVE: interrupted=%d", ctx->interrupted);
  return NULL;
}


ff_output_stream * create_output_stream(const create_output_stream_args * args)
{
  ff_output_stream * ctx = NULL;
  struct frm * frm;
  size_t frmsize;

  bool fok = false;


  if ( !args || args->cx < 2 || args->cy < 2 || !args->pxfmt || !args->server || !*args->server) {
    errno = EINVAL;
    goto end;
  }

  if ( !(ctx = av_mallocz(sizeof(*ctx))) ) {
    goto end;
  }

  if ( !(ccfifo_init(&ctx->p, OUTPUT_FIFO_SIZE, sizeof(struct frm*))) ) {
    goto end;
  }

  if ( !(ccfifo_init(&ctx->q, OUTPUT_FIFO_SIZE, sizeof(struct frm*))) ) {
    goto end;
  }


  ctx->server = av_strdup(args->server);

  if ( args->format && *args->format ) {
    ctx->format = av_strdup(args->format);
  }

  if ( args->codec && *args->codec ) {
    ctx->codec = av_strdup(args->codec);
  }

  if ( args->ffopts && *args->ffopts ) {
    ctx->ffopts = av_strdup(args->ffopts);
  }

  if ( args->events_callback ) {
    ctx->events_callback = *args->events_callback;
    ctx->cookie = args->cookie;
  }

  ctx->cx = args->cx;
  ctx->cy = args->cy;
  ctx->input_pix_fmt = args->pxfmt;
  ctx->quality = args->quality;
  ctx->gopsize = args->gopsize;
  ctx->bitrate = args->bitrate;

  ctx->state = ff_output_stream_idle;
  ctx->status = 0;

  frmsize = offsetof(struct frm, data) + FRAME_DATA_SIZE(ctx->cx, ctx->cy);
  for ( uint i = 0; i < OUTPUT_FIFO_SIZE; ++i ) {
    if ( !(frm = av_mallocz(frmsize)) ) {
      goto end;
    }
    ccfifo_ppush(&ctx->p, frm);
  }


  fok = true;

end:

  if ( !fok && ctx ) {

    while ( (frm = ccfifo_ppop(&ctx->p)) ) {
      av_free(frm);
    }

    ccfifo_cleanup(&ctx->p);
    ccfifo_cleanup(&ctx->q);

    av_free(ctx->server);
    av_free(ctx->format);
    av_free(ctx->codec);
    av_free(ctx->ffopts);

    av_free(ctx), ctx = NULL;
  }

  return ctx;
}


void destroy_output_stream(ff_output_stream * ctx)
{
  if ( ctx ) {

    struct frm * frm;

    if ( ctx->pid ) {
      pthread_join(ctx->pid, NULL);
    }

    while ( (frm = ccfifo_ppop(&ctx->p)) ) {
      av_free(frm);
    }

    while ( (frm = ccfifo_ppop(&ctx->q)) ) {
      av_free(frm);
    }

    ccfifo_cleanup(&ctx->p);
    ccfifo_cleanup(&ctx->q);

    av_free(ctx->server);
    av_free(ctx->format);
    av_free(ctx->codec);
    av_free(ctx->ffopts);
    av_free(ctx);
  }
}



bool start_output_stream(ff_output_stream * ctx)
{
  int status = -1;

  ctx_lock(ctx);

  if ( ctx->pid != 0 || ctx->state != ff_output_stream_idle ) {
    errno = EALREADY;
  }
  else {
    set_stream_state(ctx, ff_output_stream_starting, 0, false);

    if ( (status = pthread_create(&ctx->pid, NULL, output_stream_thread, ctx)) ) {
      set_stream_state(ctx, ff_output_stream_idle, errno = status, false);
    }
  }

  ctx_unlock(ctx);

  return status == 0;
}


void stop_output_stream(ff_output_stream * ctx)
{
  PDBG("ENTER");

  ctx_lock(ctx);

  ctx->interrupted = true;

  while ( ctx->state != ff_output_stream_idle ) {
    PDBG("WAIT STATE");
    ctx_signal(ctx);
    ctx_wait(ctx, -1);
  }

  ctx_unlock(ctx);

  PDBG("LEAVE");
}

ff_output_stream_state get_output_stream_state(const ff_output_stream * ctx)
{
  return ctx->state;
}

void * get_output_stream_cookie(const ff_output_stream * ctx)
{
  return ctx->cookie;
}

size_t get_output_frame_data_size(const ff_output_stream * ctx)
{
  return FRAME_DATA_SIZE(ctx->cx, ctx->cy);
}

struct frm * pop_output_frame(ff_output_stream * ctx)
{
  struct frm * frm;

  ctx_lock(ctx);

  ++ctx->stats.framesRead;

  frm = ccfifo_ppop(&ctx->p);

  ctx_unlock(ctx);
  return frm;
}

void push_output_frame(ff_output_stream * ctx, struct frm * frm)
{
  ctx_lock(ctx);

  if ( !ctx->firstpts ) {
    ctx->firstpts = frm->pts;
  }

  frm->pts -= ctx->firstpts;
  ccfifo_ppush(&ctx->q, frm);

  ctx_signal(ctx);
  ctx_unlock(ctx);
}


const struct output_stream_stats * get_output_stream_stats(ff_output_stream * ctx)
{
  int64_t t = ffmpeg_gettime_ms();

  ctx_lock(ctx);

  ctx->stats.bytesRead = ctx->stats.framesRead * FRAME_DATA_SIZE(ctx->cx, ctx->cy);

  if ( t > ctx->stats.timer ) {
    ctx->stats.inputFps = (ctx->stats.framesRead - ctx->stats.inputFpsMark) * 1000LL / (t - ctx->stats.timer);
    ctx->stats.inputBitrate = (ctx->stats.bytesRead - ctx->stats.inputBitrateMark) * 8000LL / (t - ctx->stats.timer);
    ctx->stats.outputFps = (ctx->stats.framesSent - ctx->stats.outputFpsMark) * 1000LL / (t - ctx->stats.timer);
    ctx->stats.outputBitrate = (ctx->stats.bytesSent - ctx->stats.outputBitrateMark) * 8000LL / (t - ctx->stats.timer);
  }

  ctx->stats.timer = t;
  ctx->stats.inputFpsMark = ctx->stats.framesRead;
  ctx->stats.inputBitrateMark = ctx->stats.bytesRead;
  ctx->stats.outputFpsMark = ctx->stats.framesSent;
  ctx->stats.outputBitrateMark = ctx->stats.bytesSent;

  ctx_unlock(ctx);

  return &ctx->stats;
}
