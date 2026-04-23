#!/bin/bash

WORKLOAD_ID="DuckDB-TPCDS-sf100"
WORKLOAD_OUTPUT="DuckDB-TPCDS-sf100"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpcds-sf100/.*benchmark --threads=16 --nruns=1"
WORKLOAD_EXE_NAME="benchmark_runner"
WORKLOAD_MODEL_BASE="DuckDB-TPCDS-sf100"
WORKLOAD_VIRTUAL_STEP_SAMPLES="100000"
WORKLOAD_SYSTEMS="arms hybridtier model logging"
