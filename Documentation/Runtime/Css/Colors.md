# RoboCSS Colours (Phase 9.9)

A colour is the one CSS value whose *meaning* is arithmetic. `10px` is a number and
a unit, `block` is an identifier, `var(--x)` is a token sequence — but `#336699` is
three channels in a space at a precision, and everything that can be done with it
(compare it, blend it, resolve `currentColor` against it) is arithmetic on those
three numbers.

That is why 9.9 is a module rather than a bigger colour branch inside the
computed-value layer, which is where 9.8 had to put one: 9.8's only reader was the
computed value, so the parser lived beside it. Once the *representation* has to be
shared — by the value layer, the computed style, and the renderer that will
eventually paint a colour — it needs a name and a header.

```
CSS text
   ↓  tokenizer (9.2)                    `#336699` → HASH, `rgb(...)` → FUNCTION
   ↓  grammar (9.3)                      declaration → component-value tree
   ↓  RoboCSSColor                       the tree → one record
   ↓  cascade (9.5) + specificity (9.6)  which declaration wins
   ↓  inheritance (9.7)                  which parent value a child has
   ↓  computed values (9.8)              the record → a typed computed value
   ↓  computed style                     `currentColor` → the channels it stands for
```

| Goal | What it delivers |
|---|---|
| 9.9.1 the record | `CssColor`: a kind, four 8-bit channels, and deterministic equality |
| 9.9.2 the named set | 148 standard names, reachable by name and by index |
| 9.9.3 the syntaxes | `#rgb`/`#rgba`/`#rrggbb`/`#rrggbbaa`, `rgb()`/`rgba()`, `hsl()`/`hsla()`, `transparent`, `currentColor` |
| 9.9.4 the outcomes | `RESOLVED`/`CURRENT`/`DEFERRED`/`MALFORMED`/`UNKNOWN_NAME`/`INVALID` — six answers, because they mean six things |
| 9.9.5 resolution | `currentColor`, resolved by the computed style against the one `color` that applies |
| 9.9.6 serialization | one stable spelling per kind, so two equal colours cannot print differently |

## 1. The record

```c
typedef struct CssColor {
    CssColorKind kind;   /* NONE | RGBA | CURRENT | INVALID */
    uint8_t red, green, blue, alpha;
} CssColor;
```

Four kinds rather than one record with a `valid` flag, because "no colour", "a
resolved colour", "the contextual keyword" and "not a colour" are four answers and
a record that could only say three of them would make a caller guess what an
all-zero channel set meant. In particular: a black colour and no colour at all are
different, and `background-color`'s initial value *is* transparent black, so the
distinction is not academic.

| Kind | Means | Channels |
|---|---|---|
| `NONE` | nothing: a cleared record, and what a failed parse leaves | meaningless |
| `RGBA` | a resolved colour, sRGB, 8 bits per channel | live |
| `CURRENT` | `currentColor`, unresolved by construction | never live |
| `INVALID` | a colour was meant and there is not one | never live |

`CssColorEquals` is kind-aware: a resolved red is never equal to
`currentColor`, because they are different answers and a comparison that ignored
the kind is exactly how a contextual keyword gets mistaken for a fixed channel
set. Channels are compared only when both kinds are `RGBA`.

### Why 8 bits, and why a flat record

8 bits per channel is a decision with three reasons: CSS's ordinary colour syntax
cannot express more (`rgb()` takes 0–255 or a percentage; a hex form is 3, 4, 6 or
8 nibbles), the runtime's framebuffer is 8 bits per channel, and a float triple
would invite equality comparisons that fail on values which are the same colour.
`hsl()` therefore converts once, in doubles, with a single rounding at the end —
which is what makes `hsl(120 100% 25%)` *exactly* the same colour as `#008000` and
not merely close, and exactness is a property a test can hold.

A flat record rather than a tagged union, matching `CssComputedValue`:
the channels are four bytes, and a union would buy those bytes in exchange for an
ownership rule per member that every reader has to reconstruct.

