# Script runs (Phase 12.6)

RoboText turns Unicode text into stable, shaping-ready **script runs**. A script run is an analysis fact —
what script a stretch of text is written in — and nothing else. It carries no glyph, no position, no font
and no paint information.

```
TextBuffer → UnicodeDecoder → script analysis → ScriptRun[] → font selection → ShapeRequest
```

The implementation is one file, `Runtime/Source/RoboText/RoboTextScriptResolver.c`, and the whole contract
is the doc comment on `RoboTextScriptRunsResolve` in `Runtime/Include/RoboText/RoboTextUnicode.h`. This
document is the rationale around it: which Unicode property answers which question, how a character with no
script of its own is resolved, what invariants the output has, and what this stage deliberately does not do.

---

## 1. Script and Script_Extensions are two properties

UAX #24 defines **two** properties and RoboText must not conflate them:

| Property | Question it answers | Door |
|---|---|---|
| `Script` | what script the character primarily belongs to | `RoboTextUnicodeScript` |
| `Script_Extensions` | which scripts the character may legitimately participate in | `RoboTextUnicodeScriptExtensions`, `…ScriptInfoOf`, `…ScriptExtensionsInclude` |

`U+0020 SPACE` is `Zyyy` (Common) and its extension set is the defaulted singleton `{Zyyy}`, so it links to
no script. `U+0640 ARABIC TATWEEL` is `Zyyy` too, and its extension set is nine entries including `Arab`
and `Syrc`, so it links to both. `U+0301 COMBINING ACUTE` is `Zinh` (Inherited) and lists `Latn`, `Grek`,
`Cyrl` and five more.

A one-entry set whose entry **is** the primary is the defaulted property rather than a listed set, and only
the UCD knows which it is; `RoboTextUnicodeScriptInfo.extensionsExplicit` says so. Nothing in the resolver
depends on the flag, but it is the honest record, and a primary script need not be in its own explicit set —
tatweel is `Common` and lists no `Common`.

`RoboTextUnicodeScriptInfo` is a **value** (a fixed 32-entry set). `RoboTextScriptExtensionsMax` is a
bound, not a claim: a set longer than it is truncated with `extensionsTruncated` set, never silently
believed to be the whole of itself.

## 2. The resolution rule, in the order it is applied

A scalar's own `Script` is one of three kinds:

- an **anchor** — a concrete script, or `Unknown`. It ends the neutral stretch before it.
- **`Common`** (`Zyyy`) — shared punctuation, digits, symbols, spaces.
- **`Inherited`** (`Zinh`) — a mark that takes its base's script.

For each maximal block of neutrals, `prev` is the anchor on the left (absent at the start of the text) and
`next` the anchor on the right (absent at the end). Each neutral resolves as:

| # | Condition | Answer | Why |
|---|---|---|---|
| 1 | `prev == next` | that script | the common case, and the only one that needs no table lookup: punctuation, digits and spaces between one script |
| 2 | a mark or `Inherited` | `prev`, else `next` | a mark belongs to the base on its left; with no left, to the base on its right |
| 3 | extensions list `prev` **and** `next` | neutral preserved | the scalar genuinely participates in both — do not invent certainty |
| 4 | extensions list `prev` | `prev` | one-sided, decided by the UCD |
| 5 | extensions list `next` | `next` | as above |
| 6 | `prev` exists | `prev` | carry-left: an unmatched space, digit or punctuation mark belongs to the script it follows |
| 7 | `next` exists | `next` | the leading-neutral case (`"  123 abc"`) |
| 8 | otherwise | `Common` | nothing resolved, so nothing is claimed (`"123 456"`) |

Steps 2–5 are what 12.6 adds to 12.5's single carry. The mark rule (2) is checked **before** the extension
table on purpose: `U+0345 COMBINING GREEK YPOGEGRAMMENI` lists `Grek` and nothing else, and an
implementation that consulted extensions first would pull it out of a Latin base.

