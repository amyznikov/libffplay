/*
 * opensless-audio.c
 *
 *  Created on: Dec 15, 2012
 *      Author: amyznikov
 *
 * Acoustic Echo Canceler (AEC) refs:
 *      http://developer.android.com/reference/android/media/audiofx/AcousticEchoCanceler.html
 *      http://www.amarulasolutions.com/index.php?option=com_content&view=article&id=18&Itemid=19
 *      http://yltechblog.blogspot.com/2012/08/speex-aec-with-android.html
 */

// #include "config.h"
#include <sys/system_properties.h>
#include <limits.h>
#include <stdlib.h>
#include <malloc.h>
#include <pthread.h>
#include "opensless-audio.h"
#include "debug.h"

/** engine interfaces */
static SLObjectItf engineObject;
static SLEngineItf engineInterface;

typedef
struct bufrqueue_s {
  size_t capacity;
  size_t size;

  struct item_s {
    void * bufr;
    size_t size;
  } items[];

} bufrqueue;

/** recorder interfaces and buffers */
struct opensless_audio_capture_t {

  opensless_audio_callback cb;
  void * context;

  bufrqueue * bq;
  pthread_mutex_t mutex;

  SLObjectItf recorderObject;
  SLRecordItf recorderInterface;
  SLAndroidConfigurationItf configInterface;
  SLAndroidSimpleBufferQueueItf bufferQueueInterface;
};

/** player interfaces and buffers */
struct opensless_audio_playback_t {

  opensless_audio_callback cb;
  void * context;

  bufrqueue * bq;
  pthread_mutex_t mutex;

  SLObjectItf outputMixObject;
  SLObjectItf playerObject;
  SLPlayItf playerInterface;
  SLAndroidConfigurationItf configInterface;
  SLAndroidSimpleBufferQueueItf bufferQueueInterface;
};

static int get_sdk_version()
{
  static long sdk_version = 0;
  if ( !sdk_version ) {
    static char prop_value[PROP_VALUE_MAX] = "";
    char * endptr = NULL;
    __system_property_get("ro.build.version.sdk", prop_value);

    sdk_version = strtol(prop_value, &endptr, 10);
    if ( endptr == NULL || endptr <= prop_value || sdk_version == LONG_MIN || sdk_version == LONG_MAX ) {
      sdk_version = 0;
    }
  }
  return sdk_version;
}

static const char * get_device_manufacturer()
{
  static char prop_value[PROP_VALUE_MAX] = "";
  if ( !*prop_value ) {
    __system_property_get("ro.product.manufacturer", prop_value);
  }
  return prop_value;
}

static bufrqueue * bufrqueue_create(size_t capacity)
{
  bufrqueue * bq;
  bq = (bufrqueue *) malloc(sizeof(bufrqueue) + capacity * sizeof(bq->items[0]));
  if ( bq ) {
    bq->capacity = capacity;
    bq->size = 0;
  }
  return bq;
}

static void bufrqueue_destroy(bufrqueue * bq)
{
  if ( bq ) {
    free(bq);
  }
}

static size_t bufrqueue_push(bufrqueue * bq, void * bufr, size_t size)
{
  if ( bq->size >= bq->capacity ) {
    return (size_t) (-1);
  }

  bq->items[bq->size].bufr = bufr;
  bq->items[bq->size].size = size;

  return ++bq->size;
}

static size_t bufrqueue_pop(bufrqueue * bq, void ** bufr, size_t * size)
{
  if ( bq->size < 1 ) {
    return (size_t) (-1);
  }

  *bufr = bq->items[0].bufr;
  *size = bq->items[0].size;

  if ( --bq->size ) {
    memcpy(&bq->items[0], &bq->items[1], sizeof(bq->items[0]) * bq->size);
  }

  return bq->size;
}

static int sr2sles(uint32_t samplerate)
{
  int sles_sample_rate_tag = -1;

  switch ( samplerate ) {
    case 8000 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_8;
    break;
    case 11025 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_11_025;
    break;
    case 16000 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_16;
    break;
    case 22050 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_22_05;
    break;
    case 24000 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_24;
    break;
    case 32000 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_32;
    break;
    case 44100 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_44_1;
    break;
    case 48000 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_48;
    break;
    case 64000 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_64;
    break;
    case 88200 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_88_2;
    break;
    case 96000 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_96;
    break;
    case 192000 :
      sles_sample_rate_tag = SL_SAMPLINGRATE_192;
    break;
  }
  return sles_sample_rate_tag;
}

