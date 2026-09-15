#!/usr/bin/env python3
"""Read-only structural validation of a RoboticelaOS bootable disk image.

This exists so `robo validate-deployment` can prove the deployment artifact is
real rather than merely present. It parses the artifact the same way firmware
will:

    protective MBR -> GPT header (+ CRC) -> partition array (+ CRC)
      -> ESP partition -> FAT16 boot sector -> directory tree -> files

Nothing here writes to the image. Nothing here trusts a filename: every
verdict comes from the bytes on disk. FAT parsing is imported from the same
tool that writes the image (bootloader/tools/make_esp.py) so the reader and
the writer cannot drift apart.

Exit codes: 0 all checks passed, 1 validation failed, 2 the image could not be
parsed at all.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

SECTOR = 512
GPT_HEADER_LBA = 1
ESP_TYPE_GUID = bytes.fromhex("28732ac11ff8d211ba4b00a0c93ec93b")

# Files the firmware and the OS require, with the loader that consumes them.
REQUIRED_EFI = ("EFI/BOOT/BOOTX64.EFI", "EFI/Roboticela/BOOTX64.EFI")
REQUIRED_SYSTEM = ("volumes/sys/kernel.elf", "volumes/sys/initrd")
OPTIONAL_TREE = ("config", "assets", "volumes")


def load_make_esp(bootloader_dir: Path):
    """Import the ESP writer's FAT readers so there is one FAT implementation."""
    tools = bootloader_dir / "tools"
    if not (tools / "make_esp.py").is_file():
        return None
    # This validator is read-only: importing the writer must not leave a
    # __pycache__ entry behind in the source tree.
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(tools))
    try:
        import make_esp  # type: ignore

        return make_esp
    except Exception:
        return None
    finally:
        sys.path.remove(str(tools))


class Report:
    def __init__(self) -> None:
        self.checks: list[dict] = []

    def add(self, name: str, ok: bool, detail: str = "") -> None:
        self.checks.append({"name": name, "ok": bool(ok), "detail": detail})

    def fail(self, name: str, detail: str) -> None:
        self.add(name, False, detail)

    @property
    def ok(self) -> bool:
        return all(c["ok"] for c in self.checks)

    def render(self) -> str:
        width = max((len(c["name"]) for c in self.checks), default=10)
        lines = []
        for c in self.checks:
            tag = "OK" if c["ok"] else "INVALID"
            line = f"  {tag:<8} {c['name']:<{width}}"
            if c["detail"]:
                line += f"  {c['detail']}"
            lines.append(line)
        return "\n".join(lines)


def guid_le(raw: bytes) -> str:
    """Format a GPT GUID the way firmware tools display it."""
    return (
        f"{raw[3]:02x}{raw[2]:02x}{raw[1]:02x}{raw[0]:02x}-"
        f"{raw[5]:02x}{raw[4]:02x}-{raw[7]:02x}{raw[6]:02x}-"
        f"{raw[8]:02x}{raw[9]:02x}-{raw[10]:02x}{raw[11]:02x}{raw[12]:02x}{raw[13]:02x}{raw[14]:02x}{raw[15]:02x}"
    )


