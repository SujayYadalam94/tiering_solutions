# Earlier change-point search

September 9, 2026 follow-up. No benchmark, host-setting change, graph read/copy, reboot, or working benchmark-library rebuild was performed. Two isolated `model.cpp` objects were compiled under this evidence directory to test whether a dated model edit changes normal ARMS code generation.

## Narrowest supported interval

For the same **BC Twitter 100-trial workload, nominal 4 GB configuration, and 6.18 kernel**, the missing-gap behavior is present by the June 13–14 cohort. The closest preceding comparable slow cohort is May 14.

| Cohort | ARMS mean, seconds | Model mean, seconds | ARMS minus model |
|---|---:|---:|---:|
| May 12, 4032 | 711.096 | 632.746 | 78.350 s |
| May 14, 4035 | 808.557 | 744.401 | 64.156 s |
| June 13–14, 4040 | 625.315 | 614.238 | 11.077 s |
| June 22–23, 4041 | 628.544 | 619.911 | 8.633 s |

Each mean contains three successful runs. [Exact files, original timestamps, hashes, and calculations](matched_long_cohorts.json).

**Duration/loading qualification, September 10:** all four rows above use 100 trials, so the reduction in the absolute gap from 64–78 seconds to 9–11 seconds is not merely percentage dilution from comparing ten trials with one hundred. However, these are total times: missing read/iteration splits prevent attributing that reduction to computation. A fixed loading advantage becomes a smaller percentage as trial count increases while retaining roughly the same absolute seconds, assuming unchanged computation. In particular, the May 7 best-cost advantage of 15.677 seconds over ten trials and June's roughly 9–11 seconds over one hundred trials do not independently prove that the original short-run mechanism disappeared by June; the configurations also differ. The May–June long-run and May–August short-run observation windows must remain separate unless evidence connects their causes. Rechecked all 24 original timing hashes/totals and the matching May/June `-n 100` workload definitions for this qualification.

The last slow ARMS execution ended **May 14 at 09:06:50.839 UTC**. The first fast ARMS execution ran approximately **June 13 at 23:59:08.519 through June 14 at 00:09:29.783 UTC**. This brackets an **observed performance transition**, not proof that one persistent change happened at an exact moment between them. No comparable observation was recovered inside that interval.

The commands use `OMP_NUM_THREADS=16`, `-n 100`, and the same named Twitter graph path. Filename size labels are not precise physical-memory measurements. Model names/configuration match, but the first June cohort has a real model-adapter change described below; historical graph and loaded-library hashes are missing. These are therefore the closest documented cohorts, not a fully controlled historical experiment.

The separate standard **10-trial** 4 GB series still has no successful surviving ARMS result between May 7 and August 27. Its earliest confirmed small HybridTier gap remains August 27. The long series supplies earlier evidence, but its totals must not be mixed with standard-workload totals.

## New search beyond the original inventory

- Searched the reachable Git history, including overwritten/deleted BC Twitter/Kron timing versions: **918 timing paths, 477 distinct timing blobs, 1,394 references** in the original-workload 4 GB query. [Parsed rows](historical_4gb_git_rows.json), [raw Git output](git_timing_changes.raw).
- Found **29 historical timing blobs absent from all current `.time` files**. The latest newly recovered C220G5 runs were committed May 3 local time / May 4 UTC, and remain slow: BC Twitter ARMS **119.870 s**, selected model cost 0.5 **102.086 s**. The other missing results are older, failed, or from another machine. **None supplies a May 15–June 13 bridge.** [Recovered-version inventory](not_in_current_time_files.json). A commit timestamp is an existence bound, not the lost execution timestamp.
- Expanded the history query to wrapped/general-model workload names. Its **12 additional references** are the already-known 4053 general-model and 4050 swapped-model files; they add no intervening 4 GB ARMS run. [Additional paths](general_model_additional_git_changes.json).
- Inspected the ZIP central directories. `times.zip` contains **60 relevant 4 GB ARMS/model BC timing files**, and every one matches a current timing file. It supplies no missing run. `fixed_logs.zip` contains June 6 training Parquet data, not ordinary paired 4 GB timing logs. [Archive inventory](archive_inventory.json), [timing hash comparison](zip_4gb_comparison.json).
- Inspected saved notebook textual outputs, including **42 snapshots with relevant ARMS/BC output**. No output recovers an intermediate 4036–4039 cohort or earlier 4040 timing. Notebook save times were not used to date the displayed measurements. [Extracted outputs](notebook_timing_outputs.json), [bridge-label search](notebook_bridge_matches.json).
- Inventoried all retained editor activity in the bounded interval. May 30's short runs use one BC trial and different labels; June 6's standard-looking files are training runs. Neither is a matched 4 GB ARMS/model comparison. [Interval editor activity](interval_editor_activity.json).

