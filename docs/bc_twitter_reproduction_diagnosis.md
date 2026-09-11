# BC Twitter: historical model recovery and runtime diagnosis

Investigation date: September 9, 2026. This extends the [log/storage investigation](arms_model_storage_investigation.md) and [ARMS source audit](arms_source_history_audit.md) with recovered model provenance and isolated runtime experiments.

**Subsequent experiments:** the [historical-conditions follow-up](bc_twitter_conditions_followup.md) adds recovered May logs, complete historical preparation, the requested HDD cost ablation, an explicit perf-cap comparison, and a measured 1440 MiB PMEM metadata overhead. Those results extend the nine accepted runs documented here; the original cause remains unproven.

The evidence does not establish an ARMS code improvement that accounts for the original 15–18 second gap disappearing. Rebuilding the best available pre-benchmark ARMS source still runs substantially faster today than the archived ARMS measurements. The model weights and inference policy have changed, but reconstructing the older combination does not by itself restore the old gap. Storage explains much of the recent change in absolute totals. A shared disk change between the historical and current batches must be distinguished from an ARMS/model disk mismatch within a batch: slower loading could also affect policy decisions and page placement.

The user confirms that the current 4 GB OS boot is intentional, the historical runner did not switch disks between ARMS and model cases, and their model timings are broadly unchanged on comparable storage. These observations fit the same-boot cost-ablation evidence. Treat the historical ARMS/model sweep as sharing its input disk; the unresolved storage question is which disk/preparation it shared, and whether moving both systems to a different disk changed their relative performance. The free-memory measurements below document runtime headroom, not evidence that the machine was booted into the wrong capacity.

## Which historical result should be reproduced?

The final archive combines ARMS measurements from May 7 with model measurements from May 10. The best contemporaneous comparison is the May 7 sweep, whose original file timestamps fall within the same recorded 6.18 boot:

| Historical target | ARMS total | Model total | ARMS minus model |
|---|---:|---:|---:|
| May 7, three-run means, model `2_10_0.5` | 115.804 s | 100.127 s | 15.677 s |
| Selected final results, model `1_10_0.1` from May 10 | 115.804 s | 98.126 s | 17.678 s |

The original results and boot assignments are documented in [historical_sweeps.csv](arms_model_storage_evidence/historical_sweeps.csv) and [archive_provenance.csv](arms_model_storage_evidence/archive_provenance.csv). Same-boot assignments for May are timestamp/boot-ledger inferences, not embedded run metadata. Neither comparison retains read time, executable/library hashes, or effective free DRAM.

## The strongest surviving old model is recoverable from Git

The BC Twitter model under [models/old](../models/old/model_discounted_reward_99_bc-twitter.sg_l2.txt) is byte-identical to a versioned object and text model in the **separate** repository `/users/zimooo2/tiering_models`:

```text
25cd9ed:process_data/models/model_discounted_reward_99_bc-twitter.sg_l2.o
25cd9ed:process_data/models/model_discounted_reward_99_bc-twitter.sg_l2.txt
```

The matching local files have May 3 modification times, preceding both reference batches. This is the best surviving candidate; there is no per-run hash proving that these exact weights produced the archived numbers. The later Git commit preserves the candidate but does not independently date its training to May 3. [Verified Git object identities and hashes](../reproductions/bc-twitter-may2026/diagnostics/model_git_provenance.json).

| Candidate | Forest | Model age feature range | Provenance |
|---|---|---|---|
| May 3 | 128 trees, 26 leaves each | 0–219 | `models/old`, duplicate `models/models`, and training-repository backup path above |
| May 14 | 96 trees, up to 62 leaves | 0–100 | `models/with_age`, `models/no_age`, and training-repository top-level model path |
| June 18 | 96 trees, 31 leaves | 0–100 | Current top-level model captured for this investigation |

The BC Twitter `with_age` and `no_age` objects and text files are **identical despite their names**, and their May 14 timestamps are later than the original May 7/10 results. They are not the strongest original-model candidates. The current working library's forest predictions exactly matched the June 18 object on 128 diagnostic inputs and differed from both other candidates. All three candidates have been frozen in the [reproduction package](../reproductions/bc-twitter-may2026/README.md).

## Copying the old object alone is an incomplete restoration

Three changes affect the model path:

1. **Input age:** May 14 caps age at 100. The May 3 forest contains 234 age splits above 100, so the current feature extractor prevents that forest from using its original age distinctions.
2. **History and migration-cost scaling:** The May 7 configuration uses history mode 2 (adjusted EWMA), length 10, scaler 0.5, discount 99, and the older alpha-index default. Its migration cost also scales with `250 ms / observed virtual-step duration`. May 9 removes that time factor; the selected final/current configurations use mode 1, length 10, scaler 0.1.
3. **Migration eligibility:** May 9 restricts model promotion to the hottest half of the nominal DRAM-sized ranking and demotion to the colder half of the slow-tier-sized ranking. The May 4 implementation lacks those model eligibility restrictions.

