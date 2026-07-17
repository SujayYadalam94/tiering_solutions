#!/usr/bin/env bash
set -euo pipefail

SRC_ROOT="${1:-c220g5}"
DEST_ROOT="${2:-c220g5_final}"
VERSION_TAG="${3:-6.18}"
RUN_GLOB="${4:-4041MiB_run*}"
DATASET_SUFFIX="${5:-long}"

TIERS=(arms model)
WORKLOADS=(
	bc-kron.sg
	bc-twitter.sg
	DuckDB-TPCH-sf100
	faiss_10M
	mg.D.x
	pr-kron.sg
	pr-twitter.sg
	XSBench
)

shopt -s nullglob

for tier in "${TIERS[@]}"; do
	dest_tier="${tier}_${VERSION_TAG}_${DATASET_SUFFIX}"

	for workload in "${WORKLOADS[@]}"; do
		src_dir="${SRC_ROOT}/${tier}/${workload}-${DATASET_SUFFIX}"
		dst_dir="${DEST_ROOT}/${dest_tier}/${workload}"

		mkdir -p "${dst_dir}"

		matches=("${src_dir}"/${RUN_GLOB})
		if (( ${#matches[@]} == 0 )); then
			echo "Skipping (no match): ${src_dir}/${RUN_GLOB}"
			continue
		fi

		cp "${matches[@]}" "${dst_dir}/"
		echo "Copied ${#matches[@]} file(s): ${src_dir}/${RUN_GLOB} -> ${dst_dir}/"
	done
done

echo "Copy complete."
