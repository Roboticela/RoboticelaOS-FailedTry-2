# Roboticela Runtime Progress

## Goals 0.1–1.5 — RoboBase foundation

**Status:** complete

## Phase 2 — RoboMem (Goals 2.1–2.5)

**Status:** complete

## Goal 3.1 — RoboArray

**Status:** complete

## Goal 3.2 — RoboString

**Status:** complete

## Goal 3.3 — RoboUnicode encoding

**Status:** complete

## Goal 3.4 — Unicode Character Database properties

**Status:** complete

## Goal 3.5 — Unicode Normalization

**Status:** complete

## Goal 3.6 — Extended Grapheme Cluster segmentation

**Status:** complete

## Goal 3.7 — Unicode case mapping and case folding

**Status:** complete

**Done:**

- Official Unicode **16.0.0** case data pipeline (`Tools/ucd/generate_case.py`)
- Simple 1:1 mappings from `UnicodeData.txt`; full mappings from unconditional `SpecialCasing.txt`
- Full case fold (C+F) and simple fold (C+S) from `CaseFolding.txt` (status T omitted)
- Final_Sigma context for Greek Σ in string-level full lowercase
- Public API `RoboUnicodeCase.h` — simple/full upper/lower/title/fold, UTF-8 ops, casefold compare
- Multi-code-point expansions (ß→SS, ﬁ→FI, İ→i+̇)
- Conformance fixtures from CaseFolding + SpecialCasing (1,918 cases)
- Samples: Latin, Greek, Cyrillic, Armenian, Georgian, Turkish İ, sharp S, ligatures, combining, Deseret
- Debug / Release / ASan+UBSan: **116/116** tests pass (pre–Goal 3.8 baseline)

**Definition of done:**

- [x] Simple vs full case mapping distinguished
- [x] Full case folding for comparisons (binary equality after fold)
- [x] Multi-code-point mappings supported
- [x] UCD-backed tables (not ASCII heuristics as the primary path)
- [x] Locale limitations documented (no Turkish T / Lithuanian Soft_Dotted)
- [x] Normalization kept separate
- [x] Existing suites still pass

## Goal 3.8 — Unicode Word Segmentation (UAX #29)

**Status:** complete

**Done:**

- Official Unicode **16.0.0** Word_Break tables (`Tools/ucd/generate_word.py`)
- Rules WB1–WB16 + WB999 with WB4 ignore (Extend|Format|ZWJ); ExtPict for WB3c
- Public API `RoboUnicodeWord.h` — `word_break`, streaming UTF-8 iterator, next/prev/count
- Word segments as half-open UTF-8 byte ranges `[word_start, word_end)`
- Official `WordBreakTest.txt` conformance: **1,826 / 1,826**
- Unit coverage: Latin, Greek, Cyrillic, Arabic/Persian/Urdu, Hebrew, Devanagari,
  Bengali, Thai, CJK, Korean, emoji/flags, numbers, punctuation, invalid UTF-8
- Documented vs dictionary/linguistic segmentation; grapheme API unchanged
- Debug / Release / ASan+UBSan: **127/127** tests pass

**Definition of done:**

- [x] UAX #29 word boundaries (not space-split heuristics)
- [x] Official WordBreakTest conformance
- [x] Streaming UTF-8 iterator with byte-range segments
- [x] Bytes / code points / graphemes / words distinguished in API + docs
- [x] Invalid UTF-8 policy matches RoboUnicode
- [x] No sentence/line/bidi/shaping/dictionary work
- [x] Existing suites still pass

## Goal 3.9 — Unicode Sentence Segmentation (UAX #29)

**Status:** complete

**Done:**

