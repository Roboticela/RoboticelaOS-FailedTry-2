#!/usr/bin/env python3
"""Build a GPT disk with a FAT16 ESP containing RoboBoot."""

from __future__ import annotations

import argparse
import os
import struct
import uuid
from pathlib import Path

SECTOR = 512
SPC = 8  # 4 KiB clusters
RESERVED = 1
NFAT = 2
ROOT_ENT = 512
GPT_HEADER_LBA = 1
GPT_ENTRIES_LBA = 2
PART_START = 2048


def chk_83(name11: bytes) -> int:
    s = 0
    for b in name11:
        s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
    return s


def split_83(filename: str, used: set[str]) -> bytes:
    raw = filename.replace(" ", "").upper()
    if "." in raw:
        stem, ext = raw.rsplit(".", 1)
    else:
        stem, ext = raw, ""
    stem = "".join(ch if ch.isalnum() or ch in "_-" else "_" for ch in stem)
    ext = "".join(ch if ch.isalnum() else "_" for ch in ext)[:3]
    base = (stem[:8] if len(stem) <= 8 and not any(c.islower() for c in filename.split(".")[0]) else stem[:6])
    if len(stem) > 8 or any(c.islower() for c in filename):
        n = 1
        while True:
            alias = f"{stem[:6]}~{n}"
            alias = alias[:8]
            key = f"{alias}.{ext}"
            if key not in used:
                used.add(key)
                stem8 = alias.ljust(8)
                break
            n += 1
    else:
        stem8 = stem[:8].ljust(8)
        used.add(f"{stem8.strip()}.{ext}")
    return (stem8.encode("ascii") + ext.encode("ascii").ljust(3))[:11]


def lfn_entries(name: str, checksum: int) -> list[bytes]:
    chars = list(name) + ["\x00"]
    while len(chars) % 13:
        chars.append("\uffff")
    chunks = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    out = []
    total = len(chunks)
    for i, chunk in enumerate(reversed(chunks)):
        seq = (total - i)
        if i == 0:
            seq |= 0x40
        buf = bytearray(32)
        buf[0] = seq
        buf[11] = 0x0F
        buf[13] = checksum
        u = [ord(c) for c in chunk]
        def put(off, arr):
            for j, v in enumerate(arr):
                struct.pack_into("<H", buf, off + j * 2, v)
        put(1, u[0:5])
        put(14, u[5:11])
        put(28, u[11:13])
        out.append(bytes(buf))
    return out


