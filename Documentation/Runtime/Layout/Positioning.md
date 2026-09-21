# RoboLayout Positioning — Phase 10.12

Module version at this record: **0.12.0**. Phase **10.12 — Basic Positioning**.

---

## 1. What the phase is

A `position` on a box is a *displacement*, and the phase's whole claim fits in one
sentence: a relatively positioned box is drawn where its offset puts it, its size is
untouched, and the box after it starts exactly where it always would have.

Two coordinate spaces, and the phase is the reason there are two:

```text
flow position   = where the containing block and the margins put the box
                  (the number every sibling's placement was computed from)
final position  = flow position + the resolved offset
                  (the number a renderer draws, and what a result carries)
```

Every earlier phase wrote one number; 10.12 keeps both. The result's `position`
carries the **final** one — what paint wants — and the **flow** one stays recoverable
as `final − offset`, which is why the used offset is kept on the object beside the
three insets.

`static` remains the default and means "no displacement": not a special case in the
pass but the zero of the vocabulary, so every 10.3–10.11 expectation about a position
holds for it unchanged. `absolute`, `fixed` and `sticky` are *represented* (9.13's
typed vocabulary carried intact) and **declined** — their used offset is zero, counted
as a decline, because placing one against a containing block or a scrollport is 18.5's.

## 2. The vocabulary

### The inset (a side)

Four kinds, numbered so that a cleared record is `ABSENT` — the zero of the
vocabulary, the same decision 10.9's margin vocabulary made:

| Kind | Meaning |
|---|---|
| `RoboLayoutInsetAbsent` | nobody declared this side (a cleared record's state) |
| `RoboLayoutInsetResolved` | a px length, **signed** — `left: -7px` is legal |
| `RoboLayoutInsetAuto` | the keyword `auto`, a declaration that says nothing here |
| `RoboLayoutInsetUnresolved` | declared but unusable here: a percentage, a relative unit, a `var()`-flagged length, or any other kind |

`zero` is *not* `absent`: `top: 0` is a declaration an author asked for, and
`LayoutInsetEqual` distinguishes them (`ABSENT` sides compare equal whatever
stale `length` their fields hold — the reason the kind exists).

The **sign** is the one respect in which this vocabulary differs from the padding's and
the border's: those two floors refuse or floor a negative side; this one honours it,
because CSS's inset grammar (like margin's) makes the negative half of the value space.

### The four sides, and the position

`LayoutInsets` is the four sides in `CssBoxSide` order; a cleared
record is four `ABSENT` sides. `LayoutPosition` is the mode plus the four:

```c
LayoutPosition position = LayoutPositionOf(style);
```

A cleared record is `STATIC` and four absents — and `STATIC` is 9.13's zero, which the
suite pins beside the vocabulary so the two zeros cannot drift apart. The mode is read
through a kind check: 9.13's accessor answers `static` for any other kind, so reading
it without the check would report "this box is not positioned" where the truth might be
"this is not a position property at all".

## 3. The two reads

### The seam (`LayoutPositionOf`)

One reader over a computed style, written the same way the margin, padding and border
seams are: classify each longhand, keep the kinds that mean something, keep the rest as
`UNRESOLVED`. The classification is 10.8's shared one with **one** parameter a margin
passes and a dimension does not: an inset is signed, and `LayoutPxRound`
rounds half away from zero — so `-1.5px` is −2 rather than 2, the one direction an
author would notice a towards-zero rounding in.

What each kind becomes:

| Computed value | Becomes |
|---|---|
| px length (any sign) | `RESOLVED`, rounded half away from zero |
| `auto` | `AUTO` |
| absent longhand | `ABSENT` |
| percentage, `em`/`rem`, `var()`-flagged, out-of-range | `UNRESOLVED` |

A percentage is legal CSS for an inset and is `UNRESOLVED` here for 10.8's reason:
resolving one needs a containing-block *dimension* positioned against, which no phase
in this build computes. Recorded rather than dropped, so the resolution rule is the one
that changes when a later phase arrives.

### The policy (`LayoutPositionResolve`)

The declared record in, the used offset out. The phase's **one rule**:

> **`left` wins over `right`; `top` wins over `bottom`; an `auto` side says nothing.**

