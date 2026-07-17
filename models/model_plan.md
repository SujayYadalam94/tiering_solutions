# Plan: Fix Split + Add Early Stopping

## TL;DR
Fix the split in all_models_v3.py so train, validation, and test are page-disjoint while still keeping the train portion near the current size, add early stopping with a real validation set, keep the final model trained on all data, and simplify the report to the two R^2 values you actually care about.

## Context
- Current split takes the first 20 percent of rows and randomly keeps half of that for training, so the effective train set is about 10 percent of rows and pages leak between train and test.
- The small BO pass currently trains on about 100k rows out of the 500k sampled subset. The plan must not increase that small-run training size.
- The final model should still be retrained on all data after model selection, even though that means the final-model test R^2 is not leakage-free.

## Steps

### Phase 1: Replace the split with a row-balanced grouped split

1. After feature engineering, compute per-page row counts from `local_train_df.groupby("page").size()`.
2. Shuffle pages deterministically with `np.random.RandomState(42)`.
3. Assign whole pages to train, validation, and test with row-count targets of about 10 percent, 10 percent, and 80 percent of total rows.
4. Use a simple greedy fill against row targets rather than splitting by raw page count, so uneven page lengths do not silently blow up the train or validation sizes.
5. Build boolean masks from the chosen page sets and derive `X_train`, `X_val`, `X_test`, `y_*`, and `W_*`.
6. Add explicit overlap assertions on the page sets and print both row counts and unique-page counts for each partition.

### Phase 2: Rebuild the datasets around train and validation

7. Build `data` from train rows only.
8. Build `data_val` from validation rows only, with `reference=data`.
9. Keep `data_full` on all rows for the final retrain.
10. Replace the current small-subset positional split with grouped sampling from the already-separated partitions:
11. Sample up to about 100k train rows from train pages for `data_small` so the fast BO training size stays at the current scale.
12. Sample a separate validation subset from validation pages for `data_small_val`; keep it moderate, for example 100k, since this affects validation cost rather than training-set growth.
13. Do not sample any small-set rows from test pages.

### Phase 3: Add early stopping without changing the iteration cap

14. Keep the existing `num_iterations` search bounds and structural budget caps unchanged.
15. Add `EARLY_STOPPING_ROUNDS = 20`.
16. In the SMAC fast trials, train on `data_small` and early-stop on `data_small_val`.
17. In finalist evaluation, train on `data` and early-stop on `data_val`.
18. Capture `best_iteration` from each finalist and keep the winning finalist model plus its honest test-set R^2 before the all-data retrain.
19. In extra finalist rounds, use the same train-plus-validation early-stopping flow.
20. For the final model, retrain on `data_full` with `best_params` and `num_iterations` fixed to the winning finalist's `best_iteration`.

### Phase 4: Simplify the report to the two R^2 values

21. After finalist selection, compute and store the honest R^2 from the winning finalist model on `X_test`.
22. After retraining on all data, compute the final-model R^2 on `X_test` as requested, while clearly labeling it as post-retrain and therefore not leakage-free.
23. Update console output and the report file to include just these two metrics:
24. Honest eval R^2: winning finalist, trained without test rows.
25. Final model R^2: all-data retrained model, evaluated on the old test mask.
26. Remove the current per-source metrics and extra RMSE, MAE, and MAPE sections from the report if the goal is to keep the report focused on those two R^2 numbers only.

## Relevant files
- `/home/freischuetz/memory_tiering/tiering_models/process_data/all_models_v3.py` — split logic, dataset construction, early stopping, finalist tracking, final retraining, and report generation

## Verification
1. Assert no page overlap between train, validation, and test.
2. Print row-count ratios and confirm train remains close to the current 10 percent target.
3. Print the sampled small-train size and confirm it remains about 100k or lower.
4. Verify early stopping fires for at least some trials by checking `best_iteration < num_iterations`.
5. Verify the report contains exactly the two requested R^2 metrics with clear labels.

## Decisions
- Use page-grouped splitting because page leakage is the core evaluation problem.
- Make the grouped split row-balanced, not page-count-balanced, so the train-size constraint still holds when page sizes vary.
- Keep the iteration cap unchanged, per user request.
- Keep the final retrain on all data, per user request.
- Keep adjusted EWMAs; no EWMA change is part of this plan.
- Report only the pre-retrain honest R^2 and the final all-data-model R^2.