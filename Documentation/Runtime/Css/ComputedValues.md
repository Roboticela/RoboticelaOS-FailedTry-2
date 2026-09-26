# Computed values (9.8)

RoboCSS's seventh stage, and the first that **changes** a value rather than
locating one. Inheritance says what an element *has*; computation says what that
is.

```text
DOM element
    ↓  selector matching      (9.4)
    ↓  specificity            (9.6)
    ↓  cascade                (9.5)
    ↓  inheritance            (9.7)
  resolved style                  `10px` is still the token 10px, `red` is still red
    ↓  computation            (9.8, here)
  computed style                  a length of ten pixels in px, a colour, a keyword
    ↓
layout / paint (later phases)
```

Nothing in this stage resolves against the document. A computed value is a
function of **the value's shape and the value**, and the shape is supplied by the
[property registry](Inheritance.md) — which is why the normaliser needs no
context, no parent, and no font metrics, and why the second half of the
milestone is a pure walk.

## Specified, resolved, computed, used

Four words, four different records, and the whole phase is about not confusing
them.

| Term | What it is | Where it lives |
|---|---|---|
| **specified** | what a declaration said: the component-value tree the grammar built, tokens intact | `CssValue`, `RoboCSSValue.h` |
| **resolved** | what the element *has* after cascade and inheritance, still as tokens | `CssInheritedStyle`, `RoboCSSInherit.h` |
| **computed** | what the property's own semantics make of that: a kind, a number, a unit, four channels, or a keyword | `CssComputedValue`, `RoboCSSComputedValue.h` |
| **used** | the number layout actually works with, after the containing block and the font are known | *not here* — a later phase |

`width: 50%` travels through all four and stops at the third: specified is the
token `50%`, resolved is the token `50%`, computed is **a percentage**, and used
is however many pixels that is inside a particular box. A style that turned it
into pixels here would be inventing the box.

## The typed value: a closed set of shapes

```text
NONE         nothing computed (a cleared record)
KEYWORD      one identifier, decoded and lower-cased: `auto`, `normal`, `static`
NUMBER       a bare number: `line-height: 1.5`
PERCENTAGE   a percentage, which stays one because only layout can resolve it
LENGTH       a number and a unit: `10px`, `2em`, `1rem`
COLOR        four 8-bit channels, alpha included
FAMILY_LIST  an ordered font family list
DISPLAY      one of four display values (9.12)
POSITION     one of five position values (9.13)
BACKGROUND_POSITION  a pair: two edges, or length-percentages (9.14)
BACKGROUND_SIZE      two length-percentages, `auto`, `cover` or `contain` (9.14)
BORDER_STYLE one of ten border styles (9.15)
PRESERVED    the specified tokens, carried intact
INVALID      the value did not mean anything for this property
```

It is deliberately *not* "a string, but typed". A representation that can hold
anything tells a consumer nothing, and every consumer downstream of this one —
layout, paint, the inspector — exists to do something specific with a value.

Three things in that set carry decisions a reader should be able to argue with —
two of them kinds, and one a flag no kind can express.

### `PRESERVED` is not "unimplemented"

`width: calc(100% - 20px)` **is** a real computed value in this build: it is the
expression, structurally intact. Reducing it here would be a lie, because `%`
needs a containing block. The same is true of `var(--x)` — a *reference*, not a
value — of a shorthand whose longhands are not expanded yet, and of a custom
property, which CSS *defines* as an opaque token sequence. A representation that
kept the tokens is exactly as computed as one that produced a pixel, and honest
about what it does not know.

### `INVALID` is an answer, not an error return

`color: nonsense` is a value that means nothing for `color`. The operation
succeeds; the computed value says INVALID; a diagnostic carries a short reason
(`unsupported colour function`, `a bare number is not a length`, `rgb() arguments
are out of range or malformed`), and the computed style counts it. Returning a
failure code instead would force every caller to choose between dropping the
property and inventing a value for it — and the returned *reason* is what lets a
diagnostic say which of the two problems it is.

### `UNRESOLVED` is the flag that outlives the kind

`var()`, `calc()` and `currentColor` are all carried `PRESERVED` **and** flagged
`unresolved`, because a consumer that read `var(--x)` as an ordinary preserved
token list would be reading a reference as if it were a colour. The flag is a
fact about the value that the kind cannot express.

