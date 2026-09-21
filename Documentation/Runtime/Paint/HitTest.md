# RoboPaint — hit testing (Phase 11.10)

The phase that asks the painter's question **backwards**. Painting turns a laid-out tree into an
ordered sequence of drawings; a hit test takes one point on the drawing plane and walks back down
to the node that would be drawn there.

Nothing about the document changes to make this possible. The geometry is RoboLayout's, the
boundary in force is 11.8's clip record, the strength in force is 11.9's opacity record, and the
order is the traversal's. What this phase adds is a **reader** of those conclusions — a second
walk with the same shape as the first, one installed header, one suite and this document.

```text
window / input point
        │
        ▼
PaintHitTest()          ← this phase
        │
        ├── RoboLayout geometry   (border box, bound, overflow)
        ├── 11.8 clip record      (the boundary in force)
        └── 11.9 opacity record   (the strength in force)
        │
        ▼
PaintHitTestResult   one node, or MISS, or a refusal
```

## 1. Why this lives in RoboPaint

The natural reading of the phase is a new top-level subsystem, and it is the wrong one, for two
reasons that compound.

The first is the **types**. A hit test is a second consumer of `PaintClip` and
`PaintOpacity`, and it needs `PaintBoxes`, `PaintBoxesOf()` and
`PaintClipContains()` as well. A module beside RoboPaint would either duplicate those
records or depend on paint for four types and reach past it for the geometry — two boundaries
where there is one, and a second spelling of "how wide is this box" free to drift from the first.

The second is the **walk**, and it is the real argument. The paint traversal accumulates a page
origin, narrows a boundary and multiplies a strength while descending the tree; the hit-test walk
accumulates the same three things, in the same order, from the same declarations. That is why the
two agree about where a box is, and it is a fact this module can *assert* only because both walks
live in one place. Two modules maintaining one rule independently is how a document ends up with
a box that is drawn in one place and hit in another.

So `RoboPaintDomainHitTest` is the eleventh diagnostic domain (`RoboPaintDomainCount` is
12), `hit_testing` is the sixth capability field, and the module adds no dependency edge:
RoboPaint already reads a layout tree and already owns both records (`DEPENDENCIES.md`).

## 2. The coordinate model

Three spaces, and only one of them is new.

| space | what it is | who writes it |
|---|---|---|
| **layout** | a box's position relative to its containing block's **content origin** | RoboLayout |
| **paint** | one page space: the accumulated origins of every ancestor added in | the traversal |
| **window** | what a window or input layer reports a pointer in | RoboWindow |

`PaintHitTest()` takes its point in **paint** coordinates and the result carries the point
back in both spaces:

```c
result.point   /* the query's point, paint coordinates */
result.origin  /* the hit node's containing-block content origin, paint coordinates */
result.local   /* result.point - result.origin, i.e. layout coordinates */
```

**Paint coordinates are window coordinates in this build**, and that is a statement about the
document rather than a shortcut. The traversal begins the root at `(0, 0)`, there is no scroll
offset to subtract, no transform to invert, no zoom and no device-pixel-ratio scale — so the plane
a drawing lands on and the plane an input point arrives in are the same plane. A caller with a
window-relative point hands it over unchanged *today*.

The two names are kept apart anyway because the phase that adds scrolling is the phase that makes
them differ, and a signature that said "window" here would be a lie the day that lands. The same
argument `RoboPaintOperation.h` makes for carrying both coordinate spaces on every operation.

`local` is not a convenience either. It is the space RoboLayout wrote the node's geometry in, so a
caller comparing the point against a layout rectangle uses it, and the round trip is checkable:
`local.x == point.x - origin.x` is asserted by the suite rather than trusted.

## 3. The four rules

Each is a decision rather than an implementation detail, and each mirrors the painter exactly.

### 3.1 Last drawn wins

The painter's order for a box is: the box's own decorations, then its children left to right, each
child's subtree in full (11.1, `Operations.md` §4). So the node that *covers* a point is the
**last** one drawn there, and the walk visits a box's children in **reverse** order and tests the
box itself **after** all of them.

A document-order walk answers with the box underneath. That is the classic hit-test bug, which is
why the rule is stated first, and it is why `paint.hit-test.overlap_is_by_drawing` exists: three
overlapping siblings where the **middle** one is on top is the case that catches a walk which
reversed the wrong thing.

### 3.2 A box's own region is its border box

