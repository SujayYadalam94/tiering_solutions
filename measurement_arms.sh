#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

SIZE_MIB=${1:-}
RUN_ID=${2:-}
LIB_SUFFIX=${3:-}
if [[ -z "${SIZE_MIB}" || -z "${RUN_ID}" ]]; then
    echo "Usage: $0 <sizeMiB> <runNumber> [libSuffix]" >&2
    exit 1
fi

BENCH_ROOT=${BENCH_ROOT:-/users/zimooo2}
NUMA_MEM_NODES=${NUMA_MEM_NODES:-0,1}
TASKSET_CPUS=${TASKSET_CPUS:-0-9,20-29}

WORKLOAD_ARGS=("${@:4}")
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

mkdir -p times logs times/arms

function run_workload {
    local run=$1
    local time_basename="${SIZE_MIB}MiB_run${run}"
    local time_dir="${SCRIPT_DIR}/times/arms/${WORKLOAD_OUTPUT}"
    local log_dir="${SCRIPT_DIR}/logs/${WORKLOAD_OUTPUT}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_output_path="${log_dir}/${time_basename}_arms.log"
    local max_dram_file="${time_dir}/max_dram_hugepages_${time_basename}.log"
    local model_path="${SCRIPT_DIR}/libraries/C220G5/libhemem-arms${LIB_SUFFIX}.so"

    mkdir -p "${time_dir}" "${log_dir}"

    if [[ ! -f "${model_path}" ]]; then
        echo "Skipping ${WORKLOAD_OUTPUT} run ${run}: missing ${model_path}"
        return
    fi

    cleanup_measurement_outputs "${time_file}" "${log_output_path}" "${max_dram_file}"
    run_preloaded_measurement "${WORKLOAD_COMMAND}" "${model_path}" "${log_output_path}" "${time_file}" \
        "${NUMA_MEM_NODES}" "${TASKSET_CPUS}"
    local status=$?
    if [[ ${status} -eq 124 || ${status} -eq 137 ]]; then
        echo "WARNING: ${WORKLOAD_OUTPUT} run ${run} timed out after ${MEASUREMENT_TIMEOUT_SECONDS}s"
    elif [[ ${status} -ne 0 ]]; then
        echo "WARNING: ${WORKLOAD_OUTPUT} run ${run} failed with status ${status}"
    fi
    move_max_dram_log_if_present "${max_dram_file}"
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
mkdir -p times/arms