/*
 * com_sis_ffplay_CameraPreview.c
 *
 *  Created on: May 15, 2016
 *      Author: amyznikov
 */
#include "ffplay-java-api.h"
#include "sendvideo.h"
#include "ffmpeg.h"
#include "debug.h"

#define UNUSED(x)       (void)(x)
#define UNUSED2(x,y)    (void)(x),(void)(y)


/*
 * java class mappings
 * */


/////////////////////////////////////////

struct CameraPreview {
  jobject obj;
  jmethodID onStreamStateChaged;
};


static struct CameraPreview * CameraPreview_init(JNIEnv * env, jobject obj) {

  struct CameraPreview * c = calloc(1, sizeof(struct CameraPreview ));
  if ( c ) {
    c->obj = NewGlobalRef(env, obj);
    c->onStreamStateChaged = GetObjectMethodID(env, obj, "onStreamStateChaged","(II)V");
  }
  return c;
}

static void CameraPreview_free(JNIEnv * env, struct CameraPreview * c) {

  if ( c ) {
    if ( c->obj ) {
      DeleteGlobalRef(env, c->obj);
    }
    free(c);
  }
}


/////////////////////////////////////////



static struct {
  jclass class_;
  jfieldID state;
  jfieldID inputFps, outputFps;
  jfieldID inputBitrate, outputBitrate;
  jfieldID framesRead, framesSent;
  jfieldID bytesRead, bytesSent;
} StreamStatus;


static bool StreamStatusClass_init(JNIEnv * env) {

  static const struct {
    const char * name;
    const char * signature;
    jfieldID * id;
  } fields[] = {
    { "state", "I",  &StreamStatus.state},
    { "inputFps", "D", &StreamStatus.inputFps},
    { "outputFps", "D",  &StreamStatus.outputFps},
    { "inputBitrate",  "I", &StreamStatus.inputBitrate},
    { "outputBitrate", "I", &StreamStatus.outputBitrate},
    { "framesRead",  "J", &StreamStatus.framesRead},
    { "framesSent",  "J", &StreamStatus.framesSent},
    { "bytesRead",  "J", &StreamStatus.bytesRead},
    { "bytesSent",  "J", &StreamStatus.bytesSent},
  };


  if ( !StreamStatus.class_ ) {
    if ( !(StreamStatus.class_ = NewGlobalRef(env, FindClass(env, "com/sis/ffplay/CameraPreview$StreamStatus"))) ) {
      return false;
    }
  }

  for ( size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i ) {
    *fields[i].id = GetFieldID(env, StreamStatus.class_, fields[i].name, fields[i].signature);
    if ( !*fields[i].id ) {
      return false;
    }
  }

  return true;
}

//static void StreamStatusClass_cleanup(JNIEnv * env)
//{
//  DeleteGlobalRef(env, StreamStatus.class_);
//  memset(&StreamStatus, 0, sizeof(StreamStatus));
//}


static void StreamStatus_set(JNIEnv * env, jobject obj, ff_output_stream_state state, const struct output_stream_stats * stats)
{
  SetIntField(env, obj, StreamStatus.state, state);

#define SET_STREAM_STATUS_INT_FIELD(f)  \
    SetIntField(env, obj, StreamStatus.f, stats->f)

#define SET_STREAM_STATUS_LONG_FIELD(f)  \
    SetLongField(env, obj, StreamStatus.f, stats->f)

#define SET_STREAM_STATUS_DOUBLE_FIELD(f)  \
    SetDoubleField(env, obj, StreamStatus.f, stats->f)

  SET_STREAM_STATUS_INT_FIELD(inputBitrate);
  SET_STREAM_STATUS_INT_FIELD(outputBitrate);

  SET_STREAM_STATUS_LONG_FIELD(framesRead);
  SET_STREAM_STATUS_LONG_FIELD(framesSent);
  SET_STREAM_STATUS_LONG_FIELD(bytesRead);
  SET_STREAM_STATUS_LONG_FIELD(bytesSent);


  SET_STREAM_STATUS_DOUBLE_FIELD(inputFps);
  SET_STREAM_STATUS_DOUBLE_FIELD(outputFps);

#undef SET_STREAM_STATUS_LONG_FIELD
#undef SET_STREAM_STATUS_INT_FIELD
#undef SET_STREAM_STATUS_DOUBLE_FIELD
}



static struct {
  jclass class_;
  jmethodID init;
  jfieldID QualityValues;
  jfieldID GopSizes;
  jfieldID BitRates;
} CodecOpts;


