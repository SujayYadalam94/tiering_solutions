#!/bin/bash

MEASUREMENT_LOG_PARTS=${MEASUREMENT_LOG_PARTS:-10}

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

    { time numactl --membind="${numa_mem_nodes}" -- taskset -c "${taskset_cpus}" \
        sudo \
        LOG_OUTPUT_PATH="${log_output_path}" \
        LD_PRELOAD="${library_path}" \
        ${program} 2>&1 ; } 2> "${time_file}"
}

move_max_dram_log_if_present() {
    local destination=$1

    if [[ -f max_dram_hugepages.log ]]; then
        mv max_dram_hugepages.log "${destination}"
    fi
}