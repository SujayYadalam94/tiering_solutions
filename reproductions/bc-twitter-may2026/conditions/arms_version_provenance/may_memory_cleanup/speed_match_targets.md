# Speed matching to the original May 7 BC Twitter results

Generated 2026-09-09T19:30:16.521338+00:00. Matching both archived runtimes is the objective. This analysis reads saved results only.

The verified target is **ARMS 115.804 s / model 100.127 s**, a **15.677 s total gap**. The model is the retained May 3 forest candidate with `[2, 10, 0.5]`; cost 0.5 had the lowest mean in the original sweep.

| Original May 7 cohort | Run 1 s | Run 2 s | Run 3 s | Mean s | Observed range s |
|---|---:|---:|---:|---:|---:|
| ARMS, 4009 label | 112.781 | 118.355 | 116.275 | 115.804 | 112.781–118.355 |
| Model, 4009 label | 100.394 | 100.399 | 99.587 | 100.127 | 99.587–100.399 |

All six source files are the originals in `times/c220g5`, completed May 7: ARMS at 09:07, 12:54 and 16:40 UTC; selected model at 09:15, 13:01 and 16:48. Their bytes and nanosecond mtimes still match the previously audited inventory. The May 11 `c220g5_final` copy dates are not execution dates. See [original timing provenance](../timeline_findings.md) and the exact paths/hashes in [machine-readable results](speed_match_targets.json).

## Joint speed ranking

Score is the RMS of the ARMS and model percentage errors relative to their own original means, with equal weight. Lower means closer to both historical times. It does not reward being faster, widening the gap alone, or correcting accounting. Only completed valid ten-trial pairs with the exact uninstrumented candidate library hashes are included; current-code, instrumentation, fixed-index and incomplete pairs are excluded.

| Rank | Setting / preparation / storage | Session | ARMS total s | Model total s | Total gap s | Compute gap s (10 trials) | Joint error % |
|---:|---|---|---:|---:|---:|---:|---:|
| 1 | none; SSD; may04_full | [20260909T182911Z-468305](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T182911Z-468305) | 111.42 | 97.67 | 13.75 | 13.30 | 3.19 |
| 2 | lowmem32; SSD; may04_full | [20260909T175946Z-432507](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T175946Z-432507) | 109.05 | 99.39 | 9.66 | 9.42 | 4.16 |
| 3 | watermark_boost0; SSD; may04_full | [20260909T181201Z-445918](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T181201Z-445918) | 108.78 | 98.06 | 10.72 | 10.66 | 4.53 |
| 4 | min_free16m; SSD; may04_full | [20260909T181744Z-453115](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T181744Z-453115) | 108.01 | 97.78 | 10.23 | 9.97 | 5.04 |
| 5 | none; SSD; may04_full | [20260909T170421Z-382094](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T170421Z-382094) | 107.60 | 98.93 | 8.67 | 8.56 | 5.08 |
| 6 | khugepaged_pages4096; SSD; may04_full | [20260909T182328Z-460094](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T182328Z-460094) | 107.38 | 98.15 | 9.23 | 8.98 | 5.33 |
| 7 | none; SSD; may04_full | [20260909T175404Z-425061](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T175404Z-425061) | 106.28 | 97.90 | 8.38 | 7.99 | 6.02 |
| 8 | perf_rate100000; SSD; may04_full | [20260909T132959Z-178251](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T132959Z-178251) | 104.95 | 99.62 | 5.33 | 5.32 | 6.64 |
| 9 | none; SSD; may04_full | [20260909T130001Z-142220](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T130001Z-142220) | 103.63 | 98.16 | 5.47 | 5.21 | 7.56 |
| 10 | compaction20; SSD; may04_full | [20260909T142436Z-200883](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T142436Z-200883) | 103.23 | 98.52 | 4.71 | 4.74 | 7.76 |
| 11 | khugepaged_defrag0; SSD; may04_full | [20260909T171055Z-389918](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T171055Z-389918) | 103.22 | 98.15 | 5.07 | 5.35 | 7.81 |
| 12 | none; SSD; graph_cache_only | [20260909T122951Z-129527](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/results/20260909T122951Z-129527) | 103.16 | 97.58 | 5.58 | 5.29 | 7.93 |
| 13 | lowmem8; SSD; may04_full | [20260909T192149Z-500236](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T192149Z-500236) | 123.60 | 109.33 | 14.27 | 12.53 | 8.06 |
| 14 | khugepaged_alloc1000; SSD; may04_full | [20260909T171729Z-396978](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T171729Z-396978) | 101.99 | 99.84 | 2.15 | 3.61 | 8.44 |
| 15 | khugepaged_scan1000; SSD; may04_full | [20260909T172342Z-403992](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T172342Z-403992) | 101.40 | 98.12 | 3.28 | 1.75 | 8.91 |
| 16 | khugepaged_scan10; SSD; may04_full | [20260909T143041Z-209179](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T143041Z-209179) | 99.98 | 97.79 | 2.19 | 1.79 | 9.80 |
| 17 | release_pmem_metadata; SSD; may04_full | [20260909T133945Z-186003](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T133945Z-186003) | 99.17 | 97.74 | 1.43 | -0.52 | 10.30 |
| 18 | lowmem4; SSD; may04_full | [20260909T180532Z-439108](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T180532Z-439108) | 136.85 | 112.39 | 24.46 | 24.17 | 15.50 |
| 19 | none; HDD; graph_cache_only | [20260909T124445Z-135186](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/results/20260909T124445Z-135186) | 129.19 | 122.92 | 6.27 | 3.66 | 18.05 |
| 20 | none; HDD; may04_full | [20260909T130805Z-154045](/users/zimooo2/tiering_solutions/reproductions/bc-twitter-may2026/conditions/sessions/20260909T130805Z-154045) | 132.37 | 130.75 | 1.62 | 4.65 | 23.88 |

