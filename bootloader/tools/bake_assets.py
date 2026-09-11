#!/usr/bin/env python3
"""Pack RoboBoot pictures, SVG logo, fonts, and dummy payloads."""

from __future__ import annotations

import io
import os
import struct
import sys
from collections import deque
from pathlib import Path

import cairosvg
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
FONT_OUT = ASSETS / "fonts"
VOLUMES = ROOT / "volumes"


def write_rbim(im: Image.Image, dest: Path) -> None:
    im = im.convert("RGBA")
    w, h = im.size
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as fh:
        fh.write(b"RBIM")
        fh.write(struct.pack("<III", w, h, 0))
        fh.write(im.tobytes("raw", "RGBA"))


def raster_svg(path: Path, width: int = 720) -> Image.Image:
    png = cairosvg.svg2png(url=str(path), output_width=width)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def knockout_backdrop(im: Image.Image) -> Image.Image:
    """Clear black or white studio backdrops, starting from the frame edges."""
    im = im.convert("RGBA")
    w, h = im.size
    pix = im.load()

    def luma(r: int, g: int, b: int) -> float:
        return 0.2126 * r + 0.7152 * g + 0.0722 * b

    def sat(r: int, g: int, b: int) -> int:
        return max(r, g, b) - min(r, g, b)

    def is_studio(r: int, g: int, b: int, a: int) -> bool:
        if a < 10:
            return True
        if r <= 14 and g <= 14 and b <= 14:
            return True
        if luma(r, g, b) >= 226 and sat(r, g, b) < 22:
            return True
        return False

    seen = bytearray(w * h)
    q: deque[tuple[int, int]] = deque()
    for x in range(w):
        q.append((x, 0))
        q.append((x, h - 1))
    for y in range(h):
        q.append((0, y))
        q.append((w - 1, y))
    while q:
        x, y = q.popleft()
        if x < 0 or y < 0 or x >= w or y >= h:
            continue
        i = y * w + x
        if seen[i]:
            continue
        seen[i] = 1
        r, g, b, a = pix[x, y]
        if not is_studio(r, g, b, a):
            continue
        pix[x, y] = (0, 0, 0, 0)
        q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))

    # Nibble the soft contact-shadow without eating brushed metal.
    for _ in range(10):
        doomed = []
        for y in range(h):
            for x in range(w):
                r, g, b, a = pix[x, y]
                if a < 10:
                    continue
                if luma(r, g, b) < 236 or sat(r, g, b) > 16:
                    continue
                edge = False
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and pix[nx, ny][3] < 10:
                        edge = True
                        break
                if edge:
                    doomed.append((x, y))
        if not doomed:
            break
        for x, y in doomed:
            pix[x, y] = (0, 0, 0, 0)
    return im


def place_mark(drive: Image.Image, mark: Image.Image, cy_frac: float, cover: float) -> Image.Image:
    drive = drive.convert("RGBA")
    mark = mark.convert("RGBA")
    w, h = drive.size
    target = max(32, int(min(w, h) * cover))
    mark = mark.copy()
    mark.thumbnail((target, target), Image.Resampling.LANCZOS)
    x = w // 2 - mark.width // 2
    y = int(h * cy_frac) - mark.height // 2
    out = drive.copy()
    out.alpha_composite(mark, (x, y))
    return out


def window_mark(size: int = 280) -> Image.Image:
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    m = int(size * 0.10)
    d.rounded_rectangle([m, m, size - m, size - m], radius=int(size * 0.12), fill=(255, 255, 255, 235))
    bar = m + int(size * 0.16)
    d.rounded_rectangle([m, m, size - m, bar], radius=int(size * 0.08), fill=(232, 234, 238, 255))
    d.rounded_rectangle([m, bar - 4, size - m, size - m], radius=int(size * 0.08), fill=(255, 255, 255, 235))
    gap = int(size * 0.045)
    inner = bar + gap
    cell = (size - m - inner - gap) // 2
    for row in range(2):
        for col in range(2):
            x0 = m + gap + col * (cell + gap)
            y0 = inner + row * (cell + gap)
            d.rounded_rectangle([x0, y0, x0 + cell, y0 + cell], radius=8, fill=(214, 218, 224, 255))
    return im


