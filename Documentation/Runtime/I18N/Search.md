# Locale-Aware Search & Matching (Goal 4.24)

Foundation for **locale-aware text searching and matching**. Not sorting,
transliteration, regex, or full-text indexing.

```text
haystack / needle (UTF-8, logical order)
           ↓
    RoboI18N searcher   (reusable context)
           ↓
  equal | compare | contains | starts_with | ends_with | find | find_all
           ↓
  hit ranges → UTF-8 byte offsets into the original haystack
```

## Distinct layers (do not conflate)

| Layer | What it answers | Where |
|---|---|---|
| **Byte / code-unit compare** | Identical UTF-8 octets | `memcmp` / `SEARCH_EXACT` |
| **Code-point order** | Same Unicode scalars | `SEARCH_CODEPOINT` |
| **Normalization** | Canonical (or compatibility) equivalence | `RoboUnicodeNorm` via `SEARCH_CANONICAL*` |
| **Case folding** | Full Unicode CaseFolding C+F | `RoboUnicodeCase` via `SEARCH_CASEFOLD*` |
| **Collation** | Locale-sensitive equality / order (UCA) | `RoboCollator` via `SEARCH_COLLATION` |
| **Search matching** | Substring / prefix / suffix + source ranges | **this module** |
| **Transliteration** | Script/script mapping | Goal **4.25** — not used here |
| **Locale tag matching** | BCP 47 tag equality | `I18nMatch` in Locale |

There is **no** single ambiguous “fuzzy” compare.

## Matching modes

| Mode | Behavior |
|---|---|
| `RoboI18nSearchExact` | Valid UTF-8 byte-for-byte |
| `RoboI18nSearchCodepoint` | STRICT UTF-8 → scalar sequence equality |
| `RoboI18nSearchCanonical` | NFD (or NFKD if `compatibility`) then compare |
| `RoboI18nSearchCasefold` | Full Unicode case fold (not locale lowercasing) |
| `RoboI18nSearchCanonicalCasefold` | Normalize then case-fold |
| `RoboI18nSearchCollation` | Owned `RoboCollator` at configured strength |

### Normalization

- Default path uses **canonical** forms (NFD). Set `compatibility` for NFKD.
- Compatibility is **never** applied silently.
- Equality/search may transform windows for comparison; reported hits always map
  back to **original** haystack UTF-8 byte offsets.
- Pathological expansion is bounded by `RoboI18nSearchInputMax` and internal
  CP buffers — oversized input returns `RoboInvalidArgument`.

### Case folding

- Uses Goal 3.x Unicode CaseFolding (**C+F**), including multi-code-point folds
  (e.g. German ß → ss, Greek Σ/σ/ς).
- **Not** a substitute: locale-specific lowercasing (Turkish ı/I) belongs in
  collation or a future locale-case API — use `SEARCH_COLLATION` when that
  behavior is required.

### Accent / diacritic-insensitive

- Option `ignore_diacritics` (non-COLLATION modes): NFD/NFKD, then drop
  `General_Category=Mn` (nonspacing marks).
- Does **not** imply NFKC unless `compatibility` is set.
- For COLLATION, use **PRIMARY** strength instead (`ignore_diacritics` ignored).

### Collation equality vs search

Collation equality means “equal under UCA strength,” **not** byte or code-point
equality. Substring search in COLLATION mode compares candidate windows with the
owned collator; it does not build a full-text index.

## Operations

| API | Role |
|---|---|
| `I18nSearchEqual` / `compare` | Whole-string under mode |
| `contains` / `starts_with` / `ends_with` | Boolean probes |
| `find` / `iter_next` / `find_all` | Ranges into the haystack |

Empty needle matches at offset 0 (zero-width). Iteration advances past the
previous end; empty needles advance one grapheme (or code point) to avoid loops.

## Match ranges & indexing

All offsets are **UTF-8 byte offsets** into the caller haystack, half-open
`[start, end)`.

| Field | Meaning |
|---|---|
| `start` / `end` | Semantic match span in original bytes |
| `grapheme_start` / `grapheme_end` | Same span expanded to extended grapheme clusters when `grapheme_safe` |

Distinguish:

```text
byte range      — UTF-8 octets (this API)
code-point range — Unicode scalars
grapheme range  — extended grapheme clusters (Goal 3.6)
glyph range     — shaped glyphs (RoboText later; out of scope)
```

Search operates on **logical** Unicode text order (not visual/bidi display order).

## Grapheme safety

Default `grapheme_safe = 1`: reported ranges expand to cluster boundaries so UI
selection does not split combining marks, emoji ZWJ sequences, modifiers, or
regional-indicator flags. Set `0` only when code-point-level spans are intentional.

## Locale behavior

- Uses existing `RoboLocale` — no second locale system.
- Locale may be `NULL`; COLLATION without a locale uses und/root collator.
- Unicode-only modes (exact / codepoint / canonical / casefold) do not require
  a locale and do not parse tags or load CLDR on each call.
- Reusable `I18nSearcher` owns collation state when needed so repeated
  searches do not re-parse locales or rebuild collators.

## Ownership / lifetime / threads

- Searcher: caller-owned; destroy with `I18nSearcherDestroy`.
- Owned collator (if any) is destroyed with the searcher.
- Iterators borrow haystack/needle pointers — must remain valid for the iter.
- Not synchronized: one owner thread, or external locking.

## Security & limits

| Limit | Value |
|---|---|
| Input max (haystack / needle) | `RoboI18nSearchInputMax` (65536) |
| `find_all` cap | `RoboI18nSearchFindAllMax` (256) |

Malformed UTF-8 → error (no replacement, no OOB). Adversarial combining / emoji
sequences must not crash; oversized buffers are rejected.

## Performance

- Prefer one searcher for many queries with the same mode/locale.
- EXACT uses a byte scan; transformed modes decode/compare windows without
  allocating full temporary strings when avoidable.
- Do not route every search through collation when EXACT/CASEFOLD/CANONICAL
  suffice.

## API

`RoboI18NSearch.h` — `robo_i18n_searcher_*`, `robo_i18n_search_*`,
`I18nSearchHit`, `I18nSearchIter`.

## What this is not

- Transliteration (Goal 4.25)
- Regular expressions
- Full-text / inverted-index search
- Glyph or visual-order matching
- Locale identifier matching (`I18nMatch`)
