#ifndef _STDIO_H_INCLUDED
#define _STDIO_H_INCLUDED
#include <_qnx_open.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>
/* Dinkumware stdio: FILE is 80 bytes; stdin/stdout/stderr are objects in libc.so.3. */
typedef struct _Filet { unsigned char __opaque[80]; } FILE;
typedef struct { _Int64t __off; unsigned char __state[24]; } fpos_t;
__BEGIN_DECLS
extern FILE _Stdin, _Stdout, _Stderr;
#define stdin  (&_Stdin)
#define stdout (&_Stdout)
#define stderr (&_Stderr)
#define EOF (-1)
#define BUFSIZ 1024
#define FILENAME_MAX 1024
#define FOPEN_MAX 16
#define TMP_MAX 17576
#define L_tmpnam 255
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
FILE *fopen(const char *, const char *);
FILE *fopen64(const char *, const char *);
FILE *fdopen(int, const char *);
FILE *freopen(const char *, const char *, FILE *);
FILE *popen(const char *, const char *);
int pclose(FILE *);
int fclose(FILE *);
int fflush(FILE *);
int fileno(FILE *);
void setbuf(FILE *, char *);
int setvbuf(FILE *, char *, int, size_t);
int fprintf(FILE *, const char *, ...) __attribute__((format(printf, 2, 3)));
int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...) __attribute__((format(printf, 3, 4)));
int vfprintf(FILE *, const char *, va_list);
int vprintf(const char *, va_list);
int vsprintf(char *, const char *, va_list);
int vsnprintf(char *, size_t, const char *, va_list);
int fscanf(FILE *, const char *, ...);
int scanf(const char *, ...);
int sscanf(const char *, const char *, ...);
int vfscanf(FILE *, const char *, va_list);
int vscanf(const char *, va_list);
int vsscanf(const char *, const char *, va_list);
int fgetc(FILE *);
char *fgets(char *, int, FILE *);
int fputc(int, FILE *);
int fputs(const char *, FILE *);
int getc(FILE *);
int getchar(void);
char *gets(char *);
int putc(int, FILE *);
int putchar(int);
int puts(const char *);
int ungetc(int, FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fgetpos(FILE *, fpos_t *);
int fseek(FILE *, long, int);
int fseeko(FILE *, off_t, int);
int fsetpos(FILE *, const fpos_t *);
long ftell(FILE *);
off_t ftello(FILE *);
void rewind(FILE *);
void clearerr(FILE *);
int feof(FILE *);
int ferror(FILE *);
void perror(const char *);
int remove(const char *);
int rename(const char *, const char *);
FILE *tmpfile(void);
char *tmpnam(char *);
int fwide(FILE *, int);
__END_DECLS
#endif