The region a box may be hit in is the union of the three layers it could paint into, and for every
kind of box this build has, that union is the **border box**: a background fills it (11.4), a
border band is inside it, a content box is inside it.

Declaring anything narrower would need `pointer-events` machinery this build does not have, and
would be wrong besides. CSS hits a transparent `div` over its whole border box, because painting
nothing is not the same as being absent — which is what `paint.hit-test.inside_and_outside` pins,
since its `.frame` draws no background at all and is still the answer over its whole box.

What the region is **not** is a rectangle with no interior. `paint.hit-test.empty_boxes` pins the
other end: a box that is `100×0` or `0×100` is empty, and an empty rectangle contains no point —
not even its own top-left corner, because there is no interior for one to be in. A build that
tested `<=` on the edges would report a hit on a box with no area, which is a box that draws
nothing and cannot be pointed at.

The region is **half-open**: `x` and `y` are inside, `x + width` and `y + height` are outside.
`GfxRectContainsPoint()` owns that decision and this walk calls it rather than comparing
against the edges itself, so a hit test and a fill agree about the last pixel of a row by
construction.

### 3.3 The boundary clips the contents, never the box

This is 11.8's arrangement read backwards, and it is where a naive hit test goes wrong in the
direction that is hardest to see.

A child outside a `overflow: hidden` ancestor's boundary must not be hit. But that **ancestor's
own border band** — drawn outside that same boundary — must still be. So the walk enters a box
under the boundary in force and descends into its children under the boundary *that box's own
overflow* puts in force, exactly as the painter emits them: the push stands at the box's content
boundary, and the box's decoration is outside it.

`paint.hit-test.clip_contents_not_box` asserts both halves, because a build that clipped the box
too would pass the child half of the check and be wrong about the parent.

### 3.4 Opacity does not affect hit testing, and that includes `opacity: 0`

CSS governs this with `pointer-events`, not with `opacity`: a fully transparent element still
receives a point, because an author who wanted otherwise would have said `pointer-events: none`
or `display: none`. This build has no `pointer-events`, so the rule is the property's *initial*
value — everything is hit-testable.

The strength in force travels in the **result** anyway, so the phase that does implement
`pointer-events` has the number already rather than having to add it to the walk.
`RoboPaintHitFlagTransparent` is how a caller asks "the node I hit is drawn at nothing", which
is *not* the same as "the point missed" — `paint.hit-test.opacity_does_not_gate` asserts the hit
and the strength together.

## 4. The result

`PaintHitTestResult` is a **value** owned by nobody, for the reason 11.9's records give:
a caller that kept a pointer into the layout tree would hold a borrowed view of a tree the next
mutation can move, and a click that changed meaning when a style was recomputed is worse than no
answer. Every fact the answer depends on is copied in.

| field | meaning |
|---|---|
| `outcome` | `NONE`, `ON_NODE`, `MISS` or `STALE` |
| `node` | the layout object id, well-formed exactly when `ON_NODE` |
| `dom_node` | the DOM handle the object was built from — an element for a box, a **text node** for a run |
| `kind` | `BOX` or `TEXT`; `NONE` when nothing was hit |
| `depth` | how deep the hit sits, 0 for the root |
| `point` | the query's point, paint coordinates |
| `local` | `point - origin` — layout coordinates |
| `origin` | the hit node's containing-block content origin |
| `boxes` | the border, padding and content boxes, in paint coordinates |
| `clip` | the boundary in force **on the hit node itself** |
| `opacity` | the strength in force on the hit node |
| `flags` | `TEXT`, `CONTENT`, `TRANSPARENT` |

The layout id **and** the DOM handle are both carried, and they are not redundant. The id is what
every other door in this module takes and the one that validates; the handle is what the layers
*above* consume — Phase 12's dispatch, the DOM bridge, an application asking "which element is
this" — and recovering it from the id means a walk into the tree at every use. Both travel from
the one place that already had to ask.

Note what a hit on a **text run** reports: the run's own object id and the **text node's** handle,
because that is what the object was built from. The *element* that owns the run is the parent
box's, which the tree answers in one step. This record does not guess at "the nearest thing that
could listen" — deciding that is event dispatch, and this phase does not do it.

`PaintHitTestResultIsValid()` refuses three combinations: an unknown outcome; an
identity that disagrees with the outcome (a node id without `ON_NODE`, or `ON_NODE` with no node);
and geometry, a boundary or a strength that is not a record. `MISS` and `STALE` are legitimate
records with no node, so neither is a failure.

### Flags

