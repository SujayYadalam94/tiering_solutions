#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

measurement_init_platform_from_args "$@" || exit 1
ARGS=("${MEASUREMENT_REMAINING_ARGS[@]}")

RUNS=${ARGS[0]:-5}
START_RUN=${START_RUN:-1}
WORKLOAD_ARGS=("${ARGS[@]:1}")
mapfile -t WORKLOAD_IDS < <(measurement_expand_workloads "${WORKLOAD_ARGS[@]}")

mkdir -p "${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}" logs

function run_program {
    local output=$1
    local run=$2

    local time_dir="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/${output}"
    local log_dir="${SCRIPT_DIR}/logs/${output}"
    local time_basename="run${run}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_output_path="${log_dir}/${time_basename}.log"
    local model_path="${SCRIPT_DIR}/libraries/${MEASUREMENT_LIBRARY_PROFILE_DIR}/libhemem-logging.so"

    mkdir -p "${time_dir}" "${log_dir}"

    cleanup_measurement_outputs "${time_file}" "${log_output_path}" /dev/null
    run_preloaded_measurement "${WORKLOAD_COMMAND}" "${model_path}" \
        "${log_output_path}" "${time_file}" "${NUMA_MEM_NODES}" "${TASKSET_CPUS}"
    echo "${log_output_path}"
}


for run in $(seq "${START_RUN}" "${RUNS}"); do
    for workload_id in "${WORKLOAD_IDS[@]}"; do
        measurement_load_workload "${workload_id}" || exit 1
        if ! measurement_workload_supports_system logging; then
            echo "Skipping ${workload_id}: not supported by logging runner"
            continue
        fi

        echo "Run ${run}: workload ${WORKLOAD_ID}"
        run_program "${WORKLOAD_OUTPUT}" "${run}"
    done
done

#run_program "${BENCH_ROOT}/.venv/bin/python ${BENCH_ROOT}/dlrm/dlrm_s_pytorch.py --mini-batch-size=2048 --test-mini-batch-size=16384 --test-num-workers=0 --num-batches=300 --data-generation=random --arch-mlp-bot=512-512-64 --arch-mlp-top=1024-1024-1024-1 --arch-sparse-feature-size=64 --arch-embedding-size=1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000 --num-indices-per-lookup=100 --arch-interaction-op=dot --numpy-rand-seed=727 --print-freq=100" logging dlrm_s_pytorch


#run_program "${BENCH_ROOT}/llama.cpp/build/bin/llama-bench -m ${BENCH_ROOT}/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf" logging llama_cpp-gpt_oss_120b
#run_program "python3 ${BENCH_ROOT}/pytorch_geometric/benchmark/runtime/main.py" logging pytorch_geometric

#run_benchmark "python3 ${BENCH_ROOT}/npbench/run_benchmark.py -f numba -p L -b azimint_naive" logging npbench_azimint_naive_numba_L
#run_benchmark "python3 ${BENCH_ROOT}/npbench/run_benchmark.py -f numba -p L -b channel_flow" logging npbench_channel_flow_numba_L

#run_program "python ${BENCH_ROOT}/rl-baselines3-zoo/train.py --algo sac --env MountainCarContinuous-v0 --eval-freq 10000 --eval-episodes 10 --n-eval-envs 1" logging rl_a3c_sac_mountaincar