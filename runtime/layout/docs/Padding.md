# Padding (Phase 10.10)

What an author asks for *inside* a box, and the one arithmetic that turns those
declarations into the box a reader sees.

10.9 answered how much room a box takes and put four numbers **outside** it. This
phase is the mirror image — four numbers **inside** it — and the difference between
the two is the whole of both phases:

```text
a margin     changes where the box is, what is left for it, and how far the next
             box down starts — and never changes the box;

a padding    changes how big the box is and where its contents begin — and never
             moves the box.
```

Files: `Include/RoboLayout/RoboLayoutPadding.h`, `Source/RoboLayout/RoboLayoutPadding.c`.
Suite: `layout/tests/SuiteRoboLayoutPadding.c` (20 cases).

## 1. The one structural decision: a padding grows a box and moves its contents, and never moves the box

Three numbers move:

```text
the box's border box      content box + padding, on each axis
        │                 (what robo_layout_result_t::size carries)

the content origin        the border-box origin + padding-left / padding-top,
        │                 where this box's children begin

what is left for an       the containing width less the margins (10.9) AND the
`auto` width              padding — CSS's constraint equation has both terms
```

and one number does not:

```text
result.position           the box's border-box origin — 10.9's margin origin with
                          the margins added, unchanged by any padding
```

That asymmetry is the same shape 10.9 drew in the opposite direction, and it is why
the result keeps holding *the box* while the insets that are outside or inside it are
kept **on the object**. A padding has no `auto` to distribute and no containing block
to resolve against, so nothing about it is derived the way a margin's distribution
is — but it still cannot live in the result, because the result holds the border box
by definition and the content box is that minus these four numbers.

### The identity that changed

Until this phase a box's content box **was** its border box, and every document said
so. `RoboLayoutMargin.h` closed with the prediction — "its border box *is* its content
box, which is the identity that changes when padding arrives" — and this is where it
arrives:

```text
robo_layout_result_t::size        the BORDER box: what the box's edges enclose
the box's own declarations        the CONTENT box: what `width` and `height` mean
```

`RoboLayoutResult.h` has said `size` is the border-box size since 10.1, "because that
is the only size paint can draw without re-deriving padding and border". That
sentence is now *doing something* rather than agreeing with a coincidence: the two
numbers are equal in every document that declares no padding, which is exactly why
nothing before this phase had to distinguish them.

### The order inside the pass

```text
read the declared dimensions
        │
        ▼
resolve the MARGINS                      ← what is left beside the box (10.9)
        │
        ▼
resolve the PADDING                      ← read and used; it distributes nothing
        │
        ▼
subtract the padding from what is left   ← the number an `auto` width fills
        │
        ▼
resolve the WIDTH                        ← into the CONTENT box (content-box sizing)
        │
        ▼
walk the children INTO that width
        │
        ▼
resolve the HEIGHT
        │
        ▼
derive the BORDER box                    ← content + padding is what `size` carries
```

The padding comes *after* the margins and *before* the width, and neither ordering is
a tidy one. The margins come first because they are outside the box and the number
they leave is what everything inside is measured from. The padding comes next because
`width: auto` is not "the containing block's width plus my padding": a box with
`padding: 10px` and no declared width is exactly as wide as its containing block, not
20px wider, and the subtraction that makes that true is the padding layer's — a single
axis rather than a size, because that is how a contained width arrives.

*10.11's outcome row:* two things in this document changed when a border arrived, and
neither is arithmetic. The box this phase derives and a result's `size` carries is the
**padding** box — `content + padding` — which is what `size` *was* until 10.11 and what
the border layer now derives the border box from; the door was renamed to say so
(`robo_layout_padding_edges_padding_box`), and every number it produces is unchanged
(`Borders.md` §1). The order above gained one step on each side, one layer out: the
border resolves **before** the padding because it is the outer inset, and the
subtraction the padding consumes is the *padding box width* rather than the containing
block's, so the paragraph quoted here ("a box with `padding: 10px` and no declared
width is exactly as wide as its containing block") is still true and now holds with
borders declared as well, because the number the padding subtracts from has already had
them taken out.

## 2. Three kinds, no `auto`, and why `ABSENT` is counted nowhere

