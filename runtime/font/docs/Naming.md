# Font Naming & Metadata (Goal 5.9)

Safe OpenType/TrueType `name` table decoding into Unicode strings, locale
association, deterministic localized lookup, and a normalized metadata snapshot.

```text
RoboFontSource
      ↓
RoboSfntParser
      ↓
name table  (structural parse: Goal 5.3)
      ↓
RoboFontNameInfo  (decode + locale + lookup)
      ↓
RoboFontMetadata  (normalized fields)
```

No second SFNT parser. Metadata is inert Unicode text — no URL fetch, HTML,
JavaScript, or shell interpretation.

## Table structure

| Field | Role |
|---|---|
| `format` | 0 = name records only; 1 = + language-tag names |
| `count` | number of name records |
| `stringOffset` | offset to string storage from table start |
| `nameRecord[]` | platform / encoding / language / name ID / length / offset |
| langTagCount / langTagRecord (format 1) | BCP-47 tags for langID ≥ 0x8000 |

All offsets are checked against the actual `name` table length. Record and
string budgets use the Goal 5.3 / 5.9 limits (`ROBO_FONT_NAME_MAX_*`).

## Name records

Each decoded entry (`robo_font_name_entry_t`) stores:

- platform / encoding / language / name ID
- decode status (`PRESENT` / `UNSUPPORTED` / `MALFORMED`)
- optional BCP-47 `locale_tag` when mappable
- owned UTF-8 string (no raw table pointers exposed)

Unknown name IDs remain accessible numerically and are retained up to
`ROBO_FONT_NAME_MAX_UNKNOWN_IDS`.

## Name IDs

Standard IDs 0–25 are named in `RoboFontName.h` / `RoboFontTt.h`
(copyright, family, subfamily, unique ID, full name, version, PostScript,
trademark, manufacturer, designer, description, URLs, license, typographic
family/subfamily, WWS, palette / variations PS prefix, …).

## Platform / encoding decode

| Platform | Encoding | Decode |
|---|---|---|
| Unicode (0) | Unicode encodings | UTF-16BE via `robo_unicode_utf16_to_utf8` |
| Windows (3) | BMP / full Unicode | UTF-16BE (surrogates, combining marks) |
| Macintosh (1) | MacRoman (0) | explicit MacRoman → Unicode map |
| other | — | `UNSUPPORTED` (not treated as UTF-8) |

Truncated code units and invalid surrogates → `MALFORMED`, not crash.

## Locale mapping

- Windows language IDs → common BCP-47 tags (e.g. 0x0409 → `en-US`)
- Macintosh language IDs → tags where known
- Format-1 lang tags → parsed as BCP-47 via RoboI18N
- No invented locale when the record does not provide one (`has_locale == 0`)

## Localized lookup

`robo_font_name_info_lookup(name_id, preferred_locale)`:

1. exact locale match
2. language-only match
3. platform-preferred (Windows Unicode `en-US`, any Win Unicode, Unicode
   platform, Mac Roman)
4. first remaining `PRESENT` entry for that name ID (stable lowest index)

Among equal scores, lowest table index wins (deterministic duplicates).

## Canonical metadata

`robo_font_metadata_t` exposes named fields. Each field has:

| `avail` | Meaning |
|---|---|
| `PRESENT` | font-authored for that name ID |
| `FALLBACK` | filled by preferred-family/subfamily policy from another ID |
| `UNAVAILABLE` | no usable record |
| `UNSUPPORTED` / `MALFORMED` | present but not safely usable |

Fields: legacy family/subfamily, full name, PostScript name, version
(+ optional parsed major/minor/revision), typographic family/subfamily,
WWS family/subfamily, manufacturer, designer, description, copyright,
trademark, license description, vendor/designer/license URLs.

PostScript name is never rewritten; `postscript_name_structurally_ok` reports
basic structural validity for diagnostics.

## Family / subfamily policy

Keep three layers distinct in the snapshot:

| Layer | Name IDs |
|---|---|
| Legacy | 1 / 2 |
| Typographic | 16 / 17 |
| WWS | 21 / 22 |

Preferred helpers for later selection (5.10/5.11):

- family: **16 → 21 → 1**
- subfamily: **17 → 22 → 2**

Weight/style inference from subfamily words is **not** Goal 5.9.

## Caching & thread safety

- Decoded at `robo_font_name_info_create` / face `parse_metadata`
- Face-local ownership (`face->name_info`); immutable after create
- Concurrent readers OK under the face lifetime model
- No unsynchronized lazy mutation; no global cache

## Security limits

| Limit | Purpose |
|---|---|
| max name records | bound table walk |
| max string length | bound single decode |
| max total decoded size | bound arena growth |
| max unknown IDs / lang tags | bound retention |

Malformed fonts must not OOB, overflow, UAF, or allocate without bound.

## Fuzz entry

`robo_font_name_fuzz_parse(data, size)` — arbitrary bytes; never crashes.

## API

| Call | Role |
|---|---|
| `robo_font_name_info_create` / `_from_span` | Build info + metadata |
| `robo_font_name_info_lookup` | Localized name ID query |
| `robo_font_name_info_metadata` | Snapshot |
| `robo_font_metadata_preferred_family` / `_subfamily` | Selection policy |
| `robo_font_face_parse_metadata` / `face_name_info` / `face_metadata` | Face |
| `robo_font_name_fuzz_parse` | Fuzz |

Structural low-level parse remains `robo_font_name_parse` (Goal 5.3).

## Chromium independence

No Chromium / Blink / Skia includes or link deps.
