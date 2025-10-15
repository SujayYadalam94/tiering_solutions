#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <libsyscall_intercept_hook_point.h>
#include <syscall.h>
#include <errno.h>
#define __USE_GNU
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <assert.h>

#include "hemem.h"
#include "interpose.h"

// Round up to 2MB huge page boundaries (HUGEPAGE_SIZE is defined in hemem.h)
#define PAGE_ROUND_UP(x) (((x) + (HUGEPAGE_SIZE)-1) & (~((HUGEPAGE_SIZE)-1)))

void* (*libc_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset) = NULL;
int (*libc_munmap)(void *addr, size_t length) = NULL;
void* (*libc_sbrk)(intptr_t increment) = NULL;
int (*libc_brk)(void *addr) = NULL;

// Forward declarations
static void* bind_symbol(const char *sym);

// HeMem-managed heap - we ARE the application's heap
static pthread_mutex_t hemem_brk_lock = PTHREAD_MUTEX_INITIALIZER;
static void* hemem_heap_start = NULL;        // Start of our heap (this IS the application's heap)
static void* hemem_heap_current = NULL;      // Current break
static void* hemem_heap_end = NULL;          // End of reserved VA space
static size_t hemem_heap_size = 0;           // Total reserved VA space
static bool hemem_heap_initialized = false;
static bool hemem_heap_given_to_app = false; // Have we told the app about our heap yet?

// Initialize HeMem-managed heap - this becomes THE application heap
static void hemem_heap_init(void)
{
  pthread_mutex_lock(&hemem_brk_lock);
  
  if (hemem_heap_initialized) {
    pthread_mutex_unlock(&hemem_brk_lock);
    return;
  }
  
  // Get heap size from env variable or calculate sensible default
  char* heap_size_str = getenv("HEMEM_HEAP_SIZE");
  if (heap_size_str != NULL) {
    hemem_heap_size = strtoull(heap_size_str, NULL, 10);
  } else {
    // Default: Use 50% of available physical memory (DRAM + NVM)
    uint64_t total_physical = dramsize + nvmsize;
    if (total_physical > 0) {
      hemem_heap_size = total_physical / 2;
      // Cap at reasonable maximum (64 GB)
      if (hemem_heap_size > (64ULL * 1024ULL * 1024ULL * 1024ULL)) {
        hemem_heap_size = 64ULL * 1024ULL * 1024ULL * 1024ULL;
      }
      // Ensure minimum (256 MB)
      if (hemem_heap_size < (256ULL * 1024ULL * 1024ULL)) {
        hemem_heap_size = 256ULL * 1024ULL * 1024ULL;
      }
    } else {
      hemem_heap_size = 4ULL * 1024ULL * 1024ULL * 1024ULL;  // 4GB
    }
  }
  
  // Round up to huge page boundary
  hemem_heap_size = PAGE_ROUND_UP(hemem_heap_size);
  
  LOG("hemem_heap_init: reserving %zu bytes (%.2f GB) VA space for heap\n", 
      hemem_heap_size, hemem_heap_size / (1024.0*1024.0*1024.0));
  LOG("hemem_heap_init: physical memory available: DRAM=%lu GB, NVM=%lu GB\n",
      dramsize / (1024*1024*1024), nvmsize / (1024*1024*1024));
  
  // Allocate heap through HeMem (backed by DAX devices)
  // Pages will fault-in through userfaultfd and be managed by HeMem policies
  internal_call = true;
  void* heap_addr = hemem_mmap(NULL, hemem_heap_size,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS,
                                -1, 0);
  internal_call = false;
  
  if (heap_addr == MAP_FAILED) {
    LOG("hemem_heap_init: hemem_mmap failed: %s\n", strerror(errno));
    pthread_mutex_unlock(&hemem_brk_lock);
    return;
  }
  
  hemem_heap_start = heap_addr;
  hemem_heap_current = heap_addr;  // Start with empty heap
  hemem_heap_end = (char*)heap_addr + hemem_heap_size;
  hemem_heap_initialized = true;
  
  LOG("hemem_heap_init: heap allocated via HeMem at [%p, %p)\n", 
      hemem_heap_start, hemem_heap_end);
  
  pthread_mutex_unlock(&hemem_brk_lock);
}

