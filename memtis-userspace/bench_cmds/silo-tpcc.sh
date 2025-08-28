#!/bin/bash

BIN=/mnt/ssd/workloads/silo/out-perf.masstree/benchmarks
# Use NTHREADS from environment if set, otherwise default to 12
THREADS=${NTHREADS:-12}
BENCH_RUN="${BIN}/dbtest --verbose --bench tpcc --num-threads ${THREADS} --scale-factor 100 --ops-per-worker=10000000" # --numa-memory 81260781240
BENCH_DRAM=""

if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="12500MB"
fi

export BENCH_RUN
export BENCH_DRAM
