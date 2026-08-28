#!/usr/bin/env bash
set -euo pipefail

# The toolchain lives once per workspace, not once per board: several boards
# pin the same ESP-IDF and a private copy each costs ~900 MB to say the same
# thing. The path is versioned (esp-idf-v5.5.2, never a bare esp-idf), so two
# boards on different pins coexist instead of overwriting each other.
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

# The workspace is the parent of the main checkout, found through the git
# common dir so a linked worktree resolves to the same place its parent does.
common_dir="$(git -C "$project_dir" rev-parse --git-common-dir)"
if [[ "$common_dir" != /* ]]; then
  common_dir="$project_dir/$common_dir"
fi
main_checkout_dir="$(cd "$common_dir/.." && pwd)"
workspace_dir="$(cd "$main_checkout_dir/.." && pwd)"
idf_dir="$workspace_dir/.tools/esp-idf-$idf_version"

# A leftover per-project checkout is the half-migrated state: builds keep
# working against it, nothing says the shared one is being ignored, and the two
# drift apart silently. Refuse rather than pick one.
legacy_dir="$project_dir/.tools/esp-idf"
if [[ -e "$legacy_dir" || -L "$legacy_dir" ]]; then
  printf 'error: remove the pre-workspace toolchain at %s\n' "$legacy_dir" >&2
  exit 1
fi

if [[ ! -f "$idf_dir/export.sh" ]]; then
  printf 'error: ESP-IDF is not bootstrapped; run ./scripts/bootstrap-idf.sh first\n' >&2
  exit 1
fi

# Source in this child process only; the caller's environment is unchanged.
# shellcheck source=/dev/null
source "$idf_dir/export.sh"
exec idf.py "$@"
