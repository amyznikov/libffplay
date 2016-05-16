/*
 * ffmpeg.c
 *
 *  Created on: Jul 25, 2014
 *      Author: amyznikov
 *
 *  References:
 *
 *   ffmpeg docs:
 *     http://ffmpeg.org/ffmpeg-filters.html
 *     https://ffmpeg.org/ffmpeg-protocols.html
 *
 *   FFmpeg and H.264 Encoding Guide - H264 Rate control modes
 *     https://trac.ffmpeg.org/wiki/Encode/H.264
 *
 *   H.264 encoding guide
 *     http://www.avidemux.org/admWiki/doku.php?id=tutorial:h.264
 *
 *   X264 Settings Guide
 *     http://mewiki.project357.com/wiki/X264_Settings
 *     http://www.chaneru.com/Roku/HLS/X264_Settings.htm
 *
 *
 *   VBV Encoding
 *     http://mewiki.project357.com/wiki/X264_Encoding_Suggestions#VBV_Encoding
 *
 *
 *   CRF Guide
 *     http://slhck.info/articles/crf
 *
 *   Explanation of x264 tune
 *     http://superuser.com/questions/564402/explanation-of-x264-tune
 *
 *   Forcing keyframes with an ffmpeg expression
 *     http://paulherron.com/blog/forcing_keyframes_with_ffmpeg%20copy
 *
 *   Limit Bitrate
 *     http://veetle.com/index.php/article/view/bitrateLimit
 */


#include "ffmpeg.h"
#include "debug.h"
#include <time.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int64_t ffmpeg_gettime_us(void)
{
  struct timespec tm;
  clock_gettime(CLOCK_MONOTONIC, &tm);
  return ((int64_t)tm.tv_sec * 1000000 + (int64_t)tm.tv_nsec / 1000);
}

int64_t ffmpeg_gettime_ms(void)
{
  struct timespec tm;
  clock_gettime(CLOCK_MONOTONIC, &tm);
  return ((int64_t) tm.tv_sec * 1000 + (int64_t) tm.tv_nsec / 1000000);
}


