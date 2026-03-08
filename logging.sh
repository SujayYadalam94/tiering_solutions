#!/bin/bash

KB=$((1024))
MB=$((1024*KB))
GB=$((1024*MB))

RUNS=${1:-5}
START_RUN=${START_RUN:-1}

mkdir -p times logs

function run_program {
    PROGRAM=$1
    MODEL=$2
    OUTPUT=$3
    RUN=$4

    TIME_DIR="$PWD/times/${OUTPUT}"
    LOG_DIR="$PWD/logs/${OUTPUT}"
    TIME_BASENAME="run${RUN}"

    mkdir -p "$TIME_DIR" "$LOG_DIR"

    rm -f "${TIME_DIR}/${TIME_BASENAME}.time"
    rm -f "${LOG_DIR}/${TIME_BASENAME}.log"
    for part in $(seq 0 9); do
        rm -f "${LOG_DIR}/${TIME_BASENAME}_${part}.log"
    done

    LOG_OUTPUT_PATH="${LOG_DIR}/${TIME_BASENAME}.log"
    { time numactl --preferred=0 -- taskset -c 0-15,32-47 \
        sudo \
        LOG_OUTPUT_PATH="${LOG_OUTPUT_PATH}" \
        LD_PRELOAD=$PWD/libraries/C220G5/libhemem-${MODEL}.so \
        $PROGRAM 2>&1 ; } 2> "${TIME_DIR}/${TIME_BASENAME}.time"
    echo "${LOG_DIR}/${TIME_BASENAME}.log"
}


for run in $(seq "${START_RUN}" "${RUNS}"); do


    run_program "/users/zimooo2/.venv/bin/python3 /users/zimooo2/big-ann-benchmarks/data/10M_benchmark.py --threads 16 --index-key HNSW,Flat --stress-mode latency --dataset openai" logging faiss_10M ${run}
    continue

    # Call for all D size NPB programs
    programs=("bt.D.x" "cg.D.x" "ep.D.x" "lu.D.x" "mg.D.x" "sp.D.x" "ua.D.x")
    programs=("mg.D.x")
    for prog in "${programs[@]}"; do
        echo "Running NPB program: $prog"
        run_program "OMP_NUM_THREADS=16 /users/zimooo2/NPB3.4.3/NPB3.4-OMP/bin/$prog" logging $prog ${run}
    done

    run_program "/users/zimooo2/XSBench/openmp-threading/XSBench -t 16 -g 50000 -p 20000000" logging XSBench ${run}

    # GAPBS programs for twitter and kron graphs

    gapbs_programs=("bc" "pr" )
    graphs=("twitter.sg")
    for graph in "${graphs[@]}"; do
        for prog in "${gapbs_programs[@]}"; do
            echo "Run ${run}: Running GAPBS program: $prog on graph: $graph"
            run_program "OMP_NUM_THREADS=16 /users/zimooo2/gapbs/$prog -n 40 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" logging $prog-$graph ${run}
        done
    done

    graphs=("kron.sg")
    for graph in "${graphs[@]}"; do
        for prog in "${gapbs_programs[@]}"; do
            echo "Run ${run}: Running GAPBS program: $prog on graph: $graph"
            run_program "OMP_NUM_THREADS=16 /users/zimooo2/gapbs/$prog -n 20 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" logging $prog-$graph ${run}
        done
    done

    gapbs_programs=("bfs")
    graphs=("twitter.sg")
    for graph in "${graphs[@]}"; do
        for prog in "${gapbs_programs[@]}"; do
            echo "Run ${run}: Running GAPBS program: $prog on graph: $graph"
            run_program "OMP_NUM_THREADS=16 /users/zimooo2/gapbs/$prog -n 400 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" logging $prog-$graph ${run}
        done
    done

    graphs=("kron.sg")
    for graph in "${graphs[@]}"; do
        for prog in "${gapbs_programs[@]}"; do
            echo "Run ${run}: Running GAPBS program: $prog on graph: $graph"
            run_program "OMP_NUM_THREADS=16 /users/zimooo2/gapbs/$prog -n 200 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" logging $prog-$graph ${run}
        done
    done

    run_program "OMP_NUM_THREADS=16 /users/zimooo2/LULESH/build/lulesh2.0 -i 10 -s 400" logging lulesh2.0_s400 ${run}

    run_program "/users/zimooo2/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpch-sf100/.*benchmark --threads=16" logging DuckDB-TPCH-sf100 ${run}
    run_program "/users/zimooo2/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpcds-sf100/.*benchmark --threads=16" logging DuckDB-TPCDS-sf100 ${run}
done

#run_program "/users/zimooo2/.venv/bin/python /users/zimooo2/dlrm/dlrm_s_pytorch.py --mini-batch-size=2048 --test-mini-batch-size=16384 --test-num-workers=0 --num-batches=300 --data-generation=random --arch-mlp-bot=512-512-64 --arch-mlp-top=1024-1024-1024-1 --arch-sparse-feature-size=64 --arch-embedding-size=1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000 --num-indices-per-lookup=100 --arch-interaction-op=dot --numpy-rand-seed=727 --print-freq=100" logging dlrm_s_pytorch


#run_program "/users/zimooo2/llama.cpp/build/bin/llama-bench -m /users/zimooo2/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf" logging llama_cpp-gpt_oss_120b
#run_program "python3 /users/zimooo2/pytorch_geometric/benchmark/runtime/main.py" logging pytorch_geometric

#run_benchmark "python3 /users/zimooo2/npbench/run_benchmark.py -f numba -p L -b azimint_naive" logging npbench_azimint_naive_numba_L
#run_benchmark "python3 /users/zimooo2/npbench/run_benchmark.py -f numba -p L -b channel_flow" logging npbench_channel_flow_numba_L

#run_program "python /users/zimooo2/rl-baselines3-zoo/train.py --algo sac --env MountainCarContinuous-v0 --eval-freq 10000 --eval-episodes 10 --n-eval-envs 1" logging rl_a3c_sac_mountaincar