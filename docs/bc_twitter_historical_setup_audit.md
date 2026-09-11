# BC Twitter: historical setup reconstruction

Read-only audit, September 9, 2026. This supplements the [runtime diagnosis](bc_twitter_reproduction_diagnosis.md). The current 4 GB boot is intentional. This audit does not identify a wrong-capacity boot or establish a historical disk mismatch between ARMS and the model.

The subsequent [condition experiments](bc_twitter_conditions_followup.md) have now tested the full preparation and perf-cap hypotheses below. Neither restored the original gap. They also measured a 1440 MiB fast-memory metadata overhead from the unused PMEM device and tested its removal within the same boot.

## Additional recovery from editor history

VS Code's local history preserves uncommitted saved files independently of Git. Thirteen useful snapshots, their original `entries.json` indexes, hashes, timestamps, and comparisons are saved under [conditions/editor_setup_history](../reproductions/bc-twitter-may2026/conditions/editor_setup_history/manifest.json). Saved versions are evidence of editing, not proof of execution; external changes and Git updates need not produce history entries, and several files retain only 50 versions.

- The May 7 **06:09:11 UTC** snapshot of `measurement_model.sh` (`47GZ.sh`) lists scalers **0.125, 0.25, 0.5, 1.0, 1.5**, matching the actual May 7 sweep. The May 4 Git snapshot instead lists 0.25, 0.5, 1.0, 1.5, 2.0. This strengthens reconstruction of the executed sweep and shows why Git alone was insufficient. The preparation logic is unchanged.
- The May 4 **03:21:31 UTC** `measurement_common.sh` snapshot (`89Uc.sh`), May 2 BC workload snapshot (`1DIJ.sh`), April 19 ARMS runner snapshot (`h25N.sh`), and March 13 final defrag snapshot (`G1RT.sh`) are byte-identical to their May 4 Git versions. The latest setup editor entry is March 22; it predates April's platform-parameterization change but requests the same C220G5 setup values.
- The May 10 **02:11:31 UTC** model runner (`jLFv.sh`), before the archived final-model measurements, is byte-identical to May 9 Git. The surviving `run_all_measurements.sh` history starts at May 10 **20:17 UTC**, too late to recover the May 7 or early-May-10 run-all file.

The newly recovered `/users/zimooo2/.bash_history-05835.tmp` has a **May 11 20:55:24 UTC** modification time and 2,000 commands. It repeatedly mounts devices at `tiering_solutions/logs` and `big-ann-benchmarks/data`, but contains **zero mentions of `gapbs`** and therefore no recorded mounts at `gapbs/benchmark`. In contrast, the current shell history contains many such graph-directory mounts. This supports the possibility that May's GAPBS graphs remained on the root SSD while logs and other workload data used another device. It is not proof: mounts from other sessions, fstab, symlinks, or omitted history remain possible, and individual commands lack timestamps. [Preserved targeted command evidence](../reproductions/bc-twitter-may2026/conditions/editor_setup_history/may11_shell_history_selected.json).

The May 11 history's explicit uncore command is `wrmsr --processor 39 0x620 0x707`, agreeing with the ordinary slow-tier setting. It does not recover perf-limit, memory-map, or free-memory values. Its numerous `make clean; make -j; bash run_all_measurements.sh` commands show that clean rebuilding was part of the user's workflow, but do not identify which binary served an individual archived run.

## What Git actually preserves

The normal setup body in `setup.sh` has blob ID `9269f544c` from April 9 (`d92cb822f`) through the May 4/May 9 benchmark-era snapshots. It remains unchanged until September 3 (`671e0e644`), whose additions principally specialize MEMTIS and all-NUMA setup. The ordinary ARMS/model defaults remain the same. The body of `defrag.sh` has no committed change after January 21 (`ca1bf086b`); the working-tree changes add a shebang and error handling.

The recovered May setup requests:

