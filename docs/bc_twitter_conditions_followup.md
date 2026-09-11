# BC Twitter: historical conditions and HDD cost-ablation follow-up

Investigation date: September 9, 2026. This continues the [source/model reproduction](bc_twitter_reproduction_diagnosis.md) with recovered editor history, previously hidden May logs, complete historical preparation, the requested HDD cost sweep, and a reversible memory-overhead experiment.

The later [individual setup-knob tests](bc_twitter_setup_knobs.md) distinguish background compaction and khugepaged scanning from replaying unchanged settings. In particular, the full-setup replay below did not change the captured VM/THP/perf controls: they already matched its requests. It should not be read as ruling out sensitivity to individual settings.

**The original cause is still not established.** The new experiments do not reproduce the archived 15.677-second same-boot ARMS/model gap. They do establish that shared HDD storage, omitted global preparation, and a tenfold perf-cap difference are insufficient explanations under today's conditions. They also identify and measure a substantial fast-memory overhead, but removing it makes ARMS faster and shrinks the gap further.

## Historical storage and setup evidence recovered

A private, read-only view beneath today's mounted `logs` directory recovered **27 hidden logs, including 25 from May 11**. The three May 11 BC Twitter HybridTier logs report reads of **40.491, 41.039, and 43.352 seconds**. Nearby PageRank Twitter reads are also approximately 40–41 seconds. This is direct evidence of a historical loading regime resembling today's SSD, although it does not identify the original block device. These are HybridTier logs, not phase timings for the archived ARMS/model pair. Graph node and edge counts match today; no original graph hash was found. [Recovered files and timestamp evidence](../reproductions/bc-twitter-may2026/conditions/underlay_evidence/README.md).

The May 11 shell-history snapshot contains mounts for logs and other workload data but no GAPBS references. Today's history contains many GAPBS-directory mounts. Combined with the recovered read times, this strengthens the SSD interpretation without establishing an ARMS/model disk mismatch within the historical sweep.

The recovered editor history matters because Git did not contain every executed setting. A May 7 06:09 UTC saved model runner lists exactly the historical cost scalers **0.125, 0.25, 0.5, 1.0, 1.5**. A May 7 Makefile selects C220G5, optimization, and the ARMS/model flags used in the reconstructed builds. The May 10 saved model runner matches May 9 Git. These are saved-source observations, not per-run binary identities. [Historical setup audit](bc_twitter_historical_setup_audit.md), [preserved editor history](../reproductions/bc-twitter-may2026/conditions/editor_history/README.md).

The historical runner prepared every ARMS case and every individual model scaler with process-memory migration, CPU/IRQ affinity requests, slow-socket uncore programming, aggressive THP/reclaim settings, repeated global cache drops, and **two explicit compactions**. Replaying these operations was a stronger test than merely matching current sysctl values. The [condition runner](../reproductions/bc-twitter-may2026/conditions/README.md) preserves the exact setup files from `8d6a8bc66`, records effective settings, and restores host controls afterward. It omits the old unrestricted Python/Jupyter kills and requires no competing benchmark, memory-eater module, or swap.

Shell success does not mean every setup operation succeeded: the historical perf write can fail when its CPU percentage is already zero, and some process migrations/affinity requests fail. The logs retain those failures. Accepted runs below have ten trial timings, successful workload exit, and no detected overlapping benchmark.

## Historical preparation and perf cap did not restore the gap

These pairs use the same current 6.18 boot, SSD input, BC executable, reconstructed May 4 ARMS/source, and May 3 model candidate with history mode 2, length 10, discount 99, cost scaler 0.5.

| Condition | ARMS total | Model total | Gap | ARMS average iteration | Model average iteration |
|---|---:|---:|---:|---:|---:|
| Historical May 7, three-run means | 115.804 s | 100.127 s | 15.677 s | Not retained | Not retained |
| Replayed historical preparation; effective perf cap 1,000,000 | 103.63 s | 98.16 s | 5.47 s | 6.34331 s | 5.82238 s |
| Same preparation; explicit perf cap 100,000 | 104.95 s | 99.62 s | 5.33 s | 6.33678 s | 5.80449 s |

