#!/bin/bash
set -euo pipefail

SIZE_MIB=${1:-}
RUN_ID=${2:-}
PAGE_TYPE=${3:-huge} # regular | huge

if [[ -z "${SIZE_MIB}" || -z "${RUN_ID}" ]]; then
    echo "Usage: $0 <fastTierMiB> <runNumber> [pageType]" >&2
    echo "  pageType: regular (default) | huge" >&2
    exit 1
fi

if [[ "${PAGE_TYPE}" != "regular" && "${PAGE_TYPE}" != "huge" ]]; then
    echo "ERROR: invalid pageType '${PAGE_TYPE}' (expected regular|huge)" >&2
    exit 1
fi

if [[ ${EUID} -ne 0 ]]; then
    echo "This script must be run using sudo (use: sudo -E $0 ...)" >&2
    exit 1
fi

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
WORKSPACE_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)

# Convert MiB -> GiB (ceil), because HybridTier runtime takes FAST_MEMORY_SIZE_GB.
FAST_TIER_SIZE_GB=$(( (SIZE_MIB + 1023) / 1024 ))
if [[ ${FAST_TIER_SIZE_GB} -lt 1 ]]; then
    FAST_TIER_SIZE_GB=1
fi

echo "Fast tier size: ${SIZE_MIB} MiB (compiling as ${FAST_TIER_SIZE_GB} GB)"

time_root="${SCRIPT_DIR}/times"
log_root="${SCRIPT_DIR}/logs"
mkdir -p "${time_root}" "${log_root}" "${time_root}/hybridtier"

HYBRIDTIER_ROOT=${HYBRIDTIER_ROOT:-"${WORKSPACE_ROOT}/hybridtier-asplos25-artifact"}
HOOK_DIR="${HYBRIDTIER_ROOT}/hook"
HOOK_SO="${HOOK_DIR}/hook.so"

build_hook() {
    local exe_name=$1

    if [[ ! -d "${HOOK_DIR}" ]]; then
        echo "ERROR: HybridTier hook dir not found: ${HOOK_DIR}" >&2
        exit 1
    fi

    local dpage_define="HYBRIDTIER_REGULAR"
    if [[ "${PAGE_TYPE}" == "huge" ]]; then
        dpage_define="HYBRIDTIER_HUGE"
    fi

    pushd "${HOOK_DIR}" > /dev/null

    # Build exactly like run_exp_common.sh
    g++ -shared -fPIC -g hook.cpp -o hook.so -O3 \
        -ldl -lpthread -lnuma \
        -DFAST_MEMORY_SIZE_GB=${FAST_TIER_SIZE_GB} \
        -DTARGET_EXE_NAME=\"${exe_name}\" \
        -D${dpage_define}

    popd > /dev/null

    if [[ ! -f "${HOOK_SO}" ]]; then
        echo "ERROR: hook build failed; missing ${HOOK_SO}" >&2
        exit 1
    fi
}

maybe_drop_caches() {
    # Default: clear page cache unless CLEAR_CACHES=0.
    local clear=${CLEAR_CACHES:-1}
    if [[ "${clear}" == "1" ]]; then
        sync
        echo 3 > /proc/sys/vm/drop_caches || true
    fi
}