These differences are preserved in the [frozen May 4 source](../reproductions/bc-twitter-may2026/source/may04), [May 9 source](../reproductions/bc-twitter-may2026/source/may09), and [current source](../reproductions/bc-twitter-may2026/source/current). Source and model hashes, build configurations, and compiler identity are in [manifest.json](../reproductions/bc-twitter-may2026/manifest.json).

An offline probe sampled 20,480 rows from a retained May 3 BC Twitter training trace. Clipping age changed the old forest's prediction on **10,514 of 10,878 sampled rows with age above 100 (96.65%)**. This establishes a real input/model compatibility difference, not a benchmark-speed explanation. Exact identity with the model's training dataset is unproven: several feature extrema differ, which could reflect training filtering/subsampling or a different dataset. The diagnostic is neither held-out accuracy nor a full policy simulation. [Probe results](../reproductions/bc-twitter-may2026/diagnostics/model_probe.json), [feature-range comparison](../reproductions/bc-twitter-may2026/diagnostics/training_feature_ranges.json).

## What changed in ARMS?

The source audit found no new ARMS hotness formula, sampling period, migration-worker count, or fixed migration-cost multiplier. Freshly built PEBS, pagemap, migration-worker, policy-thread, group, and timer objects have matching instructions and relocations across the audited versions with normal ARMS flags.

The relevant policy change is the May 9 addition of `can_demote` to cold-page selection, followed by its May 14 initialization. Removing an initializer leaves an uninitialized field; it does not reproduce May 4, which had no such field/check. The current snapshot deliberately preserves the user's commented initializer. The runtime comparison therefore tests that actual current experiment, not a repaired current implementation.

Build dependency fixes could have eliminated stale objects, and setup's changed perf-limit write ordering could affect whether the requested sampling limit was applied. Neither has historical binary/settings evidence establishing a causal role. See the [source audit](arms_source_history_audit.md) for the changes and exclusions.

## Same-boot runtime experiment

All cases use the current `6.18.1-061801-generic` boot, the same explicitly verified SSD graph, the same GAPBS executable, 16 OpenMP threads, CPUs `0-9,20-29`, and NUMA nodes `0,1`. Sources were rebuilt with the same installed GCC 15.1. Graph cache eviction is requested before every case, terminal output is captured, and an interference guard rejects overlapping benchmarks. The first guard-debugging attempt is marked invalid and excluded.

| First pass | Total | Read | Average of 10 BC iterations |
|---|---:|---:|---:|
| May 4 ARMS source | 103.16 s | 38.91169 s | 6.38791 s |
| May 4 source + May 3 model, `2_10_0.5` | 97.58 s | 38.78153 s | 5.85869 s |
| Current ARMS source | 100.10 s | 38.88172 s | 6.08579 s |
| Current source + June 18 model, `1_10_0.1` | 96.83 s | 38.88534 s | 5.77381 s |

[First-pass results and per-case logs](../reproductions/bc-twitter-may2026/results/20260909T122951Z-129527/results.json).

A second check reversed ARMS order: current ARMS took **101.74 s** (read 39.03869 s, average iteration 6.23145 s), then May 4 ARMS took **102.10 s** (read 38.93615 s, average iteration 6.28229 s). The old/current difference was 3.06 seconds in the first pass and only 0.36 seconds in the reversed pass. Across the two runs, old ARMS averaged 102.63 seconds and current ARMS 100.92 seconds. That small, variable difference does not explain archived ARMS averaging 115.804 seconds. It also does not establish zero source effect: this is only two repetitions with slightly different initial free memory. [Second-pass results](../reproductions/bc-twitter-may2026/results/20260909T123745Z-132387/results.json).

The May 10 final-model candidate—May 9 source, May 3 forest, `1_10_0.1`—then completed in **96.59 seconds**, with read time 38.90882 seconds and average iteration 5.74622 seconds. That is approximately 1.54 seconds faster than its 98.126-second archive mean. Across these tests, the reconstructed/current models remain broadly near the original model totals, while even reconstructed ARMS is approximately 13 seconds faster than its archive mean. Restoring the candidate model is possible; restoring the full original ARMS/model difference has not been demonstrated.

All seven accepted runs returned success with ten trial timings and no detected benchmark overlap. The result directories contain each run's actual source/model manifest, command, input hashes, before/after host snapshots, terminal output, and late process memory mappings.

These are current-host experiments, not exact historical reproductions. The runner reuses host settings and evicts only the graph's cache; it does not run the global setup/compaction scripts before each case. Node 0 has 4,737,988 KiB total, but initial free memory was approximately 2.4–2.5 GiB in this pass. The startup `DRAM size` line actually reports free memory. The scripts' MiB filenames do not impose that amount of usable DRAM: their memory-eater step was commented out in both audited old and current scripts. Historical effective free DRAM is unknown.

