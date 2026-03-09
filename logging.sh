#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"

RUNS=${1:-5}
START_RUN=${START_RUN:-1}
BENCH_ROOT=${BENCH_ROOT:-/users/zimooo2}
TASKSET_CPUS=${TASKSET_CPUS:-0-15,32-47}
NUMA_MEM_NODES=${NUMA_MEM_NODES:-0}

mkdir -p times logs

function run_program {
    local program=$1
    local model=$2
    local output=$3
    local run=$4

    local time_dir="$PWD/times/${output}"
    local log_dir="$PWD/logs/${output}"
    local time_basename="run${run}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_output_path="${log_dir}/${time_basename}.log"
    local model_path="$PWD/libraries/C220G5/libhemem-${model}.so"

    mkdir -p "${time_dir}" "${log_dir}"

    cleanup_measurement_outputs "${time_file}" "${log_output_path}" /dev/null
    run_preloaded_measurement "${program}" "${model_path}" \
        "${log_output_path}" "${time_file}" "${NUMA_MEM_NODES}" "${TASKSET_CPUS}"
    echo "${log_output_path}"
}

run_gapbs_suite() {
    local run=$1
    local threads=$2
    local graph=$3
    shift 3

    local prog
    for prog in "$@"; do
        echo "Run ${run}: Running GAPBS program: $prog on graph: $graph"
        run_program "OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/$prog -n ${threads} -f ${BENCH_ROOT}/gapbs/benchmark/graphs/$graph" \
            logging "$prog-$graph" "${run}"
    done
}


for run in $(seq "${START_RUN}" "${RUNS}"); do


    run_program "${BENCH_ROOT}/.venv/bin/python3 ${BENCH_ROOT}/big-ann-benchmarks/data/10M_benchmark.py --threads 16 --index-key HNSW,Flat --stress-mode latency --dataset openai" logging faiss_10M ${run}
    continue

    run_program "${BENCH_ROOT}/big-ann-benchmarks/.venv/bin/python3 ${BENCH_ROOT}/big-ann-benchmarks/data/10M_benchmark.py --threads 16 --index-key HNSW,Flat --stress-mode latency --dataset openai" logging faiss_10M ${run}
    
    # Call for all D size NPB programs
    programs=("bt.D.x" "cg.D.x" "ep.D.x" "lu.D.x" "mg.D.x" "sp.D.x" "ua.D.x")
    programs=("mg.D.x")
    for prog in "${programs[@]}"; do
        echo "Running NPB program: $prog"
        run_program "OMP_NUM_THREADS=16 ${BENCH_ROOT}/NPB3.4.3/NPB3.4-OMP/bin/$prog" logging $prog ${run}
    done

    run_program "${BENCH_ROOT}/XSBench/openmp-threading/XSBench -t 16 -g 50000 -p 20000000" logging XSBench ${run}

    # GAPBS programs for twitter and kron graphs

    run_gapbs_suite "${run}" 40 twitter.sg bc pr
    run_gapbs_suite "${run}" 20 kron.sg bc pr
    run_gapbs_suite "${run}" 400 twitter.sg bfs
    run_gapbs_suite "${run}" 200 kron.sg bfs

    run_program "OMP_NUM_THREADS=16 ${BENCH_ROOT}/LULESH/build/lulesh2.0 -i 10 -s 400" logging lulesh2.0_s400 ${run}

    run_program "${BENCH_ROOT}/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpch-sf100/.*benchmark --threads=16" logging DuckDB-TPCH-sf100 ${run}
    run_program "${BENCH_ROOT}/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpcds-sf100/.*benchmark --threads=16" logging DuckDB-TPCDS-sf100 ${run}
done

#run_program "/users/zimooo2/.venv/bin/python /users/zimooo2/dlrm/dlrm_s_pytorch.py --mini-batch-size=2048 --test-mini-batch-size=16384 --test-num-workers=0 --num-batches=300 --data-generation=random --arch-mlp-bot=512-512-64 --arch-mlp-top=1024-1024-1024-1 --arch-sparse-feature-size=64 --arch-embedding-size=1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000 --num-indices-per-lookup=100 --arch-interaction-op=dot --numpy-rand-seed=727 --print-freq=100" logging dlrm_s_pytorch


#run_program "/users/zimooo2/llama.cpp/build/bin/llama-bench -m /users/zimooo2/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf" logging llama_cpp-gpt_oss_120b
#run_program "python3 /users/zimooo2/pytorch_geometric/benchmark/runtime/main.py" logging pytorch_geometric

#run_benchmark "python3 /users/zimooo2/npbench/run_benchmark.py -f numba -p L -b azimint_naive" logging npbench_azimint_naive_numba_L
#run_benchmark "python3 /users/zimooo2/npbench/run_benchmark.py -f numba -p L -b channel_flow" logging npbench_channel_flow_numba_L

#run_program "python /users/zimooo2/rl-baselines3-zoo/train.py --algo sac --env MountainCarContinuous-v0 --eval-freq 10000 --eval-episodes 10 --n-eval-envs 1" logging rl_a3c_sac_mountaincar