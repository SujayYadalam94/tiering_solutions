# Hugepage settings: Linux 6.18 mechanisms and exclusions

Read-only analysis, 2026-09-09. The full local kernel tree is modified 6.2; this report checks upstream **v6.18**, with installed 6.18.1 headers available locally. It explains possible mechanisms, not a demonstrated May-to-June cause. Historical requested values are audited separately in `settings/`; today's observed values are saved in [kernel_controls_readback.json](kernel_controls_readback.json).

## What identical setup values do not guarantee

Both versions request aggressive anonymous THP allocation and collapse: top-level `enabled=always`, `defrag=always`, khugepaged defrag enabled, 8,192 pages per scan, zero scan sleep, and 1 ms allocation-failure sleep. These controls can amplify a **repeatable policy-specific allocation/migration stream** even when ARMS and the model alternate within one boot. They do not imply identical THP coverage or destination-node fragmentation.

Khugepaged selects a collapse destination using the node distribution of the pages it scans. Its collapse eligibility includes absent/zero, shared, and swapped PTE limits. Thus different existing placements and mapping contents can change collapse destination and allocation cost under identical knobs. A split followed by a later collapse is possible; the counters do **not** prove that the same addresses repeatedly cycled. See `hpage_collapse_find_target_node`, `hpage_collapse_scan_pmd`, and `alloc_charge_folio` in [v6.18 khugepaged.c](https://github.com/torvalds/linux/blob/v6.18/mm/khugepaged.c).

Migration destination allocation uses `GFP_TRANSHUGE` and the requested node, allowing direct compaction independently of top-level THP `defrag`. Splitting may follow destination allocation failure or a partially mapped/deferred-split path. Successful split fallback contributes to both migration FAIL and SPLIT, then retries smaller folios; the final aggregate syscall return can still be zero. [Migration source](https://github.com/torvalds/linux/blob/v6.18/mm/migrate.c#L1718). Details and limitations are recorded in [kernelmechanism.md](../migration_probe/kernelmechanism.md).

The current diagnostic supports investigating this mechanism: ARMS/model had 3,341/230 global migration splits, 1,960/534 collapse allocations, and 30,306/13,371 failed collapse allocations. These are global interval deltas, not PID/address attribution. The approximately 46.16/21.36 seconds of summed migration-call duration overlap across worker threads and are **not application stall time**. No equivalent historical counters survive.

## Untouched settings and their actual scope

| Setting not explicitly normalized by the ordinary setup | Current readback | Interpretation |
|---|---:|---|
| Per-size anonymous THP | 2 MiB `inherit`; smaller sizes `never` | Top-level `always` does not overwrite per-size choices. |
| `max_ptes_none` / `max_ptes_swap` / `max_ptes_shared` | 511 / 64 / 256 | Influence collapse eligibility; `max_ptes_none` also controls underused-page detection. |
| `shrink_underused` | 1 | Does not control all splitting; see exclusion below. |
| `vm.defrag_mode` | 0 | Separate allocator fragmentation-avoidance policy. |
| `extfrag_threshold` | 500 | Influences whether compaction is appropriate for allocation failure. |
| `compact_unevictable_allowed` | 1 | Allows compaction to consider unevictable pages. |
| `percpu_pagelist_high_fraction` | 0 | Kernel-managed per-CPU page-list sizing. |
| `nr_hugepages` / `nr_overcommit_hugepages` | 0 / 0 | Explicit hugetlb reservations; distinct from THP. |
| `use_zero_page` | 1 | Huge zero-page eligibility. |
| `vfs_cache_pressure_denom` | 100 | Makes requested pressure 2,000 twenty times the normal ratio. |

Per-size inheritance and the distinct anonymous/shmem controls are documented in the [6.18 THP guide](https://docs.kernel.org/6.18/admin-guide/mm/transhuge.html). `vm.defrag_mode` changes allocation fallback behavior and is most effective when set early, before fragmentation develops; `extfrag_threshold` distinguishes lack of free memory from fragmentation. These are reasonable missing readbacks, but **none has a demonstrated historical change**. [6.18 VM sysctl reference](https://github.com/torvalds/linux/blob/v6.18/Documentation/admin-guide/sysctl/vm.rst).

### A conditional setup failure involving shmem

Writing global `shmem_enabled=force` returns `EINVAL` unless the per-size shmem inherit mask is exactly the PMD-size bit. Setup does not first normalize that mask. An inherited per-size change can therefore make the write fail; the current strict writer's early return and historical unchecked writes would then differ. Today's 2 MiB-only inherit mask and successful `force` readback show **no such failure now**. See `shmem_parse_huge` and `shmem_allowable_huge_orders` in [v6.18 shmem.c](https://github.com/torvalds/linux/blob/v6.18/mm/shmem.c#L634).

This is not a direct switch for ordinary HDD/SSD graph page cache. GAPBS [`ReadSerializedGraph`](/users/zimooo2/gapbs/src/reader.h:274) reads the graph through `std::ifstream` into `new[]` arrays. The shmem policy concerns tmpfs/internal shared-memory mappings; a direct BC graph effect would require additional evidence of such mappings. Its plausible relevance here is conditional setup failure or indirect memory pressure.

## Verified exclusions and important distinctions

* **Current `max_ptes_none=511` disables zero-content underused detection.** `thp_underused()` immediately returns false when this equals `HPAGE_PMD_NR - 1`. This does not disable splitting of partially mapped folios or migration fallback. `shrink_underused=0` suppresses queueing of non-partially-mapped folios; partially mapped folios remain eligible. Lowering `max_ptes_none` simultaneously changes collapse eligibility and enables/tightens underused detection, so it is not a pure collapse-only intervention. [v6.18 huge_memory.c](https://github.com/torvalds/linux/blob/v6.18/mm/huge_memory.c#L3790).
* **Successful setup clears stale watermark boosts.** The `min_free_kbytes` and `watermark_scale_factor` handlers recalculate zone watermarks; that operation zeros `zone->watermark_boost`. New allocation activity can rebuild a boost during a run. `min_free_kbytes=1 GiB` is distributed across zones, not reserved entirely on the small DRAM node. The saved local readback previously measured about 46.7 MiB of node-0 minimum watermarks. [v6.18 page_alloc.c](https://github.com/torvalds/linux/blob/v6.18/mm/page_alloc.c#L5964).
* **The 16 MiB user/admin reserves are not physical DRAM reservations under this configuration.** `__vm_enough_memory()` returns immediately for `overcommit_memory=1`, before checking these commit-accounting reserves. They should not be treated like `min_free_kbytes` or physical zone reserves. [v6.18 util.c](https://github.com/torvalds/linux/blob/v6.18/mm/util.c#L872).
* **The large `compact_fail` count does not identify background compaction.** It records unsuccessful allocation after a direct-compaction attempt and can include application faults, migration workers, khugepaged allocation, or other callers. It is neither elapsed time nor one event per THP. Proactiveness 0 would still permit allocation-triggered direct compaction and demand-driven kcompactd. [Previously verified allocation and compaction paths](../../setup_kernel_paths.md).

## Existing tests constrain the explanation

Restoring historical settings gave ARMS/model 103.63/98.16 seconds, with recorded controls already matching before preparation. Proactiveness 80→20 gave 103.23/98.52 seconds and did not restore the old gap. Khugepaged scan sleep 0→10 ms gave 99.98/97.79 seconds; ARMS iteration time fell from about 6.34 to 5.98 seconds, then increased again after restoring zero sleep. This demonstrates some sensitivity to collapse activity, not a dated cause or proof that background compaction explains all excess kernel work. See [setup-knob results](/users/zimooo2/tiering_solutions/docs/bc_twitter_setup_knobs.md) and [historical setup audit](/users/zimooo2/tiering_solutions/docs/bc_twitter_historical_setup_audit.md).

The useful remaining distinction is between **migration-triggered allocation/splitting**, **collapse allocation**, and **partially mapped splitting**. Identical controls can produce unequal costs in those paths. Current counters establish a mechanism worth tracing, while the source/history evidence still does not identify which original boot condition changed.