Step 3 is what keeps the answer honest. `U+0964 DEVANAGARI DANDA` lists 21 Indic scripts and not `Latn`, so
after Latin it becomes the Devanagari that follows; between Devanagari and Tamil it is listed by both and
stays `Common`. Both are the same character, and only the context distinguishes them.

Nothing here reads the *paragraph* direction or the visual order, so the rule is identical for
right-to-left text: `"مرحبا, بن"` resolves exactly as its left-to-right mirror, and the space or comma
between two Arabic stretches is Arabic. Visual ordering is Phase 12.8's and does not belong in a
segmentation that a selection, a caret and a hit test also read.

## 3. Canonical example

```text
"Hello, مرحبا 世界"

[0..7)   Latn     "Hello, "
[7..12)  Arab     "مرحبا"
[12..13) Arab     " "        ← carry-left (step 6): a space between differing anchors
[13..15) Hani     "世界"
```

Note what the example is *not*: it is not "one Unicode property equals one run". The comma and the space
are `Zyyy`, and neither is a run of its own. Had the middle space been listed by both its neighbours
(step 3), it would have stayed `Common` and *would* have been a run — the resolution is contextual, and the
run count follows from the context rather than from a fixed mapping.

## 4. Invariants

Every answer satisfies all of these, and the suite checks them for every case rather than only for the
cases that were written to show them:

- `start < end` for every run; no zero-length run is ever emitted.
- The first run starts at scalar 0 and the last ends at the scalar count (empty text: **no** runs).
- `run[i].end == run[i + 1].start` — contiguous, no gaps and no overlaps.
- No two adjacent runs carry the same script. Coalescing is a property of the output, not of a fixup pass:
  a run only closes when the resolved script changes.
- Ranges are in the text's **scalar** index space, so they address the same text the decoder, the font
  selector and the glyph generator address. Not glyph indices, not byte offsets — although the diagnostic
  table prints both index spaces side by side, because a reader needs both to see the UTF-8.

The minimum number of runs follows from the rule: `"Hello, world!"` is one run; `"Hello, مرحبا 世界"` is
three (plus the carry); `"日本語、"` is Han + kana + Han, because kana are their own scripts and the
phase's answer says so rather than calling the whole string Han by association.

## 5. Unknown and unassigned

An unassigned scalar's `Script` is `Unknown` (`Zzzz`), and `RoboTextScriptIsAnchor` reports `Unknown` as an
**anchor**. That is deliberate: an unassigned scalar is a fact about the text, and treating it as neutral
would let it be absorbed into a neighbouring script — most likely Latin, which is exactly the fallback the
phase forbids. `"A" + U+0378 + "B"` is therefore `Latn | Unknown | Latn`, three runs.

`Inherited` is *not* an anchor, and neither is `Common`. A neutral block with no anchor on either side
resolves to `Common` by step 8 — including a lone combining mark, which has no base to belong to.

## 6. Cost, and the ASCII path

The resolver is one forward pass with a deferred neutral block and **no allocation**: the only storage is
the caller's run array. Each scalar is looked at twice, and each neutral's extension set is consulted at
most once.

ASCII never reaches a table at all (§"ASCII fast path" of the phase goal):

- `RoboTextUnicodeAsciiScript` answers the ASCII block in two comparisons — `A`–`Z`/`a`–`z` are `Latn`, every
  other code point below U+0080 is `Zyyy`. `RoboTextUnicodeScript` and
  `RoboTextUnicodeScriptExtensionsInclude` both take it.
- The extensions answer for ASCII is equality, which is sound because **no** ASCII code point lists an
  explicit set — so its property defaults to the singleton `{Script}`.
- `RoboTextUnicodeIsCombiningMark` declines below U+0300 in one comparison, because Unicode's first mark is
  U+0300.

All three bounds are properties of the blocks rather than of a table version, and all three are **proved**
rather than assumed: `text.script.ascii_fast_path` walks every code point in the range each one covers and
checks the shortcut against the tables, so a future UCD that moved one would fail the suite rather than
leave the resolver quietly disagreeing with the data. It also checks the mark bound is tight — U+0300 is a
mark, U+02FF is not.