## The unit model

> **The vocabulary moved in 9.10.** `CssUnit`, its accessors and the
> conversion factors now live in `RoboCSSUnit.h`/`.c` and this document summarises
> them; `docs/Units.md` is the authoritative account. This header still includes
> `RoboCSSUnit.h`, so a caller of this one reaches them through the include it
> already had. The *rule* below is unchanged — that is the point of the move: 9.10
> added units and a resolution context, and did not change what a computed length
> means.

A length is a number and a unit, and the units split into two groups:

```text
fixed factor → px      px  cm  mm  q  in  pt  pc
kept as written        em  rem  ex  ch  lh  rlh  vi  vb
                       vw  vh  vmin  vmax
                       svw svh svmin svmax  lvw lvh lvmin lvmax  dvw dvh dvmin dvmax
```

The first group converts **here**, exactly: `1in` and `96px` are the same length,
so two computed values that mean the same thing must compare equal, and
`CssComputedValueEquals` can be a comparison of typed values rather than a
comparison of spelling. The second group is kept: converting `2em` in this stage
would produce a number that looks resolved and is wrong on every element with a
different font size. There is no host-display DPI anywhere in the conversion,
because the factors are the CSS-defined ones.

Resolving one of those kept units is `CssUnitResolvePx`'s job, and it is a
*question* with four answers rather than a multiplication: it answers
`NEEDS_CONTEXT` when the context does not carry what the unit needs, which is what
keeps a caller from substituting a default it invented. This layer never calls it —
resolving `2em` needs a font size this stage does not have — and that is why the
unit stays live on the computed record.

`0` with no unit at all is a length (`0px`), which is CSS's one exception; any
other bare number is not, and is INVALID rather than read as a number of nothing.

A property whose value shape is a length accepts a **closed set** of keywords —
`auto`, `none`, `normal`, `medium`, the intrinsic sizes, the font size keywords.
The set is the union of the keyword lists of the properties that share the shape
(`font-size` has `medium`, `letter-spacing` has `normal`, `width` has `auto`),
because this layer knows a value's *shape* and the property's own grammar is the
registry's business. What it will not do is call every identifier a length
keyword: `width: red` is a mistake an author wants told about, not a keyword a
later phase can only fail on.

**`CssUnitIsAbsolute` asks the table, not `CssUnitToPx`.** The
converter answers with the value it was given when a unit has no fixed factor, so
a relative unit and an unknown one both come back looking convertible; the
predicate tests the factor's presence instead. (This was one of the two real
defects the 9.8 suites found — see `Tests.md`.)

## Colours, and the 9.9 seam

This layer does not parse colours. Since 9.9 there is exactly one colour parser in
the subsystem — `RoboCSSColor.h` — and `computed_compute_color` is the translation
from its verdict into the right typed value. That is the whole reason the module
exists: two parsers is how two spellings of `rgb()` come to disagree.

| 9.9 outcome | What this layer produces |
|---|---|
| `RESOLVED` | a `COLOR` value holding the four channels |
| `CURRENT` | a `COLOR` value whose colour is the `CURRENT` kind, flagged `unresolved` |
| `DEFERRED` | the tokens, PRESERVED and flagged `unresolved` |
| `MALFORMED` | `INVALID`, with a reason naming the function |
| `UNKNOWN_NAME` | `INVALID`, with `unknown colour name` |
| `INVALID` | `INVALID`, with `not a colour` |

The colour forms are therefore all of CSS's ordinary ones — the 148 standard
names, `transparent`, all four hex lengths, `rgb()`/`rgba()` and `hsl()`/`hsla()`
in **both** syntaxes, with clamping rather than wrapping and hue normalised rather
than clamped. The per-syntax rules, the named table and the six outcomes are
`docs/Colors.md`'s subject, not this document's.

### A reference is settled before the colour parser is asked

A `var()` reference is settled **before** the value reaches this layer: the
substitution pass (18.1, `docs/Variables.md`) replaces it with the referenced
value, so by the time a colour is asked to make sense of `color: var(--brand)`
the tokens it sees are `--brand`'s value and not the reference. What is left for
this layer is the *reference it cannot substitute* — `env()`, `calc()`, `min()`,
`max()`, `clamp()` — and for those `computed_compute_color` asks one question
first: is this value a single unresolved function? If so it carries the tokens and
flags them `unresolved`, exactly as every other value shape does. A custom
property is never *interpreted* here either: a `--brand: #336699` keeps those five
tokens, and only a reference to it becomes the colour they spell.

