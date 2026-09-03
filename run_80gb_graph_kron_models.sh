#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"

measurement_init_platform_from_args "$@" || exit 1
ARGS=("${MEASUREMENT_REMAINING_ARGS[@]}")
PLATFORM_ARGS=(--platform "${MEASUREMENT_PLATFORM}")

print_usage() {
    cat <<EOF
Usage: $0 [--platform c220g5|gsl_optane] <sizeMiB> <runNumber> [libSuffix]

Runs both GAPBS workloads on the approximately 80 GB graph under:
  ARMS
  bc with the original bc-kron.sg model
  pr with the original pr-kron.sg model

The graph defaults to:
  \${BENCH_ROOT}/gapbs/benchmark/graph-80GB.sg

Set GAPBS_80GB_GRAPH to override that path.
Outputs use directories named wl_<workload>_model_<model>.
EOF
}

for arg in "${ARGS[@]}"; do
    if [[ "${arg}" == "--help" || "${arg}" == "-h" ]]; then
        print_usage
        exit 0
    fi
done

SIZE_MIB=${ARGS[0]:-}
RUN_ID=${ARGS[1]:-}
LIB_SUFFIX=${ARGS[2]:-}

if [[ -z "${SIZE_MIB}" || -z "${RUN_ID}" || ${#ARGS[@]} -gt 3 ]]; then
    print_usage >&2
    exit 1
fi

if [[ ! -x "${SCRIPT_DIR}/measurement_model.sh" ]]; then
    echo "ERROR: ${SCRIPT_DIR}/measurement_model.sh not found or not executable" >&2
    exit 1
fi

if [[ ! -x "${SCRIPT_DIR}/measurement_arms.sh" ]]; then
    echo "ERROR: ${SCRIPT_DIR}/measurement_arms.sh not found or not executable" >&2
    exit 1
fi

GAPBS_80GB_GRAPH=${GAPBS_80GB_GRAPH:-${BENCH_ROOT}/gapbs/benchmark/graph-80GB.sg}
export GAPBS_80GB_GRAPH

if [[ ! -f "${GAPBS_80GB_GRAPH}" ]]; then
    echo "ERROR: 80 GB graph not found: ${GAPBS_80GB_GRAPH}" >&2
    exit 1
fi

MODEL_BASES=(
    "bc-kron.sg"
    "pr-kron.sg"
)

WORKLOAD_IDS=(
    "bc-graph-80GB.sg"
    "pr-graph-80GB.sg"
)

trap 'run_measurement_teardown' EXIT

for index in "${!MODEL_BASES[@]}"; do
    model_base=${MODEL_BASES[index]}
    workload_id=${WORKLOAD_IDS[index]}
    output="wl_${workload_id}_model_${model_base}"

    echo "== Running ${workload_id} with ARMS =="
    run_measurement_setup "${SIZE_MIB}"
    if ! "${SCRIPT_DIR}/measurement_arms.sh" \
            "${PLATFORM_ARGS[@]}" "${SIZE_MIB}" "${RUN_ID}" "${LIB_SUFFIX}" "${workload_id}"; then
        echo "ERROR: ${workload_id} with ARMS failed" >&2
        exit 1
    fi

    echo "== Running ${workload_id} with ${model_base} model; output: ${output} =="
    if ! MODEL_BASE_OVERRIDE="${model_base}" \
            MODEL_OUTPUT_OVERRIDE="${output}" \
            "${SCRIPT_DIR}/measurement_model.sh" \
            "${PLATFORM_ARGS[@]}" "${SIZE_MIB}" "${RUN_ID}" "${LIB_SUFFIX}" "${workload_id}"; then
        echo "ERROR: ${workload_id} with ${model_base} model failed" >&2
        exit 1
    fi
done

run_measurement_teardown
trap - EXIT

echo "All ARMS and original-Kron-model measurements on the 80 GB graph complete"
