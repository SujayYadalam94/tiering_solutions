#!/bin/bash
set -uo pipefail

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
    echo "Usage: $0 [--platform c220g5|gsl_optane] <fastTierMiB> <runNumber> [workloadId ...]" >&2
    exit 1
fi
if [[ ! "${SIZE_MIB}" =~ ^[0-9]+$ ]] || ((SIZE_MIB < 1)); then
    echo "ERROR: fastTierMiB must be a positive integer (got '${SIZE_MIB}')" >&2
    exit 1
fi
if [[ ${EUID} -ne 0 ]]; then
    echo "This script must be run using sudo (use: sudo -E $0 ...)" >&2
    exit 1
fi

MEMTIS_LOCK_FILE=${MEMTIS_LOCK_FILE:-/run/lock/measurement_memtis.lock}
if [[ "${MEMTIS_LOCK_HELD:-0}" != "1" ]]; then
    if ! command -v flock >/dev/null 2>&1; then
        echo "ERROR: flock is required to serialize MEMTIS measurements" >&2
        exit 1
    fi
    exec flock --exclusive --nonblock "${MEMTIS_LOCK_FILE}" \
        env MEMTIS_LOCK_HELD=1 bash "$0" "$@"
fi

MEMTIS_ROOT=${MEMTIS_ROOT:-/users/zimooo2/memtis/memtis-userspace}
MEMTIS_BIN_DIR="${MEMTIS_ROOT}/bin"
MEMTIS_LAUNCHER="${MEMTIS_BIN_DIR}/launch_bench"
MEMTIS_KILL_SAMPLER="${MEMTIS_BIN_DIR}/kill_ksampled"
MEMTIS_UNCORE_SCRIPT=${MEMTIS_UNCORE_SCRIPT:-"${MEMTIS_ROOT}/scripts/set_uncore_freq.sh"}
MEMTIS_UNCORE_PACKAGE_DIR=${MEMTIS_UNCORE_PACKAGE_DIR:-/sys/devices/system/cpu/intel_uncore_frequency/package_01_die_00}
# Match setup.sh's package-1 MSR value 0x707 used by ARMS/model: both the
# minimum and maximum uncore ratios are 7 (700 MHz).
MEMTIS_UNCORE_FREQ_KHZ=${MEMTIS_UNCORE_FREQ_KHZ:-700000}
MEMTIS_SYSFS_ROOT=${MEMTIS_SYSFS_ROOT:-/sys/kernel/mm/htmm}
MEMTIS_CGROUP_ROOT=${MEMTIS_CGROUP_ROOT:-/sys/fs/cgroup}
MEMTIS_CGROUP_NAME=${MEMTIS_CGROUP_NAME:-htmm}
MEMTIS_FAST_NODE=${MEMTIS_FAST_NODE:-0}
MEMTIS_INITIAL_PLACEMENT=${MEMTIS_INITIAL_PLACEMENT:-slow}
MEMTIS_OUTPUT_NAME=${MEMTIS_OUTPUT_NAME:-memtis}
# MEMTIS allocates 33 perf-ring pages for each of three events on 20 CPUs
# before the workload starts (about 8 MiB). Keep enough additional room for
# node watermarks, perf metadata, and node-local kernel allocations so HTMM's
# __GFP_THISNODE fault path does not become trapped in reclaim/OOM retries.
MEMTIS_STARTUP_HEADROOM_MIB=${MEMTIS_STARTUP_HEADROOM_MIB:-256}
MEMTIS_MIN_FAST_TIER_MIB=${MEMTIS_MIN_FAST_TIER_MIB:-128}
MEMTIS_EFFECTIVE_FAST_TIER_MIB=0
MEMTIS_FAST_NODE_FREE_MIB=0

if [[ ! "${MEMTIS_STARTUP_HEADROOM_MIB}" =~ ^[0-9]+$ ]]; then
    echo "ERROR: MEMTIS_STARTUP_HEADROOM_MIB must be a non-negative integer" >&2
    exit 1
