#!/bin/bash

WORKLOAD_ID="DuckDB-TPCH-sf100_no_virtual_step"
WORKLOAD_OUTPUT="DuckDB-TPCH-sf100_no_virtual_step"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpch-sf100/.*benchmark --threads=16 --nruns=1 --disable-timeout"
WORKLOAD_EXE_NAME="benchmark_runner"
WORKLOAD_MODEL_BASE="DuckDB-TPCH-sf100-original_no_virtual_step"
WORKLOAD_VIRTUAL_STEP_SAMPLES="100000"
WORKLOAD_SYSTEMS="model"
