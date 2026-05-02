#!/usr/bin/env bash

set -euo pipefail
shopt -s nullglob

if ! command -v pdfcrop >/dev/null 2>&1; then
	echo "pdfcrop is not available in PATH" >&2
	exit 1
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
pdfs=("$script_dir"/*.pdf)

if [[ ${#pdfs[@]} -eq 0 ]]; then
	echo "No PDF files found in $script_dir"
	exit 0
fi

for pdf in "${pdfs[@]}"; do
	tmp_pdf="$(mktemp "$script_dir/.pdfcrop.XXXXXX.pdf")"
	echo "Cropping $(basename "$pdf")"

	if pdfcrop "$pdf" "$tmp_pdf" >/dev/null; then
		mv -f "$tmp_pdf" "$pdf"
	else
		rm -f "$tmp_pdf"
		exit 1
	fi
done

echo "Cropped ${#pdfs[@]} PDF files in $script_dir"
