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
RUNS=${RUNS:-1}
# An explicitly empty suffix runs the same model path without training-data logging.
MODEL_LIB_SUFFIX=${MODEL_LIB_SUFFIX-_train}
MODEL_RUN_SUFFIX=${MODEL_RUN_SUFFIX-${MODEL_LIB_SUFFIX}}

mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${MEASUREMENT_REMAINING_ARGS[@]}")

for size in "${SIZES[@]}"; do
    echo "== Running measurements with size ${size}MiB =="

    for run in $(seq 1 "${RUNS}"); do
        echo "-- Run ${run}/${RUNS} for size ${size}MiB --"

        for workload_id in "${WORKLOAD_IDS[@]}"; do
            echo "---- Workload ${workload_id}: model library suffix '${MODEL_LIB_SUFFIX}' ----"

            run_measurement_setup "${size}"
            #"${SCRIPT_DIR}/measurement_arms.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}_train" "_train" "${workload_id}"

            "${SCRIPT_DIR}/measurement_model.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}${MODEL_RUN_SUFFIX}" "${MODEL_LIB_SUFFIX}" "${workload_id}"

            run_measurement_teardown
        done
    done
done
