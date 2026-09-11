# ARMS source audit for archived BC Twitter performance

Audit date: 2026-09-09. Scope: normal `libraries/C220G5/libhemem-arms.so`, compiled with `USE_MODEL=false`, compared with the archived 6.18 BC Twitter results.

Follow-up: the later [historical model and reproduction diagnosis](bc_twitter_reproduction_diagnosis.md) adds completed isolated May 4/current runtime experiments and recovered model provenance. The attempted-run status at the end of this document describes the earlier audit only.

The subsequent [early-May-to-September reversion audit](arms_reversion_candidates.md) includes today's new perf-ring edits, the July/September verbose toggle, a coherent demotion-check reversion, and newly compiled May 2/float-arithmetic candidates. The unchanged-ring statement below describes the earlier audit snapshot, before today's ring experiments.

## Conclusion

The source audit does not establish a change responsible for the speedup. The substantive ARMS migration-policy change is the initialization of `can_demote`. The user's experiment removing that initialization did not reproduce the old slowdown, so it is not a demonstrated explanation. Most other tiering changes affect model inference, training, or verbose logging, which normal ARMS does not execute.

The archived ARMS totals are 112.781, 118.355, and 116.275 seconds (mean 115.804). The previously reported current SSD total was 98.597 seconds, with approximately 40 seconds reading the graph. The archive does not provide the corresponding read/computation split.

## Provenance

The exact archived `4009MiB_run[123].time` results first appear in commit `41e8adedd` (May 9, repository timestamp). Commit `890a2449c` (May 14) moves the same results into `times/c220g5_final/arms_6.18/bc-twitter.sg`. Searching history for the exact `1m52.781s` total confirms this timeline. Filesystem modification dates do not establish measurement dates.

Because May 9 contains both results and policy edits, the code committed alongside the results is not proof of the binary used. The audit therefore includes its May 4 parent `8d6a8bc66`, May 9 `41e8adedd`, and current working-tree source, including uncommitted changes.

## Changes that matter to normal ARMS

| Change | Effect and evidence |
|---|---|
| May 9 adds `cold_page->can_demote` to candidate selection | The new field is not initialized in the May 9 ARMS path. Previously, cold-page selection checked `in_dram` without this field. An unset flag can prevent replacing cold DRAM pages. |
| May 14 initializes `can_demote = true` in `page_info::reset_page_access_fields()` | Fixes the uninitialized field for ARMS. This is the only source change in `page.cpp` between May 9 and current. The user's removal experiment did not demonstrate a meaningful speed difference; undefined initial state also makes that experiment an imperfect reconstruction of an old binary. |
| Current Makefile adds `defs.h` and logging headers as object prerequisites | Header edits previously could leave old object files in place. A rebuild can therefore change the effective settings even if the current textual settings look unchanged. No preserved historical library/build manifest establishes that this occurred. Compiler optimization defaults remain `-O3 -DNDEBUG`. |
| Setup changes perf-limit write ordering | Previously writes the sample-rate limit and then disables throttling. Current setup temporarily sets the CPU percentage to 25, writes the requested 1,000,000 sample limit, then sets the percentage to 0. This changes whether setup can successfully apply the intended limit when starting with throttling already disabled. Historical settings readbacks are unavailable; no performance attribution is established. |

The Makefile's default platform changed from GSL_OPTANE to C220G5 in the May 9 commit. The C220G5 measurement runner requests the C220G5 library path. This does not prove that the archived run used an incorrectly compiled platform.

## Changes excluded as a direct explanation

With normal ARMS settings, the PEBS sampling periods and ring size, pagemap scan settings, ten migration workers, page-score weights, hotness logic, and fixed 1.5 migration-cost multiplier are unchanged across the audited versions. The ARMS branch of `update_can_promote` is unchanged; May 9 changes its signature to support model-only logic.

The larger training log, Parquet writer, model age cap, model files, model history/scaling settings, and all-NUMA training mode do not change normal ARMS's active scoring path. Verbose OFFCORE metrics were replaced by IMC write-CAS metrics, but normal builds have `ARMS_VERBOSE=false` in both versions. New Arrow/Parquet helper-library filters do not affect GAPBS, which does not load those libraries.

The graph path has changed, but the historical path alone does not establish whether its backing device was HDD or SSD at measurement time. CPU affinity, intended slow-tier uncore setting, and BC command (`OMP_NUM_THREADS=16`, `-n 10`) are unchanged. ARMS still derives available fast-tier capacity from current free memory; the MiB filename is not an enforced capacity setting in these scripts.

## Verification and remaining limit

All three source versions compile and link using the same installed GCC 15.1 compiler and normal C220G5 ARMS flags. Comparing `objdump -dr` output, with only the file-identifying header removed, finds identical instructions and relocations for `pebs_scan_thread.o`, `pagemap_scan_thread.o`, `migration_worker.o`, `policy_thread.o`, `groups.o`, and `timer.o`. The May 9-to-current `page.o` disassembly changes only around the reset initialization. This compares freshly built source versions, not the unavailable original benchmark binary.

Snapshots, libraries, build logs, disassemblies, and `compiled_comparison.json` are saved under `/tmp/arms-source-audit-jraqd_s5/`. These temporary builds do not replace the working libraries.

An attempted May 9 runtime diagnostic was stopped when a user-launched benchmark began. The runs overlapped briefly; the diagnostic is explicitly marked invalid, and there is no completed old/current runtime comparison from this audit. No current or May 4 diagnostic was started. A sequential comparison of those isolated builds under identical settings remains the direct way to determine whether the source version reproduces the slowdown.
