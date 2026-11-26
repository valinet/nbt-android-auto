/* popen()/pclose() for QNX 6.5.0. On QNX these live only in the static part of libc, so binaries carry their
 * own copy. Written for this project: like the SDP version it starts "sh -c command" with spawn() and an fd map
 * (fork() is not usable in multithreaded QNX processes). */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>

struct inheritance;
extern pid_t spawn(const char *, int, const int[], const struct inheritance *, char *const[], char *const[]);

struct pipe_entry { struct pipe_entry *next; FILE *file; pid_t pid; };
static struct pipe_entry *pipes;
static pthread_mutex_t pipes_lock = PTHREAD_MUTEX_INITIALIZER;

FILE *popen(const char *command, const char *mode) {
    struct pipe_entry *e;
    int fds[2], map[3], parent_end, child_end, saved;
    char path[1025], shell[1025];
    char *argv[4];
    if (!mode || (mode[0] != 'r' && mode[0] != 'w')) { errno = EINVAL; return NULL; }
    if (!(e = (struct pipe_entry *)malloc(sizeof(*e)))) return NULL;
    if (pipe(fds) == -1) { free(e); return NULL; }
    parent_end = mode[0] == 'r' ? 0 : 1;
    child_end = 1 - parent_end;
    map[0] = mode[0] == 'r' ? 0 : fds[0];
    map[1] = mode[0] == 'r' ? fds[1] : 1;
    map[2] = 2;
    if (!(e->file = fdopen(fds[parent_end], mode))) goto fail;
    saved = errno;
    strcpy(shell, "/bin/sh");
    if (confstr(_CS_PATH, path, sizeof(path)) && pathfind_r(path, "sh", "x", shell, sizeof(shell)) == NULL)
        strcpy(shell, "/bin/sh");
    errno = saved;
    argv[0] = (char *)"sh"; argv[1] = (char *)"-c"; argv[2] = (char *)command; argv[3] = NULL;
    e->pid = spawn(shell, 3, map, NULL, argv, NULL);
    close(fds[child_end]);
    if (e->pid == -1) { fclose(e->file); free(e); return NULL; }
    pthread_mutex_lock(&pipes_lock);
    e->next = pipes; pipes = e;
    pthread_mutex_unlock(&pipes_lock);
    return e->file;
fail:
    saved = errno;
    close(fds[0]); close(fds[1]); free(e);
    errno = saved;
    return NULL;
}

int pclose(FILE *file) {
    struct pipe_entry **p, *e = NULL;
    int status;
    pid_t pid;
    pthread_mutex_lock(&pipes_lock);
    for (p = &pipes; *p; p = &(*p)->next)
        if ((*p)->file == file) { e = *p; *p = e->next; break; }
    pthread_mutex_unlock(&pipes_lock);
    if (!e) { errno = EINVAL; return -1; }
    fclose(file);
    do pid = waitpid(e->pid, &status, 0); while (pid == -1 && errno == EINTR);
    free(e);
    return pid == -1 ? -1 : status;
}
