# Direct training-log Parquet output

Builds with `PRINT_TRAINING_DATA=true` now write one Snappy-compressed Parquet
file after reward finalization, using a dedicated pool of up to 10 threads for
parallel column encoding/compression. `LOG_OUTPUT_PATH=/path/run1.log` produces
`/path/run1.parquet`; `.parquet` and extensionless paths are also accepted.
Timing files and interval/diagnostic logs are unchanged.

The writer processes 65,536 rows at a time, retaining row order and all named
CSV columns, including the optional `FULL_LOGS` fields. It does not duplicate
the entire trace in another table. The original sample buffer is still needed
until finalization, because rewards depend on future samples. A complete file
is published by rename only after its footer and output stream close
successfully. Caught write failures remove the temporary output; an abrupt
kill can leave a `.parquet.tmp.*` directory, which is not a completed log.
New files honor the process umask, just like the former CSV files.

Training builds flush on return from application `main`, or before a public
`exit()` call starts running exit handlers. This keeps Arrow's allocator and
other runtime state alive through the write; relying on the old early-registered
`atexit` handler could write after Arrow teardown. Normal exit handlers and exit
status are preserved. Non-logging builds retain their previous shutdown path.

The shared helper-library blacklist includes `libarrow.so`, `libarrow_*`, and
`libparquet.so`. Where helper-IP filtering is enabled, their samples take the
existing helper-filter path rather than normal access accounting. This also
affects application use of these libraries, not just the training-log writer;
virtual-mode helper samples retain the existing missed-access accounting rules.

## Schema compatibility

- Floating-point fields use native `float32`, preserving their original values
  instead of rounding through six-decimal CSV text and inferring `float64`.
- Unsigned integers use `uint64`; signed integers and numeric `in_dram` (0/1)
  use `int64`. Large counters and addresses are not converted through floats.
- The unnamed empty column caused by CSV's trailing comma is omitted.
- Internal fields that were never in the CSV (such as the `prev` pointer) are
  still excluded. Feature names and reward calculations are unchanged.

Read normally with `pandas.read_parquet(path)` or `pyarrow.parquet.read_table(path)`.
Consumers that require particular dtypes should cast explicitly. The existing
feature-name-based processing remains applicable, but values are intentionally
not bit-for-bit equivalent to the rounded CSV pipeline.

## Model timing telemetry

Model Parquet builds (`USE_MODEL=true`, `PRINT_TRAINING_DATA=true`) include three
additional `uint64` columns by default:

- `model_feature_aggregation_ns`: elapsed wall time for the completed virtual
  step's feature preparation across all pages, including virtual-window/EWMA and
  group updates, row extraction, and packing the model's 12 input features.
- `model_inference_ns`: elapsed wall time for the entire batch of model
  predictions, including the missed-access adjustment.
- `model_score_total_ns`: elapsed wall time from the start of feature preparation
  through final score assignment, including both phases and intervening step
  housekeeping. Row logging and Parquet serialization are excluded.

All timers use a monotonic clock. Each page row in a completed virtual step has
the same batch totals; take one value per `step`, rather than summing page rows.
Synthetic new-page rows backfilled for an earlier step have zero timings. Filter
on `model_score_total_ns > 0` before grouping. These measurements describe the
virtual-step batch that produces `model_score`, not the independently scheduled
migration-policy scoring pass or time between sampled steps.

Telemetry batches feature packing before inference, using an additional 96 bytes
per page temporarily and a constant number of clock reads per step. Non-model
builds omit these model-specific columns; ARMS has separate telemetry below.
Non-Parquet builds have no timing instrumentation.
Disable telemetry with `make -B MODEL_TIMING_TELEMETRY=false <library-target>`;
use `-B` when changing the option so existing objects are rebuilt. The same
option defaults to `true` to re-enable it.

## ARMS timing telemetry

ARMS training builds (`USE_MODEL=false`, `PRINT_TRAINING_DATA=true`,
`VIRTUAL_FEATURES_ENABLED=false`) now enable `ARMS_TIMING_TELEMETRY=true` by
default and write three `uint64` nanosecond columns:

- `arms_feature_aggregation_ns`: the batch of policy-window/EWMA updates.
- `arms_scoring_ns`: weighted-history score calculation, score assignment, and
  filling the score vector for the same batch.
