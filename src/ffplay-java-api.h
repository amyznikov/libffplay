/*
 * cclib-java-api.h
 *
 *  Created on: May 6, 2016
 *      Author: amyznikov
 */

#pragma once

#ifndef __cclib_java_api_h___
#define __cclib_java_api_h___

#include <stddef.h>
#include <stdbool.h>
#include "com_sis_ffplay_CameraPreview.h"

#ifdef __cplusplus
extern "C" {
#endif



bool java_attach_current_thread(JNIEnv ** env);
void java_deatach_current_thread(void);


int GetEnv(JNIEnv ** env);

jobject NewLocalRef(JNIEnv * env, jobject obj);
void DeleteLocalRef(JNIEnv * env, jobject obj);

jobject NewGlobalRef(JNIEnv * env, jobject obj);
void DeleteGlobalRef(JNIEnv * env, jobject obj);


jobject NewObject(JNIEnv*, jclass, jmethodID, ...);
jobject NewObjectV(JNIEnv*, jclass, jmethodID, va_list);

jobjectArray NewObjectArray(JNIEnv*, jsize, jclass, jobject);
void  SetObjectArrayElement(JNIEnv*, jobjectArray, jsize, jobject);

jstring jString(JNIEnv * env, const char * cstring);
const char * cString(JNIEnv * env, jstring jstr);
void freeCString(JNIEnv * env, jstring jstr, const char * cstr);
char * cStringDup(JNIEnv * env, jstring jstr);


jclass  FindClass(JNIEnv * env, const char * className);
jclass  GetObjectClass(JNIEnv* env, jobject obj);

jmethodID GetMethodID(JNIEnv * env, jclass cls, const char * name, const char * signature);
jmethodID GetObjectMethodID(JNIEnv * env, jobject obj, const char * name, const char * signature);

jfieldID GetFieldID(JNIEnv * env, jclass cls, const char * name, const char * signature);


jint  GetIntField(JNIEnv * env, jobject obj, jfieldID id);
void SetIntField(JNIEnv* env, jobject obj, jfieldID id, jint v);
jlong GetLongField(JNIEnv* env, jobject obj, jfieldID id);
void SetLongField(JNIEnv* env, jobject obj, jfieldID id, jlong v);
jobject GetObjectField(JNIEnv * env, jobject obj, jfieldID id);
void SetObjectField(JNIEnv * env, jobject obj, jfieldID id, jobject v);


void call_void_method(JNIEnv * env, jobject obj, jmethodID method);
void call_void_method_v(JNIEnv * env, jobject obj, jmethodID method, ...);
jint call_int_method(JNIEnv * env, jobject obj, jmethodID method);
jint call_int_method_v(JNIEnv * env, jobject obj, jmethodID method, ...);
jlong call_long_method(JNIEnv * env, jobject obj, jmethodID method);
jlong call_long_method_v(JNIEnv * env, jobject obj, jmethodID method, ...);
jboolean call_boolean_method(JNIEnv * env, jobject obj, jmethodID method);
jboolean call_boolean_method_v(JNIEnv * env, jobject obj, jmethodID method, ...);
jobject call_object_method(JNIEnv * env, jobject obj, jmethodID method);
jobject call_object_method_v(JNIEnv * env, jobject obj, jmethodID method, ...);




#ifdef __cplusplus
}
#endif

#endif /* __cclib_java_api_h___ */
