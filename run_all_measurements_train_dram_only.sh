#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

SIZES=(100000)
RUNS=1

RUN_LABEL_SUFFIX=${RUN_LABEL_SUFFIX:-_train}
COLLECT_TIMESTAMP=$(date '+%Y%m%d_%H%M%S')
COLLECT_LOGS_DIR=${COLLECT_LOGS_DIR:-"${SCRIPT_DIR}/collected_logs/dram_only_train_${COLLECT_TIMESTAMP}"}

copy_if_present() {
    local source_file=$1
    local destination_dir=$2

    if [[ -f "${source_file}" ]]; then
        cp -f "${source_file}" "${destination_dir}/"
    else
        echo "WARNING: missing artifact ${source_file}"
    fi
}

collect_dram_only_artifacts() {
    local size_mib=$1
    local run_label=$2
    local workload_output=$3
    local basename="${size_mib}MiB_run${run_label}"
    local source_time_dir="${SCRIPT_DIR}/times/dram_only/${workload_output}"
    local source_log_dir="${SCRIPT_DIR}/logs/${workload_output}"
    local destination_dir="${COLLECT_LOGS_DIR}/${workload_output}"

    mkdir -p "${destination_dir}"

    copy_if_present "${source_time_dir}/${basename}.time" "${destination_dir}"
    copy_if_present "${source_time_dir}/max_dram_hugepages_${basename}.log" "${destination_dir}"
    copy_if_present "${source_log_dir}/${basename}_dram_only.log" "${destination_dir}"
}

if [[ ! -x "${SCRIPT_DIR}/measurement_dram_only.sh" ]]; then
    echo "ERROR: ./measurement_dram_only.sh not found or not executable"
    exit 1
fi

mkdir -p "${COLLECT_LOGS_DIR}"
mapfile -t WORKLOAD_IDS < <(measurement_list_default_workloads)

for size in "${SIZES[@]}"; do
    echo "== Running DRAM-only train measurements with size ${size}MiB =="

    for run in $(seq 1 "${RUNS}"); do
        run_label="${run}${RUN_LABEL_SUFFIX}"
        echo "-- Run ${run}/${RUNS} for size ${size}MiB (label ${run_label}) --"

        for workload_id in "${WORKLOAD_IDS[@]}"; do
            measurement_load_workload "${workload_id}" || exit 1
            if ! measurement_workload_supports_system arms; then
                echo "Skipping ${workload_id}: not supported by dram_only"
                continue
            fi

            echo "---- Workload ${workload_id}: DRAM only ----"

            run_measurement_setup_baseline_default "${size}"
            "${SCRIPT_DIR}/measurement_dram_only.sh" "${size}" "${run_label}" "${workload_id}"
            collect_dram_only_artifacts "${size}" "${run_label}" "${WORKLOAD_OUTPUT}"
            run_measurement_teardown
        done
    done
done

echo "All DRAM-only train measurements complete"
echo "Collected artifacts directory: ${COLLECT_LOGS_DIR}"