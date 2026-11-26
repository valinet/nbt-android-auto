#ifndef _SYS_UN_H_INCLUDED
#define _SYS_UN_H_INCLUDED
#include <sys/socket.h>
struct sockaddr_un { _Uint8t sun_len; sa_family_t sun_family; char sun_path[104]; };
#endif
