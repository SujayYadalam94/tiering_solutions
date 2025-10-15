# Managing All Heap Memory with HeMem

## Current Situation

HeMem currently intercepts:
- ✅ **mmap** calls for large allocations (>= MIN_INTERPOSE_MEM_SIZE)
- ✅ Small mmap allocations tracked but not managed (dram_small_allocation_bytes)
- ⚠️ **brk/sbrk** calls tracked for statistics but NOT intercepted (go to libc)

## Why We Can't Intercept BRK Currently

The brk system call is fundamentally difficult to intercept because:

1. **Bootstrap Problem**: libc's malloc uses brk during HeMem initialization (before `is_init=true`)
2. **Shared Heap Region**: brk manages a contiguous region that libc has already started using
3. **MAP_FIXED Corruption**: Using MAP_FIXED to overlay libc's heap causes memory corruption
4. **Contiguity Requirement**: brk expects heap to grow contiguously; without MAP_FIXED we can't guarantee this

## Strategies for Complete Heap Management

### Strategy 1: Override malloc/free (LD_PRELOAD)
**Best approach for C programs**

**How it works**:
```c
// In interpose.c or separate malloc.c
void* malloc(size_t size) {
    // All malloc calls come here instead of libc
    return hemem_mmap(...);
}

void free(void* ptr) {
    hemem_munmap(ptr, ...);  // Need to track sizes
}
```

**Pros**:
- ✅ Intercepts ALL heap allocations (malloc, calloc, realloc, free)
- ✅ Works with any program using standard malloc
- ✅ No brk interception needed (malloc doesn't call brk internally)
- ✅ Clean separation from libc's heap

**Cons**:
- ❌ Need to implement full malloc interface (calloc, realloc, posix_memalign, etc.)
- ❌ Need to track allocation sizes for free()
- ❌ Bootstrap complexity (need libc malloc during HeMem init)
- ❌ May conflict with applications using custom allocators (Hoard, jemalloc, tcmalloc)

**Implementation**:
```c
// Wrapper to track sizes
typedef struct {
    size_t size;
    char data[];
} alloc_header_t;

void* malloc(size_t size) {
    if (!is_init || internal_call) {
        return libc_malloc(size);  // Use libc during bootstrap
    }
    
    // Allocate with header to track size
    size_t total = sizeof(alloc_header_t) + size;
    alloc_header_t *hdr = hemem_mmap(NULL, total, PROT_READ|PROT_WRITE,
                                      MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    if (hdr == MAP_FAILED) return NULL;
    
    hdr->size = size;
    return hdr->data;
}

void free(void* ptr) {
    if (ptr == NULL) return;
    if (!is_init || internal_call) {
        libc_free(ptr);
        return;
    }
    
    alloc_header_t *hdr = (alloc_header_t*)((char*)ptr - sizeof(alloc_header_t));
    hemem_munmap(hdr, sizeof(alloc_header_t) + hdr->size);
}
```

### Strategy 2: Override operator new/delete (C++)
**Required for C++ programs like MERCI**

**How it works**:
```cpp
// In interpose.cpp or separate file
void* operator new(size_t size) {
    return hemem_malloc(size);
}

void operator delete(void* ptr) noexcept {
    hemem_free(ptr);
}

// Also override array versions
void* operator new[](size_t size);
void operator delete[](void* ptr) noexcept;
```

**Pros**:
- ✅ Intercepts C++ allocations (new/delete)
- ✅ Required for C++ programs like MERCI

**Cons**:
- ❌ Doesn't catch malloc/free (C++ can still use those)
- ❌ Need to implement full operator overload set
- ❌ May conflict with custom allocators

### Strategy 3: Use Custom Allocator (Hoard Integration)
**Leverage existing allocator**

HeMem already links with Hoard (`-lhoard`). We could:
1. Modify Hoard to use HeMem as its backend
2. Let Hoard handle malloc/free interface
3. HeMem only manages underlying memory regions

**Pros**:
- ✅ Hoard already implements full malloc interface
- ✅ Hoard handles thread-local caching, size classes, etc.
- ✅ We just provide memory regions to Hoard

**Cons**:
- ❌ Requires modifying Hoard source
- ❌ Adds dependency on Hoard's internal APIs

### Strategy 4: Intercept BRK After Libc Init (Complex)
**Try to take over brk region after libc finishes using it**

**How it works**:
1. During bootstrap: let libc use brk normally
2. After HeMem init: try to "take over" the brk region
3. Use mremap() to remap libc's heap into HeMem's address space
4. Intercept future brk calls

**Pros**:
- ✅ Complete control over all memory

**Cons**:
- ❌ Extremely complex and fragile
- ❌ May not work with libc's internal state
- ❌ Probably not worth the effort

### Strategy 5: Hybrid Approach (Recommended)
**Combine multiple strategies**

1. **Override malloc/free/new/delete** for application allocations
2. **Let libc use brk** for its own internal allocations
3. **Track brk for statistics** (current implementation)
4. **Intercept mmap** for large allocations (already working)

**Implementation Priority**:
```
Phase 1 (Current): Track brk, intercept mmap
Phase 2: Override malloc/free/calloc/realloc
Phase 3: Override operator new/delete for C++
Phase 4: Handle edge cases (posix_memalign, aligned_alloc, etc.)
```

## Recommended Next Steps

### Immediate: Enhance BRK Tracking
Currently implemented - tracks brk calls for statistics:
```c
static int brk_filter(void *addr, uint64_t *result)
{
  if (is_init && addr != NULL) {
    void *old_break = libc_sbrk(0);
    intptr_t increment = (intptr_t)addr - (intptr_t)old_break;
    if (increment > 0) {
      brk_intercepted_bytes += increment;
      brk_intercepted_count++;
    }
  }
  return 1;  // Pass through to libc
}
```

### Phase 2: Implement malloc/free Interception
```bash
# Create src/malloc_interpose.c
- Implement malloc, calloc, realloc, free
- Use header to track allocation sizes
- Bootstrap check (use libc during init)
- Update Makefile to include malloc_interpose.o
```

### Phase 3: Test with MERCI
```bash
# Run MERCI with full malloc interception
LD_PRELOAD=libhemem-runtime.so merci ...
# Check stats:
# - brk_intercepted should be ~0 (malloc doesn't use brk)
# - mem_allocated should match RSS
# - No "free(): invalid pointer" errors
```

## Expected Results

With complete malloc/free interception:
```
Before (current):
  mem_allocated: 512 MB          (only large mmap)
  dram_small_allocation_bytes: 6189 MB  (small mmap)
  brk_intercepted: 134 MB        (brk tracking)
  Peak RSS: 13 GB                (MISMATCH!)

After (with malloc interception):
  mem_allocated: 13 GB           (all allocations)
  dram_small_allocation_bytes: 0 (none)
  brk_intercepted: ~0 MB         (malloc doesn't use brk)
  Peak RSS: 13 GB                (MATCH!)
```

## References
- Hoard source: `/users/hjcoffey/arms/Hoard/`
- Current interpose: `/users/hjcoffey/arms/src/interpose.c`
- glibc malloc internals: https://sourceware.org/glibc/wiki/MallocInternals
- LD_PRELOAD tricks: https://rafalcieslak.wordpress.com/2013/04/02/dynamic-linker-tricks-using-ld_preload-to-cheat-inject-features-and-investigate-programs/
