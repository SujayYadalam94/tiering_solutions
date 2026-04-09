#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

measurement_init_platform_from_args "$@" || exit 1
ARGS=("${MEASUREMENT_REMAINING_ARGS[@]}")

SIZE_MIB=${ARGS[0]:-}
RUN_ID=${ARGS[1]:-}
PAGE_TYPE=${ARGS[2]:-huge} # regular | huge

if [[ -z "${SIZE_MIB}" || -z "${RUN_ID}" ]]; then
    echo "Usage: $0 [--platform c220g5|gsl_optane] <fastTierMiB> <runNumber> [pageType] [workloadId ...]" >&2
    echo "  pageType: regular (default) | huge" >&2
    exit 1
fi

if [[ "${PAGE_TYPE}" != "regular" && "${PAGE_TYPE}" != "huge" ]]; then
    echo "ERROR: invalid pageType '${PAGE_TYPE}' (expected regular|huge)" >&2
    exit 1
fi

if [[ ${EUID} -ne 0 ]]; then
    echo "This script must be run using sudo (use: sudo -E $0 ...)" >&2
    exit 1
fi

WORKSPACE_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)
NUMA_CPU_NODE=${NUMA_CPU_NODE:-0}
STRICT_FAILURES=${STRICT_FAILURES:-0}

WORKLOAD_ARGS=("${ARGS[@]:3}")
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

declare -a FAILED_RUNS=()

# Convert MiB -> GiB (ceil), because HybridTier runtime takes FAST_MEMORY_SIZE_GB.
FAST_TIER_SIZE_GB=$(( (SIZE_MIB + 1023) / 1024 ))
if [[ ${FAST_TIER_SIZE_GB} -lt 1 ]]; then
    FAST_TIER_SIZE_GB=1
fi

echo "Fast tier size: ${SIZE_MIB} MiB (compiling as ${FAST_TIER_SIZE_GB} GB)"

time_root="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}"
log_root="${SCRIPT_DIR}/logs"
mkdir -p "${time_root}" "${log_root}" "${time_root}/hybridtier"

HYBRIDTIER_ROOT=${HYBRIDTIER_ROOT:-"${WORKSPACE_ROOT}/hybridtier-asplos25-artifact"}
HOOK_DIR="${HYBRIDTIER_ROOT}/hook"
HOOK_SO="${HOOK_DIR}/hook.so"

cleanup_run_outputs() {
    local time_file=$1
    local log_file=$2
    local max_dram_file=$3

    cleanup_measurement_outputs "${time_file}" "${log_file}" "${max_dram_file}"
}

extract_leading_env_assignments() {
    local rest=$1
    local env_kv=""

    while [[ "${rest}" =~ ^([A-Za-z_][A-Za-z0-9_]*)=([^[:space:]]+)[[:space:]]+(.+)$ ]]; do
        env_kv+=" ${BASH_REMATCH[1]}=${BASH_REMATCH[2]}"
        rest="${BASH_REMATCH[3]}"
    done

    printf '%s\n%s\n' "${env_kv}" "${rest}"
}

build_hook() {
    local exe_name=$1

    if [[ ! -d "${HOOK_DIR}" ]]; then
        echo "ERROR: HybridTier hook dir not found: ${HOOK_DIR}" >&2
        exit 1
    fi

    local dpage_define="HYBRIDTIER_REGULAR"
    if [[ "${PAGE_TYPE}" == "huge" ]]; then
        dpage_define="HYBRIDTIER_HUGE"
    fi

    pushd "${HOOK_DIR}" > /dev/null

    # Build exactly like run_exp_common.sh
    g++ -shared -fPIC -g hook.cpp -o hook.so -O3 \
        -ldl -lpthread -lnuma \
        -DFAST_MEMORY_SIZE_GB=${FAST_TIER_SIZE_GB} \
        -DTARGET_EXE_NAME=\"${exe_name}\" \
        -D${dpage_define}

    popd > /dev/null

    if [[ ! -f "${HOOK_SO}" ]]; then
        echo "ERROR: hook build failed; missing ${HOOK_SO}" >&2
        exit 1
    fi
}

