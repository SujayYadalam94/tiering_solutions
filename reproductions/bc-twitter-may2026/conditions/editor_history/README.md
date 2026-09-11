# Recovered local editor history

The files here are copies of VS Code save snapshots from `/users/zimooo2/.vscode-server/data/User/History`. [index.json](index.json) records each original resource, path, save timestamp in UTC, and SHA-256. Original `entries.json` files are retained alongside snapshots. The copied set contains 370 snapshots (about 6 MB) for eleven relevant source/runner files through May 11.

These records establish saved file contents at specific times. They do not prove that the saved version was compiled or used by a benchmark, and later changes made through Git, Vim, or scripts need not appear in VS Code history. Several busy files retain only their latest 50 entries, truncating earlier history.

## Closest surviving saves before the May 7 sweep

| File | Save timestamp UTC | Snapshot | Finding |
|---|---|---|---|
| Makefile | May 7, 04:10:44.530 | [4HiW](Makefile/4HiW) | Explicit C220G5 platform/default, normal `-O3 -DNDEBUG`, ARMS `USE_MODEL=false`; model history 2, length 10, scaler sweep including 0.03125 through 1.5 |
| defs.h | May 4, 03:21:01.042 | [iTBv.h](defs.h/iTBv.h) | Active ARMS constants match the frozen May 4 reconstruction; only the subsequently added model time-scaling declarations differ |
| measurement_common.sh | May 4, 03:21:31.547 | [89Uc.sh](measurement_common.sh/89Uc.sh) | Byte-for-byte identical to Git commit `8d6a8bc66` |
| defrag.sh | March 13, 16:36:05.950 | [G1RT.sh](defrag.sh/G1RT.sh) | THP and defrag always; sync/drop caches 3 and 2, compact, repeat cache drops |

The Makefile save precedes the May 7 boot beginning 05:59:49 UTC and the first reference ARMS result at 09:07:49 UTC. It differs from the May 4 Git Makefile by selecting C220G5 instead of GSL_OPTANE and adding scaler 0.03125. This is substantially better evidence for the historical platform than the Git commit's default, while still not an executable manifest. The [Makefile diff](may07_makefile_vs_git_may04.diff), [defs diff](may04_defs_vs_git_may04.diff), and [empty measurement-common diff](may04_measurement_common_vs_git_may04.diff) are preserved.

The relevant ARMS definitions are backoff 0, verbose false, ten workers, a 2,049-page PEBS ring, default sample period 10,007, high-fidelity period 5,003, and 500/100 ms policy intervals. They do not reveal a new explanation for the archived ARMS slowdown.

The defs history independently dates the move from model alpha index 1 to 3 to April 30, 15:17 UTC. Snapshot [9nRJ.h](defs.h/9nRJ.h), saved April 30 at 05:20:28 UTC, contains alpha index 1 and the newly added adjusted-moving-average feature. This closely precedes the retained root binary's 05:21:33 UTC timestamp and explains why it includes functions committed only on May 2. Model virtual-step default changed from 3,162 to 10,000 on May 4, 02:53:13 UTC. These model-path changes do not alter ARMS's ordinary scoring.

## Additional history and limits

The earliest retained `arms_kernel.cpp` save is May 9, 06:36 UTC. It adds model boundary-cost experiments and removes the model time factor; it also prints migration counts. It is later than the May 7 measurements and cannot establish that this printing was present during those measurements. The retained `page.cpp` snapshots also begin after the target sweep.

The separate May 11-dated shell-history backup contains repeated `make clean; make -j` and benchmark commands, but no command timestamps. Around lines 1474–1495 it shows training with `build_v3.sh`, moving existing BC models into `bc-3k`, copying parent-directory BC models into `models`, then a clean tiering rebuild. Without successful-command output or per-file hashes this does not identify the exact BC Twitter object used on May 7. The May 3 editor snapshot of the training script trains BC Twitter first for discounted reward 99, consistent with the recovered May 3 model timestamps; this strengthens chronology, not byte identity with a benchmark.

The [May 3 training script save](training_build_v3.sh/g2ps.sh) is timestamped 02:14:46.385 UTC; its metadata is retained in the same directory. The [shell excerpt](shell_training_excerpt.txt) preserves the command sequence and the backup file's May 11, 20:55:24.719324 UTC modification time. The model exporter at training-repository commit `25cd9ed:process_data/all_models_v3.py` writes model `.txt` and `.o` files relative to its current directory. The May 7 Makefile directly links `tiering_solutions/models/%.o` into the named model library. The shell backup does not record the additional copy or symlink required to connect the training repository's backup directory to this build input; neither successful commands nor exact linked object hashes survive. The recovered May 3 forest remains the best dated candidate, not a proven per-run identification.