/**
 *  close the OpenSL IO and destroy the audio engine
 */
static void openSLDestroyEngine()
{
  /* destroy engine object, and invalidate all associated interfaces */
  if ( engineObject != NULL ) {
    (*engineObject)->Destroy(engineObject);
    engineObject = NULL;
    engineInterface = NULL;
  }
}

/**
 * creates the OpenSL ES audio engine
 */
static SLresult openSLCreateEngine()
{
  SLresult status;

  if ( engineObject != NULL ) {
    PERROR("WARNING:  engineObject is not NULL");
  }

  /* create engine */
  status = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
  if ( status != SL_RESULT_SUCCESS ) {
    PERROR("slCreateEngine() fails: 0x%X",status);
    goto end;
  }

  /* realize the engine */
  status = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
  if ( status != SL_RESULT_SUCCESS ) {
    PERROR("engineObject->Realize() fails: 0x%X",status);
    goto end;
  }

  /* get the engine interface, which is needed in order to create other objects */
  status = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineInterface);
  if ( status != SL_RESULT_SUCCESS ) {
    PERROR("engineObject->GetInterface(SL_IID_ENGINE) fails: 0x%X",status);
    goto end;
  }

  end : if ( status != SL_RESULT_SUCCESS ) {
    openSLDestroyEngine();
  }

  return status;
}

/** enqueue the buffer for playing or capturing */
static int enqueue(bufrqueue * bq, pthread_mutex_t * mutex, SLAndroidSimpleBufferQueueItf bufferQueueInterface,
    void * bufr, size_t size)
{
  int status;

  pthread_mutex_lock(mutex);

  if ( bq->size >= bq->capacity ) {
    status = SL_RESULT_PRECONDITIONS_VIOLATED;
  }
  else if ( (status = (*bufferQueueInterface)->Enqueue(bufferQueueInterface, bufr, size)) == 0 ) {
    bufrqueue_push(bq, bufr, size);
  }

  pthread_mutex_unlock(mutex);

  return status;
}

/** get queue size */
static size_t get_queue_size(bufrqueue * bq, pthread_mutex_t * mutex)
{
  size_t size;

  pthread_mutex_lock(mutex);
  size = bq->size;
  pthread_mutex_unlock(mutex);

  return size;
}

static void call_audio_callback(bufrqueue * bq, pthread_mutex_t * mutex, opensless_audio_callback cb, void * context)
{
  void * bufr;
  size_t samples;
  size_t bqsize;

  pthread_mutex_lock(mutex);
  bqsize = bufrqueue_pop(bq, &bufr, &samples);
  pthread_mutex_unlock(mutex);

  if ( bqsize != (size_t) (-1) && cb != NULL ) {
    cb(context, bufr, samples);
  }
}

/**
 *  This callback handler is called every time a capture buffer finishes recording
 */
static void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void * context)
{
  opensless_audio_capture * ctx = (opensless_audio_capture *) context;
  call_audio_callback(ctx->bq, &ctx->mutex, ctx->cb, ctx->context);
  (void) (bq);
}

/**
 * This callback handler is called every time a buffer finishes playing
 */
static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void * context)
{
  opensless_audio_playback * ctx = (opensless_audio_playback *) context;
  call_audio_callback(ctx->bq, &ctx->mutex, ctx->cb, ctx->context);
  (void) (bq);
}

/**********************************************************************************************************************/

int opensless_audio_initialize(void)
{
  return (engineObject == NULL ? openSLCreateEngine() : SL_RESULT_SUCCESS);
}

void opensless_audio_shutdown(void)
{
  openSLDestroyEngine();
}

/**********************************************************************************************************************/

/**
 * Open the OpenSL ES device for input.
 * Requires the RECORD_AUDIO permission
 */
