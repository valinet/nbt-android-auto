#ifndef _SCHED_H_INCLUDED
#define _SCHED_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
__BEGIN_DECLS
#define SCHED_FIFO 1
#define SCHED_RR 2
#define SCHED_OTHER 3
struct sched_param {
  _Int32t sched_priority;
  _Int32t sched_curpriority;
  _Int32t __reserved[8];
};
int sched_yield(void);
int sched_get_priority_max(int);
int sched_get_priority_min(int);
__END_DECLS
#endif
