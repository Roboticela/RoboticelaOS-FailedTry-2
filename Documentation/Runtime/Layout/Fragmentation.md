# Fragmentation (Phase 19.7)

Version: `0.23.0`; capability: `LayoutCapabilities.fragmentation`.

A **fragmentainer** is a box that splits its contents into a sequence of *fragments*: a
column of a multi-column container, a page, a region. Fragmentation is the question of
**where the splits fall**, and this phase is the policy that answers it — one boundary at
a time — plus the record a pass reports its own splits through.

It is deliberately **not** a pass. Nothing in this phase produces a fragment, places a box
or writes a `LayoutResult`; the split is the same one 10.12 and 14.0 make for positioning
answer-the-question-and-hand-back-a-record, and the pass that has a fragmentainer applies it.

## 1. What the phase adds

| Piece | Where | What it is |
|---|---|---|
| The break vocabulary | `Runtime/Include/RoboCSS/RoboCSSFragment.h` + `Runtime/Source/RoboCSS/RoboCSSFragment.c` | The `break-between` set (thirteen values, shared by `break-before` and `break-after`) and the `break-inside` set (five values), each row carrying `forced`, `avoided`, `columnBreak` and `pageBreak` |
| Five registered rows | `RoboCSSPropertyRegistry.c` | `break-before`, `break-after`, `break-inside` (non-inherited, initial `auto`); `orphans`, `widows` (inherited, initial `2`, positive integers) |
| A diagnostic domain | `RoboCssDomainFragment` | The vocabulary's own domain, so a refusal in it is not reported as a value-phase failure |
| The policy | `Runtime/Include/RoboLayout/RoboLayoutFragment.h` + `Runtime/Source/RoboLayout/RoboLayoutFragment.c` | The container kinds, the boundary precedence, the interior refusal, the two line floors and the fragment record |
| The record | `LayoutFragmentCounters` | Nested in `LayoutBlockStats` beside the flex, grid, table and absolute ones |

## 2. The four container kinds, and the one that is a decision and not a box

`LayoutFragmentainerKind` is `None`, `Column`, `Page` or `Region`. `None` is **not** a
container — a box that establishes no fragmentation context cannot break its contents, and
every door answers that rather than guessing a kind.

`Column` is the only kind this build lays anything out in. `Page` and `Region` exist so a
*decision* about them is expressible; there is no page box, no page margin, no page
orientation and no region. That asymmetry is why
`LayoutFragmentainerKindOfForcedBreak` answers `None` for `region`: RoboCSS's table says
`region` is forced and that neither a column nor a page container honours it, and turning it
into a column break would invent a column.

## 3. The boundary policy, in the order it is applied

`LayoutFragmentBreakDecide` answers the boundary **before** the next box. Six rules, and
the order is the whole rule:

1. **No context.** A kind of `None` forbids the break and says so. Asked of the kind rather
   than of the declarations, because a `break-before: page` inside a non-container is a
   declaration with nothing to act on — a refusal, not an error, so the document still lays
   out.
2. **Nothing precedes the box.** A boundary at the start of a fragment is *suppressed*:
   the content starts the fragment anyway and a break there would produce an empty one. The
   action is `Allowed` and the reason `SuppressedAtStart`. An `avoid` at the start is **not**
   suppressed — it is trivially satisfied — and falls through to rule 4, which answers the
   same action with the reason that says why. The two are counted separately for exactly
   that reason.
3. **A forced break wins over an `avoid`**, from either side of the boundary and whichever
   order the two declarations are written in. `forcedBy` records the container the
   declaration *named*, which is not always the container being asked: a `page` break inside a
   column container is still forced, and `region` names no container at all.
4. **An `avoid` refuses**, from either side. The four avoid values all refuse, including
   `avoid-page` on a column container's boundary — a boundary declaration is a statement
   about the boundary, which is the difference from `break-inside` below, where the container
   decides.
