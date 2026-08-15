#!/usr/bin/env bash
# Prints the serial port of the board, identified by MAC rather than by port
# number. Port names like /dev/cu.usbmodem1101 are assigned in enumeration
# order, so plugging in any other USB serial device can hand that name to
# something else. Flashing is destructive and irreversible on the wrong target.
#
# Usage:
#   PORT="$(./scripts/find-board-port.sh)" && ./scripts/idf.sh -p "$PORT" app-flash
#
# The MAC defaults to the board this checkout was developed against. Override
# it for your own unit rather than editing this file:
#   RLCD_BOARD_MAC=aa:bb:cc:dd:ee:ff ./scripts/find-board-port.sh
# Read yours once with: ./scripts/idf.sh -p <port> --version >/dev/null
#                       esptool --port <port> read_mac
set -euo pipefail

expected_mac="${RLCD_BOARD_MAC:-a4:cb:8f:df:88:d0}"
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
idf_dir="$project_dir/.tools/esp-idf"

if [[ ! -f "$idf_dir/export.sh" ]]; then
  printf 'error: ESP-IDF is not bootstrapped; run ./scripts/bootstrap-idf.sh first\n' >&2
  exit 1
fi

# export.sh is chatty and this script's stdout is the port itself, consumed by
# command substitution. Send the setup noise to stderr so it stays visible
# without contaminating the result.
# shellcheck source=/dev/null
source "$idf_dir/export.sh" >&2

shopt -s nullglob
candidates=(/dev/cu.usbmodem* /dev/ttyUSB* /dev/ttyACM*)
shopt -u nullglob

if [[ ${#candidates[@]} -eq 0 ]]; then
  printf 'no candidate serial ports present; is the board plugged in and powered?\n' >&2
  exit 1
fi

for port in "${candidates[@]}"; do
  # --after no_reset so probing a port never disturbs whatever is running on
  # it, including another board mid-update.
  # `python -m esptool` rather than the console script: that has been named
  # both esptool.py and esptool across ESP-IDF versions, while the module name
  # has not changed. export.sh has already put the right interpreter on PATH.
  mac="$(python -m esptool --port "$port" --after no_reset read_mac 2>/dev/null \
        | awk '/^MAC:/ {print $2; exit}')" || true
  if [[ "$mac" == "$expected_mac" ]]; then
    printf '%s\n' "$port"
    exit 0
  fi
  printf 'skipping %s (mac %s)\n' "$port" "${mac:-unreadable}" >&2
done

printf 'board %s not found on any of: %s\n' "$expected_mac" "${candidates[*]}" >&2
exit 1
