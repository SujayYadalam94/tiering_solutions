#!/bin/bash

BIN=/mnt/nvme/workloads/xsbench
BENCH_RUN="${BIN}/XSBench -g 130000 -p 20000000 -t 12"
BENCH_DRAM=""


if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="8100MB"
fi


export BENCH_RUN
export BENCH_DRAM
