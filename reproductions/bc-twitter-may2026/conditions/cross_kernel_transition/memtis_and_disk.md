# Could MEMTIS installation or disk cleanup explain the speedup?

**Storage changes remain physically plausible, but the observed small HybridTier gap predates this MEMTIS installation.** This investigation does not establish disk fullness as the cause of May's slower ARMS. All dates below are UTC.

## Dates from independent records

| Event | Evidence |
|---|---|
| August 27: fast HybridTier ARMS | Original 4053 BC Twitter files average **129.837 s**, versus **126.418 s** for the general model, in the same HybridTier boot. The model differs from today's workload-specific forest; ARMS's absolute speed is nevertheless comparable. |
| August 31 **13:44:47**: MEMTIS clone | First `.git/logs/HEAD` record says `clone` at epoch `1788183887`; inode creation metadata agrees. |
| August 31 **15:38:07**: first MEMTIS image installed | `/boot/vmlinuz-5.15.19-htmm` modification/change/creation metadata. |
| August 31 **15:52:39**: first retained MEMTIS boot | `5.15.19-htmm` in the boot ledger. |
| September 9: small gap reproduced on HybridTier | **134.981 s ARMS / 130.877 s model**, both successful. |

[Raw installation/disk evidence](memtis_installation_and_disk.json), [August/current timing snapshot](latest_hybridtier_and_august_20260909T213825Z.json), [boot ledger](boots.json).

Thus this installation cannot be the initiating cause of a condition already visible four days before the repository was cloned. The independently faster June long-run cohort is earlier still. This does not rule out an additional later change affecting today's exact totals.

## Cleanup was real, but its exact date and recovered space are not recorded

The current bash history includes these commands after the MEMTIS installation section:

- Line 1499: `rm -rf /tmp`.
- Line 1509: `rm -rf logs.zip`.
- Lines 1521–1539: removal of `LULESH/`, `wiredtiger/`, `rocksdb/`, `mlc`, and `YCSB-cpp/`.
- Line 1568: `rm -rf benchmark_old/`, among GAPBS directory navigation.

The history contains intervening `df`/`du` commands, but **not their output or individual timestamps**. Multiple shell sessions can append history at different times; line order is not sufficient to assign an exact cleanup date. Relative-path working directories are also not independently recorded for every deletion. No surviving before/after free-space measurement was recovered here.

At inspection, the SSD filesystem reports **85% used, 66 GiB available**; the HDD shared by `gapbs/benchmark` and logs reports **57% used, 449 GiB available**. These are current observations, not May or pre-cleanup capacity measurements. Device letters change across boots: today's SSD is `/dev/sdb3`, HDD `/dev/sda`.

## How disk changes could matter to these policies

1. **SSD free space and garbage collection.** More controller-known free space can reduce garbage-collection/write-amplification work. Filesystem free space alone does not establish how much space the controller knows is unused; TRIM and prior writes matter. This is primarily evidence for a write/garbage-collection mechanism, not a guaranteed speedup for reading the same existing graph. [Primary SSD TRIM research](https://arxiv.org/abs/1208.1794), [Kingston technical explanation](https://www.kingston.com/unitedkingdom/en/blog/pc-performance/overprovisioning).
2. **A copied or regenerated graph can have different disk placement.** Deleting unrelated files does not itself relocate the existing graph's filesystem blocks. A subsequent rewrite/copy can choose a different layout. Filesystem allocation policy and available contiguous space affect fragmentation. [Kernel allocation documentation](https://www.kernel.org/doc/html/latest/filesystems/ext4/allocators.html). Current SSD graph files were created September 8; current HDD graph files August 21. Their metadata cannot certify May's graph placement or contents. Current extent counts (Twitter/Kron) are SSD **221/316**, HDD **62/170**; these counts are not directly comparable performance scores across the two media/filesystems and have no May counterpart.
3. **Loading can affect subsequent tiering, not just add seconds.** `hook/hook.cpp:327` starts the tierer before application `main`. `gapbs/src/reader.h:272` reads the serialized graph into allocated arrays and builds indexes, then closes the file. ARMS's history/scoring advances with timed policy loops; model virtual history advances with counted samples. A different loading rate can therefore change migrations, page placement, and score history at the start of the BC trials. **This is a source-supported mechanism to test, not a demonstrated cause.** A common graph/disk for both policies does not make them equally sensitive to this mechanism.
4. **Logging or competing I/O could stall a run.** This requires actual concurrent writes, errors, or logging output, rather than free-space percentage alone. Ordinary nontraining ARMS is not producing the large training datasets. The historical May ARMS read/trial split is missing, so its total cannot isolate this possibility.

The retained September 1 MEMTIS investigation supplies a real storage failure example: BC Kron read times ranged from **1204.50 to 113.90 s**, while average trials stayed approximately **14.7–15.0 s**, with SCSI read timeouts overlapping the longest load. That demonstrates a storage problem in those MEMTIS runs; it is not evidence that May ARMS had the same problem. [Original report and raw-log link](/users/zimooo2/memtis-incident-2026-09-07/GRAPH_LOAD_TIMES.md).

## Installation side effects checked

The retained MEMTIS installer writes version-specific kernel/module/initramfs paths and regenerates GRUB. The HybridTier image/initramfs retain March 7 modification/change times; 6.18's initramfs retains March 5 times. The saved installation log lists those existing images during GRUB generation; it does not show them being rebuilt. No new MEMTIS-dated file appears in the inspected persistent sysctl, modprobe, or udev configuration directories. Retained package logs around installation show kernel packages, not an accompanying libc/libnuma/compiler upgrade. These observations do not certify every historical boot argument, but provide no evidence of a cross-kernel runtime replacement.

A read-only query of `fstrim.service` for August 20–September 9 returned **no retained entries**. That is not proof that TRIM never ran. No cleanup, defragmentation, TRIM, graph copy, or benchmark was executed during this investigation.

The supported conclusion is that cleanup/storage could contribute to performance, including through load-time tiering, but **MEMTIS installation is too late to explain the first observed small gap**. Recovering the May cause still requires evidence tying a specific storage or launch condition to those runs; the surviving file-space history does not yet do that.
