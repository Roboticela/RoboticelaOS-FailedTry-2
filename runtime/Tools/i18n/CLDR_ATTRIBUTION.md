# CLDR attribution (Goal 4.2)

Roboticela Runtime embeds a **selected subset** of Unicode CLDR data,
generated offline into committed C tables (`RoboCldrData.h` /
`robo_cldr_data.c`).

## Source

| Field | Value |
|---|---|
| Project | Unicode Common Locale Data Repository (CLDR) |
| Release | **46** (JSON package **46.0.0**) |
| Unicode version in CLDR metadata | **16.0.0** |
| License | Unicode License v3 (see https://www.unicode.org/license.txt) |
| Distribution | https://github.com/unicode-org/cldr-json |

## Categories imported

### Goal 4.2
- likely subtags
- numbering systems (+ per-locale default/native ids)
- calendar preference data (identifiers only)
- measurement system preferences
- parent locale overrides

### Goal 4.3
- number symbols (decimal, group, plus, minus, nan, infinity)
- standard decimal patterns
- minimum grouping digits
- primary/secondary grouping sizes derived from patterns

## Not imported here

Currency/percent/compact patterns, plural rules, collation, display names, units,
time zones, and message catalogs remain for later goals.

## Policy

- Do not invent locale data.
- Do not mix CLDR releases in one generation.
- Normal `Build.ps1` does not download CLDR; use `Fetch-Cldr.ps1` then
  `generate_cldr.py` explicitly.
- Unicode UCD / Emoji versions are independent pins under `Tools/ucd/`.
