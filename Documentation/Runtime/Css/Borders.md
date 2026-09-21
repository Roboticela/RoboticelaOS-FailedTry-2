# RoboCSS Borders (Phase 9.15)

A border is written three ways, and all three have to agree:

```css
border-top-width: 1px;  border-top-style: solid;  border-top-color: red;
border-top: 1px solid red;
border: 1px solid red;
```

The second is three longhands spoken once; the third is those three over all four sides. So
there are **twelve longhands** — three kinds times four sides — and no fewer than **four**
declarations can set any one of them:

```text
border-top-width: 5px;        the longhand itself
border-width: 1px;            its positional row      (four sides, one kind)
border-top: 1px solid red;    its side shorthand      (one side, three kinds)
border: 2px dashed blue;      the whole shorthand     (four sides, three kinds)
```

That is what makes this phase unlike its two neighbours. `margin` has one grammar and its
shorthand is a positional mapping; `background` has six grammars and each component goes to the
longhand its kind names. A border has **three** grammars arranged in a **grid**, and the same
cell can be reached from four directions at once.

Its siblings are `docs/BoxModel.md` (9.11 — the family whose table this one's rows live in),
`docs/Display.md` (9.12) and `docs/Positioning.md` (9.13 — two closed vocabularies this one
joins a fifth to), and `docs/Backgrounds.md` (9.14 — the family whose *reset* semantics this one
shares and whose 1/2/3/4 sibling it borrows a distribution pass from).

```
CSS text
   ↓  tokenizer (9.2)                    `solid` → IDENT, `#00ff00` → HASH, `2px` → DIMENSION
   ↓  grammar (9.3)                      declaration → component-value tree
   ↓  cascade (9.5) + specificity (9.6)  four declarers compete for one longhand here
   ↓  inheritance (9.7)                  the twelve are non-inherited; `inherit`/`initial`/`unset` resolve here
   ↓  RoboCSSBorder                      the style vocabulary, the kinds, the classifier
   ↓  computed values (9.8)              typed BORDER_STYLE, a LINE_WIDTH, a COLOR, or INVALID with a reason
   ↓  paint (a later phase)              the four edges, their widths, their joins
```

| Goal | What it delivers |
|---|---|
| 9.15.1 the style vocabulary | Ten values as a closed set, each row carrying whether it draws anything |
| 9.15.2 the kinds | WIDTH / STYLE / COLOUR — the three grammars a border is made of |
| 9.15.3 the twelve longhands | Read from the box table's positional rows rather than spelled twice |
| 9.15.4 the positional rows | `border-width`, `border-style`, `border-color` on 9.11's 1/2/3/4 mapping |
| 9.15.5 the composites | `border` and the four side shorthands |
| 9.15.6 the classifier | What one component of a composite is: a width, a style, a colour, or a reference |
| 9.15.7 the distribution | One owner for the twelve longhands, because four declarations can set one |
| 9.15.8 the registry rows | Fourteen new rows, non-inherited, with `medium none currentcolor` |

## 1. One owner per longhand

This is the phase's central act, and it is a **move** rather than an addition.

9.11 put `border-width` on the box model's distribution pass, with a note that it belonged there
provisionally: a `border-width: 1px 2px` no longhand ever saw would be a declaration no later
phase could consume, and the box table is the table that knows how a 1/2/3/4 list spreads over
four sides. That reasoning was right and the placement was wrong, for one reason:

```c
/* RoboCSSInherit.c — the box pass */
if (CssBorderDistributesShorthand(CssSliceFromText(row_name))) {
    /* The three border rows are table rows here and the border family's to
     * distribute there (9.15). */
    continue;
}
```

A longhand two passes materialise is a longhand with two entries. `border-top-width` is covered
by `border-width` **and** by `border-top` **and** by `border`, and if one pass distributes the
positional row while another distributes the composites, the second pass cannot see what the
first decided — and the cascade is supposed to be the thing that decides, not the order the
passes happen to run in.

So the box pass asks one question before it distributes a row, and the question is not "is this a
positional shorthand" but **"is this row yours or mine"** — which only the border module can
answer:

```c
int CssBorderDistributesShorthand(CssSlice name);
```

**What stayed in the box table, and why it had to.** The rows themselves. The twelve longhand
names are the vocabulary, and a row is where the three-way relation between a shorthand, its four
sides and those names is written down *once* — so `CssBorderLonghandName` reads it rather
than spelling it again:

```c
const char *CssBorderLonghandName(CssBoxSide side,
                                          CssBorderKind kind);
```

Move that and the build would spell `border-top-width` in two places, which is exactly the drift
a single table exists to prevent.

The move also grew the table: 9.11's rows for `border-style` and `border-color` did not exist,
because nothing distributed them. They do now, and because a positional row's index and the kind
it carries are the *same* enumerator — `border-width` **is** the width row — there is no second
table relating the two and therefore no place for them to disagree:

```c
#define RoboCssBorderPositionalCount RoboCssBorderKindCount   /* 3 */
const CssBoxShorthand *CssBorderPositionalRowOfKind(
    CssBorderKind kind);
```

## 2. The style vocabulary, and the trap in `none`

Ten values, in CSS's own order, as a closed set in the same table `display` and `position`
already use:

```c
typedef enum CssBorderStyle {
    RoboCssBorderStyleNone = 0,    RoboCssBorderStyleHidden = 1,
    RoboCssBorderStyleDotted = 2,  RoboCssBorderStyleDashed = 3,
    RoboCssBorderStyleSolid = 4,   RoboCssBorderStyleDouble = 5,
    RoboCssBorderStyleGroove = 6,  RoboCssBorderStyleRidge = 7,
    RoboCssBorderStyleInset = 8,   RoboCssBorderStyleOutset = 9
} CssBorderStyle;

#define RoboCssBorderStyleCount 10u
```

Append-only, for the reason the display and position vocabularies are: an enumerator is a
contract a *stored* value carries, so a new style arrives at the end and never in the middle.

**The one fact a later phase must have** is asked of the row rather than derived from the
enumerator:

```c
int CssBorderStyleGeneratesBorder(CssBorderStyle style);
```

`none` and `hidden` are the two that draw nothing, and a border that draws nothing has a *used*
width of **zero** whatever `border-width` says — so this is the question a paint phase asks
before it measures anything, and it is per-row data rather than `style > HIDDEN`, because the
next style to arrive might be a third that draws nothing.

`none` and `hidden` differ only under border collapsing, where `hidden` outranks every other
style. That is a table-layout rule and this build has no tables, so the two are kept as distinct
values that answer this one question the same way.

**The trap.** `CssBorderStyleFromSlice` answers `RoboCssBorderStyleNone` both for
the keyword `none` and for "not one of the ten", because `none` is a real value and there is only
one enumerator for "nothing". So a *check* may never ask the result:

```c
/* `border-style: banana` must not compute to `none`. */
CssBorderStyleFromSlice(slice);        /* WRONG: 0 for both */
CssBorderStyleSliceIsKnown(slice);    /* RIGHT: 0 only for `banana` */
```

A `banana` that silently became `none` is the most damaging confusion these ten values have — it
is a border that vanishes while every declaration looks like it worked. `css.border.style.none_trap`
asserts both directions.

## 3. The three kinds

```c
typedef enum CssBorderKind {
    RoboCssBorderKindWidth = 0,   /* `1px`, `thin`, `2em` */
    RoboCssBorderKindStyle = 1,   /* `solid`, `dashed` */
    RoboCssBorderKindColor = 2    /* `red`, `#00ff00`, `rgb(0,0,255)` */
} CssBorderKind;

#define RoboCssBorderKindCount 3u
#define RoboCssBorderLonghandCount (RoboCssBoxSideCount * RoboCssBorderKindCount)
```

The order is the order a serialisation writes a composite in — `1px solid red` — because the
classifier's folding and a caller's reading of `out_parts` both follow it, and a set that listed
them differently would silently transpose the two.

## 4. The classifier, and why the order of its questions is the whole logic

One component of a composite shorthand is one of five things:

```c
typedef enum CssBorderClass {
    RoboCssBorderClassInvalid = 0,   /* belongs to no kind: `10%`, `#fff`, a string */
    RoboCssBorderClassWidth = 1,
    RoboCssBorderClassStyle = 2,
    RoboCssBorderClassColor = 3,
    RoboCssBorderClassDeferred = 4   /* `var()` / `env()`: legal, no answer yet */
} CssBorderClass;
```

Four answers for three kinds, because a `var()` is a fourth thing: substitution has not happened,
and a component whose kind depends on a custom property is *carried*, not guessed at.

The classifier is a chain of questions and **the order is the logic**, because three of the five
outcomes are spelled by identifiers that overlap:

```text
1. is it a style keyword?   `none`, `hidden`, `solid`, …      ← first, because `none` and
                                                               `hidden` are *also* words a
                                                               reader might place wrong
2. is it a width keyword?   `thin`, `medium`, `thick`         ← second, because after the
                                                               styles these are the only
                                                               identifiers left that are
                                                               certainly not colours
3. is it a colour name?     `red`, `teal`, `transparent`      ← third, because after those
                                                               two an identifier that names
                                                               a colour can only be one
4. otherwise                `10%`, `#fff`, a string, a block  ← INVALID, with a reason
```

`var()`/`env()` is checked before any of them and answers `DEFERRED`.

**A percentage classifies as a width, deliberately.** `border-width: 50%` is not legal CSS, and
this build computes it to a percentage — 9.11 asserted that on purpose, because 9.8's contract is
that a value layer knows a value's *shape* and not a property's *range*, and the same boundary is
what lets `-1px` through. The classifier follows the registry rather than overruling it: a
percentage is length-shaped, so it is a width. A phase that enforces the range will enforce it in
one place, and it is not this one.

## 5. The distribution: composite shorthands **reset**, positional ones do not

```c
Result CssBorderExpand(
    const CssValue *value,
    CssValue out_parts[RoboCssBorderKindCount],
    CssDiag *diag);
```

`out_parts` is indexed by kind, and each element is either a single-component list holding that
kind's component or `KIND_NONE` when the declaration did not name that kind.

`KIND_NONE` is **not** "leave it alone", and this is where the two kinds of shorthand part
company:

```css
border-width: 1px;      /* says nothing about a border's style — the longhand is untouched */
border: 1px solid;      /* stands for all three kinds on all four sides, so `border-top-color`
                           takes that longhand's INITIAL VALUE (`currentcolor`) */
```

The caller applies the initial value; supplying it here would be a second place the registry's
defaults are written down. This is the same semantics 9.14 gave `background` and the opposite of
9.11's box model, where a box edge the author did not write simply has no value — and a
*positional* row has no such case at all, because one to four components always cover all four
sides and nothing is ever left unnamed in a `border-width`.

**Three refusals**, each leaving every output `KIND_NONE` so a caller that ignores the result
code owns nothing:

- a `var()`/`env()` component, whose substitution can change how many components there are and
  which kind each is;
- more than one component for a single kind — `1px 2px solid` is ambiguous, not a pair, because
  there is no second width to give;
- a component that belongs to no kind.

The author's declaration survives on the shorthand's own entry either way, and the diagnostic
says which of the three it was.

## 6. The cascade decides, not this module

Two shorthands can cover one longhand at different specificities, and which of them the longhand
*is* comes down to the same three keys as anything else. So the pass asks
`CssCascadeCandidateCompare` rather than weighing a level or comparing a specificity
here, exactly as the box and background passes do.

`inherit_expand_border_shorthands` reads four declarers per longhand — the longhand's own
candidate, its positional row's, its side shorthand's, and `border`'s — keeps the best by
`inherit_border_weigh`, and only then decides what the longhand becomes:

```c
best = cascade candidate for `border-top-width` — if any
best = max(best, border-width's candidate)     ← positional
best = max(best, border-top's candidate)       ← side
best = max(best, border's candidate)           ← whole
```

When the winner is a composite, the component for that side and kind comes out of
`CssBorderExpand`; when it is absent, the longhand takes its initial value if the *winning*
declaration was a composite that reset it, and is simply not materialised if the element declared
no border at all — an element with no border has no border longhands, exactly as it has no
margins.

`css.border.cascade`, `css.border.positional.expansion` and `css.border.shorthand.reset` assert
this from both sides, and `css.dom.border.pipeline` asserts it over the real DOM.

## 7. `currentcolor` is an initial value, and the only one that is a *reference*

```text
border-*-width   medium
border-*-style   none
border-*-color   currentcolor      ← four references, not four colours
```

Every other initial value in the registry is a value. These four are the exception, and it
matters: `border-color` is **not** `transparent` the way `background-color` is. A border whose
colour had been resolved at registration time would be a border nobody could see and no author
could explain — `border: 1px solid` with no colour means "the colour of the text", which is the
author's obvious intent and unreachable from a fixed default.

So the reference has to survive to the one layer that has an element, and it does. The pass
resolves what a *style holds*, as it does for any colour; and the query door — the second way a
caller can reach a computed value, for a property nothing touched — gained the matching
resolution:

```c
/* RoboCSSComputed.c — the query door */
if (out->kind == RoboCssComputedValueColor &&
    CssColorIsCurrent(out->color)) {
    CssColor resolved;
    if (CssColorResolveCurrent(out->color,
                                       computed_color_of(style, COMPUTED_COLOR_PROPERTY),
                                       &resolved)) {
        out->color = resolved;
        out->unresolved = 0;
    }
}
```

Nothing else can reach it: `background-color` is `transparent`, which is a colour and not a
reference. What the change buys is that the two doors answer one question the same way — an
element that declared no border colour and an element whose shorthand *reset* the colour to the
initial value have the same computed colour, because they do.

`css.border.initial` and `css.border.longhands.computed` assert both paths.

## 8. The registry rows

Fourteen rows join the table, and the *four* that were already there are the interesting part:

| | rows | note |
|---|---|---|
| already present | `border-top-width`, `-right-`, `-bottom-`, `-left-` | came with 9.11 |
| already present | `border-width`, `border` | 9.11 and 9.1; this phase taught them new values |
| new | four `border-*-style` | |
| new | four `border-*-color` | |
| new | `border-style`, `border-color` | the two positional siblings |
| new | `border-top`, `border-right`, `border-bottom`, `border-left` | the four side shorthands |

That `border-width` and `border` were shapes before this phase and properties now is the same
*silent shape change* 9.13 recorded for `position`: a family is a family of properties, and the
ones this phase taught new values to were already in the table. Nothing was added for them —
their rows changed what they compute.

Counts are asserted, not bounded, so a row added later is a deliberate act:

```
properties                63   (was 49)
inherited                 16   (unchanged)
non-inherited             47   (was 33)
```

**None of the fourteen inherits**, because a border is drawn per element. A child that inherited
one would be showing a border it does not own, at a colour it cannot explain.
`css.border.non_inheritance` proves it from both sides — the child of a bordered parent has no
border longhand, and `border: inherit` on a child produces one.

## 9. What the phase does not do

Deliberately absent, and each a boundary rather than an unfinished edge:

- **Painting.** Nothing draws a border. No edge is measured, no join is computed, no pixel is
  written — the twelve values are facts a paint phase acts on.
- **Geometry.** A `none` border's *used* width being zero regardless of `border-width` is a rule
  for the phase with a box; this build has the two facts (`border-width` and
  `generates_border`) and does not combine them.
- **`border-radius`.** Not a value this build has. It is a whole second geometry, not a fourth
  kind.
- **`border-collapse` and `hidden`'s precedence.** The vocabulary carries `hidden` as a distinct
  value; the collapsing rule that makes it outrank every other style needs tables.
- **`outline`, `box-shadow`, `border-image`.** Properties this build has not added, each needing
  a paint model before it could mean anything.
- **Range enforcement.** `border-width: 50%` and `border-width: -1px` compute, as 9.11 recorded.
  A phase with the property's range enforces it in one place.
- **Per-side positional shorthands** (`border-top-width`'s own 1-value form is the longhand), and
  the two-value `border-top` grammar. The composite's components are classified, not counted.

## 10. Ownership

- Every value this module produces is **by value** and owns nothing beyond the component lists
  `CssBorderExpand` creates, which it also disposes on every failure path;
- `CssBorderExpand` initialises all three outputs **before** it can fail, so a caller that
  ignores the result code holds `KIND_NONE` rather than a partial distribution;
- every string this module returns is a borrowed static — the longhand names live in the box
  table, the style keywords in the style rows;
- `CssBorderStyleWrite` clears its buffer first, sets `out_length` to what it would have
  needed when the text does not fit, and refuses a NULL argument with `RoboInvalidArgument` —
  the same refusal the display and position writers give.

## 11. Running the suite

```bash
./scripts/robo test-css                                     # the whole RoboCSS surface
./Out/RoboTest.host.exe --filter css.border                 # this phase (21 cases)
./Out/RoboTest.host.exe --filter css.dom.border.pipeline    # over the real DOM
```

`docs/Tests.md` says what each asserts; its §8–§19 list the defects the suites across Phase 9
have found, one section per phase.

## 12. What the phase found

Four things. Three were **gaps** rather than mistakes, which is the pattern the previous two
phases also had, and all four were surfaced by the phase's own tests:

1. **The style set had no shape of its own until it was walked.** `border-style` needed a shape
   (there was no existing one: a style is neither a keyword nor a length), so `BORDER_STYLE`
   became the fourteenth computed-value kind — and the *read-back* is what proves it. A table
   whose rows and enumerators had drifted would still answer `is_known` for all ten; only
   reading every keyword back and comparing it to the enumerator it came from makes that visible.
2. **`none` is both a value and an absence.** `CssBorderStyleFromSlice` cannot
   distinguish `border-style: none` from `border-style: banana`, because there is one enumerator
   for "nothing". The suite asserts the trap directly
   (`css.border.style.none_trap`) rather than leaving it to the property layer to rediscover.
3. **The box table had a row that was never distributed.** `border-style` and `border-color` were
   absent as rows because nothing before this phase spread them over four sides — a family whose
   shorthand the table never learned. Adding them is a two-line change *once the table exists*,
   which is the argument for the table.
4. **The distribution had two owners.** 9.11's `border-width` row was distributed by the box
   pass. That was correct in isolation and wrong in a build where `border` and `border-top` also
   cover the same longhands, and the failure mode would not have been a crash or a wrong test —
   it would have been a *last-writer-wins* answer that depended on pass order. Moving it is the
   phase's whole architecture, and the guard is one predicate the box pass asks.
