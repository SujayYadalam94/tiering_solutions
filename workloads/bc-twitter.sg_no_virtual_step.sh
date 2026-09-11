#!/bin/bash

WORKLOAD_ID="bc-twitter.sg_no_virtual_step"
WORKLOAD_OUTPUT="bc-twitter.sg_no_virtual_step"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/bc -n 10 -f ${GRAPH_DIR:-${BENCH_ROOT}/gapbs/benchmark/graphs}/twitter.sg"
WORKLOAD_EXE_NAME="bc"
WORKLOAD_MODEL_BASE="bc-twitter.sg_no_virtual_step"
WORKLOAD_VIRTUAL_STEP_SAMPLES="10000"
WORKLOAD_SYSTEMS="model"
