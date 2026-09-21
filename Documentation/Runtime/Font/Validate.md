# Font Security & Validation (Goal 5.20)

Every font byte entering RoboFont is **untrusted input**.

This goal adds a central validation orchestration layer. It does **not** replace
parser-local bounds checks — those remain mandatory and cannot be disabled.

```text
Font Source
   ↓
Stage 1  Container / SFNT safety
   ↓
Stage 2  Required metadata
   ↓
Stage 3  Feature tables
   ↓
Stage 4  Glyph / color / variation probes
   ↓
Validated use via Loader → Face
```

> Chromium’s OpenType Sanitizer (OTS) is a **reference** for threat model only.
> RoboFont does **not** depend on OTS, Chromium, Skia, FreeType, or Blink.

## Trust boundary

Fonts may arrive from filesystem, memory, assets, downloads, collections,
variable fonts, or color fonts. None are trusted a priori.

Invalid fonts must not become trusted `RoboFontFace` objects through the loader
path when validation reports fatal issues.

## Levels

| Level | Loader policy | Behavior |
|---|---|---|
| `NONE` | `TRUSTED` | Parser bounds only; skip optional probes |
| `BASIC` | `NORMAL` | SFNT structure + required metadata consistency |
| `STRICT` | `STRICT` | + checksum diagnostics (warn) + TT head verify |
| `PARANOID` | `PARANOID` | + color/variation/CFF/OT presence probes; checksum reject |

Bounds checking is **never** turned off.

## Resource limits

`RoboFontValidationLimits` centralizes caps (source size, tables, glyphs,
variation axes/instances, color palettes/layers/paint depth, SVG/bitmap
payloads, recursion, allocation, work budget).

Applications may request **lower** limits. Values are clamped to hard ceilings
(`ROBO_FONT_VALIDATION_HARD_MAX_*`). Mandatory memory-safety restrictions
cannot be disabled.

Per-parser macros (`ROBO_FONT_COLOR_MAX_*`, `ROBO_FONT_VAR_MAX_*`, …) remain
enforced inside those modules.

## Checksums

| Policy | Effect |
|---|---|
| `ACCEPT` | Do not probe / ignore mismatches |
| `WARN` | Record `CHECKSUM_MISMATCH` as warning |
| `REJECT` | Fatal; also enables SFNT parse-time verify |

Malformed checksums are not assumed malicious; STRICT defaults to WARN.

## Diagnostics

Structured issues include:

* issue code + severity + stage
* table tag / byte offset / glyph ID / axis tag when applicable
* recoverable flag
* fixed message (no raw pointers)

Deterministic for the same input + policy.

## Loader integration

`ensure_face_loaded` calls `FontValidateForLoad` before publishing a
face. Fatal validation → load failure (non-trusted object).

## Fuzzing & sanitizers

* `FontValidateFuzzParse` plus existing per-parser fuzz entries
* Host tests run under ASan/UBSan via `Build.ps1 -Sanitize`
* Regression corpus lives in `SuiteRoboFontValidate.c` (permanent malformed blobs)

## Performance

BASIC validation is designed for load-path use. PARANOID adds feature probes.
Bench: `font.validate.bench`.

Avoid re-validating immutable sources unless policy/limits change (caller-owned
result objects; no global mutable validator state).

## API entry points

* `robo_font_validation_policy_*` / `limits_*`
* `FontValidateSource` / `validate_sfnt` / `validate_face`
* `FontValidateForLoad`
* `FontValidateFuzzParse`

See also: `Loading.md`, `Sfnt.md`, `Color.md`, `Variation.md`.
