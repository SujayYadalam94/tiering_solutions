#!/bin/bash

######## changes the below path
BIN=/mnt/ssd/workloads/gapbs
GRAPH_DIR=/mnt/ssd/inputs/gapbs

BENCH_RUN="env OMP_NUM_THREADS=12 ${BIN}/bc -n 16 -f ${GRAPH_DIR}/twitter.sg"
BENCH_DRAM=""


# twitter.sg: ~12600MB 

if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="1486MB"
fi


export BENCH_RUN
export BENCH_DRAM
