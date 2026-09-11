# Could kernel setup account for 400 MiB more usable DRAM?

September 9, 2026. This investigation is read-only: no workload, reboot, driver operation, or setting change was performed.

The user subsequently clarified that the comparison concerns **free node0 memory after preparation, immediately before application launch**, with GRUB assumed unchanged. The boot-capacity comparison below does not resolve that question. The follow-up [NUMA balancing audit](numa_balancing_history.md) confirms that the normal ARMS/model preparation requests `0` in May/June and today, and all 48 recent before/after readbacks checked also contain `0`. Explicit `migratepages` in setup still runs with automatic balancing disabled. A separate [idle snapshot](idle_free_memory.json) is deliberately labeled as unprepared: file-cache growth during investigation makes it unsuitable for comparing benchmark starting capacity.

**A same-image boot reservation or persistent kernel allocation could change available near memory by that amount. The retained evidence does not identify such a historical change. Recent actual memory readbacks show that this boot has less managed node0 RAM than the recoverable preceding boots, not 400 MiB more. May's pre-run memory consumption remains unknown.**

## Measured boot capacity

These are actual sums of node0 managed-zone pages, verified against retained kernel allocation-warning dumps; they are not the manually assigned MiB filename labels.

| Boot | Memory-map argument | Node0 managed capacity |
| --- | --- | ---: |
| Current 6.18 | `memmap=92G!2G` | 4,626.941 MiB |
| Immediately preceding 6.2 | `memmap=92G!2G` | 4,628.738 MiB |
| Earlier retained September 6.18 | `memmap=90G!2G` | 6,642.938 MiB |

Current versus previous same-map capacity is **1.797 MiB lower**. Current versus the older same-kernel 90 GiB reservation is **2,015.996 MiB lower**. The latter is a real recorded boot-setup change, but it has the wrong direction and scale to explain an extra400 MiB today.

The kernel's early global `Memory: ... available` line also differs between boots, but that includes early initialization and allocations later freed. It is not a node0 capacity measurement. The managed-zone comparison above is the relevant distinction. [Raw excerpts, chronology and coverage](actual_memory_evidence.md).

No earlier retained September6.18 boot with92GiB reservation supplies a direct same-kernel/same-map comparator. May's actual node0 MemTotal and prepared MemFree were not recovered from the original logs. Therefore these recent comparisons cannot prove that May had identical effective memory.

## Kernel reservations and switches checked

| Mechanism | Current evidence | Historical implication |
| --- | --- | --- |
| Crash-kernel reservation | Kernel supports it, but crash reservation is0 and no crash kernel is loaded. | A384MiB reserve could have the requested magnitude on this image, but no matching historical option was found. It is not a recovered May setting. |
| SWIOTLB DMA bounce pool |64MiB total; current used and high-water counts0; no transient slabs. | A deliberately larger pool could consume hundreds more MiB. No historical override or current growth supports that explanation. |
| CMA / explicit hugetlb | Both pools0. | No current hidden400MiB pool; no dated May alternate recovered. |
| Page-owner/page-extension/page-table-check/debug-pagealloc/KASAN | Compiled out of this exact6.18 image. | They cannot be toggled on at boot to create the proposed difference on this image. |
| KFENCE / per-CPU allocation | KFENCE pool2MiB; current global per-CPU memory80MiB over40 possible CPUs. | No400MiB current debug pool; retained compared boot headers also have40 CPUs. |
| Journald / retained initramfs / tmpfs / printk | Boot journal flushed to disk;20MiB initrd freed; current `/run`1.88MiB; log buffer sub-MiB. | No matching configuration change or retained400MiB RAM-backed consumer identified. |

[Exact kernel-source and configuration checks](boot400_kernel_candidates.md), [boot runtime consumers](boot_runtime_consumers.md), [current filtered readbacks](current_boot_memory.json).

The March GRUB backup contains90GiB map/debug-console settings, not a crash/SWIOTLB/debug-metadata override. Focused searches of histories and setup snapshots likewise found no such option. The old `vm.min_free_kbytes=1048576` setting produces only about46.75MiB of minimum watermarks across populated node0 zones on this host; it is not a1GiB node0 reservation. The earlier [reserve study](../arms_version_provenance/may_memory_cleanup/kernel_reserves.md) already checked that distinction.

## Free/allocatable memory can differ with the same managed total

The user's400MiB hypothesis can also mean400MiB less memory occupied or withheld from the benchmark after preparation. That would not require a larger MemTotal. Persistent kernel allocations, driver memory, and per-zone/contiguous allocation eligibility remain relevant categories.

Known real costs are already substantial: the bound emulated-PMEM namespace's metadata consumed1440MiB of node0 memory in the earlier detach/rebind diagnostic, and frozen ARMS perf rings require320.16MiB backing versus160.16MiB for the model, with a preference for node0. These explain why a nominal4GiB label is not the workload's allocation budget. They do not demonstrate an additional400MiB difference between May and today.

A fresh read-only kernel allocation inventory is saved in [current_kernel_allocations.json](current_kernel_allocations.json). The largest vmalloc-backed category is system hash tables, with58.73MiB of reported backing on node0 and the same on node1. The largest global slab object-capacity category is the IOMMU IOVA magazine cache, about69.4MiB at the snapshot. These categories overlap ordinary meminfo accounting and should not be added indiscriminately. Current NICs use MTU1500; focused histories/configuration searches found no `ethtool`, MTU, IOMMU-superpage, CPU-count, or hash-distribution override establishing an older400MiB kernel-allocation change.

There is still no recovered May prelaunch snapshot that lets us subtract old versus current kernel memory consumption. The earlier timing/reserve experiments demonstrate performance sensitivity; they are not a direct measurement that exactly400MiB of RAM disappeared historically.

The supported conclusion is therefore: **no confirmed kernel-setup change currently explains a400MiB gain. A difference in prepared free/allocatable memory remains possible, while the recent measured boot-capacity changes do not match the hypothesis.**
