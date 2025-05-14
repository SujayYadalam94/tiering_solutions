#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif


// function pointers to libc functions
extern void* (*libc_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern int (*libc_munmap)(void *addr, size_t length);
extern void* (*libc_malloc)(size_t size);
extern void (*libc_free)(void* p);

#ifdef __cplusplus
}
#endif

#define MIN_INTERPOSE_MEM_SIZE_DEFAULT (1 * 1024UL * 1024UL * 1024UL)

