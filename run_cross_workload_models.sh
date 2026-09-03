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

Runs these cross-workload model measurements:
  bc-kron.sg model    with bc-twitter.sg workload
  bc-twitter.sg model with bc-kron.sg workload
  pr-kron.sg model    with pr-twitter.sg workload
  pr-twitter.sg model with pr-kron.sg workload

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

MODEL_BASES=(
    "bc-kron.sg"
    "bc-twitter.sg"
    "pr-kron.sg"
    "pr-twitter.sg"
)

WORKLOAD_IDS=(
    "bc-twitter.sg"
    "bc-kron.sg"
    "pr-twitter.sg"
    "pr-kron.sg"
)

trap 'run_measurement_teardown' EXIT

for index in "${!MODEL_BASES[@]}"; do
    model_base=${MODEL_BASES[index]}
    workload_id=${WORKLOAD_IDS[index]}
    output="wl_${workload_id}_model_${model_base}"

    echo "== Running ${model_base} model with ${workload_id} workload; output: ${output} =="
    if ! MODEL_BASE_OVERRIDE="${model_base}" \
            MODEL_OUTPUT_OVERRIDE="${output}" \
            "${SCRIPT_DIR}/measurement_model.sh" \
            "${PLATFORM_ARGS[@]}" "${SIZE_MIB}" "${RUN_ID}" "${LIB_SUFFIX}" "${workload_id}"; then
        echo "ERROR: ${model_base} model with ${workload_id} workload failed" >&2
        exit 1
    fi
done

run_measurement_teardown
trap - EXIT

echo "All cross-workload model measurements complete"
