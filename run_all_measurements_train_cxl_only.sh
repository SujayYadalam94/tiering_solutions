#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads_short.sh"

measurement_init_platform_from_args "$@" || exit 1
PLATFORM_ARGS=(--platform "${MEASUREMENT_PLATFORM}")

print_usage() {
    cat <<EOF
Usage: $0 [--platform c220g5|gsl_optane] [workloadId ...]

Runs CXL-only time-based ARMS measurements (libhemem-arms_cxl_train.so) and collects logs.
Allocations stay bound to the platform far-memory NUMA node while ARMS training-data logging remains enabled and migration workers stay disabled.
EOF
}

for arg in "${MEASUREMENT_REMAINING_ARGS[@]}"; do
    if [[ "${arg}" == "--help" || "${arg}" == "-h" ]]; then
        print_usage
        exit 0
    fi
done

WORKLOAD_ARGS=("${MEASUREMENT_REMAINING_ARGS[@]}")

SIZES=(100013)
RUNS=1

case "${MEASUREMENT_PLATFORM}" in
    c220g5)
        DEFAULT_NUMA_MEM_NODES_OVERRIDE=1
        ;;
    gsl_optane)
        DEFAULT_NUMA_MEM_NODES_OVERRIDE=2
        ;;
esac

NUMA_MEM_NODES_OVERRIDE=${NUMA_MEM_NODES_OVERRIDE:-${NUMA_MEM_NODE_OVERRIDE:-${NUMA_MEM_NODE:-${DEFAULT_NUMA_MEM_NODES_OVERRIDE}}}}
RUN_LABEL_SUFFIX=${RUN_LABEL_SUFFIX:-_cxl_train}
ARMS_LIB_SUFFIX=${ARMS_LIB_SUFFIX:-_cxl_train}
COLLECT_TIMESTAMP=$(date '+%Y%m%d_%H%M%S')
COLLECT_LOGS_DIR=${COLLECT_LOGS_DIR:-"${SCRIPT_DIR}/collected_logs/${MEASUREMENT_PLATFORM}/cxl_only_arms_train_${COLLECT_TIMESTAMP}"}

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
    copy_if_present "${source_log_dir}/${basename}_arms.log" "${destination_dir}"
}

if [[ ! -x "${SCRIPT_DIR}/measurement_arms.sh" ]]; then
    echo "ERROR: ./measurement_arms.sh not found or not executable"
    exit 1
fi

mkdir -p "${COLLECT_LOGS_DIR}"
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

for size in "${SIZES[@]}"; do
    echo "== Running CXL-only time-based ARMS measurements with size ${size}MiB =="

    for run in $(seq 1 "${RUNS}"); do
        run_label="${run}${RUN_LABEL_SUFFIX}"
        echo "-- Run ${run}/${RUNS} for size ${size}MiB (label ${run_label}) --"

        for workload_id in "${WORKLOAD_IDS[@]}"; do
            measurement_load_workload "${workload_id}" || exit 1
            if ! measurement_workload_supports_system arms; then
                echo "Skipping ${workload_id}: not supported by cxl_only"
                continue
            fi

            echo "---- Workload ${workload_id}: CXL-only time-based ARMS ----"

            NUMA_MEM_NODES="${NUMA_MEM_NODES_OVERRIDE}" \
                "${SCRIPT_DIR}/measurement_arms.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run_label}" "${ARMS_LIB_SUFFIX}" "${workload_id}"
            collect_arms_artifacts "${size}" "${run_label}" "${WORKLOAD_OUTPUT}"
        done
    done
done

echo "All CXL-only time-based ARMS measurements complete"
echo "Collected artifacts directory: ${COLLECT_LOGS_DIR}"