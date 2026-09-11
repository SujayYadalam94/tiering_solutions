# Linux 6.18 migration-counter interpretation

Checked upstream **v6.18** source; the installed **6.18.1-061801** tree contains headers only. This explains the recorded mechanism, not the exact historical binary or a dated cause.

## Counter and return semantics

- THP statistics use PMD-mappable folios. `THP_MIGRATION_SUCCESS` records successful unsplit handling; `THP_MIGRATION_SPLIT` records a successful split during migration, before the subpage retry. `THP_MIGRATION_FAIL` also includes THPs successfully split as fallback. The counters are not mutually exclusive. [Accounting](https://github.com/torvalds/linux/blob/v6.18/mm/migrate.c#L1689), [synchronous accounting](https://github.com/torvalds/linux/blob/v6.18/mm/migrate.c#L1875).
- Splitting can follow destination-allocation `-ENOMEM`, but also the partially mapped/deferred-split path. Counts alone cannot identify which branch fired. [Branches](https://github.com/torvalds/linux/blob/v6.18/mm/migrate.c#L1718).
- `migrate_pages()` retries split folios. If no folios remain, it explicitly resets the aggregate return to zero while preserving FAIL/SPLIT statistics. Thus successful fallback can hide the initial huge-folio allocation failure from userspace `errno`. [Retry and return](https://github.com/torvalds/linux/blob/v6.18/mm/migrate.c#L1990).
- Separately, `do_pages_move()` reports some address lookup/isolation failures through individual `status[]` entries without a nonzero aggregate return. [Per-address errors](https://github.com/torvalds/linux/blob/v6.18/mm/migrate.c#L2235).

The [6.18 migration documentation](https://docs.kernel.org/6.18/mm/page_migration.html#monitoring-migration) describes FAIL as an unsplittable THP failure. That wording is narrower than the actual code above; it must not be used to label every FAIL event a failed syscall.

## What the diagnostic observed

Source: `results/20260909T162119Z-352353/global_vm_deltas.json` and `summary.json`.

| Measurement | ARMS | Model |
|---|---:|---:|
| Global THP migration success | 4,539 | 6,834 |
| Global THP migration fail | 3,341 | 230 |
| Global THP migration split | 3,341 | 230 |
| Global compaction failure | 67,343 | 27,255 |
| Summed migration-call durations, approximate | 46.16 s | 21.36 s |
| Process system CPU, approximate | 140 s | 66 s |

ARMS coincided with approximately **14.5 times as many migration splits**. FAIL equaling SPLIT in each run is compatible with split fallback, rather than thousands of unsuccessful `move_pages` calls. The instrumented aggregate calls returned zero with no `ENOMEM`; per-address `ENOENT`/`EFAULT` results are separately present and must still be inspected.

## What this does and does not establish

Migration can block accesses to affected pages while their mappings contain migration entries. This provides a possible route from migration work to benchmark delay. [Kernel description](https://docs.kernel.org/6.18/mm/page_migration.html#how-migrate-pages-works).

However, the **global** VM deltas include work by every process/kernel worker during the sampling interval. They do not identify the triggering PID, address, migration direction, or reason. The equality of global FAIL/SPLIT counters cannot pair a particular split with a particular instrumented call.

The call-duration sum combines overlapping calls by multiple migration workers; **46.16 seconds is not 46.16 seconds of application stall**. Likewise, process system CPU accumulates across threads and can exceed elapsed time. It includes kernel work outside the instrumented migration calls. Neither figure measures BC's critical-path delay or tells us how much of the runtime gap migration explains.

The evidence supports investigating repeated huge-page splitting, small-page migration, and allocation/compaction overhead as a mechanism affecting ARMS more strongly in this diagnostic. It does not show whether allocation failure or deferred splitting predominated, whether every split belonged to ARMS, or which May-to-June setup change produced the historical performance shift. No historical counters of this kind survive in the original timing files.
