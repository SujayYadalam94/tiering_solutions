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
    # Keep ownership in flock so an exiting collector's sleep cannot retain it.
    exec flock --exclusive --nonblock --close "${MEMTIS_LOCK_FILE}" \
        env MEMTIS_LOCK_HELD=1 bash "$0" "$@"
fi

MEMTIS_ROOT=${MEMTIS_ROOT:-/users/zimooo2/memtis/memtis-userspace}
MEMTIS_BIN_DIR="${MEMTIS_ROOT}/bin"
MEMTIS_LAUNCHER="${MEMTIS_BIN_DIR}/launch_bench"
MEMTIS_KILL_SAMPLER="${MEMTIS_BIN_DIR}/kill_ksampled"
MEMTIS_SYSFS_ROOT=${MEMTIS_SYSFS_ROOT:-/sys/kernel/mm/htmm}
MEMTIS_CGROUP_ROOT=${MEMTIS_CGROUP_ROOT:-/sys/fs/cgroup}
MEMTIS_CGROUP_NAME=${MEMTIS_CGROUP_NAME:-htmm}
if [[ ! "${MEMTIS_CGROUP_NAME}" =~ ^[a-zA-Z0-9_.-]+$ ||
      "${MEMTIS_CGROUP_NAME}" == . || "${MEMTIS_CGROUP_NAME}" == .. ]]; then
    echo "ERROR: MEMTIS_CGROUP_NAME must name a single child cgroup" >&2
    exit 1
fi
MEMTIS_FAST_NODE=${MEMTIS_FAST_NODE:-0}
if [[ ! "${MEMTIS_FAST_NODE}" =~ ^[0-9]+$ ]]; then
    echo "ERROR: MEMTIS_FAST_NODE must be a non-negative NUMA node number" >&2
    exit 1
fi
if ! command -v numactl >/dev/null 2>&1; then
    echo "ERROR: numactl is required to set the initial memory placement" >&2
    exit 1
fi
MEMTIS_OUTPUT_NAME=${MEMTIS_OUTPUT_NAME:-memtis}
# MEMTIS allocates 33 perf-ring pages for each of three events on 20 CPUs
# before the workload starts (about 8 MiB). Keep enough additional room for
# node watermarks, perf metadata, and node-local kernel allocations so HTMM's
# __GFP_THISNODE fault path does not become trapped in reclaim/OOM retries.
MEMTIS_STARTUP_HEADROOM_MIB=${MEMTIS_STARTUP_HEADROOM_MIB:-512}
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
if [[ ! "${MEMTIS_SLOW_NODE}" =~ ^[0-9]+$ ]]; then
    echo "ERROR: MEMTIS_SLOW_NODE must be a non-negative NUMA node number" >&2
    exit 1
fi
MEMTIS_PREFERRED_NODE=${MEMTIS_PREFERRED_NODE:-${MEMTIS_SLOW_NODE}}
if [[ ! "${MEMTIS_PREFERRED_NODE}" =~ ^[0-9]+$ ]]; then
    echo "ERROR: MEMTIS_PREFERRED_NODE must be a non-negative NUMA node number" >&2
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

# Rebuild changed launcher sources as well as missing executables.
make -C "${MEMTIS_ROOT}" || exit 1

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
MEMTIS_CGROUP_FRESH=0
MEMTIS_STATS_PID=
MEMTIS_LAUNCH_IN_PROGRESS=0
MEMTIS_LAUNCH_ATTEMPTED=0
MEMTIS_SETUP_STARTED=0

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

