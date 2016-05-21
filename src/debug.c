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


static void do_log(int pri, const char * file, const char * function, int line, const char * format, va_list arglist)
{
  UNUSED(file);
  UNUSED(pri);

  struct timespec t = {
    .tv_sec = 0,
    .tv_nsec = 0
  };

  clock_gettime(CLOCK_REALTIME, &t);

  int day = t.tv_sec / (24 * 3600);
  int hour = (t.tv_sec - day * (24 * 3600)) / 3600;
  int min = (t.tv_sec - day * (24 * 3600) - hour * 3600) / 60;
  int sec = (t.tv_sec - day * (24 * 3600) - hour * 3600 - min * 60);
  int msec = t.tv_nsec / 1000000;

  char msg[8 * 1024];
  int n;

  n = snprintf(msg, sizeof(msg) - 1, "[%6d] %.2d:%.2d:%.2d.%.3d %-24s %4d : ", gettid(), hour, min, sec, msec, function, line);
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
