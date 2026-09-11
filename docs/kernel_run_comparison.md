# Archived c220g5_final: 6.18 versus 6.2

This comparison uses archived results, not the current SSD copies. Times include loading and computation. The files do not record separate Read Time, exact kernel releases, boot IDs, runtime VM settings, or binary hashes.

## Model at approximately 6 GiB

Means of three successful runs per cell, configuration `1_10_0.1`. Capacity labels differ slightly across runs/kernels; the labels below are requested sizes, not measured resident memory.

| Workload | Labels: 6.18 / 6.2 MiB | 6.18 seconds | 6.2 seconds | Added seconds | Ratio |
|---|---|---:|---:|---:|---:|
| DuckDB-TPCH-sf100 | 6032 / 6030 | 196.10 | 454.74 | 258.63 | 2.32 |
| XSBench | 6032 / 6029 | 169.02 | 169.76 | 0.73 | 1.00 |
| bc-kron.sg | 6032 / 6029 | 208.12 | 257.62 | 49.51 | 1.24 |
| bc-twitter.sg | 6032 / 6029 | 101.71 | 134.36 | 32.65 | 1.32 |
| faiss_10M | 6032 / 6030 | 291.34 | 502.73 | 211.39 | 1.73 |
| mg.D.x | 6032 / 6030 | 224.65 | 489.16 | 264.51 | 2.18 |
| pr-kron.sg | 6032 / 6029 | 302.22 | 352.07 | 49.85 | 1.16 |
| pr-twitter.sg | 6032 / 6029 | 307.77 | 327.91 | 20.13 | 1.07 |

## Graph baselines without the model

One successful `0MiB_run1.time` per cell. Baselines were archived later than the model runs; these show a pattern rather than a paired causal decomposition.

| Workload | DRAM 6.18 | DRAM 6.2 | Delta | Far 6.18 | Far 6.2 | Delta |
|---|---:|---:|---:|---:|---:|---:|
| bc-twitter.sg | 91.68 | 127.26 | 35.58 | 150.08 | 191.45 | 41.37 |
| pr-twitter.sg | 270.28 | 305.60 | 35.31 | 570.77 | 610.96 | 40.19 |
| bc-kron.sg | 182.92 | 234.10 | 51.18 | 332.68 | 391.96 | 59.28 |
| pr-kron.sg | 282.96 | 333.60 | 50.64 | 601.28 | 660.81 | 59.53 |

## Kernel evidence and interpretation

The installed `/boot/vmlinuz-6.2.0-hybridtier+` is byte-identical to `/users/zimooo2/linux-hybridtier/arch/x86/boot/bzImage`. The local source contains an unconditional early return from `shrink_lruvec()` whenever the caller is kswapd, with a printk. This disables that background LRU-reclaim path independently of whether HybridTier userspace runs. [Patch](https://github.com/kevins981/linux/commit/81ca81500680260c88827f0b8548ea4dcd42ee97).

Other local patches hardcode node 0 as the top tier and node 1 as its demotion target, enable demotion in reclaim_folio_list(), and force AutoNUMA scanning to 1000 ms with a printk. AutoNUMA scanning requires runtime NUMA balancing to be enabled.

Major options match between the installed 6.2-hybridtier and 6.18-generic configs: HZ=1000, voluntary/dynamic preemption, THP support/madvise default, NUMA balancing support, MGLRU support/default, and disabled DEBUG_VM, DEBUG_PAGEALLOC, and PAGE_OWNER.

The graph baseline penalties are approximately constant for the same input graph across BC and PageRank. This supports a shared loading/allocation cost, not a slowdown specific to model inference. GAPBS Read Time includes file reads, allocation/page faults, and adjacency-index construction. Correct disk placement does not rule out kernel buffered-I/O or allocation differences.

The reclaim patch could increase direct-reclaim stalls, compaction pressure, or logging overhead under pressure, but the archives do not measure its contribution. Larger model slowdowns in DuckDB, FAISS, and MG require separate migration/reclaim/page-size analysis; baseline aliases ending in -original are not assumed equivalent to the model workloads.

A causal check would compare the same workload on 6.2-hybridtier with and without the kswapd early return, followed by stock 6.2 versus 6.18, holding capacity, input, binaries, uncore and cache preparation fixed. Capture Read Time and algorithm time separately, plus direct-reclaim/compaction counters, THP coverage, migration counts and I/O statistics. No kernel or runner settings were changed during this analysis.