These searches strengthen the boundary; they do not move it to a particular day within the approximately one-month gap.

## Changes immediately before the first fast June cohort

| June 13 event, UTC | What it establishes |
|---|---|
| **22:06:44.012**: `model.cpp` changes feature count 12 → 11 and removes age | A real uncommitted model-adapter change. It can alter model predictions and invalidates an assumption that the first June model implementation is identical to May. |
| **22:10:24**: reboot into `6.18.1-061801-generic` | The first fast cohort starts in a new boot. The prior retained boot, from May 31, was HybridTier. No May-versus-June command-line or prelaunch allocator snapshot was recovered to identify a specific changed boot condition. |
| **22:26–22:27**: long workload list enables the full suite; runner selects 4040 | Actual launch context changes from BC Twitter alone in the May 14 batch to BC/PR Kron before BC Twitter. |
| **23:59:08.519**, inferred | First successful fast ARMS BC Twitter execution begins. |

[Dated editor records](june13_specific_events.json), [boot ledger](../cross_kernel_transition/boots.json), [copied launch scripts and exact diffs](launch_sources/).

The May/June **outer runner snapshots differ only in the size label**. Both prepare each policy. The separately sourced **workload list** really changes. The first June BC pair follows approximately **73.7 minutes** of BC-Kron model → ARMS and PR-Kron model → ARMS, each with preparation. May 14 selects only BC Twitter. Original result order corroborates the saved lists. [Previously verified per-run predecessor chronology](../arms_version_provenance/may_memory_cleanup/long_prelude.md).

This can affect allocator layout, cache/reclaim history, or migration conditions consistently across repetitions. It is not evidence that the graph was different between policies. It is also **not a proven universal explanation**: slow May 7 already had preceding workloads, and the latest BC-Kron result also loses its gap. The exact predecessor and run duration matter; a simple claim that “running first” or “running after Kron” causes the difference is unsupported.

## The dated model edit does not directly change normal ARMS

Sixteen rechecked ARMS/source/build/setup/workload files have identical Git blobs at the May 14 and June 13 HEADs. The broader saved-source audit also found matching nearest local ARMS saves. [Rechecked hashes](interval_source_hashes.json), [earlier complete source audit](../arms_version_provenance/root_cause_code/README.md).

To test the only contemporaneous C++ edit more directly, compiled May 14 `model.cpp` and the saved June 13 version using:

```text
GCC 15.1.0
-std=c++17 -Wall -Wextra -pthread -O3 -DNDEBUG -fPIC -DC220G5 -DUSE_MODEL=false
```

Both compilations used the same recovered May 14 headers and the same source basename. Both succeeded and produced **byte-identical 12,784-byte object files**, SHA-256:

```text
e6b739a28e46c6a17a6d6bbf2b02a59a9199efe7253fc0d8656a63e6fdec074a
```

[Commands, source/object hashes, and compiler result](model_compile_check/results.json). This rules out a direct normal-ARMS code-generation effect of that model edit under these flags. It does not prove the exact historical shared objects loaded, and does not rule out indirect effects of a preceding model execution on machine state.

The age adapter was restored June 18, before the second fast June cohort, which further weakens that temporary edit as the sole explanation. No persistent ARMS implementation change recovered in this interval fits the timing transition.

## What is established, and what still needs a causal test

The earlier loss of the gap is established **by June 13–14 on 6.18**, before the August HybridTier runs, August 31 MEMTIS installation, or September experiments. Searching recovered timing versions and archives did not locate an intermediate comparable run. The evidence does **not** identify an exact causal commit or VM setting.

The concrete historically supported launch difference left to isolate is **BC-only model → ARMS versus the documented four-Kron prefix followed by the same BC pair**, on 6.18 with the same frozen code, model, graph, and preparation. Holding the model fixed would isolate the predecessor effect; recreating the June adapter is a separate comparison. Record the graph read/trial split and node/zone/THP state at each launch and after loading. An effect must reproduce the missing gap, not merely change one total.

That comparison was not run here. The current HybridTier boot cannot be treated as a reproduction of the matched historical 6.18 cohorts. Without this test or additional contemporaneous evidence, naming the predecessor sequence, disk fullness, or an unspecified reboot change as the root cause would overstate the result.
