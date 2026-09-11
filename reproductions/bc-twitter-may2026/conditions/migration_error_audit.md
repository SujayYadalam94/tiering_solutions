# Migration failures: additional condition-sensitive error handling

Read-only audit, September 9, 2026. This adds a concrete failure-handling issue to the [fragmented-batch indexing audit](capacity_policy_audit.md). Neither issue has been demonstrated to cause the historical timing gap; no production source was changed and no new workload was launched for this audit.

## Partial migration can be misclassified as a complete ENOMEM failure

Both the current and frozen May 4 `migration_worker.cpp` use this pattern in `log_move_base_pages` and `log_move_page`:

```cpp
long ret = numa_move_pages(...);
int e = errno;
if (ret != 0 && e == ENOMEM) {
    // Treat the migration as not having happened.
    return ...;
}
```

Current source locations: [base-page path](../../../migration_worker.cpp#L178), [huge-page path](../../../migration_worker.cpp#L259). The same checks occur at lines 182 and 264 in [frozen May 4 source](../source/may04/migration_worker.cpp). Git history places the relevant logic in March, preceding the May benchmarks.

The API distinguishes a syscall error (`-1`, with `errno`) from a positive result, which counts pages that were not migrated for nonfatal reasons. A positive return can coexist with successful migrations in the status array and does not make an earlier `errno` value a description of the current result. [move_pages return-value documentation](https://man7.org/linux/man-pages/man2/move_pages.2.html#RETURN_VALUE).

For example, suppose a worker retains `errno=ENOMEM` from a previous call, then submits ten pages and receives `ret=1` with nine successful statuses. The huge-page path's condition still succeeds and returns `vas.size()` (ten) before processing those statuses. It reports all ten as failed and leaves successful pages' tracked residency unchanged. That can influence demotion counts, retries, later occupancy estimates, and measured migration costs. This is an explicit triggering scenario, not an observation that this exact sequence occurred in an accepted benchmark.

Disassembly of the installed `/usr/lib/x86_64-linux-gnu/libnuma.so.1` shows `numa_move_pages` calls `move_pages` and returns directly, with no `errno` cleanup in that wrapper. The worker also consults `errno` after its own higher-level helper at current line 391 to decide whether to retry a half batch; this error reason is not carried explicitly with the helper's failed-page count.

## Why this can affect the systems differently

ARMS and the model share these workers, but they select different migration streams. The retained runtime snapshots show much more failure/compaction activity in some ARMS runs. A common bug can therefore have different effects depending on which batches, partial failures, and fragmented pages each policy produces. The necessary historical per-call records have not been recovered.

The existing indexing bug is separate: fragmented pages are removed from the syscall input vector, while returned statuses are applied to `pages[i]` in the original unfiltered vector. A mixed batch can associate a status with the wrong page even when the syscall itself is successful. Both problems can make the library's tracked DRAM placement disagree with actual placement.

The focused diagnostic would count mixed batches and positive-return/stale-ENOMEM combinations, record migration attempts and durations separately from application read/compute time, and sample tracked versus actual node residency. Instrumentation should be isolated from production libraries and evaluated for overhead. A fix improving today's runs alone would not prove that the original May gap came from this bug.
