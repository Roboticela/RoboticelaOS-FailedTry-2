# RoboCSS and Container Queries (Phase 18.11)

This document is the authority for the container-query support Phase 18 adds to
RoboCSS.

The module is `Runtime/Source/RoboCSS/RoboCSSContainer.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSContainer.h`.

## 1. What the phase is

A container query is `@container`'s prelude, evaluated against the nearest ancestor
that establishes a *query container*. The phase adds one closed set and one matcher:

```text
container-type   normal | size | inline-size
  └── establishes a container   size, inline-size
  └── inline-only                inline-size
```

`size` establishes a container for both axes; `inline-size` establishes one for the
inline axis only — the cheap one, because it does not depend on the block axis and so
cannot make layout circular. `normal` establishes none.

## 2. The condition model is the media module's

A container query's condition list is the **media-query** module's own
`CssMediaCondition` (§18.10, `RoboCSSMedia.h`), evaluated with
`CssMediaConditionMatches` against a `CssMediaContext` holding the **container's**
numbers. `CssContainerQueryMatches` is the one door: it ANDs the conditions with the
same three-valued result — yes, no or unknown — and answers **UNKNOWN** when the box
is `container-type: normal`, because a box that never declared itself a container is
not a match against something.

This reuse is the point the media module's own contract promised: the condition
grammar is one, and only the box it is measured against differs.

## 3. Non-goals

- **No `@container` grammar and no ancestor walk.** Nothing here tokenizes a prelude
  or finds the query container — the caller that has a box does that and hands the
  size here.
- **No `inline-size`/`block-size` feature names.** A logical feature needs the writing
  mode a container does not carry until 18.12 resolves it; the physical `width`/`height`
  conditions are what this build answers.
- **No `style()` container queries.** Named, not built.
- **No layout.** Nothing here measures a container.

## 4. Capabilities and version

Phase 18.11 adds one flag to `CssCapabilities` — `containerQueries` — and one diagnostic
domain (`RoboCssDomainContainer`), and takes the module to **0.30.0**. Three registry
rows join the table, none inherited: `container-type` (typed), `container-name` and
`container` (carried). The registry count moves from 140 to 143, non-inherited from 118
to 121, inherited stays 22.

## 5. Tests

`Tests/Css/SuiteRoboCssContainer.c` (`css.container.*`, 4 cases) is the contract:

- the `container-type` vocabulary, its two classifications and the safe answers;
- the matcher over a 600×800 container: a `min-width` condition, an AND-list with a
  derived orientation, one failing condition making it `No`, an undecidable feature
  answering `Unknown`, and a `normal` box answering `Unknown`;
- the typed computed shape through the real grammar and the three registry rows;
- the capability flag, the version and the diagnostic domain.
