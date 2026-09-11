# Bounded May 10–11 BC Twitter timing and source comparison

This comparison reads the existing `docs/arms_model_storage_evidence/timing_inventory.csv` and boot ledger; it does not rescan logs or execute workloads. The selected original C220G5 4 GB timing records and inferred boots are preserved in [bc4gb_timing_timeline.csv](bc4gb_timing_timeline.csv). Directory names and memory labels identify reported configurations, not verified boot settings. Filesystem timestamps support chronology only when they are original execution-like timestamps, rather than clustered archive-copy timestamps.

## ARMS chronology

| Batch | BC Twitter ARMS totals (seconds) | Interpretation |
|---|---|---|
| May 4, 4008 | 121.201, 114.783, 115.013 | Original execution-like timestamps |
| May 7, 4009 | 112.781, 118.355, 116.275 | Original reference batch |
| May 8–14, approximately 4 GB | No surviving C220G5 ARMS record in the inventory | Cannot locate a May 11 ARMS comparison or date a 4 GB speedup here |
| August 27, 4053 | 134.192, 128.651, 126.669 | Later totals remain above the original mean; storage and host conditions are not established by these rows |
| September 8–9, 4077 | 102.475, 102.645, 104.011 | First successful approximately 100-second 4 GB ARMS cases in this inventory |

The approximately 98–99-second ARMS runs on May 8–10 use 6,009, 8,009, 10,009, or 10,028 labels. They are not evidence that 4 GB ARMS became faster in May. The final archive's May 11 ARMS timestamps reproduce the exact May 7 timing contents and reflect copying, not a new May 11 ARMS run. Earlier 4,000-label ARMS backups also have clustered copy timestamps and do not establish original execution dates.

## Same model filename, different historical total

| Batch | Filename configuration | Three totals (seconds) | Mean | Boot inferred from original mtime |
|---|---|---|---|---|
| May 10, 4028 | discounted reward 99, `1_10_0.1` | 97.911, 98.486, 97.981 | 98.126 | May 9, 16:32:51 UTC, 6.18 |
| May 11, 4029 | discounted reward 99, `1_10_0.1` | 115.306, 114.632, 113.733 | 114.557 | May 11, 14:52:00 UTC, 6.18 |

The second mean is 16.431 seconds higher. The matching filenames establish the same named model/configuration, not exact forest bytes, effective source, or host conditions. The May 11 hybridtier runs immediately following these model cases are separate measurements; there is no adjacent ARMS record in the inventory.

## Closest surviving editor saves

The latest pre-May-10 `page.cpp` save, `szjq.cpp`, is timestamped May 10, 02:30:32.714 UTC and is byte-identical to the frozen May 9 source. The May 11 save `OxR3.cpp`, timestamped 02:14:26.598 UTC, only adds `this->can_demote = true` during reset. See [the exact page diff](may10_to_may11_page.cpp.diff).

The corresponding pre-May-10 `arms_kernel.cpp` save, `v8PL.cpp`, is timestamped May 9, 22:34:14.151 UTC and also matches the frozen May 9 source exactly. The May 11 save `gzgR.cpp`, timestamped 02:41:07.306 UTC, only removes two multiplications by literal `1` from migration-cost expressions. See [the exact policy diff](may10_to_may11_arms_kernel.cpp.diff).

There are no intervening retained defs or Makefile saves: their latest relevant snapshots remain May 9, 23:29:48.585 UTC and May 10, 02:11:26.596 UTC respectively. This absence does not rule out edits made outside VS Code.

The reset flag is a weak explanation for a model slowdown: model ranking calls `update_can_promote` for every scored candidate before migration selection, and that model branch explicitly rewrites `can_demote` from rank. The ARMS branch has different flag behavior, but the May 11 comparison above contains model timings, and no corresponding 4 GB ARMS run survives. No causal explanation for the 16.431-second historical model difference is established by these editor changes.
