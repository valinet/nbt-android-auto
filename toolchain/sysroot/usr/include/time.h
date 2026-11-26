#ifndef _TIME_H_INCLUDED
#define _TIME_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
__BEGIN_DECLS
#define CLOCKS_PER_SEC 1000
#define CLOCK_REALTIME 0
#define CLOCK_SOFTTIME 1
#define CLOCK_MONOTONIC 2
#define TIMER_ABSTIME 0x80000000
#ifndef _TIMESPEC_DEFINED
#define _TIMESPEC_DEFINED
struct timespec { time_t tv_sec; long tv_nsec; };
#endif
struct tm {
  int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
  long tm_gmtoff;
  const char *tm_zone;
};
struct itimerspec { struct timespec it_value, it_interval; };
extern char *tzname[2];
extern long timezone;
extern int daylight;
clock_t clock(void);
double difftime(time_t, time_t);
time_t mktime(struct tm *);
time_t time(time_t *);
char *asctime(const struct tm *);
char *asctime_r(const struct tm *, char *);
char *ctime(const time_t *);
char *ctime_r(const time_t *, char *);
struct tm *gmtime(const time_t *);
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime(const time_t *);
struct tm *localtime_r(const time_t *, struct tm *);
size_t strftime(char *, size_t, const char *, const struct tm *);
char *strptime(const char *, const char *, struct tm *);
void tzset(void);
int clock_gettime(clockid_t, struct timespec *);
int clock_settime(clockid_t, const struct timespec *);
int clock_getres(clockid_t, struct timespec *);
int nanosleep(const struct timespec *, struct timespec *);
__END_DECLS
#endif
