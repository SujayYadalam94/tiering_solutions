# Bounded residency diagnostic

Prepared and compiled without launching a benchmark. `runner_manifest.json` is consumable by the existing `conditions/run_conditions.py`; cases remain `may07_arms` and `may07_model`. The model retains the saved May03 weights, history mode 2, length 10, scaler 0.5, discount 99. `manifest.json` records all source/object/library hashes and exact compile/link commands.

Only the frozen May04 **scanner object** is recompiled. Original migration, policy, page, and other objects are reused. The partial-batch indexing defect and all migration/selection logic remain unchanged. The source patch is [residency_probe.patch](residency_probe.patch), with support source in [instrumentation.inc](instrumentation.inc). Rebuild using `python3 prepare.py` from this directory; that script never runs a workload.

## Bounds and placement

The existing scanner thread attempts two rounds, approximately **50 and 75 seconds after that thread starts**. Timing can slip if a scanner pass is slow. Each round:

- Tries the existing map lock once, copies shared pointers for the 64 lowest deterministic hashes of map keys, and releases the lock. No kernel query runs while holding the lock. Reports total map size, null entries, and all unaligned keys encountered.
- Copies candidate VA/near/fragmented bytes through one self `process_vm_readv` call. This avoids adding direct C++ reads racing the original unsynchronized field writers. It does **not** make the metadata coherent. Failed/partial metadata copies suppress region queries.
- Selects up to 32 candidates, aiming for 16 tracked near and 16 tracked far within that candidate sample; fills unused slots from remaining candidates. This is not guaranteed population-wide coverage or an unbiased population estimate.
- Issues one head query, one 512-base-page query, and another head query per selected region. All calls use `nodes=nullptr` and flags 0: **no migration is requested**. Takes a second candidate metadata copy afterward.

Maximum for both rounds: **64 full-region queries, 128 head queries, and four metadata-copy calls**. A 250 ms soft budget per round stops starting further region triplets. It cannot interrupt an already running syscall, map iteration, metadata copy, or stderr write. Memory held by the probe is bounded arrays and at most 64 extra shared-pointer references; no probe thread or logging queue is created.

Queries occur after the scanner's existing timer stops, outside migration-worker cost timers. They delay two scanner cycles and can perturb scheduling or memory placement; treat results as diagnostic observations, not uninstrumented performance measurements. Normal shutdown is unchanged and can truncate an in-progress round. No events are guaranteed if execution ends before the due time.

## Output

Each stderr line starts `[RESIDENCY_PROBE] ` followed by JSON, schema 1.

`kind=region` contains map `key`, `page_va_before`, alignment, near/fragmented flags before and after, metadata-change indicator, query returns/errors, three head statuses, head-instability indicator, and counts of near/far/other-node/ENOENT/EFAULT/other-error pages. `near_hidden_bytes` means resident near bytes behind a tracked-far flag. `near_credit_excess_bytes` means a tracked-near region's 2 MiB credit minus its queried near bytes. These quantities use the **before** flag; inspect changed metadata and query failures before interpreting them. Unaligned or key/VA-mismatched records require separate treatment.

`kind=summary` contains total map size, global unaligned-key count, candidate key/VA mismatches, candidate/selected/queried counts, syscall counts, metadata-copy results, map-copy time, query time, measurement time, elapsed time before the final summary write, budget status, and actual elapsed runtime. Times are nanoseconds except `runtime_s` and `due_s`.

All 512 statuses can race migration, allocation, or unmapping. **Stable bracketing heads and flags do not establish an atomic whole-region snapshot.** A mixed-node result documents the values returned during the query, not necessarily a single simultaneous layout. ENOENT/EFAULT means absent or inaccessible addresses; it does not by itself prove a permanently stale record. Residency alone cannot distinguish an intact THP from 512 base pages all on one node. No extrapolation from this small deterministic sample to total hidden memory is justified.

Classify sampled addresses using the runner's retained maps/numa_maps. The original scanner also tracks writable runtime/perf mappings; the probe adds no mapping exclusions.
