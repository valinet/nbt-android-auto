#ifndef _ARPA_INET_H_INCLUDED
#define _ARPA_INET_H_INCLUDED
#include <netinet/in.h>
__BEGIN_DECLS
in_addr_t inet_addr(const char *);
int inet_aton(const char *, struct in_addr *);
char *inet_ntoa(struct in_addr);
const char *inet_ntop(int, const void *, char *, socklen_t);
int inet_pton(int, const char *, void *);
__END_DECLS
#endif
