# Persistent host settings and background memory follow-up

Read-only inspection, September 10, 2026. No benchmark, service, cgroup, kernel
parameter, swap device, or CPU/memory placement was changed. The collector itself
and these file reads can create transient cache/CPU activity.

## Result

No newly recovered persistent OS setting explains the ARMS improvement after May.
In particular, this pass finds no surviving post-May memory-controller defaults,
VM sysctl override, swap/zram service, or background daemon that explains several
hundred MiB of additional occupied node-0 memory in the old runs. This does not
establish the exact settings or memory placement of the May boots.

## Persistent configuration evidence

[Host snapshot](snapshot.json) inventories 229 entries under `/etc/systemd`,
`/etc/sysctl.d`, `/etc/modprobe.d`, `/etc/modules-load.d`, `/etc/udev/rules.d`,
security/PAM configuration, cron, `/etc/default`, and user/local systemd paths.
Only the already investigated September 10 GRUB files have inode change times
after May 1. No historical value is inferred from a current default alone.

[Additional inventory](persistent_inventory.json) checks regular files in
`/usr/lib/sysctl.d`, `/usr/lib/systemd/system`, `/usr/lib/systemd/user`,
`/usr/local/etc`, `/etc/tmpfiles.d`, and `/etc/profile.d`; none has an inode change
time after May 1. `/etc/environment` retains June 2024 metadata and only sets PATH.
There is no `/etc/rc.local`, `/etc/sysfs.conf`, `/etc/cgconfig.conf`, or
`/etc/cgrules.conf`. No HWP/governor/frequency write was found in the checked
systemd, udev, init.d, sysfs, or default configuration paths.

Ordinary surviving writes normally update inode change times. Deleted or later
overwritten files, whole-filesystem restoration, manual runtime commands, and
firmware settings are not excluded by this metadata check.

## Current services and memory

The host service list contains the expected CloudLab/systemd services and
`fwupd`, `multipathd`, and `smartmontools`. It has no running `systemd-oomd`,
`numad`, `tuned`, or irqbalance service. Selected service units retain February
2026 or June 2024 installation metadata; no post-May package update for these
services appears in the retained apt records. The systemd-journald `Nice=-1`
drop-in dates to the February 23 systemd installation, not after May.

At collection, `/proc/swaps` is empty, KSM `run=0`, and MGLRU `enabled=0x0000`.
`vm.overcommit_memory=1`, `vm.vfs_cache_pressure=2000`, and
`vm.zone_reclaim_mode=0`. These values match the previously recovered ordinary
setup behavior; the existence of a swap entry in the March fstab is not evidence
that swap remained enabled during old benchmarks because setup calls swapoff.

System/user ancestor memory limits are unlimited, memory low/min protection is
zero, and systemd has no explicit CPUWeight or IOWeight override there.
`ManagedOOMMemoryPressure=auto` is not evidence that oomd is running or enforcing
a threshold. This complements the earlier per-cgroup kernel-controller audit.

A follow-up read at **16:44:40 UTC** traversed the entire host cgroup hierarchy,
including competing `system.slice` service groups. It found **61 groups**, of
which **59 expose memory controls**, and **zero nondefault memory protections or
limits**: every exposed `memory.low` and `memory.min` is zero, and every exposed
`memory.high` and `memory.max` is `max`. There were no read errors. Thus a protected
sibling service is not currently reserving memory against benchmark reclaim;
this is stronger than checking only the benchmark's ancestors. It does not
recover May values or rule out ordinary cgroup reclaim organization without
protection. [All groups and raw controls](cgroup_protection.json),
[read-only collector](collect_cgroup_protection.py).

The current node-0 snapshot is particularly useful:

| Node-0 quantity | Value |
| --- | ---: |
| MemTotal | 4,737,988 KiB |
| MemFree | 2,747,308 KiB (2,682.9 MiB) |
| AnonPages | 0 KiB |
| FilePages | 6,256 KiB |
| SUnreclaim | 164,924 KiB |
| KernelStack | 4,808 KiB |
| PageTables | 2,568 KiB |

For all 66 inspected processes with RSS at least 1 MiB, `/proc/PID/numa_maps`
reports their mapped resident pages exclusively on node 1. This includes the
large VS Code/Codex processes, tmux, journald, fwupd, and other services. It rules
out treating today's several GiB of editor RSS or a large `memory.current`
counter as node-0 consumption. Shared mappings are not summed as unique memory;
these maps also do not account for every slab, page-table, or other kernel
allocation. There is no May per-process NUMA snapshot for comparison.

## Firmware check

A host read of `fwupdmgr get-history --json` returned `FwupdError` code 9,
`No history`. The installed fwupd version is
`1.9.33-0ubuntu1~24.04.1ubuntu1`; intel-microcode is
`3.20250812.0ubuntu0.24.04.1`. Retained apt history records their package changes
on February 23, 2026. This supplies no evidence of a post-May fwupd-managed
firmware update. It does not exclude a BIOS/BMC/manual firmware update outside
fwupd or an old deleted history database.

## Collection boundary

The current tool sandbox has its own PID namespace and a read-only cgroup mount.
Those are investigation-tool constraints, not a newly discovered benchmark
environment. `collect.py` was executed with narrowly scoped read-only host
access; its `/proc/1` process is host systemd and its service calls used the host
bus. No conclusion here uses the tool sandbox's process list as a host snapshot.
