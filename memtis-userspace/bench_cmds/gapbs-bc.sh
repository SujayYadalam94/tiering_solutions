#!/bin/bash

######## changes the below path
BIN=/mnt/nvme/workloads/gapbs
GRAPH_DIR=/mnt/nvme/inputs/gapbs

BENCH_RUN="env OMP_NUM_THREADS=12 ${BIN}/bc -n 16 -f ${GRAPH_DIR}/twitter.sg"
BENCH_DRAM=""


# twitter.sg: ~12600MB 

if [[ "x${NVM_RATIO}" == "x1:32" ]]; then
    BENCH_DRAM="382MB"
elif [[ "x${NVM_RATIO}" == "x1:16" ]]; then
    BENCH_DRAM="740MB"
elif [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="1486MB"
elif [[ "x${NVM_RATIO}" == "x1:4" ]]; then
    BENCH_DRAM="2520MB"
elif [[ "x${NVM_RATIO}" == "x1:2" ]]; then
    BENCH_DRAM="4200MB"
elif [[ "x${NVM_RATIO}" == "x1:1" ]]; then
    BENCH_DRAM="6300MB"
elif [[ "x${NVM_RATIO}" == "x1:0" ]]; then
    BENCH_DRAM="70000MB"
fi


export BENCH_RUN
export BENCH_DRAM