- Official Unicode **16.0.0** Sentence_Break tables (`Tools/ucd/generate_sentence.py`)
- Rules SB1–SB11 + SB998; Extend|Format ignored except after sot/ParaSep
- Public API `RoboUnicodeSentence.h` — `sentence_break`, streaming UTF-8 iterator, next/prev/count
- Sentence segments as half-open UTF-8 byte ranges `[sentence_start, sentence_end)`
- Official `SentenceBreakTest.txt` conformance: **512 / 512**
- Unit coverage: Latin/European, Arabic/Persian/Urdu, Hebrew, Indic, Thai, CJK, Korean,
  emoji, quotes/parens, numbers/abbreviations, paragraph separators, invalid UTF-8
- Documented vs NLP sentence detection; word/grapheme APIs unchanged
- Test registry capacity raised to 256
- Debug / Release / ASan+UBSan: **138/138** tests pass

**Definition of done:**

- [x] UAX #29 sentence boundaries (not `.`/`!`/`?` heuristics)
- [x] Official SentenceBreakTest conformance
- [x] Streaming UTF-8 iterator with byte-range segments
- [x] Bytes / code points / graphemes / words / sentences distinguished
- [x] Invalid UTF-8 policy matches RoboUnicode
- [x] No line/bidi/shaping/NLP/dictionary work
- [x] Existing suites still pass

## Goal 3.10 — Unicode Line Breaking (UAX #14)

**Status:** complete

**Done:**

- Official Unicode **16.0.0** Line_Break tables (`Tools/ucd/generate_linebreak.py`)
- Rules LB1–LB31 (CM/ZWJ treat-as, numbers, quotes, RI, emoji, Brahmic, Hangul, CJK)
- Public API `RoboUnicodeLine.h` — class lookup, opportunity kinds
  (`PROHIBITED` / `ALLOWED` / `MANDATORY`), streaming UTF-8 iterator
- Line segments as half-open UTF-8 byte ranges with break kind
- Official `LineBreakTest.txt` conformance: **16,672 / 16,672**
- Unit coverage: spaces/CR/LF/CRLF/separators, numbers, CJK, Hangul, scripts,
  emoji/ZWJ/RI, combining, quotes, invalid UTF-8
- Reference oracle `Tools/ucd/lb_ref.py`; independent of word/sentence layers
- Debug / Release / ASan+UBSan: **151/151** tests pass

**Definition of done:**

- [x] UAX #14 opportunities (not ASCII whitespace wrapping)
- [x] Mandatory vs allowed vs prohibited distinguished
- [x] Official LineBreakTest conformance
- [x] Streaming UTF-8 iterator with byte-range segments
- [x] Independent of grapheme/word/sentence semantics
- [x] Invalid UTF-8 policy matches RoboUnicode
- [x] No layout/CSS/hyphenation/bidi/shaping
- [x] Existing suites still pass

## Goal 3.11 — Unicode Bidirectional Algorithm (UAX #9)

**Status:** complete

**Done:**

- Official Unicode **16.0.0** Bidi_Class + bracket tables (`Tools/ucd/generate_bidi.py`)
- Full UBA: P2–P3, X1–X10, W1–W7, N0–N2 (brackets), I1–I2, L1–L2
- Public API `RoboUnicodeBidi.h` — class lookup, paragraph direction (LTR/RTL/AUTO),
  analyze / analyze_utf8, resolve_levels, reorder, result destroy
- Levels + visual order; X9-removed → `ROBO_BIDI_LEVEL_REMOVED`; UTF-8 `byte_ends`
- Official `BidiCharacterTest.txt` conformance: **91,707 / 91,707**
- Python oracle also green on abstract `BidiTest.txt`: **770,241 / 770,241**
- Unit coverage: ASCII, Arabic, Persian, Urdu, Hebrew, mixed, EN/AN digits,
  punctuation/brackets, NSM, embeddings/overrides, isolates/FSI, paragraphs,
  empty/long/invalid UTF-8
- Documented: bidi ≠ shaping; separate from line breaking; source-index mapping
- Debug / Release / ASan+UBSan: **166/166** tests pass

**Definition of done:**