```text
ROBO_LAYOUT_PADDING_ABSENT      nobody declared this side
ROBO_LAYOUT_PADDING_RESOLVED    a px length, NON-NEGATIVE, usable here
ROBO_LAYOUT_PADDING_UNRESOLVED  declared, and not a value this build can use
```

`ABSENT` is the zero of the vocabulary, and the reason is the one 10.9 paid for: CSS's
initial value for every padding is `0`, so a side nobody declared and a declared
`padding: 0` are **one used value and two facts**, and the fact only the second one
carries is that a declaration existed. `stats.paddings_applied` is "how many paddings
did this document have", asked once per placed box, and a record that counted an
undeclared side as a used length answers **four** for every box in every document —
including one whose stylesheet declares no padding at all. So `ABSENT` is written by
`robo_layout_paddings_init`, used as zero in geometry, and counted nowhere.

`RESOLVED` is **non-negative**, and that is the one respect in which this vocabulary
differs from the margin one: `padding: -10px` is invalid CSS and the classification
refuses it, where `margin: -10px` is a technique. The two readers share one
classification and differ in exactly one parameter (`allow_negative`), which is the
whole of the difference between the two seams.

There is **no `AUTO` kind**, and the absence is a decision rather than an oversight.
The shared classification *can* answer `AUTO` — it is one function — and this reader
maps that answer onto `UNRESOLVED`, for the reason 9.11 gives for rejecting
`padding: auto` outright: the CSS grammar has no such value, so a style carrying one
has violated its own grammar, and inventing a fourth kind would give that violation a
vocabulary of its own and invite a later goal to give it a meaning.

## 3. One policy, where margins needed two

10.9 kept two doors — a block box distributes a horizontal `auto` as its share of the
leftover beside a declared width, while an atomic inline turns every `auto` into zero
— because the difference *is* the two CSS rules, and a flag would let a caller pass
the wrong one and get a plausible answer.

Padding has no `auto`, so there is nothing to distribute and nothing for two policies
to disagree about: the same four declarations mean the same thing to a block box, an
atomic inline and a transparent one. **One door** follows from the grammar, and the
whole policy is three rules rather than six:

```text
an ABSENT side      used as ZERO, counted NOWHERE
a RESOLVED side     used as written (non-negative by construction)
an UNRESOLVED side  used as ZERO, and COUNTED
```

`robo_layout_padding_resolve` cannot fail on a document, because every declaration has
a stated answer. The two callers — `layout_block_box` and `layout_inline_place_leaf` —
invoke the same function, and the inline path uses the same sizing *helper* too, since
"what this box's declarations ask for, into this containing block" is one question.
What differs between the two paths is only the containing block, and since 10.10 even
that differs by the same subtraction.

## 4. The two derived numbers, and the one clamp

`robo_layout_padding_edges_t` is what the four declarations came to — and here,
unlike a margin, "the used value" is "the declared value, or zero", so the record
exists for the reason every other record does: it is the shape the passes read, what a
determinism case compares, and where the two counters live.

Two doors go from it to a number:

| Door | Answers | Behaviour at the edge |
|---|---|---|
| `robo_layout_padding_edges_content_width` | the width left for a box's **contents**, given the border box it would occupy | **floored at zero** |
| `robo_layout_padding_edges_border_box` | the **border box** a content box asks for | **saturates**, in 64 bits |

Both are one clamp in one place, and each clamp has its own argument.

*10.13's outcome row:* the `..._border_box` door does one more job than it was built
for, and it is the job CSS gives the padding box for the same reason. When 10.13
measures a box's overflow it does so against the **padding box** — the border box less
the borders — because that is CSS's clipping edge (`overflow: hidden` clips to the
padding edge, not the border edge and not the content edge) and because it is the one
rectangle a box's own declarations cannot move: a margin is outside it, a border is
outside it, and a padding is *inside* it. So a box with `padding: 20px` and a child
that sticks 5px out of the content area has an overflow of 25, not 5 — the padding is
part of the box that the child left, and it is this layer's number that says so
(`Overflow.md` §4).

`content_width` floors, because the alternative is a negative available width, and
size arithmetic that can go negative is size arithmetic that can produce a rectangle
nobody can draw. A border box narrower than its own padding leaves no room for
contents, and "no room" is zero. The floor is applied to the *result* rather than to
each term, so a `padding-left: 100px` with `padding-right: 0` and a 50px border box
still reports 0 rather than a negative that a later `auto` would fill from.

