#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"

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

run_gapbs_suite() {
    local run=$1
    local threads=$2
    local graph=$3
    shift 3

    local prog
    for prog in "$@"; do
        echo "Running GAPBS program: $prog on graph: $graph"
        run_model_sweep "OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/$prog -n ${threads} -f ${BENCH_ROOT}/gapbs/benchmark/graphs/$graph" \
            "${prog}-${graph}" "${prog}-${graph}" "${run}"
    done
}

function run_program {
    local program=$1
    local model=$2
    local output=$3
    local run=$4
    local model_tag="${model}"
    local time_basename="${SIZE_MIB}MiB_run${run}_${model_tag}"
    local time_dir="$PWD/times/model/${output}"
    local log_dir="$PWD/logs/${output}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_output_path="${log_dir}/${time_basename}_model.log"
    local max_dram_file="${time_dir}/max_dram_hugepages_${time_basename}.log"
    local model_path="$PWD/libraries/C220G5/libhemem-${model}${LIB_SUFFIX}.so"

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

    for pct in "${pcts[@]}"; do
        for minmax in "${minmax_options[@]}"; do
            for hist_length in "${hist_lengths[@]}"; do
                for penalty in "${penalties[@]}"; do
                    model_name=$(build_model_name "${pct}" "${model_base}" "${minmax}" "${hist_length}" "${penalty}")
                    echo "Running ${output} with model: ${model_name}"
                    run_program "${program}" "${model_name}" "${output}" "${run}"
                done
            done
        done
    done
}

run_model_sweep "${BENCH_ROOT}/big-ann-benchmarks/.venv/bin/python3 ${BENCH_ROOT}/big-ann-benchmarks/data/10M_benchmark.py --threads 16 --index-key HNSW,Flat --stress-mode latency --dataset openai" "faiss_10M" "faiss_10M" "${RUN_ID}"

exit

# Sweep history lengths so outputs don't overwrite
run_model_sweep "OMP_NUM_THREADS=16 ${BENCH_ROOT}/LULESH/build/lulesh2.0 -i 10 -s 400" model_discounted_reward_95_lulesh2.0_s400_l2 lulesh2.0_s400 ${RUN_ID}

run_model_sweep "${BENCH_ROOT}/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpch-sf100/.*benchmark --threads=16" "DuckDB-TPCH-sf100" "DuckDB-TPCH-sf100" "${RUN_ID}"
run_model_sweep "${BENCH_ROOT}/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpcds-sf100/.*benchmark --threads=16" "DuckDB-TPCDS-sf100" "DuckDB-TPCDS-sf100" "${RUN_ID}"


## Call for all D size NPB programs
#programs=("bt.D.x" "cg.D.x" "ep.D.x" "lu.D.x" "mg.D.x" "sp.D.x" "ua.D.x")
programs=("mg.D.x")
for prog in "${programs[@]}"; do
    echo "Running NPB program: $prog"
    run_model_sweep "OMP_NUM_THREADS=16 ${BENCH_ROOT}/NPB3.4.3/NPB3.4-OMP/bin/$prog" "$prog" "$prog" "${RUN_ID}"
done
#
#echo "XSBench run ${RUN_ID}"
run_model_sweep "${BENCH_ROOT}/XSBench/openmp-threading/XSBench -t 16 -g 50000 -p 20000000" "XSBench" "XSBench" "${RUN_ID}"
#
## GAPBS programs for twitter and kron graphs
#gapbs_programs=("bc" "bfs" "cc_sv" "cc" "pr" "pr_spmv" "sssp" "tc")
#graphs=("twitter.sg" "kron.sg")

run_gapbs_suite "${RUN_ID}" 40 twitter.sg bc pr
run_gapbs_suite "${RUN_ID}" 20 kron.sg bc pr

mkdir -p times/model