“none” is the full May setup baseline, unless the row explicitly says graph-cache-only. Ratio settings use complete five-entry vectors with trailing zeros. Full preparation runs setup/defrag and graph eviction before each policy; graph-cache-only rows use inherited controls. SSD and HDD graphs have the same content hash, and every included case has the same executable hash, boot ID and Linux 6.18 kernel.

## Repeatability and measured phases

The two baseline pairs bracketing the five historical-setting alternatives average **108.850 / 97.785 s**, joint error **4.56%**. ARMS moved from 106.28 to 111.42 seconds between those baselines; model moved from 97.90 to 97.67. Thus a late baseline can be the closest observation without proving any setting improved reproducibility. Ratio32 is the closest tested alternative before ratio8, but its extra 1.4–1.6 seconds of reading accounts for part of its total-time movement. Boost0, min_free16m and pages4096 have computation within the observed ARMS baseline range. Ratio4 overshoots both archived runtimes; its wider model advantage alone is not a match.

| Setting | ARMS read s | Model read s | ARMS mean trial s | Model mean trial s |
|---|---:|---:|---:|---:|
| none; SSD; may04_full; 20260909T182911Z-468305 | 39.66274 | 39.40661 | 7.12985 | 5.80008 |
| lowmem32; SSD; may04_full; 20260909T175946Z-432507 | 41.10934 | 40.99477 | 6.75583 | 5.81345 |
| watermark_boost0; SSD; may04_full; 20260909T181201Z-445918 | 39.52489 | 39.63938 | 6.88353 | 5.81790 |
| min_free16m; SSD; may04_full; 20260909T181744Z-453115 | 39.66616 | 39.58555 | 6.79182 | 5.79490 |
| none; SSD; may04_full; 20260909T170421Z-382094 | 39.62140 | 39.67640 | 6.75623 | 5.90007 |
| khugepaged_pages4096; SSD; may04_full; 20260909T182328Z-460094 | 39.67523 | 39.56621 | 6.73083 | 5.83309 |
| lowmem8; SSD; may04_full; 20260909T192149Z-500236 | 41.37079 | 39.69854 | 8.18184 | 6.92849 |
| lowmem4; SSD; may04_full; 20260909T180532Z-439108 | 39.72778 | 39.51537 | 9.67427 | 7.25714 |
| none; HDD; graph_cache_only; 20260909T124445Z-135186 | 66.80747 | 64.35975 | 6.20127 | 5.83555 |
| none; HDD; may04_full; 20260909T130805Z-154045 | 68.54309 | 71.72196 | 6.33899 | 5.87354 |

The HDD pairs load in approximately 64–72 seconds and put both policy totals above their original targets. Their computation gaps remain small. Those completed HDD conditions therefore do not recreate the archived pair.

## Numeric reference for historical reproduction

**First-pass engineering band: ARMS 113–119 seconds, model 99–102 seconds, and ARMS-minus-model 13–18 seconds, all three together.** These are practical rounded tolerances anchored to the original means/ranges, not confidence intervals. A single matching pair would not establish repeatability or a historical cause; this ranking does not prescribe further parameter-fitting runs. The exact primary targets remain 115.803667 and 100.126667 seconds.

Original `.time` files do not save read time or average iteration time. Therefore the archived 15.677-second gap is a **total** gap, not a measured computation gap. Conditionally, if a new run keeps approximately 40 seconds of reading and 0.3–0.5 seconds of other elapsed overhead, the target totals imply about **7.53–7.55 seconds per ARMS trial and 5.96–5.98 seconds per model trial**. This arithmetic is a target under current loading, not recovered historical phase timing. Use the actual recorded read/overhead for any precise per-trial target.

Matching totals demonstrates a performance reproduction under recorded conditions. It does not establish that those conditions, library bytes, or graph mount were used in May. No accounting correctness criterion is used here. Full result identities, signed errors, phase gaps and exclusion reasons are in [JSON](speed_match_targets.json).
