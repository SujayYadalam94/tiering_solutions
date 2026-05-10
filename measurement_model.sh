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
LIB_SUFFIX=${ARGS[2]:-}
if [[ -z "${SIZE_MIB}" || -z "${RUN_ID}" ]]; then
    echo "Usage: $0 [--platform c220g5|gsl_optane] <sizeMiB> <runNumber> [libSuffix] [workloadId ...]" >&2
    exit 1
fi

WORKLOAD_ARGS=("${ARGS[@]:3}")
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}" logs "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/model"
echo "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}"

#pcts=(90 95 99)
#history_summary_modes=(0 1 2)
#hist_lengths=(4 8)

pcts=(99)
history_summary_modes=(2)
hist_lengths=(10)
#switch_scalers=(1.0 2.0 4.0 8.0 16.0 32.0 64.0 128.0 256.0)
#switch_scalers=(256.0 128.0 64.0 32.0 16.0 8.0 4.0 2.0 1.0 )
switch_scalers=(0.125 0.25 0.5 1.0)

#pcts=(95)
#history_summary_modes=(2)
#hist_lengths=(8)
#switch_scalers=(0.9)

function run_program {
    local program=$1
    local model_tag=$2
    local output=$3
    local run=$4
    local time_basename="${SIZE_MIB}MiB_run${run}_${model_tag}"
    local time_dir="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/model/${output}"
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
    local status=$?

    local timed_out=0
    if [[ -f "${time_file}" ]]; then
        timed_out=$(awk -F= '/^timed_out=/{print $2}' "${time_file}" | tail -n1)
    fi

    if [[ "${timed_out}" == "1" ]]; then
        echo "WARNING: ${output} run ${run} timed out after ${MEASUREMENT_TIMEOUT_SECONDS}s"
    elif [[ ${status} -ne 0 ]]; then
        echo "WARNING: ${output} run ${run} failed with status ${status}"
    fi
    move_max_dram_log_if_present "${max_dram_file}"
}

function run_model_sweep {
    local program=$1
    local model_base=$2
    local output=$3
    local run=$4
    local pct
    local history_summary
    local hist_length
    local switch_scaler
    local model_name
    local model_path

    for pct in "${pcts[@]}"; do
        for history_summary in "${history_summary_modes[@]}"; do
            for hist_length in "${hist_lengths[@]}"; do
                for switch_scaler in "${switch_scalers[@]}"; do
                    model_name=$(measurement_build_model_name "${pct}" "${model_base}" "${history_summary}" "${hist_length}" "${switch_scaler}")
                    model_path=$(measurement_build_model_library_path "${SCRIPT_DIR}" "${model_name}" "${LIB_SUFFIX}")

                    echo "Running ${output} with model: ${model_name} ${model_path}"

                    run_measurement_setup "${SIZE_MIB}"
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

mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/model"