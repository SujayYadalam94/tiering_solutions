# Oldest inputs selected by c220g5_final plotting

Checked September 10, 2026 against the current `times/c220g5_final/graph.ipynb`.
Its parsing and dataframe/filter cells were evaluated with plotting replaced by
a capture function. No plots, source files or benchmark outputs were modified.
The capture adds the input path to each row without changing selection filters.

The notebook selects 1,918 rows across seven plot families, corresponding to
1,543 distinct timing files. [Selection manifest](selected_files.json).

## Oldest verifiable selected cohort

58 selected timing files match full Git blobs present by commit `fedcde1ee`,
March 15, 2026 at 23:05:27 UTC. They comprise:

* 29 `arms_6.2` files, all labeled `4000MiB`.
* 29 `hybridtier` files, all labeled `4000MiB`.

Each policy has four runs for DuckDB-TPCH-sf100 and XSBench, and three runs for
FAISS, LULESH, MG, PR Kron, PR Twitter, BC Kron and BC Twitter. These are selected
by the normal 6.2 plot cell (cell index 5). The same 29 ARMS files also enter the
global-model plots; the 12 GAPBS ARMS files enter the transfer plots.

All retained commits touching `.time` files before March 16 were inspected for
these currently selected blob identities, across all paths. No earlier tree
matched a selected timing blob. This supplies an existence/commit bound, not a
precise execution date, historical kernel command line or reserve-ratio readback.
Final-copy modification dates must not be substituted for run dates.

[Git matches and plot memberships](early_git_matches.json).

For example, the selected BC Twitter ARMS files contain 159.639, 155.672 and
157.132 s; BC Kron contains 332.362, 332.997 and 341.066 s. Their three-run means
are 157.481 s and 335.475 s. See the separate
[March reserve-setting attribution limits](../march_reserve_timing_provenance/README.md).

## Standard 6.18 plot comparison

The selected standard 6.18 plot inputs were also matched by full contents,
filename and workload to surviving original c220g5 results. The earliest original
mtime found for ARMS is May 6 at 02:07:21 UTC:

```
times/c220g5_final/arms_6.18/pr-kron.sg/8005MiB_run1.time
times/c220g5/arms-6.18_new/pr-kron.sg/8005MiB_run1.time
```

The earliest original mtime found for the standard MANTA selection is May 10 at
02:35:50 UTC, `model_6.18/bc-kron.sg/4028MiB_run1_model_discounted_reward_99_bc-kron.sg_l2-1_10_0.1.time`,
matching `c220g5/model_6.18_new`. These dates use the surviving original metadata,
not final-copy metadata. They do not refer to the separate cost-ablation or long
plot families.

[Original matches for standard 6.18 inputs](standard_618_original_matches.json).
