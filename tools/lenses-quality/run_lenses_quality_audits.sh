#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output_dir="$1"

mkdir -p "${output_dir}"

python3 "${repo_root}/tools/lenses-quality/audit_file_sizes.py" \
  --plugin-root "${repo_root}/plugins/lenses" \
  --output-json "${output_dir}/file-sizes.json" \
  > /dev/null

python3 "${repo_root}/tools/lenses-quality/audit_function_sizes.py" \
  --plugin-root "${repo_root}/plugins/lenses" \
  --output-json "${output_dir}/function-sizes.json" \
  > /dev/null

python3 "${repo_root}/tools/lenses-quality/audit_include_deps.py" \
  --plugin-root "${repo_root}/plugins/lenses" \
  --output-json "${output_dir}/include-deps.json" \
  > /dev/null

python3 "${repo_root}/tools/lenses-quality/generate_summary.py" \
  --file-sizes "${output_dir}/file-sizes.json" \
  --function-sizes "${output_dir}/function-sizes.json" \
  --include-deps "${output_dir}/include-deps.json" \
  --output-md "${output_dir}/SUMMARY.md"

echo "wrote: ${output_dir}/file-sizes.json"
echo "wrote: ${output_dir}/function-sizes.json"
echo "wrote: ${output_dir}/include-deps.json"
echo "wrote: ${output_dir}/SUMMARY.md"
