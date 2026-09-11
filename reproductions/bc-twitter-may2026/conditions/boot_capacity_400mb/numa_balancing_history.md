ARMS and the model use `kernel.numa_balancing=0` in the retained May/June batch scripts and the current normal setup. It is not enabled temporarily during their ordinary preparation. The setup function's explicit `migratepages` operations are separate from kernel automatic NUMA balancing.

| Run path | Final requested value before workload |
| --- | --- |
| May 4/7 normal ARMS and each May 7 cost-model configuration | `0` |
| May 14 and June 13/22 long ARMS/model batches | `0` |
| Current normal ARMS/model batches | `0` |
| Separate DRAM/CXL baseline-default preparation | `1` |
| Explicit NOMAD preparation | `2` |
| Explicit TPP preparation | `3` |

The [frozen May setup](../arms_version_provenance/hugepage_settings_history/call_paths/may04_git/setup.sh:92) writes zero twice: through `sysctl` at line 92 and the proc file at line 94. Its later nonzero write belongs only to the `nomad` branch. [The common helper](../arms_version_provenance/hugepage_settings_history/call_paths/may04_git/measurement_common.sh:290) substitutes `default` when no second argument is supplied and passes it explicitly to setup. An inherited `MEASUREMENT_SYSTEM` therefore does not select NOMAD or TPP for these calls. Defrag and teardown contain no balancing write.

The [May 7 cost-model editor save](../arms_version_provenance/hugepage_settings_history/call_paths/editor/may07_cost_model.sh:101) calls that ordinary helper before every model configuration. Its name, suffix, and cost select the library/output; they do not select a different setup system. The [May 14 long runner](../arms_version_provenance/hugepage_settings_history/call_paths/editor/may14_long.sh:77) runs the model, which prepares itself, then explicitly prepares ARMS at line 84. June 13/22 retained long scripts use the same sequence. Git comparisons `8d6a8bc66 → 4da3d6c3f → d6f720be6` show no change to setup/common/ARMS; May14-to-June also has no model-runner change.

Current [setup.sh](/users/zimooo2/tiering_solutions/setup.sh:65) invokes shared settings, whose [line 57](/users/zimooo2/tiering_solutions/measurement_settings.sh:57) writes zero. The [normal helper](/users/zimooo2/tiering_solutions/measurement_common.sh:297) retains the explicit `default` behavior. The separately named `run_measurement_setup_baseline_default` calls [the baseline migration helper](/users/zimooo2/tiering_solutions/measurement_settings.sh:87), which writes one **after** ordinary setup. Plain system `default` and this baseline-default function are distinct paths.

Observed recent values agree with the normal path: all **48 before/after snapshots** across 14 historical-settings runs and 10 khugepaged runs read zero. The extracted values and exact snapshot paths are in [historical-settings readbacks](numa_readbacks_historical_settings_tests.json) and [khugepaged readbacks](numa_readbacks_khugepaged_tests.json). These are actual experiment readbacks, rather than inference from a script.

Two limits remain. Directly launching `measurement_arms.sh` does not invoke setup and inherits the current value; the batch callers above prepare it first. Historical scripts also did not reliably stop after setting-write failure, and May has no equivalent per-run readback, so requested historical zero cannot prove every write succeeded. No policy-specific override or observed setup failure establishes a different May value.
