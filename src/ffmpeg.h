/*
 * ffmpeg.h
 *
 *  Created on: Jul 12, 2014
 *      Author: amyznikov
 */


#ifndef __ffmpeg_h__
#define __ffmpeg_h__

#include <stddef.h>
#include <inttypes.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <libavutil/common.h>
#include <libavutil/error.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/eval.h>


#ifdef __cplusplus
extern "C" {
#endif


int64_t ffmpeg_gettime_us(void);
int64_t ffmpeg_gettime_ms(void);
void ffmpeg_usleep(int64_t usec);


void * ffmpeg_alloc_ptr_array(uint n, size_t item_size);
void ffmpeg_free_ptr_array(void * a, uint n);




int ffmpeg_parse_options(const char * options,
    bool remove_prefix,
    AVDictionary ** rv);

int ffmpeg_filter_codec_opts(const AVDictionary * opts,
    const AVCodec * codec,
    int flags,
    AVDictionary ** rv);

int ffmpeg_apply_opts(const char * options,
    void * obj,
    bool ignore_if_not_found);





int ffmpeg_alloc_input_context(AVFormatContext **ic,
    AVIOContext * pb,
    AVIOInterruptCB * icb,
    AVDictionary ** options);

int ffmpeg_open_input(AVFormatContext **ic,
    const char *filename,
    AVIOContext * pb,
    AVIOInterruptCB * icb,
    AVDictionary ** options);


int ffmpeg_probe_input(AVFormatContext * ic,
    bool fast);

void ffmpeg_close_input(AVFormatContext **ic);



void ffmpeg_set_pts_info(AVStream * os,
    const struct AVOutputFormat * oformat);

void ffmpeg_rescale_timestamps(AVPacket * pkt,
    const AVRational from,
    const AVRational to );



// flags is AV_OPT_FLAG_ENCODING_PARAM or AV_OPT_FLAG_DECODING_PARAM
int ffmpeg_open_codec(AVCodecContext ** ctx,
    const AVCodec * codec,
    AVDictionary * opts,
    int flags);


int ffmpeg_decode_packet(AVCodecContext * codec,
    AVPacket * pkt,
    AVFrame * outfrm,
    int * gotframe);




int ffmpeg_create_video_frame(AVFrame ** out,
    enum AVPixelFormat fmt,
    int cx, int cy,
    int aling);

int ffmpeg_create_audio_frame(AVFrame ** out,
    enum AVSampleFormat fmt,
    int sample_rate,
    int nb_samples,
    int channels,
    uint64_t channel_layout);

int ffmpeg_copy_frame(AVFrame * dst,
    const AVFrame * src);



bool ffmpeg_is_format_supported(const int fmts[],
    int fmt );

/** Select best pixel/sample format for encoder */
int ffmpeg_select_best_format(const int fmts[],
    int default_format);


bool ffmpeg_is_channel_layout_supported(const AVCodec * codec,
    uint64_t channel_layout );

uint64_t ffmpeg_select_best_channel_layout(const AVCodec * codec,
    uint64_t channel_layout );

const int * ffmpeg_get_supported_samplerates(const AVCodec * codec,
    const AVOutputFormat * ofmt);

/** Select best sample rate for encoder */
int ffmpeg_select_samplerate(const AVCodec * enc,
    const AVOutputFormat * ofmt,
    int dec_sample_rate);


/** Check id sample rate is supported by encoder */
bool ffmpeg_is_samplerate_supported(const AVCodec * enc,
    const AVOutputFormat * ofmt,
    int sample_rate);



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef
struct ffstream {
  int64_t start_time; // add pts_wrap_bits
  int64_t duration;
  AVRational time_base;
  AVRational sample_aspect_ratio;
  AVRational display_aspect_ratio;
  AVCodecParameters * codecpar;
  AVDictionary * metadata;
  enum AVDiscard discard;
  int disposition;
} ffstream;

int ffstream_init(struct ffstream * dst,
    const AVStream * src);

void ffstream_cleanup(struct ffstream * dst);

int ffstream_copy(struct ffstream * dst,
    const struct ffstream * src,
    bool copy_codecpar,
    bool copy_metadata );

int ffstream_copy_context(ffstream * dst,
    const AVStream * src,
    bool copy_codecpar,
    bool copy_metadata );

int ffstream_to_context(const ffstream * src,
    AVStream * os);

int ffstreams_to_context(const ffstream * const * streams,
    uint nb_streams,
    AVFormatContext * oc);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



#define av_tb2str(tb) \
  av_timebase2str((char[64]){0},(tb))

static inline const char * av_timebase2str(char buf[64], AVRational tb) {
  snprintf(buf, 63, "%d/%d", tb.num, tb.den);
  return buf;
}

static inline int64_t av_rescale_ts(int64_t ts, AVRational from, AVRational to) {
  return av_rescale_q_rnd(ts, from, to, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* debug */
static inline char * fcc2str(uint32_t fcc) {
  static __thread char bfr[8];
  bfr[0] = fcc & 0xFF;
  bfr[1] = (fcc >> 8) & 0xFF;
  bfr[2] = (fcc >> 16) & 0xFF;
  bfr[3] = (fcc >> 24) & 0xFF;
  return bfr;
}




#ifdef __cplusplus
}
#endif

#endif /* __ffmpeg_h__ */