- [x] Full UAX #9 (not Arabic=RTL heuristics)
- [x] Official BidiCharacterTest conformance
- [x] Embeddings, overrides, isolates, weak/neutral/implicit, L2 reorder
- [x] Explicit vs AUTO paragraph direction
- [x] Bytes / code points / levels / visual order distinguished
- [x] Invalid UTF-8 policy matches RoboUnicode
- [x] No shaping/fonts/layout/CSS bidi
- [x] Existing suites still pass

## Goal 3.12 — Script and Script_Extensions (UAX #24)

**Status:** complete

**Done:**

- Extended UCD pipeline for **Script_Extensions** + ISO 15924 short names
  (`Scripts.txt`, `ScriptExtensions.txt`, `PropertyValueAliases.txt`)
- Full `robo_unicode_script_t` enum (all Unicode 16.0.0 scripts)
- API: `script`, `script_extensions`, `has_script`, long/short names, `from_name`
- Minimal `script_runs` heuristic (documented; Common/Inherited continuation)
- Compact SCX: 110 unique sets, 174 ranges, sorted uint16 pool
- Conformance: 204 SCX ranges + 2,253 Script samples; `validate_script.py` green
- Coverage: Latin…Hangul catalogue, Persian/Urdu Arabic letters, historical
  scripts, Common/Inherited/SCX, independent of bidi
- Debug / Release / ASan+UBSan: **172/172** tests pass

**Definition of done:**

- [x] Official Script + Script_Extensions (not hand tables)
- [x] Script ≠ Script_Extensions documented and tested
- [x] All Unicode 16 scripts representable
- [x] Short/long names + Qaai
- [x] Independent of bidi / normalization / segmentation
- [x] No font/shaping/RoboText work
- [x] Existing suites still pass

## Goal 3.13 — Unicode Emoji Properties and Extended Emoji Handling (UTS #51)

**Status:** complete

**Done:**

- Extended shared UCD bool properties: Emoji_Modifier, Emoji_Modifier_Base,
  Emoji_Component, Regional_Indicator, Variation_Selector (+ existing Emoji /
  Emoji_Presentation / Extended_Pictographic)
- Official **Emoji 16.0** RGI sequence trie (`generate_emoji.py`) from
  `emoji-sequences.txt` + `emoji-zwj-sequences.txt`
- Public API `RoboUnicodeEmoji.h` — classify / UTF-8 match / next + kinds
- Property wrappers remain on `RoboUnicodeProps.h` (no duplicate tables)
- Boundaries documented: properties ≠ sequences ≠ graphemes ≠ glyphs ≠ paint
- UAX #29 grapheme algorithm unchanged; sequence layer is parallel catalog
- Conformance: fully-qualified `emoji-test.txt` ∩ catalog **3,781 / 3,781**
- Coverage: keycap/flag/modifier/ZWJ/tag/VS, invalid UTF-8, intl mixes
- Debug / Release / ASan+UBSan: **182/182** tests pass

**Definition of done:**

- [x] Official UTS #51 / emoji-data (not hand-coded famous emoji)
- [x] Independent emoji property bits preserved
- [x] RGI sequence categories (basic/keycap/flag/tag/modifier/ZWJ)
- [x] UTF-8 byte ranges; STRICT invalid UTF-8 = same RoboUnicode policy
- [x] Grapheme relationship documented; no unofficial emoji-grapheme algo
- [x] No fonts / color glyphs / shaping / layout / graphics
- [x] Existing suites still pass
- [x] Chromium remains reference-only

## Goal 3.14 — Unicode Data Generation, Versioning, Validation, Conformance

**Status:** complete

**Done:**

- Audit: `unicode/AUDIT.md`; upgrade workflow: `unicode/UPGRADE.md`
- Single pin: `Tools/ucd/robo_unicode_version.py` → `RoboUnicodeVersion.h`
- Machine-readable `manifest.json` (official URLs, SHA-256, roles)
- Orchestrator: `generate_all.py` (validate → generate → golden / repro)
- `verify_pipeline.py`, `golden_hashes.json`, `measure_tables.py`
- `Fetch-Ucd.ps1` reads manifest + verifies checksums
- Attribution: `Docs/UnicodeDataAttribution.md`
- Infra suite: cross-module version coherence, broad scripts, microbench
- Offline policy: normal `Build.ps1` never downloads or regenerates
- Deterministic generation: `--repro` green (31 artifacts)
- Debug / Release / ASan+UBSan: **185/185** tests pass

