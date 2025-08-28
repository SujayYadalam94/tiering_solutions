#!/bin/bash

BIN=/mnt/ssd/workloads/graph500/omp-csr
BENCH_RUN="env OMP_NUM_THREADS=12 SKIP_VALIDATION=1 ${BIN}/omp-csr -s 26 -V"
BENCH_DRAM=""


if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="3925MB"
fi


export BENCH_RUN
export BENCH_DRAM
