# HDD cost ablation with historical preparation

The HDD sweep preserves the expected qualitative cost response during BC
computation: increasing the model scaler from 0.125 to 0.5 improves iteration
time, followed by a plateau. It does **not** recover the historical magnitude of
the model's advantage. Variation during loading also substantially changes the
ranking by total runtime.

## Measurements

The six new cases completed in order on the same September 9 boot of
`6.18.1-061801-generic`, with ten iterations, return code zero, and no detected
benchmark interference. They used the HDD `/dev/sdb` graph, frozen May 4 source,
and the May 3 forest candidate. Before every case, the driver replayed the May 4
`setup.sh 4009 default c220g5` and `defrag.sh` preparation. CPU affinity was
`0-9,20-29`, `OMP_NUM_THREADS=16`, memory binding was `0,1`, and both virtual-step
environment variables were 10000. See the [session manifest](../sessions/20260909T130805Z-154045/manifest.json),
[input identities](../sessions/20260909T130805Z-154045/results/20260909T130805Z-154045/inputs.json),
and [new measurements](../sessions/20260909T130805Z-154045/results/20260909T130805Z-154045/results.json).

Times are seconds. Historical totals are means of three May 7 runs; new values
are one execution per setting. A positive advantage means the model finished
sooner than ARMS in the same group. The historical runs have no saved read or
iteration split. Their provenance and individual ARMS/scaler-0.5 timings are in
the [historical storage investigation](../../../../docs/arms_model_storage_investigation.md).

| Case / model scaler | May 7 mean total | New HDD total | New read | New mean iteration | May 7 advantage | New advantage |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ARMS | 115.804 | 132.37 | 68.543 | 6.33899 | — | — |
| 0.125 | 113.752 | 130.84 | 68.788 | 6.17700 | 2.052 | 1.53 |
| 0.25 | 107.968 | 133.61 | 73.524 | 5.97956 | 7.835 | -1.24 |
| 0.5 | 100.127 | 130.75 | 71.722 | 5.87354 | 15.677 | 1.62 |
| 1.0 | 100.552 | 129.68 | 69.816 | 5.95632 | 15.251 | 2.69 |
| 1.5 | 100.495 | 123.74 | 64.648 | 5.87860 | 15.309 | 8.63 |

The ten-iteration sums are 63.390 seconds for ARMS, then 61.770, 59.796,
58.735, 59.563, and 58.786 seconds as model cost increases. Thus the best observed
computation advantage is **4.655 seconds**, at scaler 0.5. The model's computation
improves by **3.035 seconds** from scaler 0.125 to 0.5. Historically the corresponding
*total* improvement was **13.625 seconds**; the old logs cannot tell us how much
of that came from computation.

Loading spans **8.876 seconds** across these executions. At scaler 0.25, the model
saves 3.594 seconds during the ten iterations but spends 4.981 seconds longer in
the read phase than ARMS, causing its total to be slower. At scaler 0.5, its
4.655-second computation saving is partly offset by a 3.179-second read penalty.
Scaler 1.5 has essentially the same computation time as scaler 0.5, but reads
7.074 seconds faster; this accounts for almost all its 7.01-second total lead over
scaler 0.5. Total minus reported read and ten-iteration sum is only 0.282–0.437
seconds across the six cases.

## Interpretation and limits

The model remains responsive to the historical cost knob on the HDD. Its
compute trend is consistent with the broad historical shape, so the present
experiment does not support treating the cost setting as ineffective. However,
replaying this historical preparation with HDD storage, historical source, and
the recovered model candidate is **insufficient to reproduce the old gap**.
Even the computation saving after separating today's read phase is much smaller
than the historical total advantage.

This sweep also shows that a large total-time difference between cost settings
can arise mainly during the read phase. It does not establish whether this
read variation is caused by cost-dependent tiering behavior, storage/cache state,
or run order. The order was fixed, and there is only one new run per setting.
We therefore cannot infer that scaler 1.5 is superior to 0.5, or reconstruct May's
disk from these totals. Loading may also influence the memory placement inherited
by computation, so subtracting read time is a useful decomposition rather than
a separate controlled experiment.

The preparation scripts returned zero, but that does not mean every internal
operation succeeded. The [ARMS preparation log](../sessions/20260909T130805Z-154045/results/20260909T130805Z-154045/01-may07_arms/preparation.log)
records the historical sample-rate write failing with `Invalid argument`, along
with process migration/affinity failures. The actual pre-run readbacks show
`perf_event_max_sample_rate=1000000`, `perf_cpu_time_max_percent=0`, NUMA balancing
off, and transparent huge pages/defrag set to `always`. The intended 4 GB boot
remained in use: node 0 reports 4,737,988 kB total, with 2,572,056–2,619,844 kB free
before these runs. These readbacks describe today's conditions; the equivalent
historical readbacks are unavailable. The session manifest also records the
deliberate omission of broad process killing/module removal.

The surviving May 3 forest and May 4 source remain historical candidates because
the original runs did not record library/model hashes. Together with the missing
historical phase timings and host-state readbacks, this limits the result to the
tested combination. It narrows the investigation away from a simple “restore HDD
and historical preparation” explanation without identifying the missing condition.
