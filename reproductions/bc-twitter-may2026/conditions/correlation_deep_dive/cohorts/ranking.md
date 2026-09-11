# Candidate-ordering peer review against dated cohorts

This is a bounded review using the recovered cohorts and previously audited source history, not an additional search or experiment. Two targets must remain separate: the very slow **March15-or-earlier 6.2 ARMS archive**, and the **May 6.18 ARMS/model gap**. A change can explain the former without explaining the latter.

| Candidate | March6.2 archive relevance | May6.18 / May14→June13 relevance | Ranking and chronological constraint |
| --- | --- | --- | --- |
| Perf backing falls from approximately960MiB to320MiB through ring/event changes | Strong concrete resource difference between March and May implementations; real memory/fragmentation mechanism | The May-sized ARMS configuration was already present by the retained April30 binary, before slow May7 and May12/14 | **High for March→later comparison; reject as the sole May→June change.** The640MiB backing difference is not proof of640MiB less node0 availability. |
| March migration-failure bookkeeping fix | Plausible implementation difference after the old March archive | Predates slow May7 and May12/14 | **Relevant only to older-era comparison without further evidence.** A one-time March fix cannot date the observed May→June transition. |
| May9 transient failure denominator / demotion-cost factors | Not relevant to a March execution | Brief saved edits were reverted; known slow May12/14 long BC cohorts occur afterwards | **Low.** No execution tie to the transient versions. Even if a brief version was built, a persistent explanation requires evidence of the loaded artifact surviving rebuilds; none recovered. |
| Unconditional migration printf removed May9 | Introduction date unknown; March linkage unproven | A plausible source of extra migration-path overhead, but slow May12/14 BC gaps remain after removal | **Low as a sole cause; possible partial May7 contribution only.** Cannot by itself explain why May14 remains slow and June becomes fast. These are different workload lengths, so later long-run slowness does not quantify its possible effect on May7's10-trial total. |
| June13 boot / full-suite predecessor sequence | No direct March linkage | Actual contemporaneous conditions differ; preceding BC/PR Kron executions fit the start of the first fast Twitter cohort | **Highest remaining dated correlation, with substantial limits.** May14 was also a recent boot. Thus “fresh boot” alone is not a distinction. Slow May7 also had earlier workloads. Exact boot allocator state or exact predecessor sequence remains plausible; “running after anything warms it up” is unsupported. |
| June13 age adapter, restored June18 | Not relevant | Could alter the first June model, but no direct normal-ARMS object-code change; June22/23 still has the small BC gap after restoration | **Reject as sole explanation for persistent BC Twitter change.** It remains a real confounder for June4040 model comparisons and may help explain different responses in other workloads. |
| July verbosity changes | Not relevant | First small-gap6.18 cohort already June13/14 | **Too late to originate the June transition.** Could affect later results, not the earlier observation. |
| August31 MEMTIS installation / later associated cleanup | Not relevant | Both June6.18 and August27 HybridTier small-gap observations precede it | **Too late to originate the observed transition.** Real later effects must be treated separately. |

## Ordering for the May question

1. **Recover or test the specific June execution conditions:** allocator/THP state and exact full-suite predecessor sequence, holding policy and model artifacts fixed. This is a correlation to isolate, not an identified cause.
2. **Unrecovered state or artifacts:** historical loaded-library/forest identity, allocation placement, graph storage/layout, and migration failure type remain open because execution manifests and prelaunch snapshots are missing. These are unknowns, not positive evidence of a hidden change.
3. **May9 logging or transient edits as partial contributors:** plausible mechanisms, but weakened strongly by later slow runs and missing execution ties. They should not outrank the actual May14→June correlation.
4. **March and post-June changes:** exclude from the primary explanation unless a separate, explicit execution/provenance link overcomes the chronology.

## Cross-workload constraint

PR Twitter100-trial runs already favor ARMS in May13, and from May13 to June14 the model improves more than ARMS. BC Twitter100-trial runs show the opposite relative response. Therefore a proposed explanation must be sensitive to workload behavior rather than merely asserting that all ARMS execution became uniformly faster. Shared migration pressure, fragmentation, or loading-overlap mechanisms can be workload-sensitive; the timings alone do not select among them.

The May14→June13 bracket is an observed long-BC transition, not proof of one permanent global change. The standard10-trial series has no matching intermediate ARMS result. A May9 change could contribute to today's10-trial speed while a separate condition causes May's long-run penalty, but that is a multi-cause hypothesis requiring additional evidence rather than an exact historical identification.
