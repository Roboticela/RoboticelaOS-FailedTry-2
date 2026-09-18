# RoboCSS Units and Numbers (Phase 9.10)

Most of what a CSS declaration says is a number with something attached to it. This
phase is the *something*: the unit, the number's shape, and the one place that turns
a written length into pixels — but only once enough is known to do it honestly.

Its sibling is `docs/Colors.md`. 9.9 took the colour model out of the computed-value
layer because a second reader arrived; 9.10 does exactly the same thing for the unit
vocabulary, for exactly the same reason.

```
CSS text
   ↓  tokenizer (9.2)              `10px` → DIMENSION, `50%` → PERCENTAGE, `1e2` → NUMBER
   ↓  grammar (9.3)                declaration → component-value tree
   ↓  RoboCSSUnit                  the token → one typed numeric value
   ↓  cascade (9.5) + specificity (9.6)
   ↓  inheritance (9.7)            the parent's *computed* value, unit and all
   ↓  computed values (9.8)        absolute units → px; contextual units carried
   ↓  RoboCSSUnit resolution       `2em`, `50vh` → px, given a context
   ↓  layout (a later phase)       the containing block, the used value
```

| Goal | What it delivers |
|---|---|
| 9.10.1 the set | A closed vocabulary of 31 units in four families, plus NONE, with one table row each |
| 9.10.2 absolute conversion | The reference-pixel relationships, exact and context-free |
| 9.10.3 the context | What a relative unit needs, with a `has_` flag per fact |
| 9.10.4 resolution | `robo_css_unit_resolve_px` — a *question* with four answers, one of which is "not yet" |
| 9.10.5 the value | `robo_css_numeric_t`: number, integer, percentage or length, with kind-aware equality |
| 9.10.6 the parse | Five outcomes, so "legal CSS I cannot evaluate" stays apart from "a mistake" |
| 9.10.7 serialization | One canonical spelling, so two equal values cannot print differently |

## 1. The unit set

```c
typedef enum robo_css_unit {
    ROBO_CSS_UNIT_NONE = 0,
    ROBO_CSS_UNIT_PX = 1, ... ROBO_CSS_UNIT_PC = 7,        /* absolute */
    ROBO_CSS_UNIT_EM = 8, ... ROBO_CSS_UNIT_RLH = 17,      /* font-relative */
    ROBO_CSS_UNIT_VW = 12, ... ROBO_CSS_UNIT_DVMAX = 31    /* viewport */
} robo_css_unit_t;
```

The enumerator values are **append-only**. 9.8's seven absolute, four font-relative
and four viewport units keep the numbers they had, and 9.10 appends `lh`, `rlh`,
`vi`, `vb` and the twelve `sv*`/`lv*`/`dv*` units. A stored enumerator is a
contract; the order within a family is documentation rather than one.

`PERCENTAGE` is not in the set, and that is 9.8's decision kept: `%` is a *kind*
(§4), because 50% of a width and 50% of a line height are different computations.
It is a proportion, not a length whose unit is a sign.

Everything about a unit is one row:

```c
typedef struct unit_entry {
    const char *name;
    robo_css_unit_t unit;
    robo_css_unit_class_t unit_class;
    double to_px;                       /* 0.0 when there is no fixed factor */
    robo_css_viewport_family_t family;  /* which viewport, for a viewport unit */
    unit_viewport_measure_t measure;    /* which dimension of it */
} unit_entry_t;
```

There is one row per unit and every accessor is a lookup in it, which is the
property worth protecting: adding `dvw` is adding a row rather than adding a branch
to four functions. `css.unit.vocabulary` asserts the table by walking it —
every enumerator's spelling resolves back to that enumerator, and the count is
pinned — so a row whose name and enumerator columns disagreed fails there.

`robo_css_unit_from_slice` is whole-spelling and ASCII case-insensitive: `10PX` is
`10px`, and `10pixels` is *not* `10px`. A prefix match is how `pixels` becomes a
length.

### The four families

| Family | Units | What the family means |
|---|---|---|
| `ABSOLUTE` | `px` `cm` `mm` `Q` `in` `pt` `pc` | A fixed factor to px; needs no context |
| `FONT_RELATIVE` | `em` `rem` `ex` `ch` `lh` `rlh` | Needs the element's or the root's font metrics |
| `VIEWPORT` | 18 units across 4 viewport families | Needs a viewport size |
| `NONE` | (NONE) | Not a length |

