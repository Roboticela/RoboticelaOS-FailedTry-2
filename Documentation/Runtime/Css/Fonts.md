# RoboCSS and the Font Family (Phase 18.14)

This document is the authority for the fourteenth thing Phase 18 adds to RoboCSS:
the **font family**. It is a *property* family rather than a text engine — every
later phase that measures or draws text reads these values — and it is the first
family here whose values describe a **request rather than a result**.

The module is `Runtime/Source/RoboCSS/RoboCSSFont.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSFont.h`.

## 1. What the phase is, and the three facts worth reading twice

CSS gives a document a family of properties that describe the font it wants, and
one at-rule that *defines* one. The phase types both, and three facts decide how.

**A value is a request.** `font-weight: 700` does not name a face; it asks for
one. Which face answers is the font database's business — RoboFont's, and for a
shell document the resolved font request 12.17 built (`LayoutFontRequestSelect`
over RoboDesktop's provider). Nothing in this module opens a font file, so nothing
in it can be wrong about which face exists; what a value carries is the ask plus
the classifications a consumer reads.

**`@font-face`'s descriptors are not properties.** Inside that block the same
spelling means something different: `font-weight: 700` *declares the weight of the
face this rule defines* rather than selecting one, so a build that read a
`@font-face` block as an element's declarations would answer the request with
itself. Eight names — `src`, `unicode-range`, `font-display`, `font-named-instance`,
the three metric overrides and `size-adjust` — exist only there, and `font-display`
is not a property at all, so no element can inherit it.

**`font-variant-*` is a family of independent axes.** Caps, numeric,
east-asian, position, ligatures and alternates compose on one element, and the
`font-variant` shorthand resets all of them. One of those axes is deliberately
*not* typed here: `font-variant-ligatures`' five tags (`liga`, `clig`, `rlig`,
`dlig`, `hlig`) and their policy are RoboText's (12.12, `RoboTextLigature.h`), and
a second copy in RoboCSS is exactly how the shape a CSS author writes and the
policy a shaper reads would come to disagree.

## 2. The split by grammar

The phase registers nineteen rows, and they split by *grammar* rather than by
importance. Six shapes cover nine properties, because the four `font-synthesis-*`
longhands take one vocabulary — each of the six is a genuinely closed set:

```text
font-style              normal | italic | oblique
  └── slanted             italic, oblique   (two answers, not one)
  └── oblique             oblique           (the slanted-upright row specifically)
font-kerning            auto | normal | none
  └── enables kerning     auto, normal
font-optical-sizing     auto | none
  └── automatic           auto
font-synthesis-*        auto | none         (four longhands, one vocabulary)
  └── allows              auto
font-variant-caps       normal | small-caps | all-small-caps | petite-caps |
                        all-petite-caps | unicase | titling-caps
  └── caps variant        everything but normal
  └── affects lowercase   the two `all-` rows
  └── petite              the two `petite` rows
font-variant-position   normal | sub | super
  └── shifted             sub, super
  └── superscript         super
```

Nine are registered as **carried declarations** (`PRESERVE`) because their real
grammar is wider than any closed set, and the closed sets *inside* them are typed
here for a consumer to read — which is exactly 18.7's split for `transform` and
its function-name set:

```text
font-stretch            nine keywords, plus a percentage this build carries
  └── percentage          50, 62.5, 75, 87.5, 100, 112.5, 125, 150, 200
  └── nearest keyword     from a percentage; a tie takes the *narrower*
font-size               ten absolute/relative keywords, over the shared length grammar
  └── absolute            the eight, against the reader's preferred size
  └── relative            larger, smaller — against the *parent's* size
  └── ratio               the spec's suggested factor per row
font-variant-numeric    nine values on five independent axes
  └── kind                normal / ordinal / slashed zero / figure style /
                          figure spacing / fraction
font-variant-east-asian ten values on three
  └── kind                normal / glyph form / width / ruby
font-feature-settings, font-variation-settings, font-language-override,
font-palette, font-variant-alternates, font-variant-ligatures, font-variant
                        carried with no vocabulary of their own here
```

The kind on the two `font-variant-*` sets is the point of typing them at all: a
consumer has to know that `lining-nums oldstyle-nums` is contradictory while
`lining-nums tabular-nums` is not, and only the row can say which values sit on
one axis.

`font-style` is the one row **upgraded in place**. It had the bare KEYWORD shape
since 9.7, so `font-style: banana` used to compute to a keyword carrying the
letters; its inheritance and its `normal` initial value are unchanged and only the
reading of the value became closed — which is the same "shape change, not a new
property" move 18.13 recorded for `direction`. CSS's `oblique <angle>{0,2}` form
is a second grammar this build does not read, and the shape it replaces refused it
too: KEYWORD accepts *one* identifier and that form is two tokens.

