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
if [[ -z "${SIZE_MIB}" || -z "${RUN_ID}" ]]; then
    echo "Usage: $0 [--platform c220g5|gsl_optane] <sizeMiB> <runNumber> [workloadId ...]" >&2
    exit 1
fi

NUMA_MEM_NODE=${NUMA_MEM_NODE:-${NUMA_MEM_NODES##*,}}

WORKLOAD_ARGS=("${ARGS[@]:2}")
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}" logs "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/cxl_only"

function run_workload {
    local run=$1
    local time_basename="${SIZE_MIB}MiB_run${run}"
    local time_dir="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/cxl_only/${WORKLOAD_OUTPUT}"
    local log_dir="${SCRIPT_DIR}/logs/${WORKLOAD_OUTPUT}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_output_path="${log_dir}/${time_basename}_cxl_only.log"
    local max_dram_file="${time_dir}/max_dram_hugepages_${time_basename}.log"

    mkdir -p "${time_dir}" "${log_dir}"

    cleanup_measurement_outputs "${time_file}" "${log_output_path}" "${max_dram_file}"
    local status=0
    local timed_out=0
    local had_errexit=0

    if [[ $- == *e* ]]; then
        had_errexit=1
    fi

    set +e
    /usr/bin/time -o "${time_file}" \
        timeout --foreground --signal=TERM \
            --kill-after="${MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS}s" \
            "${MEASUREMENT_TIMEOUT_SECONDS}s" \
            numactl --membind="${NUMA_MEM_NODE}" -- taskset -c "${TASKSET_CPUS}" \
            sudo \
            LOG_OUTPUT_PATH="${log_output_path}" \
            ${WORKLOAD_COMMAND}
    status=$?
    if [[ ${had_errexit} -eq 1 ]]; then
        set -e
    fi

    if [[ ${status} -eq 124 || ${status} -eq 137 ]]; then
        timed_out=1
    fi
    {
        echo "exit_status=${status}"
        echo "timed_out=${timed_out}"
        echo "timeout_seconds=${MEASUREMENT_TIMEOUT_SECONDS}"
    } >> "${time_file}"

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
        echo "Skipping ${workload_id}: not supported by cxl_only"
        continue
    fi

    echo "Running workload: ${WORKLOAD_ID}"
    run_workload "${RUN_ID}"
done

mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/cxl_only"