#ifndef _SYS_RESOURCE_H_INCLUDED
#define _SYS_RESOURCE_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
__BEGIN_DECLS
typedef unsigned long long rlim_t; /* 64-bit on QNX 6.5: struct rlimit is 16 bytes (checked against the SDP-built decode_runner) */
struct rlimit { rlim_t rlim_cur, rlim_max; };
#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_NOFILE 5
#define RLIMIT_AS 6
#define RLIM_INFINITY 0xffffffffffffffffull
int getrlimit(int, struct rlimit *);
int setrlimit(int, const struct rlimit *);
int getpriority(int, id_t);
int setpriority(int, id_t, int);
__END_DECLS
#endif