`robo_css_unit_is_absolute` is asked of the **table**, not of `to_px`. That is a
deliberate consequence of a defect in 9.8's version: `to_px` answers with the value
it was given when a unit has no fixed factor, so a predicate written as
`to_px(1.0, unit) != 0.0` reports `em`, `vh` and the empty unit all as absolute —
and `em` and `vh` would then be silently converted to pixels in the computed value,
which is precisely what 9.8 exists not to do. The factor being *present* is the fact
the predicate is about.

## 2. Absolute conversion is a definition

CSS's absolute units are defined in terms of the reference pixel:

| Relationship | Factor |
|---|---|
| `1in` = `96px` | 96.0 |
| `1cm` = `96/2.54px` | ≈ 37.7952755905512 |
| `1mm` = `1/10cm` | ≈ 3.77952755905512 |
| `1Q` = `1/40cm` | ≈ 0.94488188976378 |
| `1pt` = `1/72in` | ≈ 1.33333333333333 |
| `1pc` = `12pt` | 16.0 |

`robo_css_unit_to_px` and the resolver's `ABSOLUTE` arm both multiply by that
factor and consult nothing else. **Nothing here knows about DPI**, and that is the
point: an absolute length means the same thing on every display, and only a *used*
value — a later phase's — may care about the device. A conversion that read the
monitor would make `1in` a different length on a different laptop.

Converting is *exact by definition*, so the test suite asserts equality within a
tolerance rather than an approximate match, and it asserts the relationships both
ways round (`40Q` is `1cm` *and* `4Q` is `1mm`), because a factor out by forty
passes a test that checks one direction.

## 3. The context

A relative unit is a different kind of thing from an absolute one. `2em` is not a
length until something says what the font size is, and a module that guessed would
produce a number that looked computed and was wrong.

```c
typedef struct robo_css_unit_context {
    int has_font_metrics;       double font_size, x_height, zero_width, line_height;
    int has_root_font_metrics;  double root_font_size, root_line_height;
    int has_viewport;           double viewport_width, viewport_height;
    int has_small_viewport;     double small_viewport_width, small_viewport_height;
    int has_large_viewport;     double large_viewport_width, large_viewport_height;
    int has_dynamic_viewport;   double dynamic_viewport_width, dynamic_viewport_height;
    int vertical_writing;
} robo_css_unit_context_t;
```

Every value is paired with a `has_` flag, and the flags are the design: a context
that said `font_size = 16` without saying it *has* one would make `2em` resolve
against a number nobody supplied, and "I do not know the font size" and "the font
size is zero" are different answers. The setters are the only way to fill one, so a
writer cannot set a value without its flag.

The context is an **argument**, never a global. That is what keeps a resolution a
pure function of its inputs, and it is what lets two resolutions at two font sizes
happen without one of them reading the other's state.

### Why small/large/dynamic viewport is a fallback chain

CSS Values 4 defines four viewports, and they differ only when something is hiding:
the *small* one is what is left when the browser's chrome is showing, the *large*
one when it is not, and the *dynamic* one is whichever applies right now. `vw`,
`vh`, `vmin` and `vmax` are the large viewport — and the family this build calls
`DEFAULT`.

That shape is why the context carries **one default viewport plus three optional
overrides**. A caller that has a window and no dynamic chrome — which is every
caller today — sets the default and gets `vw` and `svw` and `dvw` all answering the
same size, which is exactly right for a window whose chrome never moves. A caller
that has the three sizes sets them and gets three different answers. Neither case
needs a flag on the *unit*.

`vi` and `vb` are the inline and block axes, so they are `vw` and `vh` in a
horizontal writing mode and swapped in a vertical one. `writing-mode`'s initial
value is `horizontal-tb`, so `vertical_writing = 0` is the correct default rather
than a placeholder, and the flag is the extension point for when that property is
computed.

## 4. Resolution is a question, not a conversion