`currentColor` is carried as a `COLOR` value with the `UNRESOLVED` bit set —
*typed*, not preserved as tokens — and **9.9's computed style resolves it**. The
resolution belongs there because it is the only layer with both the element and the
parent: on `color` itself `currentColor` is the parent's computed `color`, and
everywhere else it is the element's own. `docs/Colors.md` §5 has the two rules and
the reason `color` is resolved first.

## CSS-wide keywords

`inherit`, `initial` and `unset` are consumed by **inheritance** (9.7), on the
parsed value and never on a string, so by the time a value reaches computation
the keyword has already been turned into the value it meant — the parent's, the
initial one, or the same choice made according to the property's
`inherited_by_default`. Computation therefore never sees one.

`revert` is the exception and stays one: rolling it back means re-running a
cascade with one origin removed, so it is carried as a **deferred** entry with
`RoboCssComputedFlagDeferred`, counted, and reported. Obeying half of it
would be wrong in exactly the cases it exists for.

## The registry's new column

`CssPropertyDefinition` gained one field — `computation`, the *shape* of
the property's declared value — and it is what makes the normaliser a function
rather than a lookup table keyed by property name:

```text
PRESERVE       carry the tokens                    background, border, margin,
                                                   padding, border-width, custom
COLOR          named / hex / rgb() / transparent   color, background-color
LENGTH         dimension, percentage, zero, keyword
                                                   width, height, font-size,
                                                   letter-spacing, word-spacing, text-indent
KEYWORD        one identifier                      text-align, visibility,
                                                   cursor (9.7)
FONT_FAMILY    identifiers and strings, comma-separated
FONT_WEIGHT    a number 1–1000 or `normal`/`bold`
LINE_HEIGHT    `normal`, a number, a length or a percentage
EDGE_OFFSET    a length, a percentage or `auto`     margin-* (9.11), top/right/
                                                   bottom/left (9.13)
EDGE_INSET     a length or a percentage            padding-*     (9.11)
LINE_WIDTH     a length or thin/medium/thick       border-*-width (9.11)
DISPLAY        one of a closed vocabulary          display       (9.12)
POSITION       one of a closed vocabulary          position      (9.13)
Z_INDEX        `auto` or a whole number            z-index       (9.13)
BACKGROUND_REPEAT     one of a closed vocabulary  background-repeat     (9.14)
BACKGROUND_ATTACHMENT one of a closed vocabulary  background-attachment (9.14)
BACKGROUND_IMAGE      `none` or a kept reference  background-image      (9.14)
BACKGROUND_POSITION   a pair of components        background-position   (9.14)
BACKGROUND_SIZE       a pair, or cover/contain     background-size       (9.14)
BORDER_STYLE   one of a closed vocabulary          border-*-style (9.15)
```

`FONT_WEIGHT` refuses `5000` rather than clamping it — 5000 is not a heavier
1000, it is a mistake — and `LINE_HEIGHT` keeps a multiplier (`1.5`) distinct
from a length (`20px`) from a percentage (`150%`), because a line box needs the
difference.

Adding the column did not change the registry's data: every initial value was
already text, and the registry already parsed each into a component-value tree at
registration time. One field, one switch, and — after 9.15 — nineteen shapes.

### Why 9.11 added three more

`auto` is a margin and is not a padding. `medium` is a border width and is not a
margin. `min-content` is a width and is neither. The three box-model shapes differ
from each other, and from `LENGTH`, by **vocabulary** rather than by syntax — which
is the same reason `FONT_WEIGHT` and `LINE_HEIGHT` have always been separate shapes
rather than one "a number or a keyword" shape.

The three share one body (`computed_compute_length_grammar`) and differ only in the
grammar they are handed: a keyword list, its length, and the sentence to use when an
identifier is not in it. That is what makes `padding: auto` an **INVALID** computed
value rather than a keyword a later phase would have to interpret, and the suite
asserts it from both ends — `auto` is a keyword under `EDGE_OFFSET` and INVALID under
`EDGE_INSET`.

