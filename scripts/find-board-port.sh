#!/usr/bin/env bash
# Prints the serial port of THIS board, identified by MAC rather than by port
# number. /dev/cu.usbmodem1101 is not a stable name: it is assigned in
# enumeration order, so plugging in any other USB serial device can hand that
# name to something else. Flashing is destructive and irreversible on the wrong
# target, so the port is resolved from the one identifier the board carries
# itself.
#
# Usage:
#   PORT="$(./scripts/find-board-port.sh)" && ./scripts/idf.sh -p "$PORT" app-flash
set -euo pipefail

expected_mac="a4:cb:8f:df:88:d0"  # ESP32-S3-WROOM-1-N16R8, also the USB serial number
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
python_env="$HOME/.espressif/python_env/idf5.5_py3.9_env/bin/python"

if [[ ! -x "$python_env" ]]; then
  printf 'IDF python env not found at %s\n' "$python_env" >&2
  exit 1
fi

shopt -s nullglob
candidates=(/dev/cu.usbmodem*)
shopt -u nullglob

if [[ ${#candidates[@]} -eq 0 ]]; then
  printf 'no /dev/cu.usbmodem* ports present; is the board plugged in and powered?\n' >&2
  exit 1
fi

for port in "${candidates[@]}"; do
  # --after no_reset so probing a port never disturbs whatever is running on
  # it - including a board mid-OTA that happens to be the other device.
  mac="$("$python_env" -m esptool --port "$port" --after no_reset read_mac 2>/dev/null \
        | awk '/^MAC:/ {print $2; exit}')" || true
  if [[ "$mac" == "$expected_mac" ]]; then
    printf '%s\n' "$port"
    exit 0
  fi
  printf 'skipping %s (mac %s)\n' "$port" "${mac:-unreadable}" >&2
done

printf 'board %s not found on any of: %s\n' "$expected_mac" "${candidates[*]}" >&2
exit 1
