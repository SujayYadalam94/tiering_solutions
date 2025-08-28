#!/bin/bash

######## changes the below path
BIN=/mnt/ssd/workloads/gapbs
GRAPH_DIR=/mnt/ssd/inputs/gapbs

# Use NTHREADS from environment if set, otherwise default to 12
THREADS=${NTHREADS:-12}
BENCH_RUN="env OMP_NUM_THREADS=${THREADS} ${BIN}/cc_sv -n 8 -f ${GRAPH_DIR}/kron_s29.sg"
BENCH_DRAM=""


# twitter.sg: ~12600MB 

if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="8500MB"
fi


export BENCH_RUN
export BENCH_DRAM
