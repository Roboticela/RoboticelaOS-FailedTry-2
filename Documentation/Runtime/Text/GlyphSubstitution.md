# RoboText glyph substitution (Phase 12.10)

GSUB turns the glyph sequence the `cmap` produced into the glyph sequence the **font** wants:

```text
f  f  i          cmap       gid(f) gid(f) gid(i)
                 GSUB       gid(ffi)                      one glyph, three scalars, source [0,3)
```

Owner: `Runtime/Source/RoboText/RoboTextGsub.c` (the plan, the scan and the statistics),
`Runtime/Source/RoboText/RoboTextGsubSubst.c` (one reader per lookup type),
`Runtime/Source/RoboText/RoboTextGsubInternal.h` (the call record and the parse cache). Public contract:
`Runtime/Include/RoboText/RoboTextGsub.h`.

**The font owns the table and this module owns none of it.** The container is Phase 5's: `FontFaceGsub`
parses the GSUB table lazily, once per face, into a `FontOtLayout` whose ScriptList, FeatureList and
LookupList are already resolved and validated. This module interprets offsets *inside* that structure and
never parses a container itself. A second font parser here would be the defect the phase names.

---

## 1. The pipeline

```text
run (script, language, direction, bidi level, features)
        │
        ├─ RoboTextGsubPlanResolve ── the face's ScriptList → LangSys → FeatureList, filtered by the
        │                             caller's RoboTextFeatureSet and sorted into the Lookup List's order
        │
        ├─ RoboTextGsubBuffer ─────── the mutable sequence of shape glyphs (cmap output, one per scalar)
        │
        └─ RoboTextGsubApply ──────── walk the plan once; at each position, try each lookup's subtables
                                       in order; a substitution continues the scan *after* what it made
        │
        glyph run                    rebuilt from the buffer, so every advance is the *substitute's* own
```

`RoboTextGlyphs.c` performs those steps from a run's own feature set, which is where
`RoboTextLigaturePolicyApplyIfUnset` (12.12) has already put the five ligature tags.

## 2. Table discovery, version and bounds

- `FontFaceGsub` (RoboFont) owns discovery, lifetime and validation. `RoboTextGsubApply` refuses a layout
  whose `kind` is not `RoboFontOtLayoutGsub` rather than interpreting POS data as GSUB.
- RoboFont accepts GSUB 1.0 and 1.1 and reports anything else as `RoboNotSupported`, so this module never
  sees an unknown layout it might misread as the known one.
- Every read goes through `FontReader`/`FontCheckedRange` (`RoboFontBinary.h`): big-endian, bounds-checked and
  alignment-free. Offsets are resolved with `FontOtResolveOffset16/32`, and a zero offset is OpenType's
  "there is no such table here" — refused, rather than resolved to the parent's own start.

## 3. Glyph ids are font-local

GSUB operates on `GlyphId`s. A code point is never treated as one, and `RoboGlyphIdNone` is not glyph 0: a
scalar the face could not map stays `RoboGlyphIdNone` through substitution and is counted in
`RoboTextGsubStats::unmappedGlyphs` and in the run's `missing`, rather than quietly becoming `.notdef`.

## 4. The shaping buffer

`RoboTextShapingGlyph` is the unit a lookup transforms:

```c
typedef struct RoboTextShapingGlyph {
    GlyphId glyphId;
    RoboTextRange source;      /* the scalars this glyph stands for; never inverted */
    RoboTextClusterIndex cluster;
    RoboTextBidiLevel bidiLevel;
    GlyphId inputGlyphId;      /* what it was substituted from */
    uint16_t ligatureComponents;
    uint32_t flags;            /* Substituted, Multiple, Ligature, Control, Synthetic, Mirrored */
} RoboTextShapingGlyph;
```

`RoboTextGsubBuffer` is a growable array of those with three mutating doors: append, set-glyph (one id, same
source) and replace (a range in, a range out — which is how a ligature shrinks five glyphs to one and a
multiple substitution grows one to three). A replacement is validated glyph by glyph *before* anything is
spliced, so a caller cannot inject an uninitialised record.

**Why a buffer and not the glyph run:** GSUB changes the glyph *count*. `RoboTextGlyphRun` is the finished
result a measurer and a painter read; the buffer is the mutable working set, and the run is rebuilt from it
exactly once at the end (see §9).

## 5. Invariants

For every input, and for every lookup applied:

```text
the glyph count never goes below the number of non-deletable glyphs
every glyph keeps a source range that covers the scalars responsible for it
no glyph is produced without a source mapping (nothing here invents one)
the buffer is in *logical* order throughout; direction is a field, never a reordering
```