configure_memtis_cpu_affinity() {
    local cpulist_file=/sys/devices/system/node/node0/cpulist

    if [[ ! -r "${cpulist_file}" ]]; then
        echo "ERROR: NUMA node 0 CPU list is unavailable" >&2
        return 1
    fi
    # Keep the workload and runner helpers on the same socket as ksamplingd.
    TASKSET_CPUS=$(<"${cpulist_file}")
    if [[ -z "${TASKSET_CPUS}" ]]; then
        echo "ERROR: NUMA node 0 has no CPUs" >&2
        return 1
    fi
    taskset -pc "${TASKSET_CPUS}" "$$" || return 1
    echo "MEMTIS CPU placement: NUMA node 0 (${TASKSET_CPUS})"
}

configure_memtis() {
    # Shared VM/perf settings, with artifact migration controls: AutoNUMA off
    # and generic reclaim demotion inherited. HTMM remains enabled below.
    measurement_apply_common_settings write_value 1000000 0 memtis || return 1

    # Reset MEMTIS-only knobs to linux/mm/mempolicy.c's CONFIG_HTMM defaults,
    # including knobs that might retain values from a previous experiment.
    # Keep artifact migration mode and enable MEMTIS adaptive huge-page splitting.
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
    write_value "${MEMTIS_SYSFS_ROOT}/ksampled_min_sample_ratio" 50 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/ksampled_max_sample_ratio" 10 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_util_weight" 10 || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_skip_cooling" enabled || return 1
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_thres_cooling_alloc" 2621440 || return 1
    # Platform topology: node 1 emulates CXL on c220g5; Optane uses node 2.
    # This selects the same memory tiers as the other systems.
    write_value "${MEMTIS_SYSFS_ROOT}/htmm_cxl_mode" "${MEMTIS_CXL_MODE}" || return 1
}

