#!/usr/bin/env bash
# Everything this board needs from a developer, without a USB cable.
#
#   ./scripts/remote.sh ip                 # resolve the board's address
#   ./scripts/remote.sh logs [seconds]     # stream the log port
#   ./scripts/remote.sh shot [dir]         # PNG of what is on the panel now
#   ./scripts/remote.sh push               # build and offer firmware
#   ./scripts/remote.sh restart            # reboot and wait for it to answer
#
# Why not find-board-port.sh: that reads the MAC over USB, which means entering
# the ROM downloader and stopping the application. Nothing here touches serial.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The board is identified by MAC, never by address - the address is a DHCP
# lease and the MAC is the board. Same rule find-board-port.sh follows for
# ports, for the same reason: other devices come and go on this network.
board_mac="a4:cb:8f:df:88:d0"
log_port="${RLCD_LOG_PORT:-3334}"
# Upload shares the main port again - the separate listener was rolled back
# twice on the board and is out until the cause is known. See portal.cpp.
upload_port="${RLCD_UPLOAD_PORT:-80}"

board_ip() {
  if [[ -n "${RLCD_IP:-}" ]]; then
    printf '%s\n' "$RLCD_IP"
    return 0
  fi
  local ip
  # The ARP cache only knows hosts this machine has spoken to recently, so
  # prod the subnet first when the entry has aged out.
  ip="$(arp -an | awk -v mac="$board_mac" 'tolower($4) == mac {gsub(/[()]/, "", $2); print $2; exit}')"
  if [[ -z "$ip" ]]; then
    local subnet
    subnet="$(ipconfig getifaddr en0 2>/dev/null | cut -d. -f1-3)"
    [[ -z "$subnet" ]] && { echo "no en0 address; set RLCD_IP" >&2; return 1; }
    echo "board not in the ARP cache; sweeping ${subnet}.0/24" >&2
    for host in $(seq 1 254); do
      ping -c1 -W120 "${subnet}.${host}" >/dev/null 2>&1 &
    done
    wait
    ip="$(arp -an | awk -v mac="$board_mac" 'tolower($4) == mac {gsub(/[()]/, "", $2); print $2; exit}')"
  fi
  [[ -z "$ip" ]] && { echo "board $board_mac not found on this network" >&2; return 1; }
  printf '%s\n' "$ip"
}

# Reads the log stream for a bounded time.
#
# Not `timeout N nc`: timeout is GNU coreutils and macOS does not ship it, so
# that spelling fails on the machine this is written for. Not `nc -w` either -
# that measures idle time, which never elapses against a board logging every
# few seconds. Backgrounding and killing needs nothing that is not already in
# a POSIX shell.
capture_log() {
  local ip="$1" seconds="$2" pid
  nc "$ip" "$log_port" &
  pid=$!
  sleep "$seconds"
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

case "${1:-}" in
  ip)
    board_ip
    ;;

  logs)
    capture_log "$(board_ip)" "${2:-60}"
    ;;

  shot)
    out="${2:-$project_dir/screenshots}"
    capture="$(mktemp -t rlcd-shot)"
    # GET /shot, not a log capture: frames in the log are emitted once per page
    # per boot, so reading the panel that way means rebooting the board - and
    # with no cable attached, rebooting means pushing firmware just to see a
    # layout. This returns whatever is on screen right now.
    #
    # 403 is the setup page refusing to be photographed; it prints the portal
    # password, so that refusal is deliberate.
    if ! curl --fail-with-body -sS -m 30 "http://$(board_ip)/shot" > "$capture"; then
      cat "$capture" >&2
      exit 1
    fi
    python3 "$project_dir/scripts/decode-screenshots.py" "$capture" "$out"
    ;;

  restart)
    ip="$(board_ip)"
    curl --fail-with-body -sS -m 10 -X POST "http://$ip/restart" >&2
    # Waiting is the whole point: every caller reboots in order to look at what
    # happens next, and the address answers again before the application does.
    echo "waiting for the board to answer again..." >&2
    for _ in $(seq 1 40); do
      sleep 2
      if curl -s -m 3 -o /dev/null "http://$ip/shot"; then
        echo "board is back" >&2
        exit 0
      fi
    done
    echo "board did not answer within 80 s" >&2
    exit 1
    ;;

  push)
    bin="$project_dir/build/layout_carousel.bin"
    [[ -f "$bin" ]] || { echo "no build at $bin - run idf.py build" >&2; exit 1; }
    ip="$(board_ip)"
    echo "offering $(wc -c < "$bin" | tr -d ' ') bytes to $ip" >&2
    echo "the board is showing the offer now: BOOT accepts, KEY cancels" >&2
    # --max-time covers the confirmation window plus the transfer. The board
    # holds the request open until someone answers.
    #
    # The exit status is the point. A refused or unanswered push returns 403
    # with a body, and the echo that used to follow it made the script exit 0 -
    # so a push that installed nothing reported success. With no cable and no
    # eyes on the panel, that is indistinguishable from a deployment, and it is
    # how a caller ends up believing firmware is running that never left the
    # machine.
    if ! curl --fail-with-body -sS --max-time 420 \
         -X POST --data-binary "@$bin" "http://$ip:$upload_port/ota"; then
      echo >&2
      echo "push did NOT install - the board is still on its previous firmware" >&2
      exit 1
    fi
    echo
    ;;

  *)
    sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
