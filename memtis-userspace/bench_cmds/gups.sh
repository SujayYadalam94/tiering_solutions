#!/bin/bash

BIN=/mnt/ssd/workloads/gups
BENCH_RUN="${BIN}/gups-hotset-move 12 1000000000 36 8 33"
BENCH_DRAM="7270MB"

export BENCH_RUN
export BENCH_DRAM