**Definition of done:**

- [x] One coherent Unicode / Emoji version across all modules
- [x] Manifest + checksum validation
- [x] Reproducible generation without wall-clock metadata
- [x] Golden hashes detect generator drift
- [x] Conformance fixtures remain official Unicode data
- [x] No Chromium dependency; no fonts/shaping/layout
- [x] Goals 3.3–3.13 still pass

## Goal 4.1 — RoboI18N Foundation (Locale / BCP 47)

**Status:** complete

**Done:**

- Module: `Include/RoboI18N/`, `Source/RoboI18N/`, guide `i18n/README.md`
- BCP 47 parse/format for language / script / region / variants / extensions / private-use
- Value-type `robo_i18n_locale_t` (no heap for ordinary parse)
- IANA Preferred-Value + grandfathered/redundant tables (`generate_iana.py`, File-Date 2026-05-05)
- Equality, range matching, truncation `parent` fallback
- Default locale abstraction (`und`, not hard-coded `en-US`)
- Locale preference lists with optional `q=` weights
- CLDR architecture/manifest only (`version: none`) — no invented locale data
- Suite: basic/script/region/complex/invalid/canon/match/fallback/list/fuzz/microbench
- Debug / Release / ASan+UBSan: **197/197** tests pass (at Goal 4.1 completion)

**Definition of done:**

- [x] BCP 47 foundation (not stringly `"en"` only)
- [x] Canonicalization from IANA (not invented maps)
- [x] Fallback documented and tested
- [x] Unicode Script ≠ locale documented
- [x] No formatting / plural / collation / messages / detection
- [x] No Chromium dependency
- [x] Existing Unicode tests still pass

## Goal 4.2 — RoboI18N Locale Data and CLDR Integration

**Status:** complete

**Done:**

- CLDR **46** (JSON 46.0.0) pin; Unicode association **16.0.0** from CLDR metadata
- Generator `Tools/i18n/generate_cldr.py` + `Fetch-Cldr.ps1` + manifest checksums
- Compact tables: string pool, likely subtags, numbering systems, calendar prefs,
  measurement, parentLocales, per-locale numbering defaults
- APIs: maximize/minimize, data parent/root inheritance, numbering/calendar/measurement
- Suites: `SuiteRoboI18NCldr.c` (intl locales, digits, inheritance, microbench)
- Deterministic generation (`--repro` + `cldr_golden_hashes.json`)
- Docs: `i18n/README.md`, `CLDR_ATTRIBUTION.md`, architecture/version pins
- No number/date formatting, plurals, collation, or messages
- Debug / Release / ASan+UBSan: **209/209** tests pass
- Generated `robo_cldr_data.c` ≈ **511 KiB** source (string pool + tables)

**Definition of done:**

- [x] Versioned CLDR subset, offline builds
- [x] Likely subtags maximize/minimize from CLDR data
- [x] Numbering-system metadata (incl. non-Latin digits)
- [x] Calendar preference metadata (ids only)
- [x] Measurement preferences
- [x] Locale data inheritance including root
- [x] International coverage tests (ur/ar/fa/he/hi/bn/th/zh/ja/ko/…)
- [x] No Chromium dependency; no premature formatting subsystems

## Goal 4.3 — Numbering Systems and Number Formatting Foundation

**Status:** complete

**Done:**

- Extended CLDR generator: symbols, decimal patterns, grouping sizes,
  `minimumGroupingDigits` (511 locale×nsys rows); `robo_cldr_data.c` ≈ **556 KiB**
