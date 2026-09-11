# Which ARMS version produced the slow original runs?

Audited September 9, 2026. Original `times/c220g5` file modification times are used for the execution chronology, following the user's provenance information. Files under `c220g5_final` are matched by SHA-256 content, not dated by their copy timestamps. No benchmarks or host changes were performed for this audit.

The [subsequent root-cause investigation](root_cause_followup.md) adds a previously uncommitted June model-feature change, comparisons at 6 GB and across workloads, and runtime/toolchain checks. It qualifies the long-run model comparison without changing the best-supported May ARMS source lineage below.

## Best-supported identification

The slow May 7 BC Twitter/cost-ablation ARMS runs belong to the **`8d6a8bc66` checkout, with contemporaneous local C220G5 build settings**, and the earlier slow May 4 runs belong to the **`6b1bc11dd` checkout**. These are the best-supported source lineages. A checkout is not an executable manifest: uncommitted edits and a previously built library can differ from HEAD. No original ARMS binary hash or startup log was recovered to prove exact executable identity.

The local [HEAD reflog](head_reflog_timeline.json) provides actual checkout history, rather than merely the timestamps attached to commits:

| Local HEAD interval, UTC | Checkout | Relevant original ARMS runs |
|---|---|---|
| May 4 03:01:18 → May 5 01:40:38 | `6b1bc11dd` | May 4, 4008 MiB label: 121.201, 114.783, 115.013 s; mean **116.999 s** |
| May 5 01:41:13 → May 10 04:39:59 | `8d6a8bc66` | May 7, 4009 label: 112.781, 118.355, 116.275 s; mean **115.804 s** |
| July 29 02:51:30 → September 3 13:53:26 | `2caec5ede` | August 27, 4053 label: 134.192, 128.651, 126.669 s; mean **129.837 s** |
| September 3 13:53:26 onward | `671e0e644` | September 8–9, 4077 label: 102.475, 102.645, 104.011 s; mean **103.044 s** |

The brief May 5 intermediate checkout `4494a8ebb` is omitted from the table because it contains none of these selected runs. All intervals are normalized to UTC; the repository's "May 4" commit `8d6a8bc66` was committed May 4 at 20:41 in UTC−5, i.e. May 5 at 01:41 UTC. The [original results joined to checkout history](arms_runs_joined_to_checkout.json) retain full hashes and timestamps.

## Final archive copies do not date the measurements

| Original `arms-6.18_new/bc-twitter.sg` result | Original modification time, UTC | Total | Matching final copy's modification time, UTC |
|---|---|---:|---|
| `4009MiB_run1.time` | May 7 09:07:49.182845 | 112.781 s | May 11 05:34:04.787807 |
| `4009MiB_run2.time` | May 7 12:54:13.790302 | 118.355 s | May 11 05:34:04.788807 |
| `4009MiB_run3.time` | May 7 16:40:42.857317 | 116.275 s | May 11 05:34:04.788807 |

All three final files match the corresponding original bytes exactly. Thus they are the May 7 measurements, not new measurements made with May 11 source. [Verified inventory and copy hashes](verified_bc_twitter_original_timeline.json).

The 4009-label model cost-ablation files are interleaved with these original ARMS files on May 7. Their mean totals are 113.752, 107.968, 100.127, 100.552, and 100.495 seconds at scalers 0.125, 0.25, 0.5, 1.0, and 1.5. The [earlier storage investigation](../../../../docs/arms_model_storage_investigation.md) and its same-boot timing analysis remain consistent with the freshly checked originals.

## Local saves narrow the effective version

