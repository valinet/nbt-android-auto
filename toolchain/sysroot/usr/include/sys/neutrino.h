#ifndef _SYS_NEUTRINO_H_INCLUDED
#define _SYS_NEUTRINO_H_INCLUDED
#include <sys/types.h>
typedef struct { void *iov_base; size_t iov_len; } iov_t;
#define SETIOV(iov, base, len) ((iov)->iov_base = (base), (iov)->iov_len = (len))
#endif
