# Surviving ARMS binary from before the archived benchmarks

`libarms_kernel.so` is a byte-for-byte copy of the repository-root artifact, preserved on September 9, 2026. The original file has modification and change timestamps of April 30, 2026, 05:21:33 UTC. Its SHA-256 is `d6cfc27d1420ee40f588479f7c1be3b5e3353fe3d0f2738a899a7397b4c1845a`; size is 375,440 bytes. See [metadata.json](metadata.json).

This is an actual retained binary, unlike the newly compiled May 4 reconstruction. There is no per-run binary hash connecting it to the May 7 measurements. It contains some functions first committed on May 2, and a model moving-average alpha index of 1 where that commit defaults to 3. This is consistent with earlier uncommitted work but prevents assigning the binary to one exact Git commit from timestamps alone.

Read-only ELF inspection establishes the following effective settings without loading or running the library:

| Setting | Binary evidence |
|---|---|
| Platform | C220G5-compatible: application bound to node 1; free-memory path is node 0; PEBS CPU loop ends at 30 and skips 10–19 |
| Model, logging run, training data | All false in startup string arguments; no forest dependency |
| Migration workers | Enabled; creation loop spans ten thread pointers |
| Verbose diagnostics | OFFCORE setup only clears descriptor arrays and disables the metric flag; no verbose counter setup or periodic migration output in the active code |
| Policy intervals | 500,000 and 100,000 microseconds |
| PEBS events | Read `0x20d1`, write `0x82d0` |
| PEBS ring | `(1 + 2^11)` operating-system pages per ring; same as reconstructed May 4 ARMS |
| High-fidelity sample period | 5,003 |
| Migration backoff | Writes zero after promotion/demotion |
| Model history fields | History length 4, summary min/max, alpha index 1; inactive in normal ARMS scoring |
| Compiler | GCC 15.1.0, matching today's reconstruction compiler |
| Dynamic dependencies | libnuma, libstdc++, libm, libgcc_s, libc; no Arrow/Parquet/model library |

The copied [disassembly](disassembly.txt), [symbols](symbols.txt), [dynamic section](dynamic.txt), and [string offsets](strings.txt) provide the raw evidence. `arms_start_tiering` binds slow node 1 at address `0x1d9b5`, sets up the 0–29 CPU loop at `0x1dbd0`/`0x1de56`, and constructs the ring size at `0x1dc74`. Startup arguments show false for model/logging/training at `0x1d748`, `0x1d778`, and `0x1d886` respectively. The worker creation loop spans 80 bytes at `0x1e0c5`.

The principal active policy difference observed against freshly compiled May 4 code is single-precision migration cost/benefit comparisons in this retained artifact versus double-precision comparisons after the May 3 changes. The newer source also adds virtual-step time/cost helper functions and startup printouts. The model alpha-index difference does not change ARMS's ordinary score update. The existing normalizer is an approximate structural check: several reported differences concern the trailing bytes after a scalar constant or relocated labels, so its difference count must not be interpreted as a count of semantic changes.

The first isolated attempt on September 9 aborted during startup with a glibc allocator assertion, after the IMC setup banner and before initialization completed. That last banner does not establish whether the failure occurred inside IMC setup or later during thread startup. Its runtime is invalid and must not be compared with the archive. Hardware settings matching C220G5 does not establish internal binary integrity. A read-only check found the tracked-page allocation size consistent with the reset routine, so that particular proposed mixed-layout error is not established. The exact crash cause remains unresolved without further debugging. See the [failed attempt's output](../sessions/20260909T130001Z-142220/results/20260909T130001Z-142220/01-retained_april_arms/stdout.log).

The bounded crash-record check found no saved backtrace. Core handling routes through Apport; its [existing log excerpt](crash_record.txt) records BC signal 6 with core limit 0 and rejects the executable because it does not belong to a package. Both `/var/crash` and `/var/lib/systemd/coredump` were empty. The shell's “dumped core” message therefore does not establish an available core artifact. Static inspection additionally found the same constructor order in the retained and rebuilt May 4 libraries: `frame_dummy`, the `arms_kernel.cpp` global initializer, then the `migration_worker.cpp` initializer. These checks do not establish the corruption's origin.

Recovered VS Code snapshot `defs.h/9nRJ.h`, saved April 30 at 05:20:28 UTC, contains the alpha-index-1 adjusted moving-average definitions found in this artifact. This explains their presence before the May 2 Git commit. It still does not prove that this binary produced any archived run; see the [editor-history audit](../editor_history/README.md).