Three facts about the hit that the answer's fields do not state outright, one bit each — the shape
`RoboLayoutResult.h` uses for its own flags, and they are about the *hit* rather than about the
query (a "was the query stale" flag would be a state readable two ways, which is what this module
refuses everywhere).

- `RoboPaintHitFlagText` — the hit is a run, not a box. Derivable from `kind`, carried because
  a caller that has one flag to test should not have to know the enum.
- `RoboPaintHitFlagContent` — the point is inside the hit node's **content box**, not merely
  its border box. Derivable and derivable *exactly*
  (`GfxRectContainsPoint(boxes.content, point)` is the same question), which is why it is
  here as a convenience and asserted against the rectangles rather than trusted.
- `RoboPaintHitFlagTransparent` — the strength in force came to exactly `0`. The one case
  where "the node you hit is invisible" is true, which a caller presenting a focus ring wants to
  know. It goes through `RoboPaintOpacity.h`'s own predicate rather than a comparison written
  here.

## 5. The counters, and the identities between them

`PaintHitTestStats` is the same device the paint pass has, for the same reason: a query
that answered "no node" is either a document with nothing there or a build that gave up too early,
and the two are told apart by **counters**, not by an argument.

| counter | what it counts |
|---|---|
| `objects` | objects the walk examined |
| `boxes` | placed boxes examined |
| `texts` | placed runs examined |
| `unplaced` | objects nothing placed — no geometry, so nothing to test |
| `rejected_region` | subtrees the point was outside |
| `rejected_clip` | subtrees the boundary in force excluded |
| `hits` | 1 when a node was hit, else 0 |
| `depth_max` | deepest object examined |

Three identities hold, and `paint.hit-test.stats_identities` asserts each:

```text
boxes + texts + unplaced == objects     every object examined is one of the three
hits == (outcome is ON_NODE)            exactly one node can be the answer
rejected_region + rejected_clip >= 1     whenever the answer is a MISS
```

The third is the load-bearing one: reaching the end without a hit means *something* excluded the
point, and a build that missed without excluding anything has lost track of its own walk.

`objects` is deliberately **not** `LayoutTreeObjectCount()`. A subtree hanging under a box
that nothing placed is unreachable by construction — no geometry, no origin, no coordinate to test
in — so the walk counts that box as `unplaced` and does not descend, exactly as the painter emits
nothing below it. Reading the two numbers against each other is how a reader learns how much of a
document the point could even have been over.

## 6. What the walk refuses

Two shapes of broken tree are reported rather than skipped, for the reason
`RoboPaintContext.h` gives: half an answer to "what did the user click" is not an answer.

- a **child link naming no object** — `RoboError` with a diagnostic naming the parent, the index
  and the id;
- a **derived box record that is not one** — the same.

And one state is neither of those and is not an error:

### A stale tree answers `STALE` with no node

If `LayoutTreeNeedsLayout()` is set, no box's geometry is final and there is no answer to
give. `RoboPaintHitStale` is the third outcome, and it exists because a hit test against
half-computed geometry is not a *less accurate* answer, it is a **wrong** one: it would report
whichever node happened to hold a stale rectangle, and a caller that presented it as a click would
be dispatching an event to a node the user is not pointing at.

`RoboPaintHitNone` is 0 so that a cleared result describes the state no query has reached,
which is what a caller who zeroed its struct has — the same "the cleared state is the honest one"
rule the paint pass outcome follows.

`PaintHitTest()` returns **`RoboOk` for every outcome that is an answer**, `ON_NODE`,
`MISS` and `STALE` alike, since a refusal is a legitimate conclusion rather than a failure. A
nonzero code means the query could not be *asked*. `paint.hit-test.safety` asserts the difference
between "no node was hit" and "no question was asked".

## 7. The dump

`PaintHitTestWrite()` writes the whole of one query as the trace a developer reads. The
**first line is the answer** and every line after it is one object the walk examined, in the order
it examined them:

```text
hit-test point=120,80 outcome=node node=2 depth=1
node=1 depth=0 box  outcome=enter    border=0,0,800,600 bound=0,0,800,620
node=2 depth=1 box  outcome=enter    border=0,0,200,100 bound=0,0,200,100
node=7 depth=2 text outcome=enter    border=1,5,80,16   bound=1,5,80,16
hit node=7 depth=2 point=10,10
node=3 depth=1 box  outcome=region   border=400,0,200,100 bound=400,0,200,100
```

