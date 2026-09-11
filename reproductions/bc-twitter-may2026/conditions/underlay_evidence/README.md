# Logs recovered beneath the mounted log directory

Recovered September 9, 2026 through a nonrecursive bind of `/` inside a private mount namespace. The bind was remounted read-only; live mounts were not changed. The underlying `gapbs/benchmark` directory is empty, so no original graph was recovered there. The underlying `tiering_solutions/logs` directory contains 27 small log files totaling 3,096,784 bytes. All are preserved with original timestamps and hashes in [metadata.json](metadata.json).

The three BC Twitter files are May 11 HybridTier measurements. Their modification times are only 3–4 ms before the matching existing `.time` files. Their timestamps fall between the wtmp-ledger 6.18 boot at **May 11 14:52:00 UTC** and the next boot at **20:48:27 UTC**. This is a ledger inference; these logs have no embedded boot identifier.

| Run | Log completion UTC | Read time | Average of 10 BC iterations | Matching total |
|---|---|---:|---:|---:|
| 1 | 15:42:16.734582 | 40.49140 s | 9.71851 s | 137.860 s |
| 2 | 16:58:32.388034 | 41.03926 s | 9.55034 s | 136.726 s |
| 3 | 18:15:13.511090 | 43.35246 s | 10.04372 s | 143.975 s |

The graph has **61,578,415 nodes and 1,468,364,884 directed edges**, matching today's graph summary. Counts do not prove identical graph contents or edge order. The read times provide direct evidence of an approximately 40-second historical loading regime, consistent with today's SSD runs. They do not independently identify the historical block device.

The adjacent same-boot model files are `times/c220g5/model_6.18_new/bc-twitter.sg/4029MiB_run{1,2,3}_model_discounted_reward_99_bc-twitter.sg_l2-1_10_0.1.time`, with totals **115.306, 114.632, 113.733 seconds**. Each immediately precedes its HybridTier counterpart after preparation/build time. These May 11 model results are materially slower than the selected May 10 model results despite nearby 40-second graph reads; they must not be substituted for the selected final target. No contemporaneous ARMS BC Twitter run appears in the non-final timing inventory for this May 11 boot.

## What the memory fields mean

`[DEBUG] fast memory size = 4294967296` is a **compile-time configuration**, not a measurement. The unchanged `measurement_hybridtier.sh` computes `ceil(SIZE_MIB/1024)`, so label 4029 builds `FAST_MEMORY_SIZE_GB=4`. The artifact's `hybridtier_huge.cpp` multiplies this by 2^30 and prints it.

The later `node 0 mem` values **are actual node-0 MemFree readings in KiB**, from `/sys/devices/system/node/node0/meminfo`. They occur after sampling/migration while the application runs. The first values are 901864, 1020268, and 1301604 KiB; minima are 65540, 59456, and 58148 KiB. They do not establish prelaunch free memory or node MemTotal. Hundreds of migration calls return `-1` with printed errno 12. Some other nonzero calls return positive counts, where the same printed errno can be stale; every printed errno 12 must not be counted as a confirmed ENOMEM failure.

See [bc_twitter_summary.json](bc_twitter_summary.json) for parsed values, migration-return distributions, and links to the matching timing records. Source semantics were checked in `/users/zimooo2/hybridtier-asplos25-artifact/tiering_runtime/hybridtier_huge.cpp`, particularly `FAST_MEMORY_SIZE`, `get_node0_free_mem`, and the migration logging path.

## Read-only PMEM metadata follow-up

[pmem_metadata_audit.json](pmem_metadata_audit.json) records the current emulated-PMEM binding: node-0 namespace7.0 exposes 90 GiB through `nd_pmem`, with namespace mode `memory`, no PFN/DAX namespace claim, and no configured device altmap. Current kernel BTF reports a 64-byte `struct page`. Conventional metadata for 90 GiB at 4 KiB per page is **1440 MiB**; the additional 16 MiB namespace accounts for another 0.25 MiB. This is a source-motivated capacity hypothesis, not a measured historical difference or a measured metadata-allocation delta.

[pmem7_unused_check.json](pmem7_unused_check.json) verifies no mounts, swap, holders, partitions, process descriptors, or mappings at the check time, with zero access errors. This subtask made no namespace, module, or driver-binding changes. The supported stable control paths are `/sys/bus/nd/drivers/nd_pmem/unbind` and `/sys/bus/nd/drivers/nd_pmem/bind`, with value `namespace7.0`. The namespace's `driver` symlink disappears while unbound and must not be used as the restoration path.

The subsequent root-controlled experiment detached that driver at 13:39:45 UTC. Its saved snapshots confirm the metadata estimate: `nr_memmap_pages` falls from 368704 to 64, freeing **exactly 1440 MiB** of page metadata. Node-0 MemFree increases from 2,552,164 to 4,029,228 KiB (**1442.445 MiB**, including 2.445 MiB of other allocation changes); node-1 MemFree stays unchanged. Node-0 MemTotal also remains unchanged. This confirms a large fast-tier overhead without changing the intentional physical boot capacity. [Derived deltas](pmem_detach_delta.json), [original before/after snapshots](../sessions/20260909T133945Z-186003/pmem_detach.json).

This does not prove a historical driver-state difference or predict the benchmark direction. Additional DRAM normally helps. A possible nonmonotonic policy effect is that extra free pages allow more promotion-only migrations, while a full fast tier requires paying demotion cost as well and suppresses marginal migrations. Runtime measurements and migration counters are needed to establish such an effect. Historical `unsetup.sh` contains only broad process cleanup and `rmmod memeater.ko`; neither it nor its recorded revisions unload `nd_pmem`, and `rmmod memeater.ko` does not automatically remove unrelated PMEM drivers.
