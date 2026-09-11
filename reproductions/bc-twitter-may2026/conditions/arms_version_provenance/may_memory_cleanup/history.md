# May memory consumers and cleanup: retained-history audit

Read-only follow-up, September 9, 2026. Sources are the three retained user shell
histories, VS Code script/notes saves, and the setup/cleanup Git versions tied to
the original c220g5 timings. No host setting, benchmark, existing report, or
running-study file was changed. Filtered source evidence and hashes are in
`evidence.json`.

**No recovered record identifies an active extra memory reservation, cgroup
restriction, or failed cleanup during the May cost-ablation/normal timing
cohorts.** There are real cleanup limitations and missing historical readbacks,
but newly examined context dates the apparent module-loading evidence to March.

## New provenance result: May-saved module commands are March commands

The first **372 lines** of `.bash_history-05835.tmp` (saved May 11) are exactly
identical to lines **1629–2000** of `.bash_history-09434.tmp` (saved March 26
05:28:10 UTC). This is a byte-for-byte line comparison, recorded in the evidence
file. Thus the early module loads in the May-saved file are already present in
March; their file's May date must not be treated as the execution date.

Examples in the May-saved file:

```text
35 cd SoarAlto/
37 cd src
39 cd ../run/nomad_module/
41 sudo insmod async_promote.ko
42 sudo rmmod async_promote
...
68 cd SoarAlto/
70 cd run/
72 cd nomad_module/
74 sudo insmod *.ko
```

The wildcard loads at lines **158** and **179** likewise follow navigation into
`SoarAlto/run/nomad_module`. They are not evidence of loading `memeater`. Recorded
reboots occur at lines 27, 58, 64, 145, and 172. The `async_promote_main.c` editor
save is itself dated **March 22 02:54:22 UTC**
(`History/5df1659a/wspj.c`). No May accepted module list survives to show whether
that module was loaded during either benchmark cohort.

The ordinary cleanup does not unload `async_promote`, so it is a concrete
example of module state that cleanup would not reset. However, May's normal
ARMS/model setup disables the NOMAD NUMA settings, the audited benchmark callers
do not invoke the NOMAD loader, and no record places an active NOMAD module in
the target 6.18 boot. Treating these March commands as the May cause would be
unsupported.

## Memeater was an older setup mechanism, but no May activation was found

Git `6b2f5ddf7`, **March 9 21:40:05 UTC**, comments out the previously active
`sudo insmod memeater.ko sizeMiB=${alloc_mib}` in setup. March 14 `d87369bce`
comments out the surrounding allocation calculation/directory block as well.
The May 4, May 7, May 14 and June setup sources retain the commented load.

All **50 retained setup editor saves** (March 13–22) were checked for active
memeater loads, cgroup restrictions, and memory-limit commands; none restores an
active memeater load. No retained user history contains a literal memeater load.
There is no retained `unsetup.sh`, memeater, cgroup configuration, GRUB, or fstab
editor resource.

This substantially weakens a routine May setup accidentally reserving additional
RAM with memeater. It does not exclude a load through an unrecorded external
script or a module already present in a boot for which no module list survives.

## Cleanup failure is possible in the code, but not demonstrated in the logs

`unsetup.sh` is the same Git blob
`8d7958ffadf5657021bfb3a55538e39ad4b71aa4` at all four relevant revisions:
May 4 `6b1bc11dd`, May 7 `8d6a8bc66`, May 14 `4da3d6c3f`, and June
`d6f720be6`. Its complete content is:

```sh
sudo pkill -f jupyter-notebook
sudo pkill python
sudo rmmod memeater.ko
```

The common helper invokes it before setup with `|| true`; later setup success
does not prove that module removal or process termination succeeded. The script
does not wait for Python exit, confirm memory reclamation, clear cgroup limits,
or stop arbitrary non-Python benchmark processes. Setup subsequently attempts
to move existing process pages to node 1 and changes affinity, which is not
equivalent to proving node0 was fully cleared.

