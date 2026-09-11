# Migration chronology and memory-bookkeeping candidates

The retained history does not identify a migration-worker change between the slow May cohort and the faster June cohort. It does identify a separate, still-untested scanner bounds bug that can corrupt the tierer's memory-residency bookkeeping. Both conclusions matter: a real bug is not automatically the historical cause.

## Dated evidence

The source identities and `git merge-base --is-ancestor` results are saved in [migration_chronology.json](migration_chronology.json). `migration_worker.cpp`, `pagemap_scan_thread.cpp`, and `policy_thread.cpp` have identical Git blobs at May4 `8d6a8bc66`, May14 `4da3d6c3f`, and June `d6f720be6`. The worker blob is `8f8a879b3a98a69340356def955a94f3d14b1a21`.

| Date, UTC | Commit / evidence | Effective significance |
| --- | --- | --- |
| March14 15:14:19 | `d87369bce` | Introduces the active full pagemap scan's fixed 128-status vector and mismatched loop bound. Also establishes node0 free-memory minus 5% headroom and promotion-first migration handling. |
| March15 00:39:26 | `e6223c95a` | Introduces shared ownership of page metadata and fragmented-range base-page retries. Adds the scanner's early return on a nonzero residency-query result. |
| March15 23:05:27 | `fedcde1ee` | Batches fragmented retries; changes the ENOMEM retry strategy. |
| March17 00:19:43 | `ec524f599` | Corrects swapped fragmented/failure conditions: missing base pages can mark a range fragmented without counting as migration failures; returns the failed tracked-range count instead of only 0/1. This is the last active worker failure-bookkeeping change before the May/June cohorts. |
| April9 / April23 | `d92cb822f`, `563220a4a` | Runtime thread registration/exclusion changes. No new migration status or reserve algorithm. |
| April26 23:05:18 | `dd986d2e4`, “Bug fix: handling migration failures” | **Different branch**, retained on `origin/arms_kernel` and `origin/arms_kernel_working`; not an ancestor of any of the three cohort commits. It updates the older monolithic `arms_kernel.cpp` to inspect each status and divide costs by successes. It is not the active May worker implementation. |
| May7 21:36:38 | `087991476`, “Initial commits for basepage tracking” | Also not a cohort ancestor. Likewise April21 `0b85acc6b` removing `FASTTIER_SIZE` and April26 `9a01908d8` / `455f9f262` are off the cohort ancestry. |
| May9 06:12:35.165 | editor `18ca2f0f/iMTY.cpp` | Brief local worker edit: divide promotion/demotion time by `requested - initial_failed` instead of requested count. |
| May9 06:12:55.643 | editor `18ca2f0f/eUcM.cpp` | Restores the original denominators 20.478 seconds later. This final editor version exactly matches frozen May4 source. No retained object proves the intermediate was built or used; early-May archives predate it. |
| May9–14 | model-policy changes | Add model promotion/demotion rank gates. Normal ARMS' added `can_demote` remains true. No worker/scanner change; the previously tested gate reversal did not restore archived ARMS timing. |
| June13 | model feature adapter | Changes model feature construction, not the normal ARMS worker/scanner. See the existing [code/build audit](../root_cause_code/README.md). |

Dates above use UTC. For example `ec524f599`'s author timestamp is March16 19:19:43 at UTC−05, hence March17 UTC.

The editor inventory retains 50 worker snapshots. Its only April-or-later worker saves are the two May9 versions above; the 50 scanner saves all predate April. Missing saves are not proof that no unsaved or externally edited source existed.

## Newly identified active scanner bug

Frozen [pagemap_scan_thread.cpp](../../../source/may04/pagemap_scan_thread.cpp) has SHA-256 `150e974876abb5695ba6b4f5a33ebf611b0c8aba78b9b665354f11aeb8f8b4da`.

