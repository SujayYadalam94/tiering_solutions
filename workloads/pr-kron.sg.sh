#!/bin/bash

WORKLOAD_ID="pr-kron.sg"
WORKLOAD_OUTPUT="pr-kron.sg"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/pr -n 20 -f ${BENCH_ROOT}/gapbs/benchmark/graphs/kron.sg"
WORKLOAD_EXE_NAME="pr"
WORKLOAD_MODEL_BASE="pr-kron.sg"
WORKLOAD_SYSTEMS="arms hybridtier model logging"
