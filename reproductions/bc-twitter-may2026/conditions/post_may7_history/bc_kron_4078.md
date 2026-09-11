# BC-Kron 4078 cross-check and revised hypothesis ranking

September 9, 2026. Read-only follow-up to the user's observation that BC-Kron also lost its historical ARMS/model separation. No benchmark or setting change was performed. SSD remains the working assumption. [Original rows, hashes, copy verification, and calculations](bc_kron_4078_evidence.json).

**The gap collapse is confirmed on BC-Kron and is predominantly faster ARMS.** This weakens the previous emphasis on BC-Twitter's particular PR-Kron predecessor as the explanation for the missing historical separation.

| Cohort | ARMS total | Model total | ARMS minus model |
|---|---:|---:|---:|
| May 7, 4009, means of three | 241.684 s | 223.724 s | 17.960 s |
| September 9, 4078 run 1 | 221.905 s | 219.060 s | 2.845 s |

ARMS improves **19.779 seconds (8.18%)**, while the model improves **4.664 seconds (2.08%)**. The gap shrinks **15.115 seconds**, approximately 84%. Both 4078 timing files report exit status 0 and no timeout. Their completion times are 21:06:52.602 and 21:11:18.022 UTC, implying 46.360 seconds between ARMS completion and model start. The stopped HDD diagnostic had already completed restoration verification at 20:32:30 UTC.

The May model row is the best mean in the contemporaneous BC-Kron cost sweep, **summary mode 2 / history 10 / cost 1.5**. This differs from the 4078 filename's **mode 1 / history 10 / cost 0.1**; it is not a claim of identical model inputs. Independently, the archived May 10 model with the latter configuration has totals **219.827, 221.003, 222.175 seconds**, mean **221.002**. That is only **1.942 seconds** slower than 4078. All three originals match the published model copies byte for byte. This reinforces the observation that the model total is approximately preserved, but names alone do not prove identical forest weights.

The current `.time` files contain total time without application Read Time or iteration output. No benchmark tmux socket survives from which to recover that output. These totals establish which policy's overall time changed; they do not locate the change in loading versus computation. Existing BC-Twitter migration/compaction probes supply a mechanism to investigate, not a historical BC-Kron phase measurement.

## Chronology correction: first graph, not first application in the May 7 cohort

The user recalled BC-Kron running first. It is the first graph in this cohort, but the original completion timestamps show **DuckDB model cost 1.5 immediately before every May 7 BC-Kron ARMS run**, following earlier FAISS and MG cases:

| Run | DuckDB model finishes, UTC | BC-Kron ARMS finishes, UTC | Inferred preparation before BC ARMS |
|---|---|---|---:|
| 1 | May 7 07:39:36.885 | 07:44:22.855 | 46.335 s |
| 2 | May 7 11:25:53.348 | 11:30:44.305 | 47.115 s |
| 3 | May 7 15:12:14.685 | 15:17:02.930 | 46.671 s |

This uses original `c220g5` file dates, not final-copy dates. These inferred intervals subtract BC's saved elapsed time from its completion time, then subtract the preceding completion time; they are not direct setup-duration logs. The current saved workload list starts with BC-Kron, followed by BC-Twitter.

Consequently this is not a controlled demonstration of "first application after identical setup" across both dates. It **does**, however, show that a special PR-Kron→BC-Twitter effect cannot be the general explanation. My earlier commentary saying BC-Kron ran first without that distinction was too broad.

## Revised assessment

- Downgrade BC-Twitter's exact predecessor chain as the leading explanation. A full-sequence replay remains a way to test historical state, but cannot be presented as an explanation already supported by both workloads.
- Focus on a shared condition that changes ARMS's cost of migration/reclaim/compaction more than the model's. The current timing pattern supports this direction; it does not prove the trigger, recover May's free memory, or establish a specific hidden reserve.
- Continue treating the source findings in the parent report with their existing limits. The transient May 9 cost changes are not tied to May 7; logging has incomplete local history; rebuilding the known May source already failed to recover the full slowdown.
- Do not infer an entire extra 2 GiB of usable memory from these speeds. Historical BC-Kron ARMS at label 6009 averaged **205.536 seconds**, still materially faster than current 4078's 221.905 seconds. The available-memory interpretation requires direct observations, not a linear conversion of runtime to MiB.

The historical cause remains unresolved. This counterexample strengthens the conclusion that ARMS's improvement is shared across graph workloads and requires a broader explanation than BC-Twitter's placement in the suite.
