# RoboText glyph positioning (Phase 12.11)

GSUB answers *which glyphs there are*. **GPOS answers where they sit**, and this phase is that second
answer. It is one engine in two files:

```text
Runtime/Source/RoboText/RoboTextGpos.c        the plan, the apply loop, the counters, the writers
Runtime/Source/RoboText/RoboTextGposSubst.c   one reader per lookup type this build executes
Runtime/Include/RoboText/RoboTextGpos.h       the public contract
```

Both engines resolve their lookups through the **same shared core** — `RoboTextGsub.c` implements the script
tag, the LangSys fallback, the feature filter and the sorted lookup order (`RoboTextOtPlanResolve`), and
`RoboTextOtInternal.h` holds the coverage cache, the class lookup, the lookup-flag walk and the probe budget.
Positioning is therefore not a second OpenType layout implementation: it is a second set of *subtable readers*
over the same validated infrastructure. Phase 12.13's kerning is a policy over this engine and adds no third.

## 1. The pipeline

```text
UTF-8 → scalars → scripts → direction → bidi → font → features → cmap → GSUB → GPOS → glyph run
                                                                              ↓
                                                                        measurement
                                                                        layout / paint
```

Positioning runs **after** substitution, always, and the order is not a caller's choice. `ffi` is one glyph
with one advance; kerning the three characters that produced it would be kerning glyphs that no longer exist.
`text.gsub.ligature_positioning` proves the order rather than asserting it: one fixture face carries a GSUB
ligature and a GPOS pair stated against the *ligature's* glyph id, so the pair can only apply if substitution
ran first.

Nothing above the glyph run sees any of this. The pen reads `RoboTextGlyph::advanceX` and
`RoboTextGlyph::offsetX`; the measurer sums the same numbers. There is no "positioned text" type and no
positioning-aware paint path.

## 2. What the engine writes

Four device-pixel numbers per glyph, and only those:

| field | meaning |
|---|---|
| `offsetAdjustX` / `offsetAdjustY` (`RoboTextShapingGlyph`) | where the glyph sits relative to the pen |
| `advanceAdjustX` / `advanceAdjustY` (`RoboTextShapingGlyph`) | how far the pen moves after it, *in addition to* `hmtx` |

Keeping the adjustment **separate** from the face's advance is deliberate: the run build adds the two once
(`RoboTextGlyphs.c`), so `RoboTextGlyph::advanceX` is the total, `advanceAdjustX` is the font's own part, and
a dump can say "the pen moves 12.40px, of which -1.25 is the face's kerning" without guessing which number
moved. A glyph the font positioned is marked with `RoboTextShapingGlyphPositioned` — set even when the value
record is zero, because "the font said 0" and "the font said nothing" are different facts, and
`RoboTextGlyphRunPositioned` counts the first.

## 3. Precision, and the one rounding

A value record is font units. It becomes device pixels through `RoboTextFontUnitsToDevice` — the module's
**one** scaling door — and the engine multiplies by the scale the caller hands it
(`RoboTextGposOptions::deviceScale`) rather than scaling anything itself. Positions stay `float` through
shaping and layout: a `-0.35px` pair is not rounded to `0px` on the way, and rounding happens at the
rasterizer, which is the module's whole policy — *precision through shaping and layout, round once where the
drawing is*.

A device-table delta is evaluated only when the caller supplied a device size
(`RoboTextGposOptions::devicePpem`, which the generator takes from the context's size). No size means the
delta is **not** applied and `deviceUnevaluated` counts it: a hinting correction applied at an unknown size
is a worse answer than none.

## 4. The lookup types this build executes

| type | name | here |
|---|---|---|
| 1 | single adjustment | **read**, both formats |
| 2 | pair adjustment | **read**, both formats (format 2 is the class matrix, the shape real fonts use) |
| 9 | extension | **unwrapped** to the real type, which the trace then reports |
| 3–8 | cursive, mark-to-base, mark-to-ligature, mark-to-mark, the two contextual forms | **recognised and skipped**, counted in `lookupsUnsupported` |

A skipped lookup is not a wrong answer: the glyph keeps the position its advance gave it, which is exactly what
this runtime drew before positioning existed. §11 says which goal adds the rest.

## 5. Feature selection, script and language

The plan is the font's, resolved by the *shared* resolver, with **positioning's own defaults** for the tags the
caller does not name: `kern`, `mark`, `mkmk`, `curs`, `dist`, plus `abvm`/`blwm` for the scripts that use them.
That list is the one place the two tables' defaults differ, and it is why the resolver takes the defaults as an
argument instead of owning them. A caller's own setting always wins — `"kern" off` is a decision about one tag
— which is what makes `font-kerning: none` expressible without any layer knowing that kerning exists.

There is no `if (script == Latin) enableKerning()` anywhere: the font's ScriptList, its LangSys (the requested
language's, else the script's default, else `DFLT`) and its FeatureList decide. `RoboTextGposApply` asks one
question of the font besides that: a lookup whose flag is `RightToLeft` runs only on RTL text
(`RoboTextOtLookupAppliesToDirection`), which is the font's own statement and not a heuristic on the script.

## 6. Ignored glyphs, and where a pair's second glyph comes from

OpenType lets a lookup state that it does not see whole classes of glyph: `IgnoreBaseGlyphs`, `IgnoreLigatures`,
`IgnoreMarks`, a mark-attachment type, and a mark filtering set — every one of them read against **GDEF's** own
class definitions, through the shared `RoboTextOtSkipped`. An ignored glyph is *stepped over*, never removed:
`RoboTextOtNextParticipating` is how a pair finds its second glyph, so a lookup that ignores marks kerns `A`
against the base after the marks rather than against the mark.

