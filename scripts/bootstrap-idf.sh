#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
idf_dir="$project_dir/.tools/esp-idf"
if [[ ! -d "$idf_dir/.git" ]]; then
  git clone --depth 1 --branch v5.5.2 https://github.com/espressif/esp-idf.git "$idf_dir"
fi
git -C "$idf_dir" describe --tags --exact-match | grep -qx 'v5.5.2'
"$idf_dir/install.sh" esp32s3
