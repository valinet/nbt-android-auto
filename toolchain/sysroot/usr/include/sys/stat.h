#ifndef _SYS_STAT_H_INCLUDED
#define _SYS_STAT_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
#include <time.h>
__BEGIN_DECLS
/* 32-bit offset layout (default _FILE_OFFSET_BITS), little-endian: 72 bytes. */
struct stat {
  ino_t st_ino, st_ino_hi;
  off_t st_size, st_size_hi;
  dev_t st_dev, st_rdev;
  uid_t st_uid;
  gid_t st_gid;
  time_t st_mtime, st_atime, st_ctime;
  mode_t st_mode;
  nlink_t st_nlink;
  blksize_t st_blocksize;
  _Int32t st_nblocks;
  blksize_t st_blksize;
  blkcnt_t st_blocks, st_blocks_hi;
};
#define S_IFMT   0xF000
#define S_IFIFO  0x1000
#define S_IFCHR  0x2000
#define S_IFDIR  0x4000
#define S_IFNAM  0x5000
#define S_IFBLK  0x6000
#define S_IFREG  0x8000
#define S_IFLNK  0xA000
#define S_IFSOCK 0xC000
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 070
#define S_IRGRP 040
#define S_IWGRP 020
#define S_IXGRP 010
#define S_IRWXO 07
#define S_IROTH 04
#define S_IWOTH 02
#define S_IXOTH 01
int stat(const char *, struct stat *);
int lstat(const char *, struct stat *);
int fstat(int, struct stat *);
int chmod(const char *, mode_t);
int fchmod(int, mode_t);
int mkdir(const char *, mode_t);
int mkfifo(const char *, mode_t);
int mknod(const char *, mode_t, dev_t);
mode_t umask(mode_t);
__END_DECLS
#endif
