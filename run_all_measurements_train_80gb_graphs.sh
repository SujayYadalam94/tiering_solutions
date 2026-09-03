#!/bin/bash

: "${MEASUREMENT_TIMEOUT_SECONDS:=14400}"
export MEASUREMENT_TIMEOUT_SECONDS

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

measurement_init_platform_from_args "$@" || exit 1
PLATFORM_ARGS=(--platform "${MEASUREMENT_PLATFORM}")

print_usage() {
    cat <<EOF
Usage: $0 [--platform c220g5|gsl_optane]

Runs BC and PR on the approximately 80 GB GAPBS graph with the ARMS
all-NUMA training library, converts the generated training traces to parquet,
and collects each trace with its timing and peak-DRAM artifacts.

The graph defaults to:
  \${BENCH_ROOT}/gapbs/benchmark/graph-80GB.sg

Environment overrides:
  GAPBS_80GB_GRAPH       graph path
  TRAIN_SIZE_MIB         measurement size label (default: 100101)
  TRAIN_RUNS             number of runs per workload (default: 10)
  TRAIN_START_RUN        first run number (default: 1)
  MEASUREMENT_TIMEOUT_SECONDS per-workload timeout (default: 14400)
  NUMA_MEM_NODES          launch NUMA nodes (default: all platform tiers)
  RUN_LABEL_SUFFIX       run label suffix (default: _train)
  ARMS_LIB_SUFFIX        ARMS library suffix (default: _near_train_all_numa)
  COLLECT_LOGS_DIR       collected-artifact destination
EOF
}

for arg in "${MEASUREMENT_REMAINING_ARGS[@]}"; do
    if [[ "${arg}" == "--help" || "${arg}" == "-h" ]]; then
        print_usage
        exit 0
    fi
done

