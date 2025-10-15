# BRK/SBRK Interception Implementation

## Overview
This document describes the implementation of brk/sbrk interception in HeMem to handle heap expansion requests.

## Problem
MERCI workload uses sbrk/brk calls heavily for heap expansion. Without interception, these allocations bypass HeMem entirely, causing:
- Memory usage discrepancy (mem_allocated shows only 34 MB but RSS shows 13 GB)
- "free(): invalid pointer" errors due to pointer conflicts between HeMem and libc malloc

## Solution
Implemented brk syscall **tracking** (not interception) to monitor heap allocations for statistics.

### Why We Don't Intercept BRK

**Critical Issue**: We cannot safely intercept brk calls because:
1. libc's malloc uses brk during HeMem bootstrap (before is_init=true)
2. The brk region is already partially mapped by libc
3. Using MAP_FIXED would overwrite libc's heap causing corruption
4. Without MAP_FIXED, we can't guarantee contiguous heap growth required by brk

**Current Approach**: Track brk calls for statistics but let them pass through to libc

### Implementation Details

#### Files Modified
1. **src/interpose.c** - BRK tracking logic
   - Added `brk_filter()` function to track brk calls
   - Queries current break via libc_sbrk(0)
   - Calculates increment and tracks in brk_intercepted_bytes/count
   - Passes all calls through to libc (return 1)
   
2. **src/hemem.h** - Added tracking counters
   - `extern uint64_t brk_intercepted_bytes` - Total bytes allocated via brk
   - `extern uint64_t brk_intercepted_count` - Number of brk calls tracked

3. **src/hemem.c** - Stats reporting
   - Added brk_intercepted stats to LOG_STATS output
   - Format: "brk_intercepted: [%lu calls, %lu bytes (%.2f MB)]"

### Design Decisions

#### 1. Track but don't intercept
**Rationale**: Safety and correctness over completeness
- Intercepting brk causes memory corruption
- Tracking provides visibility into heap usage
- Focus on mmap interception (which works safely)

#### 2. Only track after bootstrap
**Rationale**: Avoid counting HeMem's own allocations
- Check `is_init` flag before tracking
- Ensures we only count application's brk usage

#### 3. Calculate increments dynamically
**Rationale**: Don't maintain our own break state
- Query libc_sbrk(0) to get current break on each call
- Calculate increment from old to new break
- Avoids state synchronization issues

### Code Flow
```
User calls sbrk(64MB)
  → libc sbrk() calls brk(current_break + 64MB)
    → syscall SYS_brk intercepted by hook()
      → brk_filter() called
        → Query libc_sbrk(0) to get old_break
        → Calculate increment = addr - old_break
        → Track: brk_intercepted_bytes += increment
        → Track: brk_intercepted_count++
        → Return 1 (pass through to libc)
    → Libc brk() executes normally
  → Heap expands via libc
```

## Testing

### Unit Tests (test_brk_interception.c)
**Purpose**: Verify sbrk() basic functionality and memory integrity

**Important**: These tests do NOT test actual interception because they're not loaded via LD_PRELOAD. They verify:
- sbrk() allocations work
- Memory can be written/read correctly
- No crashes or corruption

**Tests**:
1. `test_sbrk_large_allocation` - 64MB allocation
2. `test_sbrk_small_allocation` - 1MB allocation
3. `test_brk_interception_stats` - Stats tracking
4. `test_sbrk_multiple_small_calls` - Multiple 100KB calls (rounding)
5. `test_sbrk_mixed_sizes` - Mixed small/large allocations
6. `test_sbrk_memory_integrity` - Data integrity verification

**Run**: `cd tests && make test`

### Runtime Test (test_brk_runtime.sh)
**Purpose**: Test actual syscall interception with LD_PRELOAD

**Requirements**: Access to /dev/dax devices (root permissions or configured permissions)

**What it tests**:
- Compiles simple program that calls sbrk(64MB)
- Runs with LD_PRELOAD=libhemem-runtime.so
- Checks if brk_intercepted_count > 0 in stats output

**Status**: Currently skips if no DAX device access

**Run**: `cd tests && ./test_brk_runtime.sh`

## Current Status

### ✅ Completed
- [x] brk_filter implementation (tracking only)
- [x] Tracking counters (brk_intercepted_bytes, brk_intercepted_count)
- [x] Stats reporting with units (MB)
- [x] Unit tests for basic sbrk() functionality
- [x] Runtime test skeleton
- [x] All tests integrated into `make test`
- [x] Documentation of why we can't intercept brk

### ✅ Verified Working
- MERCI runs successfully without crashes
- brk calls are tracked for statistics
- mmap interception works correctly (6+ GB intercepted)

### 🔍 Known Limitations

1. **brk allocations not managed by HeMem**
   - Tracked for statistics only
   - Memory goes to libc's heap
   - Not included in mem_allocated counter

2. **Memory discrepancy remains**
   - Peak RSS > tracked memory
   - Likely due to allocations using malloc/new instead of direct mmap
   - **Solution**: Implement malloc/free interception (see HEAP_MANAGEMENT_STRATEGIES.md)

### 📋 Next Steps

1. **Implement malloc/free interception** (Strategy 1 from HEAP_MANAGEMENT_STRATEGIES.md)
   - Override malloc, calloc, realloc, free
   - Use allocation headers to track sizes
   - Bootstrap using libc malloc

2. **Add C++ operator new/delete overrides** (for MERCI)
   - Override all new/delete variants
   - Route to HeMem's malloc/free

3. **Validate with MERCI**
   - mem_allocated should match peak RSS
   - brk_intercepted should drop to ~0 (malloc won't use brk)
   - No "free(): invalid pointer" errors

## Statistics Output
BRK tracking shows how much memory is allocated via brk (outside HeMem management):
```
mem_allocated: [536870912 bytes (512.00 MB)]          # Large mmap allocations
dram_small_allocation_bytes: [6490083328 bytes (6189.43 MB)]  # Small mmap allocations
brk_intercepted: [134217728 bytes (128.00 MB)]        # Tracked brk allocations (NOT managed)
pages_allocated: [3270 (6.54 GB with 2MB pages)]
```

After implementing malloc/free interception, brk_intercepted should drop to near-zero because malloc won't use brk internally.

## References
- `man 2 brk` - brk/sbrk system call documentation
- libsyscall_intercept - syscall interception library
- `src/interpose.c:95-124` - brk_filter tracking implementation
- `tests/test_brk_interception.c` - Unit tests
- `tests/test_brk_runtime.sh` - Runtime test
- `docs/HEAP_MANAGEMENT_STRATEGIES.md` - Strategies for complete heap management
