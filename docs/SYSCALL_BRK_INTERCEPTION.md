# Syscall-Level brk/sbrk Interception for HeMem

**Date:** October 14, 2025  
**Status:** Implemented ✅

## Overview

HeMem intercepts **all brk/sbrk syscalls** at the kernel level, becoming the application's heap manager. This approach is completely transparent to applications and custom allocators (jemalloc, tcmalloc, etc.).

## Design Philosophy

**Key Insight:** Since we use `LD_PRELOAD`, we intercept brk/sbrk syscalls **before** they reach the kernel. This means:
1. We never use Linux's heap - we ARE the heap
2. No address translation needed - our heap IS the application's heap
3. Works with any allocator transparently

## How It Works

### 1. VA Space Reservation

On first brk syscall, we reserve a large VA space using `libc_mmap()`:

```c
void* heap = libc_mmap(NULL, 33GB,
                        PROT_NONE,  // No access yet
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                        -1, 0);
```

**MAP_NORESERVE** means:
- Kernel doesn't reserve swap space
- No physical memory allocated yet
- Just reserves VA space

### 2. On-Demand Page Allocation

When application calls `brk(addr)` or `sbrk(increment)`:
1. Calculate how many pages needed
2. Use `hemem_mmap()` with `MAP_FIXED` to allocate pages
3. Pages are backed by DAX devices (DRAM/NVM)
4. HeMem policies manage the pages

```c
// Application calls sbrk(1MB)
hemem_sbrk_internal(1MB):
  aligned_size = round_up_to_2MB(1MB) = 2MB
  hemem_mmap(current_break, 2MB, PROT_READ|PROT_WRITE, MAP_FIXED)
  // Now that 2MB is backed by DAX and managed by HeMem
```

### 3. Heap Contraction

When application shrinks the heap:
```c
// Application calls sbrk(-1MB)
hemem_sbrk_internal(-1MB):
  aligned_size = round_down_to_2MB(1MB) = 0MB (no pages to free)
  // Or if bigger: hemem_munmap(addr, size)
```

## Implementation

### Key Data Structures

```c
static void* hemem_heap_start = NULL;    // Start of heap VA space
static void* hemem_heap_current = NULL;  // Current break
static void* hemem_heap_end = NULL;      // End of reserved VA space
static size_t hemem_heap_size = 0;       // Total VA space (e.g., 33GB)
static bool hemem_heap_initialized = false;
```

### Initialization

```c
static void hemem_heap_init(void)
{
  // Calculate size (default 50% of DRAM+NVM, capped at 64GB)
  hemem_heap_size = (dramsize + nvmsize) / 2;
  
  // Reserve VA space (no physical memory yet)
  void* heap = libc_mmap(NULL, hemem_heap_size,
                          PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                          -1, 0);
  
  hemem_heap_start = heap;
  hemem_heap_current = heap;  // Empty heap initially
  hemem_heap_end = heap + hemem_heap_size;
}
```

### sbrk Implementation

```c
static void* hemem_sbrk_internal(intptr_t increment)
{
  void* old_break = hemem_heap_current;
  void* new_break = old_break + increment;
  
  // Check VA bounds
  if (new_break > hemem_heap_end) {
    return (void*)-1;  // Out of VA space
  }
  
  // Expanding: allocate pages
  if (increment > 0) {
    aligned_old = round_down_to_2MB(old_break);
    aligned_new = round_up_to_2MB(new_break);
    
    if (aligned_new > aligned_old) {
      size = aligned_new - aligned_old;
      hemem_mmap(aligned_old, size, PROT_READ|PROT_WRITE, MAP_FIXED);
    }
  }
  // Contracting: free pages
  else if (increment < 0) {
    aligned_old = round_up_to_2MB(old_break);
    aligned_new = round_down_to_2MB(new_break);
    
    if (aligned_new < aligned_old) {
      size = aligned_old - aligned_new;
      hemem_munmap(aligned_new, size);
    }
  }
  
  hemem_heap_current = new_break;
  return old_break;
}
```

### brk Implementation

```c
int hemem_brk(void* addr)
{
  intptr_t increment = addr - hemem_heap_current;
  return (hemem_sbrk_internal(increment) == (void*)-1) ? -1 : 0;
}
```

