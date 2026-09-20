#!/usr/bin/env bash
# Collect ARMS timing/ranking traces and the two missing MANTA traces.
set -e -o pipefail

COLLECTION_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "${COLLECTION_DIR}"

usage() {
    cat <<'EOF'
Usage: bash run_arms_and_missing_models.sh [--platform c220g5|gsl_optane] [--arms-only] [--arms-workload=ID ...] [--dry-run]

Collects ARMS training traces for BC/PR Twitter and Kron, DuckDB, XSBench,
FAISS, and MG, then runs workload-specific model logging for FAISS and MG only.
Use --arms-only to collect just the eight ARMS traces.
Use repeated --arms-workload=ID options to select a subset of ARMS workloads.

Environment overrides:
  COLLECTION_RUN   Run number for both collectors (default: 12)
  ARMS_SIZE_MIB    ARMS DRAM budget in MiB (default: 10090)
  LOGGING_SIZE_MIB MANTA logging setup size in MiB (default: 10090)
  BUILD_LIBRARIES  Rebuild the required libraries: 1 or 0 (default: 1)
  BUILD_JOBS       Parallel make jobs (default: 4)
  PARQUET_PYTHON   Python with PyArrow (default: repository .venv, then python3)

Completed traces with successful .time records are skipped only after ARMS
timing fields are validated. Older ARMS traces without timings require a new
COLLECTION_RUN; they are preserved. An existing nonempty
trace without a successful .time record is preserved and reported as an error;
choose another COLLECTION_RUN or move those artifacts before rerunning.

ARMS outputs: logs/<workload>/<ARMS_SIZE_MIB>MiB_run<COLLECTION_RUN>_train_arms.parquet
MANTA outputs: logs/{faiss_10M,mg.D.x}/run<COLLECTION_RUN>.parquet
Each invocation also keeps a matching .console.log beside its trace.

ARMS records feature/scoring/total costs per policy step; logging.sh uses virtual
steps. ARMS summaries are saved to logs/plots/arms_timing_summary_run<RUN>.csv.
EOF
}

DRY_RUN=0
ARMS_ONLY=0
SELECTED_ARMS_WORKLOADS=()
PLATFORM_OPTIONS=()
for arg in "$@"; do
    case "${arg}" in
        --dry-run) DRY_RUN=1 ;;
        --arms-only) ARMS_ONLY=1 ;;
        --arms-workload=*) SELECTED_ARMS_WORKLOADS+=("${arg#*=}") ;;
        --help|-h) usage; exit 0 ;;
        *) PLATFORM_OPTIONS+=("${arg}") ;;
    esac
done

