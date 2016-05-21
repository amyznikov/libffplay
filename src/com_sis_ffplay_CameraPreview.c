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
    { "inputFps", "I", &StreamStatus.inputFps},
    { "outputFps", "I",  &StreamStatus.outputFps},
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

  SET_STREAM_STATUS_INT_FIELD(inputFps);
  SET_STREAM_STATUS_INT_FIELD(outputFps);
  SET_STREAM_STATUS_INT_FIELD(inputBitrate);
  SET_STREAM_STATUS_INT_FIELD(outputBitrate);

  SET_STREAM_STATUS_LONG_FIELD(framesRead);
  SET_STREAM_STATUS_LONG_FIELD(framesSent);
  SET_STREAM_STATUS_LONG_FIELD(bytesRead);
  SET_STREAM_STATUS_LONG_FIELD(bytesSent);


#undef SET_STREAM_STATUS_LONG_FIELD
#undef SET_STREAM_STATUS_INT_FIELD
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
 * Signature: (IIILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;III)J
 */
JNIEXPORT jlong JNICALL Java_com_sis_ffplay_CameraPreview_start_1stream(JNIEnv * env, jobject obj, jint cx, jint cy, jint pixfmt,
    jstring server, jstring format, jstring ffopts, jstring vcodec, jstring acodec, jint vquality, jint aquality, jint gopsize)
{
  ff_output_stream * ctx = NULL;
  struct CameraPreview * cookie = NULL;

  const char * cserver = NULL;
  const char * cformat = NULL;
  const char * cffopts = NULL;

  const char * cvcodec = NULL;
  const char * cacodec = NULL;

  static const ff_output_stream_event_callback events_callback = {
    .stream_state_changed = on_stream_state_changed,
  };

  cserver = cString(env, server);
  cformat = cString(env, format);
  cffopts = cString(env, ffopts);
  cvcodec = cString(env, vcodec);
  cacodec = cString(env, acodec);

  if ( !cserver) {
    PDBG("NO SERVER SPECIFIED");
    goto end;
  }

  PDBG("cx=%d cy=%d pixfmt=%d server='%s' opts='%s'", cx, cy, pixfmt, cserver, cffopts);

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
        .pxfmt = AV_PIX_FMT_NV21, // fixme: map actual camera format to ffmpeg!
        .cvquality = vquality,
        .caquality = aquality,
        .gopsize = gopsize,
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
  UNUSED(env);
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
