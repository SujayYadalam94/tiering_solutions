#!/bin/bash
set -uo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

# Reproduce MEMTIS's original near/local-first allocation policy: do not apply
# a numactl memory policy before launch. The shared runner still applies the
# same HTMM, CXL-emulation, capacity, logging, and cleanup configuration.
export MEMTIS_INITIAL_PLACEMENT=near
export MEMTIS_OUTPUT_NAME=memtis_near

exec "${SCRIPT_DIR}/measurement_memtis.sh" "$@"
