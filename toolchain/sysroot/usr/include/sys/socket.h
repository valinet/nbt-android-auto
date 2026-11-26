#ifndef _SYS_SOCKET_H_INCLUDED
#define _SYS_SOCKET_H_INCLUDED
#include <_qnx_open.h>
#include <sys/types.h>
__BEGIN_DECLS
/* io-pkt (NetBSD-derived) socket ABI */
typedef _Uint32t socklen_t;
typedef _Uint8t sa_family_t;
struct sockaddr { _Uint8t sa_len; sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_storage { _Uint8t ss_len; sa_family_t ss_family; char __ss_pad1[6]; _Int64t __ss_align; char __ss_pad2[112]; };
struct iovec;
struct msghdr { void *msg_name; socklen_t msg_namelen; struct iovec *msg_iov; int msg_iovlen; void *msg_control; socklen_t msg_controllen; int msg_flags; };
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_SEQPACKET 5
#define AF_UNSPEC 0
#define AF_LOCAL 1
#define AF_UNIX AF_LOCAL
#define AF_INET 2
#define AF_INET6 24
#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX AF_UNIX
#define PF_LOCAL AF_LOCAL
#define PF_INET AF_INET
#define PF_INET6 AF_INET6
#define SOL_SOCKET 0xffff
#define SO_DEBUG 0x0001
#define SO_ACCEPTCONN 0x0002
#define SO_REUSEADDR 0x0004
#define SO_KEEPALIVE 0x0008
#define SO_DONTROUTE 0x0010
#define SO_BROADCAST 0x0020
#define SO_LINGER 0x0080
#define SO_OOBINLINE 0x0100
#define SO_REUSEPORT 0x0200
#define SO_SNDBUF 0x1001
#define SO_RCVBUF 0x1002
#define SO_SNDLOWAT 0x1003
#define SO_RCVLOWAT 0x1004
#define SO_SNDTIMEO 0x1005
#define SO_RCVTIMEO 0x1006
#define SO_ERROR 0x1007
#define SO_TYPE 0x1008
#define MSG_OOB 0x0001
#define MSG_PEEK 0x0002
#define MSG_DONTROUTE 0x0004
#define MSG_EOR 0x0008
#define MSG_TRUNC 0x0010
#define MSG_CTRUNC 0x0020
#define MSG_WAITALL 0x0040
#define MSG_DONTWAIT 0x0080
#define MSG_NOSIGNAL 0x0800
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
#define SOMAXCONN 128
struct linger { int l_onoff; int l_linger; };
int accept(int, struct sockaddr *, socklen_t *);
int bind(int, const struct sockaddr *, socklen_t);
int connect(int, const struct sockaddr *, socklen_t);
int getpeername(int, struct sockaddr *, socklen_t *);
int getsockname(int, struct sockaddr *, socklen_t *);
int getsockopt(int, int, int, void *, socklen_t *);
int listen(int, int);
ssize_t recv(int, void *, size_t, int);
ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
ssize_t recvmsg(int, struct msghdr *, int);
ssize_t send(int, const void *, size_t, int);
ssize_t sendmsg(int, const struct msghdr *, int);
ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
int setsockopt(int, int, int, const void *, socklen_t);
int shutdown(int, int);
int socket(int, int, int);
int socketpair(int, int, int, int[2]);
__END_DECLS
#endif
