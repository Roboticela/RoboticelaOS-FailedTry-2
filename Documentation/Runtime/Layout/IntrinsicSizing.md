# Intrinsic sizing (Phase 19.1)

How wide a box's **contents** ask to be, before any containing block has a say:
the width of the widest unbreakable piece (`min-content`), the width of the whole
content on one line (`max-content`), and the clamp between them (`fit-content`).
Every formatting context that has to shrink a box to fit — a float, an
inline-block, an absolutely positioned box with no width — needs those two numbers,
and CSS spells three of them as `width` and `height` values.

Files: `Include/RoboLayout/RoboLayoutIntrinsic.h`,
`Source/RoboLayout/RoboLayoutIntrinsic.c`; `RoboLayoutSizing.h/.c` gained the
`INTRINSIC` dimension kind, the `intrinsic` record on `LayoutSizing` and the
resolution branch; `RoboLayoutTypes.h/.c` gained `RoboLayoutDomainIntrinsic`.
Suite: `layout/tests/SuiteRoboLayoutIntrinsic.c` (16 cases, `layout.intrinsic.*`).
Version: `0.18.0`; capability: `LayoutCapabilities.intrinsicSizing`.

## 1. What an intrinsic size is, and what it deliberately is not

An intrinsic size is a *measurement of the contents taken before anything is
placed*. That is the whole reason this is a layer of its own rather than a step
inside the block pass: a layer that computed an intrinsic width by laying the
document out would be answering "how wide do the contents want to be?" with its own
answer to "how wide did I make them?".

```
   the layout tree 10.1 built
        │
        ▼
   LayoutIntrinsicMeasureObject   ← walks the tree, asks the metrics seam
        │
        ▼
   LayoutIntrinsicSize            ← min-content, max-content, two line extents
        │
        ▼
   LayoutSizingSetIntrinsic       ← the caller records the answer on a sizing record
        │
        ▼
   LayoutSizingResolveWidth       ← an INTRINSIC width resolves from it
```

The measurement writes **no result**, changes **no coordinate** and caches
nothing: it is callable before a pass has run, and it is callable again after one.
It is an *input* to layout, which is why it lives beside the sizing layer's other
inputs (`LayoutSizingSetContent` records the extent after children were laid out;
`LayoutSizingSetIntrinsic` records a measurement taken before) rather than inside
the pass.

## 2. Three kinds, and a fourth dimension kind

`LayoutIntrinsicKind` is four enumerators: `NONE` (not an intrinsic request),
`MIN_CONTENT`, `MAX_CONTENT` and `FIT_CONTENT`.

`fit-content` is **not** a third measurement. It is

```
fit-content(available) = clamp(available, min-content, max-content)
```

and `LayoutIntrinsicResolveFitContent` is that arithmetic in one place so no caller
restates it. `LayoutIntrinsicSizeWidth` answers min-content for `MIN_CONTENT` and
max-content for `MAX_CONTENT`, `NONE` and `FIT_CONTENT` (fit-content needs an
available width, which is why it is the separate door).

The dimension this phase adds is a **fourth kind** rather than a flag on a declared
length, for exactly the reason 10.8 wrote three kinds rather than a number with a
flag: a `width: max-content` and a `width: 300px` are different *statements*, and
the difference survives resolution so a diagnostic, an invalidation rule and a
later goal can tell "the author named a length" from "the contents decided". The
kind carries *which* intrinsic width in `LayoutDimension.intrinsicKind`, and a
dimension naming `NONE` is **invalid** — `LayoutDimensionIsValid` refuses it, the
same way it refuses a negative definite length.

Three kinds of dimension became four; four sources became five
(`RoboLayoutDimensionSourceIntrinsic`), because the same resolved number can now be
the answer to a fourth question.

## 3. Where the numbers come from: the seam, not a character count

`min-content` of a text run is the width of its **widest word**. The words are the
unbreakable pieces: the node's bytes are decoded through RoboUnicode in REPLACE
mode, split on CSS's collapsible whitespace (space, tab, LF, FF, CR —
`LayoutTextIsCollapsibleWhitespace`'s set), and each maximal non-whitespace segment
is measured with **its own code-point range** through the one metrics seam 10.5
built (`LayoutTextMetrics`). `max-content` is the whole node measured once,
unwrapped.

Two facts follow, and both are asserted rather than assumed:

