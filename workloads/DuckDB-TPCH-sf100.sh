#!/bin/bash

WORKLOAD_ID="DuckDB-TPCH-sf100"
WORKLOAD_OUTPUT="DuckDB-TPCH-sf100"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpch-sf100/.*benchmark --threads=16"
WORKLOAD_EXE_NAME="benchmark_runner"
WORKLOAD_MODEL_BASE="DuckDB-TPCH-sf100"
WORKLOAD_SYSTEMS="arms hybridtier model logging"
