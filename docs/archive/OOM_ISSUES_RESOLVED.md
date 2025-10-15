# Dual-Policy OOM Analysis

## Problem
Application gets OOM-killed when running dual-policy configuration, even after fixing the 10% catchall reservation.

## Root Cause Analysis

### Memory Accounting Issue
From the stderr log:
```
mem_allocated: [538968064]        # 514MB (HeMem-managed pages)
dram_small_allocation_bytes: [687218688]  # 655MB (Hoard small allocations)
pages_allocated: [628]            # 628 × 2MB = 1256MB 
```

### The Problem
1. **System Physical DRAM**: 2GB total
2. **HeMem DRAMSIZE**: 2GB (configured)
3. **Small allocations** (<64MB threshold): 655MB going to system malloc/Hoard
4. **HeMem pages**: Using HeMem's DRAM pool

**Issue**: HeMem's DRAM pool and system DRAM are the **same physical 2GB**. When Hoard allocates 655MB for small objects, it consumes physical pages from the same 2GB that HeMem thinks it owns exclusively.

### Memory Layout Confusion
```
Total Physical DRAM: 2GB
├── HeMem thinks it owns: 2GB
│   ├── Region 1 (lru): 922MB
│   ├── Region 2 (hemem): 922MB  
│   └── Region 0 (default): 204MB
└── System/Hoard also using: ~655MB (from the same 2GB!)
    └── Small allocations (<64MB each)
```

**Result**: Attempting to use ~2.6GB when only 2GB physical DRAM exists → OOM

## Solutions

### Option 1: Reduce DRAMSIZE (Recommended)
Account for system overhead and small allocations:

```bash
# Reserve ~700MB for system/Hoard, leaving 1.3GB for HeMem
DRAMSIZE=$((2147483648 - 734003200))  # 2GB - 700MB = 1.3GB
```

### Option 2: Increase Physical DRAM
Add more physical DRAM to your system if possible (requires hardware change).

### Option 3: Lower MIN_INTERPOSE_MEM_SIZE
Capture more allocations in HeMem (may impact performance):

```bash
MIN_INTERPOSE_MEM_SIZE=$((4 * 1024 * 1024))  # 4MB threshold instead of 64MB
```

This will route more allocations through HeMem instead of system malloc, but increases HeMem overhead.

### Option 4: Use Memory Cgroups
Limit the application's total memory usage explicitly:

```bash
# Create cgroup with 14GB limit (2GB DRAM + memory pressure to NVM)
cgcreate -g memory:/hemem_app
cgset -r memory.limit_in_bytes=15032385536 hemem_app  # 14GB
cgexec -g memory:hemem_app ./your_app
```

## Recommended Configuration

```bash
# In your harness script:
MIN_INTERPOSE_MEM_SIZE=67108864  # Keep at 64MB
DRAMSIZE=1414094848              # 1.3GB (2GB - 700MB overhead)
NVMSIZE=68719476736              # Keep 64GB NVM

HEMEM_REGIONS="0x555555400000-0x7fff1f800000:lru,0x7fff1f800000-0x7fff97800000:hemem" \
MIN_INTERPOSE_MEM_SIZE=$MIN_INTERPOSE_MEM_SIZE \
HEMEMPOL=~/arms/src/libhemem-runtime.so \
DRAMSIZE=$DRAMSIZE \
NVMSIZE=$NVMSIZE \
./run.sh -b merci -w merci -o results/results_dual_policy_${i}
```

## Expected Behavior After Fix

With DRAMSIZE=1.3GB:
- Region 1 (lru): 585MB (45%)
- Region 2 (hemem): 585MB (45%)  
- Region 0 (default): 130MB (10%)
- **Total HeMem**: 1.3GB
- **System/Hoard**: ~700MB
- **Grand Total**: ~2GB ✓

## Verification

After applying the fix, check stderr for:
```
HeMem: Region 0 [default] VA:[...] -> DRAM:[...] NVM:[...]
HeMem: Region 1 [lru] VA:[...] -> DRAM:[...] NVM:[...]
HeMem: Region 2 [hemem] VA:[...] -> DRAM:[...] NVM:[...]
```

The DRAM allocations should total ~1.3GB, leaving headroom for system allocations.