and the rule is CSS's own simplified: a displaced element's `left`/`top` pair is what
an author almost always writes, the opposite pair is the same displacement with the
sign flipped, and the direction-dependent over-constrained cases are exactly what
18.5 replaces. Four counters partition the four sides, and they account for every
declaration the record holds:

| Counter | Counts |
|---|---|
| `insets_resolved` | sides that produced a term of the displacement |
| `insets_auto` | sides declared `auto` (say nothing, counted as the declarations they are) |
| `insets_unresolved` | sides declared but unusable here (used as zero) |
| `insets_declined` | sides on a **non-relative** box — every mode but `relative` declines its four |

`insets_declined` is the counter that would otherwise be invisible: a `static` box
with four insets and a `relative` box with four autos both resolve to a zero
displacement, and only this counter tells the first document from the second. The
counter splits the two reasons a non-relative box declines — `static` is CSS's initial
value and CSS applies no offset to one, while `absolute`/`fixed`/`sticky` are placed
against something this build does not compute yet — by *mode*, which the record carries
beside the counts.

`LayoutPositionResolve` cannot fail on a document: every input it can be handed
is an input a document can produce, so the refusal is reserved for arguments — a NULL
record, or an *invalid* one (an unknown mode, or counters that account for more than
four sides, which no resolve produced).

## 4. The apply

```c
LayoutPositionApply(flow, &offset, &final, diag);
```

The one place the two coordinate spaces meet: the flow position plus the displacement,
in 64 bits and **clamped** — the flow position and the displacement are each bounded by
the seam's maximum and their sum is not, so an extreme pair must saturate rather than
wrap into a coordinate on the other side of the screen. Clamped rather than refused,
because there is no document an author can write that some coordinate cannot describe;
10.10 and 10.11 reached the same decision for a *size*, and a position is the same
arithmetic one axis over. A refused record (one no resolve produced) is refused
**before** it touches the out, so a sentinel survives.

## 5. The pass

### Block

A block box is placed in two steps:

1. `margin_origin` — the flow position, exactly as 10.9–10.11 left it: containing
   block's content origin plus the box's own margins.
2. `final_origin` — `LayoutPositionApply(margin_origin, offset)`, and the
   **result** carries `final_origin` while the *stacking* continues from
   `margin_origin`: the extent the parent stacks by is the margin box computed from the
   **flow** geometry, so a box shifted 100px down still leaves the hole flow gave it.

The used offset is kept on the object (`LayoutObjectPosition`), making the flow
position recoverable; it is the field that makes the two spaces two rather than one.

Counted **after** the resolve, behind the two early returns — an ordering the
determinism case found the hard way (§10, finding 1).

### Inline

An **atomic inline** carries its offset the same way a block box does: the offset is
read and resolved where the participant's other insets are, applied in two halves —
the **horizontal** half to the participant's `position.x` and never to the advance
below it (a relatively positioned atomic inline is drawn `dx` further along its line
while the cursor moves by its margin box exactly as it always would), and the
**vertical** half at line finalization, after the baseline correction, because a
member's `y` is not final until its line's baseline is (10.7).

A **transparent inline box** and a **text run** decline: a transparent box's rectangle
is the union of its contents and a text run is not a box at all, so the offset would
have nothing honest to move — the doors say so (`RoboNotFound`) rather than
answering a zero. Counted in the atomic branch only, for the same reason.

## 6. The object door

```c
LayoutObjectPosition(tree, id, &offset, diag);
```

The displacement between where flow put this box and where it is drawn. `RoboOk` for
any placed box — a zero offset *returned* rather than refused, because the zero is the
same either way ("no inset" and "no pass" both read zero here, and the result's state
distinguishes them) — and `RoboNotFound` for the three cases where no offset could
exist: an object nothing placed, a text run, and a transparent inline box with
contents. The out is cleared first, so a caller that ignores the code reads a stated
absence rather than a leftover.

## 7. What the counters carry

Both records grew four fields — `offsets_applied` (resolved sides), `offsets_declined`
(non-relative sides), `offsets_unresolved` (declared but unusable), and `boxes_shifted`
(boxes whose displacement is non-zero) — and `boxes_shifted` is the first counter in
this module that is about **where** rather than **how big**, and the first to count a
fact about the box rather than about a declaration. `LayoutBlockStatsEqual`
and `LayoutInlineStatsEqual` compare them, which is what made the ordering
finding in §10 visible at all.

