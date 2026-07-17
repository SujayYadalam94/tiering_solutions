#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads_short.sh"

measurement_init_platform_from_args "$@" || exit 1
PLATFORM_ARGS=(--platform "${MEASUREMENT_PLATFORM}")

echo "Using measurement platform: ${MEASUREMENT_PLATFORM}"

SIZES=(100014)
RUNS=1

mapfile -t WORKLOAD_IDS < <(measurement_list_default_workloads)

for size in "${SIZES[@]}"; do
    echo "== Running measurements with size ${size}MiB =="

    for run in $(seq 2 ${RUNS}); do
        echo "-- Run ${run}/${RUNS} for size ${size}MiB --"

        for workload_id in "${WORKLOAD_IDS[@]}"; do
            echo "---- Workload ${workload_id}: ARMS train then model train ----"

            run_measurement_setup "${size}"
            #"${SCRIPT_DIR}/measurement_arms.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}_train" "_train" "${workload_id}"

            "${SCRIPT_DIR}/measurement_model.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}_train" "_train" "${workload_id}"

            run_measurement_teardown
        done
    done
done
