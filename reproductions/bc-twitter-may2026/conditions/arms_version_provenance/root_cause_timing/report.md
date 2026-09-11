# Cross-workload timing evidence for the May-to-June transition

This extends the earlier BC-only provenance audit. It freshly inventories **2,084 original `times/c220g5` `.time` files modified in May or June 2026**, including `real`, `user`, `sys`, exit status, exact original mtime, SHA-256, and wtmp-inferred boot. No final-copy dates are used. No benchmarks or host changes were made.

## What the broader records establish

**The change was not confined to BC, ARMS, or the 4 GB label.** Both BC Twitter and PR Twitter improve from the May 13–15 period to June, including the 6 GB cohorts. The improvement differs by algorithm and policy; it is not a single uniform scaling factor.

All entries below are three successful ARMS runs of the 100-trial Twitter workloads. The 4032, 4035, etc. numbers are filename labels, not physical-memory readbacks.

| Workload / capacity label | May original mean (s) | June `40` mean (s) | June `41` mean (s) | May to June `40` |
|---|---:|---:|---:|---:|
| BC / 4032, May 12 | 711.096 | 625.315 | 628.544 | −12.06% |
| BC / 4035, May 14 | 808.557 | 625.315 | 628.544 | −22.66% |
| BC / 6032, May 14 | 709.392 | 607.534 | 604.708 | −14.36% |
| BC / 8 GB | no May 6.18 cohort | 594.470 | 597.038 | — |
| BC / 10 GB | no May 6.18 cohort | 591.172 | 589.728 | — |
| PR / 4032, May 13 | 2713.627 | 2587.799 | 2559.892 | −4.64% |
| PR / 4035, May 15 | 2850.425 | 2587.799 | 2559.892 | −9.21% |
| PR / 6032, May 14 | 2791.262 | 2482.627 | 2480.316 | −11.06% |
| PR / 8 GB | no May 6.18 cohort | 2412.436 | 2414.574 | — |
| PR / 10 GB | no May 6.18 cohort | 2404.154 | 2387.900 | — |

The first June 4 GB cohort is June 13–14, 6 GB June 15, 8 GB June 16, and 10 GB June 17–18. The second sweep runs the capacities in reverse order during June 18–23. Thus the June pattern persists across separate boots and reversed capacity order. A May 12 10 GB BC ARMS mean of 636.897 seconds also exists, but on **6.2**, and should not be treated as a matching 6.18 control.

Models improve too. At 6 GB, BC model totals fall from 692.705 to 613.382 seconds (−11.45%) and PR model totals fall from 2827.017 to 2559.132 (−9.48%). The BC gap narrows, while PR continues to favor ARMS: the June 4 GB PR model is 2692.481 seconds versus ARMS 2587.799. This does not support an ARMS-only blanket speedup across workloads.

Only the two Twitter algorithms provide matching 6.18 long-workload records spanning May and June. There are no May 6.18 long XSBench/DuckDB/Kron/FAISS cohorts sufficient to establish a systemwide change across those applications. The May-to-June comparison consequently cannot separate a graph-specific change from broader CPU/memory behavior.

## Same-boot controls and trial-count evidence

The May 12 BC 4032 cohort and May 13 PR 4032 cohort occupy **different boots**. The BC 4035 runs on May 14 and PR 4035 runs on May 15 do share the May 14 07:30:49 UTC boot, although the PR runs occur much later. No 4035 PR model timing file survives. The 6032 BC/PR model and ARMS long runs all fall in the May 13 16:28:17 boot; the model sweep precedes the ARMS sweep, so their matching run numbers are not consecutive pairs.

There are **same-boot standard/long model pairs** in that 6032 boot:

| Model workload | 10-trial mean | 100-trial mean | Effective seconds/additional trial | Effective fixed component |
|---|---:|---:|---:|---:|
| BC Twitter | 101.712 s | 692.705 s | 6.56658 s | 36.0465 s |
| PR Twitter | 307.773 s | 2827.017 s | 27.99159 s | 27.8574 s |