def parse_gpt(disk: bytes, report: Report):
    """Validates the protective MBR and the primary GPT header + entries."""
    if len(disk) < 34 * SECTOR:
        report.fail("disk size", f"only {len(disk)} bytes; too small to hold a GPT")
        return None

    mbr = disk[0:SECTOR]
    if mbr[510:512] != b"\x55\xAA":
        report.fail("protective MBR", "missing 0x55AA boot signature")
    else:
        ptype = mbr[446 + 4]
        if ptype != 0xEE:
            report.fail("protective MBR", f"partition type 0x{ptype:02x}, expected 0xEE (GPT protective)")
        else:
            report.add("protective MBR", True)

    header = disk[GPT_HEADER_LBA * SECTOR : (GPT_HEADER_LBA + 1) * SECTOR]
    if header[0:8] != b"EFI PART":
        report.fail("GPT header", "signature 'EFI PART' not found at LBA 1")
        return None

    header_size = struct.unpack_from("<I", header, 12)[0]
    header_crc = struct.unpack_from("<I", header, 16)[0]
    if header_size < 92 or header_size > SECTOR:
        report.fail("GPT header size", f"{header_size} (must be 92..512)")
        return None
    # The header CRC is defined over the header with its own CRC field zeroed.
    checked = bytearray(header[:header_size])
    checked[16:20] = b"\x00\x00\x00\x00"
    expect = zlib.crc32(bytes(checked)) & 0xFFFFFFFF
    if expect != header_crc:
        report.fail("GPT header CRC", f"stored 0x{header_crc:08x}, computed 0x{expect:08x}")
    else:
        report.add("GPT header CRC", True)

    current = struct.unpack_from("<Q", header, 24)[0]
    backup = struct.unpack_from("<Q", header, 32)[0]
    first_usable = struct.unpack_from("<Q", header, 40)[0]
    last_usable = struct.unpack_from("<Q", header, 48)[0]
    entries_lba = struct.unpack_from("<Q", header, 72)[0]
    entry_count = struct.unpack_from("<I", header, 80)[0]
    entry_size = struct.unpack_from("<I", header, 84)[0]
    entries_crc = struct.unpack_from("<I", header, 88)[0]

    total_sectors = len(disk) // SECTOR
    if current != 1:
        report.fail("GPT primary header", f"CurrentLBA {current}, expected 1")
    else:
        report.add("GPT primary header", True)

    if backup < total_sectors - 34:
        report.fail("GPT backup header", f"BackupLBA {backup} lies inside the data area")
    else:
        backup_off = backup * SECTOR
        if backup_off + SECTOR > len(disk):
            report.fail("GPT backup header", f"points past the end of the image (LBA {backup})")
        elif disk[backup_off : backup_off + 8] != b"EFI PART":
            report.fail("GPT backup header", f"no 'EFI PART' at LBA {backup}")
        else:
            report.add("GPT backup header", True)

    if entry_size != 128 or entry_count == 0:
        report.fail("GPT partition entries", f"count {entry_count}, size {entry_size}")
        return None

    ents_off = entries_lba * SECTOR
    ents = disk[ents_off : ents_off + entry_count * entry_size]
    if len(ents) != entry_count * entry_size:
        report.fail("GPT partition entries", "entry array is truncated")
        return None
    expect = zlib.crc32(ents) & 0xFFFFFFFF
    if expect != entries_crc:
        report.fail("partition array CRC", f"stored 0x{entries_crc:08x}, computed 0x{expect:08x}")
    else:
        report.add("partition array CRC", True)

    partitions = []
    for i in range(entry_count):
        e = ents[i * entry_size : (i + 1) * entry_size]
        if e[0:16] == b"\x00" * 16:
            continue
        start, end = struct.unpack_from("<QQ", e, 32)
        name = e[56:128].decode("utf-16-le", "replace").split("\x00")[0]
        partitions.append(
            {
                "index": i,
                "type": guid_le(e[0:16]),
                "type_raw": e[0:16],
                "unique": guid_le(e[16:32]),
                "start_lba": start,
                "end_lba": end,
                "name": name,
            }
        )

    if not partitions:
        report.fail("partitions", "no GPT partition entries are populated")
        return None
    report.add("partitions", True, f"{len(partitions)}: " + ", ".join(p["name"] or p["type"] for p in partitions))

    esp = next((p for p in partitions if p["type_raw"] == ESP_TYPE_GUID), None)
    if esp is None:
        report.fail("EFI system partition", "no partition with the ESP type GUID")
        return None

    if not (first_usable <= esp["start_lba"] <= last_usable and esp["start_lba"] < esp["end_lba"]):
        report.fail("ESP bounds", f"[{esp['start_lba']}, {esp['end_lba']}] outside usable range")
        return None
    report.add(
        "EFI system partition",
        True,
        f"LBA {esp['start_lba']}..{esp['end_lba']} "
        f"({(esp['end_lba'] - esp['start_lba'] + 1) * SECTOR / (1024 * 1024):.0f} MB)",
    )
    return esp


def read_partition(disk: bytes, esp) -> bytes:
    return disk[esp["start_lba"] * SECTOR : (esp["end_lba"] + 1) * SECTOR]


