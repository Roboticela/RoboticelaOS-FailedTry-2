#!/usr/bin/env python3
"""Format, inspect, and check RoboFS v1 images (little-endian RBFS)."""

from __future__ import annotations

import argparse
import struct
import sys

MAGIC = 0x53464252
MAJOR = 1
MINOR = 0
BLOCK = 4096
OBJ_SIZE = 128
DIRENT_SIZE = 64
NAME_MAX = 55
SB_CSUM_OFF = 68

OBJ_UNUSED = 0
OBJ_FILE = 1
OBJ_DIR = 2


def u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def p32(v: int) -> bytes:
    return struct.pack("<I", v & 0xFFFFFFFF)


def sb_csum(sb: bytes) -> int:
    s = 0
    for i in range(0, SB_CSUM_OFF, 4):
        s ^= u32(sb, i)
    return s & 0xFFFFFFFF


def read_block(img: bytearray, lba: int) -> bytes:
    off = lba * BLOCK
    return bytes(img[off : off + BLOCK])


def write_block(img: bytearray, lba: int, data: bytes) -> None:
    if len(data) != BLOCK:
        raise ValueError("block size")
    off = lba * BLOCK
    img[off : off + BLOCK] = data


def parse_sb(img: bytes) -> dict:
    sb = img[:BLOCK]
    mag = u32(sb, 0)
    ver = u32(sb, 4)
    fields = {
        "magic": mag,
        "major": ver & 0xFFFF,
        "minor": (ver >> 16) & 0xFFFF,
        "block_size": u32(sb, 8),
        "total_blocks": u32(sb, 12),
        "free_blocks": u32(sb, 16),
        "object_count": u32(sb, 20),
        "free_objects": u32(sb, 24),
        "bitmap_start": u32(sb, 28),
        "bitmap_blocks": u32(sb, 32),
        "obj_start": u32(sb, 36),
        "obj_blocks": u32(sb, 40),
        "data_start": u32(sb, 44),
        "root_id": u32(sb, 48),
        "flags": u32(sb, 52),
        "checksum": u32(sb, 68),
        "checksum_ok": u32(sb, 68) == sb_csum(sb),
    }
    return fields


