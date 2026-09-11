# Isolated ARMS reversion candidates

Prepared September 9, 2026. [Findings and ranking](../../../../docs/arms_reversion_candidates.md).

No live source or normal benchmark library was changed. [manifest.json](manifest.json) records preparation-time source/library hashes, compiler identity, and six successful compiler syntax checks across ARMS/model variants. Build logs are under `objects/`. The subsequent requested runtime comparison is recorded below.

## Completed runtime comparison

The first pass completed September 9 on the same 4 GB 6.18 boot, SSD graph, and historical preparation before each case. The user stopped the requested reverse-order pass; its partially started May 2 repetition is excluded. All three first-pass runs completed ten trials with successful exit and no detected benchmark overlap.

| Case | Total seconds | Read seconds | Average iteration seconds |
|---|---:|---:|---:|
| May 4 baseline (`may07_arms`) | 102.61 | 39.63292 | 6.25989 |
| May 4 with float arithmetic | 103.57 | 39.71666 | 6.34364 |
| Complete May 2 source | 104.49 | 39.58183 | 6.45246 |

The float reversion adds 0.96 seconds total and 0.8375 seconds over ten computation trials in this pass. The full May 2 build adds 1.88 seconds total and 1.9257 seconds of computation. Neither reproduces archived May ARMS's 115.804-second mean. Single executions do not establish a reliable small effect, but the tests do not support either reversion explaining the full discrepancy. Read times are within 0.135 seconds.

[Runtime summary](runtime_summary.json), [CSV](runtime_summary.csv), and [raw session](../sessions/20260909T153630Z-322048/results/20260909T153630Z-322048/results.json) preserve the results. [Stop verification](../sessions/20260909T153630Z-322048/stop_verification.json) confirms no remaining BC/reversion processes, unchanged boot, PMEM still bound, and zero host-control restoration errors. The [cancellation record](../sessions/20260909T153630Z-322048/cancellation.json) marks the second-pass attempt invalid.

The runner manifest inherits an older limitation describing graph-only cache eviction. That text is superseded for this session by `condition_preset=may04_full` and the per-case preparation logs: the actual runs used historical setup and two defrag invocations.

## Reviewable patches

- [01_restore_may_arms_ring.patch](patches/01_restore_may_arms_ring.patch): the audited live ARMS branch `1 << 12` to historical `1 << 11`; model branch unchanged.
- [02_restore_may_arms_demotion.patch](patches/02_restore_may_arms_demotion.patch): normal ARMS stops reading the extra `can_demote` flag, matching pre-May-9 selection. The model retains its check. This is different from commenting out initialization, which leaves the newer selection reading an unset field.
- [03_restore_early_may_float.patch](patches/03_restore_early_may_float.patch): ARMS cost/benefit variables return to float; model variables remain double.

Each patch was generated against `source/live_at_audit`, with source hashes in the manifest. The first patch expects `1 << 12`; it will need adjustment if the user changes the live ring again. They were checked independently, and their copied source trees compile for both normal ARMS and the historical model configuration. No patch has been applied to the main checkout.

For a comparison using the latest live source, first establish a deterministic baseline: use the historical ring size and coherent ARMS demotion eligibility. Do not compare a float variant against a baseline whose `can_demote` initializer is commented out while the field remains in use.

## Ready historical libraries

| Case | Contents |
|---|---|
| `may07_arms` | Existing fully rebuilt May 4 reference; already benchmarked earlier |
| `may04_float_arms` | Same frozen May 4 source, changing only ARMS cost/benefit variables to float |
| `may02_arms` | Fresh build of commit `058f63258`, C220G5, normal ARMS |

Both new libraries use `-O3 -DNDEBUG`, normal ARMS flags, backoff 0, verbose false, and `1 << 11` rings. The May 2 library is a coherent earlier source candidate; it is not claimed to be the exact archived binary. It avoids using the retained April artifact that aborted on startup.

The existing condition runner can compare them using [runner_manifest.json](runner_manifest.json). This command actually runs two benchmarks and historical host preparation; omit `--run` to check the manifest/host only:

```bash
sudo python3 reproductions/bc-twitter-may2026/conditions/run_conditions.py \
  --preset may04_full \
  --manifest reproductions/bc-twitter-may2026/conditions/arms_reversions/runner_manifest.json \
  --graph /users/zimooo2/tiering_solutions/data/graphs/twitter.sg \
  --run --cases may07_arms may04_float_arms
```

The runner supports additional case names and repetitions. The completed comparison above was stopped after one pass at the user's request; no additional runs are pending. Preparation and restoration behavior is documented in the [parent README](../README.md).

`prepare.py` records a new live snapshot and rebuilds the isolated package. It expects the audited live edits, including the `1 << 12` ARMS ring, and deliberately fails if those expected source patterns change.