```text
                   length  percentage  auto  thin/medium/thick  min-content
EDGE_OFFSET          yes       yes      yes         no               no
EDGE_INSET           yes       yes      no          no               no
LINE_WIDTH           yes        no      no         yes               no
LENGTH (9.8)         yes       yes      yes*        yes*             yes
```

`*` — `LENGTH`'s list is a **union**, and deliberately: the properties it serves
(`width`, `height`, `font-size`, `letter-spacing`, `text-indent`, `word-spacing`)
genuinely share it, and no box-model family needed a narrower cut of it. Making it
exact per property is a change with a real cost and no reader yet; the three 9.11
shapes are exact because there the difference *is* the property family.

The box longhands are also where the two halves of a phase meet. A distributed
longhand is an *ordinary declaration* by the time this layer sees it — the expansion
happened in inheritance and went through the same keyword handling a written
declaration does — so `margin: inherit` and `margin-left: inherit` compute identically
without the value layer knowing either was a shorthand. Expanding later would have
meant expanding *computed* values, which is the one thing this layer must not do: a
computed `padding: 8px 12px 4px` is a `PRESERVED` value, and splitting it here would
be interpreting a value the property never asked it to.

`docs/BoxModel.md` has the full account of the family itself.

### Why 9.11's shared shape changed its diagnostic in 9.13

Not a shape that was added but a message that had to move. The box model's three
length shapes differ only in the vocabulary their grammar carries, and since 9.11
they have shared one case body. The `EDGE_OFFSET` refusal read *"not a margin length
or auto"*, which was accurate while `margin` and its three relatives were its only
row. 9.13 put `top`, `right`, `bottom` and `left` on the same shape — on purpose, so
that the two families could not normalise a percentage differently — and from that
moment the message named one of its two families while the other was being refused
by it. Because the shape is shared, the property's identity is not available where
the message is written, so the honest fix was to describe the grammar: *"not a
length, a percentage or auto"*. This is the shape of defect a shared shape invites,
and the suite caught it in the *box* suite while the box's own behaviour was correct.

### Why 9.12 added one more, to a row that already existed

`display` was a registry row before 9.12 — non-inherited, initial `inline` — and it
carried the **generic `KEYWORD` shape**, which passes any identifier through:
`display: bogus` computed to a keyword whose text was `bogus`. That is the right
shape for a property whose vocabulary belongs to a phase that has not landed
(`cursor`, `position`), and it is the wrong shape for this one. `display` is the
property *every* later phase reads on *every* element, so a value that means nothing
has to be a mistake rather than an identifier somebody downstream fails to
interpret — and the failure mode of the generic shape is silent: it is discovered in
whatever phase first dispatches on the value.

The row therefore kept its classification and its initial value and gained a shape.
That is what the shape is *for*: the registry did not have to re-decide anything, the
column did. `display: flex` is now `INVALID` with a reason rather than a keyword
named `flex`, which is the same trade `padding: auto` makes — refusing is the honest
answer until the phase that implements it exists.

### Why 9.13 added two, and one of them is the first numeric shape

`POSITION` is the second closed vocabulary, and it joins `DISPLAY` as a **row in the
same table** rather than a second copy of "ask the identifier, then ask the table":
the vocabulary, its two doors and its refusals are data, so a third closed vocabulary
— `float`, `overflow`, `visibility` — is a row. `position` was, like `display`, a
`KEYWORD` row before this phase, and it moved for the same reason: `position: banana`
was a keyword carrying the text `banana`, and `position` is a value a layout pass
dispatches on before any other.

`Z_INDEX` is different in kind, and it is the first share whose grammar is about a
*number* rather than a keyword. Its vocabulary is two answers:

```text
auto              the KEYWORD — and not zero, since "takes no part in its parent's
                  stacking decisions" is a different statement from "at level zero"
an integer         a NUMBER with `is_integer` set
```

and everything else is `INVALID` with a reason. The refusal worth reading twice is
`0.0`: it has the same magnitude as `0` and is a **different token**, and CSS's
`<integer>` grammar is about the token. Accepting it would produce a numerically
correct value from a build that could not tell a whole magnitude from a fractional
one, which is the failure mode the `is_integer` flag exists to prevent — and it would
be silent. `1.5`, `1e2`, `red`, `10px` and `10%` are refused beside it, each with the
message that says which of those it is.

