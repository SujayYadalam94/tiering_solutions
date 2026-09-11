# Cgroup placement and 6.18 module audit

Read-only investigation on September 10, 2026. No benchmark, process placement,
module state, or host setting was changed. The collector and package verification
read files and can themselves cause transient cache/CPU activity.

## Findings

There is no evidence here of a newly imposed benchmark memory cgroup limit or a
replacement of the installed 6.18 module binaries. The current tmux pane is in a
systemd user scope; evidence strongly supports tmux having this capability before
May. Exact May benchmark cgroup membership remains unverified.

### Current placement and limits

The tmux pane's bash PID 2725 belongs to:

```
/user.slice/user-20020.slice/user@20020.service/tmux-spawn-baa84965-f14b-4cb0-b686-3f30f8d5bff6.scope
```

Its tmux server PID 2724 is in `session-1.scope`. The investigation shell is in
`session-3.scope`. No live BC benchmark was present during the inspection, so its
membership was not measured. Ordinary children inherit the launcher's cgroup
unless a launcher explicitly moves them.

For the tmux shell and investigation session, every applicable ancestor had
`memory.max=max`, `memory.high=max`, `memory.low=0`, `memory.min=0`,
`memory.swap.max=max`, and `cpu.max=max 100000`. Memory high/max/OOM events and
CPU throttling counters were zero. The cpuset and I/O controllers were not enabled
below the cgroup root; the root allowed NUMA nodes 0–1 and CPUs 0–39. Separately,
the shell's process CPU affinity was 10–19,30–39, consistent with setup's taskset;
that is not a cgroup restriction. Large pids.max values are recorded in the raw
snapshot and should not be described as all resource controls being unlimited.

The tmux scope retains approximately 12.8 GB of file cache. This is usage across
NUMA nodes, not a 12.8 GB reservation, hard limit, or measure of fast-node occupancy.
No MEMTIS/HTMM cgroup was found. The ordinary measurement/setup/runner paths
searched contain no cgroup placement command; measurement_memtis.sh explicitly
creates and enters its own group.

[Snapshot with every inspected ancestor and module identity](snapshot.json).

### Evidence before the performance investigation

* `/var/log/apt/history.log.8.gz` records the upgrade to the currently installed
  tmux `3.4-1ubuntu0.1` on September 17, 2024. `/usr/bin/tmux` retains that inode
  change date. Its Debian changelog explicitly records enabled systemd/cgroup
  integration, and the installed binary contains the `tmux-spawn-*.scope` code.
* `/var/log/apt/history.log.4.gz` records the currently installed systemd and
  libpam-systemd `255.4-1ubuntu8.12` on February 23, 2026. The PAM SSH session path
  invokes pam_systemd; its configuration retains pre-May metadata.
* `/var/log/syslog.3.gz` contains a tmux-spawn scope accounting message on August
  18, 2026, and a new tmux scope creation at 04:34:45 CDT that day. This precedes
  the recovered August 31 MEMTIS clone date.
* An August 30 BC OOM log explicitly names a tmux-spawn memory cgroup. It says
  `CONSTRAINT_MEMORY_POLICY` and `global_oom`, which does not establish a cgroup
  memory-limit OOM.
* cgroup-tools and libcgroup2 are not installed now. These optional management
  packages are unnecessary for kernel cgroups or tmux/systemd integration.

[Selected original syslog records with line numbers](historical_tmux_logs.json).
The retained journals/rotated system logs do not recover May benchmark membership.
An unavailable user bus, different launcher, or disabled memory controller in an
old boot could change placement/accounting without replacing tmux. None is
established by these records. Even unlimited memory groups can affect accounting
and reclaim organization, so absence of limits alone does not prove zero effect.

Primary references: [tmux 3.4 systemd scope creation source](https://raw.githubusercontent.com/tmux/tmux/3.4/compat/systemd.c),
[Linux 6.18 cgroup v2 documentation](https://www.kernel.org/doc/html/v6.18/admin-guide/cgroup-v2.html).

### 6.18 kernel and modules

* `dpkg --verify linux-modules-6.18.1-061801-generic linux-image-unsigned-6.18.1-061801-generic`
  completed with exit code 0 and no reported package checksum mismatches.
* All 6,812 `.ko*` files under `/lib/modules/6.18.1-061801-generic` appear in the
  installed module package file list. No additional unlisted module file was found.
* The latest inode change time of those module files is March 5, 2026, matching
  their original installation in the dpkg log. No later module installation is
  recorded for this kernel in retained logs.
* All 91 loaded modules have GNU build-ID notes matching their on-disk module
  files, including PMEM, uncore, storage and networking modules. This identifies
  their builds; it is not a checksum of runtime-patched kernel memory.
* No HTMM, MEMTIS, or memeater module was loaded.

This verifies today's installed files and loaded build identities. There is no
May lsmod snapshot here to establish that precisely the same set of drivers or
module parameters was active then. Page-allocation, memcg and core migration
behavior is primarily built into this kernel rather than supplied by a replaceable
ARMS/MEMTIS module. The checked kernel config enables CONFIG_CGROUPS and
CONFIG_MEMCG, with CONFIG_MEMCG_V1 disabled.

An auxiliary verification of tmux/systemd/libpam-systemd reported a checksum
difference only for `/usr/share/doc/systemd/README.logs`; it did not report a tmux
or systemd executable/PAM-module mismatch.

The observed `/proc/cmdline` still includes `debug`, `ignore_loglevel`, `earlycon`,
`rd.shell`, `rd.break`, and `memmap=92G!2G`. This records the effective boot state;
it does not infer which particular change the user meant by reverting GRUB.
