# Focused follow-up: was the reserve ratio 1 1 1?

Read-only audit, September 9, 2026. No workload, host setting, running-study file,
or shared runner was changed.

**Yes: there is real evidence that `1 1 1` was requested during early standalone
ARMS experiments. There is no retained evidence placing it in the May cost
ablation or showing a reserve-ratio difference between that sweep and the normal
May timing runs.** The committed May value is `256 256 32`, not `256 32 32`.

## Strongest shell evidence: write, then direct ARMS

The file `/users/zimooo2/.bash_history-09434.tmp` contains this sequence. Line
numbers are original history lines; intervening lines are read-only inspection,
directory navigation, or omitted here explicitly:

```text
390 bash run_all_measurements.sh
391 make clean ; make -j ;bash run_all_measurements.sh
392 make clean ; make -j ; bash run_all_measurements.sh
393 cat /sys/devices/system/node/node0/meminfo
394 sudo sysctl -w vm.lowmem_reserve_ratio="1 1 1"
...
404 cd tiering_solutions/times/
406 cd hybridtier/
408 cd pr-kron.sg/
410 bash measurement_arms.sh 6 1
411 cd ../../..
413 bash measurement_arms.sh 6 1
414 make clean ; make -j
415 bash setup.sh
416 bash setup.sh 1
417 bash setup.sh ; bash measurement_arms.sh 7 1
```

There is **no setup or defrag between lines 394 and 413**. The first ARMS attempt
at line 410 may have failed because its recorded working directory was inside
the timing tree; line 413 follows the return by three directory levels. Neither
launch has a saved exit status or accepted sysctl readback. This is therefore a
concrete `1 1 1` → standalone-ARMS command sequence, not proof of a completed
BC Twitter run at that value.

Line 416 is the next well-formed setup invocation. Bare `bash setup.sh` at lines
415/417 omits the required size argument; the retained setup returns usage/error
before its settings writes. A command sequence using `;` can still attempt ARMS
after that failure. The value written by a successful March setup at line 416
depends on its then-current local editor version; it cannot simply be assumed
to have been the later May `256 256 32` version.

The file was saved **March 26 05:28:10 UTC**, but commands have no timestamps.
Its line 212 `checkpoint` matches Git `6822f5e08` at **March 11 02:15:42 UTC**;
lines 536/538 match `e6223c95a` at **March 15 00:39:26 UTC**. Saved order is
consistent with March 11–15, subject to shell-history merging. A reboot is
recorded at line 427. These details do not place this sequence in May.

## Active editor writes, versus commented Git examples

There are **18 retained setup editor saves with an active**
`sudo sysctl -w vm.lowmem_reserve_ratio="1 1 1"` at line 43:

- Earliest: `30Tm.sh`, **March 13 19:58:00.634 UTC**.
- Repeated toggling during March 13, including `sDdJ.sh` at **20:25:55.629 UTC**.
- Latest: `oQdm.sh`, **March 14 01:54:24.017 UTC**.
- Next saves switch to `4 4 4`, then `8 8 8`; `ITo5.sh` writes `256 256 32`
  at **March 14 03:25:57.589 UTC**. All later retained setup saves through
  March 22 use `256 256 32`.

The original saves are under
`/users/zimooo2/.vscode-server/data/User/History/-247d494f/`; exact timestamps,
hashes, and active lines are already preserved in
`../arms_version_provenance/hugepage_settings_history/history/setup_editor_settings.json`.
Editor contents show a real local alternative, but not that a particular saved
file was executed.

The older committed line was different evidence:

```sh
#sudo sysctl -w vm.lowmem_reserve_ratio="1 1 1"
```

It is commented out in January 19 `7f73ad25b:setup.sh:14` and March 9
`6b2f5ddf7:setup.sh:43`; those commits do not actively set the ratio to 1.
March 14 `d87369bce` replaces that comment with the active `256 256 32` write.

## Cost ablation versus normal timing cohorts

| Cohort | Retained preparation evidence | Ratio that successful preparation requests |
|---|---|---|
| May 4 standard BC Twitter, 4008 | Checkout `6b1bc11dd`; outer runner calls setup before ARMS at lines 76–77; model runner prepares each configuration | `setup.sh:88`: `256 256 32` |
| May 7 cost ablation, 4009 | Checkout `8d6a8bc66` has the same setup line. Contemporaneous model save **May 7 06:09:11 UTC**, `may07_cost_model.sh:101`, calls full setup before every cost variant | `256 256 32` before ARMS and each model cost when setup succeeds |
| May 14 long timings, 4035 | `4da3d6c3f:setup.sh:88`; saved outer runner **May 14 07:44:28 UTC** explicitly prepares ARMS at lines 84–85 after model's own prepared run | `256 256 32` |
| June 14/22 long timings, 4040/4041 | Same setup blob and saved long-runner preparation logic | `256 256 32` |

The common helper explicitly invokes `sudo bash <absolute>/setup.sh size default
platform` (`may04_git/measurement_common.sh:290–302` in the preserved call-path
audit). Thus a pre-existing manual `1 1 1` would be overwritten in a successful
batch preparation. Standalone ARMS has no internal setup and can inherit it.
Historical scripts do not retain successful sysctl readbacks, so this does not
prove each old write succeeded or exclude an unrecorded local edit/failure.

The original May 7 `c220g5` timestamps place ARMS immediately before ascending
model costs, with **43.200–44.009-second inferred preparation gaps**. That is
consistent with per-case preparation, though it does not prove the setting
writes succeeded. The `c220g5_final` copies were made May 11 and are not new
runs. Details and original-copy hashes are in
`../arms_version_provenance/timeline_findings.md` and
`../arms_version_provenance/verified_bc_twitter_original_timeline.json`.

The two later retained user histories (`.bash_history-05835.tmp`, saved May 11,
and current `.bash_history`) contain **no lowmem-reserve writes**. None of the
three histories explicitly names a cost-ablation launch; the cost sweep used
the ordinary model-runner filename with locally changed cost settings. Its
contemporaneous editor save and original timing order are consequently stronger
evidence for the May sweep than the shell-history filenames alone.

The exact `256 32 32` spelling was not found in the three histories, retained
setup editor settings, or relevant setup/measurement Git changes. The supported
current/May baseline is `256 256 32`; on this kernel the readback also includes
two trailing zeros. The shell `1 1 1` write itself has no saved readback of those
additional entries.

The surviving evidence makes `1 1 1` a historically motivated setting to test,
but currently supports it for March manual experiments rather than as the
identified explanation for the May cost-ablation/normal-run relationship.
