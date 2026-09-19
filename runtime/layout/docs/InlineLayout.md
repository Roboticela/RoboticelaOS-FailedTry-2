# RoboLayout — inline layout (Phase 10.4)

This is the design record for the fourth phase of RoboLayout: the other direction.
10.3 computed a coordinate in the one axis block flow owns — *down*. This one
computes the one it does not: the boxes that participate in an **inline** formatting
context are placed one after another along a line, and when the next does not fit,
the line moves down.

`docs/Architecture.md` remains the record for the module's architecture.
`docs/BlockLayout.md` remains the record for the block direction this phase
completes. `docs/FormattingContexts.md` remains the record for the context model
both directions read.

---

## 1. What the phase is

10.3 left two counts standing for boxes it could not place — `inline_level_skipped`
and `unreached` — and said in its own text that they were 10.4's to retire. This
phase retires the first of them.

The pass does not gain a second entry point. It gains a second **branch**:

```text
robo_layout_block_layout(tree, viewport, out_block_stats, out_inline_stats, diag)
        │
        └── per box, its contents are stacked by whichever direction it owns
                │
                ├── a box that owns a BLOCK context  ─► stack children (10.3)
                └── a box that owns an INLINE context ─► walk contents (10.4)
                                                                   │
                                            ┌──────────────────────┴───────────┐
                                            │ cursor along a preliminary line  │
                                            │ wrap when one does not fit       │
                                            │ union for a transparent box      │
                                            └──────────────────────────────────┘
```

```text
                    what an inline formatting context does

    1.  its participants are placed one after another from its content origin;
    2.  a participant whose width is known before it is placed that does not fit
        after the cursor starts a new line, and the line moves down by the
        tallest thing the previous one held;
    3.  a **transparent** inline box's own rectangle is the union of what its
        contents reached;
    4.  an **atomic** inline's contents are laid out by the block pass, inside it,
        and its height comes from what they reached.
```

`robo_layout_block_layout` keeps its shape — same name, same arguments, same
job — and reports **both** records. `robo_layout_inline_layout` does not exist,
and §9 says why.

The capability that flips is `inline_layout` — `0` since 10.2 wrote the field,
`1` now — through `ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED`, alongside
`block_and_inline_layout`, which is their conjunction.

---

## 2. The one distinction the phase turns on: atomic is not a degree of transparent

An inline-level box comes in two kinds, and 10.2 already carries the fact that
tells them apart — a box either established a formatting context or it did not:

| | Display | Established a context | On the line |
|---|---|---|---|
| **atomic** | `inline-block` | yes — its inner display is `flow-root` | opaque: a unit, placed by its own width |
| **non-atomic** | `inline` | no | transparent: its children are participants of the *same* context |
| **declined** | `block` | — | not on the line at all; CSS gives it an anonymous block box and this build makes none |

The two mistakes are symmetric and both are silent:

- treat a `span` as **opaque** and it is placed with no width — nothing in this
  build measures text — so everything inside it is dropped, and the document looks
  laid out with boxes missing;
- treat an `inline-block` as **transparent** and its contents stack in the *outer*
  line, which produces plausible numbers in the wrong coordinate space.

So the rule is stated once, in terms of the two facts the tree already carries,
and `layout.inline.stats` walks the third possibility as a case rather than as a
comment. It is the same shape as 10.3's *containment is not context*: the phase
turns on keeping two questions apart, and getting either backwards is a rewrite
rather than a bug fix.

### 2.1 Why no anonymous box was needed to start

10.3 said the anonymous block box is what a block-level box inside an inline box
needs. It is still true, and this phase still builds none — but it turned out not
to be the *entry condition* for inline layout, because 10.2 had already made an
inline formatting context the ordinary case:

```text
<div><span></span><span></span></div>

   the div has no block-level child, so it establishes an INLINE context
   and its own children are the participants of it.
```

That is the commonest shape in any document, and it needed no wrapper. The wrapper
is needed only for the *mixed* case — a block-level child among inline ones — and
that case is counted (`block_level_declined`) and left `UNRESOLVED` rather than
placed badly. So `ROBO_LAYOUT_OBJECT_KIND_COUNT` stays **2**, as 10.3's
conformance case asserts: an anonymous box is the one object kind that would have
no element and therefore no computed style.