// Internal sbrk implementation - manages our heap
static void* hemem_sbrk_internal(intptr_t increment)
{
  void* old_break = hemem_heap_current;
  
  if (increment == 0) {
    // Query current break
    LOG("hemem_sbrk: query returning %p\n", old_break);
    hemem_heap_given_to_app = true;  // Mark that app knows our heap
    return old_break;
  }
  
  void* new_break = (char*)old_break + increment;
  
  // Check bounds - the entire VA space is already reserved via hemem_mmap
  if (new_break < hemem_heap_start || new_break > hemem_heap_end) {
    LOG("hemem_sbrk: out of VA space (requested %ld bytes)\n", increment);
    LOG("hemem_sbrk: current=%p, new=%p, bounds=[%p, %p)\n",
        old_break, new_break, hemem_heap_start, hemem_heap_end);
    errno = ENOMEM;
    return (void*)-1;
  }
  
  // Simply update the break pointer - no allocation needed!
  // The VA space was already reserved in hemem_heap_init() via hemem_mmap().
  // Physical pages will be allocated on-demand via userfaultfd page faults.
  if (increment > 0) {
    LOG("hemem_sbrk: expanding heap by %ld bytes (new break: %p)\n", increment, new_break);
    brk_intercepted_bytes += increment;
    brk_intercepted_count++;
  } else {
    LOG("hemem_sbrk: contracting heap by %ld bytes (new break: %p)\n", -increment, new_break);
    // Note: We don't actually free pages on contraction. The application 
    // might re-use this space, and physical pages can be reclaimed by
    // HeMem's eviction policy if needed.
  }
  
  LOG("hemem_sbrk: hemem_heap_current is now %p\n", hemem_heap_current);
  hemem_heap_current = new_break;
  LOG("hemem_sbrk: hemem_heap_current is now %p\n", hemem_heap_current);
  return old_break;
}

// Public sbrk interface - thread-safe wrapper around internal implementation
void* hemem_sbrk(intptr_t increment)
{
  // Initialize heap on THE FIRST call (whether from library or syscall)
  if (!hemem_heap_initialized) {
    LOG("hemem_sbrk: FIRST CALL - initializing HeMem heap\n");
    hemem_heap_init();
    if (!hemem_heap_initialized) {
      // Initialization failed - this is fatal for heap interception
      LOG("hemem_sbrk: FATAL - heap initialization failed\n");
      errno = ENOMEM;
      return (void*)-1;
    }
  }
  
  pthread_mutex_lock(&hemem_brk_lock);
  void* result = hemem_sbrk_internal(increment);
  pthread_mutex_unlock(&hemem_brk_lock);
  
  return result;
}

// brk implementation - simple wrapper around sbrk
int hemem_brk(void* addr)
{
  // Initialize heap on THE FIRST call (whether from library or syscall)
  if (!hemem_heap_initialized) {
    LOG("hemem_brk: FIRST CALL - initializing HeMem heap\n");
    hemem_heap_init();
    if (!hemem_heap_initialized) {
      // Initialization failed - this is fatal for heap interception
      LOG("hemem_brk: FATAL - heap initialization failed\n");
      errno = ENOMEM;
      return -1;
    }
  }
  
  pthread_mutex_lock(&hemem_brk_lock);
  
  void* old_break = hemem_heap_current;
  intptr_t increment = (intptr_t)addr - (intptr_t)old_break;
  
  LOG("hemem_brk: setting break to %p (increment=%ld)\n", addr, increment);
  
  void* result = hemem_sbrk_internal(increment);
  
  pthread_mutex_unlock(&hemem_brk_lock);
  
  return (result == (void*)-1) ? -1 : 0;
}