fi
if [[ ! "${MEMTIS_MIN_FAST_TIER_MIB}" =~ ^[0-9]+$ ]] || ((MEMTIS_MIN_FAST_TIER_MIB < 101)); then
    echo "ERROR: MEMTIS_MIN_FAST_TIER_MIB must be at least 101 MiB" >&2
    echo "MEMTIS has a 100 MiB minimum promotion watermark." >&2
    exit 1
fi
if [[ ! "${MEMTIS_UNCORE_FREQ_KHZ}" =~ ^[0-9]+$ ]] || ((MEMTIS_UNCORE_FREQ_KHZ < 1)); then
    echo "ERROR: MEMTIS_UNCORE_FREQ_KHZ must be a positive integer in kHz" >&2
    exit 1
fi

case "${MEASUREMENT_PLATFORM}" in
    c220g5)
        MEMTIS_CXL_MODE=${MEMTIS_CXL_MODE:-enabled}
        MEMTIS_SLOW_NODE=${MEMTIS_SLOW_NODE:-1}
        ;;
    gsl_optane)
        MEMTIS_CXL_MODE=${MEMTIS_CXL_MODE:-disabled}
        MEMTIS_SLOW_NODE=${MEMTIS_SLOW_NODE:-2}
        ;;
esac

if [[ "${MEMTIS_CXL_MODE}" != "enabled" && "${MEMTIS_CXL_MODE}" != "disabled" ]]; then
    echo "ERROR: MEMTIS_CXL_MODE must be enabled or disabled" >&2
    exit 1
fi
if [[ "${MEMTIS_INITIAL_PLACEMENT}" != "slow" &&
      "${MEMTIS_INITIAL_PLACEMENT}" != "near" &&
      "${MEMTIS_INITIAL_PLACEMENT}" != "artifact" ]]; then
    echo "ERROR: MEMTIS_INITIAL_PLACEMENT must be slow, near, or artifact" >&2
    exit 1
fi
if [[ ! "${MEMTIS_SLOW_NODE}" =~ ^[0-9]+$ ]]; then
    echo "ERROR: MEMTIS_SLOW_NODE must be a non-negative NUMA node number" >&2
    exit 1
fi
if [[ "${MEMTIS_INITIAL_PLACEMENT}" == "slow" ]] && ! command -v numactl >/dev/null 2>&1; then
    echo "ERROR: numactl is required for MEMTIS_INITIAL_PLACEMENT=slow" >&2
    exit 1
fi
if [[ ! -d "${MEMTIS_ROOT}" ]]; then
    echo "ERROR: MEMTIS userspace directory not found: ${MEMTIS_ROOT}" >&2
    exit 1
fi
if [[ ! -d "${MEMTIS_SYSFS_ROOT}" ]]; then
    echo "ERROR: ${MEMTIS_SYSFS_ROOT} is missing; boot the CONFIG_HTMM kernel first" >&2
    exit 1
fi
if [[ ! -w "${MEMTIS_CGROUP_ROOT}/cgroup.subtree_control" ]]; then
    echo "ERROR: cgroup v2 is not writable at ${MEMTIS_CGROUP_ROOT}" >&2
    echo "Run this measurement from the host, not a container with a read-only cgroup mount." >&2
    exit 1
fi

if [[ ! -x "${MEMTIS_LAUNCHER}" || ! -x "${MEMTIS_KILL_SAMPLER}" ]]; then
    echo "Building MEMTIS userspace launchers in ${MEMTIS_ROOT}"
    make -C "${MEMTIS_ROOT}" || exit 1
fi

WORKLOAD_ARGS=("${ARGS[@]:2}")
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

TIME_ROOT="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/${MEMTIS_OUTPUT_NAME}"
if [[ "${MEMTIS_OUTPUT_NAME}" == "memtis" ]]; then
    # Preserve the established output layout for the existing MEMTIS variant.
    LOG_ROOT="${SCRIPT_DIR}/logs"
else
    # Keep variant output together under its times/<platform>/<variant> tree.
    LOG_ROOT="${TIME_ROOT}"
fi
mkdir -p "${TIME_ROOT}" "${LOG_ROOT}"