## 8. Capabilities

`LayoutCapabilities` gains **`positioning`** — 1 since 10.12, the field after
`border` and (since 10.13) before `overflow`, which in turn sits before `painting`
(the struct's positional-walk suites assert the order).
`RoboLayoutPositioningImplemented` is its macro, and the claim is stated narrowly
like the nine before it: the mode and the four insets are read, resolved by one stated
precedence rule, and applied so a relative box is drawn where its offset puts it while
its size and flow contribution are untouched. **`absolute`, `fixed` and `sticky`
behaviour does not exist** (their values and facts do, 9.13; placing against a
containing block or a scrollport is 18.5's); no stacking context or paint order moves;
no percentage inset resolves.

The diagnostic-domain list ended with **`RoboLayoutDomainPosition`** (`"position"`,
the twentieth domain) — the first domain in this module about *where* rather than about
*how big* — until 10.13's `RoboLayoutDomainOverflow` (`"overflow"`, the
twenty-first) followed it, which is why the positioning suite now pins
`POSITION + 1 == OVERFLOW` rather than `POSITION == COUNT - 1`.

## 9. Out of scope, and where it lands

| Deferred | To |
|---|---|
| `absolute` / `fixed` / `sticky` placement | 18.5 (advanced positioning) |
| percentage insets | the containing-block/percentage phase |
| z-index, stacking contexts, paint order | paint/stacking phases |
| hit testing against final geometry | Phase 11/12 |
| direction-dependent over-constrained offset resolution | 18.5 |
| what a displaced box does to its parent's **overflow** | ~~done in 10.13~~ — a displaced box contributes its **region** (its final position grown by its extents), so a child moved out of its parent's box is recorded in the parent's overflow record while its own geometry is untouched (`Overflow.md` §10) |

The seam and the policy are written so 18.5 replaces the *resolution* without touching
the passes: the offset record is the pass's whole input, and a richer rule changes what
fills it, not who consumes it.

## 10. What the suites found

Recorded in `Tests.md` §17; the four findings in one line each:

1. **Counters after the resolve** — the block pass accumulated before it resolved, and
   a determinism case read the garbage; ordering is the fix.
2. **A stale fixture pointer is a double free** — the seam case destroyed a style the
   source had already replaced; teardown over what the source holds is the fix.
3. **A negative inset is honoured** — the one seam where the sign survives, and the
   fixture now carries the sign it was handed.
4. **A displacement composes with the flow stack** — the sibling case asserts the
   flow `y` and the final `y` differ by exactly the offset, no third number.

## 11. Test map

Nineteen cases in `layout/tests/SuiteRoboLayoutPosition.c`, domain
`layout.positioning.*`:

| Case | Asks |
|---|---|
| `kind_vocabulary` | the four kinds, their names, their order, and the twentieth domain |
| `inset_record` | the four kinds' predicates, `zero ≠ absent`, and equality |
| `position_record` | the cleared record, validity, equality, `is_relative` |
| `seam` | every kind the grammar allows, one side at a time, plus the mode's kind check |
| `offset_record` | the used record, its validity rule, its equality |
| `policy_precedence` | the one rule, hand-built: `left`/`top` win, autos say nothing, non-relative declines |
| `policy_static_declines` | the decline across all four non-relative modes, counted by mode |
| `apply` | the identity, the displacement, both saturations, and the refusal before the out |
| `block_relative` | the phase's central claim in three numbers |
| `flow_preserved` | the original space stays occupied; the sibling does not reflow |
| `block_negative` | negative offsets displace the other way, safely |
| `block_static_declines` | `static` in the pass: untouched, counted |
| `box_model_interaction` | margin, padding, border, each with the same offset |
| `descendants` | child geometry and the two inset records survive the displacement |
| `inline_atomic` | an atomic inline carries its offset, `dx` on the line and `dy` at finalization |
| `inline_transparent_not_applied` | a transparent box declines, and the door says so |
| `object_door` | the zero, the three refusals, and the cleared out |
| `relayout` | an offset change moves the box and moves nothing else |
| `determinism` / `allocator_balance` | identical runs are identical; nothing leaks |
