#ifndef _SYS_TIME_H_INCLUDED
#define _SYS_TIME_H_INCLUDED
#include <sys/types.h>
#include <time.h>
__BEGIN_DECLS
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval { long tv_sec; long tv_usec; };
#endif
struct timezone { int tz_minuteswest; int tz_dsttime; };
int gettimeofday(struct timeval *, void *);
int settimeofday(const struct timeval *, const struct timezone *);
__END_DECLS
#include <sys/select.h>
#endif
