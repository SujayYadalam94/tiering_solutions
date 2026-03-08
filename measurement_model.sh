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

mkdir -p times logs times/model

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

    MODEL_PATH="$PWD/libraries/libhemem-${MODEL}${LIB_SUFFIX}.so"

    if [[ ! -f "$MODEL_PATH" ]]; then
        echo "Skipping ${OUTPUT} run ${RUN}: missing library ${MODEL_PATH}"
        return
    fi

    rm -f "${TIME_DIR}/${TIME_BASENAME}.time"
    rm -f "${LOG_DIR}/${TIME_BASENAME}_model.log"
    rm -f "${TIME_DIR}/max_dram_hugepages_${TIME_BASENAME}.log"

    { time taskset -c 0-9,20-29 \
        sudo \
        LD_PRELOAD=${MODEL_PATH} \
        $PROGRAM 2>&1 ; } 2> "${TIME_DIR}/${TIME_BASENAME}.time"
    if [[ -f output.log ]]; then
        mv output.log "${LOG_DIR}/${TIME_BASENAME}_model.log"
    fi
    if [[ -f max_dram_hugepages.log ]]; then
        mv max_dram_hugepages.log "${TIME_DIR}/max_dram_hugepages_${TIME_BASENAME}.log"
    fi
}

run_program "/users/zimooo2/big-ann-benchmarks/.venv/bin/python3 /users/zimooo2/big-ann-benchmarks/data/10M_benchmark.py --threads 16 --index-key HNSW,Flat --stress-mode latency --dataset openai" model_discounted_reward_95_faiss_10M_l2 faiss_10M ${RUN_ID}

# Sweep history lengths so outputs don't overwrite
#run_program "/users/zimooo2/LULESH/build/lulesh2.0 -i 10 -s 400" model_discounted_reward_95_lulesh2.0_s400_l2 lulesh2.0_s400 ${RUN_ID}

#run_program "/users/zimooo2/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpch-sf100/.*benchmark --threads=16" model_discounted_reward_95_DuckDB-TPCH-sf100_l2 DuckDB-TPCH-sf100 ${RUN_ID}
#run_program "/users/zimooo2/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpcds-sf100/.*benchmark --threads=16" model_discounted_reward_95_DuckDB-TPCDS-sf100_l2 DuckDB-TPCDS-sf100 ${RUN_ID}
#
## Call for all D size NPB programs
#programs=("bt.D.x" "cg.D.x" "ep.D.x" "lu.D.x" "mg.D.x" "sp.D.x" "ua.D.x")
#programs=("mg.D.x")
#for prog in "${programs[@]}"; do
#    echo "Running NPB program: $prog"
#    run_program /users/zimooo2/NPB3.4.3/NPB3.4-OMP/bin/$prog model_discounted_reward_95_${prog}_l2 $prog ${RUN_ID}
#done
#
#echo "XSBench run ${RUN_ID}"
#run_program "/users/zimooo2/XSBench/openmp-threading/XSBench -t 20 -g 50000 -p 20000000" model_discounted_reward_95_XSBench_l2 XSBench ${RUN_ID}
#
## GAPBS programs for twitter and kron graphs
#gapbs_programs=("bc" "bfs" "cc_sv" "cc" "pr" "pr_spmv" "sssp" "tc")
#graphs=("twitter.sg" "kron.sg")

#gapbs_programs=("bc" "bfs" "cc_sv" "cc" "pr" "pr_spmv" "sssp" "tc")
#gapbs_programs=("bc" "bfs" "pr")
#gapbs_programs=("bc")
#graphs=("twitter.sg")
#for graph in "${graphs[@]}"; do
#    for prog in "${gapbs_programs[@]}"; do
#        echo "Running GAPBS program: $prog on graph: $graph"
#        run_program "OMP_NUM_THREADS=16 /users/zimooo2/gapbs/$prog -n 40 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" model_discounted_reward_95_${prog}-${graph}_l2 $prog-$graph ${RUN_ID}
#    done
#done

gapbs_programs=("pr")
graphs=("kron.sg")
pcts=(90 95 99)
minmax_options=(true false)
hist_lengths=(4 8)
penalties=(0.8 0.9)

for graph in "${graphs[@]}"; do
    for prog in "${gapbs_programs[@]}"; do
        for pct in "${pcts[@]}"; do
            for minmax in "${minmax_options[@]}"; do
                for hist_length in "${hist_lengths[@]}"; do
                    for penalty in "${penalties[@]}"; do
                        model_name="model_discounted_reward_${pct}_${prog}-${graph}_l2-${minmax}_${hist_length}_${penalty}"
                        echo "Running GAPBS program: ${prog} on graph: ${graph} with model: ${model_name}"
                        run_program "OMP_NUM_THREADS=16 /users/zimooo2/gapbs/$prog -n 20 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" "$model_name" "$prog-$graph" "${RUN_ID}"
                    done
                done
            done
        done
    done
done

mkdir -p times/model