The reason column is padded to its widest member, `unplaced`, so the rectangles line up, and the
vocabulary is exactly the walk's five decision points:

| reason | meaning |
|---|---|
| `enter` | inside its subtree's bound and the boundary in force — the walk descended (for a run, a leaf, it was tested) |
| `unplaced` | nothing placed it: no rectangle to test, nothing below it reachable |
| `region` | the point was outside the subtree's **bound** |
| `clip` | the boundary in force excluded the point |
| `hit` | this node is the answer, on its own line carrying the point |

There is **no `miss` reason**, and its absence is deliberate: a box that was entered and produced
no hit writes `enter` and then its children follow, so the line that should have been the `hit`
one is found by looking for it. A reason for "tested and not the answer" would be on nearly every
line of nearly every trace, which is the definition of noise in a diagnostic.

The dump runs the query **twice** rather than reading a recorded trace. That is sound because the
walk is a pure function of the tree and the point — the same property the query itself relies on
when a caller runs it once per input event — and it makes the dump a statement about the build
rather than about a previous call. `paint.hit-test.dump` pins it by dumping one tree twice and
comparing. The ordering is why it must be two passes: the header names the outcome, so it cannot
be written until the answer is known, and the trace has to follow it.

`RoboPaintHitTestLineMax` is 192, derived in the header from the widest line the format can
produce with headroom. The suite **measures** the widest line this build actually produces and
asserts it is inside the bound rather than trusting that paragraph.

`PaintHitTestWrite()` follows the contract every `_write` door in this tree uses:
`RoboNotSupported` when the dump does not fit, with `out_length` receiving the length it would
have needed, so a caller sizes a buffer from the refusal rather than from an estimate.

## 8. The screen, and the agreement

The phase's claim is that the two walks agree about a document. That is checkable three ways and
all three are checked.

**Against the painter's records** (`paint.hit-test.matches_the_paint_list`,
`clip_inside_matches_painter`, `opacity_nested_matches_painter`). Every case takes the
coordinates it queries from the **painter's own `BEGIN_BOX`/`TEXT` record** for the node in
question, not from a literal, so a fixture that gains a wrapper or a margin cannot silently assert
about the wrong rectangle. The result's boxes, its boundary and its strength are then compared
against that same record's.

**Against the screen** (`paint.hit-test.agrees_with_screen`). Three opaque boxes, two of them
overlapping — a frame with a `.under` child and an `.over` child that `position: relative` moved
onto it — are painted into a real RoboGraphics surface through the display list and the decoration
pass, and then every probe is checked in both directions:

```text
query(x, y) names node N
N's own background fill, read from the paint list, is rgba(…)
the pixel at (x, y) is that same rgba(…)
```

The two halves are computed by different subsystems — the colour by the painter, the pixel by the
decoration pass — so agreement is evidence rather than an identity. Opaque fills are what make it
an equality rather than a blend: two opaque colours compose by `src-over` to exactly the later one,
so "the topmost drawing is this colour" and "this pixel is this colour" are the same statement.
`.under` and `.over` are given **different** colours on purpose, because agreement is only evidence
if the two boxes could have been told apart by it. The probes are:

| probe | expected answer | what it proves |
|---|---|---|
| inside `.under`, clear of `.over` | `.under` | the walk did not answer with the covering neighbour |
| the intersection's midpoint | `.over` | **last drawn wins**, and the screen agrees |
| the frame's gutter | the frame | the ancestor fallback |
| outside the document | `MISS`, alpha 0 | nothing is hit *and* nothing was drawn there |

The fixture computes the intersection rather than assuming it, and asserts `.over`'s final top is
inside `.under`'s band — which can only be true if the relative offset was applied, since `.over`
flows *below* `.under`. The miss probe is the control: a walk that guessed a box for every point
would be caught by the unpainted pixel, and the surface is asserted to have been written to
elsewhere so the transparent corner is a fact about the document and not an unpainted surface.

**Against itself** (`paint.hit-test.determinism`, `mutation_isolation`). The same tree and the same
point produce equal results across repeated queries, and mutating the tree afterwards does not
rewrite a conclusion already drawn.

## 9. Findings the phase recorded

**The trace's reason vocabulary has no `miss`, and its absence is the vocabulary's shape rather
than an omission.** A sixth reason for "examined, in-region, and not the answer" would be printed
for every object of every trace that did not stop the walk — which is what a non-answer is — so it
would be on nearly every line and would carry no information. The five that remain each name a
*decision point*: an object is entered, or unplaced, or outside the region, or outside the
boundary, or it is the answer. That makes a trace read as the walk's control flow rather than as a
log, which is what a reader debugging a wrong answer needs.

