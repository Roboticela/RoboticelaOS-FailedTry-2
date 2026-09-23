# RoboText kerning (Phase 12.13)

Kerning is the spacing a font states between two glyphs. In this module it is **GPOS pair adjustment and
nothing else** — Phase 12.11's engine — and this layer is the three things an engine should not own: the
policy, the resolution of which source answers, and the record of what the font did.

```text
Runtime/Source/RoboText/RoboTextKerning.c     the policy, the resolution, the report
Runtime/Include/RoboText/RoboTextKerning.h    the public contract
Runtime/Include/RoboText/RoboTextTypes.h      RoboTextKerningPolicy, RoboTextKerningLegacy (beside RoboTextShapeOptions)
```

There is no second kerning engine, no per-glyph table, no Unicode pair and no accumulation of character widths.
A pair a font states is read by `GposApplyPairFormat1`/`…Format2`, applied to the shaping buffer, and summed into
the glyph's advance by the run build — exactly like every other value record.

## 1. Architecture

```text
Unicode → scripts → direction → bidi → font → features → cmap → GSUB → GPOS (kerning) → glyph run
                                                                                ↓
                                                                     measurement → layout → paint
```

`RoboTextKerningResolver` is not a resolver of pairs: it asks the face what it offers, asks the caller's settings
what applies, and answers which of the two sources won. The *policy* is data; the *engine* is 12.11's.

## 2. The `kern` feature

`kern` is an OpenType feature like any other. `RoboTextKerningPolicy` holds one `RoboTextFeatureState` for it, and
`RoboTextKerningPolicyApply`/`…ApplyIfUnset` convert it into a `RoboTextFeatureSet` — the only thing the plan
resolver reads. So:

```c
RoboTextShapeOptionsSetFeature(options, "kern", RoboTextFeatureOff, 0);
```

and

```c
RoboTextKerningPolicySet(&options.kerning, RoboTextFeatureOff);
```

are the same request, and neither is seen by a layout or paint layer. Do **not** special-case `kern` downstream:
there is nothing downstream of the feature set to special-case.

## 3. Defaults

For ordinary document and UI text, **kerning is on**: `RoboTextKerningPolicyDefault` states `kern = On`, and
`RoboTextShapeOptionsInit` installs it, so a caller that never mentions kerning gets it. That is also the GPOS
plan's own default for `kern`, which is why a face that carries the feature kerns without anyone asking.

The policy is a **default under** the caller's settings, not over them: a policy entry reaches a run's features
only for tags the caller left unstated, so `"kern" off` survives a policy that turns kerning on. That rule is the
ligature policy's (12.12 §2) for the same reason — an explicit setting is a decision about one tag and a policy is
a default.

## 4. GPOS pair adjustment is the primary path

The engine reads both pair formats: an explicit pair set (format 1) and a class matrix (format 2), the second of
which is what real fonts use for Latin. A pair's second glyph is the next glyph the lookup **sees**
(`RoboTextOtNextParticipating`), not `index + 1`, so a lookup that ignores marks kerns across them; and the value
record's four fields are all applied — `xPlacement`/`yPlacement` and `xAdvance`/`yAdvance` — because a font may
legally position a pair with placement alone.

No glyph outline is ever modified to simulate kerning, and no code-point paint path exists that could bypass the
shaped result.

## 5. The legacy `kern` table, and the rule against double counting

A face may carry kerning in GPOS **and** in a legacy `kern` table. Adding the two would kern the same pair twice,
each table having been written as the whole answer for its era, so the rule is a **choice**:

```text
RoboTextKerningLegacyNever            the legacy table is never consulted        (the default)
RoboTextKerningLegacyIfNoGposKern     it is consulted only when GPOS has no `kern` feature for the run
```

There is deliberately no "both" enumerator. What the resolution reports is split in two, because two different
facts were being conflated:

| field | meaning |
|---|---|
| `legacyTableAvailable` | the face **ships a `kern` table** — a fact about the file (`FontSfntHasTable`) |
| `source` | which source actually **answered**: `gpos`, `kern-table`, or `none` |

