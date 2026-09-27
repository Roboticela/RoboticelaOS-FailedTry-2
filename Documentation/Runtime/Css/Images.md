# RoboCSS and the Image Family (Phase 18.16)

This document is the authority for the sixteenth thing Phase 18 adds to RoboCSS:
the **image family**. It is a *property* family rather than a renderer — every later
phase that loads a file, samples a gradient or fits a replaced element reads these
values — and it is the family whose central fact is that **an `<image>` is not a
file**.

The module is `Runtime/Source/RoboCSS/RoboCSSImage.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSImage.h`.

## 1. What the phase is, and the three facts worth reading twice

CSS gives a document one data type for "some pixels to draw" — `<image>` — and a
handful of properties that decide *which* pixels and *how*. This phase types the
vocabulary of the sets that *are* closed, carrying the declarations whose grammar is
wider. Three facts decide how.

**An `<image>` is a description of where pixels come from.** A `url("a.png")` names
one, a `linear-gradient(...)` computes one, an `image-set(...)` chooses between
several and a `cross-fade(...)` composes them. What they have in common is not a byte
stream but a description, and the one fact the value itself carries is the name of
the function that describes it. So the family's largest table is the function
vocabulary — thirteen functions with the classifications a consumer dispatches on:

| function                       | gradient | repeating | axis     | intrinsic size | resource |
|--------------------------------|----------|-----------|----------|----------------|----------|
| `url()`                        | no       | no        | —        | yes            | yes      |
| `src()`                        | no       | no        | —        | yes            | yes      |
| `image()`                      | no       | no        | —        | yes            | no       |
| `image-set()`                  | no       | no        | —        | no             | no       |
| `cross-fade()`                 | no       | no        | —        | no             | no       |
| `element()`                    | no       | no        | —        | yes            | no       |
| `paint()`                      | no       | no        | —        | no             | no       |
| `linear-gradient()`            | yes      | no        | linear   | no             | no       |
| `radial-gradient()`            | yes      | no        | radial   | no             | no       |
| `conic-gradient()`             | yes      | no        | conic    | no             | no       |
| `repeating-linear-gradient()`  | yes      | yes       | linear   | no             | no       |
| `repeating-radial-gradient()`  | yes      | yes       | radial   | no             | no       |
| `repeating-conic-gradient()`   | yes      | yes       | conic    | no             | no       |

The columns are independent rather than a tree. A repeating gradient is *also* a
gradient and is on an axis; a non-gradient is on **no** axis, so the three axis
predicates are false together rather than complementing one another. And "does the
value state an intrinsic size" is a separate question from which kind it is:
`image-set()` resolves to one of its alternatives and therefore inherits *that*
image's size, while as a value it states none.

**`object-fit`'s five words are not one scale factor.**

| value        | preserves ratio | scales | clips | compares to the box |
|--------------|-----------------|--------|-------|---------------------|
| `fill`       | no              | yes    | no    | no                  |
| `contain`    | yes             | yes    | no    | no                  |
| `cover`      | yes             | yes    | yes   | no                  |
| `none`       | yes             | no     | yes   | no                  |
| `scale-down` | yes             | yes    | no    | yes                 |

Three rows in that table are why the classifications exist. `none` shows the source's
aspect ratio by **not scaling at all** — so "preserves the ratio" is not "scales", and
a consumer that read one as the other would scale an image the property promised to
leave alone. `fill` is the only value that distorts. And `scale-down` is the one value
whose answer depends on comparing the image's intrinsic size with the box, a number
the value does not carry, so a consumer must have it before it can decide.

**`image-rendering: auto` is not `smooth`.** The five spellings split into a value that
leaves the choice to the user agent (`auto`, the initial, in neither group below), two
that promise an *interpolating* algorithm (`smooth`, `high-quality`) and two that
promise to keep the source's pixels (`crisp-edges`, `pixelated`). And those last two
are **two different promises**: `crisp-edges` says "do not blur" and accepts any
non-smoothing algorithm, while `pixelated` names nearest-neighbour and may show the
pixel grid. So the table carries four columns rather than a smoothing flag, and a
consumer that read "not smoothing" as "pixelated" would pin the very choice `auto`
leaves open.

## 2. The split by grammar

The phase registers five rows, and they split by *grammar* rather than by importance.
Two take a typed shape:

```text
image-rendering   auto | smooth | high-quality | crisp-edges | pixelated
  └── automatic          auto                (the user agent's choice, in neither group)
  └── smoothing          smooth, high-quality
  └── pixelated          crisp-edges, pixelated
  └── shows pixel grid   pixelated           (nearest-neighbour specifically)
object-fit        fill | contain | cover | none | scale-down
  └── preserves ratio    contain, cover, none, scale-down
  └── scales             fill, contain, cover, scale-down
  └── clips              cover, none
  └── scales down only   scale-down
```

and three are registered as **carried declarations** (`PRESERVE`) because their real
grammar is wider than any closed set, with the closed sets *inside* them typed here for
a consumer to read — 18.7's split for `transform` and 18.15's for the decoration
longhands made once more:

