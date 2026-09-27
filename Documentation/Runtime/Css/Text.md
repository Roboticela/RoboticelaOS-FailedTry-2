# RoboCSS and the Text Family (Phase 18.15)

This document is the authority for the fifteenth thing Phase 18 adds to RoboCSS:
the **text family**. It is a *property* family rather than a text engine — every
later phase that wraps a line, justifies one or draws a decoration reads these
values — and it is the family whose central fact is that **six words hide four
independent questions**.

The module is `Runtime/Source/RoboCSS/RoboCSSText.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSText.h`.

## 1. What the phase is, and the three facts worth reading twice

CSS gives a document a family of properties that describe how its text is laid
out, and this phase types the vocabulary of the fourteen sets that *are* closed,
carrying the declarations whose grammar is wider. Three facts decide how.

**`white-space` is four questions wearing six words.** CSS spells it
`normal | pre | nowrap | pre-wrap | pre-line | break-spaces`, and what those six
actually say is:

| value          | collapses spaces | preserves newlines | wraps | breaks after a preserved space |
|----------------|------------------|--------------------|-------|-------------------------------|
| `normal`       | yes              | no                 | yes   | no                            |
| `pre`          | no               | yes                | no    | no                            |
| `nowrap`       | yes              | no                 | no    | no                            |
| `pre-wrap`     | no               | yes                | yes   | no                            |
| `pre-line`     | yes              | yes                | yes   | no                            |
| `break-spaces` | no               | yes                | yes   | yes                           |

Two pairs in that table are the reason the classifications exist. `normal` and
`pre-line` agree on the first and third columns and differ only on whether a
newline survives, so "which one is this" is not a question about collapsing or
wrapping. And `pre-wrap` and `break-spaces` agree on all three of the first columns
and differ only on the fourth — CSS Text 4 added `break-spaces` for exactly that
one behaviour, so a build with three columns could not tell them apart.

**A classification is never the negation of another.** `text-align`'s values are two
kinds of statement: `start` and `end` are *logical* (which physical edge they mean
depends on the writing mode's direction, 18.13, so they stay true when a document's
direction flips), `left` and `right` are *physical* (they name an edge and mean
it), and `center` is neither. So `CssTextAlignIsLogical` and `CssTextAlignIsPhysical`
are both false for `center`, and a consumer that read "not logical" as "physical"
would map a `center` through a direction it has nothing to do with. The same shape
recurs: `CssTextDecorationStyleIsSegmented` is false for `double` as well as for
`solid`, because `double` is two *unbroken* strokes.

**An unknown value answers what changes least.** A `white-space` this build cannot
read collapses its spaces and wraps — the initial value's behaviour — rather than
"preserve every space", which would reflow a document nobody asked to reflow; an
`overflow-wrap` it cannot read breaks nothing. The safe direction is written into
the table rather than left to a call site.

## 2. The split by grammar

The phase registers twenty rows, and they split by *grammar* rather than by
importance. Ten take a typed shape:

```text
text-align              start | end | left | right | center | justify | match-parent
  └── logical             start, end, match-parent
  └── physical            left, right
  └── justified           justify
text-align-last         auto | start | end | left | right | center | justify
  └── automatic           auto        (defer to `text-align`, not "start")
text-justify            auto | inter-word | inter-character | none
  └── justifies           everything but none
  └── expands characters  inter-character
text-transform          none | capitalize | uppercase | lowercase | full-width | full-size-kana
  └── changes case        capitalize, uppercase, lowercase
  └── changes width       full-width
  └── changes sound       full-size-kana
white-space             normal | pre | nowrap | pre-wrap | pre-line | break-spaces
  └── collapses spaces / preserves newlines / wraps / breaks after preserved space
word-break              normal | break-all | keep-all | break-word
  └── breaks within words       break-all, break-word
  └── forbids script breaks     keep-all
overflow-wrap           normal | break-word | anywhere
  └── breaks within words       break-word, anywhere
  └── affects intrinsic size    anywhere
line-break              auto | loose | normal | strict | anywhere
  └── strictness          loose, normal, strict   (one rule at three settings)
  └── breaks anywhere     anywhere   (the rules are switched off, not loosened)
hyphens                 none | manual | auto
  └── enables hyphenation   manual, auto
  └── inserts hyphens       auto
text-decoration-style   solid | double | dotted | dashed | wavy
  └── solid, wavy, segmented (dotted, dashed)
```

and ten are registered as **carried declarations** (`PRESERVE`) because their real
grammar is wider than any closed set, with the closed sets *inside* them typed here
for a consumer to read — 18.7's split for `transform` and its function-name set
made again:

```text
text-decoration-line        a *set*: `underline overline` is legal
  └── draws ink              underline, overline, line-through
  └── blink                  blink   (draws no ink on its own)
text-overflow               `[ clip | ellipsis | <string> ]{1,2}`
  └── adds marker            ellipsis
text-decoration-thickness   `auto | from-font | <length-percentage>`
  └── uses font metric       from-font
text-underline-position     a set: `under left` is legal
  └── clears descenders      under
  └── physical side          left, right   (vertical writing only)
text-emphasis, text-emphasis-style/-color/-position,
text-shadow, tab-size, text-indent, text-underline-offset
                            carried with no vocabulary of their own here
```

Three of those carry a fact a consumer needs even when the declaration is not
expanded. `text-overflow`'s two keywords are the difference between a visible
marker (`ellipsis`) and a cut (`clip`). `text-decoration-thickness`'s `from-font` is
a fact about the *file* rather than a choice — the face's own metric — while `auto`
is the user agent's. And `text-underline-position`'s four values are two different
questions: `under` and `auto` are about where the line sits vertically, while
`left` and `right` only matter in vertical writing.

