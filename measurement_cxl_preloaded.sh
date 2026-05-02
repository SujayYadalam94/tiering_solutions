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
LIBRARY_ARG=libhemem-arms_nomigrations.so
WORKLOAD_START_INDEX=2
if [[ -z "${SIZE_MIB}" || -z "${RUN_ID}" ]]; then
    echo "Usage: $0 [--platform c220g5|gsl_optane] <sizeMiB> <runNumber> [libraryPath|libraryName] [workloadId ...]" >&2
    exit 1
fi

NUMA_MEM_NODE=${NUMA_MEM_NODE:-${NUMA_MEM_NODES##*,}}

function resolve_library_path {
    local library_arg=$1
    local candidate

    if [[ "${library_arg}" == */* ]]; then
        candidate=${library_arg}
    else
        candidate="${SCRIPT_DIR}/libraries/${MEASUREMENT_LIBRARY_PROFILE_DIR}/${library_arg}"
    fi

    if [[ ! -f "${candidate}" && -f "${candidate}.so" ]]; then
        candidate="${candidate}.so"
    fi

    printf '%s\n' "${candidate}"
}

function third_arg_is_library {
    local arg=$1
    local workload_file
    local resolved_library_path

    if [[ -z "${arg}" ]]; then
        return 1
    fi

    workload_file=$(measurement_workload_file "${arg}")
    if [[ -f "${workload_file}" ]]; then
        return 1
    fi

    if [[ "${arg}" == */* || "${arg}" == *.so || "${arg}" == libhemem-* ]]; then
        return 0
    fi

    resolved_library_path=$(resolve_library_path "${arg}")
    [[ -f "${resolved_library_path}" ]]
}

if [[ ${#ARGS[@]} -ge 3 ]] && third_arg_is_library "${ARGS[2]}"; then
    LIBRARY_ARG=${ARGS[2]}
    WORKLOAD_START_INDEX=3
fi

LIBRARY_PATH=$(resolve_library_path "${LIBRARY_ARG}")
if [[ ! -f "${LIBRARY_PATH}" ]]; then
    echo "ERROR: custom library not found: ${LIBRARY_PATH}" >&2
    exit 1
fi

LIBRARY_TAG=$(basename "${LIBRARY_PATH}")
LIBRARY_TAG=${LIBRARY_TAG%.so}

WORKLOAD_ARGS=("${ARGS[@]:$WORKLOAD_START_INDEX}")
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}" logs "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/cxl_preloaded"

function run_workload {
    local run=$1
    local time_basename="${SIZE_MIB}MiB_run${run}_${LIBRARY_TAG}"
    local time_dir="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/cxl_preloaded/${WORKLOAD_OUTPUT}"
    local log_dir="${SCRIPT_DIR}/logs/${WORKLOAD_OUTPUT}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_output_path="${log_dir}/${time_basename}_cxl_preloaded.log"
    local max_dram_file="${time_dir}/max_dram_hugepages_${time_basename}.log"

    mkdir -p "${time_dir}" "${log_dir}"

    cleanup_measurement_outputs "${time_file}" "${log_output_path}" "${max_dram_file}"
    local status=0
    local timed_out=0
    local had_errexit=0

    if [[ $- == *e* ]]; then
        had_errexit=1
    fi

    if ! measurement_prepare_workload_runtime; then
        return 1
    fi

    set +e
    {
        time timeout --foreground --signal=TERM \
            --kill-after="${MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS}s" \
            "${MEASUREMENT_TIMEOUT_SECONDS}s" \
            numactl --membind="${NUMA_MEM_NODE}" -- taskset -c "${TASKSET_CPUS}" \
            sudo env ${WORKLOAD_RUNTIME_CHDIR_ARG:+${WORKLOAD_RUNTIME_CHDIR_ARG}} \
            LOG_OUTPUT_PATH="${log_output_path}" \
            VIRTUAL_STEP_SAMPLES="${WORKLOAD_VIRTUAL_STEP_SAMPLES:-3162}" \
            LD_PRELOAD="${LIBRARY_PATH}" \
            ${WORKLOAD_COMMAND} 2>&1
    } 2> "${time_file}"
    status=$?
    measurement_cleanup_workload_runtime
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
        echo "library_path=${LIBRARY_PATH}"
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
        echo "Skipping ${workload_id}: not supported by cxl_preloaded"
        continue
    fi

    echo "Running workload: ${WORKLOAD_ID} with ${LIBRARY_PATH}"
    run_workload "${RUN_ID}"
done

mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/cxl_preloaded"