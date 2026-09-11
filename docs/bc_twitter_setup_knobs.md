# BC Twitter: individual setup settings

September 9, 2026. This narrows the [conditions investigation](bc_twitter_conditions_followup.md) to the controls requested by `setup.sh`, now factored into [measurement_settings.sh](../measurement_settings.sh).

**The full historical setup replay was not an ablation of its individual settings.** Comparing the first full-preparation case's `before_preparation.json` and `before.json` shows no change in the captured VM/THP/NUMA/perf control values. They already matched the historical requests. That experiment tested the preparation operations, including repeated compaction and cache eviction, while retaining the same aggressive runtime settings. Its negative result does not establish that individual settings are irrelevant. [Readback comparison and inherited-control observations](../reproductions/bc-twitter-may2026/conditions/setup_knob_audit.json).

## Controls worth distinguishing

| Control requested in May and today | What it can affect | Historical evidence |
|---|---|---|
| THP `defrag=always` | An application can wait for reclaim/compaction while obtaining a huge page. This can change loading and the resulting page layout. | Already present before the May benchmarks. |
| `compaction_proactiveness=80` | How aggressively background compaction works to preserve contiguous free memory. This does not disable or enable every source of direct compaction. | Raised from 20 on March 14, `d87369bce`; no identified post-May ordinary-run value change. |
| khugepaged: `pages_to_scan=8192`, scan sleep `0 ms`, allocation-failure sleep `1 ms` | Huge-page collapse work, CPU consumption, and page layout. Zero scan sleep permits continuous scanning. | Added March 14. March 16 briefly used sleeps 10/10, then restored 0/1 that day (`a6ad3eb0d`, `ec524f599`). |
| `min_free_kbytes=1048576`, watermark scale `10`, boost `10000` | Per-zone allocation/reclaim thresholds and responses to fragmentation. | Minimum raised from 16384 KiB and boost from 0 on March 14. The displayed scale change 1→10 needs care: the old script also wrote 10 later. These are pre-May edits. |
| `vfs_cache_pressure=2000` | Reclaim pressure on directory/inode caches, potentially affecting cache and memory conditions. | Same ordinary requested value in the May and current setup. |
| Slow-socket uncore `0x620=0x707`; process/IRQ CPUs `10-19,30-39` | Slow-memory access costs and interference from other processes. | Same ordinary C220G5 requests in May and today; historical effective readbacks are missing. |
| Perf cap `1000000`, CPU percentage `0` | Sampling limits. The newer writer can successfully install a cap after percentage was zero. | Write order changed later; explicit 100000-versus-1000000 experiments did not recover the original gap. |