int opensless_audio_capture_create(opensless_audio_capture ** outdev, void * context, opensless_audio_callback callback,
    uint32_t numBuffers, uint32_t sampleRate)
{
  opensless_audio_capture * ctx = NULL;


  int status = 0;

  if ( !(ctx = (opensless_audio_capture *) calloc(1, sizeof(opensless_audio_capture))) ) {
    status = SL_RESULT_MEMORY_FAILURE;
  }
  else {

    /** create and setup audio source */
    const int channels = 1;
    const int speakers = SL_SPEAKER_FRONT_CENTER;

    SLDataLocator_IODevice loc_dev = {
      .locatorType = SL_DATALOCATOR_IODEVICE,
      .deviceType = SL_IODEVICE_AUDIOINPUT,
      .deviceID = SL_DEFAULTDEVICEID_AUDIOINPUT,
      .device = NULL
    };

    SLDataSource audioSrc = {
      .pLocator = &loc_dev,
      .pFormat = NULL
    };

    SLDataLocator_AndroidSimpleBufferQueue loc_bq = {
      .locatorType= SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
      .numBuffers = numBuffers
    };


    SLDataFormat_PCM format_pcm = {
      .formatType = SL_DATAFORMAT_PCM,
      .numChannels = channels,
      .samplesPerSec = sr2sles(sampleRate),
      .bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16,
      .containerSize = SL_PCMSAMPLEFORMAT_FIXED_16,
      .channelMask = speakers,
      .endianness = SL_BYTEORDER_LITTLEENDIAN
    };

    SLDataSink audioSnk = {
      .pLocator = &loc_bq,
      .pFormat = &format_pcm
    };

    const SLInterfaceID rec_iids[] = {
      SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
      SL_IID_ANDROIDCONFIGURATION
    };

    const SLboolean rec_reqs[] = {
      SL_BOOLEAN_TRUE,
      SL_BOOLEAN_TRUE
    };


    /* Create capture buffer queue and it's lock
     * */

    if ( pthread_mutex_init(&ctx->mutex, 0) != 0 ) {
      PERROR("pthread_mutex_init() fails");
      status = SL_RESULT_INTERNAL_ERROR;
      goto end;
    }

    if ( !(ctx->bq = bufrqueue_create(numBuffers)) ) {
      PERROR("bufrqueue_create(numbuffers=%u) fails",numBuffers);
      status = SL_RESULT_MEMORY_FAILURE;
      goto end;
    }

    /*
     * create audio recorder
     */
    status = (*engineInterface)->CreateAudioRecorder(engineInterface, &ctx->recorderObject, &audioSrc, &audioSnk,
        sizeof(rec_iids) / sizeof(rec_iids[0]), rec_iids, rec_reqs);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("engineInterface->CreateAudioRecorder() fails: 0x%X", status);
      goto end;
    }

    status = (*ctx->recorderObject)->GetInterface(ctx->recorderObject, SL_IID_ANDROIDCONFIGURATION,
        &ctx->configInterface);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("GetInterface(SL_IID_ANDROIDCONFIGURATION) fails: 0x%X", status);
    }
    else if ( strcasecmp(get_device_manufacturer(), "LENOVO") == 0 ) {
      PERROR("Warning: Having '%s', setting SL_ANDROID_KEY_RECORDING_PRESET skipped", get_device_manufacturer());
    }
    else {
      const int sdk_version = get_sdk_version();

      const SLint32 audioStreamType = sdk_version >= 14 ? SL_ANDROID_RECORDING_PRESET_CAMCORDER : SL_ANDROID_RECORDING_PRESET_GENERIC;
            // SL_ANDROID_RECORDING_PRESET_VOICE_COMMUNICATION

      PINFO("sdk_version=%d", sdk_version);

      status = (*ctx->configInterface)->SetConfiguration(ctx->configInterface, SL_ANDROID_KEY_RECORDING_PRESET,
          &audioStreamType, sizeof(audioStreamType));
      if ( status != SL_RESULT_SUCCESS ) {
        PERROR("SetConfiguration(audioStreamType=0x%X) fails: 0x%X", audioStreamType, status);
      }
    }

    status = (*ctx->recorderObject)->Realize(ctx->recorderObject, SL_BOOLEAN_FALSE);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("recorderObject->Realize() fails: 0x%X", status);
      goto end;
    }

    status = (*ctx->recorderObject)->GetInterface(ctx->recorderObject, SL_IID_RECORD, &ctx->recorderInterface);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR(" recorderObject->GetInterface(SL_IID_RECORD) fails: 0x%X", status);
      goto end;
    }

    /* get the buffer queue interface */
    status = (*ctx->recorderObject)->GetInterface(ctx->recorderObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
        &ctx->bufferQueueInterface);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("recorderObject->GetInterface(SL_IID_ANDROIDSIMPLEBUFFERQUEUE) fails: 0x%X", status);
      goto end;
    }

    /* register callback on the buffer queue */
    status = (*ctx->bufferQueueInterface)->RegisterCallback(ctx->bufferQueueInterface, bqRecorderCallback, ctx);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("recorderBufferQueue->RegisterCallback() fails: 0x%X", status);
      goto end;
    }

    ctx->cb = callback;
    ctx->context = context;

