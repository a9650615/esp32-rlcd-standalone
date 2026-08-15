#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
backup="$project_dir/firmware/backups/waveshare-factory-full-flash-2026-08-15.bin"
expected_bytes=16777216
expected_sha256=68db31b92d8a37bd321101d9ffb093bf2f3213d3e0bf111368e9a8f59919650f

if [[ ! -f "$backup" ]]; then
  printf 'factory backup missing: %s\n' "$backup" >&2
  exit 1
fi

actual_bytes="$(wc -c < "$backup" | tr -d '[:space:]')"
if [[ "$actual_bytes" != "$expected_bytes" ]]; then
  printf 'factory backup size mismatch: expected %s bytes, got %s\n' "$expected_bytes" "$actual_bytes" >&2
  exit 1
fi

if command -v shasum >/dev/null 2>&1; then
  actual_sha256="$(shasum -a 256 "$backup" | awk '{print $1}')"
else
  actual_sha256="$(sha256sum "$backup" | awk '{print $1}')"
fi
if [[ "$actual_sha256" != "$expected_sha256" ]]; then
  printf 'factory backup sha256 mismatch: expected %s, got %s\n' "$expected_sha256" "$actual_sha256" >&2
  exit 1
fi

printf 'factory backup verified: %s bytes, sha256 %s\n' "$actual_bytes" "$actual_sha256"
