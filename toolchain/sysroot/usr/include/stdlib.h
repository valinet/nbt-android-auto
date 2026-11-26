#ifndef _STDLIB_H_INCLUDED
#define _STDLIB_H_INCLUDED
#include <_qnx_open.h>
#include <stddef.h>
__BEGIN_DECLS
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767
#define MB_CUR_MAX 1
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void *alloca(size_t);
#define alloca(n) __builtin_alloca(n)
void abort(void) __attribute__((noreturn));
void exit(int) __attribute__((noreturn));
void _Exit(int) __attribute__((noreturn));
int atexit(void (*)(void));
char *getenv(const char *);
int setenv(const char *, const char *, int);
int unsetenv(const char *);
int putenv(char *);
int system(const char *);
int abs(int);
long labs(long);
long long llabs(long long);
div_t div(int, int);
ldiv_t ldiv(long, long);
lldiv_t lldiv(long long, long long);
int atoi(const char *);
long atol(const char *);
long long atoll(const char *);
double atof(const char *);
double strtod(const char *, char **);
float strtof(const char *, char **);
long double strtold(const char *, char **);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
int rand(void);
void srand(unsigned);
int rand_r(unsigned *);
long random(void);
void srandom(unsigned);
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
int mblen(const char *, size_t);
int mbtowc(wchar_t *, const char *, size_t);
int wctomb(char *, wchar_t);
size_t mbstowcs(wchar_t *, const char *, size_t);
size_t wcstombs(char *, const wchar_t *, size_t);
char *realpath(const char *, char *);
int mkstemp(char *);
__END_DECLS
#endif