MEMTIS_CGROUP_DIR="${MEMTIS_CGROUP_ROOT}/${MEMTIS_CGROUP_NAME}"
MEMTIS_CGROUP_ENABLED=0
MEMTIS_STATS_PID=
MEMTIS_LAUNCH_IN_PROGRESS=0
MEMTIS_UNCORE_CONFIGURED=0

write_value() {
    local path=$1
    local value=$2

    if [[ ! -e "${path}" ]]; then
        echo "ERROR: required MEMTIS interface is missing: ${path}" >&2
        return 1
    fi
    if ! printf '%s\n' "${value}" > "${path}"; then
        echo "ERROR: failed to write '${value}' to ${path}" >&2
        return 1
    fi
}

configure_memtis_uncore() {
    local mode=off
    local expected_min_khz
    local expected_max_khz
    local initial_min_file="${MEMTIS_UNCORE_PACKAGE_DIR}/initial_min_freq_khz"
    local initial_max_file="${MEMTIS_UNCORE_PACKAGE_DIR}/initial_max_freq_khz"

    if [[ ! -f "${MEMTIS_UNCORE_SCRIPT}" ]]; then
        echo "ERROR: MEMTIS uncore-frequency script not found: ${MEMTIS_UNCORE_SCRIPT}" >&2
        return 1
    fi

    if [[ "${MEMTIS_CXL_MODE}" == "enabled" ]]; then
        mode=on
        expected_min_khz=${MEMTIS_UNCORE_FREQ_KHZ}
        expected_max_khz=${MEMTIS_UNCORE_FREQ_KHZ}
    else
        if [[ ! -r "${initial_min_file}" || ! -r "${initial_max_file}" ]]; then
            echo "ERROR: initial uncore-frequency controls are missing under ${MEMTIS_UNCORE_PACKAGE_DIR}" >&2
            return 1
        fi
        expected_min_khz=$(<"${initial_min_file}")
        expected_max_khz=$(<"${initial_max_file}")
    fi

    echo "Applying MEMTIS artifact uncore-frequency mode: ${mode}"
    # Mark this before invoking the script so EXIT cleanup also restores the
    # defaults if the script changes only one limit and then fails.
    MEMTIS_UNCORE_CONFIGURED=1
    MEMTIS_UNCORE_FREQ_KHZ="${MEMTIS_UNCORE_FREQ_KHZ}" \
        bash "${MEMTIS_UNCORE_SCRIPT}" "${mode}" || return 1

    local min_file="${MEMTIS_UNCORE_PACKAGE_DIR}/min_freq_khz"
    local max_file="${MEMTIS_UNCORE_PACKAGE_DIR}/max_freq_khz"
    local actual_min_khz
    local actual_max_khz
    if [[ ! -r "${min_file}" || ! -r "${max_file}" ]]; then
        echo "ERROR: MEMTIS uncore-frequency controls are missing under ${MEMTIS_UNCORE_PACKAGE_DIR}" >&2
        return 1
    fi
    actual_min_khz=$(<"${min_file}")
    actual_max_khz=$(<"${max_file}")
    if [[ "${actual_min_khz}" != "${expected_min_khz}" || "${actual_max_khz}" != "${expected_max_khz}" ]]; then
        echo "ERROR: package-1 uncore frequency is ${actual_min_khz}-${actual_max_khz} kHz; expected ${expected_min_khz}-${expected_max_khz} kHz" >&2
        return 1
    fi
    echo "MEMTIS package-1 uncore frequency: ${actual_min_khz}-${actual_max_khz} kHz"
}

