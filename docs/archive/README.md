# HeMem Documentation Archive

This directory contains historical and superseded documentation. These files are preserved for reference but should not be used for current development.

## Archived Documents

### BRK Interception Evolution

**BRK_TRACKING_APPROACH.md** (Original, Oct 2025)
- **Status:** Superseded
- **Approach:** Tracking only (did not intercept)
- **Why archived:** Full interception now implemented
- **See instead:** `../BRK_INTERCEPTION_GUIDE.md`

**BRK_INTERMEDIATE_APPROACH.md** (Intermediate, Oct 2025)
- **Status:** Superseded
- **Approach:** Attempted MAP_FIXED interception with libc heap
- **Why archived:** Caused corruption, replaced with separate heap approach
- **See instead:** `../BRK_INTERCEPTION_GUIDE.md`

**BRK_IMPLEMENTATION_SUMMARY.md** (Oct 13, 2025)
- **Status:** Redundant (consolidated)
- **Content:** Implementation summary of full interception
- **Why archived:** Merged into comprehensive guide
- **See instead:** `../BRK_INTERCEPTION_GUIDE.md`

**BRK_INTERCEPTION_CAVEATS.md** (Oct 13, 2025)
- **Status:** Redundant (consolidated)
- **Content:** Important caveats and considerations
- **Why archived:** Merged into comprehensive guide (Caveats section)
- **See instead:** `../BRK_INTERCEPTION_GUIDE.md#important-caveats`

**BRK_INTERCEPTION_CHANGELOG.md** (Oct 13, 2025)
- **Status:** Redundant (consolidated)
- **Content:** Changelog and quick start
- **Why archived:** Merged into comprehensive guide (History section)
- **See instead:** `../BRK_INTERCEPTION_GUIDE.md#history-and-evolution`

**LAZY_INIT_SUMMARY.md** (Oct 13, 2025)
- **Status:** Redundant (consolidated)
- **Content:** Lazy initialization and smart sizing
- **Why archived:** Merged into comprehensive guide
- **See instead:** `../BRK_INTERCEPTION_GUIDE.md#implementation`

### Multi-Region System

**MULTI_REGION_SPECIFICATION.md** (Originally Assignment.md, Oct 10, 2025)
- **Status:** Superseded (original specification)
- **Content:** Original requirements and design for multi-region system
- **Why archived:** Implemented; see comprehensive guide instead
- **See instead:** `../MULTI_REGION_GUIDE.md`

### Historical Issues (Resolved)

**OOM_ISSUES_RESOLVED.md** (Originally OOM_ANALYSIS.md, Oct 8, 2025)
- **Status:** Resolved
- **Problem:** OOM kills with dual-policy configuration
- **Root cause:** Memory allocated via brk was not intercepted
- **Resolution:** Full BRK interception implementation
- **Historical value:** Documents the problem that motivated BRK interception
- **See:** `../BRK_INTERCEPTION_GUIDE.md` for the solution

**DUAL_POLICY_FIX.md** (Oct 8, 2025)
- **Status:** Resolved
- **Problem:** Memory accounting issues with dual policies
- **Root cause:** Same as OOM issues - untracked brk allocations
- **Resolution:** Full BRK interception implementation
- **Historical value:** Shows progression of understanding the problem

## Why Archive Instead of Delete?

These documents are preserved because they:

1. **Show evolution** - Document the development process and decision-making
2. **Explain failures** - Why certain approaches didn't work
3. **Provide context** - Historical context for current implementation
4. **Reference value** - May contain details useful for future work

## Current Documentation

For current, authoritative documentation, see:

- `../README.md` - Main project overview
- `../BRK_INTERCEPTION_GUIDE.md` - Complete BRK interception guide
- `../MULTI_REGION_GUIDE.md` - Multi-region policy system guide
- `../HEAP_MANAGEMENT_STRATEGIES.md` - Future malloc/free interception strategies
- `../PER_REGION_FREE_LISTS.md` - Technical details of free list management

## Document Lifecycle

```
Working Draft → Current Documentation → Superseded → Archived
                      ↓
            (Multiple versions may exist)
                      ↓
         Consolidated into comprehensive guide
                      ↓
         Redundant versions archived
```

## Questions?

If you need information from archived documents:
1. Check if the topic is covered in current documentation first
2. If not, archived documents may provide historical context
3. For current implementation details, always refer to source code and current docs

---

Last updated: October 13, 2025
