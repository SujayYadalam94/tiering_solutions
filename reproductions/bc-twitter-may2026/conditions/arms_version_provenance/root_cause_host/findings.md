# Host-condition audit around the May 14 to June 13 transition

This read-only follow-up targets the original long BC Twitter measurements: May 14, label 4035, versus June 13–14, label 4040. It investigates possible changes outside the committed ARMS source. No host configuration or benchmark was changed.

## Result

No dated host-configuration change was recovered that establishes why ARMS improved between these two boots. The surviving records narrow several possibilities, but neither the historical memmap/PMEM state nor CPU-frequency/uncore readbacks can be reconstructed for those exact boots.

## Editor saves

The complete retained VS Code history contains 130 resource records. None is a GRUB, fstab, ndctl/daxctl, BIOS, CPU-frequency, or separate benchmark-condition-log file. Relevant files and all save metadata are catalogued in `host_related_editor_catalog.json`.

- `setup.sh` saves stop March 22. The last saved version fixes slow-socket uncore at register `0x620 = 0x707`; no May/June editor change is present.
- `defrag.sh` has no May/June edit.
- `measurement_common.sh` has May 4 saves but no subsequent May/June save.
- `defs.h` has no save between May 9 and July; `Makefile` has none between May 10 and July.
- There is no retained `unsetup.sh` resource.

These are retention observations, not proof that external edits never happened. Git checkout/pull changes need not produce editor snapshots.

The one dated storage-related note change is June 17: notes add attempts to mount `/dev/sda` and `/dev/sdc`, alongside `/dev/sdb`, for `tiering_solutions/logs` and `big-ann-benchmarks/data`. Neither the March 28 nor June 17 note mounts the GAPBS graph directory. The later note does not establish a graph-disk switch before June 13. Both versions and the exact diff are preserved here.

## PMEM and boot memory layout

The saved March, May 11, and current user shell histories contain no `ndctl`/`daxctl` namespace creation/reconfiguration or PMEM driver bind/unbind command. They do contain repeated edits to `/etc/default/grub`, followed by `update-grub` and reboot, without the resulting parameter values. There are no per-command timestamps.

March's history contains an explicit `memmap=86G\$8G` experiment and a Python script to calculate a reserved memory range. This establishes that reserved-memory layouts were explored in March. It does not identify the May 14 or June 13 layout; it also uses the reserved-RAM `$` syntax rather than the currently configured `!` PMEM syntax. It must not be assigned to either later boot.

A narrowly filtered elevated read of root Vim/shell history found that both files stop March 19, with no matching memmap/PMEM/uncore entries. No retained `/etc/default/.grub.swp` or `/etc/default/grub~` exists. Current `/etc/default/grub`, last modified September 8 at 18:07:57 UTC, contains `memmap=92G!2G`. The parent investigation separately verified the same parameter in the current `/proc/cmdline`, with node-0 MemTotal 4,737,988 KiB. These establish the September condition, not the May/June boot configuration.

Consequently, a difference in PMEM metadata charged to node 0 remains mechanically possible but historically unverified. The previous driver-detach experiment measured the current 90 GiB namespace's 1440 MiB metadata cost; it does not establish a larger namespace or different binding in May. No historical namespace size, per-node MemTotal/MemFree, or metadata-page count was recovered for labels 4035 and 4040.

## Frequency and CPU pinning

The May 11 shell-history snapshot contains a manual `wrmsr --processor 39 0x620 0x707`. The current history contains `wrmsr --processor 11 0x620 0x707`. Both target CPUs on the slow socket and request the same setting as setup; neither command is dated individually. No fast-socket uncore write, cpupower/governor change, or alternate uncore value was found in the saved histories. March contains frequency **read** commands, without their output.

This supplies no evidence for a May/June fast-socket uncore or governor transition. Absence of recorded manual commands does not prove BIOS/default/effective frequencies matched.

## Storage configuration and package changes

`/etc/fstab` retains a March 1 modification date and defines root, EFI, and swap only. It does not mount the benchmark directories. The copy here is `fstab_current.txt`. Mount commands in untimestamped history cannot prove which device was mounted successfully in either selected boot; `/dev/sdX` names alone are insufficient physical-device provenance.

Retained dpkg logs contain package events on March dates, April 18/25, and September 7/8, with **no May or June events**. `package_log_dates.json` records each file's dates and hash. This weakens an explanation based on an ordinary apt/dpkg upgrade of the compiler, libnuma, libc, or kernel tools during the transition. It does not exclude software installed directly from source, manually replaced files, or firmware changes.

## Interpretation

The specific changed artifact recovered here is the June 17 storage note; it neither predates the June 13 run nor changes the GAPBS path. No recovered evidence identifies a namespace, uncore, governor, mounting, or package change during the exact transition. It would overstate these findings to select any one of those as the cause. They remain missing per-boot conditions, while the original timing/source chronology is established separately in the parent provenance audit.
