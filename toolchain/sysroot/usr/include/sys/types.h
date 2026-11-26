#ifndef _SYS_TYPES_H_INCLUDED
#define _SYS_TYPES_H_INCLUDED
#include <_qnx_open.h>
#include <stddef.h>
typedef int ssize_t;
typedef _Int32t off_t;
typedef _Int64t off64_t;
typedef _Int32t pid_t;
typedef _Int32t uid_t;
typedef _Int32t gid_t;
typedef _Uint32t mode_t;
typedef _Uint32t dev_t;
typedef _Uint32t ino_t;
typedef _Uint64t ino64_t;
typedef _Uint32t nlink_t;
typedef _Int32t blksize_t;
typedef _Int32t blkcnt_t;
typedef _Int64t blkcnt64_t;
typedef unsigned long time_t;     /* unsigned on QNX */
typedef unsigned long clock_t;
typedef int clockid_t;
typedef int timer_t;
typedef long suseconds_t;
typedef unsigned long useconds_t;
typedef int key_t;
typedef int id_t;
typedef char *caddr_t;
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef unsigned char uchar_t;
typedef unsigned short ushort_t;
typedef unsigned int uint_t;
typedef unsigned long ulong_t;
typedef _Uint8t u_int8_t;
typedef _Uint16t u_int16_t;
typedef _Uint32t u_int32_t;
typedef _Uint64t u_int64_t;
typedef _Int32t pthread_t;
typedef struct _sync { int __count; unsigned __owner; } sync_t;
typedef sync_t pthread_mutex_t;
typedef sync_t pthread_cond_t;
typedef struct { int __once; pthread_mutex_t __mutex; } pthread_once_t;
typedef int pthread_key_t;
/* attr objects are only passed by pointer: sizes rounded up generously */
typedef struct { _Uint32t __opaque[24]; } pthread_attr_t;
typedef struct { _Uint32t __opaque[8]; } pthread_mutexattr_t;
typedef struct { _Uint32t __opaque[8]; } pthread_condattr_t;
#endif