The four accepted ARMS runs accumulate approximately 52,000–61,000 global `compact_fail` events each; the first-pass historical model has zero and the current model approximately 7,900. Direct-reclaim scans also vary substantially between ARMS repetitions. These are host-wide deltas, not per-process attribution, but they support memory pressure/placement as a relevant variable in these otherwise isolated runs. No comparable counters survive for the original ARMS/model pair, so this cannot identify the historical cause. [Derived runtime and counter summary](../reproductions/bc-twitter-may2026/diagnostics/runtime_comparison.json).

## Why shared HDD versus shared SSD remains a separate hypothesis

The historical cost ablation and unchanged within-sweep graph selection argue against ARMS reading one disk while the model reads another. They do not exclude both reading HDD historically and both reading SSD today, with a different policy benefit under the slower-loading conditions.

GAPBS [ReadSerializedGraph](/users/zimooo2/gapbs/src/reader.h:254) allocates graph arrays, reads adjacency data, and generates forward/reverse indexes inside the reported read interval. The tiering library is already active. A different disk can therefore change loading-related memory pressure and the placement presented to BC iterations, not just add a constant I/O delay. This is a mechanism worth testing, not a demonstrated explanation. A paired HDD experiment uses the same reconstructed May source/model binaries and the same intentional 4 GB boot as the SSD experiments above.

The full HDD graph hash matches the SSD graph exactly (`4b7e5193a8e3384b8319359dc8c92534b8aee8e48d6c3bd0820451bb307d6eea`), and the BC executable hash matches too. The HDD path is backed by `/dev/sdb`; this comparison changes the backing device while retaining input contents. [HDD input identity](../reproductions/bc-twitter-may2026/results/20260909T124445Z-135186/inputs.json).

Both HDD cases completed successfully, with ten trials and no detected benchmark overlap:

| Reconstructed May 7 case | SSD total, first pass | HDD total | SSD read | HDD read | SSD average iteration | HDD average iteration |
|---|---:|---:|---:|---:|---:|---:|
| ARMS | 103.16 s | 129.19 s | 38.91169 s | 66.80747 s | 6.38791 s | 6.20127 s |
| Model `2_10_0.5` | 97.58 s | 122.92 s | 38.78153 s | 64.35975 s | 5.85869 s | 5.83555 s |
| ARMS minus model total | **5.58 s** | **6.27 s** | | | | |

Changing the shared disk added **26.03 seconds to ARMS and 25.34 seconds to the model**, increasing their gap by just 0.69 seconds in these passes. BC computation did not slow on HDD. This does not reproduce the historical **15.677-second** gap, and the HDD model total is substantially slower than its historical 100.127-second mean. These are one HDD pass and limited SSD repetitions; they establish what this particular cold-cache preparation and host state did, not the exact storage/cache conditions of May. [HDD results and logs](../reproductions/bc-twitter-may2026/results/20260909T124445Z-135186/results.json).

The result supports the user's observation that model absolute speed is broadly unchanged on comparable storage. Neither restoring the available old source/model combination nor moving both reconstructed systems to today's HDD restores the much slower historical ARMS result. The exact historical difference therefore remains unresolved; a large demonstrated ARMS code improvement or a simple shared-device substitution is not supported by these experiments.

## Remaining differences that prevent exact reproduction

- **Memory availability and placement:** ARMS dynamically uses node-0 free memory and a reserve. Original labels are insufficient to reconstruct this. The historical ARMS slowdown is concentrated at the smallest capacity labels; the 6–10 GB labeled archived runs already take approximately 97–99 seconds. This makes effective memory/placement an important unresolved variable, not a proven cause.
- **Graph provenance:** The current HDD graph has an August 21 modification time, later than the May runs; the SSD copy was made in September. This does not prove graph contents changed. No May graph hash was found to prove identity either. The current input is hashed in every experiment's `inputs.json`.
- **Original binary and host settings:** The exact historical library, compiler version, effective perf limit, uncore readbacks, and complete preparation state are not retained. The BC executable itself has a March 5 timestamp and no identified intervening BC source change; its current hash is recorded.
- **Storage:** Recent same-boot observations show a roughly 32–34 second loading-related shift for both systems when switching HDD to SSD. They do not restore the old model advantage on HDD. Historical backing-device records are unavailable; a path string alone cannot establish May's disk.

Current file timestamps, devices, boot ID, and read-only uncore MSR values are saved in [host_provenance.json](../reproductions/bc-twitter-may2026/diagnostics/host_provenance.json). The current MSR readbacks are `0xc14` on CPU 0 and `0x707` on CPU 10; there are no corresponding May readbacks establishing a difference.

For faithful reconstruction, use the frozen May 4 ARMS and May 4/May 3 model combination for the contemporaneous May 7 target; use the May 9/May 3 `1_10_0.1` candidate for the selected final model. Preserve the same graph hash, preparation, actual free DRAM, settings, and separate read/iteration timings across comparisons. The package provides these isolated libraries and unique output directories. Changing settings solely until the old totals appear would not establish that the original conditions were recovered.
