/*
 * sendvideo.h
 *
 *  Created on: May 15, 2016
 *      Author: amyznikov
 */

#pragma once

#ifndef __sendvideo_h__
#define __sendvideo_h__

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef struct ff_output_stream
  ff_output_stream;

typedef
enum ff_output_stream_state {
  ff_output_stream_idle = 0,
  ff_output_stream_starting = 1,
  ff_output_stream_connecting = 2,
  ff_output_stream_established = 3,
  ff_output_stream_disconnecting = 4,
  ff_output_stream_paused = 5,
} ff_output_stream_state;


// Fixme: Only NV21 assumed
#define FRAME_DATA_SIZE(cx,cy)  ((cx) * (cy) * 12 / 8)


enum {
  frm_type_video,
  frm_type_audio
};

struct frm {
  int64_t pts;
  uint32_t size;
  uint32_t type;
  uint8_t data[];
};


typedef
struct ff_output_stream_event_callback {
  void (*stream_state_changed)(void * cookie, ff_output_stream * s,  ff_output_stream_state state, int reason);
} ff_output_stream_event_callback;




typedef
struct create_output_stream_args {
  const char * server;
  const char * format;
  const char * ffopts;
  const char * cvcodec;
  const char * cacodec;
  const ff_output_stream_event_callback * events_callback;
  void * cookie;
  int cx, cy, pxfmt;
  int cvquality;
  int caquality;
  int gopsize;
} create_output_stream_args;



ff_output_stream * create_output_stream(const create_output_stream_args * args);
bool start_output_stream(ff_output_stream * ctx);
void stop_output_stream(ff_output_stream * ctx);
void destroy_output_stream(ff_output_stream * ctx);

ff_output_stream_state get_output_stream_state(const ff_output_stream * ctx);
void * get_output_stream_cookie(const ff_output_stream * ctx);
size_t get_video_frame_data_size(const ff_output_stream * ctx);

struct frm * pop_video_frame(ff_output_stream * ctx);
void push_video_frame(ff_output_stream * ctx, struct frm * frm);


struct output_stream_stats {
  int64_t timer;
  int64_t bytesRead, bytesSent;
  int64_t framesRead, framesSent;
  int64_t inputFpsMark, outputFpsMark;
  int64_t inputBitrateMark, outputBitrateMark;
  int inputFps, outputFps;
  int inputBitrate, outputBitrate;
};


const struct output_stream_stats * get_output_stream_stats(ff_output_stream * ctx);

#ifdef __cplusplus
}
#endif

#endif /* __sendvideo_h__ */