configure_memtis() {
    # Values match memtis-userspace/scripts/run_bench.sh from the MEMTIS artifact.
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_sample_period" 199 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_inst_sample_period" 100007 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_thres_hot" 1 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_split_period" 2 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_adaptation_period" 100000 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_cooling_period" 2000000 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_mode" 2 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_demotion_period_in_ms" 500 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_promotion_period_in_ms" 500 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_gamma" 4 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/ksampled_soft_cpu_quota" 30 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_thres_split" 1 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_nowarm" 0 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_cxl_mode" "${MEMTIS_CXL_MODE}" || return 1

    write_value /proc/sys/kernel/numa_balancing 0 || return 1
    # setup.sh disables perf's dynamic throttling. The kernel rejects sample-rate
    # changes while it is disabled, so restore the default before applying the
    # sample-rate value used by the MEMTIS artifact.
    write_value /proc/sys/kernel/perf_cpu_time_max_percent 25 || return 1
    write_value /proc/sys/kernel/perf_event_max_sample_rate 100000 || return 1
    write_value /sys/kernel/mm/transparent_hugepage/enabled always || return 1
    write_value /sys/kernel/mm/transparent_hugepage/defrag always || return 1
}

configure_memtis_cgroup() {
    # Enable the controllers before creating the child cgroup, as in the artifact.
    write_value "${MEMTIS_CGROUP_ROOT}/cgroup.subtree_control" +memory || return 1
    write_value "${MEMTIS_CGROUP_ROOT}/cgroup.subtree_control" +cpuset || return 1
    mkdir -p "${MEMTIS_CGROUP_DIR}" || return 1

    local htmm_enabled="${MEMTIS_CGROUP_DIR}/memory.htmm_enabled"
    local node_limit="${MEMTIS_CGROUP_DIR}/memory.max_at_node${MEMTIS_FAST_NODE}"
    if [[ ! -e "${htmm_enabled}" || ! -e "${node_limit}" ]]; then
        echo "ERROR: MEMTIS cgroup interfaces are missing under ${MEMTIS_CGROUP_DIR}" >&2
        return 1
    fi

    # Children inherit this cgroup and therefore the HTMM fast-tier limit.
    write_value "${MEMTIS_CGROUP_DIR}/cgroup.procs" "$$" || return 1
    write_value "${htmm_enabled}" enabled || return 1
    MEMTIS_CGROUP_ENABLED=1
    write_value "${node_limit}" "$((MEMTIS_EFFECTIVE_FAST_TIER_MIB * 1024 * 1024))" || return 1
}

recover_stale_memtis_cgroup() {
    local events_file="${MEMTIS_CGROUP_DIR}/cgroup.events"
    local kill_file="${MEMTIS_CGROUP_DIR}/cgroup.kill"
    local htmm_enabled="${MEMTIS_CGROUP_DIR}/memory.htmm_enabled"
    local populated=0
    local attempt

    if [[ ! -d "${MEMTIS_CGROUP_DIR}" || ! -r "${events_file}" ]]; then
        return 0
    fi
    populated=$(awk '$1 == "populated" {print $2}' "${events_file}")
    if [[ "${populated}" != "1" ]]; then
        return 0
    fi
    if [[ ! -w "${kill_file}" ]]; then
        echo "ERROR: ${MEMTIS_CGROUP_DIR} contains tasks from an abandoned run and ${kill_file} is unavailable" >&2
        return 1
    fi

    echo "Recovering abandoned MEMTIS cgroup tasks before sizing the fast tier"
    if [[ -e "${htmm_enabled}" ]]; then
        write_value "${htmm_enabled}" disabled || return 1
    fi
    write_value "${kill_file}" 1 || return 1
    for attempt in {1..50}; do
        populated=$(awk '$1 == "populated" {print $2}' "${events_file}")
        if [[ "${populated}" != "1" ]]; then
            return 0
        fi
        sleep 0.1
    done

    echo "ERROR: ${MEMTIS_CGROUP_DIR} remained populated after stale-task cleanup" >&2
    return 1
}

