# Cross-workload historical cohort audit

This audit broadens the earlier BC-focused timing search to PR Twitter, PR Kron and the other long workloads. No production source, host setting, boot, or benchmark was changed. The captured inventory contains 3,023 current C220G5 ARMS/model timing files; it preserves content hashes, original-file mtimes and inferred boot joins. Copied/archive directories are not used to date executions in the comparisons below. The inventory itself includes some copies for completeness and must not be interpreted as 3,023 independently dated executions.

## Findings

1. **The BC Twitter May→June gap collapse is not universal across long graph workloads.** PR Twitter already favors ARMS in May. Between May and June the PR model improves more than ARMS, the opposite relative response from BC. This weakens a single uniform ARMS overhead, compiler or machine-wide speed change as a sufficient explanation, while remaining compatible with workload-sensitive migration/THP effects.
2. **No missing PR timing cohort tightens the May14→June13 BC boundary.** Reading all reachable Git PR timing history adds 28 historical blobs absent from today's files, but none provides a C220G5 4 GB run in the missing interval. The latest newly recovered relevant C220G5 results were committed May 3 local / May 4 UTC, preceding the already known slow cohorts.
3. **The second June cohort matters.** Restoring the model age feature before June22 does not restore the BC Twitter gap. Other models do improve between June cohorts (notably XSBench), demonstrating why the first June model should not be treated as exactly identical to May.
4. **Successful wrapper exit status is not by itself proof that the workload executed.** June4040 FAISS-long has six exit0 files lasting only 0.22–0.35 seconds; these are excluded from performance evidence. June4041 FAISS runs last approximately 1,200 seconds.

## Matched long cohorts

Times are means of three exit0 files per policy. Positive gap means ARMS is slower. Nominal labels identify experiment cohorts rather than exact measured physical capacity.

| Workload and trials | Dates / label | ARMS seconds | Model seconds | ARMS − model |
| --- | --- | ---: | ---: | ---: |
| BC Twitter,100 | May12 /4032 | 711.096 | 632.746 | +78.350 |
| BC Twitter,100 | May14 /4035 | 808.557 | 744.401 | +64.156 |
| BC Twitter,100 | June13–14 /4040 | 625.315 | 614.238 | +11.077 |
| BC Twitter,100 | June22–23 /4041 | 628.544 | 619.911 | +8.633 |
| PR Twitter,100 | May13 /4032 | 2713.627 | 2897.917 | −184.290 |
| PR Twitter,100 | June14 /4040 | 2587.799 | 2692.481 | −104.682 |
| PR Twitter,100 | June22–23 /4041 | 2559.892 | 2672.156 | −112.265 |
| BC Kron,50 | June13–14 /4040 | 889.448 | 911.079 | −21.631 |
| BC Kron,50 | June22–23 /4041 | 886.616 | 885.864 | +0.752 |
| PR Kron,50 | June13–14 /4040 | 1297.312 | 1302.794 | −5.482 |
| PR Kron,50 | June22–23 /4041 | 1285.740 | 1297.762 | −12.022 |

All these timestamps join to 6.18.1-061801-generic boot records. Specifically, BC4032 uses May12 17:06:17 boot; PR4032 uses a different May13 01:08:19 boot despite sharing the label. May4035 BC and PR join to May14 07:30:49 boot. June4040 joins to June13 22:10:24, and June4041 to June22 03:55:37. Inference uses original mtimes plus retained boot ledger, not a kernel string inside each timing file.

Historical `workloads/pr-twitter.sg-long.sh` is byte-identical at May14 HEAD4da3d6c3f and June13 HEADd6f720be6: OMP_NUM_THREADS16, `pr -n 100`, the same named `gapbs/benchmark/graphs/twitter.sg` path, and virtual sample step10000. BC Twitter long is also100; BC/PR Kron long are50. Total time divided by trials is **not** a recovered iteration time because it includes graph loading and initialization. Do not combine long totals with the 10-trial series.

For all matched long model files the filename selects workload-specific `model_discounted_reward_99_<workload>_l2-1_10_0.1`. This does not prove identical loaded forest binaries. The June13 adapter edit and June18 restoration are documented in the parent change-point audit. Graph content and physical storage hashes are unavailable historically.