configure_memtis_cgroup() {
    # HTMM uses the memory controller. CPU placement is handled with taskset;
    # enabling cpuset here is unnecessary and can fail on an existing hierarchy.
    write_value "${MEMTIS_CGROUP_ROOT}/cgroup.subtree_control" +memory || return 1
    # A previous cgroup must be removed before creating fresh adaptive state.
    mkdir "${MEMTIS_CGROUP_DIR}" || return 1
    MEMTIS_CGROUP_FRESH=1

    local htmm_enabled="${MEMTIS_CGROUP_DIR}/memory.htmm_enabled"
    local node_limit="${MEMTIS_CGROUP_DIR}/memory.max_at_node${MEMTIS_FAST_NODE}"
    if [[ ! -e "${htmm_enabled}" || ! -e "${node_limit}" ]]; then
        echo "ERROR: MEMTIS cgroup interfaces are missing under ${MEMTIS_CGROUP_DIR}" >&2
        return 1
    fi

    # Only the timed launch enters this cgroup. Keep the runner, stats
    # collector, and tee outside HTMM's allocation and migration paths.
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
        sleep 1
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
        if ! sleep 1; then
            echo "WARNING: stopping MEMTIS stats collection because sleep failed" >&2
            return 1
        fi
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

remove_memtis_cgroup() {
    stop_memtis_stats
    if [[ ( ${MEMTIS_LAUNCH_ATTEMPTED} -eq 1 || -d "${MEMTIS_CGROUP_DIR}" ) &&
          -x "${MEMTIS_KILL_SAMPLER}" ]]; then
        "${MEMTIS_KILL_SAMPLER}" >/dev/null 2>&1 || return 1
    fi
    MEMTIS_LAUNCH_ATTEMPTED=0
    if [[ -d "${MEMTIS_CGROUP_DIR}" ]]; then
        # Leave the workload cgroup before terminating any remaining children.
        write_value "${MEMTIS_CGROUP_ROOT}/cgroup.procs" "$$" || return 1
        write_value "${MEMTIS_CGROUP_DIR}/memory.htmm_enabled" disabled || return 1
        MEMTIS_CGROUP_ENABLED=0
        recover_stale_memtis_cgroup || return 1
        if ! rmdir "${MEMTIS_CGROUP_DIR}"; then
            echo "ERROR: could not remove ${MEMTIS_CGROUP_DIR}; refusing to reuse MEMTIS state" >&2
            return 1
        fi
    fi
}

cleanup_memtis() {
    local status=$?

    measurement_cleanup_workload_runtime || true
    remove_memtis_cgroup || status=1
    if [[ ${MEMTIS_SETUP_STARTED} -eq 1 ]]; then
        run_measurement_teardown
    fi

    return "${status}"
}
trap cleanup_memtis EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# Called for every workload, after stopping old HTMM state and before sizing
# the fast tier. Batch setup already performs this preparation under our lock.
prepare_memtis_workload() {
    if [[ "${MEMTIS_RUN_SETUP:-0}" == "1" ]]; then
        MEMTIS_SETUP_STARTED=1
        run_measurement_setup "${SIZE_MIB}" memtis || return 1
        # setup.sh also writes VM/perf values; restore the shared settings.
        measurement_apply_common_settings write_value 1000000 0 memtis || return 1
        MEMTIS_CACHE_FLUSH=setup
    elif [[ "${MEMTIS_CACHE_FLUSH:-none}" != "batch-setup" ]]; then
        bash "${SCRIPT_DIR}/defrag.sh" memtis || return 1
        MEMTIS_CACHE_FLUSH=pre-run
    fi

    # Global setup may have moved this process to the slow socket.
    configure_memtis_cpu_affinity || return 1
    measurement_apply_uncore_settings || return 1
}

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

    measurement_prepare_workload_runtime || return 1
    grep -E '^(thp|htmm|pgmig)' /proc/vmstat > "${before_vmstat}" || true
    collect_memtis_stats "${memory_stat_file}" "${hotness_stat_file}" "${migration_stat_file}" &
    MEMTIS_STATS_PID=$!

    echo "Running ${WORKLOAD_OUTPUT} with MEMTIS (fast tier: ${MEMTIS_EFFECTIVE_FAST_TIER_MIB} MiB, requested: ${SIZE_MIB} MiB, CXL mode: ${MEMTIS_CXL_MODE})"
    echo "taskset -c ${TASKSET_CPUS} numactl --preferred=${MEMTIS_PREFERRED_NODE} ${MEMTIS_LAUNCHER} stdbuf -oL -eL env${env_kv} ${command_text}"

    local status=0
    local -a pipeline_status=()
    local timed_out=0
    local start_epoch_s
    local end_epoch_s
    start_epoch_s=$(date +%s)

    set +e
    MEMTIS_LAUNCH_IN_PROGRESS=1
    MEMTIS_LAUNCH_ATTEMPTED=1
    {
        time (
            # $$ still names the parent shell here; BASHPID is this child.
            write_value "${MEMTIS_CGROUP_DIR}/cgroup.procs" "${BASHPID}" || exit 1
            timeout --signal=TERM \
                --kill-after="${MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS}s" \
                "${MEASUREMENT_TIMEOUT_SECONDS}s" \
                taskset -c "${TASKSET_CPUS}" \
                numactl --preferred="${MEMTIS_PREFERRED_NODE}" -- \
                "${MEMTIS_LAUNCHER}" stdbuf -oL -eL env \
                ${WORKLOAD_RUNTIME_CHDIR_ARG:+${WORKLOAD_RUNTIME_CHDIR_ARG}} \
                "${env_assignments[@]}" "${command_argv[@]}" 2>&1
        )
    } 2> "${time_file}" | tee "${log_file}"
    pipeline_status=("${PIPESTATUS[@]}")
    status=${pipeline_status[0]}
    if [[ ${status} -eq 0 && ${pipeline_status[1]} -ne 0 ]]; then
        status=${pipeline_status[1]}
    fi
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
        echo "memtis_settings_profile=memtis-shared-vm-artifact-migration-remote-preferred-split-v23"
        echo "memtis_htmm_thres_split=1"
        echo "memtis_numa_balancing=0"
        echo "memtis_generic_demotion_policy=inherited"
        echo "memtis_cgroup_fresh=${MEMTIS_CGROUP_FRESH}"
        echo "memtis_cgroup_retained=0"
        echo "memtis_cache_flush=${MEMTIS_CACHE_FLUSH:-none}"
        echo "memtis_cgroup_path=${MEMTIS_CGROUP_DIR}"
        echo "memtis_helpers_outside_workload_cgroup=1"
        echo "memtis_kernel_release=$(uname -r)"
        echo "memtis_fast_tier_mib=${SIZE_MIB}"
        echo "memtis_effective_fast_tier_mib=${MEMTIS_EFFECTIVE_FAST_TIER_MIB}"
        echo "memtis_fast_node_free_at_start_mib=${MEMTIS_FAST_NODE_FREE_MIB}"
        echo "memtis_startup_headroom_mib=${MEMTIS_STARTUP_HEADROOM_MIB}"
        echo "memtis_min_fast_tier_mib=${MEMTIS_MIN_FAST_TIER_MIB}"
        echo "memtis_fast_node=${MEMTIS_FAST_NODE}"
        echo "memtis_cxl_mode=${MEMTIS_CXL_MODE}"
        echo "memtis_uncore_method=${MEASUREMENT_UNCORE_METHOD}"
        echo "memtis_uncore_cpus=${MEASUREMENT_UNCORE_CPUS}"
        if [[ -n "${MEASUREMENT_UNCORE_FREQ_KHZ}" ]]; then
            echo "memtis_uncore_freq_khz=${MEASUREMENT_UNCORE_FREQ_KHZ}"
        fi
        echo "memtis_memory_policy=preferred"
        echo "memtis_preferred_node=${MEMTIS_PREFERRED_NODE}"
        echo "memtis_slow_node=${MEMTIS_SLOW_NODE}"
        echo "memtis_worker_affinity_override=none"
        echo "memtis_cpu_node=0"
        echo "memtis_taskset_cpus=${TASKSET_CPUS}"
    } >> "${time_file}"

    if [[ ${timed_out} -eq 1 ]]; then
        echo "WARNING: ${WORKLOAD_OUTPUT} run ${run} timed out after ${MEASUREMENT_TIMEOUT_SECONDS}s" >&2
    elif [[ ${status} -ne 0 ]]; then
        echo "WARNING: ${WORKLOAD_OUTPUT} run ${run} failed with status ${status}" >&2
    fi

    return "${status}"
}

