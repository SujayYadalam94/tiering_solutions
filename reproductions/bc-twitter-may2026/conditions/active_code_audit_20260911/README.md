# Active ARMS code and model leakage audit

September 11, 2026. The user has already tested ARMS with `can_demote` initialized;
this investigation does not propose repeating that experiment.

Fresh compilation and linking, ELF inspection, source-history comparisons, and a
small standalone bounds diagnostic were performed. No BC/PR benchmark, host
setting, production source, or production library was changed. Build snapshots
are under `/tmp/arms-active-code-audit-20260911`; hashes, differences and results
are retained here. The first parallel compiler driver was terminated before
completion; the remaining builds were subsequently completed successfully.

## Result

There is no evidence of normal ARMS linking or executing a forest, allocating
the model/training log, or inheriting model heap state through the normal runner.
No additional post-May ARMS scoring or migration mechanism change was established.

There is one additional concrete post-May-7 **model-side** issue: the May 9
boundary-score estimator executes despite its cost contribution being commented
out, and its index can underflow. A standalone diagnostic confirms the bounds
error; the installed model library still contains the actual load and update.
It is excluded from ARMS and cannot explain the original May 7 ARMS slowdown.

## Fresh compilation gives stronger evidence than source inspection alone

Compiled all 11 common translation units for May 4 (`8d6a8bc66`), May 14
(`4da3d6c3f`), and the current source snapshot, plus the current Parquet translation
unit. All three ARMS libraries linked. Compiler: GCC 15.1.0; normal C220G5 flags
include `-std=c++17 -pthread -O3 -DNDEBUG -fPIC -DUSE_MODEL=false -DC220G5`.
See [source hashes](manifest.json), [verification](verification.json),
[whole-object comparison](whole_object_comparison.json), and
[disassembly comparison](object_comparison.json).

The following complete object files, including their constants and relocations,
are **byte-identical** between freshly compiled May 14 and current source:

* `pebs_scan_thread.o`
* `pagemap_scan_thread.o`
* `migration_worker.o`
* `policy_thread.o`
* `timer.o`
* `groups.o`
* `page.o`

For May 4 to May 14, nine of the eleven objects are byte-identical. Only
`arms_kernel.o` and `page.o` differ. Their source changes are the previously
documented demotion flag/caller changes, model-only mechanisms and model-related
diagnostic defaults. The earlier source tree already contains shared model
metadata, its initialization, and the optional sample exclusions.

The May 14 to current differences in the remaining objects are accounted for by:

| Object | Difference and normal-ARMS reachability |
|---|---|
| `arms_kernel.o` | Replacement of optional OFFCORE diagnostics with write-CAS diagnostics; disabled-counter initialization changes remain at startup. All-NUMA and model-boundary changes are gated off in normal ARMS. No forest/logging scoring call is generated in its normal object. |
| `hook.o` | Additional Arrow/Parquet helper-name matches are checked during startup mapping discovery. The training main/exit wrappers are compiled out. Ordinary ARMS later bypasses the optional IP filter. This startup parsing difference is not literally identical code, but no new steady-state policy path was identified. |
| `logging.o` | Training serializer changes and changed `data_row` layout affect retained helper functions. Normal ARMS does not create an access log or enter row extraction/model scoring. |
| `model.o` | Retained zero-output wrapper code has different row strides because `data_row` grew. Feature extraction/forest evaluation is compiled out; the active ARMS objects do not call these wrappers. |

The May 14-to-June cohort revisions `4da3d6c3f` and `d6f720be6` have no tracked
C++/header/Makefile differences. The previously recovered June 13 editor change
to the model feature adapter remains a separate uncommitted confounder; see
[the earlier detailed audit](../arms_version_provenance/root_cause_code/README.md).
Fresh builds do not establish the identity of the unavailable original May binary.

## Model linkage, allocation and run-to-run state

* Normal Makefile objects are under `build/obj/nomodel/C220G5`, with
  `-DUSE_MODEL=false`. Model combinations have separate object directories.
  The normal shared-library target does not include `models/*.o`.
* The inspected installed ARMS library and all three fresh ARMS libraries have
  **no `forest_root` symbol**. The installed ARMS library depends on libnuma,
  libstdc++, libm, libgcc and libc; neither it nor BC has a model/Arrow/Parquet
  dependency. `/etc/ld.so.preload` was absent in this inspection.
