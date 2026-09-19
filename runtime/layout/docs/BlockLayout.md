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

```text
a block box's width is its containing block's content width
```

That is CSS's `width: auto` for a block-level box in normal flow, and for this
phase it is also the answer for every *other* declared width — because resolving
`width: 200px` properly needs the border box, `box-sizing`, `min-width`,
`max-width`, and percentages against a containing block that may itself be
percentage-sized. Phase **10.8** owns all of it.

So a declared `width` is readable in the borrowed style and **not applied**, and
that is a decision with a visible consequence: a document saying `width: 200px`
lays out 100% wide here. The header says so in the header itself rather than
leaving it to be discovered, and the pipeline case in §7 exercises it.

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
| a percentage | the stacked extent | a percentage needs a containing block *height*, which is 10.8 |
| `em`, `rem`, `vw`, … | the stacked extent | a relative unit needs a font or a viewport basis, which is 10.8 |
| `min-height`, `max-height` | ignored | 10.8 |

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

Margins, padding and borders are **10.9, 10.10 and 10.11**. Until then a box's
content box **is** its border box and its content origin **is** its top-left corner,
so no arithmetic is missing and no number is faked: there is simply nothing to add
yet.

The values arrive computed and borrowed through 10.1's style seam, so a test can
read them beside the geometry — which is how the pipeline case in §7 proves that
`margin: 10px 20px` and `border: 1px solid` change no number in this phase while
still being present in the style.

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
| 10.9 margins | the content origin (§6) that becomes the margin edge |
| 10.12 positioning | `position` on the result, in the containing block's coordinate space |
| 10.13 overflow | the two result flags that are still 0, and the box that is the scroll container |
| 11 paint | a rectangle per box, in the coordinate space §2 defines |

None of these needs a change to the block model. What 10.4 adds is the line box
and the anonymous wrapper; what 10.8 adds is a resolver in front of §3 and §4.

---

## 10. Boundaries

- **No inline layout, no line box, no text.** §5, and `inline_level_skipped` and
  `unreached` are how the absence is *visible* rather than silent.
- **No anonymous box.** §5 — 10.4's.
- **No margin, padding or border arithmetic, and no `box-sizing`.** §6, and the
  content origin is stated as such.
- **No declared width.** §3 — 10.8's, and the one rule is stated in the header.
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