declare -a FAILED_RUNS=()
MEMTIS_WORKLOAD_FINISHED=0
for workload_id in "${WORKLOAD_IDS[@]}"; do
    measurement_load_workload "${workload_id}" || exit 1
    if ! measurement_workload_supports_system memtis && ! measurement_workload_supports_system arms; then
        echo "Skipping ${workload_id}: not supported by MEMTIS"
        continue
    fi

    if [[ ${MEMTIS_WORKLOAD_FINISHED} -eq 1 ]]; then
        echo "Waiting 10 seconds before the next MEMTIS workload"
        sleep 10 || exit $?
    fi
    remove_memtis_cgroup || exit 1
    configure_memtis || exit 1
    prepare_memtis_workload || exit 1
    select_fast_tier_capacity || exit 1
    configure_memtis_cgroup || exit 1
    if ! run_workload "${RUN_ID}"; then
        FAILED_RUNS+=("${WORKLOAD_OUTPUT}:run${RUN_ID}")
    fi
    remove_memtis_cgroup || exit 1
    MEMTIS_WORKLOAD_FINISHED=1
    # External preparation applies only to the first workload in an invocation.
    MEMTIS_CACHE_FLUSH=none
done

if [[ ${#FAILED_RUNS[@]} -gt 0 ]]; then
    echo "MEMTIS completed with failures:" >&2
    printf '  - %s\n' "${FAILED_RUNS[@]}" >&2
    exit 1
fi

echo "All MEMTIS workloads completed successfully."
