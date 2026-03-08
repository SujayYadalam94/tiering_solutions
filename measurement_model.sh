#!/bin/bash

SIZE_MIB=${1:-}
RUN_ID=${2:-}
LIB_SUFFIX=${3:-}
if [[ -z "${SIZE_MIB}" || -z "${RUN_ID}" ]]; then
    echo "Usage: $0 <sizeMiB> <runNumber> [libSuffix]" >&2
    exit 1
fi

KB=$((1024))
MB=$((1024*KB))
GB=$((1024*MB))
BENCH_ROOT=${BENCH_ROOT:-/users/zimooo2}
NUMA_MEM_NODES=${NUMA_MEM_NODES:-0,1}

mkdir -p times logs times/model

#pcts=(90 95 99)
#minmax_options=(true false)
#hist_lengths=(4 8)
#penalties=(0.8 0.9)

pcts=(95)
minmax_options=(false)
hist_lengths=(4)
penalties=(0.9)

function run_program {
    PROGRAM=$1
    MODEL=$2
    OUTPUT=$3
    RUN=$4
    MODEL_TAG="${MODEL}"
    TIME_BASENAME="${SIZE_MIB}MiB_run${RUN}_${MODEL_TAG}"
    TIME_DIR="$PWD/times/model/${OUTPUT}"
    LOG_DIR="$PWD/logs/${OUTPUT}"

    mkdir -p "$TIME_DIR" "$LOG_DIR"

    MODEL_PATH="$PWD/libraries/C220G5/libhemem-${MODEL}${LIB_SUFFIX}.so"

    if [[ ! -f "$MODEL_PATH" ]]; then
        echo "Skipping ${OUTPUT} run ${RUN}: missing library ${MODEL_PATH}"
        return
    fi

    rm -f "${TIME_DIR}/${TIME_BASENAME}.time"
    rm -f "${LOG_DIR}/${TIME_BASENAME}_model.log"
    for part in $(seq 0 9); do
        rm -f "${LOG_DIR}/${TIME_BASENAME}_model_${part}.log"
    done
    rm -f "${TIME_DIR}/max_dram_hugepages_${TIME_BASENAME}.log"

    LOG_OUTPUT_PATH="${LOG_DIR}/${TIME_BASENAME}_model.log"
    { time numactl --membind=${NUMA_MEM_NODES} -- taskset -c 0-9,20-29 \
        sudo \
        LOG_OUTPUT_PATH="${LOG_OUTPUT_PATH}" \
        LD_PRELOAD=${MODEL_PATH} \
        $PROGRAM 2>&1 ; } 2> "${TIME_DIR}/${TIME_BASENAME}.time"
    if [[ -f max_dram_hugepages.log ]]; then
        mv max_dram_hugepages.log "${TIME_DIR}/max_dram_hugepages_${TIME_BASENAME}.log"
    fi
}

function run_model_sweep {
    PROGRAM=$1
    MODEL_BASE=$2
    OUTPUT=$3
    RUN=$4

    for pct in "${pcts[@]}"; do
        for minmax in "${minmax_options[@]}"; do
            for hist_length in "${hist_lengths[@]}"; do
                for penalty in "${penalties[@]}"; do
                    model_name="model_discounted_reward_${pct}_${MODEL_BASE}_l2-${minmax}_${hist_length}_${penalty}"
                    echo "Running ${OUTPUT} with model: ${model_name}"
                    run_program "$PROGRAM" "$model_name" "$OUTPUT" "$RUN"
                done
            done
        done
    done
}

run_model_sweep "${BENCH_ROOT}/.venv/bin/python3 ${BENCH_ROOT}/big-ann-benchmarks/data/10M_benchmark.py --threads 16 --index-key HNSW,Flat --stress-mode latency --dataset openai" "faiss_10M" "faiss_10M" "${RUN_ID}"

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

#gapbs_programs=("bc" "bfs" "cc_sv" "cc" "pr" "pr_spmv" "sssp" "tc")
gapbs_programs=("bc" "pr")
graphs=("twitter.sg")
for graph in "${graphs[@]}"; do
    for prog in "${gapbs_programs[@]}"; do
        echo "Running GAPBS program: $prog on graph: $graph"
        run_model_sweep "OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/$prog -n 40 -f ${BENCH_ROOT}/gapbs/benchmark/graphs/$graph" "${prog}-${graph}" "${prog}-${graph}" "${RUN_ID}"
    done
done

gapbs_programs=("bc" "pr")
graphs=("kron.sg")
for graph in "${graphs[@]}"; do
    for prog in "${gapbs_programs[@]}"; do
        run_model_sweep "OMP_NUM_THREADS=16 ${BENCH_ROOT}/gapbs/$prog -n 20 -f ${BENCH_ROOT}/gapbs/benchmark/graphs/$graph" "${prog}-${graph}" "${prog}-${graph}" "${RUN_ID}"
    done
done

mkdir -p times/model