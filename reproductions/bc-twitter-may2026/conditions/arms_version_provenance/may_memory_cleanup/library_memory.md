# Frozen May ARMS/model memory footprint and allocation placement

September 9, 2026. Analysis of the existing uninstrumented `may07_arms` and
`may07_model` libraries and their frozen `source/may04` inputs. No workload or
library was executed, and no host setting or original library was changed. Two
small **compile-only** objects expose structure sizes; commands/hashes/results
are in `library_memory_layout.json` and their source is `layout_probe.cpp`.

**ARMS has 160 MiB more perf-ring backing than the model, allocated by the kernel
with a preference for node0.** This is a substantial fixed policy difference
that can interact with zone reserves. It is present in the reconstructed May
cases and therefore is not, by itself, a newly discovered May-to-current change.

## Exact ring sizes, not the stale comments

`defs.h:182` sets C220G5 `PEBS_NPROCS=30`; `arms_kernel.cpp:1238–1255` skips CPUs
10–19 and opens read/write events on CPUs 0–9 and 20–29. That is **20 CPUs × 2
events = 40 independent ring mappings** for each policy.

| Frozen case | Data pages per ring | Per-ring data | Metadata per ring | Total mapped/backing bytes | Total MiB |
|---|---:|---:|---:|---:|---:|
| ARMS | 2048 | 8 MiB | 4 KiB | 335,708,160 | **320.15625** |
| Model | 1024 | 4 MiB | 4 KiB | 167,936,000 | **160.15625** |
| ARMS minus model | 1024 | 4 MiB | 0 | 167,772,160 | **160** |

This uses the actual `sysconf(_SC_PAGESIZE) * PERF_PAGES` expression at
`arms_kernel.cpp:1208`, and the 4 KiB page configuration. The comments saying
64 MB/128 MB in `defs.h:292–294` do not describe these per-ring or 40-ring totals.
Both formulas include one metadata page per ring. They are not forty 64/128 MB
allocations.

The kernel additionally requests a data-page pointer array of **0.625 MiB** in
aggregate for ARMS versus **0.3125 MiB** for the model, plus ring/event structures,
allocator rounding, page tables, and architecture PMU state. Thus the table is
the exact ring-page backing, not an exact total process/OS footprint.

## Why these are real physical allocations on the fast node

