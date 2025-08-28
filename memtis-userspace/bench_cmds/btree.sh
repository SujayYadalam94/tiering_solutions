#!/bin/bash

BIN=/mnt/ssd/workloads/btree/bin
# Use NTHREADS from environment if set, otherwise default to 12
THREADS=${NTHREADS:-12}
BENCH_RUN="env OMP_NUM_THREADS=${THREADS} ${BIN}/bench_btree_mt -- 600000000 750000000"
BENCH_DRAM=""


if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="4886MB"
fi


export BENCH_RUN
export BENCH_DRAM
