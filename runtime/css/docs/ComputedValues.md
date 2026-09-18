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
| **specified** | what a declaration said: the component-value tree the grammar built, tokens intact | `robo_css_value_t`, `RoboCSSValue.h` |
| **resolved** | what the element *has* after cascade and inheritance, still as tokens | `robo_css_inherited_style_t`, `RoboCSSInherit.h` |
| **computed** | what the property's own semantics make of that: a kind, a number, a unit, four channels, or a keyword | `robo_css_computed_value_t`, `RoboCSSComputedValue.h` |
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

A length is a number and a unit, and the units split into two groups:

```text
fixed factor → px      px  cm  mm  q  in  pt  pc
kept as written        em  rem  ex  ch  vw  vh  vmin  vmax
```

The first group converts **here**, exactly: `1in` and `96px` are the same length,
so two computed values that mean the same thing must compare equal, and
`robo_css_computed_value_equals` can be a comparison of typed values rather than a
comparison of spelling. The second group is kept: converting `2em` in this stage
would produce a number that looks resolved and is wrong on every element with a
different font size. There is no host-display DPI anywhere in the conversion,
because the factors are the CSS-defined ones.

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

**`robo_css_unit_is_absolute` asks the table, not `robo_css_unit_to_px`.** The
converter answers with the value it was given when a unit has no fixed factor, so
a relative unit and an unknown one both come back looking convertible; the
predicate tests the factor's presence instead. (This was one of the two real
defects the 9.8 suites found — see `Tests.md`.)

## Colours, and the 9.9 seam

9.8 normalises the colour forms the parser and this build can already express:

* the **standard named set**, case-insensitively, including `rebeccapurple` and
  the `gray`/`grey` pairs;
* `transparent`;
* all four hex lengths — `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa` — with nibble
  doubling for the short forms and every other length refused rather than
  truncated;
* `rgb()` and `rgba()` in **both** syntaxes, comma and space, with numeric or
  percentage channels and numeric or percentage alpha.

Two details are the kind that only look like details:

* **The two separator styles do not mix.** `rgb(255, 0 0)` is malformed, not
  "close enough": the call starts as a comma list and the second separator is
  not a comma. Whitespace *touching* a comma is decoration, which is what makes
  `rgb( 1 , 2 , 3 )` legal. A `/` is only reachable from the space style, so
  `rgb(1, 2, 3 / 4)` never gets past its slash.
* **Channels clamp; they do not wrap.** `rgb(300, -10, 0)` is `#ff0000`, and
  `rgb(0 0 0 / 5)` is opaque black — clamping is what CSS says and what keeps a
  plausible-but-wrong channel from appearing.

Not implemented, deliberately: `hsl()`, `hwb()`, `lab()`, `lch()`, `oklab()`,
`oklch()`, `color()` and `color-mix()`. They are **recognised and carried**, still
PRESERVED (and after 9.9 they will be computed) rather than rejected as unknown
functions, and `hsl(0, 100%, 50%)` is a preserved value rather than a colour this
build guessed at. A wrong colour is worse than a deferred one.

`currentColor` is carried PRESERVED with `unresolved` set. It is the element's own
computed `color`, and this stage does not have the resolution order that makes
that answerable.

## CSS-wide keywords

`inherit`, `initial` and `unset` are consumed by **inheritance** (9.7), on the
parsed value and never on a string, so by the time a value reaches computation
the keyword has already been turned into the value it meant — the parent's, the
initial one, or the same choice made according to the property's
`inherited_by_default`. Computation therefore never sees one.

`revert` is the exception and stays one: rolling it back means re-running a
cascade with one origin removed, so it is carried as a **deferred** entry with
`ROBO_CSS_COMPUTED_FLAG_DEFERRED`, counted, and reported. Obeying half of it
would be wrong in exactly the cases it exists for.

## The registry's new column

`robo_css_property_definition_t` gained one field — `computation`, the *shape* of
the property's declared value — and it is what makes the normaliser a function
rather than a lookup table keyed by property name:

