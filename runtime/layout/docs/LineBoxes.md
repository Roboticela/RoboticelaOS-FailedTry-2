# Line Boxes (Phase 10.6)

Where the preliminary line 10.4 maintained becomes a *record* — an id, a
finalized rectangle, and the list of objects it placed — and where the one
structural decision is that **the line being built is not a line box**.

---

## 1. The one structural decision: construction state and the record are different things

10.4 kept a line as four numbers and a cursor on the pass's stack and said so in
its own header, naming the line's identity, alignment, baseline and font-derived
(that baseline is 10.7's and is now a field; this record is about what 10.6 decided)
height as 10.6's. This is that phase, and the way it could have gone wrong is worth
stating first: the obvious implementation gives the line box a mutable record,
opens one when a participant is placed, mutates it as more are placed, and closes it
at a wrap.

This build does not do that, and not for style. The requirement the phase was given
— "keep finalized results separate from temporary construction state" — is met here
by the **shape of the types** rather than by discipline:

```text
layout_inline_flow_t        the line being built
   ├── cursor, top, height, member head      on the pass's stack
   └── never in the tree, never addressable

robo_layout_line_box_t      the finalized line
   └── in the tree's line arena, id-addressed, read-only from outside
```

There is no door that mutates a line box, because there is no code that would need
one: the pass holds the open line directly and writes the finished record **once**,
at `layout_inline_finalize_line`. `robo_layout_line_box_t` has no member that
describes a line still being built — no cursor, no member-head-to-append-to, no
"so far" — so a caller cannot reach the construction record at all, and a later
phase that wants incremental layout (10.14) has to add the distinction rather than
inherit it by accident.

### 1.1 A line box is a conclusion of a pass, so it lives where conclusions live

Formatting contexts are the precedent (FormattingContexts.md), and the reasoning is
the same one line for line: what a pass *concludes* is stored in an arena on the
tree, id-addressed, released wholesale by the next pass and by anything that
releases the classification. Lines join the contexts there, and
`robo_layout_tree_line_count` answers 0 for a tree no pass has laid out.

Two arenas, not one, and the split is the interesting half. The line records are
contiguous and ordered; the **member** records are not, because the placements of
one line interleave with the placements of another. Laying out an atomic inline
recurses into the block pass for its contents, and those contents build lines and
members of their own before the atomic inline itself is placed on its
grandparent's line. So members live in one shared vector that no range can
describe, each record names the next member of *its* line, and the line names the
head. Reading a line's members is therefore a chain walk, which is why
`robo_layout_line_member` documents itself as O(index) rather than pretending to be
an array access.

## 2. Members are placements, and the distinction is load-bearing

A line's members are the objects the line **placed** — the ones whose advance width
the cursor advanced by. In this build that is exactly three things, because exactly
three things call `layout_inline_place`:

```text
a text run                    (10.5)
an atomic inline              (inline-block — opaque to the line, 10.4)
a transparent inline box with NO contents
```

A **transparent inline box with contents is not a member of any line**, and that is
not an omission. Its rectangle is the union of what it reached (InlineLayout.md
§2), and a union can span several lines. Recording it as a member of one of them
would either state a rectangle the box does not have or double-count the width its
children already contributed.

So it stays what it is — a **participant of the context**, counted by
`robo_layout_inline_stats_t.participants_placed` — and `robo_layout_object_line`
answers `ROBO_NOT_FOUND` for it. The door's own note says so rather than leaving a
caller to discover it, because the alternative reading ("it has no line, so the
pass dropped it") is exactly the wrong conclusion.

Two consequences follow, and both are asserted by the suite rather than believed:

- the members of the lines of one context are exactly the objects that advanced its
  cursor, so summing `robo_layout_line_box_t.members` over all lines gives the
  number of **placements** (`robo_layout_inline_stats_t.line_members`) and **not**
  the number of participants;
- `line.size.width` is the **sum of its members' widths**, which equals the cursor
  the line finished at, and `line.size.height` is the extent of the **baseline**.

Both are this build's rule, and the second was 10.6's "tallest member" until 10.7
replaced it: the height is now `ascent + descent`, where the two are the members'
maxima. The header says so where the fields are declared rather than in a comment
somewhere else, and the two are **equal whenever the members share a font** — which
is why the change is invisible in 10.6's own cases and why the phase that made it
put a case in a document with two fonts in it.

## 3. Geometry, and the space it is in

A line's origin and size are in its **owner's content-box space**, the owner being
the box that established the context. That is the space the line lives in, exactly
as a box's result is in its containing block's. For a direct child of the owner, a
participant's result and the line's rectangle are therefore in the same space and
can be compared without arithmetic, and the case that asserts
`line 1: x == 0, y == 0` is a statement about two numbers in one space rather than
about a conversion.

A line is **not** rebased into a transparent inline box's space the way a
participant is. 10.4's rebase exists because a participant's coordinates are written
in the space of the box it was *placed into*; a line belongs to the **context**, not
to a box inside it, so there is nothing to rebase it out of.

`available_width` is recorded per line — the owner's content width — because "why
did this line wrap?" is otherwise unanswerable from the line alone: the answer is a
comparison against a number the line does not otherwise carry, and a later phase's
overflow reporting will want it.

## 4. The lifecycle, as the pass actually runs it

```text
begin IFC (owner, content width)
   │
   ├── nothing placed yet  →  no open line
   │
   ├── first placement
   │        └── open a line: cursor 0, top = the flow's y, height 0, no members
   │
   ├── each further placement
   │        ├── does it fit?  cursor == 0  →  yes (a participant wider than its
   │        │                                   line overflows rather than loops)
   │        └── cursor + width > available_width  →  finalize, then open the next
   │
   ├── a text run is measured first and placed with the width it measured (10.5)
   └── a transparent box with contents places its *children* and takes the union

finalize:
   ├── write the record: id, FINAL, context, owner, index, origin, size,
   │                    available_width, member count
   ├── append the line to the arena, remember the id
   └── the flow forgets its line, so the next placement opens a fresh one

end of context:
   └── finalize the open line, if any
```

`begin` opens nothing. A line exists because something was placed on it, so an
**empty inline formatting context has no line boxes** — CSS's rule, and the reason
the phase has no "empty line" case to special-case. A line whose single member is an
**unmeasured** text run *does* exist, with width 0 and height 0, and the distinction
between "no line" and "a line with nothing measurable on it" is exactly the
distinction 10.5 drew between a source that answers nothing and a source that
answered zero.

## 5. What the block pass gets from the lines

An inline formatting context's height *is* the block extent of the box that owns it,
so block layout cannot finish before the lines exist and the lines cannot be laid
out before the block pass knows where their owner is. That is why the two directions
are **one walk** with no second public entry point (InlineLayout.md §9) and why the
block pass returns the extent the inline walk reached.

10.6 changes nothing about that coupling and one thing about its output: a box
whose height comes from its contents now gets it from the lines' extent, which is
the same number the preliminary line produced and is now derived from records a
caller can read back. `robo_layout_inline_stats_t.lines` therefore stops being the
*only* way to learn that anything was on a line — it becomes a check against the
arena, and the suite asserts it as one.

## 6. The state vocabulary, and why OPEN is not dead weight

`robo_layout_line_state_t` has two values. Every line a caller can read is `FINAL`,
because `robo_layout_tree_line_at` and `robo_layout_tree_line` read the arena and
`layout_inline_finalize_line` is the only writer.

`OPEN` exists anyway, for the reason the context kinds have a `NONE`: the
vocabulary is the honest description of the lifecycle the phase implements, and a
later phase that keeps an open line across a call boundary — incremental layout,
10.14 — would otherwise have to add the distinction to an already-released API. The
header states the invariant that makes it safe today: an `OPEN` line is **never
stored**, which is why no tree door can hand one back.

## 7. Why there is no new diagnostic domain

A domain is where a failure is *reported* (RoboLayoutTypes.h), and every failure
this phase can produce is raised by the inline pass that builds the lines. An arena
that could not grow is the only one it has, and that is reported in
`ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT`. A `LINE_BOX` domain with no failure of its own
would be a name with nothing behind it — the same argument 10.2 used to refuse a
`FLEX` context kind and 9.12 used to refuse `display: flex`.

## 8. What is deliberately missing

- ~~No baseline and no `line-height`.~~ **10.7 supplied the baseline**, as this
  bullet predicted it would: the line carries an ascent, a descent and the kind they
  came from, and each member is placed from them rather than at the line's top. What
  remains missing is `line-height` **as a resolved used value** and CSS
  `vertical-align`, both of which are later phases' and are named in
  `docs/Baselines.md` §11 rather than left to be discovered.
- **No fragmentation.** A participant is never split across lines, so a run too
  wide for its line **overflows** rather than breaking. `cursor == 0` is the guard
  that keeps that from being an infinite loop.
- **No Unicode line breaking and no whitespace collapsing.** The wrapping rule is
  "does it fit", and it is the same rule 10.4 wrote.
- **No bidi reordering, alignment, `text-align`, `vertical-align`, or painting.**
  Since 10.7 a line is a rectangle, a baseline and a membership list.

## 9. The invariant the suite asserts over every tree

```text
the members of the lines of one context are exactly the objects that advanced
its cursor — no more, and no fewer
```

`layout.line.members_are_placements` walks a document that nests inline boxes,
where the participant count and the member count *differ*, and asserts the
difference rather than the equality. A build that recorded a transparent box as a
member would fail it by double-counting; a build that skipped an empty transparent
box would fail it by one. Both are plausible implementations, which is why the case
is a walk over the arena rather than a single arithmetic expectation.
