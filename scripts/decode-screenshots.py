#!/usr/bin/env python3
"""Turns SHOT lines from a serial capture into PNGs of what the panel drew.

    ./scripts/idf.sh -p "$PORT" monitor | tee capture.log     # or any capture
    python3 scripts/decode-screenshots.py capture.log out/

Debug builds emit one frame per page per boot (see components/ui/
ui_screenshot.cpp). Geometry logs can say a label is out of bounds; they
cannot say a layout looks wrong, and this is how that gets looked at without
a camera pointed at the desk.

Writes a plain 1-bit PNG per page with no third-party dependencies - zlib and
struct are enough, and adding Pillow to view a monochrome bitmap would be a
dependency for nothing.
"""
import base64
import re
import struct
import sys
import zlib
from pathlib import Path

WIDTH, HEIGHT = 400, 300
ROW_BYTES = WIDTH // 8


def write_png(path: Path, bits: bytes) -> None:
    """1-bit greyscale PNG. The device sets a bit for black, PNG greyscale
    reads 1 as white, so the rows are inverted on the way out."""
    raw = bytearray()
    for y in range(HEIGHT):
        row = bits[y * ROW_BYTES:(y + 1) * ROW_BYTES]
        raw.append(0)  # filter type: none
        raw.extend(bytes(b ^ 0xFF for b in row))

    def chunk(kind: bytes, payload: bytes) -> bytes:
        body = kind + payload
        return (struct.pack(">I", len(payload)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 1, 0, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    path.write_bytes(png)


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    capture, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    name = None
    payload = []
    written = 0
    for line in capture.read_text(errors="replace").splitlines():
        begin = re.search(r"SHOT BEGIN (\S+)", line)
        if begin:
            name, payload = begin.group(1), []
            continue
        if "SHOT END" in line and name:
            bits = base64.b64decode("".join(payload))
            if len(bits) != ROW_BYTES * HEIGHT:
                # A short frame means dropped serial, not a bad decoder; say so
                # rather than writing a half-torn image that looks like a bug.
                print(f"{name}: {len(bits)} of {ROW_BYTES * HEIGHT} bytes, skipped",
                      file=sys.stderr)
            else:
                target = out_dir / f"{name}.png"
                write_png(target, bits)
                print(f"wrote {target}")
                written += 1
            name = None
            continue
        if name is not None:
            body = re.search(r"SHOT ([A-Za-z0-9+/=]+)\s*$", line)
            if body:
                payload.append(body.group(1))

    if written == 0:
        print("no complete frames found - was this a debug build?", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
