# Historical ARMS variants and the clean-build constraint

Investigated September 9, 2026. The user's repeated `make clean; make -j` practice substantially weakens the stale-object hypothesis. The May 11-dated shell-history backup independently contains many such commands. It does not contain individual command timestamps or success output, so it cannot prove a particular benchmark's build identity.

## What a successful clean build excludes

The nearest retained pre-sweep [Makefile](editor_history/Makefile/4HiW), saved May 7 at 04:10:44 UTC, removes both `build/` and `libraries/` in `clean` (lines 212–213). Its default `all` target includes normal ARMS (line 106). Thus a successful clean rebuild recreates `libraries/C220G5/libhemem-arms.so` from the current inputs.

Without cleaning, its object rules do not track header or compiler-flag changes. ARMS reuses `build/obj/nomodel/C220G5/`, while distinct model/configuration combinations use distinct object directories. That could preserve older ARMS objects alongside newly built model variants. A successful clean rebuild removes those objects, so this mechanism should not be treated as the likely explanation given the user's workflow.

One real exception is repository-root `libarms_kernel.so`: clean does not remove it, and `all` does not invoke its copy target (lines 108–109). The retained April 30 root binary illustrates this. However, `measurement_arms.sh` at Git `8d6a8bc66` explicitly selects `libraries/${MEASUREMENT_LIBRARY_PROFILE_DIR}/libhemem-arms${LIB_SUFFIX}.so`; the normal `run_all_measurements.sh` invocation supplies an empty suffix. The root artifact is not the normal benchmark input. An alternative launcher would need independent evidence before this exception could explain the archive.

The saved Makefile uses `-O3 -DNDEBUG` by default. No targeted shell-history evidence of `DEBUG=` or `CXXFLAGS=` overrides was found. An unoptimized ARMS build is therefore a mechanical possibility, not a supported historical finding.

## Concrete older policy/settings candidates

| Older behavior | Historical evidence | Possible performance effect | Evidence against attributing it to May 7 |
|---|---|---|---|
| ARMS migration backoff 4 instead of 0 | Git `058f63258` changes 4 to 0 on May 2 | Holds migrated pages out of subsequent migration decisions for several update intervals; could delay reaction to changing hot pages | Earliest valid retained defs save, April 26 `bney.h`, already has 0; nearest May 4 save and retained April 30 binary also have 0 |
| Verbose diagnostics enabled | Same Git commit disables it; retained local saves enable it April 27 at 23:44 UTC (`9ghd.h`) and disable it April 29 at 01:41 UTC (`6rn9.h`) | Enables extra counter collection and periodic output, potentially adding repeatable overhead | Nearest May 4 settings and retained April 30 binary have it disabled |
| Smaller ARMS PEBS rings | Same Git commit changes 1,025 to 2,049 OS pages per ring, approximately 4 to 8 MiB with 4 KiB pages | May change sample loss and memory consumption; direction and magnitude need measurement | Larger rings already occur in April 26 local history, nearest May 4 settings, and retained April 30 binary |
| Single-precision migration cost/benefit variables | Git `29e225954` changes `float` to `double`; committed May 3 local time / May 4 UTC. Retained April 30 binary uses the older arithmetic | Could change decisions close to the migration threshold; ARMS's multiplier remains 1.5 | No trace establishes enough threshold changes to explain the missing 12–15 seconds; freshly rebuilt May 4 source already contains the newer arithmetic |

Git commit dates are not first-use dates: local saves show several changes were in the working tree days before they were committed. In particular, calling backoff 4 and the smaller ring simply the "pre-May-2 settings" would overstate their plausibility for the May 7 sweep. They would require an older checkout, an unrecorded reversion, or an older binary surviving despite the normal clean-build workflow.

For the selected active ARMS knobs, retained valid defs saves from April 26 through the last pre-May-7 save keep backoff 0, ring 2,049 pages, sample periods 10,007/5,003, ten workers, and policy intervals 500/100 ms; verbose toggles as described above. Incomplete editor snapshots are not runnable configurations. The [editor-history index](editor_history/index.json) records the original timestamps and hashes. Saved source does not prove which executable ran.

The [April 30 binary audit](legacy_binary/README.md) supports these effective settings through disassembly. Its isolated runtime attempt aborted during initialization, so it supplies no valid comparative timing and cannot be assumed to be the archived executable.

## Interpretation

Alternating runs can preserve a fixed binary/configuration difference, so the cost-ablation consistency would be compatible with that mechanism in principle. But the clean-build practice, standard library path, and closest surviving local settings all weaken the specific stale-ARMS explanation. No inspected artifact establishes that the archived ARMS had one of the older settings above.

These are concrete historical candidates for controlled testing, not established causes. Recreating a coherent older configuration would be more informative than manufacturing mixed stale objects. The original May 7 binary or a per-run build manifest has not been recovered, and the cause of the archived ARMS slowdown remains unresolved.
