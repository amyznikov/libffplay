/*
 * cclib-java-api.c
 *
 *  Created on: May 6, 2016
 *      Author: amyznikov
 */

#include "ffplay-java-api.h"
#include "com_sis_ffplay_CameraPreview.h"
#include "ffmpeg.h"
#include "debug.h"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>


#define UNUSED(x)       (void)(x)
#define UNUSED2(x,y)    (void)(x),(void)(y)


static JavaVM * g_jvm = NULL;



int GetEnv(JNIEnv ** env) {
  return (*g_jvm)->GetEnv(g_jvm, (void**) env, JNI_VERSION_1_6);
}


bool java_attach_current_thread(JNIEnv ** env)
{
  int status;

  if ( (status = GetEnv(env)) == JNI_EDETACHED ) {
    status =  (*g_jvm)->AttachCurrentThread(g_jvm, env, NULL);
  }

  return status;
}

void java_deatach_current_thread(void)
{
  (*g_jvm)->DetachCurrentThread(g_jvm);
}


jobject NewGlobalRef(JNIEnv * env, jobject obj)
{
  return (*env)->NewGlobalRef(env, obj);
}

void DeleteGlobalRef(JNIEnv * env, jobject obj)
{
  if ( obj ) {
    (*env)->DeleteGlobalRef(env, obj);
  }
}

jobject NewLocalRef(JNIEnv * env, jobject obj)
{
  return (*env)->NewLocalRef(env, obj);
}

void DeleteLocalRef(JNIEnv * env, jobject obj)
{
  if ( obj ) {
    (*env)->DeleteLocalRef(env, obj);
  }
}



jobject NewObject(JNIEnv * env, jclass cls, jmethodID mid, ...)
{
  va_list arglist;
  jobject obj;

  va_start(arglist, mid);
  obj = (*env)->NewObjectV(env, cls, mid, arglist);
  va_end(arglist);

  return obj;
}


jobject NewObjectV(JNIEnv * env, jclass cls, jmethodID mid, va_list arglist)
{
  return (*env)->NewObjectV(env, cls, mid, arglist);
}


jobjectArray NewObjectArray(JNIEnv* env, jsize size, jclass cls, jobject v)
{
  return (*env)->NewObjectArray(env, size, cls, v);
}

void SetObjectArrayElement(JNIEnv* env, jobjectArray array, jsize index, jobject v)
{
  return (*env)->SetObjectArrayElement(env, array, index, v);
}

jstring jString(JNIEnv * env, const char * cstring)
{
  return cstring ? (*env)->NewStringUTF(env, cstring) : NULL;
}

const char * cString(JNIEnv * env, jstring jstr)
{
  return jstr ? (*env)->GetStringUTFChars(env, jstr, NULL) : NULL;
}

void freeCString(JNIEnv * env, jstring jstr, const char * cstr)
{
  if ( jstr && cstr ) {
    (*env)->ReleaseStringUTFChars(env, jstr, cstr);
  }
}

char * cStringDup(JNIEnv * env, jstring jstr)
{
  const char * cstring = cString(env, jstr);
  char * dup = strdup(cstring);
  freeCString(env, jstr, cstring);
  return dup;
}


jclass FindClass(JNIEnv * env, const char * className)
{
  return (*env)->FindClass(env, className);
}

jclass GetObjectClass(JNIEnv* env, jobject obj)
{
  return (*env)->GetObjectClass(env, obj);
}

jmethodID GetMethodID(JNIEnv * env, jclass cls, const char * name, const char * signature)
{
  return (*env)->GetMethodID(env, cls, name, signature);
}

jmethodID GetObjectMethodID(JNIEnv * env, jobject obj, const char * name, const char * signature)
{
  return (*env)->GetMethodID(env, GetObjectClass(env, obj), name, signature);
}


jfieldID GetFieldID(JNIEnv * env, jclass cls, const char * name, const char * signature)
{
  return (*env )->GetFieldID(env, cls, name, signature);
}


jint GetIntField(JNIEnv * env, jobject obj, jfieldID f)
{
  return (*env)->GetIntField(env, obj, f);
}

void SetIntField(JNIEnv* env, jobject obj, jfieldID id, jint v)
{
  (*env)->SetIntField(env, obj, id, v);
}

