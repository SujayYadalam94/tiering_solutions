#!/bin/bash

WORKLOAD_ID="pr-kron.sg_no_virtual_step"
WORKLOAD_OUTPUT="pr-kron.sg_no_virtual_step"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/pr -n 10 -f ${GRAPH_DIR:-${BENCH_ROOT}/gapbs/benchmark/graphs}/kron.sg"
WORKLOAD_EXE_NAME="pr"
WORKLOAD_MODEL_BASE="pr-kron.sg_no_virtual_step"
WORKLOAD_VIRTUAL_STEP_SAMPLES="10000"
WORKLOAD_SYSTEMS="model"