A ligature's product carries the **union** of its components' ranges and the component count, which is what
makes `ffi` selectable as three characters (see `Documentation/Runtime/Text/Ligatures.md` §6).

## 6. Feature selection

```text
RoboTextFeatureSet (caller) + script defaults (script) + language
        ↓
the face's ScriptList: a script record for the run's script, or DFLT
        ↓
the LangSys: the requested language's, else the script's default one, else no features at all
        ↓
FeatureRecords filtered: a tag the caller turned Off is out; a tag the caller turned On is in; a tag
nobody mentioned takes the script's default
        ↓
the enabled features' lookup indices, deduplicated and **sorted ascending**
```

That last step is the font's order, not the caller's: OpenType applies lookups in Lookup List order, and a
font that puts a contextual lookup *before* a ligature lookup means it. Sorting by feature tag, or applying
lookups in the order they were mentioned, produces a different answer for fonts that depend on the ordering —
so `text.gsub.lookup_order` asserts the plan's order equals the list's.

`RoboTextGsubScriptTag` maps a RoboText script to its ISO 15924 tag through the Unicode data Phase 4 already
ships; `RoboTextGsubTagOf`/`RoboTextGsubTagWrite` are the four-byte tag codec. A script with no record in the
font is not an error: the plan is empty and the cmap's glyphs are the answer.

## 7. Lookup flags, ignored glyphs and matching

A lookup's flag word says which glyphs it does not see; `RoboTextGsubSkipped` answers it from GDEF:

| bit | meaning |
|---|---|
| `IgnoreBaseGlyphs` | base glyphs do not participate |
| `IgnoreLigatures` | ligature glyphs do not participate |
| `IgnoreMarks` | marks do not participate (GDEF glyph class 3) |
| `MarkAttachmentType` | marks of another attachment class do not participate |
| `UseMarkFilteringSet` | only the named GDEF mark set participates |

Matching walks with `RoboTextGsubNextParticipating`/`…PrevParticipating`, never with `index ± 1`, so an
ignored glyph is **stepped over** and never removed: a lookup with `IgnoreMarks` matches `B m C` as `B C`
while `m` keeps its slot, its source and its advance. That asymmetry is the point of the flag word, and
`text.gsub.ignored_glyphs` proves it with two lookups sharing one subtable's bytes and differing only in flags.

## 8. The eight lookup types

| type | name | what the reader does |
|---|---|---|
| 1 | Single | format 1's signed delta, or format 2's substitute array — one glyph in, one out |
| 2 | Multiple | one glyph becomes several, each naming the same source range |
| 3 | Alternate | picks one of a set; index 0 by default, `RoboTextFeatureSetSet`'s `value` for another |
| 4 | Ligature | matches a component sequence (longest candidate first, as the font orders the set) |
| 5 | Context | format 1 (glyph rules), 2 (class rules) and 3 (coverage lists), each applying its records |
| 6 | Chaining context | the same three formats with backtrack and lookahead |
| 7 | Extension | unwraps to the real type and subtable, reporting the **inner** type in the trace |
| 8 | Reverse chaining | scans right to left, matching backtrack and lookahead per position |

Coverage (formats 1 and 2) and class definitions are Phase 5's types (`FontOtCoverage`, `FontOtClassDef`);
this module asks for them through a small per-call cache keyed by absolute offset, so a lookup over a long
word parses each coverage once rather than once per position. The cache is a *repeat-catcher*, not a database:
it holds eight entries and is freed when the call ends. The authoritative parse of the table's structure stays
on the face.

