# RoboCSS Backgrounds (Phase 9.14, extended by 18.17)

> **18.17 completes the family this document describes.** It adds `space` and `round`
> to the repeat vocabulary (rows 4 and 5), `local` to the attachment vocabulary
> (row 2), and three properties — `background-clip` and `background-origin` over the
> **box** vocabulary (`border-box`/`padding-box`/`content-box`, which nest, so the
> classifications are inclusions rather than a partition) and `background-blend-mode`
> over sixteen modes in three groups (the identity `normal`, twelve **separable**
> formulas, four **non-separable** ones). §13's "not added" list below is updated to
> match; multiple layers, painting and percentage resolution remain absent.

A background is the first property *family* whose parts are not the same kind of thing:

```css
background: url("Panel.png") 25% 50% / cover no-repeat fixed #336699;
```

Six properties are in that one declaration — a colour, an image reference, a repeat style, a
pair of positions, a pair of sizes and an attachment — and no two of them have the same shape.
The box model's four edges are four of *one* value, which is why its shorthand is a positional
mapping; a background's components go to whichever longhand their **kind** names, and that
classifier is the whole reason this is a module rather than another row in the box's.

Its siblings are `docs/BoxModel.md` (9.11 — the family whose shorthand *is* positional),
`docs/Display.md` (9.12) and `docs/Positioning.md` (9.13 — the two closed vocabularies whose
grammar the repeat and attachment sets join).

```
CSS text
   ↓  tokenizer (9.2)                 `no-repeat` → IDENT, `25%` → PERCENTAGE, `#336699` → HASH
   ↓  grammar (9.3)                   declaration → component-value tree
   ↓  cascade (9.5) + specificity (9.6)   the shorthand and the six longhands compete here
   ↓  inheritance (9.7)               the six are non-inherited; `inherit`/`initial`/`unset` resolve here
   ↓  RoboCSSBackground               the vocabularies, the two pairs, the classifier
   ↓  computed values (9.8)           typed BACKGROUND_POSITION / BACKGROUND_SIZE, or a colour,
   ↓                                  a keyword, a preserved reference, or INVALID with a reason
   ↓  paint (a later phase)           the image, the box it tiles over, the pixels
```

| Goal | What it delivers |
|---|---|
| 9.14.1 the repeat vocabulary | Four values as a closed set, each row carrying its two axes |
| 9.14.2 the attachment vocabulary | Two values as a closed set |
| 9.14.3 the edge vocabulary | `left`/`right` and `top`/`bottom` as one START/END edge spelled by its axis |
| 9.14.4 the position pair | Two components, one per axis, with CSS's single-component form folded in |
| 9.14.5 the size pair | `auto`/`cover`/`contain` or two components, with `auto auto` folded back |
| 9.14.6 the classifier | Which longhand a component belongs to, decided by its kind |
| 9.14.7 the distribution | One `background:` layer turned into six longhands, with absent parts **reset** |
| 9.14.8 the registry rows | Five new properties: non-inherited, with their standards initial values |

## 1. The six longhands, in the order a serialisation writes them

```c
#define RoboCssBackgroundLonghandCount 6u

/* image, position, size, repeat, attachment, colour */
const char *CssBackgroundLonghandName(size_t index);
int CssBackgroundIsLonghand(CssSlice name);
const char *CssBackgroundShorthandName(void);   /* "background" */
```

The order is load-bearing and is written once. `CssBackgroundExpand` fills an array
indexed by it, and it is also the order CSS itself serialises `background` in — so a table
whose order was arbitrary would be a table two readers could disagree about.

The names are spelled here rather than derived, because the registry's rows and this table are
the two places that must agree about them and this is the one that can be *walked*:
`css.background.registry` and `css.dom.background.pipeline` both walk it, which is what makes a
row written against the wrong longhand a failure rather than a silent mismatch.

## 2. The repeat vocabulary, and why `repeat-x` is a row

```c
typedef enum CssBackgroundRepeat {
    RoboCssBackgroundRepeat = 0,      /* both axes — the initial value */
    RoboCssBackgroundRepeatX = 1,    /* horizontally only */
    RoboCssBackgroundRepeatY = 2,    /* vertically only */
    RoboCssBackgroundNoRepeat = 3    /* neither axis */
} CssBackgroundRepeat;
```

`repeat-x` is not a fifth behaviour. It *is* "tiles horizontally, does not tile vertically", so
each row carries the two **axes** rather than a flag beside a spelling:

```c
int CssBackgroundRepeatRepeatsOnAxis(CssBackgroundRepeat repeat,
                                               CssBackgroundAxis axis);
