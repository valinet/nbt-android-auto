#ifndef _NETINET_IN_H_INCLUDED
#define _NETINET_IN_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
#include <sys/socket.h>
__BEGIN_DECLS
typedef _Uint16t in_port_t;
typedef _Uint32t in_addr_t;
struct in_addr { in_addr_t s_addr; };
struct sockaddr_in { _Uint8t sin_len; sa_family_t sin_family; in_port_t sin_port; struct in_addr sin_addr; _Int8t sin_zero[8]; };
struct in6_addr { _Uint8t s6_addr[16]; };
struct sockaddr_in6 { _Uint8t sin6_len; sa_family_t sin6_family; in_port_t sin6_port; _Uint32t sin6_flowinfo; struct in6_addr sin6_addr; _Uint32t sin6_scope_id; };
#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define INADDR_ANY ((in_addr_t)0x00000000)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001)
#define INADDR_NONE ((in_addr_t)0xffffffff)
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
#define __qnx_swap16(x) ((_Uint16t)((((_Uint16t)(x) & 0xffU) << 8) | (((_Uint16t)(x) >> 8) & 0xffU)))
#define __qnx_swap32(x) ((_Uint32t)((((_Uint32t)(x) & 0xffU) << 24) | (((_Uint32t)(x) & 0xff00U) << 8) | \
                                    (((_Uint32t)(x) >> 8) & 0xff00U) | (((_Uint32t)(x) >> 24) & 0xffU)))
#define htons(x) __qnx_swap16(x)
#define ntohs(x) __qnx_swap16(x)
#define htonl(x) __qnx_swap32(x)
#define ntohl(x) __qnx_swap32(x)
__END_DECLS
#endif
