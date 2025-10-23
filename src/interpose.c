#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <libsyscall_intercept_hook_point.h>
#include <syscall.h>
#include <errno.h>
#define __USE_GNU
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <assert.h>
#include <execinfo.h>

#include "hemem.h"
#include "interpose.h"
#define PROT_HC 0x03000000	/* */

// Round up to 2MB huge page boundaries (HUGEPAGE_SIZE is defined in hemem.h)
//#define PAGE_ROUND_UP(x) (((x) + (HUGEPAGE_SIZE)-1) & (~((HUGEPAGE_SIZE)-1)))

void* (*libc_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset) = NULL;
int (*libc_munmap)(void *addr, size_t length) = NULL;
void* (*libc_malloc)(size_t size) = NULL;
void (*libc_free)(void* ptr) = NULL;
void* (*libc_sbrk)(intptr_t increment) = NULL;
int (*libc_brk)(void *addr) = NULL;

static int mmap_filter(void *addr, size_t length, int prot, int flags, int fd, off_t offset, uint64_t *result)
{
  // Print internal_call_depth using write() to avoid malloc
  {
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "INTERNAL CALL IS : %d\n", internal_call_depth);
    write(STDERR_FILENO, buf, len);
  }
  
  if (internal_call_depth > 0) {
    //LOG("hemem interpose: calling libc mmap due to internal memory call: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    fputs("hemem interpose: Internal call depth > 0, passing to libc mmap\n", stderr);
    return 1;
  }

  //ensure_init();
  //LOG("INTERNAL CALL IS : %d\n", internal_call_depth);

  //TODO: figure out which mmap calls should go to libc vs hemem
  // non-anonymous mappings should probably go to libc (e.g., file mappings)
  if (((flags & MAP_ANONYMOUS) != MAP_ANONYMOUS) && !((fd == dramfd) || (fd == nvmfd))) {
    //LOG("hemem interpose: calling libc mmap due to non-anonymous, non-devdax mapping: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    fputs("hemem interpose: calling libc mmap due to non-anonymous, non-devdax mapping:\n", stderr);
    return 1;
  }

  if ((flags & MAP_STACK) == MAP_STACK) {
    // pthread mmaps are called with MAP_STACK
    //LOG("hemem interpose: calling libc mmap due to stack mapping: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    fputs("hemem interpose: calling libc mmap due to stack mapping\n", stderr);
    return 1;
  }

  //if (((flags & MAP_NORESERVE) == MAP_NORESERVE)) {
    // thread stack is called without swap space reserved, so we can probably ignore these
    //fprintf(stderr, "hemem interpose: calling libc mmap due to non-swap space reserved mapping: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    //return 1;
  //}
  
  if ((fd == dramfd) || (fd == nvmfd)) {
    //LOG("hemem interpose: calling libc mmap due to hemem devdax mapping\n");
    return 1;
  }

  
  if (!is_init) {
    //LOG("hemem interpose: calling libc mmap due to hemem init in progress\n");
    return 1;
  }

  if (length < min_interpose_mem_size) {
    dram_small_allocation_bytes += length;
    ///LOG("hemem interpose calling libc mmap due to small allocation size: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    fputs("hemem interpose: calling libc mmap due to small allocation size\n", stderr);
    return 1;
  }

  //LOG("hemem interpose: calling hemem mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
  fputs("hemem interpose: calling hemem mmap\n", stderr);
  #if 0
  
  // Print stack trace using __builtin_return_address, calling it only when needed
  HEMEM_LOG("HeMem: Stack trace for mmap(size=%ld, flags=0x%x) from TID %lu:\n", length, flags, (unsigned long)pthread_self());

  if (flags & PROT_HC) {
      LOG("  [*] PROT_HC flag detected\n");
      // Clear the flag to avoid issues in hemem_mmap
      flags &= ~PROT_HC;
      LOG("  [*] PROT_HC flag cleared for hemem_mmap\n");
      LOG("hemem interpose: calling hemem mmap without PROT_HC: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
      return 1;
  }
  #if 0 
  for (int i = 0; i < 15; i++) {
    void *addr_i = NULL;
    
    // Try to get return address at level i
    // We call __builtin_return_address right before use to go as deep as possible
    switch(i) {
      case 0: addr_i = __builtin_return_address(0); break;
      case 1: addr_i = __builtin_return_address(1); break;
      case 2: addr_i = __builtin_return_address(2); break;
      case 3: addr_i = __builtin_return_address(3); break;
      case 4: addr_i = __builtin_return_address(4); break;
      case 5: addr_i = __builtin_return_address(5); break;
      case 6: addr_i = __builtin_return_address(6); break;
      case 7: addr_i = __builtin_return_address(7); break;
      case 8: addr_i = __builtin_return_address(8); break;
      case 9: addr_i = __builtin_return_address(9); break;
      case 10: addr_i = __builtin_return_address(10); break;
      case 11: addr_i = __builtin_return_address(11); break;
      case 12: addr_i = __builtin_return_address(12); break;
      case 13: addr_i = __builtin_return_address(13); break;
      case 14: addr_i = __builtin_return_address(14); break;
      case 15: addr_i = __builtin_return_address(15); break;
      default: addr_i = NULL; break;
    }
    
    if (!addr_i) break;
    
    Dl_info info;
    if (dladdr(addr_i, &info) && info.dli_fname) {
      long offset_in_lib = (char*)addr_i - (char*)info.dli_fbase;
      const char *libname = strrchr(info.dli_fname, '/');
      libname = libname ? libname + 1 : info.dli_fname;
      HEMEM_LOG("  [%d] %s+0x%lx", i, libname, offset_in_lib);
      if (info.dli_sname) {
        HEMEM_LOG(" (%s)", info.dli_sname);
      }
      HEMEM_LOG("\n");
    }
  }
    #endif
  #endif
  
  if ((*result = (uint64_t)hemem_mmap(addr, length, prot, flags, fd, offset)) == (uint64_t)MAP_FAILED) {
    // hemem failed for some reason, try libc
    //LOG("hemem mmap failed\n\tmmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    fputs("hemem interpose: hemem mmap failed, falling back to libc mmap\n", stderr);
  }
  return 0;
}


