# Shell-history evidence for huge-page and compaction settings

Read-only audit of three retained user shell histories, filtered root history, setup/defrag editor saves, and the relevant GRUB backup options. No benchmarks, settings writes, mounts, or source changes were performed.

## Main finding

**Manual huge-page/compaction experiments did occur, and some direct ARMS runs immediately followed overrides without setup. These are March experiments, not evidence of changed settings in the May cost ablation or the May-to-June transition.** The May-11-saved and current history files contain no manual THP/compaction-setting writes matching those later periods.

There is a real distinction between invocation paths: standalone `measurement_arms.sh` does not run setup; the normal model runner prepares each configuration. The archived normal/long `run_all_measurements` scripts separately prepare ARMS, so the standalone asymmetry does not establish an omission in those sweeps. `defrag.sh` drops caches/compacts memory and sets top-level THP enabled/defrag; it does not reset every VM/khugepaged knob.

## Real manual setting changes and their order

The following line numbers refer to `/users/zimooo2/.bash_history-09434.tmp`, saved March 26 at 05:28:10 UTC. Individual commands have no timestamps or saved success/readback output.

| History lines | Recorded commands | What their order supports |
|---|---|---|
| 452–458 | Explicit `compact_memory=1`; edit compaction_proactiveness; write `50`; write khugepaged scan sleep `100 ms` | Real requested manual compaction/scan settings. No clear benchmark is recorded between these writes and subsequent setup. |
| 469–477 | `write_sysfs_value ...compaction_proactiveness 20`, then setup and direct ARMS | The helper call may not have existed in the interactive shell; setup follows before ARMS. This is not proof ARMS used 20. |
| 494–497 | Reserve ratio `256 256 32` → direct ARMS; ratio `32 32 16` → direct ARMS | The second direct run has an explicit altered reserve value and no intervening setup command. |
| 509–527 | Defrag → direct ARMS; scan sleep `1000`; later scan sleep `10` → direct ARMS at line 525; setup only at 526 | The cleanest scan-sleep example is **10 ms immediately before standalone ARMS**, followed by setup for the next run. The 1000-ms section also contains a malformed command, so its effect is less certain. |
| 627–634 | Reserve ratio `4 4 4` → direct ARMS; defrag → direct ARMS; then setup/defrag → direct ARMS | Defrag alone does not restore the reserve ratio. A requested manual override can therefore survive the defrag-only direct run. |
| 592–604 | `kernel.nmi_watchdog=0`; perf writes; later GRUB editing and reboot | NMI watchdog is not reset by ordinary setup, but this recorded override precedes an explicit reboot. It does not establish a May value. |

Other reserve experiments include `1 1 1` at line 394. Later normal setup explicitly writes `256 256 32`, scan sleep `0 ms`, and compaction proactiveness `80`, so successful full setup replaces these particular overrides. These requested setting differences are concrete; history alone cannot establish that every command succeeded or attribute a benchmark time to them.

The command at line 259, `sudo cat echo 1 | sudo tee .../use_zero_page`, is malformed; it is not reliable evidence that zero-page use was changed. Other references to `max_ptes_none`, `shrink_underused`, per-size THP enabled files, and `use_zero_page` are reads. No accepted manual change to those unreset THP knobs was recovered.

## Dating the commands

File modification dates are upper bounds on retained history, not dates for every command. The main manual-override block also has useful commit-message anchors:

- Line 212, `git commit -m "checkpoint"`, matches `6822f5e08`, **March 11 02:15:42 UTC**.
- Lines 536/538, `"seemingly working minor fragmented page support"`, match `e6223c95a`, **March 15 00:39:26 UTC**.
- Lines 549/553, `"logging"`, match `fedcde1ee`, **March 15 23:05:27 UTC**.

Thus the reserve/scan/compaction block through line 525 is consistent with **March 11–15**, corroborated by dated March 13–14 setup editor saves. Shell histories can merge sessions and contain failed commands; these anchors provide supporting chronology rather than exact per-command timestamps.

## Direct model-to-ARMS calls in the later history file

The file saved May 11 contains this explicit sequence:

```
317 bash measurement_model.sh 1 1_train _train faiss_10M
318 bash measurement_arms.sh 1 1_train _train faiss_10M
319 make clean ; make -j
320 bash measurement_arms.sh 1 1_train _train faiss_10M
321 bash measurement_arms.sh 1 1_train _train XSBench
```

There is no intervening setup/defrag command in that sequence. Repeated direct training ARMS runs continue through line 325; line 332 records a normal direct ARMS command following a training runner. These are **FAISS/XSBench training-era/direct invocations, not the BC Twitter cost ablation**.

They are also not dated May 11: line 272's `"times and model fix"` matches `2cb2ac9cf` on **March 24 14:59:53 UTC**, and line 469's `"save times"` matches `08f552ec3` on **March 28 22:31:17 UTC**. Their saved order is therefore consistent with **March 24–28**. The current 2,000-line shell history contains no direct `measurement_arms.sh` or `measurement_model.sh` invocation. No recorded absolute-path benchmark launch from another working directory was found in any of the three histories; bare filenames alone cannot prove every invocation's working directory or success.

## Brief editor-only attempt at unreset khugepaged knobs

The March 13 **20:27:28 UTC** setup save `IlY9.sh` adds:

```
max_ptes_none   1024
max_ptes_swap   1024
max_ptes_shared 1024
```

All three writes are removed by the **20:31:10 UTC** save `dSQ5.sh`; the exact diff and both snapshots are saved here. The retained local 6.2 kernel implementation rejects values greater than `HPAGE_PMD_NR - 1` in all three stores (`linux-hybridtier/mm/khugepaged.c:263`, `:288`, `:314`), which is 511 for 2 MiB PMD hugepages and 4 KiB base pages. Thus this attempted value is out of range on that implementation. There is no successful write/readback showing these settings took effect or persisted into May. Later setup does not reset these knobs, but this rejected-value candidate is weak evidence for an inherited difference.

## Root history, GRUB, and mounts

Filtered root shell/Vim history ends March 19 and contains no relevant THP, compaction, NUMA, or benchmark-mount matches. The March 16 **22:06:09 UTC** GRUB backup contains `memmap=90G!2G` but **no explicit THP, hugepage-size/count, compaction, or NUMA-balancing boot option**. The memory-map restriction is distinct from a THP-policy setting and does not establish either May/June boot's configuration.

The May-11-saved history contains mount attempts for logs/ANN data, but no GAPBS literal or graph-directory mount. The current history has many GAPBS mount attempts. Neither records command timestamps or device-success readbacks, so these are not proof of a disk change within a matched ARMS/model sweep.

## Evidence files

- `filtered_histories.json`: setting/mount matches, direct policy invocations, nearby relevant commands in original order, file dates/hashes, and absent command timestamps. Unrelated history and credential-related context are omitted.
- `history_commit_anchors.json`: exact matching Git subjects, full hashes, author and committer dates.
- `setup_editor_settings.json`: all retained setup/defrag saves and their relevant active lines.
- `setup_IlY9.sh`, `setup_dSQ5.sh`, `max_ptes_attempt_removed.diff`, and latest historical `setup_X6Fx.sh`.
- `privileged_read_summary.json`: filtered root-history metadata and relevant GRUB-option results.

The surviving evidence supports manually different conditions for some **early standalone ARMS tests**. It does not establish manual THP overrides for the archived May cost ablation, nor identify a THP-setting change responsible for the May/June gap collapse.
