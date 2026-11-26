#ifndef _FCNTL_H_INCLUDED
#define _FCNTL_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
__BEGIN_DECLS
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_ACCMODE  0x0003
#define O_APPEND   0x0008
#define O_DSYNC    0x0010
#define O_SYNC     0x0020
#define O_RSYNC    0x0040
#define O_NONBLOCK 0x0080
#define O_NDELAY   O_NONBLOCK
#define O_CREAT    0x0100
#define O_TRUNC    0x0200
#define O_EXCL     0x0400
#define O_NOCTTY   0x0800
#define O_LARGEFILE 0x8000
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_SETLK 6
#define F_SETLKW 7
#define F_GETLK 14
#define FD_CLOEXEC 0x01
#define F_RDLCK 1
#define F_WRLCK 2
#define F_UNLCK 3
struct flock {
  _Int16t l_type, l_whence;
  _Int32t __zero1;
  off_t l_start, l_start_hi;
  off_t l_len, l_len_hi;
  pid_t l_pid;
  _Uint32t l_sysid;
};
int open(const char *, int, ...);
int creat(const char *, mode_t);
int fcntl(int, int, ...);
__END_DECLS
#endif