`font-size` is the row that did **not** move. The shared length grammar has
carried its ten size keywords since 9.10 (that is why its computed kind is
`keyword` and its text `medium`), so a second value model would be the same
normalisation twice. What the phase adds is the *classification*:
`medium` is not sixteen pixels — the eight absolute keywords are measured against
the reader's preferred size and the two relative ones against the parent's — which
is the fact this property exists to carry.

## 3. `@font-face`: the descriptor vocabulary

```text
src | font-family | font-style | font-weight | font-stretch | font-display |
unicode-range | font-feature-settings | font-variation-settings |
font-named-instance | ascent-override | descent-override | line-gap-override |
size-adjust | font-language-override | font-kerning | font-optical-sizing |
font-synthesis-weight/-style/-small-caps/-position | font-variant-caps |
font-variant-east-asian | font-variant-ligatures | font-variant-numeric |
font-variant-position | font-palette
  └── shared with a property   19 of the 27   (the other 8 exist only here)
```

`font-display` is the one closed set in the module that is a *descriptor and not a
property*, and what it carries is **time**: whether a block period applies, how
long it is, and whether the fallback is replaced afterwards.

```text
font-display   auto | block | swap | fallback | optional
  └── blocks rendering       auto, block, fallback, optional
  └── block period (ms)      block 3000, fallback 100, optional 100, swap 0
  └── swaps after block      swap, fallback
```

`auto` is the user agent's policy and this build has none, which is why
`IsAutomatic` exists rather than a block period of zero meaning "no period".

## 4. Non-goals

- **No font database.** No file is opened, no family name resolved to a face, no
  coverage table walked, no fallback selected and no `@font-face` rule matched.
  That is RoboFont, and for a document 12.17's selection seam.
- **No shaper and no measurement.** No glyph id, no run, no advance, no rectangle.
  The ligature policy is RoboText's.
- **No synthesis, ever.** `font-synthesis-*` says what an author *permits*; this
  build synthesises neither a weight nor a slope (the rule 12.3 records and
  RoboText keeps), so an `auto` here is a value and not an event.
- **No `font` shorthand distribution.** The shorthand's own computed value is the
  tokens an author wrote, like every other shorthand here; expanding it over its
  seven longhands is a change of *phase*, and no phase in this build expands one.
- **No `@font-face` prelude grammar.** The descriptor *names* are typed; tokenizing
  the block and attaching the descriptors is the parser's.
- **No `font-style: oblique <angle>`, and no `font-stretch` percentage as a
  value.** The first is refused (as it already was); the second is carried, because
  refusing legal CSS is worse than carrying it.
- **A known looseness, named:** `font-size`'s shape is the *shared* length
  grammar, whose keyword list is wider than the property's own — `font-size:
  min-content` computes to a carried keyword rather than being refused. Tightening
  it would change the vocabulary of a grammar that serves twenty properties, so
  the phase names the looseness instead and types the twelve spellings that matter.

## 5. Capabilities and version

Phase 18.14 adds one flag to `CssCapabilities` — `fonts` — and one diagnostic
domain (`RoboCssDomainFont`), and takes the module to **0.32.0**. The flag claims
the twelve closed sets, the nineteen registry rows and the descriptor vocabulary;
it does not claim a font database, a shaper, a measurer or a synthesiser, and it
names `font-variant-ligatures` as RoboText's.

Six computations join the table (`FontStyle`, `FontKerning`, `FontOpticalSizing`,
`FontSynthesis`, `FontVariantCaps`, `FontVariantPosition`), each a row in the
shared closed-vocabulary body the display and position shapes use.

## 6. Tests

`Tests/Css/SuiteRoboCssFont.c` (`css.font.*`, 16 cases) is the contract:

- each of the twelve vocabularies walked in enumerator order, with every keyword
  read back, the case-insensitive match, the absent/unknown `FromText` answers and
  the `SliceIsKnown` door that tells a genuine value apart from the default;
- the classifications per row — slanted, oblique, enables-kerning, automatic,
  allows-synthesis, caps-variant, affects-lowercase, petite, shifted, superscript,
  the two variant *kinds*, stretch percentage and nearest-keyword, size-kind and
  ratio, display block period and swap — and the safe answer for an unknown
  enumerator;
- the serialization discipline: a too-small buffer is `RoboNotSupported` with the
  length it needed and the buffer left empty, and an unknown enumerator is
  `RoboInvalidArgument`;
- the six computed shapes through the real grammar, including a deferred `var()`,
  the three refusal sentences and the **carried** half of the family — a
  `font-stretch` percentage, a `font-variant-numeric` axis set and a word no
  vocabulary knows all survive as PRESERVED tokens;
- the nineteen registry rows with their shapes, their initial values and their
  inheritance, including that `font` is the family's one non-inherited row;
- the capability flag, the version, the six computation names and the domain;
- determinism: the same declaration computes to the same value twice, and the two
  caps rows are different values.

The phase also moves every count that the wider build pins — the registry's row
count and its inherited/non-inherited split, `CSS_INHERITED_FAMILY`, and the
version each family suite asserts — because a flag and a count are promises about
the build rather than about this module.
