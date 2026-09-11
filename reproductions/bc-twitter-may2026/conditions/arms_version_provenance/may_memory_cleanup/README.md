# May memory accounting investigation

Investigation on the existing 6.18.1-061801-generic boot, September 9, 2026.

**No retained historical evidence establishes a second May reserve setting equivalent to `lowmem_reserve_ratio=4/4/4`. There are, however, concrete defects in the frozen May tierer's accounting and cleanup, including a directly verified way to credit a 2 MiB migration after moving only 4 KiB.** These defects remain in the later source; their existence alone does not identify the condition that changed between the slow May and faster June runs.

**Update after a real BC diagnostic pair:** the bounded samples did not show a large residency-accounting error. ARMS did record 3,998 THP migration failures/splits and 75,512 direct-compaction stalls, versus zero for the model, on the ordinary baseline. That makes eligible/contiguous memory and migration behavior the strongest measured mechanism, while the historical trigger remains unknown. [Run findings and limits](residency_probe/runtime_findings.md).

**Update following the user's historical-reconstruction clarification:** one intermediate ratio8 pair completed at 123.60/109.33 seconds, overshooting both May7 targets. Further numerical interpolation was stopped. A newly recovered sourced workload-list change establishes that May14 long BC ran alone, while both faster June cohorts included four preceding Kron executions. The May7 cost cohort also has a documented predecessor/configuration chain absent from an isolated ARMS/model pair. These are actual conditions to reconstruct, although no causal effect is yet proved. [Reconstruction synthesis](../../speed_matching/README.md), [long-cohort chronology](long_prelude.md), [May7 sequence](profiling_order_history.md).

The earlier ratio experiment added 735.63 MiB to DMA32's protection threshold and reproduced a larger ARMS/model computation gap. That is selective allocation protection, not 735.63 MiB removed from every allocation or an equivalent fixed reduction of MemTotal. We have not found evidence that the May cost-ablation sweep actually accepted ratio4 or ratio1.

## Confirmed accounting mechanisms

### One base page stands for a whole 2 MiB record

The frozen scanner queries one aligned address per 2 MiB range and, on success, clears `fragmented` and records the whole range's `in_dram` from that address's node. The ordinary migration path likewise moves one address when the flag is clear. After a THP splits, that address can identify only one 4 KiB folio.

The isolated [kernel diagnostic](check_region_residency.cpp) demonstrated this on the actual current kernel and boot, using only its own 2 MiB private base-page region:

| Operation | Actual near memory afterward | Whole-region accounting from first page |
| --- | ---: | ---: |
| Start with all pages far, promote first page | 4 KiB | 2 MiB |
| Start with all pages near, demote first page | 2,044 KiB | 0 |

All move/query calls succeeded. The process freed its mappings on exit, and the diagnostic recorded an unchanged-controls check. That artifact stores the check result rather than the individual control snapshots. The test establishes kernel behavior; the whole-region policy credit follows from source inspection. [Complete diagnostic result](region_residency_result.json).

Thus 256 such demotions could move only 1 MiB while leaving 511 MiB of near memory incorrectly classified as far. This is an arithmetic example, not an observed historical amount. The policy still reads real kernel MemFree; the defect concerns residency, victim selection, and how much space a selected demotion is expected to release.

The assignments date to March 15 UTC (`e6223c95a`) and are present in May, June, and current source. Different split/collapse and migration behavior could expose the shared defect differently in ARMS and the model. [Source audit and exact locations](accounting_source.md).

### The scanner also processes invalid entries in partial batches

The syscall receives the actual address count, but `process_batch` iterates all 128 status slots and reads `addr_batch[i]`, even when the final VMA batch has fewer than 128 addresses. This reads beyond the vector's logical size and can use stale addresses/statuses to mutate page records.

An offline harness extracted the exact frozen function. With C++ vector assertions, full 128-address batches passed, while 1- and 5-address batches aborted on invalid indexing. An isolated correction to the loop bound passed all three cases. No production file was changed. [Harness](check_scan_batch.py), [results](scan_batch_check/results.json).

This defect dates to March 14 (`d87369bce`), not a May-to-June change. It is distinct from the previously tested migration-vector indexing bug. [Migration and scanner chronology](migration_history.md).

### Stale mappings can remain in the policy

The March 14 scanner replacement stopped calling generation-based stale-page cleanup. A VMA removed from `/proc/self/maps` can leave behind scored records. A fragmented retry in which every address is absent can also mark a nominal promotion successful despite moving no bytes. These are per-process bookkeeping errors, not proof of pages leaking across benchmark processes.

