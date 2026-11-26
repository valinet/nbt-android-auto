#ifndef _DIRENT_H_INCLUDED
#define _DIRENT_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
__BEGIN_DECLS
typedef struct _dir DIR;
/* 32-bit layout: d_ino(+hi), d_offset(+hi), reclen, namelen, name at offset 20 */
struct dirent {
  ino_t d_ino, d_ino_hi;
  off_t d_offset, d_offset_hi;
  _Int16t d_reclen;
  _Int16t d_namelen;
  char d_name[1];
};
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int readdir_r(DIR *, struct dirent *, struct dirent **);
void rewinddir(DIR *);
int closedir(DIR *);
__END_DECLS
#endif
