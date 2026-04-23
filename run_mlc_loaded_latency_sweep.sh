#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

MLC_BIN_DEFAULT="${HOME}/mlc/Linux/mlc"
NUMACTL_BIN_DEFAULT="numactl"
CPU_NODE_DEFAULT=0
MEM_NODE_DEFAULT=1

MLC_BIN=${MLC_BIN:-${MLC_BIN_DEFAULT}}
NUMACTL_BIN=${NUMACTL_BIN:-${NUMACTL_BIN_DEFAULT}}
CPU_NODE=${CPU_NODE:-${CPU_NODE_DEFAULT}}
MEM_NODE=${MEM_NODE:-${MEM_NODE_DEFAULT}}
TIMESTAMP=${TIMESTAMP:-$(date '+%Y%m%d_%H%M%S')}
OUT_DIR=${OUT_DIR:-"${SCRIPT_DIR}/mlc_loaded_latency_runs/${TIMESTAMP}"}

WORKLOAD_FLAGS=("-W6" "-R" "-W7" "-W8" "-W9")
DELAYS=(100 200 250 275 300 400 500 600 700 800 1000 1300 1700 2500 3500 5000 7000 9000 12000 16000 20000 30000 50000)

print_usage() {
    cat <<EOF
Usage: $0 [--output-dir DIR] [--mlc-bin PATH] [--numactl-bin PATH] [--cpu-node N] [--mem-node N]

Runs MLC loaded-latency sweeps for the fixed workload set:
  -R -W6 -W7 -W8 -W9

The delay list is written once to delays.txt and passed to MLC with -g<file>,
which makes MLC emit one latency/bandwidth row per delay in a single run.

Defaults:
  mlc bin:     ${MLC_BIN_DEFAULT}
  numactl bin: ${NUMACTL_BIN_DEFAULT}
  cpu node:    ${CPU_NODE_DEFAULT}
  mem node:    ${MEM_NODE_DEFAULT}
  output dir:  ${SCRIPT_DIR}/mlc_loaded_latency_runs/<timestamp>
EOF
}

workload_to_write_pct() {
    local workload_flag=$1

    case "${workload_flag}" in
        -R)
            echo "0"
            ;;
        -W6)
            echo "100"
            ;;
        -W7)
            echo "33.3333333333"
            ;;
        -W8)
            echo "50"
            ;;
        -W9)
            echo "25"
            ;;
        *)
            echo "ERROR: unsupported workload flag '${workload_flag}'" >&2
            return 1
            ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output-dir)
            OUT_DIR=$2
            shift 2
            ;;
        --mlc-bin)
            MLC_BIN=$2
            shift 2
            ;;
        --numactl-bin)
            NUMACTL_BIN=$2
            shift 2
            ;;
        --cpu-node)
            CPU_NODE=$2
            shift 2
            ;;
        --mem-node)
            MEM_NODE=$2
            shift 2
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "ERROR: unknown argument '$1'" >&2
            print_usage >&2
            exit 1
            ;;
    esac
done

if [[ ! -x "${MLC_BIN}" ]]; then
    echo "ERROR: MLC binary not found or not executable: ${MLC_BIN}" >&2
    exit 1
fi

if ! command -v "${NUMACTL_BIN}" >/dev/null 2>&1; then
    echo "ERROR: numactl binary not found: ${NUMACTL_BIN}" >&2
    exit 1
fi

mkdir -p "${OUT_DIR}"

DELAY_FILE="${OUT_DIR}/delays.txt"
MANIFEST_FILE="${OUT_DIR}/manifest.csv"

printf '%s\n' "${DELAYS[@]}" > "${DELAY_FILE}"
printf 'workload_flag,write_pct,output_file\n' > "${MANIFEST_FILE}"

echo "Using MLC binary: ${MLC_BIN}"
echo "Using numactl binary: ${NUMACTL_BIN}"
echo "Binding CPU node: ${CPU_NODE}"
echo "Binding memory node: ${MEM_NODE}"
echo "Writing outputs to: ${OUT_DIR}"
echo "Using delay file: ${DELAY_FILE}"

for workload_flag in "${WORKLOAD_FLAGS[@]}"; do
    workload_name=${workload_flag#-}
    write_pct=$(workload_to_write_pct "${workload_flag}")
    output_file="${OUT_DIR}/loaded_latency_${workload_name}.txt"

    echo "== Running ${workload_flag} (write_pct=${write_pct}) =="

    {
        echo "# workload_flag=${workload_flag}"
        echo "# requested_write_pct=${write_pct}"
        echo "# delay_file=${DELAY_FILE}"
        echo "# command=${NUMACTL_BIN} -N ${CPU_NODE} -m ${MEM_NODE} ${MLC_BIN} --loaded_latency ${workload_flag} -g${DELAY_FILE}"
        "${NUMACTL_BIN}" -N "${CPU_NODE}" -m "${MEM_NODE}" \
            "${MLC_BIN}" --loaded_latency "${workload_flag}" "-g${DELAY_FILE}"
    } | tee "${output_file}"

    printf '%s,%s,%s\n' "${workload_flag}" "${write_pct}" "$(basename "${output_file}")" >> "${MANIFEST_FILE}"
done

echo "Completed MLC loaded-latency sweep"
echo "Manifest: ${MANIFEST_FILE}"
