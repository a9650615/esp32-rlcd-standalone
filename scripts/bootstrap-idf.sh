#!/usr/bin/env bash
set -euo pipefail

# Installs the pinned ESP-IDF into the workspace's shared, versioned toolchain
# directory. See scripts/idf.sh for why it lives there rather than in this
# repository.
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
version_file="$project_dir/.idf-version"

if [[ ! -f "$version_file" ]]; then
  printf 'error: missing %s\n' "$version_file" >&2
  exit 1
fi
idf_version="$(cat "$version_file")"
if [[ ! "$idf_version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
   [[ "$(wc -l < "$version_file" | tr -d ' ')" != 1 ]]; then
  printf 'error: %s must contain one vMAJOR.MINOR.PATCH line\n' "$version_file" >&2
  exit 1
fi

common_dir="$(git -C "$project_dir" rev-parse --git-common-dir)"
if [[ "$common_dir" != /* ]]; then
  common_dir="$project_dir/$common_dir"
fi
main_checkout_dir="$(cd "$common_dir/.." && pwd)"
workspace_dir="$(cd "$main_checkout_dir/.." && pwd)"
idf_dir="$workspace_dir/.tools/esp-idf-$idf_version"

legacy_dir="$project_dir/.tools/esp-idf"
if [[ -e "$legacy_dir" || -L "$legacy_dir" ]]; then
  printf 'error: remove the pre-workspace toolchain at %s\n' "$legacy_dir" >&2
  exit 1
fi

mkdir -p "$workspace_dir/.tools"
if [[ ! -d "$idf_dir/.git" ]]; then
  git clone --depth 1 --branch "$idf_version" \
    https://github.com/espressif/esp-idf.git "$idf_dir"
fi

actual_version="$(git -C "$idf_dir" describe --tags --exact-match 2>/dev/null || true)"
if [[ "$actual_version" != "$idf_version" ]]; then
  printf 'error: %s is %s, expected %s\n' \
    "$idf_dir" "${actual_version:-not an exact tag}" "$idf_version" >&2
  exit 1
fi

# Several ESP-IDF components are submodules, and a plain clone leaves their
# directories present but empty. The firmware build pulls what it needs through
# the component manager and so appears to work, which is what makes this worth
# doing here: the failure surfaces much later, in the host test build, as
# "Cannot find source file .../cJSON/cJSON.c" with nothing pointing back at a
# half-initialised toolchain.
#
# Not --depth 1, and not the clone's own --shallow-submodules: a shallow
# submodule fetch only retrieves the tip of the default branch, and ESP-IDF
# pins commits that are frequently not the tip. The checkout then leaves the
# directory present but empty, which surfaces at link time as "No rule to make
# target .../esp_wifi/lib/esp32s3/libcore.a" rather than as anything mentioning
# submodules.
git -C "$idf_dir" submodule update --init --recursive

"$idf_dir/install.sh" esp32s3
