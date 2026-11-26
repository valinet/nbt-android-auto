#ifndef _SIGNAL_H_INCLUDED
#define _SIGNAL_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
__BEGIN_DECLS
typedef int sig_atomic_t;
typedef struct { long __bits[2]; } sigset_t;
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGIOT SIGABRT
#define SIGEMT 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGBUS 10
#define SIGSEGV 11
#define SIGSYS 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGUSR1 16
#define SIGUSR2 17
#define SIGCHLD 18
#define SIGCLD SIGCHLD
#define SIGPWR 19
#define SIGWINCH 20
#define SIGURG 21
#define SIGPOLL 22
#define SIGIO SIGPOLL
#define SIGSTOP 23
#define SIGTSTP 24
#define SIGCONT 25
#define SIGVTALRM 28
#define SIGPROF 29
#define SIGXCPU 30
#define SIGXFSZ 31
#define _SIGMAX 64
#define SIG_ERR ((void (*)(int))-1)
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
void (*signal(int, void (*)(int)))(int);
int raise(int);
int kill(pid_t, int);
int sigemptyset(sigset_t *);
int sigfillset(sigset_t *);
int sigaddset(sigset_t *, int);
int sigdelset(sigset_t *, int);
int sigismember(const sigset_t *, int);
int sigprocmask(int, const sigset_t *, sigset_t *);
int pthread_sigmask(int, const sigset_t *, sigset_t *);
int pthread_kill(pthread_t, int);
__END_DECLS
#endif
