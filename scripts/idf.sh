#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
idf_dir="$project_dir/.tools/esp-idf"
if [[ ! -f "$idf_dir/export.sh" ]]; then
  printf 'error: ESP-IDF is not bootstrapped; run ./scripts/bootstrap-idf.sh first\n' >&2
  exit 1
fi

# Source in this child process only; the caller's environment is unchanged.
# shellcheck source=/dev/null
source "$idf_dir/export.sh"
exec idf.py "$@"
