#ifndef _SETJMP_H_INCLUDED
#define _SETJMP_H_INCLUDED
#include <_qnx_open.h>
__BEGIN_DECLS
typedef struct { unsigned __opaque[16]; } jmp_buf[1];
int setjmp(jmp_buf);
void longjmp(jmp_buf, int) __attribute__((noreturn));
__END_DECLS
#endif