```text
image-orientation   `from-image | none | <angle>` (with an optional `flip`)
  └── is from image        from-image   (the file's own EXIF orientation, the initial)
  └── is none              none         (ignore any recorded orientation)
image-resolution    `[ from-image || <resolution> ] && snap` — no vocabulary of its own here
object-position     the position pair the background module owns (9.14)
```

`image-orientation`'s pair is a genuine distinction rather than two spellings of "use
the file": `from-image` reads the image's own recorded orientation — a fact about the
*file* rather than a policy — while `none` ignores it. That is the same shape as
`text-decoration-thickness`'s `from-font`, and it is why `from-image` is the initial
and why an unknown value answers its behaviour: discarding an author's rotated photo
is exactly what a build that cannot read the value must not do.

`object-position` is carried rather than re-typed because it is the position *pair*
`background-position` already owns; a second grammar for the same pair is how
`background-position: 25% 50%` and `object-position: 25% 50%` would come to disagree
about what a percentage is. The phase that places a replaced element reads the value
through the background module.

## 3. The inherited half, and why it is the half it is

Three of the five rows are inherited and two are not, and the split is the family's
home stated as a count: how an image is *rendered*, how a file's own orientation is
*read* and its resolution are settings of the element that holds the image, so a child
inside a paragraph that displays the same image sits on the same rules. How a replaced
element's content is *fitted to a box* is the box's own business, so `object-fit` and
`object-position` do not propagate.

```text
inherited          image-orientation, image-rendering, image-resolution
non-inherited      object-fit, object-position
```

## 4. Non-goals

- **No loader or decoder.** No function here opens a file, reads a header, resolves a
  URL, follows a `srcset` descriptor or decodes any pixel format. `url()` is a
  reference this build can hold and cannot fetch, exactly as 9.9 holds `oklch()` and
  9.14 holds a `background-image`.
- **No gradient engine.** A gradient's arguments — an angle, a list of colour stops, a
  shape and size for a radial gradient — are not parsed here. The vocabulary types the
  *function name* a consumer switches on; sampling a stop belongs to the phase that
  draws pixels.
- **No renderer, scaler or filter.** `image-rendering` and `object-fit` name rules a
  renderer follows; nothing here interpolates, nearest-neighbours, scales or clips a
  pixel.
- **No layout.** `object-fit` and `object-position` are registered so the cascade and
  the computed style stay correct; placing a replaced element belongs to the phase
  that has a box.
- **No `image-resolution` vocabulary.** Its grammar is a `||` of two optional parts
  (the `snap` keyword and a `<resolution>`) that has no closed set; the declaration is
  carried whole.
- **No typed computed representation.** The two typed rows compute to a keyword
  carrying the canonical spelling rather than an enumerator in the computed value — the
  same choice 18.5's and 18.15's rows record — so what the closed set buys is the
  refusal (`image-rendering: banana` is a mistake) rather than a second table.

## 5. Capabilities and version

Phase 18.16 adds one flag to `CssCapabilities` — `images` — and one diagnostic domain
(`RoboCssDomainImage`), and takes the module to **0.34.0**. The flag claims the
thirteen-function vocabulary, the five registry rows and the carried half of the
family; it does not claim a loader, a decoder, a renderer, a gradient sampler or a
layout pass.

Two computations join the table (`ImageRendering`, `ObjectFit`), each a row in the
shared closed-vocabulary body the display and position shapes use. The registry moves
from 200 rows to 205 — three inherited and two non-inherited — and the computed style's
default count from 54 to 57.

## 6. Tests

`Tests/Css/SuiteRoboCssImage.c` (`css.image.*`, 8 cases) is the contract:

- each of the four vocabularies walked in enumerator order, with every keyword read
  back, the case-insensitive match, the absent/unknown `FromText` answers and the
  `SliceIsKnown` door that tells a genuine value apart from the default;
- the classifications per row — automatic/smoothing/pixelated/pixel-grid, preserves-
  ratio/scales/clips/scales-down-only, and the seven function predicates — plus the
  safe answers for an unknown enumerator;
- the pairs that make the classifications necessary: `auto` is in neither rendering
  group; `crisp-edges` and `pixelated` are two different promises (`shows pixel grid`
  is true of one); `none` preserves the object-fit ratio *without* scaling; a
  non-gradient is on no axis and `conic` is neither linear nor radial; `image-set()` is
  the function that composes rather than states a size;
- the serialization discipline: a too-small buffer is `RoboNotSupported` with the
  length it needed and the buffer left empty, and an unknown enumerator is
  `RoboInvalidArgument` (and the longest function name is the capacity the writer
  promises — the off-by-one is pinned);
- the two computed shapes through the real grammar, including a deferred `var()` and
  the refusal sentences, plus the **carried** half — an orientation angle, a resolution
  and a position pair all survive as PRESERVED tokens;
- the five registry rows with their shapes, their initial values and their inheritance;
- the capability flag, the version, the two computation names and the domain;
- determinism: the same declaration computes to the same value twice, and the two
  `image-rendering` rows that share a pixelated classification are different values.

The phase also moves every count that the wider build pins — the registry's row count
and its inherited/non-inherited split, `CSS_INHERITED_FAMILY`, the computed style's
default count, the conformance table's initials and declaration matrix, and the version
each family suite asserts — because a flag and a count are promises about the build
rather than about this module.
