#!/bin/bash

KB=$((1024))
MB=$((1024*KB))
GB=$((1024*MB))

function run_program {
    DRAMSIZE=$1
    PROGRAM=$2
    MODEL=$3
    OUTPUT=$4

    echo "Running ${PROGRAM} with DRAM size ${DRAMSIZE} bytes using model ${MODEL}"

    rm -f times/${OUTPUT}.time
    rm -f logs/${OUTPUT}.log

    { time numactl -N0 -m0 -- taskset -c 0-9,20-25 \
        sudo \
        LD_PRELOAD=$PWD/models/libhemem-${MODEL}.so \
        DRAMSIZE=${DRAMSIZE} \
        $PROGRAM 2>&1 ; } 2> "times/${OUTPUT}_${DRAMSIZE}.time"
    mv output.txt "logs/${OUTPUT}_${DRAMSIZE}.log"
}

sizes=($((256*MB)) $((1*GB)) $((4*GB)) $((8*GB)))
sizes=($((8*GB)))
#sizes=($((32*GB)))

for MemorySize in "${sizes[@]}"; do
    # Call for all D size NPB programs
    programs=("bt.D.x" "cg.D.x" "ep.D.x" "ft.D.x" "lu.D.x" "mg.D.x" "sp.D.x" "ua.D.x")
    programs=("mg.D.x")
    for prog in "${programs[@]}"; do
        echo "Running NPB program: $prog"
        run_program $MemorySize /users/zimooo2/NPB3.4.3/NPB3.4-OMP/bin/$prog model_discounted_reward_0.99_${prog}_l2 $prog
    done

    run_program $MemorySize "/users/zimooo2/XSBench/openmp-threading/XSBench -t 16 -g 50000 -p 20000000" model_discounted_reward_0.99_XSBench_l2 XSBench

    #run_program $MemorySize "/users/zimooo2/silo/out-perf.masstree/benchmarks/dbtest --verbose --bench tpcc --num-threads 16 --scale-factor 100 --ops-per-worker 1000000 --numa-memory 33736968110" model_l2 silo_tpcc
    
    #run_program $MemorySize "/users/zimooo2/silo/out-perf.masstree/benchmarks/dbtest --verbose --bench ycsb --num-threads 16 --scale-factor 40000 --ops-per-worker 10000000 --numa-memory 33736968110" model_l2 silo_ycsb

    # GAPBS programs for twitter and kron graphs
    gapbs_programs=("bc" "bfs" "cc_sv" "cc" "pr" "pr_spmv" "sssp" "tc")
    graphs=("twitter.sg" "kron.sg")
    for graph in "${graphs[@]}"; do
        for prog in "${gapbs_programs[@]}"; do
            echo "Running GAPBS program: $prog on graph: $graph"
            run_program $MemorySize "/users/zimooo2/gapbs/$prog -n 16 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" model_discounted_reward_0.99_${prog}-${graph}_l2 $prog-$graph
        done
    done
done