- **The unit is a code point, not a byte.** `héllo wörld` is eleven code points and
  thirteen bytes; a build that counted bytes would answer 130 where this one
  answers 110. `layout.intrinsic.text_multilingual` reads the node's code-point count
  back through `LayoutTextNodeCodePoints` and asserts both numbers, so the case
  proves the decode rather than agreeing with it.
- **The measurement is shaped, not invented.** Every span goes through
  `LayoutTextMeasure`, which means a build with **no metrics source** measures every
  run as zero — the degradation 10.5 documents — rather than deriving a width from a
  byte count. `layout.intrinsic.unmeasured` asserts the zero.

A node that is empty, or entirely collapsible whitespace, has **no word**: its
`min-content` is zero (the narrowest the content can be) while its `max-content`
is still the whole node measured once. That is the same distinction the builder
draws when it drops a whitespace-only text node, seen from the measurement side.

## 4. Combining a subtree: maximum for min-content, a running line for max-content

A text object measures its node. A box combines its children, and the rule is the
one that makes the two names mean what CSS says:

| Field | Rule |
|---|---|
| `minContent` | the **maximum** of the children's min-content widths — the widest unbreakable piece anywhere in the subtree, which is the narrowest the box can be without breaking a piece |
| `maxContent` | the widest line the content would form on **one line**: a running sum of consecutive inline-level children, and the maximum over that sum and every block-level child's own max-content width |
| `minContentHeight` | the maximum child extent beside the min-content width |
| `maxContentHeight` | the maximum over the max-content lines |

A **block-level child starts its own line**: it is not summed with its siblings, and
the line that was accumulating ends there. A **text object is inline-level by
construction** (10.5), so it always sums; a box's level comes from
`CssDisplayIsInlineLevel`/`CssDisplayIsInlineBlock` on its own `display`. That is
the entire combination rule, and it is asserted from both directions:
`layout.intrinsic.inline_sum` (two `<span>`s sum to 40 while min-content stays 20)
and `layout.intrinsic.block_reset` (two `<p>`s do not sum: 40, not 60).

A box with no children, or one whose children are all empty, answers the zero size —
which `LayoutIntrinsicSizeIsValid` reports as **valid**, because content that asks
for nothing is an answer rather than a failure.

The recursion walks `firstChild` → `NextSibling` from `LayoutObjectGet`, and it does
not use a test macro: this is production code, and the module's rule that
production never calls `RoboCheck*` is what the first draft of this file got wrong
and the rewrite fixed.

## 5. The CSS seam, and the one reader

The phase added **no new property read**. `LayoutSizingReadDimension` already turns
a computed style into a `LayoutDimension`, and it is the one place a described
`width`/`height` becomes a size. What an intrinsic keyword needs is a classification
the existing reader can produce: a `max-content` keyword is an `INTRINSIC`
dimension, not an `UNRESOLVED` one and not `AUTO`.

The reader's rule (10.8's four checks) is unchanged in shape: an absent entry is
`AUTO`; a keyword spelling `auto` is `AUTO`; an intrinsic keyword is `INTRINSIC`;
any other keyword is `UNRESOLVED`; a px `LENGTH` is `DEFINITE`; a relative unit is
`UNRESOLVED`; and a percentage is its own kind since 19.2 (`PERCENTAGE`, its magnitude
kept -- see `PercentageResolution.md`, which refined this sentence). There is no new
door here, and no second interpretation of a property in the module.

## 6. Resolution, and the degradation

`LayoutSizingResolveWidth` gained one branch:

```
INTRINSIC  and  has_intrinsic   the fit-content clamp (plus an available width when
                               there is one), or the named intrinsic width
                               source INTRINSIC
INTRINSIC  and  no intrinsic    the AUTO branch: the available width, source
                               AVAILABLE -- or RoboNotFound when there is none
DEFINITE                        the declared length, source DECLARED
AUTO / UNRESOLVED               the available width, source AVAILABLE
```

The second line is the interesting one, and it is the same distinction 10.8 draws
for a percentage: an intrinsic width with nothing measured **degrades to the
containing block**, exactly as `AUTO` does, rather than inventing a width. A caller
that forgot to measure gets the old behaviour and no surprise; a caller that
measured gets the measurement. `fit-content` with no available width clamps against
the box's own max-content — "as wide as the content wants, but no wider" — which is
the answer with the most evidence behind it.

`has_intrinsic` is a **flag beside the record**, because "nobody measured this" is
the input the branch reads, and a zeroed record would be a legitimate measurement
(a box whose contents reach nothing) whose zero must not be read as an absence.

## 7. What is deliberately absent

