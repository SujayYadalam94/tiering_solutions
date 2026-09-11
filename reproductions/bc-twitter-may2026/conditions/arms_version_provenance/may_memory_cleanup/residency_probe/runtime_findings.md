# BC Twitter placement diagnostic results

One isolated ARMS/model pair completed on the existing 6.18.1-061801-generic boot, using frozen May source, the May03 model with configuration `[2,10,0.5]`, the same SSD Twitter graph, and full historical preparation before each run. The baseline accepted `lowmem_reserve_ratio=256 256 32 0 0`; no sensitivity override was used. Session: `20260909T191220Z-491552`.

**This pair did not demonstrate a large residency-bookkeeping error in its sampled regions. It did demonstrate a substantial ARMS/model difference in migration splitting and compaction on the same kernel and settings.** The accounting defects in the source remain real, but these samples are insufficient to blame them for the historical timing transition.

| Observation | ARMS | Model |
| --- | ---: | ---: |
| Total elapsed | 110.30 s | 98.35 s |
| Read Time | 39.61267 s | 39.45195 s |
| Average trial | 7.02753 s | 5.86505 s |
| Completed trials / exit status | 10 / 0 | 10 / 0 |
| Global THP migration success delta | 3,588 | 7,003 |
| Global THP migration failure delta | 3,998 | 0 |
| Global THP migration split delta | 3,998 | 0 |
| Global direct compaction stall delta | 75,512 | 0 |
| Global direct compaction failure delta | 75,473 | 0 |
| Global `allocstall_movable` delta | 37,770 | 0 |
| Global direct reclaim scan delta | 0 | 0 |

VM counters are system-wide; the runner detected no competing BC/PR/benchmark-runner process. These counts are not unique affected regions or application stall seconds. A THP failure/split can be followed by successful base-page migration, so it is not necessarily a user-visible failed `move_pages` syscall. Direct-compaction counters do not include all background compaction; the model still recorded background scan activity. Source behavior is documented in the earlier [kernel migration mechanism audit](../../migration_probe/kernelmechanism.md).

## What the actual residency queries showed

Each policy had two samples, at about 50 and 75 seconds after the pagemap thread started. Each sample queried 32 regions, all 512 addresses per region, with head queries before and after. All 128 region queries across both policies succeeded. Every sampled record had stable bracketing flags and heads, though the combined observation is not atomic with concurrent migration.

The selection was deterministic from a 64-candidate hash sample, balanced by tracked near/far state where possible. It revisited many addresses: **34 unique keys for ARMS and 35 for the model**, not 128 distinct regions. This is a bounded diagnostic, not a population estimate.

- No fully populated sampled region was split between node0/node1 while marked `fragmented=false`.
- No sampled `in_dram=false` record hid any observed node0 pages.
- No sampled record was entirely absent.
- ARMS had one partial region at 75 seconds marked `in_dram=true, fragmented=true`: 290 near pages, 2 far pages, 136 `ENOENT` and 84 `EFAULT` results. Counting it as a full 2 MiB near record overstated near occupancy by **0.8671875 MiB**. The fragmented marker itself was appropriate; this is not an example of the scanner incorrectly clearing that flag.
- Model samples showed no near-credit discrepancy.
- The full map traversal counted **two unaligned keys in ARMS and zero in the model** at both checkpoints. The diagnostic did not query those specific keys or trace their insertion. This is consistent with the separately established scanner bounds defect, but does not prove their origin or quantify additional corrupted canonical records.

The isolated tiny kernel test in the parent directory still establishes the one-page/whole-region mechanism. Its frequency and timing impact in original May BC cannot be inferred from that test, and the current small sample did not expose its strongest case.

## Overhead and validation

Actual residency queries took 3.44/3.32 ms for ARMS and 3.60/8.15 ms for the model. Including metadata copying and structured output, measured time before each summary write was 66.01/71.62 ms and 52.01/55.78 ms respectively. All rounds stayed below the 250 ms soft measurement budget. Summary-write and stack cleanup time are not included in those totals.

Only the isolated scanner object was recompiled. Production source and normal libraries were unchanged. The diagnostic performs read-only residency queries outside the migration-cost timer; it can still perturb scheduling, allocation layout, and later adaptive policy decisions. Its timings should not be treated as precise replacements for the earlier uninstrumented controls.

The [analysis](runtime_summary.json) retains exact records, timings, VM deltas, and execution paths; [analyze_run.py](analyze_run.py) reproduces it. Original stdout, inputs, preparation readbacks, before/after VM snapshots, and resource timings remain under the [session results](../../../../sessions/20260909T191220Z-491552/results/20260909T191220Z-491552).

The runner reported no restoration errors. An independent live check found no differences in captured controls, IRQ affinities, socket uncore MSRs, or surviving original processes' main-thread affinities. No benchmark remained active; the boot was unchanged and PMEM remained bound. [Restoration verification](final_restoration_check.json).

## Updated assessment

The best directly observed mechanism is **ARMS entering expensive THP allocation/splitting and compaction paths that the model avoids**, despite the same graph, boot, and requested setup. This is compatible with a zone/contiguity limitation below the node-wide free-memory heuristic. The extra 160 MiB of ARMS perf backing is a fixed physical allocation difference that can amplify this sensitivity.

These observations do not identify which historical condition made May worse. The earlier lowmem4 experiment demonstrates that changing eligible lower-zone capacity can amplify the gap, but retained history does not establish that ratio or another equivalent reserve as May's accepted state. A bookkeeping fix, a new capacity override, or a conveniently matching runtime would not by itself recover that missing historical condition.
