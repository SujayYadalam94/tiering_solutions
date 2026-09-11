# ARMS performance and migration history

September 10 follow-up: [GRUB versions and boot-history audit](../grub_interval_audit/README.md) adds protected backup inspection, boot/runtime file change times, and May/June Emulab service logs. It excludes PMEM performance results at the user's request and accounts for fresh boots between current runs. No May/June GRUB command line or causal setting change was recovered.

**The expanded history identifies additional saved source and concrete experimental differences, but no recovered change establishes why the early-May ARMS/model gap disappeared.** For the closest comparable BC Twitter long runs, the observed transition remains between May 14 and June 13–14 on 6.18. The normal ARMS worker count, relevant macro values, core migration source, and requested VM settings have no recovered persistent change in that interval. The strongest dated correlation is the execution context of the June batch: a different workload sequence and potentially different state left by preceding policy executions.[^1][^2]

There are two distinct historical questions. The older archived 6.2 ARMS timings existed by March 15 and precede substantial sampling-buffer and migration-accounting changes. The slow May 6.18 timings already incorporate those changes. A mechanism that explains the March archive cannot automatically explain the later May-to-June transition. Similarly, the long BC series is evidence of an earlier small gap, not proof that one permanent change caused every subsequent short-run result.[^3]

**Performance chronology and cross-workload constraints**

The following results are means of three successful executions per policy on nominal 4 GB 6.18 configurations. Positive difference means ARMS took longer. BC Twitter and PR Twitter both use 100 trials here; these totals include graph loading and initialization and must not be read as pure iteration times. Original result paths and timestamps, rather than final-copy dates, provide the execution chronology.[^1]

| Workload | Cohort | ARMS, seconds | Model, seconds | ARMS minus model |
|---|---|---:|---:|---:|
| BC Twitter | May 12, 4032 | 711.096 | 632.746 | +78.350 |
| BC Twitter | May 14, 4035 | 808.557 | 744.401 | +64.156 |
| BC Twitter | June 13–14, 4040 | 625.315 | 614.238 | +11.077 |
| BC Twitter | June 22–23, 4041 | 628.544 | 619.911 | +8.633 |
| PR Twitter | May 13, 4032 | 2713.627 | 2897.917 | −184.290 |
| PR Twitter | June 14, 4040 | 2587.799 | 2692.481 | −104.682 |
| PR Twitter | June 22–23, 4041 | 2559.892 | 2672.156 | −112.265 |

The new PR comparison constrains broad explanations. From May to June, BC's relative change favors ARMS, while PR's relative change favors the model. Both policies also improve in absolute time. A uniform decrease in ARMS overhead, or a uniform increase in machine speed, is therefore insufficient by itself. Workload-sensitive allocation, huge-page coverage, migration decisions, and model behavior remain compatible with this pattern.[^1]

Within the May 13 PR boot, the model times rise from 2820.302 to 2909.202 to 2964.246 seconds, while alternating ARMS stays between 2709.897 and 2719.283 seconds. The inferred preparation gaps remain about 45–48 seconds. This is evidence of policy-specific variation within a boot, but it does not identify memory state as the cause or establish that the BC cost ablation was similarly variable. BC4032 and PR4032 also belong to different boots despite sharing a size label.[^1]

The additional PR Git recovery covers 824 old/new blob references and 275 distinct timing blobs, including 28 absent from current files. None supplies an intermediate matched run between May 14 and June 13. Together with the earlier BC recovery and timing ZIP comparison, this leaves an approximately one-month observational gap rather than a known change date.[^1][^2]

**The migration-failure hypothesis and what the logs actually contain**

The mechanism has direct evidence in a later diagnostic pair: ARMS coincided with 3,998 THP migration failures/splits and 75,512 direct-compaction stalls, while the model had zero of those counters. The model nevertheless had more successful unsplit THP migrations. The appropriate hypothesis is excess expensive or redundant migration work in ARMS; fewer successful moves by the model is not established. These counters are system-wide, can count fallback work that eventually succeeds, and do not measure application stall seconds.[^4]

