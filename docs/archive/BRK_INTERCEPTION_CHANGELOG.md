# BRK/SBRK Full Interception Implementation

## Summary

Implemented full brk/sbrk interception in HeMem to manage heap allocations through HeMem's DAX devices instead of libc's default heap.

## What Was Implemented

### Core Features
1. **HeMem-Managed Heap Region**
   - Separate heap region (default 16 GB, configurable)
   - Managed entirely through HeMem's DAX devices
   - No conflict with libc's internal heap

2. **Safe Bootstrap**
   - Libc can still use its own heap during HeMem initialization
   - Internal HeMem calls bypass interception
   - No recursion or bootstrap issues

3. **Full Interception**
   - Syscall-level interception (via libsyscall_intercept)
   - Library-level overrides (via LD_PRELOAD)
   - Both brk() and sbrk() fully supported

4. **Thread-Safe**
   - Mutex protection for heap operations
   - Safe concurrent access from multiple threads

### Files Modified
- `src/interpose.c` - Core implementation
- `src/interpose.h` - Function declarations
- `src/hemem.c` - Statistics integration
- `tests/test_brk_full_interception.c` - Unit tests
- `tests/test_brk_simple.c` - Simple runtime test
- `tests/test_brk_full_runtime.sh` - Runtime test script
- `docs/BRK_FULL_INTERCEPTION.md` - Complete documentation
- `docs/USAGE_BRK_INTERCEPTION.md` - Usage guide

## Quick Start

### Build
```bash
cd src
make clean && make
```

### Run Application
```bash
export DRAMPATH=/dev/dax0.0
export NVMPATH=/dev/dax1.0
export DRAMSIZE=$((8 * 1024 * 1024 * 1024))
export NVMSIZE=$((16 * 1024 * 1024 * 1024))
export HEMEM_HEAP_SIZE=$((2 * 1024 * 1024 * 1024))

LD_PRELOAD=./src/libhemem-runtime.so ./your_application
```

### Test
```bash
cd tests
./test_brk_full_runtime.sh
```

## Configuration

### HEMEM_HEAP_SIZE
Size of HeMem-managed heap (default: 16 GB)
```bash
export HEMEM_HEAP_SIZE=$((4 * 1024 * 1024 * 1024))  # 4 GB
```

### MIN_INTERPOSE_MEM_SIZE  
Minimum mmap size to intercept (default: 1 GB)
```bash
export MIN_INTERPOSE_MEM_SIZE=$((1 * 1024 * 1024))  # 1 MB
```

## How It Works

1. **Initialization**: HeMem reserves a large virtual address space for the heap (using MAP_NORESERVE)
2. **Expansion**: When application calls sbrk/brk, HeMem allocates 2MB-aligned pages from DAX devices
3. **Mapping**: Pages are mapped with MAP_FIXED to ensure contiguous heap growth
4. **Management**: Allocated pages are tracked and managed by HeMem's tiering policies

## Key Benefits

✅ Complete control over heap memory  
✅ All heap allocations go through HeMem tiering policies  
✅ Consistent with mmap interception  
✅ No memory corruption or bootstrap issues  
✅ Thread-safe implementation  
✅ Compatible with multi-region policies  

## Statistics Output

At program exit:
```
brk_intercepted: [123 calls, 1234567890 bytes (1177.38 MB)]
HeMem-managed heap: [0x7f00..., 0x7f40...) current=0x7f10...
  Size: 16384.00 MB
  Used: 1234.56 MB (7.5%)
  Available: 15149.44 MB
```

## Next Steps

1. **malloc/free interception** - Provide complete allocator interface
2. **C++ operator new/delete** - Support for C++ applications
3. **Performance optimization** - Per-thread caching, async expansion

## Documentation

- **Full Implementation**: `docs/BRK_FULL_INTERCEPTION.md`
- **Usage Guide**: `docs/USAGE_BRK_INTERCEPTION.md`
- **Old Approach** (tracking only): `tests/BRK_INTERCEPTION.md`
- **Heap Management Strategies**: `docs/HEAP_MANAGEMENT_STRATEGIES.md`

## Testing

Unit tests verify:
- Small allocations (1 MB)
- Large allocations (64 MB)
- Multiple allocations
- Direct brk() calls
- Heap shrinking
- Data integrity
- Memory contiguity

Runtime tests verify:
- Actual interception via LD_PRELOAD
- Integration with HeMem DAX devices
- Statistics collection

## Notes

- **Virtual Address Space**: Reserves large VA range (not a problem on 64-bit systems)
- **Alignment**: 2MB internal alignment for huge pages (some internal fragmentation)
- **Bootstrap**: Libc still uses its own heap during HeMem init (by design)
- **Two Heaps**: Separate heaps for libc (internal) and application (HeMem-managed)

## Author

hjcoffey - October 2025
