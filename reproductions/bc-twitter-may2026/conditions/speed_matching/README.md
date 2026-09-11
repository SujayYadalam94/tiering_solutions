# Historical reconstruction, informed by capacity sensitivity

The goal is to reproduce the original experimental conditions and paired speeds, not to select a reserve value solely because its timings match. The user clarified this after the one intermediate `8 8 8` pair. That pair is complete; **no `16 16 16` execution was started**.

## Completed capacity-sensitivity point

| Condition | DMA32 protection | ARMS total | Model total |
| --- | ---: | ---: | ---: |
| Original May7, mean of three executions | Not recorded | 115.804 s | 100.127 s |
| Earlier ratio32 test | 93.41 MiB | 109.05 s | 99.39 s |
| New ratio8 test | 373.65 MiB | 123.60 s | 109.33 s |
| Earlier ratio4 test | 747.31 MiB | 136.85 s | 112.39 s |

The original targets were verified against original `c220g5` files, including hashes and mtimes. Final-copy dates are not used. [Exact target/cohort audit](../arms_version_provenance/may_memory_cleanup/speed_match_targets.md).

At ratio8, ARMS read/average-trial times were 41.37079/8.18184 seconds; model times were 39.69854/6.92849. Both completed ten trials successfully, without detected benchmark overlap. The 14.27-second total gap is close to the original 15.677-second gap, but **both absolute times are too slow**, by 7.80 and 9.20 seconds. Matching only the gap would conceal that mismatch. [Results and exact inputs](results.json).

The ARMS target lies between the observed ratio32 and ratio8 outcomes, while the model target is close to ratio32. This supplies a scale for sensitivity to protected lower-zone capacity on this boot. It does **not** translate into an exact number of historically occupied MiB: protection is allocation-class-specific, contiguity matters, and the two policies respond differently. These values are not evidence that any such ratio was used in May.

The baseline itself varied: opening/closing ARMS totals in the preceding study were 106.28/111.42 seconds, while model totals were 97.90/97.67. Small differences between conditions should not be treated as reproducible effects from a single pair.

## New conditions supported by historical source and executions

### May14 long runs selected only BC Twitter; June selected the full suite

The outer runner's logic barely changed, but it sourced a separately edited `measurement_workloads_long.sh`:

- May14 06:16 UTC editor save enables only BC Twitter long. Original timing order shows model→ARMS repeated three times without another retained workload between pairs.
- June13 22:27 UTC save enables eight workloads. Original timing order shows four Kron executions before BC: BC-Kron model→ARMS, then PR-Kron model→ARMS, then BC-Twitter model→ARMS.
- The same full-suite order is present in June22's original results. The four Kron commands account for about 73.7/72.5 minutes of timed work before the first BC pair in the two June cohorts.

This is a demonstrated setup-context change, not an inferred reserve override. It can be reproduced as a specific preceding-workload sequence while retaining the historical VM controls. Whether it explains a different allocator layout or timing must still be tested. [Verified chronology and source diff](../arms_version_provenance/may_memory_cleanup/long_prelude.md).

The slower May14 cohort started its first BC model about 14.6 minutes into the boot; June14 started about 97.5 minutes in. June22 had a much longer unrecorded interval before the retained suite. These are different boot/workload histories, though missing historical process and zone snapshots prevent reconstructing every initial condition.

### The May7 cost-ablation predecessor chain differs from an isolated ARMS/model pair

Every original May7 BC ARMS run immediately followed **PR-Kron model cost1.5**, with normal preparation before BC. BC then ran:

`ARMS → model cost0.125 → cost0.25 → cost0.5 → cost1.0 → cost1.5`.

Thus the selected cost0.5 model did not immediately follow ARMS; two other BC model configurations intervened, each with full preparation. Today's isolated comparison uses ARMS→cost0.5 directly. This is another actual prior-workload difference to preserve when reconstructing the May7 cohort. [Exact original sequence](../arms_version_provenance/may_memory_cleanup/profiling_order_history.md).

This does not contradict alternating policies sharing one boot and graph. The candidate is persistent allocator/kernel state created earlier in the sequence, rather than an unproven per-policy disk change or simultaneous memory consumer.

### Preparation elapsed time changed, but duration alone is insufficient

May14's model→ARMS gaps were 44.8–45.0 seconds. June14's were 64.6–92.0 seconds. These are inferred intervals outside the timed workload, not direct measurements of the defrag command.

However, **June22 was also fast with 45.4–45.7-second gaps**. A longer wait or preparation interval alone cannot explain both faster June cohorts. Adding a fitted delay would not reproduce an established causal condition.

## What the evidence does not support

No new dated May reserve value, overlapping external profiler, PMEM/DAX binding change, cgroup limit, or surviving tierer process was recovered. Historical defrag explicitly calls `sync` and drops caches repeatedly, so an omitted dirty-cache flush is not a newly identified difference. The older PMEM metadata experiment and THP/driver history are documented in [the broader investigation](../arms_version_provenance/may_memory_cleanup/README.md).

Kron preconditioning is not yet a universal explanation: the slow May7 standard sweep already included Kron, while slow May14 long BC ran alone. The exact sequence, duration and policy variants matter; simply labeling a run “mixed” or “isolated” is insufficient.

## Historically grounded next comparison

For the May7 target, reproduce its documented PR-Kron predecessor and BC cost sequence using the normal historical reserve setting, then compare with the isolated pair. For the May14→June transition, compare BC-only model→ARMS with the documented four-Kron prefix followed by the same BC pair. Keep the cohort-specific model/source and trial counts fixed, and capture zone/buddy state before and after preparation.

These are proposed historical reconstructions, not executions already performed. The retained evidence identifies the sequence to replay; it does not yet prove the resulting speed or recover all historical initial kernel state.

The best-supported May3 PR-Kron forest for the May7 predecessor has now been frozen with its text/report and [input manifest](historical_models/may03_pr_kron/manifest.json). It matches the frozen May feature layout and retained training-repository bytes; the intended predecessor configuration is `[2,10,1.5]`. There is no historical per-run library hash proving that exact object was loaded. [Model provenance and alternatives](../arms_version_provenance/may_memory_cleanup/replay_model_provenance.md).

The completed ratio8 session restored all captured VM/THP controls, IRQ affinities, uncore MSRs and surviving original processes' main-thread affinities. A separate live verification passed; no benchmark remained active and the boot/PMEM binding were unchanged. [Restoration check](final_restoration_check.json).