Nothing here resolves anything: `top: 25%` stays a percentage because it needs a
containing block, `z-index` is never compared because a stacking context is a later
phase's, and `position: sticky` is a value rather than a behaviour.

### Why 9.14 added five, and two of them are the first *pair* shapes

The background family is six longhands and five shapes, because
`background-color` already had one — the colour shape from 9.9, reused rather than
re-derived — and the other five do not share a grammar:

```text
background-repeat      one of four values                BACKGROUND_REPEAT
background-attachment  one of two values                 BACKGROUND_ATTACHMENT
background-image       `none`, or whatever `url(…)` made  BACKGROUND_IMAGE
background-position    two components                    BACKGROUND_POSITION
background-size        two components, or cover/contain   BACKGROUND_SIZE
```

The two vocabularies are **rows in the same closed-vocabulary table** `DISPLAY` and
`POSITION` use, so the machine that asks an identifier, then asks a table, is data
rather than a third copy of itself. The two pair shapes are new in kind, and the
decision they encode is that the pair is stored **folded**:

```text
background-position: center            two components — center, center
background-position: center center     two components — center, center
```

One spelling arrives short and means the same thing. Folding happens once, at parse
time, so the computed value has one representation rather than two, and the equality
the style layer will eventually use does not have to know the shorthand had an arity.
A component keeps its own kind — an edge keyword, a length, a percentage — and the
comparison is kind-aware on purpose: `left` and `0px` are equal as offsets and
**different as values**, because the phase that resolves them against a box is
entitled to two answers. `cover` and `contain` are *not* `auto`: they size an image
rather than describing a length, and they are refused a percentage beside them the
way `EDGE_INSET` refuses `auto`.

Nothing here paints: `50%` stays a percentage, `url(…)` is never fetched, `none` is a
value rather than a missing image, and `background-attachment: fixed` is a keyword
with no scrollport to fix against.

### Why 9.15 added one, and it is a vocabulary

A border is three kinds over four sides, and two of the three already had a shape:
`border-*-width` has used `LINE_WIDTH` since 9.11 and `border-*-color` the 9.9 colour
shape, so `border-top-color: currentColor` and `color: currentColor` are the same
computation rather than two that agree. The third had none — a style is neither a
keyword nor a length — so `BORDER_STYLE` is the fourteenth shape and the fifth
**closed vocabulary**:

```text
border-*-style    one of ten values    BORDER_STYLE
```

It is a row in the table `DISPLAY`, `POSITION`, `BACKGROUND_REPEAT` and
`BACKGROUND_ATTACHMENT` already use, which is why the shape cost one grammar entry
and one kind, and why a sixth closed vocabulary will cost the same.

The value it stores is an enumerator and not a keyword, and the difference is what
makes `border-style: none` distinguishable from `border-style: banana` —
`CssBorderStyleFromSlice` answers `NONE` for both, because `none` is a real
value and there is one enumerator for "nothing", so the *check* goes through
`CssBorderStyleSliceIsKnown` and an unknown identifier becomes INVALID the
way any other does. A `banana` that computed to `none` would be a border that
vanished while every declaration looked like it worked.

The one initial value in the registry that is a *reference* is here too:
`border-*-color` starts at `currentcolor`, so it is **not** `transparent` the way
`background-color` is. Resolving it at registration time would give the author a
border nobody could see and no explanation; instead both doors that reach a computed
value — the resolve pass and the query door (`CssStyleContextQueryComputedValue`)
— resolve it against the element's own colour, so an element that declared no border
colour and one whose shorthand *reset* the colour agree, because they mean the same
thing.

Nothing here draws: `border-width: 50%` computes to a percentage because 9.8's
contract is a value's shape and not a property's range, `medium` stays a keyword
rather than becoming a used length, and nothing combines a width with the style's
`generates_border` fact to produce the zero a `none` border actually has.

## The computed style

`CssComputedStyle` is the 9.1 container **made richer rather than
changed**. An entry still holds the specified value a 9.1 caller put there, and
now also carries:

* the **name** it was filed under,
* the **typed computed value**,
* a **flag set** — `DECLARED`, `INHERITED`, `DEFAULT`, `DEFERRED`, `CUSTOM`,
  `UNRESOLVED`, `INVALID`.

