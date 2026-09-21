# Font Style / Weight / Stretch Classification (Goal 5.10)

Normalized classification of a font face for later selection (Goal 5.11+).
**This goal does not perform font matching or selection.**

```text
OS/2 / head / post / STAT(tags) / name layers
        ↓
RoboFontClassification  (raw + normalized)
        ↓
style_desc (weight / stretch / style)
```

## Raw vs normalized

| Layer | Contents |
|---|---|
| **Font-authored raw** | `usWeightClass`, `usWidthClass`, `fsSelection`, `macStyle`, `post.italicAngle`, STAT axis tags, name-layer presence flags |
| **Normalized** | weight 1..1000, weight category, stretch %, width class 1..9, style normal/italic/oblique |

Raw fields are never overwritten by normalized values.

## Weight

Primary source: `OS/2.usWeightClass` when in **1..1000**.

Named categories (range buckets; numeric weight preserved):

| Category | Typical / range |
|---|---|
| Thin | ≤149 |
| Extra-light | 150–249 |
| Light | 250–349 |
| Normal | 350–449 |
| Medium | 450–549 |
| Semi-bold | 550–649 |
| Bold | 650–749 |
| Extra-bold | 750–849 |
| Black | 850–949 |
| Extra-black | 950–1000 |

### Precedence

1. Valid `usWeightClass` → **OS/2** (`PRESENT`)
2. Else `fsSelection` BOLD → **700** (`FALLBACK`)
3. Else `head.macStyle` bold → **700** (`FALLBACK`)
4. Else **400** (`DEFAULT`)

A valid `usWeightClass` is **never** overridden by the bold bit. If both disagree (e.g. weight 350 + BOLD), weight stays 350 and `conflict_bold_bit_vs_weight` is set.

Invalid `usWeightClass` is kept in raw; normalized weight falls through the chain (or defaults to 400 with `INVALID` avail).

## Stretch / width

Primary source: `OS/2.usWidthClass` **1..9**.

| Class | Name | Stretch % |
|---|---|---|
| 1 | Ultra-condensed | 50 |
| 2 | Extra-condensed | 62 |
| 3 | Condensed | 75 |
| 4 | Semi-condensed | 87 |
| 5 | Normal | 100 |
| 6 | Semi-expanded | 112 |
| 7 | Expanded | 125 |
| 8 | Extra-expanded | 150 |
| 9 | Ultra-expanded | 200 |

Missing/invalid → width 5 / stretch 100% (`DEFAULT` / `INVALID` as appropriate). Raw invalid values preserved.

## Style

| Value | Meaning |
|---|---|
| `NORMAL` | Upright |
| `ITALIC` | True italic design |
| `OBLIQUE` | Slanted / algorithmic oblique |
| `UNKNOWN` | Reserved; not emitted by current rules |

### Precedence

1. `fsSelection` OBLIQUE (bit 9) → **oblique**
2. Else `fsSelection` ITALIC (bit 0) → **italic**
3. Else `head.macStyle` italic → **italic** (`FALLBACK`)
4. Else `post.italicAngle != 0` → **oblique** (`FALLBACK`); angle preserved
5. Else **normal**

If ITALIC and OBLIQUE are both set, **OBLIQUE wins**; `both_italic_and_oblique` is set.

`REGULAR` bit does not clear slope bits. If REGULAR and slope bits coexist, slope wins and `conflict_regular_and_slope` is set.

### Angle

`post.italicAngle` (Fixed 16.16) is copied into classification when present.
Millidegrees ≈ `Fixed * 1000 / 65536`. No invented precision when absent.

## fsSelection bits

**Influence normalization:** ITALIC (0), BOLD (5), REGULAR (6, diagnostic), WWS (8, naming hint), OBLIQUE (9).

**Preserved only:** UNDERSCORE, NEGATIVE, OUTLINED, STRIKEOUT, USE_TYPO_METRICS.

## WWS

Kept distinct:

- legacy family/subfamily (name IDs 1/2)
- typographic (16/17)
- WWS names (21/22)
- `fsSelection` WWS bit
- normalized weight / stretch / style

No collapse into one string. No weight/style inference from subfamily words.

## STAT / variable preparation

Structural STAT axis tags (`wght`, `wdth`, `ital`, `slnt`, `opsz`) are retained when present.

**Goal 5.10 does not** parse axis value tables or override OS/2 numerics from STAT.
Full variable fonts: Goal **5.18**.

## API

| Call | Role |
|---|---|
| `FontClassify` | Pure raw → normalized |
| `FontClassificationInfoCreate` | From TT + optional STAT + metadata |
| `FontFaceParseClassification` / `face_classification` | Face path |
| `FontClassificationFuzzParse` | Malformed entry |
| `FontWeightCategory` / `stretch_from_width_class` | Helpers |

## Lifetime

Classification snapshot is value-owned (no dangling table pointers). Immutable after create. Concurrent readers OK under face lifetime.

## Chromium independence

No Chromium / Blink / Skia includes or link deps.