void ffmpeg_usleep( int64_t usec )
{
  struct timespec rqtp;
  rqtp.tv_sec = usec / 1000000;
  rqtp.tv_nsec = (usec - (int64_t)rqtp.tv_sec * 1000000) * 1000;
  clock_nanosleep(CLOCK_MONOTONIC, 0, &rqtp, NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void * ffmpeg_alloc_ptr_array(uint n, size_t item_size)
{
  void ** p;

  if ( (p = av_mallocz(n * sizeof(*p))) ) {

    for ( uint i = 0; i < n; ++i ) {

      if ( !(p[i] = av_mallocz(item_size)) ) {
        for ( uint j = 0; j < i; ++j ) {
          av_free(p[j]);
        }

        av_free(p);
        p = NULL;
        break;
      }
    }
  }
  return p;
}

void ffmpeg_free_ptr_array(void * a, uint n)
{
  void *** p = a;
  if ( p && *p ) {
    for ( uint i = 0; i < n; ++i ) {
      av_free((*p)[i]);
    }
    av_free(*p), *p = NULL;
  }
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




int ffmpeg_parse_options(const char * options, bool remove_prefix, AVDictionary ** rv)
{
  AVDictionary * tmp = NULL;
  AVDictionaryEntry * e = NULL;
  int status = 0;

  if ( !options || !*options  ) {
    goto end;
  }

  if ( (status = av_dict_parse_string(&tmp, options, " \t", " \t", 0)) || !tmp ) {
    goto end;
  }

  while ( (e = av_dict_get(tmp, "", e, AV_DICT_IGNORE_SUFFIX)) ) {

    const char * key = e->key;
    const char * value = e->value;

    if ( remove_prefix ) {
      if ( *e->key != '-' ) {
        status = AVERROR(EINVAL);
        break;
      }
      ++key;
    }

    if ( (status = av_dict_set(rv, key, value, 0)) < 0 ) {
      break;
    }

    status = 0;
  }

end:

  av_dict_free(&tmp);

  return status;
}




// See ffmpeg/cmdutils.c filter_codec_opts()
// flags is AV_OPT_FLAG_ENCODING_PARAM or AV_OPT_FLAG_DECODING_PARAM
int ffmpeg_filter_codec_opts(const AVDictionary * opts, const AVCodec * codec, int flags, AVDictionary ** rv)
{
  const AVClass * cc = NULL;
  AVDictionaryEntry * e = NULL;

  int status = 0;

  cc = avcodec_get_class();

  while ( (e = av_dict_get(opts, "-", e, AV_DICT_IGNORE_SUFFIX)) ) {

    if ( av_opt_find(&cc, e->key + 1, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ) {
      PDBG("[%s] set '%s' = '%s'", codec->name, e->key, e->value);
      av_dict_set(rv, e->key + 1, e->value, 0);
    }
    else if ( (codec->priv_class && av_opt_find((void*)&codec->priv_class, e->key + 1, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ)) ) {
      PDBG("[%s] set '%s' = '%s'", codec->name, e->key, e->value);
      av_dict_set(rv, e->key + 1, e->value, 0);
    }
    else {
      PDBG("[%s] OPTION '%s' NOT FOUND", codec->name, e->key);
    }
  }

  return status;
}

int ffmpeg_apply_opts(const char * options, void * obj, bool ignore_if_not_found)
{
  static const char delims[] = " \t\n\r";

  char opts[strlen(options) + 1];
  const char * opt, * val;

  int status = 0;

  opt = strtok(strcpy(opts, options), delims);

  while ( opt ) {

    if ( *opt != '-' ) {
      PDBG("Option must start with '-' symbol. Got:%s", opt);
      status = AVERROR(EINVAL);
      break;
    }

    if ( !*(++opt) ) {
      PDBG("Invalid option name '-'");
      status = AVERROR(EINVAL);
      break;
    }

    if ( !(val = strtok(NULL, delims)) ) {
      PDBG("Missing argument for option '%s'",  opt);
      status = AVERROR(EINVAL);
      break;
    }

    if ( (status = av_opt_set(obj, opt, val, AV_OPT_SEARCH_CHILDREN)) != 0 ) {
      PDBG("av_opt_set(%s=%s) FAILS: %s", opt, val, av_err2str(status));
      if ( ignore_if_not_found ) {
        status = 0;
      }
      else {
        break;
      }
    }

    opt = strtok(NULL, delims);
  }

  return status;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int ffmpeg_alloc_input_context(AVFormatContext **ic, AVIOContext * pb, AVIOInterruptCB * icb, AVDictionary ** options)
{
  AVInputFormat * fmt = NULL;
  AVDictionaryEntry * e = NULL;

  int status = 0;

  if ( !(*ic = avformat_alloc_context()) ) {
    status = AVERROR(ENOMEM);
    goto end;
  }

  if ( icb ) {
    (*ic)->interrupt_callback = *icb;
  }

  if ( pb ) {
    (*ic)->pb = pb;
    (*ic)->flags |= AVFMT_FLAG_CUSTOM_IO;
  }

  if ( options && *options ) {

    if ( (e = av_dict_get(*options, "f", e, 0)) ) {
      if ( (fmt = av_find_input_format(e->value)) ) {
        (*ic)->iformat = fmt;
      }
      else {
        status = AVERROR_DEMUXER_NOT_FOUND;
        goto end;
      }
    }

    while ( (e = av_dict_get(*options, "", e, AV_DICT_IGNORE_SUFFIX)) ) {
      if ( strcmp(e->key, "f") != 0 ) {
        av_opt_set(*ic, e->key, e->value, AV_OPT_SEARCH_CHILDREN);
      }
    }
  }

end:

  if ( status ) {
    avformat_close_input(ic);
  }

  return status;
}



int ffmpeg_open_input(AVFormatContext **ic, const char *filename, AVIOContext * pb, AVIOInterruptCB * icb,
    AVDictionary ** options)
{
  int status;

  if ( (status = ffmpeg_alloc_input_context(ic, pb, icb, options)) ) {
    PDBG("[%s] ffmpeg_alloc_format_context() fails: %s", filename, av_err2str(status));
    goto end;
  }

  (*ic)->flags |= AVFMT_FLAG_DISCARD_CORRUPT; // AVFMT_FLAG_NONBLOCK |

  if ( (status = avformat_open_input(ic, filename, NULL, options)) < 0 ) {
    if ( icb && icb->callback && icb->callback(icb->opaque) ) {
      status = AVERROR_EXIT;
    }
    goto end;
  }

end:

  if ( status ) {
    avformat_close_input(ic);
  }

  return status;
}


int ffmpeg_probe_input(AVFormatContext * ic, bool fast)
{
  int status = 0;

  if ( fast ) {
    ffmpeg_apply_opts("-fpsprobesize 0", ic, true);
  }

  if ( (status = avformat_find_stream_info(ic, NULL)) ) {
    PDBG("avformat_find_stream_info() fails: %s", av_err2str(status));
  }

  return status;
}


void ffmpeg_close_input(AVFormatContext ** ic)
{
  avformat_close_input(ic);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ffmpeg_rescale_timestamps(AVPacket * pkt, const AVRational from, const AVRational to)
{
  if ( pkt->pts != AV_NOPTS_VALUE ) {
    pkt->pts = av_rescale_q_rnd(pkt->pts, from, to, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
  }
  if ( pkt->dts != AV_NOPTS_VALUE ) {
    pkt->dts = av_rescale_q_rnd(pkt->dts, from, to, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
  }
  if ( pkt->duration != 0 ) {
    pkt->duration = av_rescale_q(pkt->duration, from, to);
  }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// flags is AV_OPT_FLAG_ENCODING_PARAM or AV_OPT_FLAG_DECODING_PARAM
int ffmpeg_open_codec(AVCodecContext ** ctx, const AVCodec * codec, AVDictionary * opts, int flags)
{
  AVDictionary * codec_opts = NULL;
  int status = 0;

  if ( opts && (status = ffmpeg_filter_codec_opts(opts, codec, flags, &codec_opts)) ) {
    PDBG("ffmpeg_filter_codec_opts() fails");
  }
  else if ( !(*ctx = avcodec_alloc_context3(codec)) ) {
    PDBG("avcodec_alloc_context3() fails");
    status = AVERROR(ENOMEM);
  }
  else if ( (status = avcodec_open2(*ctx, codec, &opts)) ) {
    PDBG("avcodec_open2('%s') fails: %s", codec->name, av_err2str(status));
    avcodec_free_context(ctx);
  }

  av_dict_free(&codec_opts);

  return status;
}


int ffmpeg_decode_packet(AVCodecContext * codec, AVPacket * pkt, AVFrame * outfrm, int * gotframe)
{
  int status = 0;

  switch ( codec->codec_type )
  {
  case AVMEDIA_TYPE_VIDEO:
    status = avcodec_decode_video2(codec, outfrm, gotframe, pkt);
    break;

  case AVMEDIA_TYPE_AUDIO:
    status = avcodec_decode_audio4(codec, outfrm, gotframe, pkt);
    break;

  default:
    status = AVERROR(ENOTSUP);
    break;
  }

  return status;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int ffmpeg_create_video_frame(AVFrame ** out, enum AVPixelFormat fmt, int cx, int cy, int aling)
{
  AVFrame * frame;
  int status;

  if ( !(frame = av_frame_alloc()) ) {
    status = AVERROR(ENOMEM);
  }
  else {

    frame->format = fmt;
    frame->width = cx;
    frame->height = cy;

    if ( (status = av_frame_get_buffer(frame, aling)) ) {
      av_frame_free(&frame);
    }
  }

  *out = frame;

  return status;
}


int ffmpeg_create_audio_frame(AVFrame ** out, enum AVSampleFormat fmt, int sample_rate, int nb_samples, int channels,
    uint64_t channel_layout)
{
  AVFrame * frame;
  int status;

  if ( !(frame = av_frame_alloc()) ) {
    status = AVERROR(ENOMEM);
  }
  else {

    frame->format = fmt;
    frame->nb_samples = nb_samples;
    frame->channel_layout = channel_layout;
    frame->channels = channels;
    frame->sample_rate = sample_rate;

    if ( (status = av_frame_get_buffer(frame, 64)) ) {
      av_frame_free(&frame);
    }
  }

  *out = frame;

  return status;
}


int ffmpeg_copy_frame(AVFrame * dst, const AVFrame * src)
{
  int status;

  if ( dst->format != src->format || dst->format < 0 ) {
    status = AVERROR(EINVAL);
  }
  else if ( dst->nb_samples != src->nb_samples ) {
    status = AVERROR(EINVAL);
  }
  else if ( dst->channels != src->channels || dst->channel_layout != src->channel_layout ) {
    status = AVERROR(EINVAL);
  }
  else if ( (status = av_frame_copy(dst, src)) >= 0 && (status = av_frame_copy_props(dst, src)) > 0 ) {
    status = 0;
  }

  return status;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Select best pixel/sample format for encoder */
int ffmpeg_select_best_format(const int fmts[], int default_fmt)
{
  int fmt = -1;

  if ( fmts ) {
    int j = 0;

    while ( fmts[j] != -1 && fmts[j] != default_fmt ) {
      ++j;
    }
    if ( fmts[j] == default_fmt ) {
      fmt = default_fmt;
    }
    else {
      fmt = fmts[0];
    }
  }

  return fmt;
}


bool ffmpeg_is_format_supported(const int fmts[], int fmt)
{
  if ( !fmts ) {
    return true;    // assume yes if not specified
  }

  for ( uint i = 0; fmts[i] != -1; ++i ) {
    if ( fmts[i] == fmt ) {
      return true;
    }
  }

  return false;
}



bool ffmpeg_is_channel_layout_supported(const AVCodec * codec, uint64_t channel_layout)
{
  if ( !codec->channel_layouts ) {
    return true; // assume yes if not specified
  }

  for ( uint i = 0; codec->channel_layouts[i]; ++i ) {
    if ( codec->channel_layouts[i] == channel_layout ) {
      return true;
    }
  }

  return false;
}


uint64_t ffmpeg_select_best_channel_layout(const AVCodec * codec, uint64_t channel_layout)
{
  if ( !codec->channel_layouts ) {
    return channel_layout;
  }

  for ( uint i = 0; codec->channel_layouts[i]; ++i ) {
    if ( codec->channel_layouts[i] == channel_layout ) {
      return channel_layout;
    }
  }

  return codec->channel_layouts[0];
}


const int * ffmpeg_get_supported_samplerates(const AVCodec * enc, const AVOutputFormat * ofmt)
{
  const int * supported_samplerates = enc->supported_samplerates;

  if ( !supported_samplerates ) {
    if ( strcmp(ofmt->name, "flv") == 0 ) {
      static const int flv_samplerates[] = { 44100, 22050, 11025, 0 };
      supported_samplerates = flv_samplerates;
    }
  }

  return supported_samplerates;
}


int ffmpeg_select_samplerate(const AVCodec * enc, const AVOutputFormat * ofmt, int dec_sample_rate)
{
  int enc_sample_rate = dec_sample_rate;
  int min_diff = INT_MAX;

  const int * supported_samplerates = ffmpeg_get_supported_samplerates(enc, ofmt);

  if ( supported_samplerates ) {
    int i;
    for ( i = 0; supported_samplerates[i] != 0; ++i ) {
      if ( abs(supported_samplerates[i] - dec_sample_rate) < min_diff ) {
        min_diff = abs(supported_samplerates[i] - dec_sample_rate);
        enc_sample_rate = supported_samplerates[i];
      }
    }
  }

  return enc_sample_rate;
}


bool ffmpeg_is_samplerate_supported(const AVCodec * enc, const AVOutputFormat * ofmt, int sample_rate)
{
  const int * supported_samplerates;
  int i;

  if ( !(supported_samplerates = ffmpeg_get_supported_samplerates(enc, ofmt)) ) {
    return true; /* assume yes if unknown */
  }

  for ( i = 0; supported_samplerates[i] != 0; ++i ) {
    if ( supported_samplerates[i] == sample_rate ) {
      return true;
    }
  }

  return false;
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int ffstream_init(struct ffstream * dst, const AVStream * src)
{
  int status = 0;

  memset(dst, 0, sizeof(*dst));

  if ( src && (status = ffstream_copy_context(dst, src, true, true)) ) {
    ffstream_cleanup(dst);
  }

  return status;
}

void ffstream_cleanup(struct ffstream * dst)
{
  av_dict_free(&dst->metadata);
  avcodec_parameters_free(&dst->codecpar);
}


int ffstream_copy(struct ffstream * dst, const struct ffstream * src, bool copy_codecpar, bool copy_metadata )
{
  int status;

  dst->start_time = src->start_time;
  dst->duration = src->duration;
  dst->time_base = src->time_base;
  dst->sample_aspect_ratio = src->sample_aspect_ratio;
  dst->display_aspect_ratio = src->display_aspect_ratio;
  dst->discard = src->discard;
  dst->disposition = src->disposition;

  if ( copy_metadata && src->metadata && (status = av_dict_copy(&dst->metadata, src->metadata, 0)) ) {
    goto end;
  }

  if ( copy_codecpar && src->codecpar ) {

    if ( !(dst->codecpar = avcodec_parameters_alloc()) ) {
      status = AVERROR(ENOMEM);
      goto end;
    }

    if ( (status = avcodec_parameters_copy(dst->codecpar, src->codecpar)) < 0 ) {
      goto end;
    }
  }

  status = 0;

end:

  if ( status ) {
    ffstream_cleanup(dst);
  }

  return status;
}

int ffstream_copy_context(ffstream * dst, const AVStream * src, bool copy_codecpar, bool copy_metadata)
{
  int status;

  dst->start_time = src->start_time;
  dst->duration = src->duration;
  dst->time_base = src->time_base;
  dst->sample_aspect_ratio = src->sample_aspect_ratio;
  dst->display_aspect_ratio = src->display_aspect_ratio;
  dst->discard = src->discard;
  dst->disposition = src->disposition;

  if ( copy_metadata && src->metadata && (status = av_dict_copy(&dst->metadata, src->metadata, 0)) ) {
    goto end;
  }

  if ( copy_codecpar && src->codecpar ) {

    if ( !dst->codecpar && !(dst->codecpar = avcodec_parameters_alloc())) {
      status = AVERROR(ENOMEM);
      goto end;
    }

    if ( (status = avcodec_parameters_copy(dst->codecpar, src->codecpar)) < 0 ) {
      goto end;
    }
  }

  status = 0;

end:

  return status;
}



int ffstream_to_context(const ffstream * src, AVStream * os)
{
  int status;

  os->start_time = src->start_time;
  os->duration = src->duration;
  os->time_base = src->time_base;
  os->sample_aspect_ratio = src->sample_aspect_ratio;
  os->display_aspect_ratio = src->display_aspect_ratio;
  os->discard = src->discard;
  os->disposition = src->disposition;

  if ( src->metadata && (status = av_dict_copy(&os->metadata, src->metadata, 0)) ) {
    goto end;
  }

  if ( !os->codecpar && !(os->codecpar = avcodec_parameters_alloc())) {
    status = AVERROR(ENOMEM);
    goto end;
  }

  if ( (status = avcodec_parameters_copy(os->codecpar, src->codecpar)) < 0 ) {
    goto end;
  }

  // hack ? ffmpeg magic ?
  os->codecpar->block_align = 0;

  status = 0;

end:

  return status;
}




int ffstreams_to_context(const ffstream * const * streams, uint nb_streams, AVFormatContext * oc)
{
  int status = 0;

  for ( uint i = 0, status = 0; i < nb_streams; ++i ) {
    if ( !avformat_new_stream(oc, NULL) ) {
      status = AVERROR(ENOMEM);
      break;
    }
    if ( (status = ffstream_to_context(streams[i], oc->streams[i])) ) {
      break;
    }
  }

  return status;
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