if [[ ${#MEASUREMENT_REMAINING_ARGS[@]} -ne 0 ]]; then
    print_usage >&2
    exit 1
fi

GAPBS_80GB_GRAPH=${GAPBS_80GB_GRAPH:-${BENCH_ROOT}/gapbs/benchmark/graph-80GB.sg}
export GAPBS_80GB_GRAPH

TRAIN_SIZE_MIB=${TRAIN_SIZE_MIB:-100101}
TRAIN_RUNS=${TRAIN_RUNS:-10}
TRAIN_START_RUN=${TRAIN_START_RUN:-2}
ALL_NUMA_MEM_NODES=${NUMA_MEM_NODES}
RUN_LABEL_SUFFIX=${RUN_LABEL_SUFFIX:-_train}
ARMS_LIB_SUFFIX=${ARMS_LIB_SUFFIX:-_near_train_all_numa}
COLLECT_TIMESTAMP=$(date '+%Y%m%d_%H%M%S')
COLLECT_LOGS_DIR=${COLLECT_LOGS_DIR:-"${SCRIPT_DIR}/collected_logs/${MEASUREMENT_PLATFORM}/arms_near_train_80gb_graphs_${COLLECT_TIMESTAMP}"}
LOG_CONVERTER_SCRIPT=${LOG_CONVERTER_SCRIPT:-"${SCRIPT_DIR}/logs/filter_v3_split_runs.py"}
LOG_CONVERTER_VENV_PYTHON=${LOG_CONVERTER_VENV_PYTHON:-"${SCRIPT_DIR}/../tiering_models/process_data/data/.venv/bin/python"}

WORKLOAD_IDS=(
    "bc-graph-80GB.sg"
    "pr-graph-80GB.sg"
)

copy_required_artifact() {
    local source_file=$1
    local destination_dir=$2

    if [[ ! -f "${source_file}" ]]; then
        echo "ERROR: expected artifact was not produced: ${source_file}" >&2
        return 1
    fi
    cp -f "${source_file}" "${destination_dir}/"
}

copy_optional_artifact() {
    local source_file=$1
    local destination_dir=$2

    if [[ -f "${source_file}" ]]; then
        cp -f "${source_file}" "${destination_dir}/"
    else
        echo "WARNING: missing optional artifact ${source_file}"
    fi
}

convert_logs_to_parquet() {
    local workload_output=$1
    local log_dir="${SCRIPT_DIR}/logs/${workload_output}"

    "${LOG_CONVERTER_PYTHON}" "${LOG_CONVERTER_SCRIPT}" "${log_dir}"
}

cleanup_stale_training_temps() {
    local size_mib=$1
    local run_label=$2
    local workload_output=$3
    local basename="${size_mib}MiB_run${run_label}_arms"
    local log_dir="${SCRIPT_DIR}/logs/${workload_output}"
    local stale_files=()

    shopt -s nullglob
    stale_files=("${log_dir}/${basename}"_*_part_*_*.tmp)
    shopt -u nullglob

    if [[ ${#stale_files[@]} -gt 0 ]]; then
        echo "Removing ${#stale_files[@]} stale temporary trace parts from a failed ${workload_output} run"
        rm -f -- "${stale_files[@]}"
    fi
}

verify_measurement_completed() {
    local size_mib=$1
    local run_label=$2
    local workload_output=$3
    local time_file="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/arms/${workload_output}/${size_mib}MiB_run${run_label}.time"
    local exit_status
    local timed_out

    if [[ ! -f "${time_file}" ]]; then
        echo "ERROR: measurement status file was not produced: ${time_file}" >&2
        return 1
    fi

    exit_status=$(awk -F= '/^exit_status=/{print $2}' "${time_file}" | tail -n1)
    timed_out=$(awk -F= '/^timed_out=/{print $2}' "${time_file}" | tail -n1)

    if [[ "${timed_out}" == "1" ]]; then
        echo "ERROR: ${workload_output} run ${run_label} timed out after ${MEASUREMENT_TIMEOUT_SECONDS}s; no complete training trace is available" >&2
        return 1
    fi
    if [[ -z "${exit_status}" || "${exit_status}" != "0" ]]; then
        echo "ERROR: ${workload_output} run ${run_label} exited with status ${exit_status:-unknown}" >&2
        return 1
    fi
}

collect_training_artifacts() {
    local size_mib=$1
    local run_label=$2
    local workload_output=$3
    local basename="${size_mib}MiB_run${run_label}"
    local source_time_dir="${SCRIPT_DIR}/times/${MEASUREMENT_PLATFORM}/arms/${workload_output}"
    local source_log_dir="${SCRIPT_DIR}/logs/${workload_output}"
    local destination_dir="${COLLECT_LOGS_DIR}/${workload_output}"

    mkdir -p "${destination_dir}"

    copy_required_artifact "${source_log_dir}/${basename}_arms.parquet" "${destination_dir}" || return 1
    copy_required_artifact "${source_time_dir}/${basename}.time" "${destination_dir}" || return 1
    copy_optional_artifact "${source_time_dir}/max_dram_hugepages_${basename}.log" "${destination_dir}"
}

if [[ ! -f "${GAPBS_80GB_GRAPH}" ]]; then
    echo "ERROR: 80 GB graph not found: ${GAPBS_80GB_GRAPH}" >&2
    exit 1
fi

if [[ ! -x "${SCRIPT_DIR}/measurement_arms.sh" ]]; then
    echo "ERROR: ${SCRIPT_DIR}/measurement_arms.sh not found or not executable" >&2
    exit 1
fi

ARMS_LIBRARY="${SCRIPT_DIR}/libraries/${MEASUREMENT_LIBRARY_PROFILE_DIR}/libhemem-arms${ARMS_LIB_SUFFIX}.so"
if [[ ! -f "${ARMS_LIBRARY}" ]]; then
    echo "ERROR: ARMS training library not found: ${ARMS_LIBRARY}" >&2
    exit 1
fi

if [[ ! "${TRAIN_SIZE_MIB}" =~ ^[0-9]+$ || ! "${TRAIN_RUNS}" =~ ^[0-9]+$ ||
      ! "${TRAIN_START_RUN}" =~ ^[0-9]+$ ]] ||
        (( TRAIN_RUNS < 1 || TRAIN_START_RUN < 1 )); then
    echo "ERROR: TRAIN_SIZE_MIB must be a nonnegative integer; TRAIN_RUNS and TRAIN_START_RUN must be positive integers" >&2
    exit 1
fi

if [[ ! -f "${LOG_CONVERTER_SCRIPT}" ]]; then
    echo "ERROR: converter script not found at ${LOG_CONVERTER_SCRIPT}" >&2
    exit 1
fi

if [[ -x "${LOG_CONVERTER_VENV_PYTHON}" ]]; then
    LOG_CONVERTER_PYTHON=${LOG_CONVERTER_VENV_PYTHON}
elif command -v python3 >/dev/null 2>&1; then
    LOG_CONVERTER_PYTHON=$(command -v python3)
else
    echo "ERROR: no Python interpreter found for trace conversion" >&2
    exit 1
fi

if ! "${LOG_CONVERTER_PYTHON}" -c 'import pandas; import pyarrow' >/dev/null 2>&1; then
    echo "ERROR: ${LOG_CONVERTER_PYTHON} needs pandas and pyarrow for trace conversion" >&2
    exit 1
fi

mkdir -p "${COLLECT_LOGS_DIR}"

echo "Using measurement platform: ${MEASUREMENT_PLATFORM}"
echo "Using 80 GB graph: ${GAPBS_80GB_GRAPH}"
echo "Using all platform NUMA tiers: ${ALL_NUMA_MEM_NODES}"
echo "Using ARMS library: ${ARMS_LIBRARY}"
echo "Using workload timeout: ${MEASUREMENT_TIMEOUT_SECONDS}s"
echo "Collecting training artifacts in: ${COLLECT_LOGS_DIR}"

trap 'run_measurement_teardown' EXIT

last_run=$((TRAIN_START_RUN + TRAIN_RUNS - 1))
for run in $(seq "${TRAIN_START_RUN}" "${last_run}"); do
    run_label="${run}${RUN_LABEL_SUFFIX}"

    for workload_id in "${WORKLOAD_IDS[@]}"; do
        measurement_load_workload "${workload_id}" || exit 1
        if ! measurement_workload_supports_system arms; then
            echo "ERROR: ${workload_id} does not support ARMS" >&2
            exit 1
        fi

        echo "== Running ${workload_id}, run ${run_label} =="
        cleanup_stale_training_temps "${TRAIN_SIZE_MIB}" "${run_label}" "${WORKLOAD_OUTPUT}"
        run_measurement_setup "${TRAIN_SIZE_MIB}" all_numa || exit 1

        if ! NUMA_MEM_NODES="${ALL_NUMA_MEM_NODES}" \
                "${SCRIPT_DIR}/measurement_arms.sh" \
                "${PLATFORM_ARGS[@]}" "${TRAIN_SIZE_MIB}" "${run_label}" \
                "${ARMS_LIB_SUFFIX}" "${workload_id}"; then
            echo "ERROR: ${workload_id} run ${run_label} failed" >&2
            exit 1
        fi

        if ! verify_measurement_completed "${TRAIN_SIZE_MIB}" "${run_label}" "${WORKLOAD_OUTPUT}"; then
            cleanup_stale_training_temps "${TRAIN_SIZE_MIB}" "${run_label}" "${WORKLOAD_OUTPUT}"
            exit 1
        fi
        convert_logs_to_parquet "${WORKLOAD_OUTPUT}" || exit 1
        collect_training_artifacts "${TRAIN_SIZE_MIB}" "${run_label}" "${WORKLOAD_OUTPUT}" || exit 1
    done
done

run_measurement_teardown
trap - EXIT

echo "All 80 GB BC/PR training-trace runs complete"
echo "Collected artifacts directory: ${COLLECT_LOGS_DIR}"
