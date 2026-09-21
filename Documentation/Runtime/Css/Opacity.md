# RoboCSS — `opacity` (added as paint phase 11.9's prerequisite)

The first property in this subsystem whose value is a **range** rather than a closed set, and the
first whose row a phase added without that phase having anything to do with geometry or a colour.

`opacity` is registered here because a paint phase needed the value, not because Phase 9 left a
hole. It is the one place in this layer where a number is the *value* rather than a measurement, and
three facts about it decide the whole shape of the family.

## 1. The value is a real in `[0, 1]`, and the initial value is **1**

CSS writes the value as `<alpha-value>`: a number, or a percentage meaning that number over a
hundred. `1` is fully opaque, `0` fully transparent, and an element that declares nothing is
**fully opaque**.

That last clause is the reason this module exists rather than being three lines at a call site. The
dangerous default is the *other* one: a computed-value record that answered `0` for "nothing was
declared" would make every element of every document without an `opacity` declaration invisible, and
the failure would be **silent** — right geometry, right layout, blank page. So the initial value is a
door (`CssOpacityInitial`) rather than a literal spelled wherever a default is needed, and
every accessor in this layer answers it for a record that is not an opacity
(`CssComputedValueOpacity`).

| Door | Answers |
|---|---|
| `CssOpacityInitial` | `1.0` — the one door, so "fully opaque" has one spelling |
| `CssOpacityClamp` | `[0, 1]`; **NaN → the initial value**, `±inf` → an end |
| `CssOpacityFromPercentage` | `percent / 100`, then the *same* clamp |
| `CssOpacityIsValid` | finite and inside `[0, 1]`; NaN separated first |
| `CssOpacityIsOpaque` / `_is_transparent` | exactly `1.0` / exactly `0.0` |
| `CssOpacityWrite` | the pinned fixed-point text form (below) |

## 2. Out of range **clamps**; not-a-number is refused

The range rule is CSS's, not a kindness:

- `opacity: 2`, `opacity: -1`, `opacity: 150%` — **clamped**, not refused. An author who wrote
  `150%` meant "as opaque as it goes", and refusing would make a document every browser renders
  correctly render here as if the declaration had never been written — a worse failure than the one
  being prevented. The clamp is applied once, in `computed_compute_opacity`, **after** the
  percentage conversion, so both spellings of `<alpha-value>` meet one range rule at one point.
- `opacity: banana` — refused: *"not a number or a percentage"*. There is no number here.
- `opacity: 1e999` — refused: *"not a finite number"*. A number the tokenizer could not make finite
  is arithmetic that went wrong rather than a value anyone writes.
- `opacity: 10px` — refused, with the *same* message as `banana`: the truthful answer for a property
  whose grammar has no length in it, and the answer `z-index` gives a dimension.
- `opacity: calc(1 / 3)` and `opacity: var(--o)` — **carried**, not refused: legal CSS this build
  does not evaluate, marked unresolved like every other deferred value.

A NaN reaching the *clamp* (rather than the parser) answers the initial value, and the direction is
the same argument as §1's: a record nothing can read must render the way an element nothing declared
does, which is *visible*. A NaN silently becoming transparent would make a bug in a caller's
arithmetic look like a document that draws nothing.

## 3. It is **not inherited**, and that is not the same fact as "a child is drawn at its parent's strength"

The registry row (`Source/RoboCSS/RoboCSSPropertyRegistry.c`) is
`RoboCssPropertyInheritanceNonInherited` with initial `"1"`, and the two clauses look
contradictory until they are read apart:

- an **inherited** `opacity` would make a child at `opacity: 1` inside a parent at `0.5` fully opaque
  again — because inheritance *copies a value down* and would overwrite what the child's own
  computed value says;
- CSS renders that child at **`0.5`**, because the parent's opacity applies to everything the parent
  contains. Opacity **multiplies along a path**, which is an *accumulation* rather than an
  inheritance.

Inheritance is the cascade's job; accumulation is the job of whoever walks the path. So the
cascade's answer stays non-inherited and the product lives one layer up, in RoboPaint
(`RoboPaint/RoboPaintOpacity.h`, `PaintOpacityMultiply`). Registering the property as
inherited would be the mistake that produces the wrong *picture* rather than a wrong number — which
is why it is stated here, argued in the registry row's own comment, and asserted by
`css.opacity.registry` (`CssPropertyDefinitionInherited(definition) == 0`, on the definition
the registry actually ships).

## 4. One longhand, no shorthand

Unlike the `overflow` family below it in the same table, `opacity` has nothing to expand into: it is
a single longhand in CSS. So there is no second name for a consumer to read and no partial form this
build declines, and the row is one line rather than the three `overflow` needed to work around a
shorthand nothing expands (`docs/Overflow.md`).

## 5. The computed value

One kind, `RoboCssComputedValueOpacity` (`15`), one field on the value record, and one read door:

| Door | Behaviour |
|---|---|
| `CssComputedValueMakeOpacity` | refuses a value that is not valid, so every `OPACITY` value in the build is inside `[0, 1]` |
| `CssComputedValueOpacity` | the value; **the initial value for any other kind** — the direction §1 argues |
| `CssComputedValueKindName` | `"opacity"`, so the vocabulary has no unnamed member |
| equality | the number exactly: two opacities a millionth apart are two different pictures, and a tolerance would turn a determinism failure into a pass |

The canonical text is the module's own fixed-point form rather than `printf`'s — the rule every
number this subsystem has emitted since 9.8 follows, restated rather than reached for: whole values
have no decimal point, so `1` and `0.5` both round-trip, and the spelling is deterministic and
locale-independent.

## 6. What the conformance tables grew

Two rows, both in `css/docs/Conformance.md`'s tables:

- the **initial-value table**: `{"opacity", "opacity", "1"}` — **67** rows (66 before this family).
  It is the row whose initial text is *not* the zero of its own vocabulary, which is exactly the
  drift the table exists to catch.
- the **declaration matrix**: three representative declarations — `0.5` (a number), `150%` (a
  percentage, clamped to `1`) and `banana` (refused, *"not a number or a percentage"*) — **74**
  rows (71 before). Three rather than one because the value has three shapes, and the matrix asserts
  one exact answer per shape, first through the computation directly and then on a real element
  through the real chain.

The capability struct's `opacity` flag is walked by `css.capabilities` like every other family's, for
the reason `docs/Conformance.md` gives: two flags were once documented as flipped and asserted
nowhere.

## 7. What this family is not

Not a colour's alpha: `rgba(0, 0, 0, 0.5)` makes a *colour* half transparent and travels with the
colour; `opacity: 0.5` makes an *element* half transparent and belongs to a box. They are two facts
that both end up as a factor on the alpha a surface receives, and keeping them apart here is what
lets the paint layer apply one without touching the other.

Also absent: any compositing, blending, layer or filter concept (that is paint's, and this layer
hands it one number per element); `opacity` as a shorthand or a custom-property expansion; and any
resolution against an element's ancestors — this layer answers for **one** element, and the
accumulation is the caller's.
