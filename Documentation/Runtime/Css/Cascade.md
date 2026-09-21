# RoboCSS Cascade (Phase 9.5)

The cascade answers one question: **what applies to this element, and in what
order**. CSS sorts the answer by three things, and all three are complete.

| Key | Owned by | Status |
|---|---|---|
| origin and importance — the *level* | 9.5 | complete, final, never revisited |
| specificity | **9.6** | complete — the cascade consumes it and holds no arithmetic of its own |
| the document order of the declarations | 9.5 | complete |

Phase 9.5 shipped the level and the document order without specificity, and
*reported* the contests it could not settle: two candidates at one level from two
different selectors came back `UNDECIDED_SPECIFICITY` rather than being settled by
document order, which would have been right only by luck. Phase 9.6 supplied the
middle key (`docs/Specificity.md`), so that verdict no longer exists and every
property question has an answer.

## The level table

Ascending precedence. `CssCascadeLevelOf(origin, important)` is the
whole of it, and `CssCascadeLevelHolds` is the round trip.

| Level | Origin | `!important` |
|---|---|---|
| `RoboCssCascadeLevelRuntime` | runtime (user agent) | no |
| `RoboCssCascadeLevelUser` | user | no |
| `RoboCssCascadeLevelAuthor` | author | no |
| `RoboCssCascadeLevelInline` | the element's `style` attribute | no |
| `RoboCssCascadeLevelAuthorImportant` | author | yes |
| `RoboCssCascadeLevelInlineImportant` | inline | yes |
| `RoboCssCascadeLevelUserImportant` | user | yes |
| `RoboCssCascadeLevelRuntimeImportant` | runtime | yes |

Two rows are decisions rather than transcriptions, and both are tested directly
(`css.cascade.levels.inline_is_author_level`,
`css.cascade.levels.important_half_is_not_a_reversal`).

**Inline sits above the origin it belongs to.** In CSS a `style` attribute is an
author-origin declaration with a specificity no selector reaches. Treating
`RoboCssOriginInline` as its own level above `AUTHOR` reproduces that
behaviour in both halves — and it is why an inline declaration can carry the zero
specificity (9.6) without weakening: its strength is the level.

**The important half is not a plain reversal.** `!important` reverses the origin
order — `runtime !important` is the strongest thing there is, then user, then
author — while inline stays directly above author, because inline *is*
author-level. An inline `!important` therefore beats an author `!important` and
still loses to a user one. Reversing levels 0–3 wholesale would get that
backwards, which is why the table is written out rather than derived.

`RoboCssCascadeLevelNone` is not a level. It is what `RoboCssOriginNone`
and an unknown origin answer with, and it compares weaker than every real level.

Not in the table, on purpose: `@layer` (cascading layers do not exist in this
model), and the animation and transition origins (effects are 9.16's world, and
inventing rows for them would invent precedence nothing can produce).

## Collection

`CssStyleContextMatchDeclarations` is the only door, and it is real as
of 9.5. It walks:

```
context
├── RUNTIME sheets, in the order they were added
├── USER sheets
├── AUTHOR sheets
└── the element's `style` attribute (through the inline callback)
```

For each top-level rule: a style rule's selector list is matched through the
element host (9.4); every *usable* declaration of a matched rule becomes a
candidate. At-rules are skipped whole, `@media` blocks included, because nothing
evaluates them — see the counters below for why that is reported rather than
ignored.

The candidate array comes out in **cascade order**: ascending level, and within a
level the document order the declarations were met in. The walk visits origins in
level order and, inside an origin, sheets, rules and declarations in document
order, so the array is nearly sorted as it is built. One case needs repairing:
two declarations in the same rule can sit at different levels (one `!important`),
so a rule's declarations do not arrive level-sorted. A **counting sort over the
eight levels** puts them back, stably — stable because document order is the
tie-break inside a level, so no comparison function is needed and no sort can
disagree with the specification about ties.

### The counters

A cascade is only as complete as its inputs, and every way this build can be
incomplete has a field.

| Field | Meaning | Marks the result incomplete? |
|---|---|---|
| `sheets_scanned` / `rules_scanned` / `rules_matched` | what was walked | — |
| `rules_undecided` | a selector list that could not be decided, or no host at all | yes |
| `rules_without_selectors` | a rule whose prelude was never parsed as selectors, or is not a valid list | yes |
| `at_rules_with_blocks` | at-rules that could be hiding rules (`@media`) | yes |
| `at_rules_skipped` | every at-rule, blocks included; `@charset` hides nothing | no |
| `sheets_unavailable` | a referenced sheet that was destroyed | yes |
| `sheets_origin_mismatch` | a sheet listed under an origin other than the one it records | no |
| `declarations_ignored` | declarations that are not usable — a malformed one is a record of a problem, not a declaration | no |

`unsupported` is the one-line version of the "yes" column. **`unsupported == 0`
does not mean nothing matched**: an empty cascade over a clean context is a
complete answer, and `CssCascadeIsComplete` says so explicitly.

Two of those deserve a sentence more.

`rules_without_selectors` is also how a caller learns it forgot to call
`CssStyleRuleParseSelectors`: this build cannot tell "never parsed" from
"invalid prelude", and both mean the rule's reach is unknown.

`sheets_origin_mismatch` does **not** mark the result incomplete. The context's
list is what places a sheet — it *is* the cascade order — so the level follows
the list, and the disagreement is counted for the caller to see.

## Resolution

```c
CssCascadeVerdict CssCascadeResolveName(cascade, name, &winner);
CssCascadeVerdict CssCascadeResolveProperty(cascade, id, &winner);
```