The application calls `mmap(..., MAP_SHARED, perf_fd, 0)`; it does not call
`mlock`, `mlockall`, or use `MAP_LOCKED`/`MAP_POPULATE`. That does **not** make perf
backing a lazy anonymous reservation. In upstream Linux v6.18's non-vmalloc
path, `rb_alloc` allocates every metadata/data page immediately via
`alloc_pages_node(cpu_to_node(event_cpu), GFP_KERNEL | __GFP_ZERO, 0)` and keeps
them until the buffer is released. Its pointer structure is also allocated on
that requested node. [Linux v6.18 ring-buffer source](https://github.com/torvalds/linux/blob/v6.18/kernel/events/ring_buffer.c#L749).

`perf_mmap_rb` supplies the event CPU to that allocator and accounts the mapping
against perf/pinned-memory limits. The CPU selected by these forty events is on
node0. This explicit allocation preference applies even though the calling
thread has bound its ordinary userspace allocations to node1; allocator fallback
is still possible. The installed 6.18 configuration defines 4 KiB pages and does
not define `CONFIG_PERF_USE_VMALLOC`. The field `pe.pinned=1` concerns keeping the
event scheduled on the PMU; it is not the reason the buffers have physical backing.
[Linux v6.18 perf mapping source](https://github.com/torvalds/linux/blob/v6.18/kernel/events/core.c#L6550).

These pages consume free memory after benchmark launch. Their allocation flags
do not include `__GFP_MOVABLE`, so their zone eligibility differs from movable
application pages. They are a credible input to the parent's zone-reserve
analysis. We have no original May ring-by-ring NUMA residency or allocation
fallback record; “prefers node0” must not be changed into “every page was proven
on node0.” Likewise, a pre-execution MemFree snapshot taken by the runner is
before this allocation, not the application's remaining fast-tier budget.

## Allocation sequence and internal placement

The historical launcher uses `taskset` CPUs 0–9,20–29 and allows NUMA nodes 0,1.
The `__libc_start_main` hook (`hook/hook.cpp:273–286`) then binds the application
thread's default allocations to **SLOW_TIER=1** and invokes `arms_start_tiering`
before program `main`. `arms_start_tiering:1900–1921` applies the same binding
again. Both normal policies have `LOGGING_RUN=false`.

Next the model creates its group trackers/access-log object, setup reads node0
memory, allocates the perf rings and counter objects, and creates **13 helper
threads**: PEBS scanner, pagemap scanner, ten migration workers, and policy thread
(`arms_kernel.cpp:1974–1996`). The PEBS scanner pins itself to CPU1; policy pins
to CPU2; the other helpers inherit the launch affinity. CPU affinity does not
undo the inherited node1 allocation policy. Ordinary new heap allocations and
first-touched helper stack pages therefore normally follow the slow-node
binding, subject to allocator reuse, existing mappings, and failed policy calls.
The early loader/constructor footprint can predate rebinding.

Thread stacks have virtual reservations whose fully touched size is not known
from source. They should not be counted as thirteen fully resident default-size
stacks. Both policies create the same helper count. Both also open **24 counting
IMC events** without large ring mappings. Optional offcore diagnostic counters
are skipped because `ARMS_VERBOSE=false`.

## The model's additional structures are much smaller than its ring saving

| Structure | ARMS bytes | Model bytes | Interpretation |
|---|---:|---:|---|
| `page_info` | 800 | 824 | One per tracked 2 MiB-aligned address; difference is history length 4 versus 10 |
| `page_ptr` / `score_entry` | 16 / 24 | 16 / 24 | Vector elements, excluding backing allocation overhead |
| `data_row` | 320 | 320 | Model inference scratch rows; no persistent training array in these cases |
| `page_group` | 64 | 64 | Model maintains two trackers; groups cover eight huge-page addresses |
| `group_tracker` / `access_log` | 96 / 208 | 96 / 208 | Normal ARMS does not create these model/logging objects |

For scale, at 10,000 tracked addresses the page objects alone are **7.63 MiB**
for ARMS and **7.86 MiB** for the model—a **0.229 MiB** difference before shared
pointer/hash/allocator overhead. One 10,000-row model scratch vector adds
approximately 3.05 MiB. Actual peak vector capacities and tracked-page counts
depend on the run; no exact whole-library resident total can be recovered from
compile flags alone.

The enormous potential training buffer is **not allocated**: `logging.cpp:28`
guards it with `PRINT_TRAINING_DATA`, which is false for both frozen cases. The
normal model creates a 208-byte `access_log` with `scores_log=nullptr`; its
reported logged allocation is zero. Enabling training would request 10 million
× 320 bytes, but that is a different build and should not be charged to these
runs.

`size -A` reports model `.text`+`.rodata` of 371,632 bytes versus ARMS 196,448;
both have 12,280-byte `.bss`. The model is linked to the same saved May03 compiled
forest object rather than loading a large training model into heap memory.
These are section sizes, not guaranteed resident-page totals.

## Capacity accounting and hidden compile-time settings

There is a misleading startup comment: `arms_kernel.cpp:1957` says MemTotal,
but the next line actually calls `get_fasttier_free_mem()` and caches that value
in `dramsize`/`FAST_MEMORY_SIZE` **before** ring allocation. However, the frozen
source does not use those cached globals to set the migration capacity. The
active selector calls `fasttier_free_kb()` at lines 1653–1662, which rereads
node0 MemFree and subtracts 5% of node0 MemTotal each time. Ring pages are thus
already reflected in that later free-memory reading. The remaining limitation
is that raw node-wide free memory minus 5% does not itself describe per-zone
allocation eligibility/protection; the parent is auditing that separately.

The exact reproduction build (`build.py`) uses C220G5, `-O3 -DNDEBUG`,
`USE_MODEL=false` for ARMS, and `USE_MODEL=true`, discount99, history summary2,
history length10, switch0.5 for the model. There is no FAST_MEMORY_SIZE override,
NUMCORE/SAMPLETYPE override, training flag, or enlarged migration-worker count.
The source's fallback `FAST_MEMORY_SIZE_GB` is unused when the memory read works
and does not allocate/reserve RAM itself. `BACKOFF_PERIOD=0` for both policies.
Sampling fields are fixed to IP, TID and address; there is no userspace-stack
sample payload hidden in the frozen configuration.
Both start with sample period 10007; ARMS can switch to 5003 in its high-fidelity
mode, while the model stays at 10007. That changes sample volume, not allocated
ring capacity.

A focused scan of **40 May Makefile saves, 50 May arms_kernel.cpp saves, and
9 May defs.h saves** is preserved in `library_compile_history.json`. The matching
ring, CPU-count, backoff, sample-type, and capacity definitions have no alternate
state in those saves. Makefiles change platform lists, but the contemporaneous
**May 7 04:10:44 UTC** `4HiW` save selects C220G5 and normal ARMS/model variants.
The separate NOMAD-no-migrations define uses a different name; it is not the
normal ARMS build used here.

This establishes a real, quantifiable **160 MiB ARMS disadvantage in kernel ring
memory**, with a relevant zone-allocation mechanism. It does not identify a
different May ring size or prove original executable identity beyond the
already-established source/build lineage. A changed host/zone condition could
amplify this constant difference without an explicit reserve-ratio edit.
