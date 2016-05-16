/*
 * pthread_wait.h
 *
 *  Created on: Dec 15, 2011
 *      Author: amyznikov
 */

#ifndef __pthread_wait_h__
#define __pthread_wait_h__

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef
struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int is_waiting;
} pthread_wait_t;


#define PTHREAD_WAIT_INITIALIZER \
  {PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0}

static inline int pthread_wait_init(pthread_wait_t * wait)
{
  int status;

  wait->is_waiting = 0;

  if ( (status = pthread_mutex_init(&wait->mutex, 0)) == 0 ) {
    if ( (status = pthread_cond_init(&wait->cond, 0)) ) {
      pthread_mutex_destroy(&wait->mutex);
    }
  }

  return status;
}

static inline int pthread_wait_destroy(pthread_wait_t * wait)
{
  int status;

  if ( wait->is_waiting ) {
    status = EBUSY;
  }
  else if ( (status = pthread_cond_destroy(&wait->cond)) == 0 ) {
    status = pthread_mutex_destroy(&wait->mutex);
  }

  return status;
}

static inline int pthread_wait_lock(pthread_wait_t * wait)
{
  return pthread_mutex_lock(&wait->mutex);
}

static inline int pthread_wait_trylock(pthread_wait_t * wait)
{
  return pthread_mutex_trylock(&wait->mutex);
}

static inline int pthread_wait_unlock(pthread_wait_t * wait)
{
  return pthread_mutex_unlock(&wait->mutex);
}

static inline int pthread_wait_signal(pthread_wait_t * wait)
{
  return wait->is_waiting ? pthread_cond_signal(&wait->cond) : 0;
}

static inline int pthread_wait_broadcast(pthread_wait_t * wait)
{
  return pthread_cond_broadcast(&wait->cond);
}

static inline int pthread_wait(pthread_wait_t * wait, int tmout)
{
  int status;

  wait->is_waiting = 1;

  if ( tmout < 0 ) {
    status = pthread_cond_wait(&wait->cond, &wait->mutex);
  }
  else {
    struct timeval now;
    struct timespec timeout;

    const long sec = tmout / 1000;
    const long msec = tmout - sec * 1000;

    if ( gettimeofday(&now, NULL) != 0 ) {
      status = errno;
    }
    else {
      timeout.tv_sec = now.tv_sec + sec;
      timeout.tv_nsec = now.tv_usec * 1000 + msec * 1000000;

      if ( timeout.tv_nsec >= 1000000000 ) {
        ++timeout.tv_sec;
        timeout.tv_nsec -= 1000000000;
      }

      status = pthread_cond_timedwait(&wait->cond, &wait->mutex, &timeout);
    }
  }

  wait->is_waiting = 0;

  return status;
}


#ifdef __cplusplus
}
#endif

#endif /* __pthread_wait_h__ */