end : ;
    if ( status != SL_RESULT_SUCCESS ) {
      opensless_audio_capture_destroy(&ctx);
    }
  }

  *outdev = ctx;

  return status;
}

/**
 * destroy audio recorder object
 */
void opensless_audio_capture_destroy(opensless_audio_capture ** ctx)
{
  if ( ctx && *ctx ) {

    if ( (*ctx)->recorderObject != NULL ) {
      (*(*ctx)->recorderObject)->Destroy((*ctx)->recorderObject);
    }

    if ( (*ctx)->bq ) {
      bufrqueue_destroy((*ctx)->bq);
      pthread_mutex_destroy(&(*ctx)->mutex);
    }

    free(*ctx);
    *ctx = NULL;
  }
}

/**
 * start audio capture
 */
int opensless_audio_capture_start(opensless_audio_capture * ctx)
{
  return (*ctx->recorderInterface)->SetRecordState(ctx->recorderInterface, SL_RECORDSTATE_RECORDING);
}

/**
 * stop audio capture
 */
int opensless_audio_capture_stop(opensless_audio_capture * ctx)
{
  SLuint32 recordState = 0;
  int status;

  status = (*ctx->recorderInterface)->GetRecordState(ctx->recorderInterface, &recordState);
  if ( status == SL_RESULT_SUCCESS && recordState != SL_RECORDSTATE_STOPPED ) {
    status = (*ctx->recorderInterface)->SetRecordState(ctx->recorderInterface, SL_RECORDSTATE_STOPPED);
    (*ctx->bufferQueueInterface)->Clear(ctx->bufferQueueInterface);
  }

  return status;
}

int opensless_audio_capture_enqueue(opensless_audio_capture * ctx, void * bufr, size_t size)
{
  return enqueue(ctx->bq, &ctx->mutex, ctx->bufferQueueInterface, bufr, size);
}

size_t opensless_audio_capture_queue_size(opensless_audio_capture * ctx)
{
  return get_queue_size(ctx->bq, &ctx->mutex);
}

/**********************************************************************************************************************/