The measured shape of that, from `text.script.performance`:

```text
ascii 20475 bytes -> 1 run, mixed 4080 bytes -> 544 runs, 1 allocation each, ~2 ms
```

One allocation for a 20 KB text, and it is the decoder's scalar array — `RoboTextScriptRunsResolve` over
already-decoded scalars allocates **nothing**, which the case asserts by counting the debug allocator's
allocations across the call. Run count scales with the number of *boundaries*, never with the number of
scalars.

The resolver holds no state, so it needs no cache and is safe to call from any thread: the Unicode tables
are read-only and the output is the caller's array. A per-scalar memo would buy little (the extension
lookup is a binary search, only reached by neutrals whose two neighbours differ) and would cost the
statelessness that makes concurrent layout trivially safe.

## 7. What this stage does not do

Script segmentation describes the **linguistic context** of text. It does not shape, and the phase's
non-goals are enforced by `RoboTextGetCapabilities`:

| Not here | Where |
|---|---|
| Full UAX #9 bidi, visual reordering | Phase 12.8 (`bidiAlgorithm` = 0) |
| GSUB, ligatures, contextual forms, Arabic joining, Indic reordering | Phase 12.10 / 12.19 (`substitution`, `complexScripts` = 0) |
| GPOS (pair adjustment and positioning) | Phase 12.11 — [Positioning.md](Positioning.md) |
| Kerning as a policy over GPOS | Phase 12.13 — [Kerning.md](Kerning.md) |
| Mark attachment, cursive attachment, contextual positioning | no goal yet; GPOS types 3–8 are skipped |
| Line breaking, hyphenation | later |
| Font selection and fallback | Phase 12.5's selector, which consumes these runs |
| Grapheme clusters | later (`RoboTextGlyph.cluster` is the identity mapping) |

Three seams are deliberately kept apart, and a change that collapses any two of them is a defect rather
than a simplification:

```
UnicodeProperties → raw Unicode facts
ScriptResolver    → contextual script runs        ← this stage
FontSelector      → coverage-gated face per run
TextShaper        → glyphs
```

The resolver does not know a face exists. The selector does not walk the text. And since 12.6 the selector
is asked **once per script run** rather than once per scalar, which is what makes run-level coherence — a
base character and its combining mark in one face — a property of the selection rather than a special case
in the shaper.

## 8. Diagnostics

`RoboTextScriptRunWrite` prints one run as `[<start>..<end>) <script>` in scalars.
`RoboTextScriptRunsWrite` prints the whole analysis with **both** index spaces, which is the only way a
reader can see the segmentation against the bytes it came from:

```text
Script Analysis
[0..6) scalars  bytes [0..6)  Latn
[6..12) scalars  bytes [6..17)  Arab
[12..14) scalars  bytes [17..23)  Hani
```

Both are complete-or-absent: a buffer that cannot hold the output is refused with the requirement and left
empty, because a truncated table reads as a different analysis. Passing an empty view omits the byte
spans — there is nothing to name them with, and inventing offsets against other text would be worse than
omitting them.

## 9. Tests

`Tests/Text/SuiteRoboTextScript.c`, filter `text.script.*`:

