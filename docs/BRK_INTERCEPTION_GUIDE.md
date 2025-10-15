# HeMem BRK/SBRK Interception Complete Guide

## Table of Contents
1. [Overview](#overview)
2. [Implementation](#implementation)
3. [Configuration](#configuration)
4. [Usage](#usage)
5. [Important Caveats](#important-caveats)
6. [Troubleshooting](#troubleshooting)
7. [History and Evolution](#history-and-evolution)

---

## Overview

HeMem implements **full interception** of brk/sbrk system calls to manage heap allocations through HeMem's DAX devices. This allows complete control over application memory, enabling tiering policies to manage heap memory alongside mmap allocations.

### What Problem Does This Solve?

**Before BRK Interception:**
- Applications using brk/sbrk (via malloc, std::vector, etc.) bypassed HeMem
- Memory usage discrepancy: HeMem tracked only mmap, missing heap allocations
- Couldn't apply tiering policies to heap memory
- OOM issues due to untracked memory consumption

**After BRK Interception:**
- All heap allocations managed by HeMem
- Complete memory tracking and statistics
- Tiering policies apply to both heap and mmap regions
- Accurate memory accounting prevents OOM

### Key Features

✅ **Full heap management** - All brk/sbrk calls intercepted after initialization  
✅ **Lazy initialization** - Heap only created if application uses brk/sbrk  
✅ **Smart sizing** - Automatically sized to 50% of physical memory  
✅ **Safe bootstrap** - libc can use brk during HeMem initialization  
✅ **Thread-safe** - Mutex-protected heap operations  
✅ **Compatible with multi-region policies** - Heap memory can be assigned to regions  

---

## Implementation

### Architecture

HeMem creates a **separate heap region** managed entirely through DAX devices:

```
Memory Layout:
┌─────────────────────────────────┐
│ libc's heap                     │  ← Used by libc internally
│   - Allocations during init     │     and HeMem's own allocations
│   - Internal libc needs         │
└─────────────────────────────────┘
         ... VA space ...
┌─────────────────────────────────┐
│ HeMem-managed heap              │  ← Application's brk/sbrk calls
│   - Application sbrk() calls    │     go here after HeMem init
│   - Managed by tiering policies │
│   - Uses DAX devices            │
└─────────────────────────────────┘
```

### How It Works

#### 1. Lazy Initialization

The heap is **not** created at startup. Instead, it's initialized on the **first brk/sbrk call**:

```c
void* hemem_sbrk(intptr_t increment) {
    if (!hemem_heap_initialized) {
        hemem_heap_init();  // Create heap on first use
    }
    // ... handle allocation
}
```

**Benefits:**
- No VA space wasted if application never uses brk
- Faster startup
- Zero overhead for mmap-only applications

#### 2. Smart Default Sizing

Heap size is automatically calculated based on available physical memory:

```c
// Calculate 50% of total physical memory
uint64_t total = dramsize + nvmsize;
hemem_heap_size = total / 2;

// Constraints:
// - Maximum: 64 GB
// - Minimum: 256 MB
```

**Example Sizing:**
```
DRAM=8GB, NVM=16GB → Heap=12GB (50% of 24GB)
DRAM=4GB, NVM=4GB  → Heap=4GB (50% of 8GB)
DRAM=128GB, NVM=256GB → Heap=64GB (capped at max)
```

#### 3. Heap Expansion

When application calls `sbrk(size)`:

1. Check if heap initialized (initialize if needed)
2. Calculate new break position
3. Round to 2MB boundaries (huge page alignment)
4. Allocate via `hemem_mmap()` with `MAP_FIXED`
5. Update current break
6. Track statistics

```
Application: sbrk(100 MB)
  ↓
Aligned old break: 0x7f0000000000 (2MB boundary)
Aligned new break: 0x7f0006600000 (2MB boundary)
  ↓
hemem_mmap(0x7f0000000000, 102 MB, MAP_FIXED)
  ↓
Pages allocated from DAX devices
  ↓
Managed by HeMem tiering policies
```

#### 4. Dual-Level Interception

Interception happens at two levels:

**Syscall Level** (via libsyscall_intercept):
```c
static int brk_filter(void *addr, uint64_t *result) {
    if (!is_init || internal_call)
        return 1;  // Pass to libc
    
    *result = hemem_brk(addr);
    return 0;  // Handled by HeMem
}
```

**Library Level** (via LD_PRELOAD):
```c
void* sbrk(intptr_t increment) {
    if (!is_init || internal_call)
        return libc_sbrk(increment);
    
    return hemem_sbrk(increment);
}
```

#### 5. Bootstrap Safety

During HeMem initialization (`is_init == false`) or internal calls (`internal_call == true`):
- brk/sbrk calls pass through to libc
- Prevents recursion
- Allows HeMem to allocate memory safely during startup

---

## Configuration

### Environment Variables

#### HEMEM_HEAP_SIZE
Size of HeMem-managed heap region (VA space reservation).

**Default:** 50% of (DRAMSIZE + NVMSIZE), capped at 64 GB

**Example:**
```bash
export HEMEM_HEAP_SIZE=$((4 * 1024 * 1024 * 1024))  # 4 GB
```

**When to set manually:**
- Heap-heavy workload: Set higher
- Many mmap allocations: Set lower (save physical for mmap)
- Known heap usage: Set to match expected usage

**When to use default:**
- Most applications
- Unknown heap usage
- Want safe automatic sizing

#### DRAMSIZE / NVMSIZE
Total physical memory available from DAX devices.

**Required:** Yes

**Example:**
```bash
export DRAMSIZE=$((8 * 1024 * 1024 * 1024))   # 8 GB
export NVMSIZE=$((16 * 1024 * 1024 * 1024))   # 16 GB
```

Used to calculate default HEMEM_HEAP_SIZE.

#### MIN_INTERPOSE_MEM_SIZE
Minimum size for mmap calls to be intercepted.

**Default:** 1 GB

**Example:**
```bash
export MIN_INTERPOSE_MEM_SIZE=$((1 * 1024 * 1024))  # 1 MB
```

Does not affect brk interception (all brk calls intercepted).

---

## Usage

### Basic Usage

```bash
# Set up environment
export DRAMPATH=/dev/dax0.0
export NVMPATH=/dev/dax1.0
export DRAMSIZE=$((8 * 1024 * 1024 * 1024))
export NVMSIZE=$((16 * 1024 * 1024 * 1024))

# Run application with HeMem
LD_PRELOAD=/path/to/libhemem-runtime.so ./your_application
```

### With Multi-Region Policies

```bash
# Assign heap region to specific policy
export HEMEM_REGIONS="0x7f0000000000-0x7f8000000000:lru"
export HEMEM_REGION_PHYS="lru:4G:16G"

LD_PRELOAD=/path/to/libhemem-runtime.so ./your_application
```

The HeMem-managed heap will be assigned to the appropriate region based on its VA range.

### Statistics Output

At program exit, HeMem prints statistics:

**If heap was used:**
```
brk_intercepted: [123 calls, 1234567890 bytes (1177.38 MB)]

HeMem-managed heap: [0x7f0000000000, 0x7f0300000000) current=0x7f0100000000
  Size: 12.00 GB
  Used: 1.18 GB (9.8%)
  Available: 10.82 GB
```

**If heap was NOT used:**
```
brk_intercepted: [0 calls, 0 bytes (0.00 MB)]
HeMem-managed heap: not initialized (no brk/sbrk calls detected)
```

---

## Important Caveats

### 1. Two Separate Heaps

Your application has **two heaps**:
- **libc's heap**: Used during HeMem init and for internal allocations
- **HeMem's heap**: Used for application brk/sbrk after init

**Important:** Pointers from one heap cannot be freed to the other!

### 2. Fixed Heap Size

The heap has a maximum size (default: 50% of physical memory):

```
If HEMEM_HEAP_SIZE=12GB:
  - Can allocate up to 12 GB via brk/sbrk
  - Beyond that: sbrk() returns -1, errno=ENOMEM
  - Solution: Increase HEMEM_HEAP_SIZE or use mmap
```

### 3. 2MB Alignment

Internally, allocations are aligned to 2MB (huge page) boundaries:

```
Application: sbrk(1 MB)
HeMem allocates: 2 MB (aligned)
Internal fragmentation: ~1 MB
```

**Impact:** Typically <1% for normal workloads with large allocations.

### 4. Bootstrap Phase

During HeMem initialization:
- brk/sbrk calls go to libc (intentional)
- Small amount of memory may go to libc heap
- After init, all brk/sbrk go to HeMem heap

**This is by design** - allows HeMem to initialize safely.

### 5. Performance Considerations

#### First Allocation (Cold Start)
- Initial `sbrk()` includes heap initialization overhead
- Subsequent calls are fast

#### Mutex Overhead
- All heap operations protected by mutex
- Thread synchronization cost on every brk/sbrk
- Typically negligible compared to allocation cost

#### High Thread Contention
- Many threads calling sbrk frequently → potential contention
- Future enhancement: per-thread heap caching

### 6. Physical Memory Exhaustion

If heap size > physical memory available:

```
HEMEM_HEAP_SIZE=16GB, Physical=8GB
  ↓
Application allocates 8 GB: Succeeds
Application allocates 4 GB more: Fails
  ↓
hemem_mmap() returns MAP_FAILED (no physical pages)
  ↓
sbrk() returns -1, errno=ENOMEM
```

**With smart sizing (default):** This is unlikely as heap defaults to 50% of physical.

### 7. malloc/free Behavior

If your application uses `malloc/free`:
- malloc **may** use brk internally (glibc implementation detail)
- If it does → HeMem intercepts it
- If malloc uses mmap → HeMem also intercepts it (if size > MIN_INTERPOSE_MEM_SIZE)
- Small malloc → may go to libc

**Result:** Partial heap management (intercepting direct brk, and large mmap from malloc)

**For complete control:** Future work - implement malloc/free interception

### 8. Signal Safety

**Not async-signal-safe!**
- Uses mutex (pthread_mutex_lock)
- Don't call malloc/sbrk/brk from signal handlers

(This is general best practice anyway)

---

## Troubleshooting

### "hemem_sbrk: out of heap space"

**Cause:** Application requested more heap than HEMEM_HEAP_SIZE

**Solutions:**
1. Remove HEMEM_HEAP_SIZE (use auto-sizing)
2. Increase HEMEM_HEAP_SIZE manually
3. Check if DRAMSIZE/NVMSIZE match actual device sizes

**Debug:**
```bash
grep "hemem_heap_init: reserving" logs.txt
# Check what heap size was actually used
```

### "not initialized (no brk/sbrk calls detected)"

**Cause:** Application never called brk/sbrk

**This is NORMAL if:**
- Application uses only mmap
- Modern glibc's malloc uses mmap for large allocations
- Application uses custom allocator

**Not a problem:** HeMem still intercepts mmap allocations

### Heap Size Smaller Than Expected

**Check logs:**
```bash
grep "hemem_heap_init" logs.txt
# Shows: physical memory available and calculated heap size
```

**If using defaults:**
- Heap = 50% of (DRAMSIZE + NVMSIZE)
- Verify DRAMSIZE and NVMSIZE are set correctly

### Segmentation Fault During Startup

**Possible causes:**
1. Recursion during initialization
2. `internal_call` flag not set properly

**Debug:**
- Check bootstrap code sets `internal_call = true`
- Verify libc functions properly bound

### Memory Corruption / "free(): invalid pointer"

**Cause:** Mixing libc and HeMem heaps

**Should NOT happen with proper `internal_call` handling**

**If it does:**
- Ensure internal HeMem allocations use libc (set internal_call)
- Don't free HeMem-allocated memory with libc's free()

### OOM Despite Available Memory

**Historical issue (now resolved):**

This was the original problem that led to full BRK interception. Applications were allocating memory via brk that HeMem wasn't tracking, causing OOM.

**Symptoms:**
- HeMem stats show low memory usage
- RSS shows high memory usage
- System kills process for OOM

**Solution:** Now fixed with full brk interception - all heap memory tracked and managed.

---

## History and Evolution

### Phase 1: No BRK Interception (Original)
- Only intercepted mmap calls
- Heap allocations bypassed HeMem
- Led to memory tracking discrepancies and OOM issues

### Phase 2: BRK Tracking Only
- Added `brk_filter()` to track brk calls for statistics
- Did NOT intercept - passed through to libc
- Provided visibility but didn't solve OOM issues
- **Rationale:** Feared corruption from intercepting libc's heap

**Documentation:** `docs/archive/BRK_TRACKING_APPROACH.md` (archived)

### Phase 3: Full BRK Interception (Current)
- Implemented separate HeMem-managed heap
- Full interception of brk/sbrk after initialization
- Safe bootstrap handling
- Resolved OOM issues
- **Key insight:** Separate heap avoids corruption issues

### Phase 4: Lazy Initialization + Smart Sizing (Current)
- Added lazy heap initialization (only create if used)
- Smart default sizing (50% of physical memory)
- Better resource efficiency
- Automatic configuration for most use cases

---

## Testing

### Unit Tests

**test_brk_full_interception.c** - Comprehensive tests:
- Small allocations (1 MB)
- Large allocations (64 MB)
- Multiple allocations
- Direct brk() calls
- Heap shrinking
- Data integrity
- Memory contiguity

**Run:**
```bash
cd tests
gcc -o test test_brk_full_interception.c
./test
```

**test_brk_simple.c** - Simple test:
- Basic sbrk/brk functionality
- Quick verification

**Run:**
```bash
cd tests
gcc -o test_simple test_brk_simple.c
./test_simple
```

### Runtime Tests

**test_brk_full_runtime.sh** - Integration test with HeMem:
- Compiles test program
- Runs with LD_PRELOAD
- Verifies interception
- Checks statistics

**Requirements:** Access to /dev/dax devices

**Run:**
```bash
cd tests
./test_brk_full_runtime.sh
```

### Test That Heap Not Used (Lazy Init)

**test_no_brk.c** - Application that never uses brk:
- Only uses mmap
- Verifies heap not initialized

**Run:**
```bash
cd tests
gcc -o test_no_brk test_no_brk.c
LD_PRELOAD=../src/libhemem-runtime.so ./test_no_brk
# Expected: "not initialized (no brk/sbrk calls detected)"
```

---

## Files

### Implementation
- `src/interpose.c` - Core brk/sbrk interception logic
- `src/interpose.h` - Function declarations
- `src/hemem.c` - Statistics integration

### Tests
- `tests/test_brk_full_interception.c` - Comprehensive unit tests
- `tests/test_brk_simple.c` - Simple verification
- `tests/test_no_brk.c` - Lazy initialization test
- `tests/test_brk_full_runtime.sh` - Integration test script

### Documentation
- `docs/BRK_INTERCEPTION_GUIDE.md` - This document (complete guide)
- `docs/HEAP_MANAGEMENT_STRATEGIES.md` - Future malloc/free interception strategies

### Archived (Historical)
- `docs/archive/BRK_TRACKING_APPROACH.md` - Old tracking-only approach
- `docs/archive/BRK_INTERMEDIATE_APPROACH.md` - Intermediate attempts
- `docs/archive/OOM_ISSUES_RESOLVED.md` - Historical OOM problems (resolved)

---

## Summary

### What You Get

✅ Complete heap memory management through HeMem  
✅ Automatic sizing based on available physical memory  
✅ No wasted resources if heap not used  
✅ Safe bootstrap without corruption  
✅ Full statistics and visibility  
✅ Compatible with multi-region tiering policies  

### Recommended Configuration

**For most users:**
```bash
# Let auto-sizing work
export DRAMSIZE=$((your_dram_size))
export NVMSIZE=$((your_nvm_size))
# Don't set HEMEM_HEAP_SIZE

LD_PRELOAD=/path/to/libhemem-runtime.so ./your_app
```

**Check logs** to verify heap size is appropriate for your workload.

### Future Work

- malloc/free interception for complete allocator control
- C++ operator new/delete overrides
- Per-thread heap caching for reduced contention
- Dynamic heap size adjustment beyond initial reservation

---

## Contact

For issues or questions about BRK interception, refer to:
- This guide
- Source code: `src/interpose.c`
- Tests: `tests/test_brk_*.c`