static bool CodecOptsClass_init(JNIEnv * env) {

  static const struct {
    const char * name;
    const char * signature;
    jfieldID * id;
  } fields[] = {
    { "QualityValues", "[I", &CodecOpts.QualityValues},
    { "GopSizes",  "[I", &CodecOpts.GopSizes},
    { "BitRates",  "[I", &CodecOpts.BitRates},
  };

  static const struct {
    const char * name;
    const char * signature;
    jmethodID * id;
  } methods[] = {
    { "<init>", "()V", &CodecOpts.init},
  };


  if ( !CodecOpts.class_ ) {
    if ( !(CodecOpts.class_ = NewGlobalRef(env, FindClass(env, "com/sis/ffplay/CameraPreview$CodecOpts"))) ) {
      return false;
    }
  }

  for ( size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i ) {
    if ( !*fields[i].id ) {
      *fields[i].id = GetFieldID(env, CodecOpts.class_, fields[i].name, fields[i].signature);
      if ( !*fields[i].id ) {
        return false;
      }
    }
  }

  for ( size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i ) {
    if ( !*methods[i].id ) {
      *methods[i].id = GetMethodID(env, CodecOpts.class_, methods[i].name, methods[i].signature);
      if ( !*methods[i].id ) {
        return false;
      }
    }
  }

  return true;
}

static struct {
  jclass class_;
  jfieldID server;
  jfieldID format;
  jfieldID ffopts;

  jfieldID vCodecName;
  jfieldID vQuality;
  jfieldID vGopSize;
  jfieldID vBitRate;
  jfieldID vBufferSize;

  jfieldID aCodecName;
  jfieldID aQuality;
  jfieldID aBitRate;
  jfieldID aBufferSize;

} StreamOpts;

static bool StreamOptsClass_init(JNIEnv * env)
{
  static const struct {
    const char * name;
    const char * signature;
    jfieldID * id;
  } fields[] = {
    { "server", "Ljava/lang/String;",  &StreamOpts.server},
    { "format", "Ljava/lang/String;",  &StreamOpts.format},
    { "ffopts", "Ljava/lang/String;",  &StreamOpts.ffopts},

    { "vCodecName", "Ljava/lang/String;",  &StreamOpts.vCodecName},
    { "vQuality", "I",  &StreamOpts.vQuality},
    { "vGopSize", "I",  &StreamOpts.vGopSize},
    { "vBitRate", "I",  &StreamOpts.vBitRate},
    { "vBufferSize", "I",  &StreamOpts.vBufferSize},

    { "aCodecName", "Ljava/lang/String;",  &StreamOpts.aCodecName},
    { "aQuality", "I",  &StreamOpts.aQuality},
    { "aBitRate", "I",  &StreamOpts.aBitRate},
    { "aBufferSize", "I",  &StreamOpts.aBufferSize},
  };

  if ( !StreamOpts.class_ ) {
    if ( !(StreamOpts.class_ = NewGlobalRef(env, FindClass(env, "com/sis/ffplay/CameraPreview$StreamOptions"))) ) {
      return false;
    }
  }

  for ( size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i ) {
    if ( !*fields[i].id ) {
      *fields[i].id = GetFieldID(env, StreamOpts.class_, fields[i].name, fields[i].signature);
      if ( !*fields[i].id ) {
        return false;
      }
    }
  }

  return true;
}

/////////////////////////////////////////

static void on_stream_state_changed(void * cookie, ff_output_stream * s, ff_output_stream_state state, int reason)
{
  (void)(s);
  struct CameraPreview * c = cookie;
  JNIEnv * env = NULL;
  if ( c->onStreamStateChaged && GetEnv(&env) == 0 ) {
    call_void_method_v(env, c->obj, c->onStreamStateChaged, state, reason);
  }
}



/*
 * Class:     com_sis_ffplay_CameraPreview
 * Method:    start_stream
 * Signature: (IIILcom/sis/ffplay/CameraPreview/StreamOptions;)J
 */
