#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

measurement_init_platform_from_args "$@" || exit 1
PLATFORM_ARGS=(--platform "${MEASUREMENT_PLATFORM}")

print_usage() {
    cat <<EOF
Usage: $0 [--platform c220g5|gsl_optane] [workloadId ...]

Runs ARMS near-train measurements (libhemem-arms_near_train.so) and collects logs.
EOF
}

for arg in "${MEASUREMENT_REMAINING_ARGS[@]}"; do
    if [[ "${arg}" == "--help" || "${arg}" == "-h" ]]; then
        print_usage
        exit 0
    fi
done

WORKLOAD_ARGS=("${MEASUREMENT_REMAINING_ARGS[@]}")

SIZES=(100101)
RUNS=1
NUMA_MEM_NODES_OVERRIDE=${NUMA_MEM_NODES_OVERRIDE:-${NUMA_MEM_NODE_OVERRIDE:-0}}

RUN_LABEL_SUFFIX=${RUN_LABEL_SUFFIX:-_train}
ARMS_LIB_SUFFIX=${ARMS_LIB_SUFFIX:-_near_train}
COLLECT_TIMESTAMP=$(date '+%Y%m%d_%H%M%S')
COLLECT_LOGS_DIR=${COLLECT_LOGS_DIR:-"${SCRIPT_DIR}/collected_logs/${MEASUREMENT_PLATFORM}/arms_near_train_${COLLECT_TIMESTAMP}"}
LOG_CONVERTER_SCRIPT=${LOG_CONVERTER_SCRIPT:-"${SCRIPT_DIR}/logs/filter_v3_split_runs.py"}
LOG_CONVERTER_VENV_PYTHON=${LOG_CONVERTER_VENV_PYTHON:-"${SCRIPT_DIR}/../tiering_models/process_data/data/.venv/bin/python"}

echo "Using measurement platform: ${MEASUREMENT_PLATFORM}"
echo "Using NUMA_MEM_NODES override: ${NUMA_MEM_NODES_OVERRIDE}"
echo "Using ARMS library suffix: ${ARMS_LIB_SUFFIX}"

copy_if_present() {
    local source_file=$1
    local destination_dir=$2

    if [[ -f "${source_file}" ]]; then
        cp -f "${source_file}" "${destination_dir}/"
    else
        echo "WARNING: missing artifact ${source_file}"
    fi
}

convert_logs_to_parquet() {
    local workload_output=$1
    local log_dir="${SCRIPT_DIR}/logs/${workload_output}"
    local python_cmd=python3

    if [[ ! -f "${LOG_CONVERTER_SCRIPT}" ]]; then
        echo "ERROR: converter script not found at ${LOG_CONVERTER_SCRIPT}" >&2
        return 1
    fi

    if [[ -x "${LOG_CONVERTER_VENV_PYTHON}" ]]; then
        python_cmd="${LOG_CONVERTER_VENV_PYTHON}"
    fi

    "${python_cmd}" "${LOG_CONVERTER_SCRIPT}" "${log_dir}"
}

collect_arms_artifacts() {
    local size_mib=$1
    local run_label=$2
    local workload_output=$3
    local basename="${size_mib}MiB_run${run_label}"
    local source_time_dir="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/arms/${workload_output}"
    local source_log_dir="${SCRIPT_DIR}/logs/${workload_output}"
    local destination_dir="${COLLECT_LOGS_DIR}/${workload_output}"

    mkdir -p "${destination_dir}"

    copy_if_present "${source_time_dir}/${basename}.time" "${destination_dir}"
    copy_if_present "${source_time_dir}/max_dram_hugepages_${basename}.log" "${destination_dir}"
    copy_if_present "${source_log_dir}/${basename}_arms.parquet" "${destination_dir}"
}

if [[ ! -x "${SCRIPT_DIR}/measurement_arms.sh" ]]; then
    echo "ERROR: ./measurement_arms.sh not found or not executable"
    exit 1
fi

mkdir -p "${COLLECT_LOGS_DIR}"
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

for size in "${SIZES[@]}"; do
    echo "== Running ARMS near-train measurements with size ${size}MiB =="

    for run in $(seq 1 "${RUNS}"); do
        run_label="${run}${RUN_LABEL_SUFFIX}"
        echo "-- Run ${run}/${RUNS} for size ${size}MiB (label ${run_label}) --"


        for workload_id in "${WORKLOAD_IDS[@]}"; do
            run_measurement_setup "${size}" || exit 1
            trap 'run_measurement_teardown' EXIT

            measurement_load_workload "${workload_id}" || exit 1
            if ! measurement_workload_supports_system arms; then
                echo "Skipping ${workload_id}: not supported by arms"
                continue
            fi

            echo "---- Workload ${workload_id}: ARMS near-train ----"

            NUMA_MEM_NODES="${NUMA_MEM_NODES_OVERRIDE}" \
                "${SCRIPT_DIR}/measurement_arms.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run_label}" "${ARMS_LIB_SUFFIX}" "${workload_id}"
            convert_logs_to_parquet "${WORKLOAD_OUTPUT}" || exit 1
            collect_arms_artifacts "${size}" "${run_label}" "${WORKLOAD_OUTPUT}"
        done

        run_measurement_teardown
        trap - EXIT
    done
done

echo "All ARMS near-train measurements complete"
echo "Collected artifacts directory: ${COLLECT_LOGS_DIR}"
