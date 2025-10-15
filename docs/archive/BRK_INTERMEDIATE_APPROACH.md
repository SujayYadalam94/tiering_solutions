# HeMem brk/sbrk Interception Implementation

## Summary

Added brk/sbrk syscall interception to HeMem to capture heap expansions from applications that use sbrk (like MERCI). This allows HeMem to manage memory allocated via the heap in addition to direct mmap calls.

## Problem

MERCI workload was allocating ~13GB of memory but HeMem was only tracking ~34MB. Investigation revealed:
- MERCI uses C++ std::vector which internally uses sbrk for large allocations
- HeMem was only intercepting mmap syscalls, not brk/sbrk
- Most of the workload's memory was escaping HeMem's tracking

## Implementation

### 1. Added brk Syscall Interception (`src/interpose.c`)

**New Filter Function** (`brk_filter`):
- Intercepts `SYS_brk` syscalls
- Checks if expansion is large enough (>= `MIN_INTERPOSE_MEM_SIZE`)
- Routes large heap expansions through HeMem's mmap infrastructure
- Falls back to libc for:
  - Internal calls
  - Heap shrinking
  - Small allocations (< MIN_INTERPOSE_MEM_SIZE)

**Key Logic**:
```c
// Get current program break
void *old_brk = libc_sbrk(0);

// Calculate expansion size
intptr_t increment = (intptr_t)addr - (intptr_t)old_brk;

// If large enough, route through HeMem
if (increment >= min_interpose_mem_size) {
    void *new_region = hemem_mmap(old_brk, increment, 
                                   PROT_READ | PROT_WRITE,
                                   MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
                                   -1, 0);
}
```

### 2. Added Tracking Statistics

**New Counters** (`src/hemem.h`, `src/hemem.c`):
- `brk_intercepted_bytes` - Total bytes intercepted via brk
- `brk_intercepted_count` - Number of brk interceptions

**Updated Stats Output** (`hemem_print_stats`):
```
mem_allocated: [X bytes (Y MB)]  dram_small_allocation_bytes: [A bytes (B MB)]  
brk_intercepted: [N calls, M bytes (P MB)]
pages_allocated: [Q (R GB with 2MB pages)]  ...
```

### 3. Improved Stats Reporting

Enhanced the stats output to include units for clarity:
- Shows bytes AND MB/GB for all memory measurements
- Clearly indicates pages_allocated is a count, with size in GB
- Helps disambiguate "running balance" vs "cumulative counter" metrics

### 4. Added Unit Tests

Created `tests/test_brk_interception.c` with tests for:
- Large sbrk allocations (>= MIN_INTERPOSE_MEM_SIZE)
- Small sbrk allocations (< MIN_INTERPOSE_MEM_SIZE)
- Brk interception statistics tracking

## Files Modified

1. **src/interpose.c**
   - Added `libc_sbrk` and `libc_brk` function pointers
   - Implemented `brk_filter()` function
   - Updated `hook()` to intercept `SYS_brk`
   - Updated `init()` to bind sbrk/brk symbols

2. **src/hemem.h**
   - Added `brk_intercepted_bytes` extern declaration
   - Added `brk_intercepted_count` extern declaration

3. **src/hemem.c**
   - Added `brk_intercepted_bytes` global variable
   - Added `brk_intercepted_count` global variable
   - Enhanced `hemem_print_stats()` with units and brk statistics

4. **tests/test_brk_interception.c** (NEW)
   - Comprehensive unit tests for brk interception

5. **tests/hemem_stubs.c**
   - Added stub variables for brk counters

6. **tests/Makefile**
   - Added `test_brk_interception` target
   - Updated `test` target to run brk tests

## How It Works

### Normal mmap Interception (existing)
```
Application → mmap() → syscall(SYS_mmap) → hook() → mmap_filter() → hemem_mmap()
```

### New brk Interception
```
Application → sbrk(N) → brk(addr) → syscall(SYS_brk) → hook() → brk_filter() → hemem_mmap()
```

### Decision Flow in brk_filter()
```
1. Is this an internal HeMem call? → YES: use libc
2. Is HeMem initialized? → NO: use libc
3. Is this heap shrinking? → YES: use libc #TODO HC: What if we do a large allocation and then shrink it by a small amount? Maybe all of the heap should be managed by HeMem? Intercept every sbrk call and manage the entire heap?
4. Is increment < MIN_INTERPOSE_MEM_SIZE? → YES: use libc, track in dram_small_allocation_bytes
5. Otherwise → Use HeMem via hemem_mmap()
```

## Configuration

The interception respects the existing `MIN_INTERPOSE_MEM_SIZE` environment variable:
- Default: 1GB (defined in `interpose.h`)
- Can be overridden via environment variable
- Applies to both mmap and brk interception

## Testing

### Unit Tests
```bash
cd tests
make test_brk_interception
./test_brk_interception
```

### With MERCI
```bash
export MIN_INTERPOSE_MEM_SIZE=16777216  # 16MB
export LD_PRELOAD=/path/to/libhemem-runtime.so
./eval_baseline --dataset amazon_All -r 1 -c 8
```

Expected output should now show:
- `brk_intercepted_count` > 0
- `brk_intercepted_bytes` showing significant memory
- More pages being tracked by HeMem

## Statistics Clarification

The improved stats output now clearly distinguishes:

1. **mem_allocated** (RUNNING BALANCE)
   - Current memory held by HeMem
   - Increments on allocation, DECREMENTS on free
   - Shows snapshot at program end

2. **pages_allocated** (CUMULATIVE COUNTER)
   - Total page faults handled over entire run
   - Never decrements
   - With 2MB pages, multiply by 2 to get GB

3. **dram_small_allocation_bytes** (CUMULATIVE)
   - Total small allocations bypassing HeMem
   - Accumulates over entire run

4. **brk_intercepted_bytes** (CUMULATIVE)
   - Total heap expansions intercepted
   - Shows how much sbrk activity HeMem captured

## Future Enhancements

Potential improvements:
1. Track brk contractions (heap shrinking)
2. Add per-policy brk statistics
3. Provide brk-specific configuration (separate threshold from mmap)
4. Add heap fragmentation metrics

## Compatibility

- Works with existing HeMem configurations
- Backward compatible - no breaking changes
- Can be disabled by setting very high MIN_INTERPOSE_MEM_SIZE
