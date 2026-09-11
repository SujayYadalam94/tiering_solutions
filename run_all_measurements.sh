#!/bin/bash

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=measurement_common.sh
source "${SCRIPT_DIR}/measurement_common.sh"
# shellcheck source=measurement_workloads.sh
source "${SCRIPT_DIR}/measurement_workloads.sh"

measurement_init_platform_from_args "$@" || exit 1
PLATFORM_ARGS=(--platform "${MEASUREMENT_PLATFORM}")

START_EPOCH=$(date +%s)
echo "Using measurement platform: ${MEASUREMENT_PLATFORM}"

format_duration_hms() {
    local total_seconds=$1
    local hours=$((total_seconds / 3600))
    local minutes=$(((total_seconds % 3600) / 60))
    local seconds=$((total_seconds % 60))
    printf '%02d:%02d:%02d' "${hours}" "${minutes}" "${seconds}"
}

send_completion_email() {
    local elapsed_seconds=$1
    local elapsed_hms
    elapsed_hms=$(format_duration_hms "${elapsed_seconds}")

    local recipient=${MEASUREMENT_NOTIFY_EMAIL:-${EMAIL:-freischuetz@wisc.edu}}
    if [[ -z "${recipient}" ]]; then
        echo "No email recipient configured. Set MEASUREMENT_NOTIFY_EMAIL (or EMAIL) to enable notifications."
        return
    fi

    local subject="run_all_measurements completed in ${elapsed_hms}"
    local body
    body=$(cat <<EOF
run_all_measurements.sh has finished.

Host: $(hostname)
Start: $(date -d "@${START_EPOCH}" '+%Y-%m-%d %H:%M:%S %Z')
End:   $(date '+%Y-%m-%d %H:%M:%S %Z')
Elapsed: ${elapsed_hms} (${elapsed_seconds}s)

Timeout per workload command: ${MEASUREMENT_TIMEOUT_SECONDS:-2700}s
EOF
)

    if command -v mail >/dev/null 2>&1; then
        printf '%s\n' "${body}" | mail -s "${subject}" "${recipient}"
        echo "Completion email sent to ${recipient}"
    elif command -v mailx >/dev/null 2>&1; then
        printf '%s\n' "${body}" | mailx -s "${subject}" "${recipient}"
        echo "Completion email sent to ${recipient}"
    else
        echo "WARNING: neithers'mail' nor 'mailx' is available; could not send completion email to ${recipient}."
    fi
}

# Own setup/cache policy here; standalone MEMTIS launches do not flush caches.
# Hold the same lock used by the standalone runner across setup and teardown.
run_memtis_measurement() {
    sudo -E flock --exclusive --nonblock "${MEMTIS_LOCK_FILE:-/run/lock/measurement_memtis.lock}" \
        bash -c '
            set -o pipefail
            source "$1/measurement_common.sh" || exit 1
            MEASUREMENT_PLATFORM=$2
            trap run_measurement_teardown EXIT
            run_measurement_setup "$3" memtis || exit $?
            MEMTIS_LOCK_HELD=1 MEMTIS_RUN_SETUP=0 MEMTIS_CACHE_FLUSH=batch-setup \
                "$1/measurement_memtis.sh" --platform "$2" "$3" "$4" "$5"
        ' memtis-batch "${SCRIPT_DIR}" "${MEASUREMENT_PLATFORM}" "$1" "$2" "$3"
}

SIZES=(4080)
RUNS=3

ARMS_LIB_SUFFIX=${ARMS_LIB_SUFFIX:-_plain}

mapfile -t WORKLOAD_IDS < <(measurement_list_default_workloads)

