#!/bin/bash

BIN=/mnt/nvme/workloads/silo/out-perf.masstree/benchmarks
BENCH_RUN="${BIN}/dbtest --verbose --bench tpcc --num-threads 12 --scale-factor 100 --ops-per-worker=10000000" # --numa-memory 81260781240
BENCH_DRAM=""

if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="12500MB"
fi

export BENCH_RUN
export BENCH_DRAM
