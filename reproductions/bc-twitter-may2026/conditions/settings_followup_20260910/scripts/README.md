# Settings and launch history recheck, September 10

Read-only investigation. No runner, benchmark, setup, defrag, or host-setting command was executed. Only this report directory was written. Today's deliberate experiments are excluded as historical explanations.

**No additional persistent ARMS setting change was found between the slow May and faster June cohorts.** This recheck separates actual source changes from execution evidence and confirms several plausible-looking later differences do not fit the observed dates. It does not establish identical effective May settings; the surviving files are requested configuration, not historical readbacks.

## Direct comparisons

Compared 16 launch/core files at six Git states and the current working tree; revisited 187 May-through-September-8 saved editor versions of these files. [Git blobs and hashes](git_blob_comparison.json), [editor manifest](saved_settings_entries.json), and [May-to-current runner diff](may14_to_current_runners.diff) preserve this recheck. This is overlapping verification of previous investigations, not 187 newly discovered snapshots.

The May 14 state `4da3d6c3f` and June state `d6f720be6` have identical `setup.sh`, `unsetup.sh`, `defrag.sh`, `measurement_common.sh`, `measurement_arms.sh`, `measurement_model.sh`, `defs.h`, `migration_worker.cpp`, `pebs_scan_thread.cpp`, and the three checked BC workload definitions. Outer runners and enabled workload lists differ. Git's outer-runner selection cannot identify the executed June suite on its own: the contemporaneous saved runners and original timing chronology remain necessary, as established in the [existing call-path audit](../../arms_version_provenance/hugepage_settings_history/call_paths/README.md).

All 20 explicit common VM/THP controls in current `measurement_settings.sh:48–67` have corresponding active May setup requests with the same final values. The three additional requested controls are MGLRU `0x0000`, perf maximum sample rate `1000000`, and perf CPU-time percentage `0`. The [cross-reference](common_setting_cross_reference.json) gives exact old and new source lines. Successful application is a separate question.

## Changes that could look suspicious, and their limitations

| Difference | Concrete evidence | Assessment |
|---|---|---|
| Model history/cost selection | May 10 04:39:59 UTC commit `41e8adedd` changes history mode `2` to `1`, and the cost sweep to `0.1`; current `measurement_model.sh:30–35` retains this. | A real post-May-7 model configuration change. Already known and restored by the old-model reproductions. It cannot directly make normal ARMS faster. [Exact diff](41e8adedd_runners.diff). |
| Perf write-order repair | Current `measurement_settings.sh:75–79` temporarily enables percentage `25`, writes the rate, then restores `0`; May tried the rate before percentage `0` and could retain an inherited rate on failure. | Real requested-versus-effective-state issue, but no recovered accepted May rate. Already tested at `100000`: ARMS **104.95 s**, old model **99.62 s**, rather than archived ARMS **115.804 s**. Do not propose it as an untested new discovery. [Existing results](../../arms_version_provenance/may_memory_cleanup/speed_match_targets.md:27). |
| Failure propagation and inner-defrag path | Current setup exits on failed common/uncore writes and invokes its inner defrag by absolute path. May generally continued after write errors and used relative `bash defrag.sh`. | A failed current setup can still be followed by a workload because callers do not check its return; an old external-CWD launch could run one rather than two defrag passes. Both are genuine conditional behavior differences, already documented. No historical target-run failure or external CWD was recovered. |
| `vfs_cache_pressure=2000` timing | The setting moved from after global process migration to the earlier common-settings block, now `measurement_settings.sh:56`. | Same eventual value. Potential preparation-state effect only; no corresponding May-to-June source transition. Existing full-May-setup reconstructions did not restore the archived gap. |
| Uncore and VM changes introduced for MEMTIS | September 3 commit `671e0e644` adds default-watermark and THP alternatives and skips common uncore slowdown only when `MEASUREMENT_SYSTEM=memtis`. | Normal ARMS/model preparation explicitly passes `default`, so these alternatives do not apply. Too late to explain the June narrowing or pre-MEMTIS August 27 fast results. [Exact conditional diff](671e0e644_runners.diff). |
| Process-affinity widening / special all-NUMA setup | Current `setup.sh:58–62` uses `taskset -pc`, affecting a PID's main thread, just as May. The later `all_numa` exception at lines 87–94 only runs for that explicit system argument. | Current ordinary ARMS/model requests `default`, so it still migrates existing processes and applies the May main-thread affinity. There is no active `--all-tasks` in this normal path and no hidden cgroup placement in the launcher. Actual host grouping belongs to the separate cgroup audit. |
| ARMS metric-file rename | July 29 02:51:30 UTC commit `2caec5ede` renames offcore output cleanup/copy to uncore CAS output. | File cleanup is before the timed region and copying is after it. This wrapper change does not enable a profiler. Source-level collection flags must be evaluated separately; the dates are already later than the June gap. [Exact diff](2caec5ede_runners.diff). |
| Broken sourced-file first line | The same July commit contains `df#!/bin/bash` in `measurement_common.sh`; September 3 fixes it. | Would emit a command-not-found when sourced and continue in the ordinary non-errexit wrapper. The line is outside the workload timer, and both dates are after the June transition. Not a credible explanation for a 15-second timed ARMS improvement. |

## Unchanged launch assumptions

The common timed wrapper still requests memory nodes `0,1`, CPUs `0-9,20-29`, a named ARMS/model library through `sudo env LD_PRELOAD=...`, and `OMP_NUM_THREADS=16` for BC. Standard Twitter remains 10 trials and long Twitter 100. No policy-specific graph-path branch occurs in this wrapper. Graph default changes affect both policies and do not establish the physical device used by an old file path.

Standalone `measurement_arms.sh` still does not run setup, whereas `measurement_model.sh:101` prepares before every model; that asymmetry predates May. The archived batch wrappers prepare ARMS themselves. Manual current comparisons need that same preparation, but this is not a newly changed setting.

The earlier expanded shell/IPython/Vim/editor search already covered allocator environment (`MALLOC_*`, `GLIBC_TUNABLES`), OpenMP placement, priorities, ulimits, cache/reclaim controls and external scripts without locating a May/June override. This recheck found no new saved source contradicting that result. [Prior scope and exact matches](../../correlation_deep_dive/setup/README.md).

## Remaining evidence limit

The dated workload/preparation context differs, and older settings writers did not guarantee every requested write succeeded. Those are evidence-backed sources of uncertainty, not identified causes. A new benchmark sensitivity setting should not be labeled historical without an old command, accepted readback, or producing script tied to the target boot. This script audit provides no additional historically supported ARMS knob to revert beyond the options already tested or explicitly qualified above.