The perf-cap experiment changes only the cap after preparation using a safe write order. Neither pair prints throttle or lost-record diagnostics. The almost identical computation times do not support this cap difference as the missing explanation. Historical effective perf settings remain unknown.

Evidence: [full-preparation pair](../reproductions/bc-twitter-may2026/conditions/sessions/20260909T130001Z-142220/results/20260909T130001Z-142220/results.json), [100,000-cap pair](../reproductions/bc-twitter-may2026/conditions/sessions/20260909T132959Z-178251/results/20260909T133000Z-178251/results.json).

## Requested HDD cost ablation

All six cases ran on the HDD graph with the complete historical preparation before each case. The SSD and HDD graph hashes are identical. The model cases use the reconstructed historical time/discount cost scaling, not today's simplified policy; the scaler is not numerically equivalent to ARMS's fixed migration-cost multiplier.

| Case | Historical May 7 mean total | New HDD total | New read | New average iteration |
|---|---:|---:|---:|---:|
| ARMS | 115.804 s | 132.37 s | 68.543 s | 6.339 s |
| Model cost 0.125 | 113.752 s | 130.84 s | 68.788 s | 6.177 s |
| Model cost 0.25 | 107.968 s | 133.61 s | 73.524 s | 5.980 s |
| Model cost 0.5 | 100.127 s | 130.75 s | 71.722 s | 5.874 s |
| Model cost 1.0 | 100.552 s | 129.68 s | 69.816 s | 5.956 s |
| Model cost 1.5 | 100.495 s | 123.74 s | 64.648 s | 5.879 s |

The expected cost trend remains visible in **computation**: increasing cost from 0.125 to 0.5 saves 3.035 seconds over ten iterations, followed by a plateau. Cost 0.5 saves 4.655 seconds of computation relative to ARMS. But it saves only 1.62 seconds in total here because its loading was slower. Loading varies by 8.876 seconds across this single sweep and changes the ranking of totals.

Thus HDD plus the historical cost settings does **not** recover the original magnitude. It also does not eliminate the cost-policy effect. The historical 0.125-to-0.5 improvement was 13.625 seconds in total, but its missing read/iteration split prevents attributing all of that difference to computation. Single executions and fixed case order limit precision. [Detailed analysis and raw result links](../reproductions/bc-twitter-may2026/conditions/ablation_builds/results_analysis.md).

## Confirmed fast-DRAM overhead: the unused PMEM device

The current intentional 4 GB boot exposes 4,737,988 KiB total on node 0. Separately, the unused 90 GiB emulated-PMEM namespace has ordinary page metadata allocated from node-0 RAM. This reduces actual free fast memory without reducing the node's reported MemTotal.

After verifying no mounts, swap, holders, partitions, open device descriptors, or process mappings, the experiment temporarily detached only `namespace7.0` from `nd_pmem`. It changed no namespace format or stored data. The measured metadata count fell from 368704 to 64 pages, returning **exactly 1440 MiB** of metadata memory. Node-0 free memory rose approximately 1.41 GiB, while node-1 free memory and node-0 total memory stayed unchanged. [Before/after evidence](../reproductions/bc-twitter-may2026/conditions/underlay_evidence/pmem_detach_delta.json).

| Same historical preparation, SSD | Startup free DRAM | Total | Read | Average iteration |
|---|---:|---:|---:|---:|
| ARMS, PMEM bound | 2.417 GiB | 103.63 s | 39.820 s | 6.34331 s |
| Model, PMEM bound | 2.439 GiB | 98.16 s | 39.687 s | 5.82238 s |
| ARMS, PMEM detached | 3.849 GiB | 99.17 s | 41.399 s | 5.74345 s |
| Model, PMEM detached | 3.882 GiB | 97.74 s | 39.541 s | 5.79556 s |
| ARMS, PMEM rebound confirmation | 2.479 GiB | 103.03 s | 39.633 s | 6.29869 s |