`border_box` saturates, because content and padding are each bounded by the seam's
maximum and **their sum is not**: a `width: 2147483647px` box with
`padding: 10px` is a document an author can write, and the honest answer is a size
that saturates rather than one that wraps into a negative rectangle. The clamp is on
the *sum*, so a box whose content already saturates keeps the padding it cannot
express rather than losing it to a subtraction. It is computed in 64 bits and refuses
only a negative *content* — a box's own extent is never negative — and a record
`robo_layout_padding_edges_is_valid` rejects, which is the one thing a padding can
fail on: a hand-built record. That is why the padding domain exists and why the
refusal is a diagnostic rather than a silent zero.

## 5. `box-sizing: content-box`, stated loudly

Stated loudly because it is the decision every number above follows from, and because
`box-sizing` is a later goal's property that this build does not have. The model is
CSS's **initial value**: a declared `width` or `height` is the **content** box, and
the border box is `content + padding`. Three consequences:

```text
a declared `width: 200px` with `padding: 5px` IS 200 wide inside and 210 outside,
and the box's children are laid out into the 200 — a build that read the
declaration as a border box would give the children 190;

`width: auto` fills what the margins AND the padding left, because CSS's constraint
equation has a padding term on each side;

a `min-width`/`max-width` bound applies to the content box, which is where
content-box sizing puts it, and the bounds path is 10.8's unchanged.
```

A later `box-sizing` phase adds one branch — "subtract the padding before the
declaration is used" — at the single place the reader hands the sizing layer a
number, and changes nothing else in this module.

## 6. Where the padding lives when the pass is over

One place, and it carries something the result cannot:

- `robo_layout_result_t` carries the **box**: `position` is the border-box origin and
  `size` is the border box, so a caller that reads one result has a rectangle it can
  draw without re-deriving anything.

- `robo_layout_object_t::padding` carries the **used padding**, read back through
  `robo_layout_object_padding`. This is the field a later phase needs first, because
  it is the answer to the one question the result cannot answer: **where a box's
  contents begin**. A result carries the border box, so the content origin is that
  origin plus `top`/`left` from here, and the content box is that border box less the
  four numbers. Re-reading `padding-left` off a computed style would mean re-deciding
  whether the declaration was one this build could use.

The door refuses three things, with the same reasons the margin door gives — an id
that names nothing, an object nothing placed, and a **text run** or a **transparent
inline box**, to neither of which any padding applies. A transparent inline box's
rectangle is the union of its contents rather than a placed size, and answering a zero
with four resolved sides would tell a caller the author's padding had been honoured
and come to nothing. The two doors are deliberately separate rather than one door with
a record kind, because an `inline-block` has both records, a transparent inline box
has neither, and the refusals are about *the record* rather than about the box.

## 7. What the suite asserts

Twenty cases in five groups, and the boundaries between them are the phase:

- **the vocabulary** — three kinds, their names, the predicates, the miss
  `robo_layout_padding_length` reports on a non-`RESOLVED` side, the cleared record as
  `ABSENT` against `robo_layout_padding_zero()`'s declared zero, and the
  non-negativity `robo_layout_padding_is_valid` states rather than trusts.
- **the seam and the record** — `robo_layout_paddings_of` over a fabricated computed
  style (four longhands, an undeclared side, a percentage, a relative unit, a negative
  length, and an `auto` — the one the fold turns into `UNRESOLVED`), the four sides of
  a shorthand, and the edges record's own validity rules.
- **the two derived numbers, driven directly**, so a wrong content width fails at the
  arithmetic rather than three boxes later in a coordinate: the border box, the
  content width, the floor, the saturation, and the two refusals.
- **the pass** — a declared width and an `auto` width, all four sides, vertical
  geometry, stacking, a declaration this build cannot use, a negative declaration, the
  margin/padding interaction the phase exists to keep distinct, a child's content
  width, an atomic inline's border box and advance, a transparent inline box's
  declared padding *not* applied, the object door's four refusals, re-layout,
  determinism, and the allocator balance.
- **`layout.paddings.block_auto_width`** is the case the model turns on: a box with
  `padding: 10px` and no declared width is as wide as its containing block, not 20px
  wider, and a build that had applied the padding after the width would report the
  containing block plus 20.