JNIEXPORT jlong JNICALL Java_com_sis_ffplay_CameraPreview_start_1stream(JNIEnv * env, jobject obj, jint cx, jint cy, jint pixfmt, jobject opts)
{
  ff_output_stream * ctx = NULL;
  struct CameraPreview * cookie = NULL;

  jstring server = NULL;
  const char * cserver = NULL;

  jstring format = NULL;
  const char * cformat = NULL;

  jstring ffopts = NULL;
  const char * cffopts = NULL;

  jstring vcodec = NULL;
  const char * cvcodec = NULL;

  jstring acodec = NULL;
  const char * cacodec = NULL;

  int vQuality = -1;
  int vGopSize = -1;
  int vBitRate = 0;
  int vBufferSize = 0;
  int aQuality = -1;
  int aBitRate = 0;
  int aBufferSize = 0;

  static const ff_output_stream_event_callback events_callback = {
    .stream_state_changed = on_stream_state_changed,
  };


  if ( !StreamOpts.class_ && !StreamOptsClass_init(env) ) {
    PDBG("StreamOptsClass_init() fails");
    goto end;
  }

  if ( !(cserver = cString(env, server = GetObjectField(env, opts, StreamOpts.server))) ) {
    PDBG("NO SERVER SPECIFIED");
    goto end;
  }

  cformat = cString(env, format = GetObjectField(env, opts, StreamOpts.format));
  cffopts = cString(env, ffopts = GetObjectField(env, opts, StreamOpts.ffopts));
  cvcodec = cString(env, vcodec = GetObjectField(env, opts, StreamOpts.vCodecName));
  cacodec = cString(env, acodec = GetObjectField(env, opts, StreamOpts.aCodecName));

  PDBG("cx=%d cy=%d pixfmt=%d server='%s' opts='%s'", cx, cy, pixfmt, cserver, cffopts);


  vQuality = GetIntField(env, opts, StreamOpts.vQuality);
  vBitRate = GetIntField(env, opts, StreamOpts.vBitRate);
  vBufferSize = GetIntField(env, opts, StreamOpts.vBufferSize);
  aQuality = GetIntField(env, opts, StreamOpts.aQuality);
  aBitRate = GetIntField(env, opts, StreamOpts.aBitRate);
  aBufferSize = GetIntField(env, opts, StreamOpts.aBufferSize);
  vGopSize = GetIntField(env, opts, StreamOpts.vGopSize);

  cookie = CameraPreview_init(env, obj);

  ctx = create_output_stream(&(struct create_output_stream_args ) {

        .server = cserver,

        .format = cformat,
        .ffopts = cffopts,

        .cvcodec = cvcodec,
        .cacodec = cacodec,

        .events_callback = &events_callback,
        .cookie = cookie,

        .cx = cx,
        .cy = cy,
        .pxfmt = AV_PIX_FMT_NV21,    // fixme: map actual camera format to ffmpeg!

        .cvquality = vQuality,
        .caquality = aQuality,

        .cvbitrate = vBitRate,
        .cabitrate = aBitRate,

        .cvbufs = vBufferSize,
        .cabufs = aBufferSize,

        .gopsize = vGopSize,
      });


  if ( ctx && !start_output_stream(ctx) ) {
    PDBG("start_output_stream() fails: %s", strerror(errno));
    CameraPreview_free(env, cookie);
    destroy_output_stream(ctx);
    ctx = NULL;
  }

end:

  freeCString(env, server, cserver);
  freeCString(env, format, cformat);
  freeCString(env, ffopts, cffopts);
  freeCString(env, vcodec, cvcodec);
  freeCString(env, acodec, cacodec);

  return (jlong) (ssize_t) (ctx);
}

/*
 * Class:     com_sis_ffplay_CameraPreview
 * Method:    stop_stream
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_sis_ffplay_CameraPreview_stop_1stream(JNIEnv * env, jobject obj, jlong handle)
{
  (void)(obj);

  ff_output_stream * ctx = (ff_output_stream *) (ssize_t) (handle);

  if ( ctx ) {
    stop_output_stream(ctx);
    CameraPreview_free(env, get_output_stream_cookie(ctx));
    destroy_output_stream(ctx);
  }
}

/*
 * Class:     com_sis_ffplay_CameraPreview
 * Method:    send_video_frame
 * Signature: (J[B)Z
 */
JNIEXPORT jboolean JNICALL Java_com_sis_ffplay_CameraPreview_send_1video_1frame(JNIEnv * env, jobject obj, jlong handle,
    jbyteArray frame)
{
  (void)(obj);

  ff_output_stream * ctx;
  struct frm * frm = NULL;
  jboolean status = JNI_TRUE;

  if ( !(ctx = (ff_output_stream *) (ssize_t) (handle)) ) {
    status = JNI_FALSE;
  }
  else if ( (frm = pop_video_frame(ctx)) ) {
    (*env)->GetByteArrayRegion(env, frame, 0, get_video_frame_data_size(ctx), (jbyte*)frm->data);
    push_video_frame(ctx, frm);
  }

  return status;
}


JNIEXPORT jboolean JNICALL Java_com_sis_ffplay_CameraPreview_get_1stream_1status(JNIEnv * env, jclass cls, jlong shandle,
    jobject stats)
{
  UNUSED(cls);

  ff_output_stream * ctx;
  jboolean fok = JNI_FALSE;

  if ( (ctx = (ff_output_stream *) (ssize_t) (shandle)) && (StreamStatus.class_ || StreamStatusClass_init(env)) ) {
    StreamStatus_set(env, stats, get_output_stream_state(ctx), get_output_stream_stats(ctx));
    fok = JNI_TRUE;
  }

  return fok;
}