- `arms_score_total_ns`: the sum of those two phases.

Three monotonic-clock samples bracket the two batch loops. Timings exclude
page-snapshot construction, training-only derivative/group features and row
extraction, logging, ranking, migration decisions, and Parquet serialization.
Each scored page row carries the same batch values. Group by `step`, take each
batch timing once, and divide summed batch nanoseconds by the number of timed
page rows to obtain page-weighted costs. ARMS steps are policy iterations;
MANTA's existing measurements use virtual steps, so their total-ms/step values
describe different batch populations. ARMS scoring is not learned inference.

The collector validates the fields and nonzero, internally consistent timings
after each ARMS run. It rejects older timing-free traces on resume. Use a fresh
run number to preserve the previous files and `--arms-only` to avoid rerunning
MANTA:

```sh
COLLECTION_RUN=12 bash run_arms_and_missing_models.sh --platform c220g5 --arms-only
```

This rebuilds the training library with timing enabled, collects all eight ARMS
workloads at the existing 10090 MiB budget, and writes
`logs/plots/arms_timing_summary_run12.csv`. The CSV includes source paths,
feature/scoring µs/page, total ms/policy-step, timed steps, and page-row counts.
The raw logs are `logs/<workload>/10090MiB_run12_train_arms.parquet`.
Add `--dry-run` to inspect the planned paths without setup or workloads.
The comparison cell in `logs/live_perf.ipynb` selects `ARMS_TIMING_RUN = 12`;
rerun that cell after collection to populate ARMS costs alongside the run 11
model results. Change that setting if collecting a different ARMS run number.

For independent extraction or validation:

```sh
python3 scripts/arms_timing_summary.py --check logs/bc-twitter.sg/10090MiB_run12_train_arms.parquet
python3 scripts/arms_timing_summary.py --output logs/plots/arms_timing_summary_run12.csv logs/*/10090MiB_run12_train_arms.parquet
```

Use a Python with pandas and PyArrow; set `PARQUET_PYTHON` for the collector if
needed. Non-training ARMS builds have no timer overhead or added columns.
Opt out with `make -B ARMS_TIMING_TELEMETRY=false <library-target>`.

## Build dependencies

Only logging/training variants link Arrow and Parquet: `logging`, `train`,
`arms_train`, `arms_near_train`, `arms_near_train_all_numa`, and `arms_cxl_train`.
Non-logging variants do not discover, include, or link those dependencies.
The adapter translation unit uses C++20 for modern Arrow headers; other source
files retain the project's C++17 setting. The parallel writer is tested with
Arrow/PyArrow 24 and requires the buffered RecordBatch writer/executor APIs.

The Makefile first looks for `arrow` and `parquet` with `pkg-config`, then uses
the C++ headers and shared libraries bundled with an existing PyArrow install.
It does not install packages or create symlinks. PyArrow-backed builds embed
that library directory as a runtime search path; keep that environment installed.

```sh
make -j4 libraries/C220G5/libhemem-logging.so
make -j4 libraries/C220G5/libhemem-arms_near_train_all_numa.so
make -j4 training-libraries  # rebuild every logging/training variant

# Select a different Python environment, when needed:
make -j4 PARQUET_PYTHON=/path/to/venv/bin/python
```

Custom installations can override both `PARQUET_CXXFLAGS` and `PARQUET_LIBS`.
Rebuild existing training libraries before running the updated scripts.
`logging.sh` and the DRAM-only, CXL-only, and 80-GB training runners consume the
direct `.parquet` output. They no longer need the Python CSV converter at run
time. Standalone CSV converters remain available for historical logs.

## Verification

```sh
python3 tests/test_logging_parquet.py
```

Requires PyArrow and a C++ compiler. Tests cover both schemas, column values
and order, full-range counters, NaN/infinity/negative zero, finalized rewards,
empty logs, row-group boundaries, file permissions, write failures, and builds
without the Parquet dependencies. A stubbed-runtime test also exercises the
actual preload hook with normal return and explicit `exit()`, including an
application that initializes Arrow itself. They do not run benchmarks or change kernel
settings.

## Dump-speed spot check (2026-09-06)

