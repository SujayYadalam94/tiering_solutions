# Historically supported driver/device state candidates

**This focused search found no new dated PMEM/DAX setup change between May7, May14 and June13.** The large PMEM metadata mechanism is real and already tested; the surviving records do not establish a historical binding change. No new reserve-ratio value or driver manipulation is recommended as a historical reconstruction on this evidence.

Scope: same6.18 kernel; retained boot/device/module records and relevant setup call paths only. [Evidence JSON](historical_state_candidates_evidence.json) contains filtered shell/editor matches, Git query results and current configuration hashes. No workloads, device operations, or settings writes were performed.

## Tier1: measured mechanism, missing historical transition

The existing PMEM audit identifies the90GiB `namespace7.0` bound to `nd_pmem`, namespace mode `memory`, with no PFN/DAX altmap claim. The earlier controlled detach released **exactly1440MiB of page metadata on node0**, while node0 MemTotal remained unchanged. This proves that the “4GB” boot can carry a substantial driver allocation independent of the capacity label. [Original metadata audit](../../underlay_evidence/pmem_metadata_audit.json), [measured detach delta](../../underlay_evidence/pmem_detach_delta.json).

That detach experiment already made ARMS computation faster while the model changed little; see [existing experiment notes](../../README.md). It therefore supplies neither a new untested setup nor a demonstrated way to recover the **slower archived ARMS** result. To attribute the May gap to this mechanism would require evidence that May had more adverse metadata allocation/binding or placement than today. No May namespace mode, binding list, `nr_memmap_pages`, or corresponding kernel log survives.

The same amount of long-lived kernel metadata could have different physical placement or fragmentation after another boot. This remains an unmeasured state possibility; it is not a recovered change in driver load order.

## Tier2: historical configuration activity, outside the required date/value evidence

- March history contains the literal candidate `memmap=86G\$8G`; the March16 GRUB backup contains `memmap=90G!2G`; current boot uses `memmap=92G!2G`. `$` reserves a region, whereas `!` marks it as protected/PMEM-type memory, which can be handled by the PMEM subsystem. These are meaningfully different configurations on the same kernel. However, the shell literal does not prove an applied boot parameter, and neither March configuration identifies the May7/May14/June13 map. [Boot-map evidence](boot_zone_history.md), [Linux6.18 parameter meanings](https://www.kernel.org/doc/html/v6.18/admin-guide/kernel-parameters.html).
- Module loads in the May-saved shell file are the already-proved March overlap: `async_promote` and wildcard `.ko` loads inside `SoarAlto/run/nomad_module`. They are not newly recovered May PMEM loads. `unsetup.sh` would not unload that module, but no target-boot module inventory proves its presence. [Exact overlap and path context](history.md).
- All-refs March–June searches find March9 `6b2f5ddf7` disabling memeater loading and March24 `2cb2ac9cf` adding the conditional NOMAD loader. Both predate the target runs. No PMEM, devdax, kmem or hotplug operation was added to the normal ARMS/model preparation.

These are positive historical records, but none supplies a new matching-cohort configuration to reproduce.

## Tier3: focused negative findings and retention limits

The three retained user histories contain no `ndctl`, `daxctl`, PMEM/DAX binding/unbinding, namespace-format operation, `kmem` reconfiguration, or memory-onlining command. Among230 May–June shell/notes editor saves, the only module matches are the conditional NOMAD helper in two May4 saves. The current-history `update-initramfs` command belongs to a later MEMTIS installation block; it does not identify a same6.18 May/June device change.

An additional focused read of `/etc/modprobe.d`, `/etc/modules-load.d`, `/etc/udev/rules.d`, `/etc/default/grub.d`, `/etc/modules`, and `/etc/initramfs-tools/modules` found no PMEM/DAX/kmem option or module-order override. The retained modprobe/modules-load/udev files have2023–2024 mtimes. The explicit initramfs module list is dated March2 and contains storage/filesystem modules, with no PMEM/DAX entry. Current contents and mtimes cannot prove deleted historical configurations never existed, but they provide no positive May/June change.

May4→May14 changes only `measurement_model.sh` among setup/unsetup/defrag/common/ARMS/model entry files; its changes concern model configurations. May14→June13 changes none of those files. Both normal policies prepare through `default`, with cleanup, setup and defrag before launch. No new device-load call order was found. [Existing call-path comparison](../hugepage_settings_history/call_paths/README.md).

System-log retention is the hard evidence limit: surviving rotated kernel/syslog files start in August, journals in September, and the older journal files are empty. There is no May/June kernel log from which to recover automatic PMEM probing, driver order, mapping failures or hotplug locality. [Coverage evidence](system_log_coverage.json).

The practical result is a narrower candidate set, not another parameter fit: boot-map/driver allocation remains a plausible **state category**, but the retained history does not identify an additional historically backed setup alternative beyond those already reported and tested.
