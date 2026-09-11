# Bash history and huge-page setup differences

September 9, 2026. This follow-up examines launch paths and effective-setting provenance for the slow May ARMS versus model comparison. No benchmark or host-setting change was performed. Historical scripts were evaluated with privileged commands replaced by recording functions; nested defrag and process enumeration were also intercepted.

**Finding:** the surviving May-and-later default scripts request identical final VM/THP controls for both policies. There is a real standalone-launch asymmetry, and earlier shell history contains manual THP experiments, but neither currently establishes a settings mismatch in the archived May cost-ablation runs.

The subsequently authorized [three khugepaged experiments](../../khugepaged_tests/results.md) are now complete. Increasing scan sleep produced a repeatable ARMS computation improvement and a return toward baseline after restoring zero sleep. These establish current setting sensitivity without supplying a dated historical change.

The subsequent [remaining-settings investigation](potential_settings/README.md)
ranks historical reserve-ratio, watermark-boost, minimum-free, and scan-batch
alternatives. In particular, the recorded `4 4 4` reserve ratio would increase
one node-0 allocation protection threshold by about 736 MiB in the current boot.
This is a calculated mechanism to test, not evidence that May used that value.

## Default setup values are unchanged across the relevant revisions

The recording comparison covers May `8d6a8bc66`, May 14 `4da3d6c3f`, June `d6f720be6`, July `2caec5ede`, September `671e0e644`, and the current working scripts. All six request the same 23 distinct VM/THP/NUMA/perf controls for `default`, C220G5, size label 4009. Each setup calls defrag once internally. Earlier March snapshots establish when the aggressive settings appeared.

| Setting | May through current default request |
|---|---:|
| THP enabled / fault defrag | `always` / `always` |
| shmem huge pages | `force` |
| khugepaged defrag | `1` |
| Compaction proactiveness | `80` |
| khugepaged pages per scan | `8192` |
| khugepaged scan / allocation-failure sleep | `0` / `1` ms |
| Minimum free memory | `1048576` KiB, distributed across zones |
| Watermark scale / boost factors | `10` / `10000` |
| Zone reclaim / automatic NUMA balancing / generic demotion | `0` / `0` / `0` |
| VFS cache pressure | `2000` |

The comparison records requested values, not proof that historical writes succeeded. Its full [events and comparison](settings/requested_comparison.json), [source hashes and commit dates](settings/snapshot_manifest.json), and [recording implementation](settings/compare_requested.py) are retained.

The current refactoring changes write order and error handling. It moves VFS cache pressure ahead of global process migration, reorders reserve/MGLRU/KSM writes, and temporarily sets perf CPU percentage to 25 so the sample-rate write can succeed before restoring zero. The old setup often continued after failed writes; the new shared writer returns failure. These are real execution differences even though final requests match. The [ordered write diff](settings/may_to_current_write_order.diff) records them. Replaying the old preparation and order previously left ARMS near its current speed, so the refactoring has not explained the archived 115.804-second result.

## A standalone ARMS launch inherits settings

`measurement_arms.sh` does not prepare the host itself. Normal `measurement_model.sh` runs the shared setup before each model configuration. Consequently a manual setting followed directly by ARMS can survive into that run; the equivalent direct model launch normally overwrites it.

The archived normal/cost-ablation runner explicitly prepares ARMS as well. Each model configuration prepares itself. Both pass the default measurement system, which rules out accidentally selecting the MEMTIS or NOMAD setup branches through these call sites. The long-runner saves from May, June 13, and June 22 retain the same preparation order. This launch asymmetry is therefore relevant to manual experiments, not evidence of a missing preparation in those archived sweeps.

Two other concrete execution differences were checked. The historical inner defrag used a relative filename, so an external working directory could omit that pass; the outer defrag used an absolute path and would still run. Current setup uses an absolute path for both. Also, the current common helper returns early on setup failure, but the ordinary ARMS/model callers do not check that return, so an unsuccessful preparation can still be followed by a measurement. No such failure was found in the accepted diagnostic runs. The [call-path audit](call_paths/README.md) preserves exact scripts, line references, and chronology.