run_program() {
    local program_str=$1
    local exe_name=$2
    local output=$3
    local run=$4

    local time_basename="${SIZE_MIB}MiB_run${run}"
    local time_dir="${time_root}/hybridtier/${output}"
    local log_dir="${log_root}/${output}"

    mkdir -p "${time_dir}" "${log_dir}"

    local time_file="${time_dir}/${time_basename}.time"
    local log_file="${log_dir}/${time_basename}_hybridtier.log"
    local max_dram_file="${time_dir}/max_dram_hugepages_${time_basename}.log"

    cleanup_run_outputs "${time_file}" "${log_file}" "${max_dram_file}"

    build_hook "${exe_name}"

    echo "Running ${output} (exe=${exe_name}) run ${run}"

    # Keep compute placement/pinning similar to existing ARM measurements.
    local pin="taskset -c ${TASKSET_CPUS}"
    local numa="/usr/bin/numactl --cpunodebind=${NUMA_CPU_NODE}"

    # Extract leading environment assignments (e.g., "OMP_NUM_THREADS=16 ") so we can
    # apply them without wrapping the workload in an extra shell process.
    local env_parse_output
    env_parse_output=$(extract_leading_env_assignments "${program_str}")
    local env_kv
    env_kv=$(printf '%s' "${env_parse_output}" | sed -n '1p')
    local rest
    rest=$(printf '%s' "${env_parse_output}" | sed -n '2p')

    local -a env_assignments=()
    if [[ -n "${env_kv// }" ]]; then
        # shellcheck disable=SC2206
        env_assignments=(${env_kv})
    fi

    eval "set -- ${rest}"
    local -a cmd_argv=("$@")

    if [[ ${#cmd_argv[@]} -eq 0 ]]; then
        echo "ERROR: workload command is empty for ${output}" >&2
        FAILED_RUNS+=("${output}:run${run}:empty_command")
        if [[ "${STRICT_FAILURES}" == "1" ]]; then
            exit 2
        fi
        return
    fi

    # Disable glob expansion so args like ".*benchmark" are passed literally.

    echo "${pin} ${numa} env LD_PRELOAD=\"${HOOK_SO}\"${env_kv} ${rest}"

    local status=0
    local timed_out=0
    set +e
    set -f
    {
        time timeout --foreground --signal=TERM \
            --kill-after="${MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS}s" \
            "${MEASUREMENT_TIMEOUT_SECONDS}s" \
            ${pin} ${numa} env LD_PRELOAD="${HOOK_SO}" "${env_assignments[@]}" "${cmd_argv[@]}" \
            &>> "${log_file}"
    } 2> "${time_file}"
    status=$?
    set +f
    set -e

    if [[ ${status} -eq 124 || ${status} -eq 137 ]]; then
        timed_out=1
    fi

    {
        echo "exit_status=${status}"
        echo "timed_out=${timed_out}"
        echo "timeout_seconds=${MEASUREMENT_TIMEOUT_SECONDS}"
    } >> "${time_file}"

    if [[ ${status} -ne 0 ]]; then
        if [[ ${timed_out} -eq 1 ]]; then
            echo "WARNING: ${output} run ${run} timed out after ${MEASUREMENT_TIMEOUT_SECONDS}s. See ${log_file}" >&2
            FAILED_RUNS+=("${output}:run${run}:timeout${MEASUREMENT_TIMEOUT_SECONDS}s")
        else
            echo "WARNING: ${output} run ${run} failed with status ${status}. See ${log_file}" >&2
            FAILED_RUNS+=("${output}:run${run}:status${status}")
        fi
        if [[ "${STRICT_FAILURES}" == "1" ]]; then
            exit ${status}
        fi
    fi

    move_max_dram_log_if_present "${max_dram_file}"
}

# ---------------- Workloads (mirrors measurement_arms.sh) ----------------

for workload_id in "${WORKLOAD_IDS[@]}"; do
    measurement_load_workload "${workload_id}" || exit 1
    if ! measurement_workload_supports_system hybridtier; then
        echo "Skipping ${workload_id}: not supported by HybridTier"
        continue
    fi

    echo "Running workload: ${WORKLOAD_ID}"
    run_program "${WORKLOAD_COMMAND}" "${WORKLOAD_EXE_NAME}" "${WORKLOAD_OUTPUT}" "${RUN_ID}"
done

if [[ ${#FAILED_RUNS[@]} -gt 0 ]]; then
    echo ""
    echo "Completed with failures (${#FAILED_RUNS[@]}):" >&2
    for item in "${FAILED_RUNS[@]}"; do
        echo "  - ${item}" >&2
    done
    echo "Set STRICT_FAILURES=1 to stop immediately on first failure." >&2
else
    echo ""
    echo "All workloads completed successfully."
fi