Flags are a bitset and not an enumerator because the facts are not exclusive: a
value the parent supplied *and* the property's default is both, and an INVALID
value is still, factually, something the element declared.

Two counters exist to keep the style from being read as more settled than it is:

* `CssComputedStyleIncompleteCount` — entries that are INVALID or
  UNRESOLVED. A `calc()` only layout can reduce and a `color: nonsense` that is
  wrong both make the style incomplete, and a consumer can see how much.
* `CssComputedStyleIsTruncated` — the resolution ran out of ancestor
  chain (or the host reported a cycle among parents), so everything inherited
  from above the stopping point is a *lower bound*. It is recorded on the style
  rather than on an entry, because it is a fact about the resolution and not about
  any one property. `0` does not mean the element declared anything: an element
  that declares nothing at all has a *complete* style, because defaults are
  answers.

## The chain door

`CssStyleContextComputeStyle` computes **the whole ancestor chain, not
just the element**. A CSS-wide keyword leaves no trace of itself here: `inherit`
is what *put* the parent's computed value in the entry, so its provenance reads
INHERITED rather than DECLARED — the fact a caller wants is where the value came
from, and the declaration was only the instruction to go and get it. It resolves and computes each level top-down and hands the
result to the level below, so an inherited value is the parent's *computed* value
— a length, a colour, a keyword — rather than a recomputation of something that
happened to look like it. The intermediate styles are the walk's own and are
disposed as it descends; only the requested element's comes back.

That is the whole difference between this door and 9.7's: inheritance used to
carry tokens down, and now it carries normalised values down, which is what makes
a child of `div { font-size: 16px }` holding `font-size: inherit` land on
`16px` as a *typed length* rather than on the token sequence `16px`.

## Determinism and safety

* The same DOM, sheets, order and declarations produce the same computed style,
  and repeated computation produces semantically identical results. Nothing in
  the stage iterates an unordered container to decide anything.
* Entries are found by **name**, not by insertion order, so a style assembled in
  a different order answers identically.
* A computed value that **owns** text owns the bytes (`copy` deep-copies; `move`
  transfers). The 9.8 suites found one real defect here: `copy` set the text
  pointer but not its length, so a copied keyword arrived as an *empty* value —
  which is how a keyword would have reached a child as no keyword at all.
* Numeric safety is explicit: NaN and the infinities are refused rather than
  clamped or dropped quietly (there is no CSS spelling for either), RGB channels
  clamp into range, and no arithmetic bug can corrupt a value rather than produce
  a wrong one. A malformed colour cannot crash the runtime, and the robust suite
  drives a table of hostile strings through the normaliser to prove it.

## Invalidation

Nothing is cached. A computed style is produced by a call and owned by the
caller; when a stylesheet, a declaration or an ancestor's style changes, the
caller re-computes, and that **is** the invalidation boundary for this stage. The
same is true one stage down, and for the same reason: a cache here would need to
know when a DOM changed, and a module that does not own a DOM cannot know.

## What is deliberately not here

* **Used values.** `50%` stays `50%`, `1em` stays `1em`, `normal` stays `normal`,
  and nothing asks how wide anything is.
* **Font resolution.** `font-size: medium` is the keyword, not a pixel count, and
  `FAMILY_LIST` is an ordered list of names, not a matched font.
* **The modern colour spaces.** `hwb()`, `lab()`, `lch()`, `oklab()`, `oklch()`,
  `color()`, `color-mix()` and `device-cmyk()` are recognised and preserved rather
  than computed; they are 9.9's declared *extension point* and not its
  implementation, because a wrong colour is worse than a deferred one
  (`docs/Colors.md` §8).
* **`var()` substitution.** Performed (18.1) in a pass *before* computation
  (`docs/Variables.md`), not here: by the time a value shape is asked to make
  sense of a declaration, its `var()` references have been replaced — or the
  declaration was invalid at computed-value time and follows `unset`. The
  references this layer still carries are `env()`, `calc()` and the maths family.
* **Resolving a relative length.** `2em`, `50vh` and `1rem` are carried with their
  unit and their number. `CssUnitResolvePx` (9.10) answers *given a
  context*; this layer never supplies one, because computing a style has no font
  metrics and no viewport.