static int mmap_filter(void *addr, size_t length, int prot, int flags, int fd, off_t offset, uint64_t *result)
{
  //ensure_init();

  //TODO: figure out which mmap calls should go to libc vs hemem
  // non-anonymous mappings should probably go to libc (e.g., file mappings)
  if (((flags & MAP_ANONYMOUS) != MAP_ANONYMOUS) && !((fd == dramfd) || (fd == nvmfd))) {
    LOG("hemem interpose: calling libc mmap due to non-anonymous, non-devdax mapping: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    return 1;
  }

  if ((flags & MAP_STACK) == MAP_STACK) {
    // pthread mmaps are called with MAP_STACK
    LOG("hemem interpose: calling libc mmap due to stack mapping: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
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

  if (internal_call) {
    LOG("hemem interpose: calling libc mmap due to internal memory call: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    return 1;
  }
  
  if (!is_init) {
    //LOG("hemem interpose: calling libc mmap due to hemem init in progress\n");
    return 1;
  }

  if (length < min_interpose_mem_size) {
    dram_small_allocation_bytes += length;
    LOG("hemem interpose calling libc mmap due to small allocation size: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    return 1;
  }

  LOG("hemem interpose: calling hemem mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
  if ((*result = (uint64_t)hemem_mmap(addr, length, prot, flags, fd, offset)) == (uint64_t)MAP_FAILED) {
    // hemem failed for some reason, try libc
    LOG("hemem mmap failed\n\tmmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
  }
  return 0;
}


static int munmap_filter(void *addr, size_t length, uint64_t* result)
{
  //ensure_init();
  
  //TODO: figure out which munmap calls should go to libc vs hemem
  
  if (internal_call) {
    return 1;
  }

  if ((*result = hemem_munmap(addr, length)) == -1) {
    LOG("hemem munmap failed\n\tmunmap(0x%lx, %ld)\n", (uint64_t)addr, length);
  }
  return 0;
}


static int brk_filter(void *addr, uint64_t *result)
{
  //return 1;
  // Before HeMem is initialized, pass through to libc to avoid bootstrap issues
  if (!is_init) {
    LOG("hemem interpose: calling libc brk due to uninitialized HeMem: brk(0x%lx)\n", (uint64_t)addr);
    return 1;  // Pass to libc
  }
  
  // During internal calls (HeMem's own allocations), pass through to libc
  if (internal_call) {
    LOG("hemem interpose: calling libc brk due to internal memory call: brk(0x%lx)\n", (uint64_t)addr);
    return 1;  // Pass to libc
  }
  
  // Initialize heap on THE FIRST brk syscall if not already initialized
  if (!hemem_heap_initialized) {
    LOG("hemem interpose: FIRST brk syscall - initializing HeMem heap\n");
    hemem_heap_init();
    if (!hemem_heap_initialized) {
      LOG("hemem interpose: FATAL - heap initialization failed\n");
      return 1;  // Fall back to libc as last resort
    }
  }
  
  // Handle brk(0) query - return current break
  if (addr == NULL || addr == 0) {
    *result = (uint64_t)hemem_heap_current;
    LOG("hemem interpose: brk(0) query returning %p\n", hemem_heap_current);
    hemem_heap_given_to_app = true;
    return 0;  // Handled
  }
  
  // Handle first non-zero brk call
  if (!hemem_heap_given_to_app) {
    // This is the first allocation request. Simply return our heap start
    // as the new break, which will become the app's heap base.
    hemem_heap_given_to_app = true;
    //hemem_heap_current = addr;  // Accept their requested size as initial allocation
    LOG("hemem interpose: first brk(%p) - setting initial break\n", addr);
    
    // Make sure requested address is within our heap bounds
    if (hemem_heap_current > hemem_heap_end) {
      LOG("hemem interpose: first brk request too large!\n");
      hemem_heap_current = hemem_heap_start;
      *result = (uint64_t)hemem_heap_start;
      return 0;
    }
    LOG("hemem interpose: initial heap set to [%p, %p)\n", hemem_heap_start, hemem_heap_end);
    LOG("hemem interpose: initial break set to %p\n", hemem_heap_current); 
    LOG("hemem interpose: addr is %p\n", addr); 
    *result = (uint64_t)hemem_heap_current + (1<<24);
    // Write to result address to test if we can access the memory there
    //volatile uint64_t *test = (volatile uint64_t*)*result;
    //*test = 0xdeadbeef;
    LOG("hemem interpose: result is %p\n", (void*)*result);
    return 0;
  }
  
  // Normal brk call - translate if outside our heap bounds
  if (addr < hemem_heap_start || addr > hemem_heap_end) {
    // This brk call is for the kernel heap. Translate it to our heap.
    // Get the current kernel heap break
    void *libc_brk_current = libc_sbrk(0);
    
    // Calculate delta: how much is the kernel heap growing/shrinking?
    intptr_t delta = (intptr_t)addr - (intptr_t)libc_brk_current;
    
    LOG("hemem interpose: brk(%p) outside HeMem heap [%p, %p)\n",
        addr, hemem_heap_start, hemem_heap_end);
    LOG("hemem interpose: kernel brk current=%p, requested=%p, delta=%ld bytes\n",
        libc_brk_current, addr, delta);
    
    // Apply the same delta to our heap
    void *new_break = hemem_sbrk(delta);
    if (new_break == (void*)-1) {
      LOG("hemem interpose: hemem_sbrk(%ld) failed\n", delta);
      errno = ENOMEM;
      *result = (uint64_t)-1;  // brk returns -1 on error
      return 0;
    }
    
    LOG("hemem interpose: translated to hemem_sbrk(%ld), new HeMem break=%p\n", delta, new_break);
    
    // Also call libc brk to keep kernel heap in sync
    libc_brk(addr);
    
    // brk() returns 0 on success
    *result = 0;
    return 0;
  }
  
  LOG("hemem interpose: brk(%p)\n", addr);
  
  // Set the break
  int brk_result = hemem_brk(addr);
  if (brk_result == -1) {
    LOG("hemem_brk failed\n");
    *result = (uint64_t)hemem_heap_current;
    return 0;
  }
  
  *result = (uint64_t)addr;
  return 0;
}



static void* bind_symbol(const char *sym)
{
  void *ptr;
  if ((ptr = dlsym(RTLD_NEXT, sym)) == NULL) {
    fprintf(stderr, "hemem memory manager interpose: dlsym failed (%s)\n", sym);
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

//static __attribute__((constructor(101))) void init(void)  // Priority 101 = very early
static __attribute__((constructor)) void init(void)
{
  libc_mmap = bind_symbol("mmap");
  libc_munmap = bind_symbol("munmap");
  libc_sbrk = bind_symbol("sbrk");
  libc_brk = bind_symbol("brk");
  intercept_hook_point = hook;
  //raise(SIGTRAP); // For debugging: pause here to allow attaching a debugger
  hemem_init();
  
  // DON'T initialize heap here! It will be initialized on the VERY FIRST
  // brk syscall, which happens before glibc's malloc is fully set up.
  // This ensures we can hijack the heap location early enough.
  
  LOG("hemem interpose: ready (heap will initialize on first brk syscall)\n");
}

static __attribute__((destructor)) void hemem_shutdown(void)
{
  hemem_stop();
}

// Override sbrk at the library level (not just syscall level)
void* sbrk(intptr_t increment)
{
  // Before init or during internal calls, use libc
  if (!is_init || internal_call) {
    if (libc_sbrk == NULL) {
      libc_sbrk = bind_symbol("sbrk");
    }
    return libc_sbrk(increment);
  }
  
  LOG("hemem interpose: intercepting sbrk(%ld)\n", increment);
  return hemem_sbrk(increment);
}

// Override brk at the library level (not just syscall level)
int brk(void* addr)
{
  // Before init or during internal calls, use libc
  if (!is_init || internal_call) {
    if (libc_brk == NULL) {
      libc_brk = bind_symbol("brk");
    }
    return libc_brk(addr);
  }
  
  LOG("hemem interpose: intercepting brk(%p)\n", addr);
  return hemem_brk(addr);
}

// Print statistics about HeMem-managed heap
void hemem_heap_print_stats(void)
{
  if (!hemem_heap_initialized) {
    LOG_STATS("%s", "HeMem-managed heap: not initialized (no brk/sbrk calls detected)\n");
    return;
  }
  
  pthread_mutex_lock(&hemem_brk_lock);
  
  size_t heap_used = (char*)hemem_heap_current - (char*)hemem_heap_start;
  size_t heap_available = (char*)hemem_heap_end - (char*)hemem_heap_current;
  double usage_percent = (heap_used * 100.0) / hemem_heap_size;
  
  LOG_STATS("HeMem-managed heap: [%p, %p) current=%p\n",
            hemem_heap_start, hemem_heap_end, hemem_heap_current);
  LOG_STATS("  Size: %zu bytes (%.2f GB)\n",
            hemem_heap_size, hemem_heap_size / (1024.0 * 1024.0 * 1024.0));
  LOG_STATS("  Used: %zu bytes (%.2f MB, %.1f%%)\n",
            heap_used, heap_used / (1024.0 * 1024.0), usage_percent);
  LOG_STATS("  Available: %zu bytes (%.2f GB)\n",
            heap_available, heap_available / (1024.0 * 1024.0 * 1024.0));
  
  pthread_mutex_unlock(&hemem_brk_lock);
}
