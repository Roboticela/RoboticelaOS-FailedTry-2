# The paint bridge — shaped text on the display list (Phase 12.16)

12.4 gave RoboText a glyph run and 12.15 gave it a measurement over the run list. **12.16 moves the shaping
out of the frame loop and onto the record.** A text record now carries the glyphs a run shaped to — ids,
placements, advances, clusters and source ranges — beside the characters it always carried, and the face
those glyphs came from as a **token** the list owns. A replay of a display list therefore draws the same ink
and shapes nothing.

Read this file for the contract. The record and the pool are in `Runtime/Include/RoboPaint/RoboPaintText.h`;
the face tokens in `RoboPaintFontTable.h`; the pass that draws them in `paint/docs/Decoration.md`; the
record → command mapping in `paint/docs/DisplayList.md`. What RoboText *is* is
[Architecture.md](Architecture.md), and the shape of a glyph run is [GlyphRuns.md](GlyphRuns.md).

```text
Unicode
  ↓
RoboText analysis   (script, direction, bidi, font selection, features)
  ↓
GSUB → GPOS → kerning
  ↓
GlyphRun / GlyphRunList        ← Phase 12.14
  ↓
TextMeasurement                ← Phase 12.15
  ↓
PaintContextSetTextFace  +  record pass   (shape ONCE, here)
  ↓
PaintTextRun { span, baselineOffset, font, glyphs, fontToken }   ← this phase
  ↓
display list  { …, glyph pool, font table }
  ↓
PaintDecorationPaint   (pen arithmetic only — no cmap, no GSUB, no GPOS)
  ↓
GfxRasterDrawGlyph  →  framebuffer
```

---

## 1. The decision, and the two that came before it

A text record's payload changed twice, and the middle one is worth stating because it looks right and is the
reason this phase exists:

| Release | What the record carries | What a replay does |
|---|---|---|
| 11.6 | the run's **characters** | looks each code point up in a `cmap` and strokes it |
| 12.4 | the run's characters | shapes them through RoboText — **every frame the list is replayed** |
| **12.16** | the characters **and** the glyphs | draws the glyphs; shapes nothing |

12.4 was the architecturally correct step — a painter owning no font knowledge — and the operationally wrong
one, because shaping is expensive (decode, select a face, segment, GSUB, GPOS, kern, generate) and
**deterministic** given the same text and face, while a display list is replayed many times per second. So the
expensive answer is computed once, where the document is recorded, and what a replay walks is eight numbers and
a flag word per glyph (`PaintGlyph`).

**The characters stay.** A build that kept only the glyphs would be a list only one font could describe, and
the code points are what a selection, a hit test and a copy will read (Phase 12.21). The record keeps both
halves, and the two halves are one record because they are one run.

## 2. The record

```c
typedef struct PaintTextRun {
    PaintTextSpan  span;            /* the characters, in the list's pool        */
    int32_t        baselineOffset;  /* run top → baseline, in px                 */
    LayoutTextStyle font;           /* the inputs the run was measured with      */
    PaintGlyphSpan glyphs;          /* the shaped glyphs, in the list's glyph pool */
    PaintFontToken fontToken;       /* the face they came from                   */
} PaintTextRun;
```

One glyph is a copy of the shaped facts rather than a pointer to a `RoboTextGlyph`:

```c
typedef struct PaintGlyph {
    uint16_t glyphId;
    uint32_t cluster;      /* the run's own cluster index         */
    uint32_t sourceFirst;  /* relative to the run's span, never a document offset */
    uint32_t sourceCount;  /* 1 ordinary, >1 ligature, 0 control  */
    float    advanceX, advanceY;
    float    offsetX, offsetY;
    uint32_t flags;        /* PaintGlyphFlag: control / ligature / mark / missing */
} PaintGlyph;
```

A shape result is a temporary the recording pass destroys, so a display list that referenced it would be a
list whose lifetime was someone else's. The four numbers a renderer needs travel by value, and so do the two a
later selection needs (`cluster`, and the source range).

**The invariant that keeps the pair honest:** `glyphs` is empty **exactly when** `fontToken` is
`PaintFontTokenNone`. A span without a token would be glyph ids a caller has to guess a font for; a token
without a span is a claim about a run that has nothing to draw. `PaintTextRunIsValid` refuses both, and
`paint.glyphrun.span_rules` and `paint.glyphrun.without_a_face` pin it.

