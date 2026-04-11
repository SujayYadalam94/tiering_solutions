#!/bin/bash

MEASUREMENT_COMMON_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

MEASUREMENT_LOG_PARTS=${MEASUREMENT_LOG_PARTS:-10}
MEASUREMENT_TIMEOUT_SECONDS=${MEASUREMENT_TIMEOUT_SECONDS:-2700}
MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS=${MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS:-30}

measurement_validate_platform() {
    local platform=$1

    case "${platform}" in
        c220g5|gsl_optane)
            return 0
            ;;
        *)
            echo "ERROR: unsupported platform '${platform}' (expected c220g5|gsl_optane)" >&2
            return 1
            ;;
    esac
}

measurement_apply_platform_defaults() {
    local platform=$1

    case "${platform}" in
        c220g5)
            BENCH_ROOT=${BENCH_ROOT:-/users/zimooo2}
            NUMA_MEM_NODES=${NUMA_MEM_NODES:-0,1}
            TASKSET_CPUS=${TASKSET_CPUS:-0-9,20-29}
            MEASUREMENT_LIBRARY_PROFILE_DIR=${MEASUREMENT_LIBRARY_PROFILE_DIR:-C220G5}
            ;;
        gsl_optane)
            BENCH_ROOT=${BENCH_ROOT:-/home/freischuetz}
            NUMA_MEM_NODES=${NUMA_MEM_NODES:-0,2}
            TASKSET_CPUS=${TASKSET_CPUS:-0-15,32-47}
            MEASUREMENT_LIBRARY_PROFILE_DIR=${MEASUREMENT_LIBRARY_PROFILE_DIR:-GSL_OPTANE}
            ;;
    esac
}

measurement_init_platform_from_args() {
    MEASUREMENT_PLATFORM=${MEASUREMENT_PLATFORM:-c220g5}
    MEASUREMENT_REMAINING_ARGS=()

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --platform)
                if [[ $# -lt 2 ]]; then
                    echo "ERROR: --platform requires a value (c220g5|gsl_optane)" >&2
                    return 1
                fi
                MEASUREMENT_PLATFORM=$2
                shift 2
                ;;
            --platform=*)
                MEASUREMENT_PLATFORM=${1#*=}
                shift
                ;;
            --help|-h)
                MEASUREMENT_REMAINING_ARGS+=("$1")
                shift
                ;;
            *)
                MEASUREMENT_REMAINING_ARGS+=("$1")
                shift
                ;;
        esac
    done

    measurement_validate_platform "${MEASUREMENT_PLATFORM}" || return 1
    measurement_apply_platform_defaults "${MEASUREMENT_PLATFORM}"
}

cleanup_split_log_files() {
    local log_output_path=$1
    local log_stem=${log_output_path%.log}
    local part

    rm -f "${log_output_path}"
    for ((part = 0; part < MEASUREMENT_LOG_PARTS; part++)); do
        rm -f "${log_stem}_${part}.log"
    done
}

cleanup_measurement_outputs() {
    local time_file=$1
    local log_output_path=$2
    local max_dram_file=$3

    rm -f "${time_file}"
    cleanup_split_log_files "${log_output_path}"
    rm -f "${max_dram_file}"
}

run_preloaded_measurement() {
    local program=$1
    local library_path=$2
    local log_output_path=$3
    local time_file=$4
    local numa_mem_nodes=$5
    local taskset_cpus=$6

    local status=0
    local timed_out=0
    local had_errexit=0
    local start_epoch_s=0
    local end_epoch_s=0
    local elapsed_seconds=0

    if [[ $- == *e* ]]; then
        had_errexit=1
    fi

    set +e
    start_epoch_s=$(date +%s)
    {
        time timeout --foreground --signal=TERM \
            --kill-after="${MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS}s" \
            "${MEASUREMENT_TIMEOUT_SECONDS}s" \
            numactl --membind="${numa_mem_nodes}" -- taskset -c "${taskset_cpus}" \
            sudo \
            LOG_OUTPUT_PATH="${log_output_path}" \
            LD_PRELOAD="${library_path}" \
            ${program} 2>&1
    } 2> "${time_file}"
    status=$?
    end_epoch_s=$(date +%s)
    if [[ ${had_errexit} -eq 1 ]]; then
        set -e
    fi

    elapsed_seconds=$((end_epoch_s - start_epoch_s))

    # Distinguish true timeout from command exits that also use status 124/137.
    # A true timeout should run for approximately the configured timeout window.
    if [[ (${status} -eq 124 || ${status} -eq 137) && ${elapsed_seconds} -ge ${MEASUREMENT_TIMEOUT_SECONDS} ]]; then
        timed_out=1
    fi

    {
        echo "exit_status=${status}"
        echo "timed_out=${timed_out}"
        echo "elapsed_seconds=${elapsed_seconds}"
        echo "timeout_seconds=${MEASUREMENT_TIMEOUT_SECONDS}"
        echo "kill_after_seconds=${MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS}"
    } >> "${time_file}"

    return ${status}
}

