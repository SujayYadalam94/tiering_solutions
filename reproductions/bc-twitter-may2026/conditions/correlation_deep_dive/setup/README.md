# Expanded setup, launch, and saved-script correlation audit

Read-only historical investigation, September 9, 2026. No workload, host-setting change, reboot, graph read, or build was performed. Original production files were not changed. The principal interval is May 14 through June 13–14; older March and later August evidence is dated separately.

## Result

**This expansion found no previously overlooked VM, THP, reserve, affinity, or launch-environment override that places a different accepted setting in the May/June comparable cohorts.** It does recover additional historical metadata and confirms that training traces are not interchangeable with ordinary ARMS timing runs. The documented BC-only → full-suite launch transition remains a real correlation, not a demonstrated migration-failure cause.

### New coverage beyond the earlier audits

| Source | Coverage and finding |
|---|---|
| IPython executed-cell database | `/users/zimooo2/.ipython/profile_default/history.sqlite`: 253 sessions, 3,372 cells, March 9–September 8. All 146 broad settings-search matches are calls to parse `max_dram_hugepages` logs. No host-setting write, benchmark launcher, profiling command, allocator environment override, or node-free-memory readback. No stored output rows. Session timestamps are recorded without timezone and are not per-cell execution timestamps. |
| External VS Code saved resources | 178 snapshots across 15 shell/Python/notebook resources **outside tiering_solutions**. Eight snapshots have control-related matches: March FAISS/HybridTier thread settings, March 22 Soar runner, and April 5 installer edits. The later model-processing saves contain no matching host controls. Unrelated user configuration was not read. |
| Old Copilot source indexes | Four read-only SQLite indexes (`local-index.1.db` and `workspace-chunks.db` in the original and `-1` workspace) yielded 12 repository script records. March setup contents survive as chunks, but are older than their database mtime. These do not provide May snapshots. |
| Copilot cached filesystem metadata | Four `codebase-external.sqlite` catalogs inspected through narrow setup/defrag/BC/graph/history predicates. They hold paths, sizes, mtimes, and empty target content hashes; no full source or disk-device identity. Old setup/defrag metadata agrees with the size of recovered May files. Deleted original graph metadata survives. |
| Shell and Vim expansion | Rechecked the three user histories plus Vim history and shell startup files for OMP/GOMP/KMP, allocator/loader variables, ulimit/priority, IRQ, I/O readahead, dirty-page controls, slab, reclaim, swap, THP, NUMA, and CPU-frequency commands. No new May/June override. Three editor backup files contain compaction proactiveness 20, all dated March 13. |
| Unsaved/editor storage inventory | No separate VS Code `Backups` or `state.vscdb` store was found under the retained remote data tree. Workspace storage contains the already audited conversations and the additional source/metadata indexes above. This does not establish that all historical unsaved buffers were preserved. |

Exact search matches and metadata are retained in `ipython_history_setting_search.json`, `external_saved_scripts_settings.json`, `copilot_script_chunks.json`, `codebase_metadata_targets.json`, `broader_shell_settings_search.json`, and `vim_compaction_backups.json`. Earlier scope is separately documented in `../../post_may7_history/README.md` and the shell-history/THP reports; this report does not count those scans as newly discovered evidence.

## Additional historical facts and their dating limits

1. **There really are direct editor backups of compaction value 20.** The three files `/users/zimooo2/compaction_proactiveness~`, `compaction_proactivenesz~`, and `compaction_proactivenesy~` all contain `20\n`. Their mtimes are March 13 18:43:11, 19:36:21, and 19:36:25 UTC. Vim history also records opening `/proc/sys/vm/compaction_proactiveness` (`.viminfo:584`). These corroborate March experimentation, not a May setting or accepted write. May setup explicitly requests 80.

2. **Old setup text can be recovered from a source index, but it is stale March text.** The original workspace index was last modified March 17, yet its setup document has contentVersionId `2943-1773116880103`, referring to the older March 10-size/version state. Its chunks are identical to the March 10 index. It has boost 0, min-free 16384, and a *commented* `lowmem_reserve_ratio="1 1 1"`. Therefore the index mtime must not be treated as a March 17 edit or as evidence of a later reserve override. The ordinary editor history already has the later active March reserve experiments.

3. **The older metadata catalog remembers the May-sized setup without a later file mtime.** In workspace `408a693c16dba7123cb586367bad5747-1`, setup is 4,976 bytes with cached mtime April 11 19:42:22.538 UTC, and defrag is 426 bytes with mtime March 13 16:36:05.950 UTC. Those sizes exactly match the May 14 Git files. This is further consistency evidence against a retained May/June setup edit; absent hashes and potential stale indexing prevent treating it as proof that the file was never temporarily modified.

4. **Metadata for the deleted original graph directory survives.** The old catalogs list `/users/zimooo2/gapbs/benchmark_old/graphs/twitter.sg` at 12,732,173,745 bytes, mtime March 5 20:01:26.548 UTC; Kron at 17,966,800,881 bytes, mtime March 5 19:47:16.106 UTC. Both target `docSha` values are empty. This recovers original sizes/dates, not graph content, physical device, mount date, or evidence that ARMS/model used different paths. A stored filesystem mtime may itself have been preserved during a copy.

5. **An external March Soar runner can change more than the ordinary setup resets, but is not linked to May.** Two March 22 snapshots of `SoarAlto/run/bc-urand/run.sh` contain conditional page-cache touching, a background MLC process, NUMA controls, and a `perf stat` wrapper. They target `urand.sg`, not the paired BC Twitter/Kron runs. Its nearby module invocations in the May-saved shell history are already proven to be March commands by exact overlap with the March history. Ordinary setup disables NUMA balancing/demotion; there is no accepted module/process snapshot tying this external path to the target boot. The presence of the script alone is not evidence that these activities overlapped the May measurements.

