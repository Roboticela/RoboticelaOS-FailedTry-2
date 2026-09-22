# Text Layout (Phase 10.5)

Where a run of characters becomes a measured participant of an inline formatting
context, and where the one thing this module cannot do for itself — measure a
glyph — is asked for through a seam instead of assumed.

Three headers, and each of them exists because the question it answers is asked by
a different caller: `RoboLayoutText.h` for the model and the seam,
`RoboLayoutTextFont.h` for the RoboFont-backed answer to the seam, and the pass's
own record in `RoboLayoutInline.h`.

---

## 1. The one structural decision: text is a participant, not a special case

The tempting model is that an inline formatting context has two things on it — boxes
and text — and that text needs its own placement rule. It does not, and building it
that way is the expensive mistake this phase could make. A run of text is a
participant of the line in exactly the sense an atomic inline is: it occupies a run
of the cursor, its width is known before it is placed, and once placed it neither
grows nor admits anything. Everything 10.4 established about the inline direction
therefore applies to it unchanged:

```text
10.4:  cursor advances by a participant's width; a participant that does not fit
       starts a new line; a transparent box's rectangle is the union of its contents

10.5:  a text run is one of those participants, and its width came from a font
```

So `layout_inline_place` is called for a text run with a width the text layer
measured, and the inline pass gained *no* text-specific branch. The one place text
appears in the pass is where a width is obtained, and that is a seam
(§4) rather than a rule.

The consequence worth stating: a `<span>` holding only text was zero-wide through
10.4 and is now as wide as its font says. That is not a patch to the inline
algorithm — the algorithm already could place it — it is the measurement finally
existing.

### 1.1 Which is why a text node becomes a layout object

A text node is not an element, has no `display` declaration and generates no box.
It still has to be *in the tree*, because the pass walks boxes and only boxes
(README, "the one structural decision"), and a participant that is not in the tree
is a participant the pass cannot see.

So the vocabulary widened by one — `RoboLayoutObjectText` — and this is the
least expensive place it could have happened. 10.1's record predicted that
formatting contexts would arrive as new object *kinds*; 10.2 showed they arrive as a
vocabulary of their own instead, and the kinds stayed one entry wide through 10.2,
10.3 and 10.4 precisely because `display` already carried the outer/inner pair that
would distinguish a block box from an inline one. A text node is the case that
argument does not cover: it has **no display to derive from**, because it is not an
element. The justification 10.1 wrote down — a second kind is warranted when there
is no property to derive the distinction from — is satisfied here and nowhere else
in the module.

Text objects therefore:

- borrow their **parent element's** computed style. A text node has none of its
  own and CSS says so: inheritable properties come from the parent, and that is
  what `LayoutStyleSourceStyle` already answers for an element.
- derive their display as **inline**. A text run is inline-level by definition in
  this build, and deriving it rather than storing it keeps the object model's one
  rule intact — nothing on an object states a fact the tree already carries.
- carry no box. `robo_layout_object_box` refuses a text object, which is the
  refusal a caller that assumed every object had an element needs to meet.

## 2. Ranges are in code points

`LayoutTextRange` is a `{start, end}` pair of **code point indices**, and
the distinction from byte offsets is the whole reason the type exists. RoboUnicode
is the module's character model (Phase 4), and this module does not get to assume
`1 byte = 1 character` or `1 code unit = 1 user-visible character`. A range that
said "bytes 4..7" would be correct for ASCII and wrong for everything else, and
wrong *silently* — the failure mode is a screenshot with half a word in Arabic.

Two doors bridge the two spaces, and both are on the *text* header rather than the
Unicode one because they are about a **DOM node**:

| Door | Answers |
|---|---|
| `LayoutTextNodeCodePoints` | how many code points the node's text holds |
| `LayoutTextRangeBytes` | the byte span a code point range covers, for a caller that must hand RoboFont or RoboUnicode a buffer |

The range helpers are deliberately small and total: length, emptiness, inversion,
validity against a code point count, and containment. `is_inverted` is separate from
`is_valid` because `{5, 2}` is a *different* mistake from `{0, 99}` on a
three-code-point node, and a caller diagnosing a range wants to know which.

`is_valid` refuses a range whose end is past the last code point. It does not
refuse an *inverted* one — that is `is_inverted`'s — and it is not a repair
function: a range is what it is, and a caller that normalises one has made a
decision the type does not make for it.

