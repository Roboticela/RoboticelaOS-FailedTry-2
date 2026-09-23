# RoboPaint — text painting (Phase 11.6)

The first drawing this module does whose payload is **not a number**. Every release before this one
put a *value* — a colour, a rectangle, a band, a style enumerator — into a fixed record, which is
what made a paint list safe to copy, to discard and to rebuild: there was nothing in an operation
that could dangle or be freed under it. A text run carries **variable-length data**, and this phase
is the whole of what that costs.

Read `paint/docs/Decoration.md` for the pass itself — the walk, the colour bridge, the clip, the
counters, ownership and safety are shared and are there. What follows is the text half. Read
`paint/docs/DisplayList.md` for the record → command mapping; `paint/docs/Operations.md` for the
record model; `paint/docs/Tests.md` §1.10 for the case list.

```text
HTML text
  → DOM text node
  → RoboCSS computed text properties
  → RoboLayout inline/text geometry (the run, its line box, its baseline)
  → RoboPaint TEXT record          (characters AND shaped glyphs, each a span into a list-owned pool)
  → DrawText command               (both spans into the display list's own pools)
  → RoboText glyph generation      (at RECORD time, once per document — Phase 12.16)
  → the pen walks the stored glyphs (`GfxRasterDrawGlyph`)
  → RoboGraphics glyph outline stroke into a raster
  → the caller's surface
```

**Since Phase 12.16 the pass shapes nothing at all**, and the arrow that used to run
"DrawText command → RoboText glyph generation" now runs at the **recording** end. The record carries
the run's characters *and* the glyphs they shaped to (`PaintGlyph`), plus the face as a token, so a
replay of the display list draws the stored ids and never looks one up. The pass's job shrank to the
pen arithmetic — the baseline from layout, each glyph at `pen + offset`, advance by `advanceX` — and
this document is the half a *painter* owns. The contract is
`Documentation/Runtime/Text/PaintBridge.md`; what RoboText is is
`Documentation/Runtime/Text/Architecture.md`.

---

## 1. The pool and the span

The one architectural decision: characters live in a **pool**, the pool is owned by exactly one
list, and a record names them with a **span** — two integers, `first` and `count` — rather than a
pointer.

That keeps everything the earlier phases established. A text operation is still a value record with
no pointers in it; a list is still safe to copy and to rebuild; "no dangling pointer" is still true
*by construction* rather than by discipline. The pool is the second array a list grows and it has
the same shape as the first: contiguous, owned, cleared in place with its capacity kept, grown on
demand, and read through a door that **validates the span against the pool** instead of trusting it
(`PaintTextPoolContains`, and `PaintTextPoolRead` refusing a span it does not hold).

`PaintTextPool` is deliberately a type of its own rather than a pair of arrays inside the
paint list, because it has **one implementation and two owners**: the record list owns one, the
display list owns one, and the builder copies each span across with
`PaintTextPoolCopy` — the single door between two pools, which moves bytes from one owned
array to another instead of round-tripping through a temporary buffer sized to the longest run in
the document.

The ownership boundary is what makes 11.2's snapshot property hold one stage further along:
**clearing the record list does not change a character the display list draws**, because the
display list's pool is its own. `paint.text.mutation_isolation` pins it by writing the display list
to text, freeing the records, and writing it again byte for byte.

## 2. Code points, not bytes — and, since 12.16, the glyphs too

A span counts **code points**, and the choice is the same one `RoboLayoutText.h` already made: a
byte offset stops being a character position the moment a document contains an accent, an Arabic
letter or an emoji — so the unit a run is *measured* in and the unit it is *painted* in are the same
one. `paint.text.characters_are_codepoints` pins it with an accented word: four code points, five
UTF-8 bytes, and the span says four.

The original rule was **not glyph ids**, and it was right for 11.6: a glyph id is a fact about a *font*,
and a list that stored only ids would be a list only that font could draw. 12.16 keeps that half and adds
the other one, because the same fact cuts the other way once shaping arrives: a glyph id is meaningless
without the face that produced it, and shaping is expensive, so it happens once when the document is
recorded. The record now carries **both** — the characters in `PaintTextSpan`, and the glyphs in
`PaintGlyphSpan` with a `PaintFontToken` naming the face. The characters stay because they are what a
selection, a hit test and a copy will read (12.21); the glyphs are there because a replay must not shape.
The token is a number into a table the list owns, so "a record has no pointers in it" is still true.
`paint.glyphrun.span_rules` pins the one-fact invariant between the two: the glyph span is empty exactly
when the token is `None`.

## 3. The record

```c
typedef struct PaintTextRun {
    PaintTextSpan span;        /* where the characters are, in the list's pool */
    int32_t                baseline_offset;  /* run top → baseline, in px, never negative */
    LayoutTextStyle font;      /* the inputs the run was measured with, by value */
} PaintTextRun;
```

