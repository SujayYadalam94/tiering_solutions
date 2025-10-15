# HeMem Multi-Region Policy System

## Table of Contents
1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Configuration](#configuration)
4. [Implementation Details](#implementation-details)
5. [Examples](#examples)
6. [Testing](#testing)

---

## Overview

HeMem's multi-region policy system allows different virtual address (VA) ranges to be managed by different tiering policies. This enables fine-grained control over how different parts of an application's memory are tiered between DRAM and NVM.

### Key Concepts

**Region:** A contiguous VA range with an assigned policy
```
Region: [0x555555400000 - 0x7fff1f600000] → LRU policy
```

**Policy:** The algorithm that manages page placement and migration
- **LRU** - Least Recently Used (for frequent access patterns)
- **PEBS** - Hardware performance counter based (for adaptive tiering)
- **SIMPLE** - Basic round-robin placement

**Physical Resources:** DRAM and NVM allocated per-policy (not per-region)
- Multiple regions using same policy share physical resources
- Physical allocation specified separately from VA regions

---

## Architecture

### Memory Management Hierarchy

```
┌─────────────────────────────────────────────────┐
│                Application VA Space              │
└─────────────────────────────────────────────────┘
                      │
        ┌─────────────┴─────────────┐
        │ Region Lookup (VA → Policy) │
        └─────────────┬─────────────┘
                      │
        ┌─────────────┴─────────────────┐
        │                               │
   ┌────▼────┐  ┌────▼────┐  ┌────▼────┐
   │ LRU     │  │ PEBS    │  │ SIMPLE  │
   │ Policy  │  │ Policy  │  │ Policy  │
   └────┬────┘  └────┬────┘  └────┬────┘
        │            │            │
   ┌────▼────────────▼────────────▼────┐
   │  DRAM Free Lists (per-policy)      │
   │  NVM Free Lists (per-policy)       │
   └────┬───────────────────────────────┘
        │
   ┌────▼────────────┐
   │ DAX Devices     │
   │ (Physical Mem)  │
   └─────────────────┘
```

### Key Design Principles

1. **VA Regions → Policies** (Many-to-One)
   - Multiple VA regions can use the same policy
   - Region lookup determines which policy handles a page fault

2. **Policies → Physical Resources** (One-to-One)
   - Each policy has dedicated DRAM and NVM pools
   - Resources allocated at initialization
   - Policies manage their own free lists

3. **Fallback Region**
   - Implicit region covering entire VA space
   - Catches any VA not in explicitly defined regions
   - Uses a default policy (typically PEBS or SIMPLE)

---

## Configuration

### Environment Variables

#### HEMEM_REGIONS
Defines VA ranges and their assigned policies.

**Format:**
```
HEMEM_REGIONS="start1-end1:policy1,start2-end2:policy2,..."
```

**Policies:**
- `lru` - Least Recently Used
- `pebs` - Performance counter based
- `simple` - Simple round-robin

**Example:**
```bash
export HEMEM_REGIONS="0x555555400000-0x7fff1f600000:lru,0x7fff1f800000-0x7fff97800000:pebs"
```

**Notes:**
- Addresses in hexadecimal
- Ranges must not overlap
- Ranges are inclusive [start, end)
- 2MB alignment recommended (will be rounded)

#### HEMEM_REGION_PHYS
Allocates physical memory (DRAM/NVM) to each policy.

**Format:**
```
HEMEM_REGION_PHYS="policy1:dram_size:nvm_size,policy2:dram_size:nvm_size,..."
```

**Sizes:** Can use suffixes (K, M, G)

**Example:**
```bash
export HEMEM_REGION_PHYS="lru:4G:16G,pebs:8G:16G"
```

This gives:
- LRU policy: 4 GB DRAM, 16 GB NVM
- PEBS policy: 8 GB DRAM, 16 GB NVM

#### Physical Memory Assignment Rules

**Leftover memory** goes to the fallback region's policy:

```
Total: DRAM=20G, NVM=32G
Assigned: lru:4G:16G, pebs:8G:16G
Leftover: 20-4-8=8G DRAM, 32-16-16=0G NVM

Fallback region uses SIMPLE policy:
  SIMPLE gets: 8G DRAM, 0G NVM
```

**Important:** If fallback uses same policy as an explicit region, resources are **merged**:

```
Total: DRAM=20G, NVM=32G
Assigned: lru:4G:16G, pebs:8G:16G
Leftover: 8G DRAM, 0G NVM

Fallback region uses PEBS policy (same as explicit PEBS region):
  PEBS total: 8+8=16G DRAM, 16+0=16G NVM (merged!)
```

---

## Implementation Details

### Region Registration

Regions are registered during bootstrap:

```c
// From HEMEM_REGIONS env var:
hemem_region_register(0x555555400000, 0x7fff1f600000, 
                      HEMEM_POLICY_LRU, "region1");
hemem_region_register(0x7fff1f800000, 0x7fff97800000, 
                      HEMEM_POLICY_PEBs, "region2");

// Fallback region (implicit, covers entire VA space):
hemem_region_register(0, UINT64_MAX, 
                      HEMEM_POLICY_SIMPLE, "fallback");
```

### Region Lookup

On page fault at VA address:

```c
struct hemem_region *region = hemem_region_lookup(va);
// Returns the most specific region containing va
// (explicit regions take precedence over fallback)

struct hemem_page *page = hemem_policy_pagefault(region, va);
// Policy allocates and returns a page
```

### Set Notation (Resolving Overlaps)

When fallback region uses same policy as explicit regions, resolve using set operations:

```
@LRU = Union of all LRU VA ranges
@PEBS = Union of all PEBS VA ranges

Effective LRU Range = @LRU \ @PEBS
Effective PEBS Range = @PEBS \ @LRU

LRU and PEBS ranges are mutually exclusive.
```

If fallback is PEBS:
```
Effective PEBS Range = (@PEBS | Fallback) \ @LRU
```

### Physical Resource Allocation

At bootstrap:

1. **Parse HEMEM_REGION_PHYS**
   ```
   lru:4G:16G → LRU gets 4GB DRAM, 16GB NVM
   pebs:8G:16G → PEBS gets 8GB DRAM, 16GB NVM
   ```

2. **Calculate leftover**
   ```
   DRAM: 20 - 4 - 8 = 8G leftover
   NVM: 32 - 16 - 16 = 0G leftover
   ```

3. **Assign leftover to fallback policy**
   ```
   If fallback is SIMPLE:
     SIMPLE gets 8G DRAM, 0G NVM
   
   If fallback is PEBS (same as explicit PEBS):
     PEBS total = 8 + 8 = 16G DRAM
     PEBS total = 16 + 0 = 16G NVM
   ```

4. **Create free lists**
   ```c
   // Split DAX devices into 2MB pages
   // Assign pages to per-policy free lists
   
   lru_policy.dram_free_list = [pages 0-2047]    // 4GB worth
   lru_policy.nvm_free_list = [pages 2048-10239]  // 16GB worth
   
   pebs_policy.dram_free_list = [pages 10240-14335]  // 8GB worth
   pebs_policy.nvm_free_list = [pages 14336-22527]   // 16GB worth
   ```

5. **Initialize policies**
   ```c
   lru_init(&lru_policy.dram_free_list, &lru_policy.nvm_free_list);
   pebs_init(&pebs_policy.dram_free_list, &pebs_policy.nvm_free_list);
   ```

### Page Allocation Flow

```
1. Page fault at VA=0x7fff2000000
   ↓
2. hemem_region_lookup(0x7fff2000000)
   → Returns region2 (PEBS policy)
   ↓
3. hemem_policy_pagefault(region2, 0x7fff2000000)
   ↓
4. pebs_pagefault(0x7fff2000000)
   ↓
5. Policy allocates from its free lists:
   - First touch → DRAM page if available
   - Or NVM page if DRAM full
   ↓
6. Page mapped to VA, tracked by policy
   ↓
7. Policy thread manages migration based on access patterns
```

---

## Examples

### Example 1: Single Policy for Entire Application

**Simplest case:** Use one policy for everything

```bash
export DRAMSIZE=$((8 * 1024 * 1024 * 1024))
export NVMSIZE=$((16 * 1024 * 1024 * 1024))

# Don't set HEMEM_REGIONS
# Don't set HEMEM_REGION_PHYS

# All memory managed by default policy (PEBS)
# Automatically gets all DRAM and NVM
```

### Example 2: Two Regions, Different Policies

**Use case:** Separate hot and cold data regions

```bash
export DRAMSIZE=$((20 * 1024 * 1024 * 1024))
export NVMSIZE=$((64 * 1024 * 1024 * 1024))

# Hot data region (0x5555... - 0x7fff1f...) → LRU policy
# Warm data region (0x7fff1f... - 0x7fff97...) → PEBS policy
# Everything else → SIMPLE policy (fallback)

export HEMEM_REGIONS="0x555555400000-0x7fff1f600000:lru,0x7fff1f800000-0x7fff97800000:pebs"

# Physical allocation:
# LRU: 6GB DRAM, 20GB NVM
# PEBS: 10GB DRAM, 30GB NVM
# SIMPLE: 4GB DRAM, 14GB NVM (leftover)

export HEMEM_REGION_PHYS="lru:6G:20G,pebs:10G:30G"
```

### Example 3: Heap Region with Specific Policy

**Use case:** Assign heap to LRU policy

```bash
export DRAMSIZE=$((8 * 1024 * 1024 * 1024))
export NVMSIZE=$((16 * 1024 * 1024 * 1024))
export HEMEM_HEAP_SIZE=$((4 * 1024 * 1024 * 1024))

# Heap region assigned to LRU
# (Note: You need to know where heap will be mapped)
export HEMEM_REGIONS="0x7f0000000000-0x7f0100000000:lru"

# Give LRU enough resources for heap
export HEMEM_REGION_PHYS="lru:4G:8G"

# Fallback (rest of VA space) uses PEBS
# PEBS gets leftover: 4G DRAM, 8G NVM
```

### Example 4: Consolidating Same Policy

**Use case:** Multiple VA ranges, same policy

```bash
export DRAMSIZE=$((16 * 1024 * 1024 * 1024))
export NVMSIZE=$((32 * 1024 * 1024 * 1024))

# Two separate VA ranges both using LRU
export HEMEM_REGIONS="0x100000000-0x200000000:lru,0x300000000-0x400000000:lru"

# Physical resources shared between both LRU regions
export HEMEM_REGION_PHYS="lru:8G:16G"

# Fallback uses PEBS, gets leftover
# PEBS: 8G DRAM, 16G NVM
```

### Example 5: Fallback Merging

**Use case:** Fallback uses same policy as explicit region

```bash
export DRAMSIZE=$((20 * 1024 * 1024 * 1024))
export NVMSIZE=$((40 * 1024 * 1024 * 1024))

# Explicit LRU region
export HEMEM_REGIONS="0x555555400000-0x7fff1f600000:lru"

# LRU gets some resources
export HEMEM_REGION_PHYS="lru:10G:20G"

# Fallback region (implicit, covers all VA space)
# If fallback policy is LRU, resources merge:
# LRU total = 10G + (leftover 10G) = 20G DRAM
# LRU total = 20G + (leftover 20G) = 40G NVM
```

---

## Testing

### Unit Tests

**tests/test_regions.c** - Comprehensive region tests:

1. **Region Registration** (4 tests)
   - Single region
   - Multiple non-overlapping regions
   - Catchall region
   - Invalid range rejection

2. **Region Lookup** (5 tests)
   - Single region lookup
   - Multiple region lookup
   - Boundary conditions
   - No match scenarios
   - Catchall matching

3. **Bootstrap** (3 tests)
   - Single policy via HEMEM_POLICY
   - Multi-region via HEMEM_REGIONS
   - Default behavior

4. **Memory Allocation** (3 tests)
   - Single region allocation
   - Multiple region distribution
   - DRAM/NVM non-overlap

**Run:**
```bash
cd tests
make test_regions
./test_regions
```

### Integration Testing

**Test with real application:**

```bash
# Set up multi-region configuration
export DRAMPATH=/dev/dax0.0
export NVMPATH=/dev/dax1.0
export DRAMSIZE=$((8 * 1024 * 1024 * 1024))
export NVMSIZE=$((16 * 1024 * 1024 * 1024))

export HEMEM_REGIONS="0x555555400000-0x7fff1f600000:lru"
export HEMEM_REGION_PHYS="lru:4G:8G"

# Run application
LD_PRELOAD=./src/libhemem-runtime.so ./your_app

# Check statistics
# Should show memory allocated to different policies
```

### Verification Checklist

- [ ] Regions registered correctly (check logs)
- [ ] Physical resources allocated per policy
- [ ] Page faults route to correct policy
- [ ] No overlap in physical pages between policies
- [ ] Statistics show per-policy allocation
- [ ] Migrations work within each policy
- [ ] Fallback region catches unmatched VAs

---

## Troubleshooting

### "hemem_region_lookup: no region found"

**Cause:** VA address not covered by any region

**Check:**
- Fallback region should catch all VAs
- Verify fallback region registered
- Check region bounds include the VA

### Physical Memory Allocation Mismatch

**Symptoms:** Policy runs out of pages unexpectedly

**Causes:**
- HEMEM_REGION_PHYS doesn't match workload distribution
- One policy allocated too little, another too much

**Solution:**
- Adjust HEMEM_REGION_PHYS based on actual usage patterns
- Monitor per-policy statistics
- Redistribute physical resources

### Overlap Errors

**Cause:** VA ranges overlap in HEMEM_REGIONS

**Solution:**
- Ensure all VA ranges are disjoint
- Check hex addresses carefully
- Use 2MB alignment

### Fallback Policy Using All Memory

**Cause:** Most VAs fall outside explicit regions

**Solutions:**
1. Expand explicit region ranges
2. Assign more resources to fallback policy
3. Profile application to determine VA usage patterns

---

## Implementation Status

### ✅ Completed
- [x] Region registration and lookup
- [x] Per-policy physical resource allocation
- [x] Policy initialization with free lists
- [x] Page fault routing to correct policy
- [x] Statistics per policy
- [x] Fallback region support
- [x] Resource merging for same policy
- [x] Unit tests

### 🔄 In Progress
- [ ] Automatic VA range detection
- [ ] Dynamic resource rebalancing
- [ ] Policy migration (changing policy for region)

### 📋 Future Work
- [ ] GUI for configuration
- [ ] Profile-guided region assignment
- [ ] Automatic policy selection based on access patterns

---

## Files

### Implementation
- `src/policy_runtime.c` - Multi-region runtime system
- `src/hemem.h` - Region and policy data structures
- `src/policies/*.c` - Individual policy implementations

### Configuration
- Environment variables: `HEMEM_REGIONS`, `HEMEM_REGION_PHYS`
- Parsed at startup in `hemem_regions_bootstrap()`

### Tests
- `tests/test_regions.c` - Comprehensive unit tests
- `tests/hemem_stubs.c` - Stub implementations for testing

### Documentation
- `docs/MULTI_REGION_GUIDE.md` - This document
- `docs/PER_REGION_FREE_LISTS.md` - Implementation details of free list management

---

## Summary

### Key Takeaways

✅ **Flexible memory management** - Different policies for different VA ranges  
✅ **Resource isolation** - Each policy has dedicated physical memory  
✅ **Fallback safety** - Catchall region handles all unspecified VAs  
✅ **Resource sharing** - Multiple regions can share same policy  
✅ **Configurable** - Environment variables for easy setup  

### Best Practices

1. **Start simple** - Use single policy initially
2. **Profile first** - Understand VA usage patterns before configuring regions
3. **Align boundaries** - Use 2MB aligned addresses
4. **Monitor statistics** - Check per-policy memory usage
5. **Test thoroughly** - Run unit tests after configuration changes

### Recommended Configuration

**For most applications:**
```bash
# Single policy, automatic allocation
# Don't set HEMEM_REGIONS or HEMEM_REGION_PHYS
# Let HeMem use default policy for everything
```

**For applications with known hot/cold separation:**
```bash
# Profile to find VA ranges
# Assign appropriate policies and resources
export HEMEM_REGIONS="hot_range:lru,warm_range:pebs"
export HEMEM_REGION_PHYS="lru:XG:YG,pebs:AG:BG"
```

---

## References

- Original specification: `Assignment.md` (archived)
- Per-region free lists: `docs/PER_REGION_FREE_LISTS.md`
- Policy implementations: `src/policies/*.c`
- Tests: `tests/test_regions.c`