- The [Makefile saved May 7 at 04:10:44 UTC](../editor_history/Makefile/4HiW) selects C220G5, normal `-O3 -DNDEBUG` optimization, and ARMS `USE_MODEL=false`. It predates that boot's reference runs. This resolves the discrepancy with the Git Makefile's GSL_OPTANE default: local settings selected C220G5.
- The [last pre-sweep defs save](../editor_history/defs.h/iTBv.h), May 4 at 03:21:01 UTC, has ARMS perf ring `1 + (1 << 11)`, backoff 0, verbose false, ten workers, sample periods 10007/5003, and policy intervals 500/100 ms. Later May saves retain these ARMS constants.
- Retained `arms_kernel.cpp` history first shows the additional cold-page `can_demote` gate on **May 9 at 21:58:05 UTC**. Earlier May 9 saves still check only `in_dram`. The first retained initialization of `can_demote` is **May 11 at 02:14:26 UTC**. Both are later than the May 7 slow runs. The [editor transitions](editor_policy_transitions.json) contain snapshot identities, save times, and hashes.
- `6b1bc11dd` and `8d6a8bc66` both contain double migration cost/benefit variables and the original demotion selection without the extra flag. Their intervening changes chiefly add virtual-step timing/cost machinery for the model, not normal ARMS scoring. The two slow batches therefore bracket that model-path addition without revealing a corresponding ARMS policy change.
- The May shell history contains repeated clean builds, consistent with the user's workflow. It lacks command timestamps and successful-build logs, so it strengthens the source-lineage interpretation without proving the exact executable.

Accordingly, the old slow ARMS should be reconstructed as the **early-May, pre-`can_demote` C220G5 variant**, with the settings above. The evidence does not favor the later May 9 uninitialized-flag version, July verbose version, or today's larger rings as the implementation behind the May 7 archive.

## Why the timing gap cannot yet be assigned to one source change

There are no surviving standard BC Twitter 4 GB ARMS results in the freshly checked inventory between May 7 and August 27. The approximately 98-second runs on May 7–9 have 6009, 8009, or 10009 MiB labels and occur under the same `8d6a8bc66` checkout. They are not evidence of a sudden 4 GB code speedup. This is a comparison across boot/capacity settings, not a claim that memory fluctuated randomly within one alternating sweep.

August 27 is a separate slow episode under the July checkout; the model forest/configuration and storage conditions differ or are unproven. September's first approximately 103-second original 4 GB results use the September checkout. That sparse chronology does not uniquely assign the original May-to-September change to the July/September verbose toggle or any other commit.

### A separate long-workload series narrows the transition to June

The original 100-iteration `bc-twitter.sg-long` files provide a bridge absent from the standard 10-iteration series:

| Original execution dates, UTC | Size label | ARMS mean | Model mean | ARMS minus model |
|---|---|---:|---:|---:|
| May 14 | 4035 | 808.557 s | 744.401 s | 64.156 s |
| June 13–14 | 4040 | 625.315 s | 614.238 s | 11.077 s |
| June 22–23 | 4041 | 628.544 s | 619.911 s | 8.633 s |

Each row contains three results per policy, and each row's original result timestamps place ARMS and the model in the same 6.18 boot. All use the named model/configuration `model_discounted_reward_99_bc-twitter.sg_l2-1_10_0.1`; identical historical weights are not established.

The May 14 checkout was `4da3d6c3f`; the June checkout was `d6f720be6`. Seventeen checked ARMS/build/setup/helper/workload Git blobs are identical across those revisions. More strongly, recovered contemporaneous `run_all_measurements_long.sh` editor saves differ **only in `SIZES=(4035)` / `(4040)` / `(4041)`**. Each runs model → setup → normal ARMS, with the same 16-thread, 100-iteration graph command. The short-lived 50-iteration editor setting was restored to 100 before the surviving May 12 runs. These are separate long-workload totals and must not be mixed with the standard archive's totals.

This dates a substantial narrowing of the performance gap to June, before the later July/September source changes. It weakens an explanation relying solely on those later committed ARMS changes, without proving the effective libraries, model weights, graph device, or host controls were identical. The [long-workload audit](long_variant_history.md) preserves exact runner snapshots, pairwise diffs, original timings, boot associations, and checked source hashes.

The [runtime-log audit](runtime_log_provenance.md) searched original timing/log files, collected logs, recovered underlay logs, shell history, and retained later launch records. It found no May ARMS startup output. The historical wrapper sent that output to the terminal; `.time` captured timing only. Later auth logs establish the normal `libraries/C220G5/libhemem-arms.so` path but no binary hashes.

The existing May 4 reproduction is already built from `8d6a8bc66` with C220G5 and the matching ARMS settings. Its latest run was **102.61 s**; the freshly rebuilt May 2 candidate was **104.49 s**. The date audit strengthens the relevance of those reconstructions but does not explain why the original May conditions made them slower. [Completed reversion results](../arms_reversions/README.md#completed-runtime-comparison).
