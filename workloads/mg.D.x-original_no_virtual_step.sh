#!/bin/bash

WORKLOAD_ID="mg.D.x-original_no_virtual_step"
WORKLOAD_OUTPUT="mg.D.x-original_no_virtual_step"
WORKLOAD_COMMAND="OMP_NUM_THREADS=16 ${BENCH_ROOT}/NPB3.4.3/NPB3.4-OMP/bin/mg.D.x"
WORKLOAD_EXE_NAME="mg.D.x"
WORKLOAD_MODEL_BASE="mg.D.x-original_no_virtual_step"
WORKLOAD_VIRTUAL_STEP_SAMPLES="10000"
WORKLOAD_SYSTEMS="model"
