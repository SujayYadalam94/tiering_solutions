| Session / setting | Policy | Total s | Read s | Avg s | System CPU s | Δ avg vs baseline mean s | Start node0 free MiB | Accepted ratio / boost / min-free KiB / scan pages | Restored |
|---|---|---:|---:|---:|---:|---:|---:|---|---|
| 20260909T170421Z-382094 / none (baseline) | may07_arms | 107.600 | 39.621 | 6.75623 | 154.03 | — | 2516.2 | 256 256 32 0 0 / 10000 / 1048576 / 8192 | yes |
| 20260909T170421Z-382094 / none (baseline) | may07_model | 98.930 | 39.676 | 5.90007 | 70.30 | — | 2531.3 | 256 256 32 0 0 / 10000 / 1048576 / 8192 | yes |
| 20260909T173344Z-414920 / none (baseline) | may07_arms | 105.800 | 39.617 | 6.57701 | 135.64 | — | 2548.7 | 256 256 32 0 0 / 10000 / 1048576 / 8192 | yes |

Differences are test minus explicit matching baseline mean; negative runtime differences indicate improvement. Baseline ranges/stdev are recorded. Sequential tests are not randomized causal estimates.

Baseline matching excludes only the four intentionally varied VM/khugepaged target paths; the audit separately checks all target and unchanged controls before/after.

VM and khugepaged event deltas are system-wide, not process-specific time or work. Thread CPU seconds may exceed wall time.

Fewer compact_fail events alone do not establish less memory pressure: inspect allocstall_movable, pgscan_direct, pgsteal_direct, THP migration outcomes, CPU seconds and runtime together.

Node0 zone watermarks and protection are native page counts from snapshots, not MiB. Before/after memory snapshots are not in-run peaks.

Latest PID status usually lacks AnonHugePages; missing remains null and RssAnon is separately labeled.
