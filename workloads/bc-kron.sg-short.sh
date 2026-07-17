#!/bin/bash

WORKLOAD_ID="bc-kron.sg-short"
WORKLOAD_OUTPUT="bc-kron.sg-short"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/bc -n 1 -f ${BENCH_ROOT}/gapbs/benchmark/graphs/kron.sg"
WORKLOAD_EXE_NAME="bc"
WORKLOAD_MODEL_BASE="bc-kron.sg"
WORKLOAD_VIRTUAL_STEP_SAMPLES="10000"
WORKLOAD_SYSTEMS="arms hybridtier model logging"