Simply restoring the old cleanup call is unsafe: existing successful scanner hits currently do not refresh `last_seen_scan`. A correct fix must update generations first. [Detailed source analysis](accounting_source.md).

## Actual memory and allocator differences

Frozen May ARMS allocates **320.16 MiB of perf-ring page backing**, versus **160.16 MiB for the model**. The kernel allocates this backing eagerly, preferring each perf event's CPU node (node0 here), using non-movable allocation flags. The userspace node1 memory policy does not determine those kernel allocations. Allocation fallback is possible, so these totals are not proof that every ring page resides on node0. This is a fixed policy difference already present in the reconstructed May builds, not evidence of a newly changed ring setting. [Library allocation audit](library_memory.md).

The active free-capacity helper rereads node0 MemFree and subtracts 5% of MemTotal. It does not use the misleading startup `dramsize` comment as a separate hard cap. Actual ring allocations are reflected in the fresh MemFree reading, but the heuristic does not describe eligible zones or available contiguous blocks.

For example, the saved opening baseline had **890 MiB free in Normal but only 232 MiB already in order-9-or-larger blocks**. DMA32 contained **1,544 MiB in such blocks**. Restricting DMA32 therefore blocks access to most of the immediately available huge-page capacity, despite substantial node-wide free memory. These are current-boot observations, not recovered May layout measurements. [Kernel reserves and layout](kernel_reserves.md).

Across all 28 snapshots from the earlier setting study, CMA and explicit hugetlb pools were zero, and node0 HighAtomic/Isolate free lists were zero. Node0 base minimum/high watermarks were about 46.75/70.11 MiB, below the tierer's 231.35 MiB allowance. PCP pages were already excluded from NR_FREE_PAGES; subtracting them again would be incorrect. No additional measured reserve of hundreds of MiB appeared.

## Cleanup and migration history

Historical setup did not verify the number of pages left behind by `migratepages`. The installed utility returns shell success for positive kernel results representing unmigrated pages. That is a real preparation-verification gap, but the available current prelaunch snapshots show negligible node0 anonymous memory, not a stranded 750 MiB ordinary anonymous allocation. [Runner cleanup audit](runner_cleanup.md), [current preparation snapshots](recent_preparation_memory.json).

The old memeater insertion was disabled before May. The apparent May wildcard module commands were duplicated March history for NOMAD. No active May cgroup memory limit, large training buffer, or surviving tierer worker process was established. The historical `unsetup.sh` is identical across the relevant cohorts. [History evidence](history.md).

The April 26 commit named “Bug fix: handling migration failures” is on a different branch and is not an ancestor of the May/June cohort revisions. Their active migration-worker/scanner blobs are identical. The brief May 9 local cost-denominator edit was restored 20.478 seconds later and cannot explain the earlier May 7 results. [Dated chronology](migration_history.md).

## Evidence limits

The original timing archives lack per-zone, base-page residency, and relevant migration-failure snapshots. Retained system logs start in August; journal coverage starts in September. The apparent older journal files examined were empty. Therefore absence of a May system-log error is a retention gap, not evidence that no error occurred. [Coverage record](system_log_coverage.json).

A focused [same-kernel boot/zone history check](boot_zone_history.md) found no retained May use of `kernelcore`, `movablecore`, Movable onlining, allocator shuffle, or a post-setup sysctl reload that explains the transition. Archived `4035/4040/4041MiB` filenames were manually supplied labels, not zone or MemTotal readbacks. Boot layout was not recorded precisely enough to recover a defensible alternate May memory map.

The [THP subcontrols audit](thp_subcontrols.md) also checked settings left untouched by ordinary setup: per-size enabled values, `shrink_underused`, `use_zero_page`, and `max_ptes_none/swap/shared`. No valid alternate May state was recovered. The March editor's attempted `max_ptes_*=1024` writes are rejected on this kernel. Current `max_ptes_none=511` disables the underused-zero split test in the exact 6.18 implementation; its split counter stayed zero in all 28 saved study snapshots. Those current migration splits therefore should not be attributed to `shrink_underused` alone.

The tiny kernel test establishes a real mechanism on this kernel. It does not establish how often BC Twitter encountered it in May, or that correcting it will recreate the archived speed difference. The subsequent ARMS/model diagnostic queried 32 records at each of two computation checkpoints per policy (34/35 unique keys). It found no sampled hidden near-memory bytes, and only one 0.87 MiB partial-region overcount in ARMS. That sample is limited, but it does not support attributing a 750 MiB discrepancy to observed bookkeeping errors in the current baseline. [Exact results](residency_probe/runtime_summary.json).

No production tiering source, normal library, or historical result was changed by this investigation. Diagnostic sources and results are isolated in this directory.
