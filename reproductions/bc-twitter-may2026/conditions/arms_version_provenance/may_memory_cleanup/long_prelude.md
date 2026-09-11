# Actual prelude at the May-to-June long-workload transition

Read-only follow-up, September 9, 2026. **The workload selection really changed: slow May 14 BC ran by itself; fast June BC followed four Kron executions inside a mixed sweep.** This is a dated experimental-condition difference, despite identical preparation helpers and outer runner logic. It supplies a specific preconditioning comparison, but does not establish that preconditioning caused the speed change.

All 105 original timing files in the three relevant boots were rechecked against their previously recorded SHA-256 and nanosecond mtime. Every check matched. [Machine evidence](long_prelude_evidence.json) includes all rows in chronological order, preceding filenames, inferred starts/gaps, original paths, and the exact saved workload-selection source/diff. Final-copy timestamps are not used.

## Positive evidence: a different workload list

The May 14 **06:16:28.572 UTC** save of `measurement_workloads_long.sh`, `q6Tj.sh`, enables only `bc-twitter.sg-long`. Its first six surviving timing files in the boot are:

`BC Twitter model → BC Twitter ARMS`, repeated three times without intervening recorded workloads.

The June 13 **22:27:19.338 UTC** save, `xqTW.sh`, enables this order:

`BC Kron → PR Kron → BC Twitter → PR Twitter → FAISS → XSBench → MG → DuckDB TPCH`.

Each workload executes model then ARMS. Original mtimes independently reproduce that exact eight-workload order for all three repetitions in both June boots. Thus the June prefix immediately before the first BC pair was:

`BC Kron model → BC Kron ARMS → PR Kron model → PR Kron ARMS → BC Twitter model → BC Twitter ARMS`.

Each arrow has normal preparation between the executions. Kron uses **50 trials**, Twitter **100 trials**, and all four graph definitions use 16 OpenMP threads. Model labels are the workload-specific `model_discounted_reward_99_*_l2-1_10_0.1`; a matching label is not proof of identical historical weights.

Sources: [May workload list](../long_workload_editor_history/1e202976/q6Tj.sh), [June workload list](../long_workload_editor_history/1e202976/xqTW.sh), [unchanged outer-runner comparison](../long_variant_history.md), [saved workload commands](../root_cause_timing/all_long_workload_git_commands.json). A June 19 21:14 save, `3Nlh.sh`, also restores the same eight-workload list after a brief FAISS-only selection; the June 22 timing order confirms the full list was used there.

## Exact timing and boot context

| Observation | May 14, 4035 | June 13–14, 4040 | June 22–23, 4041 |
|---|---:|---:|---:|
| Mean ARMS BC total | 808.557 s | 625.315 s | 628.544 s |
| Mean model BC total | 744.401 s | 614.238 s | 619.911 s |
| Retained executions before first BC model | 0 | 4 Kron | 4 Kron |
| Total elapsed in those four Kron commands | — | 4,420.060 s | 4,351.724 s |
| First BC model starts after boot | 14m 36.606s | 1h 37m 28.499s | 14h 29m 16.364s |
| Model→ARMS gap, repetition 1 | 44.800 s | 64.560 s | 45.594 s |
| Model→ARMS gap, repetition 2 | 44.803 s | 69.338 s | 45.424 s |
| Model→ARMS gap, repetition 3 | 44.960 s | 91.984 s | 45.734 s |

Times between commands are inferred as `(next file mtime − next real time) − previous file mtime`. They include preparation and wrapper overhead; they are not direct defrag timings. June's first model BC follows ARMS PR Kron, with gaps 78.603/63.888/56.475 seconds in the 4040 boot and 46.269/46.070/46.218 in the 4041 boot.

The June 22 boot has an approximately 13-hour interval before its first retained Kron execution. No timing files explain what happened during that interval. Conversely, “zero retained executions” on May 14 is not proof that no unrecorded process or overwritten run executed. The fresh BC-only editor save plus uninterrupted timing sequence nevertheless provides affirmative evidence that this batch selected only BC.

## What this narrows

1. **A Kron prelude is an actual historical condition to compare**, with precise workload/policy order and unchanged full setup before every execution. Setup and defrag do not certify a complete physical allocator/kernel-state reset. Whether this prefix improves or worsens ARMS must be measured; neither direction follows from the timestamps.
2. **Longer June preparation is not sufficient by itself.** June 22 is comparably fast with approximately 45-second gaps, essentially May's duration. More time spent preparing cannot by itself explain both fast June cohorts.
3. **Mixed-workload execution is not a universal explanation of old slowness.** The slow May 7 standard cost-ablation cohort also ran inside a mixed sweep, whereas this slower May 14 long cohort was isolated. Different preconditioning sequences remain possible; “a full sweep makes ARMS slow” does not fit both records.
4. **Policy preparation order did not change here.** Every May 14 and June BC ARMS timing immediately follows its corresponding BC model timing and full setup. The changed workload list is outside the unchanged setup/ARMS/model helper bodies. No retained policy-specific VM override, daemon operation, or cleanup exit supplies a stronger dated explanation for this transition.

The closest bounded historical prelude to reproduce is therefore the four June Kron executions followed by the BC model→ARMS pair, using historical setup between each. Replaying the complete sweep is only needed to study later repetitions. This report prepared no runner, launched no workload, and changed no host setting.
