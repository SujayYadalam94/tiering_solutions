# Fix for OOM with Dual-Policy Configuration

## Problem
When running dual-policy configuration with `HEMEM_REGIONS`, the application was running out of memory and getting OOM-killed.

## Root Cause
The original code **always added a default catchall region** when parsing `HEMEM_REGIONS`, but this catchall would consume **all remaining physical memory**, leaving insufficient memory for the explicitly specified regions.

## Solution
Modified `/users/hjcoffey/arms/src/policy_runtime.c` to:

1. **Always create default catchall region** as a safety net for memory accesses outside explicit regions
2. **Reserve 10% of DRAM and NVM** for the default catchall region
3. **Allocate remaining 90%** equally among explicitly specified regions

## Memory Allocation Strategy

When you specify:
```bash
HEMEM_REGIONS="0x555555400000-0x7fff1f800000:lru,0x7fff1f800000-0x7fff97800000:hemem"
DRAMSIZE=2147483648  # 2GB
```

The allocation is:
- **Default catchall**: 10% = 204MB DRAM, 819MB NVM (for addresses outside explicit ranges)
- **LRU region**: 45% = 922MB DRAM, 3.6GB NVM
- **HEMEM region**: 45% = 922MB DRAM, 3.6GB NVM

## Example Output
```
HeMem: Region 0 [default] VA:[0x0-0xffffffffffffffff) -> DRAM:[0x73400000-0x80000000) NVM:[0x1cce00000-0x200000000)
HeMem: Region 1 [lru] VA:[0x555555400000-0x7fff1f800000) -> DRAM:[0x0-0x39a00000) NVM:[0x0-0xe6600000)
HeMem: Region 2 [hemem] VA:[0x7fff1f800000-0x7fff97800000) -> DRAM:[0x39a00000-0x73400000) NVM:[0xe6700000-0x1ccd00000)
```

## Benefits
1. **No more OOM**: Explicit regions get predictable memory allocation
2. **Safety net**: Default catchall handles addresses outside explicit ranges
3. **Flexible**: Can still handle gaps in region specifications

## Testing
- All 15 unit tests pass ✅
- Dual-policy test validates 10%/45%/45% split ✅

## Usage
Your harness script should now work without OOM:
```bash
HEMEM_REGIONS="0x555555400000-0x7fff1f800000:lru,0x7fff1f800000-0x7fff97800000:hemem" \
MIN_INTERPOSE_MEM_SIZE=$MIN_INTERPOSE_MEM_SIZE \
HEMEMPOL=$pol \
DRAMSIZE=$size \
./run.sh -b merci -w merci -o results/results_dual_policy_${i}
```