**This build does not read the legacy `kern` table.** Phase 5 parses the OpenType layout tables inherited from
usable font formats and the `kern` table is not one of them, so a face that ships one and no GPOS `kern` feature
reports `legacyTableAvailable = 1` with `source = none` — which is the honest answer and the difference between
"this face kerns nothing" and "this face kerns and this build cannot use it yet". When the parser lands, the
answer changes in one file.

## 6. Class-based kerning

Format 2 resolves the two glyph classes through the **font's own** `ClassDef` tables (cached per call through the
shared `RoboTextOtClassOf`), never through a table of Unicode character groups. That is what makes accented and
alternate glyphs kern with their base's pair: `Aacute` is in the font's `A` class, not in a range this module
guessed at.

## 7. Interaction with GSUB

Positioning sees the glyph sequence **after** substitution. A ligature is one glyph with one advance, and its
advance is the *resulting* glyph's `hmtx` entry plus whatever the font's value records add — never the sum of its
components'. `text.gsub.ligature_positioning` is the executable form of that claim: a fixture face whose GSUB
ligates `ffi` and whose GPOS states a pair against the ligature's glyph id; the pair applies only if the order is
right, and the ligature's advance is checked against its own advance plus the adjustment.

Ligatures do **not** have a kerning system of their own. A ligature glyph participates in GPOS as an ordinary
glyph.

## 8. Direction, bidi and script selection

Kerning runs inside a shaping run, in logical order. It does not reverse the glyph buffer for RTL text: the
font's own `RightToLeft` lookup flag decides whether a lookup applies, and visual order is the bidi stage's (12.8),
afterwards.

The `kern` feature is selected from the face's own ScriptList/LangSys/FeatureList with the same resolver GSUB uses,
so a font that states `kern` for one script and not another is honoured as stated. There is no
`if (script == Latin) enableKerning()` in this module and none may be added.

Kerning also never crosses a **font-fallback** boundary: runs are per font by construction (12.5), so `A
[unsupported] V` has the unsupported scalar in its own run and the `A`–`V` pair is not a pair. That is a
consequence of the run set rather than a rule this layer enforces.

## 9. Feature overrides, including ranges

A whole-text override is `RoboTextShapeOptionsSetFeature(options, "kern", …)`. A **per-range** override is

```c
RoboTextShapeOptionsSetFeatureRange(&options, RoboTextRangeMake(0u, 2u), "kern", RoboTextFeatureOff, 0u);
```

and it works because the run set cuts the text where the effective settings change — before any plan is resolved —
so each run carries one feature set and one plan. `RoboTextMaxFeatureRanges` (8) bounds how many a request may
hold; a refusal (`RoboNotSupported`) is the answer to a ninth, never a silent drop.

Two consequences worth stating rather than discovering:

* a **range boundary is a shaping boundary**. A pair that straddles it is not a pair, so the seam loses whatever
  adjustment the font had for it. That is inherent to range overrides, not a defect of this implementation, and
  `text.kern.range_override` asserts the resulting numbers instead of pretending they are smooth.
* the range is the *more specific* statement: a tag it names wins over the whole-text setting, and a tag it does
  not name keeps it. That is the cascade rule a stylesheet applies, and the reason `RoboTextFeatureRange` holds a
  whole feature set.

## 10. Measurement

The final advances include kerning, so:

```text
painted width = measured width = final GlyphRun advance
```

within the runtime's float tolerance. `RoboTextShape` sums the runs' accumulators in `double` and rounds once, and
the per-glyph identity `advanceX == units→device(advanceUnits) + advanceAdjustX` is asserted by
`text.kern.measurement` for every glyph of a kerned string. No layout stage re-derives kerning, because no layout
stage can see a font.

## 11. Subpixel precision and the rounding policy

A value record is font units, scaled once through `RoboTextFontUnitsToDevice`; a glyph's placement and advance
stay `float` device pixels through shaping and layout. `-0.35px` is not rounded to `0px` and then re-added: the
rounding happens at the rasterizer. One policy, one place — *precision through shaping and layout, round once where
the drawing is*. It matters most at small UI sizes, fractional font sizes and fractional device scales, which is
exactly where a pair that rounds per stage drifts from the number the measurer reported.

## 12. Diagnostics

