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
and non-Parquet builds have neither these columns nor timing instrumentation.
Disable telemetry with `make -B MODEL_TIMING_TELEMETRY=false <library-target>`;
use `-B` when changing the option so existing objects are rebuilt. The same
option defaults to `true` to re-enable it.

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
