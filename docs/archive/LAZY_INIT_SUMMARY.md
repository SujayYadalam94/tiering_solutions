# Summary: Lazy Initialization and Smart Sizing

## Your Questions Answered

### Q1: What happens if DRAM size is smaller than HEAP_SIZE?

**Answer:** The heap will be **virtually reserved** (VA space only) but **physical allocation happens on-demand**. When you run out of physical memory, subsequent sbrk() calls will fail with ENOMEM.

**Now with smart sizing:** By default, HEAP_SIZE is automatically set to **50% of (DRAM + NVM)**, so this is much less likely to happen.

**Example:**
```bash
export DRAMSIZE=$((4 * 1024 * 1024 * 1024))   # 4 GB
export NVMSIZE=$((4 * 1024 * 1024 * 1024))    # 4 GB
# Auto heap size = 4 GB (50% of 8 GB)

# If you manually set:
export HEMEM_HEAP_SIZE=$((16 * 1024 * 1024 * 1024))  # 16 GB
# Then sbrk can reserve up to 16 GB VA space
# But actual physical allocations will fail after ~8 GB used
```

### Q2: What if my program never uses brk? Is heap space wasted?

**Answer:** **NO!** With the new **lazy initialization**, the heap is **only** created when your program makes its **first brk/sbrk call**.

**Before (eager):**
```
HeMem starts → Reserve 16 GB VA space immediately
Application never uses brk → 16 GB VA wasted
```

**After (lazy):**
```
HeMem starts → No heap reservation
Application never uses brk → No VA space used
```

**Statistics will show:**
```
HeMem-managed heap: not initialized (no brk/sbrk calls detected)
```

## What Changed

### 1. Smart Default Sizing
```c
// OLD: Fixed 16 GB default
hemem_heap_size = 16 GB;

// NEW: Based on physical memory
uint64_t total = dramsize + nvmsize;
hemem_heap_size = total / 2;  // 50% of physical
// Capped: max 64 GB, min 256 MB
```

### 2. Lazy Initialization
```c
// OLD: Called during init()
hemem_init();
hemem_heap_init();  // Eager

// NEW: Called on first use
void* hemem_sbrk(intptr_t increment) {
    if (!hemem_heap_initialized) {
        hemem_heap_init();  // Lazy
    }
    // ... rest
}
```

### 3. Better Logging
```
hemem_heap_init: reserving 4294967296 bytes (4.00 GB) for managed heap
hemem_heap_init: physical memory available: DRAM=4 GB, NVM=4 GB
hemem_heap_init: heap region [0x7f0000000000, 0x7f0100000000)
```

## Configuration Examples

### Let Auto-Sizing Work (Recommended)
```bash
export DRAMSIZE=$((8 * 1024 * 1024 * 1024))
export NVMSIZE=$((16 * 1024 * 1024 * 1024))
# Don't set HEMEM_HEAP_SIZE
# Result: 12 GB heap (50% of 24 GB)
```

### Manual Override (Conservative)
```bash
export DRAMSIZE=$((8 * 1024 * 1024 * 1024))
export NVMSIZE=$((16 * 1024 * 1024 * 1024))
export HEMEM_HEAP_SIZE=$((4 * 1024 * 1024 * 1024))  # 4 GB
# Result: 4 GB heap (manually set, < 50% of physical)
```

### Manual Override (Aggressive)
```bash
export DRAMSIZE=$((8 * 1024 * 1024 * 1024))
export NVMSIZE=$((16 * 1024 * 1024 * 1024))
export HEMEM_HEAP_SIZE=$((32 * 1024 * 1024 * 1024))  # 32 GB
# Result: 32 GB heap (manually set, > physical)
# Warning: Will OOM when heap usage exceeds ~24 GB
```

## Testing

### Test 1: Application with NO brk calls
```bash
cd tests
gcc -o test_no_brk test_no_brk.c
# Without HeMem (baseline)
./test_no_brk

# With HeMem
LD_PRELOAD=../src/libhemem-runtime.so ./test_no_brk
# Expected in stats: "not initialized (no brk/sbrk calls detected)"
```

### Test 2: Application WITH brk calls
```bash
cd tests
./test_brk_simple
# Expected in stats: "Used: X MB" showing heap was initialized
```

### Test 3: Check auto-sizing
```bash
export DRAMSIZE=$((4*1024*1024*1024)) NVMSIZE=$((4*1024*1024*1024))
# Run any test with brk
# Check logs for: "reserving X bytes"
# Expected: ~4 GB (50% of 8 GB)
```

## Benefits

✅ **No wasted VA space** - Heap only created if needed  
✅ **Smart defaults** - Automatically sized based on physical memory  
✅ **Safer** - Less likely to exceed physical capacity  
✅ **More efficient** - Faster startup for apps not using brk  
✅ **Transparent** - Applications don't know/care about the change  

## Files Modified

- `src/interpose.c`
  - Smart default calculation in `hemem_heap_init()`
  - Removed eager initialization from `init()`
  - Updated logging and statistics
  - Added `#include <string.h>` for strerror()

- `docs/BRK_LAZY_INIT.md`
  - Complete documentation of lazy initialization and smart sizing

## Verification

Build successful:
```bash
cd src
make clean && make
# ✓ libhemem-runtime.so created
```

Ready to test with your applications!

## Recommendation

**For most users:**
```bash
# Don't set HEMEM_HEAP_SIZE
# Let auto-sizing work based on physical memory
export DRAMSIZE=...
export NVMSIZE=...
# Heap will be 50% of total, which is usually optimal
```

**Check the logs** on first run to see what heap size was calculated.
