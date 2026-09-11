# May 7 SSD-assumed ARMS history follow-up

Read-only investigation, September 9, 2026. The user stopped the HDD experiment and requested renewed investigation of the original May 7 measurements assuming SSD storage. No new benchmark or host-setting change was made in this follow-up. All dates below are UTC. Today's source experiments are excluded as historical explanations.

**Subsequent correction:** the user's 4078 BC-Kron results also show a collapsed gap, driven primarily by faster ARMS. The [BC-Kron cross-check](bc_kron_4078.md) supersedes this report's ranking of the BC-Twitter predecessor sequence as the strongest lead. It also checks the first-workload premise against original May 7 timestamps: BC-Kron followed DuckDB, although it was the first graph.

**No recovered change establishes the cause of the original slowdown.** This pass finds additional *temporary, uncommitted* changes that reached normal ARMS, improves their dating, and finds timestamped Copilot conversations near the experiment. It does not establish that the temporary code produced the May 7 results. The most concrete remaining setup difference to reproduce is the actual workload sequence before each policy, using the historical settings and SSD.

The original May 7 ARMS times remain **112.781, 118.355, 116.275 seconds**, mean **115.804**. The contemporaneous cost-0.5 model mean is **100.127**, a **15.677-second** gap. These are the original `c220g5` files; the matching `c220g5_final` copies have later dates. [Original file hashes and chronology](../arms_version_provenance/timeline_findings.md).

## Additional coverage

- Inventoried all **130** retained VS Code resource histories. **68 resources / 566 saves** fall between May 7 00:00 and September 9 00:00. The inventory includes scripts, source, notes, and notebooks; unrelated configuration content was not inspected. [Inventory](resource_inventory.json).
- Emitted **406 source transitions across 49 repository files**, from May 7 09:05 through September 8. This includes intermediate saves that do not survive in Git. [Transitions](all_source_transitions.json), [reproducible audit script](audit_saved_sources.py).
- Preprocessed **98 retained `arms_kernel.cpp`, `migration_worker.cpp`, and `page.cpp` saves** with normal C220G5 ARMS defines, and inspected the resulting differences. All preprocessing completed. This isolates branches; it is **not** a build-success assertion: some saves have incomplete C++ syntax, and supporting headers were held at the frozen May baseline. Header/build histories were also inspected separately. [Active-branch diffs](arms_active_transitions.json).
- Parsed **62 May–June repository notebook snapshots** for setup/NUMA/compaction/MSR commands and memory readbacks. The only matches were references to parsing `max_dram_hugepages` logs, not hidden settings writes or pre-run memory measurements. [Search](notebook_setting_search.json).
- Found **30 Copilot transcript files / 5,317 events**, spanning April–June, including May 7 and May 9. These contain timestamped requests and tool-call metadata; most tool completions preserve only success status, **not stdout**. Seven retained tool-result text resources were also searched. [Transcript inventory](copilot_transcript_inventory.json), [relevant messages](copilot_relevant_messages.json), [resource search](copilot_resource_search.json).
- Checked the later Codex history through September 8 for relevant launch/settings requests. Its earliest retained sessions are July, so it supplies no May execution record. The Copilot session database contains three later sessions and no May turns. [Filtered earlier requests](codex_pre_sept9_relevant_requests.json).
- Rechecked the three shell-history files and existing command-date anchors. The May-11-saved history does not date all its commands to May: its first 372 lines are already present in the March backup. No new May lowmem, THP, compaction, CPU/uncore, or reservation override was recovered. [Shell dating and cleanup evidence](../arms_version_provenance/may_memory_cleanup/history.md).

## Suspicious source changes, with the timing limits preserved

| Evidence | Effect on normal ARMS | Relevance to the May 7 target |
|---|---|---|
| May 9 **06:12:35**, worker save `iMTY.cpp`, changes migration-cost denominators from submitted pages to submitted minus initially failed pages. **06:12:55**, `eUcM.cpp`, restores them. | Would increase measured per-page cost when migrations fail. The temporary code still checks submitted count, so an all-failed batch can produce a zero denominator. | A real 20.478-second saved experiment, not a persistent fix. May Git and the restored worker are identical. A May 9 06:13:34 transcript requests success-only accounting, but contains no subsequent completed implementation. No evidence links the transient version to May 7. |
| May 9 **06:57:13**, `gqa1.cpp`, multiplies demotion cost by **15**; **07:01:23**, `ZVzh.cpp`, uses **5**; **07:06:10** briefly restores 15; **07:06:20**, `1WXs.cpp`, restores **1**. | This line is outside `USE_MODEL`; it really does change ARMS's replacement decisions. It could reduce migrations or reject beneficial exchanges. Direction of runtime effect is not established. | The 1→15→5→15→1 transitions are dated after May 7, and no retained result/binary is tied to these few-minute states. Reverting today's ARMS to them would be a new sensitivity test, not a reconstruction of a known May 7 condition. |
| First retained kernel save, May 9 **06:36:33**, contains unconditional `std::cout << "migrations: " << decision.migrated_count << std::endl;`. **17:39:15**, `s1mT.cpp`, comments it out. | Executes even with `ARMS_VERBOSE=false`; adds output and flushing in candidate selection. Thus ordinary verbose-off comparisons alone would miss it. | Worth recording as a weak historical candidate. It is absent from May 7 Git, but this file retains only its latest 50 editor saves, all beginning May 9. **The first retained save does not prove when the print was introduced.** No May 7 binary/stdout proves it was active then, or that it caused a large delay. |
| May 9 **21:58:05**, `KJoS.cpp`, adds `cold_page->can_demote`; May 11 **02:14:26**, `OxR3.cpp`, initializes it. | The intervening state can reject demotions using an uninitialized flag. | The original May 7 source predates the gate. The existing coherent May reconstruction already omits the gate; it still ran faster than the archive. This remains insufficient, unlike simply removing the initialization from modern code. |
| Verbose enabled locally July 17 **05:13:40**, then disabled locally **July 29 22:48:58**. | Can add logging/counter overhead. | Local history dates disabling earlier than the September commit alone suggests. Both dates are after the June fast ARMS cohort, so this does not explain the earlier transition. May's closest retained defs already has verbose false. |

