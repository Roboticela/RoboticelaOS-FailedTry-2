# Baselines (Phase 10.7)

Where a line box acquires a **floor** — an ascent, a descent and the kind of thing
they were derived from — and where every member stops sitting at the line's top edge
and starts sitting on that floor.

The phase is one rule and one consequence:

```text
line ascent  = the members' maximum ascent
line descent = the members' maximum descent
line height  = their sum

member y     = line.origin.y + (line.ascent - member.ascent)
```

Everything below is either that rule stated once, or the price of stating it where
the numbers are known.

---

## 1. The one structural decision: the metric is written where the measurement is, the floor where the maximum is final

10.4 placed a participant with `layout_inline_place` and gave it
`position.y = flow->line.top` — the line's top edge — which is the only answer
available while the members are still arriving. 10.6 then finalized the line and
kept that placement, and its own record says why: alignment was 10.7's.

The obvious way to add a baseline is to compute it in `layout_inline_place`, where the
measurement is in hand, and place the member from it. That is wrong, and the reason is
a maximum: the line's ascent is not known until the *last* member has been measured, so
the first member's final y cannot be computed when it is placed. A taller sibling that
arrives later would have to move it.

So the phase splits the fact in two, and the split is the design:

| Fact | Written | Why there |
|---|---|---|
| **The member's own metrics** (`object->baseline`) | `layout_inline_place`, the moment of placement | It is the measurement, and the measurement is in hand exactly then |
| **The line's floor** (`flow->line.ascent`, `.descent`, `.baseline_kind`) | `layout_inline_place`, accumulating a running maximum | Same function, because the maximum is a fold and the fold needs every placement |
| **The member's final y** (`placed->result.position.y`) | `layout_inline_finalize_line`, one walk of the member chain | The maximum is final here and nowhere earlier |

The third row is the phase's only change to 10.4's structure, and
`layout_inline_finalize_line` states the argument in the comment above it. What it
buys is that the correction is a **single pass over the members of one line**, at the
one moment the line's geometry is being written anyway — the same walk that fills the
back-link writes the y, because "this member's baseline is the line's" and "this
member is on this line" are the same event.

## 2. The metrics record, and the sign convention it fixes

`robo_layout_baseline_metrics_t` is three fields — a kind and two magnitudes:

```text
kind      what the baseline is anchored on: NONE, TEXT, BOX
ascent    baseline up to the top of the extent, in px, never negative
descent   baseline down to the bottom of the extent, in px, never negative
```

Both terms are **magnitudes**, and `ascent + descent == extent` holds for every value
the module produces. That is a decision rather than an accident: a font authors its
descent as a positive number in one table and a negative one in another, and
RoboLayoutTextFont.c normalises that once at the seam. A caller never has to ask
which way round a font wrote it.

The same record describes a *line* and an *object*, and that is deliberate: the same
two numbers describe both. What differs is the space the caller is in, and a space is
not a type.

### 2.1 NONE is representable and invalid, and those are different things

`ROBO_LAYOUT_BASELINE_NONE` is the zero-initialised state of the record, so it has to
exist. It is also the one kind `robo_layout_baseline_metrics_is_valid` refuses — a
NONE metrics is *unusable* rather than *empty*, while a TEXT metrics of zero extent is
usable and says something true.

That distinction is what makes the tree's two doors honest: an object that was
**not placed** is reported `ROBO_NOT_FOUND` ("no pass has placed this object") rather
than as a zero-extent floor, and the line arena refuses to store a line whose baseline
is NONE, so the doors and the arena agree rather than merely coexisting.

## 3. `from_run`: where the line gap becomes half-leading

A text run (10.5) stores `height = ascent + descent + gap`, where the gap is the
font's own line gap. CSS's half-leading splits that gap across the two terms, and
splitting it is this phase's job rather than 10.5's: the seam's one question is "how
wide is this run?", and a baseline is line geometry, which is this module's.

`robo_layout_baseline_metrics_from_run` therefore **recovers** the gap as
`height - ascent - descent` rather than adding a field to a frozen record — the gap is
not a second measurement, it is the part of the height that is neither above nor below
the baseline.

