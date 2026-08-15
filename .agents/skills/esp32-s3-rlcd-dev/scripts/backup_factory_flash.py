#!/usr/bin/env python3
"""Read and verify a complete ESP32-S3 flash image without writing the device."""

from __future__ import annotations

import argparse
import datetime as dt
import glob
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


DEFAULT_CHUNK_SIZE = 0x100000


def parse_size(value: str) -> int:
    value = value.strip().upper()
    match = re.fullmatch(r"(0X[0-9A-F]+|\d+)(KB|MB)?", value)
    if not match:
        raise argparse.ArgumentTypeError(f"invalid size: {value}")
    number = int(match.group(1), 0)
    multiplier = {None: 1, "KB": 1024, "MB": 1024 * 1024}[match.group(2)]
    return number * multiplier


def format_size(size: int) -> str:
    if size % (1024 * 1024) == 0:
        return f"{size // (1024 * 1024)}MB"
    if size % 1024 == 0:
        return f"{size // 1024}KB"
    raise ValueError("esptool flash size must be an integral KB or MB value")


def find_esptool() -> list[str]:
    for executable in ("esptool", "esptool.py"):
        path = shutil.which(executable)
        if path:
            return [path]
    uvx = shutil.which("uvx")
    if uvx:
        return [uvx, "--from", "esptool", "esptool"]
    raise RuntimeError("esptool is missing; install it or install uv/uvx")


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    patterns = (
        "/dev/cu.usbmodem*",
        "/dev/cu.usbserial*",
        "/dev/ttyACM*",
        "/dev/ttyUSB*",
    )
    candidates = sorted({path for pattern in patterns for path in glob.glob(pattern)})
    if len(candidates) != 1:
        rendered = ", ".join(candidates) if candidates else "none"
        raise RuntimeError(f"expected one ESP serial port, found: {rendered}; pass --port")
    return candidates[0]


def run_esptool(prefix: list[str], port: str, args: list[str]) -> str:
    command = [*prefix, "--port", port, "--no-stub", *args]
    completed = subprocess.run(command, text=True, capture_output=True)
    if completed.returncode:
        details = (completed.stdout + completed.stderr).strip()
        raise RuntimeError(f"esptool failed ({completed.returncode}):\n{details}")
    return completed.stdout + completed.stderr


def detect_flash(prefix: list[str], port: str) -> tuple[int, dict[str, str]]:
    output = run_esptool(prefix, port, ["flash-id"])
    size_match = re.search(r"Detected flash size:\s*(\d+)(KB|MB)", output)
    if not size_match:
        raise RuntimeError("could not parse detected flash size from esptool output")
    size = parse_size("".join(size_match.groups()))
    fields: dict[str, str] = {}
    for key, pattern in {
        "chip": r"Chip type:\s*([^\r\n]+)",
        "features": r"Features:\s*([^\r\n]+)",
        "usb_mode": r"USB mode:\s*([^\r\n]+)",
        "flash_manufacturer": r"Manufacturer:\s*([^\r\n]+)",
        "flash_device": r"Device:\s*([^\r\n]+)",
    }.items():
        match = re.search(pattern, output)
        if match:
            fields[key] = match.group(1).strip()
    return size, fields


def read_chunk(
    prefix: list[str],
    port: str,
    flash_size: int,
    offset: int,
    length: int,
    output: Path,
    attempts: int,
) -> None:
    error: Exception | None = None
    for attempt in range(1, attempts + 1):
        try:
            run_esptool(
                prefix,
                port,
                [
                    "read-flash",
                    "--flash-size",
                    format_size(flash_size),
                    "--no-progress",
                    hex(offset),
                    hex(length),
                    str(output),
                ],
            )
            if output.stat().st_size != length:
                raise RuntimeError(
                    f"short read at {offset:#x}: expected {length}, got {output.stat().st_size}"
                )
            return
        except Exception as exc:  # Preserve the final esptool diagnostic.
            error = exc
            if attempt < attempts:
                print(
                    f"retrying chunk at {offset:#x} ({attempt}/{attempts})",
                    file=sys.stderr,
                    flush=True,
                )
    assert error is not None
    raise error


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_partitions(image: Path) -> list[dict[str, int | str]]:
    partitions: list[dict[str, int | str]] = []
    with image.open("rb") as handle:
        handle.seek(0x8000)
        table = handle.read(0xC00)
    for position in range(0, len(table), 32):
        entry = table[position : position + 32]
        if len(entry) < 32:
            break
        magic = int.from_bytes(entry[0:2], "little")
        if magic in (0xFFFF, 0xEBEB):
            break
        if magic != 0x50AA:
            break
        partitions.append(
            {
                "label": entry[12:28].split(b"\0", 1)[0].decode("ascii", "replace"),
                "type": entry[2],
                "subtype": entry[3],
                "offset": int.from_bytes(entry[4:8], "little"),
                "size": int.from_bytes(entry[8:12], "little"),
                "flags": int.from_bytes(entry[28:32], "little"),
            }
        )
    return partitions


