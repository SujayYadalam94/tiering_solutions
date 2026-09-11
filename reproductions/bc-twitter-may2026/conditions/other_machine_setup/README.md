# Setup supplied from another machine

The user supplied a setup script from another machine associated with March runs that also showed an ARMS/model gap. Static comparison only: no script execution, benchmark, or host setting change.

After converting the pasted CRLF line endings to LF, the **entire script is identical** to `setup.sh` at `d92cb822f`, `8d6a8bc66`, `4da3d6c3f`, and `d6f720be6`. These include the recovered May 7, May 14, and June setup states. The matching Git blob is `9269f544c753641578e44d8709ec815d031f7770`. [Exact source path and hashes](comparison.json).

Consequently, the supplied file provides no different lowmem reserve, huge-page, compaction, perf, or C220G5 uncore request to reproduce. In particular it actively sets `lowmem_reserve_ratio="256 256 32"`, `min_free_kbytes=1048576`, `watermark_boost_factor=10000`, khugepaged pages/scan sleep/allocation sleep to `8192/0/1`, and THP enabled/defrag to `always`. Its memeater block is commented out, and its final `bash defrag.sh` is active.

For ordinary ARMS/model runs on C220G5, current setup requests the same final settings through `measurement_settings.sh`. Relevant implementation differences already audited are perf write ordering (temporarily enable throttling before setting the cap), earlier application of VFS cache pressure, stopping setup on common-setting/uncore failures, and an absolute defrag path. These are the existing May-versus-current differences; the supplied script does not introduce a new one. [Existing detailed comparison](../../../../docs/bc_twitter_measurement_settings_may_comparison.md), [pasted versus current setup diff](pasted_vs_current.diff).

The source has platform branches: C220G5 uses CPUs `10-19,30-39` and uncore MSR programming; GSL Optane uses CPUs `16-31,48-63` and skips that programming. Thus an identical script on a different machine does not establish identical effective settings without its platform argument, environment overrides, and successful readbacks. The separate `defrag.sh` and policy binaries from the other machine were not supplied.

The exact matching revision first appears in this repository's retained setup Git history in **April 9, 2026, `d92cb822f`**, which adds the platform-selection code present in the paste. [Commit patch](april09_setup_change.patch). This is a source-provenance clue, not proof that it could not have existed as an uncommitted local version in March. The supplied file by itself does not establish what that machine executed for the March timing records.