- Public API `RoboI18NNumber.h`: reusable stack formatter, options, rounding modes
- Exact `int64`/`uint64`/`robo_i18n_decimal_t`; documented IEEE-754 `double` path
- Locale symbols (en/de/fr), Indian grouping (hi/bn), localized digits (arab/arabext/deva/beng/thai)
- Buffer size query + `RoboString` append; no visual/rendering code
- Suite `SuiteRoboI18NNumber.c`; Debug / Release / ASan+UBSan: **221/221**
- Docs: `i18n/README.md`, architecture, version **0.3.0**

**Definition of done:**

- [x] CLDR-driven decimal/group/sign/NaN/∞ symbols
- [x] Grouping primary/secondary + min grouping threshold
- [x] Numbering-system digit substitution
- [x] Integer exactness (incl. UINT64_MAX)
- [x] Documented rounding (default half-even)
- [x] International locale coverage tests
- [x] No currency/date/message/collation/fonts
- [x] No Chromium dependency

## Goal 4.4 — Plural Rules and Locale-Aware Plural Categories

**Status:** complete

**Done:**

- CLDR 46 `plurals.json` / `ordinals.json` → build-time bytecode (`plural_compile.py`)
- Public API `RoboI18NPlural.h`: cardinal/ordinal select, operands, category names
- Exact decimal operands (`n,i,v,w,f,t`; `e/c=0` for ordinary inputs); negative → abs
- Locale resolve: language-region → language-script → language → data parents (`pt-PT` vs `pt`)
- Generated samples from CLDR `@integer`/`@decimal` → `Tests/Data/robo_plural_samples.txt`
- Suite `SuiteRoboI18NPlural.c` (ar/ru/pl/fr/en/ja/… + official samples)
- Debug / Release / ASan+UBSan: **241/241**; CLDR samples **14335/14335**
- Version **0.4.0** / generator **1.3.0**; no message formatting
- Perf (Release): ~60k selects in ~14 ms

**Definition of done:**

- [x] Data-driven cardinal rules (not English binary plural)
- [x] Cardinal + ordinal APIs; ordinal rules from CLDR
- [x] Operands preserve trailing zeros (`1` vs `1.0` vs `1.00`)
- [x] Locale fallback without wrong-language selection
- [x] Official CLDR sample coverage + Arabic/Slavic/decimal/negative/large tests
- [x] No Chromium dependency; no message engine

## Goal 4.5 — Date/Time and Calendar Foundation

**Status:** complete

**Done:**

- Date/time model: instant (`unix_ms`), Gregorian date, wall time (ns), datetime, duration
- Calendar abstraction: gregorian/iso8601/buddhist/japanese conversions; islamic/hebrew/persian/chinese metadata-only
- CLDR calendar generator: weekData, era starts, names/patterns → `RoboCalData.*`
- Time-zone abstraction: UTC + fixed offsets; IANA deferred to Goal 4.14 (`i18n/TZDB.md`)
- Locale week/weekend + calendar display metadata APIs
- Suite `SuiteRoboI18NDateTime.c`; version was **0.5.0** / generator **1.4.0** at completion
- Debug / Release / ASan+UBSan: **252/252** at Goal 4.5 completion
- Localized date/time formatting deferred to Goal 4.6

**Definition of done:**

- [x] Documented model separating instant / local / calendar / zone / locale
- [x] Gregorian + ISO week primitives; leap-year rules
- [x] Instant UTC round-trips; fixed-offset zones
- [x] CLDR calendar metadata generated/validated
- [x] tzdb architecture documented (data not embedded)
- [x] No Chromium dependency; no full formatting engine

## Goal 4.6 — Localized Date/Time Formatting

**Status:** complete

**Done:**

