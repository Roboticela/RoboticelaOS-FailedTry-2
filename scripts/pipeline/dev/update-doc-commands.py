#!/usr/bin/env python3
"""Rewrite documentation references to the scripts the unified pipeline replaced.

Line-oriented on purpose: the docs have mixed line endings and Windows paths, so
matching whole lines (after stripping the ending, which is preserved) avoids both
CRLF and backslash-escaping surprises.
"""

from __future__ import annotations

import pathlib
import sys

# Whole-line replacements. Longest/most specific forms first is unnecessary here
# because matching is exact per line.
REPLACEMENTS: dict[str, str] = {
    r".\scripts\test-qemu-display.ps1          # rebuild the image and run":
        r"scripts\robo test-qemu-display            # rebuild the image and run",
    r".\scripts\test-qemu-display.ps1 -SkipBuild":
        r"scripts\robo test-qemu-display --skip-build",

    r".\scripts\test-host-display.ps1 -Visible   # ROBO_WINDOW_SMOKE=1":
        r"scripts\robo test-display --visible        # plus ROBO_WINDOW_SMOKE=1",
    r".\scripts\test-host-display.ps1 -Visible":
        r"scripts\robo test-display --visible",
    r".\scripts\test-host-display.ps1":
        r"scripts\robo test-display",

    r".\scripts\test-qemu-display.ps1":
        r"scripts\robo test-qemu-display",

    r".\scripts\test-phase7.ps1 -SkipQemu":
        r"scripts\robo test-all --skip-qemu",
    r".\scripts\test-phase7.ps1":
        r"scripts\robo test-all",
    r".\scripts\test-phase8.ps1":
        r"scripts\robo test-html",
    r".\scripts\run-bootloader-qemu.ps1":
        r"scripts\robo run-runtime-qemu",
    r".\scripts\test-bootloader-qemu.ps1":
        r"scripts\robo test-qemu-kernel",
    r".\scripts\create-robofs-image.ps1":
        r"scripts\robo create-robofs-image",

    "# One-shot Phase 7 gate (unit + host; QEMU optional)":
        "# One-shot gate (unit + host; QEMU optional)",

    "| Real QEMU smoke | (script) | `scripts/test-qemu-display.ps1` |":
        "| Real QEMU smoke | `robo test-qemu-display` | `scripts/pipeline/commands.test.ps1` |",
    "| Host smoke | (script) | `scripts/test-host-display.ps1` |":
        "| Host smoke | `robo test-display` | `scripts/pipeline/commands.test.ps1` |",
    "| Full Phase 7 gate | (script) | `scripts/test-phase7.ps1` |":
        "| Full Phase 7 gate | `robo test-all` | `scripts/pipeline/commands.test.ps1` |",
}

INLINE = (
    (r"Interactive visual check: `.\scripts\run-bootloader-qemu.ps1` (GTK display).",
     "Interactive visual check: `scripts\\robo run-runtime-qemu` (GTK display), or\n"
     "`scripts\\robo run-qemu` to build everything first."),
    (r"Or one-shot smoke script (build + host filters + optional visible window)",
     "Or the pipeline's display command (build + host filters + optional visible window)"),
)


def fix(path: pathlib.Path) -> int:
    if not path.is_file():
        return 0
    original = path.read_text(encoding="utf-8")
    text = original
    for old, new in INLINE:
        text = text.replace(old, new)

    lines = text.splitlines(keepends=True)
    changed = 0
    for i, line in enumerate(lines):
        body = line.rstrip("\r\n")
        end = line[len(body):]
        key = body.strip()
        if key in REPLACEMENTS:
            indent = body[: len(body) - len(body.lstrip())]
            lines[i] = indent + REPLACEMENTS[key] + end
            changed += 1
    text = "".join(lines)
    if text != original:
        path.write_text(text, encoding="utf-8")
    return changed


def main(argv: list[str]) -> int:
    total = 0
    for arg in argv:
        p = pathlib.Path(arg)
        n = fix(p)
        total += n
        if n:
            print(f"{p}: {n} line(s) updated")
    print(f"{total} line(s) updated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