opensless_audio_playback * opensless_audio_playback_create(void * context, opensless_audio_callback callback, uint32_t numBuffers,
    uint32_t sampleRate)
{
  opensless_audio_playback * ctx = (opensless_audio_playback *) calloc(1, sizeof(opensless_audio_playback));

  if ( ctx ) {
    /** setup audio player */
    const int speakers = SL_SPEAKER_FRONT_CENTER;
    const int channels = 1;

    int status;

    SLint32 audioStreamType = SL_ANDROID_STREAM_VOICE;
    // SLmillibel vol, maxvol;


    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
     .locatorType =  SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
      .numBuffers = numBuffers
    };


    SLDataFormat_PCM format_pcm = {
      .formatType = SL_DATAFORMAT_PCM,
      .numChannels = channels,
      .samplesPerSec = sr2sles(sampleRate),
      .bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16,
      .containerSize = SL_PCMSAMPLEFORMAT_FIXED_16,
      .channelMask = speakers,
      .endianness = SL_BYTEORDER_LITTLEENDIAN
    };


    SLDataSource audioSrc = {
      .pLocator = &loc_bufq,
      .pFormat = &format_pcm
    };


    SLDataLocator_OutputMix loc_outmix = {
      .locatorType = SL_DATALOCATOR_OUTPUTMIX,
      .outputMix = NULL
    };


    SLDataSink audioSnk = {
      .pLocator = &loc_outmix,
      .pFormat = NULL
    };

    const SLInterfaceID player_iids[] = {
      SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
      SL_IID_ANDROIDCONFIGURATION
    };

    const SLboolean player_reqs[] = {
      SL_BOOLEAN_TRUE,
      SL_BOOLEAN_TRUE
    };


    /* Create play buffer queue and it's lock
     * */

    if ( pthread_mutex_init(&ctx->mutex, 0) != 0 ) {
      PERROR("pthread_mutex_init() fails");
      status = SL_RESULT_INTERNAL_ERROR;
      goto end;
    }

    if ( !(ctx->bq = bufrqueue_create(numBuffers)) ) {
      PERROR("bufrqueue_create(numbuffers=%u) fails",numBuffers);
      status = SL_RESULT_MEMORY_FAILURE;
      goto end;
    }

    /* create output mix */
    status = (*engineInterface)->CreateOutputMix(engineInterface, &ctx->outputMixObject, 0, NULL, NULL);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("CreateOutputMix() fails: 0x%X",status);
      goto end;
    }

    status = (*ctx->outputMixObject)->Realize(ctx->outputMixObject, SL_BOOLEAN_FALSE);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("outputMixObject->Realize() fails: 0x%X",status);
      goto end;
    }

    /* configure audio sink */
    loc_outmix.outputMix = ctx->outputMixObject;

    /* create audio player */
    status = (*engineInterface)->CreateAudioPlayer(engineInterface, &ctx->playerObject, &audioSrc, &audioSnk,
        sizeof(player_iids) / sizeof(player_iids[0]), player_iids, player_reqs);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("CreateAudioPlayer() fails: 0x%X",status);
      goto end;
    }

    status = (*ctx->playerObject)->GetInterface(ctx->playerObject, SL_IID_ANDROIDCONFIGURATION, &ctx->configInterface);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("GetInterface(SL_IID_ANDROIDCONFIGURATION) fails: 0x%X",status);
    }
    else {
      status = (*ctx->configInterface)->SetConfiguration(ctx->configInterface, SL_ANDROID_KEY_STREAM_TYPE,
          &audioStreamType, sizeof(audioStreamType));
      if ( status != SL_RESULT_SUCCESS ) {
        PERROR("player->SetConfiguration(SL_ANDROID_STREAM_VOICE) fails: 0x%X",status);
      }
    }

    status = (*ctx->playerObject)->Realize(ctx->playerObject, SL_BOOLEAN_FALSE);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("playerObject->Realize() fails: 0x%X",status);
      goto end;
    }

    status = (*ctx->playerObject)->GetInterface(ctx->playerObject, SL_IID_PLAY, &ctx->playerInterface);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("GetInterface(SL_IID_PLAY) fails: 0x%X",status);
      goto end;
    }

    status = (*ctx->playerObject)->GetInterface(ctx->playerObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
        &(ctx->bufferQueueInterface));
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("GetInterface(SL_IID_ANDROIDSIMPLEBUFFERQUEUE) fails: 0x%X",status);
      goto end;
    }

    /* register callback on the buffer queue */
    status = (*ctx->bufferQueueInterface)->RegisterCallback(ctx->bufferQueueInterface, bqPlayerCallback, ctx);
    if ( status != SL_RESULT_SUCCESS ) {
      PERROR("RegisterCallback(PlayerCallback) fails: 0x%X",status);
      goto end;
    }

    ctx->cb = callback;
    ctx->context = context;

    end : if ( status != SL_RESULT_SUCCESS ) {
      opensless_audio_playback_destroy(ctx);
      ctx = NULL;
    }
  }

  return ctx;
}

void opensless_audio_playback_destroy(opensless_audio_playback * ctx)
{
  if ( ctx ) {
    /** destroy audio player */
    if ( ctx->playerObject != NULL ) {
      (*ctx->playerObject)->Destroy(ctx->playerObject);
    }

    /* destroy output mix object */
    if ( ctx->outputMixObject != NULL ) {
      (*ctx->outputMixObject)->Destroy(ctx->outputMixObject);
    }

    if ( ctx->bq != NULL ) {
      bufrqueue_destroy(ctx->bq);
      pthread_mutex_destroy(&ctx->mutex);
    }

    free(ctx);
  }
}

int opensless_audio_playback_start(opensless_audio_playback * ctx)
{
  return (*ctx->playerInterface)->SetPlayState(ctx->playerInterface, SL_PLAYSTATE_PLAYING);
}

int opensless_audio_playback_stop(opensless_audio_playback * ctx)
{
  int status;

  status = (*ctx->playerInterface)->SetPlayState(ctx->playerInterface, SL_PLAYSTATE_STOPPED);
  (*ctx->bufferQueueInterface)->Clear(ctx->bufferQueueInterface);
  return status;
}

int opensless_audio_playback_enqueue(opensless_audio_playback * ctx, void * bufr, size_t size)
{
  return enqueue(ctx->bq, &ctx->mutex, ctx->bufferQueueInterface, bufr, size);
}

size_t opensless_audio_playback_queue_size(opensless_audio_playback * ctx)
{
  return get_queue_size(ctx->bq, &ctx->mutex);
}