### Syscall Filter

```c
static int brk_filter(void *addr, uint64_t *result)
{
  // Skip internal calls
  if (!is_init || internal_call) {
    return 1;  // Pass to libc
  }
  
  // Initialize on first call
  if (!hemem_heap_initialized) {
    hemem_heap_init();
  }
  
  // Handle brk(0) query
  if (addr == 0) {
    *result = (uint64_t)hemem_heap_current;
    return 0;
  }
  
  // Handle brk(addr)
  if (hemem_brk(addr) == 0) {
    *result = (uint64_t)addr;
    return 0;
  }
  
  return 1;  // Fall back to libc on error
}
```

## Memory Layout

```
VA Space Layout:
┌──────────────────────────────────────────────────────────┐
│  0x0                    Application Code/Data            │
├──────────────────────────────────────────────────────────┤
│  0x555555554000         Executable                       │
│  0x555555756000         Data/BSS                         │
├══════════════════════════════════════════════════════════┤
│  hemem_heap_start       HeMem Heap (IS the app heap)     │
│   │                                                       │
│   │  [PROT_NONE VA space, 33GB reserved]                 │
│   │                                                       │
│   │  ┌─────────────────────────────────┐                 │
│   │  │ Allocated region (backed by DAX) │                │
│   ├──┤ PROT_READ|PROT_WRITE             │                │
│   │  └─────────────────────────────────┘                 │
│   │                                                       │
│  hemem_heap_current  ← Current break                     │
│   │                                                       │
│   │  [Unallocated VA space]                              │
│   │                                                       │
│  hemem_heap_end                                          │
├══════════════════════════════════════════════════════════┤
│  0x7f...                Shared Libraries, Stack, etc.    │
└──────────────────────────────────────────────────────────┘
```

## Interaction with Allocators

### glibc malloc
```
Application: malloc(1MB)
    ↓
glibc malloc: Need more heap space
    ↓
syscall(SYS_brk, current + 1MB)
    ↓
libsyscall_intercept: brk_filter()
    ↓
hemem_sbrk_internal(1MB)
    ↓
hemem_mmap(addr, 2MB, MAP_FIXED)  // Allocate aligned
    ↓
Pages backed by DAX devices
    ↓
HeMem policies manage the pages
```

### jemalloc
```
Application: malloc(1MB)
    ↓
jemalloc: Need arena space
    ↓
syscall(SYS_brk, addr) or syscall(SYS_mmap, ...)
    ↓
Both intercepted by HeMem!
    ↓
Managed by HeMem policies
```

**Key Point:** This works with **any allocator** because we intercept at the syscall level.

## Configuration

### Heap Size

```bash
export HEMEM_HEAP_SIZE=$((16*1024*1024*1024))  # 16GB
```

**Default:** 50% of (DRAMSIZE + NVMSIZE), capped at 64GB

**Considerations:**
- Too small: Application runs out of heap space
- Too large: Wastes VA space (but not physical memory!)
- Recommendation: Set to expected max heap usage

### Running with HeMem

```bash
export DRAMPATH=/dev/dax0.0
export NVMPATH=/dev/dax1.0
export DRAMSIZE=$((2*1024*1024*1024))
export NVMSIZE=$((64*1024*1024*1024))

LD_PRELOAD=./src/libhemem-runtime.so ./your_app
```

## Advantages Over malloc/free Interception

| Aspect | brk/sbrk Syscall | malloc/free Override |
|--------|------------------|---------------------|
| **Transparency** | Complete | Interferes with custom allocators |
| **Allocator Support** | Any (glibc, jemalloc, tcmalloc) | Only apps using libc malloc |
| **Granularity** | All heap allocations | Only large allocs (threshold) |
| **Simplicity** | Syscall level only | Tracking table needed |
| **Overhead** | Minimal | Hash table lookups |
| **Coverage** | 100% of heap | Depends on threshold |

## Limitations

### 1. 2MB Page Granularity

**Issue:** All allocations rounded to 2MB huge pages

**Example:**
```c
sbrk(100KB)  // Heap grows by 100KB
// But we allocate 2MB of pages
```

**Impact:**
- Memory amplification for small heap growth
- Typically not an issue (allocators request large chunks)

