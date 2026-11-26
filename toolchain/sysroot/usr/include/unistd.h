#ifndef _UNISTD_H_INCLUDED
#define _UNISTD_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
__BEGIN_DECLS
#define _POSIX_VERSION 200112L
#define _POSIX_THREAD_SAFE_FUNCTIONS 200112L
#define _POSIX_THREADS 200112L
#define _POSIX_TIMERS 200112L
#define _POSIX_MONOTONIC_CLOCK 200112L
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#define _CS_PATH 1
#define _SC_PAGESIZE 11
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_CLK_TCK 2
extern char **environ;
extern char *optarg;
extern int optind, opterr, optopt;
int access(const char *, int);
unsigned alarm(unsigned);
int chdir(const char *);
int chown(const char *, uid_t, gid_t);
int close(int);
size_t confstr(int, char *, size_t);
int dup(int);
int dup2(int, int);
int execl(const char *, const char *, ...);
int execle(const char *, const char *, ...);
int execlp(const char *, const char *, ...);
int execv(const char *, char *const[]);
int execve(const char *, char *const[], char *const[]);
int execvp(const char *, char *const[]);
void _exit(int) __attribute__((noreturn));
pid_t fork(void);
long fpathconf(int, int);
int fsync(int);
int ftruncate(int, off_t);
char *getcwd(char *, size_t);
gid_t getegid(void);
uid_t geteuid(void);
gid_t getgid(void);
char *getlogin(void);
int getopt(int, char *const[], const char *);
pid_t getpgrp(void);
pid_t getpid(void);
pid_t getppid(void);
pid_t getsid(pid_t);
uid_t getuid(void);
int gethostname(char *, size_t);
int isatty(int);
int link(const char *, const char *);
off_t lseek(int, off_t, int);
long pathconf(const char *, int);
int pause(void);
int pipe(int[2]);
ssize_t pread(int, void *, size_t, off_t);
ssize_t pwrite(int, const void *, size_t, off_t);
ssize_t read(int, void *, size_t);
ssize_t readlink(const char *, char *, size_t);
int rmdir(const char *);
int setgid(gid_t);
int setpgid(pid_t, pid_t);
pid_t setsid(void);
int setuid(uid_t);
unsigned sleep(unsigned);
int symlink(const char *, const char *);
void sync(void);
long sysconf(int);
int truncate(const char *, off_t);
char *ttyname(int);
int unlink(const char *);
int usleep(useconds_t);
ssize_t write(int, const void *, size_t);
char *pathfind_r(const char *, const char *, const char *, char *, size_t);
__END_DECLS
#endif
