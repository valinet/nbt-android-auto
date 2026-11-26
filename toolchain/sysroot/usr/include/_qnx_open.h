/* Open QNX 6.5.0 x86 sysroot: shared base types.
 * Written for this project from the public QNX 6.5.0 Library Reference and checked against binaries
 * built with the original SDP. Not QNX code. Only what this project needs; ABI-compatible with
 * libc.so.3 on the head unit (i386, little-endian, ILP32). */
#ifndef _QNX_OPEN_H
#define _QNX_OPEN_H
#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
typedef signed char        _Int8t;
typedef unsigned char      _Uint8t;
typedef short              _Int16t;
typedef unsigned short     _Uint16t;
typedef int                _Int32t;
typedef unsigned int       _Uint32t;
typedef long long          _Int64t;
typedef unsigned long long _Uint64t;
#ifndef _SIZE_T_DEFINED_Q
#define _SIZE_T_DEFINED_Q
#endif
#endif
