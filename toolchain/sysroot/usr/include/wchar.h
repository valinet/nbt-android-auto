#ifndef _WCHAR_H_INCLUDED
#define _WCHAR_H_INCLUDED
#include <_qnx_open.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
__BEGIN_DECLS
typedef long wint_t;
typedef struct { unsigned long __wchar; unsigned short __state; } mbstate_t;
#define WEOF ((wint_t)-1)
#define WCHAR_MIN 0
#define WCHAR_MAX 0xffffffffUL
size_t wcslen(const wchar_t *);
wchar_t *wmemchr(const wchar_t *, wchar_t, size_t);
int wmemcmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *wmemcpy(wchar_t *, const wchar_t *, size_t);
wchar_t *wmemmove(wchar_t *, const wchar_t *, size_t);
wchar_t *wmemset(wchar_t *, wchar_t, size_t);
wint_t btowc(int);
int wctob(wint_t);
size_t mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
size_t wcrtomb(char *, wchar_t, mbstate_t *);
int mbsinit(const mbstate_t *);
size_t mbrlen(const char *, size_t, mbstate_t *);
size_t mbsrtowcs(wchar_t *, const char **, size_t, mbstate_t *);
size_t wcsrtombs(char *, const wchar_t **, size_t, mbstate_t *);
__END_DECLS
#endif
