# Isolated May ARMS/model migration diagnostic

The two diagnostic libraries and a separate indexing-correction variant have now been run with the existing condition runner. [Completed results and interpretation](runtime_findings.md) include all three runs and verified host-control restoration. Normal source and libraries are unchanged.

| Runner case | Library | Exact baseline |
|---|---|---|
| `may07_arms` | `libraries/may07_arms_probe.so` | Frozen May 4 source, C220G5, normal ARMS |
| `may07_model` | `libraries/may07_model_probe.so` | Same source, May 3 model object, history mode 2, length 10, cost scaler 0.5 |

`prepare.py` copies the frozen May 4 source, inserts `instrumentation.inc` into `migration_worker.cpp`, and adds a declaration plus one report call in `arms_kernel.cpp`. The exact source diff is `migration_probe.patch`. Only these two objects are recompiled for each case; every other original reproduction object is reused. The model weights retain SHA-256 `763ad7a415847b832a89389d6a20bca1a4f8d01871b0d69186aee7f943591585`.

Compiler options match the original reproduction build: GCC 15.1.0, `-std=c++17 -O3 -DNDEBUG -pthread -fPIC -I.`, C220G5 and the original case defines. Link order/libraries also match. `manifest.json` records exact commands, source/object/model/library hashes, and which objects were reused. `runner_manifest.json` contains only the two diagnostic cases and names the instrumented source separately from the untouched May snapshot.

## What the diagnostic records

The report is a JSON object prefixed `[MIGRATION_PROBE]`, written once to stderr immediately before the original normal-shutdown `_exit(0)`. Four lanes separate promotion/demotion and huge-address/base-page calls.

- Started/completed/in-flight calls, requested address entries, and elapsed nanoseconds around completed `numa_move_pages` calls.
- `ret == -1` errno groups, positive-return call/count totals, and valid per-address status categories.
- Positive-return calls that retain ENOMEM, including successful target-node statuses the existing ENOMEM early return would discard.
- Huge batches with no filtering, mixed fragmented/unfragmented pages, all fragmented pages, and empty batches.
- Slots where filtered `vas[i]` differs from original `pages[i]->va`, plus slots where the original status-processing loop is reached and valid successful statuses would be associated with the wrong page.

“Requested addresses” are syscall entries, not universally base-page counts: the huge lane supplies one address per tracked huge page, while the base lane expands each tracked page into base-page addresses. Counts include retries and repeated addresses; they are not unique migrated-page counts. A target-node status is a syscall result, not an independent placement query or proof the page changed nodes during that call. `batch_entries` and filter-shape counters apply only to the huge lane, after the original retry-limit check.

## Scope and limitations

The diagnostic preserves syscall arguments, return values, and errno. It restores incoming errno immediately before the real call, captures errno immediately afterward, and restores it after all timing/counting. It does not clear stale errno, repair index mapping, change retry/selection logic, add interposition, or register a destructor/global constructor.

The original normal shutdown exits without joining workers. **This remains unchanged.** Reports are live snapshots after `terminated=true` and before `_exit`. Each atomic read is safe, but the collection is not coherent: workers may finish calls between individual reads, and outstanding calls/tail events can be truncated. `inflight_calls` is an explicit atomic gauge; do not infer exact outstanding work by subtracting independently read started/completed counters. Reports are absent if the process dies before normal shutdown/initialization.

The probe adds two clock reads, a status scan, and relaxed aggregate atomic updates per syscall. Although the reported syscall duration excludes counter aggregation, the original outer migration-cost timer includes instrumentation overhead. **This can perturb adaptive cost estimates and subsequent decisions.** Use the pair to assess whether error paths occur, not as an unqualified performance comparison against uninstrumented runs. A detected error path would not by itself prove it caused the historical May/June difference.

All counters are constant-initialized lock-free storage; no new initialization callback is added. Reports use a fixed stack buffer and one write. `synthetic_report.json` comes from a standalone mocked-syscall validation, not a benchmark. That validation covered incoming/result errno preservation, partial success with stale ENOMEM, ordinary success, ENOMEM/EPERM failures, filtered-index mismatches, empty calls, and report-once behavior. Neither real `move_pages` nor the ARMS initializer was called. Object inspection verified the report hook resolves and the original one-entry migration-worker initialization array is retained.

## Runner

The following command **executes two benchmarks and historical preparation**; omit `--run` for the existing runner's validation mode:

```bash
sudo python3 reproductions/bc-twitter-may2026/conditions/run_conditions.py \
  --preset may04_full \
  --manifest reproductions/bc-twitter-may2026/conditions/arms_version_provenance/migration_probe/runner_manifest.json \
  --graph /users/zimooo2/tiering_solutions/data/graphs/twitter.sg \
  --run --cases may07_arms may07_model
```

The parent condition runner records the setup preset and host readbacks and restores host controls. The initial library-preparation phase did not execute benchmarks; the subsequent executions are documented in the linked results above.