/*
 * Class:     com_sis_ffplay_CameraPreview
 * Method:    geterrmsg
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_sis_ffplay_CameraPreview_geterrmsg(JNIEnv * env, jclass cls, jint status)
{
  UNUSED(cls);

  jstring errmsg;

  if ( status >= 0 ) {
    errmsg = jString(env, strerror(status));
  }
  else {
    errmsg = jString(env, av_err2str(status));
  }

  return errmsg;
}



/*
 * Class:     com_sis_ffplay_CameraPreview
 * Method:    get_supported_stream_formats
 * Signature: ()[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_com_sis_ffplay_CameraPreview_get_1supported_1stream_1formats(JNIEnv * env, jclass cls)
{
  UNUSED(cls);

  jobjectArray formats = NULL;

  static const char * cformats[] = {
    "matroska",
    "asf",
    "mjpeg",
    "avi",
    "flv",
    "ffm",
  };

  const size_t count = sizeof(cformats)/sizeof(cformats[0]);

  if ( (formats = NewStringArray(env, count)) ) {
    for ( size_t i = 0; i < count; ++i ) {
      SetObjectArrayElement(env, formats, i, jString(env, cformats[i]) );
    }
  }

  return formats;
}



static jobjectArray getSupportedCodecs(JNIEnv * env, enum codec_type type)
{
  const struct codec_opts * copts = NULL;
  jobjectArray codecNames = NULL;
  int n = 0;

  if ( (copts = get_supported_codecs()) ) {

    for ( int i = 0; copts[i].name != NULL; ++i ) {
      if ( copts[i].type == type ) {
        ++n;
      }
    }

    if ( (codecNames = NewStringArray(env, n)) ) {
      for ( int i = 0, n = 0; copts[i].name != NULL; ++i ) {
        if ( copts[i].type == type ) {
          SetObjectArrayElement(env, codecNames, n++, jString(env, copts[i].name));
        }
      }
    }
  }

  return codecNames;
}

/*
 * Class:     com_sis_ffplay_CameraPreview
 * Method:    get_supported_video_codecs
 * Signature: ()[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_com_sis_ffplay_CameraPreview_get_1supported_1video_1codecs(JNIEnv * env, jclass cls)
{
  UNUSED(cls);
  return getSupportedCodecs(env, codec_type_video);
}

/*
 * Class:     com_sis_ffplay_CameraPreview
 * Method:    get_supported_audio_codecs
 * Signature: ()[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_com_sis_ffplay_CameraPreview_get_1supported_1audio_1codecs(JNIEnv * env, jclass cls)
{
  UNUSED(cls);
  return getSupportedCodecs(env, codec_type_audio);
}


static jobjectArray create_java_integer_array(JNIEnv * env, const int values[])
{
  jobjectArray arr = NULL;
  int n = 0;

  while ( values[n] != -1 ) {
    ++n;
  }

  if ( (arr = NewIntArray(env, n)) ) {
    SetIntArrayRegion(env,arr, 0, n, values);
  }

  return arr;
}

/*
 * Class:     com_sis_ffplay_CameraPreview
 * Method:    get_supported_codec_options
 * Signature: (Ljava/lang/String;)Lcom/sis/ffplay/CameraPreview/CodecOpts;
 */
JNIEXPORT jobject JNICALL Java_com_sis_ffplay_CameraPreview_get_1supported_1codec_1options(JNIEnv * env, jclass cls, jstring codecName)
{
  UNUSED(cls);
  jobject obj = NULL;
  const struct codec_opts * copts = NULL;
  const char * cname = NULL;

  if ( !(cname = cString(env, codecName)) ) {
    goto end;
  }

  copts = get_supported_codecs();

  while ( copts->name && strcmp(copts->name, cname) != 0 ) {
    ++copts;
  }

  if ( !copts->name ) {
    goto end;
  }

  if ( !CodecOpts.class_ && !CodecOptsClass_init(env) ) {
    goto end;
  }


  if ( !(obj = NewObject(env, CodecOpts.class_, CodecOpts.init)) ) {
    goto end;
  }


  if ( copts->supported_quality_values ) {
    SetObjectField(env, obj, CodecOpts.QualityValues, create_java_integer_array(env, copts->supported_quality_values));
  }


  if ( copts->supported_gop_sizes ) {
    SetObjectField(env, obj, CodecOpts.GopSizes, create_java_integer_array(env, copts->supported_gop_sizes));
  }

  if ( copts->supported_bitrates ) {
    SetObjectField(env, obj, CodecOpts.BitRates, create_java_integer_array(env, copts->supported_bitrates));
  }


end:

  freeCString(env, codecName, cname);

  return obj;
}