```c
typedef enum robo_css_length_resolution {
    ROBO_CSS_LENGTH_RESOLVED = 0,        /* *out_px holds the length */
    ROBO_CSS_LENGTH_NEEDS_CONTEXT = 1,   /* the unit is fine; the context is not */
    ROBO_CSS_LENGTH_NOT_A_LENGTH = 2,    /* NONE, unknown, or not a length */
    ROBO_CSS_LENGTH_INVALID = 3          /* a magnitude or context input that is not finite */
} robo_css_length_resolution_t;
```

`NEEDS_CONTEXT` is the answer that matters. `50vh` with no viewport is not an error
— it is a value that cannot be resolved *yet*, and saying so is the entire reason
this is a function with four outcomes rather than a converter that multiplies by
something. Collapsing `NEEDS_CONTEXT` into "failed" is how a caller ends up
substituting a default it invented.

The distinction between `NEEDS_CONTEXT` and `INVALID` is the same kind of
distinction: a caller that *did* supply a font size and supplied one that is not a
finite number can fix that; a caller that supplied nothing can only ask for it.

`0` is not an exception. `0em` with no context is still `NEEDS_CONTEXT`, because
what is missing is the context and not the magnitude — a resolver that shortcut zero
would be answering a question nobody asked.

### The two doors, and why they are two

| Door | Answers | When it refuses |
|---|---|---|
| `robo_css_unit_to_px` | "what is this in px" — a multiplication | Never; returns its input when there is no factor |
| `robo_css_unit_resolve_px` | "what is this, given what you know" — a question | With one of four outcomes |

They are deliberately different. `to_px` is the exact, context-free arithmetic;
`resolve_px` is the one that can answer "not yet". A caller that wants the first
must not ask whether a unit is absolute, and a caller that wants the second must not
assume it got a number.

`out_px` is **written only for `RESOLVED`**, which is what makes "check the outcome,
then read the value" a safe order. A refusal leaves the caller's variable alone, and
that is asserted.

## 5. The typed numeric value

```c
typedef struct robo_css_numeric {
    robo_css_numeric_kind_t kind;   /* NONE | NUMBER | INTEGER | PERCENTAGE | LENGTH */
    double number;
    robo_css_unit_t unit;           /* LENGTH only */
} robo_css_numeric_t;
```

A flat record of a tag, a double and an enumerator, owning nothing — so it can be
copied, compared and stored without a lifetime rule, exactly like a colour.
`robo_css_numeric_make_*` is the only way to construct one, so a PERCENTAGE cannot
come to carry a unit it did not mean.

### Four kinds and no fifth

They are separate because CSS's grammar separates them:

* a property whose grammar says `<integer>` must refuse `1.5`;
* it must refuse `1.0` too, because `1.0` is a *number token* — and refusing it is
  the whole point of the distinction. A single "numeric" kind would invite exactly
  that silent coercion, and the result would be a plausible wrong value.
* `50%` is not `50`. Converting a percentage to a number is a claim about what it is
  a percentage *of*, and computing a style has no containing block.

There is **no `INVALID` kind**. A value that is not one of the four is not a numeric
value, which is a fact about the *parse* (§6) rather than about the value, so the
type stays one whose every instance is a number of a known shape. A "cleared"
record is `NONE`, which is the absence of one.

### Two questions about wholeness, and why both exist

| Question | Function | `1.0` | `10` |
|---|---|---|---|
| Is the *value* whole? | `robo_css_numeric_is_integral` | yes | yes |
| Is the *grammar* an integer? | `robo_css_numeric_integer_value` | **refused** | `10` |

And conversion refuses rather than rounds or truncates: `robo_css_numeric_integer_value`
requires kind `INTEGER` and refuses an over-large magnitude rather than casting a
value the cast cannot hold, which is the whole of "avoid overflow and undefined
behaviour" here. The bound is `ROBO_CSS_NUMERIC_INTEGER_LIMIT` = 2^53, which is
where a `double` stops representing every integer — a bound that is the
*representable* limit rather than an arbitrary one is a bound a caller can reason
about. The range check comes **before** the cast, in that order, because a cast of a
double outside the integer range is undefined behaviour and a function that asked
"is this whole" by casting would have already invoked the bug it guards against.

### Equality is kind-aware and unit-aware

`0` is not `0%`. `0px` is not unitless zero. `10px` is not `10em`. All three
distinctions are deliberate: a comparison that ignored them is how a value gets used
as a length when it was a proportion, and converting for comparison would answer a
question about a context the comparison does not have.

