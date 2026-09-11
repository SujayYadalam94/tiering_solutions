# Khugepaged sensitivity tests

September 9, 2026. The three requested settings were tested separately using the uninstrumented early-May ARMS and May-model libraries, the same SSD graph, and the same intentional 4 GB Linux 6.18 boot. The model uses the retained May 3 forest with history mode 2, length 10, and cost scaler 0.5. Each case has ten BC iterations.

**All ten executions completed successfully. Scan sleep at 1000 ms gave the strongest ARMS computation result and repeated closely; restoring the original settings brought back slower computation and much higher compaction activity.** The model's computation changed much less. All captured host controls were restored and independently verified.

## First pass

| Setting | ARMS total | Model total | ARMS average iteration | Model average iteration | Computation gap over ten iterations |
|---|---:|---:|---:|---:|---:|
| Original settings | 107.60 s | 98.93 s | 6.75623 s | 5.90007 s | 8.5616 s |
| Only khugepaged defrag = 0 | 103.22 s | 98.15 s | 6.35255 s | 5.81773 s | 5.3482 s |
| Only allocation-failure sleep = 1000 ms | 101.99 s | 99.84 s | 6.18846 s | 5.82779 s | 3.6067 s |
| Only scan sleep = 1000 ms | 101.40 s | 98.12 s | 6.00241 s | 5.82721 s | 1.7520 s |

The computation gap is ten times the difference between the two reported average trial times; it excludes loading. Model loading took 41.299 seconds in the allocation-sleep case versus 39.676 at baseline, so that pair's 2.15-second total gap understates its 3.61-second computation gap. The scan-sleep ARMS run similarly had a longer 40.923-second read versus 39.621 at baseline. Full read-time and per-trial results are preserved in the [analysis table](analysis/summary.md) and [JSON](analysis/summary.json).

## What changed in kernel work

These are the ARMS observations from the first pass. VM event counts are global increases during the workload, not PID-specific attribution. System CPU belongs to the timed workload and its children; it excludes the separate khugepaged daemon.

| Setting | Workload system CPU | Direct compaction failures | Collapse allocations | Failed collapse allocations | THP migration splits |
|---|---:|---:|---:|---:|---:|
| Original settings | 154.03 s | 70,340 | 1,939 | 31,473 | 3,669 |
| Khugepaged defrag = 0 | 88.13 s | 6,446 | 1,316 | 46,143 | 3,206 |
| Allocation-failure sleep = 1000 ms | 83.84 s | 4,668 | 339 | 94 | 2,276 |
| Scan sleep = 1000 ms | 73.26 s | 2,458 | 2 | 1 | 1,256 |

Disabling khugepaged defrag increases the number of failed collapse allocations while reducing compaction and workload kernel CPU. A failure count alone therefore does not measure its cost. Increasing allocation sleep suppresses the rapid failure/retry pattern. Increasing scan sleep largely suppresses collapse activity over this workload interval and gives the best computation timing of the three tested values.

Migration splitting persists even with very little collapse activity. These results support an interaction between aggressive huge-page collapse, allocation pressure, and ARMS migration work; they do not establish an address-by-address split/re-collapse cycle or attribute every kernel event to one process. Summed system CPU is not elapsed time and must not be subtracted directly from benchmark time.

## Confirmation

Repeating only the scan-sleep ARMS case gave **100.51 seconds total, 39.92178 seconds read, and 6.01563 seconds per iteration**. Workload system CPU was 73.16 seconds, with 2,631 compaction failures. The two modified computation results differ by 0.01322 seconds per iteration, about 0.22%.

The final return-to-original-settings ARMS run gave **105.80 seconds total, 39.61655 seconds read, and 6.57701 seconds per iteration**. Workload system CPU returned to 135.64 seconds, compaction failures to 64,855, and failed collapse allocations to 29,486.

| Confirmation observation | Initial baseline | Scan 1000 ms, first pass | Scan 1000 ms, repeat | Restored baseline |
|---|---:|---:|---:|---:|
| Average iteration | 6.75623 s | 6.00241 s | 6.01563 s | 6.57701 s |
| System CPU | 154.03 s | 73.26 s | 73.16 s | 135.64 s |
| Compaction failures | 70,340 | 2,458 | 2,631 | 64,855 |

The two baseline observations differ by 0.17922 seconds per iteration, so a single exact percentage would overstate precision. Across the observed baseline/modified combinations, scan sleep saves about **5.6–7.5 seconds of computation over ten iterations**, an **8.5–11.2% reduction**. The return of slower computation and high kernel-work counts supports a repeatable setting effect rather than a monotonic improvement throughout the sequence. Defrag and allocation-sleep variants each have only one ARMS/model pair; the scan variant has two ARMS observations, and the restored baseline reruns ARMS only.

## Execution and provenance

The [study manifest](study.json) lists every session and selected library/configuration. Each execution used full historical setup plus the second defrag pass; the override was applied only after the final defrag. Seven THP/compaction settings were checked before launch. Settings were recorded before and after each workload. Each initial pair completed with ten trials, no detected competing benchmark, and zero restoration errors.

The first eight starts had 2515.0–2536.3 MiB free on node 0; the complete ten-run range was 2515.0–2548.7 MiB. The graph, BC executable, and policy-specific library hashes match across all runs. No namespace detach, model/source rebuild, or kernel switch was used. The libraries do not include the migration instrumentation from the preceding investigation. [Completed-result audit](results_audit.json).

The independent final readback found no differences from any session's saved controls, IRQ affinities, socket uncore values, or affinities of surviving original processes. No BC/PR/benchmark-runner process remained; the boot was unchanged and namespace7.0 remained bound to `nd_pmem`. [Final restoration check](final_restoration_check.json).

The initial current baseline is slower than some earlier September reconstructions, which is why a fresh baseline and a final baseline return were included. The results diagnose current setting sensitivity. The retained May and current scripts both request khugepaged defrag 1 and scan/allocation sleeps 0/1 ms; these tests do not establish that a changed setting caused the historical May-to-June performance transition.
