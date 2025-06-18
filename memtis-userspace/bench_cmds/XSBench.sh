#!/bin/bash

BIN=/mnt/nvme/workloads/xsbench
BENCH_RUN="${BIN}/XSBench -g 130000 -p 20000000 -t 12"
BENCH_DRAM=""


if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="8100MB"
elif [[ "x${NVM_RATIO}" == "x1:4" ]]; then
    BENCH_DRAM="13107MB"
elif [[ "x${NVM_RATIO}" == "x1:2" ]]; then
    BENCH_DRAM="21800MB"
elif [[ "x${NVM_RATIO}" == "x1:1" ]]; then
    BENCH_DRAM="32768MB"
elif [[ "x${NVM_RATIO}" == "x1:0" ]]; then
    BENCH_DRAM="75000MB"
fi


export BENCH_RUN
export BENCH_DRAM
