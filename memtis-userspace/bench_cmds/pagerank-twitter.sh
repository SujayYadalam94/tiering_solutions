#!/bin/bash

######## changes the below path
BIN=/mnt/ssd/workloads/gapbs
GRAPH_DIR=/mnt/ssd/inputs/gapbs

# Use NTHREADS from environment if set, otherwise default to 12
THREADS=${NTHREADS:-12}
BENCH_RUN="env OMP_NUM_THREADS=${THREADS} ${BIN}/pr -n 16 -f ${GRAPH_DIR}/twitter.sg"
BENCH_DRAM=""


# twitter.sg: ~12600MB 

if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="1404MB"
fi


export BENCH_RUN
export BENCH_DRAM
