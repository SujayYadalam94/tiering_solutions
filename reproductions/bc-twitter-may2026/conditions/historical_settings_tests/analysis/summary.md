| Session / setting | Policy | Total s | Read s | Avg s | System CPU s | Δ avg vs baseline mean s | Start node0 free MiB | Accepted ratio / boost / min-free KiB / scan pages | Restored |
|---|---|---:|---:|---:|---:|---:|---:|---|---|
| 20260909T175404Z-425061 / none (baseline) | may07_arms | 106.280 | 39.669 | 6.62072 | 138.69 | — | 2514.4 | 256 256 32 0 0 / 10000 / 1048576 / 8192 | yes |
| 20260909T175404Z-425061 / none (baseline) | may07_model | 97.900 | 39.432 | 5.82141 | 60.21 | — | 2516.1 | 256 256 32 0 0 / 10000 / 1048576 / 8192 | yes |
| 20260909T175946Z-432507 / lowmem32 | may07_arms | 109.050 | 41.109 | 6.75583 | 155.14 | -0.11946 | 2556.6 | 32 32 16 0 0 / 10000 / 1048576 / 8192 | yes |
| 20260909T175946Z-432507 / lowmem32 | may07_model | 99.390 | 40.995 | 5.81345 | 62.22 | 0.00270 | 2550.3 | 32 32 16 0 0 / 10000 / 1048576 / 8192 | yes |
| 20260909T180532Z-439108 / lowmem4 | may07_arms | 136.850 | 39.728 | 9.67427 | 366.89 | 2.79899 | 2535.2 | 4 4 4 0 0 / 10000 / 1048576 / 8192 | yes |
| 20260909T180532Z-439108 / lowmem4 | may07_model | 112.390 | 39.515 | 7.25714 | 211.22 | 1.44639 | 2594.1 | 4 4 4 0 0 / 10000 / 1048576 / 8192 | yes |
| 20260909T181201Z-445918 / watermark_boost0 | may07_arms | 108.780 | 39.525 | 6.88353 | 172.58 | 0.00825 | 2530.1 | 256 256 32 0 0 / 0 / 1048576 / 8192 | yes |
| 20260909T181201Z-445918 / watermark_boost0 | may07_model | 98.060 | 39.639 | 5.81790 | 62.31 | 0.00715 | 2549.3 | 256 256 32 0 0 / 0 / 1048576 / 8192 | yes |
| 20260909T181744Z-453115 / min_free16m | may07_arms | 108.010 | 39.666 | 6.79182 | 161.40 | -0.08346 | 2550.6 | 256 256 32 0 0 / 10000 / 16384 / 8192 | yes |
| 20260909T181744Z-453115 / min_free16m | may07_model | 97.780 | 39.586 | 5.79490 | 60.66 | -0.01585 | 2542.6 | 256 256 32 0 0 / 10000 / 16384 / 8192 | yes |
| 20260909T182328Z-460094 / khugepaged_pages4096 | may07_arms | 107.380 | 39.675 | 6.73083 | 152.85 | -0.14445 | 2531.1 | 256 256 32 0 0 / 10000 / 1048576 / 4096 | yes |
| 20260909T182328Z-460094 / khugepaged_pages4096 | may07_model | 98.150 | 39.566 | 5.83309 | 60.35 | 0.02234 | 2532.5 | 256 256 32 0 0 / 10000 / 1048576 / 4096 | yes |
| 20260909T182911Z-468305 / none (baseline) | may07_arms | 111.420 | 39.663 | 7.12985 | 201.44 | — | 2520.3 | 256 256 32 0 0 / 10000 / 1048576 / 8192 | yes |
| 20260909T182911Z-468305 / none (baseline) | may07_model | 97.670 | 39.407 | 5.80008 | 60.29 | — | 2546.9 | 256 256 32 0 0 / 10000 / 1048576 / 8192 | yes |

Differences are test minus explicit matching baseline mean; negative runtime differences indicate improvement. Baseline ranges/stdev are recorded. Sequential tests are not randomized causal estimates.

Baseline matching excludes only the four intentionally varied VM/khugepaged target paths; the audit separately checks all target and unchanged controls before/after.

VM and khugepaged event deltas are system-wide, not process-specific time or work. Thread CPU seconds may exceed wall time.

Fewer compact_fail events alone do not establish less memory pressure: inspect allocstall_movable, pgscan_direct, pgsteal_direct, THP migration outcomes, CPU seconds and runtime together.

Node0 zone watermarks and protection are native page counts from snapshots, not MiB. Before/after memory snapshots are not in-run peaks.

Latest PID status usually lacks AnonHugePages; missing remains null and RssAnon is separately labeled.
