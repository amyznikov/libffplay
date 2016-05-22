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
#include "opensless-audio.h"
#include "ffplay-java-api.h"
#include "debug.h"
#include <endian.h>

#define AUDIO_CAPTURE_BUFFERS     4
#define AUDIO_SAMPLE_SIZE         2

//#define AUDIO_SAMPLE_RATE         16000    /* [Hz] */
//#define AUDIO_SAMPLE_RATE         8000    /* [Hz] */
#define AUDIO_SAMPLE_FMT          AV_SAMPLE_FMT_S16P

#define VIDEO_POLL_SIZE           3
#define AUDIO_POLL_SIZE           150
#define OUTPUT_FIFO_SIZE          (VIDEO_POLL_SIZE+AUDIO_POLL_SIZE)

#define VIDEO_CODEC_TIME_BASE     (AVRational){1,1000}

struct ff_output_stream {

  char * server;
  char * format;
  char * ffopts;

  char * video_codec;
  int cx, cy;
  enum AVPixelFormat input_pixfmt;
  int vquality;
  int gopsize;

  char * audio_codec;
  int audio_sample_rate;
  size_t audio_samples_per_buffer;
  size_t audio_bytes_per_buffer;
  int aquality;


  ff_output_stream_event_callback events_callback;
  void * cookie;

  opensless_audio_capture * capdev;
  int16_t * capbufs;

  pthread_t pid;
  pthread_wait_t lock;

  int64_t firstpts;
  int64_t atime;
  struct ccfifo ap, vp, q;

  ff_output_stream_state state;
  int status, reason;
  bool interrupted:1, have_audio:1;

  struct output_stream_stats stats;
};


// defined below
static void audio_capture_callback(void * cookie, void * bfr, size_t size);
static bool start_audio_capture(ff_output_stream * ff);
static void stop_audio_capture(ff_output_stream * ff);


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