select_fast_tier_capacity() {
    local node_meminfo="/sys/devices/system/node/node${MEMTIS_FAST_NODE}/meminfo"
    local total_mib
    local free_mib
    local available_mib

    if [[ ! -r "${node_meminfo}" ]]; then
        echo "ERROR: NUMA node ${MEMTIS_FAST_NODE} does not exist" >&2
        return 1
    fi
    total_mib=$(awk '$3 == "MemTotal:" {print int($4 / 1024)}' "${node_meminfo}")
    free_mib=$(awk '$3 == "MemFree:" {print int($4 / 1024)}' "${node_meminfo}")
    if [[ -z "${total_mib}" || -z "${free_mib}" ]]; then
        echo "ERROR: could not read memory capacity for NUMA node ${MEMTIS_FAST_NODE}" >&2
        return 1
    fi
    if ((free_mib <= MEMTIS_STARTUP_HEADROOM_MIB)); then
        echo "ERROR: node ${MEMTIS_FAST_NODE} has only ${free_mib} MiB free; MEMTIS needs more than ${MEMTIS_STARTUP_HEADROOM_MIB} MiB to initialize and run" >&2
        return 1
    fi

    available_mib=$((free_mib - MEMTIS_STARTUP_HEADROOM_MIB))
    MEMTIS_EFFECTIVE_FAST_TIER_MIB=${SIZE_MIB}
    if ((MEMTIS_EFFECTIVE_FAST_TIER_MIB > available_mib)); then
        MEMTIS_EFFECTIVE_FAST_TIER_MIB=${available_mib}
    fi
    if ((MEMTIS_EFFECTIVE_FAST_TIER_MIB < MEMTIS_MIN_FAST_TIER_MIB)); then
        echo "ERROR: only ${MEMTIS_EFFECTIVE_FAST_TIER_MIB} MiB remains for MEMTIS after reserving startup memory;" >&2
        echo "the safe minimum fast-tier size is ${MEMTIS_MIN_FAST_TIER_MIB} MiB" >&2
        return 1
    fi

    MEMTIS_FAST_NODE_FREE_MIB=${free_mib}
    echo "MEMTIS requested fast tier: ${SIZE_MIB} MiB"
    echo "MEMTIS node-${MEMTIS_FAST_NODE} free memory at startup: ${free_mib} MiB"
    echo "MEMTIS startup buffer: ${MEMTIS_STARTUP_HEADROOM_MIB} MiB"
    echo "MEMTIS effective fast-tier limit: ${MEMTIS_EFFECTIVE_FAST_TIER_MIB} MiB"
    if ((MEMTIS_EFFECTIVE_FAST_TIER_MIB < SIZE_MIB)); then
        echo "WARNING: reduced MEMTIS fast tier from ${SIZE_MIB} MiB to ${MEMTIS_EFFECTIVE_FAST_TIER_MIB} MiB to fit current free memory" >&2
    fi
}

collect_memtis_stats() {
    local memory_stat_file=$1
    local hotness_stat_file=$2
    local migration_stat_file=$3

    while :; do
        grep -E '^(anon|anon_thp) ' "${MEMTIS_CGROUP_DIR}/memory.stat" >> "${memory_stat_file}" 2>/dev/null || true
        if [[ -r "${MEMTIS_CGROUP_DIR}/memory.hotness_stat" ]]; then
            cat "${MEMTIS_CGROUP_DIR}/memory.hotness_stat" >> "${hotness_stat_file}" 2>/dev/null || true
        fi
        grep '^pgmigrate_success' /proc/vmstat >> "${migration_stat_file}" 2>/dev/null || true
        sleep 1
    done
}