def parse_fat(part: bytes, report: Report):
    """Parses the FAT16 boot sector and returns its geometry."""
    bps = struct.unpack_from("<H", part, 11)[0]
    spc = part[13]
    reserved = struct.unpack_from("<H", part, 14)[0]
    nfats = part[16]
    root_entries = struct.unpack_from("<H", part, 17)[0]
    total16 = struct.unpack_from("<H", part, 19)[0]
    fatsz = struct.unpack_from("<H", part, 22)[0]
    # FAT16 keeps the sector count in a 16-bit field, but any volume larger than
    # 32 MB cannot represent it there, so the 32-bit field at offset 32 takes
    # over. Reading only the 16-bit field reports a 63 MB partition as empty.
    total32 = struct.unpack_from("<I", part, 32)[0]
    total = total16 if total16 else total32
    label = part[43:54].decode("ascii", "replace").strip()

    if bps not in (512, 1024, 2048, 4096):
        report.fail("ESP boot sector", f"bytes-per-sector {bps} is not a valid FAT value")
        return None
    if spc == 0 or fatsz == 0 or nfats == 0 or root_entries == 0:
        report.fail("ESP boot sector", "zero-valued cluster/root geometry")
        return None
    fat_sectors = nfats * fatsz
    root_sectors = -(-(root_entries * 32) // bps)
    fat_off = reserved * bps
    root_off = fat_off + fat_sectors * bps
    data_off = root_off + root_sectors * bps
    clusters = (total - reserved - fat_sectors - root_sectors) // spc if total else 0
    if not (4085 <= clusters <= 65524):
        report.fail("FAT type", f"{clusters} clusters; firmware expects FAT16 here")
    else:
        report.add("FAT16 filesystem", True, f"label '{label}', {clusters} clusters, {spc * bps} B/cluster")
    return {
        "bps": bps,
        "spc": spc,
        "fat_off": fat_off,
        "root_off": root_off,
        "root_entries": root_entries,
        "data_off": data_off,
    }


class FatReader:
    def __init__(self, part: bytes, geo: dict, make_esp):
        self.part = part
        self.geo = geo
        self.make_esp = make_esp

    def list_dir(self, clus: int):
        """Yields (short_name, long_name, attr, cluster, size) for one directory."""
        if clus == 0:
            blob = self.part[self.geo["root_off"] : self.geo["root_off"] + self.geo["root_entries"] * 32]
        else:
            blob = self._read_chain(clus, 1 << 22)
        return list(self.make_esp._fat_dir_entries(blob))

    def _read_chain(self, start: int, nbytes: int) -> bytes:
        return self.make_esp._fat_read_chain(
            self.part,
            self.geo["fat_off"],
            self.geo["data_off"],
            self.geo["spc"],
            self.geo["bps"],
            start,
            nbytes,
        )

    def read_file(self, clus: int, size: int) -> bytes:
        if size == 0:
            return b""
        return self._read_chain(clus, size)

    def walk(self, clus: int = 0, prefix: str = ""):
        """Yields (path, attr, cluster, size) depth-first."""
        for short, long, attr, c, size in self.list_dir(clus):
            if short in (".", ".."):
                continue
            name = long or short
            if attr & 0x08:  # volume label
                continue
            path = f"{prefix}/{name}" if prefix else name
            if attr & 0x10:
                yield (path, attr, c, 0)
                yield from self.walk(c, path)
            else:
                yield (path, attr, c, size)


def check_format(name: str, blob: bytes, kind: str, report: Report):
    """Confirms the file really is the format its extension claims."""
    if kind == "pe":
        if len(blob) < 0x40 or blob[0:2] != b"MZ":
            report.fail(name, "not a PE image (missing MZ)")
            return
        lfanew = struct.unpack_from("<I", blob, 0x3C)[0]
        if lfanew + 0x5C > len(blob) or blob[lfanew : lfanew + 4] != b"PE\x00\x00":
            report.fail(name, "no PE signature")
            return
        machine = struct.unpack_from("<H", blob, lfanew + 4)[0]
        subsystem = struct.unpack_from("<H", blob, lfanew + 0x5C)[0]
        if machine != 0x8664:
            report.fail(name, f"PE machine 0x{machine:04x}, expected 0x8664 (x86-64)")
            return
        if subsystem != 10:
            report.fail(name, f"PE subsystem {subsystem}, expected 10 (EFI application)")
            return
        report.add(name, True, f"PE32+ x86-64 EFI application, {len(blob)} bytes")
    elif kind == "elf":
        if len(blob) < 64 or blob[0:4] != b"\x7fELF":
            report.fail(name, "not an ELF image")
            return
        if blob[4] != 2:
            report.fail(name, f"ELF class {blob[4]}, expected 2 (64-bit)")
            return
        machine = struct.unpack_from("<H", blob, 18)[0]
        if machine != 0x3E:
            report.fail(name, f"ELF machine 0x{machine:04x}, expected 0x3E (x86-64)")
            return
        etype = struct.unpack_from("<H", blob, 16)[0]
        report.add(name, True, f"ELF64 x86-64 (type {etype}), {len(blob)} bytes")
    else:
        report.add(name, True, f"{len(blob)} bytes")


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate a RoboticelaOS disk image.")
    ap.add_argument("--image", required=True)
    ap.add_argument("--bootloader", required=True, help="bootloader/ directory (for FAT reader reuse)")
    ap.add_argument("--tree", default="", help="deployment tree to cross-check against the image")
    ap.add_argument("--kernel", default="", help="built kernel.elf to compare with the image's copy")
    ap.add_argument("--json", default="", help="write the report as JSON here")
    args = ap.parse_args()

    report = Report()
    image = Path(args.image)
    if not image.is_file():
        print(f"  INVALID  image  not found: {image}")
        return 2

    make_esp = load_make_esp(Path(args.bootloader))
    if make_esp is None:
        print("  INVALID  tooling  could not import bootloader/tools/make_esp.py")
        return 2

    disk = image.read_bytes()
    report.add("image", True, f"{image} - {len(disk) / (1024 * 1024):.1f} MB, {len(disk) // SECTOR} sectors")

    esp = parse_gpt(disk, report)
    if esp is None:
        print(report.render())
        if args.json:
            Path(args.json).write_text(json.dumps({"ok": False, "checks": report.checks}, indent=2), encoding="utf-8")
        return 1

    part = read_partition(disk, esp)
    geo = parse_fat(part, report)
    if geo is None:
        print(report.render())
        if args.json:
            Path(args.json).write_text(json.dumps({"ok": False, "checks": report.checks}, indent=2), encoding="utf-8")
        return 1

    reader = FatReader(part, geo, make_esp)
    files: dict[str, tuple[int, int]] = {}
    try:
        for path, attr, clus, size in reader.walk():
            if not (attr & 0x10):
                files["/" + path] = (clus, size)
    except Exception as exc:  # a corrupt FAT must be a verdict, not a crash
        report.fail("ESP directory tree", f"traversal failed: {exc}")
        print(report.render())
        if args.json:
            Path(args.json).write_text(json.dumps({"ok": False, "checks": report.checks}, indent=2), encoding="utf-8")
        return 1

    report.add("ESP directory tree", True, f"{len(files)} files")

    # --- boot loader -----------------------------------------------------
    for want in REQUIRED_EFI:
        key = "/" + want
        if key not in files:
            report.fail(want, "missing from the ESP")
            continue
        clus, size = files[key]
        check_format(want, reader.read_file(clus, size), "pe", report)

    # --- kernel and initrd ----------------------------------------------
    for want in REQUIRED_SYSTEM:
        if "/" + want not in files:
            report.fail(want, "missing from the ESP")
    if "/volumes/sys/kernel.elf" in files:
        clus, size = files["/volumes/sys/kernel.elf"]
        blob = reader.read_file(clus, size)
        check_format("volumes/sys/kernel.elf", blob, "elf", report)
        if args.kernel:
            built = Path(args.kernel)
            if built.is_file():
                on_disk = built.read_bytes()
                if on_disk != blob:
                    report.fail(
                        "kernel matches build",
                        f"image has {len(blob)} bytes, build has {len(on_disk)} - the image is stale",
                    )
                else:
                    report.add("kernel matches build", True, f"byte-identical ({len(blob)} bytes)")
            else:
                report.fail("kernel matches build", f"built kernel not found: {built}")
    if "/volumes/sys/initrd" in files:
        clus, size = files["/volumes/sys/initrd"]
        if size <= 0:
            report.fail("volumes/sys/initrd", "present but empty")
        else:
            report.add("volumes/sys/initrd", True, f"{size} bytes")

    # --- boot configuration / assets ------------------------------------
    for want in ("EFI",) + OPTIONAL_TREE:
        prefix = "/" + want + "/"
        count = sum(1 for p in files if p.startswith(prefix))
        if count == 0:
            report.fail(want, f"no files under {want}/ in the ESP")
        else:
            report.add(want, True, f"{count} files")

    # --- cross-check against the deployment tree -------------------------
    if args.tree:
        tree = Path(args.tree)
        if not tree.is_dir():
            report.fail("deployment tree", f"not found: {tree}")
        else:
            missing = []
            for rel in list(REQUIRED_EFI) + list(REQUIRED_SYSTEM):
                candidate = tree / Path(rel)
                if not candidate.is_file() or candidate.stat().st_size == 0:
                    missing.append(rel)
            if missing:
                report.fail("deployment tree", "missing or empty: " + ", ".join(missing))
            else:
                report.add("deployment tree", True, f"{tree} matches the image layout")

    ok = report.ok
    print(report.render())
    if args.json:
        Path(args.json).write_text(
            json.dumps(
                {
                    "ok": ok,
                    "image": str(image),
                    "bytes": len(disk),
                    "sectors": len(disk) // SECTOR,
                    "esp": {"start_lba": esp["start_lba"], "end_lba": esp["end_lba"], "name": esp["name"]},
                    "files": sorted(files.keys()),
                    "checks": report.checks,
                },
                indent=2,
            ),
            encoding="utf-8",
        )
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