```

This is the same shape the display vocabulary uses for its outer/inner halves and the
positioning one for its flow/positioned columns: the question is asked of the row, so a value
added later (`space`, `round` — CSS's other two) answers for itself and no function below
compares a spelling to decide anything. **18.17 added them as rows 4 and 5**, exactly as this
paragraph predicted: both tile on both axes *like* `repeat`, and they differ from it in a
fourth and fifth column — `space` distributes the leftover as gaps, `round` absorbs it by
stretching — so the axes stay the axes and the new question is asked of its own column. Note also that there is no `NONE` enumerator, so
unlike the display and positioning vocabularies a zero record reads as `repeat` — the
property's *initial* value, which is the safest thing a cleared record could hold and still
not something to read without checking the containing record's kind.

An unknown enumerator answers **zero on both axes**, not the first row's answer: "this tiles"
is a claim a value nothing defined cannot support. The two columns are therefore not each
other's negation.

## 3. The attachment vocabulary

```c
typedef enum CssBackgroundAttachment {
    RoboCssBackgroundAttachmentScroll = 0,   /* the initial value */
    RoboCssBackgroundAttachmentFixed = 1
} CssBackgroundAttachment;
```

Two values at 9.14, and the set is closed. CSS's third, `local`, scrolls with the element's own
content rather than with the page; **18.17 added it as row 2**, and the predicate that gives it
meaning (`CssBackgroundAttachmentScrollsWithContent`) asks the one question that separates it —
which *frame* it moves with — rather than comparing a spelling.

Both vocabularies are rows in the shared closed-vocabulary grammar that `display` and
`position` already use, so their refusals come in the same three sentences and none of that
code is written twice:

| Declaration | Answer |
|---|---|
| `background-repeat: repeat-x` | `repeat-x` — typed keyword, canonical spelling |
| `background-repeat: NO-REPEAT` | `no-repeat` — CSS keywords are ASCII case-insensitive |
| `background-repeat: space` | `not a background repeat value` — an identifier this build has not added |
| `background-repeat: 20px` | `not an identifier` — not an identifier at all |
| `background-repeat: repeat repeat` | `not a single background-repeat keyword` — two components |
| `background-repeat: var(--r)` | carried PRESERVED with the *unresolved* flag |

## 4. The edge vocabulary: a position keyword is spelled by its axis

```c
typedef enum CssBackgroundEdge {
    RoboCssBackgroundEdgeCenter = 0,   /* means the same on both axes */
    RoboCssBackgroundEdgeStart = 1,    /* `left` horizontally, `top` vertically */
    RoboCssBackgroundEdgeEnd = 2       /* `right` horizontally, `bottom` vertically */
} CssBackgroundEdge;

const char *CssBackgroundEdgeName(CssBackgroundEdge edge,
                                          CssBackgroundAxis axis);
int CssBackgroundEdgeSliceIsKnown(CssSlice keyword,
                                            CssBackgroundAxis axis);
```

START/END rather than LEFT/RIGHT, because the axis spells them: `left` and `top` are the
*same edge* and different words. The axis is an argument to both doors rather than ignored,
because it is what decides whether the keyword is a keyword at all — `top` names an edge on the
vertical axis and nothing on the horizontal one. Without that second door, `background-position:
top` in the horizontal slot would be a silent `center`, which turns the top edge into the
top-left corner.

## 5. The position pair

```c
typedef struct CssBackgroundComponent {
    CssBackgroundComponentKind kind;   /* NONE, EDGE, LENGTH, PERCENTAGE */
    CssBackgroundEdge edge;             /* EDGE only */
    CssNumeric numeric;                  /* LENGTH / PERCENTAGE only */
} CssBackgroundComponent;