```text
PRESERVE       carry the tokens                    background, border, margin, custom
COLOR          named / hex / rgb() / transparent   color
LENGTH         dimension, percentage, zero, keyword
                                                   width, height, font-size,
                                                   letter-spacing, word-spacing, text-indent
KEYWORD        one identifier                      display, position, text-align, visibility
FONT_FAMILY    identifiers and strings, comma-separated
FONT_WEIGHT    a number 1–1000 or `normal`/`bold`
LINE_HEIGHT    `normal`, a number, a length or a percentage
```

`FONT_WEIGHT` refuses `5000` rather than clamping it — 5000 is not a heavier
1000, it is a mistake — and `LINE_HEIGHT` keeps a multiplier (`1.5`) distinct
from a length (`20px`) from a percentage (`150%`), because a line box needs the
difference.

Adding the column did not change the registry's data: every initial value was
already text, and the registry already parsed each into a component-value tree at
registration time. One field, one switch, seven shapes.

## The computed style

`robo_css_computed_style_t` is the 9.1 container **made richer rather than
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

* `robo_css_computed_style_incomplete_count` — entries that are INVALID or
  UNRESOLVED. A `calc()` only layout can reduce and a `color: nonsense` that is
  wrong both make the style incomplete, and a consumer can see how much.
* `robo_css_computed_style_is_truncated` — the resolution ran out of ancestor
  chain (or the host reported a cycle among parents), so everything inherited
  from above the stopping point is a *lower bound*. It is recorded on the style
  rather than on an entry, because it is a fact about the resolution and not about
  any one property. `0` does not mean the element declared anything: an element
  that declares nothing at all has a *complete* style, because defaults are
  answers.

## The chain door

`robo_css_style_context_compute_style` computes **the whole ancestor chain, not
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
* **`hsl()` and the modern colour spaces.** Recognised and preserved; 9.9's.
* **`var()` substitution.** A reference is carried as a reference. No custom
  property is substituted anywhere.
* **Shorthand expansion.** `margin: 10px` stays one preserved value.
* **A default stylesheet.** The `RUNTIME` origin slot is still empty, and
  `default_styles` is still 0.

## API

`RoboCSSComputedValue.h` — the typed value:

* `robo_css_computed_value_init` / `_dispose` / `_move` / `_copy` / `_equals` /
  `_is_empty` / `_write`.
* `robo_css_computed_value_compute` — the normaliser: `(computation, specified,
  out, diag)`.
* `robo_css_computed_value_make_keyword` / `_number` / `_percentage` / `_length`
  / `_color` / `_family_list` / `_preserved` / `_invalid`.
* `robo_css_computed_value_kind_is_known` / `_kind_name` / `_text` /
  `_color` / `_preserved`.
* `robo_css_unit_is_known` / `_is_absolute` / `_name` / `_from_slice` /
  `_from_text` / `_to_px`.
* `robo_css_color_make` / `_equals` / `_from_name` / `_from_hex` /
  `_from_rgb_function`.

The record is by value, so the rest of it is read as **fields**: `kind`,
`number`, `unit`, `is_integer`, `color`, `preserved`, and `unresolved`.

`RoboCSSComputed.h` — the style container's computed half:

* `robo_css_computed_style_set_entry` / `_name_at` / `_computed_at` /
  `_specified_by_name` / `_computed_by_name` / `_flags` / `_has_flag` /
  `_has_invalid` / `_incomplete_count` / `_is_truncated`.

`RoboCSSPropertyRegistry.h` — the new column:

* `robo_css_property_definition_t.computation` — the field, alongside
  `robo_css_property_registry_register_typed` to set it.
* `robo_css_property_computation_is_known` / `_name`.

`RoboCSSStyle.h` — the chain door:

* `robo_css_style_context_compute_style`.

## How the tests are run

* `scripts/robo test-css` — the whole CSS subsystem (452 tests).
* `./Out/RoboTest.host.exe --filter "css.computedvalue."` — the value model, the
  unit table, the colour forms and the normaliser (14 tests).
* `./Out/RoboTest.host.exe --filter "css.computed."` — the pipeline over the
  CSS-side element host (13 tests).
* `./Out/RoboTest.host.exe --filter "css.dom.computed"` — the whole pipeline,
  ending in a computed style, over the **visual checkpoint's own document**
  (2 tests, in the same suite that drives 9.4's matching against the real tree).
* `pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test
  -Sanitize` — the same suites under ASan + UBSan.
