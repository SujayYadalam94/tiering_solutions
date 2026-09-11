# Further root-cause investigation

September 9, 2026. This follows the original-timestamp and checkout audit. The target remains the May 7 standard BC Twitter ARMS/model gap; the separate May-to-June long series is supporting evidence rather than an interchangeable benchmark.

**Result:** no dated change has yet been proven to cause the historical slowdown. The new runtime evidence locates substantial present ARMS overhead in huge-page migration/splitting and compaction. A tested migration-index correction does not remove it. The expanded history also reveals a June model-adapter change, but that change does not affect normal ARMS and was restored before the June 22 fast cohort.

The subsequent [bash-history and huge-page settings audit](hugepage_settings_history/README.md) checks manual overrides, standalone versus sweep launch paths, and requested controls across the May-to-current script versions. It finds a real standalone-launch asymmetry but no demonstrated policy-specific setting difference in the archived sweeps.

## What the additional history actually changes

### A local model change coincides with the June experiment

The June 13 22:06:44 UTC editor save of `model.cpp` changes the feature count from 12 to 11 and removes `age`. It precedes the June boot and the saved long runner. This is a concrete uncommitted difference missed by comparing the 17 previously checked Git files. It changes model inference, not normal ARMS: the relevant loop is excluded from normal ARMS compilation, and the edit changes no shared structures or global data.

The retained `models/no_age` and `models/with_age` BC forests are byte-identical and expect 12 inputs. If either was linked with this adapter, inputs after position 7 shift left, and the forest reads a nonexistent twelfth input. This is conditional: no per-run binary/object manifest proves those weights were linked on June 13. On June 18 at 16:07:52 UTC, the adapter was restored to 12 inputs with age; the June 22 fast runs therefore have a contemporaneous saved restoration. Current top-level forest files also date to June 18, so identical model names across these cohorts do not establish identical weights.

The exact saves, mappings, compiled-forest evidence, and normal-ARMS exclusion are in [the code audit](root_cause_code/README.md). The [model file inventory](root_cause_artifacts/model_artifact_dates.json) preserves dates and hashes. This weakens the earlier description of June as a controlled comparison of unchanged model code, but does not explain ARMS becoming faster.

### The absolute speedup extends beyond 4 GB

| Workload and capacity label | May mean ARMS | First June mean ARMS | Reduction |
|---|---:|---:|---:|
| BC Twitter, 4 GB (4035 → 4040) | 808.557 s | 625.315 s | 22.66% |
| BC Twitter, 6 GB (6032 → 6040) | 709.392 s | 607.534 s | 14.36% |
| PR Twitter, 4 GB (4035 → 4040) | 2850.425 s | 2587.799 s | 9.21% |
| PR Twitter, 6 GB (6032 → 6040) | 2791.262 s | 2482.627 s | 11.06% |

All are 100-trial workloads. Models also improve in these comparisons, with the feature/weight caveat above. This is not solely a small-DRAM ARMS phenomenon. It remains compatible with a shared graph or CPU/memory-condition change across boots, but provides no direct measurement of which condition changed.

The apparent roughly twofold improvements in some other long workloads cannot be used as corroboration: Kron trials changed from 100 to 50, and FAISS repeats from 10 to 5. Only the Twitter pair supplies matching May/June long commands in these records. Both read the same graph, so the inventory cannot distinguish a graph-specific change from a wider machine change.

The original shell timing files do not contain usable application CPU accounting: successful ARMS runs lasting minutes record a median of only 0.043 user+system seconds. Thus their `sys` values cannot diagnose migration or reclaim overhead. May 13 same-boot model runs at 10 and 100 trials provide conditional fixed/per-trial fits, but BC visits additional source vertices at 100 trials; the fitted intercept must not be called measured Read Time. There are no equivalent June pairs or matched DRAM/CXL controls. [Timing report and original rows](root_cause_timing/report.md).

### Stronger negative evidence for ordinary source/toolchain changes

The latest relevant saved ARMS core, page, defs, Makefile, and worker files match May 14 Git exactly. No retained save changes those files before June 14. The full code audit checks 597 relevant snapshots; absence of a save does not rule out external edits or stale binaries.

