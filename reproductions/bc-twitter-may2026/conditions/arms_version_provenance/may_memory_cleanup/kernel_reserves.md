# Which kernel reserves can invalidate the 5% free-memory estimate?

The saved 6.18 snapshots support zone protection and contiguity as meaningful gaps in the tierer's estimate. They do **not** reveal a second large CMA, hugetlb, or high-atomic reservation comparable to the tested `lowmem_reserve_ratio=4/4/4` effect.

This audit reads the 28 before/after snapshots from the 14-run historical-settings study. [kernel_reserves.json](kernel_reserves.json) contains source hashes and extracted values; [audit_kernel_reserves.py](audit_kernel_reserves.py) reproduces the extraction without changing controls or running workloads. These are September observations on the current boot, not measurements of the May boot.

## Relevant allocator rule

The 6.18 migration path uses `GFP_HIGHUSER_MOVABLE | __GFP_THISNODE` and adds `GFP_TRANSHUGE` for large folios. This targets the requested node while permitting eligible lower zones; the highest zone class is Movable. On this boot node0 Movable has no managed pages, leaving Normal and DMA32 as the meaningful sources. [Linux6.18 migrate.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/mm/migrate.c), [gfp_types.h](https://raw.githubusercontent.com/torvalds/linux/v6.18/include/linux/gfp_types.h).

The allocator subtracts unavailable high-atomic/CMA free pages, applies a zone watermark plus that zone's protection for the allocation class, and requires a suitable high-order block. PCP pages are accounted out when transferred from buddy to PCP, then back in when drained; they are not an extra pool silently included in `NR_FREE_PAGES`. High-atomic reservation targets roughly1% of a zone. PCP lists can be drained during allocation recovery. These distinctions follow the exact6.18 implementation. [Linux6.18 page_alloc.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/mm/page_alloc.c), functions `__zone_watermark_unusable_free`, `__zone_watermark_ok`, `page_del_and_expand`, `free_pcppages_bulk`, and `reserve_highatomic_pageblock`.

## Measured bounds on this system

| Candidate | Actual evidence | Interpretation |
| --- | --- | --- |
| CMA | `/boot/config-6.18.1-061801-generic` enables CMA, with `CONFIG_CMA_SIZE_MBYTES=0`. All28 saved snapshots have global `CmaTotal=CmaFree=0`, node0 zone `cma=0`, and no free CMA pages. | Zero current hidden capacity from CMA. Enabling its config option alone does not reserve memory. |
| Explicit hugetlb pool | All28 snapshots have `HugePages_Total=HugePages_Free=HugePages_Rsvd=Hugetlb=0`. | No current explicit huge-page pool. This is separate from THP. |
| High-atomic / isolated free blocks | All node0 HighAtomic and Isolate free-list counts are zero in all28 snapshots. The initial baseline also has zero HighAtomic/Isolate pageblock counts. | No measured free pages withheld by these types. Before/after snapshots cannot exclude a transient mid-run reserve. |
| Base watermarks | Initial baseline populated node0 zones sum to min11967pages=46.746MiB and high17949pages=70.113MiB; all recorded boosts are0. Current scale10, min-free1048576KiB. | The global1GiB min-free setting is distributed across nodes/zones; node0 does not lose1GiB to it. Its measured watermark magnitude is below the tierer's231.343MiB allowance. |
| Lower-zone protection | Node0 DMA32 protection for Movable/Normal is2989pages=11.676MiB normally;191311pages=747.309MiB with ratio4. | The tested change adds735.633MiB of protected lower-zone free pages. This is substantially larger than the5% heuristic and is an experimentally supported eligibility mismatch. |
| PCP caches | Summed node0 PCP counts are9.953–87.809MiB before and4.797–52.531MiB after the14 runs. Initial baseline ARMS87.809MiB and model86.801MiB. Current read-only `percpu_pagelist_high_fraction` is0; historical snapshots did not record that sysctl. | PCP state can affect drain/coalescing work. It is not an additional87.8MiB that the tierer's `MemFree` wrongly includes, nor a fixed protected reserve like lowmem. No dated May change to this control has been established. |

The frozen source computes `MemTotal / 100 * 5` using integer arithmetic. With node0 `MemTotal=4737988KiB`, this is236895KiB=231.343MiB. A single global allowance still cannot guarantee that each zone passes its threshold or that free pages are contiguous.

The min-free16384KiB and boost0 experiments are already complete; neither restored archived ARMS timing. Those outcomes reduce the case for revisiting their measured current settings. An unrecorded much larger watermark scale or different effective write could change eligibility, but there is no retained May readback supporting it, so this audit does not propose an arbitrary value as a historical reconstruction.

## Boot partitioning and fragmentation

Current kernel command line is `memmap=92G!2G`. In the saved baseline, node0 has3840 managed DMA pages,415411 DMA32 pages,765246 Normal pages and zero Movable pages. The total managed count1184497pages exactly yields4737988KiB node0 MemTotal. The present-minus-managed difference is37726pages=147.367MiB, already excluded from MemTotal. Boot-reserved/excluded address ranges therefore do not become phantom `MemFree` capacity in this readback.

The zone distribution still matters even at similar total node memory. With the baseline snapshot's ordinary free lists, DMA32 has1544MiB in order9-or-larger blocks while Normal has232MiB. Normal has890.469MiB free in total: most of that is not already available as2MiB-or-larger blocks. The pageblock labels are1161 Unmovable,366 Movable and9 Reclaimable; those labels are not a count of permanently pinned pages, but demonstrate that a single free-total value misses placement.

This explains why lower-zone protection can be especially expensive: it can hide access to the zone containing most of the ready large blocks while the remaining zone requires compaction. These before-launch numbers do not attribute the measured workload's individual allocation failures or prove May had the same distribution. A different historical boot map or persistent allocation layout remains a condition to establish from historical evidence, not a new control inferred from current data.

## Practical conclusion

The reserve-like candidates with actual evidence are the already-tested lowmem protection and the measured THP collapse/compaction behavior. CMA and explicit hugetlb are absent; high-atomic free reserves are absent in the snapshots; PCP is a state/contiguity consideration with different accounting. The data supports improving the tierer's free-capacity reasoning, but does not identify another historically supported knob that secretly removed hundreds of MiB in May.