## 6. Five parse outcomes

```c
typedef enum robo_css_numeric_parse {
    ROBO_CSS_NUMERIC_PARSE_INVALID = 0,      /* not a numeric value at all */
    ROBO_CSS_NUMERIC_PARSE_RESOLVED = 1,     /* one of the four kinds */
    ROBO_CSS_NUMERIC_PARSE_DEFERRED = 2,     /* calc/min/max/clamp/var/env */
    ROBO_CSS_NUMERIC_PARSE_UNKNOWN_UNIT = 3, /* `10foo` */
    ROBO_CSS_NUMERIC_PARSE_NON_FINITE = 4    /* `1e999` */
} robo_css_numeric_parse_t;
```

Five, because they tell a developer five things:

| Outcome | Input | What it says |
|---|---|---|
| `RESOLVED` | `10px`, `50%`, `1.5` | here is the value |
| `DEFERRED` | `calc(100% - 20px)`, `var(--w)` | legal CSS this build does not evaluate |
| `UNKNOWN_UNIT` | `10furlongs` | the number and the shape are fine; the unit is one nobody knows |
| `NON_FINITE` | `1e999` | legal syntax, unrepresentable magnitude |
| `INVALID` | `red blue`, `1.2.3`, `10 px` | not a numeric value |

`DEFERRED` is the one that keeps the phase from lying. A `calc()` expression is
arithmetic over lengths that needs a containing block, and `var()` is a
custom-property substitution; neither is a number yet, and both are legal CSS.
Calling them mistakes would be a lie about the author, and evaluating them here
would be a lie about the value. They are carried — by the caller, with their tokens
entire — and the extension point is the four kinds above plus a future expression
kind, not a half-evaluated number that quietly dropped its `%`.

### There is no `MALFORMED`, and that is a finding

The colour module has one, because `rgb(1)` is *one* function component that is
syntactically fine and semantically wrong.

Writing a number wrong does not have that shape. The tokenizer is lossless and the
grammar is total, so:

* `1.2.3` arrives as three components — not one numeric value, therefore `INVALID`;
* `1.` arrives as a number beside a delimiter — the same;
* and `1e` does not reach the parse as a dimension, because the tokenizer falls back
  to `NUMBER` rather than emitting a unitless `DIMENSION`.

The only remaining way to be malformed is to build a token by hand that the
tokenizer cannot produce, and a member nothing can reach would be documentation of a
state that does not exist.

`robo_css_numeric_from_value` requires **exactly one** significant component, which
is why a declaration's value list is not enough on its own: `10 20` is two numbers
and therefore not one.

## 7. The canonical spelling

`robo_css_numeric_write` renders one spelling, and the rules are the ones a reader
can check by eye:

| Rule | Example |
|---|---|
| An integral magnitude has no decimal point | `10`, not `10.000000` |
| A fractional one has no trailing zeroes | `1.5`, not `1.500000` |
| A negative zero is `0`, because it is zero | `-0` → `0` |
| A magnitude below the fixed-point precision is written compactly | `1e-07`, not `0` |
| A percentage is written with its sign | `-12.5%` |
| A length is written with the unit's *canonical* spelling | `10PX` → `10px` |

The tiny-magnitude rule is the one that is a decision rather than a convention:
`1e-9` rounded to six decimals is `0.000000`, and rendering a value that is not zero
as `0` would be a lie that a layout would then act on. The compact form round-trips.

Hex-float and `%g`-everything were both available and both rejected: the pinned shape
of every number this subsystem has emitted since 9.8 is fixed-point, and a
serialization that changed notation with the magnitude would be harder to read and no
more correct.

A value whose text does not fit its buffer is **refused** rather than truncated
(`ROBO_NOT_SUPPORTED`), with `out_length` telling the caller how much room it needed
— so a caller learns instead of silently receiving half a value.

## 8. What 9.10 does *not* do