One million synthetic rows with varying counters, addresses, and sparse/random
floats, three trials per writer, optimized builds, output on `/tmp` (`/dev/sda3`).
Row generation and reward finalization were excluded. CSV used the preceding
10-thread, 1-MiB-buffer, `to_chars` writer. No benchmark workload was run.

| Path | Median dump/conversion time | Including explicit file fsync |
| --- | ---: | ---: |
| Optimized CSV dump (10 threads) | about 1.1 s | about 2.7 s |
| Previous direct Parquet dump (serial columns) | 8.04 s | 8.34 s |
| Direct Parquet dump (10 column workers) | 4.59 s | 5.37 s |
| CSV-to-Parquet conversion alone | 6.99 s | 7.77 s |

Thus direct threaded Parquet was faster than CSV plus conversion (~8.1 s), but
not faster than dumping CSV alone. The conversion used the existing pandas /
PyArrow read-concatenate-write workflow, excluding Python import time. Production
writes do not explicitly fsync. Actual logs reside on `/dev/sdb`, so these are
not production storage or full-workload measurements. Native float32 Parquet
also differs from CSV's rounded float64 conversion, as described above.

The current DuckDB TPC-H/TPC-DS workloads use the native C++ benchmark runner,
native `.duckdb` caches, and generated tables. Their SQL does not request Arrow
results or Parquet file I/O. The runner nevertheless initializes its own Parquet
extension in `benchmark/interpreted_benchmark.cpp`; the local binary statically
links DuckDB's `libparquet_extension.a`, not the external `libparquet.so` or
`libarrow.so`. The helper-library blacklist therefore does not exclude this
DuckDB code. No PyArrow/Python dependency appears in the runner's launch command
or dynamic-library dependencies.

## Collect all ARMS traces and workload-specific FAISS/MG traces

```sh
COLLECTION_RUN=12 BUILD_LIBRARIES=1 \
bash run_arms_and_missing_models.sh --platform c220g5
```

This rebuilds timing-enabled libraries, collects eight ARMS training traces, and
collects FAISS and MG with their respective `model_discounted_reward_99_*_l2.o`
objects. The model collection uses separate `libhemem-logging-<model>.so`
libraries with logging mode, virtual steps, and a ten-score mean. There is no
fallback to the generic `libhemem-logging.so` or the global model.

`logging.sh` now also selects the workload-specific library directly, using
`WORKLOAD_MODEL_BASE` and `LOGGING_MODEL_PCT` (default 99). Build those targets
explicitly or use `make training-libraries` before invoking it standalone.

Each model trace has a `run12.model.json` sidecar containing model-object and
library paths and SHA-256 hashes. A completed model trace is skipped only when
its identity matches and its model timing columns contain timed rows. Old traces
without this evidence are preserved and require a new `COLLECTION_RUN`.

Model outputs are `logs/{faiss_10M,mg.D.x}/run12.parquet`. ARMS outputs are
`logs/<workload>/10090MiB_run12_train_arms.parquet`, with a consolidated
`logs/plots/arms_timing_summary_run12.csv`. Console logs accompany both kinds.
Run `--dry-run` to inspect all selected paths without launching workloads.

## No-virtual-step models without training logs

Models whose names contain `_no_virtual_step_` use the existing 250 ms policy
feature path in both the regular and `_train` libraries. Both execute the model;
`PRINT_TRAINING_DATA` controls whether rows are stored and written. The regular
library allocates no training-log buffer and needs no Arrow/Parquet dependency.

The existing training runner accepts workload IDs and an empty library suffix
for inference without training logs:

```sh
MODEL_LIB_SUFFIX= ./run_all_measurements_train.sh --platform c220g5 bc-twitter.sg_no_virtual_step
```

Omit `MODEL_LIB_SUFFIX` to retain training-data logging. The regular model runner reads `WORKLOAD_MODEL_BASE` from the selected file in
`workloads/` and defaults to inference without training logs. No suffix or
virtual-step flag is needed:

```sh
./measurement_model.sh --platform c220g5 4080 1 bc-twitter.sg_no_virtual_step
```

These use the existing `_no_virtual_step` model libraries; there are no separate
policy variants. The workload and measurement setup still come from the runner.