| Setting | Requested value |
|---|---|
| Benchmark CPUs / OpenMP threads | `0-9,20-29` / `16` |
| Benchmark memory nodes | `0,1` |
| Other processes | Attempt memory migration `0` to `1`; set each PID's main-thread CPU affinity to `10-19,30-39` |
| Existing IRQs | Attempt affinity `10-19,30-39` |
| Slow-socket uncore | MSR `0x620=0x707` on CPUs `10-19,30-39` |
| THP / direct THP defrag / shmem THP | `always` / `always` / `force` |
| khugepaged | Defrag `1`, pages-to-scan `8192`, scan-sleep `0 ms`, allocation-sleep `1 ms` |
| Proactive compaction | `80` |
| Minimum free memory | `1048576 KiB` |
| Watermark scale / boost | `10` / `10000` |
| Low-memory reserves | `256 256 32` |
| User / administrator reserves | `16384 KiB` / `16384 KiB` |
| VFS cache pressure / overcommit | `2000` / `1` |
| NUMA balancing / zone reclaim / generic demotion / MGLRU / KSM | Disabled |
| Swap | Disabled |
| Perf sample-rate cap / CPU percentage | `1000000` / `0`, subject to the write-order caveat below |
| BC virtual step | `10000` samples |

These are requested settings from source, not preserved May readbacks. Affinity changes and memory migrations can fail, and historical scripts continued past many failures.

## Preparation was an operation, not just a list of sysctls

Before ARMS, `run_all_measurements.sh` calls `run_measurement_setup`. Before **each individual model cost scaler**, `measurement_model.sh` calls that same helper. The helper invokes `setup.sh`, which invokes `defrag.sh`; the helper then invokes `defrag.sh` again.

The defrag-history follow-up independently checked the May 7, 06:09:11 UTC [saved model runner](../reproductions/bc-twitter-may2026/conditions/editor_setup_history/3b775728/47GZ.sh). It explicitly selects discount 99, history mode 2, length 10, and scalers `0.125 0.25 0.5 1.0 1.5`, matching the cost ablation. Lines 101–102 call setup immediately before each individual model execution. The [May 4 common-helper save](../reproductions/bc-twitter-may2026/conditions/editor_history/measurement_common.sh/89Uc.sh) invokes setup at line 296 and defrag again at line 302. Consequently there is no saved-code evidence that the May cost ablation omitted defrag between ARMS and the model, or between successive cost settings.

Recomputing the gaps from [historical_sweeps.csv](arms_model_storage_evidence/historical_sweeps.csv), using `next result mtime - next measured duration - previous result mtime`, gives ARMS-to-first-model gaps of **43.518, 43.850, and 43.677 seconds** for the three May 7 BC Twitter repetitions. All fifteen within-sweep transitions span **43.200–44.009 seconds**. These are inferred preparation intervals, not direct records of successful compaction syscalls, but independently support preparation between every execution.

There were older exceptions. Git `adfe4ed2f` (January 11) runs ARMS then model consecutively, with no intervening setup or defrag; the active workload in both scripts is XSBench, with GAPBS commented out. Git `ca1bf086b` (January 21) explicitly invokes defrag between ARMS and model. The model runner itself has no per-configuration setup through the March 19 snapshot; `761df1b26` adds it March 21. The March 19 outer runner has the whole model block commented out, so its commented setup line does not establish an executed ARMS/model pair without preparation. Git `d92cb822f` (April 9 UTC) skips internal setup specifically for the `_train` suffix; `563220a4a` removes that exception April 23 UTC. This training exception does not apply to the normal cost-ablation libraries. Later May 10 outer-runner saves likewise comment out both ARMS and its setup together, while model preparation remains inside the model runner.

Each `defrag.sh` invocation enables THP, synchronizes writes, globally drops caches with values `3` and `2`, compacts memory, then synchronizes and drops caches with `3` and `2` again. Consequently each ordinary benchmark preparation requests **two explicit whole-system compactions and multiple global cache/slab reclamations**, after attempting to move pre-existing process memory to the slow node. This agrees with the contemporaneous sweep's repeated preparation intervals and supports the user's observation of shared preparation within the cost ablation.

The original isolated reproduction runner only evicted the selected graph's file cache. Matching sysctl readbacks while omitting this preparation does not guarantee matching free hugepages, fragmentation, slab occupancy, or other-process placement. Replaying the historical preparation is therefore a substantive next isolation experiment.

