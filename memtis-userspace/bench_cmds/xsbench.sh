#!/bin/bash

BIN=/mnt/ssd/workloads/xsbench
# Use NTHREADS from environment if set, otherwise default to 12
THREADS=${NTHREADS:-12}
BENCH_RUN="${BIN}/XSBench -g 130000 -p 20000000 -t ${THREADS}"
BENCH_DRAM=""


if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="8100MB"
fi


export BENCH_RUN
export BENCH_DRAM
