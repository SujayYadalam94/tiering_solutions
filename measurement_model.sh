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

mkdir -p times logs times/model

#pcts=(90 95 99)
#minmax_options=(true false)
#hist_lengths=(4 8)
#penalties=(0.8 0.9)

pcts=(95)
minmax_options=(false)
hist_lengths=(4)
penalties=(0.9)

build_model_name() {
    local pct=$1
    local model_base=$2
    local minmax=$3
    local hist_length=$4
    local penalty=$5

    printf 'model_discounted_reward_%s_%s_l2-%s_%s_%s' \
        "${pct}" "${model_base}" "${minmax}" "${hist_length}" "${penalty}"
}

build_model_combo() {
    local minmax=$1
    local hist_length=$2
    local penalty=$3

    printf '%s_%s_%s' "${minmax}" "${hist_length}" "${penalty}"
}

build_model_library_path() {
    local model_base=$1
    local combo=$2

    if [[ "${LIB_SUFFIX}" == "_train" ]]; then
        printf '%s/libraries/C220G5/libhemem-%s_%s_train.so\n' "${SCRIPT_DIR}" "${model_base}" "${combo}"
    else
        printf '%s/libraries/C220G5/libhemem-%s-%s%s.so\n' "${SCRIPT_DIR}" "${model_base}" "${combo}" "${LIB_SUFFIX}"
    fi
}

function run_program {
    local program=$1
    local model_tag=$2
    local output=$3
    local run=$4
    local time_basename="${SIZE_MIB}MiB_run${run}_${model_tag}"
    local time_dir="${SCRIPT_DIR}/times/model/${output}"
    local log_dir="${SCRIPT_DIR}/logs/${output}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_output_path="${log_dir}/${time_basename}_model.log"
    local max_dram_file="${time_dir}/max_dram_hugepages_${time_basename}.log"
    local model_path=$5

    mkdir -p "${time_dir}" "${log_dir}"

    if [[ ! -f "${model_path}" ]]; then
        echo "Skipping ${output} run ${run}: missing library ${model_path}"
        return
    fi

    cleanup_measurement_outputs "${time_file}" "${log_output_path}" "${max_dram_file}"
    run_preloaded_measurement "${program}" "${model_path}" "${log_output_path}" "${time_file}" \
        "${NUMA_MEM_NODES}" "${TASKSET_CPUS}"
    move_max_dram_log_if_present "${max_dram_file}"
}

function run_model_sweep {
    local program=$1
    local model_base=$2
    local output=$3
    local run=$4
    local pct
    local minmax
    local hist_length
    local penalty
    local model_name
    local model_combo
    local model_path

    for pct in "${pcts[@]}"; do
        for minmax in "${minmax_options[@]}"; do
            for hist_length in "${hist_lengths[@]}"; do
                for penalty in "${penalties[@]}"; do
                    model_name=$(build_model_name "${pct}" "${model_base}" "${minmax}" "${hist_length}" "${penalty}")
                    model_combo=$(build_model_combo "${minmax}" "${hist_length}" "${penalty}")
                    model_path=$(build_model_library_path "${model_base}" "${model_combo}")
                    echo "Running ${output} with model: ${model_name}"
                    run_program "${program}" "${model_name}" "${output}" "${run}" "${model_path}"
                done
            done
        done
    done
}

for workload_id in "${WORKLOAD_IDS[@]}"; do
    measurement_load_workload "${workload_id}" || exit 1
    if ! measurement_workload_supports_system model; then
        echo "Skipping ${workload_id}: not supported by model runner"
        continue
    fi

    echo "Running workload: ${WORKLOAD_ID}"
    run_model_sweep "${WORKLOAD_COMMAND}" "${WORKLOAD_MODEL_BASE}" "${WORKLOAD_OUTPUT}" "${RUN_ID}"
done

mkdir -p times/model