A **sub-record** rather than six more fields on the operation, for the reason `PaintBoxes`
is one: the facts a text drawing has and a fill does not arrive together, so a record cannot carry
half of them. A background's `text` member is a cleared one, exactly as a text run's `style` member
is `NONE`.

`baseline_offset` is **an offset from the run rectangle's top and not a coordinate**, so the
baseline is at `rect.y + baseline_offset` in paint coordinates and at `baseline_offset` relative to
the run's own top — one number in one space, with nothing to drift. It is the distance RoboLayout's
baseline placement produced (a run's own ascent, so a run's top *is* its baseline minus its ascent)
and it is **read, never recomputed**: 10.7 placed it, and a painter that derived it would be a
second baseline model. `paint.text.record_shape` reads it back through
`LayoutBaselineOffset` — the door that *says* the two are equal — rather than off the
baseline record's `ascent`, which would be relying on a convention stated as a convention in a
module that is not allowed to know it.

`font` is the resolved text style the run was **measured with**, by value: a pixel size, a numeric
weight, an italic flag and a direction. Carrying the measurer's own record rather than four numbers
re-derived here is what makes *"the size it was measured at is the size it is drawn at"* a copy
rather than an agreement to maintain — and it travels unchanged all the way to the graphics call
that strokes the outline.

`PaintTextRunIsValid` refuses three shapes: an **empty span** (a text drawing with no
characters is not a drawing, and an operation carrying one would be a command whose consumer does
nothing), a **negative baseline offset** (a baseline above the run's own top is arithmetic that went
wrong a level down, not an interesting case), and **font inputs that are not a record** (a finite
positive pixel size and one of the two directions). The weight and the italic flag are not bounded
here — CSS allows a numeric weight outside the nine-value scale — so they are carried as resolved.

## 4. Where a text record sits in the sequence

A text run is a **child** of the box whose element owns the text node, and that is the whole of its
shape in the traversal and in the builder:

```text
BEGIN_BOX      box,             depth d
BACKGROUND     box,             depth d
BORDER_*       box,             depth d
CONTENT        box,             depth d     ← the box's own painting ends here
DRAW_TEXT      the run's node,  depth d + 1 ← one deeper, naming itself
END_BOX        box,             depth d
```

A run opens no box of its own — a run is not a box — so nothing is pushed on the structural stack,
but its depth, its node identity and its position are all checked:

- its depth is one past the innermost open box, and its node is a *different* object;
- it appears **after** its parent's `CONTENT` boundary, which is what makes "a run is one of a box's
  contents" a checked fact rather than an assumption;