| Not here | Where it belongs |
|---|---|
| Resolving a relative length against a font or viewport | whoever has those facts; this module provides the resolution *door*, not the context |
| The containing block, and therefore any used value | layout |
| `calc()` / `min()` / `max()` / `clamp()` evaluation | deferred and carried, per §6 |
| `var()` substitution | deferred and carried, per §6 |
| Any new property | 9.10 adds no property. The registry's `computation` column already says which shape a property is, and `width`/`height`/`font-size`/`letter-spacing`/`word-spacing`/`text-indent` are already `LENGTH` |
| Shorthand expansion (`margin: 0 auto` → four longhands) | a later phase; the registry classifies the shorthands `PRESERVE` |
| `URL`-relative or `attr()` values | not part of CSS's numeric grammar |

## 9. Integration with the computed-value layer

The computed-value layer **consumes** this module and holds no unit table of its
own. Two consequences, and both are the phase's contract with 9.8:

* a **computed LENGTH** normalises an *absolute* unit to `px` — 9.8's rule,
  unchanged, because CSS's absolute factors are definitions and the conversion is
  exact — and carries a *relative* or *viewport* unit with its number and its unit
  live, because only whoever has the font or the viewport can resolve it;
* a computed **PERCENTAGE** stays a percentage, and the *kind* keeps it
  distinguishable from a length.

`computed_compute_length` asks the module what the value *is*, then decides whether
that is something a length-shaped property may hold. The five outcomes map onto
different diagnostics, because a developer reading "unknown length unit", "a bare
number is not a length" or "not a finite number" learns three different things.

**A keyword is asked first, and that ordering is load-bearing.** `auto` is one IDENT
*token*, so a branch guarded by "there is no single token" never reaches it: the
identifier falls through to the numeric parse, which is right to call an identifier
not a number, and the developer is told "not a length" when the property's grammar
simply does not accept the keyword they wrote. The keyword list belongs to the
property; the numeric vocabulary belongs to the module; asking the property first is
what keeps those two diagnostics apart.

The value layer owns *which* numeric a computed value is. The module owns how a
number is spelled — because a number formatted one way here and another way there is
the same class of defect as two colour parsers, and `10px`, `10%` and `10` have to
agree about how ten is written.

## 10. Ownership

Nothing here owns anything.

* A unit is an enumerator.
* A context is a plain struct of numbers, filled only through setters so a value
  cannot arrive without its flag.
* `robo_css_numeric_t` is a tag, a double and an enumerator **by value** — which is
  what lets one be copied, compared and stored without a lifetime rule.

The one borrowed input is a component-value tree, and `robo_css_numeric_from_value`
reads it and copies nothing out of it. There is no global mutable state and no
cache, so a resolution is a pure function of its arguments.

## 11. Numeric safety

Every magnitude this module *keeps* has been checked finite, because a NaN or an
infinity reaching a style is how one bad declaration poisons a whole layout.

| Guard | Where |
|---|---|
| The four constructors refuse a non-finite magnitude | `make_number`/`make_percentage`/`make_length` |
| The parse reports `NON_FINITE` apart from `INVALID` | `robo_css_numeric_from_token` |
| The resolver refuses a non-finite magnitude *or* context input | `robo_css_unit_resolve_px` |
| A product that overflows is `INVALID`, not infinity | the resolver's final finiteness check |
| The range check precedes the integer cast | `is_integral` / `integer_value` |

The finiteness test is `value == value && (value - value) == 0.0`, which is
`RoboCSSInternal.h`'s and needs no `<math.h>` — the runtime is freestanding on the
guest side, so the module uses no `math.h` at all. `fmod` is not needed, `fmin` and
`fmax` are one comparison each, and the writer's formatting is `snprintf`.

## 12. Running the suites

```bash
scripts/robo test-css                                        # this subsystem's suites
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Test          # the whole host suite
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Test -Sanitize # with ASan + UBSan
```

Three suites carry the phase:

| Suite | Tests | Subject |
|---|---|---|
| `css/tests/SuiteRoboCssUnit.c` | 18 | The vocabulary, resolution, the typed value, the outcomes, the spelling, and the computed-value integration |
| `SuiteRoboCssComputedValue.c` | 14 | The 9.8 normaliser, whose `LENGTH` rows are now the module's |
| `Tests/Suites/SuiteRoboCssDomSelect.c` (`css.dom.unit.pipeline`) | 1 | The whole chain over the visual checkpoint's real document |

See `docs/Tests.md` for what each asserts, and §14 of that document for the defects
the suites found.