## Training-run counter comparisons need their actual runner

The saved **June 6 06:02:50.839 UTC** `run_all_measurements_train_dram_only.sh` snapshot is `/users/zimooo2/.vscode-server/data/User/History/-2f2ec34c/mzVU.sh`.

- Lines 29–34 select size label **100050**, default memory-node override **0**, run label `_train`, and library suffix **`_near_train`**.
- Lines 117–118 pass that node override and suffix into `measurement_arms.sh`.
- The recovered May wrapper constructs `libraries/<profile>/libhemem-arms${LIB_SUFFIX}.so` (line 35), so this maps to **`libhemem-arms_near_train.so`**.
- The May Makefile distinguishes `arms_train` from `arms_near_train` at lines 70–71: the latter adds `NEAR_MEM_TRACING_RUN=true`.
- The recovered defs header gives near tracing fixed **250 ms / 250 ms** intervals, while ordinary nonvirtual ARMS uses **500 ms / 100 ms** (lines 226–234).

Those distinctions are already present before June. Consequently a June 6 selection-counter distribution should not be compared to an older generic `run1.parquet` as evidence of a normal-ARMS implementation change until the older log's actual producing policy, flags, and node binding are established. The April 30 saved ordinary training runner actually has ARMS commented out and model training active (`jJwM.sh:28–31`), demonstrating why a generic log basename is insufficient for policy attribution. It does not establish that every May log came from that particular saved runner.

Copies of these exact sources, hashes, and frozen build definitions are in `training_launch/`.

## Ranked supported correlations for the comparable ordinary cohorts

1. **Actual workload/preparation context changed immediately before June's first fast BC Twitter cohort.** The June 13 long workload list enables the four Kron executions before BC; May 14 is BC-only. Original timing order confirms this. June's model→ARMS untimed intervals are 64.6/69.3/92.0 seconds versus May's approximately 44.8 seconds. This is consistent with more preparation work or a different physical allocation state, but has no per-operation timing or historical buddy/THP counters. May 7 was slow despite predecessors and today's BC-Kron gap also shrank; therefore a simple "Kron prelude makes ARMS fast" claim is unsupported. See `../../change_point/README.md` and `../../arms_version_provenance/may_memory_cleanup/profiling_order_history.md`.
2. **Fresh boot immediately before June's cohort.** The dated June 13 22:10:24 6.18 boot is real, but May 14's slow cohort also followed a fresh boot. No exact changed reserve, driver state, hugepage fragmentation, or other accepted kernel setting is recovered. Boot freshness alone is not an explanation.
3. **March manual reserve/compaction/scan changes are real, but too early.** They can explain some standalone March measurements or define historically plausible experiments. Successful May setup overwrites those particular controls. No new evidence moves them into May/June.

No causal setting change was identified. The material limitation is missing historical *effective-state* evidence, rather than a newly discovered alternate lowmem line in the May setup. A missing journal or saved buffer cannot be read as evidence that an override occurred.


## Additional recovery: three unreachable Git runner blobs

Inspected and preserved the three previously unreachable `run_all_measurements.sh` blobs supplied by the source-history audit. The loose-object mtimes below are **storage/existence evidence, not run timestamps**. All three contain unresolved merge-conflict markers at line 59 and **fail `bash -n` with exit 2 before the benchmark loop**. Syntax checking does not execute the scripts or their email function.

| Blob / object mtime UTC | Recovered conflict | Comparison and causal relevance |
|---|---|---|
| `9e9da65570411d9ddc316a2f0c35a634b3c8e646`, May 4 03:00:53 | Size label 4008 versus 8000; second conflict adds/removes already-commented DRAM/CXL blocks | Resolving the two conflicts recovers exact reachable runner versions `29e225954` and `e10786f67`. Active ARMS→model flow and setup remain the known pattern. No new setup, graph-path, or policy-library change. |
| `995f537eb79ae2306fa3eb5ceebdc268b17d3bc0`, May 5 01:40:40 | Size label 6001 versus 8001 | Taking either side recovers exact reachable runner versions `4494a8ebb` and `1b5facfc4`. Both are ARMS→model with explicit setup for ARMS. No new behavioral source. |
| `5484a4e62d2ea72438ab6d0e304800bb5c092b4c`, May 14 06:17:36 | Size label 6032 versus 10004 | Taking the 6032 side is byte-identical to the May 14 01:42:58 saved `wE1V.sh`, the **06:17:46.400** saved `9xCi.sh`, and reachable Git `890a2449c`. It is the known ARMS-only standard runner, distinct from the 4035 long BC cohort. The retained save approximately ten seconds after object creation already resolves the conflict. |

Despite `ARMS_LIB_SUFFIX` defaulting to `_plain`, the active ARMS invocation in **all three** passes an explicit empty suffix; the call using `${ARMS_LIB_SUFFIX}` is commented out. These blobs therefore do not expose an unnoticed plain/near-tracing ARMS launch. They also contain no active sysctl, THP, reserve, affinity, or disk-selection command beyond the known common setup call.

Full blobs, nearest-source diffs, exact conflict-resolution matches, and parse results are preserved in [`unreachable_runners/comparison.json`](unreachable_runners/comparison.json). This recovery adds merge-history provenance but no runnable hidden configuration correlated with the performance transition.
