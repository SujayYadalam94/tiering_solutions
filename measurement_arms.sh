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

mkdir -p times logs times/arms

function run_program {
    local program=$1
    local output=$2
    local run=$3
    local time_basename="${SIZE_MIB}MiB_run${run}"
    local time_dir="$PWD/times/arms/${output}"
    local log_dir="$PWD/logs/${output}"
    local time_file="${time_dir}/${time_basename}.time"
    local log_output_path="${log_dir}/${time_basename}_arms.log"
    local max_dram_file="${time_dir}/max_dram_hugepages_${time_basename}.log"
    local model_path="$PWD/libraries/C220G5/libhemem-arms${LIB_SUFFIX}.so"

    mkdir -p "${time_dir}" "${log_dir}"

    if [[ ! -f "${model_path}" ]]; then
        echo "Skipping ${output} run ${run}: missing ${model_path}"
        return
    fi

    cleanup_measurement_outputs "${time_file}" "${log_output_path}" "${max_dram_file}"
    run_preloaded_measurement "${program}" "${model_path}" "${log_output_path}" "${time_file}" \
        "${NUMA_MEM_NODES}" "${TASKSET_CPUS}"
    move_max_dram_log_if_present "${max_dram_file}"
}

run_program "${BENCH_ROOT}/big-ann-benchmarks/.venv/bin/python3 ${BENCH_ROOT}/big-ann-benchmarks/data/10M_benchmark.py --threads 16 --index-key HNSW,Flat --stress-mode latency --dataset openai" faiss_10M "${RUN_ID}"

run_program "OMP_NUM_THREADS=16 ${BENCH_ROOT}/LULESH/build/lulesh2.0 -i 10 -s 400" lulesh2.0_s400 ${RUN_ID}

run_program "${BENCH_ROOT}/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpch-sf100/.*benchmark --threads=16" DuckDB-TPCH-sf100 ${RUN_ID}
run_program "${BENCH_ROOT}/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpcds-sf100/.*benchmark --threads=16" DuckDB-TPCDS-sf100 ${RUN_ID}


## Call for all D size NPB programs
#programs=("bt.D.x" "cg.D.x" "ep.D.x" "lu.D.x" "mg.D.x" "sp.D.x" "ua.D.x")
programs=("mg.D.x")
for prog in "${programs[@]}"; do
    echo "Running NPB program: $prog"
    run_program "OMP_NUM_THREADS=16 ${BENCH_ROOT}/NPB3.4.3/NPB3.4-OMP/bin/$prog" "$prog" "${RUN_ID}"
done

run_program "${BENCH_ROOT}/XSBench/openmp-threading/XSBench -t 16 -g 50000 -p 20000000" XSBench ${RUN_ID}

gapbs_programs=("bc" "pr")
graphs=("twitter.sg")
for graph in "${graphs[@]}"; do
    for prog in "${gapbs_programs[@]}"; do
        echo "Running GAPBS program: $prog on graph: $graph"
        run_program "OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/$prog -n 40 -f ${BENCH_ROOT}/gapbs/benchmark/graphs/$graph" "$prog-$graph" "${RUN_ID}"
    done
done

gapbs_programs=("bc" "pr")
graphs=("kron.sg")
for graph in "${graphs[@]}"; do
    for prog in "${gapbs_programs[@]}"; do
        run_program "OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/$prog -n 20 -f ${BENCH_ROOT}/gapbs/benchmark/graphs/$graph" "$prog-$graph" "${RUN_ID}"
    done
done

#
mkdir -p times/arms