May12→June4040 BC: ARMS improves85.781s(12.06%), model18.508s(2.93%). May13→June4040 PR: ARMS improves125.828s(4.64%), model205.436s(7.09%). Both policies also improve substantially from the unusually slow May14 BC cohort to June; describing every absolute improvement as exclusive to ARMS would be inaccurate.

May15 also contains ARMS-only PR Twitter4035 runs2849.807/2851.287/2850.181s, mean2850.425. Without a corresponding model pair these cannot establish another gap transition, though they show slow absolute PR time under the same May14 boot as slow BC.

## Same-boot PR Twitter detail

May13 model and ARMS alternate, with inferred45.0–47.5seconds between executions:

| Repetition | Model seconds | ARMS seconds |
| --- | ---: | ---: |
|1|2820.302|2711.700|
|2|2909.202|2719.283|
|3|2964.246|2709.897|

The model slows across this small sequence while ARMS remains stable. This is actual within-boot, policy-specific timing variation rather than evidence that only between-boot memory conditions can affect performance. It does not identify its cause: model binary/read/trial/allocator logs are missing. The45–48second intervals are compatible with ordinary per-policy preparation and do not suggest a skipped preparation call. See [chronology](may13_pr_twitter_chronology.json).

## Other June controls

| Workload | June4040 ARMS / model | June4041 ARMS / model |
| --- | ---: | ---: |
| XSBench-long |1385.500 /1422.576|1394.950 /1350.762|
| DuckDB-long |743.070 /739.749|770.897 /734.002|

XSBench's model improves71.814s while ARMS becomes9.450s slower. BC Twitter model becomes5.673s slower while ARMS becomes3.229s slower. Therefore the unchanged small BC gap after age restoration is a useful discriminator: a temporary bad June model is not a sufficient explanation for the Twitter observation.

The June suite's invalid FAISS executions are also a real difference from June22. They occur **after the first BC/PR graph runs** in4040, so cannot explain the initial small BC gap. They change the prelude to later repetitions, but the small gap persists in June4041 with full-duration FAISS.

## Extended recovery scope

Command used for the additional PR audit:

```bash
git log --all --raw --no-abbrev --no-renames --format='COMMIT %H %cI' -- 'times/**/*pr-twitter*/*.time' 'times/**/*pr-kron*/*.time'
```

Filtered original nominal3900–4999 ARMS/model paths yielded824 old/new blob references,537 distinct paths and275 distinct blobs. Each blob was compared to Git blob hashes computed from every current `.time` file;28 are absent. Each missing blob was read via `git cat-file blob` and its total/exit status preserved. Earlier dates are commit existence bounds, not recovered execution times. Some entries are failed, training, or GSL/Optane and are excluded from matched C220G5 comparisons.

Relevant latest missing May3local/May4UTC PR ARMS results: Kron317.491/317.462s, Twitter319.718s; matching successful model cost variants generally303–321s. No newly recovered run bridges May14→June13.

## Evidence files and reproducibility

- [Captured current timing inventory](all_arms_model_rows.json): raw paths, hashes, status, dates, boot join.
- [Matched pairs with all source rows](matched_cross_workload_pairs.json).
- [Summary program](summarize_cohorts.py), reads captured inventory and regenerates paired table.
- [All nominal4GB long summaries](all_4gb_long_summaries.json), including explicitly invalid FAISS rows for transparency.
- [PR Git raw history](pr_git_changes.raw), [filtered references](pr_git_references.json), [missing historical blobs and exact contents](pr_git_missing_blobs.json).
- [Earlier BC-specific boundary/source audit](../../change_point/README.md).

The timing files do not contain historical graph read/iteration splits or meaningful attributable migration counters. No exact causal code or VM setting follows from this cohort evidence alone. The strongest resulting constraint is that a proposed cause must explain **workload-dependent relative effects**, including PR's preexisting long-run ARMS advantage, and the persistence of the small BC gap after June's temporary model edit was reverted.
