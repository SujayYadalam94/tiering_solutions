| Session / setting | Case | Total s | Read s | Avg iteration s | System CPU s | Δ avg vs explicit baseline s | Start node0 free MiB | Compact failures | Collapse failures | Δ full scans / pages collapsed | Controls d/a/s | Restored |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|
| 20260909T170421Z-382094 / none (baseline) | may07_arms | 107.600 | 39.621 | 6.75623 | 154.03 | — | 2516.2 | 70340 | 31473 | 13086/1871 | 1/1/0 | yes |
| 20260909T170421Z-382094 / none (baseline) | may07_model | 98.930 | 39.676 | 5.90007 | 70.30 | — | 2531.3 | 30797 | 15092 | 17967/584 | 1/1/0 | yes |
| 20260909T171055Z-389918 / khugepaged_defrag0 | may07_arms | 103.220 | 39.321 | 6.35255 | 88.13 | -0.40368 | 2536.3 | 6446 | 46143 | 13470/1262 | 0/1/0 | yes |
| 20260909T171055Z-389918 / khugepaged_defrag0 | may07_model | 98.150 | 39.719 | 5.81773 | 62.03 | -0.08234 | 2529.4 | 21 | 0 | 24166/260 | 0/1/0 | yes |
| 20260909T171729Z-396978 / khugepaged_alloc1000 | may07_arms | 101.990 | 39.643 | 6.18846 | 83.84 | -0.56777 | 2533.2 | 4668 | 94 | 10986/336 | 1/1000/0 | yes |
| 20260909T171729Z-396978 / khugepaged_alloc1000 | may07_model | 99.840 | 41.299 | 5.82779 | 60.29 | -0.07228 | 2535.7 | 0 | 0 | 24038/294 | 1/1000/0 | yes |
| 20260909T172342Z-403992 / khugepaged_scan1000 | may07_arms | 101.400 | 40.923 | 6.00241 | 73.26 | -0.75382 | 2515.0 | 2458 | 1 | 0/2 | 1/1/1000 | yes |
| 20260909T172342Z-403992 / khugepaged_scan1000 | may07_model | 98.120 | 39.608 | 5.82721 | 60.58 | -0.07286 | 2533.3 | 19 | 0 | 0/1 | 1/1/1000 | yes |
| 20260909T172954Z-411197 / khugepaged_scan1000 | may07_arms | 100.510 | 39.922 | 6.01563 | 73.16 | -0.74060 | 2546.7 | 2631 | 0 | 0/0 | 1/1/1000 | yes |
| 20260909T173344Z-414920 / none | may07_arms | 105.800 | 39.617 | 6.57701 | 135.64 | -0.17922 | 2548.7 | 64855 | 29486 | 13496/1674 | 1/1/0 | yes |

VM counters are whole-system before/after event deltas, not process attribution or seconds spent compacting.

Optional khugepaged full_scans/pages_collapsed values are also whole-system counter snapshots and deltas; missing older snapshots remain null.

System/user CPU seconds accumulate across threads and may exceed wall-clock time.

Before/after node/global memory values are snapshots around process execution; after is not peak/in-run usage.

latest_status.txt often lacks AnonHugePages; missing values stay null. RssAnon is separately labeled and is not huge-page usage.

Control shorthand d/a/s means khugepaged defrag / allocation sleep milliseconds / scan sleep milliseconds.

Baseline differences use only explicitly named, valid matching runs. Test minus baseline is negative for an improvement in runtime. Sequential measurements are not randomized causal estimates.
