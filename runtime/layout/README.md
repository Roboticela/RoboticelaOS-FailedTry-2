# RoboLayout

Layout subsystem for Roboticela Runtime (**through Phase 10.15, layout tests — Phase 10
complete**).

**Version:** 0.15.0 — `ROBO_LAYOUT_API_FROZEN=0` (the surface grows again in Phase 11,
where paint reads what this module placed)

`ROBO_LAYOUT_GEOMETRY_IMPLEMENTED=1` — this build computes coordinates, in both
directions.

`ROBO_LAYOUT_FORMATTING_CONTEXTS_IMPLEMENTED=1` — this build classifies every box
into the context it participates in and the context, if any, it establishes.

`ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED=1` — this build stacks block-level boxes
down the page and gives each one a position and a size.

`ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED=1` — this build places an inline formatting
context's participants along a line, wrapping when one does not fit.

`ROBO_LAYOUT_TEXT_LAYOUT_IMPLEMENTED=1` — this build measures a run of text through
a font, and a text node is a participant of the line it was placed on.

`ROBO_LAYOUT_LINE_BOXES_IMPLEMENTED=1` — this build produces line boxes a caller
can read back: identity, finalized geometry, and which objects went on them.

`ROBO_LAYOUT_BASELINES_IMPLEMENTED=1` — this build gives every line a **floor** —
an ascent, a descent and the kind they were derived from — and places each of its
members on it rather than at the line's top edge.

`ROBO_LAYOUT_SIZING_IMPLEMENTED=1` — this build resolves a box's **width and
height** from its declarations, and a box's resolved width is what its children are
laid out into. A percentage is read and recorded as `UNRESOLVED` rather than
resolved against a containing dimension, and intrinsic sizing does not exist yet.

`ROBO_LAYOUT_MARGINS_IMPLEMENTED=1` — this build places a box at its **margin
origin**, lets a horizontal `auto` take its share of the leftover beside a declared
width, and stacks a parent's children by their **margin boxes**. Margin collapsing
does not exist yet: adjacent vertical margins sum.

`ROBO_LAYOUT_PADDING_IMPLEMENTED=1` — this build reads a box's **four padding
longhands** and turns them into the box a reader sees: a result's `size` is the
**padding** box (the content box plus the padding), its contents begin
`padding-left`/`padding-top` inside that, and an `auto` width fills what the margins
*and* the padding left. The model is `box-sizing: content-box` — CSS's initial value
— so a declared `width` is the *content* box. Percentage padding is read and used as
zero, and no padding applies to a transparent inline box.

`ROBO_LAYOUT_BORDER_IMPLEMENTED=1` — this build reads a box's **twelve border
longhands** and turns them into the box a paint pass can draw: a result's `size` is
the **border** box (the padding box plus the border), its contents begin
`border-left`/`border-top` and *then* `padding-left`/`padding-top` inside that, and an
`auto` width fills what the margins, the border *and* the padding left. The one rule
this layer adds is that a side's **used** width is zero unless its style draws —
`border: 5px none` costs no geometry — and the whole per-side record (used width,
style, colour) is readable from the object so Phase 11 re-reads no declaration.
`border-radius` and any border on a transparent inline box do not exist yet.

