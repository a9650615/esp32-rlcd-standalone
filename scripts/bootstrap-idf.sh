#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
idf_dir="$project_dir/.tools/esp-idf"
if [[ ! -d "$idf_dir/.git" ]]; then
  git clone --depth 1 --branch v5.5.2 https://github.com/espressif/esp-idf.git "$idf_dir"
fi
git -C "$idf_dir" describe --tags --exact-match | grep -qx 'v5.5.2'

# Several ESP-IDF components are submodules, and a plain clone leaves their
# directories present but empty. The firmware build pulls what it needs through
# the component manager and so appears to work, which is what makes this worth
# doing here: the failure surfaces much later, in the host test build, as
# "Cannot find source file .../cJSON/cJSON.c" with nothing pointing back at a
# half-initialised toolchain.
#
# Not --depth 1: a shallow submodule fetch only retrieves the tip of the
# default branch, and ESP-IDF pins commits that are frequently not the tip.
# The checkout then leaves the directory present but empty, which surfaces at
# link time as "No rule to make target .../esp_wifi/lib/esp32s3/libcore.a"
# rather than as anything mentioning submodules.
git -C "$idf_dir" submodule update --init --recursive

"$idf_dir/install.sh" esp32s3
