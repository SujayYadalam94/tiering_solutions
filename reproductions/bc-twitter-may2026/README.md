# BC Twitter historical reproduction

This directory freezes candidate May source/model combinations alongside the working source and default model captured on September 9. It does not replace the main workspace libraries.

The [diagnosis and measured results](../../docs/bc_twitter_reproduction_diagnosis.md) explain what these reconstructions recover and why they do not yet reproduce the entire archived ARMS/model gap.

The strongest historical target is the May 7 same-boot comparison at the 4009 MiB label: ARMS 115.804 s versus model `2_10_0.5` 100.127 s, each averaged over three runs. The model results selected in `final` instead use `1_10_0.1` and originate from May 10.

Seven accepted September 9 diagnostic runs are saved under `results/20260909T122951Z-129527` and `results/20260909T123745Z-132387`. Reconstructed ARMS took 103.16/102.10 seconds; current ARMS 100.10/101.74 seconds. The reconstructed May 7 model took 97.58 seconds, the May 10 candidate 96.59 seconds, and the current model 96.83 seconds. All read times were approximately 38.8–39.0 seconds. These tests recover model totals broadly close to the originals but do not recover the much slower archived ARMS totals. The diagnosis explains the remaining host/capacity/input-provenance limitations.

A subsequent pair in `results/20260909T124445Z-135186` uses the same reconstructed May 7 binaries and byte-identical graph on HDD. ARMS took 129.19 seconds (read 66.80747), and the model 122.92 seconds (read 64.35975). Their gap was 6.27 seconds, versus 5.58 seconds in the first SSD pass and 15.677 seconds historically. Both HDD runs were accepted, bringing the completed comparison to nine runs. Changing the shared disk did not restore the old relative advantage under today's host/preparation conditions. The current 4 GB OS boot is intentional.

## Models and provenance

| Model snapshot | Surviving evidence | Forest |
|---|---|---|
| `models/may03` | Matches `models/old` and Git revision `25cd9ed`, path `process_data/models/model_discounted_reward_99_bc-twitter.sg_l2.{o,txt}`, in `/users/zimooo2/tiering_models` | 128 trees, 26 leaves each; age training range 0–219 |
| `models/may14` | Matches `models/with_age`, `models/no_age`, and training-repository revision `25cd9ed`, path `process_data/model_discounted_reward_99_bc-twitter.sg_l2.{o,txt}` | 96 trees, up to 62 leaves; age range 0–100 |
| `models/june18` | Copy of the current top-level model at capture time; current library's forest predictions matched this candidate on the diagnostic inputs | 96 trees, 31 leaves; age range 0–100 |

The May 3 object is the best surviving candidate for May 7 and May 10. There is no per-run model hash establishing its use conclusively. The May 14 object is later than both reference batches. Despite their directory names, the `with_age` and `no_age` BC Twitter objects and model text files are byte-identical.

The old model has 234 age splits above 100. Today's feature extraction clips age at 100; replacing only the object leaves that mismatch in place. The sampled prediction diagnostic is saved in `diagnostics/model_probe.json`, together with its script and input/output sample. It measures prediction changes on a retained training trace, not held-out accuracy or benchmark speed.

The retained trace's exact identity with the forest's training dataset is not established: several feature ranges differ, as recorded in `diagnostics/training_feature_ranges.json`. The trace remains useful for demonstrating the effect of the age cap. Byte identity of the recovered model files with their Git objects is independently recorded in `diagnostics/model_git_provenance.json`.

## Cases

| Case | Source | Model/configuration | Purpose |
|---|---|---|---|
| `may07_arms` | `8d6a8bc66` (May 4) | ARMS, C220G5 | Candidate ARMS source before May 7 results |
| `may07_model` | `8d6a8bc66` | May 3 forest, `2_10_0.5`, discount 99 | Closest available reconstruction of the contemporaneous historical model |
| `may10_model_candidate` | `41e8adedd` (May 9) | May 3 forest, `1_10_0.1`, discount 99 | Candidate for the model results selected in final |
| `current_arms` | Frozen working tree | ARMS, C220G5 | Current source comparison |
| `current_model` | Frozen working tree | June 18 forest, `1_10_0.1`, discount 99 | Current default model comparison |
| `current_code_may07_model` | Frozen working tree | May 3 forest, `2_10_0.5` | Holds weights/configuration constant while changing source; includes newer age cap |
| `may07_code_june_model` | `8d6a8bc66` | June 18 forest, `2_10_0.5` | Holds old source/configuration constant while changing weights |

The frozen working tree preserves the existing experiment with `can_demote` initialization commented out. It is recorded exactly; no working source was repaired or overwritten. This is an uninitialized-field experiment, not equivalent to the May 4 implementation, which has no `can_demote` field/check.

The May 4 source includes unbounded age extraction, adjusted EWMA model history (alpha index 3), virtual-step-time scaling of model migration costs, and the older model migration eligibility rules. The May 9 source changes history defaults, removes time scaling from the model cost, and restricts model promotion/demotion eligibility. These commits bracket the historical runs; uncommitted edits and the original binary/compiler flags are not recorded with the old measurements.

## Run

From the workspace root, check the frozen libraries and host without starting a benchmark:

```bash
python3 reproductions/bc-twitter-may2026/run.py
```

Run the four primary cases on an otherwise idle 6.18.1-061801-generic host:

```bash
sudo python3 reproductions/bc-twitter-may2026/run.py --run
```

For two repetitions with reversed case order in the second repetition:

```bash
sudo python3 reproductions/bc-twitter-may2026/run.py --run --repetitions 2
```

Select individual diagnostic cases with `--cases`, or choose the HDD copy explicitly with `--graph /users/zimooo2/gapbs/benchmark/graphs/twitter.sg`.

The runner:

- Checks library hashes, kernel release, and existing BC/PR benchmark processes.
- Uses the same graph file, BC executable, 16 OpenMP threads, CPUs `0-9,20-29`, and NUMA nodes `0,1` for all cases.
- Hashes the input graph and executable, then evicts only that graph's file cache before each run.
- Captures terminal output through a pseudo-terminal so GAPBS read, trial, and average times survive normal ARMS shutdown.
- Records boot ID, memory/VM/perf settings, NUMA maps, loaded mappings, exact command, and result status in unique directories under `results`.
- Stops its own comparison if another BC/PR benchmark starts. The guard was checked with dummy processes to verify that its own child is excluded and an independent process is detected.
- Requires ten trial timings and successful completion before accepting an attempt.

**This compares source and models under today's host settings.** It does not run the repository's global setup scripts, migrate other processes, reset all caches, change the kernel, or impose the historical MiB filename as a memory limit. Those differences are recorded limitations, because actual historical free DRAM and mount readbacks are missing. Do not describe a matching runtime as exact reproduction without matching the remaining conditions.

`manifest.json` records source and model hashes, model provenance, compiler, and library hashes. To rebuild the frozen sources with the installed compiler, run `python3 reproductions/bc-twitter-may2026/build.py`.

The first attempt in `results/20260909T122720Z-128588` is invalid: an initial guard bug classified timeout's separate process group as foreign. Its processes were terminated, no timing was accepted, and the runner was corrected and checked before subsequent attempts.
