#ifndef _WCTYPE_H_INCLUDED
#define _WCTYPE_H_INCLUDED
#include <wchar.h>
__BEGIN_DECLS
typedef unsigned long wctype_t;
typedef const void *wctrans_t;
int iswalpha(wint_t); int iswdigit(wint_t); int iswspace(wint_t); int iswupper(wint_t); int iswlower(wint_t);
int iswalnum(wint_t); int iswpunct(wint_t); int iswprint(wint_t); int iswcntrl(wint_t); int iswxdigit(wint_t); int iswgraph(wint_t);
wint_t towlower(wint_t); wint_t towupper(wint_t);
int iswctype(wint_t, wctype_t); wctype_t wctype(const char *);
__END_DECLS
#endif
