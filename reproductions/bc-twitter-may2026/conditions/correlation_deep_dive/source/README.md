# Saved source and build correlation audit

Read-only historical investigation, September 9, 2026. Dates below are UTC. Only evidence files in this directory were written; no benchmark, working-library rebuild, production-source edit, host-setting change, Git mutation, or reboot was performed. Macro preprocessing was performed in subprocesses without generating executable code.

## Conclusion

**No recovered normal-ARMS source/build change fits the closest slow-to-fast interval, May 14 to June 13–14.** All **20** top-level C++/header/build files plus `hook/hook.cpp` have identical contents in the May 14 and May 30 Git trees, the latter being the retained HEAD during June's fast cohort. This extends the earlier 16-file comparison to the complete source list and supporting headers. [Endpoint hashes](all_core_endpoint_hashes.json).

The expanded search did recover **previously unexamined March source chunks and unreachable Git objects**. These improve coverage, but they do not supply a missing May–June migration fix. They also show why a save/commit date cannot be treated as evidence that an executable ran.

## Additional recovery beyond the preceding audit

1. **Unreferenced editor files:** Compared every file in all retained VS Code History directories with its `entries.json` inventory. There are **zero orphan snapshot files**. No separate VS Code `data/Backups` directory or Vim source/undo backup was recovered. A filename search for the ARMS core source and conventional `.bak`/`.orig`/`.swp` variants outside the repository's already inventoried reproduction tree found no alternate ARMS implementation. [Orphan inventory](orphan_editor_files.json), [additional backup locations](additional_backup_inventory.json). This does not claim to recover deleted files or remote-machine histories.
2. **Unreachable Git objects:** `git fsck --no-reflogs --unreachable --no-progress` found **397 unreachable trees, 134 blobs, and zero commits**. Inspected tree entries and source-sized blobs; six are actual C++/header source objects. Four exactly match known July/September editor saves; two September 8 objects contain the already known training/hook and metrics changes. None establishes a May–June source change. Git loose-object mtime is a storage timestamp, not an original source-save date. [Full inventory](git_unreachable_blob_sources.json), [six core-source comparisons](unreachable_core_comparison.json), [reproducible extraction](audit_unreachable.py).
3. **Copilot source caches:** Read two SQLite indexes in read-only mode. `workspace-chunks.db` has 70 file records; `local-index.1.db` has 24,996. Recovered **183 C++/header chunks across 12 indexed revisions of nine source files**. Their embedded source-version timestamps fall on **March 9–17**, and the databases' mtimes are March 17. These are partial cached source fragments, not complete working-tree snapshots or build manifests. [Inventory](index_source_inventory.json), [extractor](audit_index_chunks.py).
4. **Index comparison:** Matched cached fragments against all reachable Git variants, including merge-parent changes, and retained editor snapshots. **179/183 chunks** have matching recovered source fragments. For the structurally elided workspace chunks, the comparison treats explicit `/*...*/` markers as omitted text and matches each remaining fragment; this is not a full-file identity claim. Four unmatched chunks are from the March 10 migration worker described below. [Match results](index_chunk_source_matches.json), [unmatched fragments](index_unmatched_chunks.json), [comparison script](compare_index_chunks.py).
5. **Other Copilot indexes:** Inspected four `codebase-external.sqlite` databases; they contain file metadata, not source text. Recovered **70 core-source metadata rows**. The older persistent worker and scanner mtimes agree with existing files; no new May–June source payload was recovered. [Metadata](codebase_index_source_metadata.json).
6. **Actual ARMS macro selection:** Reprocessed **45 pre-September-9 `defs.h` saves** with `-DC220G5 -DUSE_MODEL=false`. **42 preprocess successfully**. Two April 26 saves and one August 29 save have incomplete conditional directives; they are excluded as buildable macro states. This is preprocessing, not a build-success test. [Timeline](normal_arms_defs_timeline.json), [script](audit_defs.py).
7. **Build history:** Extracted compiler flags, source list, platform, normal-ARMS defines, and compile/link recipes from **all 50 retained Makefile saves**. No compiler-flag, source-list, normal-ARMS define, or recipe transition appears in that retained sequence. There is a brief May 5 platform edit, documented below. [Timeline](normal_arms_makefile_timeline.json), [script](audit_makefile.py).

The large unnamed unreachable Git blob is a **301,200,432-byte Parquet object**, stored July 17 04:59:08. Only its header was inspected here; it is not an old library or C++ source. [Classification](large_unreachable_blob_kind.json). Three other unreachable blobs preserve May 4/5/14 `run_all_measurements.sh` versions and were passed to the separate script audit.

## Dated candidates and their limits

