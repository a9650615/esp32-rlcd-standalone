#!/usr/bin/env bash
# Installs the AirPlay 1 RSA private key and puts the result on the board.
#
#   ./scripts/install-raop-key.sh /path/to/key.pem
#
# Everything about swapping this key except supplying the file itself: the
# format check, the permissions, the "did it just get staged for commit"
# check, the clean rebuild CMake needs, and the deploy.
#
# Why a clean rebuild: CMakeLists.txt reads the key at *configure* time and
# embeds it via EMBED_TXTFILES. Editing the file and running an incremental
# build leaves the old key in the image - the same trap version.txt has.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="$project_dir/modules/airplay/secrets/raop_private_key.pem"
src="${1:-}"

[[ -n "$src" ]] || { echo "usage: $0 /path/to/key.pem" >&2; exit 1; }
[[ -f "$src" ]] || { echo "no such file: $src" >&2; exit 1; }

# Checked before overwriting anything. A key that mbedtls cannot parse fails
# at CMake configure time with a link error rather than a clear message, and
# a key that parses but is not RSA fails later still, on the board, as
# "Error -N parsing private key" during the first Apple-Challenge.
if ! openssl rsa -in "$src" -noout -check >/dev/null 2>&1; then
  echo "not a usable RSA private key: $src" >&2
  echo "(some projects ship it as a C string literal or a bare base64 blob;" >&2
  echo " it has to be reassembled into PEM, -----BEGIN RSA PRIVATE KEY-----)" >&2
  exit 1
fi
echo "key parses as a valid RSA private key" >&2

mkdir -p "$(dirname "$dest")"
cp "$src" "$dest"
chmod 600 "$dest"

# The path is gitignored (modules/airplay/.gitignore), but a gitignore does
# nothing for a file that is already tracked, and this one must never be
# committed - the repo is public and under the author's real name.
if git -C "$project_dir" ls-files --error-unmatch "$dest" >/dev/null 2>&1; then
  echo "REFUSING TO CONTINUE: $dest is tracked by git." >&2
  echo "Run: git rm --cached '$dest'" >&2
  exit 1
fi
echo "key installed, 600, and not tracked by git" >&2

rm -rf "$project_dir/build"
"$project_dir/scripts/idf.sh" build
"$project_dir/scripts/remote.sh" push

cat >&2 <<'NEXT'

Now play to the receiver from the phone, then read the log:

    ./scripts/remote.sh logs 30

The test is whether ANNOUNCE arrives. With a key the sender rejects you get
"answered Apple-Challenge with a 256-byte signature", several more
"received OPTIONS", and a disconnect. With one it accepts, ANNOUNCE follows
and the session proceeds to SETUP/RECORD.
NEXT
