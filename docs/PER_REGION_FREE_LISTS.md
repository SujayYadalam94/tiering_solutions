# Per-Region Free Lists Implementation

## Problem
When multiple regions used the same policy (e.g., PEBS or LRU), they shared **global** free lists (`dram_free_list` and `nvm_free_list` in `pebs.c` and `lru.c`). This caused regions to interfere with each other:

- Region 0 (default catchall) and Region 1 (hemem) both used PEBS policy
- Both regions tried to use the same global free lists
- Pages from one region could be allocated to the other region
- Led to OOM errors at only 628 pages (3.7% utilization) despite having 33GB total capacity

## Solution
Implemented **per-region free lists** so each region manages its own memory pool independently.

### Changes Made

#### 1. `src/hemem.h` - Added Per-Region Free Lists
```c
struct hemem_region {
  // ... existing fields ...
  
  // Per-region free lists for page allocation
  struct fifo_list dram_free_list;
  struct fifo_list nvm_free_list;
};
```

#### 2. `src/policy_runtime.c` - Initialize Per-Region Free Lists

In `hemem_regions_bootstrap()`, after assigning memory ranges to each region:
- Initialize each region's `dram_free_list` and `nvm_free_list`
- Populate lists with pages corresponding to the region's physical memory ranges
- Each page's `region` pointer is set to point back to its owning region

#### 3. `src/policy_runtime.c` - Region-Aware Allocation

Added `region_aware_pagefault()`:
```c
static struct hemem_page* region_aware_pagefault(struct hemem_region *region)
{
  // Try DRAM first
  page = dequeue_fifo(&region->dram_free_list);
  if (page != NULL) return page;
  
  // Fall back to NVM
  page = dequeue_fifo(&region->nvm_free_list);
  if (page != NULL) return page;
  
  // Out of memory for this region
  assert(0 && "Out of memory in region");
}
```

Modified `hemem_policy_pagefault()` to use `region_aware_pagefault()` for PEBS and LRU policies.

#### 4. `src/policy_runtime.c` - Region-Aware Deallocation

Modified wrappers to return freed pages back to their region's free lists:
- `pebs_remove_wrapper()`: Returns page to `page->region->dram_free_list` or `nvm_free_list`
- `lru_remove_wrapper()`: Same behavior

#### 5. Skip Policy Init for Multi-Region Mode

Modified `pebs_init_wrapper()` and LRU init to skip populating global free lists when using per-region mode. The global lists remain empty, and all allocation/deallocation goes through per-region lists.

## Current Limitations

### Migration Disabled
PEBS and LRU hot/cold migration threads are **currently disabled** in multi-region mode because:

1. **PEBS migration** (`pebs_policy_thread`, `pebs_migration_thread`) uses global structures:
   - `pages` hash table (maps VA → page)
   - `mod_page_dq` ring buffer
   - Calls `enqueue_fifo(&dram_free_list, ...)` to return demoted pages
   - Not region-aware

2. **LRU migration** (`lru_kswapd_thread`, `lru_scan_thread`) uses global lists:
   - `active_list`, `inactive_list`
   - `nvm_active_list`, `nvm_inactive_list`
   - Not region-aware

### Current Behavior
- **Allocation**: ✅ Works correctly with per-region free lists
- **Deallocation**: ✅ Returns pages to correct region
- **Hot/Cold Migration**: ❌ Disabled (pages stay where initially allocated)
- **PEBS Tracking**: ❌ Disabled (no access pattern sampling)
- **LRU Aging**: ❌ Disabled (no active/inactive list management)

## Future Work (TODO)

To enable full PEBS/LRU functionality in multi-region mode:

### 1. Make PEBS Tracking Region-Aware
- Add per-region `pages` hash tables
- Add per-region `mod_page_dq` ring buffers
- Update `pebs_add_page()` and `pebs_remove_page()` to use region's structures
- Update migration threads to iterate over all regions

### 2. Make LRU Lists Region-Aware
- Add per-region active/inactive lists
- Update `lru_kswapd_thread` to manage per-region lists
- Update page aging logic to be region-aware

### 3. Update Migration Logic
- `promote_to_free_dram_page()`: Use `page->region->dram_free_list`
- `demote_to_free_nvm_page()`: Use `page->region->nvm_free_list`
- Ensure migrations respect region boundaries

## Testing

All unit tests pass:
```bash
cd tests && make test
# Total:  16
# Passed: 16
# Failed: 0
```

## Usage

Multi-region mode is automatically enabled when:
1. Multiple regions are registered via `HEMEM_REGIONS` environment variable
2. Or when manual registration + bootstrap is called

Example:
```bash
HEMEM_REGIONS="0x7fff1f800000-0x7fff97800000:hemem" \
DRAMSIZE=2147483648 \
./your_workload
```

This creates:
- Region 0 (default catchall): Simple policy, 50% DRAM, 50% NVM
- Region 1 (hemem): PEBS policy, 50% DRAM, 50% NVM
- Each region has independent free lists
- No more OOM at low utilization!

## Performance Implications

### Pros:
- ✅ No more region interference
- ✅ Full memory capacity available to all regions
- ✅ No OOM at low utilization
- ✅ Predictable per-region memory limits

### Cons:
- ❌ No hot/cold migration (yet) - pages stay in initial tier
- ❌ No PEBS access sampling - can't identify hot pages
- ❌ No LRU aging - can't identify cold pages
- ⚠️ Memory may be underutilized if one region is idle while another is full

## Verification

To verify per-region allocation is working, check stderr for debug output:
```
HeMem: Region 0 [default] VA:[0x0-0xffffffffffffffff) -> DRAM:[0x40000000-0x80000000) NVM:[0x800000000-0x1000000000)
HeMem:   -> Initialized 512 DRAM pages, 16384 NVM pages
HeMem: Region 1 [hemem] VA:[0x7fff1f800000-0x7fff97800000) -> DRAM:[0x0-0x40000000) NVM:[0x0-0x800000000)
HeMem:   -> Initialized 512 DRAM pages, 16384 NVM pages
DEBUG_FAULT[0]: va=0x7fffdea00000 -> region=default [0x0-0xffffffffffffffff) policy=0
DEBUG_FAULT[1]: va=0x7fffe1a00000 -> region=default [0x0-0xffffffffffffffff) policy=0
```

Each region should show page allocations going to the correct region based on VA.
