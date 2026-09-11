# What retained runtime logs can establish about historical ARMS

Read-only audit on September 9, 2026. This audit searched original `times/c220g5` results, the live `logs` and empty `logs2` directories, `collected_logs`, and the already recovered hidden-under-mount logs. It did not mount anything, launch a benchmark, or change host settings. Final archive modification times are not used as execution dates.

## May startup output is not retained in these logs

The search covered 4,537 original `.time` files and 1,346 text logs under `times/c220g5`, 135 live text logs, 87 collected text logs plus 123 collected timing files, and 27 previously recovered underlay logs. All selected files were readable. There were no matches for ARMS startup signatures (`USE_MODEL`, `ARMS_VERBOSE`, `BACKOFF_PERIOD`), ARMS library paths, or perf mmap messages in original c220g5 or collected logs. The perf mmap matches in the live/underlay logs belong to **HybridTier**, not ARMS.

The detailed inventory, paths, dates, hashes of matching logs, and matched lines are in `retained_startup_log_search.json`. The existing reproduction startup logs are intentionally excluded: they identify the reconstructed libraries used in September, not the executable used in May.

This absence agrees with the historical capture implementation. In Git revision `8d6a8bc66`, `measurement_common.sh:188–224` runs the application with `2>&1` to the terminal and redirects the surrounding shell's `time` output into the `.time` file. It does not tee terminal output into the path named by `LOG_OUTPUT_PATH`. That environment variable is an internal policy/training-log destination (see `logging.cpp:181–186`), not a shell stdout capture. Thus the retained `.time` records cannot retrospectively establish the compiled buffer size or verbose flag.

The saved May 11 shell-history file contains many tmux invocations, but no matching command to capture the benchmark terminal through `tee`, `script`, `nohup`, tmux `capture-pane`, or `pipe-pane`. It also contains repeated clean builds followed by `run_all_measurements.sh`. Command timestamps and exit statuses are absent, so the history cannot assign an exact build to a particular May 4/7 execution.

## Later launch records identify the normal library path

`retained_bc_twitter_arms_launches.json` preserves 35 previously extracted auth-log records for normal BC Twitter ARMS launches between August 18 and September 9. Every selected record explicitly loads:

```
/users/zimooo2/tiering_solutions/libraries/C220G5/libhemem-arms.so
```

These records establish the actual launcher path and workload command for the later executions. They do **not** include a binary hash, compile flags, or perf mmap output. The final records also include interrupted/overwritten attempts, so launch records must be matched with completion/timing evidence before comparing performance.

The retained auth/syslog rotations reach August, not May; previously extracted journal boots reach September. The wtmp boot ledger reaches May but records kernel boots rather than library builds. No direct May ARMS startup/version evidence was recovered here.

## Consequence for assigning a version

The May 4/7 original result dates can be correlated with Git and dated editor snapshots, but assigning a precise executable remains an inference. No retained startup output found in this audit contradicts the closest saved May configuration, and no runtime log proves an older/stale binary was loaded. A claimed exact May ARMS hash, perf ring size, verbose state, or backoff value would exceed this runtime evidence.