A contextual subtable's `SequenceLookupRecord`s name a lookup **by index**, and applying it is not the same
as the plan applying it: `RoboTextGsubApplyLookupAt` runs a lookup at one position without scanning, which is
how a font's `calt` can invoke a lookup that no enabled feature names — and why the nested application is
counted (`lookupsApplied` counts nested applications; the field's doc says so).

## 9. Output: the glyph run

After the buffer is final, `RoboTextGlyphs.c` rebuilds `RoboTextGlyphRun` from it:

- `glyphId`, `source`, `cluster` come from the shaping glyph;
- `advanceX` is asked of the face **for the resulting id** — a ligature's advance is not the sum of its
  components', which is the number a reader would notice if it were wrong;
- `substituted`, `mirrored`, `control` and `ligatureComponents` are copied; a control glyph (`LRE`, `PDF`,
  `PDI`, …) keeps its slot with no id and no advance so that source positions still line up;
- `missing` counts glyphs with no id or no metrics — a control is *not* one of them.

GSUB does **not** position: substitution decides ids, and the offsets and advances a face's GPOS states are
12.11's engine (`Positioning.md`), which runs on this engine's output rather than beside it.
Kerning and mark attachment are 12.11/12.13's, and nothing in this file assumes anything about them.

## 10. Ordering and recursion

The scan walks the plan once, in the plan's order. At each position it tries the lookup's subtables in order
and stops at the first that substitutes. `outNext` says where the scan resumes: **after** what the lookup
consumed or produced, so a lookup never re-matches its own output. A contextual subtable's records run in
the order the font lists them, and a record whose substitution changed the buffer length shifts the positions
of the records after it — measured, not assumed.

## 11. Limits, malformed fonts and thread safety

`RoboTextGsubLimits` bounds four things a hostile or broken font would otherwise be able to spend: glyph
probes per call (`maxGlyphProbes`), the buffer's size (`maxGlyphs`), the nesting depth of contextual records
(`maxNesting`) and the number of applications (`maxLookupApplications`). `RoboTextGsubDefaultLimits` is
generous and documented. Reaching one sets `RoboTextGsubStats::limitReached`, abandons that lookup and
**continues shaping**: a limit is a diagnostic, not a failure.

A malformed table is treated the same way. Every reader answers "no match" rather than erroring: a coverage
offset past the table's end, a count that cannot be read, a subtable that is too short, an unsupported type,
a cyclic extension. The lookup is skipped and counted in `lookupsSkipped`, the call returns `RoboOk`, and the
glyphs produced before it are the caller's. `RoboTextGsubApply` fails only for a programming error (NULL
arguments, a growable buffer that could not grow) — never for the font's contents.

The parsed table is immutable after parsing and shared by every shaping call, so two threads shaping two
documents share it safely. Per-call state lives in `RoboTextGsubCall` (the internal call record), the buffer
and the statistics — never in the face.

## 12. Diagnostics

```c
RoboTextGsubStatsWrite(stats, buffer, capacity, &length);
RoboTextGsubBufferWrite(buffer, bufferText, capacity, &length);
RoboTextGsubEventsWrite(events, count, buffer, capacity, &length);
RoboTextGsubPlanWrite(plan, buffer, capacity, &length);
```

`RoboTextGsubStats` counts glyphs in and out, applications and skips, subtables tried, one counter per
substitution kind, contextual matches and trace events. `RoboTextGsubEvent` is an optional trace: each
substitution records the position, the ids before and after (up to four, with the counts telling the truth
when there are more) and the source range, so `f f i → ffi` can be read as the engine saw it rather than
inferred from the result.

## 13. Tests

`Tests/Text/SuiteRoboTextGsub.c` — `text.gsub.*` via `TestRoboText.ps1 -Target Host`:

| case | what it establishes |
|---|---|
| `vocabulary` | tag codec, script tags, shaping-glyph validity |
| `plan` | script/langsys/feature resolution, defaults, the DFLT fallback, the `TRK ` language system |
| `single`, `single_delta` | both single-substitution formats, provenance and counters |
| `multiple` | one in, two out, both naming the one scalar |
| `alternate` | deterministic selection and an out-of-range clamp |
| `ligature` | `ffi` and `fffi` from one set, longest match, the component count, source unions |
| `context`, `chain`, `reverse`, `extension` | the contextual family, including the marked-up GDEF case |
| `ignored_glyphs` | the flag word: one subtable, two lookups, two answers |
| `lookup_order` | the plan applies lookups in the Lookup List's order, nested invocations counted |
| `controls` | a bidi control holds its slot and is never matched |
| `limits` | each of the four bounds, and a malformed table that renders as the cmap's glyphs |
| `run_integration` | the generator's own path: GSUB, the substitute's advance, the mode |
| `shipped_pack` | the real Inter face, through RoboFont alone |
| `diagnostics` | the dumps |

The fixture is built **in code, byte by byte**, the way the RoboFont suites build theirs: a real font's
substitution data is a moving target, while this suite needs to know that `f f i` becomes one glyph *because
the bytes say so*. Its lookups are numbered so that a feature's index is its lookup's, and lookup 8 shares
lookup 4's subtable bytes with a different flag word — the same font data, the same text, two answers.

A GSUB change is not done until `TestRoboText.ps1`, `TestLayout.ps1`, `TestPaint.ps1`, `TestGraphics.ps1`,
`TestFont.ps1` and `TestDesktop.ps1` (`-Target Host`) pass.
