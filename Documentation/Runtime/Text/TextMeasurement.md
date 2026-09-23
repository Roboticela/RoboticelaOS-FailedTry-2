# RoboText text measurement (Phase 12.15)

Measurement is the answer to one question:

> how much space does this **already-shaped** text require?

It is not an approximation of a text's size and it is not a second shaping pass. The input is the `GlyphRunList`
Phase 12.14 finalized; the output is a `RoboTextMeasurement` — advances, line metrics, a logical box and an ink
box — that a layout engine places without knowing anything about Unicode, GSUB, GPOS, kerning, ligatures, bidi or
font fallback.

---

## 1. Architecture

```text
Unicode → analysis → script → direction → bidi → font selection → features → cmap
        → GSUB → GPOS → kerning → glyph run → finalize → run list
        → TextMeasurement → TextLayout → DisplayList → Paint
```

| Piece | Where |
|---|---|
| The engine, the record, the rect, the request, the problem enum, the tolerance | `Runtime/Source/RoboText/RoboTextMeasurement.c`, `Runtime/Include/RoboText/RoboTextMeasurement.h` |
| The shape result's own measurement and `RoboTextShapeResultMeasure` | `Runtime/Source/RoboText/RoboTextShape.c` |
| The run list the engine reads | `Runtime/Include/RoboText/RoboTextRunList.h` |
| The per-glyph outline box `glyf` supplies | `Runtime/Source/RoboFont/RoboFontMetrics.c` (`FontMetricsInfoGlyph`) |
| Tests | `Tests/Text/SuiteRoboTextMeasurement.c` (`text.measure.*`) |

`RoboTextShapeResultMeasure` re-runs the engine over the result's **own** list. It shapes nothing, so the
advances it reports are bit-identical to the result's and the bounds are additional facts rather than a second
opinion.

---

## 2. `GlyphRunList` is the source of truth

The engine never re-runs Unicode decoding, font selection, the script or direction resolvers, the bidi
algorithm, GSUB, GPOS or kerning. Every number comes from:

- `RoboTextGlyphRunAdvance` / the run's own glyph `advanceX` values;
- `RoboTextGlyphRunMetrics` (ascent, descent, line gap, line height, cap and x-height);
- each glyph's `offsetX`/`offsetY` and its outline box from `RoboTextFontGlyphMetrics`.

The one thing the engine is given beside the list is a `metricsFont`, and it is consulted **only** for a text
with **no runs at all** — an empty text still has a line, and the line's height has to come from the context the
caller measured against. A list with a run carries the face it was set in, and asking a font again for what the
run already states is exactly the second reading this phase removes.

A convenience door, `RoboTextMeasureText`, shapes and then measures. It is implemented as `RoboTextShape`
followed by the engine — never as a separate move — and it exists because that is what a layout seam does per
text node.

---

## 3. Advance

For a horizontal text the width is

```text
width = Σ over runs of the run's own total advance
```

with each run's total being the sum of its glyphs' device `advanceX` in `double`. Adding the runs' totals rather
than their rounded `float`s keeps the width the sum of the glyph advances *themselves* — the same accumulation
the painter's pen performs — so a whole line's width and the pen that draws it agree by construction.

Consequences, each of which is a test:

| Text | Width |
|---|---|
| `AVATAR` | `A+V+A+T+A+R` advances **plus** the font's `kern` adjustments |
| `!?` with `dlig` on | the interrobang's own advance, not `!`+`?` |
| `q` + U+0301 | `q`'s advance: the mark's own advance is zero |
| `"   "` | three space advances, and no ink |

`advanceY` is the vertical twin and is `0.0f` for every text this build produces, because vertical writing is
Phase 12.20. It exists so that 12.20 changes a value rather than a record — a consumer accumulating progress in
an axis it has not been given needs the field, not a second API.

---

## 4. Kerning, ligatures, marks and whitespace

The engine does not know that any of these happened, and that is the point: it measures the run it is given.

- **Kerning** is a `kern` feature and a GPOS value record (12.13). Turning it off changes `options.features`, the
  run set cuts there, and the two measurements differ — asserted by `text.measure.kern` as "the mechanism reached
  the measurement", never as "this face kerns this pair" (that claim belongs to the font).
- **Ligatures** are GSUB products (12.12). `text.measure.ligature` uses the shipped Inter's `dlig`: `!?` is two
  glyphs by default and one when asked for, and the width is the substitute's `hmtx` advance.
- **Combining marks** are glyphs wherever the font did not compose them. `text.measure.mark` uses `q` + U+0301
  (no precomposed form) and asserts the mark's own advance is **zero** and that the marked text therefore
  measures exactly as its base. The composed case — `e` + U+0301, which Inter folds into one glyph — is asserted
  separately, because "the font folded them" is a fact about the measurement too.
- **Whitespace** is an advance with no ink. `text.measure.whitespace` asserts both halves, which is what makes
  the two geometries separate fields rather than one.

**Tabs** are deliberately not expanded here. `RoboText` knows glyph advances; layout knows tab stops. A tab is
an ordinary scalar and measures as the face draws it.