* **`calc()` and `min()`/`max()`/`clamp()` evaluation.** Carried as references, as
  they were in 9.8; `docs/Units.md` §6 is where the deferred set is enumerated.
* **Shorthand expansion of a computed value.** 9.11 distributes a shorthand's
declaration *before* computation, so `margin: 10px` becomes four values that are
computed individually — the author's text stays on the shorthand's own entry
(`docs/BoxModel.md`). What this layer never does is expand a **computed** value:
a computed `padding: 8px 12px 4px` is a `PRESERVED` value, and splitting it here
would be interpreting a value the property never asked it to. `inset` and the
other shorthands 9.13 does not distribute are not expanded at all.
* **Positioning as geometry.** `position` is a typed value and `z-index` a typed
number; no containing block is found, no offset is applied, and no stacking order
is computed.
* **Backgrounds as a picture.** Six typed coordinates exist; no rectangle is
painted, no `url(…)` is fetched, no image is sized or tiled, and `50%` is never
resolved against a box.
* **A default stylesheet.** The `RUNTIME` origin slot is still empty, and
  `default_styles` is still 0.

## API

`RoboCSSComputedValue.h` — the typed value:

* `CssComputedValueInit` / `_dispose` / `_move` / `_copy` / `_equals` /
  `_is_empty` / `_write`.
* `CssComputedValueCompute` — the normaliser: `(computation, specified,
  out, diag)`.
* `CssComputedValueMakeKeyword` / `_number` / `_percentage` / `_length`
  / `_color` / `_family_list` / `_display` / `_position` /
  `_background_position` / `_background_size` / `_preserved` / `_invalid`.
* `CssComputedValueKindIsKnown` / `_kind_name` / `_text` /
  `_color` / `_display` / `_position` / `_background_position` /
  `_background_size` / `_preserved`.

`RoboCSSDisplay.h` / `RoboCSSPosition.h` — the two closed vocabularies (9.12,
9.13; reached through this header's include, listed here because a reader of *this*
document meets them here):

* `CssDisplayFromSlice` / `_slice_is_known` / `_write` / `_is_known` /
  `_name` / `_count` / `_outer` / `_inner` / `_generates_box` / `_is_none` /
  `_is_block_level` / `_is_inline_level` / `_is_inline_block`.
* `CssPositioningFromSlice` / `_slice_is_known` / `_write` / `_is_known` /
  `_name` / `_count` / `_is_positioned` / `_is_in_flow` / `_is_out_of_flow`.
* `CssColorMake` / `_equals` / `_from_name` / `_from_hex` /
  `_from_rgb_function`.

