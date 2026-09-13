# Transliteration Foundation (Goal 4.25)

Standards-based, data-driven conversion between writing systems/scripts.

```text
source Unicode text (UTF-8, logical order)
        ↓
  compiled CLDR rule-set (offline)
        ↓
target Unicode text (UTF-8)
```

## Distinct from

| Concept | Role |
|---|---|
| **Transliteration** | Script/script orthographic mapping (this module) |
| **Translation** | Meaning across languages |
| **Transcription / phonetics** | Pronunciation guides |
| **Normalization** | NFC/NFD/NFKC/NFKD (may appear as pipeline stages) |
| **Case folding** | Unicode CaseFolding C+F |
| **Search matching** | Goal 4.24 — must **not** auto-enable transliteration |
| **Collation** | UCA sorting / equality (Goal 4.9) |
| **Language detection** | Out of scope |

Latin is **not** assumed as the only source or destination.

## CLDR data pipeline

1. `Fetch-Transforms.ps1` — curated `common/transforms/*.xml` from CLDR **46**
2. `generate_translit.py` — validates a documented LDML subset, expands UnicodeSets
   offline, resolves `::ID;` chains, emits `RoboTranslitData.h` + `robo_translit_data.c`
3. Runtime interprets compact tables only — **no** XML/rule parsing at runtime

Unsupported rule features (cursor `|`, segments `$1`, most function transforms) are
skipped at generate time with warnings; architecture remains extensible.

Attribution: Unicode CLDR (see `Tools/i18n/CLDR_ATTRIBUTION.md`).

## Matching modes / direction

| Mode | Meaning |
|---|---|
| Forward | Source→target as published for that ID |
| Reverse | Inverse mapping from the same bidirectional file when available |
| Bidirectional metadata | `reversible` flag from CLDR `direction="both"` — **not** a guarantee of round-trip fidelity |

Many-to-one mappings lose information; do not claim inherent reversibility.

## Normalization interaction

Pipeline stages are explicit in generated data:

- `NFD` / `NFC` / `NFKD` / `NFKC` — only when the CLDR transform requests them
- `LOWER` — full Unicode lowercase (not locale Turkish specials)
- `HANGUL_DECOMP` — algorithmic Hangul syllable → jamo before jamo rules
- `RULES` / `NULL` — conversion passes

User text is **not** silently normalized outside those stages.

## Rule engine (minimum)

Ordered longest-key match with optional before/after UnicodeSet context.
Variables and sets are expanded at generate time into range tables + CP pools.

## API

`RoboI18NTranslit.h`

- `robo_i18n_translit_find_id` / `id_info` / `id_count`
- `robo_i18n_transliterator_create` / `create_id` / `destroy`
- `robo_i18n_transliterate` (+ optional coarse source↔output map)
- `stream_reset` / `stream_feed` / `stream_finish` (bounded hold buffer)

Callers **must** select a rule-set ID. Missing source/target is not inferred from text.
`RoboLocale` may be passed for future language-specific selection; currently unused.

### Search integration

Goal 4.24 may later *optionally* call this API. Transliteration is **never** implied
by search modes alone.

## Ownership / threads

Transliterator: caller-owned. Generated tables: immutable process data.
Not synchronized — one owner thread or external locking.

## Security limits

| Limit | Macro |
|---|---|
| Input max | `ROBO_I18N_TRANSLIT_INPUT_MAX` (65536) |
| Output max | `ROBO_I18N_TRANSLIT_OUTPUT_MAX` (262144) |
| Stream lookahead | `ROBO_I18N_TRANSLIT_STREAM_LOOKAHEAD_MAX` (16 CPs) |
| Step budget | `ROBO_I18N_TRANSLIT_STEP_MAX` |

Malformed UTF-8 → `ROBO_PARSE_ERROR`. No unbounded expansion.

## Curated IDs (initial)

Cyrillic↔Latin, Greek↔Latin, Arabic↔Latin, Hebrew↔Latin, Russian/Ukrainian BGN,
Latin-ASCII, Devanagari/Bengali→Latin (InterIndic chain), Katakana/Hiragana→Latin,
Hangul→Latin. Additional pairs are added by extending the generator allowlist.