| Dated evidence | Possible mechanism | Correlation assessment |
|---|---|---|
| Cached worker source version **March 10 04:27:11**; cache retained March 17 | Unguarded per-page migration-failure `std::cout`/flush and **two** per-page promotion retry passes. More failures could multiply syscall/output work under ARMS. Older state update records `in_dram` without the later per-base-page status updates. | Newly recovered source evidence, but **execution unproven**. Predates the March 14 worker rewrite and cannot explain May 14→June 14. Useful only for separating March-era implementations from May. |
| March→April perf-event/ring changes; already established by earlier audit | Large changes in allocated sampling-buffer memory and which accesses guide migration. Could change fragmentation and repeated migration pressure. | Fits a different code era for March 6.2 ARMS versus later models; **not a post-May-14 change**. |
| **April 27 23:44:46** verbose enabled; **April 29 01:41:50** disabled | Output plus verbose-only counting overhead. | Real saved transitions, both before May's target runs. |
| **May 5 01:42:34–01:42:53** Makefile briefly selects `GSL_OPTANE` instead of `C220G5` | Building for another topology would change tier/CPU assumptions. | Only 19 seconds of retained editor state. Platform-specific output directories ordinarily produce a GSL library or a missing C220 library after clean, rather than silently replacing the C220 library. No matching build/run record; cannot fit the May 14→June interval. |
| May 9 unconditional migration-count print, temporary cost multipliers and success-denominator edit; gate changes May 9–11 | Candidate-selection output, different cost rejection, and temporarily uninitialized demotion eligibility can affect wasted migration work. | Real saved experiments already documented in the preceding audit. Print/cost experiments end before May 14, and May 14 is still slow; the restored May 14 source matches June. No new execution linkage recovered. |
| **June 13 22:06:44** model adapter removes age | Could change model selections or preceding-workload placement. | The only retained core-source save in the interval. Previous isolated compilation gives byte-identical normal-ARMS `model.o` for both adapters. It cannot directly make normal ARMS faster. Restoration precedes the second fast June cohort. |
| **July 17 05:13:40** verbose enabled; **July 29 22:48:58** disabled | Output and verbose-only counting overhead. | A real local change potentially relevant to a July-specific timing cluster, but **after the first June speedup**. The July 29 unreachable `defs.h` blob corroborates the saved verbose-off contents. |

The newly recovered March worker's **closest Git source differs in only a small set of control/accounting choices**. The whitespace-normalized diff shows the two-vs-one retry passes, active-vs-commented warning output, `expected_hugepages` treatment, and status bookkeeping. [Cached joined fragments](index_chunks/march10_migration_worker_joined.cpp), [closest-Git diff](index_chunks/march10_worker_closest_git.diff), [match metadata](march10_worker_match.json). The joined fragments intentionally do not claim to be an exact full original source file; the cache strips blank lines and stores chunks.

## What stayed constant in retained normal ARMS definitions

Across all usable retained `defs.h` saves from April 26 through September 8:

- `MIGRATION_WORKER_COUNT = 10`.
- ARMS `BACKOFF_PERIOD = 0`.
- ARMS `PERF_PAGES = 1 + (1 << 11)`.
- `PAGEMAP_FULL_SCAN_INTERVALS = 4`, `PAGEMAP_RECENT_ACCESS_WINDOW = 1`, `PAGEMAP_BATCH_HUGEPAGES = 128`.
- C220G5 fast tier 0, slow tier 1; scan CPU 1 and policy CPU 2.
- The default ARMS sample period remains 10007 in complete snapshots.

May 4's migration-cost macro rename retains **1.5** for normal ARMS. July/August logged-sample-limit changes are not evidence that ordinary ARMS logging was enabled. Today's perf-ring experiments are deliberately excluded.

The old Makefile compiles normal ARMS from `arms_kernel.cpp`, `pebs_scan_thread.cpp`, `pagemap_scan_thread.cpp`, `migration_worker.cpp`, `policy_thread.cpp`, `timer.cpp`, `hook/hook.cpp`, `groups.cpp`, `page.cpp`, `logging.cpp`, and `model.cpp`, with `USE_MODEL=false`. Legacy code not present in that source list cannot affect the normal library simply because a saved file exists. The May 14 and June Git versions request `-O3 -DNDEBUG` with C220G5. Environment overrides and exact loaded-library hashes are not preserved per historical run; repeated clean builds make a stale object explanation weaker but do not recover a missing build manifest.

## Migration fields in historical training logs

**Do not interpret Parquet `num_promotions`/`num_demotions` as completed migration counts or THP migration failures.** In the exact May Git source, they increment when the policy selects a page and pushes it onto a migration candidate list, before worker completion. Cold-start virtual rows explicitly zero both fields. [Exact May Git excerpts](may_git_migration_counter_semantics.txt).

Training variants also differ:

- `arms_train`: normal ARMS plus training logging; migration workers remain enabled.
- `arms_near_train`: additionally uses the near-memory/tracing path; workers remain enabled.
- `arms_cxl_train`: far-memory default plus `ENABLE_MIGRATION_WORKERS=false`.
- Model `_train`: `USE_MODEL=true`, so its migration decisions are model decisions.

The May Makefile's nominal `arms_nomigrations` define uses `MIGRATION_WORKERS_ENABLED`, whereas source checks `ENABLE_MIGRATION_WORKERS`; its name alone does not prove migrations were disabled. The normal benchmark runner selects the ordinary `arms` library, so this typo is not a newly identified normal-ARMS transition.

## Remaining uncertainty

No saved source proves that old May ARMS encountered more migration failures, and no newly recovered source change aligns with the entire May-to-June transition. The source evidence supports **testing ARMS's response to fragmentation, retries, and compaction**, but it does not date a cause. Unrecorded shell/editor changes, remotely copied source, and overwritten executables remain gaps in the record. Several editor resources retain only their latest 50 saves; absence from that history cannot establish that an earlier unsaved or evicted experiment never happened.
