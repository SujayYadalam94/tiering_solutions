#!/bin/bash

MEASUREMENT_WORKLOADS_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
MEASUREMENT_WORKLOADS_PATH="${MEASUREMENT_WORKLOADS_DIR}/workloads"

declare -ar MEASUREMENT_DEFAULT_WORKLOAD_IDS=(

    #"mg.D.x"
    #"DuckDB-TPCH-sf100"
    #"faiss_10M"
    #"XSBench"

    "bc-kron.sg"
    "pr-kron.sg"

    "bc-twitter.sg"
    "pr-twitter.sg"

    #"lulesh2.0_s400"
    


    #"DuckDB-TPCDS-sf100"
)

measurement_list_default_workloads() {
    local workload_id
    for workload_id in "${MEASUREMENT_DEFAULT_WORKLOAD_IDS[@]}"; do
        printf '%s\n' "${workload_id}"
    done
}

measurement_expand_workloads() {
    if [[ $# -eq 0 ]] || [[ $# -eq 1 && "$1" == "all" ]]; then
        measurement_list_default_workloads
        return
    fi

    local workload_id
    local emitted=0
    for workload_id in "$@"; do
        if [[ -z "${workload_id}" ]]; then
            continue
        fi
        printf '%s\n' "${workload_id}"
        emitted=1
    done

    if [[ ${emitted} -eq 0 ]]; then
        measurement_list_default_workloads
    fi
}

measurement_workload_file() {
    local workload_id=$1
    printf '%s/%s.sh\n' "${MEASUREMENT_WORKLOADS_PATH}" "${workload_id}"
}

measurement_reset_workload_vars() {
    unset WORKLOAD_ID
    unset WORKLOAD_OUTPUT
    unset WORKLOAD_COMMAND
    unset WORKLOAD_EXE_NAME
    unset WORKLOAD_MODEL_BASE
    unset WORKLOAD_RUNTIME_INPUT_SOURCE
    unset WORKLOAD_RUNTIME_INPUT_TARGET
    unset WORKLOAD_VIRTUAL_STEP_SAMPLES
    unset WORKLOAD_SYSTEMS
}

measurement_load_workload() {
    local workload_id=$1
    local workload_file

    workload_file=$(measurement_workload_file "${workload_id}")
    if [[ ! -f "${workload_file}" ]]; then
        echo "ERROR: unknown workload '${workload_id}' (expected ${workload_file})" >&2
        return 1
    fi

    measurement_reset_workload_vars
    # shellcheck disable=SC1090
    source "${workload_file}"

    if [[ -z "${WORKLOAD_VIRTUAL_STEP_SAMPLES:-}" ]]; then
        WORKLOAD_VIRTUAL_STEP_SAMPLES=3162
    fi

    if [[ -z "${WORKLOAD_ID:-}" || -z "${WORKLOAD_OUTPUT:-}" || -z "${WORKLOAD_COMMAND:-}" ||
          -z "${WORKLOAD_EXE_NAME:-}" || -z "${WORKLOAD_MODEL_BASE:-}" ]]; then
        echo "ERROR: workload '${workload_id}' is missing required metadata" >&2
        return 1
    fi
}

measurement_workload_supports_system() {
    local system_name=$1
    case " ${WORKLOAD_SYSTEMS:-arms hybridtier model logging} " in
        *" ${system_name} "*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}
