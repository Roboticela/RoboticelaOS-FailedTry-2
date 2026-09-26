# RoboCSS and Media Queries (Phase 18.10)

This document is the authority for the media-query support Phase 18 adds to RoboCSS.

The module is `Runtime/Source/RoboCSS/RoboCSSMedia.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSMedia.h`.

## 1. What the phase is

Unlike every family since 18.2, media queries add **no registry row**: a media query
is `@media`'s prelude, not a property. The phase adds three vocabularies, a context,
and a matcher:

```text
media type       all | screen | print
media feature    width, height, aspect-ratio, orientation, resolution, color,
                 monochrome, prefers-color-scheme, prefers-reduced-motion,
                 prefers-contrast, hover, pointer, any-hover, any-pointer,
                 display-mode, color-gamut, scan, grid, update
feature kind     boolean | length | ratio | resolution | integer | keyword
```

A feature's **spelling carries its operator**: `width` is the feature `width` with no
bound, `min-width` a floor and `max-width` a ceiling. `CssMediaFeatureFromKeyword`
answers both the base feature and the operator, and `CssMediaFeatureSliceIsKnown`
refuses a `min-`/`max-` prefix on a *discrete* feature (`min-orientation` is not a
spelling). So the range forms are not rows of their own.

## 2. The matcher's third answer

`CssMediaConditionMatches` / `CssMediaQueryMatches` answer **yes, no or unknown**. The
third is load-bearing: a feature the context cannot answer — `(prefers-contrast: more)`
on a context with no contrast preference, `(display-mode: standalone)` on a build with
no display mode — is `RoboCssMediaMatchUnknown`, **never** `No`. A sheet that could not
be decided is a different sentence from one that did not match, and conflating them is
how a `(prefers-color-scheme: dark)` rule styles a build that has no colour scheme.

`CssMediaQueryMatches` evaluates the type first (the first gate, and always decidable),
then every condition: one `No` makes the query `No`, one `Unknown` makes it `Unknown`
unless a `No` already decided it. A negated query swaps `Yes` and `No` and leaves
`Unknown` alone.

The features this build answers are the ones a `CssMediaContext` carries: viewport
width/height, orientation (derived), resolution, colour depth, monochrome depth, colour
scheme, reduced motion, hover and pointer. Everything else is `Unknown`.

## 3. What `CssMediaContext` is not

It is **not** a viewport object. It is a value record a caller fills in; a field left
cleared means "not known", and a feature depending on it is `Unknown`. Nothing in this
module finds a real viewport — the caller that owns one copies four numbers into it.

## 4. Non-goals

- **No `@media` grammar.** Nothing here tokenizes a prelude or attaches a condition to
  an at-rule; that is the parser's, and it is 18.21's wiring.
- **No `@container`.** Container queries are 18.11's. They reuse this module's
  *condition* model over a container's size rather than a viewport's.
- **No media feature the context does not carry.** `display-mode`, `color-gamut`,
  `scan`, `update`, `any-hover` and `any-pointer` are named and answer `Unknown`.

## 5. Capabilities and version

Phase 18.10 adds one flag to `CssCapabilities` — `mediaQueries` — and one diagnostic
domain (`RoboCssDomainMedia`), and takes the module to **0.29.0**. No registry row
joins the table: the count stays 140.

## 6. Tests

`Tests/Css/SuiteRoboCssMedia.c` (`css.media.*`, 5 cases) is the contract:

- the media-type vocabulary and the media-feature vocabulary with every kind read back;
- the range forms: `min-`/`max-` answer the base feature and an operator, and a prefix
  on a discrete feature is refused;
- the condition matcher over a screen context — width with all three operators,
  derived orientation, colour scheme, reduced motion, hover and pointer — and an
  undecidable feature answering **Unknown** and an unsigned context answering
  **Unknown**;
- the query matcher: the type gate, an AND-list, one failing condition making it `No`,
  one undecidable making it `Unknown`, and `not` swapping `Yes`/`No` while leaving
  `Unknown` alone;
- the capability flag, the version, the domain, and the unchanged registry count.