The last two columns solve `T(n) = fixed + n × trial` using the observed 10- and 100-trial totals. They are **inferences, not saved read times**. Each pair uses the same named workload-specific `1_10_0.1` model, the same graph path, 16 threads, virtual-step samples 10000, and the same boot/size label. Different workloads occur between the paired short and long executions.

The fits suggest that a pure graph-loading explanation is insufficient for the much larger later reductions in 100-trial totals, but they are not a proof: BC's additional 90 pseudorandom sources can have different traversal costs; policy adaptation and cache state may make trial costs nonlinear. No standard/long pair survives in the June 4/6/8/10 GB boots, so a comparable June fit cannot be constructed. No standard BC ARMS timing survives in the May 6032 boot either.

## CPU-time fields cannot identify the mechanism

The ARMS/model `.time` files do **not** contain meaningful workload CPU accounting. For example, May 14 BC ARMS run1 reports 794.368 seconds elapsed but only **0.012 seconds user + 0.034 seconds system**. June 14 BC run1 reports 621.264 elapsed but **0.015 + 0.035** CPU seconds.

Across the 219 successful `arms` runs lasting more than a minute in this May/June inventory, median user+sys is **0.043 seconds** and maximum is **0.060 seconds**. The corresponding model fields have essentially the same signature. The timed command goes through `timeout → numactl → taskset → sudo env`; the recorded CPU numbers therefore cannot support conclusions about workload migration time, frequency, or CPU utilization. HybridTier files contain substantial CPU totals, but no matched June HybridTier cohort survives.

The long-workload `logs/` directories are empty, and their original timing directories contain only `.time` files, so there are no retained read/iteration splits or migration-counter sidecars to fill this gap.

## Storage/DRAM baseline controls are from another boot

The original May 31 6.2 boot contains these standard-workload controls:

| Workload | DRAM-only | CXL-only |
|---|---:|---:|
| BC Twitter | 127.261 s | 191.453 s |
| PR Twitter | 305.598 s | 610.956 s |

These are not controls for the June 6.18 long sweeps, and their files do not identify the graph's backing disk. No same-June-boot all-DRAM/CXL-only Twitter baseline survives. The only June standard BC timing is a June 6 **training** run labeled 100050 on 6.2, which is not a normal 4 GB comparison.

## Workload consistency and invalid controls

All ten committed `*-long.sh` definitions have identical Git blobs at May 14 and May 30 HEAD. Editor history additionally confirms both Twitter algorithms restored **100 trials** on May 12 at approximately 19:41 UTC, before the retained 4 GB long runs, and kept the same path/thread count through June.

The apparently large May-to-June improvements in some other long workloads are not valid controls: Kron BC and PR changed from **100 to 50 trials** on May 12, while FAISS changed from **10 to 5 search repeats**. Their earlier May 12 runs preceded those edits. The tenfold/long labels alone do not establish a fixed workload.

The inventory also identifies 48 subsecond `mg.D.x-long` crashes with exit status 139 and 12 subsecond FAISS long results reporting status 0. Those FAISS results cannot be treated as successful full benchmarks merely because their wrapper returned zero. They are excluded from the elapsed-performance comparisons in this report.

## Evidence files

- `all_original_may_june_times.{csv,json}`: all 2,084 original timing records.
- `selected_cohort_summary.json`, `cross_workload_comparisons.json`: capacity/workload/policy means and exact source paths.
- `selected_boot_inventory.json`: all groups/policies present in the relevant boots.
- `may13_same_boot_trial_count_fits.json`: per-run n10/n100 inputs, fits, timestamps, and limitations.
- `cpu_time_field_summary.json`: CPU-accounting limitations by result group.
- `all_long_workload_git_commands.json`, `workload_editor_command_history.json`, `editor_workloads/`: command consistency and retained local edits.
- `subsecond_long_results.json`: anomalous/crashed long results kept separate from performance conclusions.