## The shell history does contain manual experiments, bounded to March

The history snapshot saved March 26 contains manual requests for compaction proactiveness 50, a helper invocation requesting 20, scan sleeps 100, 1000, and 10 ms, and several low-memory reserve ratios. Some direct ARMS launches immediately follow those overrides without full setup. One sequence places a 10 ms scan-sleep write immediately before `measurement_arms.sh`, with setup only afterward. These were plausible ways to run ARMS under different effective conditions.

These are not evidence of May overrides. The principal experiment block lies between shell commit-message anchors matching March 11 and March 15 commits. A direct training-model then training-ARMS sequence in the history saved May 11 is likewise between March 24 and March 28 commit anchors; the history file's save date must not be assigned to every command it contains. Commands have no individual timestamps, and shell-history merge ordering remains a limitation. The May 11-saved and current retained histories contain no corresponding manual THP/compaction writes in the searched records.

A March 13 editor snapshot also tried setting all three khugepaged `max_ptes_*` knobs to 1024, then removed those writes four minutes later. The checked kernels reject that value as out of range, so it is not evidence of a persistent accepted override. A March GRUB backup has no explicit THP/compaction-related argument. These findings narrow the historical candidates rather than supplying a May-to-June changed setting.

The [shell-history audit](history/findings.md) retains the ordered command excerpts, commit-date anchors, source hashes, editor saves, and filtering/dating limits.

## Why editing setup can fail to change a THP experiment

The final `defrag.sh` explicitly writes both top-level THP enablement and THP defrag back to `always`. The shared preparation normally calls it inside setup and then again afterward. Setting either to another value earlier in setup will be overwritten before the workload. To test such a setting, the override and readback must follow the final defrag.

Other knobs, including scan sleep and proactiveness, are not reset by defrag itself, but the next full setup resets them. This distinction explains why early standalone ARMS experiments could retain a manual scan-sleep setting even after defrag, while model preparation resets it.

## The measured policy sensitivity is real, but its historical trigger is missing

The prior controlled scan-sleep experiment is more informative than simply labeling all observed compaction as background work: changing only scan sleep from 0 to 10 ms improved ARMS computation by about 2.5–3.6 seconds over ten trials, with little model change, and a partial reversal when restored. Reducing proactiveness from 80 to 20 had a much smaller timing effect. Both original and current default setups request 0 ms and 80, so these results establish different policy sensitivity without establishing a historical setting change. [Existing knob experiments and readbacks](../../../../../docs/bc_twitter_setup_knobs.md).

`compact_fail` counts allocation-triggered direct compaction failures; it is not a direct measure of time spent by background kcompactd. THP migration can request synchronous destination allocation/compaction independently of top-level THP fault defrag. Khugepaged can also trigger allocation work and influence the page layout that migration encounters. Thus identical settings can produce different work under ARMS and the model. The previous global counters do not attribute every event to one of these paths.

The remaining unset controls currently show `vm.defrag_mode=0`, `extfrag_threshold=500`, `max_ptes_none=511`, and `shrink_underused=1`. No matching persistent-setting entry was found in the readable local sysctl, tmpfiles, systemd-system, or rc.local configuration. Current readbacks do not establish May values. [Current configuration scan](settings/current_persistent_config.json).

Exact Linux 6.18 source also weakens two potential inherited-state explanations. Successful minimum-free or watermark-scale writes clear the existing per-zone watermark boost. With the currently observed `max_ptes_none=511`, zero-content underused detection is disabled even though `shrink_underused=1`; partially mapped splitting and migration fallback remain possible. These are not reasons to ascribe the migration splits to khugepaged alone. The [kernel mechanism audit](kernel_mechanisms.md) supplies the source paths, additional untouched controls, and attribution limits.

The audit supports keeping both launch paths explicitly prepared and checking settings after the final defrag when making comparisons. It does not identify an ARMS-only historical compaction setting that should be restored to reproduce the old results.
