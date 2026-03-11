#!/bin/bash

WORKLOAD_ID="bc-twitter.sg"
WORKLOAD_OUTPUT="bc-twitter.sg"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/bc -n 40 -f ${BENCH_ROOT}/gapbs/benchmark/graphs/twitter.sg"
WORKLOAD_EXE_NAME="bc"
WORKLOAD_MODEL_BASE="bc-twitter.sg"
WORKLOAD_SYSTEMS="arms hybridtier model logging"
