# HeMem Documentation Index

Complete guide to HeMem's tiered memory management system documentation.

## Quick Start

**New to HeMem?** Start here:
1. Read project overview: [`README`](../README)
2. Understand BRK interception: [`BRK_INTERCEPTION_GUIDE.md`](BRK_INTERCEPTION_GUIDE.md)
3. Learn multi-region configuration: [`MULTI_REGION_GUIDE.md`](MULTI_REGION_GUIDE.md)

## Core Documentation

### System Overview

**[../README](../README)**
- Project overview
- Build instructions
- Basic usage
- System requirements

### Memory Interception

**[BRK_INTERCEPTION_GUIDE.md](BRK_INTERCEPTION_GUIDE.md)** ⭐ **Complete Guide**
- **What:** Complete guide to heap (brk/sbrk) interception
- **Topics:**
  - How BRK interception works
  - Configuration (HEMEM_HEAP_SIZE, etc.)
  - Lazy initialization and smart sizing
  - Important caveats and limitations
  - Troubleshooting
  - Historical evolution
- **When to read:** Essential for understanding heap management
- **Length:** Comprehensive (~500 lines)

### Multi-Region Policy System

**[MULTI_REGION_GUIDE.md](MULTI_REGION_GUIDE.md)** ⭐ **Complete Guide**
- **What:** Complete guide to assigning different policies to VA ranges
- **Topics:**
  - Architecture and design
  - Configuration (HEMEM_REGIONS, HEMEM_REGION_PHYS)
  - Physical memory allocation per policy
  - Examples and use cases
  - Testing
- **When to read:** When configuring multiple policies
- **Length:** Comprehensive (~450 lines)

### Implementation Details

**[PER_REGION_FREE_LISTS.md](PER_REGION_FREE_LISTS.md)**
- **What:** Technical details of per-region free list management
- **Topics:**
  - Free list data structures
  - Page allocation algorithms
  - Resource isolation between policies
- **When to read:** When modifying policy implementation
- **Length:** Technical (~200 lines)

### Future Work

**[HEAP_MANAGEMENT_STRATEGIES.md](HEAP_MANAGEMENT_STRATEGIES.md)**
- **What:** Strategies for complete heap management via malloc/free interception
- **Topics:**
  - Current limitations (BRK only captures some allocations)
  - Strategy 1: Override malloc/free
  - Strategy 2: Override C++ operator new/delete
  - Strategy 3: Integrate with Hoard
  - Strategy 4: Hybrid approaches
- **When to read:** When planning malloc/free interception
- **Length:** Design document (~220 lines)

## Test Documentation

**[../tests/README.md](../tests/README.md)**
- **What:** Test suite documentation
- **Topics:**
  - Region registration tests
  - Region lookup tests
  - Bootstrap tests
  - Memory allocation tests
- **When to read:** When running or writing tests
- **Length:** Quick reference (~50 lines)

## Archived Documentation

**[archive/README.md](archive/README.md)**
- **What:** Index of historical and superseded documentation
- **Contents:**
  - Old BRK tracking-only approach
  - Intermediate failed attempts
  - Resolved OOM issues
  - Original specifications
- **When to read:** For historical context only
- **Note:** Do not use for current development

## Documentation by Topic

### Getting Started
1. [`../README`](../README) - Project overview
2. [`BRK_INTERCEPTION_GUIDE.md`](BRK_INTERCEPTION_GUIDE.md) - Essential reading
3. [`MULTI_REGION_GUIDE.md`](MULTI_REGION_GUIDE.md) - Advanced configuration