typedef struct CssBackgroundPosition {
    CssBackgroundComponent horizontal;
    CssBackgroundComponent vertical;
} CssBackgroundPosition;
```

A flat record owning nothing — the same shape `CssNumeric` and `CssColor`
have, and for the same reason: it can be copied, compared and stored without a lifetime rule.

**Both halves are always present**, because both halves are what the value *is*. CSS's
single-value forms are **folded** into the pair during parsing, so a consumer never has to
remember which half was defaulted:

| Declaration | Computed value | Why |
|---|---|---|
| `center` | `center center` | an edge that fits both axes |
| `left` | `left center` | a horizontal edge leaves the vertical at centre |
| `top` | `center top` | a *vertical* edge goes in the vertical slot |
| `10px` | `10px center` | an offset is horizontal; the vertical is left at centre |
| `top left` | `left top` | one edge per axis, in either order |
| `25% 50%` | `25% 50%` | two offsets, horizontal first, in the order written |
| `center 10px` | `center 10px` | a centre and an offset — the one keyword-plus-offset form read here |
| `left 10px` | *refused* | the edge-relative form: a four-component grammar whose order carries the meaning |
| `left right` | *refused* | two keywords on the same axis, leaving the other unnamed |
| `left top center` | *refused* | three components: the count decides the meaning in that grammar |
| `10` | *refused* | a bare number is not a position: `<length-percentage>` has no unitless branch |
| `banana` | *refused* | an identifier that names no edge |

That asymmetry in the single-component form is CSS's own, and it is why the fold is a branch on
which axis the component names rather than the same assignment twice.

Equality is **kind-aware**: `left` and `0px` are the same *place* and are not the same *value* —
one is an edge and one is an offset, and the day something resolves them the difference is the
whole question. `0px 0px` is not `0% 0%` for the same reason at the same magnitude.

### Absolute lengths arrive canonical

`1in` and `96px` are the same length, so the pair holds one of them and it is the reference
pixel. The conversion is the unit module's (`CssUnitIsAbsolute` +
`CssUnitToPx`), taken where the component is read rather than where it is written,
because the write is not the only reader — whoever measures a box reads the pair's numeric
directly.

This is the rule the computed-value layer applies to every length-shaped property, and taking
it here is what stops `background-position: 1in` and `margin-top: 1in` from disagreeing about
what a computed length *is*. A **relative** length keeps its unit and its number (`2em` is not
a length until something says what the font size is), and a percentage is not a length at all.

## 6. The size pair

```c
typedef enum CssBackgroundSizeKind {
    RoboCssBackgroundSizeAuto = 0,     /* the initial value */
    RoboCssBackgroundSizeCover = 1,
    RoboCssBackgroundSizeContain = 2,
    RoboCssBackgroundSizePair = 3
} CssBackgroundSizeKind;
```

`cover` and `contain` are *whole values* rather than components — neither is a width, and a
pair of one of them would be a value the property does not have — so unlike a position this
shape is not always a pair.

A **single** component leaves the height `auto` (CSS's rule), and a pair of two `auto`s is
folded back to the keyword:

| Declaration | Computed value |
|---|---|
| `auto` | `auto` |
| `auto auto` | `auto` — one value, one spelling |
| `cover`, `CONTAIN` | `cover`, `contain` |
| `100px 50px`, `50% 25%` | `100px 50px`, `50% 25%` |
| `20px auto`, `auto 20px` | preserved — `auto` is a flag, not a zero |
| `100px` | `100px auto` |
| `left`, `banana`, `10`, `10px 20px 30px`, `cover contain` | *refused* |

`auto` is a **flag** rather than a third numeric kind: it says "keep the image's own size",
which is a different statement from any number. So `20px auto` differs from `20px 0px` — which
is the distinction the pair exists to keep — and two sizes are equal only when their kinds and
their components are.

One asymmetry is worth naming: a cleared size record reads as `auto`, the initial value, so
`CssBackgroundSizeIsPresent` asks whether the value is *coherent* (a whole-value
keyword, or a pair with a real component in at least one half) rather than whether it is
non-zero, and two cleared records *are* equal. A caller reads a size through the record that
holds it, exactly as it reads a display or a position.

## 7. `background-image`: `none` is a value, everything else is a reference

`none` is typed — it is the property's initial value, and it is what a `background:` shorthand
resets the image to — so it computes to a KEYWORD. Everything else that can be an image is
**carried**:

| Declaration | Answer |
|---|---|
| `none`, `NONE` | `none` |
| `url("Panel.png")`, `url(Panel.png)` | PRESERVED, *unresolved*, tokens intact |
| `linear-gradient(#336699, #ffffff)` | PRESERVED, *unresolved* |
| `var(--image)` | PRESERVED, *unresolved* |
| `banana`, `20px`, `10`, `"Panel.png"`, `none none`, *(empty)* | `not a background image` |

The flag is the whole point of the second answer: a consumer that read the tokens without it
would be reading a reference as if it were an image. This is the same answer 9.9 gives a colour
space this build has no representation for, and a different answer from *invalid* — refusing
legal CSS is worse than carrying it.

## 8. The classifier

```c
Result CssBackgroundExpand(
    const CssValue *value,
    CssValue out_parts[RoboCssBackgroundLonghandCount],
    CssDiag *diag);
```

One `background:` layer, distributed by kind:

| A component that is… | belongs to |
|---|---|
| a colour function, or a colour name, or a hash token | `background-color` |
| any other function, a URL token, `none` | `background-image` |
| a repeat keyword | `background-repeat` |
| an attachment keyword | `background-attachment` |
| a position keyword, a length or a percentage | `background-position` |
| any of the above **after a `/`** | `background-size` |

Each output part is either a *list* holding that longhand's component (or its one or two
components, for a pair) or `KIND_NONE` when the shorthand did not mention it. It is built as a
list rather than as the bare component so the result has the shape a *declared* longhand's
value has — which is what lets it go through the same normalisers with no special case.