---

## 3. Width: declared where resolvable, the containing block otherwise, zero for a transparent box

Three answers, and the middle one is 10.3's rule reused rather than restated:

| Participant | Its advance width |
|---|---|
| atomic, with a resolvable declared `width` | that width |
| atomic, with no resolvable declared `width` | its containing block's content width — CSS's `width: auto` |
| non-atomic | **nothing it declares** — `width` does not apply to a non-replaced inline box |

The first two rows count themselves (`widths_declared`, `widths_filled`) because
the difference between "sized by its author" and "filled its line" is the
difference a reader is trying to make.

The third row is CSS, not a shortcut, and it is the reason a transparent box is a
**union** rather than a size. Its rectangle is computed from the fragments its
participants produced — a real calculation — and it is **zero** when it contains
no box at all.

### 3.1 The zero, stated loudly

A `<span>` holding only text has no extent in this build, because a text node is
not a box and nothing here measures text. `layout.inline.textless` asserts the zero
*and* the reason it exists.

This is the phase's least comfortable number and it is deliberately not patched
over. A build that guessed a plausible width for text it cannot measure would
produce the one class of error a later phase cannot detect — every number would
look reasonable — whereas a zero is a fact a reader can be cross about. 10.5 is
the goal that replaces it, and `empty_contexts` counts how often a document
currently depends on it.

---

## 4. Wrapping, and what a "line" is here

The rule is one sentence:

```text
a participant whose width is known BEFORE it is placed, and which does not fit
after the cursor, starts a new line: the cursor returns to the line start and the
line moves down by the tallest thing the previous one held.
```

Width known before placement is exactly the atomic-or-leaf case (§3.1), which is
also why wrapping is the *only* thing the atomic/transparent distinction decides
for the cursor. A transparent box with contents is not pre-measured and is not
wrapped as a unit: its contents are placed one at a time and wrap individually,
which is what CSS does, and its own rectangle is the union of the fragments it
produced. `layout.inline.transparent_wraps_inside` is the case that pins it.

One guard in that rule is load-bearing:

```c
flow->line.cursor > 0 && (flow->line.cursor + width) > flow->available_width
```

The `cursor > 0` half is CSS's rule about a box wider than its containing block: a
participant that does not fit on an **empty** line is placed anyway and overflows,
rather than starting lines forever. `layout.inline.overflow_does_not_wrap` asserts
it with a 400px box on a 200px line.

Overflow itself is 10.13's and this pass sets no flag for it — a box that runs past
its line is a fact the result's two flag bits deliberately still do not carry.

### 4.1 A preliminary line is not a line box

*Written in 10.4, when nothing here produced either. §12 records what 10.6 made of
it; the argument is kept as it stood.*

What makes the cursor work is four numbers: a cursor, a top, a running height and a
count. It is **not** a line box, and the header says so in the same words:

> A line box is a box — it has an element-less identity, an alignment, a baseline
> and a height that comes from fonts — and it is 10.6's.

So the record reports the line **count** and nothing about any individual line, and
`layout.inline.empty_context` asserts that a container holding only text produces a
context with no participant and therefore no line. That distinction is what keeps a
later phase from reading a promise this one does not make.

---

## 5. Rebasing, and why it is a second walk

