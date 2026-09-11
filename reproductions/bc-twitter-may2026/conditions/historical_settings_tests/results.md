# Historical setting experiments: completed results

September 9, 2026. All 14 workload executions completed successfully: an opening
baseline pair, five separate alternative pairs, and a closing baseline pair.
Each execution contains ten BC Twitter iterations. The same intentional 4 GB
Linux 6.18 boot, SSD graph, BC executable, and frozen uninstrumented May policy
libraries were used throughout. The model is the retained May 3 forest with
configuration `[2, 10, 0.5]`.

**The `4 4 4` reserve ratio produced a large slowdown and a wider model advantage.
The other alternatives did not produce a clear change beyond the baseline
variation observed in this study.** All host controls were restored and checked
against every session's saved state.

## Timings

| Setting, applied separately | ARMS total s | Model total s | ARMS read s | Model read s | ARMS average iteration s | Model average iteration s | Computation gap over ten iterations s |
|---|---:|---:|---:|---:|---:|---:|---:|
| Opening baseline | 106.28 | 97.90 | 39.66868 | 39.43197 | 6.62072 | 5.82141 | 7.9931 |
| Reserve ratio `32 32 16` | 109.05 | 99.39 | 41.10934 | 40.99477 | 6.75583 | 5.81345 | 9.4238 |
| Reserve ratio `4 4 4` | 136.85 | 112.39 | 39.72778 | 39.51537 | 9.67427 | 7.25714 | 24.1713 |
| Watermark boost `0` | 108.78 | 98.06 | 39.52489 | 39.63938 | 6.88353 | 5.81790 | 10.6563 |
| Minimum-free request `16384` KiB | 108.01 | 97.78 | 39.66616 | 39.58555 | 6.79182 | 5.79490 | 9.9692 |
| Khugepaged scan batch `4096` | 107.38 | 98.15 | 39.67523 | 39.56621 | 6.73083 | 5.83309 | 8.9774 |
| Closing baseline | 111.42 | 97.67 | 39.66274 | 39.40661 | 7.12985 | 5.80008 | 13.2977 |

The original baseline values are ratio `256 256 32`, boost `10000`, minimum-free
request `1048576` KiB, and scan batch `8192`. The kernel's five-entry ratio
readback includes two trailing zeros; both were preserved and verified.

The ARMS baseline average ranged from 6.62072 to 7.12985 seconds. Every smaller
alternative falls inside that range. Model baseline computation ranged from
5.80008 to 5.82141 seconds; small variants remain within about 0.6% of its mean.
The higher total for ratio 32 also includes about 1.4–1.6 seconds of extra loading.
There is one pair per alternative, so these are sensitivity tests, not estimates
of a long-run distribution or confidence intervals.

Ratio 4 increases ARMS computation by about **36–46%** relative to the two observed
baselines, and model computation by about **25%**. Its 24.17-second computation
gap exceeds both baseline gaps, 7.99 and 13.30 seconds. Its loading times are
nearly unchanged. Returning to the original settings reduces both runtimes again,
although ARMS does not return exactly to its opening timing.

## Accepted settings and allocation work

The recorded DMA32 protection rose from **11.68 MiB** at baseline to **93.41 MiB**
for ratio 32 and **747.31 MiB** for ratio 4. These are allocation protection
thresholds, not physically consumed memory. Ratio 4 retained the same zone
minimum/high watermarks and began with zero zone boosts. Starting node0 free
memory was higher than the opening baseline for both policies, rather than lower.

The 16 MiB minimum-free request remained exactly `16384` before and after both
workloads. Minimum watermarks in the populated node0 zones fell from approximately
46.75 MiB to 0.72 MiB. Watermark boost 0 also started with zero existing boosts;
the scan-batch readback was exactly 4096. All other captured controls matched.

| Observation | ARMS opening baseline | ARMS ratio 4 | ARMS closing baseline | Model opening baseline | Model ratio 4 |
|---|---:|---:|---:|---:|---:|
| Workload system CPU seconds | 138.69 | 366.89 | 201.44 | 60.21 | 211.22 |
| Allocation stalls, Movable class | 34,473 | 257,644 | 35,586 | 0 | 32,513 |
| Direct-reclaim page scan events | 443 | 19,184,237 | 73,995 | 0 | 7,497 |
| Direct-reclaim page steal events | 70 | 286 | 832 | 0 | 1,762 |
| Successful THP migrations | 4,531 | 1,412 | 3,859 | 6,993 | 3,927 |
| Failed THP migrations | 3,317 | 16,273 | 4,644 | 0 | 3,883 |
| THP migration splits | 3,317 | 16,269 | 4,644 | 0 | 3,880 |
| Direct compaction failures | 67,677 | 12,739 | 63,754 | 0 | 62,610 |

VM events are global deltas, not PID-specific attribution. Scan counts are events,
not unique pages. Workload system CPU includes its children and is summed across
threads; it is not elapsed time or the separate khugepaged daemon's CPU time.
The large rise in allocation stalls, repeated scanning with little reclaim, and
migration splitting is consistent with harder migration destination allocation.
The decrease in ARMS `compact_fail` alone would give a misleading impression of
less pressure. These measurements do not identify every allocation call site.

## Relation to the original results and the new history question

Ratio 4 recreates a larger model advantage, but overshoots the archived May ARMS
runtime of about 115.8 seconds and also slows the model beyond its archived
approximately 100.1-second result. It therefore demonstrates a plausible
settings-sensitive mechanism, not the historical root cause or an exact
reproduction.

The [focused reserve-history follow-up](reserve_history_followup.md) finds an
explicit March shell write of `1 1 1` followed by standalone ARMS attempts without
intervening setup, plus 18 active setup editor saves from March 13–14. The
retained May cost-ablation and normal timing preparation paths both request
`256 256 32`. No dated May accepted readback or local edit establishes another
ratio. The original May timing archives do not retain relevant VM/zone/sysctl
snapshots, so the new pressure counters cannot be matched against those runs.
Neither `1 1 1` nor the editor-only 160 MiB minimum-free value was part of the
requested five-alternative table tested here.

## Artifacts and verification

- [Study and session commands](study.json): all seven pairs and 14 valid runs.
- [Timing, accepted controls, and counters](analysis/summary.json),
  [CSV](analysis/summary.csv), and [zone snapshots](analysis/node0_zones.csv).
- [Completed-result audit](results_audit.json): all before/after control values,
  exact graph/library/binary identities, preparation status, boot, model
  configuration, and recorded restoration checks passed.
- [Independent live restoration](final_restoration_check.json): all saved
  controls, IRQ affinities, uncore MSRs, and affinities of surviving original
  processes matched; no competing benchmark remained; boot and PMEM driver
  were unchanged.

Full historical preparation and the second defrag ran before each workload. One
override followed the final defrag, with eleven THP/VM controls checked before
launch and all captured controls audited before/after. No namespace detach,
kernel switch, model rebuild, or normal-policy source change was used. Preparation
changes memory placement/cache state; restoring control values does not restore
the previous physical page layout. The experiment helpers retain the new knobs
and strict readback checks for reproducibility.