- the generic rule every record owed by the innermost box obeys (depth `open_count − 1`, node equal
  to the box's) is **checked after the text branch rather than before it**, because a run is owed
  one level down from that rule — the ordering defect the phase found in its own builder, described
  in §8.

## 5. Glyph generation, and who prepares the face

**Since 12.16 the shaping is at the recording end, not here.** The recorder is handed a face
(`PaintContextSetTextFace`) and a font table, asks RoboText for a glyph run — `RoboTextGlyphRunGenerate`
over the run's scalars and the face — and **stores** what comes back: the ids, placements, advances,
clusters and source ranges in the list's glyph pool, and the face as a token. The drawing pass then does
the four things a text layer may own: resolve the face from the token (`PaintTextRunFace`; `Primary` is
the pass's own `face` argument, an assigned token resolves in the list's table, a miss is counted
`noFont`), set the pen at the run rectangle's left edge and the baseline at `rect.y + baseline_offset`
(both layout's answers), draw each stored id at `pen + offset` with `GfxRasterDrawGlyph` at the resolved
pixel size in the run's colour, and advance the pen by `advanceX`/`advanceY`. **Positioning is already in
the offset** — GPOS did the work and the rasterizer must not reinterpret it — and the mode selected is
Phase 12.1's coverage antialiasing.

Three consequences worth naming, because each used to be this pass's job:

- **The advance is a device quantity and comes from the same generator the measurer runs.** The pen here
  and the width `RoboLayout` justified to are sums of the same numbers, which is what removed the
  admitted fraction-of-a-pixel disagreement between "measure with RoboFont's `hmtx` in font units" and
  "paint with a `float` pen". `RoboTextFontUnitsToDevice` is that one scaling door.- **A glyph flagged `missing` advances like any other.** RoboText gives an unmapped scalar glyph 0's own
  advance and counts it; the pass does not re-decide, so undrawable text measures as wide as its ink and
  `glyphs_missing` is RoboText's count rather than a second opinion.
- **There is one face per recorder today**, and both passes get it from the same caller: the recorder
  registers it (`PaintFontTableAdd`, idempotent) and the drawing pass resolves the token through the
  **display list's own copied** table. `PaintFontTokenPrimary` is the degenerate case — one face, no table —
  and it is *provably* the same object on both sides because the same caller supplies both. A real
  multi-family recorder is a later phase's; the token vocabulary is what it lifts with.
- **A face whose cmap will not parse is still not refused.** At worst every lookup misses, every glyph is
  flagged `missing`, and `glyphs_missing` names the fact — the same answer an unmapped character gets, so a
  broken font degrades to the shape of a font with no glyph for the text rather than to a failed pass.

## 6. Counters

Two layers, and each is an identity rather than a remainder.

**The traversal** (`RoboPaintContext.h`):

| Counter | Meaning |
|---|---|
| `text_runs` | text objects the walk reached |
| `texts` | runs that produced a record |
| `text_skipped` | runs that produced none — an unresolved colour, no readable characters, or a box nothing placed |

with `text_runs = texts + text_skipped` by construction, so *"this document has text and none of it
was painted"* is a number a reader sees rather than a silence.

**The pass** (`RoboPaintDecoration.h`):

```text
draws                    = backgrounds + borders + texts
draws                    = filled + contextual + transparent + empty + no_font
commands                 = draws + skipped
```

`no_font` is the reason that is about the **call** rather than the document: a text command whose record
carries an **empty glyph span** (the recorder was given no face), or whose token does not resolve in the
display list's table, is counted and skipped — so a caller that records with no font paints a textless
document identically and is *told* about a document with text instead of being handed a silently blank
frame. `glyphs` counts the glyph draws attempted, `glyphs_missing` the glyphs **flagged `missing`** (an
unmapped scalar, or metrics that would not parse), and `rtl` **how many runs were drawn left to right
from their origin** — the resolved glyphs are placed, and the visual reordering a fully LTR-run consumer
needs is 12.8's answer read through the run list.

## 7. What the numbers do not claim

- **Shaping exists, and happens once.** GSUB, GPOS, ligatures and kerning run at **record** time; the
  stored advances and offsets are their results, so the pen here adds exactly what was measured. What is
  still absent is a glyph *cache*, an atlas, batching across compatible runs and subpixel-phase-keyed
  rasterization (`shapedTextRecording` names each).
- **No reordering in the pass.** A run's glyphs are drawn in the run's own logical order at the positions
the shaper stored. The visual order of *runs* is 12.8's answer, read through the run list; an RTL run is
  drawn from its origin and counted (`rtl`), and a consumer that needs the resolved visual order reads
  `GlyphRunList.visualOrder` rather than reversing a glyph array.
- **No line breaking here.** The runs painted are the runs RoboLayout measured. A text node is
  measured **as a unit** in this build, so a run wider than its line wraps whole rather than being
  split (`RoboLayoutInline.c`); two line fragments therefore mean two runs, and
  `paint.text.wrapped_lines` says so with two participants in one inline context.
- **No whitespace collapsing, no fallback fonts, no `text-decoration`, no `text-shadow`.** A run
  marked non-paintable by layout is simply not reached.

The boundary the shaping goals filled arrived as predicted, and 12.16 moved *when* it runs:

```text
11.6          text run → the font's cmap, per code point, in logical order → paint
12.4–12.15    text run → RoboText → a *shaped* glyph run → paint, ON EVERY REPLAY
12.16         text run → RoboText → a shaped glyph run → [RECORDED] → paint the stored ids
```

The placeholder `RoboTextGlyphGenerationCmapPerScalar` is gone: GSUB, GPOS and kerning are the
`RoboTextGlyphRunGenerate` body, and the record now holds their output. The last step the pre-12.16 text
predicted — "the record a shaper will fill in … with `glyphs_missing` becoming a shaping result" — is
this phase's `PaintGlyph` and its `missing` flag.

## 8. Findings the phase recorded

Three, each of which was a document or a test that was *asserting the wrong truth*:

1. **A structural check ordered before the rule it exempts is unreachable.** The builder's text
   branch was written after the generic "a record's depth is its innermost box's" check — which a
   run fails by construction, since a run is owed one level *down* from its parent box. Every
   document with text was refused with *"a record's depth is not its innermost box's"*, and the text
   branch below it could never run. The fix is an ordering, not a rule: the run's own checks come
   first, and the generic checks follow for every other kind. It is worth recording because the
   message named the rule the record was *supposed* to break.
2. **A pixel oracle has to follow every drawing kind.** 11.4's oracle consulted backgrounds alone;
   11.5's taught it the bands; this phase taught it the runs. The oracle now follows the same
   one-line predicate the pass itself uses (`PaintDisplayCommandKindDraws`), which is what
   keeps "every fill's centre is its own colour" true when three different kinds of drawing overlap.
3. **A synthetic face needs its cmap prepared before the first lookup, and a hand-written format-4
   table needs exactly the bytes the format says.** The suite's cmap wrote one segment too many for
   two segments, which shifted `idRangeOffset` onto the wrong word; the parser read a plausible
   glyph array out of the extra bytes and every lookup resolved to a glyph id past `maxp`'s
   `numGlyphs`, so the pass reported `INVALID_FONT` per code point and drew nothing. Both halves are
   recorded because they are the two ways a font test passes for the wrong reason: the face is never
   prepared, or the table is *nearly* right and the misses look like a rendering bug.

## 9. Debug dump

A record dump and a display dump each append **one thing** the record's own writer cannot produce,
and it is the one thing that is not inside the record: a run's characters. A span is two numbers,
and the characters belong to the pool the list owns — so the door that *has* the pool appends
`text="..."`, bounded and escaped, to the line the writer produced:

```text
#0004   text           node=2 depth=1 clip=visible/visible border=1,1,16,12 color=#ff0000ff code_points=2 baseline=12 font=16.00/400/upright/ltr text="Hi"
```

```text
#0004   draw-text        node=2 depth=1 clip=visible/visible rect=1,1,16,12 color=#ff0000ff text="Hi"
```

The **record** prints its span rather than the characters — `code_points=2` — because
`PaintOperationWrite` is handed one record, and a record carries two integers naming
characters in a pool it does not have. The two **list** dumps have the pool, and the characters are
where a reader wants them anyway, because a list is a view of a whole document rather than of one
line of it.

The characters are read back **through the pool's own door**, and the door validates the span
against the pool before printing: a span that no longer named characters prints **no** `text=` at
all rather than bytes that are no longer the run's.

The **glyphs** are not on that line — the record's line is already long and its bound is derived
from the text maxima — so a reader who wants the shaped ids, clusters and advances reads them
through the list's own pool (`PaintListGlyphPool`, `PaintDisplayListGlyphPool`, then
`PaintGlyphPoolRead`), and the face through `PaintFontTableWrite`. The line writer names the run's
characters, the baseline and the font inputs; the pool doors name everything else. The quoted text is escaped (`\"`, `\\`, and
`\uXXXX`/`\UXXXXXXXX` for anything outside printable ASCII) and truncated with a trailing `...` at
`RoboPaintDumpTextMax` / `RoboPaintDisplayDumpTextMax` (16 code points each), because a
line-max that had to hold for a run of **any** length would not be a bound — and both line maxima
are **derived** from those two rather than guessed.

## 10. What is absent, on purpose

Line breaking, whitespace collapsing, font fallback, variable-font instancing, `text-decoration`,
`text-shadow`, text selection, text editing, transforms, `border-radius`, gradients, a glyph cache or
atlas, batching across compatible runs, a multi-face recorder, display-list replay and rasterisation
architecture, compositor, scrolling, JavaScript and networking. **Shaping left this list at 12.16** —
it is `RoboTextGlyphRunGenerate`, run once at record time — and the bidi algorithm's *ordering* is read
from the run list rather than implemented in the pass.
Three things left this list after it was written, and all are recorded here rather than deleted
quietly: paint invalidation (11.11, which reads a `TEXT` record's rectangle like any other drawing —
a run's change is a `TEXT` region covering its **advance box**, and a run added or removed is a
`CONTENT` one), **image painting** (11.7 draws a document's pictures through the same pass, with a resource
where a run has characters -- `Images.md`), and the element's **strength**, because since 11.9 a run
is stroked at the opacity in force, passed in by the traversal rather than read from the run's own
style — the run's box is the element that owns it and that element already opened the scope, so a
run reading its own declaration would square it (`Opacity.md` §7, `paint.opacity.text`). The same
pass since 11.8 bounds a run by the clip scopes in force, and 11.10's hit test answers about text
without a line of this document changing.
`RoboPaintDrawingImplemented` stays **1** and `raster` is **1** since 11.12: four kinds of drawing
exist and text is one of them, and the run reaches pixels through **RoboRender's** run over the list —
while the glyphs themselves are still stroked by RoboGraphics' raster, from a face the **caller**
supplies, which is the part 11.12 did not change. Since 12.16 that face reaches the pass through the
record's token and the display list's own font table, so the raster's walk needs **no face argument of
its own** — the run names its own face. `PaintBridge.md` is that contract.

**Since then:** 11.7 landed as the phase this section predicted — the fourth drawing kind and the
second non-numeric payload — and it answered the question it was left with in favour of a
**reference to a decoded surface** (`RoboPaintImage.h`), retained rather than copied, with the run's
pool left exactly as it was (`Images.md`). The four goals after it (11.8 clipping, 11.9 opacity,
11.10 hit testing) were written first; the order the work landed in is recorded in `PROGRESS.md`.
