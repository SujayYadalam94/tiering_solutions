# ARMS, model performance, and graph storage

Follow-up: [historical model recovery and runtime diagnosis](bc_twitter_reproduction_diagnosis.md) adds nine completed isolated runs, including the reconstructed May ARMS/model pair on both HDD and SSD. It explicitly tests a shared disk change between batches, consistent with the user's confirmation that the runner did not switch disks within a sweep. This earlier log audit itself did not launch benchmarks.

The available evidence argues against HDD-versus-SSD placement explaining the entire historical BC Twitter advantage of the model over ARMS. Storage clearly causes a large shift in total runtime: in one September 9 boot, moving to the SSD graph path reduced observed ARMS and model session durations by approximately 34 and 32 seconds respectively. However, historical runs consistent with the same May 7 boot retain a roughly 16-second model advantage, and that advantage changes systematically with the model configuration. A separate May 4 batch shows a similar pattern.[^1][^2]

The strongest conclusion is that two effects coexist: a large loading-related effect associated with graph location in the recent runs, and a historical configuration- or memory-placement-dependent difference between ARMS and the model. The records do not isolate the precise cause of the latter, or prove the physical storage device used in May. They also do not justify treating total runtime differences as differences in BC computation alone.

## Evidence and comparison rules

The timing inventory contains 7,521 parseable records across `times` and `collected_logs`. The filesystem contains 7,418 `.time` files under `times` and another 123 under `collected_logs`; empty or otherwise unparseable files account for the difference. All 2,175 ordinary text logs were read: 1,953 under `times`, 135 under `logs`, and 87 under `collected_logs`. Metadata inspection covered 347 training traces, of which 343 had readable Parquet footers; none of those schemas exposed boot, mount, device, kernel, or timestamp fields. Additional evidence includes archived ZIP metadata, original result directories, Git history, retained sudo/authentication logs, journal boot IDs, and the historical reboot ledger.[^3]

Evidence has different strengths:

1. **Recent journal records:** directly record the launch command, graph path, library path, timestamp, process ID, and boot ID. Matching sudo session-close events recover durations even after timing files have been overwritten. Session durations are close to, but are not identical to, the benchmark wrapper's `real` times. A session close by itself does not establish successful benchmark completion.
2. **Original historical result files:** retain staggered modification timestamps and measured durations. These timestamps can be checked against the reboot ledger and the execution sequence. They provide strong circumstantial evidence of contemporaneous runs, but do not embed boot IDs.
3. **Final archive timestamps:** frequently describe copying or archiving. They must not be used as benchmark execution times.
4. **Directory and filename labels:** identify intended kernels, capacities, and configurations. They do not establish the exact binary, effective memory allocation, or historical backing device.

All timestamps below are UTC. Failed, interrupted, and overlapping recent attempts are excluded from primary comparisons. Different model configurations are labeled explicitly. Identical copies of archived results are not counted as additional repetitions.

## The final archive combines different historical sweeps

The three final ARMS results at the 4009 MiB label are byte-identical to files in `times/c220g5/arms-6.18_new/bc-twitter.sg`. Those original copies retain the following timestamps. The selected final model results have matching originals in `times/c220g5/model_6.18_new/bc-twitter.sg`.[^4]

| Selected archive | Run | Total, seconds | Original file timestamp |
|---|---:|---:|---|
| ARMS, 4009 MiB | 1 | 112.781 | May 7, 09:07:49 |
| ARMS, 4009 MiB | 2 | 118.355 | May 7, 12:54:13 |
| ARMS, 4009 MiB | 3 | 116.275 | May 7, 16:40:42 |
| Model, 4028 MiB, `1_10_0.1` | 1 | 97.911 | May 10, 02:44:03 |
| Model, 4028 MiB, `1_10_0.1` | 2 | 98.486 | May 10, 03:21:07 |
| Model, 4028 MiB, `1_10_0.1` | 3 | 97.981 | May 10, 03:58:06 |

The ARMS files fall within the recorded 6.18 boot beginning May 7 at 05:59:49 and ending at 19:04:31. The model files fall within a later recorded 6.18 boot beginning May 9 at 16:32:51 and ending May 10 at 04:25:07. The approximately 17.68-second difference between their means is therefore **not a comparison of contemporaneous executions**. The source copies, their hashes, and the corresponding boot-ledger entries are recorded in the evidence tables.[^4][^5]

The final archive's May 11 timestamps are clustered within milliseconds across many runs. The ZIP stores the same clustered timestamps. Those dates reflect archiving, whereas the original copies show the execution-like sequence above. Git history independently establishes that the exact ARMS results already existed in the May 9 commit.