- `scan_process_pages_full()` line352 sizes `status_batch` to `PAGEMAP_BATCH_HUGEPAGES`, which is 128.
- Each writable VMA gets a fresh address vector with capacity128. Its final batch can contain fewer than128 addresses; line410 still calls `process_batch()`.
- `process_batch()` line245 queries exactly `addr_batch.size()` addresses, but line257 processes `status_batch.size()` entries and line260 indexes `addr_batch[i]`.
- The active scanner loop calls this full scan unconditionally at line471. The separate old helper with a correctly resized status vector is not the active route.

A short final batch therefore reads beyond the address vector's logical size, using status slots the syscall did not fill. Reserved capacity does not make those vector elements valid. Stale/initial-zero statuses can lead to insertion or mutation of invalid page records, false residency, or a cleared fragmentation marker. The code counts tracked `in_dram` records as huge pages, so corrupt records can also distort estimated tier occupancy.

This is distinct from the already-tested migration worker's **filtered vector / original vector index mismatch**. Root's bounded offline check is recorded in [scan_batch_check/results.json](scan_batch_check/results.json) and [check_scan_batch.py](check_scan_batch.py). No production migration or workload is required to establish the bounds error. The harness establishes invalid indexing; it does not measure corrupt records in archived runs or a timing impact.

The bug predates all relevant cohorts. Heap layout, writable VMA shape, ASLR or library layout could change how stale addresses manifest without a source change, but we have no dated runtime evidence for that transition. It is a concrete correctness candidate to fix or instrument separately, not an explanation already proved.

## Existing logic that amplifies unavailable fast-tier capacity

Frozen `arms_kernel.cpp` lines1653–1663 estimates allocatable huge pages as node0 `MemFree` minus 5% of node0 `MemTotal`, then divides by2MiB. It does not observe per-zone protection or the existence of a contiguous order9 block. The startup `dramsize` value is not an additional active hard migration cap in this source.

The worker first attempts promotions; it performs demotions only when the initial promotion operation reports failed tracked ranges. A kernel THP allocation failure followed by successful split/base-page migration can therefore return success without triggering planned demotions. Pure demotion tasks also do not enter the worker's promotion-guarded migration body. This is an old policy/allocator interaction, not a May-to-June source delta.

Additional unchanged weaknesses can amplify an adverse allocator state:

- Cost estimates divide elapsed time by requested ranges, even for failed/redundant requests. The failed range count is not a count of the kernel's internal failed THP allocations.
- Each policy iteration clears queued work, but active worker batches remain in progress. No in-flight deduplication prevents a later iteration from queueing the same not-yet-updated record again.
- A successful query of a 2MiB range's anchor address resets its fragmented flag and treats the range as located at that anchor's node. A split range can have mixed residency.
- Worker cost averages and record flags are accessed by multiple threads without a complete synchronization scheme. This is a concurrency correctness concern, but unchanged source and no cohort-specific evidence make it a weaker historical attribution than measured allocator settings.

## What current probes and cleanup rule out

The instrumented ARMS/model pair recorded no positive `numa_move_pages` returns and no ENOMEM handling. Both policies encountered the filtered-index bug, with more affected model statuses. Correcting that bug in an isolated ARMS library changed 106.28s to108.29s, with THP splits remaining high. It did not explain the current slowdown; see [runtime findings](../migration_probe/runtime_findings.md). No new benchmark was run for this audit.

Normal shutdown calls `_exit(0)` before user-space joins/explicit perf cleanup. Queue entries retain `shared_ptr` **metadata**, not persistent physical page pins. There is no normal-route detached daemon, `mlock`, or `MAP_LOCKED` allocation in the reviewed tierer. Skipping joins does not by itself establish memory retained after process termination. System-wide fragmentation and page cache can outlive a process; surviving external jobs or modules require the separate process/module evidence in [runner_cleanup.md](runner_cleanup.md) and [history.md](history.md).

The strongest current mechanism remains allocator eligibility/contiguity plus aggressive collapse/compaction. The strongest newly found code defect is the scanner's invalid batch bound. Neither identifies the missing May setup state from historical timing files alone.
