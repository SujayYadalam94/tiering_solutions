# bwlat-profiler

Measures loaded latency vs. bandwidth for local (DRAM) and remote
(CXL-emulating) NUMA memory and writes `bw-lat.dat` in the format ARMS
expects (`arms/src/counterfactual.h`, `CF_BWLAT_DEFAULT`).

```bash
make
./run.sh                        # ~1 min; writes bw-lat.dat and bw-lat.csv
PERF_GOVERNOR=1 ./run.sh        # pin the performance governor during the run
ARMS_BWLAT=$PWD/bw-lat.dat ...  # point ARMS at the result
```

## Method

The workload uses the same idea as Intel MLC's `--loaded_latency`. All
threads run on `--cpu-node` (default 0, the node ARMS applications run on).

- **Latency:** one thread walks a random pointer chain (1 GB, THP-backed so
  that TLB misses don't count) that lives on the target node. Elapsed time
  divided by hops gives the load-to-use latency. Its core's SMT sibling is
  left idle.
- **Bandwidth:** every other CPU on the node streams read-only AVX2 loads
  over a private 128 MB buffer on the target node. After each 512 B, a thread
  spins for `delay` TSC ticks. The sweep goes from paused, through
  geometric delays, down to 0.
- `bw_gbps` is the total read bandwidth of all threads, including the
  latency thread. It is read-only to match ARMS, which feeds read bandwidth
  into the curve.

The tier is `dram` when the target is `--dram-node` (default 0) and `cxl`
when it is `--cxl-node` (default 1).

## Output

- `bw-lat.dat`: JSON with `dram.raw` and `cxl.raw` arrays of
  `{bw_gbps, latency_ns}`, sorted by bandwidth, plus a `meta` block that
  ARMS ignores. A point is dropped when it gains less than `--min-step`
  (1%) bandwidth over the previous kept point. These are saturation or
  fold-back points: ARMS extrapolates past the curve with the last
  segment's slope, and near-duplicate bandwidths make that slope explode.
- `bw-lat.csv`: every measured point, with a `kept` column.

Each tier is capped at 64 points (`CF_MAX_CURVE_POINTS`). Run `./bwlat -h` to
see all options.
