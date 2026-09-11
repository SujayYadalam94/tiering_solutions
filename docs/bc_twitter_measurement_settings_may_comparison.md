# Current measurement settings versus May ARMS

September 9, 2026. Read-only source and saved-result audit; no benchmark or host-setting change.

**All 23 final VM/THP/NUMA/perf values requested by current normal C220G5 ARMS setup match the retained May setup. Uncore programming also matches.** The new file centralizes settings previously written in `setup.sh`; its existence is not itself evidence of different ARMS settings. Historical effective values remain unrecorded for the original runs.

The setup file has the identical Git blob `9269f544c753641578e44d8709ec815d031f7770` at `6b1bc11dd`, `8d6a8bc66`, `4da3d6c3f`, and `d6f720be6`, spanning the relevant early-May, May-long, and June source states. The frozen May copy was verified byte-for-byte against Git. The current file was re-read and its final requests independently compared with the previously intercepted May shell-write recording. [Full comparison and source hashes](../reproductions/bc-twitter-may2026/conditions/boot_capacity_400mb/measurement_settings_may_comparison.json).

| Control | May request | Current ARMS request |
| --- | --- | --- |
| `kernel.numa_balancing` | `0` | `0` |
| `kernel.perf_cpu_time_max_percent` | `0` | `0` |
| `kernel.perf_event_max_sample_rate` | `1000000` | `1000000` |
| `vm.admin_reserve_kbytes` | `16384` | `16384` |
| `vm.user_reserve_kbytes` | `16384` | `16384` |
| `vm.min_free_kbytes` | `1048576` | `1048576` |
| `vm.lowmem_reserve_ratio` | `256 256 32` | `256 256 32` |
| `vm.overcommit_memory` | `1` | `1` |
| `vm.watermark_scale_factor` | `10` | `10` |
| `vm.watermark_boost_factor` | `10000` | `10000` |
| `vm.zone_reclaim_mode` | `0` | `0` |
| `vm.vfs_cache_pressure` | `2000` | `2000` |
| `vm.compaction_proactiveness` | `80` | `80` |
| `ksm/run` | `0` | `0` |
| `lru_gen/enabled` | `0x0000` | `0x0000` |
| `numa/demotion_enabled` | `0` | `0` |
| THP `enabled` | `always` | `always` |
| THP `defrag` | `always` | `always` |
| THP `shmem_enabled` | `force` | `force` |
| khugepaged `defrag` | `1` | `1` |
| khugepaged `pages_to_scan` | `8192` | `8192` |
| khugepaged `scan_sleep_millisecs` | `0` | `0` |
| khugepaged `alloc_sleep_millisecs` | `1` | `1` |

Sources: [current settings](../measurement_settings.sh:33), [May setup](../reproductions/bc-twitter-may2026/conditions/arms_version_provenance/hugepage_settings_history/call_paths/may04_git/setup.sh:81). The current [setup caller](../setup.sh:65) explicitly supplies perf rate `1000000`, percentage `0`, and the normal system argument. The `numa_balancing=1` helper at the end of the settings file is a separate DRAM/CXL-baseline helper; normal ARMS does not call it. MEMTIS exceptions and NOMAD/TPP overrides are also outside this path. [NUMA call-path audit](../reproductions/bc-twitter-may2026/conditions/boot_capacity_400mb/numa_balancing_history.md).

Uncore still requests MSR `0x620=0x707` on CPUs `10-19,30-39`, using the same default CPU list and environment override. Compare [May writes](../reproductions/bc-twitter-may2026/conditions/arms_version_provenance/hugepage_settings_history/call_paths/may04_git/setup.sh:61) with [current writes](../measurement_settings.sh:4). Current method/frequency variables and the descriptive print are new reporting, not new MSR values.

## Actual execution differences

**Perf write ordering can change the effective cap.** May writes the rate, then percentage zero. If percentage is already zero, the rate write can fail and retain an inherited cap. Current settings temporarily write percentage `25`, then the requested rate, then percentage `0`. A historical-setup replay actually logged `Invalid argument` for the rate write; its subsequent readback nevertheless remained at the intended `1000000`. [Preparation log](../reproductions/bc-twitter-may2026/conditions/sessions/20260909T130001Z-142220/results/20260909T130001Z-142220/01-may07_arms/preparation.log:15), [readback](../reproductions/bc-twitter-may2026/conditions/sessions/20260909T130001Z-142220/results/20260909T130001Z-142220/01-may07_arms/before.json:10).

The already completed `1000000` versus `100000` cap experiment gave ARMS average iterations of **6.34331 versus 6.33678 seconds**, and model averages of **5.82238 versus 5.80449 seconds**. This tested inherited-cap alternative did not reproduce the historical gap. It does not establish May's actual cap. These sysctls do not configure the perf ring-buffer page count in the ARMS source. [Results and exact run links](bc_twitter_conditions_followup.md:21).

**VFS cache pressure now applies earlier.** May sets `2000` after global process-memory migration and affinity changes; current settings apply it before those operations. Both have `2000` by the final cache-drop/compaction passes. A different initial setting could therefore affect preparation transiently, but the source supplies no persistent final difference or measured 400 MiB effect. Minimum-free versus user/admin-reserve write order also changes, as do the positions of MGLRU and KSM writes. May's duplicate NUMA-balancing zero write becomes one. [Exact ordered-write diff](../reproductions/bc-twitter-may2026/conditions/arms_version_provenance/hugepage_settings_history/settings/may_to_current_write_order.diff).

**Failure behavior is stricter inside setup, while ordinary callers still fail to enforce success.** The new common writer and uncore function return at the first error; setup exits, and the common helper can skip subsequent preparation. The ordinary ARMS/model callers still launch without checking the helper's return. Old setup generally continued after errors. This could produce an incompletely prepared run if a write fails, but it is an error-path difference, not a changed requested setting. No such skipped-preparation failure was found in the accepted diagnostic runs. MGLRU is now explicitly skipped if its path is absent; that does not distinguish these runs on the compared 6.18 kernel, where the path exists. [Detailed caller audit](../reproductions/bc-twitter-may2026/conditions/arms_version_provenance/hugepage_settings_history/call_paths/README.md).

Outside `measurement_settings.sh`, the inner defrag now uses an absolute path; May's relative path could miss that pass when invoked from another working directory. The common helper's outer defrag was already absolute. Both normal paths otherwise request two passes and reset THP enabled/defrag to `always`. No such external-directory launch has been established for the archived cohorts.

## Implication for the free-memory investigation

This file provides **no recovered May-to-current ARMS reserve or compaction value to revert**. Successful setup does not enforce a particular node0 `MemFree`: the old size-based memory-eater block is commented out in both versions. Identical requested controls therefore remain compatible with different residual kernel allocations or page layout after preparation. The previously completed May-setup replay used the old order and defrag sequence and still ran faster than the archive; the refactoring alone has not explained the discrepancy.
