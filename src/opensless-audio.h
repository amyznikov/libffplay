/*
 * opensless-audio.h
 *
 *  Created on: Dec 15, 2012
 *      Author: amyznikov
 */

#ifndef __opensless_audio_h__
#define __opensless_audio_h__

#include <stddef.h>
#include <stdint.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Callback called every time a buffer finishes recording or plaing */
typedef void (*opensless_audio_callback)(void * context, void * bufr, size_t size);


int opensless_audio_initialize(void);
void opensless_audio_shutdown(void);


/* audio capture context */
typedef struct opensless_audio_capture_t
  opensless_audio_capture;



int opensless_audio_capture_create(opensless_audio_capture ** outdev, void * context, opensless_audio_callback callback, uint32_t numBuffers, uint32_t sampleRate);
void opensless_audio_capture_destroy(opensless_audio_capture ** ctx);
int opensless_audio_capture_start(opensless_audio_capture * ctx);
int opensless_audio_capture_enqueue(opensless_audio_capture * ctx, void * bufr, size_t size);
int opensless_audio_capture_stop(opensless_audio_capture * ctx);
size_t opensless_audio_capture_queue_size(opensless_audio_capture * ctx);




/* audio playback context */
typedef struct opensless_audio_playback_t
  opensless_audio_playback;

#define AUDIO_PLAYBACK_MAX_BUFFERS  16

opensless_audio_playback * opensless_audio_playback_create(void * context, opensless_audio_callback callback,
    uint32_t numBuffers, uint32_t sampleRate);
void opensless_audio_playback_destroy(opensless_audio_playback * ctx);
int opensless_audio_playback_start(opensless_audio_playback * ctx);
int opensless_audio_playback_enqueue(opensless_audio_playback * ctx, void * bufr, size_t size);
int opensless_audio_playback_stop(opensless_audio_playback * ctx);
size_t opensless_audio_playback_queue_size(opensless_audio_playback * ctx);



#ifdef __cplusplus
}
#endif

#endif /* __opensless_audio_h__ */
