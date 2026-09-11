# ARMS/model preparation call paths

Read-only audit, September 9, 2026. No scripts were executed, no benchmark was run, and no host setting was changed. [Manifest](manifest.json) records complete copied scripts and hashes for May 4 Git, May 14 Git, June's Git state, the current working tree, and the contemporaneous editor saves below. `diffs/` contains exact comparisons.

## Result

**The retained batch runners do not give ARMS and the model different THP or compaction settings.** Both call `run_measurement_setup(size)` without a policy argument. The helper substitutes `default` and explicitly passes that string to `setup.sh`; an inherited `MEASUREMENT_SYSTEM` does not change this choice. Both therefore use the same global settings and normally two defrag invocations before each workload/configuration.

There are three concrete execution differences worth distinguishing from setting-value changes: standalone ARMS does not prepare itself, historical inner defrag depended on the caller's working directory, and current failure propagation can leave a partially prepared run because callers ignore the returned failure. None is established as the cause of the archived batch gap.

## Actual order by cohort

| Evidence | Order for BC Twitter |
|---|---|
| May 4/7 standard runner, Git `8d6a8bc66` | Full setup → ARMS → model runner, which performs full setup before each configuration → teardown after the workload |
| May 7 cost sweep, [06:09 editor save](editor/may07_cost_model.sh:101) | For each of `0.125, 0.25, 0.5, 1.0, 1.5`: full setup → model workload |
| May 14 long runner, [07:44 editor save](editor/may14_long.sh:77) | Model runner with its full setup → full setup → ARMS → teardown |
| June 13/14 long cohort, [June 13 22:27 save](editor/june13_long.sh:77) | Same model → setup → ARMS order |
| June 22/23 long cohort, [June 22 03:52 save](editor/june22_long.sh:77) | Same model → setup → ARMS order |
| Current standard runner | Full setup → ARMS → model runner with full setup for every model configuration |
| Current long runner | Model runner with full setup → full setup → ARMS → teardown |

The three historical long-runner saves differ only in the size label, 4035/4040/4041. Their matching Git states also have identical `measurement_arms.sh`, `measurement_model.sh`, `measurement_common.sh`, `setup.sh`, `defrag.sh`, `unsetup.sh`, and BC workload files. The May 7 saved *model* script is contemporaneous; no surviving exact outer-runner save identifies its executed bytes, so outer order additionally relies on Git and the independently audited result timestamps. Those show ARMS preceding ascending costs with approximately 43–44 seconds between runs.

## What “full setup” does

For normal ARMS/model, May through June:

1. The common helper runs `sudo bash <absolute>/unsetup.sh`, ignoring failure. That kills matching Python/Jupyter processes and removes `memeater` if present. It does not reset THP or NUMA sysfs controls.
2. The helper runs `sudo bash <absolute>/setup.sh size default platform`.
3. Setup writes the VM/THP/perf settings, requests uncore settings, disables swap, sets existing IRQ affinities, and migrates existing processes to the slow tier while changing their main-thread affinity. Historically the `vfs_cache_pressure=2000` write follows process migration; current common settings apply it before that migration.
4. Setup invokes **inner** `bash defrag.sh`. Historically this filename is relative to the caller's current working directory.
5. After setup returns, the common helper invokes **outer** `bash <absolute>/defrag.sh`. The NOMAD/TPP branches are not selected because the argument is `default`.
6. The child runner launches the benchmark through the common `numactl`/`taskset`/`sudo env` wrapper.

Current normal ARMS/model retain that order. Common writes now come from `measurement_settings.sh`; setup invokes its inner defrag by absolute path. The current special MEMTIS early return and `all_numa` migration exception require explicit system arguments absent from these ARMS/model call sites.

Both defrag invocations force THP `enabled=always` and `defrag=always`, perform sync/cache drops, request explicit compaction, then repeat cache drops. **Any earlier experiment setting THP defrag differently is overwritten by the final defrag call.** Passing a system argument to current `defrag.sh` has no effect: the script does not read positional arguments. No model-specific THP write occurs between the final defrag and execution.

## Real asymmetries and failure paths

**Standalone entry points differ.** [measurement_arms.sh](current/measurement_arms.sh) directly launches its workload and never calls setup or defrag. [measurement_model.sh](current/measurement_model.sh:101) prepares before every model configuration. This behavior already existed in May. A manual ARMS run can therefore inherit previous settings while a subsequent model run resets them. For example, baseline preparation enables AutoNUMA, whereas normal setup disables it. This is relevant when comparing manual launches; it does not describe the archived batch callers, which explicitly prepare ARMS. The separate shell-history audit found concrete manual ARMS setting experiments in March, but that does not establish May/June use.

**Historical working-directory dependence affects inner defrag.** Running an absolute path to `run_all_measurements.sh` from outside the repository does not change the caller's working directory: the `cd` used to calculate `SCRIPT_DIR` runs in command substitution. In May setup, `bash defrag.sh` could consequently fail or select another file. The absolute outer defrag would still run, so this generally changes two preparation passes to one rather than skipping all cache drops. Current setup's absolute path removes that difference. It applies to both policies in a batch with the same working directory. No external-CWD launch has been established for the target cohorts.

**Current setup failure can skip preparation while measurement proceeds.** Current common settings stop at the first failed write; uncore writes also stop setup on failure. `run_measurement_setup` then returns before outer defrag. However, current standard ARMS [caller](current/run_all_measurements.sh:100), long ARMS [caller](current/run_all_measurements_long.sh:84), and model [caller](current/measurement_model.sh:101) do not check that return and have no top-level `set -e`. A failed setup can therefore be followed by a benchmark. Historical setup generally continued after failed writes, and the old common helper still attempted outer defrag. Current defrag itself has `set -e`; historical defrag did not. This is a real error-path change, not evidence that a failure occurred in the successful diagnostic runs.

**Teardown does not restore settings.** The historical standard runner and all compared long runners invoke teardown after the workload. Current standard runner omits that final generic teardown. Since teardown only kills selected processes/removes `memeater`, and each next ARMS/model preparation begins with the same unsetup, this change does not directly explain policy-specific THP values during successful batch runs. It also means a standalone run cannot assume the preceding runner restored default host controls.

## CPU, NUMA, environment, and workload handoff

The normal common launcher keeps the same C220G5 `NUMA_MEM_NODES=0,1`, `TASKSET_CPUS=0-9,20-29`, and explicit per-case library path. BC uses 16 OpenMP threads; standard/long workloads use 10/100 iterations respectively. Each child loads the explicitly supplied workload ID, so the long runner sourcing `measurement_workloads_long.sh` while its children source `measurement_workloads.sh` does not silently select ten iterations: both load the `bc-twitter.sg-long` workload file when given that ID.

Workload metadata passes graph path and virtual-step sample settings, not THP/compaction sysfs overrides. The same wrapper sets `WORKLOAD_VIRTUAL_STEP_SAMPLES` and `VIRTUAL_STEP_SAMPLES` for both libraries. Normal ARMS does not use model virtual features. Current `GRAPH_DIR` defaults and workload graph override changed storage selection for both policies; no ARMS/model-specific graph branch is present in this path.

The launch wrapper uses `sudo env` with explicit benchmark variables. Setup uses `sudo bash` without `-E`; successful unexported shell assignments cannot be assumed to propagate to children, and preserved environment depends on sudo policy. Both policies use the same mechanism. BC has no temporary runtime-input directory, so its launch does not change directories between preparation and execution.

These paths request settings; they do not preserve historical readbacks or guarantee identical physical fragmentation. The call-path evidence provides no new policy-specific setting change across the May-to-June transition.