5. **The interior.** The box's own `break-inside` refuses *and* it does not fit, so it moves
   whole to the next fragment. Both halves are required: a box that refuses to be split and
   **fits** needs no break, and answering `AvoidedInside` there reports a refusal nothing
   required.
6. **Room.** With room, `Allowed`/`Auto`; without, `Forbidden`/`NoRoom` — the only break no
   author asked for.

### A defect this phase's own suite caught

Rule 3 was first written as `forcedBy != None`, i.e. "a declaration forced a break" tested
through *the container it names*. That is the same question for `column`, `page` and
`always` and a different one for `region`: `region` is forced and names no container here, so
`break-before: region` fell through to the room rule and broke nothing in a container with
room. `layout.fragment.forced_beats_avoided` failed on it. The fix is two functions kept
apart — `FragmentForces` asks the *declaration* (`CssBreakBetweenIsForced`) and
`FragmentForcedContainer` asks the *container* — so the two facts cannot be conflated again.

## 4. The interior door

`LayoutFragmentInsideRefuses(inside, kind)` answers whether a refusal is about *this*
container. `avoid` refuses everywhere; `avoid-column`, `avoid-page` and `avoid-region` refuse
in the one they name and say nothing to the others.

That split is why `CssBreakInsideIsAvoided` (the vocabulary's question) and this door (the
container's question) are two calls rather than one:

```c
RoboCheckEqInt(CssBreakInsideIsAvoided(RoboCssBreakInsideAvoidPage), 1);
RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidPage,
                                          LayoutFragmentainerKindColumn), 0);
```

`break-inside` has no forced values at all, and that is a property of CSS rather than of this
build: an interior boundary has no next fragmentainer for a break to begin one.

## 5. The line floors

`orphans` and `widows` are the minimum lines a break may leave at the end of one fragment and
carry to the start of the next, and `LayoutFragmentBreakLines` is the one place the policy
needs a **count** rather than a declaration — the number of lines the box would put on either
side of the candidate break, which 10.6's line boxes supply.

Breaking after line *n* of *N* lines is allowed exactly when *n >= orphans* and
*N − n >= widows*. Both refusals carry a reason of their own, because which floor was breached
is the only useful half of the answer. The two degenerate cases — a break before the first
line, and a break at or after the last — are refusals with `NoRoom` rather than a floor,
because neither is a floor being consulted.

These two rows are the fragmentation family's **only inherited** members, and for a reason the
break properties do not share: a break describes a boundary and belongs to the box that owns
it, while a floor describes how much text a fragment may be left with — a property of the
*text*, so a nested block keeps the page's floors instead of losing them at the first level of
nesting.

## 6. A fragment, and the counters

`LayoutFragment` is an ordinal and a block range — the node, the container kind, the fragment's
index, its block offset and extent, and a `last` flag — rather than a rectangle, because a
fragment's *geometry* is the pass's answer and its *identity* is this module's question. A box
split in three has three records whose extents sum to its block size and whose offsets tile it,
which is an assertion a rectangle could not express without re-deriving the split.

`LayoutFragmentWrite` is complete-or-absent like every dump in this module: it reports the
length a complete line needs whether or not it fits, and leaves the buffer empty when it does
not. A record that is not *valid* still dumps, because a diagnostic must be able to describe the
record it was handed.

`LayoutFragmentCounters` reports ten numbers, and the mix is the point: two documents that broke
the same number of times are entirely different if one was forced and the other ran out of room.
The refusals are counted separately from the breaks, so a build that honoured no declaration at
all reports zero forced and zero avoided breaks and does not look identical to a document that
declared nothing.

## 7. Not implemented, and named rather than implied

* **Multi-column layout itself.** A `column-count` is 18.6's computed value; no track count is
  resolved, no gutter is applied and `column-fill` balances nothing. What this phase supplies is
  the break policy such a pass would consult.
* **Page and region boxes**, their margins and their orientation. Kinds a decision can name, and
  boxes this build does not lay out.
* **A fragment's own geometry** — where its content box begins, how a margin is truncated at a
  break, what `block-size` means for the first thing in a fragment.
* **`break-*` suppression by an ancestor's `break-inside`** at a boundary deeper than that
  ancestor. The boundary record carries the box's own `break-inside` and nothing above it; an
  ancestor's refusal is applied by the pass at the ancestor's own boundary, which is where CSS
  applies it.
* **The legacy `page-break-*` spellings**, which are not registered rows — an alias is a registry
  row and this build has none.
* **`orphans`/`widows` on a container with no lines.** A box with no inline content has no floor
  to apply, and the door says so rather than answering "allowed".
* **Replaced elements and `aspect-ratio`**, which a fragmentation pass would treat as unbreakable
  and this build does not classify.

## 8. Where it shows up

| Field / door | What it says |
|---|---|
| `LayoutCapabilities.fragmentation` | 1 since 19.7, with the absences above named at the flag |
| `LayoutBlockStats.fragment` counters | the phase's own ten counters, in its own record rather than copied flat into the block stats |
| `RoboLayoutDomainFragment` | the twenty-eighth diagnostic domain |
| `LayoutFragmentainerKindOfForcedBreak` | which container a forced break names, and `NONE` for `region` |
| `LayoutFragmentInsideRefuses` | whether an interior refusal is about *this* container |

## 9. Test map

Seventeen cases in `Tests/Layout/SuiteRoboLayoutFragment.c`, domain `layout.fragment.*`:

| Case | Asks |
|---|---|
| `kind_vocabulary` | four kinds, their names, and `none` not establishing a container |
| `forced_break_kind` | which container each forced word names, `region` included |
| `action_vocabulary` | three actions, nine reasons, each round-tripped through its own name |
| `boundary_record` | the record's initial state, its validity, and each field's equality |
| `decision_record` | the cross-field rule: `forcedBy` is non-`NONE` exactly for a declared forced break |
| `no_context` | rule 1, and that a declaration inside a non-container is a refusal rather than an error |
| `suppressed_at_start` | rule 2, both halves: forced suppressed, `avoid` falling through |
| `forced_beats_avoided` | rule 3 from both sides, and `page`/`region` reported as what they named |
| `avoided` | rule 4, all four avoid values, from either side |
| `avoided_inside` | rule 5, including the box that fits and the refusal about another container |
| `room` | rule 6, and that every container kind answers it the same way |
| `decide_refusals` | the door's refusals, and that a refusal leaves the cleared record behind |
| `inside_refuses` | the vocabulary question and the container question, side by side |
| `line_floors` | the floors, the two exact boundaries, the degenerate cases and the door's refusals |
| `record` | a fragment's validity, its equality, and its complete-or-absent dump |
| `counters` | the record's own identity, one field at a time |
| `determinism` | a hundred repetitions of every door, and the allocator balance |

## 10. What the suite found

One finding, and it was in the implementation rather than in the expectations — see §3 above.
It is worth the paragraph because of its *shape*: the defect was not a wrong rule, it was
**two questions that look like one**. "Did a declaration force a break?" and "which container
did it name?" have the same answer for three of the four forced values, so a build that asked
the second question to answer the first passes every case written around `column`, `page` and
`always` — and the one that catches it is the value that names a container this build does not
have. The fix keeps the two questions in two functions with a comment saying why, which is the
only durable form: a second `forcedBy != None` would pass review.

Two things the phase deliberately did *not* do, each for a stated reason rather than for lack
of time: `orphans`/`widows` are registered and read, but nothing in this build establishes a
*page* container for a floor to be enforced at in a real document — a column container is the
only one that exists — and the policy takes the line count as an argument rather than reading
it from a tree, because counting the lines a box *would* put on either side of a candidate break
is a question about a layout that has not happened yet.
