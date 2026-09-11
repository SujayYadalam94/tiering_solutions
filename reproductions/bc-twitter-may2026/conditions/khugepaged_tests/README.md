# Khugepaged condition analysis

`analyze.py` reads completed condition-session artifacts and writes `summary.json`,
`summary.csv`, and a concise `summary.md` table. It neither runs workloads nor reads
or changes live host settings. Pass only sessions that have `restoration.json`.

From this directory, for example:

```sh
python3 analyze.py ../sessions/BASELINE ../sessions/TEST \
  --baseline-session ../sessions/BASELINE --output-dir analysis
```

Repeat `--baseline-session` to combine multiple explicitly selected baseline
sessions. Every selected baseline must also be in the positional session list.
Comparisons require matching case, library/graph/binary hashes, boot, kernel,
preparation preset, effective perf cap, and PMEM metadata condition. The three
deliberately varied khugepaged controls are excluded from that matching key.
Differences are **test minus the matching baseline mean**; negative time
differences indicate improvement. Runs from a different policy do not substitute
for a missing baseline. The script does not infer a baseline from session order.

The summaries include read and average iteration time, total runtime, aggregate
user/system CPU seconds, starting/ending node0 MemFree, selected global vmstat
counter deltas, optional khugepaged full_scans/pages_collapsed counter snapshots
and deltas, requested/effective defrag/alloc-sleep/scan-sleep controls, and runner
restoration checks. JSON and CSV contain the complete selected fields without
the large affinity snapshots.

`latest_status.txt` ordinarily contains `/proc/PID/status`, which supplies RssAnon
but no AnonHugePages. Missing AnonHugePages stays null; RssAnon is recorded as a
separate metric. Node0/global AnonHugePages from before/after meminfo are included,
but those snapshots do not describe peak huge-page use during the benchmark.
Global event counters also include activity outside the benchmark process.

Validation used only previously completed sessions
`20260909T143041Z-209179` (scan sleep 10 ms) and
`20260909T143646Z-217337` (default controls). The latter contains only ARMS, so
the model row correctly receives no baseline difference. Outputs are retained
under `validation_old_sessions/`. No new workload was started for validation.
# Completed experiments

The three requested khugepaged sensitivity pairs, fresh baseline, scan-sleep ARMS repeat, and restored ARMS baseline are complete. See [results and interpretation](results.md), [all recorded timings and counters](analysis/summary.json), and [verified host restoration](final_restoration_check.json).
