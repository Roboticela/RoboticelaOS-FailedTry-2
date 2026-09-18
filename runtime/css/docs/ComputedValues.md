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

> **The vocabulary moved in 9.10.** `robo_css_unit_t`, its accessors and the
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
`robo_css_computed_value_equals` can be a comparison of typed values rather than a
comparison of spelling. The second group is kept: converting `2em` in this stage
would produce a number that looks resolved and is wrong on every element with a
different font size. There is no host-display DPI anywhere in the conversion,
because the factors are the CSS-defined ones.

Resolving one of those kept units is `robo_css_unit_resolve_px`'s job, and it is a
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

**`robo_css_unit_is_absolute` asks the table, not `robo_css_unit_to_px`.** The
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

`color: var(--brand)` is legal CSS that this build cannot resolve yet, and the
colour parser is the wrong authority for it: the value is not a colour *yet*.
So `computed_compute_color` asks one question first — is this value a single
unresolved function (`var`, `env`, `calc`, `min`, `max`, `clamp`)? — and if so it
carries the tokens and flags them `unresolved`, exactly as every other value shape
does. Nothing substitutes `--brand`, and nothing reads a custom property as a
colour: a `--brand: #336699` keeps those five tokens until a colour-shaped property
consumes them.

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
* **The modern colour spaces.** `hwb()`, `lab()`, `lch()`, `oklab()`, `oklch()`,
  `color()`, `color-mix()` and `device-cmyk()` are recognised and preserved rather
  than computed; they are 9.9's declared *extension point* and not its
  implementation, because a wrong colour is worse than a deferred one
  (`docs/Colors.md` §8).
* **`var()` substitution.** A reference is carried as a reference. No custom
  property is substituted anywhere.
* **Resolving a relative length.** `2em`, `50vh` and `1rem` are carried with their
  unit and their number. `robo_css_unit_resolve_px` (9.10) answers *given a
  context*; this layer never supplies one, because computing a style has no font
  metrics and no viewport.
* **`calc()` and `min()`/`max()`/`clamp()` evaluation.** Carried as references, as
  they were in 9.8; `docs/Units.md` §6 is where the deferred set is enumerated.
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
* `robo_css_color_make` / `_equals` / `_from_name` / `_from_hex` /
  `_from_rgb_function`.

`RoboCSSUnit.h` — the vocabulary and the typed number (9.10; reached through this
header's include, listed here because a reader of *this* document meets them here):

* `robo_css_numeric_none` / `_make_number` / `_make_percentage` / `_make_length` /
  `_is_present` / `_is_integral` / `_integer_value` / `_equals` / `_write`.
* `robo_css_numeric_kind_is_known` / `_kind_name`,
  `robo_css_numeric_parse_is_known` / `_parse_name`,
  `robo_css_numeric_from_value` / `_from_token`.
* `robo_css_unit_is_known` / `_class` / `_class_name` / `_is_absolute` /
  `_is_viewport` / `_viewport_family` / `_name` / `_from_slice` / `_from_text` /
  `_to_px`.
* `robo_css_unit_context_init` and the six `_set_*` setters;
  `robo_css_unit_resolve_px`; `robo_css_length_resolution_is_known` / `_name`.

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

* `scripts/robo test-css` — the whole CSS subsystem (471 tests).
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
