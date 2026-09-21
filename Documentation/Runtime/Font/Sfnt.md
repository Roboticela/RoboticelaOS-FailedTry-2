# SFNT Container Parser (Goal 5.2)

Parses the **SFNT container only** — header, table directory, raw table views,
and checksum helpers. Does **not** interpret `cmap`, `head`, `glyf`, CFF, GSUB, etc.

## Binary layout

```text
Offset Subtable (12 bytes, big-endian)
  sfntVersion   uint32   0x00010000 | 'true' | 'typ1' | 'OTTO'
  numTables     uint16
  searchRange   uint16   (stored; consistency checked optionally)
  entrySelector uint16
  rangeShift    uint16

Table Record × numTables (16 bytes each)
  tag           uint32   four-char code
  checkSum      uint32
  offset        uint32   absolute in the font / collection file
  length        uint32   unpadded byte length
```

TrueType Collections (`ttcf`) select a face via `face_index`; member table
offsets remain **file-absolute**.

## API flow

```text
RoboFontSource
      ↓
FontSfntParse / FontSfntFuzzParse
      ↓
FontSfnt  (retains source)
      ↓
has_table / find_table / table_bytes / table_at / table_count
```

## Checksums

`FontSfntChecksum`: sum of big-endian `uint32` words over table bytes,
with trailing bytes logically zero-padded to a multiple of four.

Optional `RoboFontSfntFlagsVerifyChecksums` compares directory checksums.
**Not** implemented: `head.checkSumAdjustment` (belongs with semantic `head` parse).

## Validation / security

- Bounds-checked BE reader (`RoboFontBinary.h`)
- Caps: max tables 4095, max source 64 MiB
- Reject: unsupported signatures, truncated header/directory, OOB tables,
  offset+length overflow, unaligned offsets, duplicate tags, **overlapping**
  table regions, tables overlapping this face’s header/directory
- Overlaps are **rejected** (safest for a security-critical parser)
- Prefer views into the source — no full table copies

## Ownership / lifetime

```text
Font Source  ──must outlive──►  RoboSfnt  ──must outlive──►  table spans
```

`FontSfnt` retains the source. Spans are valid only while the SFNT
object (and thus the source) is alive.

## Thread safety

No mutable globals. After successful parse, read-only inspection of one SFNT
object is safe from multiple threads. Concurrent `retain`/`release` still needs
external synchronization (non-atomic refs, same as Goal 5.1). No font cache
in this goal.

## Errors

Uses `Result`: `RoboParseError`, `RoboNotSupported`, `RoboNotFound`,
`RoboInvalidArgument`, `RoboOutOfMemory`.

## Fuzzing

`FontSfntFuzzParse(data, size)` — deterministic, no crash on malformed
input. Suitable as a libFuzzer / AFL entry later.

## Chromium independence

Compile/link against Roboticela Runtime only. `chromium/` is reference material,
never a dependency.