`KIND_NONE` is deliberately **not** the initial value. The initial values live in the registry,
and a module that invented them would be a second place they are written down.

Refused, with a reason and **nothing** assigned:

| Shorthand | Why |
|---|---|
| `red, blue` | multiple *layers*: the grammar is a list of layers and each layer is a whole set of longhands |
| `cover`, `contain`, `auto` | a size keyword where CSS's grammar puts one only after a `/` |
| `var(--layer)`, `env(--layer)` | a reference whose substitution can change how many components there are |
| `red blue`, `no-repeat repeat` | two components the same longhand cannot take |
| `10` | a unitless number is neither a position nor a size |
| `banana` | a component that belongs to no longhand |
| `10px /`, `/ 10px`, `10px / 50% / 25%` | a `/` with nothing on one side, or two of them |
| *(empty)* | a shorthand has no components |

All-or-nothing matters: a caller that ignored the result code must not be handed four of a
shorthand's six longhands.

## 9. The distribution *resets*, and that is the difference from the box model

`RoboCSSInherit.c` runs the distribution as a pass of its own, and its third outcome is what
makes it a different machine from the box model's:

- a box edge the author did not write has **no value**, because `margin: 10px` says nothing
  about a fifth edge;
- a background longhand the author did not write is **reset to its initial value**, because
  `background` stands for all six.

That is what makes `background: red` mean "just this" on an element an earlier rule gave an
image:

```css
#reset { background-image: url(a.png); background-repeat: no-repeat }
#reset { background: red }        /* the image is cleared, the repeat is back to `repeat` */
```

And the mirror, where the longhand comes after the shorthand and wins on document order:

```css
#kept { background: red }
#kept { background-image: url(a.png) }   /* the image survives; the rest of the shorthand applies */
```

Which of a shorthand and a longhand wins is asked of the cascade
(`CssCascadeCandidateCompare`) rather than weighed here — the same rule the box pass
follows, and the reason a shorthand and a longhand in different *rules* settle correctly. A
shorthand that cannot be distributed leaves the author's text on the shorthand's own entry and
counts itself as an undistributed shorthand (`shorthands_unexpanded`), so a consumer can see
that six parts are unanswerable instead of finding a declaration and nothing underneath it.

## 10. The registry rows