- `RoboI18NDateTimeFormat` — style / pattern / skeleton formatters on top of Goal 4.5
- CLDR short/medium/long/full date & time; datetime combination via `{0}`/`{1}` patterns
- Compiled pattern tokens (no re-parse per format call)
- Skeleton normalize + exact/`availableFormats` match + documented best-match scoring
- Month/weekday/era/day-period names (wide/abbr/narrow where generated)
- Numbering-system digits via `robo_i18n_number_formatter`
- Zone display: GMT/ISO offsets + IANA id; **no invented localized zone names**
- Calendars with conversion only (gregorian/buddhist/japanese); islamic/hebrew/persian/chinese → `ROBO_NOT_SUPPORTED`
- Suite `SuiteRoboI18NDateTimeFormat.c`; API was **0.6.0** / generator **1.5.0** at completion
- `availableFormats` tables in `RoboCalData.*`
- Test harness limit raised to 384 cases
- Debug / Release / ASan+UBSan: **263/263** at Goal 4.6 completion

**Definition of done:**

- [x] Localized date / time / datetime formatting
- [x] CLDR styles + patterns + skeleton foundation
- [x] Localized field names + numbering systems
- [x] Calendar selection where conversion exists
- [x] Time-zone fields integrated with existing abstraction
- [x] RTL / non-Latin semantic string tests
- [x] Deterministic CLDR generation; no Chromium

## Goal 4.7 — Date/Time Interval and Relative Time Foundation

**Status:** complete

**Done:**

- Interval model (`robo_i18n_datetime_interval_t` / instant interval) with explicit reverse handling
- CLDR `intervalFormats` + fallbacks in `RoboCalData.*` (2274+ patterns)
- Greatest-difference field selection + ICU-style repeated-letter pattern split
- Relative-time model + `dateFields` tables (`RoboRelData.*`) for 18 locales
- Auto keywords (yesterday/today/…) vs always-numeric modes
- Plural categories via existing `robo_i18n_plural_*` + number formatting for `{0}`
- Suites `SuiteRoboI18NInterval.c`, `SuiteRoboI18NRelative.c`
- API **0.7.0** / generator **1.6.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **276/276**

**Definition of done:**

- [x] Interval model + CLDR interval formatting with field collapse
- [x] Relative-time formatting with plural integration
- [x] Numeric and natural modes where CLDR provides them
- [x] Non-Latin / RTL / Slavic plural tests
- [x] Calendar / timezone interaction where supported
- [x] Deterministic generation; no Chromium

## Goal 4.8 — Calendar Arithmetic and Date/Time Utilities

**Status:** complete

**Done:**

- `RoboI18NDateTimeArith` — period vs duration, date/instant arithmetic, comparisons
- End-of-month policy: **CLAMP** to last valid day (e.g. Jan 31 + 1 month → Feb 28/29)
- ISO week start/end / weeks-in-year; locale week boundaries (CLDR first day) distinct from ISO
- Civil boundaries (day/month/quarter/year); zone-aware start-of-day and instant+period (FIXED/UTC)
- Calendar-aware `add_period` via Gregorian bridge for gregorian/iso8601/buddhist/japanese; islamic/persian/hebrew → `ROBO_NOT_SUPPORTED`
- Business-day abstraction (CLDR weekend + optional holiday hook; no holiday DB)
- Checked integer overflow (`ROBO_ERROR` / existing `ROBO_NOT_SUPPORTED` on instant overflow)
- Suite `SuiteRoboI18NDateTimeArith.c` (invariants, clamp, calendars, business days, fuzz)
- API **0.8.0** / generator **1.7.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **290/290**

**Definition of done:**

- [x] Date / duration / period arithmetic with documented EOM policy
- [x] Instant arithmetic with overflow checks
- [x] ISO week + boundary utilities
- [x] Time-zone-aware period semantics (FIXED/UTC; IANA → unsupported until tzdb)
- [x] Business-day architecture without hard-coded holidays
- [x] Multi-calendar support / explicit unsupported errors
- [x] Property/fuzz invariants; Debug / Release / Sanitize green
- [x] No Chromium dependency

## Goal 4.9 — Locale-Aware Collation Foundation

**Status:** complete

**Done:**