source "${COLLECTION_DIR}/measurement_common.sh"
source "${COLLECTION_DIR}/measurement_workloads.sh"
measurement_init_platform_from_args "${PLATFORM_OPTIONS[@]}"
if (( ${#MEASUREMENT_REMAINING_ARGS[@]} != 0 )); then
    usage >&2
    exit 1
fi

COLLECTION_RUN=${COLLECTION_RUN:-12}
ARMS_SIZE_MIB=${ARMS_SIZE_MIB:-10090}
LOGGING_SIZE_MIB=${LOGGING_SIZE_MIB:-10090}
BUILD_LIBRARIES=${BUILD_LIBRARIES:-1}
BUILD_JOBS=${BUILD_JOBS:-4}
if [[ ! "${COLLECTION_RUN}" =~ ^[1-9][0-9]*$ ||
      ! "${ARMS_SIZE_MIB}" =~ ^(0|[1-9][0-9]*)$ ||
      ! "${LOGGING_SIZE_MIB}" =~ ^(0|[1-9][0-9]*)$ ||
      ! "${BUILD_JOBS}" =~ ^[1-9][0-9]*$ ||
      ! "${BUILD_LIBRARIES}" =~ ^[01]$ ]]; then
    echo "ERROR: invalid collection run, size, or build setting; see --help." >&2
    exit 1
fi

ARMS_WORKLOADS=(
    bc-twitter.sg bc-kron.sg pr-twitter.sg pr-kron.sg
    DuckDB-TPCH-sf100-original XSBench faiss_10M mg.D.x
)
if (( ${#SELECTED_ARMS_WORKLOADS[@]} )); then
    for selected in "${SELECTED_ARMS_WORKLOADS[@]}"; do
        valid=0
        for workload in "${ARMS_WORKLOADS[@]}"; do
            [[ "${selected}" != "${workload}" ]] || valid=1
        done
        if (( ! valid )); then
            echo "ERROR: unsupported ARMS workload: ${selected}" >&2
            exit 1
        fi
    done
    mapfile -t ARMS_WORKLOADS < <(printf '%s\n' "${SELECTED_ARMS_WORKLOADS[@]}" | awk '!seen[$0]++')
fi
MANTA_WORKLOADS=(faiss_10M mg.D.x)
ARMS_RUN_LABEL="${COLLECTION_RUN}_train"
ARMS_LIBRARY="libraries/${MEASUREMENT_LIBRARY_PROFILE_DIR}/libhemem-arms_train.so"

if [[ -z "${PARQUET_PYTHON:-}" ]]; then
    PARQUET_PYTHON=python3
    if [[ -x "${COLLECTION_DIR}/.venv/bin/python" ]]; then
        PARQUET_PYTHON="${COLLECTION_DIR}/.venv/bin/python"
    fi
fi
if (( ARMS_ONLY )); then
    MANTA_WORKLOADS=()
fi
REQUIRED_LIBRARIES=("${ARMS_LIBRARY}")
for workload in "${MANTA_WORKLOADS[@]}"; do
    measurement_load_workload "${workload}"
    REQUIRED_LIBRARIES+=("$(measurement_build_logging_library_path . "${WORKLOAD_MODEL_BASE}" 99)")
done

validate_model_trace() {
    "${PARQUET_PYTHON}" "${COLLECTION_DIR}/scripts/model_collection_metadata.py" check "$1" \
        "${COLLECTION_DIR}/models/model_discounted_reward_99_${WORKLOAD_MODEL_BASE}_l2.o" \
        "$(measurement_build_logging_library_path "${COLLECTION_DIR}" "${WORKLOAD_MODEL_BASE}" 99)"
}

validate_arms_timing() {
    "${PARQUET_PYTHON}" "${COLLECTION_DIR}/scripts/arms_timing_summary.py" --check "$1"
}

write_arms_summary() {
    ARMS_PARQUETS=()
    for workload in "${ARMS_WORKLOADS[@]}"; do
        measurement_load_workload "${workload}"
        ARMS_PARQUETS+=("${COLLECTION_DIR}/logs/${WORKLOAD_OUTPUT}/${ARMS_SIZE_MIB}MiB_run${ARMS_RUN_LABEL}_arms.parquet")
    done
    "${PARQUET_PYTHON}" "${COLLECTION_DIR}/scripts/arms_timing_summary.py" \
        --output "${COLLECTION_DIR}/logs/plots/arms_timing_summary_run${COLLECTION_RUN}${SELECTED_ARMS_WORKLOADS:+_selected}.csv" "${ARMS_PARQUETS[@]}"
}

successful_trace() {
    local parquet=$1 time_file=$2
    [[ -s "${parquet}" && -s "${time_file}" ]] || return 1
    awk -F= '
        BEGIN { status = "missing"; timed_out = "missing" }
        /^exit_status=/ { status = $2 }
        /^timed_out=/ { timed_out = $2 }
        END { exit !(status == "0" && timed_out == "0") }
    ' "${time_file}"
}

# Resolve paths once, using the same workload registry as both collectors.
PENDING_KINDS=()
PENDING_WORKLOADS=()
PENDING_PARQUETS=()
PENDING_TIMES=()
for kind in arms manta; do
    if [[ "${kind}" == arms ]]; then
        selected_workloads=("${ARMS_WORKLOADS[@]}")
    else
        selected_workloads=("${MANTA_WORKLOADS[@]}")
    fi
    for workload in "${selected_workloads[@]}"; do
        measurement_load_workload "${workload}"
        if [[ "${kind}" == arms ]]; then
            measurement_workload_supports_system arms
            stem="${ARMS_SIZE_MIB}MiB_run${ARMS_RUN_LABEL}"
            parquet="${COLLECTION_DIR}/logs/${WORKLOAD_OUTPUT}/${stem}_arms.parquet"
            time_file="${COLLECTION_DIR}/times/${MEASUREMENT_PLATFORM}/arms/${WORKLOAD_OUTPUT}/${stem}.time"
        else
            measurement_workload_supports_system logging
            parquet="${COLLECTION_DIR}/logs/${WORKLOAD_OUTPUT}/run${COLLECTION_RUN}.parquet"
            time_file="${COLLECTION_DIR}/times/${MEASUREMENT_PLATFORM}/${WORKLOAD_OUTPUT}/run${COLLECTION_RUN}.time"
        fi
        if successful_trace "${parquet}" "${time_file}"; then
            if [[ "${kind}" == arms ]] && ! validate_arms_timing "${parquet}"; then
                echo "ERROR: existing ARMS trace has no valid timing telemetry. Use a new COLLECTION_RUN (for example, 12)." >&2
                exit 1
            fi
            if [[ "${kind}" == manta ]] && ! validate_model_trace "${parquet}"; then
                echo "ERROR: existing model trace lacks matching workload-model identity or timing logs. Use a new COLLECTION_RUN." >&2
                exit 1
            fi
            echo "Skipping completed ${kind}: ${workload}"
            continue
        fi
        if [[ -s "${parquet}" ]]; then
            echo "ERROR: preserving existing trace without a successful timing record: ${parquet}" >&2
            echo "Choose another COLLECTION_RUN or move the existing artifacts first." >&2
            exit 1
        fi
        PENDING_KINDS+=("${kind}")
        PENDING_WORKLOADS+=("${workload}")
        PENDING_PARQUETS+=("${parquet}")
        PENDING_TIMES+=("${time_file}")
        printf '%s: %s -> %s\n' "${kind}" "${workload}" "${parquet}"
        if [[ "${kind}" == manta ]]; then
            echo "  model library: $(measurement_build_logging_library_path "${COLLECTION_DIR}" "${WORKLOAD_MODEL_BASE}" 99)"
        fi
    done
done

echo "Platform: ${MEASUREMENT_PLATFORM}; run: ${COLLECTION_RUN}; ARMS: ${ARMS_SIZE_MIB} MiB; MANTA logging: ${LOGGING_SIZE_MIB} MiB"
if (( DRY_RUN )); then
    echo "Dry run: no builds, setup, or workloads executed."
    exit 0
fi
# Fail before privileged setup or a long workload if validation cannot run.
"${PARQUET_PYTHON}" -c 'import pandas; import pyarrow.parquet'
if (( ${#PENDING_WORKLOADS[@]} == 0 )); then
    write_arms_summary
    echo "All requested traces are already complete."
    exit 0
fi

if [[ "${BUILD_LIBRARIES}" == 1 ]]; then
    make -B -j"${BUILD_JOBS}" "PLATFORMS=${MEASUREMENT_LIBRARY_PROFILE_DIR}" \
        "PARQUET_PYTHON=${PARQUET_PYTHON}" ARMS_TIMING_TELEMETRY=true MODEL_TIMING_TELEMETRY=true "${REQUIRED_LIBRARIES[@]}"
fi
for library in "${REQUIRED_LIBRARIES[@]}"; do
    if [[ ! -f "${library}" ]]; then
        echo "ERROR: required library is missing: ${library}" >&2
        exit 1
    fi
done

SETUP_ACTIVE=0
cleanup_collection() {
    local status=$?
    trap - EXIT
    if (( SETUP_ACTIVE )); then
        run_measurement_teardown || true
    fi
    exit "${status}"
}
trap cleanup_collection EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

for index in "${!PENDING_WORKLOADS[@]}"; do
    workload=${PENDING_WORKLOADS[index]}
    parquet=${PENDING_PARQUETS[index]}
    time_file=${PENDING_TIMES[index]}
    console_log="${parquet%.parquet}.console.log"
    mkdir -p "$(dirname -- "${console_log}")"
    if [[ "${PENDING_KINDS[index]}" == arms ]]; then
        # measurement_arms.sh relies on its caller to own setup and teardown.
        SETUP_ACTIVE=1
        run_measurement_setup "${ARMS_SIZE_MIB}"
        bash "${COLLECTION_DIR}/measurement_arms.sh" --platform "${MEASUREMENT_PLATFORM}" \
            "${ARMS_SIZE_MIB}" "${ARMS_RUN_LABEL}" _train "${workload}" 2>&1 | tee "${console_log}"
        # The ARMS runner can return success after warning about a workload failure.
        # Its recorded process status, timeout flag, and trace are authoritative.
        if ! successful_trace "${parquet}" "${time_file}"; then
            echo "ERROR: ARMS collection failed or produced no trace: ${workload}; see ${console_log}" >&2
            exit 1
        fi
        validate_arms_timing "${parquet}"
        run_measurement_teardown
        SETUP_ACTIVE=0
    else
        # logging.sh owns setup/teardown and selects this workload's model.
        START_RUN="${COLLECTION_RUN}" LOGGING_SIZE_MIB="${LOGGING_SIZE_MIB}" LOGGING_MODEL_PCT=99 \
            PARQUET_PYTHON="${PARQUET_PYTHON}" \
            bash "${COLLECTION_DIR}/logging.sh" --platform "${MEASUREMENT_PLATFORM}" \
            "${COLLECTION_RUN}" "${workload}" 2>&1 | tee "${console_log}"
        if ! successful_trace "${parquet}" "${time_file}"; then
            echo "ERROR: MANTA collection failed or produced no trace: ${workload}; see ${console_log}" >&2
            exit 1
        fi
    fi
    echo "Collected: ${parquet}"
done

write_arms_summary
echo "Requested collections and ARMS timing summary are complete."