No corresponding ARMS failure/THP counters were recovered for the matched May/June BC timings. The expanded file inventory contains 2,088 text logs and 224 Parquet files. The Parquet schemas include page-selection counters, but no migration-failure, split, per-zone memory, or per-call-duration columns. The system journal starts in September, and the oldest rotated system/kernel logs reach August 9. The wtmp ledger preserves older boots without their detailed allocator messages.[^5]

An actual nearby historical error trace does survive: HybridTier running MG on May 11 reports 133 `move page error: 12` messages following negative syscall returns, explicitly enters its ENOMEM retry path, and reports node0 free memory around 69,984 KiB at the first such event. This is useful evidence that migration difficulty occurred on the machine. It is a different policy and workload, with no paired fast trace, so it cannot establish that ARMS's BC slowdown had the same cause. Its earlier positive-return messages also print errno, which can be stale and should not be interpreted as reliable syscall error codes.[^5]

**New saved source recovered outside ordinary Git/editor history**

The most substantive additional source recovery is a March migration-worker revision preserved in a Copilot source index. Its indexed source mtime is March 10 at 04:27:11 UTC. Four chunks differ from every recovered Git/editor version: they contain unconditional per-page failure logging with flushing, a promotion retry limit of two versus one in the closest Git version, and older residency-only accounting. This is actual saved code that would be capable of changing failure overhead or retry behavior if built and used. Its date precedes the March worker rewrite, and no loaded binary ties it to a benchmark. It is relevant to reconstructing March, not an explanation for a transition after slow May runs.[^6]

The source audit also recovered a 19-second May 5 Makefile switch from C220G5 to GSL Optane, restored at 01:42:53 UTC. Platform-specific output directories make this a weak explanation for silently changing a normal C220G5 library: depending on cleaning/build order, it would more naturally produce a different platform output or a missing library. No build or execution evidence associates it with the later target cohorts.[^6]

Three May-dated unreachable runner blobs were examined as well. They retain unresolved merge-conflict markers and fail Bash syntax validation. They contain size/model-selection alternatives, but no additional VM, THP, or disk override. They are incomplete merge artifacts, not directly executable historical launchers. Their loose-object dates record storage rather than execution.[^7]

There were no orphan files beyond the indexed VS Code History snapshots and no unreachable Git commits. The unreachable-object inventory includes 134 blobs and 397 trees. Six core-source blobs have storage dates in July or September; their contents do not supply a missing May/June revision. An additional 301 MB Parquet blob was streamed to recover its footer and hash. It contains training features without execution timestamps or failure counters, so its July storage date and contents do not establish another matched early run.[^5][^6]

**Candidate correlations, with their temporal limits**

| Candidate | Positive historical evidence | Assessment for the May-to-June transition |
|---|---|---|
| Exact preceding workload sequence | May 14 selects BC Twitter alone; June 13 enables four BC/PR-Kron executions before the first BC pair. Timing order corroborates the saved scripts. | Best dated execution-condition correlation. Direction and causal effect remain untested; May 7 was also slow in a different mixed sequence. |
| State left by the preceding model | June 13 changes the model feature adapter immediately before the first fast cohort; ARMS follows model in both May and June. | Could indirectly alter the next ARMS execution's allocation state. No historical state trace establishes this; direct ARMS code generation is unchanged. |
| Different boot/allocator state | All relevant cohorts have identified 6.18 boots, but no accepted prelaunch zone/buddy/THP snapshots. | Open possibility, not an identified changed setting. May 14 was also a recent boot, so freshness alone does not explain the transition. |
| May 9 failure-cost denominator edit | Saved worker counts cost per submitted-minus-failed pages for 20.478 seconds, then restores the old denominator. | Mechanistically relevant to migration failures, but no build/run link; too transient to attribute later cohorts. |
| May 9 demotion-cost factors | Saved normal-ARMS code uses factors 15, 5, 15, then 1 over about nine minutes. | Real uncommitted policy changes, but restored before later slow runs; weak as a sole cause. |
| Unconditional migration-count output | Present in the first retained May 9 core save, commented out at 17:39. | Could add overhead even with verbose disabled. Slow May 12/14 follows its removal, so it cannot alone explain the common transition. Introduction date is unknown. |
| March worker failure handling and logging | Indexed March 10 code plus March 14–17 Git changes document materially different retries/accounting. | Relevant to the old March 6.2 archive; already superseded before slow May. |
| March/April perf backing and event changes | Old March configuration allocates about 960 MiB of ring data versus about 320 MiB for May ARMS, with different events. | A substantial potential allocator-pressure difference for March comparisons. The full difference is not proven to come from node0, and it predates slow May. |
| Alternative reserve and compaction values | March shell/editor records include ratios 1/1/1, 32/32/16, 4/4/4 and compaction backups containing 20. | Authentic March experiments, but no recovered May/June override. Successful normal setup requests 256/256/32 and compaction 80. |
| June model age removal | June 13 removes age; June 18 restores it. | First June model implementation differs, but the small BC gap persists after restoration. The isolated normal-ARMS objects were byte-identical across this edit. |
| Verbose/build changes in July, MEMTIS in August | Locally dated verbose toggles and later installation records survive. | Too late to cause the first small June gap. |