## 3. The glyph pool

The glyph pool is the text pool's shape and reason: contiguous, owned by exactly one list, cleared in place
with the capacity kept, grown on demand, and read through a door that **validates the span against the pool**
rather than trusting it (`PaintGlyphPoolContains`, `PaintGlyphPoolRead`). It is a second *type* and not a
second implementation — both pools live in `RoboPaintText.c` and share the one wrap-safe containment test,
because a second copy of *that* is exactly the kind of thing that drifts.

`PaintGlyphPoolAppend` is the one way a glyph enters a pool, so a record's span is always one the pool
produced; a count of zero is refused, the same way `PaintTextPoolAppend` refuses one. `PaintGlyphPoolCopy` is
the one door between two pools, and the builder's whole use of it.

The ownership boundary is what makes 11.2's snapshot property hold for glyphs as well as characters:
**clearing the record list does not change a glyph the display list draws.**
`paint.glyphrun.snapshot` writes a recorded list to text, frees the records, and writes it again byte for byte.

## 4. The font table, and why a token rather than a pointer

A glyph id means nothing on its own: 42 is a letter in one face and a different letter in another. So a run
has to name the face that produced it, and the two ways are a borrowed `FontFace *` in the record or a number
into a table the caller owns.

A pointer would trade away 11.6's whole property — a display list whose every record is a value with no
pointers in it, safe to copy, discard and rebuild, with "no dangling pointer" true by construction rather than
by discipline. So the third option is the one taken: a **`PaintFontTable`** the caller creates, the recorder
registers the face into (`PaintFontTableAdd`, **idempotent** so asking once per run does not grow the table by
one entry per run), and a record carries a **token**.

Two sentinels and no other special values:

| Token | Meaning |
|---|---|
| `PaintFontTokenNone` | no face: the glyph span is empty, nothing was shaped |
| `PaintFontTokenPrimary` | the drawing pass's **own** `face` argument — the degenerate one-document-one-face case, so a caller with one face never builds a table |
| `>= PaintFontTokenFirstAssigned` | an entry in the table the caller passed to both passes |

An assigned token that does not resolve in the drawing pass's table is a **counted miss**, not a fallback: a
glyph id drawn with the wrong face is a different letter, not a wrong colour.

Faces are added and resolved **by address** — two separately-parsed faces with the same family are two faces,
the rule `RoboTextFontContextEqual` states one layer up. The builder copies a table with
`PaintFontTableCopy` rather than sharing it, and the copy is not ceremony: a caller that clears and refills
its table every frame assigns the same token to a *different* face the moment one frame's attendance differs,
and a glyph id drawn with the wrong face is a different letter. Copying makes that impossible rather than
unlikely. `PaintFontTableCopy` carries both sentinels across unchanged, because they mean the same thing in
every table — asking the source to resolve one would fail with the source blamed for a token that was never
its to hold.

## 5. Recording: where the shaping happens

The recorder is given a face (`PaintContextSetTextFace`) and a font table
(`PaintContextSetFontTable`). When the traversal reaches a text run it shapes it **once**, through RoboText's
own generator over the run's scalars and the face, and stores what came back: the glyph span, the token, and
the counters (`PaintContextStats.textRuns`, `texts`, `textSkipped`) unchanged, with `textRuns = texts +
textSkipped` still an identity by construction.

Three consequences, each of which is a thing the phase is *for*:

- **A replay shapes nothing.** No decoding, no font selection, no GSUB, no GPOS, no kerning and no `cmap`
  lookup anywhere in the paint path. There is therefore no way for what is drawn to disagree with what was
  measured, because the numbers are the ones the measurement read.
- **A frame costs the pen arithmetic**, not a shaping pass.
- **A display list is a snapshot of shaped text** at the same strength it already was for geometry: it owns
  the glyphs *and* the faces its tokens name, so it draws the same ink after the record list that produced it
  is gone, and with **no face argument at all** — the token resolves inside the list it came in.

## 6. Drawing: the pen, and nothing else

`PaintDecorationPaint` resolves the run's face from its token first (`PaintTextRunFace`: `Primary` answers the
pass's own `face`, an assigned token resolves in the list's table, a miss is counted), then does the four
things a text layer may own: sets the pen at the run rectangle's left edge and the baseline at
`rect.y + baselineOffset`, iterates the glyphs **in the run's own logical order**, draws each id in the
resolved face at `pen + offset` through `GfxRasterDrawGlyph`, and moves the pen by `advanceX`/`advanceY`.

