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
  → RoboPaint TEXT record          (a span into the record list's pool)
  → DrawText command               (a span into the display list's own pool)
  → the face's cmap, per code point, in logical order
  → RoboGraphics glyph outline stroke into a raster
  → the caller's surface
```

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
(`robo_paint_text_pool_contains`, and `robo_paint_text_pool_read` refusing a span it does not hold).

`robo_paint_text_pool_t` is deliberately a type of its own rather than a pair of arrays inside the
paint list, because it has **one implementation and two owners**: the record list owns one, the
display list owns one, and the builder copies each span across with
`robo_paint_text_pool_copy` — the single door between two pools, which moves bytes from one owned
array to another instead of round-tripping through a temporary buffer sized to the longest run in
the document.

The ownership boundary is what makes 11.2's snapshot property hold one stage further along:
**clearing the record list does not change a character the display list draws**, because the
display list's pool is its own. `paint.text.mutation_isolation` pins it by writing the display list
to text, freeing the records, and writing it again byte for byte.

## 2. Code points, not bytes, and not glyph ids

A span counts **code points**, and the choice is the same one `RoboLayoutText.h` already made: a
byte offset stops being a character position the moment a document contains an accent, an Arabic
letter or an emoji — so the unit a run is *measured* in and the unit it is *painted* in are the same
one. `paint.text.characters_are_code_points` pins it with an accented word: four code points, five
UTF-8 bytes, and the span says four.

Deliberately **not glyph ids**, which is the more render-ready choice and the wrong one. A glyph id
is a fact about a *font*: a list that stored them would be a list only that font could draw, and
"does the painter's face agree with the traversal's face?" would be a question nothing could answer.
Storing characters keeps the display list a statement about the **document**, independent of the
font it will be drawn with — exactly as a background command is independent of the surface it will
be filled into. Glyph resolution then happens where RoboGraphics' own bridge says it belongs, at the
drawing step.

## 3. The record

```c
typedef struct robo_paint_text_run {
    robo_paint_text_span_t span;        /* where the characters are, in the list's pool */
    int32_t                baseline_offset;  /* run top → baseline, in px, never negative */
    robo_layout_text_style_t font;      /* the inputs the run was measured with, by value */
} robo_paint_text_run_t;
```

A **sub-record** rather than six more fields on the operation, for the reason `robo_paint_boxes_t`
is one: the facts a text drawing has and a fill does not arrive together, so a record cannot carry
half of them. A background's `text` member is a cleared one, exactly as a text run's `style` member
is `NONE`.

`baseline_offset` is **an offset from the run rectangle's top and not a coordinate**, so the
baseline is at `rect.y + baseline_offset` in paint coordinates and at `baseline_offset` relative to
the run's own top — one number in one space, with nothing to drift. It is the distance RoboLayout's
baseline placement produced (a run's own ascent, so a run's top *is* its baseline minus its ascent)
and it is **read, never recomputed**: 10.7 placed it, and a painter that derived it would be a
second baseline model. `paint.text.record_shape` reads it back through
`robo_layout_baseline_offset` — the door that *says* the two are equal — rather than off the
baseline record's `ascent`, which would be relying on a convention stated as a convention in a
module that is not allowed to know it.

`font` is the resolved text style the run was **measured with**, by value: a pixel size, a numeric
weight, an italic flag and a direction. Carrying the measurer's own record rather than four numbers
re-derived here is what makes *"the size it was measured at is the size it is drawn at"* a copy
rather than an agreement to maintain — and it travels unchanged all the way to the graphics call
that strokes the outline.

`robo_paint_text_run_is_valid` refuses three shapes: an **empty span** (a text drawing with no
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

## 5. Glyph resolution, and who prepares the face

The pass resolves each code point against the **caller-supplied face's own cmap**, in logical order,
and strokes the glyph's outline with `robo_gfx_raster_draw_glyph` at the resolved pixel size, in the
run's colour, advancing the pen by the glyph's own device metrics. The pen starts at the run
rectangle's left edge and the baseline at `rect.y + baseline_offset` — both of which are layout's
answers, so a run is drawn where layout placed it.

A glyph whose metrics will not parse advances by **nothing** and is counted as a miss, which is the
measurer's refusal restated: a broken font that produced a narrower run here than a working one would
pile the glyphs on each other in a way that reads as a rendering bug rather than as a missing table.

**The face's cmap is prepared by the pass, once, before the first lookup.** A `robo_font_face_t` is
created without one — `robo_font_face_lookup_glyph` answers `ROBO_NOT_SUPPORTED` for a face whose
cmap has not been parsed — so the pass calls `robo_font_face_parse_cmap` exactly as the layout
measurer does before it resolves a single glyph (`RoboLayoutTextFont.c`). One font layer builds the
map; this is the layer that says *when*, and it must say so before the first lookup rather than after
it. A face whose cmap **will not parse** is not refused: at worst every lookup misses, every glyph is
`.notdef`, and `glyphs_missing` names the fact per code point — the same answer this pass already
gives an unmapped character, so a broken font degrades to the shape of a font with no glyph for the
text rather than to a failed pass.

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

`no_font` is the reason that is about the **call** rather than the document: a text command with no
face to draw it is counted and skipped, so a caller that passes no font paints a textless document
identically and is *told* about a document with text instead of being handed a silently blank frame.
`glyphs` counts the glyph draws attempted, `glyphs_missing` the code points that resolved to
`.notdef` or whose metrics would not parse, and `rtl` **how many runs were drawn left to right from
their origin** — because there is no bidi in this build, and a count is how it says so.

## 7. What the numbers do not claim

- **No shaping.** Glyph substitution and positioning, ligatures and kerning are absent. A run is
  drawn in logical order at the advances the font's own `hmtx` gives, which is CSS's advance width of
  a run *before* layout reorders it.
- **No bidi.** An RTL run is drawn left to right from its origin and counted (`rtl`).
- **No line breaking here.** The runs painted are the runs RoboLayout measured. A text node is
  measured **as a unit** in this build, so a run wider than its line wraps whole rather than being
  split (`RoboLayoutInline.c`); two line fragments therefore mean two runs, and
  `paint.text.wrapped_lines` says so with two participants in one inline context.
- **No whitespace collapsing, no fallback fonts, no `text-decoration`, no `text-shadow`.** A run
  marked non-paintable by layout is simply not reached.

The boundary Phase 14 fills is already the right shape:

```text
this build    text run → the font's cmap, per code point, in logical order → paint
Phase 14      text run → RoboText shaping → a glyph run → paint
```

and the record a shaper will fill in is this one, with `glyphs_missing` becoming a shaping result
instead of a cmap miss.

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
   one-line predicate the pass itself uses (`robo_paint_display_command_kind_draws`), which is what
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
`robo_paint_operation_write` is handed one record, and a record carries two integers naming
characters in a pool it does not have. The two **list** dumps have the pool, and the characters are
where a reader wants them anyway, because a list is a view of a whole document rather than of one
line of it.

The characters are read back **through the pool's own door**, and the door validates the span
against the pool before printing: a span that no longer named characters prints **no** `text=` at
all rather than bytes that are no longer the run's. The quoted text is escaped (`\"`, `\\`, and
`\uXXXX`/`\UXXXXXXXX` for anything outside printable ASCII) and truncated with a trailing `...` at
`ROBO_PAINT_DUMP_TEXT_MAX` / `ROBO_PAINT_DISPLAY_DUMP_TEXT_MAX` (16 code points each), because a
line-max that had to hold for a run of **any** length would not be a bound — and both line maxima
are **derived** from those two rather than guessed.

## 10. What is absent, on purpose

Shaping, the bidi algorithm, line breaking, whitespace collapsing, font fallback, variable-font
instancing, `text-decoration`, `text-shadow`, text selection, text editing, opacity, transforms,
`border-radius`, real clipping, image painting, display-list replay, rasterisation architecture,
compositor, paint invalidation (11.11), hit testing (11.10), scrolling, JavaScript and networking.
`raster` remains **0** and `ROBO_PAINT_DRAWING_IMPLEMENTED` stays **1**: three kinds of drawing exist
and text is one of them, while the rasteriser is still RoboGraphics'.

**Next:** 11.7 — image painting, the fourth drawing kind and the second non-numeric payload, which is
the phase that has to decide whether an image is a pool of bytes or a reference to a decoded surface.