## 2. The named set

148 entries in one table, reachable two ways:

- `CssColorFromName` — ASCII case-insensitive, so `ReBeCcApUrPlE` is a
  colour and `reddish` is not;
- `CssColorNamedAt` — by index, so a suite can walk the whole set instead
  of the handful of names someone thought to type. `css.color.named_table` does
  exactly that, and it is why the *table* is tested rather than the ten names in
  the tests.

The set is CSS's own: the 16 CSS2.1 keywords, the 130 SVG/X11 names (duplicates
included — `gray` and `grey` are two names for one colour, and so are `aqua` and
`cyan`), and `rebeccapurple`, which CSS added to the standard set and which is
therefore ordinary authoring rather than an exotic spelling.

`transparent` is deliberately **not** a table entry. It *is* a colour — transparent
black, `#00000000` — but it is a keyword rather than a name, so it is handled where
the identifier is (`CssColorFromValue`) and `CssColorFromName`
answers 0 for it. A reader looking for `transparent` should not have to know it is
spelled as a table row.

An unknown identifier is never silently a colour: `CssColorFromName`
returns 0 and the value comes back `UNKNOWN_NAME`, which is a different diagnostic
from `INVALID`.

## 3. The syntaxes

### Hex

Every form, and each must be exactly one of them:

| Form | Expansion |
|---|---|
| `#rgb` | each nibble doubled: `#f00` → `ff, 00, 00` |
| `#rgba` | as above plus the alpha nibble |
| `#rrggbb` | opaque |
| `#rrggbbaa` | explicit alpha |

The nibbles are **expanded, not zero-padded**, which is the classic three-digit
mistake: `#f00` is `(0xff, 0, 0)`, not `(0x0f, 0, 0)`. A length of anything other
than 3, 4, 6 or 8, or a byte that is not a hex digit, is refused rather than
truncated or padded — a three-digit colour read as six would be a colour nobody
wrote. Digits are case-insensitive and normalize to lower-case.

An alpha nibble that is opaque normalizes away: `#f00f` *is* `#ff0000`, so writing
it back with a redundant `ff` would be the one place the representation disagreed
with the `#rrggbb` spelling of itself.

### `rgb()` / `rgba()`

Both names, both argument styles, every shape each accepts:

```
rgb(255, 0, 0)          rgba(255, 0, 0, 0.5)      legacy comma
rgb(255 0 0)            rgb(255 0 0 / 50%)        modern space
rgb(20%, 40%, 60%)      rgb(51%, 40%, 60%)        percentage channels
rgb(255, 0%, 0)                                   mixed number and percentage
```

`rgb` and `rgba` are *not* distinguished: CSS Color 4 made each accept both
shapes, so `rgba(255 0 0)` is a colour and `rgb(255, 0, 0, 0.5)` is one too.

Channels **clamp rather than wrap**: `rgb(300, 0, 0)` is red, not a colour with a
wrapped green channel, and a negative channel is zero. Percentages are converted
once, at the end: 50% of 255 is 127.5, which is 128 — rounded to nearest, not
truncated.

Mixing the two separator styles is refused, which is CSS's own rule:
`rgb(255, 0 0)` is malformed. So is `rgb(1 2 3 4)` — a space-separated call has an
alpha only after a slash, and there is no slash. `rgb( 255 , 0 , 0 )` is legal;
whitespace is legal everywhere CSS says it is.

### `hsl()` / `hsla()`

The hue is an angle: a bare number is degrees, and `deg`, `grad`, `rad` and `turn`
are the four angle units, because `hsl(0.25turn 100% 50%)` is ordinary authoring and
refusing it would be a limitation rather than a decision.

Hue is **normalised, not clamped** — `-120` and `240` are the same hue, `480` is
`120`, `720` is `0` — which is CSS's rule and the reason a wheel can be written
either way round. Saturation and lightness are clamped to 0–100%: CSS permits the
overshoot and clamps it, so `hsl(0, 150%, 50%)` is `hsl(0, 100%, 50%)` and a
negative saturation is grey rather than a colour nobody asked for.

