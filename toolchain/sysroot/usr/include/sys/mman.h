#ifndef _SYS_MMAN_H_INCLUDED
#define _SYS_MMAN_H_INCLUDED
#include <sys/types.h>
__BEGIN_DECLS
#define PROT_NONE 0x0000
#define PROT_READ 0x0100
#define PROT_WRITE 0x0200
#define PROT_EXEC 0x0400
#define MAP_SHARED 0x0001
#define MAP_PRIVATE 0x0002
#define MAP_ANON 0x00080000
#define MAP_ANONYMOUS MAP_ANON
#define MAP_FAILED ((void *)-1)
void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
__END_DECLS
#endif