ARMS computation improves by almost six seconds across ten iterations while the model changes little. Global `compact_fail` deltas fall from 60,742 to 4,471 for ARMS; `pgmigrate_fail` falls from 66,470 to 4,514. These are host-wide counters, not precise per-process attribution, but agree with reduced migration/compaction pressure. [Paired results](../reproductions/bc-twitter-may2026/conditions/sessions/20260909T133945Z-186003/results/20260909T133946Z-186003/results.json).

The confirmation run after rebinding returned ARMS to **6.29869 seconds per iteration**, close to the initial bound-device 6.34331 seconds. This before/detached/restored sequence supports a repeatable effect of the intervention, including its free-memory and fragmentation changes. It does not isolate every mechanism within that intervention. [Confirmation results](../reproductions/bc-twitter-may2026/conditions/sessions/20260909T141557Z-194881/results/20260909T141557Z-194881/results.json).

The PMEM driver was successfully reattached, its metadata count returned to 368704, and every session restored host controls with zero reported errors. A final check confirms the original boot, device presence, effective perf cap 1,000,000/percentage 0, and no active benchmarks. [Final restoration check](../reproductions/bc-twitter-may2026/conditions/final_restoration_check.json).

This is evidence that **free-memory conditions can change ARMS much more than the model within the same nominal boot capacity**. It is not evidence that PMEM binding caused the historical discrepancy. No May binding record was found; historical cleanup never unloads `nd_pmem`. Moreover, absence of this overhead historically would make the tested ARMS faster, which is the opposite of the archived slower ARMS result.

## Other concrete findings and remaining uncertainty

There was substantial variation already in May. Three May 11 model totals using the same configuration name as the selected May 10 model are **115.306, 114.632, and 113.733 seconds**—a mean of 114.557 versus May 10's 98.126. Nearby same-boot HybridTier reads are approximately 40 seconds. This does not supply the model's own read split or establish identical weights, but it shows that the unexplained change cannot safely be dated only to today's source or HDD mount. [Timing context and source snapshots](../reproductions/bc-twitter-may2026/conditions/editor_history/may10_may11_timing_context.md).

A source audit found a concrete pre-existing migration bookkeeping bug: fragmented pages are filtered from one batch vector, but statuses are then applied using indices from the unfiltered vector. A mixed batch can therefore update the wrong page's DRAM-residency flag. The bug exists in both May and current source, dating to March 15, so it is a possible sensitivity to fragmentation rather than an identified intervening ARMS improvement. There is no retained original trace proving it was exercised. No production correction was mixed into these reproduction experiments. [Capacity and migration audit](../reproductions/bc-twitter-may2026/conditions/capacity_policy_audit.md).

The retained April 30 ARMS library was also tried separately. It aborted during startup with an allocator assertion and produced no usable timing. It is excluded from every comparison. Its precise relationship to the May binary and its failure cause remain unproven. The successfully rebuilt historical source is the usable candidate reconstruction.

The strongest recoverable model remains the May 3 object/text pair under `models/old`, verified against the separate training repository. Exact original ARMS/library/model hashes, prelaunch free-memory/fragmentation snapshots, the May boot command line, and original fast-socket frequency readbacks remain missing. A March GRUB backup uses a different memory-map reservation and debug flags, but is not evidence of what May used; no reboot or arbitrary tuning was performed to chase the target number.

For reproducible follow-up, retain the reconstructed source/model combination, explicit SSD input hash, historical preparation, and **measured** free DRAM/phase timings. The package now records these, plus effective settings, boot identity, input/library hashes, and restoration results. Recovering original boot or executable evidence would allow a targeted remaining comparison; selecting settings merely because they reproduce 115 seconds would not establish the historical cause.

All condition attempts, including the invalid retained-library launch, are collected in [summary.csv](../reproductions/bc-twitter-may2026/conditions/summary.csv) and [summary.json](../reproductions/bc-twitter-may2026/conditions/summary.json).