They must be percentages in the legacy comma syntax (`hsl(0, 50, 50)` is
malformed — CSS says so); the modern space syntax also accepts bare numbers, which
CSS Color 4 reads as percentages. Alpha follows `rgb()`'s rules.

A hue beyond what a `double` can reduce to a fraction — `hsl(1e300turn ...)` —
saturates to zero rather than producing an artefact. The bound is documented and
pinned by a test rather than left to the platform.

## 4. The six outcomes

```c
typedef enum RoboCssColorParse {
    RoboCssColorParseInvalid = 0,
    RoboCssColorParseResolved,
    RoboCssColorParseCurrent,
    RoboCssColorParseDeferred,
    RoboCssColorParseMalformed,
    RoboCssColorParseUnknownName
} CssColorParse;
```

"Did it work" is not enough of an answer. `MALFORMED`, `UNKNOWN_NAME` and
`INVALID` all end as an invalid computed value for a colour property, and they are
separate because the *diagnostic* differs and a developer reading

- `rgb() arguments are out of range or malformed`,
- `unknown colour name`, or
- `not a colour`

learns three different things about the declaration they wrote. Collapsing them
would be less code and less information, and the information is the only reason a
diagnostic exists.

`DEFERRED` is the outcome that keeps this build from lying. A colour function it
has no representation for — `hwb()`, `lab()`, `lch()`, `oklab()`, `oklch()`,
`color()`, `color-mix()`, `device-cmyk()` — is *legal CSS*: the declaration is
carried with its tokens and flagged unresolved, because a colour that is wrong is
worse than a colour that is deferred and calling the author's declaration invalid
would be false. `CssColorFunctionIsUnrepresentable` is the predicate, and
it lives with the rest of the colour syntax because that is what it is a fact
about.

The extension point is the `kind`: a new space is a new kind plus a payload member,
and nothing in the value system, the cascade or the computed style changes — all
three pass a `CssColor` by value and ask it what it is.

## 5. `currentColor`

`currentColor` is not a colour. It is a reference to whatever `color` turns out to
be for the element it lands on, and CSS keeps it unresolved through the
computed-value stage for precisely that reason. Resolving it early — to black, or
to anything else — would make `color: currentColor` mean a fixed colour, which is
the one thing it must never mean.

So it is a *kind*. `CssColor` records `CURRENT`, the value layer carries it
as a `COLOR` typed value with the `UNRESOLVED` bit set, and the resolution happens
in `RoboCSSComputed.c` — the only layer that has the element and the parent.

`CssColorResolveCurrent(color, current_color, out)` does the one
substitution and nothing else:

| Input | Result |
|---|---|
| `RGBA` | unchanged, `current_color` never read |
| `CURRENT` + a resolved `current_color` | becomes those channels |
| `CURRENT` + a `CURRENT`/absent `current_color` | stays `CURRENT`, and stays flagged |
| `NONE` / `INVALID` | unchanged; there was never a colour here |

**The two rules.** *Which* `color` `currentColor` stands for depends on the
property it was written on:

```
color: currentColor              → the **parent's** computed `color`
                                   ("the colour I would have inherited" — the only
                                   reading that terminates, since the element's own
                                   `color` is the thing being computed)

background-color: currentColor   → the element's **own** computed `color`
                                   ("my text colour", so it resolves against a
                                   value that must already be final)
```

That is why `color` is resolved first, in a pass of its own, and everything else
afterwards against the settled result: nothing can read a value that has not been
settled, so two properties that both say `currentColor` agree no matter which was
declared first.

`css.color.computed.current_color_rules` separates the two rules with a parent whose
colour differs from its child's — a single element would satisfy either reading and
prove neither.