static int output_stream_interrupt_callback(void * arg) {
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

static bool is_ioerror(int status)
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



static int create_video_codec(AVCodecContext ** cctx, ff_output_stream * ff, const AVDictionary * opts)
{
  const char * codec_name = NULL;
  const AVCodec * codec = NULL;

  enum AVPixelFormat cpixfmt;
  int bitrate, gop_size, quality;
  int qmin = 1, qmax = 31, q;


  AVDictionary * codec_opts = NULL;
  AVDictionaryEntry * e = NULL;

  int status = 0;

  if ( (e = av_dict_get(opts, "-c:v", NULL, 0)) ) {
    codec_name = e->value;
  }
  else if ( ff->video_codec && *ff->video_codec ) {
    codec_name = ff->video_codec;
  }
  else {
    codec_name = "libx264";
  }

  if ( !(codec = avcodec_find_encoder_by_name(codec_name)) ) {
    status = AVERROR_ENCODER_NOT_FOUND;
    PERROR("avcodec_find_encoder_by_name('%s') fails: %s", codec_name, av_err2str(status));
    goto end;
  }


  if ( !(e = av_dict_get(opts, "-pix_fmt", NULL, 0)) ) {
    cpixfmt = codec->pix_fmts ? codec->pix_fmts[0] : ff->input_pixfmt;
    PDBG("PIXFMT %d SELECTED", cpixfmt);
  }
  else if ( (cpixfmt = av_get_pix_fmt(e->value)) == AV_PIX_FMT_NONE ) {
    PDBG("Bad pixel format specified: %s", e->value);
    status = AVERROR(EINVAL);
    goto end;
  }


  if ( (e = av_dict_get(opts, "-b:v", NULL, 0)) ) {
    if ( (bitrate = (int) av_strtod(e->value, NULL)) < 1000 ) {
      PDBG("Bad output bitrate specified: %s", e->value);
      status = AVERROR(EINVAL);
      goto end;
    }
  }
  else {
    bitrate = 128000;
  }


  if ( (e = av_dict_get(opts, "-g", NULL, 0)) ) {
    if ( sscanf(e->value, "%d", &gop_size) != 1 || gop_size < 1 ) {
      PDBG("Bad output gop size specified: %s", e->value);
      status = AVERROR(EINVAL);
      goto end;
    }
  }
  else if ( ff->gopsize > 0 ) {
    gop_size = ff->gopsize;
  }
  else {
    gop_size = 25;
  }


  if ( strcmp(codec->name, "libx264") == 0 ) {
    // Set some defaults, may be overriden by ffmpeg_filter_codec_opts()
    // See http://www.chaneru.com/Roku/HLS/X264_Settings.htm
    av_dict_set(&codec_opts, "preset", "veryfast", 0);
    av_dict_set(&codec_opts, "tune", "zerolatency", 0);
    av_dict_set(&codec_opts, "rc-lookahead", "3", 0);
    av_dict_set(&codec_opts, "profile", "Main", 0);
  }

  if ( (status = ffmpeg_filter_codec_opts(opts, codec, AV_OPT_FLAG_ENCODING_PARAM, &codec_opts)) ) {
    PERROR("ffmpeg_filter_codec_opts('%s') fails", codec->name);
    goto end;
  }

  quality = ff->vquality > 0 && ff->vquality <= 100 ? ff->vquality : 50;


  if ( strcmp(codec->name, "libx264") == 0 ) {
    if ( !av_dict_get(opts, "-crf", NULL, 0) && !av_dict_get(opts, "-qp", NULL, 0) ) {
      if ( (q = (100 - quality) * 50 / 100 + 10) > 51 ) {
        q = 51;
      }
      av_dict_set_int(&codec_opts, "qp", q, 0);
    }
  }
  else {

    if ( (q = (100 - quality) * 32 / 100 + 1) > 31 ) {
      q = 31;
    }
    qmin = qmax = q;

    if ( !av_dict_get(opts, "-qmin", NULL, 0) ) {
      av_dict_set_int(&codec_opts, "qmin", q, 0);
    }

    if ( !av_dict_get(opts, "-qmax", NULL, 0) ) {
      av_dict_set_int(&codec_opts, "qmax", q, 0);
    }

    if ( !av_dict_get(opts, "-q:v", NULL, 0) ) {
      av_dict_set_int(&codec_opts, "q", q, 0);
    }
  }

  if ( !(*cctx = avcodec_alloc_context3(codec)) ) {
    PDBG("avcodec_alloc_context3('%s') fails", codec->name);
    status = AVERROR(ENOMEM);
    goto end;
  }

  (*cctx)->time_base = VIDEO_CODEC_TIME_BASE;
  (*cctx)->pix_fmt = cpixfmt;
  (*cctx)->width = ff->cx;
  (*cctx)->height = ff->cy;
  (*cctx)->bit_rate = 0;// bitrate;
  (*cctx)->gop_size = gop_size;
  (*cctx)->me_range = 1;
  (*cctx)->flags |= CODEC_FLAG_GLOBAL_HEADER;
  (*cctx)->qmin = qmin;
  (*cctx)->qmax = qmax;

  if ( (status = avcodec_open2(*cctx, codec, &codec_opts)) ) {
    PDBG("avcodec_open2('%s') fails: %s", codec->name, av_err2str(status));
    goto end;
  }

end:

  av_dict_free(&codec_opts);

  return status;
}

static int create_audio_codec(AVCodecContext ** cctx, ff_output_stream * ff, const AVDictionary * opts)
{
  const char * codec_name = NULL;
  const AVCodec * codec = NULL;

  AVDictionary * codec_opts = NULL;
  AVDictionaryEntry * e = NULL;
  int q;
  int bitrate = 0;

  int status = 0;


  if ( (e = av_dict_get(opts, "-c:a", NULL, 0)) ) {
    codec_name = e->value;
  }
  else if ( ff->audio_codec && *ff->audio_codec ) {
    codec_name = ff->audio_codec;
  }
  else {
    codec_name = "libmp3lame";
  }

  if ( !(codec = avcodec_find_encoder_by_name(codec_name)) ) {
    status = AVERROR_ENCODER_NOT_FOUND;
    PERROR("avcodec_find_encoder_by_name('%s') fails: %s", codec_name, av_err2str(status));
    goto end;
  }


  if ( (status = ffmpeg_filter_codec_opts(opts, codec, AV_OPT_FLAG_ENCODING_PARAM, &codec_opts)) ) {
    PERROR("ffmpeg_filter_codec_opts('%s') fails", codec->name);
    goto end;
  }

  if ( strcmp(codec->name, "libmp3lame") == 0 ) {

    ff->audio_sample_rate = 16000;

    // see https://trac.ffmpeg.org/wiki/Encode/MP3
    if ( !av_dict_get(codec_opts, "q", NULL, 0) ) {

      if ( ff->aquality <= 0 || ff->aquality > 100 ) {
        q = 9;
      }
      else if ( (q = (100 - ff->aquality) / 10) > 9 ) {
        q = 9;
      }

      av_dict_set_int(&codec_opts, "q", q, 0);
    }
  }
  else if ( strcmp(codec->name, "libopencore_amrnb") == 0 ) {
    ff->audio_sample_rate = 8000;
    bitrate = 4750;
  }
  else {
    ff->audio_sample_rate = 8000;
  }


  if ( !(*cctx = avcodec_alloc_context3(codec)) ) {
    PDBG("avcodec_alloc_context3('%s') fails", codec->name);
    status = AVERROR(ENOMEM);
    goto end;
  }

  ///

  (*cctx)->time_base = (AVRational ) { 1, ff->audio_sample_rate };
  (*cctx)->sample_rate = ff->audio_sample_rate;
  (*cctx)->sample_fmt = AUDIO_SAMPLE_FMT;
  (*cctx)->channels = 1;
  (*cctx)->channel_layout = AV_CH_LAYOUT_MONO;
  (*cctx)->bit_rate = bitrate;


  if ( (status = avcodec_open2(*cctx, codec, &codec_opts)) ) {
    PDBG("avcodec_open2('%s') fails: %s", codec->name, av_err2str(status));
    goto end;
  }

  if ( (*cctx)->frame_size ) {
    ff->audio_samples_per_buffer = (*cctx)->frame_size;
    PDBG("FRAME_SIZE: AUDIO_SAMPLES_PER_BUFFER=%zu", ff->audio_samples_per_buffer);
  }
  else {
    ff->audio_samples_per_buffer = 320;
    PDBG("MANUAL: AUDIO_SAMPLES_PER_BUFFER=%zu", ff->audio_samples_per_buffer);
  }

  ff->audio_bytes_per_buffer = ff->audio_samples_per_buffer * AUDIO_SAMPLE_SIZE; // fixme here

end: ;

  return status;
}


static int add_stream(AVFormatContext * oc, AVCodecContext * codec)
{
  AVStream * os = NULL;

  int status = 0;

  if ( !(os = avformat_new_stream(oc, codec->codec)) ) {
    status = AVERROR(ENOMEM);
    goto end;
  }

  if ( (status = avcodec_parameters_from_context(os->codecpar, codec)) < 0 ) {
    PDBG("avcodec_parameters_from_context('%s') fails: %s", codec->codec->name, av_err2str(status));
    goto end;
  }

  os->time_base = codec->time_base;

end:;

  return status;
}



static bool start_audio_capture(ff_output_stream * ff)
{
  int status = 0;

  if ( (status = opensless_audio_initialize()) != SL_RESULT_SUCCESS ) {
    PERROR("opensless_audio_initialize() fails: status=0x%0X", status);
    goto end;
  }

  if ( !(ff->capbufs = av_malloc(AUDIO_CAPTURE_BUFFERS * ff->audio_bytes_per_buffer)) ) {
    PERROR("av_malloc(AUDIO_CAPTURE_BUFFERS) fails");
    status = SL_RESULT_MEMORY_FAILURE;
    goto end;
  }


  /* create capture device */
  status = opensless_audio_capture_create(&ff->capdev, ff, audio_capture_callback, AUDIO_CAPTURE_BUFFERS, ff->audio_sample_rate);
  if ( status ) {
    PERROR("audio_capture_create() fails: status=0x%0X", status);
    goto end;
  }

  /* Enqueue capture bufers */
  for ( int i = 0; i < AUDIO_CAPTURE_BUFFERS; ++i ) {
    int16_t * capbuf = ff->capbufs + i * ff->audio_samples_per_buffer;
    if ( (status = opensless_audio_capture_enqueue(ff->capdev, capbuf, ff->audio_bytes_per_buffer)) != 0 ) {
      PERROR("audio_capture_enqueue() fails: status=0x%0X", status);
      goto end;
    }
  }

  /* start audio capture device */
  if ( (status = opensless_audio_capture_start(ff->capdev)) != 0 ) {
    PERROR("audio_capture_start() fails: status=0x%0X", status);
    goto end;
  }

end:

  if ( status ) {

    av_free(ff->capbufs);
    opensless_audio_capture_destroy(&ff->capdev);

    opensless_audio_shutdown();
  }

  return status == 0;
}

static void stop_audio_capture(ff_output_stream * ff)
{
  if ( ff->capdev ) {
    opensless_audio_capture_stop(ff->capdev);
    opensless_audio_capture_destroy(&ff->capdev);
    opensless_audio_shutdown();
  }

  av_free(ff->capbufs);
  ff->capbufs = NULL;
}


static int create_frame_poll(struct ccfifo * fifo, size_t count, size_t datasize)
{
  struct frm * frm;
  size_t frmsize;
  int status = 0;

  if ( !ccfifo_init(fifo, count, sizeof(struct frm*)) ) {
    PERROR("ccfifo_init() fails");
    status = AVERROR(ENOMEM);
    goto end;
  }

  frmsize = offsetof(struct frm, data) + datasize;
  for ( uint i = 0; i < count; ++i ) {
    if ( !(frm = av_mallocz(frmsize)) ) {
      PERROR("av_mallocz( frame: size=%zu) fails", frmsize);
      status = AVERROR(ENOMEM);
      goto end;
    }
    ccfifo_ppush(fifo, frm);
  }

end: ;

  if ( status ) {
    while ( (frm = ccfifo_ppop(fifo)) ) {
      av_free(frm);
    }
    ccfifo_cleanup(fifo);
  }

  return status;
}


static int output_loop(struct ff_output_stream * ff)
{
  AVDictionary * opts = NULL;
  AVDictionaryEntry * e = NULL;

  AVCodecContext * vcodec = NULL;
  AVFrame * output_video_frame = NULL;
  AVFrame * input_video_frame = NULL;
  struct SwsContext * sws = NULL;
  int cx, cy;


  AVCodecContext * acodec = NULL;
  AVFrame * output_audio_frame = NULL;


  const char * format_name = ff->format ? ff->format : "matroska";
  AVOutputFormat * oformat = NULL;
  AVFormatContext * oc = NULL;
  bool write_header_ok = false;


  struct frm * frm;
  AVPacket pkt;
  int pkt_size;
  int gotpkt;


  AVCodecContext * codec = NULL;
  int stidx;



  int status;


  PDBG("ENTER");





  cx = ff->cx;
  cy = ff->cy;


  av_init_packet(&pkt);
  pkt.data = NULL, pkt.size = 0;


  /// Parse command line
  if ( (status = av_dict_parse_string(&opts, ff->ffopts, " \t", " \t", 0)) ) {
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


  /// Open codecs

  if ( !(ccfifo_init(&ff->q, OUTPUT_FIFO_SIZE, sizeof(struct frm*))) ) {
    PERROR("ccfifo_init(frame queue) fails");
    status = AVERROR(ENOMEM);
    goto end;
  }


  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  if ( ff->video_codec && *ff->video_codec ) {

    if ( (status = create_video_codec(&vcodec, ff, opts)) ) {
      PERROR("create_video_codec() fails: %s", av_err2str(status));
      goto end;
    }

    PDBG("VIDEO: %s %s %dx%d", vcodec->codec->name, av_get_pix_fmt_name(ff->input_pixfmt), ff->cx, ff->cy );

    if ( (status = create_frame_poll(&ff->vp, VIDEO_POLL_SIZE, FRAME_DATA_SIZE(ff->cx, ff->cy))) ) {
      PERROR("create_frame_poll(video) fails: %s", av_err2str(status));
      goto end;
    }

    /// Alloc frame buffers
    if ( (status = ffmpeg_create_video_frame(&input_video_frame, ff->input_pixfmt, ff->cx, ff->cy, 0)) ) {
      PERROR("ffmpeg_create_video_frame(input_frame) fails: %s", av_err2str(status));
      goto end;
    }

    PDBG("create output_video_frame: %s %dx%d", av_get_pix_fmt_name(vcodec->pix_fmt), vcodec->width, vcodec->height );
    if ( (status = ffmpeg_create_video_frame(&output_video_frame, vcodec->pix_fmt, vcodec->width, vcodec->height, 32)) ) {
      PERROR("ffmpeg_create_video_frame(output_frame) fails: %s", av_err2str(status));
      goto end;
    }

    if ( !(sws = sws_getContext(cx, cy, ff->input_pixfmt, vcodec->width, vcodec->height, vcodec->pix_fmt, SWS_FAST_BILINEAR, NULL, NULL, NULL)) ) {
      PERROR("sws_getContext() fails");
      goto end;
    }
  }


  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  if ( ff->audio_codec && *ff->audio_codec ) {

    if ( (status = create_audio_codec(&acodec, ff, opts)) ) {
      PERROR("create_audio_codec(%s) fails: %s", ff->audio_codec, av_err2str(status));
      goto end;
    }

    PDBG("AUDIO: %s %s %d Hz %d samples", acodec->codec->name, av_get_sample_fmt_name(acodec->sample_fmt), acodec->sample_rate, ff->audio_samples_per_buffer );

    if ( (status = create_frame_poll(&ff->ap, AUDIO_POLL_SIZE, ff->audio_bytes_per_buffer)) ) {
      PERROR("create_frame_poll(audio) fails: %s", av_err2str(status));
      goto end;
    }

    if ( (status = ffmpeg_create_audio_frame(&output_audio_frame, acodec->sample_fmt, acodec->sample_rate, ff->audio_samples_per_buffer, 1, AV_CH_LAYOUT_MONO)) ) {
      PERROR("ffmpeg_create_audio_frame() fails: %s", av_err2str(status));
      goto end;
    }

    if ( !(ff->have_audio = start_audio_capture(ff)) ) {
      PERROR("start_audio_capture() fails. Audio disabled");
    }
  }

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////





  /// Alloc output context

  if ( (status = avformat_alloc_output_context2(&oc, oformat, NULL, ff->server)) < 0 ) {
    PERROR("avformat_alloc_output_context2() fails: %s", av_err2str(status));
    goto end;
  }

  oc->interrupt_callback.callback = output_stream_interrupt_callback;
  oc->interrupt_callback.opaque = ff;


  /// Create video stream
  if ( (status = add_stream(oc, vcodec)) ) {
    PERROR("create_video_stream('%s') fails: %s", vcodec->codec->name, av_err2str(status));
    goto end;
  }

  /// Create audio stream
  if ( ff->have_audio && (status = add_stream(oc, acodec)) ) {
    PERROR("create_audio_stream('%s') fails: %s", acodec->codec->name, av_err2str(status));
    goto end;
  }


  /// Start server connection
  set_stream_state(ff, ff_output_stream_connecting, 0, true);


  PDBG("C avio_open2('%s')", ff->server);

  if ( (status = avio_open2(&oc->pb, ff->server, AVIO_FLAG_WRITE, &oc->interrupt_callback, NULL)) < 0 ) {
    PCRITICAL("avio_open(%s) fails: %s", ff->server, av_err2str(status));
    goto end;
  }

  set_stream_state(ff, ff_output_stream_established, 0, true);

  /* Write the stream header */
  PDBG("C avformat_write_header('%s')", ff->server);

  if ( (status = avformat_write_header(oc, NULL)) < 0 ) {
    PERROR("avformat_write_header() fails: %s", av_err2str(status));
    goto end;
  }

  PDBG("R avformat_write_header('%s')", ff->server);

  write_header_ok = true;

  ctx_lock(ff);

  while ( status >= 0 ) {

    frm = NULL;
    stidx = -1;

    while ( !ff->interrupted && !(frm = ccfifo_ppop(&ff->q)) ) {
      ctx_wait(ff, -1);
    }

    if ( ff->interrupted ) {
      status = AVERROR_EXIT;
      if ( frm ) {
        switch ( frm->type ) {
          case frm_type_audio :
            ccfifo_ppush(&ff->ap, frm);
          break;
          case frm_type_video :
            ccfifo_ppush(&ff->vp, frm);
          break;
        }
      }
      break;
    }

    ctx_unlock(ff);

    gotpkt = false;

    switch ( frm->type ) {

      case frm_type_video: {
        stidx = 0;
        codec = vcodec;
        output_video_frame->pts = frm->pts;
        if ( (status = av_image_fill_arrays(input_video_frame->data, input_video_frame->linesize, frm->data, input_video_frame->format, cx, cy, 1)) <= 0 ) {
          PERROR("av_image_fill_arrays() fails: %s", av_err2str(status));
        }
        else if ( (status = sws_scale(sws, (const uint8_t * const*)input_video_frame->data, input_video_frame->linesize, 0, cy, output_video_frame->data, output_video_frame->linesize)) < 0 ) {
          PERROR("sws_scale() fails: %s", av_err2str(status));
        }
        else if ( (status = avcodec_encode_video2(vcodec, &pkt, output_video_frame, &gotpkt)) < 0 ) {
          PERROR("avcodec_encode_video2() fails: %s", av_err2str(status));
        }
      }
      break;

      case frm_type_audio: {
        stidx = 1;
        codec = acodec;
        output_audio_frame->pts = frm->pts;
        output_audio_frame->nb_samples = ff->audio_samples_per_buffer;
        if ( (status = avcodec_fill_audio_frame(output_audio_frame, 1, AUDIO_SAMPLE_FMT, frm->data, frm->size, 0)) < 0 ) {
          PERROR("avcodec_fill_audio_frame() fails: %s", av_err2str(status));
        }
        else if ( (status = avcodec_encode_audio2(acodec, &pkt, output_audio_frame, &gotpkt)) < 0 ) {
          PERROR("avcodec_encode_audio2() fails: %s", av_err2str(status));
        }
      }
      break;
    }


    if ( gotpkt ) {

      const AVStream * os = oc->streams[pkt.stream_index = stidx];

      if ( codec->time_base.num != os->time_base.num || codec->time_base.den != os->time_base.den ) {
        av_packet_rescale_ts(&pkt, codec->time_base, os->time_base);
      }

//      {
//        int64_t upts = av_rescale_ts(pkt.pts, os->time_base, (AVRational){1, 1000});
//        int64_t udts = av_rescale_ts(pkt.dts, os->time_base, (AVRational){1, 1000});
//        PDBG("av_write_frame(): st=%d pts=%s dts=%s ctb=%s stb=%s upts=%s udts=%s size=%d",stidx,
//            av_ts2str(pkt.pts), av_ts2str(pkt.dts),
//            av_tb2str(codec->time_base), av_tb2str(os->time_base),
//            av_ts2str(upts), av_ts2str(udts),
//            pkt.size);
//      }


      // av_interleaved_write_frame() will destroy pkt
      pkt_size = pkt.size;

      if ( oc->nb_streams > 1 ) {
        if ( (status = av_interleaved_write_frame(oc, &pkt)) < 0 ) {
          PERROR("av_interleaved_write_frame(st=%d) fails: status=%d %s", stidx, status, av_err2str(status));
        }
      }
      else if ( (status = av_write_frame(oc, &pkt)) < 0 ) {
        PERROR("av_write_frame() fails: status=%d %s", status, av_err2str(status));
      }

      if ( status >= 0 ) {
        if ( stidx == 0 ) {
          ++ff->stats.framesSent;
        }
        ff->stats.bytesSent += pkt_size;
      }

      av_packet_unref(&pkt);
    }

    ctx_lock(ff);

    if ( frm ) {
      switch ( frm->type ) {
        case frm_type_audio :
          ccfifo_ppush(&ff->ap, frm);
        break;
        case frm_type_video :
          ccfifo_ppush(&ff->vp, frm);
        break;
      }
    }
  }

  ctx_unlock(ff);

end:

  PDBG("C set_output_stream_state(disconnecting, status=%d)", status);
  set_stream_state(ff, ff_output_stream_disconnecting, status, true);

  if ( ff->have_audio ) {
    stop_audio_capture(ff);
  }


  if ( write_header_ok && !is_ioerror(status) ) {
    int status2 = av_write_trailer(oc);
    if ( status2 ) {
      PERROR("av_write_trailer() fails: %s", av_err2str(status2));
      if ( status == 0 ) {
        status = status2;
      }
    }
  }

  ctx_lock(ff);

  av_frame_free(&output_audio_frame);
  av_frame_free(&output_video_frame);
  av_frame_free(&input_video_frame);

  if ( sws ) {
    sws_freeContext(sws);
  }

  if ( vcodec ) {
    if ( avcodec_is_open(vcodec) ) {
      avcodec_close(vcodec);
    }
    avcodec_free_context(&vcodec);
  }

  if ( acodec ) {
    if ( avcodec_is_open(acodec) ) {
      avcodec_close(acodec);
    }
    avcodec_free_context(&acodec);
  }

  if ( oc ) {
    avio_closep(&oc->pb);
    avformat_free_context(oc);
  }

  av_dict_free(&opts);

  while ( (frm = ccfifo_ppop(&ff->q)) ) {
    av_free(frm);
  }
  while ( (frm = ccfifo_ppop(&ff->ap)) ) {
    av_free(frm);
  }
  while ( (frm = ccfifo_ppop(&ff->vp)) ) {
    av_free(frm);
  }

  ccfifo_cleanup(&ff->ap);
  ccfifo_cleanup(&ff->vp);
  ccfifo_cleanup(&ff->q);

  ctx_unlock(ff);

  PDBG("LEAVE");

  return status;
}


static void * output_stream_thread(void * arg)
{
  struct ff_output_stream * ff = arg;
  JNIEnv * env = NULL;
  int64_t tmo;


  int status = 0;

  PDBG("ENTER");

  java_attach_current_thread(&env);

  ctx_lock(ff);

  while ( !ff->interrupted && status >= 0 ) {

    ff->firstpts = 0;
    ff->atime = 0;
    memset(&ff->stats, 0, sizeof(ff->stats));

    ctx_unlock(ff);

    set_stream_state(ff, ff_output_stream_starting, 0, false);

    status = output_loop(ff);
    PDBG("output_loop() finished with status=%d (%s): errno=%d (%s)", status, av_err2str(status), errno, strerror(errno));

    ctx_lock(ff);

    if ( !ff->interrupted && is_ioerror(status) ) {

      //PDEBUG("Make a short delay");

      set_stream_state(ff, ff_output_stream_paused, 0, false);

      tmo = ffmpeg_gettime_ms() + 2 * 1000;
      while ( !ff->interrupted && ffmpeg_gettime_ms() < tmo ) {
        ctx_wait(ff, 500);
      }

      //PDEBUG("Delay finished");
      status = 0;
    }
  }


  set_stream_state(ff, ff_output_stream_idle, status, false);

  ctx_unlock(ff);

  java_deatach_current_thread();

  PDBG("LEAVE: interrupted=%d", ff->interrupted);
  return NULL;
}


ff_output_stream * create_output_stream(const create_output_stream_args * args)
{
  ff_output_stream * ff = NULL;

  bool fok = false;


  if ( !args || args->cx < 2 || args->cy < 2 || !args->pxfmt || !args->server || !*args->server) {
    errno = EINVAL;
    goto end;
  }

  if ( !(ff = av_mallocz(sizeof(*ff))) ) {
    goto end;
  }



  ff->server = av_strdup(args->server);

  if ( args->format && *args->format ) {
    ff->format = av_strdup(args->format);
  }

  if ( args->ffopts && *args->ffopts ) {
    ff->ffopts = av_strdup(args->ffopts);
  }

  if ( args->cvcodec && *args->cvcodec ) {
    ff->video_codec = av_strdup(args->cvcodec);
  }

  if ( args->cacodec && *args->cacodec ) {
    //ff->audio_codec = av_strdup(args->cacodec);
    ff->audio_codec = av_strdup("libopencore_amrnb");
  }

  if ( args->events_callback ) {
    ff->events_callback = *args->events_callback;
    ff->cookie = args->cookie;
  }

  ff->cx = args->cx;
  ff->cy = args->cy;
  ff->input_pixfmt = args->pxfmt;
  ff->vquality = args->cvquality;
  ff->aquality = args->caquality;
  ff->gopsize = args->gopsize;

  ff->state = ff_output_stream_idle;
  ff->status = 0;






  fok = true;

end:

  if ( !fok && ff ) {


    av_free(ff->server);
    av_free(ff->format);
    av_free(ff->video_codec);
    av_free(ff->ffopts);

    av_free(ff), ff = NULL;
  }

  return ff;
}


void destroy_output_stream(ff_output_stream * ff)
{
  if ( ff ) {

    if ( ff->pid ) {
      pthread_join(ff->pid, NULL);
    }

    av_free(ff->server);
    av_free(ff->format);
    av_free(ff->video_codec);
    av_free(ff->ffopts);
    av_free(ff);
  }
}


bool start_output_stream(ff_output_stream * ff)
{
  int status = -1;

  ctx_lock(ff);

  if ( ff->pid != 0 || ff->state != ff_output_stream_idle ) {
    errno = EALREADY;
  }
  else {
    set_stream_state(ff, ff_output_stream_starting, 0, false);

    if ( (status = pthread_create(&ff->pid, NULL, output_stream_thread, ff)) ) {
      set_stream_state(ff, ff_output_stream_idle, errno = status, false);
    }
  }

  ctx_unlock(ff);

  return status == 0;
}


void stop_output_stream(ff_output_stream * ff)
{
  PDBG("ENTER");

  ctx_lock(ff);

  ff->interrupted = true;

  while ( ff->state != ff_output_stream_idle ) {
    PDBG("WAIT STATE");
    ctx_signal(ff);
    ctx_wait(ff, -1);
  }

  ctx_unlock(ff);

  PDBG("LEAVE");
}




ff_output_stream_state get_output_stream_state(const ff_output_stream * ctx)
{
  return ctx->state;
}

void * get_output_stream_cookie(const ff_output_stream * ff)
{
  return ff->cookie;
}

size_t get_video_frame_data_size(const ff_output_stream * ff)
{
  return FRAME_DATA_SIZE(ff->cx, ff->cy);
}

struct frm * pop_video_frame(ff_output_stream * ff)
{
  struct frm * frm = NULL;

  ctx_lock(ff);

  ++ff->stats.framesRead;

  if ( ff->state == ff_output_stream_established ) {
    frm = ccfifo_ppop(&ff->vp);
  }

  ctx_unlock(ff);

  return frm;
}

void push_video_frame(ff_output_stream * ff, struct frm * frm)
{
  frm->type = frm_type_video;
  frm->pts = ffmpeg_gettime_ms();

  ctx_lock(ff);

  if ( !ff->firstpts ) {
    ff->firstpts = frm->pts;
  }

  frm->pts -= ff->firstpts;
  frm->size = FRAME_DATA_SIZE(ff->cx, ff->cy);
  ccfifo_ppush(&ff->q, frm);

  ctx_signal(ff);
  ctx_unlock(ff);
}

static void audio_capture_callback(void * cookie, void * bfr, size_t size)
{
  ff_output_stream * ff = cookie;
  struct frm * frm = NULL;

  ctx_lock(ff);

  if ( !ff->interrupted ) {

    ff->atime += ff->audio_samples_per_buffer;
    if ( !ff->firstpts ) {
      ff->firstpts = ffmpeg_gettime_ms();
    }

    if ( ff->state == ff_output_stream_established && (frm = ccfifo_ppop(&ff->ap)) ) {

      frm->type = frm_type_audio;
      frm->pts = ff->atime;
      memcpy(frm->data, bfr, frm->size = size);
      ccfifo_ppush(&ff->q, frm);
      ctx_signal(ff);
    }

    if ( opensless_audio_capture_enqueue(ff->capdev, bfr, ff->audio_bytes_per_buffer) != 0 ) {
      PERROR("BUG BUG BUG: audio_capture_enqueue() fails");
    }
  }
  ctx_unlock(ff);
}



const struct output_stream_stats * get_output_stream_stats(ff_output_stream * ff)
{
  int64_t t = ffmpeg_gettime_ms();

  ctx_lock(ff);

  ff->stats.bytesRead = ff->stats.framesRead * FRAME_DATA_SIZE(ff->cx, ff->cy);

  if ( t > ff->stats.timer ) {
    ff->stats.inputFps = (ff->stats.framesRead - ff->stats.inputFpsMark) * 1000LL / (t - ff->stats.timer);
    ff->stats.inputBitrate = (ff->stats.bytesRead - ff->stats.inputBitrateMark) * 8000LL / (t - ff->stats.timer);
    ff->stats.outputFps = (ff->stats.framesSent - ff->stats.outputFpsMark) * 1000LL / (t - ff->stats.timer);
    ff->stats.outputBitrate = (ff->stats.bytesSent - ff->stats.outputBitrateMark) * 8000LL / (t - ff->stats.timer);
  }

  ff->stats.timer = t;
  ff->stats.inputFpsMark = ff->stats.framesRead;
  ff->stats.inputBitrateMark = ff->stats.bytesRead;
  ff->stats.outputFpsMark = ff->stats.framesSent;
  ff->stats.outputBitrateMark = ff->stats.bytesSent;

  ctx_unlock(ff);

  return &ff->stats;
}








////////////

