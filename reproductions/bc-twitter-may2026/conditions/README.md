# Historical setup and cost-ablation experiments

These experiments extend the source/model/storage comparisons with the preparation operations used by the historical runner. They use the intentional 4 GB `6.18.1-061801-generic` boot and keep all results separate from normal benchmark output.

See the [completed investigation](../../../docs/bc_twitter_conditions_followup.md), [HDD cost analysis](ablation_builds/results_analysis.md), and machine-readable [summary.csv](summary.csv). The summary retains invalid attempts explicitly; the April library's failed launch is not timing evidence.

## Historical conditions recovered from Git

`may04_setup` contains exact files from `8d6a8bc66`. The normal setup values were unchanged from April 9 through the May benchmarks and into September. The historical runner called `setup.sh` and then `defrag.sh`; `setup.sh` itself also called `defrag.sh`. Each case therefore received two explicit compactions and repeated global cache drops. Model sweeps repeated that preparation for every cost setting.

| Control | Historical value |
|---|---|
| BC CPU affinity / OpenMP threads | `0-9,20-29` / 16 |
| BC memory nodes / virtual step | `0,1` / 10000 |
| Other processes | Attempt NUMA node 0 → 1 memory migration and set each PID's main-thread CPU affinity to `10-19,30-39` |
| IRQ affinity | Attempt `10-19,30-39` for each existing IRQ |
| Slow-tier uncore | MSR `0x620=0x707` on CPUs `10-19,30-39` |
| THP | Enabled always, defrag always, shmem force |
| khugepaged | Scan 8192 pages, scan sleep 0 ms, allocation sleep 1 ms, defrag 1 |
| Reclaim | Minimum free 1048576 KiB, watermark scale 10, boost 10000, VFS cache pressure 2000 |
| Compaction | Proactiveness 80, plus two explicit compactions per case |
| Kernel migration | AutoNUMA, zone reclaim, generic demotion, MGLRU disabled |
| Perf requested values | Maximum sample rate 1000000, CPU-time percentage 0 |

The one relevant writer change is perf ordering. The historical script attempts to write the rate while percentage may already be zero, which can fail and retain the existing rate. The current writer temporarily sets percentage 25 before writing the rate, then restores percentage zero. A failing historical write does not by itself show that May had a different effective rate. Preparation logs and readbacks retain what happened in these experiments.

## Runner and restoration

`run_conditions.py` imports a separate copy of the original diagnostic runner, adding a preparation hook. It snapshots host control values, uncore MSRs, IRQ affinities, and live process main-thread affinities. After the session it restores these values, checking process identity before affinity restoration. Before/after memory, VM, buddy, zone, and process metadata is retained for every case.

The historical `unsetup.sh` is preserved as evidence but **not executed**: its broad `pkill python` and Jupyter cleanup could terminate unrelated work and the diagnostic runner. The runner requires no active benchmark, no memory-eater module, and no active swap before applying setup. It performs the historical main-thread affinity operation, not an expanded all-thread version. Memory migration and cache eviction are intentional preparation effects and are not undone by restoring control values.

Default invocation checks the selected case libraries and host without applying preparation or running BC. Add `--run` under sudo to execute. For example, compare reconstructed May ARMS/model with the retained older ARMS binary on SSD:

```bash
sudo python3 reproductions/bc-twitter-may2026/conditions/run_conditions.py \
  --preset may04_full \
  --manifest reproductions/bc-twitter-may2026/conditions/source_comparison_manifest.json \
  --run --cases may07_arms may07_model retained_april_arms
```

Run the historical HDD cost ablation:

```bash
sudo python3 reproductions/bc-twitter-may2026/conditions/run_conditions.py \
  --preset may04_full \
  --manifest reproductions/bc-twitter-may2026/conditions/ablation_builds/manifest.json \
  --graph /users/zimooo2/gapbs/benchmark/graphs/twitter.sg \
  --run --cases may07_arms may07_cost_0p125 may07_cost_0p25 \
  may07_cost_0p5 may07_cost_1p0 may07_cost_1p5
```

The model cases use the May 3 forest, May 4 source, history mode 2, length 10, discount 99, and scaler values from the historical sweep. The model's discount/time scaling means its scaler is not numerically equivalent to the ARMS fixed cost multiplier. See [ablation build provenance](ablation_builds/README.md) and [retained binary provenance](legacy_binary/README.md).

Each session writes a unique manifest and logs under `sessions`, including `host_controls_before.json` and `restoration.json`. Benchmark results are accepted only with successful exit, ten trial timings, read/average timings, and no detected overlap. Source revisions are candidate reconstructions; no historical per-run hash identifies the exact original executable.

## Bounded condition controls

`--perf-rate 100000` installs that cap after preparation and restores the original value at session exit. This tests an inherited setting; it does not claim to recover the May value.

`--knob compaction20` changes only background compaction proactiveness to 20; `--knob khugepaged_scan10` changes only khugepaged scan sleep to 10 ms. Both require full historical preparation, apply their override after the final defrag, verify it, and restore it on exit. The values occur in pre-May history; these tests isolate sensitivity and do not establish May's effective settings. See [individual setup-knob audit](../../../docs/bc_twitter_setup_knobs.md).

`--release-pmem-metadata` verifies that the known 90 GiB `namespace7.0` is unused, temporarily unbinds it from `nd_pmem`, and rebinds it in `finally` using the stable driver path. It changes no namespace format or stored data. Its `pmem_detach.json` and `pmem_restore.json` record both transitions. This option is specific to the audited host and refuses unexpected namespace sizes, bindings, or use. It is an isolation experiment, not a proposed normal benchmark default.

The measured detach released exactly 1440 MiB of conventional page metadata on node 0 without changing node MemTotal or the boot. In the first pair, ARMS computation became faster while the model changed little. Historical PMEM binding is unknown. Restoration reports must be checked for both the driver and host controls.

Run `python3 reproductions/bc-twitter-may2026/conditions/summarize.py` to regenerate summaries from finished attempts. Per-session manifests retain graph/library identity, condition choices, and runner/setup hashes for the newer sessions.
