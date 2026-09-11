# BC Twitter long-workload historical comparison

Original `times/c220g5` mtimes place the narrowing of the long-workload ARMS/model gap by June 14, before the September speedup first visible in surviving standard-workload 4 GB files.

**Follow-up qualification:** the expanded editor audit recovered a June 13 local `model.cpp` change removing the age input, restored June 18. The checked ARMS files and runner equality below still hold, but the June 13 model is not established as the same effective model implementation. See the [further investigation](root_cause_followup.md) and [exact model adapter evidence](root_cause_code/README.md).

| Original execution dates (UTC) | Filename size label | ARMS mean (s) | Model mean (s) | Difference (s) |
|---|---:|---:|---:|---:|
| May 12 | 4032 | 711.096 | 632.746 | 78.350 |
| May 14 | 4035 | 808.557 | 744.401 | 64.156 |
| June 13–14 | 4040 | 625.315 | 614.238 | 11.077 |
| June 22–23 | 4041 | 628.544 | 619.911 | 8.633 |

Each mean contains three successful results. All four cohorts have the exact same model filename suffix, **`model_discounted_reward_99_bc-twitter.sg_l2-1_10_0.1.time`**. This identifies the named model and configuration, **not the model object's historical weights**. Each cohort's ARMS/model files falls in one 6.18 boot when original mtimes are joined to the wtmp-derived boot ledger.

## Evidence for stable workload and execution logic

The committed long BC workload is byte-identical at May 14 HEAD `4da3d6c3f` and May 30 HEAD `d6f720be6` (the latter still being HEAD through June). Its command has 16 OpenMP threads, 100 BC iterations, and the same `${BENCH_ROOT}/gapbs/benchmark/graphs/twitter.sg` path. Seventeen checked ARMS implementation, build, setup, runner-helper, and workload files have identical Git blob hashes across those two HEADs; hashes are recorded in `long_variant_history.json`.

The saved long workload file briefly changed to 50 iterations on May 12, but `5f7fe257/TSz8.sh` restored 100 at **19:41:39 UTC**, before the first surviving 4032 ARMS completion. This editor save also has virtual-step samples 10000, matching the May 14/June committed definition. No later saved command changes survive until today's graph-path override.

The exact contemporaneous long-runner files are preserved here:

| Original resource | Save date (UTC) | Preserved snapshot |
|---|---|---|
| `/users/zimooo2/tiering_solutions/run_all_measurements_long.sh` | May 14 07:44:28.249 | `long_workload_editor_history/-1356624/I8Fr.sh` |
| Same | June 13 22:27:44.262 | `long_workload_editor_history/-1356624/07D5.sh` |
| Same | June 22 03:52:27.444 | `long_workload_editor_history/-1356624/XMVM.sh` |

Their pairwise diffs, saved as `long_runner_may14_to_june14.diff` and `long_runner_june14_to_june22.diff`, change **only `SIZES=(4035)` to `(4040)` to `(4041)`**. All three run the model, invoke `run_measurement_setup`, then run ARMS with an explicit empty library suffix. The result timestamps agree with that order.

Inferred model-to-ARMS preparation gaps were 44.800/44.803/44.960 seconds in May, 64.560/69.338/91.984 seconds in the first June cohort, and 45.594/45.424/45.734 seconds in the second June cohort. These provide no indication of skipped preparation. The May 12 run1 model and ARMS files were completed hours apart, so that numbered pair is not a consecutive comparison.

Git's **standard** `run_all_measurements.sh` changed to a CXL-only experiment on May 30. It does not reconstruct these June launches; the saved **long runner** provides the more relevant execution evidence. Source equality still does not prove the effective shared object or success of every requested setup operation.

## Limits

This narrows the historical performance transition while weakening an explanation based solely on later committed ARMS source changes. It does not prove a cause: no retained per-run shared-object hashes, historical graph hashes/mount readbacks, or complete host-control readbacks accompany these timing files. Graph-path equality and identical model names cannot close those gaps. The 4035/4040/4041 numbers are run labels, not precise physical-memory measurements.

The original rows, timestamps, hashes, inferred boots, and means are in `verified_bc_twitter_long_timeline.{csv,json}` and `long_variant_history.json`. The editor manifest preserves the original VS Code resource URI, exact original history path, save timestamp, and SHA-256 for every copied snapshot.
