#!/bin/bash

SIZE_MIB=${1:-}
if [[ -z "${SIZE_MIB}" ]]; then
    echo "Usage: $0 <sizeMiB>" >&2
    exit 1
fi

KB=$((1024))
MB=$((1024*KB))
GB=$((1024*MB))

mkdir -p times logs

function run_program {
    PROGRAM=$1
    MODEL=$2
    OUTPUT=$3
    TIME_BASENAME="${OUTPUT}_${SIZE_MIB}MiB"

    MODEL_PATH="$PWD/libraries/libhemem-${MODEL}.so"

    if [[ ! -f "$MODEL_PATH" ]]; then
        echo "Skipping ${OUTPUT}: missing ${MODEL_PATH}"
        return
    fi

    rm -f "times/${TIME_BASENAME}.time"
    rm -f "logs/${TIME_BASENAME}_model.log"
    rm -f "times/model/max_dram_hugepages_${TIME_BASENAME}.log"

    { time numactl -N0 --preferred=0 -- taskset -c 0-9,20-29 \
        sudo \
        LD_PRELOAD=${MODEL_PATH} \
        $PROGRAM 2>&1 ; } 2> "times/${TIME_BASENAME}.time"
    mv output.log "logs/${TIME_BASENAME}_model.log"
    mv max_dram_hugepages.log "times/model/max_dram_hugepages_${TIME_BASENAME}.log"
}

#run_program "/users/zimooo2/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpch-sf100/.*benchmark --threads=20" model_discounted_reward_99_DuckDB-TPCH-sf100_l2 DuckDB-TPCH-sf100

# Call for all D size NPB programs
#programs=("bt.D.x" "cg.D.x" "ep.D.x" "lu.D.x" "mg.D.x" "sp.D.x" "ua.D.x")
#for prog in "${programs[@]}"; do
#    echo "Running NPB program: $prog"
#    run_program /users/zimooo2/NPB3.4.3/NPB3.4-OMP/bin/$prog model_discounted_reward_99_${prog}_l2 $prog
#done
#
run_program "/users/zimooo2/XSBench/openmp-threading/XSBench -t 20 -g 50000 -p 20000000" model_discounted_reward_99_XSBench_l2 XSBench
#
## GAPBS programs for twitter and kron graphs
#gapbs_programs=("bc" "bfs" "cc_sv" "cc" "pr" "pr_spmv" "sssp" "tc")
#graphs=("twitter.sg" "kron.sg")
#for graph in "${graphs[@]}"; do
#    for prog in "${gapbs_programs[@]}"; do
#        echo "Running GAPBS program: $prog on graph: $graph"
#        run_program "/users/zimooo2/gapbs/$prog -n 16 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" model_discounted_reward_99_${prog}-${graph}_l2 $prog-$graph
#    done
#done

mkdir -p times/model
mv times/*.time times/model/