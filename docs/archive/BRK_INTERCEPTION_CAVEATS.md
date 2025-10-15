# IMPORTANT: BRK Interception Caveats and Considerations

## Critical Information for Using BRK Interception

### 1. Two Heaps in Memory

Your application now has **TWO separate heaps**:

```
Memory Layout:
┌─────────────────────────────────┐
│ libc's heap (around program end)│  ← Used by libc internally
│   - malloc during HeMem init    │     and HeMem's own allocations
│   - internal libc allocations   │
└─────────────────────────────────┘

         ... some VA space ...

┌─────────────────────────────────┐
│ HeMem-managed heap              │  ← Your application's brk/sbrk calls
│   - application sbrk() calls    │     go here after HeMem init
│   - managed by HeMem policies   │
│   - uses DAX devices            │
└─────────────────────────────────┘
```

**IMPORTANT:** Pointers from one heap CANNOT be freed to the other!

### 2. Bootstrap Phase

During HeMem initialization (`is_init == false`):
- brk/sbrk calls go to **libc**, not HeMem
- This is **intentional and necessary**
- Allows HeMem itself to allocate memory during startup
- Prevents infinite recursion

**What this means:**
- Small allocations during startup may go to libc heap
- After initialization completes, all brk/sbrk go to HeMem heap
- This is transparent to your application

### 3. Internal Call Flag

When HeMem makes internal allocations (`internal_call == true`):
- brk/sbrk/malloc calls go to **libc**, not HeMem
- Prevents recursion (e.g., HeMem calling malloc → hemem_sbrk → malloc → ...)

**Code example:**
```c
// HeMem internal code
internal_call = true;
void* ptr = malloc(1024);  // Goes to libc
internal_call = false;
```

### 4. Heap Size Limit

The HeMem-managed heap has a **fixed size** (configurable):
```bash
export HEMEM_HEAP_SIZE=$((16 * 1024 * 1024 * 1024))  # 16 GB default
```

**If exceeded:**
```
hemem_sbrk: out of heap space (requested X bytes, would exceed bounds)
```

**Solutions:**
1. Increase HEMEM_HEAP_SIZE
2. Modify application to use less heap
3. Use mmap for large allocations (already intercepted)

### 5. 2MB Alignment and Fragmentation

Internally, allocations are aligned to 2MB (huge page) boundaries:

```
Application: sbrk(1 MB)
HeMem allocates: 2 MB (aligned)
Internal fragmentation: 1 MB wasted
```

**Impact:**
- Small heap allocations waste space
- Large allocations (>2MB) minimal impact
- Typical fragmentation: <1% for most workloads

**Mitigation:**
- Applications using large heap allocations → no problem
- Applications using tiny heap (< 1 MB) → may waste space
- Consider increasing MIN_INTERPOSE_MEM_SIZE to avoid intercepting small allocations

### 6. Performance Considerations

#### First Allocation (Cold Start)
```
sbrk(64 MB) → hemem_mmap() → page fault handling → ~slower than libc
```
- Initial allocation involves HeMem's page fault mechanism
- Subsequent access to same pages → fast

#### Steady State
```
sbrk() → mutex lock → bounds check → update break → mutex unlock
```
- Mutex overhead on every brk/sbrk call
- Thread synchronization cost
- Typically negligible compared to allocation cost

#### Thread Contention
- All threads share single hemem_brk_lock
- High-frequency sbrk from many threads → potential contention
- **Future enhancement:** Per-thread heap caching

### 7. Compatibility with malloc

**If your application uses malloc/free:**
- malloc **may** call brk internally (glibc implementation detail)
- If malloc uses brk → HeMem intercepts it
- If malloc uses mmap → HeMem intercepts it (if > MIN_INTERPOSE_MEM_SIZE)
- If malloc uses mmap with small size → goes to libc

**Result:** Partial heap management

**For complete control:** Implement malloc/free interception (see HEAP_MANAGEMENT_STRATEGIES.md)

### 8. C++ Applications

**C++ new/delete:**
- Often implemented using malloc/free
- May or may not use brk (depends on allocator)
- For full control: Override operator new/delete

**Example:**
```cpp
void* operator new(size_t size) {
    void* ptr = hemem_mmap(...);  // Direct allocation
    if (ptr == MAP_FAILED) throw std::bad_alloc();
    return ptr;
}
```

### 9. Custom Allocators

**If your application uses:**
- jemalloc
- tcmalloc  
- Hoard (already linked in HeMem)
- Any custom allocator

**Then:**
- That allocator may not use brk/sbrk
- May use mmap directly (which HeMem intercepts)
- May maintain its own heap (outside HeMem control)

**Check:** Run with LD_PRELOAD and check statistics to see what's intercepted

### 10. Statistics Interpretation

```
brk_intercepted: [123 calls, 1234567890 bytes (1177.38 MB)]
```

**What this means NOW (with full interception):**
- Memory **managed by HeMem** through brk/sbrk
- Included in HeMem's tiering policies
- Backed by DAX devices

