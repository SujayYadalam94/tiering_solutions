# Remaining settings supported by history

The subsequently authorized table tests are now complete. See the
[14-run results](../../../historical_settings_tests/results.md). The candidate
ranking and untested status below describe the investigation before those runs.

Read-only investigation, September 9, 2026, following the completed khugepaged
tests. No new benchmark, host-setting change, or policy rebuild was performed.

The strongest remaining candidates are lower-zone allocation protection,
fragmentation-triggered watermark boosting, and the minimum-free request. They
have real historical alternatives, but **none has been established as the setting
used for the slow May cost-ablation runs**. The complete command excerpts and
editor-save provenance are in [the history report](../candidate_settings_history.md)
and its [filtered evidence](../candidate_settings_history_evidence.json).

## Candidate order

| Suggested order | Isolated override | Historical support | Question it would answer |
|---|---|---|---|
| 1 | `vm.lowmem_reserve_ratio="32 32 16"`, then separately `"4 4 4"` | March-saved shell history explicitly writes each value immediately before standalone ARMS; normal baseline is `"256 256 32"`. | Can greater lower-zone protection reproduce slower ARMS, despite the same 4 GB boot? |
| 2 | `vm.watermark_boost_factor=0` | Active committed setup before March 14 `d87369bce`, which changes it to `10000`. | Does reclaim triggered by fragmentation affect the two policies differently? Direction is not established. |
| 3 | `vm.min_free_kbytes=16384` | Active committed setup before that same March 14 commit; baseline request is `1048576`. `163840` is an additional March 13 editor-save value. | Does different allocation headroom change compaction, migration success, or the policy gap? |
| 4 | `khugepaged/pages_to_scan=4096` | March 13–14 editor saves, versus baseline `8192`. No matching executed run/readback retained. | Is ARMS sensitive to the amount of scanning per batch as well as its sleep interval? |

These are independent experiments, not a recommendation to restore all March
settings together. They have no isolated override in the retained condition
sessions. Older watermark boost editor values `100` and `15000` are weaker
provenance than the committed `0`.

## Why the reserve ratio deserves a test

In the saved baseline snapshot from session `20260909T173344Z-414920`, node 0 has
765,246 managed Normal-zone pages, 415,411 DMA32 pages, and no populated Movable
zone. With 4 KiB base pages, the Linux 6.18 reserve formula gives:

| Ratio vector | DMA32 protection for higher-zone allocations | Difference from baseline |
|---|---:|---:|
| `256 256 32` | 11.68 MiB | — |
| `32 32 16` | 93.41 MiB | +81.73 MiB |
| `4 4 4` | 747.31 MiB | +735.63 MiB |

Calculation: `floor(765246 / DMA32_ratio) * 4096 / 1048576`.
The baseline calculated 2,989 pages agrees with the saved zone protection
readback. Alternative rows are projections, not settings we applied or historical
measurements. [Machine-readable projection](reserve_projection.json).

Smaller ratios protect more lower-zone memory. This protection contributes to
allocation watermark eligibility; it is not a physical allocation or a blanket
subtraction from all free memory. [Linux 6.18 VM documentation](https://raw.githubusercontent.com/torvalds/linux/v6.18/Documentation/admin-guide/sysctl/vm.rst).

The migration target allocation uses `GFP_HIGHUSER_MOVABLE` / `GFP_TRANSHUGE`.
Their highest GFP zone is Movable; with this host's empty Movable zone, the
effective fallback includes Normal then DMA32. DMA32 protection for Movable and
Normal is equal here. `__GFP_THISNODE` restricts the node, not fallback among its
zones. This makes the protection relevant to migration destination allocations.
[Migration code](https://github.com/torvalds/linux/blob/v6.18/mm/migrate.c#L2080),
[GFP zone mapping](https://github.com/torvalds/linux/blob/v6.18/include/linux/gfp.h#L114),
[GFP definitions](https://github.com/torvalds/linux/blob/v6.18/include/linux/gfp_types.h#L373).

The frozen May policy code estimates usable fast memory from aggregate node-0
`MemFree`, subtracting approximately 5% of node capacity. It does not use the
per-zone protection thresholds: see
[free-memory estimate](../../../../source/may04/arms_kernel.cpp#L1653) and
[demotion selection](../../../../source/may04/arms_kernel.cpp#L1727).
Consequently, a restrictive ratio could let its capacity estimate remain positive
while some destination allocations become difficult. This is a concrete mechanism
compatible with consistent results in one boot. Both policy builds share this
code; a larger ARMS slowdown is a hypothesis that still requires measurement.
The snapshot alone does not demonstrate that these thresholds were reached.

## Limits on the historical explanation

The ratio commands are in a history snapshot saved March 26, with no per-command
timestamps. `32 32 16` is between March 11 and March 15 commit anchors; `4 4 4`
follows a March 15 anchor and a recorded reboot. Neither is dated to May. The
history saved May 11 and the current history contain no later corresponding
override. Editor timestamps date saved contents, not execution.

Full default setup resets all four candidate knobs. Standalone ARMS can inherit
a manual override, whereas the archived sweep prepares both policies explicitly.
Thus the direct-run evidence does not establish different settings inside the
cost-ablation sweep. The May 4, May 14, and June setup snapshots share Git blob
`9269f544c753641578e44d8709ec815d031f7770`; merge-aware all-ref review did not
recover a hidden relevant May-to-June settings delta.

The March 14 patch's apparent `watermark_scale_factor=1` to `10` change is
misleading: the earlier script already overwrites `1` with `10` later. Reverting
that revision does not produce an effective value of `1`. The older
`lowmem_reserve_ratio="1 1 1"` Git line is commented. The September MEMTIS-only
setup branch is not reached by normal ARMS/model default preparation. No actual
cgroup restriction, relevant pinning override, or nonzero zone-reclaim command
was recovered.

Exact relevant patches are retained here:
[March 14 setup changes](d87369bce_setup.patch),
[March 16 sleep changes](a6ad3eb0d_setup.patch), and
[March 17 UTC restoration](ec524f599_setup.patch).

Any subsequent test should apply one override after the final setup/defrag and
record the accepted value and zone watermarks immediately before launch. THP
configuration can raise the effective minimum-free setting, so a script request
alone does not establish that experiment's allocation conditions. Compare graph
read time, iteration time, migration outcomes, compaction, and zone free memory
for both policies on the same graph and boot.