class Fat16:
    def __init__(self, sector_count: int):
        self.spc = SPC
        self.fat_sectors = 0
        self.root_sectors = (ROOT_ENT * 32 + SECTOR - 1) // SECTOR
        self.sector_count = sector_count
        self._calc()
        self.fat = [0] * (self.max_cluster + 1)
        self.fat[0] = 0xFFF8
        self.fat[1] = 0xFFFF
        self.next_cluster = 2
        self.root = []  # list of 32-byte entries
        self.data = {}  # cluster -> bytes
        self.used_83: dict[int, set[str]] = {id(self.root): set()}

    def _calc(self) -> None:
        # iterate to stable FAT size
        for _ in range(4):
            data_sectors = self.sector_count - RESERVED - NFAT * self.fat_sectors - self.root_sectors
            clusters = max(0, data_sectors // self.spc)
            self.max_cluster = clusters + 1
            self.fat_sectors = ((self.max_cluster + 1) * 2 + SECTOR - 1) // SECTOR
        self.data_start = RESERVED + NFAT * self.fat_sectors + self.root_sectors

    def alloc(self, nbytes: int) -> list[int]:
        nclus = max(1, (nbytes + self.spc * SECTOR - 1) // (self.spc * SECTOR)) if nbytes else 1
        chain = []
        for _ in range(nclus):
            if self.next_cluster > self.max_cluster:
                raise RuntimeError("ESP image full")
            chain.append(self.next_cluster)
            self.next_cluster += 1
        for i, c in enumerate(chain):
            self.fat[c] = 0xFFFF if i + 1 == len(chain) else chain[i + 1]
        return chain

    def write_chain(self, chain: list[int], blob: bytes) -> None:
        cs = self.spc * SECTOR
        for i, c in enumerate(chain):
            chunk = blob[i * cs:(i + 1) * cs]
            if len(chunk) < cs:
                chunk = chunk + b"\x00" * (cs - len(chunk))
            self.data[c] = chunk

    def add_dir_entries(self, table: list[bytes], used: set[str], name: str, cluster: int, size: int, attr: int) -> None:
        n11 = split_83(name, used)
        for e in lfn_entries(name, chk_83(n11)):
            table.append(e)
        ent = bytearray(32)
        ent[0:11] = n11
        ent[11] = attr
        struct.pack_into("<H", ent, 26, cluster & 0xFFFF)
        struct.pack_into("<I", ent, 28, size)
        table.append(bytes(ent))

    def mkdir(self, parent_table: list[bytes], parent_used: set[str], parent_cluster: int, name: str) -> tuple[list[bytes], set[str], int]:
        chain = self.alloc(self.spc * SECTOR)
        table: list[bytes] = []
        used: set[str] = set()
        # . and ..
        def dot(nm: bytes, clus: int) -> bytes:
            e = bytearray(32)
            e[0:11] = nm
            e[11] = 0x10
            struct.pack_into("<H", e, 26, clus & 0xFFFF)
            return bytes(e)
        table.append(dot(b".          ", chain[0]))
        table.append(dot(b"..         ", parent_cluster))
        self.add_dir_entries(parent_table, parent_used, name, chain[0], 0, 0x10)
        self.used_83[chain[0]] = used
        # keep empty dir cluster reserved
        self._dir_tables = getattr(self, "_dir_tables", {})
        self._dir_tables[chain[0]] = (table, chain)
        return table, used, chain[0]

    def add_file(self, parent_table: list[bytes], parent_used: set[str], name: str, blob: bytes) -> None:
        chain = self.alloc(len(blob) if blob else 1)
        self.write_chain(chain, blob)
        self.add_dir_entries(parent_table, parent_used, name, chain[0], len(blob), 0x20)
        print(f"  + {name} ({len(blob)} bytes)")

    def extend_chain(self, chain: list[int], nbytes: int) -> list[int]:
        cs = self.spc * SECTOR
        need = max(1, (nbytes + cs - 1) // cs)
        while len(chain) < need:
            if self.next_cluster > self.max_cluster:
                raise RuntimeError("ESP image full")
            nxt = self.next_cluster
            self.next_cluster += 1
            self.fat[chain[-1]] = nxt
            self.fat[nxt] = 0xFFFF
            chain.append(nxt)
        return chain

    def finalize_dirs(self) -> None:
        for clus, (table, chain) in getattr(self, "_dir_tables", {}).items():
            blob = b"".join(table)
            self.extend_chain(chain, len(blob) if blob else 1)
            self.write_chain(chain, blob)

    def to_partition(self) -> bytes:
        self.finalize_dirs()
        bps = SECTOR
        boot = bytearray(bps)
        boot[0:3] = b"\xEB\x3C\x90"
        boot[3:11] = b"ROBOBOOT"
        struct.pack_into("<HBHBHHBHHHII", boot, 11,
                         bps, self.spc, RESERVED, NFAT, ROOT_ENT, 0, 0xF8,
                         self.fat_sectors, 32, 2, 0, self.sector_count)
        boot[36] = 0x80
        boot[38] = 0x29
        struct.pack_into("<I", boot, 39, 0x52424F54)
        boot[43:54] = b"ROBOBOOT   "
        boot[54:62] = b"FAT16   "
        boot[510] = 0x55
        boot[511] = 0xAA

        fatb = bytearray(self.fat_sectors * bps)
        for i, v in enumerate(self.fat):
            if i * 2 + 1 < len(fatb):
                struct.pack_into("<H", fatb, i * 2, v & 0xFFFF)

        root = bytearray(self.root_sectors * bps)
        raw = b"".join(self.root)
        root[:len(raw)] = raw

        data_bytes = (self.sector_count - self.data_start) * bps
        data = bytearray(data_bytes)
        cs = self.spc * bps
        for c, blob in self.data.items():
            off = (c - 2) * cs
            data[off:off + len(blob)] = blob

        return bytes(boot) + bytes(fatb) * NFAT + bytes(root) + bytes(data)


def gpt_crc(data: bytes) -> int:
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF


def build_disk(part: bytes, disk_sectors: int) -> bytes:
    part_sectors = len(part) // SECTOR
    part_end = PART_START + part_sectors - 1
    disk = bytearray(disk_sectors * SECTOR)

    # Protective MBR
    mbr = bytearray(SECTOR)
    mbr[446:462] = bytes([
        0x00, 0x00, 0x02, 0x00, 0xEE, 0xFF, 0xFF, 0xFF,
        0x01, 0x00, 0x00, 0x00
    ]) + struct.pack("<I", disk_sectors - 1)
    mbr[510] = 0x55
    mbr[511] = 0xAA
    disk[0:SECTOR] = mbr

    esp_guid = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
    part_guid = uuid.uuid4()
    disk_guid = uuid.uuid4()

    def guid_bytes(u: uuid.UUID) -> bytes:
        b = u.bytes_le
        return b

    entry = bytearray(128)
    entry[0:16] = guid_bytes(esp_guid)
    entry[16:32] = guid_bytes(part_guid)
    struct.pack_into("<QQ", entry, 32, PART_START, part_end)
    name = "RoboticelaOS".encode("utf-16le")
    entry[56:56 + len(name)] = name

    entries = bytearray(32 * SECTOR)
    entries[0:128] = entry

    header = bytearray(SECTOR)
    header[0:8] = b"EFI PART"
    struct.pack_into("<I", header, 8, 0x00010000)
    struct.pack_into("<I", header, 12, 92)
    struct.pack_into("<Q", header, 24, 1)
    struct.pack_into("<Q", header, 32, disk_sectors - 1)
    struct.pack_into("<Q", header, 40, PART_START)
    struct.pack_into("<Q", header, 48, disk_sectors - 34)
    header[56:72] = guid_bytes(disk_guid)
    struct.pack_into("<Q", header, 72, GPT_ENTRIES_LBA)
    struct.pack_into("<I", header, 80, 128)
    struct.pack_into("<I", header, 84, 128)
    struct.pack_into("<I", header, 88, gpt_crc(bytes(entries)))
    struct.pack_into("<I", header, 16, gpt_crc(bytes(header[:92])))

    disk[SECTOR:2 * SECTOR] = header
    disk[2 * SECTOR:2 * SECTOR + len(entries)] = entries
    disk[PART_START * SECTOR:PART_START * SECTOR + len(part)] = part

    # backup entries + header at end
    disk[(disk_sectors - 33) * SECTOR:(disk_sectors - 1) * SECTOR] = entries
    bhdr = bytearray(header)
    struct.pack_into("<Q", bhdr, 24, disk_sectors - 1)  # current = backup
    struct.pack_into("<Q", bhdr, 32, 1)  # backup of backup is primary
    struct.pack_into("<Q", bhdr, 72, disk_sectors - 33)
    struct.pack_into("<I", bhdr, 16, 0)
    struct.pack_into("<I", bhdr, 16, gpt_crc(bytes(bhdr[:92])))
    disk[(disk_sectors - 1) * SECTOR:] = bhdr
    return bytes(disk)


def add_tree(fat: Fat16, table: list[bytes], used: set[str], parent_clus: int, src: Path, rel: str = "") -> None:
    skip = {".png", ".svg", ".gitkeep", ".gitignore"}
    for child in sorted(src.iterdir(), key=lambda p: p.name.lower()):
        if child.name.startswith("."):
            continue
        if child.suffix.lower() in skip and child.name not in ("qemu.flags",):
            if child.suffix.lower() in skip:
                continue
        if child.is_dir():
            if child.name in ("icons", "build", "src", "include", "tests", "tools"):
                continue
            nt, nu, nc = fat.mkdir(table, used, parent_clus, child.name)
            add_tree(fat, nt, nu, nc, child, rel + "/" + child.name)
        else:
            fat.add_file(table, used, child.name, child.read_bytes())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bootloader", required=True)
    ap.add_argument("--efi", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--flags", default="")
    ap.add_argument("--size-mb", type=int, default=64)
    args = ap.parse_args()

    boot = Path(args.bootloader)
    efi = Path(args.efi)
    out = Path(args.out)
    disk_sectors = args.size_mb * 1024 * 1024 // SECTOR
    part_sectors = disk_sectors - PART_START - 33

    fat = Fat16(part_sectors)
    used = set()
    # EFI/BOOT/BOOTX64.EFI
    efi_table, efi_used, efi_clus = fat.mkdir(fat.root, used, 0, "EFI")
    boot_table, boot_used, _ = fat.mkdir(efi_table, efi_used, efi_clus, "BOOT")
    fat.add_file(boot_table, boot_used, "BOOTX64.EFI", efi.read_bytes())
    vendor_table, vendor_used, _ = fat.mkdir(efi_table, efi_used, efi_clus, "Roboticela")
    fat.add_file(vendor_table, vendor_used, "BOOTX64.EFI", efi.read_bytes())

    for name in ("config", "assets", "volumes"):
        src = boot / name
        if src.is_dir():
            nt, nu, nc = fat.mkdir(fat.root, used, 0, name)
            add_tree(fat, nt, nu, nc, src)

    flags = args.flags.replace(",", "\n").strip()
    if flags:
        fat.add_file(fat.root, used, "qemu.flags", (flags + "\n").encode("ascii"))

    part = fat.to_partition()
    if len(part) < part_sectors * SECTOR:
        part = part + b"\x00" * (part_sectors * SECTOR - len(part))
    disk = build_disk(part[:part_sectors * SECTOR], disk_sectors)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(disk)
    print(f"ESP image {out} ({len(disk)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
