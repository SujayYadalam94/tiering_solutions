# DRAM accounting after THP splitting and mapping removal

Read-only source/history audit, 2026-09-09. No production source, host settings, or workloads were changed. **The strongest accounting candidate is that a 2 MiB tracking record can describe only one 4 KiB page correctly after splitting.** The code can then overstate either successful promotion or memory released by demotion. This is a concrete existing defect exposed by page layout, not proof of which May condition changed.

## One queried address stands for an entire 2 MiB region

The audited May 4, May 7, May 14, June, and committed current `pagemap_scan_thread.cpp` are byte-identical (SHA-256 prefix `150e974876abb569`). The current working copy also has no diff for this file.

1. Full scanning rounds each writable VMA's start down to a 2 MiB boundary and advances by `PAGE_SIZE`, which is fixed at 2 MiB. It submits only that aligned address to the residency query. [Frozen scanner](../../../source/may04/pagemap_scan_thread.cpp:385).
2. A successful node-0/node-1 query assigns the **whole record's** `in_dram` boolean and unconditionally sets `fragmented=false`. It does not query all 512 base pages or verify the mapping is a THP. [Scanner result handling](../../../source/may04/pagemap_scan_thread.cpp:257).
3. The ordinary migration path submits one address for every record whose `fragmented` flag is false. Success sets that record's `in_dram` from the target node. [Migration input and result](../../../source/may04/migration_worker.cpp:242).
4. The policy counts each `in_dram=true` record as one near-memory hugepage, and decrements/increments its temporary free-page budget by one for a selected promotion/demotion. No per-record byte or subpage-residency count exists. See `summarize_page_distribution`, `rank_scores_for_migration`, and `select_migration_candidates` in [frozen policy](../../../source/may04/arms_kernel.cpp).

For an intact THP, one address identifies the whole folio. For a region already split into 4 KiB folios, migrating one address need only move that one folio. The scanner's `fragmented=false` assignment can suppress the intended 512-address fallback even though the region remains split. The flag therefore means “the aligned address was queryable,” not “all 512 pages form a huge folio.”

Two explicit conditional examples quantify the mismatch:

| Starting layout and operation | Actual result | Policy representation |
|---|---|---|
| 512 far base pages; promote aligned page | 4 KiB near, 2,044 KiB far | Entire 2 MiB record near |
| 512 near base pages; demote aligned page | 4 KiB far, 2,044 KiB near | Entire 2 MiB record far; one 2 MiB victim budgeted |

The maximum occupancy error in either example is **511/512 of the record, or 2,044 KiB**. For 256 affected records, the second example moves only **1 MiB** while near occupancy is undercounted by **511 MiB**. These are arithmetic examples, not counts observed in a benchmark. Global THP split events do not reveal how many distinct ranges later had this layout.

This gives a route to apparent memory restriction without changing a capacity knob: near pages remain physically occupied, while the policy thinks their regions are far and cannot choose them as ordinary near victims. Conversely, overcounted near records can be selected as victims that release much less memory than expected. Different ARMS/model migration streams can expose the common defect at different rates.

The policy still reads **real node-0 MemFree**, so this does not falsify the kernel's free-memory counter. It corrupts scored occupancy, promotion eligibility, victim selection, and the amount of space expected from a selected operation. Migration costs are also calculated per requested tracking record, not per successfully moved byte.

### The fallback has an additional empty-region corner case

`log_move_base_pages` enumerates all 512 addresses, but excludes `ENOENT` and `EFAULT` from its failure count. If every address is absent, `failed_migrations` remains zero and the code still assigns `in_dram = (target_node == FAST_TIER)`. Thus a completely unmapped record can be marked near after a nominal promotion of **zero bytes**. It retains `fragmented=true`. [Fallback accounting](../../../source/may04/migration_worker.cpp:168).

This is separate from the previously diagnosed filtered-vector index mismatch and positive-return/stale-errno bug. It also does not require those bugs to trigger. Actual incidence needs address-level evidence.

## Stale mapping cleanup stopped running in March

Commit `d87369bce`, **March 14, 15:14 UTC**, replaced the full-scan implementation, removed its calls to `refresh_page_residency`, `cleanup_stale_pages`, and `update_dram_residency_stats`, and left those functions defined but unused. It also changed the scanner sleep from a ten-interval expression to one policy interval. The replacement scans current VMAs and removes some records only when a queried address returns `EFAULT/ENOENT`.

Consequences:

