#!/bin/bash
set -uo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

# Compatibility name; there is only one MEMTIS placement/run configuration.
exec "${SCRIPT_DIR}/measurement_memtis.sh" "$@"
