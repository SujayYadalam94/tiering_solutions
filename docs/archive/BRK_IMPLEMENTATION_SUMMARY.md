# BRK/SBRK Full Interception - Implementation Summary

## Problem Solved

Previously, HeMem only **tracked** brk/sbrk calls for statistics but didn't intercept them. This meant:
- Heap memory bypassed HeMem management
- Memory usage discrepancy (HeMem stats didn't match RSS)
- Applications couldn't be fully managed by HeMem's tiering policies

## Solution Implemented

Created a **separate HeMem-managed heap** that:
- Uses HeMem's DAX devices for physical backing
- Doesn't conflict with libc's internal heap
- Handles bootstrap safely (libc can still allocate during init)
- Fully intercepts both brk() and sbrk() calls

## How It Works

### 1. Heap Initialization
```c
// Reserve 16 GB virtual address space (configurable)
void* heap = mmap(NULL, HEMEM_HEAP_SIZE, PROT_NONE, 
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
```
- Uses `MAP_NORESERVE` - no physical memory committed upfront
- Virtual space only, pages allocated on-demand

### 2. Heap Expansion (sbrk/brk)
```
Application calls: sbrk(64 MB)
  ↓
1. Calculate new break position
2. Round to 2MB boundaries (huge pages)
3. Allocate via: hemem_mmap(..., MAP_FIXED)
4. Update current break
5. Track statistics
```

### 3. Safe Bootstrap
```c
void* sbrk(intptr_t increment) {
    // Before init or during internal calls: use libc
    if (!is_init || internal_call) 
        return libc_sbrk(increment);
    
    // After init: use HeMem
    return hemem_sbrk(increment);
}
```

### 4. Interception Points
- **Syscall level**: SYS_brk caught by libsyscall_intercept
- **Library level**: sbrk()/brk() overridden via LD_PRELOAD

## Key Design Decisions

### Why Separate Heap?
- ❌ Can't take over libc's heap (already in use, causes corruption)
- ✅ Separate heap = clean separation, no conflicts
- ✅ Libc can still allocate during HeMem bootstrap

### Why 2MB Alignment?
- HeMem uses 2MB huge pages
- Reduces TLB pressure
- Consistent with rest of HeMem design
- Some internal fragmentation, but negligible

### Why MAP_FIXED?
- brk/sbrk requires **contiguous** heap growth
- MAP_FIXED ensures pages mapped at exact addresses
- Safe because we reserved the VA space upfront

## File Changes Summary

### src/interpose.c (Main Implementation)
- Added `hemem_heap_init()` - Reserve heap VA space
- Added `hemem_sbrk()` - sbrk using HeMem DAX devices
- Added `hemem_brk()` - brk using HeMem DAX devices
- Modified `brk_filter()` - Full interception instead of tracking
- Added library-level `sbrk()`/`brk()` overrides
- Added `hemem_heap_print_stats()` - Statistics

### src/interpose.h
- Added function declarations
- Added libc function pointer externs

### src/hemem.c
- Added call to heap stats in shutdown

## Configuration

```bash
# Heap size (default: 16 GB)
export HEMEM_HEAP_SIZE=$((4 * 1024 * 1024 * 1024))

# Run with HeMem
LD_PRELOAD=./src/libhemem-runtime.so ./application
```

## Statistics Output

```
brk_intercepted: [123 calls, 1234567890 bytes (1177.38 MB)]

HeMem-managed heap: [0x7f0000000000, 0x7f0400000000) current=0x7f0010000000
  Size: 16384.00 MB
  Used: 256.00 MB (1.6%)
  Available: 16128.00 MB
```

## Testing

### Unit Test
```bash
cd tests
gcc -o test test_brk_full_interception.c
./test
```
Tests basic functionality without HeMem (uses libc).

### Runtime Test
```bash
cd tests
./test_brk_full_runtime.sh
```
Tests actual interception with HeMem loaded via LD_PRELOAD.
Requires access to /dev/dax devices.

## What This Enables

✅ **Complete heap management** - All heap allocations via HeMem  
✅ **Tiering policies** - Heap memory can be tiered between DRAM/NVM  
✅ **Accurate statistics** - mem_allocated matches actual usage  
✅ **Multi-region support** - Heap can be assigned to specific regions  

## Thread Safety

All heap operations protected by mutex:
```c
static pthread_mutex_t hemem_brk_lock;

void* hemem_sbrk(intptr_t increment) {
    pthread_mutex_lock(&hemem_brk_lock);
    // ... perform allocation ...
    pthread_mutex_unlock(&hemem_brk_lock);
}
```

## Limitations

1. **Fixed heap size** - Can't grow beyond HEMEM_HEAP_SIZE (future enhancement)
2. **Virtual address space** - Reserves large VA range (not a problem on 64-bit)
3. **2MB alignment** - Some internal fragmentation (typically negligible)

## Future Enhancements

1. **malloc/free interception** - Complete allocator interface
2. **C++ support** - operator new/delete overrides
3. **Dynamic heap sizing** - Grow beyond initial reservation
4. **Per-thread caching** - Reduce lock contention

## Documentation

- **Full details**: `docs/BRK_FULL_INTERCEPTION.md`
- **Usage guide**: `docs/USAGE_BRK_INTERCEPTION.md`
- **Changelog**: `BRK_INTERCEPTION_CHANGELOG.md`

## Code Locations

- Implementation: `src/interpose.c:25-197`
- Heap init: `src/interpose.c:35-89`
- sbrk handler: `src/interpose.c:91-152`
- brk handler: `src/interpose.c:154-181`
- Syscall filter: `src/interpose.c:263-279`
- Library overrides: `src/interpose.c:357-386`

## Verification

Build and test:
```bash
cd /users/hjcoffey/arms/src
make clean && make

cd ../tests
./test_brk_full_runtime.sh
```

Expected output:
```
✓ Test program completed successfully
✓ HeMem brk interception active
✓ HeMem heap statistics printed
```
