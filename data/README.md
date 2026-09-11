The local `graphs/` directory holds SSD copies of `kron.sg` and `twitter.sg`
from `/users/zimooo2/gapbs/benchmark/graphs/`. The copy operation verifies
SHA-256 checksums and stores them alongside the files as `*.sha256`.
Graph data is excluded from Git.

On c220g5, the BC and PageRank Kron and Twitter workload variants default to
these SSD copies in `data/graphs`, including via
`run_all_measurements.sh` and for the short, long, and no-virtual-step variants.
Set `GRAPH_DIR` to use the HDD originals instead:

```bash
sudo -E env GRAPH_DIR=/users/zimooo2/gapbs/benchmark/graphs \
  ./measurement_model.sh --platform c220g5 6073 1 '' bc-kron.sg
```

The gsl_optane default remains `${BENCH_ROOT}/gapbs/benchmark/graphs`.
The 80 GB workloads use their separate `GAPBS_80GB_GRAPH` setting.