The candidate register also distinguishes a plausible mechanism from evidence of a changed condition. A defect or expensive retry path that exists in both old and new source can amplify a changed allocator state, but its existence alone does not date or identify that state change. The recovered worker count is ten in both periods.[^2][^3][^6][^8]

**Preparation timing does not provide a single explanation**

The model-to-ARMS intervals in May 14 are approximately 44.8 seconds. In June4040 they are 64.6, 69.3, and 92.0 seconds. Those intervals include preparation and wrapper overhead and cannot be equated with defrag duration. More critically, June4041 remains fast with gaps of 45.4–45.7 seconds, almost the May duration. Longer preparation is therefore an observation for one cohort, not a sufficient explanation for both fast June cohorts.[^9]

The June4040 FAISS-long files are another real execution difference: they report successful wrapper status but last only about a quarter of a second. They were excluded from performance comparisons. They occur after the first graph pairs, so they cannot explain the first small BC gap. June4041 executes full-duration FAISS and retains the small BC gap, weakening that failed-workload prelude as a sole cause.[^1]

**Training traces expose a misleading apparent correlation**

Older generic BC training files have maximum per-page promotion-selection counts as high as 204 for Twitter and 525 for Kron; the June 6 files reach only 20. Viewed without producing scripts, this resembles reduced migration pressure. The surrounding evidence disqualifies that interpretation as a normal-ARMS change: representative May files have zero ARMS scores and populated model scores, whereas June has the reverse. The saved April training runner enables model training, and the June 6 runner explicitly selects ARMS `_near_train`, node0, with fixed 250/250 ms timing.[^5][^7]

Moreover, `num_promotions` and `num_demotions` are incremented during candidate selection before worker completion. They are neither success nor failure counters. Old raw score fields contain extreme outliers that require validation before analysis. These limitations are why the training counter drop is recorded as a rejected comparison rather than a new performance change.[^5]

**Graph and setup provenance from deleted-file metadata**

Cached filesystem catalogs retain entries for the deleted original graph directory: Twitter at 12,732,173,745 bytes and Kron at 17,966,800,881 bytes, with March 5 mtimes. Both current SSD and HDD files match those exact sizes. This constrains graph-size explanations and provides useful metadata that was previously missing. Empty historical hashes prevent establishing identical graph content or ordering; the catalogs also do not identify the underlying device.[^5][^7]

The older catalog remembers a 4,976-byte setup with an April 11 mtime and a 426-byte defrag script with a March 13 mtime. Those sizes match recovered May Git. Separately, the supplied setup from another machine matches the entire May/June setup after line-ending normalization. These are consistency checks, not proof that transient edits or failed writes never occurred. The same script also contains platform branches and environment overrides, so another machine's effective configuration needs its actual launch arguments.[^7][^10]

**Coverage and remaining evidence gaps**