| Case | What it pins |
|---|---|
| `pure_scripts` | Latin, Cyrillic, Greek, Arabic, Hebrew, Devanagari, Bengali, Thai, Han, Japanese, Hangul: one run each, with the short name RoboUnicode gives it |
| `mixed_scripts` | where the boundaries fall, and that a space beside a transition carries the script it follows |
| `common` | `"Hello, world!"`, `"Version 2.0"` and `"2026/09/22"` are one run each; `"  123 abc"` resolves right |
| `numbers` | `123` is `Zyyy`; Arabic-Indic, Thai and Myanmar digits are their own scripts; a script's digits do not split its text |
| `combining_marks` | a mark belongs to its base, left first and right when there is no left; `U+0345` stays with a Latin base; `U+0591` anchors Hebrew; a mark no face covers is still classified |
| `extensions_facts` | `Script` vs `Script_Extensions`, the defaulted singleton, an explicit singleton, tatweel's nine-entry set, both doors agreeing over a matrix, capacity truncation, the non-character case |
| `extensions_resolution` | the four extension-driven outcomes: right-wins, left-wins, both-listed stays neutral, one-script short-circuits |
| `symbols_emoji` | symbols and emoji are `Zyyy`/`Zinh`; no invented `Emoji` script; an emoji between two scripts takes the left carry |
| `unknown` | `U+0378` and `U+FDD0` are `Unknown` and stay their own runs |
| `hint` | a caller's hint classifies everything, and is the only difference from no hint |
| `ascii_fast_path` | the three block bounds, proved against the tables over their whole range |
| `diagnostics` | the table, exactly, in both index spaces; complete-or-absent; out-of-text runs refused |
| `performance` | one pass, one allocation, run count scaling with boundaries; records bytes, runs, allocations and time |
| `empty_and_refusals` | empty text, NULL and short-capacity refusals, the per-scalar array door, fuzz invariants |
| `analysis_integration` | the analysis consumes these runs; `ShapeRequest.script` is the resolved script |

A script change is not done until `TestRoboText.ps1 -Target Host` and
`TestLayout`/`TestPaint`/`TestGraphics`/`TestFont`/`TestDesktop.ps1 -Target Host` all pass — the script
stage is what every one of them segments text with, whether they call it directly or through the analysis
layer.

### The recorded surface

`text.gallery.glyph_table` writes `Build/Text-GlyphTable.txt`, and since 12.6 it carries the phase's script
samples — Greek, Cyrillic, Arabic, Hebrew, Devanagari, Bengali, Thai, Han, Japanese, Korean, a mixed-script
line, accents, three scripts' digits, punctuation, symbols and emoji — each with its run rows *and* its
glyph rows. That pairing is where the phase's "mixed-script visual regression" is honest in this layer:

```text
  input     bytes=23 scalars=14 text-runs=4 script-runs=3
  run[0]    Latn ltr [0,6)
  run[1]    Arab rtl [6,11)
  run[2]    Arab ltr [11,12)      ← the space, carried left
  run[3]    Hani ltr [12,14)
  glyph-run[0] Latn ltr glyphs=6 advance=43.06 missing=0
  glyph-run[1] Arab rtl glyphs=5 advance=52.50 missing=5
  glyph-run[2] Arab ltr glyphs=1 advance=4.50 missing=0
  glyph-run[3] Hani ltr glyphs=2 advance=21.00 missing=2
```

The space between the Arabic and the Han is its own glyph run because its *direction* differs from the
Arabic that precedes it — a script run cut by classified direction, which is exactly what an analysis run
is — while the Arabic and the space are one *script* run.

A run row says what script the text *is*; a `missing` count says whether the shipped pack could *draw* it.
Keeping both is what stops "the pack has no CJK or Arabic family" from being mistaken for a segmentation
bug — Arabic, Hebrew, Devanagari, Bengali, Thai, Han, Japanese and Korean are all `missing = scalars` above,
and that is a **pack** fact, not a text one.

The rendered half — actual pixels — remains the shell's `Build/Typography-Desktop.png`, `-Chrome.png` and
`-Document.png`, because this module links no rasterizer by its own boundary. Two consequences a reader
should not have to infer:

- The shell's galleries are unchanged by this phase: a mixed-script line would draw `.notdef` boxes in a
  reference picture whose only assertion is determinism, which would look like a regression rather than the
  coverage limit it is. Making that picture *meaningful* for Arabic or CJK requires a second family in the
  pack (assets + manifest + licence + image check) — a pack change, not a text-layer one.
- The shell and the document bridge still paint through one face per surface. Walking a run's selected face
  is the same follow-on work 12.4 and 12.5 both flag; this phase gives the segmentation and the per-run
  selection that it will consume.
