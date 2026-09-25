# Roboticela Runtime Progress

## Note on commands in this log

Entries below were written as the work landed, and some of them name test and
run scripts that have since been consolidated. History is left as written; the
current commands are:

```
scripts\robo check                 environment validation
scripts\robo build-all             the whole system
scripts\robo test-all              every test surface
scripts\robo test-<component>      base, memory, containers, unicode, i18n,
                                   font, script, graphics, window, display,
                                   html, demo, infra
scripts\robo test-qemu-<stage>     boot, kernel, display
scripts\robo validate-deployment   structural validation of the artifact
scripts\robo run-qemu              full build, then boot the OS
```

See `docs/Build.md` at the repository root for the authoritative guide.

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
- Levels + visual order; X9-removed → `RoboBidiLevelRemoved`; UTF-8 `byte_ends`
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
- Full `UnicodeScript` enum (all Unicode 16.0.0 scripts)
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
- Value-type `I18nLocale` (no heap for ordinary parse)
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
- Exact `int64`/`uint64`/`I18nDecimal`; documented IEEE-754 `double` path
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
- Numbering-system digits via `I18nNumberFormatter`
- Zone display: GMT/ISO offsets + IANA id; **no invented localized zone names**
- Calendars with conversion only (gregorian/buddhist/japanese); islamic/hebrew/persian/chinese → `RoboNotSupported`
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

- Interval model (`I18nDatetimeInterval` / instant interval) with explicit reverse handling
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
- Calendar-aware `add_period` via Gregorian bridge for gregorian/iso8601/buddhist/japanese; islamic/persian/hebrew → `RoboNotSupported`
- Business-day abstraction (CLDR weekend + optional holiday hook; no holiday DB)
- Checked integer overflow (`RoboError` / existing `RoboNotSupported` on instant overflow)
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

- `RoboI18NCollation` / `I18nCollator` — locale-bound collator (no hidden global locale)
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
- [x] Invalid UTF-8 → `RoboParseError`
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
  identifier (`FALLBACK_CODE`, default) or `RoboNotFound`
- Locale composition via CLDR `localePattern` / `localeSeparator` (not naive join)
- Currency name / symbol / narrow symbol; no conversion rates
- TZ: exemplar city, metazone long names, GMT format for FIXED/UTC; IANA GMT
  without tzdb → `RoboNotSupported` where offset is required
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
- Generated `RoboMsgData` tables + `RMSG` binary blob (`MsgDemoBlob`)
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
- Catalog integration: `I18nMessageFormatMf`
- Ordinal architecture via existing `I18nPluralOrdinal`
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
- Structured diagnostics (`I18nMfDiagnostic`)
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
- API **0.14.0** / generator **1.13.0** (bumped by later goals)
- Debug / Release / ASan+UBSan: **361/361**

**Definition of done:**

- [x] Model + public API + tzdb integration + reproducible generation
- [x] Instant↔local + ambiguity/gap + transitions + aliases + fixed
- [x] Historical range documented; validation + golden hashes
- [x] Integration with existing datetime APIs; no Chromium
- [x] Debug / Release / Sanitize green

## Goal 4.15 — Time Zone Formatting, Names, and Display Data

**Status:** complete

**Done:**

- Timezone display styles: exemplar, long/short generic/std/dst, specific, GMT, identifier
- `I18nTimezoneDisplayNameAt` (instant-aware metazone + DST)
- CLDR data: short metazone names, zone overrides, historical mz ranges, gmtZeroFormat
- GMT formatting with locale patterns + numbering-system digits
- Exemplar cities (CLDR or structural `_`→` ` fallback)
- Alias → canonical data lookup; requested id for IDENTIFIER
- Date/time formatter `z`/`v`/`V`/`O` wired to display layer
- Suite `SuiteRoboI18NTimeZoneDisplay.c`; docs `i18n/TZDisplay.md`
- API **0.15.0** / generator **1.14.0**
- Debug / Release / ASan+UBSan: **369/369**

**Definition of done:**

- [x] Localized names + GMT + exemplar + metazone windows
- [x] Formatter integration + fallbacks + intl tests
- [x] Generated/validated CLDR display data; no Chromium
- [x] Debug / Release / Sanitize green

## Goal 4.16 — Locale Data Completeness, Fallback, and Runtime Locale Service

**Status:** complete

**Done:**

- `RoboI18NLocaleService` — Unicode keywords (`ca`/`nu`/`hc`/`co`/`tz`), resolve settings, fallback chain, negotiate
- Application vs default vs effective locale abstraction (no OS coupling)
- Centralized numbering / calendar / hour-cycle / collation / timezone selection with documented precedence
- CLDR `timeData` → `robo_hc_data` (`generate_hour_cycle.py`)
- Number, DateTimeFormat, Collation, Message wired to the service / data parents
- Capability queries + `I18nDataVersions`
- Consistency validator `validate_locale_consistency.py`
- Suite `SuiteRoboI18NLocaleService.c`; docs `i18n/LocaleService.md`
- API **0.16.0** / generator **1.15.0**
- Debug / Release / ASan+UBSan: **381/381**

**Definition of done:**

- [x] RoboLocale runtime context + centralized resolve/fallback/matching
- [x] Cross-subsystem consistency + intl/robustness tests
- [x] Documentation + version metadata
- [x] Debug / Release / Sanitize green

## Goal 4.17 — Locale Negotiation, Accept-Language, and Language Matching

**Status:** complete

**Done:**

- `RoboI18NLocaleNegotiate` — language ranges, q millipercent, Accept-Language parse/serialize
- Script-sensitive matching via CLDR likely subtags; region ranking; `q=0` exclusions; `*`
- Deterministic `negotiate_ex` + match metadata; preference normalize (highest-q / first-wins)
- Goal 4.16 `locale_negotiate` delegates to the new engine
- Suite `SuiteRoboI18NLocaleNegotiate.c`; docs `i18n/LocaleNegotiate.md`
- API **0.17.0** (generator **1.15.0**, no new CLDR tables)
- Debug / Release / ASan+UBSan: **395/395**

**Definition of done:**

- [x] Ranges, weights, matching, negotiation, Accept-Language, limits, fuzz
- [x] Intl + cross-subsystem tests; Debug / Release / Sanitize green

## Goal 4.18 — Ordinal Plural Rules

**Status:** complete

**Done:**

- Confirmed CLDR 46 ordinal bytecode path (`ordinals.json` → `PluralOrdinalIndex`)
- API: `plural_ordinal` / `ordinal_i64`, `has_rules`, `category_from_name`; categories always include `other`
- Cardinal vs ordinal remain distinct; locale fallback shared with Goal 4.4
- RoboMsgFmt `selectordinal` + exact-selector precedence verified
- Suite `SuiteRoboI18NOrdinal.c`; docs `i18n/OrdinalPlurals.md`
- API **0.18.0** (generator **1.15.0**; existing plural tables)
- Debug / Release / ASan+UBSan: **408/408** (CLDR plural samples **14335/14335**)

**Definition of done:**

- [x] Ordinal categories + CLDR data + operands + locale integration
- [x] Message-format selectordinal + exact selectors
- [x] Intl / conformance / fuzz coverage
- [x] Documentation; Debug / Release / Sanitize green

## Goal 4.19 — Relative Time and Unit Formatting

**Status:** complete

**Done:**

- CLDR units tables (`generate_units.py` → `RoboUnitData.*`): 60 curated units × 3 widths × 19 locales
- `RoboI18NUnit` — long/short/narrow formatting, plural patterns, compound `per`, conversion foundation
- Extended Goal 4.7 relative AUTO for CLDR `relative-type--2` / `+2` (e.g. anteayer / vorgestern)
- Suites `SuiteRoboI18NUnit.c` (+ relative ±2); docs `i18n/Units.md`
- API **0.19.0** / generator **1.16.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **417/417**
- No unit parsing (deferred); no Chromium dependency

**Definition of done:**

- [x] Locale-aware unit formatting (styles, plurals, compounds)
- [x] Conversion separated from localization (exact vs approximate documented)
- [x] Relative-time production layer on 4.7 foundation (special forms ±2)
- [x] Intl / fuzz tests; Debug / Release / Sanitize green
- [x] Documentation; PROGRESS updated

## Goal 4.20 — Measurement Systems and Unit Preferences

**Status:** complete

**Done:**

- CLDR `unitPreferenceData` + temperature measurement systems → `RoboMeasurePrefData.*`
- `RoboI18NMeasure` preference context (system, category overrides, `ms` keyword)
- Preferred-unit resolution with documented precedence; integrate convert→format
- Suite `SuiteRoboI18NMeasure.c` (region matrix, lang≠region, conversions, fuzz)
- Docs `i18n/MeasurePrefs.md`
- API **0.20.0** / generator **1.17.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **425/425**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Measurement systems + locale-derived preferred units
- [x] User/app overrides without global mutable state
- [x] Integration with Goal 4.19 convert/format
- [x] Intl / conversion / fuzz tests; Debug / Release / Sanitize green
- [x] Documentation; PROGRESS updated

## Goal 4.21 — Currency Data and Currency Formatting

**Status:** complete

**Done:**

- CLDR `currencyData` + `codeMappings` + `currencyFormats` → `RoboCurrencyData.*`
- `RoboI18NCurrency` — lookup, region/locale defaults, symbol/code/name/accounting
- Standard vs cash precision; `format_minor` for exact minor units; no FX conversion
- Suite `SuiteRoboI18NCurrency.c` (intl matrix, accounting, cash, fuzz)
- Docs `i18n/Currency.md`
- API **0.21.0** / generator **1.18.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **436/436**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Currency metadata + locale-aware formatting on Goal 4.3 numbers
- [x] Fraction digits / cash rounding separated; accounting from CLDR patterns
- [x] Intl / precision / robustness tests; Debug / Release / Sanitize green
- [x] Documentation; PROGRESS updated

## Goal 4.22 — Currency/Number Parsing

**Status:** complete

**Done:**

- `RoboI18NParse` — strict/permissive number + currency parsers
- Localized digits, grouping, decimal/sign symbols; currency symbol/code/name
- Ambiguity reporting; `require_currency` / accepted sets; round-trip tests
- Suite `SuiteRoboI18NParse.c`; docs `i18n/Parse.md`
- API **0.22.0** / generator **1.19.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **448/448**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Number + currency parsing with structured diagnostics
- [x] Strict vs permissive; ambiguity never silent in strict
- [x] Intl / round-trip / digit / fuzz tests; Debug / Release / Sanitize green
- [x] Documentation; PROGRESS updated

## Goal 4.23 — Date/Time Parsing

**Status:** complete

**Done:**

- `RoboI18NDateTimeParse` — strict/permissive date/time/zoned parsers
- CLDR styles/patterns/skeletons; localized names & digits; calendar fields
- Timezone offset/IANA parsing; DST ambiguity policies; no invented UTC
- Suite `SuiteRoboI18NDateTimeParse.c`; docs `i18n/DateTimeParse.md`
- API **0.23.0** / generator **1.20.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **461/461**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Date/time parsing with structured diagnostics
- [x] Strict vs permissive; missing zone never invents UTC
- [x] Intl / round-trip / calendar / DST / digit / fuzz tests green
- [x] Documentation; PROGRESS updated

## Goal 4.24 — Locale-Aware Search & String Matching

**Status:** complete

**Done:**

- `RoboI18NSearch` — explicit modes (exact / codepoint / canonical / casefold /
  canonical+casefold / collation); diacritic-insensitive option; grapheme-safe ranges
- Reusable searcher with owned collator; UTF-8 byte hit ranges; find / iter / find_all
- Builds on RoboUnicode norm/casefold/grapheme + Goal 4.9 collation (no duplicated UCA)
- No transliteration; logical (not visual) order; input / find_all limits
- Suite `SuiteRoboI18NSearch.c`; docs `i18n/Search.md`
- API **0.24.0** / generator **1.21.0** / CLDR **46** / Unicode **16.0.0**
- Debug / Release / ASan+UBSan: **472/472**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Explicit matching modes (not one fuzzy compare)
- [x] Canonical / casefold / diacritic / collation search with source byte ranges
- [x] Grapheme-safe defaults; intl + RTL + fuzz tests; Debug / Release / Sanitize green
- [x] Documentation; PROGRESS updated

## Goal 4.25 — Transliteration Foundation

**Status:** complete

**Done:**

- `RoboI18NTranslit` — CLDR transform pipeline (curated IDs), offline
  `generate_translit.py` + `Fetch-Transforms.ps1`
- Compact rule tables (2835 rules / 16 IDs); forward/reverse; streaming hold
- Distinct from search/collation/translation; search does not auto-transliterate
- Suite `SuiteRoboI18NTranslit.c`; docs `i18n/Transliteration.md`
- API **0.25.0** / generator **1.23.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **484/484**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Data-driven transliteration engine + CLDR transform compilation
- [x] Explicit direction / normalization stages / security limits
- [x] Multilingual matrix + fuzz tests; docs; PROGRESS updated

## Goal 4.26 — Locale Data Completeness and Conformance Suite

**Status:** complete

**Done:**

- Authoritative data/version manifest (`Tools/i18n/manifest.json`,
  `i18n/DataManifest.md`) covering Unicode/Emoji/UCA/CLDR/tzdb/IANA/ISO4217-via-CLDR
- `verify_i18n_pipeline.py` — pin coherence, `i18n_golden_hashes.json`, locale
  consistency, Chromium include/link audit
- Expanded `validate_locale_consistency.py` + `I18nDataVersions` (UCA + ISO4217)
- Conformance suite `SuiteRoboI18NConformance.c` — 18-locale corpus, numbering,
  plural kinds, search≠translit, RTL/fuzz/perf smoke
- Completeness report `i18n/CompletenessReport.md` + `Completeness.md`
- API **0.26.0** / generator **1.24.0** / CLDR **46** / Unicode **16.0.0** / tzdb **2026c**
- Debug / Release / ASan+UBSan: **495/495**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Version / generator / golden / locale audits
- [x] Conformance + regression corpus; Chromium independence check
- [x] Documentation; PROGRESS updated

## Goal 4.27 — Final Integration, API Freeze, and Phase 4 Validation

**Status:** complete — **Phase 4 frozen**

**Done:**

- Public API freeze (`RoboI18nApiFrozen`, API **0.27.0** / generator **1.25.0**)
- Architecture / UTF-8 / performance / license / Phase4 docs
- Final suite `SuiteRoboI18NPhase4.c` — 20-locale regional matrix, format/parse
  round trips, message plural≠ordinal, Unicode-heavy, search byte ranges,
  concurrent formatters, fuzz/perf
- Dependency + Chromium independence re-audit; completeness limitation review
- Debug / Release / ASan+UBSan + `verify_i18n_pipeline.py` green
- Host suite: **507/507**

**Definition of done:**

- [x] Integration audit + dependency direction documented
- [x] Public API frozen; UTF-8 / locale / version contracts documented
- [x] Final regression / sanitizer / Chromium validation
- [x] PROGRESS marks Phase 4 complete

## Goal 8.6 — HTML attribute model

The temporary attribute table is gone. There is now exactly one attribute
type — `HtmlAttribute` — which the tokenizer stages, the tree builder
stores, the element owns, the readers return borrowed, and the mutation API
rewrites. Only the lifetime differs from stage to stage, so nothing has to be
translated between the parser and the tree.

**Shipped:**

- `RoboHTMLElement.h/.c` — the model's surface and its write verbs:
  - `HtmlAttribute` (borrowed name/value plus byte lengths and a
    namespace field that stays `NONE` until foreign content lands) and
    `HtmlAttributeInit`;
  - read side — `attribute_count`, `attribute_at`, `find_attribute`,
    `has_attribute`, and `attribute_value`, which returns `NULL` for absent
    and a non-`NULL` empty string for present-and-empty;
  - write side — `set_attribute` (collapses duplicates at the first
    match's position, appends when new), `add_attribute` (appends
    unconditionally; the parser's verb, and the only way a duplicate can
    enter the model), `remove_attribute` (drops every match; absent is a
    successful no-op). Names are validated and lowercased on the way in; the
    parser path still tolerates malformed names, because a stray byte must
    not abort a parse.
- `RoboHTMLPayload.c` — the encoder is the model's stored form: explicit
  lengths are the authority, `0` means "measure with `strlen`", and
  `HtmlPayloadMutateAttributes` rewrites an element payload by
  measuring what survives, staging the replacement body in zeroed scratch
  memory, and appending it. Node identity, children and tree position are
  untouched, so attribute mutation survives finalization — 8.4 closed
  *tree construction*, not the document.
- `RoboHTMLInternal.h` — one shared ASCII-case-insensitive name comparison
  (`HtmlNameEqual` / `_span`), used by element-name and attribute-name
  lookups alike, so the two cannot disagree.
- `RoboHTMLTreeBuilder.c` — `stage_attrs` produces `HtmlAttribute`
  instead of a second attribute record, preserving source order and
  duplicates; the tree dump reads attributes through the model.
- `RoboDemoHtml.c` — the walk honours the real `hidden` attribute: the
  element is recognised and counted but paints nothing and its subtree is not
  entered. `id`/`class` on the demo document cannot affect the picture, and a
  rule with visible consequences is the only way to show the seam works.
- `RoboHTMLDom.c` — the 8.1 capability flip: `attributes = 1` now that
  attribute storage is real (the field means the attribute model, not a
  `NamedNodeMap` surface, and its comment says so).
- `html/tests/SuiteRoboHTMLAttribute.c` — 21 `html.attr.*` cases;
  `demo/tests/SuiteRoboDemoHtml.c` — `demo.tree.attribute_hidden`.
- Docs: `html/docs/AttributeModel.md`, `html/README.md`,
  `html/docs/Architecture.md` §14, `html/docs/ElementModel.md`.
- RoboHTML **0.6.0** (`RoboHtmlApiFrozen=0`), runtime **0.11.0** (superseded
  by 8.7's 0.7.0 / 0.12.0).

**Defect fixed (found by the new suite):** the mutation scratch buffer was
**never zeroed**, while every string in the payload layout ends at a
terminator the writer does not copy — so the buffer's contents are part of
the encoding. Fresh allocations happened to be zero-filled, which hid the bug
until enough rewrites reused heap: `mutation_many` then read an element name
whose length drifted (3 → 5 → 11 → 85 bytes), the attribute walk lost its
place, and each rewrite silently dropped the table back to one entry.
`HtmlPayloadMutateAttributes` now zeroes the scratch block, exactly
as the payload writer does. Regression tests: `html.attr.mutation_many`,
`html.attr.teardown_with_many_mutations`.

Also fixed while wiring the verbs: a refused name reported
`RoboInvalidArgument` for every reason when the caller passed no `diag`,
because the strict normalizer reports through the diagnostics record. The
mutation path now supplies one, so a bad character (`PARSE_ERROR`) and a
too-long name (`NOT_SUPPORTED`) stay distinguishable.

**Definition of done:**

- [x] Real `HtmlAttribute` attribute set, owned by the element
- [x] Name and value storage with byte lengths; presence distinguishable from
      an empty value (`<input disabled>` vs `<input disabled="">` vs absent)
- [x] Lookup (case-insensitive, first match), insertion, update, removal,
      iteration, deterministic order, defined duplicate handling
- [x] The parser's duplicates and source order reach storage unchanged; the
      tree builder no longer keeps a parallel attribute record
- [x] Minimal namespace foundation on the attribute type
- [x] Bounded table: over the limit the write is refused and the element is
      left exactly as it was
- [x] Unicode values (Arabic/Persian/Urdu/CJK/emoji) and non-ASCII names
      round-trip byte-exactly; malformed input never desynchronises the walk
- [x] Ownership/lifetime safe: attributes cannot outlive their document, and
      nothing dangles across a rewrite
- [x] Debug dump shows attributes, read through the model
- [x] Visual vertical slice unchanged on the host and in QEMU, with `hidden`
      now driven by the real attribute set
- [x] Full suite green (1159/1159, Debug and Release) plus the
      panic/assert/mem-debug child probes
- [x] No Chromium dependency; all independence scans green
- [x] Stop after 8.6

## Goal 8.7 — Text nodes

**Status:** complete

Character data was already a node kind before this phase (`RoboHtmlNodeKindText`,
created by the 8.3 builder, already painted by the demo renderer). 8.7 adds the
**model over that kind**, the way 8.5 added one over elements and 8.6 over
attributes. The organising decision is the same: **a text node is a node**, so
it inherits identity, ownership, staleness detection, sibling links and
teardown rather than reimplementing them.

**What changed**

- `Include/RoboHTML/RoboHTMLText.h`, `Source/RoboHTML/RoboHTMLText.c` — the
  text model: `HtmlTextCreate` (the construction seam, text twin of
  `HtmlElementCreate`), a view (handle / parent / content / length /
  state), the read side (`is_text`, `content`, `length`, `is_empty`,
  `parent`), and the mutation verbs (`set`, `append`). Sibling navigation is
  deliberately left to `RoboHTMLNode.h` rather than mirrored, so there is no
  second traversal surface to keep in step.
- `RoboHTMLTreeBuilder.c` — text creation goes through the seam (the builder
  fills a descriptor instead of registering, encoding and linking by hand),
  and the run merge goes through `HtmlTextAppend`. `create_leaf`
  becomes `create_comment_leaf`; comments are not text nodes.
- `RoboHTMLTreeBuilder.c` (dump) — a text line is labelled `Text: "..."` and
  read through `HtmlTextContent`, so the dump cannot drift from the
  accessor and a whitespace-only run is legible.
- `RoboHTMLTypes.h/.c` — `RoboHtmlDomainText` for text-domain rejections.
- `RoboHTMLDom.h/.c` — capability flip: `text_nodes = 1`, with its comment
  making clear it is the text model and not a `CharacterData` surface.
- `html/tests/SuiteRoboHTMLText.c` — 23 `html.text.*` cases.
- Docs: `html/docs/TextModel.md`, `html/README.md`,
  `html/docs/Architecture.md` §15.
- RoboHTML **0.7.0** (`RoboHtmlApiFrozen=0`), runtime **0.12.0**.

**The three contracts worth stating once**

- **`NULL` is not `""`.** `HtmlTextContent` returns `NULL` for "not a
  text node" and a non-NULL empty string for "a text node whose run is
  empty", the same shape `HtmlElementAttributeValue` uses. `is_empty`
  is therefore a different question from `is_text`.
- **A stale handle is a miss, not misuse.** A well-formed handle naming
  nothing live resolves as `RoboNotFound`; only `NULL` arguments are
  argument errors — so every accessor lets a caller test before trusting.
- **Bytes are preserved exactly, and a run coalesces per run.** No
  normalisation, trimming, collapsing, resampling or re-encoding; and the
  tokenizer's segmentation is not observable in the tree, so
  `<p>Hello <b>world</b>!</p>` is three children rather than one node per
  token.

**Consistency defect found by the new suite:** the first draft of `set` /
`append` rejected an ill-formed handle with `RoboInvalidArgument` while
`view_get` / `parent` / `content` resolved the same handle as `RoboNotFound`.
The suite pinned the inconsistent pair, and the model now answers one way: a
cleared or stale handle is a lookup that missed. No defects were found in the
payload layer this phase — the 8.6 scratch-zeroing fix already covers the
`set`/`append` path, which the 32-iteration and long-run cases exercise.

**Definition of done:**

- [x] A real text node type, integrated with Node, Element and Document
- [x] `HtmlTextCreate` as the single construction seam; the tree
      builder goes through it
- [x] Content / length / parent / state access, with `NULL`-vs-`""`
      distinguishing "not text" from "present and empty"
- [x] `set` and `append`, including the append slow path when a payload sits
      behind the run; identity, parent, siblings and order untouched, so
      mutation survives finalization
- [x] Parser integration: real text nodes, runs that coalesce per run,
      elements/comments/mode changes closing a run
- [x] Whitespace (spaces, tabs, newlines, leading/trailing) and Unicode
      (Arabic/Persian/Urdu/Hindi/Bengali/CJK/emoji) preserved byte-exactly
- [x] Character references arrive already decoded and join their run
- [x] Empty-text policy stated and tested: the model permits an empty node,
      the parser never produces one (`<p></p>` has no children)
- [x] Traversal over a mixed child list; text is a leaf with no children
- [x] Ownership/lifetime safe: no dangling content, stale handles harmless,
      teardown releases registry and arena together
- [x] Malformed input never crashes and always leaves a validate_tree-clean
      tree
- [x] Debug dump shows text nodes with escaped whitespace
- [x] Visual vertical slice unchanged on the host and in QEMU
- [x] Full suite green (1182/1182, Debug and Release)
- [x] No Chromium dependency; all independence scans green
- [x] Stop after 8.7

## Goal 8.8 — DOM traversal

**Status:** complete

The tree was already navigable before this phase (`child_at`, `next_sibling`,
`find_element`, `subtree_text`). What 8.8 adds is the **authoritative
traversal surface over every node kind**: one header that answers what is next,
what is above, what is inside, in what order, and where the first match is —
without any caller knowing how children are stored. Three things made it worth
a phase rather than leaving the accessors alone: the same pre-order successor
existed in three places (element search, subtree text, and the debug dump —
recursively), child iteration by index was O(n²), and the recursive dump was a
stack-overflow vector on a tree the node ceiling permits.

**What changed**

- `Include/RoboHTML/RoboHTMLTraversal.h`, `Source/RoboHTML/RoboHTMLTraversal.c`
  — the traversal surface: relationship accessors (parent / first / last /
  next / prev), `depth` and `root`, the child cursor, the all/elements/text
  filters, the pre-order and descendant walkers, and `find_first` /
  `find_descendant`.
- `RoboHTMLNode.c` — `find_element` and `subtree_text` lose their private
  copies of the pre-order successor and go through the walker (element search
  through a predicate, subtree text through a text-filtered descendant walk),
  so the order, the filter, the cycle bound and the no-recursion rule live in
  one place.
- `RoboHTMLTreeBuilder.c` (dump) — the dump is a visitor on the walker instead
  of a recursion; depth arrives from the walk, and the depth cut is now a dump
  policy rather than a traversal limit.
- `RoboHTMLTypes.h/.c` — `RoboHtmlDomainTraversal` for traversal-domain
  rejections.
- `RoboHTMLDom.h/.c` — capability flip: `traversal = 1`, with `mutation` and
  `events` explicitly still `0`.
- `html/tests/SuiteRoboHTMLTraversal.c` — 20 `html.traverse.*` cases.
- Docs: `html/docs/Traversal.md`, `html/README.md`,
  `html/docs/Architecture.md` §16.
- RoboHTML **0.8.0** (`RoboHtmlApiFrozen=0`), runtime **0.13.0**.

**The contracts worth stating once**

- **Handles, not pointers.** Every accessor takes and returns a node handle, so
  no caller can see the registry, the sibling links, the payload arena or any
  container layout — which is what lets that storage change later without
  touching a caller. Nothing borrowed can outlive its call: a visitor gets a
  view for that call, and the child cursor holds two integers.
- **"Nothing there" is an answer, not an error.** A missing link is
  `RoboNotFound` with a cleared output, mirroring `child_at`'s out-of-range
  contract, so a zeroed handle is never returned as success. `NULL` arguments
  are argument errors; a handle that does not resolve is a lookup that missed.
  Link targets are looked up rather than trusted, so a corrupted or retired
  link reports a miss instead of aliasing a new node.
- **A filter selects what is visited, never what is stepped through.** A walk
  for elements still descends past text nodes, so every filtered walk is a
  subsequence of the same single document order.
- **Iterative, bounded, allocation-free, deterministic.** No recursion, no
  allocation, O(n), and a step bound of the document's live node count so a
  cycle is reported as `RoboError` rather than silently truncating the walk.
  Depth is absolute (document = 0) and free to the visitor.
- **The visitor's code is the walk's result**, so early exit needs no cancel
  channel; `find_first` is built on exactly that. A visitor may walk
  reentrantly; destroying the document mid-walk is detected, not dereferenced.

**Contract defect found while refactoring the consumers:** the walker checked
whether the document still existed only at the top of each step, so a visitor
that destroyed the document could still run a final `advance` and report
`RoboOk` — a walk reported as finished over a tree that no longer existed.
The check now also runs immediately after the visitor and at the "finished"
exit, making `RoboNotFound` unconditional. The suite separately caught four
wrong expectations of its own (relative-vs-absolute depth, `find_descendant`
expected to match its start, a cursor copy expected to share state, and dump
indentation off by one level) — each a body-of-the-test error, now pinned by
the literal value the model produces.

**Definition of done:**

- [x] Authoritative traversal API over Document, Element and Text
- [x] Relationships in both directions, with the miss contract
- [x] Absolute depth from the document node; rootmost-ancestor traversal
- [x] Child iteration that is O(n) (the cursor) and safe on empty parents
- [x] Deterministic document-order / pre-order enumeration, allocation-free
      and non-recursive, with a cycle bound that reports rather than truncates
- [x] Element-only and text-only traversals that still step through every node
- [x] Bounded search: `find_first` (at or below) vs `find_descendant` (inside)
- [x] `find_element` and `subtree_text` refactored onto the walker; the dump
      refactored onto the walker; the demo renderer's child iteration moved off
      `child_at` onto the cursor (O(n) per child list instead of O(n^2))
- [x] Attributes and text stay reachable from any traversed handle
- [x] Parser integration, Unicode, a 1000-level tree, empty documents and
      defensive input covered
- [x] Ownership/lifetime safe: stale handles and abandoned cursors are inert
- [x] Read-only by construction; capability reports `traversal = 1`
- [x] Visual vertical slice unchanged on the host and in QEMU
- [x] Full suite green (1202/1202, Debug and Release)
- [x] No Chromium dependency; all independence scans green
- [x] Stop after 8.8

## Goal 8.9 — DOM mutation

The tree was a read-only structure with an owner and a good navigation
surface. Now it is editable, and the whole phase is organised around the fact
that there is still exactly **one place a child link is written**:
`HtmlNodeLinkChild` (the parser's append, construction-gated) is now a
wrapper over `HtmlNodeInsertRelative`, which is where the kind check,
the detached-child rule, the reference-child rule and the sibling rewiring
live. A parse and a DOM edit cannot disagree about what a tree is.

**Five operations, one invariant set.** `append_child`, `insert_before`,
`remove_child`, `replace_child` and `remove_children`, all on a finalized
document — which is the state a parsed page is in, and the reason the phase is
useful at all. A node has at most one parent (inserting an attached node is
`RoboAlreadyExists`, never a silent steal), the result is always acyclic (a
node beneath its own descendant is refused), the document node can never
become a child, and only a document/element/fragment can be a parent.

**Detachment is not destruction.** A removed node keeps its subtree,
attributes and text, stays owned by its document, and can be re-inserted; in
the meantime it is a tree in its own right — `subtree_text`, `contains`,
`depth` (restarting at 0) and walks all work on it. There is no
node-destroy API because there is no state that needs one.

**A refusal is provably inert.** Every rejection is decided before the first
write, so a failed mutation leaves the tree byte-for-byte as it was — including
the document's tree version, which makes "this call changed nothing" something
a test asserts rather than assumes.

**Structural change is observable, so traversal can be honest about it.**
Every insert, detach, replace and clear bumps a monotonic per-document tree
version (0 stays reserved for "no document"). The 8.8 cursor and walkers now
snapshot it and stop with a MUTATION-domain `RoboError` the moment it moves,
because the node they were about to hand out may have been detached — and a
detached node is not a child. Mutation of a *different* document cannot affect
an iteration, a refused mutation does not invalidate one, and a
mid-visitor destroy stays the distinct `RoboNotFound` it was.

**The whole-tree audit learned the difference** between a legally detached
subtree (counted once at its root as `detached`, its own links audited, its
descendants marked so they are not miscounted) and an orphan — a node that
claims a parent while nothing reachable lists it, which is exactly the shape a
buggy mutation leaves behind. Without the split, every document holding a
detached node would report an unclean audit, which is how an alarm becomes
noise.

The mutation hook from 8.1 finally has something to report:
`RoboHtmlMutationTreeMutated` fires once per structural change, which is
the seam a future style/layout invalidation listens to. It is not a
MutationObserver — no records, no targets, no subtree options, no microtask.

**Two defects the new suite found, both in the tests' own bookkeeping:**
cycle cases were being refused by the one-parent rule before the cycle rule
could decide, so the fixtures now detach the ancestor first and the refusal is
the rule under test rather than an accident (a wrong expectation that would
have passed for the wrong reason); and a fixture created its siblings under the
wrong parent, which the exact positional assertions caught immediately.

**Definition of done:**

- [x] `appendChild` / `insertBefore` / `removeChild` / `replaceChild` +
      bulk child removal, on a finalized document
- [x] One linking primitive shared with the parser; every structural rule in
      one place
- [x] One-parent invariant; cycle prevention; root immutability; parent-kind rule
- [x] First / middle / last / only child positional correctness, each verified
      by a specific assertion *and* a whole-tree audit
- [x] Detached nodes legal, traversable, re-insertable, still document-owned
- [x] Cross-document mutation refused (`RoboNotSupported`), stale handles miss
- [x] A refused mutation changes nothing, version included
- [x] Traversal invalidation reported with a MUTATION-domain diagnostic
- [x] Audit separates `detached` from `orphans`
- [x] `TREE_MUTATED` hook event; capability reports `mutation = 1`
- [x] Attributes and Unicode text preserved across moves
- [x] `html.mutate.*` (34 cases) + `demo.mutation.changes_picture`
- [x] Visual vertical slice renders before and after a mutation, unchanged
      when nothing is mutated
- [x] Full suite green (1237/1237, Debug and Release)
- [x] No Chromium dependency
- [x] Stop after 8.9

## Goal 8.10 — document lifecycle

The document had a lifecycle in name since 8.1, but one of its states was doing
two jobs: `READY` meant both "parsing finished" and "there is nothing left to
load". 8.10 splits that into the two facts it always was, and names the point a
parse finishes after the web platform's own vocabulary.

**INTERACTIVE is about the tree, COMPLETE is about the input.** Parsing being
done and input being done are different questions. `LOADING → INTERACTIVE` says
parsing finished and the DOM is live; `INTERACTIVE → COMPLETE` says no further
input can arrive. Keeping them apart is what makes the rest of the phase
possible — a rule about *input* can be wrong in ways a rule about the tree
cannot.

**A document cannot be finished loading before it has finished parsing.**
`CREATED → COMPLETE` and `LOADING → COMPLETE` are refused, not merely unused,
and `COMPLETE → INTERACTIVE` does not exist: the intermediate state is the only
way through, and completion is not a state you step back from. That is the one
edge difference that gives the new state its meaning, so it is tested as a
rule rather than left as an implementation detail.

**Completion describes input, not permission.** Reading, traversal, mutation, a
replacement source and reset all keep working after COMPLETE. A document that
stopped being editable at COMPLETE would be a worse state machine, not a
stricter one — and it would have broken every 8.9 guarantee on the most common
document in the runtime, the one that has finished loading.

**Which entry point settles where now follows from that one idea.** A one-shot
load consumes a complete source, so it settles at COMPLETE with no caller help.
A build **truncated at the node ceiling** settles at INTERACTIVE instead,
because the source was abandoned rather than consumed: claiming completion
would be saying "no further input can arrive" about input that was never read.
Detaching abandons input and settles at INTERACTIVE for the same reason. An
empty source has nothing to parse and settles at COMPLETE — through
INTERACTIVE, never by jumping.

**A document with no source stays CREATED.** A tree built through the element
API has no load to settle, and CREATED already permits content, mutation and
traversal; there is deliberately no `CREATED → INTERACTIVE` edge, because every
state above CREATED is a statement about a load. `hand_built_document_stays_created`
states that as a rule with a test rather than leaving it to be discovered as a
surprise.

**A transition is not a rebuild.** After every transition the suite runs the
whole-tree audit and asserts the root is the same node — a state machine that
quietly retired nodes while reporting a new state would pass every state
assertion and still be broken. And because not one node moves, a lifecycle
change **does not move the tree version**, so the 8.8 cursor and walkers keep
working through one instead of reporting an invalidation for a tree that did not
change. A *reload* is a structural change, and 8.9's rules apply to it
unchanged.

**Observability is id-based and closed by default.** `is_loading`,
`is_interactive`, `is_complete`, `is_settled` are each true for exactly their
state and false for an unknown or destroyed id, so a retired id answers "no"
rather than reporting a state it no longer has. Distinguishing "not that state"
from "not a document" is `get_lifecycle`, which is `RoboNotFound` — retired
ids are not probeable, so the query cannot become an oracle for recycled ids.
That is why there is no `is_destroyed(id)`: the honest version of that question
is asked of a lifecycle *value* you already hold (`is_destroyed(life)`), such as
the one a client lifecycle callback receives.

**One real regression, caught by an existing test.** `html.docmodel.metadata_built`
asserted `previous_lifecycle == LOADING` after a build. A one-shot load now
takes two steps, so `previous_lifecycle` is INTERACTIVE — and that is the better
assertion: the recorded previous state is now evidence that both steps
happened rather than one. Nothing was weakened, and the dump test that pinned
`Document\n` was updated to pin `Document lifecycle=complete\n`, which checks
more than it used to.

**Definition of done:**

- [x] Explicit lifecycle: `created`, `loading`, `interactive`, `complete`,
      `failed`, `destroying`, `destroyed`
- [x] Transition graph with no shortcut into completion (`LOADING → COMPLETE`
      and `CREATED → COMPLETE` refused, `COMPLETE → INTERACTIVE` absent)
- [x] Which entry point settles where: one-shot load, truncated build, detach,
      parser detach, empty source, reset, explicit marks
- [x] Completion never closes the DOM; content and source attach accept
      INTERACTIVE and COMPLETE
- [x] Id-based predicates (each exactly one state, closed by default) and
      value-based classifiers
- [x] A transition never changes the tree: audit clean, same root, same version
- [x] Destroy retires the id; stale handles report `RoboNotFound`; repeated
      destroy is `RoboNotFound`, never a double free
- [x] Reset settles an abandoning document at COMPLETE, a fresh one at CREATED
- [x] Tree dump reports the real state on the root line
- [x] `html.lifecycle.*` (27 cases) + `demo.lifecycle.advance_keeps_picture`
      (a transition renders an identical picture; a reload does not)
- [x] Unicode, attributes and the visual vertical slice preserved
- [x] Full suite green (1264 tests, Debug); the only failures are the two
      documented foreground-desktop `window.host.*` cases
- [x] No Chromium dependency
- [x] Stop after 8.10

## Goal 8.11 — basic HTML elements

An element has had a *name* since 8.5. A name is bytes and a type is a symbol,
and the difference showed in how many places answered "what is this element?".
The element model kept a 102-row table mapping a name to a coarse role. The
tree builder kept its own 18-name list of void elements. A consumer wanting
anything finer compared a string literal of its own. Three answers to one
question, and they drift. 8.11 replaces all of them with one table.

**A row is the whole answer: name, role, flags.** `RoboHTMLTag.h` names every
element this runtime recognises — the 32 basic elements this goal introduces,
plus the entire vocabulary the role table already knew — and gives each its
canonical spelling, its role and its intrinsic flags (void, root, document
structure, metadata, sectioning, region, heading, list, list item,
text-level). The role table *moved* into those rows rather than sitting beside
them, so `HtmlElementClassify` is now a delegation and a role and a type
cannot disagree about what an element is.

**Identity is derived, never stored.** `HtmlElementTag` is a pure
function of `(namespace, name)`, exactly as the role already was: no payload
field, no arena growth, no cached tag to invalidate when a tree is re-parsed or
mutated, and a name that arrived before this phase resolves exactly as one that
arrives after it. The price is one bounded case-insensitive scan per
inspection — the same shape and the same cost the role lookup already had, and
it runs when an element is inspected rather than once per painted pixel.

**The table is index-addressed, and that is load-bearing.** The enumerators are
dense from 1 and the rows are declared in enum order, so `name`/`class`/`flags`
are `k_tags[tag - 1]` and `is_known` is a range test. `html.tag.table_is_dense`
asserts row order, ids, lowercase spellings, name uniqueness, a defined role
per row and the name round-trip — because if the table and the enum ever drift,
that test is the one that says so, and every other assertion in the phase is
then meaningless rather than misleading.

**The parser now reads two element facts from here, and only two.** Void-ness
and heading-ness are properties of the element *type*: `<br>` has no children
because `br` is void and the parser merely obeys, and "which elements are
headings" has one answer regardless of who asks. Eighteen lines of duplicated
truth were deleted rather than kept in step. Three parser tables deliberately
stayed where they were — the `<p>`-closing start-tag list, the head-content
list and the text-family table — because those are parsing *policy* with
contextual entries (`noscript` is head content in one mode and paragraph
content in another), and moving policy into an identity table would confuse
exactly the two things this phase keeps apart.

**Unknown is a state, not an error.** A custom element, a future element or
any non-HTML-namespace name is `UNKNOWN`, and it is a *fully working* element:
name, attributes, children, text, traversal and mutation all behave, asserted
rather than assumed. Three edges are pinned because each is easy to get wrong:
`is_tag(element, UNKNOWN)` is answerable and is not a shared identity; a
non-element handle (text, document, NULL, stale) satisfies no tag at all,
including UNKNOWN, because "no recognised type" is a statement about an
element; and an undefined enumerator is refused rather than made equivalent to
UNKNOWN, so a mistyped tag cannot silently match every custom element in a
document.

**The element-side question is asked with a symbol.** `is_tag(element,
RoboHtmlTagDiv)` replaces `is_named(element, "div")` where the question is
about *type*: the caller names a symbol, the runtime owns the spelling, and a
misspelling is a compile error instead of a test that quietly returns false.
`is_named` stays and still compares names, because that is a different question
— it is true of a custom element — and silently routing it through the tag
table would make it answer `false` for every name the vocabulary does not know.

**Two design limits, stated rather than discovered.** Element *creation* stays
gated on a finalized document (8.9's boundary), so the element suite exercises
creation on an unfinalized document and mutation by rearranging parsed nodes —
which is also what a consumer does to a loaded page. And the debug dump was not
reformatted: it already names every element by its tag, which *is* its type, so
the phase adds coverage that every supported type appears instead of a new line
format that would invalidate six suites to say the same thing.

The renderer is the one consumer touched. The demo adapter now asks for
`RoboHtmlTagButton` and `RoboHtmlTagTitle` by symbol rather than by
string, and a `<div>` that holds text and nothing else paints as a text block —
without that row the goal's own checkpoint document would silently drop two of
its four body elements. The condition is not a heuristic: a block's text in
the adapter is its whole subtree, so painting a div that *contains* blocks
would draw their text a second time; a div holding elements is therefore
transparent and the walk descends into it.

**Definition of done:**

- [x] Tag identity for every recognised element, read through a handle
- [x] One vocabulary table: canonical name, role and flags per row
- [x] Role table deleted, classify delegated — one definition of an element's role
- [x] Void and heading knowledge moved to the table; parser tables that are
      policy left alone
- [x] 10 predicates (void, root, document structure, metadata, sectioning,
      region, heading, list, list item, text-level), each pinned to its flag
      for every tag
- [x] Identity derived from (namespace, name); nothing added to a payload
- [x] Unknown/custom elements are supported, fully functional, never coerced
- [x] Element-side typed accessors (`element_tag`, `element_is_tag`), with the
      name-vs-type distinction documented and tested
- [x] `HtmlDomCapabilities.element_tags = 1`, reported by the existing
      query and asserted, so a consumer discovers the model the way it
      discovers every other one
- [x] All 32 basic element types round-trip through the parser
- [x] Every type creatable through the element seam (unfinalized document)
- [x] Attributes, text nodes, whitespace (`<pre>`), and Unicode (Arabic,
      Persian, Urdu, Hindi, Bengali, CJK, emoji) preserved on the new elements
- [x] Traversal order, mutation and lifecycle integration on typed elements
- [x] 23 malformed inputs parsed without a crash and with a sound tree
- [x] Debug dump shows the real types, and reflects mutation
- [x] Visual checkpoint renders a typed multi-element document
      (`demo.paint.element_kinds`), pixel-probed and deterministic
- [x] `html.tag.*` (18) + `html.elements.*` (22) + `demo.paint.element_kinds`
- [x] Full suite green (1305 tests, Debug)
- [x] No Chromium dependency
- [x] Stop after 8.11

## Goal 8.12 — images

8.11 gave `img` identity and void behaviour, so the parser was already building
the element. What was missing is everything after that, because an `<img>` is
only interesting on account of the resource its `src` names. 8.12 adds that
resource — and the decoder underneath it, which the runtime did not have at
all.

**The decoder went into RoboGraphics, not RoboHTML.** A document may own an
`<img>`, but what an `<img>` holds is a `GfxImage`; PNG is a container
format, not a document concept, and the same decoder has to serve a screenshot
path, a style sheet, a favicon and a test fixture. Putting it under RoboHTML
would make every future caller depend on the HTML subsystem to read a picture.
It is additive, so the v1 freeze holds: the module minor moves to 0.16.0 and
`RoboGraphicsApiFrozen` stays 1.

**It implements what real files actually use.** PNG at bit depth 8 across
colour types 0/2/3/4/6, DEFLATE in full — stored, fixed-Huffman *and*
dynamic-Huffman blocks — and all five row filters. The 6.13 encoder only ever
writes stored blocks, so a decoder that handled only those would round-trip our
own output perfectly and fail on every PNG from every real encoder. That is a
test that cannot fail for the wrong reason, which is the worst kind, so the
fixtures are generated by `python3 zlib` and hand-built blocks both. Adam7
interlacing and sub-byte bit depths are refused as `RoboNotSupported` rather
than approximated, because "this cannot be decoded here" is a better answer
than a silently wrong picture.

**Hostile metadata is treated as the normal case.** A file's dimensions are
checked against caller policy *and* against integer overflow before a single
byte is allocated, the expected raw scanline size is computed with explicit
overflow checks, and the DEFLATE stream must produce exactly that many bytes —
short or long are both `RoboError`. A 16-byte file claiming 100000×100000
therefore fails in the header parse at zero allocation, which is the property
the security tests pin.

**Bytes arrive through a seam, never through RoboHTML.** 8.1 drew that line
and this phase keeps it: a caller installs a resolver, RoboHTML hands it the
reference and receives bytes *borrowed for the duration of the call*. That one
sentence is why the future network loader is an *implementation of a resolver*
rather than a change to this model — resource ownership never crosses the
boundary, so a resolver may serve bytes from static storage, a stack buffer, an
arena or an mmap with no lifetime contract. The one resolver this phase ships
is an in-memory asset table: it cannot touch the filesystem, so there is no
path to traverse and nothing to sandbox.

**`UNSUPPORTED` and `FAILED` are different answers.** A reference naming a
scheme (`http:`, `file:`, `data:`, `//host`) is refused as UNSUPPORTED — "this
runtime will not fetch that", which is true whether or not the resource exists.
A reference the resolver does not know is FAILED — "the bytes you asked for are
not here". Collapsing the two would make the network phase's work look like a
bug fix.

**Failure is state, not an exception.** A missing asset, undecodable bytes and
a refused scheme all leave the DOM exactly as it was and move the image's
*state*; `HtmlImageCreateDecoded` returns `RoboOk` with a FAILED state
precisely so the object exists to be asked why. The states describe the
resource, not the element — `<img>` with no `src` is a resource in state NONE,
which is valid and not an error, and `HtmlElementImageState` answers it
without needing a resolver, which is what makes it usable from a renderer
handed a document it did not load.

**Declared dimensions are parsed, not trusted.** The HTML attribute rule minus
percentages: optional ASCII whitespace, then digits, nothing else — no `px`, no
signs, no exponent, no overflow. `set` and `valid` are separate flags so
`<img width>`, `<img width="abc">` and `<img>` are three distinguishable
situations, and an over-limit value is reported rather than clamped so a typo
cannot silently resize anything.

**Attachments follow their own refcount, which is why they are not in the
payload arena.** The per-document table is freed as a unit on both `clear_tree`
and `destroy`, but it holds one reference: a caller that retained a resource
keeps a valid image past teardown. The payload arena is freed and re-bumped as
a unit, so a resource does not belong in it.

**The parser needed no change, and the consequence that is easiest to get wrong
is pinned anyway.** `img` is void, so the phase added nothing to the tokenizer
or the tree builder — but the suite still asserts that an `<img>` does not
consume what follows it: in `<div><img …><span>After image</span></div>` the
`span` is a *sibling*, not a child, and document order is
`div, img, span, Text("After image")`.

**Definition of done:**

- [x] PNG decoder: probe / decode_info / decode; colour types 0/2/3/4/6 at bit
      depth 8; DEFLATE stored + fixed + dynamic; all five row filters; IDAT
      across one or many chunks; CRC-32 verified
- [x] Safe refusals: interlaced and sub-byte depth → `RoboNotSupported`;
      JPEG/WebP/AVIF/GIF → `CONTAINER_UNKNOWN`; unknown critical chunk →
      `NOT_SUPPORTED`; malformed/truncated → `RoboError`, never a partial image
- [x] Allocation bounded by declared limits (default 4096 px / 64 MiB) *and* by
      explicit overflow checks, before any pixel buffer exists
- [x] `RoboGraphicsDecode` added to the authoritative build; freeze pin updated
      to state that 0.16.0 is additive and the v1 surface is unchanged
- [x] `RoboHTMLImage.h`: attribute view with `set`/`valid` split, dimension
      parsing, the five resource states, `describe`
- [x] Fetch seam as a borrowed-bytes resolver, plus the in-memory asset table
      and the `UNSUPPORTED` vs `FAILED` scheme rule
- [x] Per-element attachment: resolve / get / detach / state, replacement on
      re-resolve, and document-level release on `clear_tree` and `destroy`
- [x] `<img>` through the parser: identity, four attributes, void behaviour,
      sibling not child after it
- [x] Unicode `alt` (Arabic, Persian, Urdu, Hindi, Bengali, CJK, emoji)
      preserved byte-for-byte
- [x] Traversal and mutation integration; append/insert/remove/replace an `img`
- [x] `HtmlDomCapabilities` reports the feature, so a consumer
      discovers it the way it discovers every other one
- [x] Debug output (`HtmlImageDescribe`) reflects real state
- [x] Renderer: `ROBO_DEMO_BLOCK_IMAGE` paints the real decoded bitmap at one
      image pixel per surface pixel; no fake rectangle
- [x] `graphics.decode.*` (13, so `test-graphics` runs them) + `html.image.*`
      (15) + `demo.image.reaches_framebuffer`
- [x] Full suite green (1334 tests, Debug; 2 documented foreground-desktop
      `window.host.*` cases excluded)
- [x] No Chromium dependency
- [x] Stop after 8.12

## Goal 8.13 — links

8.11 gave `a` identity, so the parser was already building anchors. What was
missing is everything after the element, because an `<a>` is only interesting
on account of what its `href` names. 8.13 adds the name, the resolution and the
activation — and, more importantly, the vocabulary that makes "nothing
happened" impossible to spell "success".

**There is no browser here, so there is no browser security model.**
RoboticelaOS is an application runtime: it has applications and windows, not
origins, cookie jars, tabs or a same-origin policy, and nothing in this model
would use any of them. A document belongs to an application, and navigation is a
request the application's own router takes or does not; what a browser expresses
through cookies and sessions, RoboticelaOS expresses through application
identity and the capability model it already has. So this phase invents no
security model of its own. What it adds is refusal, with a reason.

**The central statement is that a refused scheme never reaches a handler.**
`javascript:`, `data:` and `file:` are refused by policy *before* the seam is
consulted — so an attribute cannot be used to reach a subsystem the document was
never granted, and there is no privileged bypass to forget to check. Two
questions are deliberately separate because they answer different things:
`..._scheme_is_known` says whether the runtime understands a scheme at all,
`..._scheme_is_permitted` says whether it may be handed to a handler. `file` is
known and refused ("we understand what you asked for and we refuse it"); an
unknown scheme is neither.

**And permitted is not reachable.** With no handler installed, a route or an
`https` URI is `UNSUPPORTED`, which is the truth: the stack that would serve it
is a later phase. That is why the outcome vocabulary has **no `NAVIGATED` kind**
and will not have one until something can actually navigate. Every activation
passes through the same order — read and resolve, decide without consulting
anything (no usable `href` → `NO_TARGET`, unparseable → `REJECTED`, a `download`
intent → `DOWNLOAD_PENDING`), resolve a fragment here, refuse an unpermitted
scheme, and only then ask the handler — and `RoboOk` accompanies every outcome,
including the refusals, because they are answers rather than errors.

**A fragment resolves locally, because it needs no I/O.** `#Overview` names an
element in *this* document and finding one is a tree walk, so it is answered
here as `FRAGMENT` or `FRAGMENT_MISSING` rather than delegated: the one
navigation this phase can complete, and it completes it honestly. The lookup is
named for its caller and is explicitly **not** `getElementById` — that API
belongs to the later DOM-query phase, with its own caching question and its own
error surface, and shipping a stub of it here would have to be replaced.

**The outcome owns the target it reports.** That is the only shape that works:
activation resolves the reference itself, so a borrowed target would die at the
end of the call and every reported string would dangle. Instead the target is
handed to the outcome, the convenience pointers borrow from it, and one clear
rule — `HtmlNavigationOutcomeClear` — releases it. An outcome that was
never filled is safe to clear, so "clear what you were given" is valid even on a
failure path.

**Resolution is small, total, and the only one.** One function turns a
reference into a target, against the document's own reference: route (root
relative, relative, `./`, `../` with `..` clamped at the root like a URL),
fragment, external URI, invalid, or none. Failure is a *kind*, not an error,
which is also why resolution is a pure function of its arguments and works with
no document at all — and why activation is the only part that has to care about
document lifetime. There is no second URL parser: the `href` bytes are kept
verbatim beside the resolution, so a caller can always see what the document
actually wrote. No percent-encoding, no path case folding, no IDNA — those
belong to a URL library, and half-implementing them would be a behavioural claim
this phase cannot back.

**`target`, `rel` and `download` are preserved and classified, not
implemented.** `_blank` is a distinct context and does not create anything —
RoboticelaOS has applications and windows, not a tab strip, and collapsing the
request to `CURRENT` would throw away what the document asked for. `rel` is
preserved and answerable as a whole token, never interpreted: `noopener`,
`nofollow` and `external` all change what a browser's network layer does and
mean nothing to a runtime with no network, so inventing consequences would be
inventing behaviour. `download` is an intent, and an activation that would need
to save something says `DOWNLOAD_PENDING` instead of pretending.

**The checkpoint shows a link that completes and a link that refuses.** The
built-in demo document gained two anchors and the `id` one of them targets: the
route link reports `unsupported` with its resolution, and the fragment link
reports `fragment` with the element it found — because a checkpoint that only
showed link-coloured text would prove nothing about navigation. The adapter
paints a link in the brand colour with a hairline rule and hit-tests it, which
is temporary adapter behaviour rather than a style engine: styling a link is
CSS's job in a later phase, and no other part of the runtime may infer a visual
contract from it. Two limits are asserted rather than left to be discovered —
a link nested in a paragraph is painted as part of that run, because the adapter
has no inline flow; and a page built from markup owns the document it built,
because the HTML it was handed is gone and activation needs a tree.

**Definition of done:**

- [x] `RoboHTMLAnchor.h` / `.c`: attribute view (present vs usable), navigation
      target, resolution, scheme policy, fragment lookup, target context, `rel`
      tokens, request / handler / outcome, activation, debug describe
- [x] `a` through the parser: identity, attributes, nested content, sibling not
      child after it, malformed markup
- [x] Resolution matrix: root-relative, relative, `./`, `../` (clamped),
      fragment, route + fragment, absolute URIs, overflow, with and without a
      base reference
- [x] Scheme known vs permitted; `javascript:`, `data:` and `file:` never reach
      a handler; unknown schemes refused
- [x] Fragment activation really finds the `id` it names (`FRAGMENT`), and a
      missing one reports `FRAGMENT_MISSING` rather than nothing
- [x] Outcome vocabulary with no "navigated" kind; `NO_TARGET`, `REJECTED`,
      `UNSUPPORTED` and `DOWNLOAD_PENDING` all reachable and all tested
- [x] `HtmlDomCapabilities` reports the feature
- [x] Mutation: append/insert/remove/replace an anchor, `href` set/removed/
      replaced, `target`/`rel`/`download` mutation, and the picture following it
- [x] Lifecycle: activation while created/loading/interactive/complete, and a
      destroyed document answering `NOT_FOUND` with no invented outcome
- [x] Unicode link text (Arabic, Persian, Urdu, emoji) preserved
- [x] Checkpoint: `ROBO_DEMO_BLOCK_LINK` paints the link distinguishably and
      hit-tests it; the host drives pointer → hit test → activation through the
      real event queue and offscreen, and asserts one fragment and one refusal
- [x] QEMU: the guest hit-tests and activates the same two links and reports
      `ROBO_QEMU_HTML_LINKS_HIT=2`, `_LINK_FRAGMENT=1`, `_LINK_UNSUPPORTED=1`,
      so the model is validated on the real display path and not only on the
      host — `test-qemu-display` passes
- [x] `html.anchor.*` (27) + `demo.link.*` (4); full suite green (1365 tests,
      Debug, no documented exclusions)
- [x] No Chromium dependency (verified by scan and by the build's own
      independence assertion)
- [x] Stop after 8.13

## Goal 8.14 — forms

8.11 gave `form`, `label`, `input`, `button`, `textarea`, `select` and `option`
tag identity, so the parser was already building them before this phase began —
`input` was already void and `<textarea>` was already an RCDATA text element.
What was missing is everything after the element, because a control is only
interesting on account of the *state* its attributes declare. 8.14 adds that
state: the attribute views, the type vocabularies, the relationships, and the
declared/current split that a form reset exists to express.

**A control does not have one value; it has two, and the split is the point.**
`<input value="a">` then `.value = "b"` leaves the attribute saying `a` and the
control saying `b`, because the first is the *default* value the author wrote and
the second is the *current* value the control is holding. A model with a single
field cannot express that, so this phase keeps the override in a per-document
table beside the element that owns it and always reports *which* of the two
answered: `HtmlElementControlValue` returns the bytes plus
`out_is_declared`, `set_control_value` pins a current value, and
`clear_control_value` drops the override so the value tracks the attribute again.
The consequence is worth stating rather than tidying away — for a control that
has not been overridden, rewriting the `value` attribute *does* change the
current value, and for one that has, it does not. That is not an inconsistency;
it is the entire content of the dirty-value flag, and the suites pin both halves.
The same shape applies to checkedness, and the two overrides are independent
because in HTML they are separate dirty flags. Checkedness is modelled for
`<input>` only and only when the effective type is checkable, so
`<input type="text" checked>` honestly has an attribute that is preserved and no
checkedness to report, rather than one misleading fact.

**Nothing is cached, so nothing can go stale.** 8.14.15 asks that a mutation
never leave a stale association, and the answer is that there is nothing to keep
in step: a form owner, a label's target, an option's position and a select's
selected option are all derived on demand from the live tree. Move a control
between forms and its owner changes; aim a `form` attribute at a missing form
and the answer comes from ancestry; retarget a label's `for` and the next call
describes the tree as it is. There is no mutation hook because there is no cache,
and the only stored state is the two explicit overrides, which belong to one node
by identity rather than to a relationship — which is exactly why they come along
when the node moves and die with the tree when it is reset or destroyed.

**`present` is not `usable`, and `NONE` is not `UNKNOWN`.** `<form action>` is
present and unusable, `<form action="">` is present and unusable, and `<form>`
is neither, so `action_usable` is reported rather than inferred from a length. A
missing `method` is `NONE` and a `method="weird"` is `UNKNOWN`, because both
would submit as GET in a browser and that defaulting is a *submission* rule this
phase deliberately does not apply. Reading the wrong element is a refusal rather
than an empty view — asking for a `<div>`'s form attributes is
`RoboNotSupported`, not "a form with nothing to do".

**An unmodelled vocabulary degrades without destroying anything.** An `<input
 type>` this phase does not model classifies as `UNKNOWN` and behaves as `TEXT`;
a `<button type>` does the same towards `SUBMIT`. The element keeps its
attributes, its name and its place in the tree, and the view reports declared,
effective and *recognised* separately, so a caller can tell "the author said
`text`" from "the author said `date` and we treat it as text". `TEXT` and
`SUBMIT` are the zero values of their enums on purpose, so the zero value and the
HTML default agree instead of needing a special case.

**Numbers are parsed, not compared.** `min`/`max`/`step` carry the same
present/valid split the image module's dimensions do, which is what lets a caller
tell `<input min>` from `<input min="abc">` from `<input>`; an invalid number is
*reported* rather than dropped, because a document that says `min="ten"` has said
something and hiding it would hide a real authoring error from the phase that
finally implements validation. Nothing is validated here: `<input min="10"
max="1">` is reported faithfully rather than flagged. `rows`/`cols` are
non-negative integers bounded by a stated maximum, invalid rather than clamped, so
a typo cannot silently resize a control. Boolean attributes are presence tests,
so `<input checked>`, `checked="checked"` and `checked=""` all report 1.

**Textarea, select and option follow the rules that make them mean something.** A
textarea has no `value` attribute — its declared value is its child text — so the
attributes view has no `value` field rather than inventing one. Options are
counted and ordered by a document-order walk of the subtree, so an option nested
in an `<optgroup>` counts and an index always agrees with the count. An option's
value falls back to its own text when there is no `value` attribute, and the
answer says which of the two it gave. The selected option is determined rather
than guessed: the first with `selected`, else the first that is not `disabled`,
else none — a select with no `selected` shows its first selectable option, and
picking a disabled one would be a state a reader could never reach.

**A label and a form owner resolve by a stated rule, not a maintained index.**
A label resolves through a usable `for` (when the element it names is labelable)
and then through the first labelable descendant, reporting which step answered;
a `for` that names something unlabelable is *not* retried as a descendant search,
because the author named something and the honest answer is that it does not
resolve. A control resolves to the form its `form` attribute names, else to the
nearest ancestor form, else to no owner at all — which is a valid state, not an
error. The source is reported because the two rules differ for a later phase: an
owner found by attribute is a deliberate author reference that survives a move,
and one found by ancestry is a positional fact a move changes.

**The checkpoint shows state, which is the one thing a picture cannot fake.** The
demo adapter gained `ROBO_DEMO_BLOCK_FIELD`: `label`, `input`, `textarea` and
`select` share one style because what a caller needs to tell apart is the state,
not the element. Every reported fact is read through the model at build time —
never re-derived from the markup — so the picture is a report of the DOM rather
than a second opinion about it. A caption draws no box while a field draws a
white bordered one; a control whose text is its placeholder, or which is
disabled, is drawn muted while a value is drawn in the text colour; a checked
box draws the brand mark; a select draws a chevron a text field does not. The
document carries a declared value (`value="8"`), two placeholders, a checked
checkbox, a checked radio, a password field with nothing to show and a select on
its second option, and the field heights, box size and row gap are chosen so the
whole checkpoint fits an 800-pixel display — `demo.layout.flow` pins the result,
because a demonstration that ran off the bottom of a real framebuffer would be a
checkpoint in name only.

**Definition of done:**

- [x] `RoboHTMLForm.h` / `.c`: the `<form>`, `<label>`, `<input>`, `<button>`,
      `<textarea>`, `<select>` and `<option>` attribute views; the input and
      button type vocabularies with declared/effective/recognised and their
      degradation; control state (current value and checkedness, read, pinned
      and cleared, with the declared/current split reported); option count,
      index, value fallback and the deterministic selected option; label
      target; form owner and owner source
- [x] The seven elements through the parser: identity, attributes, voidness,
      RCDATA textarea content, nesting, siblings, malformed markup
- [x] `time`, `number`, `checkbox`, `radio`, `password`, `hidden` and the rest of
      the modelled input types; `range`/`date`/`color`/`file` and unknown
      spellings degrade to `text` while keeping identity and attributes
- [x] Declared vs current: the attribute keeps what the author wrote, the
      override is returned as the current value, `out_is_declared` says which
      answered, a textarea's children are its declared value, and setting one
      override never disturbs the other
- [x] Checkedness only where it exists — checkbox and radio — and refused
      everywhere else
- [x] `min`/`max`/`step` and `rows`/`cols` parsed with the present/valid split,
      invalid values reported rather than clamped or dropped
- [x] `present` vs `usable` and `NONE` vs `UNKNOWN` for `action`, `method` and
      `enctype`; a non-form element refused rather than described as empty
- [x] Select/option ordering by document-order walk (including under
      `<optgroup>`), the option value fallback with `out_is_explicit`, and the
      `selected` → first-enabled → none rule
- [x] Label resolution: `for` by id when labelable, else the first labelable
      descendant, else nothing; a non-labelable target reported rather than
      retried; `out_from_attribute` says which step answered
- [x] Form ownership: attribute by id, else nearest ancestor, else none;
      attribute source vs ancestor source reported; non-form-associated elements
      refused
- [x] Mutation: no stale association under any of append / remove / move between
      forms / removed form / changed `for` / restructured options, with the state
      following the node by identity
- [x] Lifecycle: state readable and writable on a parsed (settled) document, no
      override surviving a reset or a destroy, and a destroyed document
      answering without inventing state
- [x] `HtmlDomCapabilities` reports the forms model
- [x] Unicode values, option text and control text preserved verbatim (Arabic,
      Persian, Urdu, CJK, emoji)
- [x] Checkpoint: `ROBO_DEMO_BLOCK_FIELD` reports and paints the state; `html.form.*`
      (25) and `demo.field.*` (4) cover the model, the pixels, the mutation rule
      and the unknown-type degradation
- [x] QEMU: the guest reports the checkpoint's form state
      (`ROBO_QEMU_HTML_FIELDS`, `_FIELDS_STATE`, `_FIELDS_CHECKED`,
      `_FIELDS_DECLARED`, `_FIELDS_PLACEHOLDER`, `_FIELD_OPTIONS`) and
      `test-qemu-display` asserts the values, so the model is validated on the
      real display path and not only on the host
- [x] Full suite green (1394 tests, Debug, no documented exclusions)
- [x] No Chromium dependency (verified by scan and by the build's own
      independence assertion)
- [x] Stop after 8.14

## Goal 8.15 — DOM tests

This phase adds no product behaviour. Every surface it exercises was built by
8.6 through 8.14, and the running system is unchanged by it. What it adds is the
test layer those phases could not write, because a suite written *by* a phase is
positioned to see that phase's surface and not the seams between surfaces.

**Fourteen suites can each be green while the model as a system is wrong.**
`html.attr.*` can prove an attribute changes and `html.image.*` can prove an
image's state follows its `src`, and both can pass while the *first* one stops
moving the second. `html.anchor.*` can prove a fragment resolves and
`html.form.*` can prove a label resolves its `for`, and both can pass while the
two disagree about which element an id names. `html.tree.*` can prove the builder
never puts a child under `<input>` and `html.mutate.*` can prove the mutation API
inserts children into a node of any kind, and both are correct — the invariant
that is *actually* true is that void is a parser rule and not a DOM rule, and no
single-phase suite has a reason to state it. So this phase adds one more suite,
`SuiteRoboHTMLDom.c` (`html.dom.*`, 13 tests), and it is the only one allowed to
be cross-cutting. It is not a second framework: it uses the same
`TestRegister` and the same `RoboCheck*` vocabulary, and its shared
support header is `static inline` helpers rather than a runner, a reporter or a
new assertion language.

**The subject is invariants, not APIs.** Three tests carry most of that weight.
`document_isolation` parses the same bytes into two documents and then checks
every cross-document operation is refused *and* that the document nobody
addressed is byte-identical afterwards — which is what the ownership model
exists for, since identical markup means identical names and identical ids, and
anything resolved by name or id-string across documents would look right on one
and corrupt the other. `id_resolution_agrees` asserts the three consumers of the
single id rule agree, including the case where they agree on *nothing*: a `for`
naming a non-labelable element answers nothing rather than falling back to
ancestry, while a `form` attribute that does not resolve deliberately does fall
back — two different choices, both deliberate, and the suite pins the difference
rather than smoothing it away. `attribute_mutation_sequences` walks set → read →
replace → remove with the dependent state checked at every step, through the
anchor view, the image state and the declared/current control value, including
the case that justifies the declared/current split existing at all: an explicit
control value surviving the removal of the attribute it defaulted from.

The rest of the suite is the matrix around those: a malformed corpus that must
still audit clean and rebuild byte-identically, 200 levels and 300 siblings,
thirteen script families through both the parser and the construction seam and
through all three id consumers (with a one-byte near miss), 24 document
lifetimes with the debug allocator's live count returning exactly to baseline,
and one document that uses every subsystem at once, is mutated, and is torn
down. The capability flags are asserted as they are: `dom_nodes` is checked to
be **0**, because it names a *complete* node DOM surface and this build has one
model per aspect rather than the whole, and `events` is 0 because its phase has
not run. A pass does not get to round either of those up.

**The fixtures parse real HTML, with one exception that is the model's.**
`dom_document_of()` runs the real tokenizer and tree builder, and the lookup and
audit helpers use the runtime's own descendant walk and its own
`HtmlDocumentValidateTree`, so a test cannot pass on hand-built nodes
while the parser integration it claims to test is broken. Construction is the
one case that cannot go through the parser — the construction seam is gated on a
document that has not been finalized, and a parsed document is finalized by
definition — so those tests build a document by hand and are still checked by
the same traversal and the same audit.

**The sanitizer run was the point of the exercise, and it found two real bugs
that predate this phase.** `-Sanitize` already existed in the host build and was
used rather than duplicated. UBSan reported a misaligned payload header on
*every element write*: the payload arena is a bump allocator over a byte cursor,
so a reservation following an odd-length name began at an odd offset, and a
header is four `uint32_t`s — undefined behaviour on x86-64, a fault on stricter
targets. Fixed by aligning the cursor to the header's own required alignment
before each reservation, with the padding unreachable through any offset/length
pair and the reservation still ending exactly at the new `payload_used` so the
in-place tail-growth path is unchanged. UBSan then found `href + begin` on a NULL
`href` — `NULL + 0` is UB even at a zero offset — fixed by keeping an absent
reference NULL. Neither was introduced here; both had been latent since the arena
and the anchor model were written and both are invisible without UBSan.

- [x] One cross-cutting suite (`SuiteRoboHTMLDom.c`, `html.dom.*`, 13 tests)
      reusing the existing framework — no second runner, reporter or assertion
      vocabulary
- [x] Shared fixtures (`RoboHTMLDomTestSupport.h`): parsed-document builders, the
      three lookups over the runtime's own descendant walk, the whole-tree audit,
      truncation-fatal text/dump readers, and one Unicode sample table shared by
      both Unicode tests
- [x] Document isolation: identical bytes in two documents, every cross-document
      operation refused, the unaddressed document verified unchanged
- [x] Elements, attributes, text nodes, traversal and mutation covered at the
      system level through parsed trees, with the whole-tree audit run after
      every mutating case
- [x] The three id consumers asserted to agree, to move together under an `id`
      rename, and to diverge where the model deliberately diverges — with no
      parallel id index introduced for the tests
- [x] Void asserted as a parser rule *and* as a node-kind rule, stated as two
      different facts rather than one conflated one
- [x] Parser: nested, empty, void, comment, malformed, incomplete and Unicode
      markup, and a recovery corpus that must audit clean and rebuild
      deterministically
- [x] Links, images and forms regression-tested through the shared model,
      including anchor view, image state, label→control, control→form and the
      declared/current value and checkedness sequences
- [x] Lifecycle churn: 24 lifetimes balanced against the debug allocator's live
      count, stale handles after destroy, and a second destroy as `NOT_FOUND`
- [x] Unicode: English, French, German, Arabic, Persian, Urdu, Hebrew,
      Devanagari, Bengali, Chinese, Japanese, Korean and emoji, preserved
      verbatim through parse, attributes, text nodes, mutation and all three id
      consumers
- [x] Attribute mutation sequences, deep (200) and wide (300) structure, and one
      document integrating every subsystem
- [x] `dom_nodes` and `events` asserted 0 — the flags report what this build
      does, not what the phase list intends
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer enabled, full
      suite green, **zero findings after the two fixes**
- [x] Two real latent defects fixed rather than suppressed: misaligned payload
      headers in the arena, and `NULL + 0` in anchor reference trimming
- [x] `html/docs/Tests.md` records the suite map, the cross-cutting tests, the
      fixture rule, the focused and full run commands, and the sanitizer result
- [x] `html/README.md` documents the `html.dom.*` filter and the docs list
- [x] Full suite green (1407 tests, Debug, no documented exclusions) — up 13
      from 8.14's 1394, with no existing test weakened or removed
- [x] No Chromium dependency (verified by scan and by the build's own
      independence assertion)
- [x] Stop after 8.15

## Goal 9.1 — CSS architecture

This phase builds the *model* the rest of Phase 9 will be written against, and
deliberately nothing that interprets it. No tokenizer, no parser, no selector
matching, no cascade, no computed style, no layout, no painting — those are
9.2 onward, and the whole point of doing 9.1 first is that each of them can then
land independently, behind an interface that already exists.

**The interesting decisions are the seams, not the structs.** RoboCSS is a
**peer of RoboHTML**, not a client: per `DEPENDENCIES.md` it works on any DOM
tree, so it must not include RoboHTML, and it must not know that layout,
painting, rendering, windowing, graphics or display exist. That is enforced, not
merely stated — `-Verify` now runs `Assert-RoboCSSChromiumIndependence` over
`Include/RoboCSS`, `Source/RoboCSS` and `css/`, rejecting Chromium/Skia/V8 *and*
the higher runtime layers, the same way the RoboHTML scan does. The DOM stays
authoritative for elements, attributes, tree relationships and text; RoboCSS
owner nothing from the tree and stores no second copy of it.

**Syntax is modelled as by-value records, and that has a real cost.** A token, a
value, a declaration, a selector and a rule are inert description with no
external identity, so they are plain records rather than opaque pointers. The
price is one precondition, stated once in `RoboCSSTypes.h` and depended on
everywhere: a record must be **cleared** before any call that may release what
it holds, because `make`/`init`/`append`/`dispose` release *first* and then set,
so reconfiguring never leaks — and therefore an *uninitialized* record is not
safe, since the release step reads fields still holding stack garbage. That is
not theoretical: a `CssValue` local that reached
`CssValueMakeList` uninitialized walked a `count` of stack garbage and
freed a pointer that was never an allocation. The fix was to name the contract
(`RoboCssCleared`, whose all-zero byte pattern is the cleared state *by
design* — every field's zero is its empty value) and then to *test* it rather
than comment it: `css.records.cleared_state` asserts the equivalence field by
field for eleven record types, then asserts each cleared record is a usable
starting point rather than an inert one.

**Two real defects were found and fixed rather than worked around.**
`CssSourceSetString` delegated to `set_memory` and so never produced
`KIND_STRING`, leaving a documented source kind unreachable — the one place a
caller asks for owned bytes it controls the lifetime of. And
`CssStylesheetParse` dereferenced its `out_error` unconditionally while
reporting the not-yet-implemented parse, even though `CssErrorSet`
tolerates a NULL destination and the header promises the record is optional;
AddressSanitizer caught the access at `0x0`. Both were in code written this
phase, and both were fixed at the source, not at the test.

**The parser boundary refuses honestly.** `CssStylesheetParse` answers
`RoboNotSupported`, names Goal 9.3 in the message, leaves the rule list and the
stylesheet state untouched, and *records the reason on the stylesheet* so a
caller that did not ask for the error still sees it. There is no stub that
pretends to tokenize or parse. *(Goal 9.3 replaced this refusal with the real
parse; see the Goal 9.3 record below. This paragraph describes 9.1 as it
shipped.)* `CssGetCapabilities` reports which
subsystems exist, so a caller never has to infer capability from a version
number — and the suites assert the flags as they are rather than as the phase
list intends them.

**The plan's ten-directory layout is deliberately not reproduced.**
`source/ tokenizer/ parser/ syntax/ selector/ declaration/ stylesheet/ cascade/
computed/` would be ten directories holding one file each, against the
repository's own convention of one flat module directory with a `Robo*`-prefixed
translation unit per concern and an explicit, readable source list in the build.
The separation of concerns the plan asks for lives in sixteen public headers —
one per model, plus the umbrella `RoboCSS.h` — which is where a reader actually
looks for it.

- [x] Module boundaries for every later stage: source, token, value, property,
      declaration, selector, rule, stylesheet, parser cursor, computed style
- [x] Source model: borrowed string, borrowed memory and owned copy, byte
      offsets, 1-based line/column tracking, half-open ranges that slice the
      source, UTF-8 preservation, and malformed input representable safely
- [x] Full CSS Syntax token vocabulary as an enum, with classification, bracket
      pairing, hash types and range binding — but no tokenizer
- [x] Component-value model (`token`/`function`/`block`/`list`) with move-in
      children and recursive ownership — but no value parser
- [x] Declaration model: name, value, `!important`, source ranges, and a
      `NONE`/`PARSED`/`INVALID` state — but no property semantics
- [x] Style rules, at-rules and nested-rule storage; a stylesheet with origin,
      state, attached source, rule storage, rule traversal and module slots
- [x] Selector architecture: simple (type/class/id/attribute/pseudo/universal),
      compound, complex with combinators, and lists — but no matching and no
      specificity
- [x] Property boundary: a property table mapping name → stable per-table
      identity, plus id → parser/computer hook registration, so later phases
      register rather than hard-code a property database
- [x] Error model distinct from `Result`, with `recoverable` vs `fatal`
      and a non-fatal default for malformed source — nothing crashes on bad CSS
- [x] DOM integration boundary (an element view and a style context) and the
      inline-style boundary — the DOM keeps the `style` attribute, RoboCSS is
      handed its text — with no resolution implemented and no second attribute
      store
- [x] Default/user-agent stylesheet boundary and a stylesheet-origin model
      capable of distinguishing application, inline and runtime sources, with no
      built-in stylesheet and no cascade priority assigned
- [x] Parser pipeline interface defined end to end (source → tokenizer → parser →
      stylesheet) with a real, deterministic, compilable skeleton that refuses
      rather than fakes
- [x] Deterministic lifetimes throughout: static bounded module slots, monotonic
      never-reused stylesheet ids, borrowed slices never freed, and sanitizer
      runs where supported
- [x] Nine suites, 88 tests, reusing the existing framework — no second runner,
      reporter or assertion vocabulary
- [x] Tests: source (kinds, ownership, reconfiguration, argument rejection,
      labels, LF/CRLF/lone-CR positions, ranges, Unicode preservation), token,
      value, declaration, selector, rule, stylesheet, style context, robustness
- [x] Fixtures for later phases as source data only, plus one shared Unicode
      sample table so two tests cannot disagree about what "Arabic" means
- [x] Unicode source preserved verbatim through every model: Arabic, Persian,
      Urdu, Hebrew, Devanagari, Bengali, Chinese, Japanese, Korean and emoji
- [x] Robustness: empty, whitespace-only, large, incomplete, malformed and
      Unicode source, repeated creation/destruction, deep rule nesting, wide
      structures and deep value trees — memory-safe in every case
- [x] Build integration: production sources, test sources, module registration
      and a `css` component in the pipeline's runtime component table, so
      `scripts/robo test-css` is a real focused command — no competing build
- [x] Chromium-independence asserted by the build itself, extended to reject any
      higher runtime layer as well
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer enabled, full
      suite green, **zero findings after the two fixes**
- [x] Phases 6–8 regression-tested: full suite green (1495 tests, Debug, no
      documented exclusions) — up 88 from 8.15's 1407, with no existing test
      weakened or removed and no existing HTML/DOM behaviour changed
- [x] The visual vertical slice still works end to end on the host and in QEMU,
      and CSS is isolated from rendering — nothing in the rendering path
      consults it yet
- [x] `css/README.md`, `css/docs/Architecture.md` and `css/docs/Tests.md`
      document the module boundaries, ownership and lifetime rules, every model,
      the DOM/inline/default-stylesheet boundaries, the fixture rule and how to
      run the focused and full suites
- [x] No Chromium dependency introduced
- [x] Stop after 9.1

## Goal 9.2 — CSS tokenizer

9.1 shipped the record; 9.2 ships the machine that fills it. This is the CSS
Syntax state machine — whitespace, comments, CDO/CDC, identifiers, functions,
at-keywords, hashes, strings, urls, numbers, percentages, dimensions,
punctuation and EOF — written independently, with no Chromium code or tables.
Nothing downstream of it exists: this reads bytes, it does not interpret them.

**The tokenizer is a cursor the caller owns, and it allocates nothing.**
RoboHTML's tokenizer is created and destroyed because it retains a bounded
diagnostic list; RoboCSS's is a by-value record with `init`/`dispose`/`reset`,
exactly like `CssParser` — the thing it will be embedded in. So `init`
cannot fail for an allocation reason, nothing leaks, and a tokenizer is as cheap
to put on the stack as to keep in a parser. That is not a micro-optimisation: it
is what makes "does this style sheet tokenize" answerable without a lifetime
ceremony, and it is why the whole subsystem's memory-safety surface is two
`memcmp`s and a bound check.

**The caller's bytes are never rewritten, and that is the decision that
distinguishes the rest.** The specification's input preprocessing maps CR, CRLF
and FF to LF and NUL to U+FFFD; this build does not, because a token's range is a
byte range into the caller's own buffer and rewriting would make that range a
lie. The same effect is reached by *classification* — CR/LF/FF are read as one
newline, a hex escape's trailing whitespace swallows a whole CRLF pair, and a
newline inside a string makes it a `BAD_STRING` rather than being replaced.
Exactly one case classification cannot substitute: NUL, which preprocessing
would have made an identifier character. Reporting the delimiter that is
actually there beats claiming a value the bytes do not contain, and the suite
pins that choice rather than leaving it to drift. Ill-formed UTF-8 is likewise
preserved and reported as an `INVALID` token one byte at a time — the same
"one ill-formed position" rule the source model uses — never repaired and never
silently folded into a neighbouring name.

**A token is raw; its value is computed.** The tokenizer fills every field of
the 9.1 record — numeric value, representation, unit, hash classification — but
leaves `text` as the raw bytes, so `text` and `range` can never disagree. The
resolved value (escapes applied, delimiters stripped) is reached on demand
through the `robo_css_token_value_*` accessors, which work from `text` alone. The
payoff is worth stating plainly: no token points into scratch storage, none is
invalidated by asking for the next one, and a token can be copied, stored and
compared for as long as its source lives.

**Four real defects were found and fixed at the source, not at the test.** The
largest was a rule split across two layers: the scanner swallowed the whitespace
that terminates a hexadecimal escape (`\41 b` is the ident `Ab`), but the *value*
accessor did not re-apply it, so the accessor returned `A b` for a token whose
representation said `Ab`. The same class of drift hit line continuations in
strings, where the accessor wrote both the backslash and the newline into the
resolved text. Both are now tested against each other so the two layers cannot
diverge again. Two more were a wrong comparison in a value predicate and a
framework ceiling — the expectation log is bounded, so a several-thousand-token
audit failed by overflowing the reporter instead of by reporting; that suite now
streams. And one finding was a *contract violation in the new suites
themselves*: records reaching `dispose`/`copy` without being `RoboCssCleared`
first, which surfaced as an invalid-free rather than a failed assertion — the
exact failure mode 9.1's cleared-record contract exists to prevent, and the
reason it gained a regression test in the tokenizer suite.

**Malformed CSS is a value, not a failure.** A malformed style sheet is a token
plus a recorded error, never a failed call and never a terminated caller. The
first problem wins and later ones do not overwrite it, matching the parser
cursor's rule. EOF is a real token with an empty range at the end offset;
asking again returns that same token without advancing or touching the source, so
a loop that terminates only on EOF terminates on a value.

- [x] `CssTokenizer` as a real state machine over `CssSource`,
      emitting the 9.1 token record — no second token model
- [x] Cursor API: `init`/`dispose`/`reset`, `next`/`peek`/`skip`/`skip_trivia`,
      `at_end`/`offset`/`remaining`/`has_peek`/`tokens_produced`/`position`
- [x] One-token lookahead shared by `peek` and `skip_trivia`, so peeking twice
      reads the same token rather than tokenizing it twice — and trivia skipping
      is opt-out, because trivia is preserved by default
- [x] Whitespace runs, comments (empty, multiline, Unicode, unterminated), and
      CDO/CDC as their own tokens
- [x] Identifiers (hyphenated, underscore, `--custom-property`, non-ASCII,
      escaped), functions, at-keywords, hashes (`HASH_ID` vs
      `HASH_UNRESTRICTED`)
- [x] Strings (both quotes, escapes, hex escapes, line continuations, Unicode,
      empty, malformed) and urls (bare, quoted, escaped, malformed) as `URL` /
      `BAD_URL` with `url(` and `)` excluded from the value
- [x] Numbers, percentages and dimensions preserving both components (the
      numeric value and the unit identifier), with no unit semantics
- [x] Delimiters, punctuation and brackets, with matching-open/close pairing
- [x] CSS escapes: hex escapes with their terminator whitespace, escaped
      punctuation, escaped code points, and safe handling of invalid escapes
- [x] Accurate source ranges on every token — including that a whitespace run is
      one token, a comment's range includes its delimiters, a function's includes
      the `(`, and a url's includes `url(` and `)`
- [x] Unicode preserved verbatim through tokenization: Arabic, Persian, Urdu,
      Hebrew, Devanagari, Bengali, Chinese, Japanese, Korean and emoji
- [x] LF/CR/CRLF/form-feed newline handling with deterministic positions
- [x] Deterministic, non-fatal malformed-input behaviour with no crash, no
      out-of-bounds read and no source truncation
- [x] Five new suites, 63 tests: stream/lifetime, core classes plus the two ends
      of the byte spectrum, identifiers/functions/at-keywords/hashes/urls,
      strings/numbers/percentages/dimensions, and whole-snippet sequences
- [x] Tests assert token types, values and ranges — not merely that a function
      returned without crashing
- [x] Fixtures and helpers reused from the 9.1 support header; no second runner,
      reporter or assertion vocabulary
- [x] Build integration through the existing component table and source lists
- [x] Capability flag `tokenizer` flipped to 1 because the thing it names now
      works end to end; every other behaviour flag still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer enabled over the
      whole suite, **zero findings** after the fixes
- [x] Phases 6–9.1 regression-tested: full suite green (1558 tests, Debug, no
      documented exclusions) — up 63 from 9.1's 1495, with no existing test
      weakened or removed and no HTML/DOM behaviour changed
- [x] The visual vertical slice still works end to end on the host and in QEMU;
      tokenization is not consulted by the rendering path
- [x] `css/docs/Tokenizer.md` added, and `css/README.md`, `css/docs/Tests.md` and
      `css/docs/Architecture.md` updated for the tokenizer
- [x] No Chromium dependency introduced
- [x] Stop after 9.2

## Goal 9.3 — CSS parser

9.1 built the record, 9.2 built the machine that fills it, 9.3 builds the grammar
that drives it. This is the CSS Syntax *structure* rules — a stylesheet, a rule
list, an at-rule, a qualified rule, a declaration block, a declaration and
component values — written independently, with no Chromium code or tables.
`CssStylesheetParse` stopped answering `RoboNotSupported` and became a
real parse.

**The grammar lexes nothing.** Every byte it sees arrives as a 9.2 token, and
every token it keeps is that same record — same type, same range, same hash
classification, same dimension unit, same numeric payload. There is no path from
the parser to the source bytes except through a token. That is asserted rather
than asserted-to-be-true: `css.parse.integration.values_are_the_tokenizers_tokens`
re-lexes the same source independently, walks the parse to its leaves, and
requires every leaf to match a token at the same range with the same
classification — and separately checks that `10px`'s number came across as 10 and
its unit as `px`. A parser that scanned bytes itself, or trimmed a token's range,
fails there and nowhere else.

**Two doors, one grammar.** `CssStylesheetParse` (a sheet's source → its
rules) and `CssDeclarationListParseText` (a caller's bytes → a
declaration list) both call `grammar_consume_declaration_block`, so a `style`
attribute and the inside of a `{ … }` cannot drift apart — the 9.1 inline-style
boundary was filled without a second implementation.

**The call's outcome and the CSS's problems are different things.** The return
value is `RoboOk` whenever a model was built, *including* one built by
recovering, because CSS error recovery is the normal case and the rules that
survived are real rules. `out_error` is always filled with the *first* problem,
or a record of kind `NONE` when the input was clean; the same record is kept on
the sheet so a call site that did not want it still sees it. State is `PARSED`
for a recovered parse and `FAILED` only when no usable stylesheet could be
produced at all. And the parse is **transactional**: it builds into its own rule
list and only a successful parse replaces the sheet's rules, so parsing twice is
idempotent rather than additive.

**Nesting is bounded, and past the bound a construct is flattened rather than
abandoned.** `RoboCssMaxNesting` caps both recursions. The over-deep
construct's tokens are still consumed so the parse resumes in the right place,
the rule or at-rule is still *produced*, and only its contents are dropped. A
hostile sheet therefore costs bounded stack and — the property the test actually
pins — cannot cost the rules that follow it. `css.parse.recover.rule_nesting_bound`
builds a chain past the bound and asserts both that the chain is still there and
that the ordinary rule after it survived.

**One place the grammar speculates, and it says so.** A block's contents are
rules for some at-rules and declarations for others, and an at-rule this build has
never heard of could be either. So the block is read as a rule list first — a rule
carries more information — and when that yields nothing the same bytes are read
again as declarations, on a **tokenizer of its own over the same source**, so no
range has to be adjusted afterwards. The guess can be wrong in the direction of
complaining about legal input (`@font-face { font-family: X }`), so the grammar
saves its error state around the guess and restores it when the second reading
succeeds: the first reading's complaint described this build's guess rather than
the stylesheet.

- [x] `CssGrammarParseStylesheet` and `CssGrammarParseDeclarations`
      behind the two public entry points; one grammar, two doors
- [x] Rule lists, style rules, at-rules (statement and block), declaration blocks,
      declarations and component values — with no interpretation anywhere
- [x] A rule's prelude is component values, so `selectors` stays empty for 9.4;
      `p, .a > b { … }` is nine component values and no combinator
- [x] `prelude_values` and `block_closed` added to the 9.1 rule model, so a prelude
      has structure and an unclosed block is a fact rather than a missing `}`
- [x] `!important` recognised, recorded, and *removed from the value* — finding it
      is what removes it, so no value carries a stray flag
- [x] Last-declaration-without-`;`, repeated semicolons, whitespace around `:`,
      `;` and `{`, and interior whitespace preserved as component values
- [x] Custom properties keep their name and their raw value, including `{ a: b }`
      and a bare `,` — values that are not values anywhere else
- [x] Malformed declarations are **kept and marked** (`state = INVALID`,
      `is_usable() == 0`) rather than dropped, so the count reflects what the
      author wrote; a malformed rule is dropped with the reason recorded
- [x] Two recovery boundaries, both the specification's: skip to the `;`, the
      balanced `{ … }`, or the enclosing `}` — which is deliberately *not*
      consumed, because eating it would end the enclosing block one declaration
      early
- [x] Nested rule and nested at-rule inside a style block are recognised as CSS
      Nesting, consumed whole and skipped, so a nested `color: blue` never becomes
      its parent's declaration
- [x] Accurate byte ranges for rules, at-rules, preludes, blocks, declarations,
      names, values and component values — all inside the source, asserted for a
      fixture containing a custom property, a `calc()` and an attribute selector
- [x] Unicode preserved verbatim through the parse: preludes, string values,
      property names, custom properties, comments and at-rule preludes, over the
      thirteen-script corpus, with byte-offset arithmetic asserted separately
      from the bytes so a character-counting bug cannot hide behind correct text
- [x] Five new suites, 57 tests (`css.parse.*` is 59 with 9.1's two cursor cases)
- [x] Existing 9.1/9.2 suites updated only where they asserted the *old* refusal:
      the parse-boundary and declaration-boundary tests now assert the real parse,
      and the sheet/robust tests were corrected rather than weakened
- [x] Build integration through the existing source and component tables
- [x] Capability flags `parser` and `inline_styles` flipped to 1 because the things
      they name now work end to end; every other behaviour flag still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; `--filter=css` is 208/208 under the sanitized
      binary
- [x] Phases 6–9.2 regression-tested: full suite green (1615 tests, Debug, plain
      build 3 runs in a row) — up 57 from 9.2's 1558, with no existing test
      weakened or removed and no HTML/DOM behaviour changed
- [x] The visual vertical slice still works end to end on the host and in QEMU;
      the parser is not consulted by the rendering path
- [x] `css/docs/Parser.md` added, and `css/README.md`, `css/docs/Tests.md` and
      `css/docs/Architecture.md` updated for the parser
- [x] No Chromium dependency introduced
- [x] Stop after 9.3

### What 9.3 found

No product defect, and that is worth recording precisely. Three of the new
suites' expectations were wrong and the implementation was right each time: `var(--gutter, 20px)` has four children and not three (the comma and the space are
component values until something reads them as separators); `a[1]` is contiguous,
so `x: a[1] (2) [3` is six children with the blocks at 1, 3 and 5; and a block's
range begins at the `{`, one byte after the space that separates it from the
prelude. A fourth was a property of the corpus: four of the thirteen samples are
two words, and a property name is one identifier, so the name test uses each
sample's first word.

The one crash was a **build hazard, not a source defect**: `RoboCSSRule.h` had
grown `prelude_values` and `block_closed`, and the build reused objects compiled
against the old layout. A clean rebuild fixed it, and the 9.1 tests that had
called `strstr` on a now-`NULL` message were then updated to assert the real
parse instead of a refusal that no longer exists.

## Goal 9.4 — CSS selectors

9.1 built the selector *records*, 9.3 deliberately left every rule's `selectors`
empty and kept the prelude as component values so that "has this prelude been
understood yet" would be answerable by asking. 9.4 is the thing that answers it:
a selector grammar over those records, and a matcher that says whether one element
satisfies one selector. Written independently, with no Chromium code or tables.

**The grammar lexes nothing.** Every byte arrives as a 9.2 token and every name it
records is that token's **value** — the tokenizer's own escape rule, already
applied — interned once into the list's name arena. So `.foo\:bar` carries the six
bytes `foo:bar` and `#item\31 23` carries `item23`, and a matcher never has to know
that escapes exist, because both sides compare resolved bytes. There is no second
escape implementation, which is the point: 9.2 has one.

**All-or-nothing, as CSS says.** One bad selector in a list makes the whole list
invalid — the rule that makes `:is()` worth having later. `out` is initialised
first (the 9.3 grammar's rule) and `out_error` is written on success too, so a
caller that never checks a result code still cannot be handed a record describing
a previous call. Nothing fails the process, allocates unboundedly or recurses
unboundedly: `:not()` nesting is capped by `RoboCssMaxSelectorNesting` (8) and
past the cap the selector is **rejected** rather than parsed more deeply.

**The matcher holds no DOM, no document, no cache.** It asks a *host* — the four
callbacks in `RoboCSSStyle.h` — about one element at a time, which is what keeps
RoboCSS a peer of RoboHTML rather than a client: nothing in `Source/RoboCSS/`
includes a RoboHTML header, and the DOM integration suite is what makes the real
tree visible one element at a time.

**Three outcomes, not two.** `matched` and `unsupported` are independent and both
can be set. A host that supplies only a view can answer type, universal, class, id
and attribute selectors perfectly well and cannot answer `>` or `+` at all — so
`h1 + p` against such a host is *undecided*, not false. The distinction is
load-bearing in both directions: an absent callback and an answer of `NONE` both
yield no element, and reading the first as the second would silently turn every
`+` and `>` selector into a non-match, while `:first-child` *is* "no previous
sibling". Every caller that would read the answer as a *fact* goes through
`match_relation_answer`, which refuses the answer when the relation was
unavailable.

**Right-most compound first, with backtracking.** A compound is evaluated against
one view; a complex selector starts at its right-most compound — the one the
candidate must be — and walks leftwards through the combinators. Backtracking is
not an optimisation: `a b c` must be able to reject an ancestor `b` and try a
higher one, and `.x .y .z` can match through either of two `.y`s. The recursion is
bounded by the number of **compounds in the selector**, never by the tree, so the
stack cost is a property of the stylesheet rather than of the document; and a host
whose parent links form a cycle stops after `MATCH_WALK_LIMIT` steps with
`unsupported = 1` rather than answering wrongly or hanging.

**Supported exactly, and no more.** Type, universal, class, id, the six attribute
comparators plus existence (with the `i`/`s` case flags), the four combinators, and
`:root`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`,
`:nth-last-child()`, `:not()`. Sibling counting is **element** counting, so a text
node or a comment between two elements neither hides a sibling nor counts as one.
Pseudo-elements are parsed and distinguished from pseudo-classes, including the
four legacy one-colon spellings. Everything outside that set is *reported* rather
than guessed at, and two boundaries are deliberate rather than gaps: `:hover` and
its state-dependent relatives parse and are `unsupported`, while a pseudo-element
matches no element **decidedly**, because an element is not its own `::before`.

### Definition of done

- [x] Selector grammar over the 9.2 tokenizer: type, universal, id, class,
      attribute (existence plus all six comparators, quoted and identifier values,
      `i`/`s` flags), all four combinators, compounds, complex selectors, comma
      lists, escapes resolved through the tokenizer (`\:`, hex escapes, leading
      digits), pseudo-classes and pseudo-elements with the legacy one-colon
      spellings
- [x] The seven structural pseudo-classes parse, and `:not()` takes a real
      selector list rather than a re-parsed string, with an explicit nesting bound
- [x] An+B parsed once (`odd`, `even`, integers, `n`, `-n`, `+n`, signed offsets,
      whitespace only where the microsyntax allows it) and matched against a
      1-based element index
- [x] `CssStyleRuleParseSelectors` closes 9.3's open question and is the
      **only** way a rule's `selectors` is filled — replaces on success, so calling
      it twice is idempotent, and leaves an empty list on failure
- [x] The element host seam (`CssElementHost`): one userdata, four
      callbacks, `view` the only required one
- [x] Four matcher entry points, one per structural level, each independently
      testable and each exercised directly
- [x] A relation the host cannot answer is *reported*, never guessed
- [x] `matched` / `unsupported` / `condition` / `selector_index` / `range`, with
      `condition` documented as borrowing from the list's arena
- [x] Malformed selectors fail safely with real diagnostics and real positions,
      and a selector list is all-or-nothing
- [x] Source ranges preserved on every selector record
- [x] Six new suites, 111 tests: `css.select.parse` 51, `css.select.match` 34
      (simple 13, combinators 14, structural 7), `css.select.levels` 14, and
      `css.dom` 12 against the real RoboHTML tree
- [x] DOM integration against the **real** DOM: the visual checkpoint's own
      document, text and comment nodes not counting as siblings, deep trees,
      rebuilding after mutation, determinism, and matching not mutating the DOM
- [x] The visual checkpoint is untouched and still renders: running the selectors
      over its document leaves the picture identical
- [x] Build integration through the existing source and component tables
- [x] Capability flag `selector_matching` flipped to 1 because the thing it names
      now works end to end; `cascade`, `inheritance` and `computed_values` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; the plain build is 1726/1726
- [x] Phases 6–9.3 regression-tested: full suite green (1726 tests) — up 111 from
      9.3's 1615, with no existing test weakened or removed and no HTML/DOM
      behaviour changed
- [x] `css/docs/Selectors.md` added, and `css/README.md`, `css/docs/Tests.md` and
      `css/docs/Architecture.md` updated for the selector milestone
- [x] No Chromium dependency introduced
- [x] No cascade, no specificity, no inheritance, no computed values, no layout,
      no painting, no CSSOM, no animations
- [x] Stop after 9.4

### What 9.4 found

**One product defect, caught by UBSan.** A long enough run of digits in
`:nth-child(...)` overflowed the An+B accumulator — `long` is 32-bit on Windows —
reported as `signed integer overflow`. The accumulation is now bounded at
`ANB_VALUE_MAX` inside `anb_digits` and the oversize form is *rejected* rather
than wrapped into a plausible wrong sequence, with the bound test written as a
division so the check itself cannot overflow.

**One gap in the matcher's public surface**, found by asking which functions
anything actually calls rather than by any failing test:
`CssSimpleSelectorMatches`, `CssCompoundSelectorMatches` and
`CssComplexSelectorMatches` are declared as independently testable and
were reached by *nothing* — every existing matcher test went through the list
door. All three could have been broken, or could have disagreed with the list, and
the suite would have stayed green. `SuiteRoboCssMatchLevels.c` now drives each
door directly and then requires the levels to agree across a table of 24 selectors
× 7 elements, which is what would catch a change to the shared walk that moved two
doors in different directions.

**Six test expectations were wrong and the implementation was right.** Four were
wrong about the fixture: two hosts asserted `h1 + p` and `:nth-child(2)` over
elements added without a parent, where the host double derives siblings from the
parent link and two parentless elements are not siblings (the `css_host_seal`
rule, which exists so two roots are not treated as siblings); one passed `NULL`
for the matcher's required output; and one asserted that a cyclic host leaves a
`>` selector undecided, when a `>` chain recurses once per compound and so
terminates on its own — the *descendant* walk is the one that needs the step
limit, and both behaviors are now pinned side by side. Two were wrong about CSS
itself: `[data-x=1]` is not a valid attribute selector (an unquoted value must be
an identifier, and a bare number is not one), and `div + , span` cannot be parsed
as "a list with one broken member", because a selector list is all-or-nothing —
so the list door's skip-an-incoherent-member guard only ever sees hand-built
input, and the suite builds it that way while asserting in the same test that the
parser refuses the text that would have produced it.

## Goal 9.5 — CSS cascade

9.4 answered yes or no about one element and one selector. 9.5 is the first thing
in the subsystem that *applies* anything: it collects everything that matches an
element, in cascade order, and answers which declaration wins for a property.
Written independently, with no Chromium code or tables.

**The cascade has three keys and this phase owns two.** Origin and importance
(the *level*), and the document order of the declarations, are complete here.
Specificity is 9.6, and it is not approximated, not defaulted and not given a
placeholder field. So resolution has **three** outcomes rather than two *(9.6 later supplied specificity and **deleted the third verdict** — see the Goal 9.6 record; the text below describes 9.5 as it shipped)*:
`DECIDED`, `NONE`, and `UNDECIDED_SPECIFICITY` for a contest between candidates of
one level from two different selectors. Settling *that* by document order would be
right only by luck, and wrong silently — the same reason 9.4 reports a relation its
host cannot answer instead of reading it as a negative.

**The level table is written out rather than derived**, because the derivation
that reads as obviously right is not. Two rows are decisions: inline sits above
the origin it belongs to (a `style` attribute is author-level with a specificity
no selector reaches — modelled as a level, which is what makes inline work without
9.6), and the important half is *not* a plain reversal — it reverses the origins
while inline stays directly above author, so an inline `!important` beats an
author `!important` and still loses to a user one.

**Collection reports everything it could not use.** An undecided selector, an
at-rule block nothing evaluates, a rule whose prelude was never parsed as
selectors, a sheet that was destroyed while referenced: each is counted on the
result and each sets `unsupported`, because a cascade that dropped any of them
quietly would apply the wrong rule and look like it had answered. `unsupported == 0`
means the answer is complete — **not** that nothing matched.

**One forward pass is enough, and the verdict can go back.** The array is in
cascade order (ascending level, then document order), so a candidate is at the
current level or above it. A strictly stronger level wins and *re-enables* an
answer that an earlier same-level pair had made undecided — the case a resolver
that remembered its first verdict would get wrong — while same-level rivals from
different selectors make it undecided and keep it there however many same-selector
declarations follow. Two declarations of one selector are comparable because a
specificity is a property of the selector and there is only one of it.

### Definition of done

- [x] The cascade model (`RoboCSSCascade.h`): the eight levels, the candidate with
      its provenance and document position, the collected result with its
      counters, and the three verdicts
- [x] `CssCascadeLevelOf` / `_holds` / `_compare`, the level names, and
      `RoboCssCascadeLevelNone` for `ORIGIN_NONE` and unknown origins
- [x] `CssStyleContextMatchDeclarations` is real: every stylesheet origin
      in the context's order, every matched rule's usable declarations, and the
      element's own `style` attribute, all in cascade order — it was the 9.1
      boundary whose own header said 9.4 and 9.5 would arrive here
- [x] Cascade order maintained by counting sort over the eight levels, stable,
      because two declarations in one rule can sit at different levels and
      document order is the tie-break inside a level
- [x] The level table verified twice: every pair reaching exactly one level and
      every level being reachable, and the eight rows asserted by name so a
      "correct by construction" mistake cannot hide behind a loop
- [x] `CssCascadeResolveName` and `_resolve_property`, with the winner
      written out **only** for `DECIDED`, and `NONE` refused as a property id
      rather than matched
- [x] `robo_css_cascade_candidate_same_provenance` public, so a caller reading an
      undecided verdict can see which pairs were comparable
- [x] The inline boundary exercised from the host side: no attribute vs `style=""`,
      malformed inline CSS recovered and reported, a host that cannot answer marked
      as an incomplete answer rather than as "no inline declarations", and the
      declarations parsed into a list the cascade owns
- [x] Every "could not use this" path counted and tested: undecided selectors, no
      host at all, unparsed rules, at-rules with blocks vs without, destroyed
      sheets, origin mismatches, unusable declarations
- [x] `CssSliceEqualsCi`, the slice-to-slice ASCII fold both the attribute
      view and the cascade need — and it replaced a private duplicate in
      `RoboCSSStyle.c` rather than being added beside one
- [x] `CssStyleContextInlineUserdata`, so a caller (the cascade) can
      actually call the callback the getter hands out
- [x] Four new suites, 32 tests: `css.cascade.levels` 5, `css.cascade.collect` 15,
      `css.cascade.resolve` 12, plus `css.style.match_declarations_empty_context`
      rewritten from the 9.1 refusal to the 9.5 answer
- [x] `css.dom.cascade` 4 against the **real** RoboHTML tree: the document's own
      `style` attribute read through the seam, a collection that needs real parent
      and sibling links to pick the right rules, importance outranking inline, and
      a byte-for-byte proof that collecting a cascade for all 29 checkpoint
      elements leaves the painted frame identical
- [x] Build integration through the existing source and component tables, and the
      `css/tests` include path the 9.4 DOM suite needed (see below)
- [x] Capability flag `cascade` flipped to 1 for exactly what works end to end;
      `inheritance`, `computed_values`, `default_styles` and `stylesheet_loading`
      still 0. Version 0.5.0, `RoboCssApiFrozen` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; the plain build is 1762/1762
- [x] Phases 6–9.4 regression-tested: full suite green (1762 tests) — up 36 from
      9.4's 1726, with no existing test weakened or removed and no HTML/DOM
      behaviour changed
- [x] `css/docs/Cascade.md` added, and `css/README.md`, `css/docs/Tests.md`,
      `css/docs/Architecture.md`, `css/docs/Selectors.md` and `ARCHITECTURE.md`
      updated for the cascade milestone
- [x] No Chromium dependency introduced
- [x] No specificity, no inheritance, no computed values, no colours, no units, no
      box model, no at-rule evaluation, no shorthand expansion, no layout, no
      painting, no CSSOM, no animations
- [x] Stop after 9.5

### What 9.5 found

**One build-integration defect that a green 9.4 claim was hiding.**
`SuiteRoboCssDomSelect.c` lives in `Tests/Suites/` — it is the one suite that must
see RoboCSS and RoboHTML together, and the two are peers whose guards forbid each
other's headers, so it belongs in neither directory — but it includes
`css/tests`' support header and the host compile line never had that directory on
its include path. A suite inside its own test directory resolves the header
relatively; a suite outside one needs `-I`. The 9.4 record reported the whole
suite green, so the claim and the build disagreed; the fix is one line in
`Build.ps1`, and the lesson is that a suite which has never been *compiled* from
the documented command cannot be reported as passing.

**One expectation was wrong about CSS, and the table was right.**
`css.cascade.levels.inline_is_author_level` first asserted that inline loses to a
user rule. It does not: author normal beats user normal, and an inline `style`
attribute is an author declaration with a specificity no selector reaches. The
spelled-out assertions beside the loop exist for exactly this — a table that
returned *a* level for every input would have passed the wrong test.

**Two fixtures disagreed with their own rules.** Three resolution tests asserted
that `div` and `.app` cannot be separated without specificity while the host was
built with the `app` *id*, so `.app` matched nothing, one candidate arrived and the
cascade answered `DECIDED` where the test expected `UNDECIDED_SPECIFICITY`. The
implementation was right both times; the fixtures were not. This is the same
lesson 9.4 learned about its host double, and it is why every cascade test now
adds its elements before the sheet that selects them.

**One thing worth writing down about the sort.** `cascade_order_by_level` counts
and then places over the *same* bucket function, deliberately: a candidate counted
into one bucket and placed from another is the one failure a counting sort has,
and it is invisible — the array would simply be shorter than the collection's own
count, with no error anywhere. The bucket helper is the contract between the two
passes, not an implementation detail.

## Goal 9.6 — CSS specificity

9.5 built an ordering that could place two declarations from one selector but had
to *report* a contest between two declarations from different selectors. 9.6
supplies the missing key, so the cascade is now total: every question it can see,
it answers. Written independently, with no Chromium code or tables.

**Specificity is not a fourth record beside the selector model.** It is three
counts *about* a selector, and the one thing it must never be is a second
description of one — so it is computed by walking the same simple/compound/complex
records the matcher walks, and there is no field on a selector record that could
disagree with a calculation. Nothing is stored, so nothing can go stale and no
cache can be wrong.

**The representation is a tuple, not a packed integer.** `(A, B, C)` — ids, then
classes/attributes/pseudo-classes, then types/pseudo-elements — compared
lexicographically by one exported function. A packed integer has the same
comparison outcome only until a component overflows into its neighbour, at which
point a more specific selector compares as less specific; that ambiguity is not
representable in a tuple. Each count clamps at `RoboCssSpecificityComponentMax`, so
saturation is a stated behaviour rather than a wrap.

**Three rules that are easy to get subtly wrong, and each has a test that would
catch the wrong reading.** The universal selector is zero, so `*.card` is `(0,1,0)`
and not `(0,1,1)`. `:not()` is worth **its argument's** specificity, not one class
component — because `:not()` takes the *most specific* selector in its argument
list, `:not(.a, #x)` is `(1,0,0)`. And a pseudo-element counts toward the type
component while a pseudo-class counts toward the class component, which is exactly
the distinction the matcher already makes to decide whether an element *is* the
pseudo-element or merely satisfies the pseudo-class.

**A rule's selector list is worth the specificity of the member that matched**,
not its first member and not its maximum. `div, .card, #main` is `(0,0,1)` to a
bare `div` and `(1,0,0)` to the element the `#main` member matched; a cascade that
took the list's maximum would let an unrelated member promote a rule over a more
specific one. The specificity is carried on every *candidate*, so it is the
specificity of the declaration's actual reason for being here.

**9.5's third verdict was deleted rather than kept.** `UNDECIDED_SPECIFICITY`
described a question specificity answers, and keeping it would leave a documented
outcome that can never occur — the same failure mode the capability flags exist to
prevent. `RoboCssCascadeVerdictCount` is 2 and the enumerator's name string is
gone with it. This is the one change in the phase that could break a caller, which
is why the API is not frozen.

### Definition of done

- [x] The specificity model (`RoboCSSSpecificity.h`): the three-count record, the
      component constants, `CssSpecificityCompare` / `_equal` / `_is_zero`,
      the clamping add, the four computation doors, and
      `CssSpecificityOfListItem` — the per-member door the cascade uses
      with the index the matcher reported
- [x] `RoboCSSSpecificity.c` computes the counts by walking the 9.4 records at all
      four levels (simple, compound, complex, list) — one implementation, and the
      `levels_agree` test requires the levels to reach the same answer for a wide
      selector table so they cannot drift
- [x] `:not()` worth its argument: the argument's *most specific* selector,
      recursed for nested `:not()`, with `not_chain_depth` pinning the recursion to
      the parser's own nesting bound rather than a second one invented here
- [x] Saturation at `RoboCssSpecificityComponentMax` with a stress selector built from a
      generated `.a.a.a…` chain and a many-id chain, so a wrap could not pass
- [x] Malformed input never reaches a calculation: the parser refuses the text, the
      suite asserts the refusal, and a hand-built incoherent record is refused by
      the calculation door rather than read
- [x] Cascade integration: `RoboCSSCascade.h` carries a `specificity` on every
      candidate, the resolver consults `CssSpecificityCompare` and holds no
      arithmetic of its own, and `ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY`
      is removed
- [x] Ordering unchanged where it should be: level before specificity, specificity
      before document order, `!important` at its own stage, inline still above
      author — each re-asserted with the new key in place
- [x] Two new suites, 30 tests: `css.specificity.*` 20 (the matrix, the simple
      kinds, the attribute operators, pseudo-classes, pseudo-elements, `:not()`,
      compounds and complexes, combinators-are-zero, lists, escaping, saturation,
      stress, level agreement, malformed, the API contract) and
      `css.specificity.cascade.*` 10 (decides, order-independence, source order
      after specificity, level outranks specificity, importance, the
      matching-member rule, `:not()` deciding, a pseudo-element winning nothing,
      inline, determinism)
- [x] `css.dom.specificity.picks_the_winner` against the **real** RoboHTML tree: an
      `id`/`class`/`type` contest over the visual checkpoint's own document
- [x] The 9.5 tests that asserted the placeholder verdict rewritten to assert the
      *decision* and, in the same test, that the decision is about specificity
      rather than order — the rival is written later in both, so a resolver that
      had fallen back to document order would pick the same winner by luck
- [x] Capability flag `specificity` flipped to 1, and `cascade` re-documented as
      complete now that its third key exists; `inheritance`, `computed_values`,
      `default_styles` and `stylesheet_loading` still 0. Version 0.6.0,
      `RoboCssApiFrozen` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; the plain build is 1793/1793 and `css.` is 386/386
- [x] Phases 6–9.5 regression-tested: full suite green (1793 tests) — up 31 from
      9.5's 1762, with no existing test weakened or removed and no HTML/DOM or
      selector behaviour changed
- [x] `css/docs/Specificity.md` added, and `css/README.md`, `css/docs/Tests.md`,
      `css/docs/Cascade.md`, `css/docs/Architecture.md`, `css/docs/Selectors.md`,
      `css/docs/Parser.md` and `ARCHITECTURE.md` updated for the milestone
- [x] No Chromium dependency introduced
- [x] No inheritance, no computed values, no colours, no units, no box model, no
      at-rule evaluation, no shorthand expansion, no layout, no painting, no
      CSSOM, no animations
- [x] Stop after 9.6

### What 9.6 found

**No product defect and no sanitizer finding.** What the phase found was one
public-API change that had to be made rather than tested, and one expectation
written before the implementation that CSS disagrees with.

**The third verdict had to be deleted, and that is the riskiest thing here.** 9.5
shipped `ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY` and its name, and the 9.5
tests asserted both. 9.6 makes the cascade total, so the verdict describes nothing
and is gone: `RoboCssCascadeVerdictCount` is 2, a value that used to name the
third verdict is no longer known, and its name string is gone with it. The
alternative — keep the dead verdict so nothing has to change — would leave a
documented outcome that can never occur.

**`:not(:not(.a)):not(#x)` is `(1,1,0)`, not `(1,0,0)`.** The matrix was written
before the code, and this row was copied from the simpler `:not(:not(#x))` =
`(1,0,0)` without thinking the two `:not()`s through separately: the outer one is
worth *its argument's whole specificity*, so `:not(:not(.a))` is `(0,1,0)` and the
`:not(#x)` beside it adds the `(1,0,0)`. The implementation was right and the
expectation was wrong. The neighbouring rows (`:not(#x)` and `:not(:not(#x))`, both
`(1,0,0)`) exist so that nesting is pinned as "the argument's specificity, however
deep" rather than by the one case that would pass under either reading.

**Two tests had to say something different, not something weaker.**
`css.cascade.resolve.same_level_rivals_are_*` and
`css.cascade.collect.two_sheets_in_one_origin` both asserted the 9.5 undecided
answer. They now assert the *decision* and, in the same test, that the decision is
about specificity rather than order — the rival selector is written *later* in both
cases, so a resolver that had simply fallen back to document order would have
picked the same winner and the test would have proved nothing. The collect case
goes further and asserts `CssSpecificityEqual` on the two candidates before
asserting the winner, so "this was decided by order" is a fact the test establishes
rather than an assumption it leans on.

## Goal 9.7 — CSS inheritance

**Status: complete.** The property registry, the resolved-style model, the
resolution, the CSS-wide keywords and the ancestor walk are implemented, tested
against the real RoboHTML DOM, and verified under sanitizers.

### Delivered

- [x] `Include/RoboCSS/RoboCSSPropertyRegistry.h` + `Source/RoboCSS/RoboCSSPropertyRegistry.c`
      — the authoritative metadata: does a property inherit by default, and what is
      its initial value. Initial values are stored as text **and** as the parsed
      component-value tree, built once at registration through the same grammar a
      declaration goes through; a text that does not parse is refused at
      registration. The standard set is the sixteen inherited properties these
      phases know (colour, the font and text properties) and the common
      non-inherited ones. Deliberately *not* a second property table: no ids, no
      per-caller state.
- [x] `CssPropertyRegistryApplyToTable` — the one supported way to build
      a table, setting `RoboCssPropertyFlagInherited` from the classification,
      so the flag and the classification cannot drift
- [x] `Include/RoboCSS/RoboCSSInherit.h` + `Source/RoboCSS/RoboCSSInherit.c` — the
      resolved style: one owned entry per property the element has an answer for,
      each carrying its provenance (`DECLARED`/`PARENT`/`DEFAULT`/`DEFERRED`) and
      the CSS-wide keyword that produced it, plus the counters that stop the result
      being read as more complete than it is
- [x] `RoboCssInheritResolve` — the primitive, and the only door that does any
      work. It takes the parent as an **argument** and walks three sets in order:
      the element's own cascade winners (through the cascade's own resolver), the
      inherited-by-default properties the parent carries, then the registry's
      initial values for the rest. A resolution is therefore a pure function of its
      inputs, with nothing cached and nothing global to invalidate
- [x] The CSS-wide keywords obeyed on the parsed value and never on a string:
      `inherit`, `initial`, `unset`; `revert` recognised, carried as a *deferred*
      entry and **reported** rather than half-obeyed, because obeying it means
      re-running a cascade with one origin removed
- [x] Custom properties: inherit always, compared byte-exact, and carrying no
      initial value (an unset one is invalid, not `initial`)
- [x] `CssStyleContextResolveInheritedStyle` — the chain door: walk up
      through the host's `parent`, resolve **top down** so each level's parent is
      the *immediate resolved* parent, bounded at `RoboCssInheritMaxDepth` and
      reported when the bound is hit (a cycle is caught by the same bound)
- [x] `CssStyleContextSetPropertyRegistry` / `_property_registry` and the
      registry-aware classification path, so the table and the registry are both
      borrowed from the caller
- [x] `CssValueCopy` — a deep, transactional copy, the primitive inheritance
      needed and no part of the subsystem had
- [x] Three new suites, 37 tests: `css.registry.*` 11 (lifecycle, the standard set,
      classification, initial values, custom-property naming, lookup, registration,
      definition stability, apply-to-table, the name tables, and a leak check),
      `css.inherit.*` 6 (the keyword table and its negatives, the name tables, the
      resolved style's lifecycle, the deep value copy and its balance) and
      `css.inherit.resolve.*` + `css.inherit.chain.*` 20 (declared, inherited,
      non-inherited, child and intermediate overrides, root behaviour, `inherit`,
      `initial`, `unset`, `revert`, custom properties, an unknown property, inline,
      an incomplete cascade, counters and determinism, a leak check, and the walk)
- [x] Capability flag `inheritance` flipped to 1; `computed_values`,
      `default_styles` and `stylesheet_loading` still 0. Version 0.7.0,
      `RoboCssApiFrozen` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; the plain build is 1832/1832 and `css.` is 423/423
- [x] QEMU display validation passes; `scripts/robo check` passes with the runtime
      sources, headers and build wiring **CLEAN** for Chromium independence
- [x] Phases 6–9.6 regression-tested: full suite green — up 37 CSS tests from 9.6,
      with no existing test weakened or removed and no HTML/DOM, selector, cascade
      or specificity behaviour changed
- [x] `css/docs/Inheritance.md` added, and `css/README.md`, `css/docs/Tests.md`,
      `css/docs/Architecture.md` and `ARCHITECTURE.md` updated for the milestone
- [x] No Chromium dependency introduced
- [x] No computed values, no colours, no units, no `calc()`, no `var()
      substitution, no box model, no layout, no painting, no CSSOM, no animations
- [x] Stop after 9.7

### What 9.7 found

**One real product defect, caught by the sanitizer run and not by the plain one.**
`CssPropertyRegistryCreateStandard` wrote `*out_registry` without checking
it was non-NULL, so `create_standard(NULL, …)` — the argument contract every other
door honours — crashed instead of returning `RoboInvalidArgument`. The plain
build's combined run died with a segfault whose position could not be located,
because the harness does not flush stdout on exit; the sanitized build pinned it to
the line and the call.

**The fixed slot table is why a filtered run and a combined run disagree.**
`RoboCssMaxStylesheets` is 64, and the suites created a sheet per test and never
released it, so a combined `css.` run exhausted the table partway through and every
later sheet creation failed — making unrelated tests fail for a reason with nothing
to do with them. The fix is in the fixture, not the constant: the cascade fixture
now owns the sheets it makes and releases them in its `end`, so the bound stays a
real product bound.

**Three count expectations were off by one against the contract the header
states.** `unset` on an inherited property lands inside the inherited family and so
adds no entry (it is `inherit` on `width`, a property outside the family, that adds
one); a declared inherited property leaves one fewer default; and a child that
declares one family property still inherits the other fifteen. The implementation
matched the header in all three; the expectations did not.

**Two smaller fixture mistakes.** The value-copy tests built tokens with a NULL
source, which the constructor refuses (a token with no source is not a token). And
the without-a-host case cleared the whole element host — taking matching away too —
then asserted the sheet still matched; it now installs a host with the view but no
`parent` callback, which is the case the header describes.

## Goal 9.8 — CSS computed values

**Status: complete.** The typed value model, the property registry's
`computation` column, the normaliser, the computed-style container and the chain
door are implemented, tested against the real RoboHTML DOM, and verified under
sanitizers.

### Delivered

- [x] `Include/RoboCSS/RoboCSSComputedValue.h` + `Source/RoboCSS/RoboCSSComputedValue.c`
      — the typed value, and it is a **closed set of shapes** rather than "a
      string, but typed": `KEYWORD`, `NUMBER`, `PERCENTAGE`, `LENGTH`, `COLOR`,
      `FAMILY_LIST`, `PRESERVED`, `INVALID`. Three of those carry decisions:
      `PRESERVED` is a real computed value (`calc(100% - 20px)` *is* the
      expression, and reducing it would be a lie because `%` needs a containing
      block), `INVALID` is an **answer** the call reports successfully with a
      reason rather than an error return that forces a caller to drop the
      property, and `unresolved` is a flag no kind can express (`var()`, `calc()`
      and `currentColor` are references, not values)
- [x] The unit model: the seven units with a fixed factor (`px cm mm q in pt pc`)
      convert **exactly** (so `1in` and `96px` compare equal), the eight relative
      units are kept as written (converting `2em` here would produce a number that
      looks resolved and is wrong wherever the font size differs), and a bare `0`
      is a length because CSS's one exception says so
- [x] The colour forms this build can already express: the **standard named set**
      in full, `transparent`, all four hex lengths (nibble doubling included;
      every other length refused rather than truncated), and `rgb()`/`rgba()` in
      **both** syntaxes with numeric or percentage channels and alpha. Channels
      clamp rather than wrap; the two separator styles do not mix
- [x] `CssComputedValueCompute` — the normaliser, and a function of **the
      value's shape and the value**, never of a property name. That is what keeps
      it free of context (no containing block, no font metrics, no parent) and
      free of the caches a context-dependent stage would need
- [x] `computation`, a new column on the property registry's definition:
      `PRESERVE`, `COLOR`, `LENGTH`, `KEYWORD`, `FONT_FAMILY`, `FONT_WEIGHT`,
      `LINE_HEIGHT`. Seven shapes serve every property; a length-shaped property
      accepts a closed keyword set (the union of the lists of the properties that
      share the shape), so `width: red` is INVALID rather than a keyword no later
      phase can use
- [x] The computed style — the 9.1 container **made richer rather than changed**:
      each entry still holds the specified value a 9.1 caller put there, and now
      also its name, its typed value and a provenance bitset (`DECLARED`,
      `INHERITED`, `DEFAULT`, `DEFERRED`, `CUSTOM`, `UNRESOLVED`, `INVALID`),
      plus `_incomplete_count` and `_is_truncated` so a style cannot be read as
      more settled than it is
- [x] `CssStyleContextComputeStyle` — the chain door: resolve **and
      compute** each level top-down, so a child inherits its parent's *computed*
      value rather than a recomputation of what the parent specified. The
      intermediate styles are the walk's own and are disposed as it descends
- [x] CSS-wide keywords are consumed by 9.7 on the parsed value, so computation
      never sees one; `revert` stays deferred and reported. Custom properties
      remain opaque token sequences, never eagerly interpreted as colours
- [x] Numeric safety: NaN and the infinities are refused rather than clamped or
      dropped (there is no CSS spelling for either), channels clamp, and a
      malformed colour cannot crash the runtime — the robust suite drives a table
      of hostile strings through the normaliser to prove it
- [x] Two new suites, 27 tests: `css.computedvalue.*` 14 (the unit table, the hex
      forms against every wrong length, the whole named set, `rgb()` in both
      syntaxes and its range handling, the value's lifecycle and copy/move
      contract, the lifetime balance, the seven `computation` shapes, and hostile
      input) and `css.computed.*` 13 (the pipeline: declared,
      inherited-as-computed, default-then-replaced, non-inherited, the CSS-wide
      keywords, `var()`/`calc()`/`currentColor` carried and counted, an invalid
      value, custom properties, inline, a three-level chain, a cyclic chain
      reported as truncated, determinism, the argument contract, and no
      stylesheets at all)
- [x] `css.dom.computed.*` 2 — the whole pipeline, all seven stages, over the
      **visual checkpoint's own document**: specificity picking between two
      colours, a typed colour's channels, `font-size: inherit` landing on the
      length the body computed, `width: inherit` pulling a value for a property
      that does not inherit by default, a custom property travelling the path
      untouched, `color: nonsense` reported as invalid without corrupting the
      rest of the style, and a byte-for-byte proof that computing every
      element's style twice changes nothing the demo paints
- [x] Capability flag `computed_values` flipped to 1; `default_styles` and
      `stylesheet_loading` still 0. Version 0.8.0, `RoboCssApiFrozen` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; the plain build is 1859/1859 and `css.` is 452/452
- [x] `scripts/robo check`, `build-runtime`, `test-css`, `test-qemu-display` all
      pass; `test-runtime` reports only the documented environmental
      `window.host.keyboard` failure, which no RoboCSS code touches
- [x] Phases 6–9.7 regression-tested: full suite green — up 29 CSS tests from 9.7,
      with no existing test weakened or removed
- [x] `css/docs/ComputedValues.md` added, and `css/README.md`, `css/docs/Tests.md`,
      `css/docs/Architecture.md`, `css/docs/Inheritance.md`, `css/docs/Cascade.md`,
      `css/docs/Parser.md`, `css/docs/Selectors.md` and `ARCHITECTURE.md` updated
- [x] No Chromium dependency introduced
- [x] No used values, no colours beyond the forms the parser already expressed
      (9.9), no units beyond the length forms a computed value carries (9.10), no
      box model, no layout, no painting, no CSSOM, no animations
- [x] Stop after 9.8

### What 9.8 found

**Three real product defects.** Two of them had survived a green `--filter=css.`
build because the new suite files were compiled but **not registered** — the build
script's source list and `Tests/Main.c` are two places, and a suite has to be in
both. Until it was, 1846 lines of assertions were files on disk and nothing more.

* `CssComputedValueCopy` set the text pointer but not its length, so a
  copied value read as *empty*. Inheritance copies the parent's computed value, so
  this is how a keyword would reach a child as no keyword at all.
* `CssUnitIsAbsolute` was written as `unit_to_px(1.0, unit) != 0.0`, and
  the converter returns the value it was given for a unit with no fixed factor — so
  `em`, `vh` and the empty unit all reported themselves absolute, and `em` and `vh`
  would have been silently converted to pixels in the computed value.
* `rgb()` could not parse the space syntax at all: the splitter skipped whitespace
  and enforced separators with a "last component was an argument" flag, so two
  space-separated numbers looked adjacent. The fix records which of the two
  separator styles a call started with and refuses a second one, which is also
  what makes `rgb(255, 0 0)` malformed and `rgb( 1 , 2 , 3 )` legal.

**Five expectations were wrong rather than the implementation**: `#ff00` is a
valid `#rgba` colour (the malformed case is `#ff`); a length-shaped property must
reject a non-length keyword; `not an identifier` is the right message for `k: 10`
(`10` *is* one component); `css_allocator_ensure_enabled` reports whether *this
call* enabled the tracker; and three argument-contract cases passed a computed
value where a specified value belongs.

## Goal 9.9 — CSS colours

**Status: complete.** The colour module, the value layer's delegation to it, the
contextual `currentColor` resolution in the computed style, the registry's second
colour property, and 19 new tests are implemented and verified.

### Delivered

- [x] `Include/RoboCSS/RoboCSSColor.h` + `Source/RoboCSS/RoboCSSColor.c` — the
      **eighth module**, and the first that is a *representation* rather than a
      stage. A colour is the one CSS value whose meaning is arithmetic, and 9.8's
      colour code lived inside the computed-value layer because that was its only
      reader; 9.9 gives it a header, an implementation and every spelling
- [x] `CssColor`: **four kinds, not one.** `NONE` (nothing, and what a
      failed parse leaves), `RGBA` (four 8-bit channels in sRGB — the canonical
      resolved colour), `CURRENT` (`currentColor`, contextual *by construction*)
      and `INVALID` (a colour was meant and there is not one). The kind is what
      makes `currentColor` expressible at all, and it is why equality is
      kind-aware: a resolved red is never equal to `currentColor`, because they
      are different answers
- [x] **8 bits per channel, and it is a decision.** CSS's ordinary syntax cannot
      express more, the framebuffer is 8-bit, and a float triple would invite
      equality comparisons that fail on values which are the same colour. `hsl()`
      converts once in doubles with a single rounding at the end, so
      `hsl(120 100% 25%)` is *exactly* `#008000` — a property a test can hold
- [x] The **148-name standard set** (the 16 CSS2.1 keywords, the 130 SVG/X11 names
      with their duplicates, and `rebeccapurple`), ASCII case-insensitive,
      reachable by name **and by index** so a suite walks the whole table rather
      than the handful of names someone thought to type. `transparent` is
      deliberately **not** a table entry: it is a colour but a *keyword*, handled
      where the identifier is. An unknown identifier is never silently a colour
- [x] All four hex lengths with nibble **expansion** (not zero-padding — the
      classic three-digit mistake), every other length and every non-hex digit
      refused rather than truncated, case-insensitive, and an opaque alpha nibble
      normalised away (`#f00f` *is* `#ff0000`)
- [x] `rgb()`/`rgba()` in **both** syntaxes — legacy comma and modern space, with
      numeric, percentage or *mixed* channels and numeric or percentage alpha —
      with `rgb` and `rgba` deliberately not distinguished, because CSS Color 4
      made each accept both shapes. Channels **clamp rather than wrap**; the two
      separator styles do not mix, which is what makes `rgb(255, 0 0)` malformed,
      `rgb( 1 , 2 , 3 )` legal, and `rgb(1 2 3 4)` a call with an alpha separator
      nobody wrote
- [x] `hsl()`/`hsla()` with hue as an angle in the four units (`deg`, `grad`,
      `rad`, `turn`), hue **normalised rather than clamped** (`-120` and `240` are
      the same hue, `480` is `120`, `720` is `0`), saturation and lightness
      clamped, percentages required in the legacy syntax and bare numbers read as
      percentages in the modern one, and a hue too large to reduce saturating to
      zero rather than producing an artefact
- [x] **Six outcomes**, because "did it work" is not enough of an answer:
      `RESOLVED`, `CURRENT`, `DEFERRED`, `MALFORMED`, `UNKNOWN_NAME`, `INVALID`.
      `DEFERRED` is the one that keeps the build from lying — `hwb`, `lab`, `lch`,
      `oklab`, `oklch`, `color()`, `color-mix()`, `device-cmyk()` are legal CSS
      this build cannot represent, so the declaration is carried and flagged
      unresolved rather than called invalid; a wrong colour is worse than a
      deferred one. `MALFORMED`, `UNKNOWN_NAME` and `INVALID` all end as an
      invalid computed value and stay separate because the *diagnostic* differs
      and the diagnostic is the point
- [x] The value layer now **consumes** the colour module instead of carrying its
      own parser — one authoritative implementation, so two spellings of the same
      colour cannot disagree. A **reference is settled before the colour parser is
      asked**, which is the defect this phase found: `color: var(--brand)` is a
      `var()` reference, not a mistake, and it is carried with its tokens and the
      `UNRESOLVED` flag exactly as every other value shape is
- [x] `currentColor` resolved by the **computed style**, at the only point that
      has both the element and the parent, and by **two rules**: on `color` itself
      it is the parent's computed `color` ("the colour I would have inherited",
      the only reading that terminates), and everywhere else it is the element's
      own ("my text colour"). `color` is therefore resolved first, in a pass of its
      own, so nothing can read a value that has not been settled. With nothing to
      resolve against — a root element — the colour **stays** `CURRENT` and
      **stays** flagged, because inventing black is the mistake the keyword exists
      to prevent. Only the `UNRESOLVED` bit is cleared, never the whole flag set
- [x] **`background-color` joined the property registry.** The reason is not
      backgrounds: it is the only *second* colour-shaped property, and without one
      the rule that resolves `currentColor` against the element's own `color`
      would be unreachable and therefore unprovable. A property fact is what that
      table holds — no background is painted, no shorthand is expanded, and
      `background` keeps its own `PRESERVE` classification. The registry suite's
      count assertion (24 → 25 rows, 8 → 9 non-inherited) is updated with it, which
      is exactly what that deliberate count exists for
- [x] Serialization: one stable spelling per kind (`none`, `invalid`,
      `currentColor`, lower-case hex with alpha only when it is not opaque). Named
      colours are deliberately **not** written back as names, because two equal
      colours must print the same and hex always can — which is what makes a
      written colour usable as an assertion. A short buffer is refused with the
      length it needed rather than truncated
- [x] Numeric safety: channels clamp, a non-finite literal is clamped into range,
      an underflow is a channel like any other, and a hue with no fractional part
      left saturates. 37 malformed declarations are driven through the parser with
      two invariants asserted on every one — the outcome is a defined enumerator,
      and a colour that was not produced is `NONE` or `INVALID`, never a
      plausible-looking channel set nobody wrote
- [x] New suite `css/tests/SuiteRoboCssColor.c`, **19 tests**: `css.color.*` 12
      (the record and its kind tables, the whole 148-entry table walked by index,
      all four hex lengths against every wrong length and non-hex digit, both
      argument styles of both functions with clamping and one-rounding, hue
      normalisation, `currentColor` typed and the substitution's four cases, the
      deferred spaces, the not-a-colour line, 37 malformed inputs, serialization)
      and `css.color.computed.*` 7 (twelve spellings of four syntaxes converging on
      one computed colour each, inheritance through two levels with an intermediate
      override, `background-color` not inheriting, the CSS-wide keywords on both
      kinds, the two `currentColor` rules separated by a parent whose colour
      differs from its child's, a root whose `currentColor` stays contextual, the
      `var()` boundary, the whole selector pipeline over a three-level tree, and
      determinism)
- [x] Capability flag `colors` flipped to 1; `default_styles` and
      `stylesheet_loading` still 0. Version 0.9.0, `RoboCssApiFrozen` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer, **zero findings**;
      `css.color` is 19/19 under the sanitized binary, the CSS surface is 471/471
      and the whole suite is 1878/1878 run in the foreground
- [x] `scripts/robo check` **PASS** with Chromium independence **CLEAN** (sources
      + headers, build wiring); `build-runtime` **PASS**; `test-css` **PASS** (2/2);
      `test-qemu-display` **PASS** (2/2, the booted QEMU display); the host visual
      checkpoint **PASS** offscreen at **15/15 probes**; `validate-deployment`
      **PASS**. `test-runtime` reports only the documented environmental
      `window.host.keyboard` failure, which no RoboCSS code touches
- [x] Phases 6–9.8 regression-tested: full suite green — up 19 tests from 9.8,
      with no existing test weakened or removed
- [x] `css/docs/Colors.md` added, and `css/README.md`, `css/docs/Tests.md`,
      `css/docs/Architecture.md`, `css/docs/ComputedValues.md` and `ARCHITECTURE.md`
      updated
- [x] No Chromium dependency introduced
- [x] No colour interpolation, no blending, no second space, no unit system beyond
      the length forms a computed value carries (9.10), no box model, no layout, no
      painting, no CSSOM, no animations
- [x] Stop after 9.9

### What 9.9 found

**One real product defect, and it is the kind that hides in plain sight.**

* A `var()` in a colour position was reported as a **mistake**. `color:
  var(--brand)` reached the colour parser, which called it non-colour syntax and
  returned `INVALID`, so a legal reference to a colour produced the same computed
  value as `color: nonsense` and the diagnostic said `not a colour`. The parser
  was right about the syntax and wrong about the author. `computed_compute_color`
  now settles the reference first — a single unresolved function (`var`, `env`,
  `calc`, `min`, `max`, `clamp`) is carried with its tokens and the `UNRESOLVED`
  flag — which is what every other value shape already did.

**Three expectations were wrong rather than the implementation**, and each was a
real piece of CSS arithmetic rather than a typo:

* `#f00f` is `#ff0000` — an opaque alpha nibble normalises away, which is the
  representation agreeing with the `#rrggbb` spelling of itself;
* `rgb(51%, 40%, 60%)` is `#826699`, not `#336699` — the percentage case had the
  numeric channels' expected value copied into it, and the corrected pair now
  proves 20% → 51 exactly and 51% → 130.05 rounded to 130;
* `hsl(210, 100%, 20%)` is `#003366`, not `rgb(0, 68, 102)` — the green channel is
  the sector's *intermediate* (51), not its endpoint, while the blue is lightness
  plus half the chroma (102).

One helper was broken in a way that made a **failure message lie**:
`css_color_same` passed `NULL` for the serialization length, which the writer
refuses, so the two buffers it printed as "the colours that differ" were
uninitialised stack. It now checks both writes, because a diagnostic that names
stack noise as a colour is worse than no diagnostic.

## Goal 9.10 — CSS units

**Status: complete.** The CSS numeric-and-unit subsystem: a closed unit
vocabulary, a resolution context, a typed numeric value, and the computed-value
integration that consumes them.

### Delivered

| Item | Where |
|---|---|
| The unit vocabulary | `Include/RoboCSS/RoboCSSUnit.h`, `Source/RoboCSS/RoboCSSUnit.c` — 31 units plus `NONE`, four families, one table row each: spelling, family, fixed factor, viewport family and dimension |
| The resolution context | `CssUnitContext` and six `_set_*` setters — a `has_` flag per fact, so "the font size is 16" and "I do not know the font size" stay different answers. An argument, never a global |
| Resolution | `CssUnitResolvePx` — four outcomes (`RESOLVED` / `NEEDS_CONTEXT` / `NOT_A_LENGTH` / `INVALID`), with `out_px` written only on success |
| Absolute conversion | The reference-pixel relationships (`1in` = 96px, `1cm` = 96/2.54px, `1pt` = 4/3px, `1Q` = 1/40cm), exact and context-free — no DPI anywhere |
| The typed value | `CssNumeric` — `NUMBER` / `INTEGER` / `PERCENTAGE` / `LENGTH`, a tag plus a double plus an enumerator by value, owning nothing |
| The parse | Five outcomes: `RESOLVED`, `DEFERRED` (`calc`/`min`/`max`/`clamp`/`var`/`env`), `UNKNOWN_UNIT`, `NON_FINITE`, `INVALID` |
| Serialization | `CssNumericWrite` — one canonical spelling, a short buffer refused rather than truncated |
| Computed-value integration | `RoboCSSComputedValue.c` now *consumes* the module: absolute units convert to `px`, contextual ones carry their unit and number, percentages stay percentages |
| Version | `RoboCSSVersion.h` 0.10.0; capability `units` = 1 |
| Documentation | `css/docs/Units.md` (new, the design record), plus `README.md`, `docs/Architecture.md`, `docs/Tests.md`, `docs/ComputedValues.md` and `docs/Colors.md` |

### Definition of done

- [x] A typed numeric/unit subsystem exists as a module of its own, and the
      computed-value layer holds no second unit table.
- [x] Tokens the tokenizer produces read as one of four kinds, and nothing reads
      a number's text.
- [x] Absolute CSS lengths convert deterministically and exactly, and nothing
      consults a display.
- [x] Relative and viewport lengths have a contextual representation and a
      resolver that can answer `NEEDS_CONTEXT` rather than inventing a font size
      or a viewport.
- [x] Percentages are a kind, not a unit, and stay distinguishable from lengths.
- [x] Unknown units fail safely and are reported apart from a value that is not
      numeric at all.
- [x] Numeric overflow, underflow and non-finite input are refused, and the
      integer conversion checks its range *before* casting.
- [x] `calc()` and the other reference functions are carried, not claimed.
- [x] The computed-value integration keeps 9.8's rule: absolute converts, contextual
      is carried, percentage is kept.
- [x] Cascade, inheritance, colour and selector behaviour are unchanged, and the
      visual vertical slice is verified rather than assumed.
- [x] Tests are comprehensive: 18 in `SuiteRoboCssUnit.c` and one real-DOM pipeline
      test, covering valid, invalid, boundary, conversion, precision, safety and
      regression cases.
- [x] Host and QEMU runtimes still work; no Chromium dependency was introduced.

### What 9.10 found

**One defect the delegation introduced, and one the serializer had all along.**

* **A length-shaped property rejected every keyword.** Moving the keyword handling
  inside the branch guarded by "there is no single token" meant `auto` never
  reached it: an IDENT *is* one token, so the identifier fell through to the
  numeric parse — which is right to call an identifier not a number — and the
  developer was told `not a length` when the property's grammar simply did not
  accept the keyword. Seven 9.8 tests failed on it, all the same defect wearing a
  different property's clothes (`l: auto`, `h: auto`, `font-size: medium`,
  `width: auto`). The fix asks the property's keyword list before the numeric
  vocabulary, and the ordering is now commented as load-bearing.
* **A whole `-0` printed as `-0`.** The negative-zero rule lived in the
  *fractional* branch, and `-0.0` has no fraction to strip — `%.0f` renders it
  whole — so the sign survived on a value that is zero, against the header's own
  documented rule. It now runs after both branches, which is where a rule about the
  rendered digits belongs.

Four expectations were wrong rather than the implementation: `40Q` is `37.795px`
and not `9.6px` (a factor of four applied twice — and the suite now asserts the
relationship in both directions, `40Q` = `1cm` *and* `4Q` = `1mm`); `1e300in` does
*not* overflow, so the overflow case proves nothing until it uses `1e308` (and now
asserts that `1e300` resolves, pinning the boundary from both sides); there are 18
viewport units and not 16 (six in the default family, four in each overridable
one); and the malformed-input sweep used the runtime's own finiteness predicate,
which would have agreed with the code under test by construction.

## Goal 9.11 — CSS box model

**Status: complete.** The box-model property and value foundation: the four sides,
the three shorthands this build distributes, a typed value family for the twelve
longhands, and the resolution rule that decides an edge two declarations speak for.
Layout is not implemented and is not claimed.

### Delivered

| Item | Where |
|---|---|
| The side vocabulary | `Include/RoboCSS/RoboCSSBox.h` — the four edges as an ordered, closed set, in CSS's own order because the component mapping depends on it |
| The shorthand table | `CssBoxShorthand` — `margin`, `padding`, `border-width` with the longhand each side stands for, walkable both ways (`_find`, `_shorthand_of_longhand`) |
| The mapping | The 1/2/3/4 distribution as a 4×4 table, with `CssBoxComponentCount` and `CssBoxIsExpandable` separating "how many components" from "may they be distributed" |
| Expansion | `CssBoxExpandSide` and `CssBoxExpand` — a component copied out as a one-item list, all-or-nothing |
| The value shapes | `EDGE_OFFSET`, `EDGE_INSET`, `LINE_WIDTH` — three shapes sharing one body and differing by vocabulary, so `padding: auto` is INVALID and `thin` is a keyword |
| The cascade door | `CssCascadeCandidateCompare` — two candidates of one cascade weighed by level, specificity, then document order, which is the answer expansion must not re-derive |
| The registry rows | Fourteen: the twelve longhands, `border-width`, and `box-sizing`. None inherits |
| The pass | `inherit_expand_box_shorthands` inside `RoboCssInheritResolve` — longhands skipped by the declared pass, materialised here exactly once, through the same `inherit_value` a written declaration uses |
| Reportable failure | `CssInheritedStyleShorthandsUnexpanded` — a shorthand that cannot be distributed leaves its own entry intact, counts, sets `unsupported`, and logs one line |
| Version | `RoboCSSVersion.h` 0.11.0; capability `box_model` = 1 |
| Documentation | `css/docs/BoxModel.md` (new, the design record), plus `README.md`, `docs/Architecture.md`, `docs/Tests.md`, `docs/ComputedValues.md` |

### Definition of done

- [x] A typed box-model value family exists, and no property's value is a string.
- [x] `width` and `height` are supported at the value and computed-style level,
      with `auto`, lengths and percentages distinguishable and `auto` not a number.
- [x] All four margin longhands and the `margin` shorthand, with the standard
      1/2/3/4 expansion.
- [x] All four padding longhands and the `padding` shorthand — and `padding: auto`
      is refused rather than accepted.
- [x] All four border-width longhands and the `border-width` shorthand, with
      `thin`/`medium`/`thick` kept as keywords.
- [x] Percentages stay percentages: nothing converts one without a containing block.
- [x] `auto` is a typed representation and is never zero.
- [x] Initial values are standards-compatible (`0` for the edges, `medium` for a
      border width, `content-box` for `box-sizing`), held in the registry rather
      than scattered through a resolution path.
- [x] None of the fourteen inherits, and an explicit `inherit` still works through
      the existing mechanism.
- [x] `initial`, `inherit` and `unset` resolve correctly for every one of them,
      through the computed-style pipeline and with no duplicated keyword logic.
- [x] Shorthand/longhand cascade interaction is correct in both directions, with
      specificity and `!important`, and decided by the cascade rather than by a
      second opinion here.
- [x] Invalid declarations fail safely: `padding: auto`, `width: red`,
      `border-width: 1foo` and a five-value shorthand all produce an explicit
      answer rather than a plausible one.
- [x] `box-sizing`'s value model exists and no layout behaviour is claimed for it.
- [x] Cascade, inheritance, colour, unit, selector and DOM behaviour are unchanged,
      and the visual vertical slice is verified rather than assumed.
- [x] Tests are comprehensive: 13 in `SuiteRoboCssBox.c` and one real-DOM pipeline
      test, covering valid, invalid, shorthand, cascade, keyword, non-inheritance,
      robustness and regression cases.
- [x] Host and QEMU runtimes still work; no Chromium dependency was introduced;
      no layout was implemented prematurely.

### What 9.11 found

**No product defect, and the reason is the phase's one architectural decision.**
Deciding which of a shorthand and a longhand wins an edge was built by *asking the
cascade* (`CssCascadeCandidateCompare`) rather than by re-deriving its three
keys in the box model. The four directions the suite pins — document order both ways,
specificity, importance — therefore all ran through code that was already correct,
and the failure mode a second implementation would have produced (a shorthand
ownerwriting a more specific longhand, or vice versa) never existed to be found.

Five expectations were wrong rather than the implementation, and three are the same
shape of mistake:

* A computed `PRESERVED` value renders with spaces, not commas — the assertion
expected the *declaration-shape* helper's `8px,_,12px,_,4px` where the computed
  serializer writes `8px 12px 4px`.
* An id selector does not match a class. `css_host_add(host, "div", "panel", "box")`
  gives `id=panel` and `class=box`, so `#box { margin: 10px }` never matched and the
  *type* selector's `margin-left: 20px` won — which read as a precedence bug and was
  a fixture bug.
* An absent entry's provenance is `<absent>`, not `<none>`: `<none>` belongs to
  `SOURCE_NONE`, a *present* entry with no provenance.
* An inherited default is inherited *from the parent*, not defaulted again — a
  child's `color` whose parent's value was the registry's default has provenance
  `parent`.
* A context's sheets are shared by every element in it, so the "a shorthand that
  distributes leaves the counter at zero" half of the unexpanded-shorthand test had
  to move to a fixture of its own rather than assert zero on input that should set
  the counter.

## Goal 9.12 — CSS display

**Status: complete.** The `display` property and its value system: a closed four-value
vocabulary carrying what CSS Display 3 calls each value's outer and inner halves, the
classifications a layout pass asks, and a typed value on the computed style. Layout is
not implemented and is not claimed.

### Delivered

| Item | Where |
|---|---|
| The vocabulary | `Include/RoboCSS/RoboCSSDisplay.h` — four values as an ordered, closed set: `none`, `block`, `inline`, `inline-block` |
| The outer/inner model | `CssDisplayOuter` / `_inner_t` and `CssDisplayOuter()` / `_inner()`, so `inline-block` is `inline` plus `flow-root` rather than a flag beside a flag |
| The classifications | `CssDisplayGeneratesBox`, `_is_block_level`, `_is_inline_level`, `_is_inline_block`, `_is_none` — derived in the table so two readers cannot disagree |
| Parsing and serialization | `CssDisplayFromText` (ASCII case-insensitive against the rows and nothing else) and `CssDisplayWrite` (the author's own spelling) |
| The shape | `RoboCssPropertyComputationDisplay` — one new value shape, with its own field, accessor, equality and serializer in the computed-value layer |
| The registry row | `display` moved off the generic `KEYWORD` shape onto the new one; classification and `inline` initial value unchanged |
| The value accessor | `CssComputedValueDisplay` on the typed record. There is deliberately no accessor on the *style* object: `display` does not inherit, so an element that declares nothing has no entry, and only the style context's `CssStyleContextQueryComputedValue` can answer the property's initial value and say that is what it answered |
| Version | `RoboCSSVersion.h` 0.12.0; capability `display` = 1 |
| Documentation | `css/docs/Display.md` (new, the design record), plus `README.md`, `docs/Architecture.md`, `docs/Tests.md`, `docs/ComputedValues.md` |

### Definition of done

- [x] `display` is a typed property; no value is a string after it is parsed.
- [x] `none`, `block`, `inline` and `inline-block` are all supported.
- [x] The representation is the outer/inner pair rather than one enumerator, so
      `flex`, `grid`, `table`, `flow-root`, `contents` and the rest arrive as rows.
- [x] The initial value is `inline`, independent of the element's tag: nothing in the
      module reads an element name, and no UA stylesheet is invented.
- [x] `display` is non-inherited, proven from both sides, and an explicit `inherit`
      still works through the existing mechanism.
- [x] `initial`, `inherit` and `unset` resolve correctly with no duplicated
      CSS-wide keyword handling.
- [x] The cascade is unchanged and uncopied; specificity, source order, `!important`
      and an inline style all decide a display the same way they decide anything else.
- [x] Invalid values fail safely: `bogus`, `flex`, `20px`, `10%`, `#fff`,
      `inline block` and `rgb(0, 0, 0)` each get their own diagnostic rather than a
      silent `inline`.
- [x] `display: none` is a decided value rather than zero geometry, a transparent
      colour, a hidden visibility or an invalid value.
- [x] A computed display is readable as a typed value: from a style the element
      declares for, through the record's accessor, and for an element that declares
      nothing through the style context's query door, which answers the property's
      initial value and marks it as such.
- [x] Tests are comprehensive: 16 in `SuiteRoboCssDisplay.c` and one real-DOM
      pipeline test, covering the vocabulary, classification, parsing, cascade,
      inheritance, keywords, invalid values, robustness and determinism.
- [x] Host and QEMU runtimes still work; no Chromium dependency was introduced; no
      layout was implemented prematurely.

### What 9.12 found

**No product defect, and here the reason is structural rather than lucky.** Every
question the module answers is a lookup in one four-row table, so there is no place a
keyword spelling can be compared wrongly and no branch that can disagree with a
sibling. The failure this phase was exposed to is the one a table cannot make: a value
outside the vocabulary slipping through as valid. That is why the refusals are the
largest group in the suite and why each asserts its own diagnostic instead of a shared
"invalid".

Three expectations moved, and the first is the one worth recording:

* The 9.11 property-registry suite went green **untouched** even though this phase moved
a row onto a new shape and added an eleventh shape to the column. It derives — every row
classified one way or the other, every initial value parseable — rather than pinning a
shape per property name or a count of shapes, which is the only way a table suite
survives its table growing.
* A property's initial value arrives typed on *that property's own shape*.
  `css.dom.computed.pipeline` asserted that the property's initial value came back as
the kind `keyword`. It did, and correctly: that is what an initial value is until its
property has a shape. The property that test reads is `display`, so once the row gained
a shape the same door began answering a `DISPLAY` value holding an enumerator. The
assertion was right about the pipeline and wrong about the row; it now asserts the
enumerator, which is strictly more than before.
* The computed-value malformed sweep gained the display shape so a closed vocabulary is
  fuzzed for its *refusals* too, with the invariant stated explicitly: no string may
  produce a display value that is not one of the four.

One compile-time fix was needed and no more: the new module had to reach the internal
header for the missing-symbol path it shares with every other value family.

## Goal 9.13 — CSS positioning

**Status: complete.** The `position` property and its value system: a closed five-value
vocabulary carrying the two facts a position value *is* (whether the box stays in flow
and whether it is a positioned box), the four inset properties on the box model's
`EDGE_OFFSET` shape, and `z-index` as `auto` or a whole number. No containing block,
no offset, no rectangle and no stacking order is computed.

### Delivered

| Item | Where |
|---|---|
| The vocabulary | `Include/RoboCSS/RoboCSSPosition.h` — five values as an ordered, closed set: `static`, `relative`, `absolute`, `fixed`, `sticky` |
| The two facts | `in_flow` and `positioned` as two columns of the row table, asked through `CssPositioningIsInFlow()` / `_is_out_of_flow()` / `_is_positioned()` — the two are not each other's opposite, since `relative` and `sticky` answer yes to both |
| Parsing and serialization | `CssPositioningFromText` (ASCII case-insensitive against the rows and nothing else) and `CssPositioningWrite` (the author's own spelling) |
| The unknown-identifier door | `CssPositioningSliceIsKnown` — `static` is both a value and the answer for "not one of them", so the question is asked of the *identifier* |
| The shape | `RoboCssPropertyComputationPosition`, sharing one case body with `DISPLAY` because the vocabulary, its doors and its refusals are data in a row |
| The four insets | `top`, `right`, `bottom`, `left` on the box model's `EDGE_OFFSET` shape, with their names read from `CssBoxSideName` — no second grammar for `<length-percentage> | auto` |
| `z-index` | `RoboCssPropertyComputationZIndex`: `auto` as the keyword (not zero) or a NUMBER that must carry `is_integer`, so `1.5`, `0.0` and `1e2` are refused |
| The registry rows | Five new non-inherited rows plus `position` moved onto its own shape; initials `static` and `auto` |
| The value accessors | `CssComputedValuePosition` and the display accessor beside it on the typed record. No accessor on the *style* object, for the same reason as 9.12: none of the six inherits, so a declaring-nothing element has no entry and only the style context's `CssStyleContextQueryComputedValue` can answer the initial value |
| The diagnostic domain | `RoboCssDomainPosition` |
| Version | `RoboCSSVersion.h` 0.13.0; capability `positioning` = 1 |
| Documentation | `css/docs/Positioning.md` (new, the design record), plus `README.md`, `docs/Architecture.md`, `docs/Tests.md`, `docs/ComputedValues.md` |

### Definition of done

- [x] `position` is a typed property; no value is a string after it is parsed.
- [x] `static`, `relative`, `absolute`, `fixed` and `sticky` are all supported.
- [x] The representation carries the two facts separately, so `relative` and `sticky`
      being in flow *and* positioned at once is expressible rather than a contradiction.
- [x] `sticky` is a value and not a behaviour: it classifies as in flow and positioned,
      which is what it is before anything scrolls, and no scrollport is invented.
- [x] The initial values are `static` and `auto`, and `auto` is never read as zero —
      `z-index: auto` is "takes no part in its parent's stacking decisions" and not
      "at level zero".
- [x] All six properties are non-inherited, proven from both sides, and an explicit
      `inherit` still works through the existing mechanism.
- [x] `initial`, `inherit` and `unset` resolve correctly with no duplicated CSS-wide
      keyword handling; `unset` behaving as `initial` is asserted separately because it
      is the assertion that would fail if one of the six were misclassified.
- [x] The four insets reuse the existing `<length-percentage> | auto` family rather than
      a second grammar, so `top: 25%` stays a percentage, `top: auto` stays the keyword
      and `top: min-content` is a mistake.
- [x] `z-index` accepts `auto`, `0`, positive and negative integers, and refuses a
      fraction, `0.0` and an exponent — because CSS's `<integer>` grammar is about the
      token and `0.0` is a number token.
- [x] The cascade is unchanged and uncopied; specificity, source order, `!important`
      and an inline style decide a position the same way they decide anything else.
- [x] Invalid values fail safely, each with its own diagnostic: `running()` is an
      identifier this build has not added, `20px`/`#fff`/`10%` are not identifiers,
      `absolute fixed` and `rgb(0, 0, 0)` are not one keyword, and a reference
      (`var()`, `env()`) is carried and flagged rather than refused.
- [x] Tests are comprehensive: 19 in `SuiteRoboCssPosition.c` and one real-DOM pipeline
      test, covering the vocabulary, classification, parsing, serialization, the inset
      family walked over the side vocabulary, `z-index`, the cascade, inheritance,
      keywords, invalid values, robustness and determinism.
- [x] Host and QEMU runtimes still work; no Chromium dependency was introduced; no
      layout, stacking or scrolling was implemented prematurely.

### What 9.13 found

**One real defect, and it was in a diagnostic rather than in a value.** The box model's
`EDGE_OFFSET` shape had carried the message *"not a margin length or auto"* since 9.11,
which was accurate while `margin` and its three relatives were its only row. This phase
put the four insets on the same shape — deliberately, so the two families could not
normalise a percentage differently — and at that moment the message began naming one of
its two families while it was refusing the other. Because the shape is shared, the
property's identity is not available where the message is written, so the honest fix was
to describe the grammar: *"not a length, a percentage or auto"*. Three assertions in
`SuiteRoboCssBox.c` moved with it.

The other expectation that moved is the one 9.12 predicted, and the reason is the same:

* The registry suite pins counts. It asserted 39 properties and 23 non-inherited; the
  five new rows plus the shape change made the numbers 44 and 28. The structural
  assertions beside them — every row classified, every initial value parseable — went
  green untouched, which is the part that means the table is still coherent.

Two things the suite was written to catch rather than to confirm:

* The four insets are walked over `robo_css_box_side_count()` with each name read from
  `CssBoxSideName`, so a registry row written against the wrong side fails. A
  hand-listed `top, right, bottom, left` could not see that: `top: 10px` computing to
  `10px` is unremarkable.
* `z-index: 0.0` is asserted refused beside `1.5` and `1e2`, so the rule being tested is
  visibly "the token, not the magnitude" — the one refusal in the phase that a
  reasonable implementation gets wrong while every value it produces is numerically
  correct.

One compile-time fix was needed and no more: the module surface is `positioning_*` and
not `position_*`, because `CssPosition` is already the **source position** type
(offset, line, column). The suite reads every name back, so a stray spelling would have
surfaced as a compile error rather than as a behaviour — which is the good outcome.

## Goal 9.14 — CSS backgrounds

**Status: complete.** The six background longhands as typed values: five models rather
than one because the six do not share a grammar — two closed vocabularies with rows read
back by name, a colour reused from 9.9, an image reference kept as the tokens the grammar
produced, and the phase's first two **pair** values. The `background` shorthand is
distributed through the inheritance layer's existing pass, reset semantics included. No
rectangle is painted, no `url(…)` is fetched, and no percentage is resolved against a box.

### Delivered

| Item | Where |
|---|---|
| The repeat vocabulary | `CssBackgroundRepeat` — `repeat`, `repeat-x`, `repeat-y`, `no-repeat`, with the two **axes** read from the same row the name comes from: `repeat-x` *is* `repeat no-repeat`, not a spelling that happens to produce it |
| The attachment vocabulary | `CssBackgroundAttachment` — `scroll`, `fixed` |
| The component | `CssBackgroundComponent` — an edge keyword, a length or a percentage, each keeping its own kind, with kind-aware equality so `left` and `0px` stay different values however equal they look |
| The pairs | `CssBackgroundPosition` and `CssBackgroundSize` — the first values in the build that are two components, **folded at parse time** so `center` and `center center` are one computed value rather than two, plus `cover`/`contain` as size keywords that are not `auto` |
| The image | `RoboCssPropertyComputationBackgroundImage` — `none` as the canonical keyword, and a `url(…)`, a gradient or a `var()` carried as the tokens it came from |
| The colour | `background-color` keeps the 9.9 colour shape unchanged; the phase reuses the colour family rather than growing a second one |
| The registry rows | Five new non-inherited longhands and their initials (`none`, `repeat`, `0% 0%`, `auto`, `scroll`). `background` and `background-color` were already rows — a shape change is not a new property, and the shorthand is *distributed*, not re-registered |
| The shorthand | `CssBackgroundExpand`, reached through the inheritance layer's existing distribution pass so it travels the same rules a declared longhand does, with the **reset** semantics a shorthand owes: a longhand the shorthand does not name goes back to its initial value rather than leaving an earlier declaration standing |
| The diagnostic domain | `RoboCssDomainBackground` |
| Version | `RoboCSSVersion.h` 0.14.0; capability `backgrounds` = 1 |
| Documentation | `css/docs/Backgrounds.md` (new, the design record), plus `README.md`, `docs/Architecture.md`, `docs/Tests.md`, `docs/ComputedValues.md` |

### Definition of done

- [x] All six properties are recognized rows in the existing registry, on shapes of their own.
- [x] Supported values parse and invalid values fail safely, each refusal with its own
      diagnostic rather than one collapsed "invalid".
- [x] Values participate in the existing cascade with no background-specific precedence logic.
- [x] All six are non-inherited, proven from both sides, and an explicit `inherit` still works.
- [x] The correct initial values are represented through the existing computed-style system.
- [x] Computed styles expose typed background values, including the pairs.
- [x] Lengths and percentages remain unresolved for later phases — `50%` is a percentage,
      not a coordinate.
- [x] The `background` shorthand works through the existing declaration/cascade
      architecture, reset semantics included, and unsupported syntax is refused rather than
      half-read.
- [x] `background-color` reuses the existing colour parser and representation; no second
      colour system exists.
- [x] Tests are comprehensive: 31 in `SuiteRoboCssBackground.c` and one real-DOM pipeline
      test over the visual checkpoint's own document.
- [x] All Phase 9.1–9.13 tests still pass; host and QEMU runtimes still work; no Chromium
      dependency was introduced; no painting, image fetching, layout or border work was
      implemented prematurely.

### What 9.14 found

**Four things, and the first three were gaps rather than mistakes** — the phase's own tests
are what surfaced them:

* **Five computation shapes had no name.** `CssPropertyComputationName` fell to
  `"unknown"` for the five new shapes. A name is what a diagnostic prints, so an
  unrecognised shape is a message nobody can act on, and nothing else in the build reads
  that function — only a test walking the set can see it.
* **The parse-outcome vocabulary had no doors.** `CssBackgroundParseIsKnown` and
  `_parse_name` were declared in the header and had no definition. That is a link error
  rather than a silent one *because the suite walked the set*; a suite that had typed out
  the one outcome it cared about would have left the other three undefined.
* **A hex colour in the shorthand was refused.** `background: #fff` classified as belonging
  to no longhand while `background: white` did not, because the classifier's HASH case fell
  to the default. Four spellings of one colour reaching one longhand is now asserted directly.
* **The suite caught a bug in itself.** `css.background.robust` compares two computations of
  one malformed input and compared `b.kind` against `a.kind` *after* disposing `a`. Reading a
  disposed record's kind is exactly the class of defect that suite exists to hunt.

The one expectation that moved is the count 9.13 predicted would keep moving: the registry
suite asserted 44 properties and 28 non-inherited, and the five new longhands made the
numbers 49, 16 inherited and 33 non-inherited. `background` and `background-color` were
already rows — the shorthand is distributed rather than re-registered, and a shape change is
not a new property, so adding the family did not add the two rows a count of six would have
suggested. The structural assertions beside the counts — every row classified, every initial
value parseable, every name usable — went green untouched, which is the part that means the
table is still coherent rather than merely the right size.

Three things the suite was written to catch rather than to confirm:

* **A pair has two spellings and only one is stored.** `center` and `center center` are
  asserted to arrive *equal*, because the folding happens once at parse time. An
  implementation that kept the source's arity would make two equal styles compare unequal,
  and the failure would only surface at the phase that starts comparing computed styles.
* **A component's kind is part of its identity.** `left` and `0px` are asserted *unequal*
  on purpose. They are equal as offsets and different as values, and the phase that
  resolves them against a box is entitled to two answers.
* **The shorthand's reset is asserted, not implied.** `background: red` sets the longhands
  it does not name back to their **initial** values. This is the one shorthand behaviour a
  "populate what you can" implementation gets wrong while every value it writes is correct,
  and it is only visible when an earlier declaration exists for the same longhand.

## Goal 9.15 — CSS borders

**Status: complete.** The twelve border longhands as typed values — three kinds over four
sides — with the phase's central act a **move**: the distribution of the three border rows
left the box model's pass for one owner that can see all four declarers at once, because a
longhand two passes materialise is a longhand with two entries. The style vocabulary is the
fifth closed set, the classifier's question *order* is the whole logic, and `currentcolor` is
the one initial value in the registry that is a reference rather than a value. Nothing paints,
no edge is measured, and `border-radius` is not a value this build has.

### Delivered

| Item | Where |
|---|---|
| The style vocabulary | `CssBorderStyle` — ten values as a closed set in the shared row table, each row carrying whether it `generates_border`, which is the fact that makes a `none` border's *used* width zero whatever `border-width` says |
| The kinds | `CssBorderKind` — WIDTH / STYLE / COLOUR, in the order a serialisation writes a composite (`1px solid red`) |
| The twelve longhands | `RoboCssBorderLonghandCount` = 4 sides × 3 kinds, with `CssBorderLonghandName` reading the names from the box table's positional rows so `border-top-width` is spelled once in the build |
| The positional rows | `border-width`, `border-style`, `border-color` on 9.11's 1/2/3/4 mapping — and the box table *grew* the two rows it never had, because nothing before this phase spread them over four sides |
| The composites | `border` and the four side shorthands, each spanning more than one kind |
| The classifier | `CssBorderClassify` — five outcomes for three kinds, with the order of the questions (style, then width keyword, then colour name) as the logic, a percentage classified as a width because the registry admits it, and `var()` as a fourth answer rather than a guess |
| The distribution | `CssBorderExpand` splits one composite into three parts, leaving `KIND_NONE` for a kind the author did not name — which the composite *resets* to its initial value while a positional row has no such case at all |
| The ownership move | `inherit_expand_border_shorthands` is the one pass that materialises the twelve, reading four declarers per longhand and weighing them with `CssCascadeCandidateCompare`; the box pass now asks `CssBorderDistributesShorthand` before it distributes a row |
| `currentcolor` | The four colour longhands start at `currentcolor`, the registry's only *reference* initial, resolved at both doors — the resolve pass and `CssStyleContextQueryComputedValue` — so an element that declared no border colour and one whose shorthand reset it agree |
| The computed shape | `RoboCssComputedValueBorderStyle` — the fourteenth kind, the fifth closed vocabulary, and the one shape a style needed because a style is neither a keyword nor a length |
| The registry rows | Fourteen new non-inherited rows and their initials (`medium`, `none`, `currentcolor`). `border-width` and `border` were already rows — a shape change is not a new property |
| The diagnostic domain | `RoboCssDomainBorder` |
| Version | `RoboCSSVersion.h` 0.15.0; capability `borders` = 1 |
| Documentation | `css/docs/Borders.md` (new, the design record), plus `README.md`, `docs/Architecture.md`, `docs/Tests.md`, `docs/ComputedValues.md` |

### Definition of done

- [x] All twelve longhands parse, on shapes that already existed for two of the three kinds
      (`LINE_WIDTH` from 9.11, the 9.9 colour) and one new shape for the third.
- [x] The four side shorthands parse and populate the corresponding width/style/colour
      longhands through the existing declaration architecture.
- [x] `border-width`, `border-style` and `border-color` work, on the same 1/2/3/4 mapping the
      box model already had.
- [x] `border` works for supported syntax, with a kind it does not name **reset** to that
      longhand's initial value.
- [x] Values participate in the existing cascade with no border-specific precedence logic.
- [x] All fourteen rows are non-inherited, proven from both sides, and an explicit `inherit`
      still works.
- [x] Correct initial values are represented through the existing computed-style system,
      including `currentcolor` resolving at both doors.
- [x] Computed-style accessors expose typed border values — an enumerator for a style, never
      the keyword's letters.
- [x] Invalid declarations fail safely, each refusal with its own wording.
- [x] Focused border tests pass (21 cases) plus a real-DOM pipeline test.
- [x] All Phase 9.1–9.14 tests still pass; host and QEMU runtimes still work; no Chromium
      dependency was introduced; no painting, border geometry or `border-radius` was
      implemented prematurely.

### What 9.15 found

**Four things, and the first three were gaps rather than mistakes** — the phase's own tests
are what surfaced them, which is the same pattern 9.13 and 9.14 recorded:

* **The style set had no shape of its own.** `border-style` could reuse nothing: a style is
  neither a keyword nor a length, so `BORDER_STYLE` became the fourteenth computed-value kind.
  The *read-back* is the assertion that matters — a table whose rows and enumerators had
  drifted would still answer `is_known` for all ten, and would only show as a value printing
  as another row's keyword.
* **`none` is both a value and an absence.** `CssBorderStyleFromSlice` answers `NONE`
  for the keyword `none` and for "not one of the ten", because there is one enumerator for
  "nothing". The suite asserts the trap directly rather than leaving the property layer to
  rediscover it: a `border-style: banana` that computed to `none` would be a border that
  vanished while every declaration looked like it worked.
* **The box table had a row nothing distributed.** `border-style` and `border-color` were
  absent as shorthand rows because nothing before this phase spread them over four sides.
  Adding them is a two-line change *once the table exists*, which is the argument for having
  the table rather than a per-family mapping.
* **The distribution had two owners, and no test would have caught it.** 9.11's `border-width`
  row was distributed by the box model's pass. That was right in isolation and wrong in a
  build where `border` and a side shorthand cover the same longhands, and the failure mode is
  not a crash or a wrong value — it is a *last-writer-wins* answer that depends on the order
  the passes happen to run in. One owner and one predicate is the fix, and the fix is the
  phase.

The one expectation that moved is again the count: the registry suite asserted 49 properties,
16 inherited and 33 non-inherited, and the fourteen new non-inherited rows made the numbers
**63, 16 inherited and 47 non-inherited**. `border-width` and `border` were already rows — the
phase distributes them rather than re-registering them, and a shape change is not a new
property. The structural assertions beside the counts went green untouched.

One documented flake, unchanged and not this subsystem's: running the whole suite from a
shell Windows does not consider entitled to the foreground fails `window.host.keyboard`, which
asserts `WindowRequestFocus(id) == RoboOk` and gets `RoboNotSupported` when
`SetForegroundWindow` is declined. It passed 3/3 standalone and in both the plain and the
sanitized full suite runs of this phase, and no `window.host.*` case reaches any RoboCSS code.
See `docs/Build.md`.

## Goal 9.16 — CSS conformance tests

**Status: complete.** The only phase of Phase 9 that adds **no production code at all** — one
suite and a version bump, because a promise about the *build* is part of the build. The
forty-one suites before it are each trustworthy for the same reason they were writable: the
question fits inside one module. The questions that are only true of the *product* are the four
this phase asks, and `css/docs/Conformance.md` is their design record.

### Delivered

| Item | Where |
|---|---|
| Every initial value computes | `css.conformance.registry.every_initial_computes` — the registry's own 63 rows in its own order, each initial run through the normaliser the row's own `computation` names, with the table's length asserted against `CssPropertyRegistryCount` so a property added later cannot go uncovered, and each row carrying its kind, its canonical text *and* its name so a drifted row fails as a named mismatch rather than as a count that still agreed |
| The reference initial | `css.conformance.registry.reference_initial` — the four `currentColor` border colours, plus the control that is a value and not a reference |
| The computation vocabulary | `css.conformance.registry.computations_are_known` — every row's `computation` is a *named* enumerator, because an unnamed one prints as `"unknown"` in a diagnostic and nothing else in the build reads that function |
| The matrix | `css.conformance.matrix.declarations` — 66 representative declarations, one per property, each with its exact computed value **and kind** |
| The matrix through the pipeline | `css.conformance.matrix.through_the_pipeline` — the same 66, on real elements, through tokenizer, parser, matcher, cascade, inheritance and computation |
| One document, every family | `css.conformance.document.every_family` — a realistic sheet with every family asserted on one element *and* on its child |
| The ordering keys across families | `css.conformance.document.cascade_across_families` — specificity, source order, importance and the inline level |
| Shorthand and longhand | `css.conformance.document.shorthand_and_longhand` — both orders, for all five distributing families |
| The three declarers | `css.conformance.document.three_declarers` — one longhand, three declarations at three keys, one owner |
| The whole chain, stage by stage | `css.conformance.pipeline.tokenizer_to_computed` |
| The malformed corpus | `css.conformance.pipeline.malformed_corpus` — 42 hostile documents through the whole chain, for crash safety |
| Determinism | `css.conformance.pipeline.determinism` — whole *styles* compared by `CssComputedValueEquals` rather than by text, so a difference in formatting cannot hide one in value or the reverse |
| Lifetime | `css.conformance.pipeline.lifetime` — the allocator returned, every round |
| The capability flags | `css.conformance.capabilities.every_flag` — every field of the struct asserted *by name* against a table that names all of them |
| Version | `RoboCSSVersion.h` 0.16.0 — no new capability, because 9.16 promises that every *other* flag is what its phase said |
| Documentation | `css/docs/Conformance.md` (new, the design record), plus `README.md`, `docs/Architecture.md`, `docs/Tests.md`, `docs/ComputedValues.md` |

### Definition of done

- [x] The tokenizer, parser, selectors, cascade, specificity, inheritance, computed values,
      colours, units, box model, display, positioning, backgrounds and borders are each covered
      by the Phase 9 suites that already existed (42 suites, 609 cases).
- [x] Full-pipeline integration is asserted end to end on real elements and on one realistic
      document, not only per module.
- [x] Invalid CSS is verified to fail safely — a 42-document malformed corpus through the whole
      chain, plus the crash-safety cases inside each family suite.
- [x] No existing Phase 9 test was weakened or deleted; no new CSS feature was added to make a
      test pass; no implementation defect was papered over.
- [x] No Phase 10 or later functionality was started.

### What 9.16 found

**Two whole-product facts, and neither was reachable from inside a family suite:**

* **Two capability flags were documented and asserted nowhere.** 9.14 and 9.15 each recorded
  that their flag had flipped to 1. `css.capabilities` stopped at `positioning` and then tested
  the two flags that are 0, so `caps.backgrounds` and `caps.borders` could each have been 0 in
  the shipped build while every family suite stayed green — because a capability flag is a
  promise about the *build* and a family suite tests its family. The fix is a table naming every
  field of the struct, walked field by field, so a field added later is a visible omission
  rather than a silently untested flag.
* **A registry row can compute `INVALID` on every element of every document and nothing would
  say so.** `css.registry.initial_values` asserts the initial value's *shape* — a 9.1 fact about
  a value tree — and says nothing about what that shape computes to under the row's own
  `computation` column. A drifted pair produces an `INVALID` for every element, declared or not,
  and the only other place it would show is a render, which does not exist yet.

Two further findings were bugs in **the suite itself**, recorded because they are the failure
mode a conformance suite is most prone to: a cascade case whose selector named a class no
element had, so the declarations it compared were all absent rather than competing; and a
per-context inline stub that answered every element instead of the one under test, so an
assertion about a child was really an assertion about the root.

## Goal 10.1 — Layout architecture

**Status: complete.** The first goal of Phase 10, and deliberately the whole of it: 10.1 builds
the *shape* of a layout pass and states the boundaries the following fourteen goals fill in. It
computes **no coordinate** — `LayoutGetCapabilities().geometry` is 0,
`LayoutGeometryIsImplemented()` answers 0, and a pass leaves every box `UNRESOLVED`,
which is a statement about this build rather than a placeholder for one. `layout/README.md` and
`layout/docs/` are the design record.

### Delivered

| Item | Where |
|---|---|
| The layout tree, distinct from the DOM | `RoboLayoutTree.h`, `RoboLayoutObject.h` — an owned arena, dense tree-scoped ids, parent/child/sibling links by id, a reverse lookup from element to box, and `add_object` validating every rule a later goal would otherwise re-check |
| The box | `RoboLayoutObject.h` — an inert DOM handle, a **borrowed** computed style, the one CSS value that is copied (`display`, because the box would not exist without it), its links, its depth, its flags and its result |
| Constraints that can say "unset" | `RoboLayoutConstraints.h` — available/minimum/maximum, each with an explicit flag, plus `is_valid` and `clamp`, which is the module's only bounds arithmetic. `available` is deliberately **not** an implicit maximum: filling it is 10.3's decision |
| A result with a state, not a rectangle | `RoboLayoutResult.h` — `NONE` / `UNRESOLVED` / `COMPUTED`, two overflow flags for 10.13, a stated coordinate space (containing block, border-box) and refusals for impossible geometry |
| The pass | `RoboLayoutContext.h` — one context per pass, the viewport as *available space only*, the stats, the outcome, and the user-data seam a later formatting context hangs scratch state on |
| The builder's one rule | `RoboLayoutBuilder.h` — an element generates a box unless its computed `display` says otherwise; children are **elements** (so `+` and `:first-child` mean the same thing here as in the matcher), the root is the document element, and an empty build is an answer rather than a failure |
| The CSS seam | `RoboLayoutStyle.h` — a callback, so the builder is testable **without CSS**; a source that answers nothing yields `STYLE_MISSING`, which is a fact a formatting context checks rather than an accident |
| The bridge | `RoboLayoutDom.h` — the first module in the runtime that depends on RoboHTML *and* RoboCSS (DEPENDENCIES.md: whoever needs both sits above both); the element reference is the DOM node id; element facts are captured before any style is computed, because RoboCSS borrows them; and the bridge owns one computed style per element so the tree can borrow it |
| Borrowed geometry | `RoboLayoutGeometry.h` — aliases of RoboGraphics' canonical pixel geometry. A second point/size/rectangle would be two types that agree today and differ in a year, with paint converting between them at the one boundary where a mistake is a visible artifact |
| The module surface | `RoboLayout.h`, `RoboLayoutTypes.h`, `RoboLayoutDiagnostics.h` — lifecycle, version, the nine-field capability struct, one object kind, eleven diagnostic domains, and the same optional caller-owned diagnostic record RoboHTML and RoboCSS use |
| Seven suites, 71 cases | `layout/tests/` — module surface, tree, constraints, context/pass, builder, DOM bridge, and conformance; plus `layout/docs/Tests.md` |
| Version | `RoboLayoutVersion.h` 0.1.0 — `RoboLayoutApiFrozen=0` (the surface grows for the whole of Phase 10) and `RoboLayoutGeometryImplemented=0` |
| Documentation | `layout/README.md`, `layout/docs/Architecture.md` (17 sections), `layout/docs/Tests.md`, plus root `ARCHITECTURE.md`, `DEPENDENCIES.md` and the plan tracker |

### Two decisions worth naming

* **Why not walk the DOM.** The two trees do not have the same members: a `display: none` element
  is a DOM node and generates no box, a text node is a DOM node and will generate something in
  10.5 that is not an element box, a future `::before` is a box with no element behind it. A pass
  that walked the DOM would re-decide which nodes it may look at at every step, and each of the
  fourteen goals that follows would add another such decision somewhere else. The tree is built
  **once**, so a pass walks boxes and only boxes.
* **Why a callback and not a style pointer.** So the structural suites can exist: a test that
  proves "`display: none` generates no box" has no business standing up a style context, a
  property table, a registry and a sheet. Five of the seven suites never instantiate a style
  context, and the two that do are exactly the two whose subject is the joining.

### Definition of done

- [x] A layout tree distinct from the DOM exists, with parent/child/sibling relationships,
      DOM-node association and computed-style association.
- [x] Layout objects represent DOM nodes and computed styles without copying either, and the one
      exception (`display`) is argued rather than incidental.
- [x] A layout context, a foundational constraints representation and a minimal result
      representation exist and are tested.
- [x] DOM → layout-tree construction works, including `display: none` and an empty document.
- [x] RoboCSS computed styles are consumed and no CSS is parsed inside RoboLayout; the one
      property it reads is read through 9.12's own accessor.
- [x] No Chromium dependency; `Tools/Build.ps1 -Verify` grew a RoboLayout arm asserting it, and
      also asserting that nothing below it in the graph (window, display) is reached.
- [x] All Phase 9 suites (609 CSS cases) and all DOM, graphics and window suites still pass.
- [x] No Phase 10.2 or later functionality was started.

### What 10.1 found

**Five things, two of them production bugs — both found by tests, not by reading:**

* **A dangling parent when the arena grows.** `LayoutTreeAddObject` resolved the borrowed
  `parent` pointer *before* the reserve that can move the arena, so a tree that outgrew its
  initial capacity linked a new box to a freed address: invisible below the growth boundary,
  a segfault above it. Fixed by resolving the parent after the reserve. `layout.tree.growth` is
  the only case whose stated purpose is to push a container past a boundary, and the only one
  that could have found it.
* **`passes` was not cumulative.** It reported the last pass's count while its own header
  documented a total. Every single-pass case passed; the case that found it exists to ask the
  question (`layout.context.run.repeated`), and its other half now also pins that a second pass
  re-unresolves geometry a previous pass had computed.
* **The test registry had a fixed cap.** Seven new suites overflowed a fixed-size array in
  `runtime/Tests/Framework/RoboTest.c`. It failed loudly, which is the right behaviour — a suite
  that is silently not registered is a suite that is silently not run — and it now fails loudly
  by design rather than by luck.
* **Two Release-only test defects, pre-existing.** `SuiteRoboHTMLDom.c` had a debug-only variable
  whose only use is inside `#if RoboMemDebug`, and two CSS suites asserted the Debug
  allocator's bookkeeping unconditionally. The project's default configuration is Debug, so
  Release `-Test` had never been exercised across the whole suite; both configurations are now
  green. 10.1 is the first thing to run all 2087 tests in Release, and it found three defects
  that have nothing to do with layout.
* **The `-Verify` boundary scan needed a RoboLayout arm.** RoboLayout is the first module whose
  forbidden set includes layers *below* it (RoboWindow, RoboDisplay) for a reason that is not
  layering — a pass that consulted a window could not be tested without one — so its scan is the
  first that is not purely "no upward edges".

## Goal 10.2 — Formatting contexts

**Status: complete.** The phase that answers the question every layout algorithm starts with:
*which boxes' layout am I coordinating, and in which direction?* It adds no coordinate — geometry
is still 0 — and it adds no algorithm. What it adds is the classification, the context tree, and
the two ids a box now carries.

### Delivered

| Item | Where |
|---|---|
| The four kinds | `RoboLayoutFormattingContext.h` — `NONE`, `ROOT`, `BLOCK`, `INLINE`, with `is_known`, `kind_name`, and `is_block_container` (true for `ROOT` and `BLOCK`), which is the one fact a later block pass and a later inline pass each ask first |
| The record | the same header — id, kind, owner, parent, participant count and depth, by value and owned by nobody, so reading one copies it and the tree is unaffected |
| **The classification, as a pure function** | `LayoutFcEstablishes(display, has_block_level_child, is_root)` — three facts, no tree, no pass, no allocation, so the rules are a table the suites can walk rather than a behaviour that can only be observed by building a document |
| Two predicates that are not the classifier | `LayoutFcDisplayIsBlockContainer` and `LayoutFcDisplayCanEstablish` — because "could this box ever establish one?" and "does it, given this child composition?" are different questions, and a caller diagnosing "why did nothing establish a context here?" needs both |
| Two ids on every box | `RoboLayoutObject.h` — `context` (where it participates) and `context_owned` (what it establishes, `NONE` for most boxes), read through `LayoutObjectContext`, `LayoutObjectContextOwned` and `LayoutObjectEstablishesContext` |
| A second arena on the tree | `RoboLayoutTree.h` — `context_count`, `is_classified`, `context_at`, `context`, `root_context`, and `invalidate_contexts`; dense 1-based ids, `id == index + 1`, the same discipline box ids follow |
| Classification in the pass | `RoboLayoutContext.h` / `RoboLayoutContext.c` — the same walk that marks every box `UNRESOLVED` now resolves the context tree, and the stats gained `contexts`, `contexts_by_kind`, `boxes_establishing`, `boxes_participating_only` and `context_depth_max` |
| A build or an add invalidates | `LayoutTreeInvalidateContexts` — a box arriving changes the child composition one of the classification's three inputs is made of, so the classification is released rather than left describing a tree that has moved |
| Eight suites, 84 cases | `layout/tests/SuiteRoboLayoutFormattingContext.c` plus five extended suites; `layout/docs/Tests.md` |
| Version | `RoboLayoutVersion.h` 0.2.0 — `ROBO_LAYOUT_FORMATTING_CONTEXTS_IMPLEMENTED=1`, the first capability flag this module flips |
| Documentation | `layout/docs/FormattingContexts.md` (new, 10 sections), plus `layout/README.md`, `layout/docs/Architecture.md`, `layout/docs/Tests.md`, root `ARCHITECTURE.md`, `DEPENDENCIES.md` and the plan tracker |

### Three decisions worth naming

* **A box has two relationships to formatting contexts, not one.** A box *participates* in one
  (where its own layout decisions happen) and *establishes* one for its contents (usually none).
  The tempting single flag — "this box is a block formatting context" — holds two of the three
  states a box can be in, and the third is the one that matters: a plain block box whose children
  are all block-level establishes **nothing** and is *transparent*, which is what makes a margin
  collapse through it and a float escape it. A build that made every block box a barrier would
  pass every test this phase writes and make 10.9 and 10.13 impossible.
* **`inline-block` establishes `BLOCK` because its inner display is `flow-root`.** The phase never
  names `inline-block`. 9.12 records the outer/inner pair precisely so that this module reads *one*
  inner value and is right about every future value that shares it.
* **The classification lives on the tree, not on the pass.** A later phase needs to ask *after* the
  pass that produced the answer — 10.12 resolves a containing block, 10.13 decides what a scroll
  container is — and neither should have to hold a pass object to ask which context a box is in.
  The pass is a thing that runs; the classification is a fact about the tree.

### Definition of done

- [x] Formatting-context architecture exists, with root, block and inline contexts established and
      identified, and a strongly typed vocabulary — with **no enumerator** for flex, grid, table,
      ruby or multicol, for the same reason 9.12 shipped no `display: flex`.
- [x] Classification is deterministic and consumes **computed** CSS: the only thing read out of a
      style is the outer/inner display pair, through 9.12's own accessors. No CSS is parsed here.
- [x] Contexts integrate with the 10.1 layout tree by referencing existing boxes; nothing about the
      DOM is duplicated, mutated, or re-derived.
- [x] Ownership, lifetime and nesting are explicit: one context per establishing box per pass, a
      new context's parent is the one its *owner* participates in, `depth` is the parent's plus one,
      and `ROOT` is depth 0 with no parent.
- [x] Rebuild/invalidation foundation exists: a build and an `add_object` release the classification,
      `invalidate_contexts` releases it explicitly and idempotently, and a context id read across
      one answers `RoboNotFound` rather than resolving against the new classification.
- [x] Focused tests cover the classification table, the two predicates, the record, ownership,
      transparency, inline boxes, `display: none`, nested depth, invalidation, lifecycle, validation
      and allocator balance.
- [x] All 2100 tests pass in Debug and Release, and under ASan + UBSan.
- [x] No Chromium dependency; `Tools/Build.ps1 -Verify` still scans this module for one.
- [x] No block or inline layout algorithm was implemented, and `geometry` is still 0.

### What 10.2 found

**Three things, one of them a production bug — found by a test, not by reading:**

* **A stale context id read across an invalidation.** A box's context id kept its value after the
  classification was released, so a read *after* a re-classification resolved the old id against
  the new context array — answering with a context that is not the one the caller asked about, and
  answering `RoboOk` while doing it. `layout.fc.tree.add_invalidates` is the only case whose stated
  purpose is to add a box to an already-classified tree. Both context doors now report
  `RoboNotFound` for a box in an unclassified tree, which is the truth.
* **A `const` tree in a signature that mutated through it.** One internal helper took the tree
  `const` while writing every box's result, and the compiler had nothing to say because the write
  went through a `const`-stripped pointer one level down. Corrected rather than cast away.
* **Two wrong expectations in the phase's own suite, both about nesting.** `layout.dom.pipeline`
  first asserted the deepest context sat at depth 1 with six root participants. The tree says 2 and
  5, and the tree is right: `<p>` is inside the atomic inline, so its inline context nests inside
  the panel's block context, and the root's participants are its owner plus the three boxes that
  reach it through a transparent `<body>`. The expectations were **replaced**, not merely
  corrected — the case now asserts the parent link and the depth of the nested pair, which is the
  assertion a flat-but-correctly-counted context tree fails.

## Goal 10.3 — Block layout

**Status: complete.** The first goal of RoboLayout that computes a **coordinate** — the point every
phase before it was built to reach. 10.1 built the tree and a pass that walked it without resolving
anything; 10.2 answered *which boxes' contents are coordinated in which direction* and still
computed nothing; 10.3 takes that answer and lays the block direction out.

| Deliverable | Where |
|---|---|
| The pass | `RoboLayoutBlock.h` / `RoboLayoutBlock.c` — `LayoutBlockLayout(tree, viewport, &stats, diag)`, the module's first function that writes a position and a size |
| Containment, kept apart from context | A box is placed inside its **parent**; its **formatting context** decides which pass stacks its contents. The walk descends the parent link always, and the placement gate is 10.2's own `LayoutFcKindIsBlockContainer` — the distinction 10.9 and 10.12 would otherwise force a rewrite of |
| The width rule, stated | A block box fills its containing block's content width, which is CSS's `width: auto`. A declared `width` is readable and **not applied**: resolving `200px`, percentages, `min-width` and `box-sizing` is 10.8 |
| The height rule, stated | A declared `height` is honoured where this build can resolve it — a computed `px` length — and everything else (`auto`, a percentage, `em`, `vw`) takes the content path rather than a guess. One property, one kind check, one unit check: not a sizing algorithm |
| Vertical stacking | Block-level children from the content origin, each below the last; a parent's height is the stacked extent of its children, so an empty container is legitimately zero tall |
| A record of the mix | `RoboLayoutBlock.h` — `containers`, `blocks_placed`, `inline_level_skipped`, `unreached`, `heights_explicit`, `heights_from_content`, `empty_containers`, `depth_max`. Counters rather than one total, because a tree of transparent containers and a tree of placed leaves can share a count and have nothing else in common |
| Geometry withdrawn with the classification | `RoboLayoutTree.c` — `contexts_reset` now demotes `COMPUTED` results to `UNRESOLVED`, because block geometry is a *function of* the classification. Written once in the shared path so all three callers agree, and conditioned so a box that never had a conclusion keeps `NONE` |
| The pass wired into the context | `RoboLayoutContext.c` — a context that recorded a viewport runs the block pass after classifying, and derives `boxes_computed` from `blocks_placed` so the two cannot disagree. A context without one places nothing and is **not** an error |
| Nine suites, 100 cases | `layout/tests/SuiteRoboLayoutBlock.c` (16) plus eight suites updated; `layout/docs/Tests.md` |
| Version | `RoboLayoutVersion.h` 0.3.0 — `RoboLayoutGeometryImplemented=1` and `RoboLayoutBlockLayoutImplemented=1`, with `inline_layout` and `block_and_inline_layout` still 0 |
| The design record | `layout/docs/BlockLayout.md` — containment versus context, the width and height rules, what the pass declines to place and which goal owns each refusal, determinism, and the invalidation rule |

### Completion criteria

- [x] A real block-layout algorithm: a position and a size per block-level box in a block-direction
      context, in the containing block's coordinate space.
- [x] Deterministic traversal — creation order, which the builder makes document order, siblings in
      link order — asserted by two passes over one tree and two trees from one document.
- [x] The containing-block relationship is established and asserted at three levels of nesting: a
      child is placed at **its parent's** content origin, including when the parent is transparent
      to formatting context.
- [x] Available width propagates: a child's width is its containing block's content width.
- [x] Siblings stack vertically, each starting where the previous one's extent ended.
- [x] `display: none` is excluded — it generates no box at all, so it is absent from the counts too.
- [x] Inline objects are not processed as blocks, and are **counted** (`inline_level_skipped`,
      `unreached`) rather than silently dropped.
- [x] Layout results integrate with the 10.1 architecture, and the formatting contexts of 10.2 are the
      placement gate rather than an input the pass re-derives.
- [x] Re-layout after a relevant input change recalculates correctly, and an invalidation now also
      withdraws the geometry derived from it.
- [x] All 2116 tests pass in Debug and Release, and under ASan + UBSan.
- [x] No Chromium dependency; `Tools/Build.ps1 -Verify` still scans this module for one.
- [x] No inline layout, no line box, no anonymous box, and no deferred arithmetic (declared width,
      margins, padding, borders, positioning, overflow) was implemented.

### What 10.3 found

**One production bug and two fixture problems — and no fourth finding, which is itself worth stating:**

* **An invalidation released the classification but kept the geometry derived from it.** 10.2's
  `LayoutTreeContextsReset` dropped the context arena and left every box's result as it was —
  correct while nothing derived geometry from the classification, and wrong from the moment the block
  pass started reading its placement gate out of `object->context` and taking a box's height from the
  children that gate let through. A surviving result was a `COMPUTED` state asserting a coordinate
  about a tree that no longer existed. `layout.block.invalidation` is the case that found it, and it is
  the same code path 10.2's `layout.fc.tree.add_invalidates` covers one level up: both phases found a
  stale fact there, and the second only became reachable when something started depending on the first.
  Fixed in the shared reset rather than in the invalidation door, because all three callers are saying
  the same thing; and conditioned on the previous state, so `NONE` stays reachable and eight adds do
  not manufacture `UNRESOLVED` for every box.
* **A fixture that promised a state the builder cannot produce.** The invalidation case needs a
  document it can add an element to. Building one through the tree builder does not help — the builder
  finalizes at EOF, so no document it produces can ever accept a new element — and the first attempt to
  leave one un-finalized depended on a state the builder never produces. The fixture took the honest
  tool (`robo_html_document_create_element` over a live document) and the production builder was left
  alone: a fixture working around a limitation is one thing; a fixture editing production to suit
  itself is how a suite stops testing the thing it names.
* **Three suites the phase's own numbers invalidated.** `layout.module`'s version and capability pins,
  `layout.conformance.statement`'s `geometry == 0` claims, and `layout.conformance.no_geometry` all had
  to move with the phase. None was weakened and none was deleted: the pins were re-pinned, `statement`
  now asserts `1`, and `no_geometry` was **replaced** by `no_viewport` — the sharper claim that was true
  all along and still is (`geometry == 1` describes a *build*, while a pass with no recorded viewport
  places nothing), with the numbers moving to `layout.block.*`.
* **The block algorithm itself came out right on the first build.** Reported as a fact about the phase
  and not as a virtue: the nine suites around it had already pinned every input the pass reads — the
  placement gate, the containing-block convention, the display vocabulary — so the arithmetic had
  nowhere to hide.

### The one distinction the phase turns on

10.2 called a block container holding a block-level child **transparent**, and the word invites one
dangerous reading: *not in the chain*. If a child were placed inside the nearest box that *establishes*
a context rather than inside its parent, a grandchild's origin would be the grandparent's, and a document
whose `<body>` is transparent would look laid out while being wrong about every nested box — and
margin collapsing (10.9) and positioned containing blocks (10.12) would each have to unpick it.

So the two questions are asked separately and answered from two different places: *what am I laid out
inside* is the **parent link, always**; *which direction stacks my contents* is the **formatting
context**. `layout.block.context_gate` makes the second claim impossible to break over every box in a
tree, and `layout.block.nested` asserts the first at three levels.

## Goal 10.4 — Inline layout

**Status: complete.** The other direction. 10.3 computed a coordinate in the one axis block flow owns
— *down* — and left two counts standing for the boxes it could not place (`inline_level_skipped`,
`unreached`) with a note saying they were 10.4's to retire. This phase retires the first of them, and it
does it as a **branch of the same pass** rather than as a second entry point: an inline formatting
context's height *is* the block extent of the box that owns it, so block layout cannot finish before
the lines exist and the lines cannot be laid out before the block pass knows where their owner is.

| Deliverable | Where |
|---|---|
| The direction | `RoboLayoutInline.h` / `RoboLayoutInline.c` — `LayoutInlineWalkContents(walk, owner, width, &extent)`, reached only by the block pass. There is deliberately **no** `robo_layout_inline_layout` beside `LayoutBlockLayout`: a second public entry point could only duplicate that walk or run it in secret |
| Atomic versus transparent, the phase's one distinction | An inline-level box is **atomic** exactly when it established a formatting context of its own (`inline-block`, whose inner display 10.2 records as `flow-root`) and **transparent** when it established nothing (`inline`). Derived from the two facts the tree already carries — 9.12's outer display and 10.2's ids — with no third flag and no "is this an inline-block" predicate in the module |
| The line, and what it is not | A cursor, a top, a running height and a count. A line *box* has identity, alignment, a baseline and a font-derived height, and it is 10.6's; the record reports the line **count** and nothing about any individual line |
| Wrapping, and its one guard | A participant whose width is known before it is placed that does not fit after the cursor starts a new line. The `cursor > 0` half of the test is CSS's rule about a box wider than its containing block: placed anyway and allowed to overflow, rather than starting lines forever |
| Width: three answers | An atomic inline is sized by its own declaration where it resolves (a computed `px` length) and fills its containing block otherwise; a transparent inline is sized by *neither*, and its rectangle is the **union** of what its contents reached. The declared/filled split is counted (`widths_declared`, `widths_filled`) because the difference is what a reader is trying to make |
| An atomic inline's contents | Handed to the block pass in the box's own space through `LayoutBlockWalkChildren` — the one door, so there is no second child loop. A `<p>` inside a 300px `inline-block` comes out exactly as wide as the span and inside it |
| Rebasing | Participants are placed in the owner's space and then rebased onto their own containing block, because a transparent box's origin is a **union** and a later sibling that wraps can move it. A second walk, and a *pass* rather than a placement, because of when the union is known. It stops at an atomic inline, whose contents are already stated relative to it |
| The second record | `RoboLayoutInline.h` — `contexts`, `empty_contexts`, `participants_placed`, `atomic_placed`, `non_atomic_placed`, `block_level_declined`, `lines`, `wraps`, `widths_declared`/`widths_filled`, `heights_declared`/`heights_from_content`, `depth_max`. Reported beside the block record and derived from neither, because it is one pass asked two questions |
| The accounting closes over both directions | `blocks_placed + inline_level_skipped + participants_placed + unreached == objects`, asserted over every laid-out tree, so a build that dropped a box would be visible as a document that did not add up |
| Ten suites, 123 cases | `layout/tests/SuiteRoboLayoutInline.c` (23) plus three suites updated; `layout/docs/Tests.md` |
| Version | `RoboLayoutVersion.h` 0.4.0 — `RoboLayoutInlineLayoutImplemented=1`, `inline_layout` and the derived `block_and_inline_layout` now 1 |
| The design record | `layout/docs/InlineLayout.md` — atomic versus transparent, the three width answers, the wrapping rule and why a preliminary line is not a line box, rebasing, and why there is no second entry point |

### Completion criteria

- [x] An inline-layout algorithm exists, and Inline Formatting Contexts are actually the gate it runs
      on: a box's contents are placed along a line exactly when the context it *participates in* is an
      inline one.
- [x] Inline children are traversed deterministically — sibling order, descending *through* transparent
      boxes so a `span`'s children are reached in the same loop as the `span`.
- [x] Basic inline width and height are represented: an atomic inline's declared width/height where
      they resolve, the containing block's width and the contents' extent otherwise.
- [x] Inline objects progress horizontally, and `B.x == A.x + A.width` is asserted from the results
      rather than from the input.
- [x] Basic wrapping occurs when the available width is exhausted, and a box wider than its line is
      placed anyway rather than wrapping forever.
- [x] Nested inline objects are supported, and the coordinate convention survives a rebase at two
      depths.
- [x] Block/inline integration works for simple mixed content: an atomic inline's contents are laid out
      by the block pass inside it, and a block-level box among inline ones is **counted**
      (`block_level_declined`) rather than placed badly.
- [x] `display: none` is excluded, and block objects are not processed as inline.
- [x] Layout results contain valid x/y/width/height, in the containing block's coordinate space.
- [x] Re-layout works when available width changes — a narrower viewport moves `lines` and `wraps` — and
      an invalidation still withdraws the geometry derived from the classification.
- [x] All 2139 tests pass in Debug and Release, and under ASan + UBSan.
- [x] No Chromium dependency; `Tools/Build.ps1 -Verify` still scans this module for one.
- [x] No line box, no text measurement, no font metrics, no baseline, no anonymous box, and no
      deferred arithmetic (declared width in the block direction, shrink-to-fit, margins, padding,
      borders, positioning, overflow) was implemented.

### What 10.4 found

**No production bug, and two mistakes in the phase's own suite — which is the interesting shape:**

* **A fixture that declared one tag twice, and a lookup that silently absorbed it.** The suite's shared
  `LAYOUT_INLINE_BLOCK_RULES` macro declares the three containers every case shares, `p` among them,
  and one case that needed a *sized* paragraph appended a second `p` rule after it. The fixture's style
  lookup answers with the **first** match by tag name, so the case ran silently against the macro's
  height-less `<p>` — surfacing as a span that was zero tall rather than as a rule that never applied.
  Fixed in the fixture rather than the case, and fixed as a **refusal**: `layout_inline_style_add` now
  asserts the tag is not already declared, so two rules for one element fail immediately with a
  readable message. A fixture is a script, and a script with two lines for one element is a mistake in
  the test rather than a shape a document can have.
* **Object counts that ignored the parser's own `<head>`.** `layout.inline.no_viewport` asserted four
  boxes for a four-tag markup string; RoboHTML's builder inserts the `<head>` a well-formed document
  requires, so the answer is five. `RoboLayoutTestSupport.h` already argued the rule in its own text —
  the element count of a fixture is a fact about the *parser*, so assert the number **and say which
  elements are being counted** — and the case now carries that list rather than a bare 5.
* **The inline algorithm itself came out right on the first build**, for 10.3's reason: the ten suites
  around it had already pinned every input the pass reads — the two context ids, the
  atomic/transparent derivation, the containing-block convention — so the cursor arithmetic had nowhere
  to hide.

It is also the second phase in a row to meet the host-window flake and the first to meet **only** it:
`robo test-runtime` aborted at the Debug step with 2138 of 2139 and `window.host.keyboard` the single
red case, while the same binary was 2139/2139 twice within the minute and `--filter=window.host` was
19/19 three times. Recorded rather than worked around — the assertion is right and the OS denial is the
anomaly — and `git status` reports **no** file matching `window` in either phase's diff.

### The one distinction the phase turns on

An inline-level box comes in two kinds, and they are not degrees of the same thing. One is **opaque to
the line** — it occupies a run, what is inside it is laid out inside it, and its width therefore has to
be known before it is placed, which is what makes it the one participant the wrapping rule can act on.
The other is **transparent to the line** — its children are participants of the *same* context, placed
at the same cursor, and its own rectangle is the **union** of what they reached.

Reading that the wrong way round is the expensive mistake both times, exactly as 10.2's
participating/establishing pair was: treat a `span` as opaque and it is placed with no width (nothing
here measures text) so everything inside it is dropped, and the document looks laid out with boxes
missing; treat an `inline-block` as transparent and its contents stack in the outer line, producing
plausible numbers in the wrong coordinate space. So atomicity is derived rather than stored, and
`InlineLayout.md` §2 keeps the two questions apart the way 10.3's §2 keeps containment apart from
context.

## Goal 10.5 — Text layout

**Status: complete.** The phase that makes a `<span>` holding only text as wide as its font says — and
the one that widens the object vocabulary for the first time since 10.1.

### Delivered

| Piece | Where |
|---|---|
| Code-point ranges, direction, the resolved font inputs, the run record, the measurement seam | `Include/RoboLayout/RoboLayoutText.h` |
| Range and byte-span helpers, style resolution from computed CSS, whitespace predicate, the text counters | `Source/RoboLayout/RoboLayoutText.c` |
| The RoboFont-backed measurer — cmap + hmtx, summed in font units and scaled once | `Include/RoboLayout/RoboLayoutTextFont.h`, `Source/RoboLayout/RoboLayoutTextFont.c` |
| `RoboLayoutObjectText`, and the derived inline display | `RoboLayoutTypes.h`, `RoboLayoutObject.h` |
| The builder's second rule, with `whitespace_nodes_skipped` in its report | `RoboLayoutBuilder.c` |
| Text placed on the line, the text record, and the accounting identity closing over it | `RoboLayoutInline.c`, `RoboLayoutBlock.c`, `RoboLayoutContext.h` |
| Version `0.5.0`, `RoboLayoutTextLayoutImplemented`, the umbrella | `RoboLayoutVersion.h`, `RoboLayout.h` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A text node becomes a layout object and borrows its parent's style | `layout.text.build`, `layout.text.object_model` |
| A real font measures a run | `layout.text.font_measure` — against a synthetic SFNT built in the suite |
| Text is an ordinary participant of the line | `layout.text.pass_measured`, `layout.text.pipeline` |
| A pass with no metrics source is legitimate and honest | `layout.text.pass_unmeasured` |
| Ranges are code points, not bytes | `layout.text.*`, and RoboUnicode through them |
| The identity still closes | `layout.text.accounting`, `layout.block.*` |

### The one distinction the phase turns on

Three answers to "measure this", and the type keeps all three apart: a **source that
is not there** (`RoboNotSupported`, counted `unmeasured`), a **source that failed** (its own
`Result`, counted `unmeasured`), and a **source that answered** (a width).
`TextLayout.md` §4 argues it at length, and it is why `LayoutTextFontMeasure` refuses a
detached font rather than answering zero: "I cannot measure" and "this is zero wide" are different
statements, and a phase that conflated them would ship a document laid out as if every character
were absent.

### What 10.5 found

A measurement seam that produced numbers from a **detached** font — not a crash, which is worse,
because a null face still has a shape in memory. `layout.text.font_source` is the case, and the fix
is the refusal above. Reported rather than smoothed over: the failure it would have caused is a
document whose text is the wrong width in a way nobody can attribute to anything.

---

## Goal 10.6 — Line boxes

**Status: complete.** The line stops being a cursor on the stack and becomes a record a caller can
read back — with identity, geometry, an ordinal within its context, and the objects that went on it.

### Delivered

| Piece | Where |
|---|---|
| The record, the two-state vocabulary, the read side | `Include/RoboLayout/RoboLayoutLineBox.h` |
| Line creation and finalization in the inline pass | `Source/RoboLayout/RoboLayoutInline.c` |
| The two arenas — line records and the interleaved member chain — and their release | `Source/RoboLayout/RoboLayoutTree.c`, `RoboLayoutInternal.h` |
| `LayoutTreeLineCount` / `_line_at` / `_line` / `_line_member`, `LayoutObjectLine` | `RoboLayoutTree.h`, `RoboLayoutObject.h` |
| `line_members` in the inline record, beside the unchanged `lines` | `RoboLayoutInline.h` |
| Version `0.6.0`, `RoboLayoutLineBoxesImplemented`, the umbrella | `RoboLayoutVersion.h`, `RoboLayout.h` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| Construction state and the record are different things | `layout.line.record`, `layout.line.state_vocabulary` |
| Inline content produces lines, in the right order and at the right y | `layout.line.single_run`, `layout.line.wrapping` |
| Members are the **placements**, not the participants | `layout.line.members_are_placements`, `layout.line.empty_span_is_a_member` |
| A line whose only member is unmeasured exists and is 0 × 0 | `layout.line.unmeasured_run` |
| An empty context produces no lines | `layout.line.empty_context` |
| A second pass replaces rather than accumulates | `layout.line.repeat_pass_replaces`, `layout.line.allocator_balance` |

### The one distinction the phase turns on

A line's **members are not the context's participants**, and the difference is exactly the transparent
inline boxes that enclosed something. A union can span several lines, so recording such a box as a
member of one of them would either state a rectangle it does not have or double-count the width its
children already contributed. `LineBoxes.md` §2 and §9 argue it, and
`layout.line.members_are_placements` is a *walk over the arena* on a document that nests inline boxes,
where the two counts differ — so both plausible implementations (record it, or skip an empty one) fail
it in opposite directions.

### What 10.6 found

One Release-only defect, and it was in this phase's **own new case**: an `allocator_balance` case
asserted that destroying the tree *lowered* the Debug allocator's live count, which is a fact that does
not exist in a Release build — `layout_live_blocks` reads 0 there because the counter behind it is a
`RoboMemDebug` instrument. So it passed in Debug and failed in Release while saying nothing about
line boxes in either. Rewritten to the equality every other case in the directory already uses, and
broadened to bracket the whole fixture rather than only the passes. That is 10.1's fourth finding
recurring two phases later and one level down, and the first time a phase made the mistake itself
rather than inheriting it.

## Goal 10.7 — Baselines

**Status: complete.** Every line gets a **floor** — an ascent, a descent and the kind they came from
— and every member is placed *on* it rather than at the line's top edge.

### Delivered

| Piece | Where |
|---|---|
| The three kinds, the metrics record, its arithmetic, and the two rules that produce it | `Include/RoboLayout/RoboLayoutBaseline.h`, `Source/RoboLayout/RoboLayoutBaseline.c` |
| The line's floor, in three fields, plus the two baseline doors (`_baseline`, `_baseline_offset`, `_baseline_y`) | `RoboLayoutLineBox.h`, `RoboLayoutLineBox.c` |
| The metrics an object was **placed** with, and the object door that reads them | `RoboLayoutObject.h`, `RoboLayoutObject.c` |
| The accumulation, the finalization walk that writes each member's y, and the union moved after it | `Source/RoboLayout/RoboLayoutInline.c` |
| The two counters that partition the lines | `RoboLayoutInline.h` |
| Version `0.7.0`, `RoboLayoutBaselinesImplemented`, the umbrella | `RoboLayoutVersion.h`, `RoboLayout.h` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A dedicated baseline representation exists | `layout.baseline.kind_vocabulary`, `layout.baseline.metrics_record` |
| Text metrics and the fallback are two rules, each stated once | `layout.baseline.metrics_from_run`, `layout.baseline.metrics_from_extent` |
| A line's baseline is its members' maxima, and its height is their sum | `layout.baseline.line_from_text`, `layout.baseline.mixed_size_is_not_top_aligned` |
| Members of different sizes share one floor | `layout.baseline.shared_floor`, `layout.baseline.descent_deepens_the_line` |
| Every member's own baseline lands on the line's | `layout.baseline.accounting` |
| Empty content has three different answers, and none is invalid | `layout.baseline.empty_content` |
| Re-layout recalculates baseline-dependent geometry | `layout.baseline.relayout`, `layout.baseline.determinism` |
| The phase added no arena | `layout.baseline.allocator_balance` |

### The two structural claims

**One fact, two moments.** A participant's own metrics are recorded when it is *placed*, because that
is when the measurement is in hand; its final `y` is written when the line is *finalized*, because the
line's ascent is a maximum over members and is not final until the last of them has arrived. The
correction is therefore a single walk of the member chain at the moment the line's geometry is written
anyway — the same walk that fills each back-link, because "this member's baseline is the line's" and
"this member is on this line" are one event.

**And the union had to move with it.** A transparent inline box's rectangle bounds its contents'
positions, so it can no longer be computed when the box closes: it is a post-order walk over the
finished context, run *before* the rebase that subtracts the origin it just wrote. The rule is
unchanged; only *when* it runs. `Baselines.md` §1 and §7 argue both, and
`layout.baseline.accounting` asserts the resulting invariant over every line of a wrapped document.

### What 10.7 found

Three findings, and the first is a **defect in the implementation that the new suite caught**.

1. **The half-leading rule clamped its terms in the wrong order.** `from_run` recovers a font's line
   gap as `height - ascent - descent`; computing the gap *before* clamping the terms widens it by
   exactly the amount about to be discarded, so a zero-height record with `ascent = descent = -4`
   reported an extent of 8 — a floor spanning something that spans nothing. Clamping first makes it
   fall back to the no-gap reading, which is what "the extent is the larger of the height and
   `asc + desc`" asks for. `layout.baseline.metrics_from_run` is the row that holds the order down.
2. **A rule array grew an entry while its call site kept the old count literal**, so the last rules
   were silently dropped — and the symptom read as a *layout* bug (a box with the initial display, an
   atomic with no declared size). The count is now derived from the array
   (`LAYOUT_BASELINE_RULE_COUNT`), and the fixture reads each rule's display back out of the style it
   just built, so a refused rule fails at the rule.
3. **Two fixtures encoded assumptions that were true enough to pass and wrong enough to mislead** — a
   nested member's stored `y` is relative to the box that contains it, not to the line, and an invented
   tag name is a claim about the parser rather than about layout.

Plus two 10.4 cases whose *numbers* the phase changed on purpose (the shorter of two atomics no longer
starts at the line's top), with every width, count and membership left exactly as 10.4 wrote them.

## Goal 10.8 — Width and height

**Status: complete.** The `width` RoboCSS has parsed since 9.11 finally *means* something: a box's
dimensions are resolved by a layer of their own, in three kinds (`auto`, a definite `px` length, and a
declaration this build cannot resolve), and the width a box resolves is the width its children are laid
out **into**.

### Delivered

| Piece | Where |
|---|---|
| The dimension vocabulary (three kinds), the sizing record, its doors, the two resolvers and the pair, and the one seam a computed style becomes a size through | `Include/RoboLayout/RoboLayoutSizing.h`, `Source/RoboLayout/RoboLayoutSizing.c` |
| The block pass routed through the layer: width first, children laid out at it, height after | `Source/RoboLayout/RoboLayoutBlock.c` |
| The atomic inline's three-step order — width, contents, height — and the moved `px` reader | `Source/RoboLayout/RoboLayoutInline.c`, `RoboLayoutInternal.h` |
| The two counters that split one declared width from the filled ones | `RoboLayoutBlock.h` |
| Version `0.8.0`, `RoboLayoutSizingImplemented`, the umbrella | `RoboLayoutVersion.h`, `RoboLayout.h` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A dedicated sizing abstraction exists | `layout.sizing.dimension`, `layout.sizing.vocabulary` |
| Computed `width`/`height` are consumed, never parsed here | `layout.sizing.seam` |
| Availability is per direction and explicit | `layout.sizing.record` |
| `auto` has defined behaviour, and the two axes differ | `layout.sizing.width`, `layout.sizing.height` |
| A declared width reaches the box **and its children** | `layout.sizing.block_declared_width` |
| A declaration this build cannot resolve is filled and reported | `layout.sizing.block_unresolved_width` |
| A keyword and a relative unit take the same branches as an omission | `layout.sizing.block_keyword_and_unit` |
| Clamping works where both dimensions name a length | `layout.sizing.block_bounds`, `layout.sizing.block_half_bound` |
| An atomic inline is sized before and after its contents | `layout.sizing.atomic_inline` |
| Line boxes were not turned into CSS boxes | `layout.sizing.line_regression` |
| Re-layout responds to a changed input, and the same inputs give one answer | `layout.sizing.reresolve`, `layout.sizing.block_auto_width` |
| The phase added no arena | `layout.sizing.allocator_balance` |

### The one distinction the phase turns on

**One resolved width, two uses.** 10.3 resolved a single width from the containing block and spent it
twice — the box's own size, and the width its children were laid out into — so a `width: 300px` box in
an 800px parent came out 800 wide *and* gave its children 800. The layer settles the width first and
recursion uses that number. For an `auto` width the two uses coincide, which is why real sizing arrived
without a single existing geometry assertion needing to be rewritten: the two answers agree exactly
where the old build knew only one of them.

The ordering is what the two resolvers exist for. `LayoutSizingResolve` is the pair, and it is
the shape a block box uses. An **atomic inline** cannot use it: its `auto` height is the block extent of
its own contents, and those contents can only be laid out *at* a width, so the width has to be settled,
the contents walked, and the height asked for afterwards. A single door would have forced that caller to
either lie about contents it does not have yet, or resolve twice and throw one answer away.

And a dimension this build cannot use is never a zero. A `%`, an `em`, `min-content` — each is read,
recorded as `UNRESOLVED`, and *filled* from the available space, with a **source** on the resolved size
saying so. The three kinds are a vocabulary rather than a number with a flag, which is what lets a later
goal make one of them resolvable without touching the record.

### What 10.8 found

One finding, and it is the first of its kind in this module: **the phase's own suite caught a wrong
*convention*, not a wrong number — a fixture indexing the arena.** Five cases addressed boxes as
`layout_sizing_box_at(&fixture, 2u)` — "the second box is the `<div>`" — which is a claim about the
order the builder produced rather than about the element the case named. Resolving elements **by tag**
(`layout_sizing_box_for`) is the fix, and the reason it matters is that a fixture's *indices* are the
one thing a later phase is free to change.

The rest is a lesson already paid for in 10.7 and paid again here: **a rule array and its count are one
fact.** Where the count comes from a literal it can be left behind by an edit, and a dropped rule shows
up three assertions later as a layout bug.

Nothing existing was weakened. Two suites had written an assertion *designed to fail the day a declared
width was resolved* — `layout.block.pipeline` and `layout.conformance.pipeline` — and both said so in
their own comments ("the assertion is written as the viewport's width rather than as *not 200* so that a
build that started honouring a declared width would fail here and be **told** to update the phase's
promise"). Each moved only its numbers, and the conformance card gained the assertion that makes the
phase's real claim: its `<p>` child resolves against the card's 200 rather than the viewport's 1280.
Everything else that changed was a version or capability pin.

The count went from **2193 pass to 2210**: 17 new cases, no case removed, and the module's 194 cases
in fourteen suites are green in Debug, in Release, and under ASan and UBSan with no diagnostic.

## Goal 10.9 — Margins

**Status: complete.** The first number in this module that is *not* a size. A box's position becomes
its **margin origin**, its `size` is untouched by any of the four declarations, and what a parent stacks
its next child below becomes the **margin box**. The four margins resolve *before* the width, because
an `auto` width fills whatever they left.

### Delivered

| Piece | Where |
|---|---|
| The margin vocabulary (four kinds, `absent` among them), the four-declaration record, the four **used values**, the two policies, and the one seam a computed style becomes a margin through | `Include/RoboLayout/RoboLayoutMargin.h`, `Source/RoboLayout/RoboLayoutMargin.c` |
| The shared classification opened to a **signed** length, so the sizing seam keeps refusing a negative `width` while the margin seam accepts a negative `margin` | `RoboLayoutInternal.h`, `RoboLayoutSizing.c` |
| The used margins kept on the object, with their read door and their clearing | `RoboLayoutObject.h` / `.c`, `RoboLayoutTree.c` |
| The block pass: margins before the width, the box placed at its margin origin, the margin box reported upward | `Source/RoboLayout/RoboLayoutBlock.c`, `RoboLayoutBlock.h` |
| The inline pass: an atomic inline's advance includes its horizontal margins, and its baseline is its bottom margin edge | `Source/RoboLayout/RoboLayoutInline.c`, `RoboLayoutInline.h` |
| Version `0.9.0`, `RoboLayoutMarginsImplemented`, the `margins` diagnostic domain, the umbrella, the build and the suite registration | `RoboLayoutVersion.h`, `RoboLayout.h`, `RoboLayoutDiagnostics.h`, `Tools/Build.ps1`, `Tests/Main.c` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A margin representation exists in RoboLayout | `layout.margins.kind_vocabulary`, `layout.margins.one_side`, `layout.margins.sides` |
| Computed CSS margins are consumed, never parsed here | `layout.margins.seam` |
| Block layout accounts for margins | `layout.margins.block_basic`, `layout.margins.block_sides` |
| Horizontal geometry integrates with the sizing layer | `layout.margins.block_auto_center`, `layout.margins.block_auto_odd`, `layout.margins.block_auto_under_auto_width`, `layout.margins.block_overconstrained` |
| Vertical placement accounts for margins | `layout.margins.block_geometry`, `layout.margins.margin_box_flows_down`, `layout.margins.block_stacking` |
| A margin moves the box and not its size | `layout.margins.block_geometry`, `layout.conformance.pipeline` |
| Negative margins are supported safely | `layout.margins.block_negative` |
| Existing inline layout remains functional | `layout.margins.inline_advance`, `layout.margins.inline_baseline`, `layout.margins.inline_policy` |
| Re-layout responds to a changed margin | `layout.margins.relayout` |
| Geometry stays finite and deterministic | every case, plus `layout.margins.allocator_balance` |
| Collapsing is explicitly **not** implemented, and says so | `layout.margins.block_no_collapse` |

### The one distinction the phase turns on

**A margin is outside the box.** Three numbers move and one does not:

```text
containing block's content origin
        │
        ├── left / top ────────────────▶ where the box is PLACED
        ├── left + right ──────────────▶ what is LEFT for the contents (and for an `auto` width)
        └── top + height + bottom ─────▶ what the PARENT STACKS the next box below

the box's own size             unchanged — the gap beside it is not part of it
```

That asymmetry is why the result's position and its size are no longer derived from one another, and
why the four used margins live **on the object** rather than in the result: adding them back is exactly
the arithmetic this phase exists to stop a caller re-deriving. Every case that reads a coordinate
asserts the pair — a moved origin and an unmoved size — so a build that added the margins to the `size`
fails whichever half it got wrong.

The order inside the pass is forced rather than tidy. The margins resolve **before** the width, because
an `auto` width fills whatever they left, and what decides whether there is anything to distribute is
the *declared* dimension rather than the resolution — so the block policy takes the dimension and not a
number. Two doors rather than one with a flag, for the reason 10.8 kept two dimension resolvers: a block
box distributes a horizontal `auto` beside a declared definite width (an odd pixel going to `right`) and
treats a vertical `auto` as zero, while an atomic inline turns every `auto` into zero — and the
difference *is* the two CSS rules, so a flag would let a caller pass the wrong one and get a plausible
answer.

### What 10.9 found

One finding, and it is a **design** one rather than an implementation one: **the vocabulary was one kind
short, and only the accounting could see it.** The first draft had three kinds — a resolved length
(including a declared zero), `auto`, and `unresolved` — with a cleared side reading as a resolved zero.
That is CSS's initial value and it produces correct geometry, which is exactly why it survives every
case that only reads coordinates. It is the **counters** that cannot be built on it: "how many of this
document's margins did an author actually write?" has no answer when a cleared record and `margin: 0`
are one value. `ABSENT` is the fix — the same split 10.8 reached from the other direction, where a
box's `auto` width and its undeclared width are likewise one geometry and two facts — and it is
deliberately **0**, for the reason RoboCSS gives for its own first-slot defaults.

The suite's own lesson is 10.8's, paid a third time: **a case whose fixture never reaches its subject
reports the absence as a pass.** Three cases were fixed for it — two that drove a stylesheet with no
`display: block` chain and no viewport, so no pass ran at all, and one whose "determinism" claim
compared a value with a second read of itself. Every margin case that reads a coordinate now resolves
the element **by tag** and asserts that a pass actually placed something before it reads the number.

Three earlier assertions changed, and each named the phase that would change it: `layout.block.pipeline`
(the card's origin 0/0 → **20/10** for its `margin: 10px 20px`) and `layout.conformance.pipeline` (the
same card's origin, and `<body>`'s and `<html>`'s extent 100 → **120**, the margin box the cursor
reaches). Neither card's own size moved — 200×100 in both — which is what makes the pair the phase's
claim rather than two numbers. Five pins moved for the version, the capability, the diagnostic domain
and the statement, the same set every phase moves.

The count went from **2210 pass to 2235**: 25 new cases, no case removed, and the module's 219 cases
in fifteen suites are green in Debug, in Release, and under ASan and UBSan with no diagnostic.

## Goal 10.10 — Padding

**Status: complete.** The mirror image of 10.9, and the phase that retires the identity a box's content
box used to have with its border box. A box's `size` becomes its **border** box — the content box the
sizing layer resolved plus the padding on each axis — its contents begin `padding-left`/`padding-top`
inside it, and its `position` is untouched. An `auto` width fills what the margins **and** the padding
left, because CSS's constraint equation has a term for each.

### Delivered

| Piece | Where |
|---|---|
| The padding vocabulary (three kinds **including** `absent` and **excluding** `auto`), the four-declaration record, the four **used values**, the one policy, and the one seam a computed style becomes a padding through | `Include/RoboLayout/RoboLayoutPadding.h`, `Source/RoboLayout/RoboLayoutPadding.c` |
| The two derived numbers: the width left for a box's contents (**floored** at zero) and the border box a content box asks for (**64-bit**, saturated) | `Source/RoboLayout/RoboLayoutPadding.c` |
| The shared classification asked with `allow_negative = 0`, so the padding seam refuses a negative `padding` where the margin seam accepts a negative `margin` | `RoboLayoutInternal.h`, `RoboLayoutSizing.c`, `RoboLayoutMargin.c` |
| The used padding kept on the object, with its read door and its clearing | `RoboLayoutObject.h` / `.c`, `RoboLayoutTree.c` |
| The block pass: the padding read before the width, the width resolved against what it left, the result sized as the border box, the padding box reported upward | `Source/RoboLayout/RoboLayoutBlock.c`, `RoboLayoutBlock.h` |
| The inline pass: an atomic inline's width, height and advance carry its padding | `Source/RoboLayout/RoboLayoutInline.c`, `RoboLayoutInline.h` |
| Version `0.10.0`, `RoboLayoutPaddingImplemented`, the `padding` diagnostic domain, the umbrella, the build and the suite registration | `RoboLayoutVersion.h`, `RoboLayout.h`, `RoboLayoutDiagnostics.h`, `Tools/Build.ps1`, `Tests/Main.c` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A padding representation exists in RoboLayout | `layout.paddings.kind_vocabulary`, `layout.paddings.one_side`, `layout.paddings.sides` |
| Computed CSS padding is consumed, never parsed here | `layout.paddings.seam` |
| Block layout accounts for padding | `layout.paddings.block_declared_width`, `layout.paddings.block_sides` |
| Content origin and content width reflect padding | `layout.paddings.children_get_content_width`, `layout.paddings.block_auto_width` |
| Content geometry accounts for padding under the current model | `layout.paddings.derived_boxes`, `layout.paddings.block_declared_width` |
| Descendants are laid out inside the padded content area | `layout.paddings.children_get_content_width` |
| Margin and padding stay distinct | `layout.paddings.margin_interaction` |
| Existing inline/text layout remains functional | `layout.paddings.inline_atomic`, `layout.paddings.inline_transparent_not_applied` |
| Re-layout responds to a changed padding | `layout.paddings.relayout` |
| Invalid geometry cannot escape the layout system | `layout.paddings.derived_boxes`, `layout.paddings.block_negative` |
| Geometry stays finite and deterministic | every case, plus `layout.paddings.allocator_balance` |

### The one distinction the phase turns on

**A padding is inside the box.** Two numbers move and one does not:

```text
the box's border box            content box + padding on each axis  (what `size` carries)
the box's content origin        the border-box origin + left / top
the width left for contents     containing width - margins - padding  (an `auto` width fills this)

the box's position              unchanged — an inset inside it does not move it
```

That is the exact mirror of 10.9's claim, and the pair is why the module keeps two records on the object
rather than one: a margin is kept there so a caller does not re-derive the space beside the box, and a
padding so a caller does not re-derive the space inside it. A result can carry neither, for the same kind
of reason — `size` is the border box by definition and `position` is where the box begins.

The order inside the pass is forced in one direction and chosen in the other: the padding resolves
**after** the margins, because what is left beside the box is what the numbers inside it are measured
from, and **before** the width, because a box with `padding: 10px` and no declared width is as wide as
its containing block rather than 20px wider. Three kinds rather than the margin vocabulary's four,
because the CSS grammar has no `padding: auto` (the shared classification *can* answer `AUTO`, and this
reader folds it onto `UNRESOLVED` rather than giving a grammar violation a vocabulary of its own); and
**one** resolution door rather than two, because with no `auto` there is nothing for a block box and an
atomic inline to disagree about. The two readers share one classification and differ in one parameter.
The model is `box-sizing: content-box` — CSS's initial value — so a declared `width: 200px` with
`padding: 5px` is 200 wide **inside** and 210 outside, and the box's children still get 200.

### What 10.10 found

Three findings. The first is an **arithmetic** one, and it is the first in this module about a magnitude
rather than a rule: **two bounded numbers can sum out of range, and the sum is the size.** A padding is
added to a content box and each is bounded by the seam's maximum, but their sum is not — so a
`width: 2147483647px` box with `padding: 10px` is a document an author can write and a pixel type
cannot hold. The first shape of the door added four pixel values, which wraps into a **negative**
rectangle. The fix is two lines — sum in `int64_t`, clamp at the maximum — and the decision to **clamp
rather than refuse**, because there is no document some number cannot describe and a size that saturates
is still a drawable rectangle. That the two derived numbers have *opposite* edge policies, one floor and
one ceiling, is the finding: "clamp it" and "refuse it" are each wrong for one of them.

The second is a **test-design** one: a size assertion alone passes for a build that grows a box the
wrong way. The phase's claim is a pair — the box's `size` grows **and** its `position` does not move —
and half of it is invisible in the number a suite reaches for first, so a build that applied the padding
outward from the content origin would report exactly the same size and put the box in the wrong place.
`layout.paddings.relayout` re-runs the pass and asserts the pair; `layout.paddings.block_auto_width` is
the same class of error one level out, where a build that applied the padding *after* the width would
report the containing block plus 20.

The third is 10.8's and 10.9's lesson paid another way: **the assertion that deliberately did not move is
the one paying for the model.** Four earlier assertions changed (`layout.block.pipeline` 200×100 →
**210×110**; `layout.conformance.pipeline` the same card and `<body>`'s and `<html>`'s extent 120 →
**130**, plus its two padding counters, absent → **4 and 0**) and one did not: the paragraph inside the
card is still **200** wide. The card's border box is 210 but its content box is the 200 its author
declared, so a build that read `width: 200px` as a border box would fail there and nowhere else. Six pins
moved for the version, the capabilities and the diagnostic domains — one more than 10.9's five, because
`layout.module.diagnostic_domains` now asserts three positions rather than two.

The count went from **2235 pass to 2255**: 20 new cases, no case removed, and the module's 239 cases
in sixteen suites are green in Debug, in Release, and under ASan and UBSan with no diagnostic.

## Goal 10.11 — Borders

**Status: complete.** The phase that completes the box model, and the one whose work was mostly a
**move**: a border is an *in-box* layer that sits between the padding and the margin, so it grows the
**border box** and moves the **content origin** and never moves the box — the exact mirror of 10.9's
margin, which moves the box and never the size. The padding layer's `border_box` door is renamed
`padding_box` (its real meaning now that a border exists) and the pass grows a fourth term, so the
box model reads `margin → border → padding → content` and is now, as a model, done.

### Delivered

| Piece | Where |
|---|---|
| The border vocabulary (three width kinds **including** the keywords `thin`/`medium`/`thick`, the ten-row style vocabulary and its `generates_border` fact, the colour reused from 9.9), the side record, the four-sides record, the used **edges** record, the **one** policy, and the **three** seams a computed style becomes borders through | `Include/RoboLayout/RoboLayoutBorder.h`, `Source/RoboLayout/RoboLayoutBorder.c` |
| The two derived numbers: the border box a padding box asks for (**64-bit**, saturated) and the width left for a box's contents after borders **and** padding (**floored** at zero) — with the two edge policies still opposite | `Source/RoboLayout/RoboLayoutBorder.c` |
| The padding layer's `border_box` door renamed `padding_box`, threaded through the layer, the block pass, the inline pass and the padding suite | `RoboLayoutPadding.h` / `.c`, `RoboLayoutBlock.c`, `RoboLayoutInline.c` |
| The three seams: the width reader on the computed style's `px`-or-keyword door (no other property's keyword names a length, so the shared classification folds them onto `UNUSABLE`), the style reader's closed vocabulary, and the colour reader | `RoboLayoutBorder.c`, `RoboLayoutInternal.h` |
| The used borders kept on the object, with their read door and their clearing | `RoboLayoutObject.h` / `.c`, `RoboLayoutTree.c` |
| The block pass: the borders read before the padding, the availability chain `margins → borders → padding → width`, the border box on the result, the three counters | `Source/RoboLayout/RoboLayoutBlock.c`, `RoboLayoutBlock.h` |
| The inline pass: an atomic inline's advance, size and baseline carry its borders; a transparent inline box declares one and it does not apply | `Source/RoboLayout/RoboLayoutInline.c`, `RoboLayoutInline.h` |
| The three counters `widths_resolved` / `widths_unresolved` / `borders_drawn` in both records and both `equal()` comparisons | `RoboLayoutBlock.h`, `RoboLayoutInline.h`, `RoboLayoutBlock.c`, `RoboLayoutInline.c` |
| Version `0.11.0`, `RoboLayoutBorderImplemented`, the `border` diagnostic domain, the umbrella, the build and the suite registration | `RoboLayoutVersion.h`, `RoboLayout.h`, `RoboLayoutDiagnostics.h`, `Tools/Build.ps1`, `Tests/Main.c` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A border representation exists in RoboLayout | `layout.borders.kind_vocabulary`, `layout.borders.width_record`, `layout.borders.side_record` |
| Four sides are independently represented | `layout.borders.seam`, `layout.borders.block_sides` |
| Computed CSS width/style/colour are consumed, never parsed here | `layout.borders.seam` |
| Border geometry integrates with padding and content geometry | `layout.borders.padding_interaction`, `layout.borders.derived_boxes` |
| Block layout accounts for borders | `layout.borders.block_declared_width`, `layout.borders.block_sides` |
| Margins stay external to the border box | `layout.borders.margin_interaction` |
| Padding stays inside the border | `layout.borders.padding_interaction` |
| Content origin accounts for border + padding | `layout.borders.padding_interaction`, `layout.borders.block_vertical` |
| Width/height stay compatible with 10.8 | `layout.borders.block_auto_width`, `layout.borders.block_declared_width` |
| Existing inline/text layout remains functional | `layout.borders.inline_atomic`, `layout.borders.inline_transparent_not_applied` |
| Border information is exposed for a future RoboPaint | `layout.borders.object_door`, `layout.borders.edges_record` |
| Re-layout responds to a changed border width | `layout.borders.relayout` |
| Geometry stays finite and deterministic | `layout.borders.derived_boxes`, every case, plus `layout.borders.allocator_balance` |

### The one distinction the phase turns on

**A border is inside the box.** It is the third inset, ordered against the other two and unlike
both in what it moves:

```text
the box's position        unchanged — 10.9's job
the box's border box      padding box + border on each axis   (what `size` carries)
the box's content origin  border-box origin + border-left/top + padding-left/top
the width left for it     containing width - margins - borders - padding  (an `auto` width fills this)
```

An `auto` width fills what the margins, the borders **and** the padding left, because CSS's
constraint equation has a term for each; a `border: 0` and a `border-style: none` both mean "no
visible border" and both leave the geometry alone, because a border that draws nothing must not
reserve space. The pass orders borders **before** the padding, the same reason 10.10 ordered the
padding before the width: the border box is what the padding is an inset *within*, so it must be
known first. Three seams rather than one, because the three properties compute three different
ways — a width's keywords are a grammar no other property shares, a style's rows carry the one fact
a paint phase must ask, and a colour is 9.9's. The model is still `box-sizing: content-box`, so a
declared `width: 200px` with `padding: 5px` and `border: 5px` is 200 inside and **220** outside.

### What 10.11 found

Four findings. The first is a **vocabulary** one, the mirror of 10.10's arithmetic finding, and the
kind only the accounting can see.

**A record was one kind short of its own subject.** A padding's used value *is* its declared value,
so "this side drew a border" and "this side was declared at five" were the same question — and a
border makes them two. `layout.borders.none_style` is what exposed it: twelve declarations arrived
and **none of them took room**, because `border-style: none` makes the used width zero whatever
`border-width` says. One counter cannot report a pair, so the record carries three —
`widths_resolved`, `widths_unresolved` and `borders_drawn` — and the third is the finding: it is the
only number in this module that answers a question about *geometry* rather than about a
*declaration*.

**A door's name outlived its meaning, and only a second layer could see it.**
`robo_layout_padding_edges_border_box` returned `content + padding` — the **padding** box — and was
named for the box the module *hoped* to complete rather than the box it computed. That was
unobservable while the padding was the only inset (the two boxes were one number) and became
observable the moment a border existed: paint draws the border box's edge and would have drawn it
`border-width` too far in. The fix is a rename plus a border-layer door composed above it, and **no
arithmetic changed** — every 10.10 case about those numbers passes with the new spelling. 10.10's
prediction ("the doors below are named for what they add so that 10.11 can extend them rather than
rename them") was half right, and this is the half that was wrong.

**A negative side is a caller bug, and the moment to see it is before a size is derived from it.**
10.10 floored a negative *available width* at zero, because a containing block narrower than a box's
padding is a **document**. A negative *border record* is not — this module's own resolver cannot
produce one — so the two derived doors **refuse** with a diagnostic rather than flooring, through
one shared `layout_border_edges_require`. It is the first place in the module where "this record is
impossible" and "this document is extreme" needed different answers.

**The same two numbers moved for the fourth time.** `layout.block.pipeline` (card 210 × 110 →
**218 × 118**) and `layout.conformance.pipeline` (212 × 112 and heights **132**), each assertion's
comment now naming which phase moved it. The assertion that deliberately **did not** move is still
the paragraph inside the card: **200**, not 212 and not 188 — the card's border box grew and its
content box did not, so a build that read `width: 200px` as a border box would fail there and
nowhere else. Six pins moved for the version, the capabilities and the diagnostic domains.

The count went from **2255 pass to 2277**: 22 new cases, no case removed, and the module's 261
cases in seventeen suites are green in Debug, in Release, and under ASan and UBSan with no
diagnostic — `layout.borders.*` 22/22 under them.

## Goal 10.12 — Basic positioning

**Status: complete.** The phase that gives a box a second coordinate, and the first that is about
**where** rather than about **how big**: a `position` is a *displacement* — one declaration that
changes where a box is drawn without changing anything about the box. Every box is placed at its
**flow** position (the number siblings were computed from) and a relatively positioned box's result
carries the **final** one, flow displaced by the resolved offset, with the used offset kept on the
object so `final − offset` is one subtraction and neither space is derived from the other twice.

### Delivered

| Piece | Where |
|---|---|
| The inset vocabulary — four kinds, `ABSENT` the zero of the record, **signed** `RESOLVED` (the one inset reader whose sign survives, because the grammar's negative half is the grammar's), `AUTO` and `UNRESOLVED` — the four-sides record, the **position** record (mode plus the four) and its predicates | `Include/RoboLayout/RoboLayoutPosition.h`, `Source/RoboLayout/RoboLayoutPosition.c` |
| The seam `LayoutPositionOf`: the shared classification with one extra parameter (an inset is signed, where a padding and a border are not), the mode read through a kind check so "not positioned" and "not a position property" stay two answers | `RoboLayoutPosition.c`, `RoboLayoutInternal.h` |
| The policy `LayoutPositionResolve`: the one precedence rule (`left` over `right`, `top` over `bottom`, an `auto` saying nothing) and the four counters that account for every declaration — including `offsets_declined`, which tells a `static` box with four insets from a `relative` box with four autos | `RoboLayoutPosition.c` |
| The apply `LayoutPositionApply`: flow plus displacement in **64 bits, saturated** — a position and a displacement are each bounded by the seam's maximum and their sum is not — refusing a record no resolve produced *before* it touches the out | `RoboLayoutPosition.c` |
| The used offset kept on the object, its read door `LayoutObjectPosition` (zero returned rather than refused; `RoboNotFound` for an unplaced object, a text run and a transparent inline box), and its clearing | `RoboLayoutObject.h` / `.c`, `RoboLayoutTree.c` |
| The block pass: the two-step placement — `margin_origin` (flow) then `final_origin` — the result carrying final, the stacking continuing from flow, the counters accumulated **after** the resolve | `Source/RoboLayout/RoboLayoutBlock.c`, `RoboLayoutBlock.h` |
| The inline pass: an atomic inline's displacement in two halves — `dx` to its `position.x` and never to the advance, `dy` at finalization after the baseline correction; transparent boxes and text runs decline | `Source/RoboLayout/RoboLayoutInline.c`, `RoboLayoutInline.h` |
| The four counters in both records and both `equal()` comparisons, `boxes_shifted` the first counter about the box rather than a declaration | `RoboLayoutBlock.h`, `RoboLayoutInline.h`, and their `.c` |
| Version `0.12.0`, `RoboLayoutPositioningImplemented`, the `position` diagnostic domain (the twentieth), the umbrella, the build and the suite registration | `RoboLayoutVersion.h`, `RoboLayout.h`, `RoboLayoutDiagnostics.h`, `Tools/Build.ps1`, `Tests/Main.c` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A dedicated positioning representation exists | `layout.positioning.kind_vocabulary`, `layout.positioning.position_record` |
| `static` remains fully compatible with normal flow | `layout.positioning.policy_static_declines`, `layout.positioning.block_static_declines` |
| `relative` works with basic offsets | `layout.positioning.policy_precedence`, `layout.positioning.apply` |
| Relative elements retain their normal-flow space | `layout.positioning.flow_preserved`, `layout.positioning.block_relative` |
| Final positioned geometry is distinct from flow geometry | `layout.positioning.block_relative`, `layout.positioning.object_door` |
| Margins, borders, padding, width and height keep working | `layout.positioning.box_model_interaction`, `layout.positioning.descendants` |
| Positive and negative offsets are safe | `layout.positioning.block_negative`, `layout.positioning.apply` |
| Descendant geometry remains correct | `layout.positioning.descendants` |
| Offset changes invalidate correctly | `layout.positioning.relayout` |
| Final geometry available for paint and hit testing | `LayoutObjectPosition` (the door both read) |
| Focused positioning tests pass | `layout.positioning.*` 19/19 |
| All previous CSS/layout tests pass | 2295/2295, Debug and Release |
| No Chromium dependency | `Tools/Build.ps1 -Verify` boundary scan |

### What 10.12 found

Four findings, the first two **ordering** ones — the displacement arithmetic was correct the whole
time, and what it sat next to was wrong.

**A counter read a variable that was not written yet.** The block pass accumulated the four
position counters *before* the call that resolved the displacement, so on the first box of every
pass `offsets_applied` summed whatever the previous frame had left on the stack — visible as
pointer-sized garbage in a determinism case that compared two passes' records. Every earlier
counter was preceded by its own resolve, so this is the module's first accumulator that outlived
its producer; the fix is the ordering, and only a *comparison* could see it (both passes were wrong
the same way, so both records were self-consistent).

**A test freed a pointer the fixture had already freed.** The seam case held the style pointer the
first build handed back, replaced that style three times, and ended by destroying the stale first
pointer — a double free that segfaulted the suite mid-case, after every assertion in it had passed.
A handle a builder hands back is a view of one moment; a replace invalidates it.

**A negative inset is honoured here and nowhere else.** `layout.positioning.seam` reads
`right: -4px` back as **-4** — the one seam of the three inset readers where the sign survives —
and the fixture's first draft stored the magnitude and expected the layer to negate it. The run
said otherwise, and the helper now carries the sign it was handed.

**A displacement composes with the flow stack, not with the boxes after it.** The box-model case's
first draft expected a shifted sibling's `y` to restart from the displaced box's final position;
the engine put it where the *flow* position says, and the case now asserts the pair — the box
drawn at margin origin plus offset, the sibling at flow's number, the two differing by exactly the
offset. Two coordinate spaces, one box, no third number.

The count went from **2277 pass to 2295**: 19 new cases, and the module's 280 cases in eighteen
suites are green in Debug, in Release, and under ASan and UBSan with no diagnostic —
`layout.positioning.*` 19/19 under them. The host visual checkpoint is 23/23 probes and
`robo test-qemu-display` passes; `robo test-runtime` remains red only on the documented
`window.host.keyboard` host-focus flake, fourth consecutive phase, on a diff touching no `window`
file.

## Goal 14.0 — Out-of-flow placement (pulled forward from the CSS-completion goal)

**Status: complete.** The behaviour 10.12 named and deferred, and the reason it could not wait: Phase
14's desktop shell cannot be *composed* without it. A wallpaper that fills the viewport, a dock on the
bottom edge, a menu floating over a window and a window that is a rectangle at a place are four
out-of-flow boxes, and a build that stacks them in normal flow can express none of the four.

### Delivered

| Piece | Where |
|---|---|
| The vocabulary: two **axes**, four kinds of side over a near/far pair, the containing block as a *rectangle* (origin and size, plus whether it is the initial one), and the placement record a pass applies | `Include/RoboLayout/RoboLayoutAbsolute.h` |
| The seam `LayoutAbsoluteEdgesForAxis`, which goes through 10.12's own reader (`LayoutPositionOf`) rather than reading the four inset properties again, so the two phases cannot disagree about what a style declared | `Source/RoboLayout/RoboLayoutAbsolute.c` |
| The policy `LayoutAbsoluteResolveAxis`: the near side wins (`declaredOverConstrained` when a definite size sits beside it), the far side positions the axis where the near side says nothing, and two present insets size the axis between them when the declared dimension is not definite — clamped and floored at zero | `RoboLayoutAbsolute.c` |
| The two doors that need the box's **own** size: `LayoutAbsolutePlacementOrigin` (the far anchor is a *distance back* from the containing block's far edge, so it needs the margin box) and `LayoutAbsoluteContentHeight` (the two insets size the margin box; the content box is what is left after the margins, borders and paddings) | `RoboLayoutAbsolute.c` |
| The recursion: `LayoutBlockChildren` **skips** an out-of-flow child, a positioned box places the out-of-flow descendants whose containing block it is *after* its own size is final, the walk descends through static wrappers carrying the same rectangle and never into a positioned child, and `fixed` takes the initial containing block whichever ancestor found it | `RoboLayoutBlock.c` |
| The counters in their own record (`LayoutBlockStats.absolute`), `offsetsDeclined` narrowed to `static`/`sticky`, `RoboLayoutDomainAbsolute` as the twenty-third domain, and `LayoutGetCapabilities().outOfFlow` with every absence named at the flag | `RoboLayoutBlock.h`, `RoboLayoutTypes.*`, `RoboLayout.h`, `RoboLayoutVersion.h` (`0.17.0`) |
| The build fix that made the phase's integration test runnable: the archiver's object list goes through a **response file**, because a part naming eighteen dependency prefixes produced a command line past what Windows hands a process | `Runtime/Tools/Build.ps1` |

### What 14.0 found

**The containing block had the padding on the wrong side of the origin.** The first implementation
placed an out-of-flow descendant at `finalOrigin + border + padding`, which is the *content* origin —
so a box inside a padded wrapper was placed one padding further in than CSS says, and the number
looked plausible in every document whose wrapper had no padding. The rule the pass actually writes in
is content-relative (a child's cursor starts at its parent's content origin), so the padding box
origin in that space is `(-padding-left, -padding-top)`: the padding box begins exactly where the
padding does, and a border appears in the number nowhere because the content origin is already inside
it. `layout.absolute.containing_block_padding_edge` is the case that pins it, and it was written
*after* the coordinate was fixed rather than before it.

**A rule was asserted with only half of itself.** The policy case expected `left`/`right` with
`width: auto` to decline the far side; CSS sizes the axis between the two insets, and that is what the
code does. The expectation was rewritten to assert both halves of one rule — over-constrained *with* a
definite size, stretched *without* one — because those are the two questions a build gets wrong
together and a case that asked one of them would have passed a build that answered the other wrong.

### Verification

`TestLayout.ps1 -Target Host` **381/381** (21 new cases), `TestPaint` 242/242, `TestGraphics` 179/179,
`TestFont` 189/189, `TestRasterizer` 31/31 and `TestDesktop` 56/56. `TestCss` and `TestHtml` remain
**pre-existing** red, for a reason that has nothing to do with this phase: their part dependency lists
omit `RoboHTML`/`RoboGraphics`/`RoboLayout`/`RoboPaint`/`RoboRender`, so
`Tests/Runtime/Suites/SuiteRoboCssDomSelect.c` — the one suite that must see RoboCSS and RoboHTML
together — links with unresolved externals. Recorded here rather than fixed in passing, because it is a
part-build dependency question rather than a layout one.

## Goal 10.13 — Overflow

**Status: complete.** The phase that admits a box's contents may not **fit** — and whose whole
shape follows from refusing to call that a size. Every phase before it assumed the contents stayed
inside; the tempting model is to grow the box, and it is wrong in a way no single-box test can see,
because a *size* cannot express an overflow: a box that spills 40px left and a box that spills 40px
right have the same width and two different records. So the box keeps the size its declarations and
its contents asked for, and the overflow is four **extents** measured from its **padding box** —
CSS's clipping edge, and the one rectangle a box's own declarations cannot move — recorded on the
object beside a per-axis mode.

### Delivered

| Piece | Where |
|---|---|
| The RoboCSS overflow vocabulary: one *value* (`visible`, `hidden`) with the **axis belonging to the property**, the closed set with `scroll`/`auto`/`clip` named as rows this build has not added, and the **shorthand registered on the vocabulary** — read for whichever axis a longhand said nothing about, rather than kept `PRESERVED` while nothing reads it | `Include/RoboCSS/RoboCSSOverflow.h`, `Source/RoboCSS/RoboCSSOverflow.c` |
| The computed-value kind, the three registry rows (`overflow`, `overflow-x`, `overflow-y`, all non-inherited, all `visible`), and the CSS module surface: version `0.17.0`, `capabilities.overflow`, 66 rows in the table (50 non-inherited) | `RoboCSSComputedValue.h` / `.c`, `RoboCSSPropertyRegistry.h` / `.c`, `RoboCSS.h`, `RoboCSSVersion.h` |
| The layout vocabulary — three kinds (`ABSENT`, `RESOLVED`, `UNRESOLVED`), two **named** axes (direction, not a letter), the declared record, and the used **edges** record: two modes, four non-negative extents and three counters | `Include/RoboLayout/RoboLayoutOverflow.h`, `Source/RoboLayout/RoboLayoutOverflow.c` |
| The one policy `LayoutOverflowResolve`: longhands first, then the shorthand for either axis, then `visible`; an unusable declaration used as `visible` and **counted**, so a `var()`-driven boundary is visible rather than silent | `RoboLayoutOverflow.c` |
| `LayoutOverflowEdgesInclude`: the per-side **maximum** of how far a region reaches past a padding box, computed in 64 bits and **saturated**, with no extent ever negative | `RoboLayoutOverflow.c` |
| The two derived rectangles: the **region** (the padding box grown by the extents, clipped back on an axis whose mode clips) and the **clip rectangle** (the padding box, refused with `RoboNotFound` when neither axis clips) | `RoboLayoutOverflow.c` |
| `LayoutOverflowMeasure` and `LayoutOverflowObjectRegion`: a box's padding box in its own space, one loop over every placed child folding in the child's **region**, and the same door the block pass calls | `RoboLayoutOverflow.c`, `RoboLayoutInternal.h` |
| The used overflow kept on the object, the read door `LayoutObjectOverflow` (declining an unplaced box, a text run and a transparent inline box), and its clearing with the geometry | `RoboLayoutObject.h` / `.c`, `RoboLayoutTree.c` |
| The block pass: the measurement at the point a box's padding box is known, the record written, the three counters, and the result's two flags folded from the record's per-axis answer | `Source/RoboLayout/RoboLayoutBlock.c`, `RoboLayoutBlock.h` |
| The inline pass: an **atomic participant** measured and its flags set **before** placement (the geometry door does not touch the flag word); text runs and transparent inline boxes declined and still contributing through `..._object_region` | `Source/RoboLayout/RoboLayoutInline.c`, `RoboLayoutInline.h` |
| The three counters `overflow_clipped` / `overflowing` / `overflow_unresolved` in both records and both `equal()` comparisons | `RoboLayoutBlock.h`, `RoboLayoutInline.h`, and their `.c` |
| Version `0.13.0`, `RoboLayoutOverflowImplemented`, the `overflow` diagnostic domain (the twenty-first), the umbrella, the build and the two suite registrations | `RoboLayoutVersion.h`, `RoboLayout.h`, `RoboLayoutDiagnostics.h`, `Tools/Build.ps1`, `Tests/Main.c` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A dedicated overflow representation exists | `layout.overflow.kind_vocabulary`, `layout.overflow.axis_vocabulary`, `layout.overflow.edges_record` |
| `visible` and `hidden` are supported | `css.overflow.vocabulary`, `layout.overflow.resolve_modes` |
| `overflow-x` and `overflow-y` are representable independently | `layout.overflow.axis_record`, `layout.overflow.pass_axis_split` |
| Overflow bounds are tracked separately from layout geometry | `layout.overflow.pass_size_unchanged`, `layout.overflow.pass_fitting` |
| Child overflow contributes correctly to the parent | `layout.overflow.pass_child_right`, `layout.overflow.pass_child_below`, `layout.overflow.pass_recursion_clipped` |
| Relative positioning interacts correctly with overflow | `layout.overflow.pass_relative_child` |
| Overflow does not silently change resolved width/height | `layout.overflow.pass_size_unchanged`, `layout.overflow.pass_extreme_width` |
| Box-model geometry remains correct | `layout.overflow.pass_box_model` |
| Overflow information is available for future clipping | `LayoutObjectOverflow`, `..._edges_region`, `..._edges_clip_rect` |
| Re-layout updates overflow information | `layout.overflow.relayout` |
| Geometry remains finite and deterministic | `layout.overflow.include_saturates`, `layout.overflow.clip_rect_refusals`, `layout.overflow.determinism` |
| Focused overflow tests pass | `layout.overflow.*` 26/26 and `css.overflow.*` 7/7 |
| All previous CSS/layout tests pass | 2330/2330, Debug and Release |
| No Chromium dependency | `Tools/Build.ps1 -Verify` boundary scan |

### What 10.13 found

Four findings, recorded in full in `layout/docs/Tests.md` §18. In one line each: the shared block
fixture's assertion of **silence** (`flags & ALL == 0`, right while nothing set a flag) had to become
an assertion of **agreement** with the overflow record, in sixteen cases at once; the `overflow`
shorthand could not ride the `PRESERVED` convention four other families use, because a declaration
nothing reads would make "clip this" lay out as though nobody had written anything; "the newest
domain is the last one" turned out to be an invariant pinned in **three** suites, so adding
domain 21 moved all three and each now pins `POSITION + 1 == OVERFLOW`; and the recursion — a parent
folds in a child's **region** rather than its contents — is the one thing only a case with a
*clipping* child can see.

The count went from **2295 pass to 2330**: 26 new layout cases and 7 new CSS cases, and the module's
307 cases in nineteen suites are green in Debug, in Release, and under ASan and UBSan with no
diagnostic — `layout.overflow.*` 26/26 and `css.overflow.*` 7/7 under them. The host visual
checkpoint is 11 probes with `RESULT: PASS` and `robo test-qemu-display` passes; `robo test-runtime`
remains red only on the documented `window.host.keyboard` host-focus flake, fifth consecutive phase,
on a diff touching no `window` file. The visual-validation criterion is met the way 10.11 and 10.12
met it: the demo rendering path does not run RoboLayout at all, so the geometry is what the phase
owes and the suite is where it is proved — the checkpoint was run and passes unchanged rather than
being extended into a paint test this phase is not allowed to write.

## Goal 10.14 — Layout Invalidation

**Status: complete.** The phase that makes a conclusion **expire** — the first whose subject is not a
box's geometry but its truth, and the last of the box-modelling phases for a reason worth naming: an
invalidation is a claim *about* a set of conclusions, so it can only be written once there are
conclusions to be about and once their shape has stopped moving. Every phase before it answered "what
is a box's geometry?"; this one answers the question that only exists because those answers persist:
**which of them is still true?**

The tempting model — record which declarations changed and re-resolve those — is wrong at the second
box, because geometry is a function of *ancestors'* conclusions as well as a box's own declarations: a
leaf whose author changed nothing moves when its parent's padding changes. So what is tracked is not
which inputs moved but which *conclusions cannot be trusted*, and that is a property of boxes.

### Delivered

| Piece | Where |
|---|---|
| The vocabulary: four **state bits** that compose (`STYLE`, `LAYOUT`, `SUBTREE`, `OVERFLOW`) with `CLEAN` as the 0 a cleared record already is, seven **reasons** answering "what happened?" where the bits answer "what is stale?", and the name doors that spell a combination as `layout+subtree` so a failing assertion prints something a reader decodes without a table | `Include/RoboLayout/RoboLayoutInvalidation.h`, `Source/RoboLayout/RoboLayoutInvalidation.c` |
| The three **masks** a caller selects with — `LAYOUT_BITS` for *must a pass run*, `OWN_BITS` (= `LAYOUT` alone) for *where would it start*, `ALL` for *is anything owed* — because the three questions have three different answers and one mask answers all three wrongly | `RoboLayoutInvalidation.h` |
| The style classifier `LayoutStyleEffectForProperty`: a property **name** in, one of *NONE* / *PAINT* / *GEOMETRY* out, written out by hand rather than derived from RoboCSS's registry because the question is what *layout* reads and only this module can answer it — with the two answers a naive table gets wrong being `border-style` (geometry: 10.11's used width is zero unless the style draws) and an unknown name (nothing, rather than everything) | `RoboLayoutInvalidation.c` |
| The record and its stats: the invalid record with an `equal`, and `LayoutInvalidationStats` — `invalidated`, `style_only`, `root`, `reason`, `recomputed` — the pair (`invalidated`, `style_only`) being the one that tells a pass that found nothing from a pass that began with a colour change on every box | `RoboLayoutInvalidation.h` / `.c` |
| The three **marking policies** and their three different shapes: a box-model change (`LayoutTreeMarkLayoutDirty`) marks the box, its ancestors' **measurement** and the *transitively* following flow; a displacement (`LayoutTreeMarkPositionDirty`) marks the box and its ancestors' measurement and **leaves the following flow clean**, which is 10.12's contract made testable; an overflow-mode change (`LayoutTreeMarkOverflowDirty`) marks measurements alone and no geometry at all — beside `mark_style`, `mark_style_property` (the classifier and the marking sharing one door so they cannot disagree), `mark_subtree_dirty`, `mark_clean`, `mark_all` and `mark_recomputed` | `RoboLayoutInvalidation.h` / `.c`, `RoboLayoutTree.h` |
| The **dirty root**, computed as the lowest **common ancestor** of the boxes whose own geometry is stale rather than the shallowest such box — the difference being that a following sibling can sit higher in the tree than the changed box, so "shallowest" names a subtree that does not contain the change at all | `RoboLayoutInvalidation.c` |
| The two doors a consumer must keep apart, and their two answers: `LayoutTreeIsClean` (subtree clean of everything, `STYLE` included) and `LayoutTreeNeedsLayout` (a layout bit anywhere) — held apart because *not clean* and *need layout* are not the same statement | `RoboLayoutInvalidation.h` |
| The state on the **object** — the only field that must outlive a pass — with its reader and its clearing folded into the geometry sweep, and every box created dirty (`REASON_CONTENT`) so a box the layer never ran over is not implicitly clean | `RoboLayoutObject.h` / `.c`, `RoboLayoutTree.c` |
| The one **decision** the model is put to: `LayoutContextRunIncremental`, which skips the walk and reports `RoboLayoutPassClean` exactly when the context has completed a pass, on *this* tree, with no input changed since, and no box carries a layout bit — the fourth pass outcome, because a caller that cannot tell "nothing was stale" from "everything was recomputed" cannot use the answer | `Include/RoboLayout/RoboLayoutContext.h`, `Source/RoboLayout/RoboLayoutContext.c` |
| The pass-half accounting: a pass reports how many boxes it began with dirty, so the model's claim and the walk's own numbers can be checked against each other, and the clearing sits **inside** the branch that laid geometry out rather than beside the pass counter, so a pass with no viewport — which classifies and computes nothing — clears nothing | `Source/RoboLayout/RoboLayoutContext.c`, `Source/RoboLayout/RoboLayoutBlock.c`, `Source/RoboLayout/RoboLayoutInline.c` |
| Version `0.14.0`, `RoboLayoutInvalidationImplemented`, the `invalidation` diagnostic domain (the twenty-second), the umbrella, the build and the two suite registrations | `RoboLayoutVersion.h`, `RoboLayout.h`, `RoboLayoutTypes.h` / `.c`, `Tools/Build.ps1`, `Tests/Main.c` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A dedicated layout-invalidation mechanism exists | `layout.invalidation.vocabulary`, `layout.invalidation.state_names` |
| Invalidation states are explicit and deterministic | `layout.invalidation.masks`, `layout.invalidation.state_known` |
| Style changes trigger appropriate invalidation | `layout.invalidation.classify_*`, `layout.invalidation.style_property` |
| Width/height changes propagate correctly | `layout.invalidation.layout_dirty_propagation`, `layout.invalidation.width_marks_descendants` |
| Margin/padding/border changes propagate correctly | `layout.invalidation.box_model_marks_following_flow`, `layout.invalidation.border_style_is_geometry` |
| Relative-position changes invalidate final geometry | `layout.invalidation.position_dirty_is_narrow`, `layout.invalidation.position_leaves_flow_clean` |
| Overflow data is invalidated and recomputed | `layout.invalidation.overflow_mode_marks_measurement`, `layout.invalidation.overflow_only_root` |
| Parent/ancestor propagation works | `layout.invalidation.layout_dirty_propagation`, `layout.invalidation.root_is_the_common_ancestor` |
| Affected following-flow siblings are recomputed | `layout.invalidation.transitive_following_flow`, `layout.invalidation.relayout_moves_the_following_flow` |
| Subtree invalidation works | `layout.invalidation.subtree_dirty`, `layout.invalidation.subtree_marks_descendants` |
| Successful layout clears required invalidation state | `layout.invalidation.pass_clears_layout_bits`, `layout.invalidation.pass_without_viewport_clears_nothing` |
| No stale geometry survives a completed pass | `layout.invalidation.relayout_moves_the_following_flow`, `layout.invalidation.repeated_layout_is_stable` |
| No-op changes avoid unnecessary invalidation | `layout.invalidation.incremental_skips_a_paint_only_change`, `layout.invalidation.style_bit_survives_the_pass` |
| Geometry remains finite and deterministic | `layout.invalidation.determinism` |
| Focused invalidation tests pass | `layout.invalidation.*` 25/25 |
| All previous CSS/layout tests pass | 2355/2355, Debug and Release |
| No Chromium dependency | `Tools/Build.ps1 -Verify` boundary scan |

### What 10.14 found

Five findings and one design point, recorded in full in `layout/docs/Tests.md` §19. In one line each:
"shallowest stale box" is **not** the same question as "where does relayout start", and the first
implementation was wrong — a real document failed it, because a following sibling sits higher in the
tree than the changed box, and the fix is not a tie-break but a different question (the **lowest
common ancestor**); `SUBTREE` on an ancestor is a relayout bit that must **not** count as one, which is
why the module has three masks rather than one; the propagation rule needed a *transitive*
following-flow walk, and only a three-level document can tell it from a one-deep rule; the incremental
door cannot be tested through a rebuilt tree — rebuilding invalidates everything by construction — so
the fixture had to rewrite a declaration on the pointer the built tree already holds, which is what
makes `relayout_moves_the_following_flow` the only case where the pass actually has to run; and a pass
that computed no coordinate must clear **nothing**, so the clearing is inside the branch that laid
geometry out rather than beside the pass counter. The design point the suite pins rather than finds is
that `STYLE` is **not** cleared by a recomputation, so a box can be *not clean* and *not need layout*
at the same time — asserted three ways, because a build that cleared it would look correct until the
repaint never happened.

The count went from **2330 to 2355**: 25 new layout cases, and the module's **332 cases in twenty
suites** are green in Debug, in Release, and under ASan and UBSan with no diagnostic —
`layout.invalidation.*` 25/25 under them. The host visual checkpoint passes unchanged (11 probes,
`RESULT: PASS`) and `robo test-qemu-display` passes; the visual-validation criterion is met the way
10.11–10.13 met it, since the demo rendering path does not run RoboLayout and this phase may not
write a paint test. `robo test-runtime` remains red only on the documented `window.host.keyboard`
host-focus flake — **sixth** consecutive phase, on a diff touching no `window` file, with the direct
Debug run 2355/2355, so the §14 proposal to fix the focus race or quarantine the case is now overdue
rather than optional.

## Goal 10.15 — Layout Tests

**Status: complete — Phase 10 is complete.** The goal that ends Phase 10, and it does it by asking a
question the twenty suites before it could not: whether the fourteen layers are right *about each
other*. Every earlier suite proves one layer against its own declarations — sizing against a `width`,
margin against a `margin`, border against a `border`, position against an `inset`, overflow against an
`overflow` — and a build that got each layer right and **composed them wrongly** passes every one of
them. What that looks like in practice is a content origin computed from the padding but not the
border, a final position that is the offset but not the flow, a parent that grows to hold an
overflowing child, or a region that folds in a clipped child's *contents*. Each is one line of wrong
arithmetic that no single-family assertion can see, and each is asked here on a real document through
the real chain: `RoboHTML → RoboCSS → the bridge → the tree → the pass`.

### Delivered

| Piece | Where |
|---|---|
| The **consolidation suite**: twelve cases in four groups — *composition* (a card inside a column inside a shell, carrying a declared width, a margin, a padding, a border, a relative offset and a clipping child in one pass, with the **derived** relationships asserted — content origin, content width, margin-box stacking, line-to-baseline — rather than a pile of magic numbers, because the composition is the claim); *determinism* (the same document **re-built into a second tree** with every result, inset, offset, overflow record and line compared field by field); *robustness* (a dimension near the geometry's maximum, a zero, a negative margin, an unusable declaration and a deep chain, all of which must produce finite geometry); and *regressions* (the documented findings of 10.11, 10.13 and 10.14, each pinned as the minimal reproduction that failed before its fix) | `layout/tests/SuiteRoboLayoutIntegration.c`, `layout.integration.*` |
| The **page-origin helper** the suite had to grow, because a result carries two coordinate spaces since 10.12 — `position` is where a box is *drawn* and lives in its **parent's** space, so composing the chain explicitly is the only way an integration assertion can be written without silently comparing the wrong pair | `SuiteRoboLayoutIntegration.c` |
| Three regression cases whose reproductions had existed only as **prose**: 10.11's border-style finding (a width without a style draws nothing, so a `border-style` change moves geometry and the pair `width` + `style` is what makes it nonzero), 10.13's clipped-child finding (a clipping child contributes its *region* rather than its contents), and 10.14's dirty-root finding (the lowest common **ancestor**, not the shallowest dirty box) — a finding recorded in a document is a finding nobody runs | `SuiteRoboLayoutIntegration.c`, beside `layout/docs/Borders.md` §8, `Overflow.md` §11 and `Invalidation.md` §11 |
| The one **production** change the phase made, and the reason a consolidation goal exists: the rule the padding, border, margin, position and overflow layers had each stated for their *own* sums — "add in 64 bits, saturate rather than refuse" — applied to the **pass's own accumulations**, which had never had it. Three sites (the block cursor, a margin origin, a margin box) summed bounded numbers into an `int32` and wrapped; a wrapped extent is *negative*, and `LayoutSizingSetContent` refuses a negative extent — correctly — so a merely tall document made the whole pass **fail** instead of saturating. One helper now stands where the rule does, so a layer and the pass cannot disagree about the bound | `Source/RoboLayout/RoboLayoutInternal.h` (`layout_extent_clamp`), `Source/RoboLayout/RoboLayoutBlock.c` |
| Version `0.15.0` — no door, no enumerator and no capability field, which is the phase's smallest claim rather than an omission: the first goal since 10.2 to add nothing to the module's surface, because a `layout_tests` flag would invite a caller to ask whether the *build* is tested and there is no such thing to ask | `RoboLayoutVersion.h` |
| The build and registry wiring, and the three version pins the bump forced a phase to update — the pins working exactly as intended | `Tools/Build.ps1`, `Tests/Main.c`, `SuiteRoboLayout.c`, `SuiteRoboLayoutConformance.c` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A dedicated RoboLayout test architecture exists | `layout/tests/`, twenty-one suites, wired explicitly in `$SuiteSrcs` and `Tests/Main.c` |
| Reusable test helpers exist | `RoboLayoutTestSupport.h` (the real-author-sheet fixture), plus the suite's scripted measurer and page-origin helper |
| Geometry assertions are reliable and informative | `RoboCheck*` with the derived relationships named per case |
| Block / inline layout have focused tests | `layout.block.*` 16, `layout.inline.*` 23 |
| Text, line boxes and baselines have focused tests | `layout.text.*` 26, `layout.line.*` 12, `layout.baseline.*` 16 |
| Width/height, margins, padding, borders have focused tests | `layout.sizing.*` 17, `layout.margins.*` 25, `layout.paddings.*` 20, `layout.borders.*` 22 |
| Positioning and overflow have focused tests | `layout.positioning.*` 20, `layout.overflow.*` 26 |
| Invalidation has focused tests | `layout.invalidation.*` 25 |
| Cross-feature integration tests exist | `layout.integration.box_model`, `.inline_text`, `.position_overflow`, `.nested_tree`, `.showcase` |
| Determinism is explicitly tested | `layout.integration.determinism` (two independently built trees) |
| Invalid/extreme input behaviour is tested | `layout.integration.extreme_input`, `.no_stale_geometry` |
| Lightweight performance sanity coverage exists | `layout.integration.performance_sanity` |
| The complete Phase 10 suite runs deterministically | `--filter layout.` 346/346 |
| All Phase 9 tests continue passing | `--filter css.` 616/616 |
| All existing project tests continue passing | 2367/2367, Debug and Release; 2365/2367 under sanitizers |
| Visual validation succeeds where the display path supports it | `layout.integration.showcase` plus the host checkpoint (11 probes, `RESULT: PASS`) |
| No Chromium dependency is introduced | `Tools/Build.ps1 -Verify` boundary scan |

### What 10.15 found

Five findings, recorded in full in `layout/docs/Tests.md` §20. In one line each: **the pass's own
arithmetic was unbounded**, and only an integration case could see it — four layer suites test the
geometry's maximum already, one layer at a time, and the overflow is a property of an accumulation
*across boxes*, so it needed a pass, a parent and two children; a **passed case can be the wrong case**,
which the border regression proved by failing first on the suite's own mistake (a generic keyword
value where the reader needs the `border-*-style` longhands); two **coordinate spaces** live in one
result since 10.12 and the first assertions mixed them, so the fixture now composes the chain; the
three **regression** cases each found their reproduction in a *document* rather than a suite; and the
**version pins** are load-bearing — the bump reddened three of them, with `allocator_balance` reporting
sixty-four failed checks because its loop re-asserts the version on every iteration.

The count went from **2355 to 2367**: twelve new layout cases, and the module's **344 cases in
twenty-one suites** are green in Debug, in Release, and under ASan and UBSan with no sanitizer
diagnostic — `layout.integration.*` 12/12 under them. The host visual checkpoint passes unchanged (11
probes, `RESULT: PASS`) and `robo test-qemu-display` passes; the visual-validation criterion is met the
way 10.11–10.14 met it, since the demo rendering path does not run RoboLayout and this phase may not
write a paint test. `robo test-runtime` was **2365/2367** with `window.host.keyboard` **and**
`window.host.mouse` the only red cases — **seventh** consecutive phase, on a diff touching no `window`
file, while the Debug, Release and sanitized builds each reported `ALL TESTS PASS` at 2367/2367 in the
same session. The §14 proposal to fix the focus race or quarantine the cases is now overdue by five
phases rather than one.

## Goal 11.1 — Paint architecture

**Status: complete.** RoboPaint is the fifth module in the layout stack and the first whose subject is
not a *conclusion* but a **sequence of work**: RoboLayout answers where every box is, and RoboPaint
turns those answers into an ordered list of operations a rasteriser will one day execute. This goal is
the boundary only — the pipeline stops at `RoboLayout → RoboPaint` and nothing above it — and the one
idea it turns on is that **a paint list is a record of what will be drawn, not a set of drawing
instructions, produced by one deterministic traversal of the layout tree**.

### Delivered

| Piece | Where |
|---|---|
| The module surface: eight headers and one umbrella — the operation vocabulary (`NONE`, `BEGIN_BOX`, `BACKGROUND`, four `BORDER_*`, `END_BOX`, deliberately **seven wide** because a name with no code behind it is a promise), the diagnostic domains (nine), the geometry records, the operation, the list, the context and the dump | `Include/RoboPaint/` |
| `PaintOperation` — a value record with **no pointers in it**, carrying the three rectangles in paint coordinates *and* the containing block's content origin so layout coordinates are `boxes - origin`, plus a depth that makes the tree's shape recoverable from a flat list | `RoboPaintOperation.h` |
| `PaintBoxesFromLayers` — the one derivation in the module: border box → padding box → content box, each layer **floored at zero and saturated in 64 bits**, because a forward per-layer computation puts the padding box outside its border box at the coordinate range's extreme | `RoboPaintGeometry.c` |
| `PaintClip` — **two axes, not one rectangle**: `overflow-x: hidden` beside `overflow-y: visible` is legal and a rectangle cannot express it, so each axis is a mode plus a half-open interval and `PaintClipRect` **refuses** unless both axes are bounded | `RoboPaintGeometry.h` |
| The deterministic traversal — document order, parents before children, a `BEGIN_BOX`/decoration/child/`END_BOX` sequence per box, the page origin accumulated as the containing block's content origin, and the clip refined per axis from each box's own used overflow modes | `RoboPaintContext.c` |
| The **staleness refusal**: a tree that needs layout is refused with `RoboPaintPassStale` and an empty list rather than painted, so a list built from half-computed geometry never leaves the module — the whole of this phase's integration with 10.14 | `RoboPaintContext.c` |
| The list, and the one allocation in the module: an array of value operations, cleared before every pass and grown in place, with `PaintListAppend` **refusing** any operation `PaintOperationIsValid` rejects — the safety rule enforced once in the container rather than at every call site | `RoboPaintList.c` |
| The developer's view — one line per operation, indented by the operation's own depth, with a **derived** line bound (`RoboPaintDumpLineMax`) that a suite measures against the widest line this build can produce | `RoboPaintDump.c`, `RoboPaintDump.h` |
| The module lifecycle, capabilities and version; `drawing` and `display_lists` and `raster` and `paint_invalidation` are **0**, and `decoration_ops` is **1** and named for what it is — this build records *that* a background will be filled and *which* colour; it does not fill it | `RoboPaint.c`, `RoboPaintVersion.h` |
| Three suites, 39 cases, wired explicitly in `$SuiteSrcs` and `Tests/Main.c` | `paint/tests/` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| A dedicated RoboPaint subsystem exists | `Include/RoboPaint/`, `Source/RoboPaint/`, five sources |
| A clear public/private API boundary exists | the umbrella header vs `RoboPaintInternal.h`; `PaintAddSaturated` is private |
| A paint context exists | `PaintContext` with its pass record and outcomes |
| A deterministic paint-operation representation exists | `PaintOperation` plus `PaintOperationEqual` |
| A paint-list representation exists | `PaintList`, `paint.list.*` |
| RoboPaint consumes RoboLayout output without recalculating layout | `PaintBoxesOf` reads layout's result and two inset records; the pass takes the tree `const` |
| Border/padding/content/overflow geometry is accessed correctly | `paint.geometry.*` 6, `paint.traversal.box_model_geometry` |
| A deterministic paint traversal exists | `paint.traversal.siblings_in_order`, `.counts_and_determinism` |
| Paint-list ownership and lifetime are safe | `paint.list.lifecycle`, `paint.traversal.allocator_balance`, ASan/UBSan |
| Geometry validation prevents invalid paint operations | `PaintOperationIsValid` enforced at `PaintListAppend`; `paint.operation.record`, `paint.conformance.hostile_inputs` |
| Basic paint architecture tests pass | `--filter paint.` 45/45 (39 module cases + 6 demo cases) |
| Determinism is tested | `paint.traversal.counts_and_determinism` |
| Existing Phase 9/10 tests continue passing | `--filter css.` 616/616, `--filter layout.` 346/346 |
| A minimal graphics-path validation succeeds where supported | the host visual checkpoint (11 probes, `RESULT: PASS`) — the demo path is the runtime's rendering path; drawing is `0` by design here |
| No Chromium dependency is introduced | `Tools/Build.ps1 -Verify` boundary scan |

### What 11.1 found

Five findings, recorded in full in `paint/docs/Tests.md` §6 and `paint/docs/Architecture.md` §14. In one
line each: a **forward per-layer derivation** put the padding box outside its border box at the extreme
of the coordinate range, so the derivation now works inward with saturation and a zero floor; the two
**coordinate spaces** need an assertion and not just one field each, so the traversal suite checks
`local == boxes - origin` against layout for every operation; a **one-rectangle clip** silently clips an
axis the author left open, which is why the record is two axes; a **guessed dump bound** is a latent
overflow, so `paint.dump.line_bound` measures the widest real line; and the fixture needed
`display: block` on its containers, because a container computing to `display: inline` makes layout
legitimately decline its insets — a correct layout conclusion that reads as a paint bug until the
suite's own stylesheet is fixed. One pipeline note: the new `paint.` test group is a **substring**
match, so it also carries the demo layer's six `demo.paint.*` cases.

The count went from **2367 to 2406**: 39 new paint cases. Debug `-Verify -Test` reports `ALL TESTS
PASS` at 2406/2406. `--filter paint.` is 45/45. `robo check` passes with Chromium independence CLEAN.
`robo test-runtime` remains red solely on the documented `window.host.keyboard` host-focus flake, on a
diff touching no `window` file — the eighth consecutive phase, and the §14 proposal to fix the focus
race rather than keep noting it is now overdue by five phases.

## Goal 11.2 — Paint records

**Status: complete.** The goal that makes the record model **explicit**. 11.1 established the
architecture — an operation is a value snapshot with no pointers, the list owns it, the traversal
orders it deterministically and the dump reads it back — so "records" is what this phase
consolidates rather than something it introduces. What it adds is the one record the first release's
sequence was missing, and the tests that pin the guarantees the brief demands: a regular sequence, an
explicit content boundary, a snapshot that does not move, and ownership that copies.

### Delivered

| Piece | Where |
|---|---|
| The **`CONTENT`** record — the boundary between a box's own records and its descendants', emitted for **every** placed box, acting on the content box. It is the one kind since 11.1, and it is what makes the sequence regular: a consumer reads a box's own painting as `[BEGIN_BOX .. CONTENT]` and its subtree as `(CONTENT .. END_BOX)` without inferring the boundary from the next child's depth | `RoboPaintTypes.h`, `RoboPaintContext.c` |
| The **structural/drawing split**, made an explicit rule: structural records (`BEGIN_BOX`, `CONTENT`, `END_BOX`) are emitted for every box and carry none; drawing records (`BACKGROUND`, the four `BORDER_*`) are emitted only when they draw. `PaintOpKindIsStructural` / `PaintOpKindDraws` are the partition, and the validity rule splits on **drawing** rather than on **marker** so `CONTENT` is held to the structural side | `RoboPaintTypes.h/.c`, `RoboPaintOperation.c` |
| The record's one-line writer learning `CONTENT` — it prints the rectangle it acts on (` content=<the content box>`) so a reader of a dump can tell a boundary from a marker | `RoboPaintOperation.c` |
| Version `0.2.0` — one enumerator and no new door; `RoboPaintDrawingImplemented` stays `0`, because a `CONTENT` record is a statement about the sequence and not a pixel either | `RoboPaintVersion.h` |
| The **record-model suite** — 13 cases in `paint.records.*`: the sequence and its boundary, the fields and the partition, snapshot isolation under a re-layout, ownership and copy-by-value, the empty and degenerate cases, geometry safety at the coordinate range's maximum, determinism across two independently built trees, the dump, and allocation balance | `paint/tests/SuiteRoboPaintRecords.c` |
| The build/registry wiring, and the version pins the bump forced a phase to update — the pins working as intended | `Tools/Build.ps1`, `Tests/Main.c`, `SuiteRoboPaint.c`, `SuiteRoboPaintConformance.c` |

### Completion criteria

| Criterion | Where it is proved |
|---|---|
| RoboPaint record types exist | `PaintOpKind` — nine kinds, structural and drawing |
| PaintRecord ownership is explicit | `RoboPaintList.h`; `paint.records.ownership` |
| PaintRecordList exists | `PaintList` — create/clear/append/at/operations/equal |
| Record insertion preserves deterministic order | `paint.records.nested_order`, `.siblings_order` |
| Records snapshot required geometry | `paint.records.content_geometry`, `paint.traversal.box_model_geometry` |
| Records snapshot required paint metadata | colour per drawing record; the content box on `CONTENT`; `paint.records.fields` |
| Records do not mutate RoboLayout | the pass takes the tree `const`; `paint.records.snapshot_isolation` |
| Nested nodes produce correct ordering | `paint.records.nested_order` (four-deep chain, depths 0–3) |
| Empty cases are safe | `paint.records.empty_cases` (empty body, zero-sized box, empty dump) |
| Geometry is validated | `PaintOperationIsValid` at `PaintListAppend`; `paint.records.geometry_safety` |
| Mutation isolation is tested | `paint.records.snapshot_isolation` (re-layout of the same tree leaves the first list's records byte-identical) |
| Determinism is tested | `paint.records.determinism` (two passes, and a second independently built tree, compared by list and dump) |
| Debug dump works | `RoboPaintDump.c`; `paint.records.dump` |
| Existing tests remain passing | `--filter paint.` 58/58; whole suite 2417/2419 with the two documented host-focus flakes |
| No Chromium/Blink runtime dependency was introduced | `Tools/Build.ps1 -Verify` boundary scan; `robo check` CLEAN |
| No out-of-scope subsystem was implemented | the deferral tables in `paint/README.md` and `paint/docs/Architecture.md` §13 are unchanged except for `CONTENT` |

### What 11.2 found

Three findings, recorded in `paint/docs/Tests.md` §7. In one line each: **"marker" did not partition
the vocabulary** — 11.1's colour rule split on `is_marker`, which had exactly two members, so a third
structural kind fell through to the drawing half and was asked for a colour it has no use for; two
failing conformance checks exposed it, and the split is now `draws`; a **snapshot test has to prove
its mutation**, so the isolation case uses a block with no declared width (which fills its containing
block) and asserts the two lists *differ* as well as that the first did not move; and a **count
assertion is a calibration, not a constant** — two cases named an operation total that omitted the
document's decorations, so the arithmetic is now stated in the case.

The count went from **2406 to 2419**: 13 new paint cases, and the module's **52 cases in four
suites**. Debug and Release `-Verify -Test` each report `ALL TESTS PASS`; the sanitized build is
clean under ASan and UBSan with no diagnostic. `--filter paint.` is **58/58** (52 module cases plus
the six `demo.paint.*` cases the substring filter also matches). `robo check` passes with Chromium
independence CLEAN, and the host visual checkpoint passes unchanged (11 probes, 15 checks,
0 failed — `RESULT: PASS`). `robo test-runtime` remains red solely on the documented
`window.host.keyboard` and `window.host.mouse` host-focus flakes, on a diff touching no `window`
file — the ninth consecutive phase, and the §14 proposal to fix the focus race rather than keep
noting it is now overdue by five phases.

## Goal 11.3 — Display lists

**Status: complete.** The goal above the record list, and the first RoboPaint release that produces
an **artifact** rather than a description: a `PaintDisplayList` -- an ordered sequence of
`PaintDisplayCommand` -- that a rasteriser walks once and never consults the box model for.
The phase's one idea is that a paint operation says *which rectangles a decision was made from* while
a display command says *which one rectangle to act on, resolved*, and the resolution is where the
work is.

**Done:**

| Thing | Where |
|---|---|
| `PaintDisplayCommand` -- a value record with the **one resolved rectangle** the command acts on, plus node, depth, clip and colour. The vocabulary is **five kinds**, one narrower than the record's seven: the four border kinds collapse into one `DRAW_BORDER`, because the fact that distinguished them (which side) is consumed into the band rectangle | `RoboPaintDisplayCommand.h/.c` |
| The **record-to-command mapping** as a total function: `BEGIN_BOX`→`BEGIN_NODE` over the border box, `BACKGROUND`→`DRAW_BACKGROUND` over the border box, `BORDER_*`→`DRAW_BORDER` over the **band** (via `PaintOperationSideBand`), `CONTENT`→`CONTENT` over the content box, `END_BOX`→`END_NODE` over the border box. Node, depth, clip and colour carry through unchanged, and the mapping is one-to-one so the two lists pair positionally | `RoboPaintDisplayListBuilder.c` |
| The **padding box disappears** and that is the point: it was an input to the band derivation, and nothing above needs it once the band is resolved. The coordinate space is unchanged -- paint coordinates, one page space | `RoboPaintDisplayListBuilder.c`, `RoboPaintDisplayCommand.h` |
| The **builder**, and the phase's second claim: a paint list is a container that validates each operation but not their *sequence*, so the builder walks the records with a stack of the open boxes and **refuses** a list that is not a traversable document -- an unclosed box, a wrong-depth begin, a boundary with no box, a close before the boundary, a marker naming the wrong node, a decoration after the boundary. A refused build leaves the output list **empty** | `RoboPaintDisplayListBuilder.c` |
| The list and its ownership, shaped like the paint list's: one growable array of value commands, cleared first and grown in place, with `PaintDisplayListAppend` refusing any command the validity rule rejects | `RoboPaintDisplayList.c` |
| The developer's view -- one line per command, indented by the command's own depth, with a **derived** line bound (`RoboPaintDisplayDumpLineMax`) a suite measures against a real widest line | `RoboPaintDisplayDump.c/.h` |
| Version `0.3.0`; `display_lists` flips to **1** while `drawing` stays **0** -- a render-ready list exists and no colour reaches a surface, and keeping the two fields apart is what stops the first from being read as the second. A new `RoboPaintDomainDisplayList` diagnostic domain (count 10) | `RoboPaintVersion.h`, `RoboPaint.c`, `RoboPaintTypes.h` |
| The **display-list suite** -- 13 cases in `paint.display.*`: the vocabulary, the empty list, the mapping and resolved geometry checked against the records in lockstep, ordering and recoverable nesting, the clip carried, six malformed lists refused (and a well-formed hand-built one accepted), ownership and allocation balance, determinism by list and by dump, mutation isolation under a record-list clear and a re-layout, empty and degenerate documents, geometry safety, and the whole pipeline end to end | `paint/tests/SuiteRoboPaintDisplayList.c` |

### Completion criteria

| Criterion | Evidence |
|---|---|
| `RoboPaintDisplayList` exists | `RoboPaintDisplayList.h`; `PaintDisplayList` |
| DisplayCommand representation exists | `RoboPaintDisplayCommand.h`; `PaintDisplayCommand` |
| DisplayListBuilder/conversion layer exists | `RoboPaintDisplayListBuilder.h`; `PaintDisplayListBuilderBuild` |
| PaintRecordList converts into the display list | `paint.display.single_node_mapping`, `paint.display.end_to_end` |
| Command payloads are snapshot-based | a command is a value with no pointers |
| Ownership is explicit | `RoboPaintDisplayList.h`; `paint.display.ownership_and_balance` |
| Ordering is deterministic | `paint.display.ordering`, `paint.display.determinism` |
| Geometry is validated | `PaintDisplayCommandIsValid`; `paint.display.geometry_safety` |
| Nested nodes work | `paint.display.ordering` walks the recovered nesting |
| Empty cases work | `paint.display.empty_list`, `paint.display.empty_and_degenerate` |
| Mutation isolation is tested | `paint.display.mutation_isolation` |
| Debug dump works | `RoboPaintDisplayDump.c`; `paint.display.dump` |
| End-to-end PaintRecord → DisplayList test passes | `paint.display.end_to_end` |
| Existing tests remain passing | whole suite green |
| No Chromium/Blink runtime dependency | boundary scan CLEAN; `robo check` PASS |
| No out-of-scope subsystem was implemented | the deferral tables in `paint/README.md` and `paint/docs/DisplayList.md` §8 |

### What 11.3 found

Three findings, recorded in `paint/docs/Tests.md` §8. In one line each: **`RoboDisplay` is already
taken** -- the obvious header names for a display list collide with the low-level RoboDisplay
module's namespace *and* with the boundary scan that forbids RoboPaint from including anything named
`RoboDisplay`, so the display list lives in RoboPaint's own namespace (`RoboPaintDisplay*.h`,
`robo_paint_display_*`), which is where it belongs; the **version and domain-count pins are real
work** -- bumping to `0.3.0` and adding the domain failed two existing cases until their pins were
updated, which is the pins doing their job rather than a nuisance; and the **builder's stack is why
it is an object** -- the structural check needs the node identity of every open box, not just a
depth counter.

The count went from **2419 to 2432**: 13 new display cases, and the module's **65 cases in five
suites**. Debug and Release `-Verify -Test` each report `ALL TESTS PASS`; the sanitized build is
clean under ASan and UBSan with no diagnostic. `--filter paint.` is **71/71** (65 module cases plus
the six `demo.paint.*` cases the substring filter also matches), and `robo test-paint` passes.
`robo check` passes with Chromium independence CLEAN, and the host visual checkpoint passes unchanged
(11 probes, 15 checks, 0 failed -- `RESULT: PASS`).

## Goal 11.4 — Background painting

**Status: complete.** The first RoboPaint release that puts a colour on a **surface**, and the arrow
the previous three phases left empty. `robo_paint_background_paint` walks a display list and fills
every `draw-background` command into a caller's RoboGraphics surface; everything else is **skipped
and counted**. `RoboPaintDrawingImplemented` flips to **1** -- the condition the version header
wrote down for the flip was exactly "a build in which an operation's rectangle and colour reach a
RoboGraphics surface" -- while `raster` stays **0**, because this painter consumes exactly one
command kind.

**Done:**

| Thing | Where |
|---|---|
| The **colour bridge**, one direction and one record: `PaintColorToRgba8` is a field-for-field copy between RoboCSS's `CssColor` and RoboGraphics' `GfxColorRgba8` -- both 8-bit sRGB with straight alpha, so there is no arithmetic, no colour space and nothing to lose or invent, and alpha survives rather than being flattened. The four kinds are **decided rather than flattened**: a resolved colour draws, a fully transparent one converts and does not draw, `currentColor` is **refused** (only the layer that owns an element's `color` can resolve it -- that is the paint pass, not this bridge), `none` means nothing was declared and `invalid` means it was meant to be a colour and was not | `RoboPaintColor.h/.c` |
| The **background pass**: walk the display list once, in order, resolve each `draw-background`'s colour, apply its clip, fill its rectangle through `GfxRasterFillRect`. `begin-node`/`content`/`end-node` draw nothing and a `draw-border` band is a later goal's -- all four are **skipped and counted**, so "the backgrounds are painted and the borders are not" is a number rather than a silence | `RoboPaintBackground.h/.c` |
| The **list is the input, not the records**, because the display list is the artifact whose geometry is *resolved*: a command carries the one rectangle to fill and the boundary in force, so the painter never reads the box model, a computed style or the tree. The only conversion left is the colour | `RoboPaintBackground.h` |
| The **clip read rather than implemented**: reset the raster's clip to the surface bounds (so nothing leaks from the previous command), replace the axis a boundary hides while leaving an open axis at the surface's own interval, and let `GfxRasterSetClip` intersect -- so "the boundary bounds the fill" is the graphics module's arithmetic, and an `overflow: hidden` ancestor bounds a descendant's background without a raster clipping system existing | `RoboPaintBackground.c` |
| The **pass record**, whose fields are the *pairs*: `commands`/`backgrounds`/`filled`/`skipped`/`contextual`/`transparent`/`empty`/`clipped`, with two identities holding by construction -- `commands = backgrounds + skipped` and `backgrounds = filled + contextual + transparent + empty` | `RoboPaintBackground.h` |
| Version `0.4.0`; `drawing` flips to **1** and `background_painting` is added as **1** -- the field that says *which* drawing exists -- while `raster` stays **0** | `RoboPaintVersion.h`, `RoboPaint.c` |
| The **background suite** -- 14 cases in `paint.background.*`: the colour bridge's five kinds; a document's colour read back from the surface at the centre of the rectangle the list named and nowhere one pixel past it; the fill order proving depth-first traversal; the clip A/B against a `visible` control one declaration away plus the axis split read from a hand-built command; the three "nothing to paint" documents (no declaration, `borders_not_painted`, zero-area); a relative offset moving the fill and not the size; the counter identities; the four "draws nothing" reasons on a hand-built list; determinism across two surfaces and a repeat pass in place; snapshot isolation across a re-layout; and the empty and refusing cases | `paint/tests/SuiteRoboPaintBackground.c` |

### Completion criteria

| Criterion | Evidence |
|---|---|
| The CSS background representation is understood and reused | `background-color` only, resolved by the paint pass; `background:`'s expansion is RoboCSS's |
| Solid background painting works for supported colours | `paint.background.pixel_fill`, `paint.background.end_to_end` |
| Correct layout geometry is used | the command's resolved rectangle; the painter reads no box model |
| CSS colours convert correctly to RoboGraphics colours | `RoboPaintColor.c`; `paint.background.color_bridge` |
| `DrawBackground` commands carry real drawable data | 11.3's command, now consumed |
| Display-list ordering is correct | `paint.background.ordering` (the child's colour wins where they overlap) |
| Transparent/no-background cases work | `paint.background.nothing_to_paint` (no command exists at all) |
| Geometry safety is enforced | `paint.background.zero_area`, `paint.background.hand_built_kinds` |
| Mutation isolation is verified | `paint.background.mutation_isolation` |
| Deterministic output is verified | `paint.background.determinism` |
| RoboGraphics is used for the actual drawing primitive | `GfxRasterFillRect` through a raster the pass creates and destroys |
| A minimal visual smoke test works | the suite reads a real surface back at the rectangle each command names |
| Existing tests remain passing | whole suite green |
| No Chromium/Blink runtime dependency | boundary scan CLEAN; `robo check` PASS |
| Nothing out of scope was implemented | `paint/README.md` §Boundaries and `paint/docs/Background.md` §11 |

### What 11.4 found

Four findings, three in `paint/docs/Tests.md` §9 and the fourth in `paint/docs/Architecture.md` §14.
In one line each: **a colour refusal arrives as a different code one level down than the layer above
promised** -- the graphics raster refuses a target that cannot take a write with
`RoboInvalidArgument` and an unimplemented format with `RoboNotSupported`, while the painter's
header said "read-only or unsupported" was one `RoboNotSupported`, and a *test* is what made the
two statements meet (the mapping is now explicit in one place, with `RoboOutOfMemory`
deliberately not folded in); **"every fill's centre is its own colour" is false the moment two
fills overlap** -- a child's background covers its parent's, so the expectation at a point is the
colour of the *last* drawable command whose rectangle contains it and whose clip admits it, which is
what "the fills happen in the list's order" means stated where a pixel can be read; **a two-document
comparison must spell out both documents** -- the clipping case had built its stylesheet from a
variable left at one value, so it compared a thing with itself until the two sheets were written out
side by side; and **the host test link is the one command in this tree whose argument list can fail**
-- 498 arguments and 32,741 characters crossed Windows' ~32k process limit once the sanitizer flags
were added, and the failure mode is *the compiler not starting* rather than a build error, so
`Tools/Build.ps1` now hands the list over in a response file (and the list must be written with
**forward slashes**, because clang's response-file tokenizer treats `\` as an escape and silently ate
every separator in a Windows path -- `D:\Muhammad\...` arriving as `D:Muhammad...`).

The count went from **2432 to 2446**: 14 new background cases, and the module's **79 cases in six
suites**. Debug and Release `-Verify -Test` each report `ALL TESTS PASS`; the sanitized build is
clean under ASan and UBSan with no diagnostic. `--filter paint.` is **85/85** (79 module cases plus
the six `demo.paint.*` cases the substring filter also matches), `--filter paint.background.` is
**14/14**, and `robo test-paint` passes. `robo check` passes with Chromium independence CLEAN, and
the host visual checkpoint passes unchanged (15 checks, 0 failed -- `RESULT: PASS`).

## Next

- **Phase 11.5 — the next RoboPaint goal.** 11.4 fills exactly one command kind and stops at it:
  `drawing` and `background_painting` are `1`, `raster` is still `0`, and the border bands, text,
  images, opacity, real clipping, transforms, batching, rasterisation of the whole list, paint
  invalidation (11.11) and hit testing (11.10) are all still absent. The deferred tables in
  `paint/README.md` and `paint/docs/Background.md` §11 name every one of them, and
  `paint/docs/Architecture.md` §15 lists the eighteen rules the next goal may assume. Do not start
  until requested

## Goal 11.5 — Border painting

**Status: complete.** The second drawing this module does, and the first one with a **shape**. A
box's four border sides are filled into a caller's RoboGraphics surface, in the same pass that fills
its background -- and the pass is *one* pass because backgrounds and bands are one **order**: the
list interleaves them the way the traversal emits them, and CSS paints a descendant's background
*after* its ancestor's border, so a child that spills over its parent draws over the parent's ring.
The release also fixes one thing 11.3 had dropped a stage early -- the **`border-style`** -- which is
the only fact about a band that its rectangle cannot state.

**Done:**

| Thing | Where |
|---|---|
| The **style travels**: a `BORDER_*` record and a `draw-border` command each carry the side's `border-style`, required to be one that **generates a border** (`generates_border`, not "not `none`" -- `hidden` draws nothing, which is why RoboLayout gives such a side a used width of zero and no record exists at all), and required to be `none` for every other kind. The side is *consumed* into the band rectangle and the style is not, because the rectangle says **where** a band is and the style is the shape **inside** it | `RoboPaintOperation.h`, `RoboPaintDisplayCommand.h`, `RoboPaintContext.c`, `RoboPaintDisplayListBuilder.c` |
| **One pass, both drawing kinds**: `PaintDecorationPaint` walks the display list once, in order, and fills a background's border box and a band alike -- replacing 11.4's background-only door rather than sitting beside it, because two passes (one per kind) would fill every background in the document before every border and paint an ancestor's ring over a descendant's spill | `RoboPaintDecoration.h/.c` |
| The **band** is the region of the border box outside the padding box on one edge, derived once at build time by `PaintOperationSideBand` and carried as the command's one rectangle. Both faces of an axis contribute to the *other* axis's extent, so a box with two borders has two full-height bands | `RoboPaintOperation.c` (11.1), consumed here |
| The **seven pattern styles** (`dotted`, `dashed`, `double`, `groove`, `ridge`, `inset`, `outset`) are drawn as their **band**, in the side's colour -- CSS's `solid` shape -- and counted (`stats.unshaped`), because a border drawn as a solid band when the author asked for dots is exactly the kind of quiet wrongness this module counts instead of shipping | `RoboPaintDecoration.h/.c` |
| The **pass record** grows three fields and three identities: `draws`, `borders` and `unshaped` join `commands`/`backgrounds`/`filled`/`skipped`/`contextual`/`transparent`/`empty`/`clipped`, and `commands = draws + skipped`, `draws = backgrounds + borders`, `draws = filled + contextual + transparent + empty` all hold by construction and are asserted | `RoboPaintDecoration.h` |
| Version **0.5.0**; `border_painting` is added as **1** beside `background_painting`, while `raster` stays **0** -- the three structural command kinds are not drawings | `RoboPaintVersion.h`, `RoboPaint.c` |
| The **border suite** -- 14 cases in `paint.border.*`: the style end to end at both ends of the chain; the four bands' geometry derived from the box's own border box; per-side widths from a four-value shorthand; six documents one declaration apart for the sides that are *not* there; four shorthand colours and the four **corner** pixels that say which band was drawn last; background-plus-border ring/interior/outside probes; the decisive **child over its parent's border** case; the clip A/B plus the axis split; the seven pattern styles; the style rule and the counter identities on a hand-built list; determinism and snapshot isolation; the empty and refusing cases; and the whole document end to end | `paint/tests/SuiteRoboPaintBorder.c` |
| The existing suites updated to the generalised pass: `paint.background.*` keeps its 14 cases, with the oracle now following **every** drawing kind and the counters rewritten to the three identities; the unit suite's hand-built records carry the style their kind requires, and the validity case asserts both halves of the style rule | `SuiteRoboPaintBackground.c`, `SuiteRoboPaint.c`, `SuiteRoboPaintConformance.c` |

### Completion criteria

| Criterion | Evidence |
|---|---|
| The existing CSS border representation is reused | per-side used width/style/colour from RoboLayout's record; no CSS parsing here |
| Supported border styles are painted | `paint.border.pattern_styles` (seven styles), `paint.border.four_bands` (`solid`) |
| Border geometry comes from completed layout | the band is a difference of two rectangles layout wrote; the painter reads no box model |
| Border widths are handled correctly | `paint.border.per_side_widths`, `paint.border.absent_sides` |
| Border colours use the existing colour system | `paint.border.colors_and_corners`; the same `PaintColorToRgba8` door |
| Border records carry real drawable data | `paint.border.style_reaches_the_command` (the record's accessor) |
| `draw-border` commands carry real drawable data | the same case (the command's field) plus `style=` in the display dump |
| All supported sides render correctly | `paint.border.four_bands`, `paint.border.with_background` |
| Paint ordering is correct | `paint.border.child_over_parent_border` -- the case two passes would fail |
| Background + border integration works | `paint.border.with_background`, `paint.border.end_to_end` |
| Zero/transparent/no-border cases are safe | `paint.border.absent_sides`, `paint.border.hand_built_kinds` |
| Geometry validation is enforced | the command's validity rule, plus the saturating band derivation |
| Mutation isolation is verified | `paint.border.mutation_isolation` |
| Deterministic output is verified | `paint.border.determinism` (two passes, and an independently rebuilt list compared command for command) |
| RoboGraphics performs the actual drawing | `GfxRasterFillRect`, one call per drawing command |
| A minimal visual smoke test works | the suite paints a background-plus-border document into a real surface and reads the ring, the interior and the outside back |
| Existing tests remain passing | whole suite green |
| No Chromium/Blink runtime dependency | boundary scan CLEAN; `robo check` PASS |
| No out-of-scope subsystem was implemented | no mitring, no `border-radius`, no patterns, no batching, no rasteriser of the whole list |

### Results

`Tools/Build.ps1 -Configuration Debug/Release -Verify -Test` each report **ALL TESTS PASS**
(2460 cases, `Failed: 0` on both). The sanitized build is clean under ASan and UBSan -- no
diagnostic of either kind in the whole run -- and reports **2458/2460**, red on exactly the two
documented host-focus flakes (`window.host.keyboard`, `window.host.mouse`, both failing
`WindowRequestFocus(id) == RoboOk (7 != 0)`), on a diff touching no `window` file and
with those two green in the Debug and Release runs above.
`--filter paint.border.` is **14/14**, `--filter paint.` is **99/99** (93 module cases plus the six
`demo.paint.*` cases the substring filter also matches), `--filter layout.` is **346/346**, and
`robo test-paint` passes. `robo check` passes with Chromium independence CLEAN, and the host visual
checkpoint passes (15 checks, 0 failed -- `RESULT: PASS`).

### Files

**Added:** `Include/RoboPaint/RoboPaintDecoration.h`, `Source/RoboPaint/RoboPaintDecoration.c`,
`paint/tests/SuiteRoboPaintBorder.c`, `paint/docs/Border.md`.
**Retired:** `RoboPaintBackground.h/.c` (replaced by the generalised door; its suite and its
document remain, renamed as a document to `paint/docs/Decoration.md`).
**Modified:** `RoboPaint.h`, `RoboPaintVersion.h`, `RoboPaint.c`, `RoboPaintOperation.h/.c`,
`RoboPaintDisplayCommand.h/.c`, `RoboPaintDisplayListBuilder.c`, `RoboPaintContext.c`,
`Tests/Main.c`, `Tools/Build.ps1`, the three existing paint suites, `paint/README.md`,
`paint/docs/Architecture.md`, `DisplayList.md`, `Operations.md`, `Tests.md`, `PROGRESS.md`,
`ARCHITECTURE.md`, `DEPENDENCIES.md` and `.cursor/rules/runtime-plan.mdc`.

## Goal 11.6 — Text painting

**Status: complete.** The first drawing this module does whose payload is **not a number**. Every
release before it put a value -- a colour, a rectangle, a band, a style enumerator -- into a fixed
record, which is what made a paint list safe to copy, to discard and to rebuild. A text run carries
**variable-length data**, and the whole of this release is what that costs: the characters live in a
**pool** owned by exactly one list, a record carries a **span** (two integers) into it, and the
display list owns a pool of its own so the artifact keeps 11.2's snapshot property one stage
further along. The runs painted are the runs RoboLayout measured -- in logical order, at the font's
own advances -- and the passage to a shaped run is left as an explicit seam for Phase 14.

**Done:**

| Thing | Where |
|---|---|
| The **code-point pool and the span**: `PaintTextPool` is one contiguous array a list owns, and a record names characters with `first`/`count` rather than a pointer -- so a text operation is still a value record with no pointers in it and "no dangling pointer" is still true by construction. The pool is a type of its own because it has **one implementation and two owners** (the record list and the display list), and every door that consumes a span takes the pool and **refuses** one that runs past the end rather than reading whatever is there | `Include/RoboPaint/RoboPaintText.h`, `Source/RoboPaint/RoboPaintText.c` |
| **Code points, not bytes and not glyph ids**: code points are the unit RoboLayout measures in, so a run is measured and painted in the same unit; glyph ids are deliberately not stored, because a glyph id is a fact about a *font* and a list that held them would be a list only that font could draw. Storing characters keeps the display list a statement about the **document** and puts glyph resolution at the drawing step, where RoboGraphics' own bridge says it belongs | `RoboPaintText.h` |
| The **`TEXT` record** (kind 8, a **drawing** kind): the font inputs and the **baseline offset** beside the span. The baseline is an *offset from the run rectangle's top* and not a coordinate -- one number in one space, so there are not two spaces to keep in agreement -- read through `LayoutBaselineOffset` and **never recomputed**, because 10.7 placed it and a painter that derived it would be a second baseline model | `RoboPaintOperation.h/.c`, `RoboPaintContext.c` |
| A run is a **child** in the sequence, not a box: one `TEXT` record, at one depth past the box whose element owns the text node, naming itself and appearing **after that box's `CONTENT`** -- and no `BEGIN`/`END` pair, because a run has nothing to open and no children | `RoboPaintContext.c`, `RoboPaintDisplayListBuilder.c` |
| The **`draw-text` command** (kind 5) and the builder's **pool copy**: every span is re-numbered into the output's own pool by `PaintTextPoolCopy`, the one door between two pools, which moves bytes from one owned array to another instead of round-tripping through a buffer sized to the longest run in the document | `RoboPaintDisplayCommand.h/.c`, `RoboPaintDisplayListBuilder.c` |
| **Glyphs reach a surface**: the pass resolves each code point through the **caller's face** and strokes its outline with `GfxRasterDrawGlyph` at the run's own pixel size, from the rectangle's left edge, on the baseline. A glyph whose metrics will not parse advances by **nothing** and is counted as a miss -- the measurer's refusal restated, because a broken font that produced a narrower run here would pile glyphs on each other in a way that reads as a rendering bug rather than a missing table | `RoboPaintDecoration.c` |
| The pass **prepares the face's cmap**, once, before the first lookup -- a `FontFace` is created without one and `FontFaceLookupGlyph` answers `RoboNotSupported` for a face whose map has not been parsed, which is the same door the layout measurer calls (`RoboLayoutTextFont.c`). A face whose map will not parse is still not a refusal: every glyph becomes `.notdef` and `glyphs_missing` names the fact per code point | `RoboPaintDecoration.c` |
| The **pass record** grows `texts`, `no_font`, `glyphs`, `glyphs_missing` and `rtl`, and the identities move with them: `draws = backgrounds + borders + texts`, `draws = filled + contextual + transparent + empty + no_font`. A text run is **exempt from the zero-area rule** -- a run's rectangle is an *advance* box and a glyph's ink can overhang it -- and `no_font` sits on the far side of `draws` from the other three because it is a fact about the **call** rather than the document | `RoboPaintDecoration.h` |
| The dumps: a record line and a command line each carry ` text="..."`, bounded and escaped -- the one thing a record's own writer cannot produce, because a span is two integers and the characters belong to the pool, so the door that *has* the pool appends it | `RoboPaintDump.c`, `RoboPaintDisplayDump.c` |
| Version **0.6.0**; `text_painting` is added as **1** beside `background_painting` and `border_painting`, while `raster` stays **0** | `RoboPaintVersion.h`, `RoboPaint.c` |
| The **text suite** -- 17 cases in `paint.text.*`: the characters travelling record pool -> display pool -> pixels and read back through the pool's own door; code points against an accented word (four code points, five UTF-8 bytes); the run's boxes checked against the tree with the box suites' own helper; `draw-text` at one depth past its parent box and after its `CONTENT`; two participants in one inline context landing at two line tops; colour; glyphs stroked into a real surface from a synthetic face; an unmapped code point drawn as `.notdef` and counted; a document with text painted with **no face**; an RTL run counted; the record list freed and the display list still writing and painting identically; the validity rules and the pool doors on hand-built values; empties; determinism against an independently rebuilt tree; and the dump | `paint/tests/SuiteRoboPaintText.c` |
| The existing suites updated for the new vocabulary: the unit suite's `text` record needs its run or the validity rule refuses it; the display list's lockstep walk gains the `TEXT` -> `DRAW_TEXT` mapping and the run's rectangle; the conformance suite's positional capabilities list gains `text_painting`; the traversal suite reads a run's characters back through the list's pool | `SuiteRoboPaint.c`, `SuiteRoboPaintDisplayList.c`, `SuiteRoboPaintConformance.c`, `SuiteRoboPaintTraversal.c` |

### Completion criteria

| Criterion | Evidence |
|---|---|
| The existing text/layout infrastructure is understood and reused | the run, its range, its baseline and its font inputs all come from RoboLayout's own doors; no shaping is re-derived |
| Text PaintRecord exists | `RoboPaintOpText`, `PaintOperationText`; `paint.text.record_shape` |
| Text paint data is snapshot-based | the characters live in the list's pool and the span is two integers; clearing the records cannot change a character the list draws -- `paint.text.mutation_isolation` |
| Text geometry comes from completed layout | `paint.text.order_and_depth` and the shared `paint_local_boxes_match_layout` helper on every record |
| Baseline is preserved correctly | `paint.text.record_shape` reads it through `LayoutBaselineOffset` |
| Text colour uses the existing colour system | the same `PaintColorToRgba8` door; `paint.text.color` |
| RoboFont is used for font/glyph handling | `FontFaceParseCmap` + `FontFaceLookupGlyph` + `GfxGlyphMetricsDevice`; no second loader |
| `DrawText` display command exists | `RoboPaintDisplayCmdDrawText`, `PaintDisplayCommandText`; `paint.text.command_and_span` |
| PaintRecord -> DrawText conversion works | the builder's pool copy, asserted with both pools read back |
| Multiple text runs are supported | `paint.text.multiple_runs`, `paint.text.wrapped_lines` |
| Empty/missing-font cases are safe | `paint.text.empty_cases`, `paint.text.no_font`, `paint.text.missing_glyph` |
| Geometry is validated | the run's own validity rule, plus the saturation every geometry door already had |
| Mutation isolation is verified | `paint.text.mutation_isolation` |
| Determinism is verified | `paint.text.determinism` (an independently rebuilt tree, compared by every run and both dumps) |
| Debug dump includes text operations | `paint.text.dump`; `paint.dump.line_bound` still measures the widest real line |
| Basic Latin text reaches the graphics path | `paint.text.pixels` -- a real surface with the run's own colour on it -- and the host visual checkpoint, whose text probes are ink at the heading, paragraph and link positions |
| Existing tests remain passing | whole suite green |
| No Chromium/Blink runtime dependency | boundary scan CLEAN; `robo check` PASS |
| No out-of-scope subsystem was implemented | no shaping, no bidi, no line breaking, no fallback, no image kind, no clipping, no opacity, no rasteriser of the whole list |

### Results

`Tools/Build.ps1 -Configuration Debug -Verify -Test` and `-Configuration Release -Verify -Test` each
report **ALL TESTS PASS** -- **2477 cases, `Failed: 0`** on both -- and the sanitized build (ASan +
UBSan) is **clean with no diagnostic of either kind** over the whole run. A later full run of the
finished tree reports **2476/2477**, red on one of the two **documented host-focus flakes** --
`window.host.keyboard`, failing `WindowRequestFocus(id) == RoboOk (7 != 0)`, the signature
11.5's report already records -- in a module this diff does not touch and green in the Debug,
Release and sanitized runs above; the Win32 focus request depends on which process owns the desktop
foreground when the suite runs, which is why it moves between runs and not between diffs.
`--filter paint.text.` is
**17/17**, `--filter paint.` is **116/116** (110 module cases plus the six `demo.paint.*` cases the
substring filter also matches), `--filter layout.` is **346/346**, `--filter css.` is **616/616**, and
`robo test-paint` passes. `robo check` passes with Chromium independence CLEAN, and the host visual
checkpoint passes: **15 checks, 0 failed -- `RESULT: PASS`**, with `paint: blocks=22 rects=50 lines=3
text_runs=19` and the five ink probes (`heading-ink`, three `paragraph-ink`, two `link-ink`) each
finding a run's own colour at the placed position.

### Files

**Added:** `Include/RoboPaint/RoboPaintText.h`, `Source/RoboPaint/RoboPaintText.c`,
`paint/tests/SuiteRoboPaintText.c`, `paint/docs/Text.md`.
**Modified:** `RoboPaint.h`, `RoboPaintVersion.h`, `RoboPaint.c`, `RoboPaintTypes.h/.c`,
`RoboPaintOperation.h/.c`, `RoboPaintList.h/.c`, `RoboPaintContext.h/.c`,
`RoboPaintDisplayCommand.h/.c`, `RoboPaintDisplayList.h/.c`, `RoboPaintDisplayListBuilder.c`,
`RoboPaintDecoration.h/.c`, `RoboPaintDump.h/.c`, `RoboPaintDisplayDump.h/.c`,
`RoboPaintInternal.h`, `Tests/Main.c`, `Tools/Build.ps1`, the five existing paint suites,
`paint/README.md`, `paint/docs/{Architecture,Decoration,DisplayList,Operations,Tests,Border}.md`,
`PROGRESS.md`, `ARCHITECTURE.md`, `DEPENDENCIES.md` and `.cursor/rules/runtime-plan.mdc`.

### What 11.6 found

1. **A structural check ordered before the rule it exempts is unreachable.** The display builder's
   text branch sat *after* the generic "a record's depth is its innermost box's" check -- a rule a
   run fails by construction, since a run is owed one level **down** from the box that owns it -- so
   every document with text was refused with that message and the text branch could never run. The
   fix is an ordering, not a rule.
2. **A face is created without a cmap and the drawing layer must prepare it.** Without the
   `FontFaceParseCmap` call the pass drew nothing and reported every code point missing --
   a rendering-shaped failure with a font-shaped cause.
3. **A synthetic face can be *nearly* right.** The suite's hand-written format-4 cmap carried one
   segment too many for two, which shifted `idRangeOffset` onto the wrong word; the parser read a
   plausible glyph array out of the extra bytes, every lookup resolved past `maxp`'s `numGlyphs`,
   and the pass reported `INVALID_FONT` while drawing nothing. Both halves are recorded because
   they are the two ways a font test passes for the wrong reason.
4. **A wrapped line is two participants, not one fragmented run.** RoboLayout measures a text node
   as a **unit**, so a run wider than its line wraps whole rather than breaking; the case that
   expected fragments was asserting a later phase's behaviour and now asserts the build's -- which
   is the stronger statement, because it pins that the wrap is layout's rather than paint's.

## Goal 11.7 — Image painting

_Implemented. RoboPaint **0.10.0**, capability `image_painting` = **1**; `raster` stays **0**.
RoboGraphics **0.17.0** adds `GfxRasterDrawImage`, the scaled, alpha-blended blit the pass
uses._

**Order of landing.** 11.7 is the goal that 11.8, 11.9 and 11.10 were implemented **ahead of** --
each of those three sections says so in its own words, and the tree they were built on had no
`IMAGE` kind and no `draw-image` command at all. This section is written where the goal belongs --
between 11.6 and 11.8 -- and the three later sections have been re-read against it: the vocabulary
they extended gained a **row** and a **field** from this release rather than a changed meaning,
because a fourth drawing kind is one more member of a vocabulary those phases had already fixed.
That is also why the phase's own record lives here rather than in a note at the end of the file:
a reader following Phase 11 in order should not have to learn from 11.8 onward that a kind is
missing.

**What the phase does.** An `<img>`'s decoded resource reaches a surface at the rectangle layout
gave its element. That is the whole claim, and the work is what a *resource* does to a module whose
four previous releases had only numbers in their records: a background is a colour, a border band is
a colour and a shape, a run is a pool of characters and a span -- and a picture is pixels that
belong to **another module's** object.

| Piece | Where |
|---|---|
| `PaintImage` -- the payload: a **retained** `GfxImage`, the resource's own `identity`, `width`, `height` and `format` copied at snapshot time, and the **source rectangle** in image pixels, with `PaintImageIsFullSource` to tell the full frame from a crop | `RoboPaintImage.h/.c` |
| `PaintImageFromResource`, the validity rule, equality **by identity**, the retain/release pair and `RoboPaintImageMaxDimension` | `RoboPaintImage.h/.c` |
| The `IMAGE` record (kind 14), the `image` field on an operation, and the emission -- inside its own element's box, after that box's own decoration and **before** its `CONTENT` | `RoboPaintTypes.h/.c`, `RoboPaintOperation.h/.c`, `RoboPaintContext.c` |
| The **colour-rule split**: `PaintOpKindUsesColor` beside `PaintOpKindDraws`, on the record vocabulary and on the command vocabulary | `RoboPaintTypes.h/.c`, `RoboPaintDisplayCommand.h/.c` |
| `DRAW_IMAGE` (command kind 11) and its `image` payload | `RoboPaintDisplayCommand.h/.c` |
| The builder's conversion -- the resource retained across the two containers -- and the traversal's `image_elements` / `images` / `image_skipped` | `RoboPaintDisplayListBuilder.c`, `RoboPaintContext.h/.c` |
| The pass's fourth drawing: `GfxRasterDrawImage` at the one application point the clip and the strength already use, plus `images` and the sixth partition term `no_resource` | `RoboPaintDecoration.h/.c` |
| `GfxRasterDrawImage` -- nearest-neighbour scaling, source alpha through the raster's own blend, the raster's own clip honoured | `RoboGraphicsRaster.h/.c` (graphics **0.17.0**) |
| The two containers' retain/release walks -- `append` retains, `clear`/`destroy` release, one static walk per file that both paths call | `RoboPaintList.c`, `RoboPaintDisplayList.c` |
| The record's and the command's writers printing `resource=`, `src=`, `dst=` and the resource's own size | `RoboPaintOperation.c`, `RoboPaintDisplayCommand.c` |
| The `image_painting` capability field | `RoboPaint.h/.c` |

**The resource is somebody else's, and this layer refuses to own a second copy of it.**
`GfxImage` already holds the pixels, the format and the refcount, so RoboPaint *retains*
rather than copies: the record borrows, and the paint list and the display list each hold a
reference of their own. `identity` is RoboGraphics' own id for the resource and it is what two
records are compared by -- **not the pointer** -- which is what keeps `PaintOperationEqual` a
question about a drawing rather than about the address it happens to live at.

**The record never names a document.** RoboHTML owns the `src`, the fetch seam and the failure
state; RoboPaint sees only the outcome that is paintable. So the missing-image policy is a **counted
skip**: five ways for an image to be absent -- no `src`, never resolved, a resource that was not
found, a refused scheme, a resource that did not decode -- each produce **no record** and one
`image_skipped`, while a zero-sized destination is a counted `empty` in the pass. The traversal's
`image_elements = images + image_skipped` is the identity that keeps "this document has no images"
apart from "this document's images did not draw", which is the number an author needs first.

**Destination and source are both statements, and only one of them can be derived.** The
destination is the box's **content box** -- the rectangle layout resolved for a replaced element,
read through layout's own doors and never recomputed here -- and the source is the **whole image**,
because nothing in this built runtime establishes a crop. Reducing either to the other would delete
a document fact: an image is not re-fitted to a box it was laid out in (`object-fit` is
unregistered), and the source rectangle exists as a field now so the artifact's shape can already
say "crop" the day a declaration produces one.

**Scaling and alpha are RoboGraphics', not this module's.** The pass hands the resource, the source
rectangle and the destination to `GfxRasterDrawImage` and reads nothing back: the sampler is
the graphics module's neatest-neighbour mapping, the alpha is the source's own through the blend the
raster already used, and the **raster's own clip** bounds the blit -- which is how 11.8's boundary
later bounds a picture without RoboPaint knowing that a boundary exists.

### What 11.7 found

1. **The colour rule had to be split rather than extended.** `PaintOpKindDraws` was the
   predicate the record validity rule was stated on -- a drawing carries a colour, a structural
   record carries `none` -- and every release since 11.1 had assumed it. Adding `IMAGE` to it (the
   obvious first move) demands a colour of a record whose appearance is a resource, so **every image
   in every document would have been refused** and the phase would have looked like it did nothing.
   The rule is now stated on a new positive list, `PaintOpKindUsesColor`, and the
   vocabulary case asserts the split (`uses_color` ⊆ `draws`, with `IMAGE` the first drawing kind
   outside it) rather than leaving it to the site that would have failed.
2. **A mem-debug baseline is not a leak test.** Measuring the live-block count before a rig and
   after its teardown reported a three-block difference in both directions depending on what the
   harness had already allocated -- exactly the shape of assertion that gets weakened until it
   passes. Running the whole cycle (build, paint, detach, destroy the records, paint again, tear
   down) **twice** and comparing the two ends measures the module's own ownership instead.
3. **Two containers needed the same release walk.** The paint list and the display list were both
   documented as arrays of pointer-free values; 11.7 makes a value name a resource, so both gained a
   retain at `append` and a release at `clear`/`destroy`. Writing the release twice is how one of
   them ends up without it, so each file has **one** static walk that both paths call -- a leak in a
   *failure* path is the kind that survives every green run.
4. **An image's placement does not generalise from a run's.** A text record is emitted a level
   *below* the box that owns it (a run is a child object); an image record is emitted **inside** its
   own element's box, before that box's `CONTENT` (an image *is* the element's content). The order
   case had to be written for the image's own group rather than borrowed from the text suite's, and
   `IMAGE` is the one row in the vocabulary table that **is** in paint order.
5. **A hand-built record needs boxes that are already valid.** The record rule refuses un-nested
   boxes *before* it ever reads a payload, so a case that built an operation with zeroed geometry
   would have passed every image assertion for the wrong reason; the hand-built case sets three
   nested rectangles and asserts `PaintBoxesIsValid` on them first.

### Verification

| Command | Result |
|---|---|
| `Build.ps1 -Configuration Debug -Verify -Test` | `VERIFY PASS` on every boundary scan; **2578 cases, 2578 passed, `Failed: 0`** |
| `Build.ps1 -Configuration Release -Verify -Test` | **2578/2578** |
| `Build.ps1 -Configuration Debug -Test -Sanitize` | ASan + UBSan **clean, no diagnostics** -- 2578/2578 |
| `--filter paint.image.` / `paint.` | **15/15** / **209/209** (203 module cases and the six `demo.paint.*` cases the substring filter also matches) |
| `robo check` | PASS, Chromium independence **CLEAN** (sources + headers, and the build wiring) |
| `robo test-paint` | PASS -- 2 steps, 0 failed |
| host visual checkpoint (`--demo-html --no-window`) | **15 checks, 0 failed -- `RESULT: PASS`**, unchanged: the demo document declares no `<img>` (`blocks=22 rects=50 lines=3 text_runs=19`), so this goal's picture evidence is the suite's own pixels and the demo layer's own image case rather than a new probe |

### Files

**Added:** `Include/RoboPaint/RoboPaintImage.h`, `Source/RoboPaint/RoboPaintImage.c`,
`paint/tests/SuiteRoboPaintImage.c` (15 cases), `paint/docs/Images.md`.
**Modified:** `RoboPaint.h`, `RoboPaintVersion.h`, `RoboPaint.c`, `RoboPaintTypes.h/.c`,
`RoboPaintOperation.h/.c`, `RoboPaintList.c`, `RoboPaintContext.h/.c`,
`RoboPaintDisplayCommand.h/.c`, `RoboPaintDisplayList.h/.c`, `RoboPaintDisplayListBuilder.c`,
`RoboPaintDecoration.h/.c`, `RoboGraphicsRaster.h/.c`, `RoboGraphicsVersion.h`, `Tests/Main.c`,
`Tools/Build.ps1`, the three existing paint suites and two graphics suites, `paint/README.md`,
`paint/docs/{Tests,Text,Decoration}.md`, `PROGRESS.md`, `ARCHITECTURE.md`,
`.cursor/rules/runtime-plan.mdc`.

## Goal 11.8 — Clipping

_Implemented. RoboPaint **0.7.0**, capability `clipping` = **1**; `raster` stays **0**._

11.8 turned the boundary in force from a **field** into a **scope**. Since 11.1 every record and
every display command has carried a `PaintClip` -- two axes, both modes, both intervals,
because `overflow-x: hidden` beside `overflow-y: visible` is a legal declaration one rectangle
cannot express -- and the decoration pass has bounded a drawing by it since 11.4. What was missing
was the statement of **where a boundary begins and ends**: a flat list carried the same two
intervals on every command and nowhere said that the box under command 12 is what narrowed them.

| Piece | Where |
|---|---|
| `PaintClipStack` -- a LIFO of whole clip records: create/destroy/reset/push/pop/current/depth/capacity/is_empty | `RoboPaintClip.h/.c` |
| The canonical **empty** interval: a disjoint intersection becomes `[max(start), max(start)]` rather than a record that runs backwards | `RoboPaintGeometry.c` (`paint_clip_intersect_axis`, both public doors) |
| `PUSH_CLIP`/`POP_CLIP` operation kinds (8 and 10; `TEXT` moves to 9) and predicates | `RoboPaintTypes.h/.c` |
| `PUSH_CLIP`/`POP_CLIP` display command kinds (5 and 7; `DRAW_TEXT` moves to 6) | `RoboPaintDisplayCommand.h/.c` |
| The scope emission -- one condition, after the box's `CONTENT`, closed before its `END_BOX` -- and `stats.clip_scopes` | `RoboPaintContext.c/.h` |
| The builder's scope stack and its **nine** refusals | `RoboPaintDisplayListBuilder.c` |
| The pass's stack, the scope-driven application and `clip_scopes`/`clip_depth_max` | `RoboPaintDecoration.c/.h` |
| The record dump naming the padded box an operation acts on (`padding=`), so a scope does not print as its border box | `RoboPaintOperation.c` |

The load-bearing decisions are that a scope is opened **only when the intersection changed the
boundary** (so `stats.clipped - stats.clip_scopes` is the number of scopes the traversal declined
to push), that a box's own background and border are painted **outside** the scope it establishes
(CSS clips the *contents*), that a pop carries the same clip its push carried so the pair is
self-identifying, and that an empty scope suppresses drawing **without** poisoning the stack. The
boundary is applied in **one place** for every drawing kind, so no painter carries a second copy of
the rule.

### What 11.8 found

1. **A containment test written backwards refuses exactly the documents it was written to
   accept.** The builder's "a scope is no wider than the boundary it is inside" check was spelled
   `narrow.start > outer.start || narrow.end < outer.end` -- the opposite predicate. An inset child
   (`[20, 200]` inside `[0, 200]`) satisfies `20 > 0`, so every document with a **nested** scope was
   refused as "wider than the boundary it is inside", while a document with one scope never reached
   the check and passed as green. The fix is eight characters, and the case that would have caught
   it immediately is a document with **two** scopes whose expectation is recomputed from the tree.
2. **A disjoint intersection has to be canonicalized, or the record type lies.** An excluded box
   produced `[200, 40]`, which `PaintClipIsValid` refuses -- so "nothing can be drawn
   here", the one cell a clip model most needs, was the one cell the model could not hold. It is
   now a valid zero-length interval at the upper start, stable under further intersection.
3. **`filled` counts fills *attempted*.** The empty-scope case asserted `filled == 0` and was
   wrong about the counter rather than about the pixels: the pass executes the fill, the raster
   bounds it out, the pixel stays transparent. The claim became the pixel and `clipped`, and the
   counter's own documentation now says what it measures.
4. **A no-clip case is weak if it only checks that the clip is inactive.** The visible half of the
   pixel showcase now asserts there is **no push and no pop** anywhere in the list, and that the
   list is exactly two commands shorter than the clipped one -- which is what "avoid unnecessary
   clip commands" actually means.

### Verification

| Command | Result |
|---|---|
| `Build.ps1 -Configuration Debug -Verify -Test` | `VERIFY PASS` on every boundary scan; **2501 cases, 2499 passed** |
| `Build.ps1 -Configuration Release -Test` | **2499/2501** |
| `Build.ps1 -Configuration Debug -Test -Sanitize` | ASan + UBSan **clean, no diagnostics** -- 2499/2501 |
| `--filter paint.` / `paint.clip.` / `layout.` / `css.` / `html.` | **140/140** / **24/24** / **346/346** / **616/616** / **386/386** |
| `robo check` | PASS, Chromium independence **CLEAN** (sources + headers, and the build wiring) |
| `robo test-paint` | PASS -- 2 steps, 0 failed |
| `robo test-qemu-display` | PASS -- `ROBO_QEMU_DISPLAY_OK` and the visual runtime markers on the final tree; the guest is otherwise unchanged (the demo document declares no `overflow`, so no scope is emitted there and no checkpoint counter moved) |

The two red cases in the Release, sanitizer and final Debug runs are `window.host.keyboard` and
`window.host.mouse`, and both fail on the *same* assertion:
`WindowRequestFocus(id) == RoboOk (7 != 0)` -- `RoboNotSupported` from the Win32 backend in
a session whose desktop foreground the test process does not own. It is the host-platform case 11.5's
report already records, in a module this diff does not touch, it moves between runs (the earlier
Debug runs of this same tree were green), and the mouse case fails with it because it injects input
through `SendInput` and shares the precondition. Nothing in the 140 `paint.*` cases is red.

**Note on the baseline (historical):** 11.8 was implemented on a tree in which **no Phase 11.7 image
painting existed** -- no `IMAGE` kind, no `DRAW_IMAGE` command, no image payload and no
`image_painting` field -- with 11.6 the last completed phase before it. That is still the tree this
section was written against, and nothing here was rewritten to pretend otherwise. What changed
afterwards is that **11.7 has since been filled in** (`## Goal 11.7 -- Image painting`, above), and
it is a *wiring* rather than a revision: this phase's vocabulary gained one row
(`PUSH_CLIP`/`POP_CLIP` did not move), the record and the command each gained one payload field
beside the one they already had, and the capability list gained one field at the end because the
list is positional. Two headers were corrected rather than left claiming a gap that no longer
exists: the clip suite's said the brief's "image drawing respects clipping" had **no subject** in
this build, and the hit-test suite's said the same of "image bounds". Both now say what is true --
the subject exists, the mechanism is shared rather than per-kind (a drawing is bounded and a region
is a border box, so an image is covered *by construction*), and the pixel case that would pin it is
**owed** in those suites rather than counted as covered. No case in either suite changed, and
`HitTest.md`'s deferral list was corrected with them.

### Files

**Added:** `Include/RoboPaint/RoboPaintClip.h`, `Source/RoboPaint/RoboPaintClip.c`,
`paint/tests/SuiteRoboPaintClip.c` (24 cases), `paint/docs/Clipping.md`.
**Modified:** `RoboPaint.h`, `RoboPaintVersion.h`, `RoboPaint.c`, `RoboPaintTypes.h/.c`,
`RoboPaintGeometry.h/.c`, `RoboPaintOperation.c`, `RoboPaintContext.h/.c`, `RoboPaintDecoration.h/.c`,
`RoboPaintDisplayCommand.h/.c`, `RoboPaintDisplayList.h/.c`, `RoboPaintDisplayListBuilder.c`,
`RoboPaintInternal.h`, `Tests/Main.c`, `Tools/Build.ps1`, the five existing paint suites,
`paint/README.md`, `paint/docs/{Architecture,Decoration,DisplayList,Operations,Tests}.md`,
`PROGRESS.md`, `ARCHITECTURE.md`, `DEPENDENCIES.md` and `.cursor/rules/runtime-plan.mdc`.

## Goal 11.9 — Opacity

_Implemented. RoboPaint **0.8.0**, capability `opacity` = **1**; `raster` stays **0**. RoboCSS
**0.18.0** -- the `opacity` property family is this goal's prerequisite and is registered by it._

11.9 gave a drawing a **strength**. Every release since 11.1 had a colour's alpha and nothing else:
`rgba(255, 0, 0, 0.5)` makes a *colour* half transparent, and an element's `opacity` makes a **box**
half transparent -- its background, its border, its text and every descendant it contains composite
at that strength while the colours they were declared with are untouched. The phase adds the second
factor, and it is deliberately **not** a compositor: the strength is multiplied per drawing as each
one is drawn, and the gap from CSS's group semantics is asserted rather than hidden.

| Piece | Where |
|---|---|
| The CSS value: the `[0, 1]` range, the clamp, the percentage conversion, the fixed-point text, and the initial value as a **door** | `RoboCSSOpacity.h/.c` |
| The computed-value kind `OPACITY` (15), its make door, its read door and its equality | `RoboCSSComputedValue.h/.c` |
| The registry row: `NON_INHERITED`, computation `OPACITY`, initial `"1"`, not a shorthand | `RoboCSSPropertyRegistry.c/.h` |
| `PaintOpacity` -- one real, the arithmetic, and the three doors that answer for a value they cannot read | `RoboPaintOpacity.h/.c` |
| `PaintOpacityStack` -- the same LIFO shape as the clip's, one field along | `RoboPaintOpacity.h/.c` |
| `PUSH_OPACITY`/`POP_OPACITY` operation kinds (12 and 13) and predicates | `RoboPaintTypes.h/.c` |
| `PUSH_OPACITY`/`POP_OPACITY` display command kinds (9 and 10) | `RoboPaintDisplayCommand.h/.c` |
| Where the scope is emitted -- **at the box's marker, before its first decoration** -- and `stats.opacity_declared`/`opacity_scopes` | `RoboPaintContext.c/.h` |
| The builder's opacity stack and its **eight** refusals | `RoboPaintDisplayListBuilder.c` |
| The pass's stack, the one application point (`GfxRasterSetOpacity`) and `opacity_scopes`/`opacity_depth_max`/`faded` | `RoboPaintDecoration.c/.h` |
| The strength in both dumps (`opacity=0.50`), written by each type's own `write` door | `RoboPaintOperation.c`, `RoboPaintDisplayCommand.c` |
| The declaration matrix grew three rows: a number, a percentage and a refusal | `SuiteRoboCssConformance.c` |

The load-bearing decisions are that **the push stands at the box's marker rather than at its
content boundary** (CSS paints a box's own background and border at the box's own opacity, and its
overflow clips only the box's *contents* -- so the two pairs nest differently around one box), that
a scope opens **only when the declaration changed the strength**, that **both halves of a pair carry
the value the pair establishes**, that the strength is on the **record** as well as in the scope (so
a consumer that ignores the two command kinds still draws every fading command correctly), and that
the strength is **passed in** to a text run rather than read from the run's own style.

### What 11.9 found

1. **A run that reads its own declaration applies it twice.** A text node's box is the element that
ows it, and that element has already opened the scope around it -- so a run reading its own
`opacity` would square the declaration it stands inside. The fix is that the traversal passes the
strength to the text emitter as a value, and the case that would catch a regression is a **count of
faded drawings** (three for nested `0.5`/`0.5` with a run inside, the third at `0.25` rather than
`0.125`) rather than a pixel: at those strengths the two answers are two pixels apart and a count is
unambiguous.
2. **The safe direction is two decisions, and they point opposite ways.** A record *constructed*
from a value nothing can read must be **opaque** -- a zeroed record that meant "invisible" would
make every element of every document that never mentions `opacity` disappear, silently, with correct
geometry and a blank page -- while a record *consumed* at the last gate before a pixel must answer
**no ink**, because the alternative turns a caller's arithmetic bug into a fully drawn document.
Both directions are asserted, and the asymmetry is documented where it lives rather than averaged
into one rule.
3. **A derived bound is not a bound until something measures it.** The dump's two line bounds are
header arithmetic, and this goal's `opacity=` field made the **display** line 13 characters wider
(362 -> 375) while the operation line already had the room. The suite builds the widest line each
kind can produce rather than trusting the derivation, which is the only reason the growth was seen.
4. **The sanitizer run found two defects outside this module.** Neither is a paint bug and neither was
reachable without ASan, which is why both are recorded: the HTML tokenizer suite allocated a
20000-byte buffer with the markup filling it exactly and passed it to `HtmlSourceString`,
which scans for a `NUL` -- so the terminator it found lived in ASan's redzone; and the tree-builder
suite asserted that a **second** `destroy` of the same builder is a no-op, which cannot be a contract
when the first call frees the struct the magic-word guard lives in. A one-byte allocation and a line
of prose; both are fixed and the whole suite is clean under ASan + UBSan.

### Suite

`paint/tests/SuiteRoboPaintOpacity.c` -- **28** cases under `paint.opacity.*`: the record and its
byte conversion, the stack and its growth, the default and `opacity: 1` (neither opens a scope), the
declaration, the scope's **placement**, nesting and the six-row parent/child table, sibling
isolation, zero, text, the display commands, a hand-built scope, the **eight** builder refusals and
the pass's own, three drawing kinds read back from a surface against
`GfxCompositeSrcOverOpacity`, a control that a document with no `opacity` is unchanged, the
group-deviation document (both answers through the graphics module's own blend), determinism,
mutation isolation, both dumps and the empty/malformed cases. `css/tests/SuiteRoboCssOpacity.c` adds
**8** under `css.opacity.*`, and the capstone paints nested opacity with three drawing kinds and
encodes the result as a **PNG** through RoboGraphics' own encoder, so the blend is inspectable rather
than merely asserted.

### Gates

| Gate | Result |
|---|---|
| `pwsh -File Tools/Build.ps1 -Configuration Debug -Test` | **2537/2537** |
| `-Configuration Release -Test -Sanitize` (ASan + UBSan) | **2537/2537**, no diagnostics |
| `--filter paint.` / `css.opacity.` / `css.conformance.` | **168/168** / **8/8** / **14/14** (the filter is a substring match, so the 168 is the ten paint suites' 162 plus the six `demo.paint.*` cases) |
| `robo check` | PASS -- Chromium independence **CLEAN**; the RoboPaint boundary source scan passes |

### Files

**Added:** `Include/RoboCSS/RoboCSSOpacity.h`, `Source/RoboCSS/RoboCSSOpacity.c`,
`css/tests/SuiteRoboCssOpacity.c` (8 cases), `css/docs/Opacity.md`, `Include/RoboPaint/RoboPaintOpacity.h`,
`Source/RoboPaint/RoboPaintOpacity.c`, `paint/tests/SuiteRoboPaintOpacity.c` (28 cases),
`paint/docs/Opacity.md`.
**Modified:** `RoboCSSComputedValue.h/.c`, `RoboCSSPropertyRegistry.h/.c`, `RoboCSSVersion.h`,
`RoboPaint.h`, `RoboPaintVersion.h`, `RoboPaint.c`, `RoboPaintTypes.h/.c`, `RoboPaintOperation.h/.c`,
`RoboPaintContext.h/.c`, `RoboPaintDecoration.h/.c`, `RoboPaintDisplayCommand.h/.c`,
`RoboPaintDisplayList.h`, `RoboPaintDisplayListBuilder.c`, `Tests/Main.c`, `Tools/Build.ps1`, the
three existing CSS suites, the three existing paint suites, `html/tests/SuiteRoboHTMLToken.c` and
`html/tests/SuiteRoboHTMLTreeBuilder.c` (the two sanitizer findings), `paint/README.md`,
`css/README.md`, `paint/docs/{Architecture,Border,Clipping,Decoration,DisplayList,Operations,Text,Tests}.md`,
`css/docs/{Opacity,Tests}.md`, `PROGRESS.md`, `ARCHITECTURE.md`, `DEPENDENCIES.md` and
`.cursor/rules/runtime-plan.mdc`.

## Goal 11.10 — Hit testing

_Implemented. RoboPaint **0.9.0**, capability `hit_testing` = **1**; `raster` stays **0**._

11.10 asks the painter's question **backwards**: given a point, which node is drawn there? Nothing
about the document changes to make it possible -- the geometry is RoboLayout's, the boundary in
force is 11.8's clip record, the strength in force is 11.9's opacity record, and the order is the
traversal's -- so the goal adds a **reader** of those conclusions rather than a second model of any
of them, and it lives in RoboPaint because a second walk with the painter's own shape is what makes
"the two agree about a document" an assertion instead of a coincidence.

| Piece | Where |
|---|---|
| The four outcomes (`NONE`, `ON_NODE`, `MISS`, `STALE`), the three flags, the result and its validity, the eight counters and their three identities | `RoboPaintHitTest.h/.c` |
| `PaintHitTest()` -- the reverse walk: subtree bound, boundary in force, children last-first, the box itself after them | `RoboPaintHitTest.c` |
| `PaintHitTestWrite()` -- the header line plus one line per examined object, with the five-member reason column | `RoboPaintHitTest.c` |
| The containment door both walks read (`PaintClipContains`) and the pixel it agrees with | `RoboPaintGeometry.h/.c` |
| The third style reader made shareable -- the traversal's opacity/colour/font readers moved behind `RoboPaintInternal.h` | `RoboPaintContext.c/.h`, `RoboPaintInternal.h` |
| The capability field, the version, and `RoboPaintDomainHitTest` (11, count 12) | `RoboPaint.h`, `RoboPaintVersion.h`, `RoboPaintTypes.h/.c`, `RoboPaint.c` |

The load-bearing decisions are that **the walk visits children last-first and tests the box after
them** (the node that covers a point is the last one *drawn* there, so a document-order walk answers
with the box underneath), that a box's **region is its border box** (the union of everything it could
paint into -- CSS hits a transparent `div` over all of it, while an **empty** rectangle contains no
point at all, not even its own corner), that the **boundary clips the contents and never the box** (a
`overflow: hidden` ancestor's child outside the boundary is not hit while that ancestor's own border
band still is -- 11.8's arrangement read backwards), and that a **strength does not gate** (`opacity:
0` is still hit, because CSS governs this with `pointer-events` and this build has none; the strength
travels in the result anyway so the goal that adds the property has the number).

### What 11.10 found

1. **A relatively positioned sibling does not end up above the one it flows after.** The visual
fixture's premise was written as "`.over`'s top is above `.under`'s" -- and it is not, because
`.over` flows **below** `.under`, so a `top: -60px` carries it from `under.bottom` up to
`under.top + 20`. The offset was applied; the assertion was wrong. The case now asserts the fact that
can *only* be true if the offset applied (`.over`'s final top is inside `.under`'s vertical band,
which normal flow alone can never produce) and **computes** the overlap as the two rectangles'
intersection rather than guessing at a point from the declarations -- the second failure was the same
mistake one layer along, since the guessed probe landed outside `.under` entirely.
2. **The containment rule is half-open, and a hit test must inherit it or invent a hole.** A box's
right and bottom edges are *outside* it while `right - 1` and `bottom - 1` are inside, and the same
rule makes an **empty** box contain nothing -- not even its own top-left corner. Both ends are
asserted, because a walk comparing `<=` on the edges would pass every interior case in the suite and
still report hits on boxes with no area.
3. **`visibility` is registered in RoboCSS and consumed by nobody.** No layer in this build reads it
-- RoboLayout builds the box and the painter paints it -- so there is no behaviour for a hit test to
respect, and reading it here would make the query refuse a point on something visibly drawn. The
suite records the limitation as a case (`visibility_is_not_consumed`) rather than papering over it,
so the goal that teaches layout and paint about `visibility` has a failing assertion pointing at the
place that must change.

### Suite

`paint/tests/SuiteRoboPaintHitTest.c` -- **26** cases under `paint.hit-test.*`: inside and outside,
the border box as the region, empty boxes, nested fallback, overlapping siblings and the
"overlap is by drawing" case that catches a walk which reversed the wrong thing, descendant over
ancestor, five cases on the boundary (including the one where the ancestor's own band stays
hittable), opacity that does not gate, `display: none`, the recorded `visibility` limitation, a text
run, safety, determinism, mutation isolation, the counter identities, the result model, the dump,
the agreement against the paint list, the whole chain end to end, and the **screen** case -- three
opaque boxes, two overlapping, painted into a real surface, every probe checked in both directions
(the answer's own fill, read from the paint list, **is** the pixel). No coordinate in the suite is a
literal: each comes from the painter's own `BEGIN_BOX`/`TEXT` record for the node in question.

### Gates

| Gate | Result |
|---|---|
| `pwsh -File Tools/Build.ps1 -Configuration Debug -Test` | **2563/2563** |
| `-Configuration Release` and `-Configuration Debug -Test -Sanitize` | build OK; **2563/2563**, no diagnostics |
| `--filter paint.` | **187/187** across the twelve suite files (plus the six `demo.paint.*` cases, since the filter is a substring match) |
| `robo check` | PASS -- Chromium independence **CLEAN**; the RoboPaint boundary source scan passes |

### Files

**Added:** `Include/RoboPaint/RoboPaintHitTest.h`, `Source/RoboPaint/RoboPaintHitTest.c`,
`paint/tests/SuiteRoboPaintHitTest.c` (26 cases), `paint/docs/HitTest.md`.
**Modified:** `RoboPaintGeometry.h/.c` (the `contains` door made shareable), `RoboPaintInternal.h`,
`RoboPaintContext.c/.h` (three style readers un-static'd), `RoboPaintTypes.h/.c`,
`RoboPaintOperation.h/.c` and the two existing paint fixtures (the new domain and kind row),
`RoboPaint.h`, `RoboPaint.c`, `RoboPaintVersion.h`, `Tests/Main.c`, `Tools/Build.ps1`,
`paint/tests/SuiteRoboPaint.c` and `paint/tests/SuiteRoboPaintConformance.c`,
`paint/README.md`, `paint/docs/{Architecture,Clipping,DisplayList,Operations,Tests}.md`,
`PROGRESS.md`, `ARCHITECTURE.md` and `.cursor/rules/runtime-plan.mdc`.

## Goal 11.11 — Paint invalidation

_Implemented. RoboPaint **0.11.0**, capability `paint_invalidation` = **1**; `raster` is now the only
field still **0**._

11.11 asks the question the ten releases before it raised and could not answer: given a picture this
module already produced, **which of its pixels are no longer the pixels the document describes?** The
answer is a set of rectangles rather than a mechanism, and that is the release's central decision --
11.1 recorded that a paint list is *not a cache of conclusions* but is regenerated from the tree on
every pass, and this goal crosses the line that record drew **without** turning the list into a
cache: the display list is still rebuilt whole, and the set says how little of it a caller may
re-execute. The input 11.1 left behind (`PaintStats.style_only`) is now the coarsest door in
the new module rather than a promise about a future one.

| Piece | Where |
|---|---|
| The region record (rectangle, node, reason, boundary, `narrowed`, `full`) and its validity rule | `RoboPaintInvalidation.h/.c` |
| The three vocabularies -- 11 reasons, 3 classes, 3 scopes -- and their names | `RoboPaintInvalidation.h/.c` |
| `classify_property` / `reason_for_property`: what a declaration costs **this build's picture** | `RoboPaintInvalidation.c` |
| The owned, growable, deterministic set: add, iterate, bound, compare, count | `RoboPaintInvalidation.c` |
| The ten producers: `add_rect`, `add_change`, `mark_operation`, `mark_node`, `mark_subtree`, `mark_node_change`, `mark_full`, `from_lists`, `from_display_lists`, `from_stats` | `RoboPaintInvalidation.c` |
| `PaintInvalidationSetWrite()` -- one line per region plus the summary the dump ends with | `RoboPaintInvalidation.c` |
| `RoboPaintDomainInvalidation` (12, count 13) and the capability field 11.1 declared and left 0 | `RoboPaintTypes.h/.c`, `RoboPaint.h`, `RoboPaint.c`, `RoboPaintVersion.h` |
| The suite: 29 cases built from **two states of one document**, two of them at the pixel | `paint/tests/SuiteRoboPaintInvalidation.c` |
| The module's own document | `paint/docs/Invalidation.md` |

Four load-bearing decisions.

**A region promises that a repaint of it is *sufficient*, never that those pixels changed.** Every door
widens when it is not certain and narrows only against a boundary it can **read**: a boundary naming
both axes narrows the rectangle and sets `narrowed`, an axis with no extent keeps it whole and counts
it `conservative`, and a boundary the module cannot read is treated the same way. The failure mode the
goal was written against is a **missing** rectangle -- a stale pixel nobody repaints -- and the
cheapest insurance against it is being allowed to be too large, which is also why a change the
producer cannot place becomes **one whole-extent** region rather than a guess.

**A move is two rectangles, and which doors union them is not an inconsistency.** `add_change` stores
the **union** (one region, because the caller vouched that both rectangles came from one thing) while
`mark_node_change` and the diff store **two** (because the two were drawn under boundaries that need
not be the same -- a box that moved out of an `overflow: hidden` ancestor). Collapsing the second
shape into one union narrowed by one of the two would repaint the middle of a move under a boundary
describing only one end of it, which is the mistake the phase exists to avoid.

**Only records that paint produce regions.** A marker, a content boundary or a scope draws nothing, so
a region out of its box would be a rectangle with nothing stale inside it -- and one per moved box
would say more about the traversal than about the document. The scopes are not lost by the rule
because every record **inside** a scope carries the scope's boundary in its own `clip` and its
strength in its own `opacity`, so a clip that moves and an element that fades reach the set through
the drawings they bound. The structural positions are still **counted** (`compared`, `matched`,
`differed`, `inserted`, `removed`), which is what leaves the pass counts as claims about the walk
rather than about regions.

**A parent's strength change invalidates the descendants' rectangles.** 11.9's accumulation is what
makes this free: the strength is carried on every record, so the diff finds the subtree without a
compositor and without a damage tree. `mark_subtree` is the explicit door for the same fact when the
caller has only a list and a box id.

### What 11.11 found

1. **One moved box produced six regions before the drawing rule.** The first walk compared every
position, so `BEGIN_BOX`, `BACKGROUND` and `END_BOX` each contributed an old and a new rectangle for
a box that draws one thing. Filtering on `PaintOpKindDraws` -- 11.2's own predicate, so a
future drawing kind is covered the day it is declared -- makes the answer two, and the rule had to be
*checked* against a moving clip and a fading element rather than argued about, because both reach the
set only through the records they bound.
2. **A box that grows a margin is not an insertion.** The case was first written as "the box grows a
bottom margin, so the records after it are inserted" -- which is wrong twice: the lists then have the
**same** length, so the difference is a `GEOMETRY` region and there is no insertion to find. A
genuine insertion needs records only one artifact has, and the cheapest honest way to make them is an
element that is `display: none` in one state and painted in the other, **last in the document** so the
tail is what differs. The same pair read backwards is the removal, which is why one fixture answers
both directions.
3. **A layout-space box and a region are 20 pixels apart.** The subtree case compared a region against
the *layout-space* boxes of a descendant and failed -- not because the region was wrong, but because
the two spaces are not interchangeable (11.1): a region is a **paint** rectangle. The suite already
had the door that reads the other space (`inval_box_rect`, a record's own boxes) and now uses it in
the geometry comparisons, and the case grew a bottom-right probe so a corner-only assertion could not
pass with the wrong space and the wrong box.
4. **Three properties RoboLayout calls paint-visible are drawn by nobody, and saying `PAINT` about
them would repaint an identical picture.** `visibility`, `text-decoration` and `background-image`
answer `NONE` from `classify_property`, and the case that pins it
(`paint.invalidation.unconsumed_properties`) is where the goal that consumes one of them finds out it
has to add a row to the table. The completion criterion "no-op changes do not unnecessarily require
repaint" is the reason the class vocabulary has three members and not two.
5. **A set outliving the artifacts it came from is the property that makes the module usable at all.**
Its whole purpose is to be read *after* the thing it describes has changed, so the determinism case
builds the same transition in two independent trees, compares the sets and their dumps, then destroys
the trees and re-reads the set: a statement no address, allocation order or clock can satisfy by
accident.

### Suite

`paint/tests/SuiteRoboPaintInvalidation.c` -- **29** cases under `paint.invalidation.*`, and every one
is about **two states of one document**: two trees built from one HTML with two sheets and painted
through the real chain, or one document plus a mark naming a box in it. A hand-built list would prove
the walk reads a struct rather than a document, so there is not one. The vocabularies and the
classifier, the set's own arithmetic (narrowed / conservative / zero-area / union), the no-op, five
change reasons read out of the artifacts' own records, an insertion and the same pair as a removal,
seven door cases, the dump, ownership measured by two cycles, and two pixel cases: every differing
pixel inside a region, and **clearing the regions of the first frame and painting the second into it
reproduces a full render byte for byte**.

### Gates

| Gate | Result |
|---|---|
| `pwsh -File Tools/Build.ps1 -Configuration Debug -Test` | **2607/2607** |
| `-Configuration Release` and `-Configuration Debug -Test -Sanitize` | build OK; **2607/2607**, no diagnostics |
| `--filter paint.` | **238/238** across the fourteen suite files (plus the six `demo.paint.*` cases, since the filter is a substring match) |
| `robo check` | PASS -- Chromium independence **CLEAN**; the RoboPaint boundary source scan passes |

### Files

**Added:** `Include/RoboPaint/RoboPaintInvalidation.h`, `Source/RoboPaint/RoboPaintInvalidation.c`,
`paint/tests/SuiteRoboPaintInvalidation.c` (29 cases), `paint/docs/Invalidation.md`.
**Modified:** `RoboPaintTypes.h/.c` (the invalidation domain), `RoboPaint.h` (the capability field
11.1 declared), `RoboPaint.c`, `RoboPaintContext.h` (the 11.1 note on what it left for this goal),
`RoboPaintVersion.h`, `Tests/Main.c`, `Tools/Build.ps1`,
`paint/tests/SuiteRoboPaint.c` and `paint/tests/SuiteRoboPaintConformance.c` (version, domain count,
capability row), `paint/README.md`, `paint/docs/{Architecture,Tests}.md`, `PROGRESS.md`,
`ARCHITECTURE.md` and `.cursor/rules/runtime-plan.mdc`.

## Goal 11.12 — Rasterization

_Implemented. A new module, **RoboRender 0.1.0**, `RoboRenderRasterizationImplemented=1`; RoboPaint
**0.12.0** with `raster` flipped to **1** — the last field 11.1 declared and left at 0. The module has
no lifecycle: it holds no global state, so there is no `init`/`shutdown` pair to write._

11.12 is the release that spends the answers every module below it produced. RoboLayout says where
every box is, RoboPaint says what to draw in what order under which boundary and at which strength,
11.11 says which pixels went stale — and all three of those are *data*. This module makes a **frame**
out of them: an owned pixel target, one run over the whole command vocabulary, an execution record, and
byte-identical output for identical input. The module boundary is the phase's first decision and it is
stated in three headers, because getting it wrong in either direction produces a worse build: RoboPaint
owns what the list **means** (the walk, the two scope stacks, the per-drawing boundary and strength, the
buckets that say why a drawing did not land) and RoboRender owns what a **frame** is. So 11.12 added
exactly one door below it — `PaintDecorationPaintBounded`, the same walk with the raster's clip
narrowed by a rectangle — and no second walk exists to disagree with the first.

| Piece | Where |
|---|---|
| The owned pixel target: 15 doors, RGBA8, `RoboRenderMaxDimension`, `bytes`, `checksum`, `equal` | `RoboRenderRasterizer.h/.c` |
| The frame record: the pass's record **by value** plus `regions`/`limited`/`region`/`region_pixels`/`target_pixels`/`checksum` | `RoboRenderRasterizer.h/.c` |
| `RenderRasterize` — the whole frame | `RoboRenderRasterizer.c` |
| `RenderRasterizeRegions` — the same list bounded by an invalidation set (NULL/empty = success, nothing done) | `RoboRenderRasterizer.c` |
| The preflight: one pass over values, all-or-nothing, `RoboNotSupported` with a `LIST` diagnostic | `RoboRenderRasterizer.c` |
| `RenderFrameWrite` — seven lines, no address in them | `RoboRenderFrameDump.c` |
| The capability table: sixteen flags, ten 1s and six 0s, each 0 naming its phase | `RoboRender.h`, `RoboRender.c` |
| `PaintDecorationPaintBounded` + the `bounded_out` counter | `RoboPaintDecoration.h/.c` |
| RoboPaint **0.12.0**: `raster` = 1, and the prose that said otherwise | `RoboPaintVersion.h`, `RoboPaint.h`, `RoboPaint.c` |
| The suites: 24 cases in three files, plus the fixture header | `render/tests/` |
| The module's own documents | `render/README.md`, `render/docs/{Rasterization,Tests}.md` |

Four load-bearing decisions, and each has a version that looks right.

**The bound is a *clip*, not a *filter*.** A region run executes **every** command and narrows the
raster's clip to the region; it does not skip the commands whose rectangle misses it. Skipping looks
like the same picture until a text run is involved: a run's rectangle is its **advance** box, so a
glyph's ink can land inside a region its rectangle never reaches. Bounding with the clip needs no
per-kind proof and is what makes a region run equal to a full frame *inside* the region — which
`render.scene.region_matches_full` asserts byte for byte rather than argues.

**One run per set, over the set's own bounds.** A set can name **overlapping** regions, and running the
list once per region would composite a semi-transparent drawing **twice** where two overlap — two
half-transparent fills over one pixel do not make one fill, and no counter would catch it. So the run
executes once, over `PaintInvalidationSetBounds`, which 11.11 documents as *sufficient* (it can
only be too large). Merging into disjoint rectangles is the optimisation it is, not a claim.

**All-or-nothing, therefore a preflight.** A run walks the list once before the first pixel: an unknown
command kind, a pop that closes nothing, a scope left open at the end — each is refused and the target
keeps exactly the bytes it had. Two properties follow: an unsupported command is never silently
reinterpreted (the brief's §5 as behaviour), and a run that returns `RoboOk` drew the whole list it was
given, which is what makes the region equality checkable at all. The preflight deliberately does *not*
re-derive the display list builder's document rules: that would be a second authority on what a list
may contain.

**Clearing is the caller's.** A run never clears, because a region run *must* not: clearing would erase
the pixels its regions were computed to preserve. The frame contract is clear-once-then-execute, and
conflating the two would make the correct partial repaint unexpressible.

### What 11.12 found

1. **`position` is parsed by RoboCSS and laid out by nobody.** The scene's first draft placed an
"overlay" with `position: absolute; left: 110px; top: 10px` and asserted a pixel there. Every box in
this build is in **normal flow**: the declaration was accepted and ignored and the box landed 100 pixels
lower. The scene's document comment now states its flow geometry as numbers and says why — a later
layout phase that starts honouring `left`/`top` fails a named assertion instead of quietly moving the
picture. It is a Phase 10 boundary, found by a Phase 11 test, and it is the reason
`render/docs/Tests.md` §4 exists.
2. **A glyph is an outline, so a pixel in the middle of it is background.** The no-face case asserted
that the pixel at (8,8) had changed — the centre of a ten-pixel square at `font-size: 100px`. The glyph
*was* drawn (the checksum moved) and the pixel was untouched: the stroke is the ink. Hence
`render_region_has_ink_is`, and the rule that a text claim is a claim about a region.
3. **`0x` appears inside `200x200`.** The "no address in the dump" check was written as
`strstr(text, "0x") == NULL`: it passed at 16x8 and failed at 200x200 because it was testing the
dimensions string. It is now `=0x`, which is what a printed pointer looks like.
4. **A clip display command must carry an *active* boundary to be appendable.** The preflight case builds
an unbalanced list by hand, and its first draft was refused by the **list's own** append rather than
reaching the preflight — 11.8's validity rule refuses a scope command whose clip bounds neither axis.
The case now states the boundary the record must carry, which is also the fact that makes the rule
checkable: the *list* guarantees each record is a command, and the *rasteriser* is what guarantees a
sequence of them is a frame.
5. **`-Verify` had never been run, and it caught two things.** Every build in this phase's development
used `-Test` (which builds and runs the suites) and `robo check` (which scans all runtime sources for
Chromium), so `Assert-RoboRenderBoundaries` — the function 11.12 added for the new module — did not
run until the gate list was executed in full. It flagged, first, the module's own
`#include "RoboLayout/RoboLayoutGeometry.h"`: a direct document-layer include in the layer whose input
contract is a display list, fixed by taking the rectangle through **RoboPaint's** geometry re-export
(the type is RoboGraphics' canonical `GfxRect` either way). It then flagged the *fixture*, which
includes RoboHTML because a render fixture **is** a paint fixture plus a target and must build a
document to have a list to rasterise — so the rule was split, the paint scan's own asymmetry one layer
down: the module is held to **both** halves (no document layer, no higher layer, no Chromium) and the
suites and documents to the **upward** half only, because an upward include in a suite is the leak that
would happen the day a suite reached for a window to look at its own frame.

### Suite

`render/tests/` — three suite files, **24** cases. `SuiteRoboRender.c` (9: the module, the target, the
record, the dump — including the capability table asserted **by name**, so a phase that flips one has to
change this case deliberately), `SuiteRoboRenderRaster.c` (10: an empty frame, argument rules, a
filled rectangle's pixels, alpha and nested opacity, clipping and bounds, off-surface geometry, byte
determinism, the two preflight refusals, the no-face case, and region clamping) and
`SuiteRoboRenderScene.c` (5: the visual checkpoint, the region-equals-full capstone, nothing-owed,
snapshot isolation, and the dump of a real frame). No golden image and no reference bitmap: the verdict
is the pixels asserted, the PNG written to `Out/RenderScene.png` is **evidence**, and a read-only tree
prints a note rather than failing a suite whose pixels are perfectly good.

### Gates

| Gate | Result |
|---|---|
| `pwsh -File Tools/Build.ps1 -Configuration Debug -Test` | **2629/2631** — the two failures are `window.host.keyboard`/`mouse`, which fail on `SetForegroundWindow` being declined (a Windows foreground-steal refusal, pre-existing, environment-dependent and untouched by this phase) |
| `-Configuration Debug -Verify` | **VERIFY PASS** — `RoboRender boundary source scan: OK`, and every other module's scan unchanged |
| `-Configuration Release`, and `-Configuration Debug -Test -Sanitize` | build OK; the same 2629/2631, no sanitizer diagnostics |
| `--filter render.` (Debug, Release, sanitize) | **24/24** each time; `--filter paint.` unchanged at **238/238** |
| `robo check` | PASS — Chromium independence **CLEAN** with the new module present; `Assert-RoboRenderBoundaries` passes |

### Files

**Added:** `Include/RoboRender/{RoboRender,RoboRenderVersion,RoboRenderDiagnostics,RoboRenderRasterizer}.h`,
`Source/RoboRender/{RoboRender,RoboRenderDiagnostics,RoboRenderRasterizer,RoboRenderFrameDump}.c`,
`render/tests/{RoboRenderTestSupport.h,SuiteRoboRender.c,SuiteRoboRenderRaster.c,SuiteRoboRenderScene.c}`,
`render/README.md`, `render/docs/{Rasterization,Tests}.md`.
**Modified:** `RoboPaintDecoration.h/.c` (the bounded door and its counter), `RoboPaintVersion.h`,
`RoboPaint.h`, `RoboPaint.c`, `paint/tests/SuiteRoboPaint.c` and `SuiteRoboPaintConformance.c` (version
and the `raster` capability row), `Tools/Build.ps1` (sources, suites, boundary scan),
`Tests/Main.c`, `paint/README.md` and the paint documents that named `raster` as 0, `PROGRESS.md`,
`ARCHITECTURE.md`, `DEPENDENCIES.md` and `.cursor/rules/runtime-plan.mdc`.

## Goal 11.13 — Framebuffer presentation and the desktop shell

_Implemented, in two halves that meet at one door. A new module, **RoboDesktop 0.1.0**,
`RoboDesktopShellImplemented=1` and `RoboDesktopDocumentsImplemented=1`, with six suite files over
**41** cases; **RoboRender 0.2.0** with `presentation` flipped to **1** — the sixth 0 11.12's own table
declared — and seven new cases of its own. Nothing below this phase changed except one comment that was
wrong about a rule nothing enforced._

11.13 is the release that gives the pipeline somewhere to *arrive*. Every module below it answers what a
document looks like; this one answers **what is on the display**, and it does it in two pieces that are
deliberately different sizes:

- **RoboRender's presenter** is the small half: a frame composited into a destination surface at an
offset, with a record whose two pixel counts sum to the frame's own. It is a *call*, not a system — no
queue, no pacing, no second buffer — and it is the door 11.12's release note named and left at 0;
- **RoboDesktop** is the large half: a native shell that owns a screen target, paints a desktop surface,
a panel and a work area, holds shell windows with real decorations, rasterises their contents from
display lists and composites them under the chrome, describes each frame with a record and a checksum,
and presents it into a window's render surface or any writable surface a caller names.

The boundary between them is the phase's first decision and it is a *type* decision: the presenter takes
a **surface**, never a window id and never a display framebuffer, because a rasteriser that could reach
RoboWindow could not be tested without one and would have to be re-taught what a QEMU mapping is. A frame
is pixels; where they land is the caller's choice, and the two callers this build has — the desktop's
window path and the guest's wrapped framebuffer — reach the same door with the surface their own backend
gave them.

| Piece | Where |
|---|---|
| The presenter: `present_region`, `present`, `present_framebuffer`, the record, the four-line dump | `RoboRender/RoboRenderPresenter.h/.c` |
| RoboRender **0.2.0**: `presentation` = 1, and the prose that said otherwise | `RoboRenderVersion.h`, `RoboRender.h`, `render/README.md` |
| The shell: lifecycle, the frame triple, resize, `sync_size`, the presentation doors, hit testing and the pointer, the statistics | `RoboDesktop/RoboDesktop.c` |
| The geometry: metrics defaults, `compute`, `panel_area`, `place`, `contain` | `RoboDesktopGeometry.h/.c` |
| The window list: add/remove, bounds, title, contents, z-order, focus, the close request | `RoboDesktopWindow.h/.c` |
| The chrome: the surface, the panel, its labels, decorations, the close button, a title bar's text | `RoboDesktopChrome.c` |
| The markup bridge — the **one** file allowed to name a document layer | `RoboDesktopDocument.h/.c` |
| The dump (seven lines, no address), the diagnostics and the vocabularies, the theme | `RoboDesktopDump.c`, `RoboDesktop{Diagnostics,Types,Theme,Text}.c` |
| The capability table: eighteen flags, eight presences and ten absences | `RoboDesktop.h`, `RoboDesktopTypes.h` |
| The suites: 41 cases in six files, plus the fixture header | `desktop/tests/` |
| The module's own documents | `desktop/README.md`, `desktop/docs/{Architecture,Geometry,Windows,Frames,Documents,Tests}.md` |
| The presenter's document | `render/docs/Presentation.md` |

Seven decision-bearing rules, and each has a version that looks right.

**Clip, not refusal — and the counts are exhaustive.** A frame that does not fit its destination is
clipped, the way every drawing in this tree is clipped, because a window that shrank between the
rasterise and the present must show the part of the picture that still fits. What would not be acceptable
is silence about it, so `presented_pixels + clipped_pixels` sums to the frame's own count and
`RenderPresentRecordIsValid` refuses a record where they do not — including one that claims
`complete` for a frame that lost pixels.

**One authority for where a pixel lands.** `RenderPresent` computes the destination rectangle once
(through `present_region`) and then hands the frame's **own** bounds to the graphics composite rather
than the intersection it just computed: the call that writes the pixels applies the clip, and the
intersection is used only for the *record*. A presenter that pre-clipped its source rectangle would be
re-deriving the composite's rule, and the two would drift the first time a destination had a stride, a
format or a bounds rule this file did not think of.

**A frame does not present itself.** `begin_frame`/`paint`/`end_frame` is RoboWindow's own triple, and
`DesktopRender` is the shorthand — which `desktop.frame.sequence` asserts is **equal** to the
sequence it abbreviates, because a shorthand that could drift is a second behaviour. Separating the
presentation door is what lets a caller render a frame and look at it with no display at all
(a test, a screenshot, a headless build) and what lets one frame be presented twice or into two
destinations.

**A document cannot break the shell, and a broken document leaves a window.** The shell paints with
graphics fills and never reads a page; a window's contents are a display list rasterised into the
window's **own** target and composited under the chrome, so a `push-clip` inside a document cannot reach
the panel and a document that fails to draw still leaves a body, a title bar and a close button on
screen — with the renderer's own message travelling out in a `WINDOW` diagnostic while the rest of the
frame is still painted. The one file allowed to know about markup is `RoboDesktopDocument.c`, and the
boundary scan enforces the exemption **by name** so it is visible in the build's output.

**`documents == 0` beside `RoboDesktopDocumentsImplemented == 1`.** The two are not in conflict and
the distinction is the module's centre: the shell builds no document, and the runtime can build one for
an application window. A build that removed the bridge would keep a working shell and set the constant to
0; a build that let the shell read markup would have to change the scan.

**The list is the z-order, focus follows the raise, and closing is a request.** Eight slots, raising is
one `memmove` to the end, and a press on a title bar raises *and* focuses, so there is no second focus
model to drift from the first; removal repairs the focused index rather than letting it dangle. A window
below its decoration minimum is **grown** (the minimum is a property of decorations the caller does not
control) while a window below *its* constraints is refused one module down (those constraints are the
caller's policy) — two opposite rules, each right for its own subject, stated together where they
diverge. `request_close` marks a window `CLOSING` and leaves it in the list: the owner destroys it, which
is what lets an application decide what a close means.

**Geometry is a value; "drawable" is a different door.** `compute` is total — every rectangle inside the
screen by construction, the work area thin rather than negative on a screen that barely holds a panel —
and the question "does a window fit" is answered at `DesktopWindowAdd`, which refuses a work area
thinner than `RoboDesktopMinDimension`. Hit testing walks **front to back** (a point over two windows
belongs to the front one, the answer a user would give) and reports a five-member area vocabulary rather
than a boolean, because that vocabulary is what an application will eventually be handed.

**Determinism, stated as bytes.** Nothing in a frame reads a clock (the status text is the caller's
value), the z-order is an array's order, the dump carries **no address**, and the target has a byte view,
a checksum and a byte equality — which is how `desktop.frame.determinism` can compare two *independently
created* desktops, each with a window built from the same markup, byte for byte, with their records and
checksums beside them.

### What 11.13 found

1. **A constant nothing read, and a comment describing a rule the code did not implement.** Writing the
   geometry document surfaced both: `RoboDesktopMinDimension` was read by nothing, and
   `RoboDesktopGeometry.h` claimed that a screen below the panel's height plus that constant "leaves an
   empty work area" while `compute` — deliberately total — returns whatever the panel leaves. The
   constant now decides the one door where "fits" is a question (`DesktopWindowAdd`, which
   previously refused only an *empty* work area, so a one-pixel work area would have had a window placed
   in it), the comment states what the code guarantees, and `desktop.window.no_work_area` pins the rule
   from the failing **and** the passing side — a rule asserted only where it refuses is a rule that could
   be a different rule.
2. **Arithmetic in a comment can be wrong about its own fields.** The metrics reasoning said a window's
   minimum is "42" at these metrics; the two minima are computed from the decoration fields and come to
   **10 by 34**. The case asserts the arithmetic rather than the numbers (which is what makes a restyle
   safe), and the prose now states what the arithmetic yields.
3. **A capability table's own prose drifted from its fields.** The conformance suite's header called the
   table sixteen fields with nine absences; the struct has eighteen with ten. The assertion was never
   the prose (the case reads every field by name), but a reader trusts the prose first — so the counts in
   the document are stated from the struct rather than remembered.
4. **A counter whose scope is unstated is worse than no counter.** `stats.presentations` counts the
   **window** path — the one with a backend behind it — and a `present_to_surface` copy is a caller's own
   handoff described by the record it returns. That distinction had to be written down: "presentations"
   with no stated scope invites the reading that a headless build presented nothing.
5. **The `-Verify` gate is the one that catches boundary drift, and 11.12's lesson still holds.** The
   phase's new scan (`Assert-RoboDesktopBoundaries`) refuses Chromium, a document layer in the shell (with
   `RoboDesktopDocument` exempt by name) and any higher layer, and it passes — but only because the gate
   list is run in full rather than `-Test` alone.
6. **The two host window cases remain environment-dependent.** `window.host.keyboard`/`mouse` need the
   process to be granted foreground focus; one run of this build reported 2679/2679 and another reported
   the same two failures, with every `desktop.` case at 41/41 each time. It is pre-existing, it is not
   this phase's, and it is recorded so a future run's number is not read as a regression.

### Suite

`desktop/tests/` — six suite files, **41** cases: `SuiteRoboDesktop.c` (9: the version and the eighteen
capability flags read by name, the two name vocabularies, the diagnostic record, the configuration
defaults, the lifetime, the record's closed validity rule, the seven-line dump), `SuiteRoboDesktopGeometry.c`
(7: metrics, the four rectangles, a small screen, the three panel areas, placement, containment, and a
resize following the screen), `SuiteRoboDesktopWindow.c` (9: defaults, the decoration minimum, the
work-area rule from both sides, capacity, title and contents, raise/focus, close and destroy, the six hit
areas, the pointer's two actions), `SuiteRoboDesktopFrame.c` (8: the sequence and its refusals, the
shell's pixels, a window's pixels, real contents, the label pair both ways, byte-for-byte determinism,
resize, and the headless contract), `SuiteRoboDesktopDocument.c` (5: the report, the refusals, markup
without a sheet, with and without a face, and a build/destroy cycle returning the allocator to its
baseline) and `SuiteRoboDesktopPresent.c` (3: a copy with a record, a desktop with no window, and the
end-to-end capstone).

The render side gained **7**: `render.conformance.presenter` (the release shape, asserted rather than
exercised, including that `presentation` is 1 **while `compositor` is 0**), `render.present_region`,
`render.present_pixels`, `render.present_clipping`, `render.present_framebuffer`,
`render.present_record_and_dump` and `render.present_refusals`.

The visual checkpoint is `Out/Desktop.png` (or `$ROBO_DESKTOP_PNG`): the shell with a panel, an identity
mark, and a window whose contents came from real markup, written by the end-to-end case as **evidence**
rather than as a verdict — the verdict is the pixels that case asserts.

### Gates

| Gate | Result |
|---|---|
| `pwsh -File Tools/Build.ps1 -Configuration Debug -Verify -Test` | **VERIFY PASS** — `RoboDesktop boundary source scan: OK`, `RoboRender` unchanged — and **2679/2679** tests passed |
| `-Configuration Release -Test` | build OK, **2679/2679**, no failures |
| `-Configuration Debug -Sanitize -Test` | **SANITIZE PASS**, **2679/2679**, no diagnostics |
| `--filter desktop.` | **41/41** (Debug, and 41/41 under the sanitizer) |
| `--filter render.` / `--filter render.present_` | **31/31** / **6/6** |
| `robo check` | PASS — Chromium independence **CLEAN** for runtime sources and build wiring with the new module present |

### Files

**Added:** `Include/RoboDesktop/{RoboDesktop,RoboDesktopDiagnostics,RoboDesktopDocument,RoboDesktopGeometry,
RoboDesktopTheme,RoboDesktopTypes,RoboDesktopVersion,RoboDesktopWindow}.h`,
`Source/RoboDesktop/{RoboDesktop,RoboDesktopChrome,RoboDesktopDiagnostics,RoboDesktopDocument,RoboDesktopDump,
RoboDesktopGeometry,RoboDesktopInternal.h,RoboDesktopText,RoboDesktopTheme,RoboDesktopTypes,
RoboDesktopWindow}.c`, `Include/RoboRender/RoboRenderPresenter.h`, `Source/RoboRender/RoboRenderPresenter.c`,
`desktop/tests/{RoboDesktopTestSupport.h,SuiteRoboDesktop,SuiteRoboDesktopDocument,SuiteRoboDesktopFrame,
SuiteRoboDesktopGeometry,SuiteRoboDesktopPresent,SuiteRoboDesktopWindow}.c`,
`desktop/README.md`, `desktop/docs/{Architecture,Documents,Frames,Geometry,Tests,Windows}.md`,
`render/docs/Presentation.md`.
The module also emits its own **debug output** at the six points a developer debugs from — shell
create/release, window add, close request and destroy, a resize that actually changed the screen, a
document build (with the five stage counts, so a blank window names the stage that produced nothing),
and the two failures that would otherwise be an empty rectangle (`document refused`, `window contents
could not be drawn`, the latter warned per frame because a broken document is a *state* the caller is in)
— under the same rules the dump keeps: no address, no clock, nothing that is not this shell's own state.
A paint and a presentation are deliberately **not** logged, because a driven shell would fill the output.
`desktop/docs/Architecture.md` §6 lists every line.

**Modified:** `RoboRender.h`/`RoboRenderRasterizer.h`/`RoboRenderVersion.h`/`RoboRender.c` (the
twelfth capability row, version 0.2.0, the presenter's doors), `RoboDesktopTypes.h` and
`RoboDesktopGeometry.c` (the constants and arithmetic the phase's documents made honest),
`RoboDesktopWindow.c` (the work-area rule), `Tools/Build.ps1` (sources, suites, the new boundary scan),
`Tests/Main.c`, `render/tests/{SuiteRoboRender,SuiteRoboRenderRaster}.c`, `render/README.md`,
`render/docs/{Rasterization,Tests}.md`, `paint/README.md` and the paint documents that named 11.13 as
future, `PROGRESS.md`, `ARCHITECTURE.md`, `DEPENDENCIES.md` and `.cursor/rules/runtime-plan.mdc`.

## The desktop on the guest display — 11.13's other half

_Implemented. `user/robo-runtime/main.c` is now the desktop rather than the demo page, the demo layer is
gone from the tree, and the guest image is built from the module sources it needs instead of a list kept
beside them. Two kernel facts had to be fixed for the picture to be complete, and they are recorded below
rather than worked around._

11.13 landed the shell and the host launcher, and left the device path where it had been: the OS still
booted to the **deleted demo layer's** page. `scripts/robo run-runtime-qemu` therefore showed a throwaway
document while `run-runtime` showed the shell — two pictures, one of them from code that no longer
existed in the source tree but was still compiled into the image.

That is what this change is. The guest program drives the **same scene** the host launcher drives
(`RoboDesktopScene`, added here), so the two backends show one definition of the checkpoint; the demo
layer is deleted outright (sources, headers, asset, suites, host `--demo-html` route, pack entries and its
markers); and the pipeline's display gate now asserts the desktop's own counters rather than the demo's
HTML counts.

| Piece | Where |
|---|---|
| The scene, shared by both launchers | `Include/RoboDesktop/RoboDesktopScene.h`, `Source/RoboDesktop/RoboDesktopScene.c` |
| The host launcher (`RoboTest.host.exe --desktop`) | `Source/RoboDesktop/platform/host/RoboDesktopHost.{h,c}` |
| The guest program — the OS's desktop, on the framebuffer | `user/robo-runtime/main.c` |
| The face the text is drawn with, and its licence | `desktop/assets/Inter.ttf`, `desktop/assets/Inter-License.txt` |
| The scene's suite (`desktop.scene.*`, 5 cases) | `desktop/tests/SuiteRoboDesktopScene.c` |
| The guest's build rule: modules, not a file list | `scripts/roboboot-build-common.ps1` (`RuntimeModules`) |
| The guest path's document | `docs/userspace/desktop-runtime.md` |

**One scene, two launchers, and the counters agree.** A launcher is the one part that *must* differ (one
registers the QEMU window backend, the other Win32), so the scene is defined once and each launcher
contributes only its backend, its lifetime and its reporting. The host launcher prints its offscreen frame
and the guest prints its own, and they are the same numbers: `fills=12 labels=4 unshaped=0 windows=1
content_runs=1 content_pixels=652912`. The display gate pins the guest's, with that equality written down
beside them — a guest number that drifted from the host's would mean the launchers had stopped drawing the
desktop.

**The face is what makes the labels text.** The shell's chrome and the document's text need a real face,
and the image had none: the demo layer carried a throwaway bitmap atlas, and it went with the layer. So the
runtime vendors one (`desktop/assets/Inter.ttf`, OFL, with its licence beside it), the image packs it at
`/System/Fonts/Inter.ttf`, and the guest builds a `FontFace` from those bytes. Without it the
desktop still paints — panel, chrome, document fills — and every label lands in `labels_unshaped` instead,
which is why the gate asserts `UNSHAPED=0`. Loading reports the step it stopped at
(`ROBO_QEMU_FACE_FAIL <stage> <path>`), because "the file is missing" and "the file will not parse" are
different defects and a single failure marker conflated them.

**Discovered sources, because the copied list drifted.** The guest build used to name every runtime source
by hand, and that list went on compiling the deleted demo layer after the host had a real shell. It now
names **modules** and compiles every `.c` under each one — minus `tests` and the platform backends of the
platforms a guest is not (`win32`, `host`; the guest's own is `platform/qemu`). What the walk yields is
exactly what `Tools/Build.ps1` names file by file for those modules. New modules reached the guest for the
first time as a result: RoboContainers, **RoboI18N** (RoboFont looks a face's names up through its locale
model), RoboFont, RoboCSS, RoboLayout, RoboPaint, RoboRender and RoboDesktop.

### What the guest exposed, one level down

The guest ELF is 6.28 MB, and the boot image grew to 14.3 MB. Two kernel limits that had been latent became
blocking, and neither was worked around — the picture is *complete* because both were fixed:

1. **The root volume was a constant 512 blocks** (2 MiB). The import walk stopped partway through the
   image, so every entry after the stopping point — including `/System/Fonts/Inter.ttf` — was simply absent
   from the filesystem, with one `VFS_IMPORT_FAIL -20` line printed and nothing to make a reader act on it.
   It is now sized from the image (`FsRootBlocks`: payload + 8 blocks per file + metadata + 1 MiB
   slack, capped at 64 MiB), which is what the volume's size always should have been — a statement about
   what it holds rather than a guess about it.
2. **One file cannot exceed 4.03 MiB** (8 direct + 1024 single-indirect block pointers). The runtime's own
   ELF is larger, so it cannot live in RoboFS v1 at all. The import now **skips** such a file: it removes
   what it wrote (never leaving a shorter file behind to be loaded as a truncated ELF), names it
   (`VFS_IMPORT_SKIP  <path>`), and carries on with the rest of the image, ending on
   `VFS_IMPORT_PARTIAL  <count>`. Walking on is the point: the fonts and everything after the large file
   still import, and the ELF is still *runnable* — `Exec` falls back to the image for a path the VFS
   does not hold. A negative return, still reported as `VFS_IMPORT_FAIL`, now means only that the image
   itself could not be walked.

The 4.03 MiB ceiling is a real limit, not a fixed bug: it is written down in
`docs/filesystems/robofs.md` with the shape that would raise it (a second indirect level), and the runtime's
ELF is larger than it, so the ceiling and the runtime cannot both stay as they are forever.

### Verification

| Gate | Result |
|---|---|
| `Tools/Build.ps1 -Configuration Debug -Verify` | **VERIFY PASS** (including `RoboDesktop boundary source scan: OK`) |
| `RoboTest.host.exe` (Debug) | **2650 / 2651** — the one failure is the pre-existing focus-dependent `window.host.keyboard` |
| `--filter desktop.` / `--filter desktop.scene.` | **46 / 46** / **5 / 5** |
| host launcher, offscreen (`--desktop --no-window`) | **14 checks, 0 failed — `RESULT: PASS`** |
| `scripts/robo test-qemu-display` | **PASS** — `ROBO_QEMU_DISPLAY_OK`, `_FACE_OK`, `_DESKTOP_OK`, `_LABELS=4`, `_UNSHAPED=0`, `_PRESENT_OK`, `_INPUT_OK`, `_DISPLAY_SHUTDOWN_OK` |

**Added:** `Include/RoboDesktop/RoboDesktopScene.h`, `Source/RoboDesktop/RoboDesktopScene.c`,
`Include/RoboDesktop/RoboDesktopHost.h`, `Source/RoboDesktop/platform/host/RoboDesktopHost.c`,
`desktop/tests/SuiteRoboDesktopScene.c`, `desktop/assets/{Inter.ttf,Inter-License.txt}`,
`docs/userspace/desktop-runtime.md`.
**Removed:** `Source/RoboDemo/`, `Include/RoboDemo/`, `demo/` (sources, suites and their cases), the host
`--demo-html` route, the demo layer's paint entries, and its display markers.
**Modified:** `user/robo-runtime/main.c` (rewritten as the desktop),
`Source/RoboDesktop/{RoboDesktop,RoboDesktopChrome,RoboDesktopGeometry,RoboDesktopWindow}.c` and their
headers (the logs a developer debugs from, the minimum-dimension door, the corrected metrics comment),
`Tools/Build.ps1` (the scene's sources, its suite, the boundary scan's third exemption, the `$DemoSrcs`
references the deletion left behind), `Tests/Main.c` (the `--desktop` route),
`scripts/roboboot-build-common.ps1` (module discovery, the face's pack entry),
`scripts/pipeline/{commands.run,commands.test,config,engine,robo,common}.ps1` (the desktop launcher, the
display markers, the component groups, the `ROBO_DESKTOP_*` run settings),
`kernel/fs/{robofs.c,robofs.h}`, `kernel/fs/vfs/vfs.{c,h}`, `kernel/src/kernel.c`,
`user/init/config.c`, `docs/{Build.md,kernel/vfs.md,filesystems/robofs.md,userspace/terminal-and-robo.md}`,
`html/docs/ElementModel.md`, `PROGRESS.md`.

## Goal 12.1 — Text raster quality (coverage antialiasing, gamma-correct fills)

_Implemented. **RoboGraphics** gains one new file and header — `RoboGraphicsGlyph.c/.h` — and one
implemented paint mode: `RoboGfxGlyphModeAlpha` stops being reserved and becomes the mode the paint
stack's text path and the desktop's labels select. Two suite files over **21** cases
(`graphics.glyph.*` and the visual fixture `graphics.typography.*`). Nothing about text **placement**
changed: no pen, no baseline, no advance, no line break, no shaping._

Phase 6.12's text path strokes a glyph's contour with one-pixel Bresenham lines. At 12 px a one-pixel
stroke is about as wide as the stem it is tracing, so UI text read as a hard-edged blob with **two**
gray levels — the ink's exact bytes or the page's — and at 32 px it read as a hollow double outline,
because the contour was drawn and the interior was not. The fixture measures that before/after as a
number rather than as a claim: the same specimen, through the same face at 12/14/16/18/24/32 px, has
**65** distinct gray levels through the coverage path and **2** through the stroke path, with 2 660
partial-coverage pixels in the 12 px band where the stroke path can have none.

The phase's rule is that **rasterizing a glyph and shaping text are separate jobs**, and the seam
between them is a `GlyphId` plus a device-space baseline origin — the record `GfxGlyphDraw` already
carried. Everything below that seam is new and knows nothing about cmap, font files, runs, kerning or
fallback; everything above it is untouched and keeps deciding which glyphs are drawn. That is what
makes Phase 12.4's shaped runs a replacement of the *chooser* rather than a rewrite.

| Piece | Where |
|---|---|
| The rasterizer: 1/256 px lattice, adaptive flattening, an edge list, sorted scanline crossings under a non-zero winding rule, an 8×8 sample grid per pixel, coverage in `uint8` | `RoboGraphics/RoboGraphicsGlyph.c` |
| The contract: the mask, its accessors, the parameters, the statistics, the three gamma models, `GfxGammaCoverageToAlpha`, `GfxRasterDrawGlyphMask` | `RoboGraphics/RoboGraphicsGlyph.h` |
| The mode: `ALPHA` implemented, `MASK`/`COLOR` still refused, and a `GfxRasterDrawGlyphCoverage` door for explicit parameters | `RoboGraphics/RoboGraphicsText.h/.c` |
| The two call sites: document text and shell labels, each selecting the mode where it knows it is drawing UI text | `RoboPaint/RoboPaintDecoration.c`, `RoboDesktop/RoboDesktopChrome.c` |
| The suites: 19 coverage/bounds/gamma/alpha/determinism/integration cases, and the specimen with its two PNGs | `Tests/Graphics/SuiteRoboGraphicsGlyph.c`, `Tests/Graphics/SuiteRoboGraphicsTypography.c` |
| The document, and the two documents that had to stop claiming a snap | `Documentation/Runtime/Typography/TextRasterQuality.md`, `Documentation/Runtime/Graphics/TextBridge.md`, `RoboRenderVersion.h`, `AGENTS.md` |

Five decisions worth naming, each with a version that looks right.

**Coverage is not alpha, and neither is opacity.** A mask byte is the linear area fraction of a pixel
that is ink. It carries no colour, and the three factors meet at exactly one place:
`effective = coverage × colour.alpha × raster.opacity`. Folding opacity into the mask — the obvious
shortcut, since the compositor is going to multiply by it anyway — is the mistake the module's
contract forbids in as many words, because it makes "half-covered pixel" and "half-transparent ink"
the same number and no later phase can tell them apart again.

**Gamma is a choice with three documented answers, not a constant.** The framebuffer stores
sRGB-encoded bytes and the compositor blends those bytes, so writing linear coverage into that blend is
a *decision*. `RoboGfxCoverageGammaLinear` is the default because it is the polarity-neutral one —
linear-light alpha is below coverage for dark ink on a light page and above it for light ink on a dark
one, two errors of comparable size and opposite sign — and the two polarity models are the exact
mixture when a caller knows which it has. The conversion is one function and two `const` 256-entry
tables, evaluated at build time, with no floating point and no magic factor.

**Integer after the quantizer, because the guest builds this file too.** Every vertex is quantized to
a 1/256 device-pixel lattice before a pixel is touched, and everything downstream — bounds, scanline
intersections, span-to-sample mapping, coverage counts, the gamma tables — is integer arithmetic. The
only floating point is the font-units→device mapping and the curve subdivision that feeds the
quantizer, so determinism is a property of the code rather than of the platform's floating point. There
is no `sqrt`, no `pow` and no libm call in the file.

**Sub-pixel placement, and the two documents that had to be corrected.** The outline is *placed* at the
pen's own fraction instead of snapped to a whole pixel, so a run sits on its accumulated advances
rather than on rounded copies of them. That change made two existing comments false — `RoboPaint`'s
run-drawing preamble and `RoboRenderVersion.h`'s coordinate statement both said the pen was snapped one
layer down — and they were rewritten rather than left to be discovered. `GfxGlyphSnapPoint` remains the
module's whole-pixel policy for a caller that wants it.

**No mask cache, and the absence is argued.** The outline fetch is already cached one module down, a
mask is one pass over its own ink, and a cache keyed on (face, glyph, size, mode, gamma) would need
lifetime, invalidation and mutation-isolation rules that this phase has no consumer to justify. Phase
12.4's glyph runs are that consumer; `GfxGlyphMaskRetain`/`…Release` are the ownership primitives it
will need, and they exist now because the fixture needed them first.

## Goal 12.2 — System typeface pack (UI Display + UI Text, weights, packaging, discovery)

_Implemented. The OS now ships a real system typography package instead of a stopgap face: a
`System/Fonts/` pack, its manifest, the faces and licences it names, and a `RoboFontSystemPack`
door that resolves a **semantic role** through the existing Phase 5 pipeline. Six suite cases
(`font.systempack.*`) open the shipped pack itself. The rasterizer is untouched; no shaping was
added._

Phase 12.1 gave text a rasterizer that fills a glyph at a fractional position and keeps coverage
separate from colour. 12.2 answers the question 12.1 left open — *which faces exist, and where do
they live* — and it answers it with one authoritative root rather than another path constant.

| Piece | Where |
|---|---|
| The pack source of truth: `UI/Inter.ttf`, `licenses/Inter-OFL.txt`, `system-fonts.manifest` | `System/Fonts/` |
| The manifest parser, the registry binding, role resolution, cache warming, fallback and generic bridges | `RoboFont/RoboFontSystemPack.c/.h` |
| The suites: manifest parse/reject, shipped open, role/weight resolution, fallback + generics, missing manifest/file | `Tests/Font/SuiteRoboFontSystemPack.c` |
| The build: `BuildFont.ps1` and the image packer packing the face, manifest and licence under `/System/Fonts/` | `Runtime/Tools/Build.ps1`, `scripts/roboboot-build-common.ps1` |
| The contract and the legal record | `Documentation/Development/SystemTypefacePack.md`, `Documentation/Legal/FontLicenses.md`, `AGENTS.md` |

**The face is Inter, and the doc says why that is honest.** The design target is SF-class metrics,
proportions and spacing. Inter is an independent, OFL-1.1 face; it is never renamed, never presented
as Apple's SF, and no proprietary font is copied. It is **variable** — `wght` 100–400–900 and `opsz`
14–14–32 — so "multiple weights" is a real axis rather than a synthesized emboldening, and the
manifest preserves `opsz` for the phase that will drive it. RoboFont's own boundary is respected:
`fvar` is parsed and selectable as a hint, but outlines are not interpolated
(`FontOutlineVariationApplied` stays 0), so nothing in this phase pretends to instancing it does not
do.

**UI Display and UI Text are a semantic distinction over one physical family, not two files.** The
manifest binds `display` to weight 600 and `text` to weight 400 on the same Inter file, and a caller
asks for `RoboFontSystemRoleUIText`, never a filename. The roles exist at the layer that should own
them — selection — while the pack adds no parser, no loader and no rasterizer of its own: it binds the
existing discovery → registry → loader → selection pipeline to one root.

**A partial pack is a failure, not a degraded mode.** A manifest that cannot be read, a face file that
is absent, or a family whose registered faces do not resolve to the declared family name all fail the
open, and the image build throws if the face, manifest or licence is missing. That strictness is what
makes "the image shipped its system font" a checkable claim, and it is why the suite opens the
**shipped** pack through a host IO adapter rather than a synthetic stand-in: a suite that tested a
substitute could pass while the real pack was broken.

**Desktop binding is 12.3, and the plan says so.** The guest program and the host launcher load the
pack's face from `/System/Fonts/UI/Inter.ttf`; binding the shell's chrome and document text to the
*roles* by name is the runtime plan's 12.3 ("Desktop + document face binding"), which keeps this phase
from reaching into the shell. Optical-size policy likewise waits for Phase 14.

## Goal 12.3 — Desktop + document face binding (typography roles)

_Implemented. A new module — **RoboTypography** — turns a semantic role into a loaded system face and its
numbers, and every existing text surface in the shell asks for a role instead of holding a font. Four
suite files over **24** cases (`typography.roles.*`, `typography.resolve.*`, `typography.document.*`,
`desktop.typography.*`) plus three deterministic galleries. No shaping, no CSS font matching, no desktop
redesign._

12.2 decided which faces exist and where they live. 12.3 decides **what type each surface is set in**,
and it does it with one table rather than with a constant at each of the thirty-odd places a size or a
weight used to be written down. The pre-12.3 shell held one caller-supplied `FontFace` and one
`labelSizePx` and drew every label with them; a document's headings were whatever its author sheet said,
or the layout engine's initial 16 px. Both of those are *bindings*, and a binding that lives in each
surface is a binding that drifts.

| Piece | Where |
|---|---|
| The vocabulary: 21 roles in four groups, five optical-size classes, four weights, the resolved style record | `RoboTypography/RoboTypographyTypes.c/.h` |
| The scale: one array, one entry per role, plus the CSS emitter for a document's defaults | `RoboTypography/RoboTypographyScale.c/.h` |
| The binding: role → face over the 12.2 pack, the per-role cache, coverage fallback, the document sheet | `RoboTypography/RoboTypographyResolver.c/.h` |
| The shell's four role bindings and the typographic label placement | `RoboDesktop/RoboDesktopChrome.c` |
| The document's face and its runtime-origin default sheet | `RoboDesktop/RoboDesktopDocument.c` |
| The host and guest stacks that open the pack | `RoboDesktop/platform/host/RoboDesktopHost.c`, `User/robo-runtime/main.c` |
| The suites and the three galleries | `Tests/Typography/`, `Tests/Desktop/SuiteRoboDesktopTypography.c`, `Tests/Desktop/SuiteRoboDesktopTypographyGallery.c` |
| The contract | `Documentation/Runtime/Typography/TypeBinding.md`, `AGENTS.md` |

Four decisions worth naming, each with a version that looks right.

**A role, not a size, because the role is the durable part.** `WindowTitle` resolves to UI Text at
weight 500, 13 px, 16 px leading, the UI optical class — and every one of those numbers can change
without a caller changing, because the caller asked for `WindowTitle`. The UI roles cluster in the 11–13 px
band on purpose: the 30 px panel and 24 px title band were sized in 11.13 for an 11 px label, and this
phase may not redesign the desktop. So binding the chrome changed *which face and which weight* those
surfaces draw with — proven by `desktop.typography.roles_change_picture`, which shows the role-driven
shell and the legacy single-face shell drawing different frames at the same label count — without moving
a rectangle.

**The scale is data, and that is what makes it testable.** `RoboTypographyResolverConfig` takes a scale,
so the gallery suites build a *second* resolver over the same pack with one role resized and assert the
frame checksum changes. That is the one check that would catch a surface still reading a size from
anywhere but the scale, which is the regression this phase is most likely to suffer.

**Fallback is two different things and neither synthesizes.** A weight the family cannot select steps
down to the nearest one *in the vocabulary* — ties lighter, the tie rule stated rather than left to a
comparison's luck — and the style records what was actually selected, so a step-down is visible rather
than inferred. Text the face cannot *draw* is a coverage question, answered by RoboFont's own chain
through `RoboTypographyResolveGlyphs`. Nothing emboldens, nothing slants. A role whose family is missing
is **not** an error: it resolves face-less, the shell falls back to `DesktopConfig.face`, and the result
is a worse desktop rather than a dead one — asserted with a pack that has no display family at all.

**Optical size is a class held one step short of an axis.** The shipped Inter face has a real `opsz`
14–32 and the manifest preserves it, but RoboFont does not interpolate outlines
(`FontOutlineVariationApplied` stays 0), so applying it would mean instancing. Roles are set at
`Small`/`Body`/`UI`/`Display`/`LargeDisplay` instead, and a resolver that later maps a class to an axis
replaces this one without a caller changing. The same reasoning explains the gallery's four weight lines
rendering alike: the four weights **resolve and differ** (`typography.resolve.weights`), and they will
render differently on the day 12.4's shaped run has somewhere to carry the axis value.

Two smaller things the phase decided in passing. **A document paints through one face** — the body
role's — so "measured with X, painted through Y" is structurally impossible rather than merely avoided,
and `font-weight` reaching layout as a number rather than as a bolder face is stated as a limitation
rather than papered over. And **`h4`–`h6` take the heading-3 setting**, because the vocabulary has three
heading roles and a fourth size invented in an element table would be a type setting that exists nowhere
else in the system.

**The QEMU checkpoint found a build defect, and it is the kind worth naming.** The guest reported a
desktop with `windows=0`, `fills=6` and no window content — the windowless record — while its own serial
said a window had been added. The cause was not the phase's code: the guest build's dependency list for a
runtime source was `Get-HeaderDeps @($libc.Inc, …, $runtime/Include)`, and `Get-HeaderDeps` reads **one
directory level** while every runtime header lives one level deeper (`Runtime/Include/RoboDesktop/…`) or
in a module's *internal* tree (`Runtime/Source/RoboDesktop/RoboDesktopInternal.h`). Adding the `typography`
field to `DesktopInternal.h` therefore recompiled `RoboDesktop.c` and left `RoboDesktopWindow.o` compiled
against the old layout, so one module wrote `windowCount` where another read the field before it. A
mismatched object is not a build error anybody sees — it surfaces as a wrong number on a target that costs
minutes to boot. `Get-HeaderTreeDeps` (recursive, over `Runtime/Include` **and** `Runtime/Source`, applied
to the runtime sources only, so a guest program that never includes the runtime is not rebuilt because a
module header moved) is the fix. The same run now reports `ROBO_QEMU_TYPOGRAPHY_OK` with 21 roles over the
shipped pack and the expected `windows=1 content_runs=1 labels=4 unshaped=0 fills=12`.

## Goal 12.4 — RoboText architecture (text as a first-class runtime object)

RoboText is the runtime's text layer: between RoboFont and the two layers that consume text, and
ownership of the answer to *what is a piece of text, as an object?* Before this goal the tree answered
that question in four places — a measurer that walked code points and summed `hmtx` in font units, a
painter that walked the *same* code points, resolved each through the cmap and accumulated a `float`
pen, and a shell that asked the typography layer for a face. Nothing was wrong. The arrangement had a
stated limit — two implementations of "advance" over one font, allowed to differ by a fraction of a
pixel at a run's end — because each was a careful restatement of the same three lines. This goal removes
the restatement without adding a shaping engine, and the contract is `Documentation/Runtime/Text/Architecture.md`.

**Four index spaces, four types, and the conversions are functions with a `Result`.** `ByteIndex`,
`ScalarIndex`, `GlyphIndex`, `ClusterIndex` are distinct, because `héllo` is five scalars and six bytes
and `🙂` is one scalar and four. `RoboTextBufferScalarToByte` is **total** for every in-range scalar —
including the one past the last — so no caller can fabricate a byte offset that is not on a scalar
boundary and the end-of-text caret position has an offset; `RoboTextBufferByteToScalar` **refuses** a byte
inside a multi-byte scalar rather than rounding, because a selection range that cut a character in half is
a silent corruption and a refusal is a detour. The buffer owns its UTF-8 and keeps a lazily built scalar
index — one byte offset per scalar, invalidated by any mutation — which is what turns `ScalarAt` from a
walk into one decode at a known offset.

**The load-bearing change is that a glyph's advance is a device quantity.** Before, the measurer summed
font units and scaled once while the painter accumulated device floats; today a `RoboTextGlyph` carries
its device advance, and the measurement **is the sum of those advances**, accumulated in `double` and
rounded once at the boundary. One rounding per run survives — it was never about font units, it was about
not rounding per glyph — and the pen the painter accumulates and the width the measurer reports are sums
of the *same numbers*. `RoboTextFontUnitsToDevice` is the module's one scaling door and it states the same
expression the graphics layer's device-metrics bridge uses, so a glyph's advance one layer down is the
same number rather than a second rounding of one. `glyph.id_advance_unit_boundary` in
`Tests/Text/SuiteRoboTextGlyphs.c` pins it across a non-integer size and a non-power-of-two upem.

**Because the advance decides so much, a missing glyph still moves the pen.** A scalar the cmap does not
map becomes `.notdef` and advances by glyph 0's own advance: a zero advance would reflow a line around a
character the reader can *see* (the box is drawn) and would make undrawable text measure narrower than its
ink. `missingGlyphs` counts from RoboText's own count — a glyph whose id is `.notdef`, **or** whose metrics
would not parse — and a face whose metrics table will not parse answers `Unavailable` per glyph, which is
an *answer* rather than a refusal, because what is true is that its glyphs do not advance.

**Classification is not resolution, and the phase says so in its direction runs.** Each scalar is
classified from its own `Bidi_Class`: `R`/`AL` right-to-left, `L` left-to-right, everything else takes the
paragraph direction. Embedding levels, the explicit formatting characters, the weak and neutral rules
(W1–W7, N0–N2) and reordering are 12.8's, and the run type is designed so 12.8 populates it without a
caller changing. The paragraph base is the caller's declared direction, else the text's first strong
scalar, else `Unknown` — and the third case is a real state, not an error: `RoboUnicodeBidi`'s paragraph
door documents "0 if none", and 0 is `RoboLtr`, so taking it at face value would make `"123 456"` a
left-to-right paragraph, a claim UAX #9's P2/P3 deliberately does not make. The consequence worth knowing
is that **declaring a base moves the neutrals**: `"Hello שלום"` is two text runs left-to-right and three
with `baseDirection = RTL`, because the space between the scripts stops belonging to the Latin run —
`text.runs.declared_base` shows both, and the script fragmentation's neutrality rule (a neutral takes the
previous resolved script, or the next one if none has resolved yet, so `"abc 123 def"` is *one* Latin run
rather than `[Latn][Zyyy][Latn]`) is the same rule seen from the other side.

**The transitional path kept its name and lost its knowledge.** `RoboPaintDecoration.c`'s own header called
it the transitional text path (11.6: a run, the cmap per code point, paint). What remains of that loop is
four lines that own no font knowledge at all — it does not know what a cmap is, does not resolve an id,
does not scale an advance, and cannot disagree with the measurer because the measurer runs the same glyph
generation. What is left to RoboPaint is the two facts a text layer has no business knowing: where the run
originates *on this surface*, and that UI text is drawn coverage-antialiased (Phase 12.1's
`RoboGfxGlyphModeAlpha`, chosen by the paint pass because it is the only thing in the tree that knows it is
drawing UI text rather than auditing an outline). One consequence is that the pass now **prepares both of a
face's tables** — cmap and metrics — at the top of the pass: the old loop inherited the metrics parse as a
side effect of the graphics layer's advance door, and the new one does not call that door, so the
preparation has to be *said*. That was the one real defect this migration produced (`SuiteRoboPaintText`
broke with the synthetic face's metrics unprepared) and it is the kind a reviewer would not have predicted
from the diff.

**Refusals are exactly two, and everything else is an answer.** `RoboTextNotSupported` for a context that
names no face, `RoboError` for a cmap that will not parse. An unmapped scalar is `.notdef` plus a counted
miss; direction-less text gets `Unknown` runs; empty text produces an empty run set, because no text is a
fact about a document rather than an error in it. `RoboTextGetCapabilities` is the honest record and
`text.conformance.capabilities` asserts it field by field: the twelve model flags are present and **every
algorithm flag is absent** (variable axes, GSUB, GPOS, ligatures, kerning, bidi, complex scripts, vertical
text), because a capability table that overstates itself is worse than none. Determinism is enforced by
shape rather than discipline: no global mutable state, no cache, no randomness, no time, no host text API,
and `text.conformance.repetition` measures one string eight times and compares every field.

**The visual surface splits at the module boundary, and that is a finding rather than a shortcut.**
The phase asks for a surface showing raw text, runs, glyph ids, advances and a rendered glyph run.
`RoboText` links no rasterizer — the boundary scan forbids `RoboGraphics` and `RoboRender` — so nothing
under `Tests/Text/` can draw a glyph, and the surface is two artifacts: `Build/Text-GlyphTable.txt`
(`text.gallery.glyph_table`), which writes every named string's raw text, byte/scalar counts, run rows
(`script direction [first,last)`) and glyph rows (`id`, source span, cluster, device advance, `hmtx` units,
offset, `mapped`) beside the measurement, and the three existing `Build/Typography-*.png` galleries, whose
pixels are RoboText's because the paint pass generates its glyphs through `RoboTextShape`. The table is
asserted as it is written — the rows' advances must sum to `measurement.width`, the row count must equal
`measurement.glyphCount`, every source span must be non-empty and inside the text, and the file must be
byte-identical on a second build — because a dump proves nothing by itself. Its last two rows are the ones
worth reading: `unicode` (`é Å Ω 中 🙂`) fragments into `Latn`/`Grek`/`Hani` because the spaces are neutral
and take the previous resolved script, and the noncharacter (`U+FDD0`) is one glyph, `id=0`, `mapped=0`,
advancing by glyph 0's own 10.50 px — `.notdef` with the pen still moving, which is the rule the module
argues for shown as a row rather than as prose.

**The module's dependency list is short on purpose, and the fixture proves it.** RoboText may include
`RoboBase`, `RoboMem`, `RoboUnicode` and `RoboFont` — it parses no font (a second cmap reader would be the
second font parser in the tree) and keeps no Unicode table (a second script table would be the second
Unicode database). It may not include `RoboHTML`, `RoboCSS`, `RoboLayout`, `RoboPaint`, `RoboRender`,
`RoboWindow`, `RoboDisplay`, `RoboGraphics` or `RoboTypography` — every one is a text *consumer*, and an
arrow back at one of them would let the text layer ask a caller what the answer is.
`Assert-RoboTextBoundaries` enforces the list, and `Tests/Text/RoboTextTestSupport.h` opens the shipped
pack through the font layer alone, because a fixture that linked the layer above would let a change that
broke the boundary compile and pass.

**Two pre-existing compile breaks were in the way and are fixed, not worked around.**
`Tests/Layout/SuiteRoboLayoutText.c` did not compile and `window.host.keyboard` failed; and the guest
build's header-dependency defect from 12.3 is the same family of mistake this phase's migration would
most easily have repeated across a measurer/painter pair. All 344 Layout cases pass now, and the QEMU
desktop's presented-frame checksum is `0x9F670` — **byte-identical to what the same run reported before
the migration**, which is this goal's strongest single piece of evidence that the shell now measures and
paints every glyph through RoboText.

### What this goal deliberately did not do

Shaping in any form (GSUB, GPOS, ligatures, kerning, complex scripts), the bidirectional algorithm,
vertical text, grapheme clustering, normalization, case mapping, selection, caret rendering, editing,
`@font-face`, `font-feature-settings`, `font-variation-settings`, `font-stretch` matching and full
generic-family semantics. The four weights and the `opsz` class still **bind but do not instance**, and a
document still paints through **one** face, so `font-weight: 700` reaches layout as a number rather than a
bolder face. Both are the same missing thing — a shaped run that can carry an axis value — and that is
12.5's and 12.10's work on top of these seams rather than a rewrite of them.

## Next

- **Phase 11.13 — framebuffer presentation and the desktop shell: landed.** The paragraph that used to
  stand here framed two questions and named `render/docs/Rasterization.md` §12 as the place the three
  possible shapes were stated. Both questions were answered in favour of the *copy*: a presentation
  composites a frame into a destination surface the caller owns, so a target is never a window's back
  buffer, and pacing was declined rather than guessed at — `frame_scheduling` and `vsync` remain **0**,
  and a document can only say "no clock" in one way. What is left from that paragraph is now the *Next*
  list below rather than a plan.

- **Phase 11.7 — image painting: filled in.** The paragraph that used to stand here called it "the
  next drawing kind and the second non-numeric payload", and the answer this tree arrived at is the
  **reference to a decoded surface** rather than a pool of encoded bytes
  (`## Goal 11.7 -- Image painting`, above). It inherited the three things predicted for it and
  changed nothing to take them: a `draw-image` inside a clip scope is bounded by the scope's clip and
  drawn at the scope's strength through the same one application point every other drawing uses, and
  a drawn image is something a hit test answers about without a line of the walk changing -- the
  image's element has a border box, and that is the region.

  What 11.8's, 11.9's and 11.10's sections used to say about a missing 11.7 is now the record of the
  order the work landed in rather than of a gap: their vocabulary gained a row and a field, and the
  two suite headers and one deferral list that named the picture as untestable now name it as covered
  *by construction* with the pixel case **owed** -- a gap stated rather than a gap papered over.

- **Phase 12.2 — the system typeface pack: landed** (`## Goal 12.2`, above). It decided which faces
  exist and where they live, not how their ink lands: one `/System/Fonts/` root, a deterministic
  manifest, semantic UI Display / UI Text / Monospace roles over a real variable Inter face, and a
  build that refuses to ship the image without them.

- **Phase 12.3 — desktop + document face binding: landed** (`## Goal 12.3`, above). It decided what
  type each existing surface is set in: a `RoboTypography` module with 21 semantic roles over one
  scale, the shell's four chrome bands bound to roles by name, a document that gets the system's
  defaults as a runtime-origin sheet and is measured and painted through the same face, and three
  deterministic galleries. The next goal under the reordered plan (`.cursor/rules/runtime-plan.mdc`)
  is **12.4 — RoboText architecture**, which is what makes the two things this phase could not finish
  (a run that carries its own weight, and per-element face selection) natural rather than bolted on.

- **Phase 12.4 — the RoboText architecture: landed** (`## Goal 12.4`, above). It made text a first-class
  runtime object rather than something the renderer discovers while painting: distinct byte / scalar /
  glyph index spaces, one `TextBuffer`, one `TextRange` algebra, script / direction / text runs, glyphs
  whose advance is a *device* quantity, and a measurement that sums those advances so the pen RoboPaint
  accumulates and the width RoboLayout justifies to are sums of the same numbers. The two seams the rest
  of Phase 12 was planned against — `RoboTextRun` and `RoboTextGlyph` — now exist, and the transitional
  cmap loop in `RoboPaintDecoration.c` is four lines that own no font knowledge. The next goal is
  **12.5 — Unicode / font integration**, which fills those seams with font intelligence rather than
  replacing them; 12.8 completes the direction runs this goal only classifies, and 12.10 replaces
  `RoboTextGlyphRunGenerate`'s body.

- **Event dispatch — now Phase 13 (RoboInput).** One 11.10 handed a result to: a hit test that
  answers *which node is at this point* is what a dispatch walks, so the phase consumes
  `PaintHitTest`'s answer rather than replacing it -- the shell's pointer reaches a window's
  contents for the first time, which is the `input_delivery` 0 in `RoboDesktop.h`'s table. Then
  transforms, `pointer-events`, group opacity (which needs an offscreen layer, and whose measured gap
  is `paint.opacity.group_deviation`), a compositor over the desktop's per-window targets (the layer
  the phase-11.13 architecture stops one step short of), the region *subtraction* a tile tree would
  need, and a frame scheduler with a clock, once something needs one. The deferred
tables in
  `paint/README.md`, `paint/docs/Invalidation.md` §12, `paint/docs/Opacity.md` §12,
  `paint/docs/Clipping.md` §12, `paint/docs/HitTest.md` §11, `paint/docs/Decoration.md` §11,
  `paint/docs/Text.md` §10 and `paint/docs/Border.md` §6 name every one of them, and
  `paint/docs/Architecture.md` §15 lists the rules the next goal may assume, and the desktop's own
deferrals are named in `desktop/README.md`'s capability table -- ten absences, each with what it would
need. Do not start until requested.

## Goals 15.1–15.18 — RoboCompositor (a software compositor)

- **Landed.** `Runtime/Source/RoboCompositor/` + `Runtime/Include/RoboCompositor/`, nine public headers over
  one umbrella, 50 cases in three suites, `Scripts/Build/BuildCompositor.ps1` and
  `Scripts/Test/TestCompositor.ps1 -Target Host`. This supersedes the compositor named as deferred in
  `Next` above. Version `1.0.0`; `Assert-RoboCompositorBoundaries` in `Runtime/Tools/Build.ps1` is the rule
  that keeps it what it is.

- **What it is.** A compositor of **surfaces**: a layer tree (id, parent, surface, rectangle, offset, uniform
  scale, opacity, axis-aligned clip, visibility), one composite pass that walks that tree **back to front**
  and lands in a destination surface the **caller** owns, a damage region list, a tile grid per surface, a
  raster scheduler with a per-frame job budget, a surface pool under a byte budget, frames with reasons, an
  interval-clock vsync, scroll offsets, eased animation tracks, a frame pump with a post queue, present
  fences, counters, and dumps that are complete or absent.

- **15.17's baseline is a model, not a stopwatch.** `compositor.performance.baseline` prints a frame's
  cost and asserts what the cost *is*: under region presents (`config.damagePresent`, what a frame loop
  that cares about cost would set) the pixels a frame writes are the damage and the layers over it,
  exactly — so doubling the damaged area doubles the work, and the hundred-and-twentieth frame costs what
  the first did. A build that started re-presenting whole frames fails there rather than merely getting
  slower.

- **The four decisions worth carrying forward.** *Paint order is child order* — `CompositorLayerPaintOrder`
  is the only place an order is decided, so a reparent is a move in an array and nothing else needs to know
  what is on top. *Over-merging damage is the right trade* — a false positive costs a present and a false
  negative is a stale pixel, so a region that touches merges and a list past `maxDamageRects` becomes a
  full-surface present rather than dropping regions. *Tiles are derived, never maintained* — the dirty set
  comes from the damage region, because a hand-kept tile list is a second answer to "what is stale".
  *Rasterizing is the caller's function* — `CompositorSetRasterFn` is the module's one seam into drawing, and
  a callback that refuses **keeps its job** instead of losing it.

- **Two rules that are about honesty rather than behaviour.** The clock is the caller's
  (`CompositorAdvanceTime`, nothing sleeps and nothing reads a wall clock), so a vsync interval and a late
  frame are facts about a timeline the caller owns. And a present fence reports what a build with **no swap
  chain** can honestly report: the destination *is* those pixels — which the conformance suite proves by
  reading them back.

- **What it deliberately is not**, each a row in the capability table's second block rather than a silence:
  GPU resources and GPU/CPU sync (`CompositorResourceCreate` **refuses** the two GPU kinds by name instead
  of pretending to allocate a texture), a second frame thread (15.14's model is a **pump**, so producers
  post and one thread pumps — `compositorThread` is `0` because a flag claiming otherwise would be false),
  double buffering, rotation (a quarter turn is the rasterizer's business, as 12.20 already established),
  subpixel sampling (nearest neighbour, integer arithmetic, exact at 1:1 and at integer magnification),
  colour management, layer blur and tone mapping, and presenting to a display — the destination is a
  `GfxSurface`, and a framebuffer is `RenderPresentFramebuffer`'s door one module over.

- **The dependency rule is one line, and it is enforced.** RoboCompositor may include RoboGraphics and
  **nothing above it**. The temptation this refuses is concrete — a layer's content is a surface, and
  "so let me rasterize the display list here" reads like a simplification — and giving in would make the
  module untestable without a renderer while making 15.6's seam a formality.

- **Production consumer: RoboDesktop (15.19).** The module is still a leaf of RoboGraphics (it may
  include nothing above it). The shell consumes it through `RoboDesktopCompose.c`: a layer per
  wallpaper / panel / window, one surface per window, damage tracking, and `CompositorPresent` into
  the screen target. Display present stays outside this module. `CompositorPumpFrame` is unused by
  the shell today — Desktop drives present directly. The artifact of record for the module itself is
  `Build/Compositor-Conformance.png`: a scene with a clip, a scroll, a translucent child and a scaled
  surface, rendered twice and compared **byte for byte** before it is written.

## Phase 13 — RoboInput, closed late: coalescing, latency, the accessibility lane, gamepad navigation, freeze

Recorded here out of order, because the work landed after Phase 15: Phase 13 had been written off as
"complete as a module" with four goals still open and a documentation section that described five landed
capabilities as zeros. All four are now landed, at version `0.7.0`, and the module is API-frozen.

- **Coalescing (13.47).** A mouse can report a thousand moves a second and a frame draws sixty times. One
  function answers what may be merged — `InputEventTypeMayCoalesce` says yes for `POINTER_MOVE`, `TOUCH_MOVE`
  and `PEN_MOVE` and **no for every other one of the thirty-three types**, because merging a press, a key or a
  text event either drops one or invents one, and a type appended later must default to a barrier. The table is
  keyed on *(device, pointer, type)*, so two fingers are two slots and never one. `Push` does not dispatch; it
  answers `EMIT`, `HOLD` or `FLUSH_FIRST`, and the third is the entire ordering rule — a press may not
  overtake the moves it happened after. Nothing is dropped: a full table asks the caller to drain, and a flush
  against a full queue answers `RoboBusy` with the remainder **still held**, so a slow frame costs a delayed
  move rather than a lost pointer.

- **Latency (13.48).** Five stamps — hardware, queue, dispatch, handler, present — and four differences. The
  rule that matters is a refusal: a stamp earlier than a stage already marked is refused and counted rather
  than saturated, because saturating it would report a *good* latency for a call site measuring from the wrong
  clock, and a plausible wrong number is worse than being told. Sixteen records, no allocator: a never-used
  slot first, the oldest finished record after that, `RoboBusy` only when every record is still live.

- **The accessibility lane (13.52).** A screen reader asks for a *meaning*, not a scan code — a synthetic
  keystroke would have to guess a layout, a key and a modifier state, and would then be indistinguishable from
  a keyboard in every log and every policy decision. `InputA11yInvokeCommand` routes the mapped command through
  the same `InputCommandRouter` a shortcut uses, and `InputA11yCommandAllowed` is the boundary: the text, focus
  and view families plus cancellation, and **not** the window family, the debug overlay, the reset or quit —
  a client that could close the surface it was describing could dismiss the thing it was reading. Checked
  *before* the router, so a barred command cannot reach a target by another name. `SET_VALUE` is refused by
  name (no command exists for it) and `TOGGLE` is an activation.

- **Gamepad navigation (13.54).** D-pad, d-pad axes and the left stick become focus moves by the **dominant
  axis** with ties going vertical, the d-pad beats the stick and a pressed A or B beats both — which is what
  makes an activation one action instead of an activation interleaved with repeats. A held direction repeats on
  the 13.8 cadence shape from the caller's clock; `Activate` and `Back` are **edges and never repeat**. One
  defect was found and fixed by the suite while writing it: a zero deadzone made an all-zero axis pair read as
  a direction, because the comparison was inclusive where the contract says "at or inside it is a rest".

- **The freeze, and the documentation that had gone wrong.** `RoboInputApiFrozen` is 1 with
  `Documentation/Runtime/Input/ApiStability.md` saying door by door what that means (and that it is
  *additive*: a door, a value appended after the last, and a capability field are how both phases landed).
  `Overview.md`'s zero table now names the **five** flags that are really 0, its case study no longer calls
  focus and routing "later", and `EventPolicies.md` is the four layers in detail. `AGENTS.md`'s Input section
  was corrected from `0.3.0` and an eleven-flag zero list.

- **Evidence.** `TestInput.ps1 -Target Host` — **182 cases, 28 suites, all green** (156 before), including a
  `input.coalescing.baseline` case that asserts the *cost model* rather than a stopwatch: 960 moves arriving in
  60 frames cost 60 queue pushes and 900 merges, so a build that emitted every event fails instead of merely
  getting slower.