---

## 5. Line metrics and the aggregation policy

`RoboTextMeasureLineMetrics` is

```text
ascent  = maximum participating ascent
descent = maximum participating descent
lineGap = maximum participating line gap
height  = ascent + descent + lineGap
```

`lineHeight` is **not** `height`. `lineHeight` is what a line box *occupies* — the context's declared leading
when it declares one, else `height` — while `height` is the typographic sum of the three metrics. A caller with
an explicit `line-height` reads the first; one computing leading itself reads the second.

The maximum rule is the reason a mixed-font line's baseline placement is deterministic rather than dependent on
which run happened to be first. `MeasureLineMetricsOf` is the one implementation: the engine's
`ascent`/`descent`/`lineGap` fields and `RoboTextMeasureGlyphRunListLineMetrics`'s record are the same answer.

A run whose face could not be asked reports a zero line height; the policy falls back to `height` so that a line
stays visible rather than collapsing, the same rule `RoboTextFontContextMetrics` already states for a face with
no line metrics.

---

## 6. The baseline convention

Every y in a measurement is measured from the **baseline**, with y increasing **upward**: `ascent` is above it,
`descent` is below it as a positive magnitude, and `baseline` is a field that is always `0.0f`.

A measurement is a statement about text, not about where a renderer put it. Layout adds a line's baseline when it
places the line; a measurement carrying a top-left origin would be a second coordinate space that has to agree
with layout's, and it never does.

---

## 7. Logical bounds and ink bounds

| Geometry | Meaning | Example |
|---|---|---|
| advance | what the pen does | a space: an advance, no ink |
| logical bounds | the box layout places: origin on the baseline, `width` across, `ascent + descent` tall | every glyph's cell |
| ink bounds | what the outlines occupy | a mark above its base; a descender below the baseline |

The logical box is a **derivation** of the width and the two metrics. It is stored and it is also checked —
`RoboTextMeasureValidate` recomputes it and refuses a record that disagrees, which is what makes the one
duplicated fact in the record a duplicated *checked* fact.

The ink box is computed per glyph:

```text
glyphInkX = penX + offsetX + bbox.xMin × (size / upem)
glyphInkY =          offsetY + bbox.yMin × (size / upem)
```

- `penX` is the run's own prefix advance, so ink sits where the glyph paints;
- `offsetX`/`offsetY` are the glyph's **placement** offset: GPOS moves a mark's ink without moving its advance
  (12.11), and an ink box that ignored that would clip every attached mark;
- the box comes from `glyf` through `FontFaceGlyphMetrics`, scaled by the **run's own** context — the module's
  one scaling door.

The advance box is never used as the ink box. A glyph whose face reports no outline box (a CFF face, or a glyph
with no outline data) is skipped and the completeness flag answers **false**. The alternative — answering with
the advance box — would report a rectangle that paints nothing, and a caller clipping to it would clip to a
guess.

---

## 8. The request

```c
typedef struct RoboTextMeasureRequest {
    RoboTextMeasureOptions options;
} RoboTextMeasureRequest;
```

| Option | What it adds |
|---|---|
| (none) | advances, counts and line metrics — the cheapest and always computed |
| `LogicalBounds` | fills `logicalBounds` and `hasLogicalBounds` |
| `InkBounds` | fills `inkBounds` and `hasInkBounds`, by asking the face for each glyph's outline box |

The split exists because the costs differ by orders of magnitude: advances and metrics are sums over arrays that
already exist, while ink bounds parse each distinct glyph's outline. A line-break candidate evaluated thousands of
times a frame must not pay for outlines it never reads. `RoboTextShape` measures with the default request.

The API carries `advanceX`/`advanceY` rather than only a `width` so that a later writing mode does not change the
record's shape.

---

## 9. Empty text, whitespace and missing glyphs

| Input | `width` | glyphs | line metrics |
|---|---|---|---|
| `""` | 0 | 0 | from the context the caller measured against |
| `"   "` | the spaces' advances | the space glyphs | the face's |
| `"\uFDD0"` | `.notdef`'s advance | 1, `missingGlyphs = 1` | the face's |

An empty text has **no** ink bounds (there is no glyph to measure) and reports that as absent rather than as a
zero-area rectangle a caller would clip to. Its line metrics still come from the context, because a zero-height
line is not a smaller line, it is an invisible one.

A missing glyph is drawn and advanced over deliberately, and counted. The count is the one number that stops a
plausible width for undrawable text from passing as a measurement.

---

## 10. Precision, tolerance and determinism

```c
#define RoboTextMeasureTolerancePx (1.0f / 64.0f)
```

The project's subpixel precision, and the rule is that a *comparison* uses it:

- `RoboTextMeasureFloatEqual(a, b, tolerance)`;
- `RoboTextMeasurementApproxEqual(a, b, tolerance)` — every field within tolerance, the counts compared exactly,
  and the two boxes **not** compared, because they are a different geometry rather than the same geometry to a
  coarser precision.