Every fixture declares `html` and `body` as **block**, for the reason 10.9's suite
records: an element nothing declared for computes to `display: inline`, which is
9.12's initial value, so a document that declared only the subject would never reach
the subject at all.

## 8. What the phase changed in the suites that came before it

| Case | Was | Is |
|---|---|---|
| `layout.block.pipeline` | card `width: 200px; height: 100px; padding: 5px` → 200 × 100 | → **210 × 110** |
| `layout.conformance.pipeline` | card → 200 × 100 | → **210 × 110** |
| `layout.conformance.pipeline` | `<body>`/`<html>` height → 120 | → **130** |
| `layout.conformance.pipeline` | `<p>` inside the card → 200 wide | → **200** (unchanged, and now the point) |
| `layout.conformance.pipeline` | padding counters absent | → **`paddings_applied` 4, `paddings_unresolved` 0** |

The third is the phase seen one level up: the extent the cursor reached is the card's
margin box, which now spans all three of the card's boxes — 10 (margin) + 110 (the
border box, whose `height: 100px` is a *content* box plus 5px on each axis) + 10
(margin). The fourth is the one that **did not** move and is worth writing down: the
card's border box is 210, but its content box is the 200 its author declared, so the
paragraph still fills 200 — not the 210 the card occupies and not the 190 a
`border-box` reading of the declaration would give its child.

Seven other assertions moved and none was weakened: the version pin (`0.10.0` in
three places), the capability table in two suites (`padding` is an eighteenth field,
seventeen of eighteen implemented), `ROBO_LAYOUT_DOMAIN_COUNT` (18, with `padding`
newest, and `margins` now one behind it), the `layout.module.allocator_balance`
version string, and the conformance statement. Each carries a note in place saying
which phase changed it and why — the convention 10.8 and 10.9 both used.

## 9. Re-layout, and why it needed no machinery

`layout.paddings.relayout` changes a declaration and re-runs the pass, and changes the
**viewport** and re-runs it — the second says a padding is not a function of the
containing block, which is the difference between an *inside* inset and a distributed
`auto`. The invalidation itself is 10.3's — a rebuild releases the classification and
every result with it — and this case drives it rather than re-implementing it. The
used padding is cleared with the geometry in the same sweep, for the reason the
margins are: a used padding that outlived the classification would be a
plausible-looking number about a layout that no longer holds.

There is no padding cache and no invalidation flag, for the reason 10.8 and 10.9 gave:
the pass is a pure function of the tree, and a padding resolution is a function of a
declaration and nothing else at all.

## 10. What is deliberately missing

Each a named later goal:

- ~~**overflow**~~ **done in 10.13**: the reference edge is the **padding box** this
  layer derives, so the padding is what makes a child's overhang count — see §4's
  outcome row.

- ~~**borders**~~ **done in 10.11** (`Borders.md`). What this section got right is
  that the door is named for what it adds; what it got wrong is *which* box it adds to.
  `..._border_box` returned `content + padding` — the padding box — and the border
  layer's `..._border_box` is the composition above it. The padding itself is still
  exactly what this phase made it: a border changes the size these four numbers sit
  inside and never changes the numbers.

- **padding on a transparent inline box** — a `<span>` with contents, whose rectangle
  is the *union* of what it contains. CSS does grow such a box's inline advance by its
  horizontal padding and its line box by its vertical padding; this build applies
  neither, and the reason is not economy: an inline box's rectangle is derived from
  its contents rather than placed, so padding on it is a rule about *fragmenting* an
  inline box across lines, which is the anonymous-box and line-breaking work this
  module has not done. It is stated in the header, asserted as a value by the suite
  (`layout.paddings.inline_transparent_not_applied`), and is the thing a later goal
  changes — the same treatment 10.9 gave to margin collapsing.

- **percentages** — `padding: 5%` resolves against the containing block's *width* in
  CSS, for every side including the vertical ones. This build has a containing width
  and could; it does not, because 10.8 left every percentage unresolved and resolving
  one here would make `padding: 5%` the only percentage in the module that means
  something. It is `UNRESOLVED` and counted, and the phase that resolves percentages
  owns all of them at once.

- **`box-sizing: border-box`** — §5. The model is the CSS initial value so that the
  later property is a branch rather than a change of meaning.

- margin collapsing, which is 10.9's and still absent: adjacent vertical margins
  **sum** here, and this phase changes none of that arithmetic.
