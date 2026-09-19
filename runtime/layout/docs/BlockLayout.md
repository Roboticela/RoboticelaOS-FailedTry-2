# RoboLayout — block layout (Phase 10.3)

This is the design record for the third phase of RoboLayout: the first one that
computes a coordinate. It states what the block pass is allowed to decide, the one
distinction the whole phase turns on, what it deliberately refuses to do and which
later goal owns each refusal, and what 10.4 onwards is entitled to assume.

`docs/Architecture.md` remains the record for the module's architecture.
`docs/FormattingContexts.md` remains the record for the context model this phase
consumes. This document is the record for the block direction.

---

## 1. What the phase is

10.1 built the box tree and a pass that walked it; 10.2 answered *which boxes'
layout is being coordinated, and in which direction*. 10.3 answers the next
question — for one of those directions, and for the boxes that direction owns:

```text
                    where does each box go, and how big is it?
```

One pass now does four things where it used to do three:

```text
robo_layout_block_layout(tree, viewport, out_stats, diag)
        │
        ├── classify the gates   ─► which boxes this pass owns (10.2's contexts)
        ├── place                ─► x, y in the containing block's space
        ├── size                 ─► width from the containing block, height from
        │                           a declared length or from the stacked children
        └── count                ─► robo_layout_block_stats_t
                    │
                    └── robo_layout_context_run() calls it, for a context that
                        recorded a viewport
```

```text
                    what a block box does

    1.  it fills its containing block's content width;
    2.  its block-level children are stacked from its content origin, each below
        the last;
    3.  its height is the declared one where this build can resolve it, and the
        stacked extent of its children otherwise;
    4.  its result is written in its containing block's coordinate space.
```

`robo_layout_context_run` gains the call and nothing else: a context that recorded
a viewport runs the block pass at the end of its own walk, and a context that did
not leaves every box `UNRESOLVED` with a zeroed block record. The capability that
flips is `geometry` — `0` since 10.1, `1` now — alongside the new `block_layout`,
and `ROBO_LAYOUT_GEOMETRY_IMPLEMENTED` and `ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED`
flip with them.

---

## 2. The one distinction the phase turns on: containment is not context

10.2 called a block container holding a block-level child **transparent**, and the
word invites exactly one dangerous reading: *not in the chain*. If a child were
placed inside the nearest box that *establishes* a context rather than inside its
parent, then a grandchild's origin would be the grandparent's origin, and a
document whose body is transparent would look laid out while being wrong about
every nested box.

So the phase keeps the two questions apart, and answers them from two different
places:

| Question | Answer | Door |
|---|---|---|
| What am I laid out **inside**? | my **parent box** | the `parent` link, always |
| Which direction stacks **my contents**? | my **formatting context** | `robo_layout_fc_kind_is_block_container(box.context)` |

```text
box.body       display: block          establishes nothing ("transparent")
   │
   └── box.section   display: block
          │
          └── box.p     display: block

   section.parent is body, and section.x/y is (0,0) *of body's content box*.
   The fact that section participates in the ROOT context is why the *root*
   stacks it; it is not where it is placed.
```

`layout.block.nested` asserts three levels of this, and
`layout.block.context_gate` asserts the invariant that goes with it over *every*
box in a tree: a block-level box is never in a non-block-direction context, and a
box in an inline context is never placed by this pass. The invariant is 10.2's
rules made impossible to break, checked rather than assumed.

---

## 3. Width: one rule, and it is `auto`

> **Superseded by 10.8, and the reason this section is kept rather than rewritten**
> The rule below is what 10.3 shipped, and it is still the `auto` rule. What
> changed is the *other* declared widths: a `px` length is now resolved by the
> sizing layer and is the width the box's children are laid out **into**
> (`Sizing.md` §1). Read the rest of this section as 10.3's answer and the record
> of why it was worth shipping a build that said "not yet" out loud.

```text
a block box's width is its containing block's content width
```

