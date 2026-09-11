# Do final timings survive from the March reserve experiments?

Checked September 10, 2026. No benchmark or setting was changed.

**March-era results survive in c220g5_final, but none recovered here can be
identified as a completed run using the manual 4 4 4 or saved 8 8 8 settings.**

## Final files with March Git provenance

The 1,887 current final `.time` files were inventoried and hashed as Git blobs.
58 files, matching both filename/workload and full contents, are present in
March 15 commit `fedcde1ee` (23:05:27 UTC): 29 under final `arms_6.2` and 29 under
final `hybridtier`. Their final-copy mtimes begin in May and are not run dates.
The same current result identities do not occur in the three checked earlier
trees (`6822f5e08`, `d87369bce`, `e6223c95a`). First presence in a checked Git tree
is an existence bound, not an exact execution timestamp or actual kernel record.

| Final group / workload / 4000MiB label | Three real times (s) | Mean (s) |
| --- | --- | ---: |
| arms_6.2 / bc-twitter.sg | 159.639, 155.672, 157.132 | 157.481 |
| hybridtier / bc-twitter.sg | 162.292, 163.473, 166.708 | 164.158 |
| arms_6.2 / bc-kron.sg | 332.362, 332.997, 341.066 | 335.475 |
| hybridtier / bc-kron.sg | 353.184, 361.813, 354.805 | 356.601 |

[All 58 exact Git-to-final matches](march15_final_matches.json).

That committed batch runner calls setup before each policy, and its setup requests
256 256 32. The recorded manual 4 4 4 command occurs later in the shell history
than this commit. Shell-history merging and missing per-command timestamps limit
exact ordering claims; neither this sequence nor the final `.time` text proves an
accepted alternate value for any listed result. These are ARMS/HybridTier totals,
not a verified reserve-ratio ablation or contemporaneous Manta comparison.

## Outputs expected from the explicit manual tests

The history command after 4 4 4 is `bash measurement_arms.sh 10 1`. The historical
runner maps those arguments to `10MiB_run1.time` for each selected workload, and
removes that file before rerunning the same case. The history repeats that label
after both the override and later setup invocations, so even a surviving last
file need not represent the override run.

No `10MiB_run1.time` exists anywhere in today's c220g5_final. Git retains old
versions under the original `times/arms` and renamed `hybridtier_times_4GB` paths;
the complete history of those filenames was checked for byte-identical final
files, with no match. [Recovered unique versions and final matches](manual_10MiB_versions.json),
[raw Git change records](manual_10MiB_git_changes.txt).

The retained BC Twitter/Kron manual-label blobs already exist in March 14
`d87369bce` and contain only 0.445 s and 0.396 s respectively. They have no exit
status, trial output, or setting readback and are not credible completed versions
of these graph benchmarks. They also predate the recorded later 4 4 4 sequence.
The 10MiB name is an argument label, not evidence of physically available memory.

The precisely dated 8 8 8 evidence consists of March 14 editor saves, last at
03:24:23 UTC, followed by restoration to 256 256 32 at 03:25:57. There is no
identified completed final result attached to those saves. [Reserve history](../historical_settings_tests/reserve_history_followup.md).

Conclusion: the final archive preserves March performance, but it does not
provide a known-lowmem-4/8 performance target from these records.
