#ifndef _MATH_H_INCLUDED
#define _MATH_H_INCLUDED
#include <_qnx_open.h>
__BEGIN_DECLS
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_E 2.7182818284590452354
#define M_SQRT2 1.41421356237309504880
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
double acos(double); double asin(double); double atan(double); double atan2(double, double);
double ceil(double); double cos(double); double cosh(double); double exp(double); double fabs(double);
double floor(double); double fmod(double, double); double frexp(double, int *); double ldexp(double, int);
double log(double); double log10(double); double modf(double, double *); double pow(double, double);
double sin(double); double sinh(double); double sqrt(double); double tan(double); double tanh(double);
double round(double); double trunc(double); double log2(double); double exp2(double); double hypot(double, double);
long lround(double); long lrint(double); double rint(double); double cbrt(double); double fmin(double, double); double fmax(double, double);
float acosf(float); float asinf(float); float atanf(float); float atan2f(float, float); float ceilf(float);
float cosf(float); float coshf(float); float expf(float); float fabsf(float); float floorf(float);
float fmodf(float, float); float frexpf(float, int *); float ldexpf(float, int); float logf(float);
float log10f(float); float modff(float, float *); float powf(float, float); float sinf(float);
float sinhf(float); float sqrtf(float); float tanf(float); float tanhf(float); float roundf(float);
float truncf(float); long lroundf(float); float log2f(float); float exp2f(float); float fminf(float, float); float fmaxf(float, float); float hypotf(float, float);
long double acosl(long double); long double asinl(long double); long double atanl(long double); long double atan2l(long double, long double);
long double ceill(long double); long double cosl(long double); long double coshl(long double); long double expl(long double);
long double fabsl(long double); long double floorl(long double); long double fmodl(long double, long double);
long double frexpl(long double, int *); long double ldexpl(long double, int); long double logl(long double);
long double log10l(long double); long double modfl(long double, long double *); long double powl(long double, long double);
long double sinl(long double); long double sinhl(long double); long double sqrtl(long double); long double tanl(long double); long double tanhl(long double);
__END_DECLS
#endif