stop_memtis_stats() {
    if [[ -n "${MEMTIS_STATS_PID}" ]]; then
        kill "${MEMTIS_STATS_PID}" 2>/dev/null || true
        wait "${MEMTIS_STATS_PID}" 2>/dev/null || true
        MEMTIS_STATS_PID=
    fi
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

cleanup_memtis() {
    local status=$?

    stop_memtis_stats
    measurement_cleanup_workload_runtime || true
    if [[ ${MEMTIS_LAUNCH_IN_PROGRESS} -eq 1 && -x "${MEMTIS_KILL_SAMPLER}" ]]; then
        "${MEMTIS_KILL_SAMPLER}" >/dev/null 2>&1 || true
    fi
    if [[ ${MEMTIS_CGROUP_ENABLED} -eq 1 && -e "${MEMTIS_CGROUP_DIR}/memory.htmm_enabled" ]]; then
        printf '%s\n' disabled > "${MEMTIS_CGROUP_DIR}/memory.htmm_enabled" || true
    fi
    if [[ ${MEMTIS_UNCORE_CONFIGURED} -eq 1 && -f "${MEMTIS_UNCORE_SCRIPT}" ]]; then
        MEMTIS_UNCORE_FREQ_KHZ="${MEMTIS_UNCORE_FREQ_KHZ}" \
            bash "${MEMTIS_UNCORE_SCRIPT}" off >/dev/null 2>&1 || true
        MEMTIS_UNCORE_CONFIGURED=0
    fi

    return "${status}"
}
trap cleanup_memtis EXIT

recover_stale_memtis_cgroup || exit 1
select_fast_tier_capacity || exit 1
configure_memtis_uncore || exit 1
configure_memtis || exit 1
configure_memtis_cgroup || exit 1

run_workload() {
    local run=$1
    # Keep result names comparable with the other systems: label the run by
    # the requested experiment size even when MEMTIS must use a smaller limit.
    local time_basename="${SIZE_MIB}MiB_run${run}"
    local time_dir="${TIME_ROOT}/${WORKLOAD_OUTPUT}"
    local log_dir="${LOG_ROOT}/${WORKLOAD_OUTPUT}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_file="${log_dir}/${time_basename}_${MEMTIS_OUTPUT_NAME}.log"
    local before_vmstat="${time_dir}/${time_basename}_before_vmstat.log"
    local after_vmstat="${time_dir}/${time_basename}_after_vmstat.log"
    local memory_stat_file="${time_dir}/${time_basename}_memory_stat.log"
    local hotness_stat_file="${time_dir}/${time_basename}_hotness_stat.log"
    local migration_stat_file="${time_dir}/${time_basename}_pgmig.log"

    mkdir -p "${time_dir}" "${log_dir}"
    rm -f "${time_file}" "${log_file}" "${before_vmstat}" "${after_vmstat}" \
        "${memory_stat_file}" "${hotness_stat_file}" "${migration_stat_file}"

    local env_parse_output
    env_parse_output=$(extract_leading_env_assignments "${WORKLOAD_COMMAND}")
    local env_kv
    env_kv=$(printf '%s' "${env_parse_output}" | sed -n '1p')
    local command_text
    command_text=$(printf '%s' "${env_parse_output}" | sed -n '2p')
    local -a env_assignments=()
    if [[ -n "${env_kv// }" ]]; then
        # shellcheck disable=SC2206
        env_assignments=(${env_kv})
    fi

    eval "set -- ${command_text}"
    local -a command_argv=("$@")
    if [[ ${#command_argv[@]} -eq 0 ]]; then
        echo "ERROR: workload command is empty for ${WORKLOAD_OUTPUT}" >&2
        return 1
    fi

    local -a memory_policy_argv=()
    local memory_policy_text=""
    if [[ "${MEMTIS_INITIAL_PLACEMENT}" == "slow" ]]; then
        # The MEMTIS fault allocator explicitly honors MPOL_PREFERRED but
        # bypasses MPOL_BIND. Prefer the slow node so new anonymous pages
        # start remotely while retaining normal allocation fallback.
        memory_policy_argv=(numactl --preferred="${MEMTIS_SLOW_NODE}" --)
        memory_policy_text=" numactl --preferred=${MEMTIS_SLOW_NODE} --"
    fi

    measurement_prepare_workload_runtime || return 1
    grep -E '^(thp|htmm|pgmig)' /proc/vmstat > "${before_vmstat}" || true
    sync
    printf '%s\n' 3 > /proc/sys/vm/drop_caches || return 1
    collect_memtis_stats "${memory_stat_file}" "${hotness_stat_file}" "${migration_stat_file}" &
    MEMTIS_STATS_PID=$!

    echo "Running ${WORKLOAD_OUTPUT} with MEMTIS (fast tier: ${MEMTIS_EFFECTIVE_FAST_TIER_MIB} MiB, requested: ${SIZE_MIB} MiB, CXL mode: ${MEMTIS_CXL_MODE}, initial placement: ${MEMTIS_INITIAL_PLACEMENT})"
    echo "taskset -c ${TASKSET_CPUS} ${MEMTIS_LAUNCHER} stdbuf -oL -eL env${env_kv}${memory_policy_text} ${command_text}"

    local status=0
    local timed_out=0
    local start_epoch_s
    local end_epoch_s
    start_epoch_s=$(date +%s)

    set +e
    MEMTIS_LAUNCH_IN_PROGRESS=1
    {
        time timeout --signal=TERM \
            --kill-after="${MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS}s" \
            "${MEASUREMENT_TIMEOUT_SECONDS}s" \
            taskset -c "${TASKSET_CPUS}" \
            "${MEMTIS_LAUNCHER}" stdbuf -oL -eL env \
            ${WORKLOAD_RUNTIME_CHDIR_ARG:+${WORKLOAD_RUNTIME_CHDIR_ARG}} \
            "${env_assignments[@]}" "${memory_policy_argv[@]}" "${command_argv[@]}" 2>&1
    } > >(tee "${log_file}") 2> "${time_file}"
    status=$?
    MEMTIS_LAUNCH_IN_PROGRESS=0

    end_epoch_s=$(date +%s)
    stop_memtis_stats
    measurement_cleanup_workload_runtime
    grep -E '^(thp|htmm|pgmig)' /proc/vmstat > "${after_vmstat}" || true

    if [[ ${status} -eq 124 || ${status} -eq 137 ]]; then
        timed_out=1
        # timeout may terminate the launcher before it can stop ksamplingd.
        "${MEMTIS_KILL_SAMPLER}" >/dev/null 2>&1 || true
    fi
    {
        echo "exit_status=${status}"
        echo "timed_out=${timed_out}"
        echo "elapsed_seconds=$((end_epoch_s - start_epoch_s))"
        echo "timeout_seconds=${MEASUREMENT_TIMEOUT_SECONDS}"
        echo "kill_after_seconds=${MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS}"
        echo "memtis_fast_tier_mib=${SIZE_MIB}"
        echo "memtis_effective_fast_tier_mib=${MEMTIS_EFFECTIVE_FAST_TIER_MIB}"
        echo "memtis_fast_node_free_at_start_mib=${MEMTIS_FAST_NODE_FREE_MIB}"
        echo "memtis_startup_headroom_mib=${MEMTIS_STARTUP_HEADROOM_MIB}"
        echo "memtis_min_fast_tier_mib=${MEMTIS_MIN_FAST_TIER_MIB}"
        echo "memtis_fast_node=${MEMTIS_FAST_NODE}"
        echo "memtis_cxl_mode=${MEMTIS_CXL_MODE}"
        echo "memtis_uncore_freq_khz=${MEMTIS_UNCORE_FREQ_KHZ}"
        echo "memtis_initial_placement=${MEMTIS_INITIAL_PLACEMENT}"
        echo "memtis_slow_node=${MEMTIS_SLOW_NODE}"
    } >> "${time_file}"

    if [[ ${timed_out} -eq 1 ]]; then
        echo "WARNING: ${WORKLOAD_OUTPUT} run ${run} timed out after ${MEASUREMENT_TIMEOUT_SECONDS}s" >&2
    elif [[ ${status} -ne 0 ]]; then
        echo "WARNING: ${WORKLOAD_OUTPUT} run ${run} failed with status ${status}" >&2
    fi

    return "${status}"
}

declare -a FAILED_RUNS=()
for workload_id in "${WORKLOAD_IDS[@]}"; do
    measurement_load_workload "${workload_id}" || exit 1
    if ! measurement_workload_supports_system memtis && ! measurement_workload_supports_system arms; then
        echo "Skipping ${workload_id}: not supported by MEMTIS"
        continue
    fi

    if ! run_workload "${RUN_ID}"; then
        FAILED_RUNS+=("${WORKLOAD_OUTPUT}:run${RUN_ID}")
    fi
done

if [[ ${#FAILED_RUNS[@]} -gt 0 ]]; then
    echo "MEMTIS completed with failures:" >&2
    printf '  - %s\n' "${FAILED_RUNS[@]}" >&2
    exit 1
fi

echo "All MEMTIS workloads completed successfully."