That is also why a combining mark does not acquire a pair advance it should not have: a face that states
`IgnoreMarks` on its kerning lookups gets exactly that, and a face that wants to position marks states
`mark`/`mkmk` — which this build recognises and skips (§11), leaving the mark where its advance put it.

## 7. Bounds, malformed fonts and thread safety

A font's contents never fail a run of text. Every read goes through `FontReader`/`FontOtResolveOffset16/32` and
a `Result` door that is always compared (`GposU16`/`GposU32`/`GposI16` — `!GposReadU32(...)` inverts the
question, because `RoboOk` is zero, and that mistake once hid single-substitution format 1, the extension
wrapper and GDEF's mark filtering set on the substitution side). A malformed subtable **skips**: counted in
`subtablesMalformed` and the scan continues.

Two bounds are worth naming because they are *facts about tables* that a hand-written reader gets wrong:

* a subtable offset that lands **inside its own lookup's header** (six bytes, plus the mark filtering set) is no
  subtable at all. OpenType states a lookup's subtable offsets against the lookup, so the format's "there is no
  subtable here" — zero — resolves onto the lookup's first byte, and reading that as a coverage offset is how a
  table that means nothing becomes a table that means garbage. `RoboTextOtSubtableInsideLookup` refuses it,
  **once per lookup** rather than once per buffer position, because the number should tell a reader about the
  font and not about the paragraph.
* a **resolved** coverage offset is a `size_t`, not a `uint16_t`. A pair subtable whose coverage sits past 64 KiB
  in the table — which is where the shipped Inter's real kerning lives — silently loses every pair if the reader
  narrows it. That was a real defect in this phase, and it is why the pair readers take `size_t`.

The probe budget (`RoboTextOtLimits::maxGlyphProbes`) is charged through `RoboTextOtSpendProbes`, which records
the count in the **owning engine's** statistics — `RoboTextGposStats::probes` is positioning's work and
`RoboTextGsubStats::probes` is substitution's, never one reported as the other.

Font tables are immutable after parsing and the engine allocates nothing per call beyond the coverage cache the
shared helpers keep, so several shaping calls may run concurrently over one face. Per-shape mutable state lives
in the shaping buffer and in `RoboTextGposApply`'s own call record; nothing is stored on `FontFace`.

## 8. Diagnostics

* `RoboTextGposStatsWrite` — one counter per line: glyphs before/after, lookups applied/skipped/unsupported,
  subtables tried/malformed, single adjustments, pair adjustments, class pair adjustments, device corrections,
  unevaluated device records, records not read for want of a scale, adjusted glyphs, probes, limit reached,
  trace events.
* `RoboTextGposEventsWrite` — the trace: one event per value record applied, with the lookup index, the
  **inner** type after unwrapping, the two glyphs, the two classes for a class pair, the source range and the
  values. `RoboTextGposOptions::trace`/`traceCapacity` enable it; producing no trace allocates nothing.
* Every writer is **complete-or-absent**: a buffer that cannot hold the whole table is refused
  (`RoboNotSupported`) and left untouched, because a truncated table reads as a *different* table.
* `RoboTextOtPlanWrite` prints the script, the language, the enabled features and the sorted lookup order — the
  same line for GSUB and GPOS, because the plan is the same type.

## 9. Tests

| suite | cases |
|---|---|
| `Tests/Text/SuiteRoboTextGpos.c` (`text.gpos.*`) | `vocabulary`, `plan`, `single`, `pair_format1`, `pair_format2`, `extension`, `device`, `marks_and_direction`, `limits_malformed`, `diagnostics`, `shipped_pack`, `paint_agrees`, `glyph_dump` |
| `Tests/Text/SuiteRoboTextGsub.c` | `text.gsub.ligature_positioning` — the GSUB-then-GPOS order through one face that carries both |
| `Tests/Text/SuiteRoboTextKerning.c` (`text.kern.*`) | the policy and report over this engine (Phase 12.13) |

Two kinds of evidence, and the split matters. The **synthetic** table is built in the test file byte by byte and
handed to `FontGposParse` directly, which is how the shapes OpenType allows and no real font ships — a zero value
record, a class index past the matrix, a device delta, an unknown lookup type — become executable facts. The
**shipped pack** is the end-to-end evidence: `text.gpos.shipped_pack` measures the same text with `kern` on and
off and asserts that the face's own kerning moved the advance, and `text.gpos.paint_agrees` asserts the number the
pen uses is the number the measurer reports.

## 10. Known limitations

* **Mark attachments and cursive attachment are not executed** (lookup types 3–8): a combining mark keeps the
  position its advance and the base's advance imply, and `RoboTextCapabilities::positioning` says so by
  documenting them as skipped.
* **Contextual positioning** (types 7 and 8) is not executed either — the two contextual *substitution* types
  are, because 12.10 needed them.
* **Variation-index value records** are not evaluated: a `VariationIndex` needs the face's variation
  coordinates, which no goal applies yet (`RoboTextCapabilities::variableAxisApplication` is 0). A device table
  *is* evaluated, at the ppem the caller supplies.
* **Vertical writing modes** are not distinguished: value records are applied as they are stated, and vertical
  text is 12.20's.
* `RoboTextGlyphRun::bidiLevel` travels on the run and the trace, but positioning does not reorder anything —
  visual order is the bidi stage's (12.8) and the run is in logical order.