## A model advantage survives the closest historical comparison

The original model directory contains sweeps at the **same 4009 MiB label and run numbers as the archived ARMS measurements**, on May 7. Each sweep contains model history mode 2, history length 10, discount 99, and several scaler values. The `2_10_0.5` results are a particularly useful comparison because they appear roughly seven and a half minutes after the corresponding ARMS file, with the intervening model configurations also preserved.[^1]

| Run | ARMS file timestamp | ARMS, seconds | Model `2_10_0.5` timestamp | Model, seconds | Model advantage, seconds |
|---|---|---:|---|---:|---:|
| 1 | May 7, 09:07:49 | 112.781 | May 7, 09:15:23 | 100.394 | 12.387 |
| 2 | May 7, 12:54:13 | 118.355 | May 7, 13:01:45 | 100.399 | 17.956 |
| 3 | May 7, 16:40:42 | 116.275 | May 7, 16:48:15 | 99.587 | 16.688 |
| **Mean** | | **115.804** | | **100.127** | **15.677** |

All eighteen ARMS/model executions in these three sweeps fall within the same recorded boot interval. Their timestamps have an additional consistency check: subtracting the next run's measured duration from its file timestamp places its start **43.20–44.01 seconds after the previous file timestamp**, across all fifteen within-sweep transitions. That is consistent with the benchmark runner performing preparation between executions. It is very different from the millisecond clustering caused by copying files into the final archive.

The contemporaneous model response is also systematic:

| Configuration, 4009 MiB | Mean total, seconds | Advantage over ARMS, seconds |
|---|---:|---:|
| ARMS | 115.804 | — |
| Model `2_10_0.125` | 113.752 | 2.052 |
| Model `2_10_0.25` | 107.968 | 7.835 |
| Model `2_10_0.5` | 100.127 | 15.677 |
| Model `2_10_1.0` | 100.552 | 15.251 |
| Model `2_10_1.5` | 100.495 | 15.309 |

Each mean uses three runs. These results are not all the same model executable as the selected final `1_10_0.1` configuration; they establish that a substantial advantage already existed in the nearby sweep.

The historical scripts define a single BC Twitter graph path shared by ARMS and the model. Each model configuration calls `run_measurement_setup`, which calls cache-dropping/compaction preparation. There is no per-scaler SSD/HDD selection in those scripts.[^6] A storage-only explanation would need additional unrecorded behavior, such as mount changes within each sweep or preparation failures correlated with configuration order. Neither is evidenced by the saved records.

The order of configurations was fixed, however, and May's command logs and settings readbacks are unavailable. This is strong evidence against a simple disk mismatch, not a randomized experiment proving that the entire 15.68 seconds came from model decisions.

## A second historical batch and other workloads support that interpretation

The May 4 BC Twitter batch at 4008 MiB repeats the pattern: ARMS averages **116.999 seconds**, while model `2_10_0.5` averages **101.941 seconds**, a **15.058-second** advantage. Its three ARMS/model sweeps fall within the recorded boot beginning May 3 at 18:07:36 UTC and ending May 4 at 18:16:19 UTC. Model `2_10_0.25` averages 107.537 seconds; increasing the scaler to 0.5 produces the largest improvement before performance flattens or slightly worsens.[^1][^5]

Nearby May 7 measurements at 4009 MiB also show that the advantage is workload-dependent, despite the same runner structure:

| Workload | ARMS mean, seconds | Model `2_10_0.5` mean, seconds | ARMS minus model |
|---|---:|---:|---:|
| BC Twitter | 115.804 | 100.127 | +15.677 |
| PageRank Twitter | 320.226 | 307.230 | +12.996 |
| BC Kron | 241.684 | 243.607 | −1.923 |
| PageRank Kron | 320.530 | 304.879 | +15.651 |
| XSBench | 194.925 | 167.522 | +27.403 |

Each cell uses three runs. XSBench's command generates its workload without reading either GAPBS graph path, so its improvement cannot come from changing those graph mounts. This does not independently prove the cause of BC Twitter's improvement, but it demonstrates that the historical model advantage was not universally a graph-storage artifact.[^1][^6]

Capacity dependence provides another cross-check. The selected final BC Twitter results are:

| Approximate MiB label | ARMS mean, seconds | Model `1_10_0.1` mean, seconds | ARMS minus model |
|---|---:|---:|---:|
| 4000 | 115.804 | 98.126 | +17.678 |
| 6000 | 98.704 | 101.712 | −3.009 |
| 8000 | 98.174 | 98.203 | −0.030 |
| 10000 | 97.419 | 99.158 | −1.739 |