jobject GetObjectField(JNIEnv * env, jobject obj, jfieldID f)
{
  return (*env)->GetObjectField(env, obj, f);
}

void SetObjectField(JNIEnv * env, jobject obj, jfieldID f, jobject v)
{
  (*env)->SetObjectField(env, obj, f, v);
}

void call_void_method(JNIEnv * env, jobject obj, jmethodID method)
{
  (*env)->CallVoidMethod(env, obj, method);
}

void call_void_method_v(JNIEnv * env, jobject obj, jmethodID method, ...)
{
  va_list arglist;
  va_start(arglist, method);
  (*env)->CallVoidMethodV(env, obj, method, arglist);
  va_end(arglist);
}

jint call_int_method(JNIEnv * env, jobject obj, jmethodID method)
{
  return (*env)->CallIntMethod(env, obj, method);
}

jint call_int_method_v(JNIEnv * env, jobject obj, jmethodID method, ...)
{
  va_list arglist;
  jint  status;
  va_start(arglist, method);
  status = (*env)->CallIntMethodV(env, obj, method, arglist);
  va_end(arglist);
  return status;
}

jlong call_long_method(JNIEnv * env, jobject obj, jmethodID method)
{
  return (*env)->CallLongMethod(env, obj, method);
}

jlong call_long_method_v(JNIEnv * env, jobject obj, jmethodID method, ...)
{
  va_list arglist;
  jlong status;
  va_start(arglist, method);
  status = (*env)->CallLongMethodV(env, obj, method, arglist);
  va_end(arglist);
  return status;
}


jboolean call_boolean_method(JNIEnv * env, jobject obj, jmethodID method)
{
  return (*env)->CallBooleanMethod(env, obj, method);
}

jboolean call_boolean_method_v(JNIEnv * env, jobject obj, jmethodID method, ...)
{
  va_list arglist;
  jboolean status;
  va_start(arglist, method);
  status = (*env)->CallBooleanMethodV(env, obj, method, arglist);
  va_end(arglist);
  return status;
}

jobject call_object_method(JNIEnv * env, jobject obj, jmethodID method)
{
  return (*env)->CallObjectMethod(env, obj, method);
}

jobject call_object_method_v(JNIEnv * env, jobject obj, jmethodID method, ...)
{
  va_list arglist;
  jobject object;
  va_start(arglist, method);
  object = (*env)->CallObjectMethodV(env, obj, method, arglist);
  va_end(arglist);
  return object;
}


///////////////////////////////////////////////////////////////////////////////////////////////


//AV_LOG_DEBUG
static int str2avll(const char * str)
{
  int ll = AV_LOG_WARNING;

  static const struct {
    const char * s;
    int ll;
  } avlls[] = {
    {"quiet", AV_LOG_QUIET},
    {"panic", AV_LOG_PANIC},
    {"fatal", AV_LOG_FATAL},
    {"error", AV_LOG_ERROR},
    {"warning", AV_LOG_WARNING},
    {"info", AV_LOG_INFO},
    {"verbose", AV_LOG_VERBOSE},
    {"debug", AV_LOG_DEBUG},
    {"trace", AV_LOG_TRACE},
  };

  if ( str ) {
    for ( uint i = 0; i < sizeof(avlls)/sizeof(avlls[0]); ++i ) {
      if ( strcasecmp(avlls[i].s, str) == 0 ) {
        ll = avlls[i].ll;
        break;
      }
    }
  }

  return ll;
}

static void av_log_callback(void *avcl, int level, const char *fmt, va_list arglist)
{
  if ( level <= av_log_get_level() ) {
    AVClass * avc = avcl ? *(AVClass **) avcl : NULL;
    ffplay_plogv(avc ? avc->item_name(avcl) : "avc", "ffmpeg", 0, fmt, arglist);
  }
}


/** JNI OnLoad */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM * jvm, void * reserved)
{
  UNUSED(reserved);

  g_jvm = jvm;

  av_log_set_level(str2avll("warning"));
  av_log_set_callback(av_log_callback);
  av_register_all();
  avformat_network_init();

  return JNI_VERSION_1_6;
}

