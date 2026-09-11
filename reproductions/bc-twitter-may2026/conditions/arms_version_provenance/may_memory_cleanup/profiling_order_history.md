# Historical profiling and launch-order follow-up

September 9, 2026. This is a read-only examination of retained shell histories,
230 May–June script/notes editor saves, sourced workload-list saves, and original
`times/c220g5` results. No benchmark was run. Exact commands, source hashes,
editor dates, timing sequences and inferred preparation intervals are in
[profiling_order_history.json](profiling_order_history.json).

**A concrete historical change exists in the workload selection surrounding BC
Twitter: the May 14 long study ran BC Twitter alone, whereas the June 13–14 long
study traversed an eight-workload suite. The later study also spent substantially
longer outside the timed workload between model and ARMS.** These facts identify
different preparation context; they do not prove fragmentation or its cause.
No positive record of an overlapping May profiler or background memory consumer
was recovered.

## A sourced workload list changed even though the main runner barely changed

The retained `measurement_workloads_long.sh` save
[q6Tj.sh](launch_order_editor/q6Tj.sh), dated **May 14 06:16:28.572 UTC**,
enables only `bc-twitter.sg-long`. The next retained save, May 15 02:52:17.200,
switches to PR Twitter only. The original 4035 BC files independently confirm
three consecutive model→ARMS pairs, from May 14 07:57 through 09:06 UTC, with
no other retained 4035 workload between them.

The **June 13 22:27:19.338 UTC** save
[xqTW.sh](launch_order_editor/xqTW.sh) enables:

1. BC Kron, long
2. PR Kron, long
3. BC Twitter, long
4. PR Twitter, long
5. FAISS, long
6. XSBench, long
7. MG, long
8. DuckDB TPCH, long

The original 4040 results confirm that traversal. BC Twitter's three model→ARMS
pairs are separated by the remaining workloads and the next repetition's Kron
workloads. This difference is in the **sourced workload-list file**; comparing
`run_all_measurements_long.sh` alone misses it. The BC command itself and the
model→ARMS order within BC did not change in these retained snapshots.

The saved boot inventory places the May cohort in the boot starting May 14
07:30:49 UTC, and June in the boot starting June 13 22:10:24 UTC. The first May
BC model begins approximately 14.6 minutes into its boot; the first June BC
model begins approximately 97.5 minutes into its boot, after the Kron workloads.
These start estimates subtract saved elapsed times from original completion
mtimes. They are not directly captured process-start timestamps.

## More elapsed preparation before ARMS in June

For each model→ARMS transition, subtract ARMS's saved `real` time from its
completion timestamp, then subtract the previous model's completion timestamp:

| Original long cohort | Run 1 gap s | Run 2 gap s | Run 3 gap s |
|---|---:|---:|---:|
| May 14, 4035 | 44.800 | 44.803 | 44.960 |
| June 13–14, 4040 | 64.560 | 69.338 | 91.984 |

Thus June has about **20–47 seconds more untimed elapsed work before ARMS**.
Both saved callers still invoke preparation before ARMS. This is consistent
with setup/defrag taking longer after a different workload history, but the gap
also includes any other untimed work, scheduling delay, or pause. There is no
per-command May/June preparation log identifying which operation consumed it.

This is a historically supported place to investigate an allocation-layout
effect. It is not evidence that the reserve ratio was changed, a profiler
remained alive, or a specific number of MiB was unavailable.

## The May 7 cost sweep also differs from today's isolated pair

All three original May 7, 4009 BC ARMS cases immediately follow the **PR Kron
model at cost 1.5**, which took approximately 304–308 seconds. The inferred
preparation intervals before BC ARMS are **43.802, 43.667 and 44.069 seconds**.
BC then runs in this recorded order:

`ARMS → model cost 0.125 → model cost 0.25 → model cost 0.5 → model cost 1.0 → model cost 1.5`

Each transition has the previously audited approximately 43–44 seconds of
preparation. Consequently, the selected original model at cost 0.5 was preceded
by two other BC model configurations; it was not immediately after ARMS as in
the current isolated diagnostic pairs.

The May 4, 4008 sweep similarly puts BC ARMS after PR Kron's final model cost,
then 0.25 before the selected 0.5 model. These are positive records of different
prior workloads, not a claim that setup was omitted or graph cache was retained.

There is an important limit: **slow May 7 standard BC already followed Kron,
while slow May 14 long BC ran alone**. Therefore “preceded by Kron” cannot by
itself be a universal explanation of all the slow historical cohorts. Likewise,
the model-before-ARMS order is the same in the May and June long cohorts. The
evidence supports preserving exact prior-workload context when reconstructing a
cohort; it does not identify a single causal ordering rule.

## No recovered May profiler invocation

The three retained user histories contain **no invocation of `perf record`,
`perf stat`, `perf mem`, `perf top`, BPF tools, PCM, `strace`, `heaptrack`,
Valgrind or an explicit shell background job**. The March-saved history contains
`perf` discovery/version commands and repeated `watch numastat -m`; these do not
establish an active profiler in a May boot. No active profiling command or
background operator appears in the 230 audited May–June script/notes saves.

The May-saved history does contain MLC experiments and parsers referring to
April 18 and April 21 run directories. These are foreground commands, and there
are no command timestamps, process snapshots or output that place them in the
May 7/14 benchmark boot. A retained file's May 11 save time does not date every
command inside it; its first 372 lines are already present in the March 26
history, as demonstrated in [the previous cleanup audit](history.md).

There are real recorded **sequential** training/normal runner launches:

- May-saved history lines 1397–1405: normal runner `;` training runner.
- Line 1425: training runner `; sudo reboot`.
- Line 1448: training runner `;` normal runner.
- Lines 1450 and 1456: normal runner `;` training runner.
- Lines 1778–1785 and 1810–1827: repeated `make clean ; make -j ;` normal runner.

These sequences can change the work done earlier in a boot, but have no
per-command date or accepted process-state record linking them to a target BC
cohort. The unique `new version` commit at line 1843 maps to `41e8adedd`, May 10
04:39:59 UTC; it is a useful chronology anchor, not a timestamp for all preceding
launches. Multiple tmux sessions and `htop` are recorded, but neither proves an
overlapping benchmark or a persistent kernel-memory allocation. No new positive
overlap candidate emerged from this search.

The **new strongest evidence is the changed sourced workload list plus longer
June preparation intervals**. Their effect on page layout remains unmeasured
historically. They are a better grounded investigation target than assuming an
unrecorded profiler or an arbitrary hidden memory limit.