## 3. Direction is metadata, not an algorithm

`LayoutTextDirection` has two values, `LTR` and `RTL`, resolved from the
computed style and carried on the run and into the pass's counters
(`LayoutTextStats.rtl`). It is not the bidi algorithm and the header says
so where the enumerator is declared: Unicode's algorithm reorders characters
*within* a run, splits a run into levels, and is Phase 10.5's neighbour in the plan
rather than its subject.

What direction *is* here is the fact a later phase needs before it can reorder
anything, and the reason to establish it now is that it is the one input to a
shaping decision that comes from CSS rather than from the text. A build that
derived direction from the first strong character would answer the same thing on
most documents and disagree with the cascade on exactly the documents where it
matters.

## 4. The measurement seam, and why a pass without one is legitimate

This module can compute geometry for a box without measuring anything — a caret
and a viewport are enough — and it can place text only if something tells it how
wide a run is. The thing that tells it is a **callback**, not a font, not a
RoboFont handle, and not a link-time dependency:

```c
typedef Result (*LayoutTextMeasureFn)(
    void *userdata,
    const LayoutTextStyle *style,
    const char *text, size_t length,
    LayoutTextRun *out_run,
    LayoutDiag *diag);
```

`LayoutTextMetrics` is that callback, its userdata, and the *fallback*
line metrics a caller can supply when it has no glyph metrics at all. A metrics
record with no callback is **not** an error: it is a legitimate way to run the
module, and everything that follows from it is reported rather than hidden.

That is the decision this section exists to state. A run measured by nobody is
**placed with width 0 and counted** — `LayoutTextStats.unmeasured` — and
the case that asserts the zero says so in its own text
(`layout.text.pass_unmeasured`). The alternative, refusing to run without a font,
would make RoboLayout untestable without RoboFont and would make "how wide is this
run?" a question the module cannot answer *about itself* afterwards.

So there are three answers to "measure this", and the type distinguishes all three:

| Situation | Result |
|---|---|
| no source, no callback, or a detached font | `RoboNotSupported`, and the run is counted `unmeasured` |
| a callback that fails | the callback's own `Result`, counted `unmeasured` |
| a callback that answers | a measured run, counted `measured` |

The first row is why `LayoutTextFontMeasure` answers `NOT_SUPPORTED` on a
detached font rather than crashing or answering zero: "I cannot measure" and "this
is zero wide" are different statements, and 10.5 is the phase that has to keep them
apart.

### 4.1 The font-backed measurer, and since 12.4 the RoboText one

`LayoutTextFont` is the module's own *use* of the font layer.

**What it used to be, and why the change is the point.** It mapped code points
through the cmap, summed their `hmtx` advances in **font units**, and scaled the sum
once at the end — the last line of which was a real property (a run's width equals
the sum of its glyphs' widths, which a per-glyph integer conversion would break by
rounding each one) achieved by *restating* an arithmetic that `RoboPaint` also
performed in device floats. The two were allowed to differ by a fraction of a pixel
at a run's end, and a comment in the paint path admitted it.

Since **Phase 12.4** the measurer asks RoboText for a shaped-equivalent run and
sums the glyphs' **device** advances, accumulated in `double` and rounded once at
the boundary (`RoboTextMeasurement.width`). The property is unchanged and it is no
longer a coincidence of two careful restatements: the pen the painter accumulates
and the width this line is justified to are sums of the *same numbers*, produced by
the same `RoboTextGlyphRunGenerate`. The contract is
`Documentation/Runtime/Text/Architecture.md`.

Nothing in `RoboLayout` includes a RoboFont header. The measurer is the one
translation unit that reaches the font layer, and it does so through RoboText's
public headers — a `Source/` file rather than an `Include/`-only API, which is how
the layering stays true while the capability exists.

A scalar the cmap does not map is counted — `glyphs_missing` on the run and in
the pass's record — rather than silently measuring as zero, because "this font has
no such character" is the fact a later fallback phase needs and a zero width is not
it. RoboText answers `.notdef` **with glyph 0's own advance**, so the count and the
width come from one place.

## 5. Style resolution: the four facts a font needs