Each a named later goal, and stated in the header's own comment where a reader will
look for it:

- **Intrinsic height as a function of width.** A box's height at a max-content width
  is one line; at a min-content width it is as many lines as the text wraps into, and
  *that* count is line breaking's (19.7's and 19.2's successors). This layer reports
  the content's line extent **at the width it measured** and says so rather than
  pretending to wrap. That is why `minContentHeight` and `maxContentHeight` are
  equal for a text run and are kept apart only because a box's combination of
  children can make them differ.
- **Replaced elements.** An `<img>`'s intrinsic dimensions are its file's, and the
  image-loading seam that would answer them is 17.5's; this layer has no way to ask,
  so a replaced element contributes zero until that seam is threaded through.
  `19.1`'s own list of tests therefore names images and replaced elements, and this
  document records them as **not built** rather than implying they are.
- **Flex, grid and table intrinsic sizing.** Those are 19.3, 19.4 and 19.6. Each
  *consumes* these two numbers and adds its own track/flex arithmetic, and none of
  them belongs beside the measurement.
- **`shrink-to-fit` as a placement rule.** `fit-content`'s arithmetic is here; the
  decision to apply it to a float or an absolutely positioned box is the placement
  goal's (14.0 sizes an out-of-flow box between two opposing insets, which is a
  different question).

## 8. What the suite asserts

Sixteen cases in four groups: the vocabulary, the record, the measurement, and the
sizing plumbing.

**The vocabulary and the record** — `layout.intrinsic.kind` (the four kinds walked,
their names, an undefined enum refused) and `layout.intrinsic.record` and
`layout.intrinsic.fit_content` (zero is valid, `min > max` and a negative extent are
not, the width-by-kind door, and the clamp held at both ends and inside).

**The measurement** — `text_min_max` (`hello world` → 110 max, 50 min, and **three
seam calls**: the whole node and each of two words), `text_long_word` (one word:
min equals max, two calls), `text_whitespace` (no word → min 0, max 30, one call),
`text_multilingual` (eleven code points, thirteen bytes), `inline_sum`,
`block_reset` and `tree`. The call **count** is what makes "the seam was reached
with the right spans" assertable rather than assumed: a build that measured the
whole node and called it the widest word would answer the same numbers for a
one-word node and the wrong ones for `hello world`.

**The sizing plumbing** — `dimension` (the fourth kind, `NONE` invalid, kind is
part of equality), `sizing_record` (set/has/read, an invalid record refused) and
`sizing_resolve` (max-content, min-content, fit-content against an available width
at both ends and inside, fit-content with none, and the degradation to `AVAILABLE`
and to `RoboNotFound`).

Plus `unmeasured` (no source → zero) and `refusals` (NULL arguments, a `NONE` id as
`RoboInvalidArgument`, a non-live id as `RoboNotFound`, a tree with no root as
`RoboNotFound`, and a failing measurer propagating rather than being read as a zero
width).

The suite drives a **scripted measurer** (a per-code-point answer), which is not a
shortcut: the phase's claim is that an intrinsic width is a *shaped* measurement,
and the only way to prove the seam was reached with the right spans is to make the
answer depend on the span.

## 9. What the phase changed in the suites that came before it

Nothing about geometry moved, because an intrinsic width only reaches geometry when
a caller records one — and no pass records one yet, exactly as no pass recorded a
min/max bound before 10.9. What moved is the module's self-description:

| Assertion | Was | Is |
|---|---|---|
| `layout.module.version` + `layout.conformance.statement` | `0.17.0` | **`0.18.0`** |
| the capability table (three suites) | 24 rows, `painting` last | **25 rows**, `intrinsic_sizing` before `painting` |
| `layout.conformance.capabilities` implemented count | 23 of 24 | **24 of 25** |
| `RoboLayoutDomainCount` (three suites) | 23 | **24** |
| `RoboLayoutDimensionKindCount` / `…SourceCount` | 3 / 4 | **4 / 5** |

## 10. Boundaries

The dependency direction is unchanged: RoboLayout, and this layer inside it, may
depend on RoboBase, RoboMem, RoboGraphics (geometry only), RoboHTML and RoboCSS, and
consumes text metrics **only** through the `LayoutTextMetrics` seam. This header
includes no RoboFont type and no RoboText type, and it parses no font. It is the
same boundary 10.5 drew, asked one question further out — and it is enforced by the
module's `-Verify` scan, which forbids RoboFont just as firmly for a measurement as
for a pass.
