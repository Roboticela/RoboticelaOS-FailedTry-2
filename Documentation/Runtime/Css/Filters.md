# RoboCSS and the Filter Family (Phase 18.19)

This document is the authority for the seventeenth thing Phase 18 adds to RoboCSS: the
**filter family**. It is a *property* family rather than a filter engine — every later
phase that renders a filter reads these values — and it is the first family this build
types whose **value is an ordered list of functions**, where the order is the meaning:
`blur(2px) grayscale(100%)` is not `grayscale(100%) blur(2px)`.

The module is `Runtime/Source/RoboCSS/RoboCSSFilter.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSFilter.h`.

## 1. What the phase is, and the three facts worth reading twice

CSS gives a document two properties that name a sequence of pixel operations —
`filter` and `backdrop-filter` — plus a set of eleven built-in `<filter-function>`s. The
property grammar is a list, carried; the phase's typed deliverable is the **name
vocabulary** the list is built from, exactly as 18.7's `CssTransformFunction` is the
name set behind `transform`. Three facts decide how it is read.

**A filter list is ordered, so it is a list and not a set.** `blur(2px)
grayscale(100%)` blurs the image and then drains its colour; `grayscale(100%)
blur(2px)` drains it and then blurs — different pixels. That is why the property is a
carried declaration and why the vocabulary types *names* rather than a whole value: the
order is a fact about the declaration, and a closed set cannot hold it.

**What a function does is three groups, and `url()` is in neither of the last two.**

| group      | functions                                                              |
|------------|------------------------------------------------------------------------|
| reference  | `url()`                                                                |
| spatial    | `blur()`, `drop-shadow()` — they move pixels                          |
| colour     | `brightness()`, `contrast()`, `grayscale()`, `hue-rotate()`, `invert()`, `opacity()`, `saturate()`, `sepia()` |

The split is the one a phase that has to know whether a filter can change *geometry*
asks first: a blur grows the painted area and a drop-shadow offsets a copy of it, while
every colour filter leaves the edges exactly where they were. `url()` is in neither —
it is a reference whose effect is the referenced filter's — so a consumer that read
"not spatial" as "colour" would apply a colour matrix to an SVG filter.

**What a function takes is four kinds of argument, and they do not line up with the
groups.**

```text
<length>   blur, drop-shadow
<angle>    hue-rotate
<color>    drop-shadow
amount     brightness, contrast, grayscale, invert, opacity, saturate, sepia
           (the seven whose omitted argument defaults to 1)
```

`drop-shadow` is the only function that takes both a colour and lengths, and
`hue-rotate` takes an angle and no amount — which is why the four `takes*` columns are
independent and not one enumeration. A consumer that parsed one kind as the other would
accept `blur(50%)` or refuse `brightness(2px)`. That the seven amount functions may be
written with **no argument at all** (defaulting to `1`) is why the grammar has to allow
a bare function token.

## 2. `none` is a value, not an empty list

```text
filter           none | <filter-function-list>   initial: none
backdrop-filter  none | <filter-function-list>   initial: none
```

`none` is the property's initial value and is a *value*: it says "apply no filter",
which is what an author who declared nothing gets. It is not the same statement as an
empty list, any more than `background-image: none` is the same as an image that happens
to have no pixels. Because the list grammar is wider than any closed set, both rows are
**carried** (`PRESERVE`), so the declaration — order included — survives intact and is
read by the phase that checks its function tokens against the vocabulary here.

## 3. What the phase adds to the registry

| Property | Shape | Initial | Inherited |
|---|---|---|---|
| `filter` | PRESERVE (a carried list) | `none` | no |
| `backdrop-filter` | PRESERVE (a carried list) | `none` | no |

Two rows, both non-inherited, because a filter is painted per element. There is no new
computation: a list is wider than any closed set, so the rows carry rather than compute,
and the vocabulary lives where a consumer can reach it without the normaliser pretending
the grammar is closed. That is 18.7's split exactly, restated for filters.

## 4. Non-goals

- **No filter engine.** Nothing here blurs a pixel, shifts a shadow, mixes a colour
  matrix or measures the result. The vocabulary says *which filter* a renderer applies;
  the pixels belong to the phase that has a surface.
- **No SVG pipeline.** `url(#f)` names an SVG filter element this build can hold and
  cannot resolve. It is a reference, and the classification says so rather than pretending
  it is an operation this build describes.
- **No typed filter value.** The list is carried; a grammar that type-checked each
  function's *arguments* (a length for `blur`, an angle for `hue-rotate`) would need a
  per-function argument model, and it is the grammar phase's, not this one's. What this
  phase provides is the facts that model would dispatch on.
- **No `mix-blend-mode`.** The blend-mode vocabulary is 18.17's (`background-blend-mode`);
  `mix-blend-mode` is a separate property and is not added here.
- **No filtering applied to an image.** `filter` on a *background image* is not a
  thing CSS has; the property applies to the element's rendering.

## 5. Capabilities and version

Phase 18.19 adds one flag to `CssCapabilities` — `filters` — and one diagnostic domain
(`RoboCssDomainFilter`), and takes the module to **0.37.0**. The flag claims the eleven
function names, their classifications, and the two carried rows; it does not claim a
filter engine, an SVG pipeline or a type-checked argument grammar.

No computation joins the table, because no property's grammar *is* a closed set here —
the two rows are carried. The registry moves from 224 rows to 226, both non-inherited.

## 6. Tests

`Tests/Css/SuiteRoboCssFilter.c` (`css.filter.*`, 6 cases) is the contract:

- the eleven functions walked in enumerator order, with every name read back, the
  case-insensitive match, the absent/unknown `FromText` answers and the `SliceIsKnown`
  door that tells a genuine name apart from the default;
- the three "what it does" columns — reference, spatial, colour — including that `url`
  is in neither of the latter two, and the four "what it takes" columns — length, angle,
  amount, colour — including that `drop-shadow` takes both a length and a colour;
- every classification's safe answer for an unknown enumerator;
- the serialization discipline: a too-small buffer is `RoboNotSupported` with the length
  it needed and the buffer left empty, and an unknown enumerator is `RoboInvalidArgument`;
- the carried computed values, including **order**: the two orderings of one pair of
  functions are different values;
- the two registry rows with their shape, initial and inheritance;
- the capability flag, the domain and the version;
- determinism: the same declaration computes to the same value twice, and the reversed
  order does not.

The phase also moves every count the wider build pins — the registry's row count and its
non-inherited split, the capability list, the conformance table's initials and
declarations, and the version each family suite asserts.
