#undef assert
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#include <stdio.h>
#include <stdlib.h>
#define assert(e) ((e) ? (void)0 : (fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #e), abort()))
#endif
