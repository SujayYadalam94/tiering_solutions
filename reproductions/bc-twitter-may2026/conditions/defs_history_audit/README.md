# Historical defs.h audit for the early-May performance gap

September 9, 2026. Scope excludes today's edits and performance-fitting alternatives. No libraries were rebuilt, no benchmark ran, and no host setting changed.

**The retained header history does not identify an ARMS setting change that explains why the slow May runs became faster. A genuine post-May-7 model smoothing change exists, but the existing historical reconstruction already restores it.**

This audit re-examined nine Git states and 41 retained editor saves from April 26 through May 9. The headers were preprocessed for normal C220G5 ARMS, using `USE_MODEL=false`, `PRINT_TRAINING_DATA=false`, and `LOGGING_RUN=false`. Three incomplete April 26 editor saves fail preprocessing and are explicitly marked; the remaining 47 inputs pass. Preprocessing resolves conditional definitions but does not establish whether every defined macro is used by the running policy. Relevant call sites were checked separately. [Extracted macros, timestamps, source hashes and diagnostics](macro_history.json).

## Changes that look suspicious in Git, with their actual dating

| Definition | Git change | Earlier local-source evidence | Relevance to the slow May runs |
| --- | --- | --- | --- |
| `BACKOFF_PERIOD` | May 2 `058f63258`: `4` → `0` for ARMS | April 26 20:03:47 UTC `bney.h` already has `0`; subsequent inspected valid saves retain it | No evidence for backoff `4` in May 4/7. It would require an unrecorded reversion or older executable. |
| `PERF_PAGES` | Same commit: ARMS `1+(1<<10)` → `1+(1<<11)` | April 26 `bney.h` already has `1+(1<<11)`; nearest pre-sweep May 4 `iTBv.h` agrees | Historical reconstruction correctly uses `1+(1<<11)`. The change was not first introduced after the slow May runs. |
| `ARMS_VERBOSE` | Same commit: `true` → `false` | April 27 23:44:46 UTC save enables it; April 29 01:41:50 UTC disables it; subsequent May saves retain `false` | Extra diagnostics are a real overhead mechanism, but no retained May source supports enabling them for the target runs. |
| ARMS migration-cost multiplier | May 2 and May 4 changes split/rename `CB_MULTIPLIER`, promotion/demotion multipliers and finally `BASE_MIGRATION_COST_MULTIPLIER` | Normal ARMS remains numerically `1.5` through all these names | This is not a recovered ARMS cost-threshold change. The substantive scaling changes apply to the model branch. |

The nearest pre-sweep header is [May 4 03:21:01 UTC `iTBv.h`](../editor_history/defs.h/iTBv.h); the earlier supporting files are [April 26 `bney.h`](../editor_history/defs.h/bney.h) and [April 29 `6rn9.h`](../editor_history/defs.h/6rn9.h). Their recorded save times come from [VS Code entries](../editor_history/defs.h/entries.json), rather than copied-file modification times. The [checkout/result chronology](../arms_version_provenance/README.md) places May 4 slow ARMS under `6b1bc11dd` and May 7 under `8d6a8bc66`. Source identity still does not prove an original executable hash.

## Supported ARMS configuration for May 4/7

| Control | Supported value |
| --- | --- |
| Backoff / verbose | `0` / `false` |
| Perf pages per read/write ring | `1 + (1 << 11)` |
| Migration worker count | `10` |
| Default / high-fidelity sample periods | `10007` / `5003` |
| Large / small policy intervals | `500000` / `100000` microseconds |
| Base migration-cost multiplier / decay | `1.5` / `1.5` |
| Migration metadata cost | `500` microseconds |
| Pagemap full-scan intervals / recent window / batch | `4` / `1` / `128` |
| Partial-rank multiplier | `8` |

The platform latency/bandwidth constants, ARMS hotness weights and detector thresholds also show no relevant early-May-to-June header delta. A complete freshly rebuilt May 2 source variant, which carries the supported ARMS values above, was already tested: **104.49 seconds total**, versus **102.61** for the May 4 reconstruction and **115.804** archived May 7 mean. The separate float-arithmetic reversion was **103.57**. These are single-pass comparisons with limited precision for small differences; neither recovered the full gap. [Completed reversion results](../arms_reversions/README.md).

## The actual model-side header changes

The May 7 cost-ablation setup selects adjusted moving-average scoring (`MODEL_SCORE_HISTORY_SUMMARY=2`), history length `10`, and the cost sweep including `0.5`. Its nearest header and checkout use **`MODEL_SCORE_MOVING_AVERAGE_ALPHA_IDX=3`**, selecting `2/(100+1)`, approximately `0.019802`. The later value `2` selects `2/(20+1)`, approximately `0.095238`, which gives newer scores more weight.

The editor history records index `2` briefly on May 3 at 17:39 UTC, then back to `3` at 19:21 UTC, before the May 4/7 reference runs. On May 9 it records a temporary `1` at 22:16 UTC and `2` at 23:29 UTC. Commit `41e8adedd`, May 10 04:39:59 UTC (May 9 local time), records both model-score and virtual-duration alpha indices changing from `3` to `2`. The latter duration index did not exist in the early May 4 source, but is `3` in the May 7 checkout.

This is a defensible historical setting to preserve for the May 7 model, and **our existing reconstruction already does**: both indices print `3`, and history prints `adjusted_moving_average` with length `10`. See the [first full-setup model run](../sessions/20260909T130001Z-142220/results/20260909T130001Z-142220/01-may07_model/stdout.log:9) and [latest cost-model reconstruction](../sessions/20260909T192149Z-500236/results/20260909T192149Z-500236/01-may07_model/stdout.log:9). Thus it does not explain the gap still missing in those reconstructed comparisons.

`VIRTUAL_STEP_SAMPLES` changed from `3162` to `10000` by the May 4 02:53 UTC editor save, before the nearest pre-sweep header. Virtual-step timing constants were added in the May 5 UTC commit sequence. These affect the model/virtual-feature path; normal ARMS uses its unchanged real-time policy intervals and fixed cost multiplier. The [May source](../../source/may04/arms_kernel.cpp:262) separates these multiplier branches, and [normal ARMS skips model-score updating](../../source/may04/arms_kernel.cpp:1511).

## The stronger chronological constraint

The full `defs.h` Git blob is **identical** between the slow May 14 long-run checkout `4da3d6c3f` and the faster June long-run checkout `d6f720be6`: `83bfc8ceb970596b5489b15e586892fcf969fd42`. There is no intervening committed header setting to revert for that observed narrowing of the gap. Unrecorded edits or an unidentified executable remain possible, but are not established by this audit and should not replace the user's clean-build evidence with a stale-build assumption.

The historically supported reconstruction remains the frozen May ARMS settings plus the May model smoothing/configuration. This audit supplies no new ARMS `defs.h` reversion with positive evidence for the target cohort.