Current equivalent locations: [setup process migration](../setup.sh#L58), [setup defrag call](../setup.sh#L111), [second defrag call](../measurement_common.sh#L297), [defrag operations](../defrag.sh#L7).

The historical cleanup also executes unrestricted `pkill python` and `pkill -f jupyter-notebook`. A diagnostic should use targeted cleanup or explicitly omit these commands; otherwise it can kill its own Python runner and unrelated work. Historical `taskset -pc PID` changes the main thread, so replacing it with all-thread affinity changes would also change the experiment.

## Actual perf-setting difference

Historical setup writes the sample-rate cap before setting the CPU percentage to zero. Linux rejects a sample-rate write while the CPU percentage is already zero or 100. Thus repeated old setup can retain an inherited cap even while printing the intended cap. Current [measurement_settings.sh](../measurement_settings.sh#L75) temporarily sets percentage 25, installs the requested cap, then sets percentage zero.

This establishes a mechanism, not a historical value. A normal fresh boot starts at a 100000 sample-rate cap and percentage 25 in the local kernel sources, so its first old setup should successfully install 1000000. There is no recovered May readback showing failure or a lower cap. The cap also controls the per-tick interrupt limit for fixed-period events; setting percentage zero disables dynamic adjustment but does not remove that fixed limit. Local source evidence is in `/users/zimooo2/linux-hybridtier/kernel/events/core.c`, `perf_proc_update_handler`, `perf_cpu_time_max_percent_handler`, and `__perf_event_account_interrupt`; the MEMTIS kernel copy contains the same mechanism.

If complete preparation does not restore the difference, changing only the cap between 100000 and 1000000 is a bounded, source-motivated check. It must not be described as restoring a known May setting.

## Boot and capacity provenance search

A targeted search of `/etc/default`, `/boot/grub`, sysctl configuration, shell history, and the runner's size history did not recover a May command line or node-free-memory measurement.

- `/etc/default/grub.bak`, modified March 16, contains `memmap=90G!2G` and verbose/debug defaults: `console=ttyS0,115200 earlycon=uart,io,0x3f8,115200 loglevel=7 ignore_loglevel debug rd.shell rd.break`. A later assignment in the same file overrides `GRUB_CMDLINE_LINUX`; the memory-map setting also occurs in `GRUB_CMDLINE_LINUX_DEFAULT`. This is a real earlier configuration, **not proof of the May benchmark command line**. It can correspond to another capacity experiment. Serial/kernel-log overhead is only a low-confidence possibility; no evidence links it to May's ARMS slowdown.
- Current `/etc/default/grub`, modified September 8, has empty default flags and `memmap=92G!2G` in the general command line. It records today's intended preparation, not May's.
- `/boot/grub/grub.cfg.preemulab`, modified February 23, contains only Ubuntu 6.8 entries and no memory-map restriction. `/etc/default/boot` is empty. Distribution GRUB templates are older installation defaults.
- Shell history records many `vim /etc/default/grub`, `update-grub`, and `numastat -m` commands, but does not preserve the edited values or command output. Its only explicit `wrmsr` command sets CPU 11 to `0x620=0x707`, agreeing with ordinary slow-tier setup; it has no timestamp establishing May use.
- No committed shell-script revision contains literal `SIZES=(4009)` or `SIZES=(4028)`. The May 4 snapshot contains `6005`, May 9 contains `8028`, and May 14 contains `6032`; preceding side branches contain `4008`, `6001`, or `8001`. The values were edited between measurements/commits. The scripts do not compute them from live free memory, and the memory-eater allocation is commented out. A filename therefore cannot recover original usable DRAM.

Earlier substantive memory-setting changes are committed on March 14 (`d87369bce`): minimum free memory rose from 16384 to 1048576 KiB, watermark boost changed from 0 to 10000, compaction rose from 20 to 80, and aggressive khugepaged settings were added. The diff also changes an early watermark-scale write from 1 to 10, but removes a later write of 10, so it does not establish an effective scale-factor change. March 16 briefly changed khugepaged sleeps to 10/10 ms, then restored 0/1 ms. These changes predate both May targets and are not evidence of a change after those measurements.

After complete setup and perf-cap checks, the strongest remaining setup uncertainty is effective memory placement and fragmentation at launch, followed by missing fast-socket frequency/CPU/boot readbacks. Historical configuration records do not establish their exact values. Input and binary identity remain separate provenance questions covered by the broader investigation.
