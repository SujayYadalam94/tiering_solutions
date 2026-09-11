#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

measurement_init_platform_from_args "$@" || exit 1
ARGS=("${MEASUREMENT_REMAINING_ARGS[@]}")

SIZE_MIB=${ARGS[0]:-}
RUN_ID=${ARGS[1]:-}
LIB_SUFFIX=${ARGS[2]:-}
if [[ -z "${SIZE_MIB}" || -z "${RUN_ID}" ]]; then
    echo "Usage: $0 [--platform c220g5|gsl_optane] <sizeMiB> <runNumber> [libSuffix] [workloadId ...]" >&2
    exit 1
fi

WORKLOAD_ARGS=("${ARGS[@]:3}")
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}" logs "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/arms"

function run_workload {
    local run=$1
    local time_basename="${SIZE_MIB}MiB_run${run}"
    local time_dir="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/arms/${WORKLOAD_OUTPUT}"
    local log_dir="${SCRIPT_DIR}/logs/${WORKLOAD_OUTPUT}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_output_path="${log_dir}/${time_basename}_arms.log"
    local offcore_metrics_file="${log_dir}/${time_basename}_arms_offcore_write_l3_metrics.log"
    local max_dram_file="${time_dir}/max_dram_hugepages_${time_basename}.log"
    local model_path="${SCRIPT_DIR}/libraries/${MEASUREMENT_LIBRARY_PROFILE_DIR}/libhemem-arms${LIB_SUFFIX}.so"

    mkdir -p "${time_dir}" "${log_dir}"

    if [[ ! -f "${model_path}" ]]; then
        echo "Skipping ${WORKLOAD_OUTPUT} run ${run}: missing ${model_path}"
        return
    fi

    cleanup_measurement_outputs "${time_file}" "${log_output_path}" "${max_dram_file}"
    rm -f "${offcore_metrics_file}"
    cleanup_offcore_write_l3_metrics_log
    run_preloaded_measurement "${WORKLOAD_COMMAND}" "${model_path}" "${log_output_path}" "${time_file}" \
        "${NUMA_MEM_NODES}" "${TASKSET_CPUS}"
    local status=$?

    local timed_out=0
    if [[ -f "${time_file}" ]]; then
        timed_out=$(awk -F= '/^timed_out=/{print $2}' "${time_file}" | tail -n1)
    fi

    if [[ "${timed_out}" == "1" ]]; then
        echo "WARNING: ${WORKLOAD_OUTPUT} run ${run} timed out after ${MEASUREMENT_TIMEOUT_SECONDS}s"
    elif [[ ${status} -ne 0 ]]; then
        echo "WARNING: ${WORKLOAD_OUTPUT} run ${run} failed with status ${status}"
    fi
    move_max_dram_log_if_present "${max_dram_file}"
    copy_offcore_write_l3_metrics_log_if_present "${offcore_metrics_file}"
}

for workload_id in "${WORKLOAD_IDS[@]}"; do
    measurement_load_workload "${workload_id}" || exit 1
    if ! measurement_workload_supports_system arms; then
        echo "Skipping ${workload_id}: not supported by ARMS"
        continue
    fi

    echo "Running workload: ${WORKLOAD_ID}"
    run_workload "${RUN_ID}"
done

#
mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/arms"