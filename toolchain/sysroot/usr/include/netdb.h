#ifndef _NETDB_H_INCLUDED
#define _NETDB_H_INCLUDED
#include <netinet/in.h>
__BEGIN_DECLS
struct hostent { char *h_name; char **h_aliases; int h_addrtype; int h_length; char **h_addr_list; };
struct addrinfo {
  int ai_flags, ai_family, ai_socktype, ai_protocol;
  socklen_t ai_addrlen;
  char *ai_canonname;
  struct sockaddr *ai_addr;
  struct addrinfo *ai_next;
};
#define AI_PASSIVE 0x00000001
#define AI_CANONNAME 0x00000002
#define AI_NUMERICHOST 0x00000004
#define AI_NUMERICSERV 0x00000008
#define NI_MAXHOST 1025
#define NI_MAXSERV 32
#define NI_NUMERICHOST 0x00000002
#define NI_NUMERICSERV 0x00000008
#define EAI_AGAIN 2
#define EAI_FAIL 4
#define EAI_NONAME 8
struct hostent *gethostbyname(const char *);
int getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void freeaddrinfo(struct addrinfo *);
const char *gai_strerror(int);
int getnameinfo(const struct sockaddr *, socklen_t, char *, size_t, char *, size_t, int);
__END_DECLS
#endif