| Verdict | Meaning |
|---|---|
| `NONE` | no candidate declares that property |
| `DECIDED` | a winner was found, and `out_winner` points at it |

A winner is filled in **only** for `DECIDED`, and it borrows from the cascade's
own array.

Walking the array once, front to back, is enough because it is in cascade order —
ascending level, and within a level the document order the declarations were met
in:

* a candidate at a **strictly higher level** than the best so far wins outright;
* at the **same level**, the greater *specificity* wins — compared through
  `CssSpecificityCompare`, the one implementation, never by arithmetic
  here;
* at the same level with an **equal** specificity the candidate wins too, because
  it is the later of the two in document order and that is the last tie-break.

Two declarations of one rule always share a specificity (a specificity is a
property of the selector, and there is only one of it), so they resolve between
themselves by document order — the special case 9.5 needed
`same_provenance` for is now just the equality branch of the same comparison.

**The specificity a candidate carries is the one of the selector that matched**,
not the first member of the rule's list and not the list's maximum. A list has no
specificity: each member has one, and CSS uses the most specific member that
matches the element. `docs/Specificity.md` has the rule and its tests.

By name or by property id? Both, and neither falls back on the other. A
declaration's name is what the author wrote, so `resolve_name` works with no
property table at all and compares ASCII case-insensitively (CSS property names
fold). A declaration's *identity* is the context's property table's business, so
the collection resolves each candidate against the table the context was handed
and `resolve_property` answers on that. `RoboCssPropertyIdNone` is refused
rather than matched: every unresolved declaration would answer to it.

## The inline boundary

The DOM keeps the `style` attribute; RoboCSS is handed its *text* through
`CssInlineStyleFn`, parses it with the same declaration-list grammar a
rule's block uses, and stores none of it. The parsed declarations live in the
cascade's own `inline_declarations` list, and the inline candidates borrow from
there.

Three answers, three facts:

* `RoboNotFound` — the element has no `style` attribute: `inline_present` is 0;
* `RoboOk` with empty text (`style=""`) — an attribute that declares nothing:
  `inline_present` is 1 and the list is empty;
* a host failure — the answer is a *lower bound*, so `has_inline_error` is set and
  `unsupported` is 1. Treating "the host could not answer" as "no inline
  declarations" is exactly the silent-wrong-rule bug the three-outcome discipline
  exists to prevent.

Malformed inline CSS is recovered from like malformed CSS anywhere else: the
valid declarations are collected, the problem lands in the call's `out_error`
record and in `inline_error`, and `unsupported` stays 0, because a recovered
problem is not an incomplete answer.

## What this file's phase does not do

* **Specificity** was 9.5's one gap, and **9.6 closed it** rather than this file.
  The cascade consumes `RoboCSSSpecificity.h` and carries a `specificity` on every
  candidate; nothing about how a specificity is computed is decided here.
* **Inheritance** was 9.5's other gap, and **9.7 closed it** rather than this
  file: a cascade of declarations is not a style, so `RoboCssInheritResolve`
  turns one element's cascade (plus an explicit parent) into a *resolved* style.
  The cascade itself still does not inherit anything.
* **Computed values** were 9.5's other gap, and **9.8 closed them**: a resolved
  style is a token sequence, not a value, so
  `CssComputedValueCompute` turns one into a length, a percentage, a
  colour or a keyword (`docs/ComputedValues.md`). The cascade still decides *which*
  declaration wins and nothing else — it neither inherits nor computes.
* **Evaluating at-rules.** No `@media` is tested, no `@import` is fetched, no
  `@supports` is checked, no layer is created — and none of them is *named* in a
  table, which is the 9.3 decision this phase keeps.
* **Shorthands.** `padding: 4px` is one declaration with one name. Expanding it
  into longhands is 9.9/9.11's work, and doing it here would invent a property
  database this phase does not have.
* **Animations and transitions.** They would add rows to the level table; nothing
  can produce them yet.

## Lifetime

| Owner | Owns | Does not own |
|---|---|---|
| `CssCascade` | its candidate array, and the declaration list parsed from the `style` attribute | the declarations it borrows from the context's sheets, the bytes those borrow, the sheets |
| each candidate | nothing: `declaration` points into a sheet, or into the cascade's inline list | — |

So a cascade is valid exactly as long as the sheets it was collected from — a
fact the caller already owns, because it owns those sheets. Collecting into a
record that already holds a cascade replaces it (`init` releases first), so
collecting twice cannot accumulate. `CssCascadeDispose` is idempotent, and
every suite that allocates asserts the live block count returns to its baseline.

## Running the suites

```bash
scripts/robo test-css        # the whole RoboCSS subsystem
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test -Filter css.cascade
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test -Filter css.dom.cascade
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test -Sanitize
```

| Suite | Prefix | Covers |
|---|---|---|
| `SuiteRoboCssCascade.c` | `css.cascade.levels.*` | the table, its order, its names, and the two decisions it encodes |
| `SuiteRoboCssCascadeCollect.c` | `css.cascade.collect.*` | what applies, the order across origins, importance, the inline boundary, and every "could not use this" counter |
| `SuiteRoboCssCascadeResolve.c` | `css.cascade.resolve.*` | both verdicts, level before specificity, specificity before document order, and identity by name and by id |
| `SuiteRoboCssSpecificityCascade.c` | `css.specificity.cascade.*` | specificity deciding in the cascade, the matching-selector-of-a-list rule, inline, and determinism |
| `SuiteRoboCssDomSelect.c` | `css.dom.cascade.*` | the same cascade over the **real** RoboHTML tree, including the document's own `style` attribute, and a byte-for-byte proof that collecting changes nothing the demo paints |
