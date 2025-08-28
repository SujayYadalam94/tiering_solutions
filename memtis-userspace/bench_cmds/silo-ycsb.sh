#!/bin/bash

BIN=/mnt/ssd/workloads/silo/out-perf.masstree/benchmarks
BENCH_RUN="${BIN}/dbtest --verbose --parallel-loading --bench ycsb --num-threads 12 --scale-factor 400000 --ops-per-worker=150000000" # --numa-memory 76665166233
BENCH_DRAM=""

#####
# Silo ~59500MB memory footprint
#####

if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="10700MB"
fi

export BENCH_RUN
export BENCH_DRAM
