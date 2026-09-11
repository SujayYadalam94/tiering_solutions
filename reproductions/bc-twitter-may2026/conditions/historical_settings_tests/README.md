# Historical settings study analysis

The authorized five-alternative experiment is complete: **14 valid runs**, with
all settings and host restoration checks passing. See [results](results.md) and
the [focused `1 1 1` history follow-up](reserve_history_followup.md).

These tools analyze completed sessions without running workloads or writing host
settings. `study.json` and `run_study.py` belong to the study driver; the analysis
tools do not change them or the existing khugepaged study.

From this directory, after all study sessions have completed:

```sh
python3 analyze.py --study study.json
python3 audit_results.py
python3 verify_restoration.py
```

The first command writes `analysis/summary.{json,csv,md}` and
`analysis/node0_zones.csv`. Study entries named `none` are explicitly selected as
baselines; matching baseline means, ranges, sample standard deviations, and
test-minus-baseline differences are retained. The timing comparison matches
policy, artifact hashes, model configuration, graph device, boot, preparation,
and every captured control except the four deliberately varied target paths.

Alternatively pass completed session directories positionally and repeat
`--baseline-session PATH` for each desired baseline. `--output-dir PATH` chooses
a separate analysis directory. A study with unassigned sessions is rejected.

`audit_results.py` writes `results_audit.json` and fails if assigned runs are
missing, identities differ, the accepted control values differ from the baseline
plus the intended single override, preparation failed, or recorded restoration
failed. It checks the complete five-element lowmem-reserve vector, matching the
runner's whitespace-normalized full-vector equality. It also checks the frozen
May library files and model configuration `[2, 10, 0.5]` against the study.

`verify_restoration.py` defaults to saved snapshots only, making it safe to use
while another experiment is running. After the entire study finishes, optional
`--live` additionally reads current controls, MSRs, IRQs, surviving-process
affinities, boot ID, and active benchmark names. MSR reads may require root:

```sh
sudo python3 verify_restoration.py --live
```

All three tools accept explicit `--study` paths. The auditor also accepts
`--summary` and `--output`; the restoration checker accepts `--output`.

The analysis retains requested and accepted ratio, watermark boost, minimum-free
KiB, and khugepaged pages-to-scan values before and after each run. It also keeps
node0 zone free/min/low/high/boost/managed/present page counts and protection
vectors, allowing their observed changes to be inspected. These are native page
counts, not MiB. Zone projections are intentionally not required to match across
conditions; physical fragmentation is not proven identical. VM/khugepaged event
deltas remain system-wide, and PID status generally lacks AnonHugePages.
Allocation stalls (`allocstall_movable`), direct-reclaim scan/steal counts
(`pgscan_direct`, `pgsteal_direct`), and THP migration success/failure deltas are
included. A decrease in `compact_fail` alone does not establish less memory
pressure; inspect these counters together with CPU seconds and runtime.

The timing/resource parser is reused read-only from
`../khugepaged_tests/analyze.py`; its hash is stored in each summary. No code in
that study is modified.

Offline validation used completed baseline sessions
`20260909T170421Z-382094` and `20260909T173344Z-414920` (three runs). Analysis,
identity/control audit, and saved restoration checks all passed; artifacts are
under `validation/`. Validation did not launch workloads or read current MSRs.