Three writers, each complete-or-absent (a buffer too small is refused and left untouched, never holding a prefix):

| door | answers |
|---|---|
| `RoboTextKerningPolicyWrite` | `kern=on legacy=never` |
| `RoboTextKerningResolutionWrite` | `kerning=on source=gpos lookups=3 face-kern=1 kern-table=0` |
| `RoboTextKerningReportWrite` | `positioned=17 advances=6 pairs=6 sum=-6.91 min=-1.53 glyphs=17 advance=174.29` |

and `RoboTextKerningAdjustmentAt` walks the positioned glyphs one at a time, giving the glyph, its neighbour, the
source range it stands for (a ligature's whole span), the placement and the advance the font asked for. Why a pair
did **not** apply is answerable from the same records: `enabled == 0` (feature off), `lookupCount == 0` (no lookup
for this script/language), `applies == 0` (nothing will answer), `adjustedAdvances == 0` (the font had no pair for
these two glyphs), and `legacyTableAvailable` (the face's `kern` table exists and this build cannot read it).

## 13. Test fonts

`text.kern.*` uses the **shipped** pack, and that is the interesting choice: Inter's real kerning lives in a
`PairPos` format 2 matrix behind an extension lookup whose coverage sits past 64 KiB in the table. A build that
stops after a subtable whose coverage misses, or that narrows a resolved offset to sixteen bits, produces **no**
kerning on this face while passing every synthetic test — both were real defects here, and the shipped pack is what
found them. The classic pairs (`AV`, `VA`, `To`, `Yo`, `WA`, `Ta`, `Ty`, `LT`) are tried and the suite asserts that
*some* of them pull in and that each is accounted for in the report; it never asserts what a particular pair's value
is, because that belongs to the font.

The synthetic face for the GSUB-then-GPOS order is `Tests/Text/SuiteRoboTextGsub.c`'s fixture (deliberately simple:
one ligature, one pair, known advances), because a controlled font is what makes "longest match" and "the pair
applies to the ligature" deterministic.

## 14. Performance

Nothing is parsed twice: the face's table is parsed once and immutable, the plan is resolved once per run (not per
glyph), the coverage and class definitions are cached per call, and the probe budget
(`RoboTextOtLimits::maxGlyphProbes`) bounds a hostile table. A kerned Latin run allocates nothing in this engine —
the only allocation in the shaping pipeline is the shaping buffer itself, and its growth is bounded by
`maxGlyphs`.

`text.kern.*` records no timing of its own: the cost claims belong to the shaping pipeline as a whole, and
`text.direction.performance`/`text.script.performance` (12.6, 12.7) are the module's measured-cost cases.

## 15. Thread safety

The font's GPOS data is immutable after parsing, so any number of shaping calls may read it concurrently. All
per-shape mutable state — the glyph buffer, the call's counters, the coverage cache, the trace — lives in the
call's own records; nothing is stored on `FontFace`, and `RoboTextKerningResolve` is pure: it caches nothing
globally and the same request answers the same resolution.

## 16. Known limitations

* **The legacy `kern` table is not read** (§5). The policy and the resolution name the shape the fallback will
  take; the parser is Phase 5's work.
* **Mark attachment is not executed**: a mark's position comes from its own advance and the base's, because GPOS
  lookup types 4–6 are recognised and skipped (12.11 §10). Kerning therefore does not disturb marks — a kerning
  lookup that states `IgnoreMarks` gets exactly that — but a face that positions marks with attachment does not get
  that either.
* **Variation-index value records** are not evaluated (no goal applies variation coordinates yet); device-table
  deltas are, at the ppem the caller supplies.
* **`font-kerning` is not a CSS property yet.** The mapping is one call —
  `RoboTextShapeOptionsSetFeature(options, "kern", …)` — and the CSS layer will make it; nothing in this module
  parses a stylesheet.
* **Optical kerning, tracking and letter-spacing are not here** and are not planned as kerning: they change the
  advances without a font's pair saying so, which is a different feature.
* **The display-list text path shapes with the default policy.** A text command carries no kerning policy yet, so
  the shell's labels get the default (`kern` on) rather than a per-element `font-kerning`. That is one field plus
  the CSS property, in a later phase.
