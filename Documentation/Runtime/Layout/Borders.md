# Borders (Phase 10.11)

The third inset, and the phase that finishes the box model.

10.9 put four numbers **outside** a box and 10.10 put four numbers **inside** it. This
phase puts four numbers **in the box itself**, between those two layers:

```text
┌─────────────────────────────────────────────┐
│ margin      moves the box, changes nothing  │
│  ┌───────────────────────────────────────┐  │
│  │ border   grows the box and moves its  │  │
│  │  ┌─────────────────────────────────┐  │  │
│  │  │ padding  contents in, and never │  │  │
│  │  │  ┌───────────────────────────┐  │  │  │
│  │  │  │ content                   │  │  │  │
│  │  │  └───────────────────────────┘  │  │  │
│  │  └─────────────────────────────────┘  │  │
│  └───────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
```

A border and a padding do the *same two things* to a box: they grow it and they move
its contents inward. What differs is only where they sit — and that difference is one
number, the edge a paint pass draws its line on. So this phase is a second layer of the
same arithmetic, composed with 10.10 rather than duplicated beside it, and it introduces
exactly one rule that neither earlier layer has.

Files: `Include/RoboLayout/RoboLayoutBorder.h`, `Source/RoboLayout/RoboLayoutBorder.c`.
Suite: `layout/tests/SuiteRoboLayoutBorder.c` (22 cases).

## 1. The one structural decision: a border grows a box and moves its contents, and never moves the box

Three numbers move, and they are 10.10's three, one layer out:

```text
the box's border box      padding box + border, on each axis
        │                 (what LayoutResult::size carries — for the first
        │                  time this field's name is exactly what it holds)

the content origin        the border-box origin + border-left/top (this phase)
        │                 + padding-left/top (10.10) — two terms, composed in that
        │                 order

what is left for an       the containing width less the margins (10.9), less the
`auto` width              border (this phase) AND less the padding (10.10)
```

and one number does not:

```text
result.position           the box's border-box origin — unchanged by any border
```

Downward, one subtraction per layer, each owned by the layer that knows the property:

```text
the containing block's content box, less the margins (10.9)
       │                       -> the space the box's own box may occupy
       │  less the border      -> the PADDING box width     (this phase)
       │  less the padding     -> the CONTENT width         (10.10)
       v
what an `auto` width fills, and what the children are laid out into
```

Upward, one addition per layer, so the result a caller reads is the box a paint pass
can draw without re-deriving anything:

```text
the content box the sizing layer resolved   (10.8)
       │  plus the padding                  -> the PADDING box (10.10)
       │  plus the border                   -> the BORDER box  (this phase)
       v
`LayoutResult::size`
```

CSS's own constraint equation is why the downward direction has a border term at all,
and it is the same sentence 10.10 wrote one layer down: a box with `border: 5px solid`
and no declared width is exactly as wide as its containing block, not 10px wider. That
is `layout.borders.block_auto_width`, and it is the case a build that grew the box
outward fails.

### A door was renamed, and the name was the defect

`RoboLayoutPadding.h` closed with a prediction:

```text
"The doors below are named for what they add so that 10.11 can extend them rather
 than rename them."
```

That was half right, and which half was wrong is worth recording. The **content width**
door extended cleanly: it takes a width and subtracts what it owns, and a caller that
has already subtracted the border hands it a padding box. The **`..._border_box`** door
did not — because it was named for the box it *hoped* to complete rather than for the
box it computed. It returns `content + padding`, which is the **padding box**:

```text
before   robo_layout_padding_edges_border_box(...)   -> content + padding
after    LayoutPaddingEdgesPaddingBox(...)  -> content + padding
         LayoutBorderEdgesBorderBox(...)    -> (that) + border
```

The misnomer was unobservable while a padding was the only inset, because the two boxes
were one number. It became observable the moment a border existed: paint draws the
*border* box's edge and would have drawn it `border-width` too far in. So one name was
corrected and no arithmetic was — the numbers the padding layer produced are unchanged,
and every case 10.10 wrote about them passes with the new spelling. That is the "small
compatibility change strictly required" a phase's brief allows, and the honest reading
of it is that the *name* was the defect rather than the code.

### The order inside the pass

```text
read the declared dimensions
        │
        ▼
resolve the MARGINS                      ← what is left beside the box (10.9)
        │
        ▼
resolve the BORDER                       ← read and used; it distributes nothing
        │
        ▼
resolve the PADDING                      ← read and used; it distributes nothing
        │
        ▼
subtract the border, then the padding    ← the number an `auto` width fills
        │
        ▼
resolve the WIDTH                        ← into the CONTENT box (content-box sizing)
        │
        ▼
walk the children INTO that width
        │
        ▼
resolve the HEIGHT
        │
        ▼
derive the PADDING box, then the BORDER box   ← what `size` carries
```

