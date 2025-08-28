#!/bin/bash

######## changes the below path
BIN=/mnt/ssd/workloads/gapbs
GRAPH_DIR=/mnt/ssd/inputs/gapbs

# Use NTHREADS from environment if set, otherwise default to 12
THREADS=${NTHREADS:-12}
BENCH_RUN="env OMP_NUM_THREADS=${THREADS} ${BIN}/pr -f ${GRAPH_DIR}/twitter.sg -n 16"
BENCH_DRAM=""


# twitter.sg: ~12600MB 

if [[ "x${NVM_RATIO}" == "x1:32" ]]; then
    BENCH_DRAM="382MB"
elif [[ "x${NVM_RATIO}" == "x1:16" ]]; then
    BENCH_DRAM="740MB"
elif [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="1400MB"
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
