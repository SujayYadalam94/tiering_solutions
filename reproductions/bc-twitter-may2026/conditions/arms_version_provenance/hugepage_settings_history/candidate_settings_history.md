# Untested settings supported by retained history

The subsequently authorized table tests are now complete; this document records
the evidence and test status before them. See the
[14-run results](../../historical_settings_tests/results.md) and
[focused reserve-ratio follow-up](../../historical_settings_tests/reserve_history_followup.md).

Read-only follow-up after the khugepaged defrag/allocation-sleep/scan-sleep tests,
2026-09-09. No workload or host-setting changes were performed. Exact filtered
commands, editor-save timestamps/hashes, Git source lines, and retained condition
overrides are in `candidate_settings_history_evidence.json`.

**There are concrete old alternative values worth distinguishing from guesses,
but no new May-to-June override was found.** The three retained user histories
contain no manual writes of watermark boost, minimum-free memory, zone reclaim,
or pages-to-scan beyond the March evidence below. The later file dates do not
date their individual commands.

## Ranked historical candidates

This ranks the strength and usefulness of actual historical evidence, not a
prediction of benchmark improvement. “Untested” means no isolated override in the
retained condition-session manifests, including the completed three-control
khugepaged study; the current default values were exercised by baseline runs.

| Rank | Candidate and current experiment default | Exact evidence | Runtime provenance / test status |
|---|---|---|---|
| 1 | `vm.lowmem_reserve_ratio="32 32 16"` or `"4 4 4"`, versus `"256 256 32"` | March-saved shell history lines **496 → 497** write `32 32 16` then directly run ARMS; lines **627 → 628**, then **629 → 630**, write `4 4 4`, run ARMS, defrag, run ARMS again. | Strongest evidence of a requested alternative immediately before an actual policy command. No successful-value readback or matching timing retained. Neither alternative has an isolated retained condition test. |
| 2 | `vm.min_free_kbytes=16384` (16 MiB), versus `1048576` (1 GiB) | Committed January 19 setup (`7f73ad25b`, line 11) and March 9 setup (`6b2f5ddf7`, line 40); changed to `1048576` by **March 14 15:14:19 UTC**, `d87369bce`. Editor saves also show `163840` (160 MiB) on **March 13 19:58:00** (`30Tm.sh:40`) and **19:58:25** (`z1KC.sh:40`), and `16384` in intervening/later saves. | A real older setup condition, not evidence the May benchmark used it. Neither 16 MiB nor 160 MiB has an isolated retained condition test. The 1 GiB setting is already in the frozen May baseline. |
| 3 | `vm.watermark_boost_factor=0`, versus `10000` | January/March committed setup; changed to `10000` in `d87369bce`. Dated editor saves show `0` at **March 13 20:12:43** (`RdRZ.sh:38`), `100` at **21:04:47** (`QfSZ.sh:38`), and `15000` in **19:58:00** (`30Tm.sh:38`). | `0` has committed historical support; `100`/`15000` are editor-only alternatives. No recovered direct shell write or isolated condition test for any of these alternatives. |
| 4 | `khugepaged/pages_to_scan=4096`, versus `8192` | Editor saves from **March 13 20:27:28** (`IlY9.sh:60`) through early March 14 explicitly write `4096`; **March 14 03:56:09** (`SBKh.sh:58`) changes to `8192`. `UlLy.sh:58` briefly returns to `4096` at **March 14 21:20:32**; `foBl.sh:58` restores `8192` at **23:13:06**. | No manual shell write or run/readback proves a particular saved version executed. `4096` is untested in the retained isolated sessions. `9Jnq.sh:58` also contains the unusual `840962` at **21:19:51**, lasting 40 seconds in editor history; that is a weak transient-edit candidate, not a proven benchmark condition. |

Full non-MEMTIS setup explicitly sets all four knobs to the baseline values.
Therefore an earlier manual value can affect a standalone ARMS call or a
defrag-only preparation, but successful full setup replaces it. The archived
normal and long sweep runners explicitly prepare ARMS; the standalone ARMS script
does not. This establishes a possible early direct-run difference, not a hidden
policy asymmetry inside the archived cost-ablation sweep.

## Two misleading candidates to avoid

- **`watermark_scale_factor=1` is not the effective result of a March 8/9 setup
  reversion.** `7d4508f98:setup.sh` writes 1 at line 15 and then 10 at line 29;
  `6b2f5ddf7` does the same at lines 37 and 51. January 19 setup does have only
  the 1 write. Some March 13 editor saves also remove the later duplicate and
  write 1, but a Git patch displaying `1 → 10` alone overstates the effective
  March change. An editor-only `10000` at `uEfM.sh:37` lasts 13 seconds before
  returning to 10; no accepted runtime value was recovered.
- **Zone reclaim has no recovered alternative value.** March 8 onward committed
  setup and all relevant editor saves explicitly set `zone_reclaim_mode=0`.
  Earlier setup has only a commented example of 0. None of the three shell
  histories records a zone-reclaim write. There is no history-backed value of 1,
  2, or 3 to reproduce.

## Date bounds and inherited state

The direct `32 32 16` example is in
`/users/zimooo2/.bash_history-09434.tmp`, saved March 26 05:28:10 UTC.
Line 212's `checkpoint` commit matches **March 11 02:15:42 UTC** (`6822f5e08`);
lines 536/538 match **March 15 00:39:26 UTC** (`e6223c95a`). Its saved order is
consistent with March 11–15. The `4 4 4` example follows the March 15 `logging`
commit anchor at lines 549/553 and a recorded reboot at 604, with only the
March 26 saved-file upper bound thereafter. Shell-session merging and missing
per-command timestamps prevent exact dates or boot attribution.

The May-11-saved history and current history provide no additional writes of
these candidate knobs. They also provide no ARMS CPU/memory pinning override.
Recorded `numactl -m ... -N ...` commands in the May-11-saved file target **MLC**,
not BC Twitter. Current-history `OMP_NUM_THREADS=...` commands target graph
converters and are command-local assignments; they do not establish an inherited
ARMS thread setting.

March history lines 321–322 create cgroup directories (`cpuset/my_app` and
`benchmarking`), but no retained command sets their CPU/memory limits or places
the benchmark PID in them. Setup follows at line 323. Directory creation alone
does not establish a cgroup restriction. No shell assignments to `IRQ_AFFINITY`,
`PROCESS_CPUSET`, `CPU_SLOWDOWN_CPUS`, or migration-node environment overrides were
found. `kernel.nmi_watchdog=0` at line 592 is a real unreset override, but the
recorded reboot at 604 prevents treating it as persistent evidence for May.

The September 3 MEMTIS-only setup branch supplies other values (including boost
15000 and pages-to-scan 4096). Its explicit system selector distinguishes it from
the ARMS/model default branch; its existence does not show that the matched
ARMS/model runs inherited those settings. No active, later manual setting found
in this audit explains the May-to-June performance-gap change.
