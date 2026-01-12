#!/bin/bash

KB=$((1024))
MB=$((1024*KB))
GB=$((1024*MB))

function run_program {
    PROGRAM=$1
    MODEL=$2
    OUTPUT=$3

    rm -f times/${OUTPUT}.time
    rm -f logs/${OUTPUT}.log

    { time numactl -N0 --preferred=0 -- taskset -c 0-9,20-29 \
        sudo \
        LD_PRELOAD=$PWD/libraries/libhemem-${MODEL}.so \
        $PROGRAM 2>&1 ; } 2> "times/${OUTPUT}.time"
    mv output.log "logs/${OUTPUT}.log"
}

#run_program "/users/zimooo2/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpch-sf100/.*benchmark --threads=16" logging DuckDB-TPCH-sf100
#
## Call for all D size NPB programs
#programs=("bt.D.x" "cg.D.x" "ep.D.x" "lu.D.x" "mg.D.x" "sp.D.x" "ua.D.x")
#for prog in "${programs[@]}"; do
#    echo "Runn#ing NPB program: $prog"
#    run_program /users/zimooo2/NPB3.4.3/NPB3.4-OMP/bin/$prog logging $prog
#done
#
run_program "/users/zimooo2/XSBench/openmp-threading/XSBench -t 20 -g 50000 -p 20000000" logging XSBench
#
#
#
## GAPBS programs for twitter and kron graphs
#gapbs_programs=("bc" "bfs" "cc_sv" "cc" "pr" "pr_spmv" "sssp" "tc")
#graphs=("twitter.sg" "kron.sg")
#for graph in "${graphs[@]}"; do
#    for prog in "${gapbs_programs[@]}"; do
#        echo "Running GAPBS program: $prog on graph: $graph"
#        run_program "/users/zimooo2/gapbs/$prog -n 16 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" logging $prog-$graph
#    done
#done