def bit_set(bitmap: bytearray, blk: int) -> None:
    bitmap[blk // 8] = (bitmap[blk // 8] | (1 << (blk % 8))) & 0xFF


def encode_obj(otype: int, nlink: int = 1) -> bytes:
    buf = bytearray(OBJ_SIZE)
    buf[0:4] = p32(otype)
    buf[40:44] = p32(nlink)
    return bytes(buf)


def cmd_format(path: str, blocks: int) -> None:
    if blocks < 16:
        raise SystemExit("need at least 16 blocks")
    img = bytearray(blocks * BLOCK)
    bitmap_start = 1
    bits = (blocks + 7) // 8
    bitmap_blocks = max(1, (bits + BLOCK - 1) // BLOCK)
    obj_start = 1 + bitmap_blocks
    objs = 256
    obj_blocks = (objs * OBJ_SIZE + BLOCK - 1) // BLOCK
    data_start = obj_start + obj_blocks
    if data_start >= blocks:
        objs = 64
        obj_blocks = (objs * OBJ_SIZE + BLOCK - 1) // BLOCK
        data_start = obj_start + obj_blocks
    if data_start >= blocks:
        raise SystemExit("image too small")
    reserved = data_start
    bitmap = bytearray(bitmap_blocks * BLOCK)
    for i in range(reserved):
        bit_set(bitmap, i)
    for i in range(bitmap_blocks):
        write_block(img, bitmap_start + i, bytes(bitmap[i * BLOCK : (i + 1) * BLOCK]))
    root = encode_obj(OBJ_DIR, 1)
    obj_region = bytearray(obj_blocks * BLOCK)
    obj_region[OBJ_SIZE : OBJ_SIZE * 2] = root
    for i in range(obj_blocks):
        write_block(img, obj_start + i, bytes(obj_region[i * BLOCK : (i + 1) * BLOCK]))
    sb = bytearray(BLOCK)
    sb[0:4] = p32(MAGIC)
    sb[4:8] = p32((MINOR << 16) | MAJOR)
    sb[8:12] = p32(BLOCK)
    sb[12:16] = p32(blocks)
    sb[16:20] = p32(blocks - reserved)
    sb[20:24] = p32(objs)
    sb[24:28] = p32(objs - 2)
    sb[28:32] = p32(bitmap_start)
    sb[32:36] = p32(bitmap_blocks)
    sb[36:40] = p32(obj_start)
    sb[40:44] = p32(obj_blocks)
    sb[44:48] = p32(data_start)
    sb[48:52] = p32(1)
    sb[68:72] = p32(sb_csum(sb))
    write_block(img, 0, bytes(sb))
    with open(path, "wb") as f:
        f.write(img)
    print("formatted %s  blocks=%d  data_start=%d" % (path, blocks, data_start))


def load_obj(img: bytearray, sb: dict, oid: int) -> bytes:
    off = (sb["obj_start"] * BLOCK) + oid * OBJ_SIZE
    return bytes(img[off : off + OBJ_SIZE])


def dir_entries(img: bytearray, sb: dict, oid: int) -> list:
    obj = load_obj(img, sb, oid)
    otype = u32(obj, 0)
    size = struct.unpack_from("<Q", obj, 8)[0]
    if otype != OBJ_DIR:
        return []
    n = size // DIRENT_SIZE
    direct = [u32(obj, 48 + i * 4) for i in range(8)]
    out = []
    for slot in range(n):
        lb = (slot * DIRENT_SIZE) // BLOCK
        off = (slot * DIRENT_SIZE) % BLOCK
        blk = direct[lb] if lb < 8 else 0
        if not blk:
            continue
        data = read_block(img, blk)
        cid = u32(data, off)
        if not cid:
            continue
        ctype = u32(data, off + 4)
        raw = data[off + 8 : off + 8 + NAME_MAX]
        name = raw.split(b"\0", 1)[0].decode("ascii", "replace")
        out.append((cid, ctype, name))
    return out


def cmd_inspect(path: str) -> None:
    with open(path, "rb") as f:
        img = bytearray(f.read())
    sb = parse_sb(img)
    print("magic          : 0x%08x" % sb["magic"])
    print("version        : %u.%u" % (sb["major"], sb["minor"]))
    print("block_size     : %u" % sb["block_size"])
    print("total_blocks   : %u" % sb["total_blocks"])
    print("free_blocks    : %u" % sb["free_blocks"])
    print("object_count   : %u" % sb["object_count"])
    print("free_objects   : %u" % sb["free_objects"])
    print("root_object    : %u" % sb["root_id"])
    print("data_start     : %u" % sb["data_start"])
    print("checksum       : %s" % ("ok" if sb["checksum_ok"] else "BAD"))
    if sb["magic"] == MAGIC and sb["checksum_ok"] and sb["root_id"]:
        print("root directory :")
        for cid, ctype, name in dir_entries(img, sb, sb["root_id"]):
            kind = "dir" if ctype == OBJ_DIR else "file"
            print("  %u  %s  %s" % (cid, kind, name))


def cmd_check(path: str) -> int:
    with open(path, "rb") as f:
        img = f.read()
    sb = parse_sb(img)
    errs = []
    if sb["magic"] != MAGIC:
        errs.append("bad magic")
    if sb["major"] != MAJOR:
        errs.append("unsupported major")
    if sb["block_size"] != BLOCK:
        errs.append("block size")
    if not sb["checksum_ok"]:
        errs.append("checksum")
    if sb["data_start"] >= sb["total_blocks"]:
        errs.append("data_start")
    if not sb["root_id"] or sb["root_id"] >= sb["object_count"]:
        errs.append("root id")
    if errs:
        print("FAIL: " + ", ".join(errs))
        return 1
    print("OK")
    return 0


def cmd_populate(path: str, files: list[str]) -> None:
    raise SystemExit("populate is not implemented; mount the image in-kernel instead")


def main() -> int:
    p = argparse.ArgumentParser(description="RoboFS v1 image tool")
    sub = p.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("format")
    f.add_argument("image")
    f.add_argument("--blocks", type=int, default=256)
    i = sub.add_parser("inspect")
    i.add_argument("image")
    c = sub.add_parser("check")
    c.add_argument("image")
    args = p.parse_args()
    if args.cmd == "format":
        cmd_format(args.image, args.blocks)
        return 0
    if args.cmd == "inspect":
        cmd_inspect(args.image)
        return 0
    if args.cmd == "check":
        return cmd_check(args.image)
    return 1


if __name__ == "__main__":
    sys.exit(main())
