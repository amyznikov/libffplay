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


// java class mapping
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
 * Signature: (IIILjava/lang/String;Ljava/lang/String;Ljava/lang/String;IIILjava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_com_sis_ffplay_CameraPreview_start_1stream(JNIEnv * env, jobject obj, jint cx, jint cy,
    jint pixfmt, jstring server, jstring format, jstring codec, jint quality, jint gopsize, jint bitrate,
    jstring ffopts)
{
  ff_output_stream * ctx = NULL;
  struct CameraPreview * cookie = NULL;

  const char * cformat = NULL;
  const char * cserver = NULL;

  const char * ccodec = NULL;
  const char * cffopts = NULL;

  static const ff_output_stream_event_callback events_callback = {
    .stream_state_changed = on_stream_state_changed,
  };

  cformat = cString(env, format);
  cserver = cString(env, server);
  ccodec = cString(env, codec);
  cffopts = cString(env, ffopts);

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
        .codec = ccodec,
        .events_callback = &events_callback,
        .cookie = cookie,
        .cx = cx,
        .cy = cy,
        .pxfmt = AV_PIX_FMT_NV21,
        .quality = quality,
        .gopsize = gopsize,
        .bitrate = bitrate,
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
  freeCString(env, codec, ccodec);
  freeCString(env, ffopts, cffopts);

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
  else if ( (frm = pop_output_frame(ctx)) ) {
    frm->pts = ffmpeg_gettime_ms();
    (*env)->GetByteArrayRegion(env, frame, 0, get_output_frame_data_size(ctx), (jbyte*)frm->data);
    push_output_frame(ctx, frm);
  }

  return status;
}