Three rows are **upgraded in place**: `text-align`, `text-transform` and
`white-space` had the bare KEYWORD shape since 9.7, so `white-space: banana` used
to compute to a keyword carrying the letters. Their inheritance and their initial
values are unchanged and only the reading of the value became closed — the same
"shape change, not a new property" move 18.13 recorded for `direction` and 18.14
for `font-style`.

`text-indent` is the row that did **not** move: its `hanging`/`each-line` keywords
are named as not read rather than silently dropped, and the property keeps the
shared length shape it has had since 9.11.

## 3. The non-inherited half, and why it is the half it is

Fourteen of the twenty rows are inherited and six are not, and the split is the
family's central fact stated as a count: an alignment, a transformation, a
white-space rule, a line-breaking control, a shadow and an emphasis mark describe
how the **text** is set, so a child inside a paragraph sits on the same rules. A
decoration is *painted per element* — a child of an underlined link is not
underlined — and truncation belongs to the box that clips rather than to the text
inside it, so the five `text-decoration*` rows and `text-overflow` do not
propagate.

```text
inherited          hyphens, line-break, overflow-wrap, tab-size, text-align,
                   text-align-last, text-emphasis, text-emphasis-color,
                   text-emphasis-position, text-emphasis-style, text-justify,
                   text-shadow, text-transform, text-underline-offset,
                   text-underline-position, word-break, white-space
non-inherited      text-decoration, text-decoration-color, text-decoration-line,
                   text-decoration-style, text-decoration-thickness, text-overflow
```

`text-emphasis-color` is the family's one *inherited* reference initial
(`currentcolor`), which means it starts as a colour a machine can only resolve
where the element's own `color` is known. Its sibling `text-emphasis` carries the
same word as a preserved token, exactly as `border` and `border-color` differ.

## 4. Non-goals

- **No line breaker, justifier or hyphenator.** Nothing wraps a line, distributes
  slack, finds a hyphenation point or measures a word, and no function takes or
  returns a line, a glyph id or a rectangle. A value here says *what rule* a text
  pass follows; where the line breaks belongs to the phase that has a text engine
  (12.x).
- **No text transformation.** `text-transform` names the rule; changing a
  character's case, width or sound is not done here.
- **No direction or writing mode.** `direction`, `writing-mode` and
  `text-orientation` are 18.13's, and the logical alignment values only *mean*
  something once a consumer reads them.
- **No emphasis rendering.** The `text-emphasis-*` marks are a shape list of eight
  kinds with a filled/open pair over them, and nothing in this build draws an
  emphasis mark; the declarations are carried so the cascade and the computed style
  stay correct.
- **No decoration geometry.** Thickness, offset and underline position are carried
  or typed; placing an underline against a face's metrics is the phase that draws
  one.
- **No `text-decoration` distribution.** The shorthand's own computed value is the
  tokens an author wrote, like every other shorthand here.

## 5. Capabilities and version

Phase 18.15 adds one flag to `CssCapabilities` — `text` — and one diagnostic domain
(`RoboCssDomainText`), and takes the module to **0.33.0**. The flag claims the
fourteen closed sets, the twenty registry rows and the carried half of the family;
it does not claim a line breaker, a justifier, a hyphenator, a text transformer or
any decoration painting.

Ten computations join the table (`TextAlign`, `TextAlignLast`, `TextJustify`,
`TextTransform`, `WhiteSpace`, `WordBreak`, `OverflowWrap`, `LineBreak`, `Hyphens`,
`TextDecorationStyle`), each a row in the shared closed-vocabulary body the display
and position shapes use.

## 6. Tests

`Tests/Css/SuiteRoboCssText.c` (`css.text.*`, 18 cases) is the contract:

- each of the fourteen vocabularies walked in enumerator order, with every keyword
  read back, the case-insensitive match, the absent/unknown `FromText` answers and
  the `SliceIsKnown` door that tells a genuine value apart from the default;
- the classifications per row — logical, physical, justified, automatic, expands-
  characters, changes case/width/sound, and the four `white-space` columns — plus
  the safe answers for an unknown enumerator;
- the pairs that make the classifications necessary: `normal` and `pre-line` agree
  on collapsing and wrapping and differ only on newlines; `pre-wrap` and
  `break-spaces` agree on the first three columns and differ only on the fourth;
  `break-word` and `anywhere` break identically and differ only on intrinsic size;
  `manual` enables hyphenation and inserts nothing; `center` is neither logical nor
  physical;
- the serialization discipline: a too-small buffer is `RoboNotSupported` with the
  length it needed and the buffer left empty, and an unknown enumerator is
  `RoboInvalidArgument`;
- the ten computed shapes through the real grammar, including a deferred `var()`,
  the refusal sentences and the **carried** half of the family — a decoration line
  set, a string-valued overflow marker and a thickness length all survive as
  PRESERVED tokens;
- the twenty registry rows with their shapes, their initial values and their
  inheritance, including that `text-decoration` is the family's one non-inherited
  shorthand and `text-indent` keeps the shared length shape;
- the capability flag, the version, the ten computation names and the domain;
- determinism: the same declaration computes to the same value twice, and the two
  `white-space` rows that share three columns are different values.

The phase also moves every count that the wider build pins — the registry's row
count and its inherited/non-inherited split, `CSS_INHERITED_FAMILY`, the computed
style's default count, and the version each family suite asserts — because a flag
and a count are promises about the build rather than about this module.