A participant is placed in the **owner's** content-box space while the context
runs, because that is the space a line lives in. But a box's containing block is
its **parent** (10.3's rule), and for a participant inside a transparent inline box
that parent is the inline box — not the context owner.

So when a context is complete, each participant that has a parent *within* the
context has that parent's origin subtracted from its position.
`layout.inline.nested_rebase` and `layout.inline.nested_two_levels` are the cases.

It is a second walk rather than a running adjustment, and that is forced by the
ordering rather than chosen: a transparent box's origin is a **union**, so a later
sibling that wraps can move the box's left edge, and a child written before that
happened would be left holding a stale offset. Writing parents after children is
the same order 10.3's `auto` height forces, and the rebase is exactly what it
costs. Two passes over one context is the honest price of a bounding box.

The recursion stops at an **atomic** inline, and the reason is the coordinate
convention rather than an optimisation: an atomic inline's contents were laid out
by the block pass in the box's own space, so they are already stated relative to
it and rebasing them again would subtract twice.

---

## 6. Where an atomic inline's contents are laid out

An `inline-block`'s inner display is `flow-root` — a **block** formatting
context — so its contents are not a line problem at all. The inline pass hands
them to the block pass, in the box's own space, through one door:

```c
robo_layout_block_walk_children(walk, span_id, span_width, &extent);
```

The door exists so there is no second child loop: `layout_block_children` is the
only place children are stacked, and both the block recursion and the inline pass
reach it. `layout.inline.atomic_contents_are_blocks` is the case — a `<p>` inside a
300px `inline-block` comes out exactly as wide as the span and inside it, with the
span's own height taken from the extent the `<p>` reached.

---

## 7. Determinism, and what a second pass means

The pass is a pure function of three inputs: the tree, the viewport and the
borrowed styles.

- participants are visited in **sibling order**, which the builder made document
  order, and the walk descends *through* transparent boxes so a `span`'s children
  are reached in the same loop as the `span`;
- the flow state lives on the C stack of the recursive call, and the union chain is
  the call stack's own shape — so there is **nothing allocated, cached or read from
  a previous pass**, and nothing to release when a call returns early;
- a box the pass declines is left `UNRESOLVED`, not given a zero rectangle.

`layout.inline.determinism` asserts two halves: two passes on **one context** — a
helper that made a fresh context per call would prove the geometry is reproducible
and say nothing about whether a second pass stacks on the first, which is the
failure a cumulative cursor has — and a second **tree** from the same document.

`layout.inline.viewport_changes_layout` is the other side: the line width an inline
context gets is the containing block's content width, and a block box fills its
containing block (10.3), so a narrower viewport re-places and re-wraps. `wraps` and
`lines` are asserted to *move*, which is the claim that the geometry is a function
of the input rather than of the first pass.

---

## 8. The records the pass leaves

Two records, reported side by side, neither derived from the other — because it is
one pass asked two questions. `stats.boxes_computed` is their two placement counts
added together, and the identity the block pass's suite asserts closes over both
directions at once:

```text
blocks_placed + inline_level_skipped + participants_placed + unreached == objects
```

| Field | Meaning |
|---|---|
| `contexts` | inline formatting contexts whose contents were walked |
| `empty_contexts` | of those, the ones with no participant — a `<p>` holding only text |
| `participants_placed` | inline-level boxes given geometry |
| `atomic_placed` | of those, the ones opaque to the line |
| `non_atomic_placed` | of those, the ones transparent to it |
| `block_level_declined` | block-level boxes inside an inline context — the anonymous-box case |
| `lines` | preliminary lines started |
| `wraps` | participants that began a new line |
| `widths_declared` / `widths_filled` | atomic widths, from a declaration or from the containing block |
| `heights_declared` / `heights_from_content` | atomic heights, likewise |
| `depth_max` | deepest participant placed |

`block_level_declined` is the mirror of 10.3's `inline_level_skipped` and it is
counted rather than dropped for the reason 10.3 gave: **"not placed yet" and "not in
the document" must not look the same.** One pass, two directions, and every box
that has no geometry is explained by one of the four counters.

---

## 9. Why there is no `robo_layout_inline_layout`

Because the two directions are **one pass**, and the reason is a dependency rather
than a preference:

```text
an inline formatting context's height IS the block extent of the box that owns it

   so block layout cannot finish before the lines exist,
   and the lines cannot be laid out before the block pass knows where their owner is.
```

A second public entry point could only duplicate that walk or run it in secret. The
door is `robo_layout_block_layout` — unchanged in what it is *for*, now laying out
both directions and reporting both records — and it is the one caller that decides
whether a box's contents are stacked or placed along a line, by reading the context
the box owns through 10.2's own door.

---

## 10. What 10.5–10.13 will consume

| Later goal | What it asks this phase |
|---|---|
| 10.5 text layout | `empty_contexts` — *how often a document currently depends on the zero* |
| 10.6 line boxes | `lines`, and the preliminary line it replaces (§4.1) |
| 10.7 baselines | the line's `top` and its running height, which is a union of extents rather than a baseline |
| 10.8 sizing | the atomic inline's two fallbacks (§3) and the `widths_filled` count |
| 10.9 margins | the union arithmetic (§5) that becomes the margin edge |
| 10.12 positioning | the rebase (§5) — the coordinate convention surviving nesting |
| 10.13 overflow | the `cursor > 0` guard (§4), which is the only place overflow is decided |
| the anonymous box | `block_level_declined` (§2.1), the count of what the wrapper will be for |
| 11 paint | a rectangle per participant, in the coordinate space §5 defines |

None of these needs a change to the inline model. What 10.5 adds is a measured
width for the participant that currently has none; what 10.6 adds is identity and a
baseline to the line that currently has neither.

---

## 11. What 10.5 and 10.6 turned out to be

*Added after the fact, because the table above is a prediction and two of its rows
have since been settled. Neither phase needed the change §1 promised the inline
model would not need.*

**10.5 gave the participant that had none a measured width, and nothing else.** The
zero §3.1 argues loudly for is still exactly what a text run gets from a pass with
no measurement seam; what changed is that a seam can now be installed, and then a
`<span>` holding only text is as wide as its font says. A text run is placed by the
same call an atomic inline is, and the inline pass gained no text-specific branch.
`empty_contexts` went on meaning what it meant — and it is worth recording that its
*example* changed rather than its subject: a `<p>` holding only text used to be the
archetype of an empty context and is no longer one, because it now has a
participant, which is `empty_contexts` falling to zero for the document that
introduced it rather than a case being deleted.

**10.6 gave identity to the line, and this header's §4.1 was half right.** The line
is now a record with an id, an ordinal, a finalized rectangle and its members — so
the "identity" half of the prediction landed. The "alignment, baseline and height
that comes from fonts" half did **not**: a line's height is its **tallest member**,
and there is still no baseline in this module. So 10.6 was a smaller phase than
§4.1 foresaw, deliberately, and the header of `RoboLayoutLineBox.h` says so where
the field is declared rather than in a remark here.

The one thing the inline pass itself gained is that `lines` stopped being the only
way to learn that anything was on a line. The counter is unchanged and still the
number it was — 10.6's record is what a caller reads back — so the counter became a
*check against the arena* rather than the answer, which is why the suite asserts the
two against each other.

And the distinction §2 drew is what 10.6 turned into an assertion:
`participants_placed` and the sum of the lines' member counts **differ** for a
document that nests inline boxes, by exactly the transparent boxes that enclosed
something. 10.6 made that difference visible instead of leaving it as a note.

---

## 12. Boundaries

- **No line box, no text measurement, no font metrics, no baseline, no shaping.**
  §3.1, §4.1, and `empty_contexts` is how the absence is *visible* rather than
  silent.
- **No anonymous box, and no automatic inner inline context for an atomic inline.**
  §2.1 — the wrapper is still 10.4's own successor's, and an `inline-block`'s
  contents are stacked because its inner display says so, not because a second
  wrapper was made.
- **No margin, padding or border arithmetic, and no `box-sizing`.** Unchanged from
  10.3: a box's content origin is still its top-left corner, so no coordinate is
  missing a term.
- **No declared width in the block direction, no shrink-to-fit, no percentage or
  relative-unit resolution.** §3, and the atomic fallback is stated rather than
  guessed.
- **No absolute or fixed positioning, no floats, no overflow flag, no
  fragmentation.** §4, and 10.12–10.13.
- **No paint, no rendering, no windowing, no display.** The viewport is still an
  argument (10.3 §3.1) and `Tools/Build.ps1 -Verify` enforces the boundary.
- **No CSS parsing, and no second display predicate.** Atomicity is read from the
  box's own outer display through 9.12's `robo_css_display_is_inline_level` and from
  the context the box established, which 10.2 stored. There is deliberately no
  "is this an inline-block" test in this module: that question is 9.12's, and the
  day `inline-flex` arrives the answer changes in 9.12's table and not here.
- **No Chromium.** Blink's `LayoutInline`, `InlineBox` and
  `LineBreaker`/`LineInfo` machinery are temporary technical reference only; the
  rules here are Roboticela's, and `Tools/Build.ps1 -Verify` scans this directory
  for a Chromium include and for a dependency on any higher layer.