**`RoboPaintHitFlagContent` is derivable, and derivable exactly.** The flag exists because a
caller reaching for the thing the node *contains* — a text editor, a scroll target, a surface a
later phase draws into — needs it before it asks anything else. Because it is exactly
`GfxRectContainsPoint(boxes.content, point)`, the suite asserts the two agree for every
case it raises rather than trusting the flag: a redundancy with an assertion holding the halves
together is a test; one without is a place for them to drift.

**`visibility` is registered in RoboCSS and consumed by nobody, so the hit test cannot respect
it.** `RoboCSSPropertyRegistry.c` knows the property; RoboLayout builds the box and the painter
paints it regardless, because no layer in this build reads it. Reading it here would make a hit
test refuse a point on something visibly drawn — the one failure rule 3.4 exists to avoid.
`paint.hit-test.visibility_is_not_consumed` records the fact as a case, so the phase that teaches
layout and paint about `visibility` has a failing assertion pointing at the place that must change
rather than a silent divergence between what is drawn and what is hit.

**`object_count` and `stats.objects` answer different questions.** The first is how many objects the
tree holds; the second is how many the walk reached. A subtree under an unplaced box is
unreachable — no origin, no coordinate — so the difference between the two numbers is exactly how
much of the document the point could not have been over, which is the number a reader debugging a
miss actually wants.

## 10. Visual smoke test

Run `paint.hit-test.agrees_with_screen` and read §8's table. The fixture is:

```html
<div class="frame">
  <div class="under"></div>
  <div class="over"></div>
</div>
```

```css
.frame { width: 300px; height: 200px; background-color: #14283c }
.under { width: 120px; height: 80px;  background-color: #c81e1e }
.over  { width: 120px; height: 80px;  background-color: #1ea05a;
         position: relative; top: -60px; left: 40px }
```

```text
+--------------------------------------+  ← .frame  #14283c
|   +---------------+                  |
|   | .over  #1ea05a|  ← moved up 60px |
|   +---------------+                  |
|   +---------------+                  |
|   | .under #c81e1e|                  |
|   +---------------+                  |
|                                      |
+--------------------------------------+
```

The overlap band carries `.over`'s green on the screen **and** is answered by `.over`; the
`.under` strip below it carries red and is answered by `.under`; the frame's gutter is answered by
the frame. The following element does not move into the relatively-positioned box's flow space —
`position: relative` moved `.over` without removing it from flow, which is 10.12's rule and the
premise this fixture depends on.

To reproduce it alone:

```bash
cd runtime
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Test
./Out/RoboTest.host.exe
```

## 11. What is absent, on purpose

No **event dispatch** and no bubbling. The result names one node; walking up to the nearest element
that could listen, deciding whether an event is a `click`, and delivering it are **Phase 12's**.
This phase ends at the answer.

No **caret or character mapping** inside a run. A run is one rect and one node here, and "which
character is between these two coordinates" is a text-editing question this phase leaves. No text
selection, no cursor positioning, no grapheme or bidi hit mapping, no IME.

No **`pointer-events`**, and consequently no rule that opacity or transparency suppresses a hit
(§3.4).

No **`border-radius`**, `clip-path`, masks, filters, blend modes or transforms: every one of those
needs a shape this build does not have, and testing a *rectangle* against them would be a partial
behaviour wearing the name of a full one.

No **scrolling, zoom, or device-pixel-ratio** handling: there is no scroll offset to subtract and
no scale to divide out, so §2's coordinate model has one plane.

No **alpha-mask or SVG geometry** hit testing, and no **replaced-element-specific** hit region: since
11.7 a document's `<img>` paints, so the brief's "image bounds" case has a subject at last — but this
suite landed before that kind existed and its cases name the boxes the build had then. The mechanism
is not per-kind — a node's region is its border box through `PaintBoxesOf()`, the same door a
replaced element's geometry comes from — so an image element is answered by construction, and the
case that would pin it beside `paint.hit-test.region_is_border_box` is recorded as owed by the
suite's own header rather than counted as covered.

No **animation-aware** hit testing, no compositor or GPU hit testing, no JavaScript, no networking,
no filesystem, and **no Chromium dependency**: nothing here is a translation of Blink's hit-testing
code, and the suite depends on no Chromium header, library or test framework.
