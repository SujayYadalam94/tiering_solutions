# HeMem Multi-Region Test Suite

## Overview
Unit tests for HeMem multi-region memory management functionality.

## Running Tests
```bash
make test
```

## What's Tested (15 tests total)

### Region Registration (4 tests)
- Single region registration
- Multiple non-overlapping regions
- Catchall region (entire address space)
- Invalid range rejection

### Region Lookup (5 tests)
- Single region lookup
- Multiple region lookup
- Boundary conditions
- No match scenarios
- Catchall matching

### Bootstrap (3 tests)
- Single policy via HEMEM_POLICY env var
- Multi-region via HEMEM_REGIONS env var
- Default behavior with no configuration

### Memory Allocation (3 tests)
- Single region memory allocation
- Multiple region memory distribution
- DRAM/NVM non-overlap verification

## Important Notes

**2MB Page Alignment**: HeMem uses 2MB huge pages. All region boundaries are automatically aligned to 2MB boundaries.

Example:
```c
#define MB2 (2UL * 1024UL * 1024UL)  // 2MB

hemem_region_register(0, MB2, HEMEM_POLICY_PEBs, "region1");
hemem_region_register(MB2, 2*MB2, HEMEM_POLICY_LRU, "region2");
```

## Files
- **test_regions.c** - Test suite (15 tests)
- **hemem_stubs.c** - Stub implementations for kernel-dependent functions
- **Makefile** - Build system