These are retained code facts. There is **no saved May cleanup exit status,
`rmmod` error, process-memory inventory, module readback, or per-node pre-run free
memory** establishing that such a failure happened. An absent module could also
produce a removal error without leaving a reservation. The command's `.ko`
spelling alone is not treated here as evidence of failure.

The May 7 original timing sequence has approximately **43.2–44.0 seconds** of
inferred preparation between adjacent cases, and the May 14 long runner
explicitly prepares ARMS after the model. This supports execution of preparation
work, but cannot prove every cleanup operation succeeded. Both paths request
the same cleanup, not policy-specific removal logic.

## Cgroup restrictions and persistent/background workloads

March history lines **321–322** request creation of
`/sys/fs/cgroup/cpuset/my_app` and `/sys/fs/cgroup/benchmarking`; line 323 then
runs setup and ARMS. No retained command writes memory/CPU limits, assigns a PID
to either group, or wraps a benchmark in `cgexec` or `systemd-run`. The Git
`systemctl set-property ... AllowedMemoryNodes=1` examples in setup are comments.
Directory creation alone does not establish a restriction.

A broader scan checked **230 May–June editor saves** of tiering_solutions shell
scripts/notes for active module/reservation commands, cgroup memory/CPU controls,
PID assignments, process killers, and background-shell launches. Only the
already-known conditional NOMAD loader matched, in the May 4 common-helper saves
`uPO2.sh` and `89Uc.sh`. No active memory restriction or background job was found.

Training/data-processing commands and tmux use are present in shell history,
but no saved process snapshot proves a training job overlapped the May BC
Twitter runs. Several potentially suspicious train → standalone ARMS sequences
are inside the newly proven March history overlap. Later recorded normal →
training commands use `;`, specifying sequential shell execution; they are not
evidence of simultaneous jobs. Separate tmux sessions remain an unmeasured
possibility, not a recovered event.

The explicit current-history stop sequence—`pkill run_all_measurements`,
`pkill bc`, `sudo pkill bc` at lines **1273–1275**—follows the unique
`"final paper"` commit anchor at lines 122/123 (`d6f720be6`, **May 30 19:26:56
UTC**). It cannot be used as evidence of interrupted cleanup during May 7.
It has no per-command success output and is surrounded by later mount attempts.

## PMEM, boot maps, and mount-state limits

The refreshed exact-command search again found no `ndctl`/`daxctl` command,
PMEM driver bind/unbind, `/dev/dax`/`/dev/pmem` use, or tmpfs/hugetlbfs mount in
the three histories. March contains the explicit `memmap=86G\$8G` experiment;
later histories contain GRUB edits and reboots without their resulting values.
Those do not reconstruct the May boot's memory map or namespace metadata cost.

Recorded `numactl -m ... -N ...` overrides in the May-saved history target MLC,
not the ARMS/model BC command. The audited May measurement wrapper uses the
same allowed memory nodes and CPU set for both policies. The dated March 28 and
June 17 notes concern mounting logs and ANN data, not GAPBS graphs. Untimestamped
mount attempts have no accepted mount-state readback and cannot establish the
May graph device or hidden reserved memory.

The current boot's `memmap=92G!2G` and measured PMEM metadata cost are known from
the parent investigation; they cannot be projected backward as proof of May's
effective memory headroom. Original c220g5 May 7 timings, not the May 11 final
copy dates, remain the correct chronology for the cost-ablation comparison.

## What this changes

The newly exact history overlap eliminates the May-saved wildcard module loads
as positive May evidence. The retained setup/cleanup versions give no routine
extra reservation or cleanup-policy difference between normal and cost-ablation
runs. A consistent boot-level shortage caused by unrecorded reserved memory or
an unremoved consumer is still possible, but the surviving history does not
identify one. The missing evidence is the historical effective state, not an
unresolved ambiguity in whether the committed May setup actively loads memeater.