run_program() {
    local program_str=$1
    local exe_name=$2
    local output=$3
    local run=$4

    local time_basename="${SIZE_MIB}MiB_run${run}"
    local time_dir="${time_root}/hybridtier/${output}"
    local log_dir="${log_root}/${output}"

    mkdir -p "${time_dir}" "${log_dir}"

    local time_file="${time_dir}/${time_basename}.time"
    local log_file="${log_dir}/${time_basename}_hybridtier.log"

    rm -f "${time_file}" "${log_file}"

    maybe_drop_caches
    build_hook "${exe_name}"

    echo "Running ${output} (exe=${exe_name}) run ${run}"

    # Keep compute placement/pinning similar to existing ARM measurements.
    local pin="taskset -c 0-9,20-29"
    local numa="/usr/bin/numactl --cpunodebind=0"

    # Extract leading environment assignments (e.g., "OMP_NUM_THREADS=16 ") so we can
    # apply them without wrapping the workload in an extra shell process.
    local rest="${program_str}"
    local env_kv=""
    while [[ "${rest}" =~ ^([A-Za-z_][A-Za-z0-9_]*)=([^[:space:]]+)[[:space:]]+(.+)$ ]]; do
        env_kv+=" ${BASH_REMATCH[1]}=${BASH_REMATCH[2]}"
        rest="${BASH_REMATCH[3]}"
    done

    # Disable glob expansion so args like ".*benchmark" are passed literally.

    echo "${pin} ${numa} env LD_PRELOAD=\"${HOOK_SO}\"${env_kv} ${rest}"

    set -f
    {
        time eval "${pin} ${numa} env LD_PRELOAD=\"${HOOK_SO}\"${env_kv} ${rest}" &>> "${log_file}"
    } 2> "${time_file}"
    set +f

    echo "test"
}

# ---------------- Workloads (mirrors measurement_arms.sh) ----------------

run_program "/users/zimooo2/big-ann-benchmarks/.venv/bin/python3 /users/zimooo2/big-ann-benchmarks/data/10M_benchmark.py --threads 16 --index-key HNSW,Flat --stress-mode latency --dataset openai" "python3" faiss_10M "${RUN_ID}"

exit

run_program "OMP_NUM_THREADS=16 /users/zimooo2/LULESH/build/lulesh2.0 -i 10 -s 400" "lulesh2.0" lulesh2.0_s400 "${RUN_ID}"

run_program "OMP_NUM_THREADS=16 /users/zimooo2/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpch-sf100/.*benchmark --threads=16" "benchmark_runner" DuckDB-TPCH-sf100 "${RUN_ID}"
run_program "OMP_NUM_THREADS=16 /users/zimooo2/duckdb/build/release/benchmark/benchmark_runner benchmark/large/tpcds-sf100/.*benchmark --threads=16" "benchmark_runner" DuckDB-TPCDS-sf100 "${RUN_ID}"

## Call for all D size NPB programs
programs=("mg.D.x")
for prog in "${programs[@]}"; do
    echo "Running NPB program: $prog"
    run_program "OMP_NUM_THREADS=16 /users/zimooo2/NPB3.4.3/NPB3.4-OMP/bin/$prog" "$prog" "$prog" "${RUN_ID}"
done

#echo "XSBench run ${RUN_ID}"
run_program "OMP_NUM_THREADS=16 /users/zimooo2/XSBench/openmp-threading/XSBench -t 16 -g 50000 -p 20000000" "XSBench" XSBench "${RUN_ID}"

## GAPBS programs for twitter and kron graphs
#gapbs_programs=("bc" "bfs" "cc_sv" "cc" "pr" "pr_spmv" "sssp" "tc")
#graphs=("twitter.sg" "kron.sg")

gapbs_programs=("bc" "bfs" "pr")
graphs=("twitter.sg")
for graph in "${graphs[@]}"; do
    for prog in "${gapbs_programs[@]}"; do
        echo "Running GAPBS program: $prog on graph: $graph"
        run_program "OMP_NUM_THREADS=16 /users/zimooo2/gapbs/$prog -n 40 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" "$prog" "$prog-$graph" "${RUN_ID}"
    done
done

gapbs_programs=("bc" "bfs" "pr")
graphs=("kron.sg")
for graph in "${graphs[@]}"; do
    for prog in "${gapbs_programs[@]}"; do
        echo "Running GAPBS program: $prog on graph: $graph"
        run_program "OMP_NUM_THREADS=16 /users/zimooo2/gapbs/$prog -n 20 -f /users/zimooo2/gapbs/benchmark/graphs/$graph" "$prog" "$prog-$graph" "${RUN_ID}"
    done
done
