#ifndef _DLFCN_H_INCLUDED
#define _DLFCN_H_INCLUDED
#include <_qnx_open.h>
__BEGIN_DECLS
#define RTLD_LAZY   0x0001
#define RTLD_NOW    0x0002
#define RTLD_NOLOAD 0x0004
#define RTLD_GLOBAL 0x0100
#define RTLD_LOCAL  0x0200
#define RTLD_DEFAULT ((void *)-2)
void *dlopen(const char *, int);
void *dlsym(void *, const char *);
int dlclose(void *);
char *dlerror(void);
__END_DECLS
#endif