The border resolves before the padding only because it is the outer of the two — the
two calls are independent, and a reader following the arithmetic down the box wants the
order the box is built in. The *subtraction* order is not a matter of taste, and neither
is the addition: addition commutes, subtraction does not, which is why the suite drives
the downward direction through both layers (`layout.borders.children_get_content_width`)
rather than trusting the upward one to imply it.

## 2. The vocabulary: three width kinds, a borrowed style, a borrowed colour

```text
ABSENT       the side a style declared nothing for. CSS's initial border width is
             `medium`; this build uses a zero for it in geometry and counts it
             nowhere -- the distinction 10.9 added a kind for and 10.10 kept.

RESOLVED     a px length this build can use, NON-NEGATIVE, in
             LayoutBorderWidth::length.

UNRESOLVED   declared, and not usable here: a relative unit, a `calc()`, a negative
             length, or the `auto` the grammar does not allow.
```

Three kinds and not four, for the reason `Padding.md` §2 gives at length: the shared
classification can *answer* `auto`, the CSS grammar has no `border-width: auto`, and
folding it onto `UNRESOLVED` is the honest report of a style that broke its own grammar
rather than a fourth kind invented for it. `ABSENT` is `0` so that a cleared record
holds the side's initial state, and `LayoutBorderWidthIsValid` is where the one
check this vocabulary has and the margin's does not lives: a border width is **never
negative**. `border-width: -5px` is invalid CSS, so a record holding one is not one this
module's reader produced, and the moment to see it is before a size is derived from it.

The style and the colour are **not** this module's vocabularies and are not restated:

```text
the style    CssBorderStyle -- 9.15's ten enumerators, kept as a typed
             value rather than as the keyword's text

the colour   CssColor -- 9.9's record, carried exactly as it was left,
             `currentColor` included: this layer has a style rather than an
             element, so the layer that *does* have one resolves a contextual
             colour, and copying channels here would be inventing a second
             colour system
```

That is the third time this module has borrowed a closed vocabulary rather than
restating it (`display` in 10.2, the box side order in 10.9, `position` in 10.13's
future), and it is the same argument each time: a second definition of one vocabulary is
one place for a transposed `border-width: 1px 2px` to hide.

## 3. One policy, and the fourth rule

A padding's used value **was** its declared value; a margin's was a distribution; a
border's is a **decision**, and CSS states it:

```text
1. an `ABSENT` width is used as zero and counted nowhere;
2. a `RESOLVED` width is used as written, and it is non-negative by construction;
3. an `UNRESOLVED` width is used as zero and counted;
4. EITHER WAY, the used width is zero unless the style generates a border.
```

Rule 4 is the phase. `CssBorderStyleGeneratesBorder` is 9.15's own predicate —
its header calls it "the one fact about a style that a later phase must have" — and this
is that later phase, for **geometry** as much as for paint:

```text
border: 5px none        a declared five and a used ZERO -- the box is exactly as big
                        as one with no border declaration at all

border: 5px hidden      the same, and the two styles stay distinct values because
                        CSS's difference between them is a table-collapsing rule
                        this build has no tables for

border: 5px solid       five, and the box is five bigger on that side
```

A build that used the declared width would give a `border: 5px none` box ten extra
pixels of width and a painted nothing to show for them, which is the single most
plausible way to get this phase wrong — `layout.borders.none_style` and
`layout.borders.edges_record` both fail it.

There is **one** resolution door rather than two, and for the reason 10.10 gave about
padding: with no `auto` there is nothing for a block box and an atomic inline to
disagree about, so a second policy would be a second answer with no question behind it.

## 4. The two derived numbers, one clamp, and the counters

```text
LayoutBorderEdgesPaddingBoxWidth(edges, containing_width, &out, diag)
        the containing block less the two HORIZONTAL borders, floored at zero

LayoutBorderEdgesBorderBox(edges, padding_box, &out, diag)
        the padding box plus all four borders, on both axes
```

One axis downward and two upward, because that is how a contained width arrives and a
result holds a size. Both are floored/saturated at `LAYOUT_STYLE_LENGTH_MAX` in 64 bits,
and the saturation is this layer's arithmetic finding rather than a copied idiom: a
padding box at the seam's maximum is a number whose **sum** with a border is not
representable, so a `width: 2147483647px` box with a `border: 5px solid` must clamp
rather than wrap into a negative rectangle. The two clamps compose with 10.10's because
each saturates at the same bound, so a padding box that already saturated keeps the
border it cannot express rather than losing it.