Three readings of a malformed run are worth naming, because each is a decision:

* **A height smaller than `ascent + descent`.** A third-party measurer can produce one
  — the record's contract does not forbid it, and a scripted measurer in a suite is
  exactly where it happens. Read as *no gap*, so the metrics' extent is the **larger**
  of the two numbers. That keeps the invariant the line's rectangle depends on: a
  member can never be taller than the line it is on, because a member's own height is
  its own ascent plus its own descent and both are bounded by the maxima.
* **A negative term.** Clamped to zero rather than propagated.
* **The order of those two.** The terms are clamped **before** the gap is recovered
  from them, and this is the one place where the obvious order is a defect. Computing
  the gap first and clamping after widens the gap by exactly the amount about to be
  discarded, so a zero-height run carrying `ascent = descent = -4` reports an extent of
  8 — a floor spanning something that spans nothing. The suite's negative-term case is
  what holds the order down; it is the phase's own bug, found by the phase's own test.

The odd pixel of an odd gap goes **below** the baseline, so the second term is written
`gap - half` rather than as a second division. A build that halved the gap twice would
be one pixel short on every odd gap and would pass every other case in the suite.

## 4. `from_extent`: the fallback, stated once

Everything that is not a text run takes the **bottom-edge fallback**: the whole of the
object's extent is above its baseline, so its feet are its floor.

```text
from_extent(h) -> { kind = BOX, ascent = h, descent = 0 }
```

This covers an atomic inline sized by its declarations, an atomic inline sized by what
it contained, and an empty inline box — which is zero-size and therefore sits on the
line's floor with no extent at all in either direction.

It is a deliberate simplification with a name, not a stopgap: CSS gives an inline-block
the baseline of its *last in-flow line box* and a replaced element a baseline from its
own box model. Both are later phases' rules, and `RoboLayoutBaseline.h` names them
where they would otherwise be assumed. What this phase owes is a **deterministic**
answer for the objects that have no font metrics, and one rule in one function is what
that looks like.

## 5. The line's baseline, including who owns the kind

`layout_inline_place` accumulates as it places:

```text
first member on the line      -> ascent and kind unconditionally
a later member with a strictly greater ascent -> both
a later member with a greater descent         -> the descent only
```

Two things fall out of "unconditionally for the first, strictly greater for the rest":

* **A line of unmeasured text reports a TEXT baseline of zero extent**, not NONE. The
  claim is that the floor is *anchored on text* and has nothing above or below it,
  which is a statement about the document rather than a missing answer — and a NONE
  kind here would make the line unstorable and the arena's refusal reachable from the
  field.
* **The kind belongs to the first member among the tallest**, which makes it a fact
  about *document order* rather than about the order a walk happened to visit. On a
  line holding a 16/5 text run and an 18/0 atomic, the kind is BOX — the atomic
  out-reaches the run — and the suite asserts that rather than the TEXT a reader might
  expect, so the rule cannot be quietly rewritten into "prefer text".

The descent takes no kind: it is a maximum like the ascent, but the *floor* is what a
baseline is, and only the ascent determines where it goes.

## 6. Two coordinate spaces, and the one that moved

A line box states its floor twice, and the difference is the space:

```text
robo_layout_line_box_baseline_offset   the baseline's distance from the line's TOP
robo_layout_line_box_baseline_y        the baseline's y in the space the line's
                                       origin is in -- its owner's
```

`baseline_y` is the conversion a renderer needs and is clamped rather than wrapped, so
a line near the top of the pixel range cannot report a floor above its own origin.

The subtler space is the **member's**. A participant's stored `position` is relative to
the *box that contains it* — 10.4's rebasing pass re-expresses every participant that
way once the unions are known — while a line's rectangle is in the space of the object
that owns the context. For a member that is a direct child of the context owner the two
coincide, which is why the common case reads naturally. For a run inside a transparent
inline box they do not, and a reader comparing such a member with its line must add the
box's own y: the run sits at the top of its box, and the box is what is placed on the
floor.