The three numbers stay three, as they do one phase down for coverage, alpha and opacity:

- **advance** is what the pen does — `pen += advance`, fractional and never rounded here;
- **offset** is where the glyph sits relative to the pen, and **positioning is already in it** (GPOS did the
  work, the rasterizer must not reinterpret it);
- **the baseline** is layout's, read from `baselineOffset`, never recomputed.

A glyph with the `missing` flag is drawn as `.notdef` (glyph 0) and counted (`glyphsMissing`), the same rule
and the same number as the layout measurer's `glyphsMissing`. A glyph with the `control` flag draws nothing and
advances nothing; it is kept in the array so every later glyph's index matches the one the shaped run named.
`rtl` still counts the runs drawn left to right **from their origin** — 12.16 places the resolved glyphs, and
the visual reordering a fully LTR-run consumer needs is 12.8's answer read through the run list, named here
rather than implied.

## 7. The counters

| Counter | Meaning |
|---|---|
| `textRuns = texts + textSkipped` | the traversal's identity, unchanged |
| `draws = filled + contextual + transparent + empty + noFont + noResource` | the pass's identity |
| `noFont` | a text command whose record has an empty glyph span (no face was recorded) **or** whose token does not resolve |
| `glyphs` | glyph draws attempted |
| `glyphsMissing` | glyphs flagged `missing` |
| `rtl` | runs drawn left to right from their origin |

`noFont` is the reason that is about the **call** rather than the document: a caller that records with no face
records runs with empty glyph spans, and a replay paints a textless document while *saying so* — a document
with text that wanted a face, not a silently blank frame.

## 8. What is deliberately not here

Each named where a reader would look for it rather than implied:

- **no glyph cache, no atlas, no batching** across compatible runs, no subpixel-phase-keyed rasterization, no
  `UnicodeCodePoint`-keyed identity — `shapedTextRecording`'s "what it does not claim";
- **one face per run.** Since 12.17 that face is the one the run's own `font-family` selected (`run.face`,
  with `context->textFace` as the fallback for a document built with no provider), so a mixed-font document
  already records the right face per run — but a run that *straddles* two faces is still one token. The token
  vocabulary is what a later phase lifts that with, and `glyphRuns`'s "what it does not claim" says so; a
  real multi-family recorder is a pack/selection change and not a record-shape change;
- **no new drawing kind, no `SAVE_STATE`/`RESTORE_STATE`.** The three drawing kinds and the two scope pairs are
  exactly what 11.12 executed, with one of them carrying a different payload;
- **no layout, no line breaking, no caret or selection geometry, no hit testing inside a run.** The clusters
  and source ranges survive to the record (12.21's input) and paint ignores them, which is the same
  "preparation, not implementation" rule 12.14's clusters followed.

## 9. Tests

`Tests/Paint/SuiteRoboPaintText.c` (`paint.text.*`) and the `paint.glyphrun.*` cases:

| Case | What it pins |
|---|---|
| `paint.glyphrun.recorded` | a record carries the glyphs the pass shaped, with ids, advances and clusters |
| `paint.glyphrun.snapshot` | the display list draws the same bytes after the records are freed |
| `paint.glyphrun.pool_doors` | the glyph pool's append/copy/read/contains rules |
| `paint.glyphrun.font_table_doors` | add is idempotent, resolve by token, the two sentinels are not entries |
| `paint.glyphrun.primary_token` | one face, no table, and a record drawn with the pass's own face |
| `paint.glyphrun.span_rules` | `glyphs` empty **iff** `fontToken` is `None`; a span past the pool is refused |
| `paint.glyphrun.without_a_face` | a face-less recorder produces empty glyph spans and a counted `noFont` |
| `paint.text.pixels` / `paint.text.missing_glyph` | glyphs reach a surface; an unmapped scalar is `.notdef` and counted, not invented |

A paint change is not done until `TestPaint.ps1 -Target Host` passes, and this phase changed the record every
consumer reads, so `TestRoboText.ps1`, `TestFont.ps1`, `TestGraphics.ps1`, `TestLayout.ps1`,
`TestRasterizer.ps1` and `TestDesktop.ps1` (all `-Target Host`) must pass with it — and
`-Target Qemu` when the guest image is involved.
