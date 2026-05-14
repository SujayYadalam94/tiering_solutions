#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
target_dir="${1:-$script_dir}"
archive_name="${2:-$(basename "$target_dir")_pdfs.zip}"

if ! command -v pdfcrop >/dev/null 2>&1; then
	echo "pdfcrop is required but was not found in PATH." >&2
	exit 1
fi

if ! command -v zip >/dev/null 2>&1; then
	echo "zip is required but was not found in PATH." >&2
	exit 1
fi

if [[ ! -d "$target_dir" ]]; then
	echo "Target directory does not exist: $target_dir" >&2
	exit 1
fi

shopt -s nullglob
pdfs=("$target_dir"/*.pdf)

if (( ${#pdfs[@]} == 0 )); then
	echo "No PDFs found in $target_dir"
	exit 0
fi

for pdf in "${pdfs[@]}"; do
	tmp_pdf="${pdf%.pdf}.cropped.tmp.pdf"
	pdfcrop "$pdf" "$tmp_pdf" >/dev/null
	mv "$tmp_pdf" "$pdf"
done

archive_path="$target_dir/$archive_name"
rm -f "$archive_path"

(
	cd "$target_dir"
	zip -q "$archive_name" -- ./*.pdf
)

echo "Cropped ${#pdfs[@]} PDF(s) and created $archive_path"