- A region whose **entire VMA disappears** is no longer visited through `/proc/self/maps`. Its old record can remain in `pages_map`, including a stale `in_dram=true` flag, until another path changes it or the process ends.
- Since March 15 UTC, the missing-address branch retains records marked `fragmented`; it can mark a sampled record fragmented without clearing `in_dram`. That is another route to retained stale occupancy.
- Every retained record enters the score vector. Stale near records increase `dram_pages + free_hugepages`, potentially widening ARMS's promotion eligibility and hot-age accumulation. Stale far records can still consume scoring work. This is per-process persistence; it does not survive normal process exit into the next benchmark.
- The max-DRAM statistics writer has no active sampling updates from this scanner. A zero/sampleless max-DRAM log therefore cannot certify zero near occupancy.

**Do not simply call the old cleanup function again.** The current `process_batch` only sets `last_seen_scan` when creating a record; it does not update that field on an existing successful hit. Re-enabling generation-based pruning without marking all observed canonical records would incorrectly remove live records. The old refresh function also queried one address per record, so the March change should not be described as replacing a fully basepage-aware scanner.

`PAGEMAP_FULL_SCAN_INTERVALS` and `PAGEMAP_RECENT_ACCESS_WINDOW` remain defined, but the active thread always calls the full scanner and neither setting controls it. Changing these macros alone would not restore pruning or alter scan frequency.

## What changed when, and what did not

| Date / revision | Accounting relevance |
|---|---|
| March 14, `d87369bce` | New full scanner; generation cleanup and statistics calls become unused. Live free-memory-minus-5% formula already present here. |
| March 15, 00:39 UTC, `e6223c95a` | Adds fragmented fallback and the two unconditional `fragmented=false` scanner assignments; retains some missing records. |
| May 4 `6b1bc11dd`; May 7 `8d6a8bc66` | Same scanner and the above mechanisms. |
| May 10, 04:39 UTC, `41e8adedd` | Model gets rank-based promotion/demotion guards. ARMS starts checking the new `can_demote` field, but it is initially uninitialized on the ARMS path. |
| May 14, 06:17 UTC, `890a2449c` | Initializes `can_demote=true`, closing that specific undefined-state window. |
| May 14 `4da3d6c3f` → June `d6f720be6` | Scanner and free-memory accounting unchanged. No dated repair explains the long-run speedup. |

The `can_demote` window can concern the May 12 long cohort. It cannot explain the May 7 slow standard cohort, which predates that field, or the May 14-to-June transition, where it is initialized. Prior demotion reversion experiments are recorded separately in [ARMS reversion candidates](/users/zimooo2/tiering_solutions/docs/arms_reversion_candidates.md).

Four functions—`get_fasttier_free_mem`, `get_fasttier_total_mem`, `fasttier_free_kb`, and `calculate_free_hugepages`—have identical extracted bodies from March 14 through May 4/7/14, June, and HEAD. Their SHA-256 prefixes respectively are `12bc84e55863`, `4e700607e811`, `7597674f3d95`, and `89f4d8eef177`. Free capacity is:

`max(node0_MemFree_KiB - floor(node0_MemTotal_KiB / 100) * 5, 0) / 2048`

It is not the filename capacity, startup `FAST_MEMORY_SIZE`, kernel zone watermarks, or actual contiguous 2 MiB availability. At the recorded node-0 total of 4,737,988 KiB, the fixed policy allowance withheld is 236,895 KiB, approximately 231.34 MiB. There is no observed May-to-current change in that formula.

The April 26 basepage-related revisions `9a01908d8`, `455f9f262`, `dd986d2e4`, and May 7 `087991476` are **not ancestors** of `8d6a8bc66`, `d6f720be6`, or current HEAD. The last resides on `origin/arms_kernel_working`. Their changes cannot be treated as improvements actually incorporated into the audited benchmark lineage merely because their dates fall nearby.

## Settings that change exposure on the same kernel

THP allocation/coverage and later splitting change whether the one-address assumption is valid. Top-level THP `enabled`/`defrag`, per-size 2 MiB eligibility, and khugepaged collapse pacing/defrag can change that layout. Migration destination-allocation pressure can also split folios. A collapse can temporarily restore a single-folio region; a later split can expose the mismatch again. These pathways are verified in [kernel mechanisms](../hugepage_settings_history/kernel_mechanisms.md). Current migration split counts support checking the pathway, but do not establish mixed residency or historical attribution.

Ordinary ARMS uses the 500 ms policy/scanner interval, while the model's virtual-feature build uses 250 ms. That is an existing exposure difference: more frequent scans can clear a known-fragmented flag sooner, although direction and net performance effect require measurement. Virtual-step sample count does not change the 2 MiB keying or add per-basepage residency accounting. Sampling can affect `found_in_pebs` and retained-fragment eligibility, but that is not a newly dated May setting change.

The most discriminating future check is to compare tracked state against **all 512 node statuses** for a bounded sample of split/fragmented records, and distinguish requested records from bytes actually moved. An isolated correction must address residency granularity as well as migration granularity; merely stopping the flag reset still misses previously unknown split regions. This audit proposes no production change and runs no additional benchmark.
