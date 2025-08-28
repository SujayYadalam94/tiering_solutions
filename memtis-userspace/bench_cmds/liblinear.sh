#!/bin/bash
BENCH_BIN=/mnt/ssd/workloads/liblinear

# anon footprint 79640MB
# file footprint 21581MB

BENCH_RUN="${BENCH_BIN}/train -s 6 -m 20 /mnt/ssd/inputs/liblinear/kddb"
# Liblinear requires a dataset file (kdd12)
# Please refer to memtis-userspace/bench_dir/README.md for downloading this dataset

if [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="2780MB"
fi


export BENCH_RUN
export BENCH_DRAM
