# Original BC Twitter timing provenance

Fresh read-only inventory on September 9, 2026. `verified_bc_twitter_original_timeline.{json,csv}` records original `times/c220g5` file modification times, full SHA-256 hashes, parsed shell `real` times, exit status, and any byte-identical `times/c220g5_final` copies. Only the original modification times are used to date executions. Final-copy dates are included solely to show why they must not be used as execution dates. The inventory is limited to the standard `bc-twitter.sg` directory and ARMS/model result groups; the `-long` and general-model wrapper workloads are separately noted below.

## Main 4 GB chronology

| Original result group and size label | Original completion dates, UTC | Successful totals (s) | Mean (s) |
|---|---|---|---:|
| `arms-6.18_new`, 4008 | May 4, 06:27:30–14:00:57 | 121.201, 114.783, 115.013 | 116.999 |
| `arms-6.18_new`, 4009 | May 7, 09:07:49–16:40:42 | 112.781, 118.355, 116.275 | 115.804 |
| `arms`, 4053 | August 27, 08:07:42–09:42:32 | 134.192, 128.651, 126.669 | 129.837 |
| `arms`, 4077 | September 8, 23:58:38–September 9, 10:11:10 | 102.475, 102.645, 104.011 | 103.044 |
| `arms`, 4078 | September 9, 15:35:23 | 100.486 | 100.486 |

No surviving standard-workload 4 GB ARMS `.time` files bridge May 7 and August 27. These totals alone cannot separate loading from computation or establish a source revision. The current 4078 file has been overwritten by a later execution since previous investigation snapshots, so its current contents supersede older values for that filename.

## Final-copy correction

All three `times/c220g5_final/arms_6.18/bc-twitter.sg/4009MiB_run{1,2,3}.time` files match their respective `times/c220g5/arms-6.18_new/bc-twitter.sg/` originals byte for byte. The originals completed May 7, while the final copies were modified May 11 at 05:34:04 UTC. Thus the archived slow ARMS series should be associated with May 7 source/build evidence, not May 11.

The fifteen 4009 cost-ablation model files likewise match final copies under `model_6.18_cost_ablation`. For example, the 0.5/run1 original completed May 7 at 09:15:23 UTC, but its final copy is dated May 11 at 05:17:18 UTC.

## Contemporary model and memory controls

The May 4 4008 workload-specific model cost-0.5 series averages 101.941 seconds, versus ARMS 116.999. The May 7 4009 cost sweep averages:

| Model cost | Mean total (s) |
|---|---:|
| 0.125 | 113.752 |
| 0.25 | 107.968 |
| 0.5 | 100.127 |
| 1.0 | 100.552 |
| 1.5 | 100.495 |

Within every May 7 repetition, ARMS is followed by the model costs in ascending order. Inferred preparation gaps (next file mtime minus its runtime minus preceding file mtime) are 43.518, 43.850, and 43.677 seconds from ARMS to cost 0.125. All fifteen transitions are between 43.200 and 44.009 seconds. This supports a real contemporaneous sweep, although timestamps do not record the success of individual preparation operations.

The nearby higher-memory ARMS series were already substantially faster: 6009 averages 98.704 seconds on May 7–8, 8009 averages 98.174 on May 8, and 10009 averages 97.419 on May 9. Their twelve final ARMS copies, including the three 4009 files, all match their originals exactly.

On August 27, the 4053 ARMS runs alternated with `model/wl_bc-twitter.sg_model_all` runs (128.381, 124.881, 125.991 seconds, mean 126.418). Those use the general `all` model filename, so they are not a repeat of the May workload-specific cost study.

## Separate long-workload records

Original 4 GB `arms/bc-twitter.sg-long` files exist on May 12 (4032: 705.438, 715.936, 711.914 seconds), May 14 (4035: 794.368, 819.386, 811.916), June 14 (4040: 621.264, 623.501, 631.179), and June 22–23 (4041: 624.103, 626.036, 635.493). All report success. These are a separate workload and should only be used to infer a transition after verifying its historical command and inputs.

There are June/July standard BC Twitter records with labels 100050, 100100, and 100101; those are not 4 GB labels and are excluded from the 4 GB chronology.

### Long-workload follow-up verification

`verified_bc_twitter_long_timeline.{json,csv}` contains a separate fresh inventory of the four long-workload cohorts and their model companions. The relevant saved editor files are preserved under `long_workload_editor_history/`, with exact save timestamps and hashes in its manifest.

| Size label / original dates | ARMS mean (s) | Model mean (s) | ARMS minus model (s) | Latest local HEAD from reflog |
|---|---:|---:|---:|---|
| 4032 / May 12 | 711.096 | 632.746 | 78.350 | `41e8adedd` |
| 4035 / May 14 | 808.557 | 744.401 | 64.156 | `4da3d6c3f` |
| 4040 / June 13–14 | 625.315 | 614.238 | 11.077 | `d6f720be6` |
| 4041 / June 22–23 | 628.544 | 619.911 | 8.633 | `d6f720be6` |

All four cohorts fall in 6.18 boots according to original mtimes joined to the saved wtmp boot ledger. Each cohort's ARMS and model results fall in the same boot. HEAD is a repository state, not proof of the loaded shared object's contents.

The long workload editor history records `-n 100` on May 11, briefly `-n 50` on May 12 at 18:19 and 18:41 UTC, then restores `-n 100` at May 12 19:41:39 UTC (`5f7fe257/TSz8.sh`), before any surviving 4032 ARMS completion. That last save uses:

```sh
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/bc -n 100 -f ${BENCH_ROOT}/gapbs/benchmark/graphs/twitter.sg"
WORKLOAD_VIRTUAL_STEP_SAMPLES="10000"
```

It matches the committed May 14 workload definition and remains byte-identical at May 30 HEAD, which persisted through both June cohorts. There is no recorded intervening thread-count, iteration-count, or graph-path change. This is not a historical hash or mount record for the graph itself.

The May 14 to May 30 Git comparison has no changes to the ARMS implementation, Makefile, setup/defrag helpers, common measurement helper, individual ARMS runner, or long BC workload definition. Git's standard `run_all_measurements.sh` does change to CXL-only, so that file alone would not reconstruct June launches. However, contemporaneous **long runner** editor saves resolve the runner selection much more closely:

- `-1356624/I8Fr.sh`, May 14 07:44:28 UTC, `SIZES=(4035)`.
- `-1356624/07D5.sh`, June 13 22:27:44 UTC, `SIZES=(4040)`.
- `-1356624/XMVM.sh`, June 22 03:52:27 UTC, `SIZES=(4041)`.

These three complete scripts differ **only in the size label**. They all run the model, then `run_measurement_setup`, then the standard ARMS library with an explicit empty suffix. The actual result timestamps agree with model-then-ARMS order. Inferred model-to-ARMS preparation gaps are approximately 44.8–45.0 seconds on May 14, 64.6–92.0 seconds on June 14, and 45.4–45.7 seconds on June 22–23. They therefore provide no indication of skipped preparation. May 12 run1's ARMS file was completed much later than its model companion, so that particular same-number pair should not be described as consecutive.

This identifies a second, earlier narrowing of the ARMS/model gap by June under an unchanged **committed** ARMS implementation, with stable saved workload and per-run preparation logic. It does not establish the effective binary, graph contents/backing device, or every host setting, and does not by itself identify a cause.
