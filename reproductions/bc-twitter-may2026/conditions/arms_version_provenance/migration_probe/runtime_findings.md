# Migration diagnostic results

The two reconstructed May 7 cases completed on the same September 9 6.18 boot, using the same SSD graph and full historical preparation before each execution. Both had ten trials, successful exits, no detected benchmark overlap, and zero host-control restoration errors. Libraries are isolated; no normal source or library was replaced.

| Observation | Reconstructed ARMS | Reconstructed May model |
|---|---:|---:|
| Total | 106.28 s | 98.71 s |
| Read Time | 39.61346 s | 39.71335 s |
| Average trial | 6.62367 s | 5.87459 s |
| Completed migration calls | 1,443 | 1,373 |
| Completed syscall wall time, summed across workers | 46.158 s | 21.356 s |
| Process system CPU time | 140.22 s | 66.62 s |
| Mixed filtered/unfiltered request batches | 8 | 19 |
| Successful statuses mapped to a different original page record | 46 | 117 |
| Positive-return/stale-ENOMEM combinations | 0 | 0 |
| Global `thp_migration_success` delta | 4,539 | 6,834 |
| Global `thp_migration_fail` delta | 3,341 | 230 |
| Global `thp_migration_split` delta | 3,341 | 230 |
| Global `compact_fail` delta | 67,343 | 27,255 |

The [raw probe summaries](results/20260909T162119Z-352353/summary.json) retain all four promotion/demotion and base/huge-address lanes, syscall outcomes, validation, source/log paths, and restoration. [Global VM deltas](results/20260909T162119Z-352353/global_vm_deltas.json) are computed from each run's before/after snapshots.

## The indexing bug is exercised, but not exclusive to ARMS

The instrumentation observes a successful target-node status for `vas[i]` while the original caller subsequently processes a different `pages[i]`. This is stronger than finding a mixed batch in source. It is not a count of unique corrupted pages, an independently queried residency mismatch, or proof that every assignment changed the previous metadata value. Counters run just before the original status-processing loop; normal exit can truncate a tail.

The model encountered more of these statuses than ARMS and was still faster. Therefore simple exposure frequency does not explain the relative slowdown. Neither case exercised the separate positive-return/stale-ENOMEM early-return bug. All 2,816 completed calls returned zero, although individual statuses include missing/unmapped addresses.

## Successful syscalls can hide expensive huge-page fallback

The kernel counters show much more huge-page splitting and compaction work during the ARMS run. A zero `move_pages` return does not rule this out. Linux v6.18 retries split folios and explicitly returns success when the split pages ultimately migrate, retaining the THP failure/split counters. Splitting can follow an allocation failure or other paths, including deferred splitting of partially mapped folios; these counters do not identify each initiating cause. See the [exact kernel mechanism audit](kernelmechanism.md) and [v6.18 migration implementation](https://github.com/torvalds/linux/blob/v6.18/mm/migrate.c#L1990).

This establishes a present difference in migration/compaction behavior between the policies. It does not establish that the original May runs had the same counters or that every recorded kernel event belongs to BC. The original May timing files do not retain corresponding observations.

## Measurement limits

The syscall times sum overlapping worker calls; they are not application stall time and must not be subtracted from elapsed time. The process CPU times are meaningful accounting from the diagnostic runner, unlike the tiny launcher CPU values in the historical `.time` files. VM counters remain system-wide even with overlap detection. No direct-reclaim scans were recorded in either diagnostic.

Aggregate instrumentation adds work inside the existing adaptive migration-cost timer. ARMS's 106.28 seconds is somewhat above the previous roughly 103-second uninstrumented reconstructions. Consequently this pair is primarily evidence of which paths occur, not a precise new estimate of the uninstrumented performance gap. One execution per case and incomplete historical provenance limit causal conclusions.

## Correcting the indexing does not remove the observed overhead

A third execution used an isolated ARMS library with the same instrumentation and a filtered `submitted_pages` vector kept alongside `vas`. The status-processing loop and diagnostic now use that matching vector. Only `migration_worker.cpp` was recompiled; all other objects match the instrumented ARMS baseline. The [build record](fixed_index/build_record.json) and [source manifest](fixed_index/source_manifest.json) retain the exact change and hashes. The additional vector allocation/reference counting is a further performance-comparison limitation.

| Observation | Original mapping | Corrected mapping |
|---|---:|---:|
| Total | 106.28 s | 108.29 s |
| Read Time | 39.61346 s | 39.92193 s |
| Average trial | 6.62367 s | 6.79411 s |
| Completed migration calls | 1,443 | 2,119 |
| Summed completed syscall wall time | 46.158 s | 52.406 s |
| Process system CPU time | 140.22 s | 156.07 s |
| Mixed filtered/unfiltered batches | 8 | 85 |
| Successful statuses mapped to the wrong page | 46 | 0 |
| Global THP migration failures/splits | 3,341 / 3,341 | 3,506 / 3,506 |
| Global compaction failures | 67,343 | 74,369 |

The correction was exercised successfully: mixed batches remained, with zero index mismatches. It did not improve elapsed or iteration time, and the high migration/splitting/compaction activity persisted. This argues against the simple explanation that this mapping bug causes the present ARMS gap and that correcting it removes the gap. One trial per variant, instrumentation inside adaptive cost measurement, and the correction's own overhead prevent a general claim of no performance effect. This is also a diagnostic correction, not a change found between May and June.

The corrected run again had no positive-return or `ret == -1` ENOMEM calls. It recorded 1,552 direct-reclaim scan and 634 steal events globally; the initial pair recorded zero. [Corrected-run summary](results/20260909T163041Z-361705/summary.json), [VM deltas](results/20260909T163041Z-361705/global_vm_deltas.json).

All three runs completed ten trials with exit zero and no detected workload overlap. Both runner sessions reported zero restoration errors. A separate final readback found no differences in captured controls, IRQ affinity, socket uncore settings, or affinities of surviving original processes, and no BC/PR/benchmark-runner processes. The boot remained unchanged and namespace7.0 remained bound to `nd_pmem`. [Final restoration check](results/final_restoration_check.json).
