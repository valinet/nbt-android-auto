#ifndef _SYS_SELECT_H_INCLUDED
#define _SYS_SELECT_H_INCLUDED
#include <sys/types.h>
#include <string.h>
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval { long tv_sec; long tv_usec; };
#endif
__BEGIN_DECLS
#define FD_SETSIZE 256
typedef struct fd_set { long fds_bits[FD_SETSIZE / 32]; } fd_set;
#define FD_SET(fd, p)   ((p)->fds_bits[(fd) >> 5] |= (1L << ((fd) & 31)))
#define FD_CLR(fd, p)   ((p)->fds_bits[(fd) >> 5] &= ~(1L << ((fd) & 31)))
#define FD_ISSET(fd, p) (((p)->fds_bits[(fd) >> 5] & (1L << ((fd) & 31))) != 0)
#define FD_ZERO(p)      memset((p), 0, sizeof(*(p)))
int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
__END_DECLS
#endif