A record this module's own resolver could not have produced — a negative side, counters
that disagree with four sides — is **refused with a diagnostic** rather than floored.
The floor is for a document; the refusal is for a caller bug, and the moment to see one
is before a size is derived from it. `LayoutBorderEdgesIsValid` is the one
place that decision is made, so both doors refuse identically, and it also states the
invariant rule 4 *is*: a used width above zero implies the style draws.

The record's three counters answer three different questions, and the third is the one
only this record can answer:

```text
widths_resolved     a side whose declared width reached this layer, `0` included
widths_unresolved   a declaration this build could not use. Its used value is 0
borders_drawn       a side whose USED width is above zero -- a border that takes
                    room and that a paint pass will draw
```

`borders_drawn` is not derivable from the four widths by a caller asking "how many
borders does this document have", because the zero it must compare against is
`widths_resolved`'s answer too. A document can declare four `border-width: 5px` and draw
nothing; the two counters and their difference are what say so.

## 5. The width keywords, and the one place this build picks a number

CSS defines `thin`, `medium` and `thick` as "implementation-defined but constant", and
9.15 keeps them as **keywords** — the right call, because a used width is a rendering
decision and not a parse. So the number has to come from somewhere to be geometry, and
it comes from here:

```text
#define RoboLayoutBorderThinPx   1
#define RoboLayoutBorderMediumPx 3
#define RoboLayoutBorderThickPx  5
```

which is what the browsers use and what a page designed against them expects. The
shared classification folds every keyword but `auto` onto UNUSABLE — correctly, because
no *box dimension* takes a keyword that names a length — so the width reader consults
the keyword vocabulary **only** on the branch the classification already called
unusable, which is why a `KEYWORD` read costs nothing on the common path.

The alternative — recording the keyword as `UNRESOLVED` and using zero — was rejected
because it would make `border: thin solid` draw *nothing* in every document, and a
keyword the specification defines as a length growing a box by nothing is a worse lie
than a number this build chose and documented. The three constants are asserted as
values by `layout.borders.kind_vocabulary`, so a later goal that changes them changes
one case and three macros.

## 6. Where the border lives when the pass is over

On the **object**, beside the margins and the padding, as a record per side carrying the
used width *and* the style and colour that produced it:

```text
LayoutObjectBorders(tree, id, &out, diag)
```

Geometry needs only the first. A paint pass needs all three at the same moment — it
decides whether to draw, which of the ten styles to draw and in what colour — and it
must not decide them from a style, because "is this declaration one this build could
use" is a question this layer has already answered once. Carrying them costs twelve
bytes a side and removes a second reader of the same declarations, which is the module's
standing rule.

The door refuses the same four things `LayoutObjectPadding` refuses:

```text
an id that names nothing            -- LayoutObjectGet's miss
an object nothing placed            -- a cleared record is legal (no border is CSS's
                                       initial geometry), so it asks the result
a text run                          -- no border applies to it
a transparent inline box            -- its rectangle is the union of its contents,
                                       not a placed size; this build does not inflate
                                       that union, and the header says so out loud
```

and it **clears the output first**, so a caller that ignores the result code reads a
stated absence rather than whatever its stack held. All three of those refusals are
asserted, because a zero with a count of resolved sides would tell a caller the author's
border had been honoured and come to nothing.

## 7. What the suite asserts

`layout/tests/SuiteRoboLayoutBorder.c` — 22 cases in five groups, in the order the phase
was built:

```text
the vocabularies   the three kinds and their names; the width record's validity,
                   its length door and its equality; the side's `generates`
                   question walked over all ten styles rather than a
                   representative; equality that keeps `currentColor` apart from a
                   resolved colour

the seam           twelve longhands read out of a real computed style, the three
                   width keywords becoming 1/3/5, the colour record carried
                   unaltered, `border-color: red blue green black` landing on four
                   sides, `currentColor` surviving, and every declaration this
                   build refuses -- an `em`, a `%`, a negative length, `auto`, a
                   `var()`-flagged px

the used values    the fourth rule, the three counters, the equality that includes
                   them, and every way a hand-built record can fail validation

the two numbers    both directions of the composition, the zero floor, the
                   saturation, the negative-padding-box refusal

the pass           block geometry one side at a time and all four at once, an `auto`
                   width filling what the border left, stacking by the border box,
                   margin/padding interaction, an atomic inline's advance, a
                   transparent inline box taking none, the object door, re-layout,
                   determinism and allocator balance
```