- `RoboI18NCollation` / `robo_i18n_collator_t` — locale-bound collator (no hidden global locale)
- UCA / DUCET **16.0.0** tables (`RoboUcaData`, `generate_uca.py`, `Fetch-Uca.ps1`)
- Strengths: primary…identical; alternate non-ignorable/shifted; numeric digit-run option
- Pipeline: STRICT UTF-8 → NFD → CE formation (contractions/expansions/implicit) → level compare / sort keys
- Binary / code-point helpers kept separate from linguistic compare
- CLDR **46** collation metadata + compiled remaps for selected tailorings (sv/es/de-phonebook/tr/…)
- Large CJK/Arabic rule CDATA: architecture present (type metadata); runtime uses root UCA until full LDML builder
- Conformance sample from Unicode `CollationTest_NON_IGNORABLE_SHORT.txt`
- Suite `SuiteRoboI18NCollation.c`
- API **0.9.0** / generator **1.8.0** / UCA **16.0.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **300/300**

**Definition of done:**

- [x] RoboCollator with compare / equal / sort keys
- [x] UCA foundation + authoritative allkeys data (deterministic)
- [x] Locale tailoring architecture + representative remaps
- [x] Strength / alternate / numeric / case-first options
- [x] Invalid UTF-8 → `ROBO_PARSE_ERROR`
- [x] International locale smoke + fuzz + UCA sample tests
- [x] Debug / Release / Sanitize green; no Chromium

## Goal 4.10 — Locale Display Names and Localization Data Foundation

**Status:** complete

**Done:**

- `RoboI18NDisplayNames` — language / script / region / currency / calendar /
  numbering-system / time-zone / composed locale display names
- Identifier vs display locale always distinct; output is logical UTF-8 (no bidi reorder)
- Compact CLDR **46** tables (`RoboDispData`, `generate_display.py`) from
  localenames, currencies, timeZoneNames, metaZones
- Fallback: display locale → CLDR parent / truncation → **en** tables →
  identifier (`FALLBACK_CODE`, default) or `ROBO_NOT_FOUND`
- Locale composition via CLDR `localePattern` / `localeSeparator` (not naive join)
- Currency name / symbol / narrow symbol; no conversion rates
- TZ: exemplar city, metazone long names, GMT format for FIXED/UTC; IANA GMT
  without tzdb → `ROBO_NOT_SUPPORTED` where offset is required
- Context: standard names only; `dialect` option reserved
- Suite `SuiteRoboI18NDisplayNames.c` (RTL/non-Latin smoke + fuzz)
- API **0.10.0** / generator **1.9.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **310/310**

**Definition of done:**

- [x] All display-name categories listed in Goal 4.10
- [x] Deterministic generated data + version metadata
- [x] Explicit missing-data / fallback policy
- [x] International locale coverage + fuzz; no invented translations
- [x] Debug / Release / Sanitize green; no Chromium

## Goal 4.11 — Message Localization and Resource Catalog Foundation

**Status:** complete

**Done:**

- RoboMsg source format (`.robomsg.json`) + `generate_messages.py` validator/compiler
- Generated `RoboMsgData` tables + `RMSG` binary blob (`robo_msg_demo_blob`)
- `RoboI18NMessage` — catalog open/load/close, exists/get/format/format_id
- Stable message IDs; optional translator `context`; named `{placeholder}` + `{{`/`}}` escapes
- Plural map architecture selecting via existing `robo_i18n_plural_*`
- Locale fallback: exact → truncation parents → **configured** fallback (not assumed English)
- Missing-message: `NOT_FOUND` or `RETURN_ID` (never empty success)
- Demo catalogs for 15 locales (RTL / non-Latin / emoji / combining)
- Suite `SuiteRoboI18NMessage.c` (fallback, blob load, fuzz)
- Docs: `i18n/RoboMsg.md`
- API **0.11.0** / generator **1.10.0**
- Debug / Release / ASan+UBSan: **319/319**

**Definition of done:**

