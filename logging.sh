#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

measurement_init_platform_from_args "$@" || exit 1
ARGS=("${MEASUREMENT_REMAINING_ARGS[@]}")

RUNS=${ARGS[0]:-11}
START_RUN=${START_RUN:-11}
LOGGING_SIZE_MIB=${LOGGING_SIZE_MIB:-0}
LOGGING_MODEL_PCT=${LOGGING_MODEL_PCT:-99}
PARQUET_PYTHON=${PARQUET_PYTHON:-python3}
WORKLOAD_ARGS=("${ARGS[@]:1}")
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

# Reject missing workload-specific libraries before changing machine settings.
for workload_id in "${WORKLOAD_IDS[@]}"; do
    measurement_load_workload "${workload_id}" || exit 1
    measurement_workload_supports_system logging || continue
    model_path=$(measurement_build_logging_library_path "${SCRIPT_DIR}" "${WORKLOAD_MODEL_BASE}" "${LOGGING_MODEL_PCT}")
    if [[ ! -f "${model_path}" ]]; then
        echo "ERROR: missing workload-specific logging library: ${model_path}" >&2
        exit 1
    fi
done
"${PARQUET_PYTHON}" -c 'import pyarrow.parquet' || exit 1

mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}" logs

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
    local model_object="${SCRIPT_DIR}/models/model_discounted_reward_${LOGGING_MODEL_PCT}_${WORKLOAD_MODEL_BASE}_l2.o"
    # Save identity before the workload starts. Resume additionally requires a
    # successful process record and the model timing fields in its Parquet file.
    "${PARQUET_PYTHON}" "${SCRIPT_DIR}/scripts/model_collection_metadata.py" write \
        "${log_output_path%.log}.parquet" "${model_object}" "${model_path}" || return 1
    run_preloaded_measurement "${WORKLOAD_COMMAND}" "${model_path}" \
        "${log_output_path}" "${time_file}" "${NUMA_MEM_NODES}" "${TASKSET_CPUS}" || return 1
    "${PARQUET_PYTHON}" "${SCRIPT_DIR}/scripts/model_collection_metadata.py" check \
        "${log_output_path%.log}.parquet" "${model_object}" "${model_path}" || return 1
    echo "${log_output_path%.log}.parquet"
}


for run in $(seq "${START_RUN}" "${RUNS}"); do
    for workload_id in "${WORKLOAD_IDS[@]}"; do
        measurement_load_workload "${workload_id}" || exit 1
        if ! measurement_workload_supports_system logging; then
            echo "Skipping ${workload_id}: not supported by logging runner"
            continue
        fi

        model_path=$(measurement_build_logging_library_path "${SCRIPT_DIR}" "${WORKLOAD_MODEL_BASE}" "${LOGGING_MODEL_PCT}")

        echo "Run ${run}: workload ${WORKLOAD_ID} library ${model_path}"
        if ! run_program "${WORKLOAD_OUTPUT}" "${run}" "${model_path}"; then
            exit 1
        fi
        if ! measurement_require_training_parquet "${SCRIPT_DIR}/logs/${WORKLOAD_OUTPUT}/run${run}.parquet"; then
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