| Property | Shape | Initial | Inherited |
|---|---|---|---|
| `background` | PRESERVE (the shorthand's own entry) | `transparent` | no |
| `background-color` | COLOUR (9.9) | `transparent` | no |
| `background-image` | BACKGROUND_IMAGE | `none` | no |
| `background-position` | BACKGROUND_POSITION | `0% 0%` | no |
| `background-size` | BACKGROUND_SIZE | `auto` | no |
| `background-repeat` | BACKGROUND_REPEAT | `repeat` | no |
| `background-attachment` | BACKGROUND_ATTACHMENT | `scroll` | no |
| `background-clip` (18.17) | BACKGROUND_BOX | `border-box` | no |
| `background-origin` (18.17) | BACKGROUND_BOX | `padding-box` | no |
| `background-blend-mode` (18.17) | BLEND_MODE | `normal` | no |

Two of the initial values are worth reading twice: `background-position` starts at `0% 0%` —
the top-left as a **percentage**, so it stays relative to whatever box it lands on — and
`background-size` at `auto`, which is not a length.

`background-color` keeps the shape 9.9 gave it, and the reason is not backgrounds: it is the
second property whose value *is* a colour, and without one the rule that resolves `currentColor`
against the element's own `color` would be unreachable and therefore unprovable.

## 11. Non-inheritance, and the hole it leaves

None of the six inherits, and that is CSS's rule rather than a choice: a background is painted
per element, so a child that inherited one would tile its parent's image over its own box.

The consequence is the one 9.12 and 9.13 record: an element that declared nothing has **no
entry** for any of the six, because 9.7 materialises initial values for the inherited-by-default
set only. `css_computed_is(style, "background-image", ...)` on such an element answers
`<absent>` — and the honest answer still exists, through the door that holds the registry:

```c
CssStyleContextQueryComputedValue(context, style,
                                            CssSliceFromText("background-image"),
                                            &value, &from_initial, NULL);
/* RoboOk, value is a KEYWORD reading `none`, from_initial is 1 */
```

An explicit `inherit` goes the other way: it *is* a declaration, so the child has an entry,
the value is the parent's **computed** value, and the provenance flag is `inherited` — not
`declared`, because the declaration said `inherit` and the value came from the parent. `initial`
and `unset` on a non-inherited property are the same thing said in two words: both resolve to
the property's own initial value, and both are flagged `default`.

## 12. Ownership

Nothing in `RoboCSSBackground.h` owns memory. The vocabularies are static tables, and the four
record types (`component`, `position`, `size`, and the parse outcome) are values with no
pointers — so there is no lifetime rule to get wrong and no free to forget.

The two functions that *build* something take a `CssDiag *` and write through it:

- `CssBackgroundExpand` initialises all six outputs **before** it can fail, and on any
  failure disposes all six and leaves them `KIND_NONE` — so a caller that ignores the result
  code owns nothing rather than half a shorthand;
- every writer clears its buffer before anything else, sets `out_length` to what it would have
  needed when the text does not fit, and refuses a NULL argument with `RoboInvalidArgument`.

## 13. What 9.14 does not do

Deliberately absent, and each is a boundary rather than an unfinished edge:

- **Painting.** Nothing draws a background. No image is loaded, no box is measured, no pixel is
  written — the six values are facts a paint phase acts on.
- **Percentage resolution.** `25% 50%` and `50% 25%` stay percentages and `auto` stays a flag;
  only the phase with a box can say 25% *of what*.
- **Multiple layers.** `url(a.png), url(b.png)` is refused with a reason rather than half-read:
  each layer is a whole set of six longhands, and this build's value model holds one.
- **`background-clip`, `background-origin`, `background-blend-mode`.** *Added by 18.17*
  (see the box and blend-mode vocabulary in the header): `border-box`/`padding-box`/
  `content-box`, and the sixteen modes. `background-position-x`/`-y` remain unadded — they are
  two more rows for a later phase.
- **`local` attachment, `space`/`round` repeat.** *Added by 18.17* as rows 2, 4 and 5 — each
  was one row in a table the refusals already read from, which is what the 9.14 text predicted.
- **`image-set()`, `element()`, `paint()`, `color-mix()` in a layer.** Carried as references
  where they are images, refused where they are colours — the answer 9.9 already gives.
- **Fetching.** `url(...)` is a reference this build can hold and cannot follow. No networking,
  no filesystem, no cache.

## 14. Running the suite

```bash
./scripts/robo test-css                                       # the whole RoboCSS surface
./Out/RoboTest.host.exe --filter css.background               # this phase (31 cases)
./Out/RoboTest.host.exe --filter css.dom.background.pipeline   # over the real DOM
```

`docs/Tests.md` says what each asserts; its §8–§18 list the defects the suites across
Phase 9 have found, one section per phase.

## 15. What the phase found

Four things, and the first three were *gaps* rather than mistakes — the phase's own tests are
what surfaced them:

1. **Five computations had no name.** `CssPropertyComputationName` fell to `"unknown"`
   for the five new shapes. A name is what a diagnostic prints, so an unrecognised shape is a
   message nobody can act on.
2. **The parse outcome vocabulary had no doors.** `CssBackgroundParseIsKnown` and
   `CssBackgroundParseName` were declared in the header and had no definition — a link
   error rather than a silent one, because the suite walked the set.
3. **A hex colour in the shorthand was refused.** `background: #fff` classified as "belongs to
   no longhand" while `background: white` did not, because the classifier's HASH case fell to
   the default. Four spellings of one colour reaching one longhand is now asserted directly.
4. **The test that found its own bug.** `css.background.robust` compares two computations of one
   malformed input — and compared `b.kind` against `a.kind` after disposing `a`. Reading a
   disposed record's kind is exactly the class of bug that suite hunts, and the suite caught it
   in itself.