def recovery_mark(size: int = 280) -> Image.Image:
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pad = int(size * 0.08)
    d.ellipse([pad, pad, size - pad, size - pad], fill=(255, 255, 255, 236))
    cx = cy = size / 2
    arm = size * 0.13
    span = size * 0.42
    d.rounded_rectangle([cx - span / 2, cy - arm / 2, cx + span / 2, cy + arm / 2],
                        radius=arm / 2, fill=(40, 44, 48, 255))
    d.rounded_rectangle([cx - arm / 2, cy - span / 2, cx + arm / 2, cy + span / 2],
                        radius=arm / 2, fill=(40, 44, 48, 255))
    return im


def trim_alpha(im: Image.Image, pad: int = 8) -> Image.Image:
    im = im.convert("RGBA")
    bbox = im.split()[-1].getbbox()
    if not bbox:
        return im
    cropped = im.crop(bbox)
    w, h = cropped.size
    out = Image.new("RGBA", (w + pad * 2, h + pad * 2), (0, 0, 0, 0))
    out.alpha_composite(cropped, (pad, pad))
    return out


def finish_pic(im: Image.Image) -> Image.Image:
    """Tight crop, then contain in a square well so picker columns stay even."""
    im = trim_alpha(im, pad=8)
    side = 720
    box = side - 48
    fitted = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    im.thumbnail((box, box), Image.Resampling.LANCZOS)
    fitted.alpha_composite(im, ((side - im.width) // 2, (side - im.height) // 2))
    return fitted


def prepare_drive(src: Path) -> Image.Image:
    im = Image.open(src).convert("RGBA")
    if src.name != "HDD.png":
        im = knockout_backdrop(im)
    # Fit to the HDD icon canvas so picker pics share the same framing.
    canvas_w, canvas_h = 515, 720
    fitted = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    im.thumbnail((canvas_w, canvas_h), Image.Resampling.LANCZOS)
    fitted.alpha_composite(im, ((canvas_w - im.width) // 2, (canvas_h - im.height) // 2))
    return fitted


def build_pics() -> dict[str, Image.Image]:
    hdd = Image.open(ASSETS / "HDD.png").convert("RGBA")
    ssd = prepare_drive(ASSETS / "SSD.png")
    nvme = prepare_drive(ASSETS / "NVMe.png")
    usb = prepare_drive(ASSETS / "USB.png")
    logo = raster_svg(ASSETS / "logo.svg", width=420)

    pics = {
        "HDD": hdd,
        "SSD": ssd,
        "NVMe": nvme,
        "USB": usb,
        "os-roboticela": place_mark(hdd, logo, cy_frac=0.33, cover=0.46),
        "os-windows": place_mark(ssd, window_mark(), cy_frac=0.46, cover=0.34),
        "os-recovery": place_mark(hdd, recovery_mark(), cy_frac=0.33, cover=0.34),
        "os-firmware": nvme,
    }
    return {name: finish_pic(im) for name, im in pics.items()}


def pack_images() -> None:
    logo = raster_svg(ASSETS / "logo.svg", width=1024)
    write_rbim(logo, ASSETS / "logo.rbim")

    pics = build_pics()
    for name, im in pics.items():
        png_path = ASSETS / f"{name}.png"
        if name not in ("HDD", "SSD", "NVMe", "USB"):
            im.save(png_path)
        write_rbim(im, ASSETS / f"{name}.rbim")


def bake_font(ttf_path: Path, px: int, dest: Path) -> None:
    font = ImageFont.truetype(str(ttf_path), px)
    first, last = 32, 126
    tmp = Image.new("L", (4, 4))
    draw = ImageDraw.Draw(tmp)
    glyphs = []
    for code in range(first, last + 1):
        ch = chr(code)
        bbox = draw.textbbox((0, 0), ch, font=font)
        gw = max(1, bbox[2] - bbox[0])
        gh = max(1, bbox[3] - bbox[1])
        glyphs.append((ch, gw, gh, bbox))

    atlas_w = 512
    pad = 2
    x = pad
    y = pad
    row_h = 0
    packed = []
    for ch, gw, gh, bbox in glyphs:
        cell_w = gw + pad * 2
        cell_h = gh + pad * 2
        if x + cell_w >= atlas_w:
            x = pad
            y += row_h + pad
            row_h = 0
        packed.append((ch, x + pad, y + pad, gw, gh, bbox))
        x += cell_w
        row_h = max(row_h, cell_h)
    atlas_h = y + row_h + pad + 4
    atlas = Image.new("L", (atlas_w, atlas_h), 0)
    ad = ImageDraw.Draw(atlas)
    records = []
    for ch, gx, gy, gw, gh, bbox in packed:
        ox, oy = -bbox[0], -bbox[1]
        ad.text((gx + ox, gy + oy), ch, font=font, fill=255)
        advance = max(gw + 1, int(font.getlength(ch)))
        records.append((gx, gy, gw, gh, advance, bbox[0], bbox[1]))
    atlas = atlas.filter(ImageFilter.GaussianBlur(radius=0.15))
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as fh:
        fh.write(b"RBFN")
        fh.write(struct.pack("<HHHHHH", px, first, last, atlas_w, atlas_h, len(records)))
        for rec in records:
            fh.write(struct.pack("<HHhhHhh", rec[0], rec[1], rec[2], rec[3], rec[4], rec[5], rec[6]))
        fh.write(atlas.tobytes())


def write_dummy_elf(path: Path, entry: int = 0x401000) -> None:
    ehsize = 64
    phsize = 56
    phoff = 64
    offset = 0x1000
    filesz = 32
    data = bytearray(offset + filesz)
    data[0:16] = b"\x7fELF\x02\x01\x01" + b"\x00" * 9
    struct.pack_into("<HHIQQQIHHHHHH", data, 16,
                     2, 62, 1, entry, phoff, 0, 0, ehsize, phsize, 1, 0, 0, 0)
    struct.pack_into("<IIQQQQQQ", data, phoff,
                     1, 5, offset, entry, entry, filesz, filesz, 0x1000)
    data[offset:offset + 4] = b"\xf4\xeb\xfd\x90"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(data))


def write_dummy_payloads() -> None:
    sys_dir = VOLUMES / "sys"
    sys_dir.mkdir(parents=True, exist_ok=True)
    write_dummy_elf(sys_dir / "kernel.elf")
    write_dummy_elf(sys_dir / "recovery.elf", entry=0x402000)
    write_dummy_elf(sys_dir / "install.elf", entry=0x403000)
    initrd = sys_dir / "initrd"
    if not initrd.exists():
        initrd.write_bytes(b"RBINITRD\x00dummy-init-ramdisk\n")
    win = VOLUMES / "windows" / "EFI" / "Microsoft" / "Boot"
    win.mkdir(parents=True, exist_ok=True)
    (win / "bootmgfw.efi").write_bytes(b"MZ-DUMMY-EFI-CHAINLOAD")


def pick_font(semibold: bool = False) -> Path:
    fonts = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"
    names = (
        ("segoeuisb.ttf", "SegoeUISB.ttf", "calibrib.ttf", "segoeui.ttf", "arialbd.ttf")
        if semibold
        else ("segoeui.ttf", "SegoeUI.ttf", "calibri.ttf", "arial.ttf")
    )
    for name in names:
        candidate = fonts / name
        if candidate.exists():
            return candidate
    raise SystemExit("No UI font found for atlas baking")


def main() -> int:
    pack_images()
    FONT_OUT.mkdir(parents=True, exist_ok=True)
    bake_font(pick_font(True), 28, FONT_OUT / "ui-title.rbfn")
    bake_font(pick_font(False), 17, FONT_OUT / "ui-body.rbfn")
    bake_font(pick_font(False), 12, FONT_OUT / "ui-caption.rbfn")
    write_dummy_payloads()
    print("RoboBoot assets baked (SVG logo + pic drives).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
