#ifndef _LOCALE_H_INCLUDED
#define _LOCALE_H_INCLUDED
#include <_qnx_open.h>
#include <stddef.h>
__BEGIN_DECLS
struct lconv {
  char *decimal_point, *thousands_sep, *grouping, *int_curr_symbol, *currency_symbol, *mon_decimal_point,
       *mon_thousands_sep, *mon_grouping, *positive_sign, *negative_sign;
  char int_frac_digits, frac_digits, p_cs_precedes, p_sep_by_space, n_cs_precedes, n_sep_by_space, p_sign_posn, n_sign_posn;
};
#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5
#define LC_MESSAGES 6
char *setlocale(int, const char *);
struct lconv *localeconv(void);
__END_DECLS
#endif