move_max_dram_log_if_present() {
    local destination=$1

    if [[ -f max_dram_hugepages.log ]]; then
        mv max_dram_hugepages.log "${destination}"
    fi
}

measurement_apply_nomad_settings() {
    echo 1 | sudo tee /sys/kernel/mm/numa/demotion_enabled >/dev/null
    echo 2 | sudo tee /proc/sys/kernel/numa_balancing >/dev/null
    sudo sysctl -w vm.demote_scale_factor=1000 >/dev/null
    sudo swapoff -a
}

measurement_apply_tpp_settings() {
    echo 1 | sudo tee /sys/kernel/mm/numa/demotion_enabled >/dev/null
    echo 3 | sudo tee /proc/sys/kernel/numa_balancing >/dev/null
    sudo sysctl -w vm.demote_scale_factor=200 >/dev/null
}

measurement_apply_baseline_default_migration_settings() {
    echo 1 | sudo tee /proc/sys/kernel/numa_balancing >/dev/null
    echo 0 | sudo tee /proc/sys/vm/zone_reclaim_mode >/dev/null
    echo false | sudo tee /sys/kernel/mm/numa/demotion_enabled >/dev/null
}

run_measurement_setup() {
    local size_mib=$1
    local measurement_system=${2:-default}
    local measurement_platform=${MEASUREMENT_PLATFORM:-c220g5}

    sudo bash "${MEASUREMENT_COMMON_DIR}/unsetup.sh" || true
    sudo bash "${MEASUREMENT_COMMON_DIR}/setup.sh" "${size_mib}" "${measurement_system}" "${measurement_platform}"
    if [[ "${measurement_system}" == "nomad" ]]; then
        measurement_apply_nomad_settings
    elif [[ "${measurement_system}" == "tpp" ]]; then
        measurement_apply_tpp_settings
    fi
    bash "${MEASUREMENT_COMMON_DIR}/defrag.sh"
}

run_measurement_setup_baseline_default() {
    local size_mib=$1

    sudo bash "${MEASUREMENT_COMMON_DIR}/unsetup.sh" || true
    sudo bash "${MEASUREMENT_COMMON_DIR}/setup.sh" "${size_mib}" default
    measurement_apply_baseline_default_migration_settings || return 1
}

run_measurement_teardown() {
    sudo bash "${MEASUREMENT_COMMON_DIR}/unsetup.sh" || true
}

measurement_ensure_nomad_ready() {
    local min_major=5
    local min_minor=13
    local kernel_release
    local kernel_core
    local kernel_major
    local kernel_minor
    local nomad_module_dir
    local nomad_module_name
    local nomad_module_ko

    kernel_release=$(uname -r)
    kernel_core=${kernel_release%%-*}
    IFS='.' read -r kernel_major kernel_minor _ <<< "${kernel_core}"

    if [[ ! "${kernel_major}" =~ ^[0-9]+$ || ! "${kernel_minor}" =~ ^[0-9]+$ ]]; then
        echo "ERROR: unable to parse kernel version '${kernel_release}'" >&2
        return 1
    fi

    if (( kernel_major < min_major || (kernel_major == min_major && kernel_minor < min_minor) )); then
        echo "ERROR: NOMAD requires kernel ${min_major}.${min_minor}+ (found ${kernel_release})" >&2
        return 1
    fi

    nomad_module_dir=${NOMAD_MODULE_DIR:-${HOME}/SoarAlto/run/nomad_module}
    nomad_module_name=${NOMAD_MODULE_NAME:-async_promote}
    nomad_module_ko=${NOMAD_MODULE_KO:-${nomad_module_dir}/${nomad_module_name}.ko}

    if [[ ! -d "${nomad_module_dir}" ]]; then
        echo "ERROR: NOMAD module directory not found: ${nomad_module_dir}" >&2
        return 1
    fi

    if [[ ! -f "${nomad_module_ko}" ]]; then
        echo "ERROR: NOMAD module binary not found: ${nomad_module_ko}" >&2
        return 1
    fi

    if lsmod | awk '{print $1}' | grep -qx "${nomad_module_name}"; then
        return 0
    fi

    echo "NOMAD module '${nomad_module_name}' is not loaded; loading ${nomad_module_ko}"
    if ! sudo insmod "${nomad_module_ko}"; then
        echo "ERROR: failed to load NOMAD module '${nomad_module_name}' from ${nomad_module_ko}" >&2
        return 1
    fi

    if ! lsmod | awk '{print $1}' | grep -qx "${nomad_module_name}"; then
        echo "ERROR: NOMAD module '${nomad_module_name}' is still not active after insmod" >&2
        return 1
    fi
}