def backup(args: argparse.Namespace) -> None:
    prefix = find_esptool()
    port = find_port(args.port)
    detected_size, device = detect_flash(prefix, port)
    flash_size = args.size or detected_size
    if flash_size != detected_size:
        raise RuntimeError(
            f"requested size {format_size(flash_size)} differs from detected "
            f"{format_size(detected_size)}"
        )
    output = args.output.expanduser().resolve()
    if output.exists() and not args.force:
        raise RuntimeError(f"refusing to overwrite {output}; pass --force deliberately")
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="esp32-s3-rlcd-backup-") as temp_name:
        temp = Path(temp_name)
        chunks: list[Path] = []
        for index, offset in enumerate(range(0, flash_size, args.chunk_size)):
            length = min(args.chunk_size, flash_size - offset)
            chunk = temp / f"chunk-{index:02d}.bin"
            read_chunk(prefix, port, flash_size, offset, length, chunk, args.attempts)
            chunks.append(chunk)
            print(f"read {offset:#010x}..{offset + length - 1:#010x}", flush=True)

        staged = output.with_name(output.name + ".partial")
        with staged.open("wb") as destination:
            for chunk in chunks:
                with chunk.open("rb") as source:
                    shutil.copyfileobj(source, destination, 1024 * 1024)
        if staged.stat().st_size != flash_size:
            raise RuntimeError("assembled image size does not match detected flash size")

        verification: list[dict[str, int | str]] = []
        if not args.no_verify:
            indexes = sorted({0, len(chunks) // 2, len(chunks) - 1})
            for index in indexes:
                offset = index * args.chunk_size
                length = min(args.chunk_size, flash_size - offset)
                sample = temp / f"verify-{index:02d}.bin"
                read_chunk(prefix, port, flash_size, offset, length, sample, args.attempts)
                expected = hashlib.sha256(chunks[index].read_bytes()).hexdigest()
                actual = hashlib.sha256(sample.read_bytes()).hexdigest()
                if actual != expected:
                    raise RuntimeError(f"device verification mismatch at {offset:#x}")
                verification.append({"offset": offset, "size": length, "sha256": actual})
                print(f"verified sample at {offset:#010x}: {actual}", flush=True)

        os.replace(staged, output)

    digest = sha256_file(output)
    manifest = {
        "created_at": dt.datetime.now(dt.timezone.utc).astimezone().isoformat(),
        "source": "complete flash read from connected device",
        "read_only": True,
        "port": port,
        "esptool_command": " ".join(prefix),
        "flash_size": flash_size,
        "chunk_size": args.chunk_size,
        "sha256": digest,
        "device": device,
        "partitions": parse_partitions(output),
        "verification_samples": verification,
    }
    manifest_path = output.with_suffix(output.suffix + ".json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"backup:   {output}", flush=True)
    print(f"manifest: {manifest_path}", flush=True)
    print(f"sha256:   {digest}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Safely back up a complete ESP32-S3 flash using ROM-loader reads."
    )
    parser.add_argument("output", type=Path, help="destination .bin file")
    parser.add_argument("--port", help="serial port; auto-detect when omitted")
    parser.add_argument("--size", type=parse_size, help="must match detected flash size")
    parser.add_argument("--chunk-size", type=parse_size, default=DEFAULT_CHUNK_SIZE)
    parser.add_argument("--attempts", type=int, default=2)
    parser.add_argument("--no-verify", action="store_true", help="skip three-point device reread")
    parser.add_argument("--force", action="store_true", help="replace an existing destination")
    args = parser.parse_args()
    if args.chunk_size <= 0 or args.attempts <= 0:
        parser.error("--chunk-size and --attempts must be positive")
    try:
        backup(args)
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