**Before (with tracking only):**
- Memory **bypassing HeMem** (went to libc)
- Not managed by tiering policies

**Important:** Same metric name, different meaning!

### 11. Debugging Tips

#### Check if interception is working
```bash
LD_PRELOAD=./src/libhemem-runtime.so ./app 2>&1 | grep "hemem.*brk"
```

Look for:
```
hemem interpose: intercepting sbrk(...)
hemem_sbrk: expanding heap by ...
```

#### Check heap usage
```bash
# At program exit, look for:
HeMem-managed heap: ...
  Used: X MB
```

If Used = 0, no brk calls were made (application may use only mmap).

#### Check if DAX devices accessible
```bash
ls -l /dev/dax*
# Should show read/write permissions
```

#### Common error patterns
```
# Bad: Indicates bootstrap recursion
hemem_sbrk: out of heap space
  → During HeMem init
  → Check internal_call flag logic

# Bad: Indicates memory corruption  
free(): invalid pointer
  → Mixing libc and HeMem heaps
  → Check internal_call flag

# OK: Normal during init
hemem interpose: calling libc mmap due to hemem init in progress
  → Expected during bootstrap
```

### 12. Multi-Region Integration

BRK-allocated memory can be assigned to regions:

```bash
# Heap region assigned to LRU policy
export HEMEM_REGIONS="0x7f0000000000-0x7f8000000000:lru"
export HEMEM_REGION_PHYS="lru:4G:16G"
```

**Important:**
- HeMem heap will be mapped in available VA space
- Check `hemem_heap_print_stats()` output for actual address range
- Assign that range to desired policy in HEMEM_REGIONS

### 13. Fork/Exec Considerations

**fork():**
- Child inherits HeMem state (is_init, heap pointers)
- Child inherits DAX device mappings
- Both parent and child share same physical pages (COW)

**exec():**
- New process image
- HeMem reinitializes from scratch
- Previous heap state lost (normal behavior)

**Recommended:**
- Set LD_PRELOAD in shell environment, not code
- Each process gets its own HeMem instance

### 14. Signal Safety

**Current implementation:** NOT async-signal-safe
- Uses mutex (pthread_mutex_lock)
- If signal handler calls sbrk → potential deadlock

**Solution:** Don't call malloc/sbrk/brk from signal handlers (general best practice anyway)

### 15. Memory Overcommit

**With MAP_NORESERVE:**
- VA space reserved, but no physical memory committed
- Physical pages allocated on first access
- Linux may overcommit memory

**Potential issue:**
- sbrk() succeeds but first access triggers OOM killer

**Solution:**
- Ensure sufficient DRAM + NVM for workload
- Configure HEMEM_HEAP_SIZE appropriately
- Monitor `Used` percentage in stats

## Recommendations

### For Most Applications
```bash
export HEMEM_HEAP_SIZE=$((16 * 1024 * 1024 * 1024))   # 16 GB
export MIN_INTERPOSE_MEM_SIZE=$((1 * 1024 * 1024))    # 1 MB
```

### For Heap-Heavy Applications
```bash
export HEMEM_HEAP_SIZE=$((64 * 1024 * 1024 * 1024))   # 64 GB
export MIN_INTERPOSE_MEM_SIZE=$((1 * 1024 * 1024))    # 1 MB
```

### For Small Allocations
```bash
# Don't intercept very small allocations
export MIN_INTERPOSE_MEM_SIZE=$((100 * 1024 * 1024))  # 100 MB
# Let libc handle heap, HeMem handles large mmap
```

## Testing Checklist

Before deploying with your application:

- [ ] Build succeeds: `make clean && make`
- [ ] Unit test passes: `./test_brk_full_interception`
- [ ] Runtime test passes: `./test_brk_full_runtime.sh`
- [ ] DAX devices accessible: `ls -l /dev/dax*`
- [ ] Application runs: `LD_PRELOAD=... ./app`
- [ ] Statistics printed at exit
- [ ] No segmentation faults
- [ ] No "free(): invalid pointer" errors
- [ ] `Used` heap size makes sense
- [ ] Performance acceptable

## Getting Help

If you encounter issues:

1. **Check logs** - Look for "hemem" messages
2. **Check statistics** - Verify brk_intercepted counter
3. **Check heap stats** - Verify Used/Available makes sense
4. **Disable temporarily** - Run without LD_PRELOAD to compare
5. **Review documentation**:
   - `docs/BRK_FULL_INTERCEPTION.md` - Implementation details
   - `docs/USAGE_BRK_INTERCEPTION.md` - Usage guide
   - This file - Caveats and considerations

## Summary

✅ **Do:**
- Configure HEMEM_HEAP_SIZE appropriately
- Check statistics to verify interception
- Test thoroughly before production use
- Monitor heap usage during runtime

❌ **Don't:**
- Mix pointers between libc and HeMem heaps
- Call malloc/sbrk from signal handlers
- Assume infinite heap size
- Use with 32-bit applications (VA space limited)

## Questions?

See documentation in `docs/` directory or review source code in `src/interpose.c`.