for size in "${SIZES[@]}"; do
    echo "== Running measurements with size ${size}MiB =="

    for run in $(seq 1 ${RUNS}); do
        echo "-- Run ${run}/${RUNS} for size ${size}MiB --"

        for workload_id in "${WORKLOAD_IDS[@]}"; do
            echo "---- Workload ${workload_id} ----"

            # Model
            #if [[ -x "${SCRIPT_DIR}/measurement_model.sh" ]]; then
            #    "${SCRIPT_DIR}/measurement_model.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}" "" "${workload_id}"
            #else
            #    echo "WARNING: ./measurement_model.sh not found or not executable; skipping model"
            #fi

            # ARMS
            measurement_load_workload "${workload_id}" || exit 1
            if measurement_workload_supports_system arms; then
                run_measurement_setup "${size}"
                "${SCRIPT_DIR}/measurement_arms.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}" "" "${workload_id}"
            else
                echo "Skipping ${workload_id}: not supported by ARMS"
            fi


            # NOMAD
            #if [[ -x "${SCRIPT_DIR}/measurement_nomad.sh" ]]; then
            #    measurement_ensure_nomad_ready || exit 1
            #    run_measurement_setup "${size}" nomad
            #    "${SCRIPT_DIR}/measurement_nomad.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}" "${workload_id}"
            #else
            #    echo "WARNING: ./measurement_nomad.sh not found or not executable; skipping NOMAD"
            #fi

            # MEMTIS
            #if [[ -x "${SCRIPT_DIR}/measurement_memtis.sh" ]]; then
            #    # Batch setup, cache flushing, and measurement share one lock.
            #    if run_memtis_measurement "${size}" "${run}" "${workload_id}"; then
            #        :
            #    else
            #        status=$?
            #        echo "ERROR: MEMTIS ${workload_id} failed (status ${status}); stopping measurements" >&2
            #        exit "${status}"
            #    fi
            #else
            #    echo "ERROR: ./measurement_memtis.sh not found or not executable" >&2
            #    exit 1
            #fi

            # Model
            if [[ -x "${SCRIPT_DIR}/measurement_model.sh" ]]; then
                "${SCRIPT_DIR}/measurement_model.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}" "" "${workload_id}"
            else
                echo "WARNING: ./measurement_model.sh not found or not executable; skipping model"
            fi

            # HybridTier
            #run_measurement_setup "${size}"
            #if [[ -x "${SCRIPT_DIR}/measurement_hybridtier.sh" ]]; then
            #    sudo -E "${SCRIPT_DIR}/measurement_hybridtier.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}" huge "${workload_id}"
            #else
            #    echo "WARNING: ./measurement_hybridtier.sh not found or not executable; skipping HybridTier"
            #fi

            #run_measurement_setup "${size}"
            #"${SCRIPT_DIR}/measurement_arms.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}" "${ARMS_LIB_SUFFIX}" "${workload_id}"

            # DRAM-only baseline
            #if [[ -x "${SCRIPT_DIR}/measurement_dram_only.sh" ]]; then
            #    run_measurement_setup_baseline_default "${size}"
            #    "${SCRIPT_DIR}/measurement_dram_only.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}" "${workload_id}"
            #else
            #    echo "WARNING: ./measurement_dram_only.sh not found or not executable; skipping DRAM-only"
            #fi

            # CXL-only baseline
            #if [[ -x "${SCRIPT_DIR}/measurement_cxl_only.sh" ]]; then
            #    run_measurement_setup_baseline_default "${size}"
            #    "${SCRIPT_DIR}/measurement_cxl_only.sh" "${PLATFORM_ARGS[@]}" "${size}" "${run}" "${workload_id}"
            #else
            #    echo "WARNING: ./measurement_cxl_only.sh not found or not executable; skipping CXL-only"
            #fi

            # MEMTIS teardown is performed by the batch helper while its lock is held.
        done
    done
done

END_EPOCH=$(date +%s)
ELAPSED_SECONDS=$((END_EPOCH - START_EPOCH))
ELAPSED_HMS=$(format_duration_hms "${ELAPSED_SECONDS}")

echo "All measurements finished in ${ELAPSED_HMS} (${ELAPSED_SECONDS}s)"
send_completion_email "${ELAPSED_SECONDS}"