`RoboCSSUnit.h` — the vocabulary and the typed number (9.10; reached through this
header's include, listed here because a reader of *this* document meets them here):

* `CssNumericNone` / `_make_number` / `_make_percentage` / `_make_length` /
  `_is_present` / `_is_integral` / `_integer_value` / `_equals` / `_write`.
* `CssNumericKindIsKnown` / `_kind_name`,
  `CssNumericParseIsKnown` / `_parse_name`,
  `CssNumericFromValue` / `_from_token`.
* `CssUnitIsKnown` / `_class` / `_class_name` / `_is_absolute` /
  `_is_viewport` / `_viewport_family` / `_name` / `_from_slice` / `_from_text` /
  `_to_px`.
* `CssUnitContextInit` and the six `_set_*` setters;
  `CssUnitResolvePx`; `CssLengthResolutionIsKnown` / `_name`.

`RoboCSSBackground.h` — the background family (9.14; reached through this header's
include, listed here for the same reason):

* `CssBackgroundRepeatFromSlice` / `_from_text` / `_slice_is_known` /
  `_write` / `_is_known` / `_name` / `_count`, and
  `CssBackgroundRepeatRepeatsOnAxis` — the row read to answer whether it
  repeats on an axis, so `repeat-x` is the pair rather than a spelling beside it.
* `CssBackgroundAttachmentFromSlice` / `_from_text` / `_slice_is_known` /
  `_write` / `_is_known` / `_name` / `_count`.
* The two *pair* values: `robo_css_background_component_*` (a component, its kind
  and its kind-aware `_equals`), `robo_css_background_position_*` and
  `robo_css_background_size_*` (`_none` / `_is_present` / `_equals` / `_write`),
  the parse door `robo_css_background_{position,size}_from_value`, and the six
  longhand names plus `CssBackgroundExpand`, which is what the `background`
  shorthand reaches the longhands through.

`RoboCSSBorder.h` — the border family (9.15; reached through the box header's
include, listed here for the same reason):

* `CssBorderStyleFromSlice` / `_from_text` / `_slice_is_known` / `_write` /
  `_is_known` / `_name` / `_count`, and `CssBorderStyleGeneratesBorder` —
  the row read to answer whether a style draws anything, which is the fact that
  makes a `none` border's used width zero whatever `border-width` says.
* `CssBorderKindIsKnown` / `_name` / `_count` / `_longhand_count`, and
  `CssBorderLonghandName` / `_is_longhand`, which read the twelve names from
  the box table's positional rows rather than spelling them.
* The positional half: `CssBorderPositionalCount` / `_name` / `_kind` /
  `_row` / `_row_of_kind`, and `CssBorderDistributesShorthand` — the one
  predicate the box model's pass asks before it distributes a row.
* The composite half: `CssBorderCompositeCount` / `_name` / `_side` /
  `_is_whole` / `_is_composite`, and the classifier
  `CssBorderClassIsKnown` / `_class_name` / `_class_count` /
  `CssBorderClassify`.
* `CssBorderExpand`, which is what `border` and the four side shorthands
  reach the twelve longhands through.

The record is by value, so the rest of it is read as **fields**: `kind`,
`number`, `unit`, `is_integer`, `color`, `display`, `position`,
`background_position`, `background_size`, `border_style`, `preserved`, and
`unresolved`.

`RoboCSSComputed.h` — the style container's computed half:

* `CssComputedStyleSetEntry` / `_name_at` / `_computed_at` /
  `_specified_by_name` / `_computed_by_name` / `_flags` / `_has_flag` /
  `_has_invalid` / `_incomplete_count` / `_is_truncated`.

`RoboCSSPropertyRegistry.h` — the new column:

* `CssPropertyDefinition.computation` — the field, alongside
  `CssPropertyRegistryRegisterTyped` to set it.
* `CssPropertyComputationIsKnown` / `_name`.

`RoboCSSStyle.h` — the chain door:

* `CssStyleContextComputeStyle`.

## What 9.16 pins about this vocabulary

The conformance suite (`css.conformance.registry.*`) is the only place the *whole*
vocabulary is walked at once, and it pins three things this document states in
prose. Every shape here is reachable from a registry row, so every registered row's
initial values are run through the normaliser each row's own `computation` names
(63 rows when 9.16 wrote this, 66 since 10.13's overflow family) — a row
whose shape and whose initial had drifted apart computes `INVALID` for every element
of every document and would otherwise be silent. Every name in
`CssPropertyComputationIsKnown`'s table is asserted to be a *named*
enumerator, because an unnamed one prints as `"unknown"` in a diagnostic and nothing
else in the build reads that function. And the two spellings a value can have *by
design* are asserted side by side: a **preserved** value keeps the source tokens, so
`border`'s initial prints `currentcolor`, while a **colour** value is the colour
module's canonical rendering, so it prints `currentColor`. Five rows use one
spelling and six the other; a build that normalised a shorthand's tokens, or
preserved a colour's, shows up as exactly that diff.

## How the tests are run

* `scripts/robo test-css` — the whole CSS subsystem (609 tests).
* `./Out/RoboTest.host.exe --filter "css.conformance"` — the whole-product
  questions no family suite can ask (14 tests).
* `./Out/RoboTest.host.exe --filter "css.computedvalue."` — the value model, the
  unit table, the colour forms and the normaliser (14 tests).
* `./Out/RoboTest.host.exe --filter "css.computed."` — the pipeline over the
  CSS-side element host (15 tests — the 13 of this phase plus the two 9.1
  container-boundary cases the container suite kept).
* `./Out/RoboTest.host.exe --filter "css.dom.computed"` — the whole pipeline,
  ending in a computed style, over the **visual checkpoint's own document**
  (2 tests, in the same suite that drives 9.4's matching against the real tree).
* `pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test
  -Sanitize` — the same suites under ASan + UBSan.
