#ifndef _DEVCTL_H_INCLUDED
#define _DEVCTL_H_INCLUDED
#include <_qnx_open.h>
#include <sys/neutrino.h>
__BEGIN_DECLS
int devctlv(int, int, int, int, const iov_t *, const iov_t *, int *);
__END_DECLS
#endif