The exact worker and cost-factor patches are in [the ARMS branch comparison](arms_active_transitions.json). The source comparison deliberately uses May Git for the first ARMS-branch baseline: a much older editor save otherwise makes the worker's thread-registration code falsely appear new on May 9. That registration is already present in May Git.

Most other May 9 candidate-cost, boundary-score, prediction-smoothing, and eligibility experiments are compiled out of normal ARMS. Some static helpers remain in preprocessed source but have no ARMS call site. Their presence alone is not runtime overhead. The changes to model feature count in June also do not execute in normal ARMS. [Separate model compatibility evidence](../arms_version_provenance/root_cause_code/README.md).

## What the newly found conversations establish

The May 7 conversation records notebook filename-parser work around **03:19–03:45**, then reads/discussion of virtual-step timing around **04:11–04:13**. It does not preserve a setup invocation, a different ARMS build flag, or a memory/THP readback for the 4009 runs. The relevant notebook changes alter parsing/group labels, not original timing-file contents; the original file comparison therefore remains the reliable reference.

The May 9 conversations corroborate model-cost experimentation and the request to change migration-success accounting. A saved terminal result at **06:19:50** shows `8d6a8bc66` as HEAD, consistent with the reflog chronology. It is the output of `git show`, **not** a complete uncommitted-worktree snapshot or loaded-binary manifest. Replayed conversation messages can retain earlier timestamps, so transcript-file modification times are not used to date every contained action.

## A specific launch-condition difference remains worth reproducing

Every May 7 BC ARMS run follows the **PR-Kron model, cost 1.5**, then approximately 44 seconds of preparation. Within BC, the order is:

```text
PR-Kron model [2,10,1.5]
  -> full preparation -> BC ARMS
  -> full preparation -> BC model cost 0.125
  -> full preparation -> BC model cost 0.25
  -> full preparation -> BC model cost 0.5
  -> full preparation -> BC model cost 1.0
  -> full preparation -> BC model cost 1.5
```

The old cost-0.5 model therefore has two intervening BC executions after ARMS. The isolated ARMS→cost-0.5 reconstruction does not reproduce that history. On **September 7 19:55:15**, saved workload list `WXPu.sh` explicitly moves BC Twitter to the beginning, ahead of both Kron workloads. [Exact later list change](all_diffs/1788810915383_-198ec96b_WXPu.sh.diff), [original May predecessor evidence](../arms_version_provenance/may_memory_cleanup/profiling_order_history.md).

This is compatible with consistent alternating results in one boot: the same predecessor chain is repeated for each policy. It does not require a disk switch or a random extra memory consumer. Preparation drops caches and invokes compaction, but does not prove identical post-preparation physical-page layout. The existing runtime diagnostics show that ARMS is particularly sensitive to splitting and compaction; **connecting that sensitivity to the historical sequence is still an untested hypothesis**.

The separate May 14→June long series has a different documented sequence change: BC-only becomes a full suite. It cannot be reduced to "Kron always makes ARMS faster," because May 7 was slow despite having Kron predecessors. Exact policy, duration, and order matter. [Cohort-specific reconstruction proposal](../speed_matching/README.md).

## Assessment

No dated persistent ARMS change in these histories explains the missing May 7 slowdown. The code candidates above should not be presented as a discovered root cause. In particular, the faster June long runs occur while the nearest retained ARMS core/header/worker/build saves match the May 14 versions; later July/September changes cannot explain that transition on their own.

The strongest historically grounded next comparison remains the **documented May 7 predecessor and BC cost sequence on SSD**, using the frozen May code/models and unchanged historical settings, versus the isolated pair. The appropriate observations are per-node free memory, zone watermarks/buddy state, THP splits, compaction, and the Read Time/iteration split. This would test an actual recovered difference rather than selecting a new reserve ratio to force the totals to match. It was **not run** in this follow-up.

The HDD job is stopped. Its saved controls were restored and independently verified before this investigation; the interrupted repetition is excluded. [Stop/restoration verification](../hdd_historical_speedup/stop_verification.json).