`LayoutTextStyleResolve` takes a **computed style** and fills the inputs a
measurer needs: family, size, weight, style (upright/italic), and direction. It
reads through RoboCSS's own accessors — the same ones every other consumer uses —
and it parses nothing. There is no CSS tokenizer in this module, no property name,
and no stylesheet (README, "Boundaries").

The ordering matters and is not the order of the parameters: resolution *fails* only
for a NULL style, and every missing property takes the CSS initial value rather
than a guess. A style that never mentioned `font-size` resolves to the initial size,
which is what the cascade would have produced had the declaration been written out.

## 6. The builder's second rule

The builder had one rule through 10.4 — an element generates a box unless its
computed display says otherwise. 10.5 adds the rule for nodes that are not
elements:

```text
a text node generates a TEXT object unless it holds only collapsible whitespace
```

The exception is deliberate and narrow. Whitespace-only text between two elements is
DOM structure rather than content, and in this build nothing collapses whitespace
yet, so generating an object for it would put a participant on the line for every
newline in a formatted document — and the line would then advance by the width of
indentation. `LayoutTextIsCollapsibleWhitespace` is the predicate, and it
is a *module* predicate rather than a suite's because the builder and any later
phase's collapsing rule have to agree about what counts. The builder counts what it
skipped (`whitespace_nodes_skipped`) so that "no text object" and "no text" are two
different quantities.

## 7. What the pass gained, and nothing else

`LayoutBlockLayout` kept its name and its arguments through a second phase
that changed what it does, which is the README's rule rather than an accident: the
two directions are one walk (InlineLayout.md §9), and a `robo_layout_inline_layout`
entry point would have to duplicate it or run it in secret. What changed is that a
text object is now a participant the inline pass measures before it advances the
cursor, and the block pass sees the resulting extents.

The record is `LayoutTextStats`, and its fields are the questions a caller
can actually ask afterwards:

| Field | Question |
|---|---|
| `runs_placed` | how many text objects went onto a line |
| `measured` / `unmeasured` | how many of them had a width from a source |
| `empty` | how many were zero-length (safe, and counted rather than skipped) |
| `code_points` | the characters the pass walked |
| `glyphs_missing` | characters the font had no glyph for |
| `rtl` | runs whose direction came from the style |
| `width_max` / `height_max` | the largest run, which is what a line-box height will need |
| `depth_max` | the deepest text object reached |

The record hangs on the pass's stats beside the inline record
(`LayoutContext.h`), so a pass still has one record per direction and one
more for the thing that feeds the inline one.

## 8. What is deliberately missing

- **No shaping.** No glyph substitution, no positioning, no ligature, no kerning.
  A run's width is the sum of its glyphs' advances, which is what an unshaped run
  is; OpenType is Phase 12.10 onward, in RoboText.
- **No Unicode-level text work in this module.** Since 12.4 the code-point walk, the
  index spaces, the ranges and the glyph records all belong to **RoboText**
  (`Documentation/Runtime/Text/Architecture.md`); this module consumes a
  `RoboTextMeasurement` and keeps the geometry. A UTF-8 walk written here again would
  be the second implementation of the thing that phase removed.
- ~~**No line boxes.**~~ **10.6** made the line a record, and **10.7** gave it a floor.
- ~~**No baselines and no `line-height`.**~~ **10.7 consumed them, and did not
  re-measure — but it did add one thing, and the split is worth stating here because
  it was this record's to state.** The run carries `ascent`, `descent` and a height
  that includes the font's line **gap**; turning that gap into CSS half-leading is
  10.7's (`LayoutBaselineMetricsFromRun`), and the phase takes the gap as a
  *difference* — `height - ascent - descent` — rather than adding a field here. The
  reason is the seam's own scope: its one question is "how wide is this run?", and a
  baseline is line geometry. `line-height` **as a resolved used value** is still
  absent, and remains a later phase's.
- **No Unicode line breaking**, no whitespace collapsing, no bidi reordering.
  Fragmentation is absent, so a run too wide for its line overflows — and since
  10.13 that overflow is *recorded* rather than only happening: a run contributes its
  **region** to its line's owner's overflow measurement, while the object door
  (`LayoutObjectOverflow`) declines a run outright. The reason for the split is
  that a run is not a box: it has a rectangle to contribute and no padding box of its
  own to measure one against (`Overflow.md` §8).
- **No text selection, editing, or painting.** A text object holds geometry, not a
  raster.
