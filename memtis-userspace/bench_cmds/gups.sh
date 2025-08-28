#!/bin/bash

BIN=/mnt/ssd/workloads/gups
# Use NTHREADS from environment if set, otherwise default to 12
THREADS=${NTHREADS:-12}
BENCH_RUN="${BIN}/gups-hotset-move ${THREADS} 1000000000 36 8 33"
BENCH_DRAM="7270MB"

export BENCH_RUN
export BENCH_DRAM
