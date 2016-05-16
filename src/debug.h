/*
 * debug.h
 *
 *  Created on: Oct 18, 2015
 *      Author: amyznikov
 */


#ifndef __ffplay_debug_h__
#define __ffplay_debug_h__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif


extern int g_log_level; // defined in debug.c

enum {
  LOG_NONE   = -1,
  LOG_EMERG   = 0, /* system is unusable */
  LOG_ALERT   = 1, /* action must be taken immediately */
  LOG_CRIT    = 2, /* critical conditions */
  LOG_ERR     = 3, /* error conditions */
  LOG_WARNING = 4, /* warning conditions */
  LOG_NOTICE  = 5, /* normal but significant condition */
  LOG_INFO    = 6, /* informational */
  LOG_DEBUG   = 7  /* debug-level messages */
};

void ffplay_plogv(const char * file, const char * function, int line, const char * format, va_list arglist);
void ffplay_plog(int pri, const char * file, const char * function, int line, const char * format, ...) __attribute__ ((__format__ (__printf__, 5, 6)));

# define PEMERG(...)    if(g_log_level>=LOG_EMERG)  { ffplay_plog(LOG_EMERG,   __FILE__, __FUNCTION__,__LINE__,__VA_ARGS__); }
# define PALERT(...)    if(g_log_level>=LOG_ALERT)  { ffplay_plog(LOG_ALERT,   __FILE__, __FUNCTION__,__LINE__,__VA_ARGS__); }
# define PCRITICAL(...) if(g_log_level>=LOG_CRIT)   { ffplay_plog(LOG_CRIT,    __FILE__, __FUNCTION__,__LINE__,__VA_ARGS__); }
# define PERROR(...)    if(g_log_level>=LOG_ERR)    { ffplay_plog(LOG_ERR,     __FILE__, __FUNCTION__,__LINE__,__VA_ARGS__); }
# define PWARNING(...)  if(g_log_level>=LOG_WARNING){ ffplay_plog(LOG_WARNING, __FILE__, __FUNCTION__,__LINE__,__VA_ARGS__); }
# define PNOTICE(...)   if(g_log_level>=LOG_NOTICE) { ffplay_plog(LOG_NOTICE,  __FILE__, __FUNCTION__,__LINE__,__VA_ARGS__); }
# define PINFO(...)     if(g_log_level>=LOG_INFO)   { ffplay_plog(LOG_INFO,    __FILE__, __FUNCTION__,__LINE__,__VA_ARGS__); }
# define PDEBUG(...)    if(g_log_level>=LOG_DEBUG)  { ffplay_plog(LOG_DEBUG,   __FILE__, __FUNCTION__,__LINE__,__VA_ARGS__); }

# define PDBG  PDEBUG




#ifdef __cplusplus
}
#endif

#endif /* __ffplay_debug_h__ */
