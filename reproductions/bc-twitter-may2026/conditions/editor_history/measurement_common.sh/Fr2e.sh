#!/bin/bash

MEASUREMENT_COMMON_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

MEASUREMENT_LOG_PARTS=${MEASUREMENT_LOG_PARTS:-10}
MEASUREMENT_TIMEOUT_SECONDS=${MEASUREMENT_TIMEOUT_SECONDS:-2700}
MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS=${MEASUREMENT_TIMEOUT_KILL_AFTER_SECONDS:-30}

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

    if [[ $- == *e* ]]; then
        had_errexit=1
    fi

    set +e
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

run_measurement_setup() {
    local size_mib=$1
    local measurement_system=${2:-default}

    sudo bash "${MEASUREMENT_COMMON_DIR}/unsetup.sh" || true
    sudo bash "${MEASUREMENT_COMMON_DIR}/setup.sh" "${size_mib}" "${measurement_system}"
    if [[ "${measurement_system}" == "nomad" ]]; then
        measurement_apply_nomad_settings
    elif [[ "${measurement_system}" == "tpp" ]]; then
        measurement_apply_tpp_settings
    fi
    bash "${MEASUREMENT_COMMON_DIR}/defrag.sh"
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