**Mitigation:**
- Allocators usually request multi-MB chunks anyway
- glibc malloc default threshold is 128KB → 2MB alignment not bad

### 2. VA Space Limit

**Issue:** Fixed VA space reservation (e.g., 33GB)

**Impact:**
- Application can't use more heap than reserved
- Fails with ENOMEM if exceeded

**Mitigation:**
- Set `HEMEM_HEAP_SIZE` appropriately
- Monitor heap usage statistics

### 3. Bootstrap Chicken-and-Egg

**Issue:** HeMem's own initialization needs memory

**Solution:**
- `internal_call` flag bypasses interception
- HeMem's init uses libc malloc/mmap directly

### 4. Not Suitable for All Workloads

**Works Best With:**
✅ Applications using standard allocators (glibc, jemalloc)
✅ Large heap usage (hundreds of MB to GBs)
✅ Long-lived allocations

**Not Ideal For:**
❌ Applications that don't use heap (stack-only)
❌ Custom memory management (mmap-only apps)
❌ Tiny heap usage (<10MB)

## Debugging

### Enable Logging

Set `DEBUG=1` in environment or code to see:
```
hemem_heap_init: reserving 35433480192 bytes (33.00 GB) VA space for heap
hemem_heap_init: heap VA space reserved at [0x7fe6b7200000, 0x7feef7200000)
hemem interpose: intercepting brk(0x7fe6b7200000)
hemem_sbrk: expanding by 135168 bytes (allocating 2097152 aligned bytes at 0x7fe6b7200000)
hemem interpose: calling hemem mmap(0x7fe6b7200000, 2097152, 3, 18, -1, 0)
```

### Check Heap Statistics

At program exit:
```
HeMem-managed heap: [0x7fe6b7200000, 0x7feef7200000) current=0x7fe6b9400000
  Size: 35433480192 bytes (33.00 GB)
  Used: 35651584 bytes (34.00 MB, 0.1%)
  Available: 35397828608 bytes (32.97 GB)
```

### Verify DAX Mappings

```bash
# While app is running
cat /proc/$(pidof your_app)/maps | grep dax
```

Should show your heap region mapped to `/dev/dax0.0` or `/dev/dax1.0`.

## Testing

### Simple Test

```c
#include <stdio.h>
#include <unistd.h>

int main() {
    void* initial = sbrk(0);
    printf("Initial break: %p\n", initial);
    
    void* after = sbrk(1024*1024);  // 1MB
    printf("After sbrk(1MB): %p\n", sbrk(0));
    
    return 0;
}
```

**Expected output:**
```
hemem_heap_init: reserving 33.00 GB VA space for heap
Initial break: 0x7fe6b7200000
hemem_sbrk: expanding by 1048576 bytes (allocating 2097152 aligned bytes)
After sbrk(1MB): 0x7fe6b7300000
```

### With jemalloc

```bash
LD_PRELOAD=./src/libhemem-runtime.so:/path/to/libjemalloc.so ./your_app
```

jemalloc will use our brk for its arenas → all allocations go through HeMem!

## Future Improvements

### 1. Dynamic VA Space Expansion

Currently fixed 33GB. Could:
- Start small (e.g., 1GB)
- Grow dynamically as needed
- Use `mremap()` to extend

### 2. Finer-Grained Allocation

Instead of always using 2MB pages:
- Use 4KB pages for small heap regions
- Switch to 2MB when region grows large
- Trade-off: complexity vs memory efficiency

### 3. Heap Compaction

Free fragmented pages:
- Track which 2MB regions are fully unused
- Unmap them to save physical memory
- Requires cooperation with allocator

## Summary

✅ **Complete transparency** - works with any allocator  
✅ **Syscall-level interception** - no malloc/free override needed  
✅ **Simple implementation** - no address translation  
✅ **On-demand allocation** - MAP_NORESERVE saves memory  
✅ **DAX-backed pages** - all heap on tiered memory  

**Result:** All heap allocations automatically managed by HeMem, regardless of allocator!

---

**See Also:**
- `docs/BRK_INTERCEPTION_GUIDE.md` - Historical attempts (address translation)
- `docs/MALLOC_FREE_INTERCEPTION.md` - Alternative approach
- `docs/MULTI_REGION_GUIDE.md` - Multi-policy configuration