Kernel semantics: [THP controls](https://docs.kernel.org/admin-guide/mm/transhuge.html#sysfs), [VM compaction controls](https://docs.kernel.org/admin-guide/sysctl/vm.html#compaction-proactiveness). These explain mechanisms; they do not establish which May settings took effect.

## Two setup traps

1. **The final defrag script overwrites THP choices.** [defrag.sh:4](../defrag.sh#L4) explicitly writes both THP enabled and defrag to `always`. The historical helper calls it twice, once inside setup and once afterward. Changing a THP value earlier in setup alone will therefore not leave that value active for the benchmark. Experimental overrides must follow the final defrag, with a readback.
2. **The 1 GiB minimum is not a 1 GiB reservation solely on node 0.** Linux distributes minimum watermarks across memory zones. In the recorded normal preparation, populated node-0 zones had minimum watermarks of 38, 4197, and 7732 pages: **46.746 MiB combined**, before dynamic boosting and other reserve constraints. Most of the global minimum belongs to the much larger slow node. This setting cannot be treated as equivalent to the measured 1440 MiB PMEM metadata allocation on node 0. [Kernel watermark semantics](https://docs.kernel.org/admin-guide/sysctl/vm.html#min-free-kbytes), [recorded zoneinfo](../reproductions/bc-twitter-may2026/conditions/sessions/20260909T130001Z-142220/results/20260909T130001Z-142220/01-may07_arms/before.json).

The older setup comment about “shrinking” kernel reserves is misleading beside the March change from 16 MiB to 1024 MiB. Also, enabling THP can raise a previously lowered minimum, so a minimum-free experiment would need a final readback after THP preparation.

## Why ARMS is a credible sensitivity target

In the first full-preparation SSD pair, global direct-compaction counters increased by **60,813 stalls and 60,742 failures** during ARMS, versus zero during the model. THP collapse allocation failures increased by **28,315 versus zero**. These counters are host-wide, not per-process attribution, and no equivalent original May counters were found. They nevertheless motivate distinguishing compaction and huge-page behavior from simple disk read time.

ARMS huge-page migration destination allocation can enter direct reclaim/compaction independently of the THP `defrag` sysfs choice. Consequently neither reducing background compaction nor changing fault-time THP defrag would automatically eliminate migration-worker compaction. The [kernel path audit](../reproductions/bc-twitter-may2026/conditions/setup_kernel_paths.md) records the distinction.

## Controlled sensitivity runs

The isolated condition runner now supports `--knob compaction20` and `--knob khugepaged_scan10`. Each changes exactly one control after the final historical defrag, verifies its value, and restores the original host settings afterward. The second option keeps allocation-failure sleep at 1 ms, so it isolates scan sleep rather than replaying the entire temporary March 16 10/10 configuration.

Both experiments use the same intentional 4 GB 6.18 boot, bound PMEM driver, SSD graph, old ARMS source, and reconstructed May 7 model (`2_10_0.5`). They are sensitivity tests using values found in earlier history, not claims that May used those values. Results are retained in the [condition summary](../reproductions/bc-twitter-may2026/conditions/summary.csv).

| Runtime control | ARMS total | Model total | ARMS average iteration | Model average iteration |
|---|---:|---:|---:|---:|
| May/current requested values: proactiveness 80, scan sleep 0 | 103.63 s | 98.16 s | 6.34331 s | 5.82238 s |
| Only proactiveness reduced to 20 | 103.23 s | 98.52 s | 6.31776 s | 5.84393 s |
| Only khugepaged scan sleep raised to 10 ms | 99.98 s | 97.79 s | 5.98396 s | 5.80486 s |
| Return to requested values, ARMS confirmation | 104.85 s | Not rerun | 6.23062 s | Not rerun |

The proactiveness-20 pair retains a **4.71-second total gap**, compared with 5.47 seconds in the full-preparation reference and 15.677 seconds in the May archive. ARMS remains near its recent timings. Effective proactiveness was 20 both before and after each case, and restoration reported zero errors. This one pair does not recover the historical difference. [Raw results](../reproductions/bc-twitter-may2026/conditions/sessions/20260909T142436Z-200883/results/20260909T142436Z-200883/results.json).

Background counter deltas were lower under the override: during the model, daemon scanned-page deltas fell from 38,526 free / 2,322,432 migration scans to zero for both; during ARMS, substantial direct compaction remained (51,296 stalls, 51,273 failures). These global counters support the distinction between background and direct paths; one sequential pair does not establish that the override caused every counter change or provide per-process time attribution.

The 10 ms scan-sleep pair showed a larger ARMS change: **3.65 seconds faster in total**, including approximately 3.59 seconds across ten computation trials, while the model changed little. Its total gap is **2.19 seconds**. ARMS's global compaction-failure delta was 3268, versus 60742 in the original full-preparation pair, and its THP collapse allocation-failure delta was 459 versus 28315. Initial free DRAM was 2.473 GiB, close to the immediately preceding proactiveness-20 ARMS run's 2.477 GiB. Both scan-sleep readbacks were verified at 10 ms and restoration reported zero errors. [Raw scan-sleep results](../reproductions/bc-twitter-may2026/conditions/sessions/20260909T143041Z-209179/results/20260909T143041Z-209179/results.json).

Returning scan sleep to zero brought ARMS computation back toward its earlier level: **6.23062 seconds per iteration**, versus 5.98396 at 10 ms and the initial 6.34331 at zero. Global compaction failures returned to 49,678, and THP collapse allocation failures returned to 24,535. Startup free DRAM was 2.477 GiB. Its 104.85-second total includes a longer 42.124-second read, so the computation split is the useful comparison. [Return-to-zero confirmation](../reproductions/bc-twitter-may2026/conditions/sessions/20260909T143646Z-217337/results/20260909T143646Z-217337/results.json).

This partial reversal supports a scan-setting sensitivity of approximately **2.5–3.6 seconds over ten computation iterations** in these runs. It is consistent with aggressive huge-page collapse/compaction contributing to ARMS overhead, but it does not establish the original discrepancy: the recorded May and current setup both request zero scan sleep, and the altered value makes ARMS faster rather than restoring the archived slower result. These few sequential runs are not a precise effect-size estimate or proof of per-process time attribution.

All five new workload executions were accepted. The final verification confirms proactiveness 80, scan/alloc sleeps 0/1 ms, original perf limits, bound PMEM, the same boot, and no active benchmark. Every session reported zero restoration errors. [Final knob restoration check](../reproductions/bc-twitter-may2026/conditions/setup_knob_restoration_check.json).

## Inherited settings remain a separate category

Setup does not fix fast-socket uncore, the CPU governor/turbo policy, per-size THP enablement, khugepaged `max_ptes_*`, or `extfrag_threshold`. Current read-only checks show the 2 MiB THP size inherits the top-level setting, smaller sizes are disabled, `max_ptes_none/swap/shared=511/64/256`, `extfrag_threshold=500`, and CPUs 0/10 report the `powersave` governor with turbo permitted. The governor name alone does not establish actual frequencies or an historical slowdown. No original readbacks identify an intervening change in these controls.

Process and IRQ placement is also only partly enforced. The setup's `taskset -pc PID` operation pins each process's main thread rather than every existing thread. IRQ write failures are ignored, and the future-IRQ default assignment is commented out. Matching requests therefore do not guarantee matching interference from background threads or effective IRQ placement. These behaviors are unchanged from May.

These individual knobs remain plausible sources of sensitivity even though their requested May/current values match. An explanation of the original discrepancy would still require either a repeatable effect in the relevant direction plus historical-state evidence, or recovery of the original runtime configuration.