The label pairs are 4009/4028, 6009/6032, 8009/8028, and 10009/10028. These are different sweeps and boots, and the labels are not measured resident DRAM. Nevertheless, ARMS is already near 98 seconds at the higher labels. A constant 17-second loading penalty applied to every archived ARMS result does not fit this pattern. The nearby mode-2 model sweeps similarly show BC Twitter gains at 4008/4009, but approximately equal or slightly worse model times at 6005/6009 and 8005/8009.[^1][^4]

## Recent same-boot records show a large storage effect on both systems

The September 9 journal directly identifies boot `09bab7b7ee024496b131e20399b88193`, running `6.18.1-061801-generic`. Launch commands distinguish the two graph paths:

- HDD path: `/users/zimooo2/gapbs/benchmark/graphs/twitter.sg`, backed by `/dev/sdb`, a rotational ST1200MM0088 device.
- SSD path: `/users/zimooo2/tiering_solutions/data/graphs/twitter.sg`, backed by `/dev/sda3` on an Intel SSDSC2BB48 SSD.

Matching launch and session-close records by PID recovers this sequence:[^2][^7]

| Start, September 9 UTC | System | Graph location | Sudo session duration, seconds | Completion evidence |
|---|---|---|---:|---|
| 11:17:24 | Model | HDD | 128.284 | Previously observed successful `real=128.324`; file later overwritten |
| 11:22:06 | ARMS | HDD | 132.716 | Session closes after a plausible full run; exit status not retained |
| 11:34:58 | ARMS | SSD | 98.555 | Previously observed successful `real=98.597`; file later overwritten |
| 11:38:00 | Model | SSD | 96.550 | Saved diagnostic `real=96.587` and `Read Time=38.95843` |

Using session durations consistently, ARMS improves by **34.161 seconds** between the HDD and SSD attempts; the model improves by **31.734 seconds**. The observed gap between systems is **4.432 seconds on HDD** and **2.005 seconds on SSD**. The HDD ARMS completion status is less certain than the other three, so that comparison is supportive rather than a clean four-run experiment.

The model's HDD read time was reported as approximately 70 seconds, versus 38.95843 seconds saved for the SSD diagnostic. That roughly 31-second read-time change agrees with the total-time change. The HDD read figure is an approximate terminal observation, not a retained exact log value. GAPBS read time also contains loading-related allocation and construction work; it should not be interpreted as a pure device-bandwidth measurement.

These records support storage explaining much of the recent absolute timing shift. They do **not** show a return of the old 15–18-second model advantage when both systems use the HDD graph path. Library paths match across the recent runs, but only the SSD diagnostic preserves a library hash. Cache preparation also differs between the normal runner and that diagnostic. Consequently, the measured 32–34 seconds should not be treated as a universally applicable disk correction factor.

## Recent 6.2 pairs directly confirm shared SSD input

Two successful 4077 MiB pairs have launch commands and timing files that agree to within normal wrapper overhead. All four launches record boot ID `8d89477fa10d420cb3fb6ef5f44ea07e`; the boot ledger identifies this as the September 8 stock 6.2 boot. Both systems explicitly use the SSD path.[^8]

| Pair | ARMS start UTC | Model start UTC | ARMS, seconds | Model, seconds | Advantage, seconds |
|---|---|---|---:|---:|---:|
| 1 | Sep 8, 23:56:55 | Sep 8, 23:59:27 | 102.475 | 100.941 | 1.534 |
| 2 | Sep 9, 05:01:24 | Sep 9, 05:03:54 | 102.645 | 101.430 | 1.215 |
| **Mean** | | | **102.560** | **101.186** | **1.375** |

The third model run was interrupted and is excluded. These pairs demonstrate that the small current gap is not produced by comparing different graph paths or different boots.

## Exclusions and unresolved provenance

Normal ARMS/model `.time` files retain total duration and completion status, but not separate read/iteration times. The surviving ordinary text logs with detailed GAPBS timing are predominantly MEMTIS runs; they are not substitutes for matched ARMS/model runs. Available BC Twitter training traces are from different modes and dates. Their inspected schema contains page-access/model features rather than a historical mount or boot record.

Retained authentication logs provide relevant launch/mount entries from August onward, and the current machine's retained journal boot list begins September 6. The reboot ledger reaches May, but it records kernel boots rather than mounts. Shell history includes repeated attempts to mount `/dev/sda`, `/dev/sdb`, and `/dev/sdc` at the same benchmark directory, without command timestamps or success status. Thus an old path string cannot identify the old physical disk.