That is CSS's `width: auto` for a block-level box in normal flow, and for this
phase it is also the answer for every *other* declared width — because resolving
`width: 200px` properly needs the border box, `box-sizing`, `min-width`,
`max-width`, and percentages against a containing block that may itself be
percentage-sized. Phase **10.8** owned all of it, and delivered the half that
needs no border box: `auto` and a `px` length. A percentage, a relative unit,
`min-width`/`max-width` as registered properties and `box-sizing` are still
absent — the sizing layer *reads* the first two and records them as unresolved
rather than resolving them.

So a declared `width` was readable in the borrowed style and **not applied**, and
that was a decision with a visible consequence: a document saying `width: 200px`
laid out 100% wide here. The header said so rather than leaving it to be
discovered, and the pipeline case in §7 exercised it — and that case is one of the
two assertions 10.8 updated *because* it failed, which is the outcome this section
was written to produce.

The alternative — applying a `px` width and ignoring the rest — was rejected on
this module's own precedent: it would make `width: 200px` mean something that
`width: 12em` does not, with no rule that explains the difference, and the day
10.8 lands every test that depended on the split would need rewriting. One rule
that is *stated* is worth more than two that are halfway applied.

### 3.1 The viewport is the root's containing block, and it is an argument

The root box's containing block is the initial containing block — the viewport.
RoboLayout owns no window and no display, and `Tools/Build.ps1 -Verify` enforces
it, so the viewport arrives as a `robo_layout_size_t` argument. That is the same
decision 10.1 made and for the same reason: a pass that asked a window how big it
was could not be tested without one.

---

## 4. Height: declared where resolvable, contents otherwise

The block direction is the first direction whose size is *contents-dependent*, and
this phase implements the smallest honest version of that:

| What the style says | What this build does | Why |
|---|---|---|
| `auto` (the initial value) | the stacked extent of the children | this is the definition, for a block container |
| a `px` length | that height | one property, one kind check, one unit check |
| a percentage | the stacked extent | a percentage needs a containing block *height*, which no goal resolves yet — 10.8 reads it, records it unresolved and fills it (`Sizing.md` §4) |
| `em`, `rem`, `vw`, … | the stacked extent | a relative unit needs a font or a viewport basis, which no goal resolves yet |
| `min-height`, `max-height` | ignored | the sizing layer *has* bounds and clamps through them, but RoboCSS registers neither property, so a declared one is still a rule 9.11 will bring (`Sizing.md` §6) |

The row that matters is the third and fourth: **an unresolvable declared height is
treated as `auto`, not as a guess and not as a refusal of the whole document.** "I
cannot resolve this yet" and "the content decides" produce the same geometry here,
and only one of them is a claim this phase can defend — so it takes the one it can.
`layout.block.unresolvable_height` walks `50%`, `2em` and `10vw` and asserts the
content extent each time.

Reading the declared height is deliberately *not* a sizing algorithm: there is no
normalisation pass, no unit table, no resolution machinery. It is one call to
check the computed kind and one to check the unit, and the phase that needs more
will build it.

### 4.1 An empty container is zero tall

Follows from the rule rather than being a special case: a container with no
block-level child stacks nothing, so its extent is zero and its height is `0`
unless one was declared. It is the single most surprising fact in the phase —
`<div></div>` is invisible — and it is therefore counted
(`empty_containers`) rather than left to be noticed.

