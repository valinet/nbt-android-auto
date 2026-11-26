#ifndef _PTHREAD_H_INCLUDED
#define _PTHREAD_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
#include <time.h>
#include <sched.h>
__BEGIN_DECLS
#define _NTO_SYNC_NONRECURSIVE 0x80000000
#define _NTO_SYNC_INITIALIZER  0xffffffff
#define _NTO_SYNC_COND         0xfffffffd
#define PTHREAD_MUTEX_INITIALIZER { (int)_NTO_SYNC_NONRECURSIVE, (unsigned)_NTO_SYNC_INITIALIZER }
#define PTHREAD_COND_INITIALIZER  { (int)_NTO_SYNC_COND, (unsigned)_NTO_SYNC_INITIALIZER }
#define PTHREAD_ONCE_INIT { 0, PTHREAD_MUTEX_INITIALIZER }
#define PTHREAD_CREATE_JOINABLE 0x00
#define PTHREAD_CREATE_DETACHED 0x01
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCELED ((void *)-1)
int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
int pthread_detach(pthread_t);
pthread_t pthread_self(void);
int pthread_equal(pthread_t, pthread_t);
void pthread_exit(void *) __attribute__((noreturn));
int pthread_cancel(pthread_t);
int pthread_once(pthread_once_t *, void (*)(void));
int pthread_atfork(void (*)(void), void (*)(void), void (*)(void));
int pthread_attr_init(pthread_attr_t *);
int pthread_attr_destroy(pthread_attr_t *);
int pthread_attr_setdetachstate(pthread_attr_t *, int);
int pthread_attr_getdetachstate(const pthread_attr_t *, int *);
int pthread_attr_setstacksize(pthread_attr_t *, size_t);
int pthread_attr_setschedparam(pthread_attr_t *, const struct sched_param *);
int pthread_attr_setschedpolicy(pthread_attr_t *, int);
int pthread_attr_setinheritsched(pthread_attr_t *, int);
int pthread_setschedparam(pthread_t, int, const struct sched_param *);
int pthread_getschedparam(pthread_t, int *, struct sched_param *);
int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_trylock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_mutexattr_init(pthread_mutexattr_t *);
int pthread_mutexattr_destroy(pthread_mutexattr_t *);
int pthread_mutexattr_settype(pthread_mutexattr_t *, int);
int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int pthread_cond_destroy(pthread_cond_t *);
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int pthread_cond_signal(pthread_cond_t *);
int pthread_cond_broadcast(pthread_cond_t *);
int pthread_condattr_init(pthread_condattr_t *);
int pthread_condattr_destroy(pthread_condattr_t *);
int pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
int pthread_key_create(pthread_key_t *, void (*)(void *));
int pthread_key_delete(pthread_key_t);
void *pthread_getspecific(pthread_key_t);
int pthread_setspecific(pthread_key_t, const void *);
int pthread_setcancelstate(int, int *);
__END_DECLS
#endif