static int munmap_filter(void *addr, size_t length, uint64_t* result)
{
  //ensure_init();
  
  //TODO: figure out which munmap calls should go to libc vs hemem

  if (internal_call_depth > 0) {
    return 1;
  }

  if ((*result = hemem_munmap(addr, length)) == -1) {
    LOG("hemem munmap failed\n\tmunmap(0x%lx, %ld)\n", (uint64_t)addr, length);
  }
  return 0;
}


static int brk_filter(void *addr, uint64_t *result)
{
  // brk() is used for heap expansion by libc malloc
  // 
  // IMPORTANT: We do NOT intercept brk calls because:
  // 1. libc's malloc uses brk during HeMem bootstrap (before is_init=true)
  // 2. The brk region is already partially mapped by libc
  // 3. Using MAP_FIXED would overwrite libc's heap causing corruption
  // 4. Without MAP_FIXED, we can't guarantee contiguous heap growth
  //
  // However, we DO track brk calls for statistics to understand how much
  // memory is being allocated outside of HeMem's management.
  
  // Track this brk call before passing through to libc
  if (is_init && addr != NULL && addr != (void*)0) {
    // Get current break
    void *old_break = libc_sbrk(0);
    if (old_break != (void*)-1) {
      intptr_t increment = (intptr_t)addr - (intptr_t)old_break;
      if (increment > 0) {
        // Track the allocation
        brk_intercepted_bytes += increment;
        brk_intercepted_count++;
        LOG("hemem interpose: brk expansion %zu bytes (total: %lu bytes, %lu calls)\n",
            (size_t)increment, brk_intercepted_bytes, brk_intercepted_count);
      }
    }
  }
  
  // Always pass through to libc
  return 1;
}


static void* bind_symbol(const char *sym)
{
  void *ptr;
  if ((ptr = dlsym(RTLD_NEXT, sym)) == NULL) {
    HEMEM_LOG("hemem memory manager interpose: dlsym failed (%s)\n", sym);
    abort();
  }
  return ptr;
}

static int hook(long syscall_number, long arg0, long arg1, long arg2, long arg3,	long arg4, long arg5,	long *result)
{
	if (syscall_number == SYS_mmap) {
	  return mmap_filter((void*)arg0, (size_t)arg1, (int)arg2, (int)arg3, (int)arg4, (off_t)arg5, (uint64_t*)result);
	} else if (syscall_number == SYS_munmap){
    return munmap_filter((void*)arg0, (size_t)arg1, (uint64_t*)result);
  } else if (syscall_number == SYS_brk) {
    return brk_filter((void*)arg0, (uint64_t*)result);
  } else {
    // ignore non-mmap/brk system calls
		return 1;
	}
}

static __attribute__((constructor)) void init(void)
{
  HEMEM_LOG("HEMEM_TRACE: [TID %lu] Constructor init() starting\n", (unsigned long)pthread_self());
  
  libc_mmap = bind_symbol("mmap");
  libc_munmap = bind_symbol("munmap");
  libc_malloc = bind_symbol("malloc");
  libc_free = bind_symbol("free");
  libc_sbrk = bind_symbol("sbrk");
  libc_brk = bind_symbol("brk");
  intercept_hook_point = hook;

  HEMEM_LOG("HEMEM_TRACE: [TID %lu] Constructor about to call hemem_init()\n", (unsigned long)pthread_self());
  hemem_init();
  HEMEM_LOG("HEMEM_TRACE: [TID %lu] Constructor: hemem_init() returned\n", (unsigned long)pthread_self());
}

static __attribute__((destructor)) void hemem_shutdown(void)
{
  hemem_stop();
}

/* 
void* malloc(size_t size)
{
  void* ret;
  if(libc_malloc == NULL) {
    libc_malloc = bind_symbol("malloc");
  }
  assert(libc_malloc != NULL);
  ret = libc_malloc(size);
  return ret;
}

void free(void* ptr)
{
  if(libc_free == NULL) {
    libc_free = bind_symbol("free");
  }
  assert(libc_free != NULL);
  libc_free(ptr);
}
*/