The GAPBS BC executable retains both modification and change timestamps from March 5; its source matches the checked-out GAPBS revision. SourcePicker initializes a fixed seed, so a time-of-day random seed is not an evidenced explanation. The installed OpenMP, C++ runtime, and libnuma files predate May. Retained package logs contain no May/June transactions. These observations weaken an ordinary BC rebuild or package-upgrade explanation. [Executable/runtime hashes and package chronology](root_cause_artifacts/gapbs_runtime_package_evidence.json).

No dated May-to-June GRUB/PMEM, governor, uncore, or graph-mount change was recovered. The setup scripts request slow-socket uncore but do not capture effective historical frequencies. Their error handling also does not establish successful writes. Those are gaps in provenance, not proof that a write failed. [Host audit](root_cause_host/findings.md).

## Interpretation before the new runtime probe

The source lineage of the original slow ARMS is substantially narrowed, but no intervening ARMS commit or recovered host change establishes its slowdown. Rebuilding early-May ARMS and replaying the recovered setup previously produced roughly 103 seconds, versus 115.804 seconds in the original May 7 archive. A shared HDD test also did not recover the old relative difference.

The bounded diagnostic below measures whether the already identified migration error paths are exercised by reconstructed ARMS and the model. Aggregate observation can establish a current mechanism or weaken a proposed bug explanation. Without equivalent May traces, it cannot by itself establish the cause of the historical transition.

## Completed runtime diagnosis

Three isolated ten-trial runs used the same SSD graph on the same 4 GB 6.18 boot, with the recovered full preparation before each. The reconstructed model uses the May 3 forest and history mode 2, length 10, cost scaler 0.5.

| Case | Total | Read | Average iteration | Global THP migration splits |
|---|---:|---:|---:|---:|
| Reconstructed May ARMS, instrumented | 106.28 s | 39.61 s | 6.624 s | 3,341 |
| Reconstructed May model, instrumented | 98.71 s | 39.71 s | 5.875 s | 230 |
| ARMS with migration-index correction, instrumented | 108.29 s | 39.92 s | 6.794 s | 3,506 |

ARMS's process system CPU time was 140.22 seconds versus 66.62 for the model, with 46.16 versus 21.36 seconds of migration-syscall wall time summed across workers. These measures overlap work and cannot be treated as elapsed-time components. They nevertheless provide a concrete execution difference alongside nearly equal loading time. Global compaction failures were 67,343 versus 27,255. Linux 6.18 can report successful migration after splitting a huge page and migrating its smaller pages, so zero syscall returns do not contradict this kernel work.

The suspected page-status indexing bug was observed: 46 successful statuses in ARMS and 117 in the model would be processed against a different original page record. Correcting the mapping eliminated observed mismatches but did not reduce the ARMS gap or its splitting/compaction activity. The separate positive-return/stale-ENOMEM path was not observed in any of these runs.

The diagnostics add measurement overhead inside the adaptive migration-cost timer, and there is one execution per case. Their timings should not replace the earlier uninstrumented reproduction results. The counters support a present migration/compaction mechanism; they do not establish the missing historical change. Exact syscall semantics, instrumentation limits, raw logs, build hashes, VM deltas, correction patch, and validation are in [the runtime findings](migration_probe/runtime_findings.md).

## What can and cannot be attributed

- **The old ARMS source lineage remains early May `8d6a8bc66` with local C220G5 settings.** Reconstructing it has not reproduced the original 115.804-second mean. No recovered intervening ARMS source change has explained the transition.
- **HDD versus SSD affects the loading total, but has not recovered the original relative gap in controlled shared-device tests.** The new SSD pair differs mainly in computation. This does not retroactively identify May's graph device or graph hash.
- **The June model change is real but insufficient.** It affects model comparability, cannot directly speed normal ARMS, and was restored before another fast June cohort.
- **Page migration and huge-page handling are the strongest measured conditions to explain next.** ARMS encounters substantially more splitting/compaction than the model under identical preparation. A stable difference in boot-level memory layout or effective kernel controls could influence the two policies differently while preserving consistent alternating results. No dated historical measurement currently demonstrates that such a condition changed.

The evidence does not justify applying a speculative live-source reversion or claiming the original results are reproduced. Host controls were restored and independently read back; no diagnostic benchmark remains running. Exact historical executable hashes and effective memory/migration observations are missing from the old timing logs, so the specific May-to-June trigger remains unresolved.