Three of those are load-bearing beyond their group. `layout.borders.block_auto_width` is
the case a build that grew the box *outward* fails. `layout.borders.padding_interaction`
is the case a build that placed contents at `padding-left` and forgot the border fails —
the content origin really is a two-term sum. And `layout.borders.none_style` is the case
a build that used the declared width fails, with both counters asserted beside the
geometry so the *difference* between "four declarations arrived" and "nothing draws" is
visible rather than inferred.

## 8. What the phase changed in the suites that came before it

Four expectations moved and each moved by exactly the border term, which is the
measurement of whether the phase is additive:

| Suite | Was | Now | Why |
|---|---|---|---|
| `layout.block.pipeline` | 210 × 110 | **218 × 118** | the card declares `border: 4px solid black`; 10.10's number was the padding box |
| `layout.conformance.pipeline` | 210 × 110, heights 130 | **212 × 112**, heights **132** | the card declares `border: 1px solid black`; the `<body>` extent is the card's margin box |
| `layout.module.version` | `0.10.0` | **`0.11.0`** | the pin |
| `layout.module.capabilities`, `layout.conformance.capabilities` | 14 fields, 17 implemented | **15 fields, 18 implemented** | the `border` field |
| `layout.module.diagnostic_domains` | 18 domains, padding newest | **19 domains, border newest** | `RoboLayoutDomainBorder` |

Nothing was deleted or weakened. Every one of those is an assertion whose *number* the
phase legitimately moved, and each carries a rewritten comment saying which phase moved
it and why — which is the convention 10.9 established and 10.10 followed: a superseded
expectation is recorded with its outcome rather than silently edited.

The one that deliberately did **not** move is worth naming, because it is the assertion
paying for the box model: `layout.conformance.pipeline` still reads the card's child at
**200**, not 212 and not 188. The card's border box grew; its *content* box did not,
because this build is `box-sizing: content-box` and a declared `width` is the content
box. A build that read the declaration as a border box would give the paragraph 188 and
pass every assertion on the card itself.

## 9. Re-layout, and why it needed no machinery

A border-width change is a changed document, and the module has no invalidation flag for
it because it needs none: every door below is a function of the record it is handed, and
every record on a box is **rewritten by a pass** in the same sweep that clears the
classification. So a re-layout is: change the declaration, rebuild the style, rebuild
the tree against it, run the pass again.

The rebuild step is not optional and is the one thing a caller must remember — the tree
**borrows** one computed style per element for as long as the bridge lives
(`Architecture.md` §6.2), so a replacement that left the old tree standing would leave
it holding a destroyed style. `layout.borders.relayout` does it and asserts the *second*
answer, and it makes the phase's pair visible at once: a width change moves the
geometry, and a **style-only** change (`solid` → `none`, same widths) moves none of it
while still showing up in the counters. That is what makes the two counters earn their
keep.

## 10. What is deliberately missing

```text
border-radius, and any corner geometry
        a border here is four trapezoids' worth of arithmetic reduced to four
        numbers; a corner is a later goal's problem altogether

`hidden`'s collapsing meaning
        `none` and `hidden` are distinct values that answer `generates_border` the
        same way. CSS's difference between them is a border-collapsing rule for
        tables, which this build has no tables for -- so they stay distinct so that
        the day tables arrive the fact is still recorded

percentage border widths
        CSS does not define one for `border-width` at all, so an `UNRESOLVED` width
        here is a relative unit, a `calc()`, a negative length or the forbidden
        `auto`, and never a percentage this build declined to resolve -- which is a
        difference from 10.10's vocabulary worth noting, where a percentage is legal
        CSS and is counted as a declaration the build cannot yet honour

any border on a transparent inline box
        it takes none for the reason it takes no margin and no padding: its
        rectangle is the union of its contents

`border-radius` on a *painted* edge, and painting at all
        Phase 11. `painting` is still 0, and this phase's job was to make the
        record a paint pass reads complete -- used width, style and colour per
        side, decided once

and, since 10.13, what a border DOES to an overflow
        nothing, and that is the phase's own conclusion rather than an omission. A
        border is outside the padding box, and the padding box is the edge an
        overflow is measured from and the edge `overflow: hidden` clips to -- so a
        border changes the *box* an extent sits beside without changing the extent.
        The one thing it does change is the room: a wider border shrinks the padding
        box, so the same child that fitted with no border can overflow with one, and
        `layout.overflow.pass_box_model` asserts the record and the box-model numbers
        side by side so the two cannot be confused for each other (`Overflow.md`
        §10)

visual validation
        the current demo's rendering path paints no borders (11) and does not run
        RoboLayout at all, so a border is invisible in the host checkpoint however
        it is written. The geometry is what this phase owes, and the suite is where
        it is proved; the checkpoint was run and passes unchanged (15/15 probes)
        rather than being extended into a paint test this phase is not allowed to
        write
```
