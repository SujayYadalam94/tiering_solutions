| Session / setting | Case | Total s | Read s | Avg iteration s | System CPU s | Δ avg vs explicit baseline s | Start node0 free MiB | Compact failures | Collapse failures | Δ full scans / pages collapsed | Controls d/a/s | Restored |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|
| 20260909T143041Z-209179 / khugepaged_scan10 | may07_arms | 99.980 | 39.723 | 5.98396 | 71.75 | -0.24666 | 2533.0 | 3268 | 459 | ?/? | 1/1/10 | yes |
| 20260909T143041Z-209179 / khugepaged_scan10 | may07_model | 97.790 | 39.490 | 5.80486 | 61.43 | — | 2539.6 | 0 | 0 | ?/? | 1/1/10 | yes |
| 20260909T143646Z-217337 / none (baseline) | may07_arms | 104.850 | 42.124 | 6.23062 | 94.31 | — | 2538.0 | 49678 | 24535 | ?/? | 1/1/0 | yes |

VM counters are whole-system before/after event deltas, not process attribution or seconds spent compacting.

Optional khugepaged full_scans/pages_collapsed values are also whole-system counter snapshots and deltas; missing older snapshots remain null.

System/user CPU seconds accumulate across threads and may exceed wall-clock time.

Before/after node/global memory values are snapshots around process execution; after is not peak/in-run usage.

latest_status.txt often lacks AnonHugePages; missing values stay null. RssAnon is separately labeled and is not huge-page usage.

Control shorthand d/a/s means khugepaged defrag / allocation sleep milliseconds / scan sleep milliseconds.

Baseline differences use only explicitly named, valid matching runs. Test minus baseline is negative for an improvement in runtime. Sequential measurements are not randomized causal estimates.
