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

run_measurement_setup() {
    local size_mib=$1

    sudo bash "${MEASUREMENT_COMMON_DIR}/unsetup.sh" || true
    sudo bash "${MEASUREMENT_COMMON_DIR}/setup.sh" "${size_mib}"
    bash "${MEASUREMENT_COMMON_DIR}/defrag.sh"
}

run_measurement_teardown() {
    sudo bash "${MEASUREMENT_COMMON_DIR}/unsetup.sh" || true
}