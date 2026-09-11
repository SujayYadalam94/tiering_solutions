# Recorded memory capacity: the proposed 400 MiB difference

**The recovered records do not demonstrate that the current boot has 400 MiB
more DRAM capacity than May. May's actual capacity remains unrecorded. Recent
boots provide a concrete comparison: current node0 managed memory is almost
identical to the immediately preceding 6.2 boot, and about 2 GiB lower than the
earlier September 6.18 boot that reserved 90 GiB instead of 92 GiB.**

This is a capacity comparison, not proof of equal free memory at benchmark
launch. Different live kernel allocations, PMEM metadata or other consumers
could change usable headroom while leaving managed capacity unchanged. No
matched May pre-run free-memory readback was recovered to quantify that.

## Actual zone readbacks, not filename labels

Retained kernel allocation-warning dumps contain `present` and `managed`
readbacks for each zone. Summing node0 DMA, DMA32 and Normal gives:

| Recorded boot, UTC | Kernel | Reserved map | Node0 managed KiB | Node0 managed MiB |
|---|---|---|---:|---:|
| September 8, 17:01 | 6.18.1-061801 | `90G!2G` | 6,802,368 | 6,642.938 |
| September 8, 20:20, immediately preceding current boot | 6.2.0-060200 | `92G!2G` | 4,739,828 | 4,628.738 |
| September 9, 11:15, current boot | 6.18.1-061801 | `92G!2G` | 4,737,988 | 4,626.941 |

Current versus the immediately preceding boot is **−1,840 KiB / −1.797 MiB**.
Current versus the September 8 6.18/90G boot is **−2,064,380 KiB / −2,015.996
MiB**. Neither is a current-boot gain of approximately 400 MiB.

The node0 zone values are:

| Boot | DMA managed KiB | DMA32 managed KiB | Normal managed KiB |
|---|---:|---:|---:|
| Earlier 6.18, `90G!2G` | 15,360 | 1,661,644 | 5,125,364 |
| Preceding 6.2, `92G!2G` | 15,360 | 1,661,644 | 3,062,824 |
| Current 6.18, `92G!2G` | 15,360 | 1,661,644 | 3,060,984 |

Exact dated messages and boot IDs are in the short
[raw excerpts](actual_memory_excerpts.txt); parsed values and source messages
are in [actual_memory_summary.json](actual_memory_summary.json). The current
sum agrees with the independently captured node0 `MemTotal` of 4,737,988 KiB.
The excerpts' `free` values were captured during allocation warnings, not at a
matched baseline; they must not be used as the before-benchmark comparison.

## Early boot reservations do not show a 400 MiB gain either

`/var/log/dmesg` line 352 records the current boot:

```text
Memory: 103560544K/105552188K available (... 1814088K reserved, 0K cma-reserved)
```

`/var/log/dmesg.0` line 272 records the immediately preceding boot:

```text
Memory: 103750400K/105552188K available (... 1801528K reserved, 0K cma-reserved)
```

Both boot command lines use `memmap=92G!2G`; both initialize the same node
address ranges, 40 possible CPUs, and a **64 MiB SWIOTLB** at the same physical
range. Both explicitly report zero CMA reservation. Neither command line
requests `crashkernel`, `cma`, or `page_owner`.

The current boot's early global `available` field is 185.406 MiB lower, while
the logged reserved amount is 12.266 MiB higher. Those fields are **early global
boot accounting**, not node0 managed capacity or `/proc/meminfo`
`MemAvailable`. The zone readbacks above demonstrate why the 185 MiB number
must not be interpreted as a corresponding node0 capacity difference.

All six retained `dmesg` files, including four compressed rotations, were read
with privileges and filtered for memory/boot identity. Their hashes, dates and
exact selected lines are in [kernel_memory_logs.json](kernel_memory_logs.json).
This recovers the initial boot messages even though the current live dmesg ring
has lost them to later warning traffic.

## Which earlier 6.18 boot is available for comparison?

Combining the historical header scan with the separate completed current/prior
queries recovers identity headers for **all 22 boots** in the retained journal
list, beginning September 7. Every pre-current 6.18 header uses `90G!2G` or no
`memmap` reservation; there is no earlier retained September **6.18 + 92G!2G**
boot for a direct same-kernel, same-map comparison. The first recovered 92G boot
is September 8 18:15 UTC, running 6.2.

The broad header query was stopped after approximately two minutes to keep the
read bounded. Its partial result and return code −2 are preserved in
[recent_boot_memory_headers.json](recent_boot_memory_headers.json). It had
already recovered 20 older boot identities; the independently completed
current/prior queries provide the other two. This is complete identity-header
coverage of the listed boots, not a claim of complete historical memory dumps.

## What survives from May?

A focused content search examined **2,111 readable original text artifacts**
under `logs/` and `times/c220g5`, with mtimes from May 1 through June 30. It
excluded `c220g5_final` copies and searched memory totals/free values, DRAM
capacity/startup text, kernel command lines and reservation diagnostics. Full
scope and matches are in
[original_text_memory_search.json](original_text_memory_search.json).

One May artifact has an actual node0 free-memory readback:
`logs/mg.D.x-original/4029MiB_run1_hybridtier.log`, mtime **May 11 19:21:50 UTC**,
lines 1533–1543, reports **76,832–77,696 KiB free**. These observations occur
inside a HybridTier MG execution, after its stabilization sleep. They are not
an ARMS/model pre-run baseline and do not reveal node0 total capacity.

That file also prints `fast memory size = 4294967296` at line 6. This is a
configured **4 GiB policy limit**, not a kernel memory measurement: the
HybridTier source initializes the value from `FAST_MEMORY_SIZE_GB`, and the
measurement wrapper compiles that macro from the requested size. It should not
be compared with current node0 `MemTotal` as proof that the old OS had less RAM.

No actual May node0 total, boot `Memory: ... available/reserved` line, accepted
May kernel command line, or matched pre-run free-memory snapshot was recovered.
Previously audited system-log retention reaches only August for rotated
kern/syslog and September for readable journal boots; it cannot supply May
values. A lack of May log evidence is a retention limitation, not evidence that
May reservations or memory consumers were identical to today's.

**Supported conclusion:** recent actual capacity readbacks do not explain a
400 MiB gain. A May-versus-current difference in *allocated memory/headroom*
remains possible, but the retained original logs do not quantify one. No
benchmark, boot change, sysctl write or other host configuration change was
performed for this audit.