Text does not change it. A `<p>text</p>` contains a text node, which is not a box
in this build, so the paragraph is a container with no block-level child: it
establishes an **inline** context (10.2's rule) and gets height `0` from this pass.
That is 10.4's and 10.6's to fill in, and `layout.block.context_gate` pins the
box's *placement* while leaving its height to the pass that will own it.

---

## 5. What the pass declines to place, and where each refusal belongs

| Not placed | Counted as | Owner |
|---|---|---|
| a box whose display is inline-level | `inline_level_skipped` | 10.4 |
| a box participating in an inline context | `inline_level_skipped` | 10.4 |
| a box whose display is `none` | *nothing* — it has no box at all | 10.1's builder |
| a block-level box inside an inline box | `unreached` | 10.4, via the anonymous box |
| anything under an unreached box | `unreached` | 10.4 |

The last two rows are the interesting ones. A block-level box inside an inline one
is block-level **and** in the root's context, so both of this pass's gates say yes —
and it still cannot be placed, because the box that contains it has no geometry to
place it inside. It is therefore counted as *unreached* rather than silently
dropped, and the recursion descends only through boxes this pass placed. This is
precisely the case the **anonymous block box** exists for: 10.4 will wrap the
inline run, the wrapper will establish the block context, and the block-level child
will be reached inside it.

That is also why this phase builds no anonymous box. An anonymous box exists to
give an inline run a block-level home, and a home whose contents are laid out along
a line is 10.4's. Building the wrapper here without the line boxes to put in it
would be a box with a correct parent and nothing correct inside.

`ROBO_LAYOUT_OBJECT_KIND_COUNT` therefore stays **2** — `NONE` and `BOX` — and the
conformance suite asserts it, because an anonymous box is the one object kind that
would have no element and no `display` to derive a kind from.

---

## 6. Not applied, and readable anyway

**Superseded in part by 10.9, again by 10.10 and again by 10.11, and kept as it
stood.** Margins, padding and borders were **10.9, 10.10 and 10.11**, and until then a box's content
box **is** its border box and its content origin **is** its top-left corner, so no
arithmetic was missing and no number was faked: there was simply nothing to add yet.

None of that is true of margins, padding or borders any more. A box's position is now
its **margin origin** — the content origin of §3 plus its own `margin-top` and
`margin-left` — the number the pass reports to a parent as the extent to stack below is
the **margin box**, `top + height + bottom`, and since 10.11 the `size` it writes is the
**border** box: the content box the sizing layer resolved, plus the padding on each
axis, plus the border. Three things in this document therefore say something a build no
longer does, and they are left standing rather than silently edited because how they
changed is the record:

| §6 as written | what 10.9, 10.10 and 10.11 made of it |
|---|---|
| "a box's content origin **is** its top-left corner" | it is its top-left corner **plus its top and left margins** (`Margins.md` §1) **plus its top and left border** (`Borders.md` §1) **plus its top and left padding** (`Padding.md` §1) |
| "`margin: 10px 20px` … change[s] no number" | it changes two: the origin moves to (20, 10), and the extent the parent stacks by becomes the margin box (`Margins.md` §7) |
| "a box's content box **is** its border box" | the border box is the content box **plus the padding and the border**, which is what `size` carries; an `auto` width fills what the margins, the border *and* the padding left (`Padding.md` §5, `Borders.md` §1) |

10.11 closes the box model this document was written one inset short of, and it
changed one number the document states as a rule not at all: the width the children are
laid out **into** is still the content box, which under this build's
`box-sizing: content-box` (CSS's initial value) is the declared one — so §3's "the
resolved width is what the children get" still reads exactly as written, and the box
around it is 2 × (border + padding) wider. The `content-box` model is what makes those
two sentences compatible rather than contradictory, and it is the assertion
`layout.conformance.pipeline` keeps paying for: the card is 218 wide and its paragraph
is still 200.

The values arrive computed and borrowed through 10.1's style seam, so a test can
read them beside the geometry — which is how the pipeline case in §7 proved that
`margin: 10px 20px` and `border: 1px solid` changed no number in *this* phase while
still being present in the style, and how the same case now asserts that the margin
and the padding both reach geometry where the border still does not.

The two overflow flags on the result stay **0** even when a declared height is
smaller than the content it holds, and that is deliberate: *that a box overflows*
is a fact 10.13 owns, and setting the flag here would be answering 10.13's
question with 10.3's arithmetic. A box is still free to have a child outside its
own rectangle; the result record simply does not comment on it.

---

## 7. Determinism, and what a second pass means

The pass is a pure function of three inputs: the tree, the viewport, and the
borrowed styles.

- boxes are visited in **creation order**, which the builder makes document order;
- children are visited in **sibling order**, which is the order the builder linked
  them;
- nothing is cached between calls and nothing from a previous pass is read.

`layout.block.determinism` asserts the two halves of that: two passes over one tree
produce equal results box for box, and two trees built from one document produce
equal geometry.

The pass also **replaces** rather than refines: every box it reaches is written from
scratch. So calling it twice is not a second pass stacked on the first, and
`robo_layout_context_run` clearing every result before computing is a statement of
the contract rather than a defensive habit.

### 7.1 Invalidation withdraws a conclusion (the bug the phase found)

Block layout is a **function of the classification**: the placement gate *is*
`object->context`, and a height is the extent of the children that gate let
through. So the moment a classification stops describing the tree, every
coordinate derived from it is a statement about a tree that no longer exists.

10.2 released a classification on invalidation and left the results alone. That was
correct while nothing derived geometry from the classification; from 10.3 on it is
not, because the results would still be marked `COMPUTED` — which is the one thing
that state is not allowed to mean. Fixed in `robo_layout_tree_contexts_reset`, and
**in the shared path on purpose**: all three callers (an explicit invalidation, a
clear, and an add) are saying the same thing, and a rule written three times is a
rule two of them will eventually disagree about.

One detail is load-bearing:

```c
if (robo_layout_result_state(result) != ROBO_LAYOUT_RESULT_NONE) {
    robo_layout_result_set_unresolved(result);
}
```

The condition is what keeps `NONE` reachable. A blunt `set_unresolved` over every
slot would make every add manufacture `UNRESOLVED` for every box — including boxes
the add did not touch — and a tree built by eight adds would be indistinguishable
from one a pass had swept without placing anything. Only a result that claimed
something is demoted.

`layout.block.invalidation` is the case that asks for it: place, add a box, and
read the old geometry.

---

## 8. The record the pass leaves

`robo_layout_block_stats_t` is counters by value and owned by nobody, because the
interesting fact about a document is the **mix**: a tree of nested transparent
containers and a tree of filled leaves can place the same number of boxes with
completely different shapes, and a suite that could not tell them apart would not
be testing the algorithm.

| Field | Meaning |
|---|---|
| `containers` | boxes whose contents were stacked in the block direction |
| `blocks_placed` | block-level boxes that received geometry |
| `inline_level_skipped` | boxes 10.4 places instead |
| `unreached` | boxes nothing containing was placed for |
| `heights_explicit` | heights read from a resolvable declared `height` |
| `heights_from_content` | heights obtained by stacking children |
| `empty_containers` | containers with no block-level child, so extent zero |
| `margins_applied`, `margins_auto_used`, `margins_unresolved` | 10.9's three, summed over sides |
| `paddings_applied`, `paddings_unresolved` | 10.10's two, summed over sides — a `padding: 5px` is four declarations one layer down, so `.card` alone reports four |
| `depth_max` | deepest box placed |

`robo_layout_context_run` derives `stats.boxes_computed` from `blocks_placed`
rather than counting it again, so the two numbers cannot disagree by construction —
one source of truth for one fact, which is the rule this module applies everywhere.

---

## 9. What 10.4–10.13 will consume

| Later goal | What it asks this phase |
|---|---|
| 10.4 inline layout | the `unreached` and `inline_level_skipped` counts — *these boxes have no geometry yet* |
| 10.4 anonymous boxes | the recursion rule: the pass descends only through boxes it placed |
| 10.6 line boxes | a block container's content origin, and the box whose extent a line box will add to |
| 10.8 sizing | the single width rule (§3) it replaces, and the `heights_from_content` `auto` path |
| 10.8 sizing (outcome) | §3's single width is now **one resolution and two uses**: the pass resolves the width first and recursion lays children out into that number, so a declared `width` reaches a box's *children* and not only the box (`Sizing.md` §1); §4's four rows become the sizing layer's three kinds, and a percentage or relative unit is read and recorded **unresolved** rather than silently treated as `auto` (`Sizing.md` §4) |
| 10.9 margins | the content origin (§6) that becomes the margin edge |
| 10.9 margins (outcome) | §6's content origin is now a **margin origin**: the pass resolves the four margins *before* the width (an `auto` width fills what they left), writes the result's position from them, and advances the stacking cursor by the child's **margin box** rather than its content height (`Margins.md` §1) — so the card's origin moves to (20, 10) and `<body>`'s extent becomes 120, and the size stays 200×100 in both cases |
| 10.10 padding (outcome) | the width is resolved against what the margins **and** the padding left, and the result's `size` is the **padding** box the padding layer derives: the sizing layer's content box plus the padding on each axis (`Padding.md` §1). The card's own size moves 200×100 → **210×110** and `<body>`'s extent 120 → **130**, while its **children** still get 200 — the `content-box` model, and the assertion that keeps the declared-width rule in §3 true |
| 10.11 border (outcome) | one more subtraction and one more addition, and the last of each: the width is now resolved against the containing block less the **margins, the border and the padding**, and the result's `size` is the **border** box — the padding box plus the border — so the card declares `border: 4px solid black` and is **218×118** while `<body>`'s extent is **132** (`Borders.md` §1). The content origin this document defines becomes a **two-term** number, the border's `top`/`left` and then the padding's, and the `content-box` model is unmoved: the paragraph is still **200**. What the phase adds to this pass is one call in each direction, in the order the box is built |
| 10.12 positioning (**done**) | a result's `position` is now the **final** position — flow displaced — while every number this pass stacks by stays the flow geometry; the used offset is on the object and §2's coordinate space is unchanged (`Positioning.md` §5) |
| 10.13 overflow | the two result flags that are still 0, and the box that is the scroll container |
| 11 paint | a rectangle per box, in the coordinate space §2 defines |

None of these needs a change to the block model. What 10.4 added is the line box
and the anonymous wrapper; what 10.8 added is a resolver in front of §3 and §4 —
and, as its outcome row says, split §3's one number into the two uses it was always
doing.

---

## 10. Boundaries

- **No inline layout, no line box, no text.** §5, and `inline_level_skipped` and
  `unreached` are how the absence is *visible* rather than silent.
- **No anonymous box.** §5 — 10.4's.
- **No `box-sizing`, and no collapsing.** §6. Margins **are** arithmetic since 10.9,
padding since 10.10 and borders since 10.11 (each with its own row in §9); the last of
the three completes the box this document describes, as the outermost subtraction that
sets what the width is resolved against and the last addition before a result's `size`.
What is still absent is collapsing — adjacent vertical margins sum — which has its own
boundary item in `Margins.md` §6.
- **No declared width.** §3 — 10.8's, and it landed: a `px` length and `auto` are
  both resolved by the sizing layer, and everything else is read, recorded as
  unresolved and filled rather than guessed (`Sizing.md`).
- **No percentage, relative-unit, `min-height` or `max-height` resolution.** §4.
- **No absolute or fixed positioning, no floats, no overflow, no fragmentation.**
  §6 and 10.12–10.13.
- **No paint, no rendering, no windowing, no display.** The viewport is an argument
  (§3.1) and `-Verify` enforces the boundary.
- **No CSS parsing.** Display is read through 9.12's own accessors —
  `robo_css_display_is_block_level` and its siblings — and there is deliberately no
  second block-level predicate in this module: a copy would be the *worse* copy,
  because the day `flex` adds a row with an outer of `block`, 9.12's table is where
  that fact lands.
- **No Chromium.** Blink's `LayoutBlockFlow` and its margin-collapsing and
  intrinsic-size machinery are temporary technical reference only; the rules here
  are Roboticela's, and `Tools/Build.ps1 -Verify` scans this directory for a
  Chromium include and for a dependency on any higher layer.