### Configuration
- **BRK Heap Size:** [`BRK_INTERCEPTION_GUIDE.md#configuration`](BRK_INTERCEPTION_GUIDE.md#configuration)
- **Multi-Region Setup:** [`MULTI_REGION_GUIDE.md#configuration`](MULTI_REGION_GUIDE.md#configuration)
- **Environment Variables:** Both guides above

### Troubleshooting
- **BRK Issues:** [`BRK_INTERCEPTION_GUIDE.md#troubleshooting`](BRK_INTERCEPTION_GUIDE.md#troubleshooting)
- **Region Issues:** [`MULTI_REGION_GUIDE.md#troubleshooting`](MULTI_REGION_GUIDE.md#troubleshooting)
- **OOM Problems:** See archived [`archive/OOM_ISSUES_RESOLVED.md`](archive/OOM_ISSUES_RESOLVED.md) for historical context

### Implementation Details
- **BRK Implementation:** [`BRK_INTERCEPTION_GUIDE.md#implementation`](BRK_INTERCEPTION_GUIDE.md#implementation)
- **Region Architecture:** [`MULTI_REGION_GUIDE.md#architecture`](MULTI_REGION_GUIDE.md#architecture)
- **Free Lists:** [`PER_REGION_FREE_LISTS.md`](PER_REGION_FREE_LISTS.md)

### Testing
- **BRK Tests:** [`BRK_INTERCEPTION_GUIDE.md#testing`](BRK_INTERCEPTION_GUIDE.md#testing)
- **Region Tests:** [`MULTI_REGION_GUIDE.md#testing`](MULTI_REGION_GUIDE.md#testing)
- **Test Suite:** [`../tests/README.md`](../tests/README.md)

### Future Work
- **malloc/free Interception:** [`HEAP_MANAGEMENT_STRATEGIES.md`](HEAP_MANAGEMENT_STRATEGIES.md)
- **Planned Features:** See "Future Work" sections in guides

## Environment Variables Reference

Quick reference for all HeMem environment variables:

### Required
```bash
DRAMPATH=/dev/dax0.0          # Path to DRAM DAX device
NVMPATH=/dev/dax1.0           # Path to NVM DAX device
DRAMSIZE=$((8*1024*1024*1024))  # Total DRAM size (bytes)
NVMSIZE=$((16*1024*1024*1024))  # Total NVM size (bytes)
```

### BRK Interception (Optional)
```bash
HEMEM_HEAP_SIZE=$((4*1024*1024*1024))  # Heap size (default: 50% of DRAM+NVM)
MIN_INTERPOSE_MEM_SIZE=$((1*1024*1024)) # Min mmap size to intercept (default: 1GB)
```

### Multi-Region (Optional)
```bash
HEMEM_REGIONS="start1-end1:policy1,..."     # VA ranges and policies
HEMEM_REGION_PHYS="policy1:dram:nvm,..."   # Physical allocation per policy
```

**Details:** See respective guides above

## Usage Patterns

### Single Policy (Simplest)
```bash
export DRAMPATH=/dev/dax0.0
export NVMPATH=/dev/dax1.0
export DRAMSIZE=$((8*1024*1024*1024))
export NVMSIZE=$((16*1024*1024*1024))

LD_PRELOAD=./src/libhemem-runtime.so ./your_app
```

**See:** [`BRK_INTERCEPTION_GUIDE.md#usage`](BRK_INTERCEPTION_GUIDE.md#usage)

### Multi-Region Configuration
```bash
export DRAMPATH=/dev/dax0.0
export NVMPATH=/dev/dax1.0
export DRAMSIZE=$((20*1024*1024*1024))
export NVMSIZE=$((64*1024*1024*1024))
export HEMEM_REGIONS="0x555555400000-0x7fff1f600000:lru"
export HEMEM_REGION_PHYS="lru:6G:20G"

LD_PRELOAD=./src/libhemem-runtime.so ./your_app
```

**See:** [`MULTI_REGION_GUIDE.md#examples`](MULTI_REGION_GUIDE.md#examples)

## Source Code Reference

### Core Implementation
- `src/hemem.c` - Main HeMem runtime
- `src/interpose.c` - BRK/mmap interception
- `src/policy_runtime.c` - Multi-region policy runtime
- `src/policies/*.c` - Individual policy implementations

### Tests
- `tests/test_regions.c` - Region system tests
- `tests/test_brk_*.c` - BRK interception tests
- `tests/hemem_stubs.c` - Test stubs

## Document Status Legend

- ⭐ **Complete Guide** - Comprehensive, authoritative documentation
- 📖 **Technical Details** - Implementation-specific documentation
- 🔮 **Future Work** - Design documents for planned features
- 📚 **Archived** - Historical, superseded, or redundant

## Contributing to Documentation

### When to Update

**Update existing docs when:**
- Implementation changes behavior
- Configuration options added/changed
- Bugs fixed that affect documented behavior

**Create new docs when:**
- Adding major new feature
- Complex implementation needs explanation

**Archive docs when:**
- Feature completely replaced
- Document superseded by more comprehensive guide
- Historical value only

### Documentation Standards

1. **Clear structure** - Use table of contents for long docs
2. **Code examples** - Show concrete usage patterns
3. **Troubleshooting** - Include common issues and solutions
4. **Cross-references** - Link to related documentation
5. **Date information** - Note when doc last updated

## Quick Links

- [Main README](../README)
- [BRK Interception Guide](BRK_INTERCEPTION_GUIDE.md)
- [Multi-Region Guide](MULTI_REGION_GUIDE.md)
- [Test Documentation](../tests/README.md)
- [Archived Docs](archive/README.md)

## Questions?

1. **For current features:** Check the relevant guide above
2. **For troubleshooting:** See troubleshooting sections in guides
3. **For historical context:** Check archived documentation
4. **For source code details:** Read the source with guide as reference

---

**Last updated:** October 13, 2025  
**Documentation structure:** docs/ contains current documentation, docs/archive/ contains historical  
**Total active guides:** 4 (BRK, Multi-Region, Free Lists, Heap Strategies)