It is not a licence for an inexact pipeline. Two measurements of the same unchanged list are **bit-identical**,
because the accumulation is a fixed order of `double` adds — `text.measure.precision` asserts exact equality and
the tolerance beside it. The tolerance exists for comparisons across two different but equivalent derivations,
such as a measurement against a width computed by summing runs outside the engine.

No `NaN` or infinity may reach paint: `text.measure.bounds` doctors a width to infinity and asserts validation
names it.

---

## 11. Validation

`RoboTextMeasureValidate(measurement, list, outProblem, diag)` checks, in the enumerator's own order:

| Problem | What it means |
|---|---|
| `not-finite` | a width, a metric or a box member is `NaN` or infinite |
| `metrics` | a line metric is negative, or `baseline` is not zero |
| `counts` | `glyphRunCount` or `glyphCount` disagrees with the runs |
| `width` | the stored width is not the sum of the runs' own advances |
| `bounds` | a box has a negative extent |
| `logical-bounds` | the stored logical box disagrees with the width and metrics it derives from |

The runs are required because the facts worth checking are *relations*. A door that read the measurement alone
could only check that a number is finite, which is not the question: a plausible wrong width is exactly the
failure this layer produces and a reader does not notice.

Validation is a function a test calls, not an `#ifdef` on the hot path. The engine does not call it: a
measurement it produced is valid by construction, and this door exists to *prove* that in a suite.

---

## 12. Diagnostics

`RoboTextMeasureWrite` prints the block a typography bug is diagnosed from: the totals, then a line per run and a
line per glyph with its **pen position** and its **ink box** — the two numbers that tell a kerned pair, an
unmapped glyph's `.notdef` advance and an overhanging italic apart.

```text
TextMeasurement {
  width 61.20  advanceY 0.00  baseline 0.00
  ascent 15.10  descent 3.70  lineGap 0.00  lineHeight 18.80
  glyphs 6  runs 1  scalars 6  missing 0  advanceMax 12.40
  logicalBounds [0.00, -3.70, 61.20, 18.80]
  inkBounds [0.30, -3.20, 60.90, 15.00]
  runs 1
  run[0] source [0..6) script latin direction ltr level 0 advance 61.20 ... glyphs 6 clusters 6 missing 0
    glyph[0] pen 0.00 id 42 advance 8.30 offset 0.00,0.00 cluster 0 ink [0.30, 0.00, 8.20, 11.40]
}
```

A record that breaks its own invariants says `INVALID <problem>` on the first line, before any of its numbers.
The block is **complete or absent**: a buffer that cannot hold it is left as the empty string, and a run block
that would not fit is refused whole rather than truncated.

`RoboTextMeasurementWrite` stays the one-line summary it has always been. Its callers are a stable comparison
and a `192`-byte buffer convention, and a line that grows whenever the record does is a line that eventually
refuses a buffer a caller has always had. The bounded record has its own block.

---

## 13. Caching

None, and deliberately. Measurement is cheap because the expensive work already happened: a run's advance and
metrics are fields, and only ink bounds walk glyphs. `RoboTextCapabilities.shapingCache` remains `0`, and
`RoboTextMeasureValidate` is a door rather than a step.

A cache added later is an optimization over `RoboTextGlyphRunKey` — the same key the run list uses — and must
never be required for correctness.

---

## 14. Cost

`text.measure.performance` records the baseline: a 200-measurement loop over an 80-glyph paragraph, and a
20-measurement loop with ink bounds. The second is the per-glyph outline ask (`glyf` parse per distinct glyph)
and is reported separately so the two costs are not confused for one — which is the whole reason ink bounds are
opt-in.

---

## 15. Thread safety

`RoboTextGlyphRunList` is immutable and `RoboTextMeasurement` is a value with no pointers, so both are safe to
read concurrently. Nothing in this file mutates shared state, keeps a cache or allocates on the advance path:
the engine writes its `out` record and reads the list. A shaper thread, a layout pass and a diagnostic can
therefore read one shaped result at once.

---

## 16. Known limitations

1. **No vertical text.** `advanceY` exists and is zero; the measurement's primary flow axis is x. Vertical
   writing is Phase 12.20.
2. **No line breaking, wrapping, justification or hyphenation.** A newline is an ordinary scalar and measures as
   the face draws it. Arranging lines is `TextLayout`'s job, and the boundary is: this answers *how much space*,
   layout answers *where the space goes*.
3. **No tab expansion.** Tab stops are layout state; the measurement answers with the glyph advance.
4. **No grapheme-aware bounds.** Ink bounds are per glyph, so a cluster whose glyphs overlap is the union of
   their boxes, which is right for clipping and not a cluster-level metric.
5. **Ink bounds require `glyf`.** A CFF face reports `hasInkBounds = 0`; Phase 5 parses CFF outlines elsewhere
   and this layer does not approximate across the gap.
6. **The pack ships one family.** Fallback is exercised with a no-face primary plus a chain, so a real
   multi-family measurement is a pack change (assets + manifest + licence), not a text-layer change. The mixed-
   font aggregation is exercised through 12.5's chain rather than through two real families.