This phase did **not** change that convention; it inherited it and had to respect it.
The suite reads members through a helper that sums the chain to the owner for exactly
this reason — the alternative, comparing a parent-relative number against a line, is a
comparison of two different spaces that happens to pass whenever the nesting is one
level of zero-offset boxes.

## 7. Why the union walk had to move, and what did not change

The transparent-box union — a box's rectangle is the bounding box of what its contents
reached — used to run when each box *closed*, during the placement walk. That is no
longer possible, and the reason is §1: a member's position is not final until the line
is finalized, so a union computed before that would bound stale numbers.

The union is therefore a **post-order walk over the finished context**
(`layout_inline_union`), run immediately after the last line is finalized and before
the rebase that depends on it. The rule itself is unchanged: a box whose contents all
landed on a later line is still the bounding box of what they reached, and a box that
placed nothing is still zero-size at the cursor.

The order of the two closing walks is load-bearing and is stated in the code:
**union first, then rebase**, because the rebase subtracts a box's origin and that
origin is what the union just wrote.

## 8. Empty content, in three different senses

The three cases are different answers and the suite keeps them apart:

| Document | Line? | Baseline |
|---|---|---|
| `<p></p>` — no inline content | **none** | nothing to report; the doors miss |
| `<p><span></span></p>` — an empty inline box | one, 0 tall | BOX, 0/0: it is a leaf, so it is placed, and it sits on the floor with no extent |
| `<p><span>abc</span></p>` with no measurer | one, 0 tall | TEXT, 0/0: anchored on text, nothing measured |

The second and third rows are the ones a build gets wrong by making "no extent" and
"no baseline" the same answer. `robo_layout_text_stats_t.unmeasured` is what tells them
apart from the stats side, and the two line counters tell them apart from the line side.

## 9. The counters, and the partition they assert

`robo_layout_inline_stats_t` gains two fields, and they partition the lines:

```text
lines_from_text_baseline + lines_from_fallback_baseline == lines
```

Every stored line has exactly one kind and this build produces exactly two usable
kinds, so a build that invented a third or forgot to count one fails in the suite's
arithmetic rather than in a later phase's. `lines` itself is unchanged in meaning —
10.6's count of finalized lines — and so is `line_members`, which is still the
*placements*: the phase changed where a member goes and not which objects are members.

## 10. Re-layout

Baseline-dependent geometry is recomputed, not patched: a second pass with different
metrics moves the floor, and a narrower viewport re-wraps and produces different lines
with different baselines from the same tree. The line id is reused (ids restart at 1),
which is what makes "a baseline is replaced rather than accumulated" checkable — a
build that carried a maximum forward across passes would fail on the second one.

## 11. What is deliberately missing

* CSS `vertical-align` in any of its forms, including the inline-box and
  `sub`/`super`/length cases.
* `line-height` as a resolved used value: a line's height is `ascent + descent`, which
  is the *extent the metrics span* rather than a `line-height` computation.
* An inline-block's baseline from its last in-flow line box; replaced-element
  baselines; `struts`.
* Fragmentation of a line into fragments, alignment, and anything glyph-level — the
  first two are 10.6's stated omissions and remain so.

## 12. The invariant the suite asserts

Beyond the per-case numbers, `layout.baseline.accounting` walks every line of a
wrapped document and asserts, over each one:

```text
line.size.height                     == extent(line.baseline)
baseline_y(line)                     == line.origin.y + line.baseline.ascent
baseline_y(line)                     >= line.origin.y
baseline_y(line)                     <= line.origin.y + line.size.height
for every member: member.y + member.ascent == baseline_y
                  member.y                 >= line.origin.y
                  member.y + member.height <= line.origin.y + line.size.height
```

The two-sided containment is the property the union walk depends on, and the middle
two are what a wrong maximum breaks: a build that took the *minimum* ascent, or that
placed members from the descent, would fail the first equality immediately and the
containment on the member whose ascent exceeds the line's.
