#ifndef _SYS_WAIT_H_INCLUDED
#define _SYS_WAIT_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
__BEGIN_DECLS
#define WNOHANG   0x0040
#define WUNTRACED 0x0004
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WTERMSIG(s)    ((s) & 0x7f)
#define WSTOPSIG(s)    WEXITSTATUS(s)
#define WIFEXITED(s)   (((s) & 0xff) == 0)
#define WIFSIGNALED(s) (((s) & 0xff) != 0 && ((s) & 0xff00) == 0)
#define WIFSTOPPED(s)  (((s) & 0xff) == 0x7f && ((s) & 0xff00) != 0)
pid_t wait(int *);
pid_t waitpid(pid_t, int *, int);
__END_DECLS
#endif
