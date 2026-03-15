#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

SIZES=(4000)
RUNS=5

mapfile -t WORKLOAD_IDS < <(measurement_list_default_workloads)

for size in "${SIZES[@]}"; do
    echo "== Running measurements with size ${size}MiB =="

    for run in $(seq 1 ${RUNS}); do
        echo "-- Run ${run}/${RUNS} for size ${size}MiB --"

        for workload_id in "${WORKLOAD_IDS[@]}"; do
            echo "---- Workload ${workload_id}: ARMS then HybridTier ----"

            run_measurement_setup "${size}"

            "${SCRIPT_DIR}/measurement_arms.sh" "${size}" "${run}" "" "${workload_id}"

            run_measurement_setup "${size}"

            if [[ -x "${SCRIPT_DIR}/measurement_hybridtier.sh" ]]; then
                sudo -E "${SCRIPT_DIR}/measurement_hybridtier.sh" "${size}" "${run}" huge "${workload_id}"
            else
                echo "WARNING: ./measurement_hybridtier.sh not found or not executable; skipping HybridTier"
            fi

            run_measurement_teardown
        done
    done
done
