# C++ and build follow-up: slow May long runs to fast June long runs

Read-only investigation on September 9, 2026. No compilation, benchmark, live source change, or host setting change was performed. This directory preserves editor evidence and excerpts from existing binaries.

## Main finding

The closest retained **local** ARMS source/build saves agree with the identical committed source across the May 14 (`4da3d6c3f`) to June 13/14 (`d6f720be6`) interval. A real uncommitted **model feature adapter** change occurred immediately before the first June cohort, but it cannot directly account for ARMS becoming faster. No new historical ARMS reversion is established by this audit.

The inventory covers 597 retained snapshots for 24 C++/header/build/normal preparation files in `/users/zimooo2/.vscode-server/data/User/History`. It includes all retained dates, not just the copies originally collected through May 11. The only save in this scope between May 14 and June 14 is `model.cpp` on June 13. [Inventory and hashes](editor_inventory.json).

| File | Closest retained save before May 14, UTC | Comparison with May 14 Git |
|---|---|---|
| `arms_kernel.cpp` | May 11 02:41:07 | Byte-identical |
| `page.cpp` | May 11 02:14:26 | Byte-identical |
| `defs.h` | May 9 23:29:48 | Byte-identical |
| `Makefile` | May 10 02:11:26 | Byte-identical |
| `page.h` | May 9 22:24:55 | Byte-identical |
| `migration_worker.cpp` | May 9 06:12:55 | Byte-identical |
| `model.cpp` | May 12 23:05:41 | Byte-identical |

Normal measurement helpers and defrag also have matching nearest saves. Older retained saves of some less-edited files predate committed additions, so an old editor snapshot alone must not be treated as the file actually present in May. Copies of the nearest saves and exact Git comparisons are in `nearest_may14/` and `nearest_diffs/`.

The matching Makefile selects C220G5, `-O3 -DNDEBUG`, normal ARMS `USE_MODEL=false`, and the standard `libraries/C220G5/libhemem-arms.so` target. Relevant retained shell histories contain repeated clean builds and no `DEBUG=`, `CXXFLAGS`, `LD_LIBRARY_PATH`, `GLIBC_TUNABLES`, `MALLOC_*`, or ARMS-specific override evidence in the targeted search. These observations strengthen the user's clean-build account; absent per-command timestamps and build-success logs, they do not identify exact executables.

## Concrete June model change and compatibility problem

[June 13 save `GnjX.cpp`](model_editor/GnjX.cpp), at **22:06:44.012 UTC**, changes `MODEL_NUM_FEATURES` from 12 to 11 and comments out the age feature. The later long-runner save selecting size 4040 is June 13 at 22:27:44.262 UTC. This is contemporaneous local evidence for a model change before the fast June cohort even though the `.time` filenames use the same model name/configuration.

The retained `models/no_age/` and `models/with_age/` BC Twitter forests are byte-identical and both require **12 features including age**. Their object SHA-256 is `96148ddca2bbc531768cef66f9012cf91830696b683a82b6d7dbd52cfebb60dc`. Every one of their 96 trees has a split using feature index 11. The compiled object independently confirms a 12-double input: `forest_root` computes the address `input + 0x58` at offset `0x2a5` and unconditionally loads that twelfth double at `0x46d`.

**If that retained forest was linked to the June 13 adapter**, the resulting mapping would be:

| Forest expects | Adapter supplies |
|---|---|
| Index 8: age | read ratio 20 |
| Index 9: read ratio 20 | read ratio 100 |
| Index 10: read ratio 100 | group variance |
| Index 11: group variance | Read beyond the 11-double array |

That is not a valid age ablation and could materially alter predictions and migrations. It is a **conditional compatibility finding**: no saved per-run object hash proves which forest the June 13 executable used. A matching 11-feature forest might have existed and been replaced later.

The feature adapter was restored on **June 18 16:07:52.055 UTC** in [FnPW.cpp](model_editor/FnPW.cpp), byte-identical to May 14 source. Subsequent edits end at 16:10:22.524 in [Fpit.cpp](model_editor/Fpit.cpp), differing only by comments. Thus the June 22/23 fast cohort has a surviving restoration before it. An 11-second intermediate save `Y9D9.cpp` has 11 declared slots but 12 writes; it is an incomplete edit, not evidence a runnable benchmark used it.

[Model provenance JSON](model_feature_provenance.json) records all May/June adapter saves, retained forest hashes, feature names, and tree counts. `model_editor/*.diff` gives exact comparisons with May 14.

### Why this does not directly change normal ARMS

The changed feature count is local to `model.cpp`; no header or `page_info`/`data_row` layout changes. Its only feature buffer and call to `extract_features()` are inside `#if VIRTUAL_FEATURES_ENABLED`, false for normal nontraining ARMS. No added global initializer or persistent array is involved. Normal ARMS's policy also returns from `update_model_scores_and_log()` before model inference. Therefore this change does not provide a direct mechanism for the approximately 180-second ARMS long-workload speedup. No rebuild was performed to assert byte-identical object output.

## Additional real sampling bugs, not evidence of a historical transition

1. **Bias pointer comparison crosses translation units incorrectly.** `defs.h` declares `hist_bias` and `recn_bias` as `static const` arrays, creating separate arrays in each translation unit. `arms_kernel.cpp` assigns `active_bias` to its arrays, while `policy_thread.cpp` compares that pointer against its own arrays. Existing May 4 library disassembly confirms distinct addresses: kernel recency array `0x42c20` and policy recency array `0x435c0`; the policy compares the latter. Normal frequency-switch conditions therefore cannot match the active arrays in this build. The retained April 30 artifact has the same duplicated arrays and comparisons.
2. **The frequency updater reads the old mode.** The caller invokes `change_sampling_frequency()` before setting `sampling_mode`; the callee reads `sampling_mode` to select the period. Fixing pointer identity alone would expose this second issue and select the previous period during each transition.
3. **IMC feedback does not distinguish the two tiers as written.** Both values of the tier loop open the same event type/configuration on the first CPU in each IMC's cpumask. Raw CAS deltas are then labeled GB/s without applying the event's byte scale or measured elapsed time. The reconstructed policy therefore does not implement the intended tier-specific bandwidth feedback. This behavior predates the investigated interval.

These are concrete correctness targets for a separate diagnostic, not proposed reversions that restore a verified May setting. Neither source history nor the retained old binary establishes that these bugs changed between May and June. A search of all 33 retained reproduction `stdout.log` files found **zero phase-change messages and zero sample-period ioctl error lines**; it does not reveal a current phase-transition effect capable of explaining the historical gap. [Search results](reproduction_hcd_log_search.json), [May 4 binary evidence](may04_rebuilt_sampling_evidence.txt), [April 30 binary evidence](retained_april30_sampling_evidence.txt).

Save history is incomplete: Git checkouts, script/Vim edits, overwritten binaries, and unrecorded build flags need not appear here. The evidence narrows the possibilities but does not identify the cause of ARMS's speedup.