- [x] Catalog format + compiler + runtime generated catalogs
- [x] Deterministic fallback / missing / placeholder / plural architecture
- [x] International coverage + malformed rejection + fuzz
- [x] Debug / Release / Sanitize green; no Chromium

## Goal 4.12 — RoboI18N Message Formatting Engine

**Status:** complete

**Done:**

- RoboMsgFmt parser + AST + formatter (`RoboI18NMessageFormat`)
- Literals, named args, `number` / `date` / `time`, `select`, `plural`, `selectordinal`
- Exact plural selectors (`=N`), `#` → existing number formatter
- Nested select/plural; depth / size / output limits
- Explicit value model (string / i64 / u64 / decimal / double / bool / date/time/instant)
- Catalog integration: `robo_i18n_message_format_mf`
- Ordinal architecture via existing `robo_i18n_plural_ordinal`
- Suite `SuiteRoboI18NMessageFormat.c` (intl + malformed + fuzz)
- Docs: `i18n/RoboMsgFormat.md`
- API **0.12.0** / generator **1.11.0** (bumped by later goals)
- Debug / Release / ASan+UBSan: **334/334**

**Definition of done:**

- [x] Parser + reusable message representation
- [x] select / cardinal plural / exact / `#` / nesting
- [x] Number + date/time delegated to RoboI18N
- [x] Ordinal API boundary integrated
- [x] Catalog integration; malformed + Unicode + intl tests
- [x] Debug / Release / Sanitize green; no Chromium

## Goal 4.13 — Message Formatting Validation and Compatibility Layer

**Status:** complete

**Done:**

- RoboMsgFmt syntax version **1** + optional `#!robo-mf/1` prologue
- Structured diagnostics (`robo_i18n_mf_diagnostic_t`)
- `message_validate` / `mf_validate_ex` / `mf_parse_ex` with limits
- Static argument analysis + contract compatibility
- Duplicate-branch rejection; type-conflict detection
- Catalog entry enumeration + catalog-wide / fallback validation
- Golden + negative + fuzz suite (`SuiteRoboI18NMessageValidate.c`)
- Docs: `i18n/RoboMsgFormat.md` validation section
- API **0.13.0** / generator **1.12.0** (bumped by later goals)
- Debug / Release / ASan+UBSan: **350/350**

**Definition of done:**

- [x] Versioning + validation + diagnostics + analysis
- [x] Catalog / fallback compatibility
- [x] Limits, golden, negative, fuzz
- [x] Debug / Release / Sanitize green; no Chromium

## Goal 4.14 — Time Zones and Time Zone Database Foundation

**Status:** complete

**Done:**

- Time-zone model: Instant / local / offset / IANA zone / fixed / UTC (not locale)
- IANA tzdb **2026c** pipeline: `Fetch-Tzdb.ps1` + `generate_tzdb.py`
- Compact `RoboTzData` tables (zones, aliases, transitions, string pool)
- TZif parse + POSIX TZ footer expansion through 2100
- Lookup, offset/DST/abbr, instant↔local, next/prev transition
- Ambiguous / nonexistent local times with EARLIER / LATER / REJECT
- Aliases (Link) without duplicating rules; fixed-offset zones ±18h
- Leap-second policy: POSIX 86400-day days (not modeled)
- Suite `SuiteRoboI18NTimeZone.c` (DST, round-trip, intl, fuzz)
- Docs: `i18n/TZDB.md`, `Tools/i18n/TZDB_ATTRIBUTION.md`
- API **0.14.0** / generator **1.13.0**
- Debug / Release / ASan+UBSan: **361/361**

**Definition of done:**

- [x] Model + public API + tzdb integration + reproducible generation
- [x] Instant↔local + ambiguity/gap + transitions + aliases + fixed
- [x] Historical range documented; validation + golden hashes
- [x] Integration with existing datetime APIs; no Chromium
- [x] Debug / Release / Sanitize green

## Next

- Goal 4.15+ (do not start until requested)