This expansion adds source-index contents and metadata catalogs, unreachable Git objects, orphan-file checks, all retained Makefile/defs snapshots, 3,372 executed IPython cells from 253 sessions, and 178 external editor snapshots across 15 resources. It supplements the earlier audit of 130 editor resources, 406 saved source transitions, 98 ARMS-preprocessed snapshots, shell histories, notebooks, conversations, and reachable timing history. Counts describe overlapping evidence collections and should not be added into a count of independent observations.[^6][^7][^8]

The current logs/times inventory covers 9,863 accessible files. All 224 Parquet schemas/metadata were inspected; the approximately 308.5 GB of training payload was not read in full. Text logs were searched directly. The unrelated inaccessible 80 GB temporary directory, missing old journals, and absent historical binary/graph hashes remain explicit limits. No benchmark, reboot, or host-setting change was performed for this audit.[^5]

The main missing evidence is not another confirmed setting line. It is the original runs' effective page layout, THP migration outcomes, and loaded-artifact identities. Without those records, no temporal join can distinguish repeated destination-allocation failures from splitting for another reason, excessive selections, or different predecessor-induced state.

**Most useful next causal comparisons**

For historical replication, the most defensible comparison is the actual May BC-only sequence versus the documented June four-Kron prefix, with the same frozen ARMS/model libraries, SSD graph, and preparation. The preceding model should initially be held fixed so its effect is not mixed with workload ordering. This tests a documented condition rather than selecting reserve values to fit an elapsed time.[^9]

For mechanism diagnosis, one-worker ARMS or reduced khugepaged activity after graph loading would be informative controls, but neither is a recovered historical setting. Capture per-call duration, selected versus completed pages, repeated addresses, promotion/demotion direction, and THP/compaction changes alongside the graph read/trial split. A useful result would explain the relative ARMS/model response and distinguish the expensive kernel path, not merely reproduce one old total.[^4]

**Sources and supporting artifacts**

[^1]: [Cross-workload cohort report](cohorts/README.md), [paired original timing rows](cohorts/matched_cross_workload_pairs.json), and [temporal candidate review](cohorts/ranking.md). Original run dates span May–June 2026; the report preserves paths, hashes, statuses and boot joins.
[^2]: [Earlier change-point report](../change_point/README.md), [dated June 13 saves](../change_point/june13_specific_events.json), and [isolated compilation results](../change_point/model_compile_check/results.json).
[^3]: [March-to-August cross-kernel provenance](../cross_kernel_transition/README.md), including March archive origin and sampling-buffer source differences.
[^4]: [September BC placement diagnostic](../arms_version_provenance/may_memory_cleanup/residency_probe/runtime_findings.md) and [migration counter interpretation](../arms_version_provenance/migration_probe/kernelmechanism.md). These are later mechanism observations, not May counter records.
[^5]: [Runtime-log audit](logs/README.md), [file inventory](logs/file_inventory.json), [Parquet metadata](logs/parquet_metadata.json), [training classification](logs/training_policy_comparison.json), [May HybridTier error counts](logs/may11_hybridtier_mg_errors.json), [recovered unreachable Parquet](logs/unreachable_parquet_metadata.json), and [historical/current graph sizes](logs/graph_size_comparison.json).
[^6]: [Expanded saved-source audit](source/README.md), [normal-ARMS macro timeline](source/normal_arms_defs_timeline.json), [source-index inventory](source/index_source_inventory.json), and [unreachable-object inventory](source/git_unreachable_blobs_inventory.json).
[^7]: [Expanded setup and script audit](setup/README.md), including original editor paths, source-index versions, cached-file metadata, training launchers, IPython search and unreachable runner recovery.
[^8]: [Saved May/July source transitions](../post_may7_history/README.md) and [migration-change chronology](../arms_version_provenance/may_memory_cleanup/migration_history.md).
[^9]: [Exact long-run predecessor chronology](../arms_version_provenance/may_memory_cleanup/long_prelude.md) and [underlying timestamp/source evidence](../arms_version_provenance/may_memory_cleanup/long_prelude_evidence.json).
[^10]: [Other-machine setup comparison](../other_machine_setup/README.md), including pasted-file hash and exact Git matches.
