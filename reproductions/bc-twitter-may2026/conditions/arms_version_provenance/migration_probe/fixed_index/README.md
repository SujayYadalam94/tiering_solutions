# Isolated filtered-index correction

The corrected **ARMS-only library is compiled and ready; no benchmark was launched by this package**. Only `migration_worker.cpp` was recompiled, using the original ARMS flags; the instrumented kernel object and all other original probe objects were reused. [Build record](build_record.json), [runner manifest](fixedrunner_manifest.json), [build script](build.py).

The source tree starts from the instrumented May 4 probe. Only `migration_worker.cpp` differs, as recorded in the original preparation-time [source manifest](source_manifest.json) and [fixed_index.patch](fixed_index.patch). The model library remains untouched.

The correction keeps a `submitted_pages` vector alongside `vas` and applies each returned status to the corresponding submitted page. Original mixed-batch accounting still sees the complete input and filtered counts. The wrapper also receives `submitted_pages`, so its existing wrong-index counters now check the mapping actually used by the corrected caller; they should remain zero even when `mixed_filtered_batches` is nonzero. The report adds `status_mapping: filtered_pages` to identify that interpretation.

This leaves the base-page path, original syscall arguments, status classification, early-ENOMEM branch, retry limits, and shutdown behavior unchanged. The extra vector allocation and shared-pointer copies add overhead that enters the original adaptive cost timer. It is a correction experiment, not a reconstruction of a proven historical binary.

## What the first ARMS probe establishes

Session `20260909T162119Z-352353` has 1,443 completed calls and zero in-flight calls at its live snapshot. All calls returned zero. No positive returns, syscall ENOMEM, per-address ENOMEM, or early-ENOMEM branches were observed.

Eight mixed filtered batches occurred in the promotion-huge lane. There were 47 positions where the submitted address differed from the original `pages[i]` record. Of these, 46 had valid target-node statuses and one had a negative status. The negative result was EFAULT or ENOENT; aggregate counters cannot distinguish which for this particular position.

The 46 successes establish that valid successful syscall results were associated with the wrong page records by the original mapping. They do **not** establish 46 distinct corrupted pages or 46 changes to previously correct metadata: addresses can repeat, a target record might already say `in_dram=true`, and counters are collected immediately before the caller executes its assignment loop. The original immediate exit can truncate a small tail between counting and assignment.

The one negative mismatch can likewise misidentify the page placed in the fragmentation/retry list. The probe did not record the exact addresses or later pagemap repairs. No actual demotion syscall occurred in this ARMS run: the demotion lane records one empty input batch only.

Whole-run status counts include 8,147 promotion-huge target statuses and 12,993 promotion-base target statuses. Base addresses and huge addresses are different units, and repeated successful statuses are not a count of unique newly migrated pages. Summed syscall wall time overlaps across workers and is not application stall time.

No equivalent instrumentation exists for May. This experiment can measure the bug's current effect if compiled and run, but cannot establish that it caused the historical performance transition.
