/*
 * debug.c
 *
 *  Created on: Oct 18, 2015
 *      Author: amyznikov
 */


#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
# include <android/log.h>

#define UNUSED(x)  (void)(x)

#define TAG "libffplay"


int g_log_level = LOG_DEBUG;


static inline const struct tm * gettime() {
  time_t t = time(0);
  return localtime(&t);
}

static void do_log(int pri, const char * file, const char * function, int line, const char * format, va_list arglist)
{
  UNUSED(file);
  UNUSED(pri);

  const struct tm * t = gettime();
  const int hour = t->tm_hour;
  const int minute = t->tm_min;
  const int sec = t->tm_sec;
  int n;

  char msg[8 * 1024];

  n = snprintf(msg, sizeof(msg) - 1, "[%6d] %.2d:%.2d:%.2d %-24s %4d : ", gettid(), hour, minute, sec, function, line);
  if ( n > 0 && n < (int) (sizeof(msg) - 1) ) {
    n += vsnprintf(msg + n, sizeof(msg) - n - 1, format, arglist);
  }
  msg[n > 0 ? n : 0] = 0;

  __android_log_write(ANDROID_LOG_DEBUG, TAG, msg);
}



void ffplay_plog(int pri, const char * file, const char * function, int line, const char * format, ...)
{
  va_list arglist;
  va_start(arglist, format);

  do_log(pri, file, function, line, format, arglist);

  va_end(arglist);
}

void ffplay_plogv(const char * file, const char * function, int line, const char * format, va_list arglist)
{
  do_log(LOG_DEBUG, file, function, line, format, arglist);
}
