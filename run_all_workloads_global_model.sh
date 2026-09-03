#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

measurement_init_platform_from_args "$@" || exit 1
ARGS=("${MEASUREMENT_REMAINING_ARGS[@]}")
PLATFORM_ARGS=(--platform "${MEASUREMENT_PLATFORM}")

print_usage() {
    cat <<EOF
Usage: $0 [--platform c220g5|gsl_optane] <sizeMiB> <runNumber> [libSuffix]

Runs every default workload under:
  ARMS
  the global model (model_discounted_reward_99_all_l2)

Outputs use directories named wl_<workload>_model_all.
Set GLOBAL_MODEL_BASE to override the model base (default: all).
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

for required_script in measurement_arms.sh measurement_model.sh; do
    if [[ ! -x "${SCRIPT_DIR}/${required_script}" ]]; then
        echo "ERROR: ${SCRIPT_DIR}/${required_script} not found or not executable" >&2
        exit 1
    fi
done

GLOBAL_MODEL_BASE=${GLOBAL_MODEL_BASE:-all}
GLOBAL_MODEL_NAME=$(measurement_build_model_name 99 "${GLOBAL_MODEL_BASE}" 1 10 0.1)
GLOBAL_MODEL_LIBRARY=$(
    measurement_build_model_library_path \
        "${SCRIPT_DIR}" "${GLOBAL_MODEL_NAME}" "${LIB_SUFFIX}"
)

if [[ ! -f "${GLOBAL_MODEL_LIBRARY}" ]]; then
    echo "ERROR: global model library not found: ${GLOBAL_MODEL_LIBRARY}" >&2
    echo "Expected model base: ${GLOBAL_MODEL_BASE}" >&2
    exit 1
fi

mapfile -t WORKLOAD_IDS < <(measurement_list_default_workloads)
if [[ ${#WORKLOAD_IDS[@]} -eq 0 ]]; then
    echo "ERROR: default workload list is empty" >&2
    exit 1
fi

trap 'run_measurement_teardown' EXIT

for workload_id in "${WORKLOAD_IDS[@]}"; do
    measurement_load_workload "${workload_id}" || exit 1

    if ! measurement_workload_supports_system arms; then
        echo "Skipping ${workload_id} ARMS run: workload does not support ARMS"
    else
        echo "== Running ${workload_id} with ARMS =="
        run_measurement_setup "${SIZE_MIB}"
        if ! "${SCRIPT_DIR}/measurement_arms.sh" \
                "${PLATFORM_ARGS[@]}" "${SIZE_MIB}" "${RUN_ID}" \
                "${LIB_SUFFIX}" "${workload_id}"; then
            echo "ERROR: ${workload_id} with ARMS failed" >&2
            exit 1
        fi
    fi

    if ! measurement_workload_supports_system model; then
        echo "Skipping ${workload_id} global-model run: workload does not support model"
        continue
    fi

    output="wl_${workload_id}_model_${GLOBAL_MODEL_BASE}"
    echo "== Running ${workload_id} with global model; output: ${output} =="
    if ! MODEL_BASE_OVERRIDE="${GLOBAL_MODEL_BASE}" \
            MODEL_OUTPUT_OVERRIDE="${output}" \
            "${SCRIPT_DIR}/measurement_model.sh" \
            "${PLATFORM_ARGS[@]}" "${SIZE_MIB}" "${RUN_ID}" \
            "${LIB_SUFFIX}" "${workload_id}"; then
        echo "ERROR: ${workload_id} with global model failed" >&2
        exit 1
    fi
done

run_measurement_teardown
trap - EXIT

echo "All ARMS and global-model measurements complete"
