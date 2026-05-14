#!/bin/bash

WORKLOAD_ID="pr-twitter.sg-long"
WORKLOAD_OUTPUT="pr-twitter.sg-long"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/pr -n 100 -f ${BENCH_ROOT}/gapbs/benchmark/graphs/twitter.sg"
WORKLOAD_EXE_NAME="pr"
WORKLOAD_MODEL_BASE="pr-twitter.sg"
WORKLOAD_VIRTUAL_STEP_SAMPLES="10000"
WORKLOAD_SYSTEMS="arms hybridtier model logging"