`ROBO_LAYOUT_POSITIONING_IMPLEMENTED=1` — this build reads a box's **`position`**
and its **four inset longhands** and turns them into a displacement: a `relative`
box is drawn where its offset puts it — `left` over `right`, `top` over `bottom`, an
`auto` saying nothing — while its **size** is untouched and its **flow** contribution
is untouched, so the following sibling starts exactly where it always would have. A
result carries the *final* position; the *flow* position stays recoverable through the
used offset the object keeps. `static` remains the default and means no displacement;
`absolute`, `fixed` and `sticky` are represented and declined (placing one is 18.5's).

`ROBO_LAYOUT_OVERFLOW_IMPLEMENTED=1` — this build reads a box's **`overflow-x` and
`overflow-y`** (and the `overflow` shorthand, for either axis a longhand said nothing
about) and records what its contents reached **beside** the box: the result's size is
untouched, and the box carries a four-extent record measured from its **padding box**
(CSS's clipping edge) plus the used mode per axis. `visible` and `hidden` are the two
modes; `hidden` *exposes* a clip rectangle and clips nothing here, because this phase
does not paint. A child contributes its **region** — its padding box grown by its own
extents and clipped back on an axis it hides — so a clipping child contributes only
what it kept. Scrolling, scrollbars, `overflow: auto`, `text-overflow` and CSS's axis
promotion do not exist yet.

Public RoboLayout never depends on Chromium, and it does not depend on paint,
render, compositing, JavaScript or the shell. It is the **first module in the tree
allowed to see RoboHTML and RoboCSS together**, which `DEPENDENCIES.md` permits
precisely here and nowhere below: the two are peers and must not know each other,
so whoever needs both sits above both.

## Scope

**10.1 (layout architecture).** The phase exists to build the *shape* of a layout
pass and to state the boundaries the following fourteen goals fill in.

**10.2 (formatting contexts).** Which boxes lay each other out, and in which
direction. A box now carries two context ids — where it *participates* and what it
*establishes* — and the pass that walks the tree answers both, so that 10.3 and
10.4 each start with a question answered rather than a question to re-derive. A
context is a grouping, and no member's geometry came from it; that is 10.3's.

**10.3 (block layout).** Where each block-level box goes and how big it is. The
first phase that computes a coordinate, and its one structural claim is that
**containment is not context**: a box is placed inside its *parent*, and its
*formatting context* decides which pass stacks its contents. A block box fills its
containing block's content width, its block-level children stack below one another
from its content origin, and its height is a declared `px` length where this build
can resolve one and the stacked extent of its children otherwise.

**10.4 (inline layout).** The other direction, and it is a *branch* of the same
pass rather than a second one: a box that owns an **inline** formatting context
has its contents placed along a preliminary line, wrapping when one does not fit.
Its one structural claim is that an inline-level box is **atomic** or
**transparent** and never a degree of the same thing — an `inline-block` is
opaque to the line and sized before it is placed; an `inline` is transparent to it,
so its children are participants of the *same* context and its own rectangle is the
**union** of what they reached. The same pass therefore reports two records, and
`boxes_computed` is their two placement counts added together.

**10.5 (text layout).** The width a `<span>` holding only text produces. A text
node is not an element and has no `display`, so it is the one thing the object
vocabulary genuinely had to widen for (`ROBO_LAYOUT_OBJECT_TEXT`); it borrows its
parent element's computed style, derives its display as inline, and becomes an
**ordinary participant of the line** rather than a special case in the inline
pass. Where the width comes from is a **seam**: a callback a caller installs, with
a RoboFont-backed implementation supplied beside it. A pass with no seam measures
every run as zero and **counts it**, which is a legitimate way to run the module
and not a failure.

**10.6 (line boxes).** The preliminary line becomes a record. The structural claim
is that **the line being built is not a line box**: construction state stays on the
pass's stack and an immutable record is appended to the tree's line arena once, at
finalization — so "keep finalized results separate from temporary state" is the
shape of the types rather than a rule anyone has to remember. A line's **members
are the objects it placed**, which is not the same set as the context's
participants: a transparent inline box *with* contents encloses members without
being one, because its rectangle is a union and a union can span lines.

**10.7 (baselines).** Where the line gets a floor, and where members stop sitting
at its top edge. The structural claim is a **split of one fact across two
moments**: a participant's own metrics are recorded when it is *placed*, because
that is when the measurement is in hand, but its final y is written when the line
is *finalized*, because a line's ascent is a maximum that is not final until the
last member arrives — so the correction is one walk of the member chain at the
moment the line's geometry is written anyway. The line gap a font reports becomes
CSS half-leading here (the seam's one question stays "how wide is this run?"),
everything that is not a text run takes a documented **bottom-edge fallback**, and
the two coordinate spaces a baseline is read in — the line's own and its owner's —
are named by two doors rather than left to a reader to infer.

**10.8 (width/height).** Where a declaration becomes a size. The structural claim is
a **split of one number into two uses**: a box's resolved width is what its children
are laid out *into*, not merely what it is — so the width resolves first (it depends
on nothing), the children go in at it, and the height follows (it depends on them).
For an `auto` width the two numbers are the same, because `auto` *is* “fill the
containing block”, which is why every earlier expectation about a box with no
declared width still holds. A dimension is **three kinds** (`auto`, a definite px
length, and *declared but unresolvable* — a percentage or a relative unit, which
resolves like `auto` today and will not when a containing dimension is known), the
bounds are applied through 10.1's own clamp so “min wins over max” is still decided
in one place, and both directions ask `robo_layout_object_sizing` the same question.

**10.9 (margins).** The first number in this module that is *not* a size. A margin
moves a box and grows the room beside it while leaving the box itself alone, and that
asymmetry is the whole phase: a result's origin becomes its **margin origin** while
its `size` stays the content box, and what a parent stacks its next child below is
the **margin box**. The four used margins therefore live **on the object** rather
than in the result — adding them back is the arithmetic this phase exists to stop a
caller from re-deriving. A margin is one of **four kinds**, and "nothing was
declared" is one of them: CSS's initial value makes an absent side and a declared
`margin: 0` one used value and two facts, and it is the *counters* that need the
difference, which is why the kind was added rather than the two being folded. Two
resolution doors rather than one with a flag, because a block box distributes a
horizontal `auto` beside a declared width and treats a vertical one as zero while an
atomic inline turns every `auto` into zero — and collapsing is deliberately not
implemented, with the shape left for it in one record of numbers.

**10.10 (padding).** The mirror image of 10.9, and the phase that retires the
identity a box's content box used to have with its border box. A padding changes two
things and neither is where the box is: a result's `size` becomes the **border** box
(the content box the sizing layer resolved, plus the padding on each axis) and the
box's **contents** begin `padding-left`/`padding-top` inside its border edges — while
`position` stays the border-box origin it was. Three kinds rather than the margin
layer's four, because the CSS grammar has no `padding: auto`; one resolution door
rather than two, because with no `auto` there is nothing for a block box and an
atomic inline to disagree about; and a padding is **non-negative**, which is the one
parameter (`allow_negative`) by which the two readers differ over a shared
classification. What is new is the *arithmetic*: the containing block less the
margins **and the padding** is what an `auto` width fills, and the border box is
derived in the layer that owns the property rather than inside the sizing one —
because "how big did this box's declarations ask it to be" and "how big is the box"
are two questions and only the first is the sizing layer's.

**10.11 (borders).** The phase that completes the box model, and whose work was mostly
a *move*: a border is an in-box layer between the padding and the margin, so it grows
the **border box** and moves the **content origin** and never moves the box — the
exact mirror of 10.9's margin, which moves the box and never the size. The padding
layer's `border_box` door is renamed `padding_box` (its real meaning now that a border
exists) and the border layer composes above it, so the box model now reads
`margin → border → padding → content` with one owner per inset. Three seams rather
than one, because the three properties compute three different ways: a width is a
length *or* one of `thin`/`medium`/`thick` (a grammar no other property shares, so the
border reader asks the computed style's `px`-or-keyword door rather than the generic
one), a style is a closed ten-row vocabulary whose rows carry the one fact a paint
phase must ask — `generates_border`, false only for `none` — and a colour is 9.9's,
`currentColor` and all, rather than a second colour system. A zero width and a `none`
style both mean "no visible border" and both leave the geometry alone, because a
border that draws nothing must not reserve space; and an `auto` width fills what the
margins, the borders *and* the padding left, because CSS's constraint equation has a
term for each. It is the first phase where "this record is impossible" and "this
document is extreme" needed different answers: a negative *available width* is floored
at zero, but a negative *border record* is refused with a diagnostic.

**10.12 (basic positioning).** The phase that gives a box a second coordinate. A
`position` is a *displacement* — the first declaration in this module that changes
**where** a box is without changing anything about the box — and the phase's work is
keeping the two coordinate spaces two: every box is placed at its **flow** position
(the number siblings were computed from) and drawn at its **final** position (flow
plus the resolved offset), with the used offset kept on the object so neither is
derived from the other twice. One resolution rule stated narrowly (`left` over
`right`, `top` over `bottom`, an `auto` saying nothing), applied in 64 bits and
saturated, counted in four counters whose **decline** count is the one that tells a
`static` box with four insets from a `relative` box with four autos. Inline keeps its
line: an atomic inline's `dx` moves it along its line without touching the advance,
and its `dy` lands at finalization after the baseline correction.

**10.13 (overflow).** The phase that admits the contents may not **fit** — and whose
whole shape follows from refusing to call that a size. Every phase before it assumed a
box's contents stayed inside it; the tempting model is to grow the box, and it is wrong
in a way that is invisible in a single-box test, because a *size* cannot express an
overflow: a box that spills left and a box that spills right have the same width and
two different records. So the box keeps the size its declarations and contents asked
for and the overflow is four **extents** measured from the **padding box** — CSS's
clipping edge, and the one rectangle a box's own declarations cannot move — recorded on
the object beside a per-axis mode. Two rectangles fall out of that: the **region** (the
padding box grown by the extents, clipped back on an axis whose mode clips), which is
what a *parent* folds in, and the **clip rectangle**, which is what a *paint pass* will
clip to. That the parent folds in the child's region rather than the child's contents is
the recursion one line long, and the one case a suite needs a clipping child to see.
The three counters keep two facts apart that a naive count would merge: how many boxes
*clip* and how many boxes *overflow* are independent, since a document can declare
`overflow: hidden` on every box and have nothing stick out of any of them.

**10.14 (invalidation).** The phase that makes a conclusion **expire** — the first whose
subject is not a box's geometry but its truth, and the last among the layout phases for a
reason worth naming: an invalidation is a claim *about* a set of conclusions, so it can
only be written once there are conclusions to be about and once their shape has stopped
moving. A box now carries four state bits that compose — `STYLE` for a change layout does
not read, `LAYOUT` for its own stale geometry, `SUBTREE` for its descendants', `OVERFLOW`
for its measurement alone — plus a reason for a reader. Three questions follow from the
bits and they have three answers, which is why there are three masks rather than one:
*must a pass run* (`LAYOUT_BITS`), *where would it start* (`OWN_BITS`, which is `LAYOUT`
alone), and *is anything owed at all* (`ALL`). A property **name** is classified into what
changing it costs, written out by hand rather than derived from RoboCSS's registry,
because the question is what *layout* reads and only this module can answer it — and the
two answers a naive table gets wrong are `border-style` (geometry: 10.11's used width is
zero unless the style draws) and an unknown name (nothing, rather than everything). Three
marking policies make three different shapes: a box-model change marks the box, its
ancestors' **measurement** and the *transitively* following flow; a displacement marks the
box and its ancestors' measurement and **leaves the following flow clean**, which is
10.12's contract made testable; an overflow-mode change marks measurements alone and no
geometry at all. One decision is built on the model — `robo_layout_context_run_incremental`
skips the pass when nothing needs one — and **no partial layout exists**: the dirty root
(the lowest common ancestor of the boxes whose own geometry is stale) is computed and
reported, and the pass still recomputes from the tree root, because a dirty region a build
*guesses* is a fast wrong answer.

**10.15 (layout tests).** The goal that ends Phase 10 by asking whether the fourteen
layers are right *about each other*, and it adds no door, no enumerator and no capability
field: a twenty-first suite lays out whole trees — a card inside a column inside a shell,
carrying a declared width, a margin, a padding, a border, a relative offset and a clipping
child in the same pass — through the real chain, `RoboHTML → RoboCSS → the bridge → the
tree → the pass`. That is a different question from the one the twenty suites before it
asked, and the difference is not academic: a build that gets each layer right and
*composes* them wrongly passes every one of them. What that looks like is a content origin
computed from the padding but not the border, a final position that is the offset but not
the flow, a parent that grows to hold an overflowing child, or a region that folds in a
clipped child's *contents* — each one line of wrong arithmetic that no single-family
assertion can see. The suite is also where the three findings 10.11, 10.13 and 10.14 left
in prose became the minimal reproductions that failed before their fixes, so a later phase
that reintroduces one fails here rather than in a paragraph nobody runs.

And it found one real defect, which is the argument for a consolidation phase existing: a
version-agnostic *rule* — "one clamp on the sum, saturated rather than refused" — that the
padding, border, margin, position and overflow layers had each stated for their own sums
and that the **pass had not applied to itself**. Three accumulations (the block cursor, a
margin origin, a margin box) summed bounded numbers into an `int32` and wrapped, and a
wrapped extent is *negative*, which the sizing layer refuses — correctly — so a document
that was merely very tall or very deeply indented made the whole pass **fail**. No layer
suite could have caught it: four of them test the geometry's maximum already, one layer at
a time, and the overflow is a property of an accumulation *across boxes*. One helper
(`layout_extent_clamp`) now stands where the rule does.

| Layer | What it delivers | What it does **not** do |
|---|---|---|
| tree | An owned arena of boxes, ids that are dense and tree-scoped, parent/child/sibling links by id, a reverse lookup from element to box | Edit a box, re-parent, reorder, or build incrementally |
| object | One box per element that generates one: its element handle, its **borrowed** computed style, its structural links, its depth, the computed `display` it was created under, and its result | Say what *kind* of box it is beyond "a box" |
| constraints | Available space, minimum and maximum, each either **known or unset** and each with an explicit flag; a validity predicate; and `clamp` | Resolve a percentage, fill an `auto`, size anything intrinsically, or apply `box-sizing` |
| result | Position and size, plus an explicit **state**: `NONE`, `UNRESOLVED`, `COMPUTED` — the per-box authority on whether its numbers are real | Refine a previous pass's numbers rather than replacing them |
| context | One pass: the constraints it ran with, the viewport, the counters, the outcome, and the user-data seam a later goal hangs scratch state on | Lay out a direction whose context it did not record a viewport for |
| builder | DOM tree in, layout tree out, with `display: none` elements generating no box, every other element generating one, and a text node generating a TEXT object unless it is collapsible whitespace | Handle anonymous boxes or fragments |
| bridge | The RoboHTML + RoboCSS adapter: RoboCSS's four element-host callbacks, one computed style per element, and the style source the builder reads through | Own a style context, a stylesheet, or a parser |
| formatting context | The four kinds, the record, and the classification as a **pure function of a display value**, a child composition and a root flag; the per-tree arena of contexts; the two ids on each box | Lay anything out, measure anything, or create an anonymous box |
| block layout | A real position and size for every block-level box in a block-direction context: width **resolved through the sizing layer** against what the margins and padding left, a **border box** on the result, children stacked from the content origin **at that width**, height declared or content-derived, and a record of what was placed and what was not | Resolve a percentage against a containing dimension, a margin or a border; place an inline box, a line, or an unreached box; build an anonymous wrapper |
| inline layout | A real position and size for every participant of an inline formatting context: a cursor that advances, a line that moves down when a participant does not fit, a union for a transparent box, and an atomic box's contents laid out inside it by the block pass | Collapse whitespace, break Unicode lines, fragment a participant across two lines, or build the anonymous wrapper a block-level box in an inline context needs |
| text layout | A code-point range type, the style a font needs resolved from computed CSS, the **measurement seam**, a run record, and a RoboFont-backed measurer that sums advances in font units and scales once | Shape, kern, ligate, reorder for bidi, break lines, or rasterise a glyph |
| line box | The record of one line — identity, ordinal within its context, finalized rectangle in its owner's content-box space, the width it had to fit, the **baseline** it was given (ascent, descent, kind), and the objects it placed — read back through the tree's arena | Align, fragment, or paint a line; derive its height from a resolved `line-height` or from an inline-block's last in-flow line box |
| sizing | A box's dimensions as a question and an answer: the available extent per direction with whether it is known at all, the declared dimension in three kinds, the bounds, the contents' extent, and the resolved size with a **source** and a clamped flag per dimension — plus the one seam a computed style becomes a size through | Resolve a percentage against a containing *dimension*, size intrinsically (`min-content`, `max-content`, `fit-content`, shrink-to-fit), apply `box-sizing`, or add the contribution of borders — a padding's and a border's contribution is their layers', each derived *beside* this one from the answer it gives |
| margin | The gap beside a box, in two vocabularies: the four declarations in four kinds (`absent`, `resolved` — signed — `auto`, `unresolved`) read from a computed style, and the four **used values** a result is placed by; two policies (a block box's, which distributes a horizontal `auto` beside a declared width, and an atomic inline's, which does not); and the read door a caller uses instead of re-deriving the addition | Collapse two adjacent vertical margins, resolve a percentage against the containing *width*, place a vertically `auto` margin (`auto` means something only for an absolutely positioned box, 10.12), or add padding or border to any of it |
| padding | The same declarations, one layer *inside*: three kinds (`absent`, `resolved` — non-negative — `unresolved`) read from a computed style, the four **used values**, and two derived numbers — the width left for a box's contents (floored at zero) and the **padding box** the result carries (summed in 64 bits and saturated) — with the read door a caller uses for where a box's contents begin | Inflate a **transparent inline box** by a declared padding, resolve a percentage against the containing *width*, apply `box-sizing: border-box`, or add a **border** to either derived number — the border layer composes above this one and calls the padding box its input |
| border | The third inset, and the one between the padding and the margin: the twelve longhands read through **three** seams (a width's `px`-or-`thin`/`medium`/`thick` door, a style's ten-row vocabulary and its `generates_border` fact, a colour reused from 9.9), the four sides as a width + style + colour each, the **used edges** record, and the two derived numbers — the **border box** the result carries (summed in 64 bits and saturated) and the content width after borders *and* padding (floored at zero) — with the read door a caller uses for the box a paint pass draws, and the three counters that keep "declared" and "drawn" two facts | Draw a border (Phase 11), round a corner (`border-radius`), apply a border to a **transparent inline box**, resolve a percentage border width, or draw any style but `solid` — the vocabulary is ten rows wide because the *value model* is, not because a painter exists |
| position | A box's **second coordinate**: the mode and four inset longhands in four kinds (`absent`, `resolved` — signed — `auto`, `unresolved`), the **used offset** record (a displacement plus four counters that account for every declaration), one precedence rule (`left` over `right`, `top` over `bottom`, an `auto` saying nothing), the apply in 64 bits and saturated, and the object door that separates a box's **flow** position from its **final** one | Place an `absolute`, `fixed` or `sticky` box (18.5), resolve a percentage inset, move a stacking context or a paint order, or offset a **transparent inline box** or a text run — both decline, and the doors say so rather than answering a zero |
| overflow | What the contents reached **beside** the box: the two axes in three kinds (`absent`, `resolved`, `unresolved` — declared and unusable), one resolve policy (longhands first, then the shorthand for either axis, then `visible`), the four **extents** measured from the padding box with a per-side maximum computed in 64 bits and saturated, the two **derived rectangles** (the region a parent folds in, the clip rectangle a paint pass draws against), and the three counters that keep "declares a boundary" and "has something outside it" two facts | Scroll anything (no scroll offset, scrollport, scrollbar or `overflow: auto`), promote `visible` to `auto` beside a scrolling axis, clip a pixel, answer `text-overflow`, or measure a **text run** or a **transparent inline box** — the object door declines both, and both still contribute their region to their parent |

## The one structural decision

RoboLayout does not use the DOM as its tree.

The two trees do not have the same members. An element with `display: none` is a
DOM node and generates no box; a text node is a DOM node and will generate
something in 10.5 that is not an element box; a future `::before` is a box with no
element behind it. A pass that walked the DOM would re-decide, at every step,
which nodes it was allowed to look at — and every later goal (line breaking,
anonymous blocks, fragments) would add another such decision in another place.

So the tree is built **once**, by the builder, and the pass walks boxes and only
boxes. The DOM stays the DOM; RoboLayout references it.

## The second structural decision: two context ids, not one flag

The tempting model is one fact per box — "this box *is* a block formatting
context" — and it is wrong in a way that costs a later phase a rewrite. A box has
two independent relationships to formatting contexts:

```text
box.panel   display: inline-block
   │
   ├── participates in   the root context        (where its own layout happens)
   └── establishes       a block context         (how its *contents* are laid out)

box.body    display: block, holds a block child
   │
   ├── participates in   the root context
   └── establishes       nothing — it is *transparent*
```

Talking about transparency takes three cases (establishes, transparent, inline),
and a single boolean on the box can hold two. So `robo_layout_object_t` carries
`context` and `context_owned`, and a child resolves its own participation with one
expression: *the parent's owned context if it has one, otherwise the parent's own*.

Transparency is the case worth getting right early. A plain block box whose
children are all block-level establishes nothing, which is what makes a margin
collapse *through* it and a float escape it. A build that made every block box a
barrier would pass every test this phase writes and make 10.9 and 10.13 impossible.

## The one rule the builder applies

```text
an element generates a box unless its computed display says otherwise
```

Everything else is structure, and the two lines that matter are:

- **children are elements, not nodes.** The DOM stores the newline between two
  elements as a text node, so a builder that walked *nodes* would produce a tree
  whose sibling order was half whitespace. Asking the DOM for element children is
  what makes `+` and `:first-child` mean the same thing here as they do in the
  selector matcher.
- **the root box is the document element**, not the document node. A document is
  not an element, generates nothing, and had a box of its own it would be a box
  with no style and no parent.

## The third structural decision: containment is not context

10.2 called a block container holding a block-level child "transparent", and the
word invites one dangerous reading — *not in the chain*. If a child were placed
inside the nearest box that *establishes* a context rather than inside its parent,
a grandchild's origin would be the grandparent's, and a document whose `<body>` is
transparent would look laid out while being wrong about every nested box.

So the two questions are answered from two different places, and 10.3's header
argues it at length:

```text
what am I laid out inside?      my parent box          the parent link, always
which direction stacks my
  contents?                     my formatting context  fc_kind_is_block_container()
```

`layout.block.context_gate` makes the second claim impossible to break over every
box in a tree: a block-level box is never in a non-block-direction context, and a
box in an inline context is never placed by the block pass.

## The fourth structural decision: atomic is not a degree of transparent

10.4's pair, and it is the same *shape* of decision as the one above — two
questions that look like one. An inline-level box comes in two kinds, and the
difference is not "how much does it participate" but *what its contents belong
to*:

```text
box.span   display: inline-block   established a context (flow-root)
   │
   └── opaque to the line: it occupies a run, and what is inside is laid out
       INSIDE it — which is why its width has to be known before it is placed,
       and why it is the one participant the wrapping rule can act on

box.em     display: inline        established nothing
   │
   └── transparent to the line: its children are participants of the SAME
       context, placed at the same cursor; its own rectangle is the UNION of
       what they reached
```

Reading that the wrong way round is the expensive mistake both times: treat a
`span` as opaque and it is placed with no width — nothing here measures text — so
everything inside is dropped; treat an `inline-block` as transparent and its
contents stack in the outer line, producing plausible numbers in the wrong
coordinate space. So atomicity is derived from the two facts the tree already
carries — the box's outer display, and whether it established a context — and there
is no "is this an inline-block" test in this module, because that question is
9.12's.

## The fifth structural decision: the line being built is not a line box

10.6's, and the counterpart one level down. Formatting contexts taught the module
that what a pass *concludes* belongs in an arena on the tree, released wholesale by
the next pass. A line box is a conclusion too, but the temptation is stronger here,
because a line is built incrementally and *feels* like a record being appended to:

```text
the tempting model
   open a line box → mutate it as participants are placed → close it

this build
   the open line is four numbers on the pass's stack;
   a record is written ONCE, at finalization, and appended to the tree's arena
```

The difference is not tidiness. In the tempting model there is a window during which
a line box exists and is wrong — addressable, half-built, with a cursor that will
change — and every later phase that reads a line has to know not to read it then.
Here there is no such window, and no door that mutates a line, because there is no
code that would need one. `robo_layout_line_box_t` has no cursor, no member head to
append to, and no "so far" field, so the requirement is met by the shape of the
types and `state` is a fact rather than a promise.

Members are a **chain in a shared arena** rather than a range, and that too is
forced by the recursion rather than chosen: laying out an atomic inline recurses
into the block pass for its contents, and those contents build lines and members of
their own before the atomic inline is placed on its parent's line. So the
placements of one line interleave with another's, each member names the next member
of *its* line, and reading is O(index) — which the door's own note says rather than
leaving a caller to infer an array.

## The sixth structural decision: one fact, two moments

10.7's, and it is the subtlest of the first six because both halves of it look like an
implementation detail for the same function. A member's **metrics** are recorded when
it is placed, because that is when the measurement is in hand. A member's **final y**
is written when the line is finalized, because the line's ascent is a *maximum* over
members and is not final until the last of them has arrived:

```text
the tempting model
   place a member at its final y
     -> wrong the moment a taller sibling follows it

the tempting fix
   place every member at the line's top, forever
     -> 10.6, and right only when the members share a font

this build
   place -> record the member's metrics and fold the line's maxima
         -> at finalization, one walk of the member chain writes each y
```

The third row is only available because of the fifth decision: a line is written
**once**, so there is exactly one moment at which its members' y can be corrected,
and that moment already exists. The walk that fills each member's back-link is the
same walk that writes its y, because "this member's baseline is the line's" and "this
member is on this line" are one event.

The same phase moved the **union** walk, and for the same reason: a transparent box's
rectangle bounds its contents' positions, so it cannot be computed until those
positions are final. The rule is unchanged — a box is the bounding box of what it
contains — and only *when* it is computed moved, from each box's close to a post-order
walk over the finished context, with the rebasing pass following it because the rebase
subtracts the origin the union just wrote.

## The seventh structural decision: one number, two uses

10.8's, and the only one of the seven before it that changes an answer the module was
already giving. 10.3 read a box's width once and used it twice — as the box's own
size and as the width its children were laid out into — and the two uses are the same
number only while nothing can size a box:

```text
the tempting model
   resolve the box's size -> then lay its children out at the containing block's width
     -> a 300px card whose child is 800 wide

this build
   resolve the WIDTH -> lay the children out AT IT -> resolve the HEIGHT (from them)
```

The split is the phase, and it is why the sizing layer has **two dimension
resolvers** beside the pair: an atomic inline's `auto` height is its own contents'
extent, so its width has to be settled before they are walked. A single door would
have made that caller either lie about contents it does not have yet or resolve
twice and discard one answer.

For an `auto` width the two uses agree — `auto` *is* "fill the containing block" —
which is why the seven structural decisions can share the recursion without any of
the earlier ones moving.

## The eighth structural decision: a margin is outside the box

10.9's, and the first decision in this module about a number that is *not* a size.
A margin changes three things and none of them is the box:

```text
the box's own size        unchanged — a margin is the gap beside it
where the box is placed   its containing block's content origin + left/top
what is left inside       containing width - left - right   (an `auto` width fills this)
what the parent stacks    top + height + bottom             (the MARGIN box)
```

That is why the block pass writes an origin and a size that are not derived from one
another, why `size` in a result still means the content box although the box sits at a
margin origin, and why the four used margins are kept **on the object** rather than in
the result: adding them back is exactly the arithmetic this phase exists to stop a
caller from re-deriving.

The order inside the pass is forced: the margins resolve **before** the width,
because an `auto` width fills whatever the margins left, and what decides whether
there is anything to distribute is the *declared* dimension rather than the
resolution — so `robo_layout_margins_resolve_block` takes the dimension and not a
number.

## The ninth structural decision: a padding is inside the box

10.10's, and the one that retires the identity a box's content box used to have with
its border box. A padding changes two things and neither of them is where the box is:

```text
where the box is placed   unchanged -- a padding is inside it
how big the box is        content box + padding on each axis  (the BORDER box)
where its contents begin  its border-box origin + left/top
what is left inside       containing width - margins - padding  (an `auto` width fills this)
```

That is the exact mirror of the decision above it, and the pair is the reason the
module has two records rather than one: a margin is kept **on the object** so a caller
does not re-derive the space beside the box, and a padding is kept there so a caller
does not re-derive the space inside it. A result can carry neither, and for the same
kind of reason — `size` is the border box by definition, `position` is where the box
begins, and the insets on either side of those two numbers are facts about the box
rather than about its placement.

The order inside the pass is forced in one direction and chosen in the other: the
padding resolves **after** the margins, because what is left beside the box is what
the numbers inside it are measured from; and **before** the width, because CSS's
constraint equation has a padding term on each side, so a box with no declared width
is as wide as its containing block rather than 20px wider. What it does *not* do is
change the shape of the pass: the padding is read and used, it distributes nothing
and resolves against nothing, so it is one call rather than the margin layer's two
doors.

## The tenth structural decision: a border is an inset inside the padding

10.11's, and the one that completes the box model. A border is the third inset, and it
is ordered against the other two and unlike both in what it moves:

```text
where the box is placed   unchanged -- 10.9's job, and the borders do not touch it
how big the box is        padding box + border on each axis  (the BORDER box)
where its contents begin  the border-box origin + border-left/top + padding-left/top
what is left inside       containing width - margins - borders - padding  (an `auto` width fills this)
```

A border is unlike a margin (which moves the box and never the size) and unlike a
padding (which is inside the box but the *innermost* inset) in exactly the way a
three-layer inset forces: it is known **before** the padding, because the border box is
the frame the padding is measured within, and it is the one whose two derived numbers
carry **opposite** edge policies — the border box ceilings (a size that saturates is
still a drawable rectangle) while the content width floors (a containing block narrower
than a box's insets is a document, not a bug). The phase's own finding is that a
*record* is not a document: a negative border record is refused with a diagnostic
rather than floored, because this layer's resolver cannot produce one and a hand-built
one is a caller error rather than an extreme document.

## The eleventh structural decision: a position is two coordinates, not one

10.12's, and the first that is about **where** rather than about **how big**. Every
earlier phase wrote one position per box and every consumer read it; a displacement
would have been invisible — and quietly destructive — in that model, because the
number a sibling's placement was computed from and the number a renderer draws would
have been the same field. So the phase's rule is that the two spaces are written
**once each**:

```text
the result carries the FINAL position   (what paint draws)
the object carries the USED OFFSET      (final − flow is one subtraction)
the flow position exists nowhere else   (and is never written over)
```

A build that added the offset into the flow position would look identical in every
single-box test and move every following sibling; the suite asserts the pair — the box
is drawn where its offset puts it and the next sibling starts where flow says — which
is the case paying for the model rather than documenting it. The counters complete it:
`boxes_shifted` counts a fact about the *box* (its displacement is non-zero) while the
other three count *declarations*, and `offsets_declined` is the number that tells a
`static` box with four insets from a `relative` box with four autos.

## The twelfth structural decision: an overflow is a record beside the box, not a size of it

10.13's, and the first that is about **what a box's contents did to the space around
them** rather than about the box. Every phase before it wrote what a box *is*; this one
writes what got out of it, and the tempting model — let the box grow — is wrong in a way
that is invisible:

```text
the box      the size its declarations and its contents asked for   (unchanged)
the extents  four non-negative numbers measured FROM the padding box  (beside it)
the region   the padding box grown by the extents, clipped per axis   (what a parent folds in)
```

A **size** cannot express an overflow: a box that spills 40px left and a box that spills
40px right have the same width and two different records, and a model with one number per
dimension would have to call them the same. So the record is an extent *per side*, and
the reference edge is the padding box — CSS's clipping edge, and the only rectangle a
box's own declarations cannot move (a margin is outside it, a border is outside it, a
padding is inside it).

The second half of the decision is that a parent folds in its child's **region** rather
than its child's contents. A child that clips contributes only what it kept, while its
own record still says how far its contents reached — two consumers, two answers, one
record. Fold the contents in instead and a clipped subtree appears in an ancestor's
scrollable area, which is the one failure this model exists to prevent and the one case
(`layout.overflow.pass_recursion_clipped`) that can see it.

And the counters complete it in the same spirit as 10.12's: `overflow_clipped` counts a
fact about a box *declaring* a boundary while `overflowing` counts a fact about its
*contents* — neither implies the other, since a document can clip every box and have
nothing stick out of any of them — and `overflow_unresolved` is the number that makes a
`var()`-driven declaration visible instead of silent.

## The thirteenth structural decision: an invalidation is a claim about a conclusion, not about an input

10.14's, and the one that is not about a box at all. The tempting model — record which
declarations changed and re-resolve those — is wrong at the second box, because geometry
is a function of *ancestors'* conclusions as well as a box's own declarations: a leaf whose
author changed nothing moves when its parent's padding changes. So what is tracked is not
which inputs moved but which *conclusions cannot be trusted*, which is a property of boxes:

```text
the record    a state per box, on the object   (the only field that must outlive a pass)
the policy    three shapes of propagation      (box model / displacement / overflow mode)
the decision  one, and it skips the walk       (never a partial layout)
```

Three bits of state and a reason, and every one of them earns its place: `STYLE` keeps a
repaint from pulling geometry and survives the pass that declines to run; `OWN_BITS` is
`LAYOUT` alone, which is what makes the dirty root a *region* instead of the document, since
the marking policy never claims an ancestor's own geometry moved; and `OVERFLOW` exists
because 10.13 made a box's measurement a second rectangle, so a change can leave the box
right and the measurement wrong — which is exactly what a `position: relative` offset does
to every ancestor it does not otherwise touch. The pair a consumer must keep apart is
therefore *not clean* and *need layout*, and it is held apart by two doors with two answers:
`robo_layout_tree_is_clean` and `robo_layout_tree_needs_layout`.

## What "the pass ran" is allowed to mean

There are two passes now, and they answer different questions about the same tree.

`robo_layout_context_run` classifies every box and then — if the context recorded a
viewport — runs layout, in both directions, as one pass. A box holds a
`robo_layout_result_t` whose **state** is the per-box authority:

| State | Means |
|---|---|
| `NONE` | no pass has run over this box |
| `UNRESOLVED` | a pass ran and did not give this box geometry |
| `COMPUTED` | a pass placed this box, in one direction or the other, and its numbers are real |

That a box is `UNRESOLVED` is a *statement*, not a placeholder for one. A caller
that reads a result and finds it unresolved has been told the truth; a phase that
returned `(0, 0)` instead would have been told a lie, and every consumer above —
paint, compositing, hit testing — has a decision to make about a box whose geometry
is not known.

The capability fields are deliberately the wider ones. `geometry` says **the build
computes coordinates**, not that a particular box has one: a box participating in a
block-direction context that is itself inside an unreached box has none, and a
`<span>` holding only text is zero-wide because nothing here measures text.
`layout.conformance.no_viewport` asserts the two claims side by side over one tree —
`geometry == 1` while nothing was placed — which is the conflation the per-box state
exists to make impossible.

## Geometry is borrowed, not defined

RoboLayout has no geometry types of its own. `robo_layout_point_t`,
`robo_layout_size_t`, `robo_layout_rect_t` and `robo_layout_rect_f_t` are aliases
of RoboGraphics' canonical pixel geometry, because a second point/size/rectangle
would be the worst kind of duplication: two types that agree today and differ
subtly in a year, with paint converting between them at the one boundary where a
mistake is a visible artifact.

The aliases exist so a layout signature reads as layout — `out_rect` cannot be
mistaken for a device rectangle — while an implementation passes one object to a
paint phase with no conversion.

## The seam that keeps the builder testable without CSS

The builder reads computed styles through a **callback**, not a
`robo_css_computed_style_t *` argument:

```c
typedef const robo_css_computed_style_t *(*robo_layout_style_fn)(
    void *userdata,
    const robo_html_node_handle_t *element);
```

A suite that wants to prove "a `display: none` element generates no box" or
"children are linked in document order" has no business standing up a style
context, a property table, a registry and a sheet to do it. What a caller does
*not* get to do is skip CSS and still claim the boxes are styled: a source that
answers nothing produces boxes with no style and the `STYLE_MISSING` flag, which
is a fact a later formatting context checks rather than an accident.

The real supplier is `RoboLayoutDom.h`, and it owns one computed style per element
so the tree can borrow them.

## Public API

| Header | Responsibility |
|---|---|
| `RoboLayout.h` | The umbrella: lifecycle, version, capabilities |
| `RoboLayoutTypes.h` | Object identity, the object-kind vocabulary, diagnostic domains |
| `RoboLayoutGeometry.h` | The aliases to RoboGraphics geometry |
| `RoboLayoutObject.h` | One box, and the read side of every field |
| `RoboLayoutTree.h` | The arena, the links, the lookup, `add_object` |
| `RoboLayoutConstraints.h` | Available/minimum/maximum, each known or unset; `clamp` |
| `RoboLayoutSizing.h` | One dimension in three kinds, the sizing record (available, declared, bounded, contents, resolved, with a source per dimension), the two resolvers and the pair, and the one seam a computed style becomes a size through |
| `RoboLayoutResult.h` | Position, size, flags, and the state that says which are real |
| `RoboLayoutContext.h` | One pass, its stats and its outcome |
| `RoboLayoutFormattingContext.h` | The four kinds, the record, and the pure classifier |
| `RoboLayoutBlock.h` | The block pass, its preconditions, and the record it leaves |
| `RoboLayoutInline.h` | The inline direction: the record it leaves, and the doctrine of atomic versus transparent |
| `RoboLayoutText.h` | Ranges in code points, direction, the resolved font inputs, the run record, the measurement seam, and the text counters |
| `RoboLayoutTextFont.h` | The RoboFont-backed measurer — cmap + hmtx, summed in font units and scaled once |
| `RoboLayoutLineBox.h` | One finalized line: the record, the state vocabulary, the baseline it carries, and the read side |
| `RoboLayoutBaseline.h` | The three baseline kinds, the metrics record, its arithmetic, and the two rules that produce it (a run's metrics; the bottom-edge fallback) |
| `RoboLayoutMargin.h` | The four margin declarations in four kinds, the four used values, the two policies, and the read door a caller uses instead of re-adding them |
| `RoboLayoutPadding.h` | The four padding declarations in three kinds, the four used values, the one policy, and the two derived numbers (the content width, floored; the padding box, saturated) |
| `RoboLayoutBorder.h` | The twelve border longhands read through three seams, the four sides, the used **edges** record, and the two derived numbers (the border box, saturated; the content width after borders and padding, floored) |
| `RoboLayoutPosition.h` | The `position` mode and the four inset longhands in four kinds, the used **offset** record with its four counters, the one precedence rule, the apply, and the seam a computed style becomes a displacement through |
| `RoboLayoutOverflow.h` | The two axes in three kinds, the one resolve policy, the **edges** record (two modes, four non-negative extents, three counters), the per-side-maximum accumulate, and the two derived rectangles (the region, the clip rectangle) |
| `RoboLayoutStyle.h` | The style-source seam |
| `RoboLayoutBuilder.h` | DOM document in, layout tree out |
| `RoboLayoutDom.h` | The RoboHTML + RoboCSS bridge that supplies the styles |
| `RoboLayoutDiagnostics.h` | The caller-owned diagnostic record |
| `RoboLayoutVersion.h` | Version pin and the capability constants |

## Lifecycle

```text
robo_layout_init()
       │
       ├── robo_layout_tree_create()
       │        │
       │        └── robo_layout_tree_build_from_document(tree, document,
       │                                                   options, report, diag)
       │                     ▲
       │                     │ options.styles
       │            robo_layout_dom_style_source(bridge, &source, NULL)
       │                     ▲
       │            robo_layout_dom_create(context, document, &bridge, NULL)
       │            robo_layout_dom_refresh(bridge, NULL)
       │
       ├── robo_layout_context_create()
       │        ├── robo_layout_context_set_viewport(size)
       │        └── robo_layout_context_run(context, tree, diag)
       │                 │  builds the context tree, classifies every box, and
       │                 │  lays both directions out
       │                 │
│                 ├── robo_layout_block_layout(tree, viewport, &block, &inline, &text, diag)
│                 │        └── robo_layout_inline_walk_contents(walk, owner, width, &extent)
│                 │                 ├── robo_layout_text_measure_node(node, style, metrics, &run)
│                 │                 │        └── robo_layout_text_font_measure(font, ...)   [the seam]
│                 │                 ├── layout_inline_finalize_line(walk, flow)
│                 │                 │        └── the line record + its member chain
│                 │                 └── robo_layout_block_walk_children(walk, atomic, w, &ext)
│                 │
│                 ├── robo_layout_tree_root_context(tree, &id, diag)
│                 ├── robo_layout_tree_context(tree, id, &fc, diag)
│                 ├── robo_layout_tree_line_count(tree)
│                 ├── robo_layout_tree_line(tree, id, &line, diag)
│                 ├── robo_layout_tree_line_member(tree, id, index, &node, diag)
│                 └── robo_layout_object_line(tree, node, &line_id, diag)
       │
       └── destroy in any order, except that the tree goes before the bridge
robo_layout_shutdown()
```

RoboLayout owns no global state, so `robo_layout_init` is a state transition with
nothing that can fail beyond being called twice. It is here because every module
in the runtime has the same lifecycle, and a caller should not have to know which
ones are special.

## One lifetime rule a compiler cannot check

**A tree built from a bridge's style source must be destroyed before the bridge.**
The tree borrows the bridge's computed styles, so destroying the bridge under it
leaves the tree pointing at freed memory. It is written down in `RoboLayoutDom.h`,
asserted by `layout.dom.lifecycle`, and it is the only ordering constraint in the
module.

## Documentation

| Document | Covers |
|---|---|
| `docs/Architecture.md` | Why the tree is separate, the builder's rule, the pass contract, the seams, and the ownership model |
| `docs/FormattingContexts.md` | The two-context model, the classification rules, ownership and depth, invalidation, and what 10.3–10.13 will consume |
| `docs/BlockLayout.md` | Containment versus context, the width and height rules, what the pass declines to place, determinism and invalidation, and what 10.4–10.13 will consume |
| `docs/InlineLayout.md` | Atomic versus transparent, where a participant's width comes from, the wrapping rule and why a preliminary line is not a line box, rebasing, and why there is no second entry point |
| `docs/TextLayout.md` | Text as an ordinary participant, ranges in code points, direction as metadata, the measurement seam and its three answers, the four facts a font needs, and the builder's second rule |
| `docs/LineBoxes.md` | Construction state versus the record, members as placements, the space a line is in, the lifecycle the pass runs, and what the block pass gets from the lines |
| `docs/Baselines.md` | Where the member's metrics are written versus where the line's floor is final, half-leading from the run's gap, the bottom-edge fallback, the two coordinate spaces, and why the union walk had to move |
| `docs/Sizing.md` | Why a resolved width is what the children are laid out into, the three dimension kinds, per-direction availability, where the bounds come from (and the both-dimensions rule), the two readers over one classification, and the three earlier assertions the phase updated |
| `docs/Margins.md` | Why a margin moves a box and never resizes it, the four kinds (and why an absent side is not a declared zero), the two policies, the used values record and its one clamped number, the three earlier assertions the phase updated, and the missing collapsing |
| `docs/Padding.md` | Why a padding grows a box and never moves it, the three kinds (and why there is no `auto`), the one policy and why one door is enough where margins needed two, the two derived numbers and their two different clamps, the `content-box` model, the four earlier assertions the phase updated, and the missing transparent-inline padding |
| `docs/Borders.md` | Why a border is an in-box inset and not a spacing, the three seams and why the three properties compute three ways, the `padding_box` rename and why the old name was the defect, the two derived numbers with opposite clamps, the `content-box` model, and the assertion that deliberately did not move |
| `docs/Positioning.md` | Why a position is two coordinates, the four inset kinds (and why an inset is signed where a padding is not), the one precedence rule and its four counters, the apply's saturation, the two inline halves, and the object door that separates flow from final |
| `docs/Overflow.md` | Why an overflow is a record rather than a size, the RoboSS vocabulary (and why the shorthand is read rather than expanded), the padding box as the reference edge, the one resolve policy and its three kinds, the per-side-maximum accumulate and its saturation, the region and the clip rectangle, the recursion that folds a child's region, where it is measured in each pass, and everything deliberately absent |
| `docs/Invalidation.md` | Why an invalidation is a claim about a conclusion rather than about an input, the four state bits and the three masks, the classifier and its three decisions, the three marking policies and the shape each propagates, why the dirty root is a common ancestor rather than the shallowest box, where the state lives and why on the box, what a pass consumes and leaves, the four-term incremental decision, and everything deliberately absent |
| `docs/Tests.md` | The twenty-one suites, what each asks, and what each phase found |

## Building and testing

```sh
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Test
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Release -Test
./Out/RoboTest.host.exe --filter layout.       # 344 cases (+2 demo cases)
```

`Tools/Build.ps1 -Verify` additionally scans `Include/RoboLayout`,
`Source/RoboLayout` and `layout/` for a Chromium include and for a dependency on
any higher layer — paint, render, compositing, JS, the shell, or a window/display
module, which sits below layout in the graph but has no business in a layout pass.

## Boundaries

- No Chromium headers, sources, libraries or build outputs. Chromium's
  `LayoutObject` / `LayoutBox` / constraint models are temporary technical
  reference only.
- No CSS parsing, no property names, no stylesheet, no normalisation. Display is
  read through 9.12's own accessors, including the block-level predicate — there is
  deliberately no second one in this module.
- No **percentage resolved against a containing dimension**, no `min-content` /
  `max-content` / `fit-content`, no shrink-to-fit and no `box-sizing`. A declared
  `width` or `height` is honoured where it is a computed `px` length; a percentage and
  a relative unit are recorded as `UNRESOLVED` and take the `auto` path rather than a
  guess. The bounds path *is* live — it is applied through 10.1's clamp and tested
  end to end — but no property RoboCSS registers feeds it yet, so a document can
  declare `min-width` and change no number here.
- No **margin collapsing**. Margins are added and placed: a box sits at its margin
  origin, a horizontal `auto` takes its share of the leftover beside a declared
  width, and a parent stacks its children by their margin boxes — but two adjacent
  vertical margins **sum** where CSS takes the larger, and a parent's top margin does
  not merge with its first child's. `layout.margins.block_no_collapse` asserts the
  sum, so the difference is a value a later goal changes rather than an omission.
- No `box-sizing`, and no border **painting**. Borders reach geometry since 10.11: a
  box's `size` is its border box, its contents begin `border-left`/`border-top` and then
  `padding-left`/`padding-top` inside it, and an `auto` width fills what the margins, the
  borders and the padding left — but a border is a *layout* fact here, not a drawn one,
  so no style but `solid` is rendered and `border-radius` is not a value this build has.
  The build is `box-sizing: content-box`, CSS's initial value, so a declared `width` is
  the content box; `border-box` is a branch a later goal adds at the one place the inset
  layers hand the sizing layer a number.
- No border or `padding` on a **transparent inline box**. A `<span>` with contents has a
  rectangle that is the union of what it contains, and this build does not inflate that
  union by a declared padding or border. `layout.paddings.inline_transparent_not_applied`
  and `layout.borders.inline_transparent_not_applied` assert the omission as a value, so
  a later goal changes an assertion rather than adding one.
- No **absolute**, `fixed` or `sticky` placement. Their values and facts exist (9.13)
  and a non-relative box's insets are read and **declined** — counted, not dropped —
  but placing one against a containing block or a scrollport is 18.5's. No percentage
  inset resolves, and no `z-index` or stacking context moves.
- No paint, no rendering, no windowing, no display. The viewport is an argument.
- No **shaping**, no kerning, no ligature, no bidi reordering, no Unicode line
  breaking and no whitespace collapsing. A run's width is the sum of its glyphs'
  advances, which is what an unshaped run is. A participant too wide for its line
  **overflows** rather than fragmenting.
- No `line-height` **as a resolved used value**, and no `vertical-align` in any of
  its forms. 10.7 gave every line a floor and placed its members on it: the ascent is
  the members' maximum ascent, the descent their maximum descent, the height their
  sum, and a member's y is `origin.y + (line.ascent - member.ascent)`. What that is
  *not* is a `line-height` computation — a line's extent is the extent its metrics
  span — and the fallback for everything without font metrics is the documented
  **bottom edge**, not an inline-block's last in-flow line box, which is a later
  phase's rule and is named where it would otherwise be assumed.
- No **text measurement of its own**. The seam is a callback and the RoboFont-backed
  measurer is one implementation beside it; a pass with no seam measures every run
  as zero and counts it (`robo_layout_text_stats_t.unmeasured`), which is a
  legitimate way to run the module rather than a degraded one.
- No **anonymous box**, and no automatic inner inline context for an atomic inline.
  A block-level box inside an inline one is counted as `block_level_declined`
  rather than placed inside something with no geometry — that box is what the
  anonymous wrapper will be for.
- No float, fragmentation or **scrolling** behaviour. `position` is resolved by 10.12
  and overflow by 10.13, but an overflow here is *recorded* rather than scrolled: there
  is no scroll offset, no scrollport and no scrollbar, `overflow: auto` and
  `overflow: scroll` are not modes this build has, and CSS's axis promotion
  (`overflow-x: visible` beside a scrolling axis) deliberately does not apply. The
  result's two overflow flags are set by 10.13 from a *measured* extent, so a declared
  height smaller than the content sets no bit unless something is actually outside the box.
- No `flex`, `grid`, `table`, `ruby` or `multicol` context, and no enumerator for
  any of them. `ROBO_LAYOUT_OBJECT_KIND_COUNT` is three — `NONE`, `BOX` and `TEXT`,
  and 10.5 is the phase that widened it, with the no-property-to-derive-from
  justification 10.1 wrote down — and `ROBO_LAYOUT_FC_KIND_COUNT` is four.
- No **line-box** alignment, `text-align` or painting. A line is a rectangle, a
  baseline and a membership list; `ROBO_LAYOUT_BASELINE_KIND_COUNT` is three —
  `NONE`, `TEXT` and `BOX` — and `NONE` is the one kind the validity predicate
  refuses, which is what keeps "not placed" from reading as "placed with no extent".