* Normal objects for the core, PEBS scanner, pagemap scanner and policy thread
  have no relocation calls to model prediction, row extraction, log construction,
  or group-tracker construction. Model-named wrappers remain exported in the
  library, but their presence is not evidence that ARMS calls them.
* `arms_start_tiering()` explicitly sets the log/group pointers to null for
  normal ARMS. `access_log` is a global **pointer**, not a global log object whose
  constructor allocates storage before `main`. The fresh libraries have the same
  two C++ global-initializer functions: core container state and migration-worker
  container state; no added forest/model initializer was found.
* The normal runner assigns one explicit `LD_PRELOAD` path when starting each
  new benchmark. It does not append the previous model library, use `dlopen` to
  switch policies in one address space, or reuse a model process. A preceding
  model can still change kernel allocator/cache state; that is distinct from
  retaining its heap or forest in the next ARMS process.

See [linkage verification](linkage_verification.json), installed/fresh symbol and
dynamic-section reports, and `*.existing_calls.json` in this directory.

## Layout and stale-object checks

The compiled [layout probe](layouts.json) found:

| Type/offset | May 4 | May 14 | Current |
|---|---:|---:|---:|
| `sizeof(page_info)` | 800 | 800 | 800 |
| `sizeof(score_entry)` | 24 | 24 | 24 |
| `sizeof(access_log)` | 208 | 208 | 208 |
| `sizeof(data_row)` | 320 | 320 | 328 |
| `page_info::last_seen_scan` offset | 288 | 288 | 288 |
| `page_info::last_logged_row` offset | 792 | 792 | 792 |

Adding `can_demote` consumes padding rather than increasing per-page allocation
size or shifting the following fields. The row increase is from the July change
of a float diagnostic field to a uint64_t field, not a larger ordinary ARMS page
record or newly allocated forest.

The installed ARMS library's complete disassembly matches a relink of its current
ordinary object files. Eleven of twelve existing objects have disassembly
identical to fresh current compilation. The exception is **only** `page.o`'s
already-discussed `can_demote` reset: the current source initializes it, while
the installed object/library still has the no-initialization version. The exact
difference is [preserved](existing_to_fresh_page.cpp.objdump.diff). This does not
dispute the user's earlier initialized-flag runs or explain their results.

The Makefile still omits some headers, including `page.h`, from explicit object
prerequisites, and changed compiler options are not themselves dependencies.
Those are build-system exposures, not proof of a historically mixed build. The
actual-object comparison found no further current mismatch, and repeated clean
builds weaken that explanation for the May-to-June transition.

## Additional model-side issue: a partially disabled boundary feature

Introduced in `41e8adedd` on May 9, after the May 7 target. In
[arms_kernel.cpp](../../../../arms_kernel.cpp:1531), the function checks that
`max_hugepages` is nonnegative and less than the vector length, then reads:

```cpp
scores[static_cast<size_t>(max_hugepages) - 250].score
```

For `0 <= max_hugepages < 250` and `max_hugepages < scores.size()`, the index
underflows. The capacity estimate is tracked DRAM pages plus estimated free
huge-page slots; it is not the filename's capacity label. Whether an actual BC
execution reaches that condition has not been measured.

The caller still computes the estimate, scans the page vector, and updates its
moving average even though both `cost += boundary_score_cost` lines are commented
out. The installed BC Twitter model disassembly contains the score load and
moving-average update, so this work was not fully optimized away. See
[compiled evidence](model_boundary.disassembly.txt). No such boundary state or
load appears in the ordinary ARMS build.

The [standalone probe](boundary_probe.cpp) copies the actual helper and EWMA
implementation and uses the current `score_entry` definition. It performs no
NUMA setup, sampling, page migration, or benchmark work. Under ASan/UBSan:

| Vector entries | Estimated capacity | Outcome |
|---|---:|---|
| 1000 | 249 | Confirmed heap-buffer-overflow read |
| 1000 | 250 | Valid read/update; successful exit |
| 1000 | 1000 | Existing upper guard returns; successful exit |

[Results](boundary_probe_results.json), [failing diagnostic](boundary_probe_249.txt).
LeakSanitizer was disabled for this bounds-only probe. This demonstrates the
invalid read, not a memory leak or occurrence in historical runs. For capacities
at or above 250, the helper is ordinarily redundant overhead because its result
does not enter the migration cost. There is no measured runtime attribution.

The appropriate isolated correction would remove the unused estimator call, or
restore an intentional cost use with a valid lower-bound guard. No production
correction or timing experiment was performed by this investigation.
