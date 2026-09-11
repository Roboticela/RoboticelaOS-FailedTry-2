#!/usr/bin/env python3
"""Pack named files into a RoboFS image (RFS1)."""
import argparse
import struct
import sys
from pathlib import Path

MAGIC = 0x31534652  # 'RFS1'
VERSION = 1
NAME = 112
HDR = 16
ENT = 120


def pack(out: Path, files: list[tuple[str, bytes]]) -> None:
    n = len(files)
    off = HDR + n * ENT
    entries = []
    payloads = []
    for name, data in files:
        raw = name.encode("ascii")
        if len(raw) >= NAME:
            raise SystemExit(f"name too long: {name}")
        padded = raw + b"\x00" * (NAME - len(raw))
        entries.append((padded, off, len(data)))
        payloads.append(data)
        off += len(data)
    size = off
    buf = bytearray()
    buf += struct.pack("<IIII", MAGIC, VERSION, n, size)
    for padded, o, ln in entries:
        buf += padded + struct.pack("<II", o, ln)
    for payload in payloads:
        buf += payload
    if len(buf) != size:
        raise SystemExit("RoboFS size mismatch")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(buf))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("items", nargs="+", help="path_in_image=host_file")
    args = ap.parse_args()
    files = []
    for item in args.items:
        if "=" not in item:
            raise SystemExit(f"expected name=file, got {item}")
        name, host = item.split("=", 1)
        data = Path(host).read_bytes()
        files.append((name, data))
    pack(Path(args.output), files)
    return 0


if __name__ == "__main__":
    sys.exit(main())
