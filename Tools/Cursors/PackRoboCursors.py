#!/usr/bin/env python3
"""Pack RoboticelaOS system cursors: SVG masters + roboticela.rcur (PNG frames).

Usage:
  python Tools/Cursors/PackRoboCursors.py
  python Tools/Cursors/PackRoboCursors.py --root System/Cursors

Produces:
  System/Cursors/Masters/*.svg
  System/Cursors/roboticela.rcur

.rcur layout (v1):
  magic "RCUR" | major | minor | flags | reserved
  uint32 meta_len + UTF-8 JSON metadata
  uint16 cursor_count
  index: state_id, name_len, name, offset, length  (per cursor)
  entries: theme_count, themes{ theme_id, hotspot_x/y, frame_count,
           duration_ms, loop, frames{ res_count, sizes{ px, png_len, png } } }
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import zlib
from pathlib import Path

ROOT_DEFAULT = Path(__file__).resolve().parents[2] / "System" / "Cursors"

# state_id matches RoboInput where possible; extras use ≥64.
CURSORS = [
    ("default", 0, 70, 50, "arrow"),
    ("pointer", 1, 84, 49, "hand"),
    ("text", 2, 128, 128, "ibeam"),
    ("crosshair", 10, 128, 128, "cross"),
    ("move", 4, 128, 128, "move"),
    ("resize-h", 5, 128, 128, "ew"),
    ("resize-v", 6, 128, 128, "ns"),
    ("resize-diagonal", 8, 128, 128, "nwse"),
    ("not-allowed", 5, 128, 128, "forbid"),  # Input id 8 — fix below
    ("wait", 9, 128, 128, "wait"),
    ("progress", 10, 52, 38, "progress"),
    ("grabbing", 12, 128, 128, "grab"),
    ("pencil", 64, 40, 220, "pencil"),
    ("link", 65, 89, 41, "hand"),
    ("vertical-text", 66, 128, 128, "hibeam"),
    ("resize-diag-nesw", 67, 128, 128, "nesw"),
    ("col-resize", 68, 128, 128, "colew"),
    ("row-resize", 69, 128, 128, "rowns"),
]

# Correct InputCursorType IDs for the first 13 roles.
STATE_IDS = {
    "default": 0,
    "pointer": 1,
    "text": 2,
    "crosshair": 3,
    "move": 4,
    "resize-h": 5,
    "resize-v": 6,
    "resize-diagonal": 7,
    "not-allowed": 8,
    "wait": 9,
    "progress": 10,
    "grabbing": 12,
    "pencil": 64,
    "link": 65,
    "vertical-text": 66,
    "resize-diag-nesw": 67,
    "col-resize": 68,
    "row-resize": 69,
}

SIZES = (32, 48, 64)


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(width: int, height: int, rgba: bytearray) -> bytes:
    raw = bytearray()
    row = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * row : (y + 1) * row])
    compressed = zlib.compress(bytes(raw), 9)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + png_chunk(b"IDAT", compressed)
        + png_chunk(b"IEND", b"")
    )


def clamp(v: int, lo: int, hi: int) -> int:
    return lo if v < lo else hi if v > hi else v


def put(rgba: bytearray, w: int, h: int, x: int, y: int, r: int, g: int, b: int, a: int = 255) -> None:
    if x < 0 or y < 0 or x >= w or y >= h or a <= 0:
        return
    i = (y * w + x) * 4
    if a >= 255:
        rgba[i : i + 4] = bytes((r, g, b, 255))
        return
    oa = rgba[i + 3]
    if oa == 0:
        rgba[i : i + 4] = bytes((r, g, b, a))
        return
    na = a + (oa * (255 - a)) // 255
    rgba[i] = (r * a + rgba[i] * oa * (255 - a) // 255) // max(na, 1)
    rgba[i + 1] = (g * a + rgba[i + 1] * oa * (255 - a) // 255) // max(na, 1)
    rgba[i + 2] = (b * a + rgba[i + 2] * oa * (255 - a) // 255) // max(na, 1)
    rgba[i + 3] = na


def fill_poly(rgba: bytearray, w: int, h: int, pts, fill, stroke=None, sw=1) -> None:
    if len(pts) < 3:
        return
    miny = max(0, int(min(p[1] for p in pts)))
    maxy = min(h - 1, int(max(p[1] for p in pts)))
    for y in range(miny, maxy + 1):
        xs = []
        for i in range(len(pts)):
            x0, y0 = pts[i]
            x1, y1 = pts[(i + 1) % len(pts)]
            if (y0 <= y < y1) or (y1 <= y < y0):
                t = (y - y0) / (y1 - y0) if y1 != y0 else 0.0
                xs.append(x0 + t * (x1 - x0))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            x_a = int(math.floor(xs[i]))
            x_b = int(math.ceil(xs[i + 1]))
            for x in range(max(0, x_a), min(w, x_b + 1)):
                put(rgba, w, h, x, y, *fill)
    if stroke and sw > 0:
        for i in range(len(pts)):
            x0, y0 = pts[i]
            x1, y1 = pts[(i + 1) % len(pts)]
            steps = max(int(abs(x1 - x0) + abs(y1 - y0)), 1)
            for s in range(steps + 1):
                t = s / steps
                x = int(x0 + (x1 - x0) * t)
                y = int(y0 + (y1 - y0) * t)
                for dy in range(-sw, sw + 1):
                    for dx in range(-sw, sw + 1):
                        put(rgba, w, h, x + dx, y + dy, *stroke)


def line(rgba, w, h, x0, y0, x1, y1, color, sw=2):
    steps = max(int(abs(x1 - x0) + abs(y1 - y0)), 1)
    for s in range(steps + 1):
        t = s / steps
        x = int(x0 + (x1 - x0) * t)
        y = int(y0 + (y1 - y0) * t)
        for dy in range(-sw, sw + 1):
            for dx in range(-sw, sw + 1):
                put(rgba, w, h, x + dx, y + dy, *color)


def circle(rgba, w, h, cx, cy, r, stroke, fill=None, sw=2):
    for y in range(int(cy - r - sw), int(cy + r + sw) + 1):
        for x in range(int(cx - r - sw), int(cx + r + sw) + 1):
            d = math.hypot(x - cx, y - cy)
            if fill and d <= r - sw:
                put(rgba, w, h, x, y, *fill)
            elif abs(d - r) <= sw:
                put(rgba, w, h, x, y, *stroke)


def theme_colors(dark: bool):
    if dark:
        return (39, 39, 42), (24, 24, 27), (255, 255, 255), (59, 158, 255), (248, 113, 113)
    return (255, 255, 255), (241, 245, 249), (15, 23, 42), (59, 158, 255), (220, 38, 38)


def draw_shape(kind: str, size: int, dark: bool, frame: int = 0) -> bytes:
    fill_s, fill_e, stroke, accent, danger = theme_colors(dark)
    rgba = bytearray(size * size * 4)
    s = size / 256.0

    def P(x, y):
        return (x * s, y * s)

    def scale_pts(pts):
        return [P(x, y) for x, y in pts]

    sw = max(1, int(round(2 * s * 2)))

    if kind == "arrow":
        pts = scale_pts([(70, 50), (70, 204), (108, 171), (130, 220), (152, 209), (125, 160), (180, 160)])
        fill_poly(rgba, size, size, pts, fill_s, stroke, sw)
    elif kind == "hand":
        # Simplified pointing hand blob
        pts = scale_pts(
            [
                (90, 50),
                (110, 50),
                (118, 140),
                (150, 120),
                (170, 130),
                (160, 160),
                (200, 170),
                (190, 210),
                (100, 210),
                (80, 160),
                (70, 110),
            ]
        )
        fill_poly(rgba, size, size, pts, fill_s, stroke, sw)
    elif kind == "grab":
        pts = scale_pts(
            [
                (90, 80),
                (120, 70),
                (150, 80),
                (180, 90),
                (200, 120),
                (200, 180),
                (160, 210),
                (100, 210),
                (70, 170),
                (70, 120),
            ]
        )
        fill_poly(rgba, size, size, pts, fill_s, stroke, sw)
    elif kind == "ibeam":
        cx, cy = P(128, 128)
        bar_w = 6 * s
        bar_h = 140 * s
        cap = 36 * s
        # stem
        fill_poly(
            rgba,
            size,
            size,
            [
                (cx - bar_w, cy - bar_h / 2),
                (cx + bar_w, cy - bar_h / 2),
                (cx + bar_w, cy + bar_h / 2),
                (cx - bar_w, cy + bar_h / 2),
            ],
            fill_s,
            stroke,
            sw,
        )
        for y in (cy - bar_h / 2, cy + bar_h / 2):
            fill_poly(
                rgba,
                size,
                size,
                [(cx - cap, y - bar_w), (cx + cap, y - bar_w), (cx + cap, y + bar_w), (cx - cap, y + bar_w)],
                fill_s,
                stroke,
                sw,
            )
    elif kind == "hibeam":
        cx, cy = P(128, 128)
        bar_w = 140 * s
        bar_h = 6 * s
        cap = 36 * s
        fill_poly(
            rgba,
            size,
            size,
            [
                (cx - bar_w / 2, cy - bar_h),
                (cx + bar_w / 2, cy - bar_h),
                (cx + bar_w / 2, cy + bar_h),
                (cx - bar_w / 2, cy + bar_h),
            ],
            fill_s,
            stroke,
            sw,
        )
        for x in (cx - bar_w / 2, cx + bar_w / 2):
            fill_poly(
                rgba,
                size,
                size,
                [(x - bar_h, cy - cap), (x + bar_h, cy - cap), (x + bar_h, cy + cap), (x - bar_h, cy + cap)],
                fill_s,
                stroke,
                sw,
            )
    elif kind == "cross":
        t = 10 * s
        fill_poly(
            rgba,
            size,
            size,
            scale_pts(
                [
                    (118, 20),
                    (138, 20),
                    (138, 118),
                    (236, 118),
                    (236, 138),
                    (138, 138),
                    (138, 236),
                    (118, 236),
                    (118, 138),
                    (20, 138),
                    (20, 118),
                    (118, 118),
                ]
            ),
            fill_s,
            stroke,
            sw,
        )
    elif kind == "move":
        fill_poly(
            rgba,
            size,
            size,
            scale_pts(
                [
                    (128, 16),
                    (84, 60),
                    (112, 60),
                    (112, 112),
                    (60, 112),
                    (60, 84),
                    (16, 128),
                    (60, 172),
                    (60, 144),
                    (112, 144),
                    (112, 196),
                    (84, 196),
                    (128, 240),
                    (172, 196),
                    (144, 196),
                    (144, 144),
                    (196, 144),
                    (196, 172),
                    (240, 128),
                    (196, 84),
                    (196, 112),
                    (144, 112),
                    (144, 60),
                    (172, 60),
                ]
            ),
            fill_s,
            stroke,
            sw,
        )
    elif kind == "ew":
        fill_poly(
            rgba,
            size,
            size,
            scale_pts([(20, 128), (64, 84), (64, 112), (193, 112), (193, 84), (237, 128), (193, 172), (193, 144), (64, 144), (64, 172)]),
            fill_s,
            stroke,
            sw,
        )
    elif kind == "ns":
        fill_poly(
            rgba,
            size,
            size,
            scale_pts([(128, 20), (84, 64), (112, 64), (112, 193), (84, 193), (128, 237), (172, 193), (144, 193), (144, 64), (172, 64)]),
            fill_s,
            stroke,
            sw,
        )
    elif kind == "nwse":
        fill_poly(
            rgba,
            size,
            size,
            scale_pts([(52, 52), (114, 52), (94, 71), (185, 163), (205, 143), (205, 205), (143, 205), (163, 185), (71, 94), (52, 114)]),
            fill_s,
            stroke,
            sw,
        )
    elif kind == "nesw":
        fill_poly(
            rgba,
            size,
            size,
            scale_pts([(52, 204), (52, 142), (71, 162), (163, 71), (143, 51), (205, 51), (205, 113), (185, 93), (94, 185), (114, 204)]),
            fill_s,
            stroke,
            sw,
        )
    elif kind == "colew":
        fill_poly(
            rgba,
            size,
            size,
            scale_pts(
                [
                    (20, 128),
                    (64, 84),
                    (64, 112),
                    (118, 112),
                    (118, 20),
                    (138, 20),
                    (138, 112),
                    (192, 112),
                    (192, 84),
                    (236, 128),
                    (192, 172),
                    (192, 142),
                    (138, 142),
                    (138, 236),
                    (118, 236),
                    (118, 142),
                    (64, 142),
                    (64, 172),
                ]
            ),
            fill_s,
            stroke,
            sw,
        )
    elif kind == "rowns":
        fill_poly(
            rgba,
            size,
            size,
            scale_pts(
                [
                    (128, 20),
                    (172, 64),
                    (144, 64),
                    (144, 118),
                    (236, 118),
                    (236, 138),
                    (144, 138),
                    (144, 192),
                    (172, 192),
                    (128, 236),
                    (84, 192),
                    (112, 192),
                    (112, 138),
                    (20, 138),
                    (20, 118),
                    (112, 118),
                    (112, 64),
                    (84, 64),
                ]
            ),
            fill_s,
            stroke,
            sw,
        )
    elif kind == "forbid":
        cx, cy, r = P(128, 128)[0], P(128, 128)[1], 84 * s
        circle(rgba, size, size, cx, cy, r, danger, None, max(2, int(4 * s)))
        line(rgba, size, size, 70 * s, 186 * s, 186 * s, 70 * s, danger, max(2, int(4 * s)))
    elif kind == "wait":
        cx, cy, r = P(128, 128)[0], P(128, 128)[1], 100 * s
        track = (72, 72, 74) if dark else (142, 142, 147)
        circle(rgba, size, size, cx, cy, r, track, None, max(2, int(5 * s)))
        # spinning arc via frame
        ang0 = (frame % 12) * (math.pi * 2 / 12)
        for a in range(40):
            ang = ang0 + a * 0.04
            x = int(cx + math.cos(ang) * r)
            y = int(cy + math.sin(ang) * r)
            for dy in range(-3, 4):
                for dx in range(-3, 4):
                    put(rgba, size, size, x + dx, y + dy, *accent)
    elif kind == "progress":
        # small arrow + corner spinner
        pts = scale_pts([(70, 50), (70, 204), (108, 171), (130, 220), (152, 209), (125, 160), (180, 160)])
        # scale arrow down into top-left
        pts = [(x * 0.55, y * 0.55) for x, y in pts]
        fill_poly(rgba, size, size, pts, fill_s, stroke, sw)
        cx, cy, r = 192 * s, 192 * s, 36 * s
        track = (72, 72, 74) if dark else (142, 142, 147)
        circle(rgba, size, size, cx, cy, r, track, None, max(2, int(3 * s)))
        ang0 = (frame % 12) * (math.pi * 2 / 12)
        for a in range(30):
            ang = ang0 + a * 0.05
            x = int(cx + math.cos(ang) * r)
            y = int(cy + math.sin(ang) * r)
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    put(rgba, size, size, x + dx, y + dy, *accent)
    elif kind == "pencil":
        pts = scale_pts([(40, 220), (61, 210), (212, 67), (193, 48), (58, 182)])
        fill_poly(rgba, size, size, pts, fill_s, stroke, sw)
        tip = scale_pts([(40, 220), (61, 210), (50, 199)])
        fill_poly(rgba, size, size, tip, stroke, None, 0)
    else:
        # fallback arrow
        return draw_shape("arrow", size, dark, frame)

    return write_png(size, size, rgba)


def svg_shell(body: str, w: int = 256, h: int = 256) -> str:
    return (
        f'<svg width="{w}" height="{h}" viewBox="0 0 {w} {h}" fill="none" '
        f'xmlns="http://www.w3.org/2000/svg">\n{body}\n</svg>\n'
    )


def write_svg_masters(masters: Path) -> None:
    masters.mkdir(parents=True, exist_ok=True)
    # Compact SVG masters (light-theme baked) for tooling / CSS / gallery.
    svgs = {
        "RoboDefault.svg": svg_shell(
            '  <path d="M70 50 L70 204 L108.5 171 L130.5 220.5 L152.5 209.5 L125 160 L180 160 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboPointer.svg": svg_shell(
            '  <path d="M86 50 L110 48 L120 140 L155 125 L175 140 L165 175 L200 185 L185 220 L95 215 L75 150 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboLink.svg": svg_shell(
            '  <path d="M89 41 L110 45 L120 130 L160 120 L180 140 L170 180 L200 190 L185 220 L100 215 L70 150 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboGrab.svg": svg_shell(
            '  <path d="M90 80 L130 70 L170 90 L200 130 L195 190 L150 220 L95 210 L70 160 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboCrosshair.svg": svg_shell(
            '  <path d="M118 20 H138 V118 H236 V138 H138 V236 H118 V138 H20 V118 H118 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboMove.svg": svg_shell(
            '  <path d="M128 16 L84 60 H112 V112 H60 V84 L16 128 L60 172 V144 H112 V196 H84 L128 240 '
            'L172 196 H144 V144 H196 V172 L240 128 L196 84 V112 H144 V60 H172 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboResizeEW.svg": svg_shell(
            '  <path d="M20 128 L64 84 V112 H193 V84 L237 128 L193 172 V144 H64 V172 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboResizeNS.svg": svg_shell(
            '  <path d="M128 20 L84 64 H112 V193 H84 L128 237 L172 193 H144 V64 H172 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboResizeNWSE.svg": svg_shell(
            '  <path d="M51.6 51.6 H113.9 L94.1 71.4 L185.3 162.6 L205.1 142.9 V205.1 H142.9 L162.6 185.3 '
            'L71.4 94.1 L51.6 113.9 Z" fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboResizeNESW.svg": svg_shell(
            '  <path d="M51.6 204.4 V142.1 L71.4 161.9 L162.6 70.7 L142.9 50.9 H205.1 V113.2 L185.3 93.3 '
            'L94.1 184.6 L113.9 204.4 Z" fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboResizeSplitEW.svg": svg_shell(
            '  <path d="M20 128 L64 84 V112 H118 V20 H138 V112 H192 V84 L236 128 L192 172 V142 H138 V236 H118 V142 H64 V172 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboResizeSplitNS.svg": svg_shell(
            '  <path d="M128 20 L172 64 H144 V118 H236 V138 H144 V192 H172 L128 236 L84 192 H112 V138 H20 V118 H112 V64 H84 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>'
        ),
        "RoboNotAllowed.svg": svg_shell(
            '  <circle cx="128" cy="128" r="84" fill="none" stroke="#DC2626" stroke-width="16"/>\n'
            '  <line x1="70" y1="186" x2="186" y2="70" stroke="#DC2626" stroke-width="16" stroke-linecap="round"/>'
        ),
        "RoboTextVertical.svg": svg_shell(
            '  <path d="M120 40 H136 V216 H120 Z" fill="#FFFFFF" stroke="#0F172A" stroke-width="8"/>\n'
            '  <path d="M96 48 H160" stroke="#0F172A" stroke-width="10" stroke-linecap="round"/>\n'
            '  <path d="M96 208 H160" stroke="#0F172A" stroke-width="10" stroke-linecap="round"/>'
        ),
        "RoboTextHorizontal.svg": svg_shell(
            '  <path d="M40 120 H216 V136 H40 Z" fill="#FFFFFF" stroke="#0F172A" stroke-width="8"/>\n'
            '  <path d="M48 96 V160" stroke="#0F172A" stroke-width="10" stroke-linecap="round"/>\n'
            '  <path d="M208 96 V160" stroke="#0F172A" stroke-width="10" stroke-linecap="round"/>'
        ),
        "RoboEdit.svg": svg_shell(
            '  <path d="M40 220 L78 202 L212 67 L193 48 L58 182 Z" fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>\n'
            '  <path d="M40 220 L61 210 L50 199 Z" fill="#0F172A"/>'
        ),
        "RoboWait.svg": svg_shell(
            '  <circle cx="128" cy="128" r="100" fill="none" stroke="#8E8E93" stroke-width="18" stroke-opacity="0.35"/>\n'
            '  <circle cx="128" cy="128" r="100" fill="none" stroke="#3B9EFF" stroke-width="18" stroke-linecap="round" '
            'stroke-dasharray="197 704" transform="rotate(-90 128 128)"/>'
        ),
        "RoboWorking.svg": svg_shell(
            '  <g transform="scale(0.55)">\n'
            '    <path d="M70 50 L70 204 L108.5 171 L130.5 220.5 L152.5 209.5 L125 160 L180 160 Z" '
            'fill="#FFFFFF" stroke="#0F172A" stroke-width="8" stroke-linejoin="round"/>\n'
            "  </g>\n"
            '  <circle cx="192" cy="192" r="40" fill="none" stroke="#8E8E93" stroke-width="14" stroke-opacity="0.35"/>\n'
            '  <circle cx="192" cy="192" r="40" fill="none" stroke="#3B9EFF" stroke-width="14" stroke-linecap="round" '
            'stroke-dasharray="70 251" transform="rotate(-90 192 192)"/>'
        ),
    }
    for name, text in svgs.items():
        (masters / name).write_text(text, encoding="utf-8")


def pack_rcur(out_path: Path) -> None:
    meta = {
        "name": "RoboticelaOS Cursors",
        "author": "Roboticela Systems",
        "brandAccent": "#3B9EFF",
        "created": "2026-09-27",
        "cursorCount": len(CURSORS),
        "hasThemes": True,
        "hasAnimations": True,
        "logicalPt": 16,
        "formats": ["SVG", "RCUR"],
    }
    meta_bytes = json.dumps(meta, separators=(",", ":")).encode("utf-8")

    entries = []
    blobs = []
    for name, _sid, hx, hy, kind in CURSORS:
        sid = STATE_IDS[name]
        animated = kind in ("wait", "progress")
        frame_count = 12 if animated else 1
        duration = 83 if animated else 0
        themes = []
        for theme_id, dark in ((0, False), (1, True)):
            frames = []
            for fi in range(frame_count):
                res = []
                for px in SIZES:
                    png = draw_shape(kind, px, dark, fi)
                    res.append((px, png))
                frames.append(res)
            themes.append((theme_id, hx, hy, frame_count, duration, 1 if animated else 0, frames))

        body = bytearray()
        body.append(len(themes))
        for theme_id, thx, thy, fc, dur, loop, frames in themes:
            body.append(theme_id)
            body += struct.pack("<hh", int(thx * 32 / 256), int(thy * 32 / 256))  # hotspot at 32px logical
            body.append(fc)
            body += struct.pack("<H", dur)
            body.append(loop)
            for res_list in frames:
                body.append(len(res_list))
                for px, png in res_list:
                    body += struct.pack("<HI", px, len(png))
                    body += png
        blobs.append(bytes(body))
        entries.append((sid, name, len(blobs[-1])))

    # Header + meta + table + bodies
    flags = 0x03  # animations + dark
    header = b"RCUR" + bytes((1, 0, flags, 0))
    meta_block = struct.pack("<I", len(meta_bytes)) + meta_bytes
    table = bytearray()
    table += struct.pack("<H", len(entries))
    # offsets relative to start of file; compute after table size known
    table_size = 2 + sum(1 + 1 + len(n.encode()) + 4 + 4 for _, n, _ in entries)
    offset = len(header) + len(meta_block) + table_size
    for sid, name, length in entries:
        nb = name.encode("utf-8")
        table += struct.pack("<BB", sid, len(nb)) + nb
        table += struct.pack("<II", offset, length)
        offset += length

    out_path.write_bytes(header + meta_block + bytes(table) + b"".join(blobs))
    print(f"wrote {out_path} ({out_path.stat().st_size} bytes, {len(entries)} cursors)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=ROOT_DEFAULT)
    args = ap.parse_args()
    root: Path = args.root
    root.mkdir(parents=True, exist_ok=True)
    write_svg_masters(root / "Masters")
    pack_rcur(root / "roboticela.rcur")
    print(f"masters -> {root / 'Masters'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