When there is nothing to resolve against — no parent computed style, which is what
a root element looks like — the colour **stays** `CURRENT` and the `UNRESOLVED` bit
**stays** set. That is the honest answer, and inventing black would be exactly the
mistake the keyword exists to prevent.

Only the `UNRESOLVED` bit is cleared on resolution, never the whole flag set: an
entry can be declared and unresolved and inherited all at once, and those other
facts are still true afterwards. Clearing it is safe for a colour specifically
because a `COLOR` value's own unresolved flag is set in exactly one place — the
`currentColor` branch of the colour computation — and not by anything else that
could leave the bit meaning something different.

### `var()` in a colour position

`color: var(--brand)` is legal CSS that this build cannot resolve yet, and the
colour parser is the wrong authority for it: the value is not a colour *yet*. So
the value layer settles it before asking the colour module — a single `var()`,
`env()`, `calc()` and friends is carried with its tokens and flagged `UNRESOLVED`,
exactly as it is for every other value shape. Nothing substitutes `--brand`, and
nothing *reads* `--brand` as a colour: a custom property holding `#336699` keeps
those five tokens, and only a colour-shaped property consumes them as a colour.

## 6. Serialization

`CssColorWrite` writes one stable spelling per kind: `none`, `invalid`,
`currentColor`, and hex for a resolved colour, lower-case, with alpha appended only
when it is not opaque.

Named colours are deliberately *not* written back as names. `rebeccapurple` is the
same colour as `#663399`, and serializing whichever one the author happened to type
would make two equal values print differently — which is the property that makes a
written colour usable as an assertion.

This is a *diagnostic* rendering rather than a CSSOM serialization: `none` and
`invalid` are not CSS, and they are written as the diagnostics they are because a
value nobody can print is a value nobody can debug. The buffer must be
`RoboCssColorTextMax` bytes, the length is required, and a buffer too small is
refused with the length it would have needed rather than truncated — no truncation,
and no half a colour.

## 7. Colour properties and the registry

The registry (RoboCSSPropertyRegistry.c) carries one row per property with the
facts a computation needs. Two of them are colour-shaped:

| Property | Inheritance | Initial value |
|---|---|---|
| `color` | inherited | `black` |
| `background-color` | non-inherited | `transparent` |

`background-color` is the row 9.9 added, and the reason is not backgrounds: it is
the only *second* property whose value is a colour, and without one the rule that
resolves `currentColor` against the element's own `color` — as opposed to the
parent's, which is what it means on `color` itself — would be unreachable and
therefore unprovable. A property fact is what that table holds: no background is
painted, no shorthand is expanded, and `background` itself keeps its own
`PRESERVE` classification untouched.

`color` participates in `inherit`, `initial` and `unset` through the ordinary 9.7
and 9.8 machinery, and inherited colours arrive as typed colours:

```
div { color: rgba(255, 0, 0, 0.5) }     → computed `#ff000080`
section { color: #336699 }              → computed `#336699`
p (no declaration)                      → inherits `#336699`, a RGBA record
```

```
p { color: initial }                    → `#000000` (the registry's initial), not red
p { color: unset }                      → inherits, because `color` inherits
p { background-color: unset }           → `transparent`, because it does not
```

## 8. What is not here

| Not implemented | Why, and what happens instead |
|---|---|
| `hwb()`, `lab()`, `lch()`, `oklab()`, `oklch()`, `color()`, `color-mix()`, `device-cmyk()` | Recognised and reported `DEFERRED`: the declaration is legal, so it is carried rather than refused, and a colour that is wrong is worse than one that is deferred. `css.color.deferred_spaces` pins the list. |
| `color-mix(in srgb, …)` | Same: mixture is arithmetic this build has no representation for a *mixed* colour to be. |
| Colour interpolation, blending, gradients | Later phases. Nothing here interpolates. |
| A colour space other than sRGB | The `kind` is the extension point, and a new space is a new kind plus a payload member. |
| Writing a colour back as a name | Deliberate: two equal colours must print the same, and hex always can. |
| `color: red blue` | Refused: a colour is one component, and two is not a colour. |

## 9. Ownership

Nothing in the module owns anything. Every input is a borrowed slice or a borrowed
component-value tree, and every output is a record of plain integers — which is what
lets a colour be copied, compared, stored and passed across a boundary without a
lifetime rule. In particular a copied colour is a *copy*: the suites in 9.7 and 9.8
proved that an inherited value survives its parent's style being destroyed, and the
same holds here because there is nothing to dangle.

The one ownership question this phase touched is in `CssComputedValueCopy`,
which had to be able to duplicate a colour record — including when that record is
`CURRENT` and carries the `UNRESOLVED` bit, since `currentColor` is inherited like
any other colour value.

## 10. Bounds and robustness

`css.color.robust` drives 37 malformed declarations through the colour parser and
asserts two things about every one of them: the outcome is a defined enumerator, and
a colour that was not produced is `NONE` or `INVALID` — never a plausible-looking
channel set nobody wrote. The inputs include `#`, `##`, `#fffffffffffff`, `#0x1234`,
`#-1`, `rgb(`, `rgb()`, `rgb(,,,,)`, `rgb(/ / /)`, `hsl(0 0 0 /)`,
`rgb(-1e400, 0, 0)`, `rgb(1e-400, 0, 0)`, `currentColor()`,
`transparent transparent`, `,,,,`, `;;;`, `()`, `[]`, `{}`, `inherit`, `var(--x)`,
`calc(1px + 1px)`, `url(x)`, `\`, `1 2 3` and `red, blue`.

Two of those are worth naming because they are the numeric edges:

- `rgb(-1e400, 0, 0)` — the tokenizer can produce a non-finite literal, and a
  channel is clamped into range rather than propagated as an infinity;
- `rgb(1e-400, 0, 0)` — an underflow to zero, which is a channel like any other.

A malformed colour value is frequently a *grammar-level* error too — `rgb(` is an
unclosed function — which is why the suite's fixture asserts that one declaration
came out and does not assert a clean parse: the subject is the colour parser's
verdict on a value the grammar did produce.

## 11. Escapes and identifiers

An escaped identifier reaches the colour layer already decoded: `#item\31 23` is a
hash token whose value bytes are the id's own text, and `.\66 oo` is a class by the
time matching sees it. Escaping changes identifier *parsing* and nothing about what
a colour is, so the specificity rules (docs/Specificity.md) and the colour rules are
independent — a colour keyword spelled with an escape is the same keyword, because
the escape is resolved before the comparison.

## 12. Running the suites

```bash
scripts/robo test-css                                        # the whole CSS surface
Out/RoboTest.host.exe --filter css.color                     # this module only
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Test -Filter css.color -Sanitize
```

> **The sibling module is `docs/Units.md` (9.10).** Colours and numbers are the two
> CSS values whose *meaning* is arithmetic, and each got a module the same way and
> for the same reason: the computed-value layer was their only reader until a second
> one arrived. The two boundaries are asserted against each other in
> `css.unit.computed.coexists_with_colours` — a numeric value is not a colour and a
> colour is not a numeric value — because widening one vocabulary is exactly how a
> value gets accepted by the wrong property.

The suite is `css/tests/SuiteRoboCssColor.c`, 19 tests in three groups:

- **the record** — kinds, predicates, kind-aware equality, the cleared state,
  the outcome vocabulary, and serialization;
- **the syntaxes** — the named set walked whole, all four hex lengths, `rgb()` and
  `hsl()` in both styles with their clamping and normalisation, `transparent`,
  `currentColor`, the deferred spaces, and the malformed shapes;
- **integration** — colours through the computed-value layer and the computed
  style: declared forms, inheritance, the CSS-wide keywords, the two `currentColor`
  rules, a `var()` reference beside a misspelling, the full selector pipeline over a
  three-level tree, and determinism.
