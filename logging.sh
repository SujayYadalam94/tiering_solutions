#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

measurement_init_platform_from_args "$@" || exit 1
ARGS=("${MEASUREMENT_REMAINING_ARGS[@]}")

RUNS=${ARGS[0]:-10}
START_RUN=${START_RUN:-3}
LOGGING_SIZE_MIB=${LOGGING_SIZE_MIB:-0}
LOGGING_MODEL_PCT=${LOGGING_MODEL_PCT:-95}
LOGGING_MODEL_MINMAX=${LOGGING_MODEL_MINMAX:-false}
LOGGING_MODEL_HISTORY_LENGTH=${LOGGING_MODEL_HISTORY_LENGTH:-4}
LOGGING_MODEL_PENALTY=${LOGGING_MODEL_PENALTY:-0.9}
LOGGING_LIB_SUFFIX=${LOGGING_LIB_SUFFIX:-_train}
WORKLOAD_ARGS=("${ARGS[@]:1}")
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}" logs
LOG_CONVERTER_SCRIPT="${SCRIPT_DIR}/../tiering_models/process_data/data/filter_v3_split_runs.py"
LOG_CONVERTER_VENV_PYTHON="${SCRIPT_DIR}/../tiering_models/process_data/data/.venv/bin/python"

run_measurement_setup "${LOGGING_SIZE_MIB}" || exit 1
trap 'run_measurement_teardown' EXIT

function run_program {
    local output=$1
    local run=$2
    local model_path=$3

    local time_dir="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/${output}"
    local log_dir="${SCRIPT_DIR}/logs/${output}"
    local time_basename="run${run}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_output_path="${log_dir}/${time_basename}.log"

    mkdir -p "${time_dir}" "${log_dir}"

    if [[ ! -f "${model_path}" ]]; then
        echo "Skipping ${output} run ${run}: missing library ${model_path}"
        return 1
    fi

    cleanup_measurement_outputs "${time_file}" "${log_output_path}" /dev/null
    run_preloaded_measurement "${WORKLOAD_COMMAND}" "${model_path}" \
        "${log_output_path}" "${time_file}" "${NUMA_MEM_NODES}" "${TASKSET_CPUS}"
    echo "${log_output_path}"
}


function convert_logs_to_parquet {
    local output=$1
    local log_dir="${SCRIPT_DIR}/logs/${output}"
    local python_cmd=python3

    if [[ ! -f "${LOG_CONVERTER_SCRIPT}" ]]; then
        echo "ERROR: converter script not found at ${LOG_CONVERTER_SCRIPT}" >&2
        return 1
    fi

    if [[ -x "${LOG_CONVERTER_VENV_PYTHON}" ]]; then
        python_cmd="${LOG_CONVERTER_VENV_PYTHON}"
    fi

    "${python_cmd}" "${LOG_CONVERTER_SCRIPT}" "${log_dir}"
}


for run in $(seq "${START_RUN}" "${RUNS}"); do
    for workload_id in "${WORKLOAD_IDS[@]}"; do
        measurement_load_workload "${workload_id}" || exit 1
        if ! measurement_workload_supports_system logging; then
            echo "Skipping ${workload_id}: not supported by logging runner"
            continue
        fi

        model_path=$(measurement_build_logging_library_path "${SCRIPT_DIR}")

        echo "Run ${run}: workload ${WORKLOAD_ID} library ${model_path}"
        if ! run_program "${WORKLOAD_OUTPUT}" "${run}" "${model_path}"; then
            exit 1
        fi
        if ! convert_logs_to_parquet "${WORKLOAD_OUTPUT}"; then
            exit 1
        fi
    done
done

#run_program "${BENCH_ROOT}/.venv/bin/python ${BENCH_ROOT}/dlrm/dlrm_s_pytorch.py --mini-batch-size=2048 --test-mini-batch-size=16384 --test-num-workers=0 --num-batches=300 --data-generation=random --arch-mlp-bot=512-512-64 --arch-mlp-top=1024-1024-1024-1 --arch-sparse-feature-size=64 --arch-embedding-size=1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000 --num-indices-per-lookup=100 --arch-interaction-op=dot --numpy-rand-seed=727 --print-freq=100" logging dlrm_s_pytorch


#run_program "${BENCH_ROOT}/llama.cpp/build/bin/llama-bench -m ${BENCH_ROOT}/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf" logging llama_cpp-gpt_oss_120b
#run_program "python3 ${BENCH_ROOT}/pytorch_geometric/benchmark/runtime/main.py" logging pytorch_geometric

#run_benchmark "python3 ${BENCH_ROOT}/npbench/run_benchmark.py -f numba -p L -b azimint_naive" logging npbench_azimint_naive_numba_L
#run_benchmark "python3 ${BENCH_ROOT}/npbench/run_benchmark.py -f numba -p L -b channel_flow" logging npbench_channel_flow_numba_L

#run_program "python ${BENCH_ROOT}/rl-baselines3-zoo/train.py --algo sac --env MountainCarContinuous-v0 --eval-freq 10000 --eval-episodes 10 --n-eval-envs 1" logging rl_a3c_sac_mountaincar