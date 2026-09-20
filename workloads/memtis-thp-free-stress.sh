#!/bin/bash

if [[ $(uname -r) != 5.15.19-htmm-node0-fix10-split-metadata ]]; then
    echo "ERROR: boot the fix10-split-metadata kernel before this stress workload" >&2
    return 1
fi
WORKLOAD_ID=memtis-thp-free-stress
WORKLOAD_OUTPUT=memtis-thp-free-stress
WORKLOAD_COMMAND="${SCRIPT_DIR}/reproductions/memtis-hang-20260917/fix10/thp-free-stress 8 10000"
WORKLOAD_EXE_NAME=thp-free-stress
WORKLOAD_MODEL_BASE=memtis-thp-free-stress
WORKLOAD_SYSTEMS=memtis
