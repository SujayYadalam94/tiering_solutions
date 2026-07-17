#!/bin/bash

WORKLOAD_ID="bc-twitter.sg-short"
WORKLOAD_OUTPUT="bc-twitter.sg-short"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/bc -n 1 -f ${BENCH_ROOT}/gapbs/benchmark/graphs/twitter.sg"
WORKLOAD_EXE_NAME="bc"
WORKLOAD_MODEL_BASE="bc-twitter.sg"
WORKLOAD_VIRTUAL_STEP_SAMPLES="10000"
WORKLOAD_SYSTEMS="arms hybridtier model logging"