The latest overwritten 4078 MiB ARMS result of **100.641 seconds** is excluded from the primary SSD comparison: journal records establish that it overlapped the historical-source diagnostic for approximately 18 seconds. The diagnostic was stopped. The subsequent 97.359-second model result completed, but its preceding ARMS run is not a clean comparator. Subsecond attempts and the later ARMS run with `exit_status=130` are also excluded.[^2]

No new benchmarks, mounts, kernel changes, or tiering-policy changes were performed for this investigation.

## Assessment

**Storage materially affects the totals, but the complete historical model advantage is unlikely to be just an HDD/SSD mismatch.** A roughly 15.7-second advantage survives the closest May 7 comparison, with a comparable result in another boot on May 4. The response to scaler settings, the capacity dependence, and the different outcomes across workloads all argue for an additional configuration- or memory-placement-dependent effect.

The exact contribution of model decisions versus loading/allocation interactions remains unresolved. The historical final comparison also mixes sweeps and configurations. The direct next experiment would use isolated historical and current libraries, a single explicitly verified graph device, recorded effective DRAM capacity, and separate read/iteration timing, alternating execution order. It should preserve boot ID, graph device, library and executable hashes, settings readbacks, and completion status for every attempt. Reusing a filename without retaining each attempt is a major source of the current uncertainty.

## Sources

[^1]: Original ARMS/model result files under `times/c220g5/{arms-6.18_new,model_6.18_new}`. Individual results, paths, configuration labels, timestamps, and inferred boot intervals are preserved in [historical_sweeps.csv](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/historical_sweeps.csv).

[^2]: Current-boot sudo journal launch and session-close records, extracted September 9. Selected original messages, boot IDs, PIDs, timestamps, and journal cursors: [current_boot_session_records.json](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/current_boot_session_records.json). Derived durations and exclusions: [current_boot_sessions.csv](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/current_boot_sessions.csv).

[^3]: Complete parsed timing-file inventory with source paths, status, timestamps, and content hashes: [timing_inventory.csv](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/timing_inventory.csv). Matching text-log records: [text_log_evidence.json](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/text_log_evidence.json). Training-trace metadata and four read errors: [training_trace_metadata.json](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/training_trace_metadata.json). File counts describe the filesystem at investigation time; later runs may overwrite files.

[^4]: Byte-identical original/final BC Twitter results: [archive_provenance.csv](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/archive_provenance.csv). Primary archive directories: [ARMS](/users/zimooo2/tiering_solutions/times/c220g5_final/arms_6.18/bc-twitter.sg) and [model](/users/zimooo2/tiering_solutions/times/c220g5_final/model_6.18/bc-twitter.sg). Git commits `41e8adedd` and `890a2449c` provide independent bounds on archiving history.

[^5]: `/var/log/wtmp` and `/var/log/wtmp.1`, read with UTC timestamps. Extracted boot starts and kernel labels: [boot_ledger.csv](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/boot_ledger.csv). Kernel labels in `last` output are truncated; historical same-boot assignments use original file timestamps and are inferences, not embedded run metadata.

[^6]: Repository revision `8d6a8bc66`: `measurement_model.sh` calls setup for each model configuration; `measurement_common.sh` and `defrag.sh` implement cache preparation; `workloads/bc-twitter.sg.sh` defines the shared graph path. `workloads/XSBench.sh` defines the generated, non-GAPBS workload. Current ARMS source history is documented separately in [arms_source_history_audit.md](/users/zimooo2/tiering_solutions/docs/arms_source_history_audit.md).

[^7]: Host `findmnt`, `lsblk`, kernel release, and boot-ID [readbacks](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/host_storage_readback.txt) on September 9; [current_boot_kernel_records.json](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/current_boot_kernel_records.json). Preserved SSD diagnostic: [timing](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/ssd_model_diagnostic_run_time.txt), [stdout](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/ssd_model_diagnostic_stdout_log.txt), [metadata](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/ssd_model_diagnostic_metadata_log.txt), and [runner](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/ssd_model_diagnostic_run_sh.txt).

[^8]: Recent journal command records: [recent_journal_launches.json](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/recent_journal_launches.json), with the previous boot's exact [kernel release](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/previous_boot_kernel.txt). Timing-file/launch matches, graph paths, boot IDs, and authentication-log line references: [recent_launch_matches.csv](/users/zimooo2/tiering_solutions/docs/arms_model_storage_evidence/recent_launch_matches.csv).
