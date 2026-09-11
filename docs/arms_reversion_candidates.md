# ARMS changes worth isolating: early May through September

Audited September 9, 2026, after the user's perf-ring experiments. This supplements the earlier [source audit](arms_source_history_audit.md) and its [completed runtime comparisons](bc_twitter_reproduction_diagnosis.md). Scope is normal C220G5 ARMS (`USE_MODEL=false`, logging/training/near-memory tracing disabled), not every training variant sharing the source tree.

The audit found a short list of concrete reversions, but no newly established cause of the archived slowdown. The existing reconstructed May 4 runs already remove the later policy changes and still finish around 103 seconds rather than the archived 116 seconds. A newly prepared May 2 build extends the comparison earlier than those completed source reproductions.

**Runtime follow-up:** the user subsequently requested these candidates be run. One SSD pass completed: May 4 baseline **102.61 s**, float reversion **103.57 s**, full May 2 **104.49 s**, with read times all approximately 39.6–39.7 s. The user stopped further repetitions. Neither candidate recovered the archived slowdown; small differences are not established precisely by one run each. Host controls restored with zero errors. [Completed results and raw evidence](../reproductions/bc-twitter-may2026/conditions/arms_reversions/README.md#completed-runtime-comparison). Statements below about untested candidates describe the audit before this follow-up.

## Candidates and exact reversions

| Change | Exact experiment | Historical relevance and limits |
|---|---|---|
| Today's ARMS perf ring is `1 + (1 << 12)` at the audit snapshot, after a temporary `1 << 14` edit | Restore the ARMS branch to `1 + (1 << 11)` | Definite difference from the May saved source and the earlier September 9 diagnostic builds. On this host this restores approximately 320 MiB total ring capacity, from 640 MiB (`12`) or 2560 MiB (`14`). Necessary for a comparable baseline, but earlier diagnostics already used `11` and still missed the archive. |
| May 9 `41e8adedd` adds `&& cold_page->can_demote` to cold-page selection | For normal ARMS, restore the old `if (cold_page->in_dram)` condition; retain the model check | A real change to which pages ARMS can replace. May 14 `890a2449c` initialized the new flag. The latest working tree has that initializer commented out but still reads the flag. Removing initialization is not a coherent pre-May-9 reversion; the old path did not read this flag at all. The May 4 reconstruction already tests the old condition, so this is a baseline correction and isolated check, not a newly untested explanation. |
| May 3 local time / May 4 UTC `29e225954` changes migration cost/benefit variables from `float` to `double` | Restore the ARMS variables to `float`, keeping the 1.5 multiplier and all other conditions fixed | The strongest remaining untested source change from early May. The retained April 30 artifact also has the older arithmetic. Cost rounding and accumulated demotion cost can change threshold decisions. Most benefit arithmetic still has float operands, so this is not evidence of a broad precision or throughput difference. No measurements yet establish the effect size or direction. |
| July 28 `2caec5ede` switches verbose diagnostics on; September 3 `671e0e644` switches them off | Enable `ARMS_VERBOSE` in an isolated build with the matching instrumentation implementation | Could explain a July/August-to-September improvement. Saved May source and the retained April artifact have verbose off, so evidence for the May archive is weak. |
| July 28 replaces OFFCORE demand-RFO diagnostics with an IMC write-CAS view | Compare the old and new implementations with verbose explicitly enabled in both | Old verbose mode opens two additional core events on each of 20 CPUs and repeatedly reads them. Counter availability and additional work could affect sampling/runtime. Both implementations skip their optional counter work when verbose is false; simply reverting this large diff under normal May/current settings is low priority. No historical evidence shows verbose enabled during May 7. |

The demotion patch deliberately bypasses the extra flag only under `USE_MODEL=false && LOGGING_RUN=false`. It does not remove the model's rank-dependent eligibility logic. The float patch changes ARMS arithmetic while retaining the model's double variables. All patches are saved separately and have not been applied to live source.

## Changes at the beginning of May

Commit `058f63258` on May 2 records backoff 4 to 0, verbose on to off, and ARMS rings from `1 << 10` to `1 << 11`. These are reasonable sensitivity knobs, but the retained local saves already show backoff 0 and the larger ring on April 26. They must not be assumed to be changes that happened after the archived May measurements. The [stale-build history](../reproductions/bc-twitter-may2026/conditions/stale_build_history.md) explains this distinction and the user's clean-build evidence.

May 2 also changes the zero-address write filter to apply only when `apply_filters` is true. Normal ARMS has `apply_filters=false`, but the unconditional `page_va != 0` eligibility check immediately afterward still rejects address zero before page creation or sample accounting. The change therefore does not make zero-address writes count toward ARMS scores. May 4/5 adds virtual-step timing to the PEBS scanner; normal ARMS does not execute the virtual-feature path. Its ordinary sample periods remain 10007/5003.

## What the broader audit did not find

- `migration_worker.cpp`, `pagemap_scan_thread.cpp`, `groups.cpp`, and `timer.cpp` have one identical Git blob each across the selected May 2, May 3/4, May 4/5, May 9, May 14, July 28, and September 3 revisions; the current diff also leaves them unchanged. The [source timeline](../reproductions/bc-twitter-may2026/conditions/arms_reversions/source_timeline.json) records the hashes.
- There is no intervening normal-ARMS change to worker count, hotness score weights, rank-update behavior, migration-cost multiplier 1.5, policy intervals 500/100 ms, or default far-memory binding in this range.
- The model history, time-cost scaling, age clipping, and boundary-score work do not change normal ARMS scoring. A changed function signature alone does not change the ARMS branch's rank logic.
- September all-NUMA allocation changes require a dedicated training define or logging mode. Normal ARMS continues to bind application allocations to the slow tier.
- Current Parquet shutdown hooks and allocations require training mode. Normal ARMS has the optional PID/thread/instruction-range filters disabled, so added Arrow/Parquet helper prefixes do not alter its samples. This ARMS/model filtering asymmetry predates the audited May-to-September changes; it is not a newly found reversion candidate.
- The normal runner's C220G5 library path, BC `-n 10`, and `OMP_NUM_THREADS=16` remain the same. Storage selection changed and has been tested separately. Setup/defrag/perf ordering is covered by the [setup audit](bc_twitter_historical_setup_audit.md) and [knob experiments](bc_twitter_setup_knobs.md).

These checks narrow code candidates; they do not establish identical historical binaries, shared-library versions, graph bytes, or effective host state. Successful clean builds substantially weaken the ordinary stale-object hypothesis.

## Prepared artifacts and how to compare them

The isolated [reversion package](../reproductions/bc-twitter-may2026/conditions/arms_reversions/README.md) contains:

1. Three reviewable patches against a hashed snapshot of the latest working sources: historical ARMS ring size, pre-May-9 demotion selection, and early-May float arithmetic. Each is independent; none is installed in the main checkout.
2. A complete fresh C220G5 ARMS library from May 2 commit `058f63258`.
3. A May 4 library with only the ARMS cost/benefit variables reverted to float. Comparing this with the existing May 4 library isolates arithmetic from the other early-May additions.

The patches pass application checks and compiler syntax checks for both ARMS and model configurations. The two historical candidates are compiled and linked with the same GCC/optimization configuration as the earlier reproductions; neither has been benchmarked. Their hashes, source snapshots, compiler identity, and build logs are retained. No production source/library or host setup was changed by this audit.

The most informative next comparison is the already tested May 4 baseline versus May 4 with float arithmetic, followed by the full May 2 source if needed. Use the same disk, preparation, and graph, alternating order, and retain read time plus iteration times. Restoring the ring size and removing the unintended ARMS flag read should precede comparisons using the live tree. Turning on older diagnostics or backoff can test sensitivity, but reproducing a slower number alone would not prove that those settings produced the archived result.
