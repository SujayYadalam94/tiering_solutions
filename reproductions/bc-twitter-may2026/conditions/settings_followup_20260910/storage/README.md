# Storage settings follow-up, September 10

**No additional post-May storage setting change was identified.** This read-only pass extends the existing disk audit with queue readbacks and persistent device configuration. It does not establish that May used identical effective I/O settings. No graph contents were read, no benchmark or cache flush was run, and no setting, mount, TRIM, or disk configuration was changed.

## Current observations

The ordinary c220g5 runner selects `data/graphs` through `measurement_common.sh:30`. Current Twitter/Kron copies resolve to the Intel SSD (`/dev/sda3`, root ext3). The other graph directory resolves to the Seagate HDD (`/dev/sdb`, ext4). Device letters reverse across some boots; use device identity rather than letters in historical comparisons.

| Device control | Intel SSD | Seagate HDD |
|---|---:|---:|
| Scheduler | mq-deadline | mq-deadline |
| Readahead | 256 KiB | 256 KiB |
| nr_requests | 256 | 256 |
| Device queue depth | 32 | 64 |
| Request affinity | 1 | 1 |
| max_sectors_kb | 4096 | 4096 |
| Write cache | write back | write back |
| Writeback latency target | 2000 microseconds | 75000 microseconds |

These are current values, **not documented changes** or a recovered May baseline. Both backing-device interfaces have `min_ratio=0`, `max_ratio=100`, `strict_limit=0`. Dirty-page controls read ratio 20/background 10, expire 3000 centiseconds, writeback 500 centiseconds, and both byte overrides zero. None of these was found being overridden in the retained shell histories.

`vm.vfs_cache_pressure=2000` is conspicuous but already appears in the recovered May setup (`../../may04_setup/setup.sh:135`) and today's shared settings. The September 3 change in commit `671e0e644` gives **MEMTIS** value 100; the ordinary ARMS/model branch continues using 2000. This is not a post-May ARMS setting change.

Raw current observations: [snapshot.json](snapshot.json). This task runs in a filesystem sandbox: its `findmnt` and `/proc/1/mountinfo` show sandbox bind restrictions, including read-only mounts. **Those restrictions are not evidence that the user's tmux workloads run on read-only mounts or in this sandbox.** See [namespace check](sandbox_mount_namespace.json). The underlying filesystem identity/type remains visible. Host systemd bus queries were unavailable inside the sandbox, so no service-active-state claim is made from those failed queries.

## Persistent configuration and historical searches

- `/etc/fstab` retains March 2 UTC inode metadata and specifies the root filesystem as ext3 with defaults. No new surviving mount-options edit was found.
- `/etc/hdparm.conf`, its udev rule, and parser retain June 2024 inode metadata. The config contains only the active `quiet` option; read-ahead, write-cache, APM, and acoustic examples are commented out. There is no configured device-specific tuning block.
- The weekly `fstrim.timer` enablement symlink retains June 2024 inode metadata. Its service and timer files retain February 23 installation metadata. The timer is configured persistent, weekly, with a randomized delay. The latest timer-stamp mtime is **September 7 05:34:24 UTC**. This is evidence of a recent timer-trigger record, not proof of successful TRIM or how many bytes were discarded. It also predates neither the fast June nor August cohorts and does not show that automatic TRIM was newly enabled then.
- No `read_ahead`, `readahead`, I/O scheduler, `nr_requests`, `fstrim`, `blkdiscard`, `hdparm`, `blockdev`, `tune2fs`, or dirty-page-control command appears in the three retained user shell histories or Vim history. The prior saved-script audit also found no additional May/June I/O override. Missing history remains a limitation.
- A targeted all-refs Git search identifies September 3's MEMTIS-specific setup branch, older pre-May setup work, and imported artifact sources. It finds no new ordinary ARMS/model queue, TRIM, or filesystem-setting command.

See [configuration with dates and numbered active lines](persistent_storage_configuration.json), [narrow history search](queue_settings_history.json), and [existing expanded saved-script audit](../../correlation_deep_dive/setup/README.md).

## Remaining storage uncertainty is graph replacement, not a recovered tuning change

The current SSD graph files were created September 8; HDD copies retain August 21 creation/change metadata. Cached original graph metadata has March 5 mtimes and the same byte counts, but empty content hashes. The original graph directory was renamed and later deleted; the command history has those operations and real cleanup commands, without per-command timestamps or before/after free-space outputs. The active history has shifted line numbers since prior audits; [selected history captured here](graph_cleanup_history.json) preserves this pass's references.

Thus physical extent placement, controller free-space state, or original graph ordering remain unmeasured alternatives even assuming both policies read the same SSD. Current graph copies cannot recover the May file's blocks or prove byte identity. A loading-rate change could alter policy state because tiering runs during loading, but this pass supplies no dated I/O-setting transition or evidence establishing that mechanism as the cause. It would be inappropriate to propose an arbitrary readahead/scheduler change as a historical reproduction setting on this evidence.

The earlier [disk investigation](../../cross_kernel_transition/memtis_and_disk.md) remains the source for graph extent counts, the MEMTIS installation timeline, and the fact that the August 27 small gap precedes the August 31 MEMTIS clone.

## Controller IRQ/queue follow-up

The current host IRQ audit found storage-controller interrupts still assigned to workload CPUs despite the general affinity setup. To test for an actual driver-configuration transition, this pass also read protected rotated kernel logs with a read-only privileged collector. The first retained controller startup is **August 18 09:33 UTC**, so this check cannot recover May.

All **75 retained controller initializations** through September 10 show **40 enabled MSI-X vectors**, `max_msix_vectors=-1`, high-IOPS queues disabled, current controller queue depth 8596/maximum 8704, and `nr_hw_queues=40`. Both August 27 startups and the current boot have these values. Firmware stays `20.00.02.00`, chip revision `0x01`; logs that also print a BIOS version consistently report `09.39.01.00`. IRQ numbers vary between kernel boots, but vector counts do not. These startup messages do not record per-vector CPU affinity and cannot establish its historical placement.

Current module parameters include `smp_affinity_enable=1`, `host_tagset_enable=1`, `poll_queues=0`, `max_msix_vectors=-1`, `max_queue_depth=-1`, and `perf_mode=-1`. No historical readback for every parameter is available. This finds **no August-to-current controller queue/firmware change**, while preserving the narrower possibility of changed effective IRQ placement. [Selected raw historical records](mpt3sas_log_history.json), [grouped records and current parameters](mpt3sas_summary.json).
