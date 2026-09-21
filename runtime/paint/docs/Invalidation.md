# RoboPaint — paint invalidation (Phase 11.11)

The eleventh release of RoboPaint answers a question the ten before it raised and could not
answer: given a picture this module already produced, **which of its pixels are no longer the
pixels the document describes?**

```text
DOM / CSS / layout change
        ↓
RoboPaintInvalidation        ← a set of rectangles, with why, and the boundary each was drawn under
        ↓
RoboPaint                    ← the records, rebuilt by the ordinary pass
        ↓
RoboDisplayList              ← the artifact a rasteriser consumes, rebuilt whole
        ↓
RoboGraphics                 ← the caller re-executes only the regions
```

Two framing sentences belong at the top, because everything below is a consequence of them.

**A region is a promise that a repaint of it is *sufficient*, never that those pixels changed.**
Every door in the module widens rather than narrows when it is not certain — a boundary it cannot
read, an axis with no extent to intersect with, a change it cannot place. The failure mode the phase
was written against is a *missing* rectangle (a stale pixel nobody repaints), and the cheapest
insurance against it is to be allowed to be too large.

**It is not a cache of the picture and not a patch of the list.** 11.1 recorded that a paint list is
regenerated from the tree on every pass rather than stored; this phase does not change that. The
display list is still rebuilt whole and the set says how little of it a caller may **re-execute**.
There is no dirty-rectangle history, no tile tree, no damage accumulation between frames, no
compositor layer and no partial list mutation — each of those is named again in §12.

---

## 1. Why this lives in RoboPaint

The same reason 11.10's hit test does, from the other end of the pipeline. A region cannot be
computed from the DOM (a declaration is not a rectangle), nor from layout (layout knows where boxes
are, not what was drawn there), nor from the display list alone (a command has one resolved rectangle
and no history, so two lists compared command by command can say *that* position differs and not
which declaration moved it). Only this module holds both the **geometry** and the **sequence**, and
only this module's records carry the two fields a region's reason is read from — the boundary in
force and the strength in force.

What it deliberately is **not** is an extension of RoboLayout's invalidation (10.14), which is a
different question asked about the same change:

| | RoboLayout 10.14 | RoboPaint 11.11 |
|---|---|---|
| question | *is this box's conclusion still true?* | *which pixels are stale?* |
| answer | a state on the box | a set of rectangles, each with a reason |
| scope | the box and its dependents | a region of the page |
| result of a hit | that box is laid out again | that region is repainted |

The two compose and neither calls the other. A property RoboLayout *measures* is classified here as
`LAYOUT` (§3) rather than as a paint change: the caller owes a layout pass, **and then** a repaint,
and this module never runs the first of those. That is also why the module has no door that takes a
DOM node or a computed style: it reads artifacts — records, commands, and a node id — and a change it
was told about.

---

## 2. The region

```c
typedef struct robo_paint_invalidation_region {
    robo_layout_rect_t              rect;      /* paint coordinates — one page space */
    robo_layout_node_id_t           node;      /* the box, or NODE_ID_NONE */
    robo_paint_invalidation_reason_t reason;   /* why the pixels there are stale */
    robo_paint_clip_t               clip;      /* the boundary in force where it was produced */
    int                             narrowed;  /* rect already **is** the intersection with clip */
    int                             full;      /* rect is a conservative whole-extent answer */
} robo_paint_invalidation_region_t;
```

A value record owned by nobody — and unlike every other record in this module, the **first whose
whole purpose is to survive a mutation**. It is produced from a tree that is about to change and read
after the change, so it carries no pointer to that tree, no borrowed style and no live geometry. A
`robo_layout_node_id_t` is a number rather than a handle, and an id that no longer names any box is
exactly the removal case the record exists to describe.

The three fields that are not the rectangle are the three a consumer needs and none of which it can
infer from the rectangle:

- **`reason`** — *why*, one per region rather than one per property. A per-property reason would have
  to be a string, which would make the record own storage and the module own lifetimes for a fact
  only a person reads. The dump prints the name.
- **`narrowed`** — when set, `rect` **is** the intersection with `clip`, so a consumer holding the same
  boundary may treat it as exact. When clear, the rectangle is *conservative*: an axis of the boundary
  had no extent to intersect with (`overflow-x: visible`), so the module kept the whole rectangle
  rather than inventing a bound for the free axis — the rule `robo_paint_clip_rect` states by refusing
  to answer (§7).
- **`full`** — the rectangle is a whole-extent answer: the producer knew *something* became stale and
  could not say where. `robo_paint_invalidation_set_scope` reads these and nothing else.

The rectangle itself is validated the way every rectangle in this tree is: `region_is_valid` requires
a non-negative size, a defined reason, a valid boundary, and — the one implication worth writing down
— `narrowed` only beside a boundary that is actually **active**. A rectangle claiming to have been
narrowed by nothing is arithmetic that went wrong somewhere.

---

## 3. The three vocabularies

### 3.1 Reasons

Eleven, and every one has a **producer in this module** — the rule `RoboPaintTypes.h` states for an
enumerator: a name with no code behind it is a promise rather than a vocabulary.

| reason | produced by |
|---|---|
| `STYLE` | `set_from_stats`, for paint-visible staleness the traversal counted but did not place |
| `BACKGROUND` | a `BACKGROUND` record differs between the two lists |
| `BORDER` | one of the four `BORDER_*` records differs |
| `TEXT` | a `TEXT` record differs |
| `IMAGE` | an `IMAGE` record differs |
| `GEOMETRY` | the same node at a position, with different boxes |
| `OPACITY` | the strength in force differs |
| `CLIP` | the boundary in force differs |
| `CONTENT` | the node at a position is a *different* box, or a drawing appeared/went away |
| `VIEWPORT` | `set_mark_full` — the containing block is the caller's to name |
| `NONE` | a cleared record, and the reason an empty set has |

Two absences are deliberate and both are argued in the header. There is **no `VISIBILITY`** reason,
because no layer in this runtime consumes `visibility`, so a change to it cannot change a pixel:
`robo_paint_invalidation_reason_for_property("visibility")` answers `NONE` and
`paint.invalidation.unconsumed_properties` pins it. And there is **no reason per property** at all —
`BACKGROUND`/`BORDER`/`TEXT`/`IMAGE` are what a *record* can tell you, and the property is recovered by
asking `reason_for_property` about a declaration instead.

### 3.2 Classes

`robo_paint_invalidation_classify_property` answers one of three things about a property name, and
three rather than two for the reason `robo_layout_style_effect_t` gives: *"this build draws nothing
from it"* and *"this build has never heard of it"* are different statements about a property, and only
the first belongs to a name a stylesheet may legitimately write.

| class | meaning |
|---|---|
| `NONE` | this build draws nothing from it: a change repaints nothing |
| `PAINT` | a repaint is owed and the geometry is sound |
| `LAYOUT` | the geometry moved: a layout pass first, **then** the repaint |

The answer is *derived* from RoboLayout's classifier rather than restated — a property layout measures
answers `LAYOUT`, everything else is asked of this module's own small table of properties it draws
from (`color`, the `background` family, the four `border-*-color`s and their shorthand, and `opacity`).
That second table is the whole reason the door exists instead of re-exporting layout's: RoboLayout's
`PAINT` row means *"drawn, not measured"*, which is a statement about **layout**, and three of its
members are drawn by nobody in this runtime — `visibility`, `text-decoration` and `background-image`.
Answering `PAINT` for those would make a change producing an **identical picture** request a repaint,
which is the one thing this module must not do casually. They answer `NONE`, and the case that pins it
is where a later goal finds out that consuming one of them means adding a row here.

A name neither module has heard of answers `NONE`, which is the *sound* answer rather than the lazy
one: the table names every property this build draws from, so a name outside it cannot have changed a
pixel.

### 3.3 Scopes

`robo_paint_invalidation_set_scope` reads the set's own flags and answers `NONE` (empty — the picture
is current and no work is owed), `PARTIAL` (the regions are a smaller answer than the extent) or
`FULL` (at least one region is a conservative whole-extent answer). The distinction is the brief's,
and it is the one a caller acts on: `FULL` is exactly what *"the producer could not tell where"* looks
like, and it must not be read as *"one box changed"*.

---

## 4. The doors

Five producers, and the difference between them is **how much the caller already knows**.

| door | the caller knows | the answer |
|---|---|---|
| `set_add_rect` | a rectangle, a reason, maybe a box and a boundary | one region, narrowed by the boundary |
| `set_add_change` | an old rectangle **and** a new one | **one** region — their union |
| `set_mark_operation` | a record, and why | the record's box's border box |
| `set_mark_node` | a paint list and a box id | every record that names that box |
| `set_mark_subtree` | a paint list and a box id | the box **and its descendants**, as one region |
| `set_mark_node_change` | two lists and a box id | the old rectangle **and** the new one, as two regions |
| `set_mark_full` | an extent and why | one whole-extent region |
| `set_from_lists` | two record lists | every region in which they disagree |
| `set_from_display_lists` | two display lists | the same, over the commands a rasteriser reads |
| `set_from_stats` | a paint pass record | nothing, or one whole-extent region |

Two of those are worth stating in words rather than in a table.

**`add_change` is one region and `mark_node_change` is two, and that is not an inconsistency.** A
caller reaching the first knows the two rectangles came from *one thing* that changed, so their union
is a single honest answer and a consumer with one rectangle still gets the whole of it. The second is
the *diff* case, where the two rectangles were drawn under boundaries that need not be the same (the
box moved out of an `overflow: hidden` ancestor, say), so each must be narrowed by its own — collapsing
them into one union narrowed by one of them would repaint the middle of the move under a boundary that
describes only one end of it. §6 is the long version.

**`from_stats` is the integration 11.1 promised, and it is deliberately the coarsest door here.**
`robo_paint_stats_t.style_only` counts the visited boxes whose only staleness is a paint-visible
change — *exactly* the set a paint-invalidation system must repaint — but only as a **count**, with no
node and no rectangle attached. So `style_only == 0` leaves the set as it is (nothing is owed, which is
the case a caller turns into *skip the frame* rather than *repaint everything to be safe*), and
`style_only > 0` produces **one** region covering the caller's `extent`, `full`, with reason `STYLE`.
The extent is the caller's because this module owns no viewport: passing one that is too small is the
single mistake here that can leave stale pixels, which is why the door documents it rather than
defaulting it.

`add_region` is the one door every other adding door goes through, and the only place the set's rules
are written: a region with **no area** is refused by the module's own exclusion rule (`excluded` counts
it, nothing is appended). Refusing to *store* it is not refusing the call — a zero-area invalidation is
a conclusion this build reaches all the time (*a box with no size changed nothing*) and it is success
with nothing added.

---

## 5. The drawing rule

**Only records that paint produce regions.** A structural record — a box marker, the content boundary,
a clip or opacity scope — draws nothing, so a region out of its box would be a rectangle with nothing
stale inside it; and the alternative is worse than useless: it would put a region in the set for every
box that moved even when the box draws nothing at all.

The scopes are not lost by the rule, and that is the part that would be easy to get wrong:

```text
PUSH_CLIP    → carries no region of its own,
               but every record **inside** it carries the boundary in its own `clip`
PUSH_OPACITY → carries no region of its own,
               but every record inside it carries the strength in its own `opacity`
```

So a clip that moves and an element that fades both reach the set through the drawings they bound —
which is also precisely where a rasteriser would have to look. `paint.invalidation.clip_change` and
`paint.invalidation.opacity_change` pin the two halves separately, and the reason a scope's own effect
is reported as `CLIP`/`OPACITY` rather than as "it moved" is that the reader's question is *what is
drawn there now*, not *which coordinate moved*.

The structural positions are still **counted**. `compared`, `matched`, `differed`, `inserted` and
`removed` describe the walk — every record or command the diff looked at — and a structural
difference really is a difference; it simply contributes no rectangle. `from_display_lists` follows
the same rule through `robo_paint_display_command_is_drawing`.

---

## 6. The old rectangle and the new one

The failure this phase exists to prevent is not "the box is not repainted", it is **"the box is
repainted and the box's old position still shows"**. So every door that knows two rectangles states
both, and the diff emits **two regions** for one position whose boxes differ:

```text
old bounds = A   new bounds = B      invalidate A, and invalidate B
```

The two are not unioned in the diff for the reason §4 gives — each was drawn under its own boundary
and each must be narrowed by its own — and the union is taken only where the caller vouches that both
rectangles came from one thing (`set_add_change`, and one `set_mark_full` for the case with no
rectangle at all).

Rectangles are unioned in 64 bits and saturated, like every other accumulation in this module: two
bounded coordinates have an unbounded difference, and a wrapped union would be a rectangle somewhere
*else* on the page rather than an obvious failure. `set_bounds` is the coarse door for a consumer whose
rasteriser takes one rectangle — a union of all the regions is always sufficient, and merging them into
a *smaller number of tighter rectangles* is an optimization 11.11 deliberately does not attempt,
because it needs a region tree and a subtle merge bug is a stale pixel that no test of counts would
catch.

---

## 7. The boundary, and the strength

Clipping is **read, never invented**. When a door is handed a boundary, the region is its intersection
with the rectangle and `narrowed` is set: a change inside an `overflow: hidden` ancestor cannot have
touched a pixel outside it, and `robo_paint_clip_rect` is the door that says so. When either axis has
no extent to intersect with — or the boundary is unreadable, or there is none — the rectangle is kept
**whole** and `conservative` counts it.

This is the asymmetry the whole phase runs on, restated at the level of a single rectangle:

```text
narrow when the boundary is certain
widen  when it is not
```

Inventing a bound for a free axis would exclude pixels this build does draw, which is the one failure a
repaint list cannot recover from. There is no `clip-path`, no mask, no rounded clipping and no filter:
the boundary model is 11.8's and this module does not change it. The region carries the boundary
**for the consumer**, not because a repaint needs it — a rasteriser re-reads the clip stack from the
list — but because a person debugging a repaint needs to know *what bounded the answer*, and because
`narrowed` is meaningless without it.

Opacity is the same shape one level up, and 11.9's contribution to it is the reason a *parent's*
change is not a parent-sized region: an element's strength is accumulated along the path that reaches
it, so changing `opacity` on a container changes what **every descendant** composites to. The diff
finds this without a compositor because the strength is carried on every record, so the regions that
appear are the *descendants'* rectangles — which is the whole difference between invalidating the box
and invalidating what the box composites to. `mark_subtree` is the explicit door for the same fact when
a caller only has a box id and a list.

---

## 8. The counters, and the identities between them

```c
typedef struct robo_paint_invalidation_stats {
    size_t added;        /* regions in the set */
    size_t narrowed;     /* of those, the rectangle is the intersection with its clip */
    size_t conservative; /* of those, kept whole because an axis of the clip has no extent */
    size_t excluded;     /* produced and dropped: the clip excluded them, or they had no area */
    size_t full;         /* of those, conservative whole-extent answers */
    size_t unions;       /* set_add_change calls that marked an old-and-new pair as one region */
    size_t spans;        /* regions that covered a box's **subtree** rather than one box */
    size_t compared;     /* positions a diff walked where both artifacts had an entry */
    size_t matched;      /* of compared, the positions whose artifacts were equal */
    size_t differed;     /* of compared, the positions whose artifacts were not */
    size_t inserted;     /* positions only the **after** artifact had */
    size_t removed;      /* positions only the **before** artifact had */
} robo_paint_invalidation_stats_t;
```

Three identities, and each is a claim about the module rather than a coincidence:

```text
added = narrowed + conservative    every stored region was classified exactly once
compared = matched + differed      the diff's walk accounted for every position it visited
excluded = the regions NOT in the set   the number that says "nothing changed" and
                                        "everything changed outside the boundary" are
                                        different answers
```

The last one is why `excluded` exists at all: a reader who saw only `added` could not tell a document
that did not change from a change the boundary excluded entirely. The pass counts are **positional** —
`inserted` counts structural positions too, though they produce no region (§5) — and the header says
so where a reader would otherwise derive a missing identity from them.

---

## 9. The dump

```text
0 reason=geometry rect=20,20,60,40 node=7 clip=visible/visible narrowed=no full=no
1 reason=geometry rect=60,20,60,40 node=7 clip=visible/visible narrowed=no full=no
scope=partial regions=2 narrowed=0 conservative=2 excluded=0 full=0
```

One line per region, then a summary line, in the format every `_write` door in this tree uses: the
header at the top and the accounting at the **bottom**, so a dump whose lines are cut short still ends
with the numbers that say what it was describing. There are no addresses — `node` is an id, and the
determinism case compares two independently built sets **and their dumps** — and a `_write` that does
not fit refuses with `ROBO_NOT_SUPPORTED` and hands back the length it would have needed, so a caller
can size a buffer from the refusal rather than an estimate.

The dump prints **one rectangle per region** rather than the brief's `old`/`new` pair, and that is a
decision rather than an omission: the pair does not survive into a region by design
(`set_add_change` stores the union; `mark_node_change` stores two regions), so a dump printing both
fields would be describing a record this module does not have. The pair is visible where it exists —
two consecutive lines with the same reason and the box's two rectangles, which is what the example
above is.

---

## 10. Findings the phase recorded

**A. Only records that paint produce regions, and the rule is what keeps the answer small.** The
first version of the walk compared *every* position, which made a single moved box produce six regions
(`BEGIN_BOX`, `BACKGROUND` and `END_BOX`, each as an old and a new rectangle) for one box that draws
one thing. The filter is per *kind* rather than per record, so it needs no per-kind list and a future
drawing kind is covered the day it is declared: `robo_paint_op_kind_draws` is the predicate, the one
11.2 already gave the colour rule.

**B. The scope's own record is redundant for invalidation, and the redundancy is what makes the rule
safe.** `PUSH_CLIP` and `PUSH_OPACITY` carry the boundary and the strength that their contents repeat
on every record, so nothing is lost by ignoring them — but the walk had to be *checked* against a clip
that moves and an element that fades rather than argued about, and both cases exist for that reason.
The clip case is the interesting one: the records inside the scope differ in their `clip` **and** their
boxes, so the reason is read from the boundary rather than from the geometry.

**C. `add_change` and `mark_node_change` must not be unified.** One region and two regions from the
same pair is the sort of "inconsistency" a later simplification removes; §4 and §6 are the argument for
why it is not one. The two doors answer different questions and the union-narrowed-once shape is wrong
in exactly the case clipping exists for.

**D. The exclusion rule belongs in one place, and it earns its keep by what it does *not* count.** One
door (`add_region`) refuses zero-area regions, so no caller can make the set claim an area that cannot
hold a pixel — and `excluded` is the counter that keeps the refusal visible rather than silent.

**E. A region set is a snapshot, and the tests say so by outliving the artifacts it came from.** The
determinism case builds the same transition in two independent trees and compares the sets *and their
dumps*, then destroys the trees and re-reads the set. That is a statement no address, no allocation
order and no clock can satisfy by accident — and it is the property that makes the module usable at
all, since its whole purpose is to be read *after* the thing it describes has changed.

---

## 11. Visual smoke test

The strongest statement this build can make about invalidation with a rasteriser that has no notion of
damage, made twice, in pixels:

**Sufficiency (`paint.invalidation.stale_pixels`).** Two documents, two surfaces. Render the before
list into `frame_a` and the after list into `frame_b`; build the set from the two record lists; then
walk the two surfaces and compare. Every pixel that differs must lie inside a region, and the case
pins that `differing > 0` as well, so the claim is not a fact about an empty picture. The transition is
a recolour **and** a displacement at once, deliberately harder than the shape of the answer.

**Repaint (`paint.invalidation.partial_repaint_matches_full`).** The capstone. `frame_a` is painted in
full (what a caller would already have on screen); the set is built; the regions are cleared back to
the page colour; the **after** list is painted into the same surface; and a fresh render of the same
list into a second surface is compared against it **byte for byte**. Zero differing pixels, or the set
was missing something. That is why the module is allowed to claim sufficiency rather than exactness.

Neither case paints through a second rendering path: both use the ordinary
`robo_paint_decoration_paint` pass over the ordinary display list, which is what a caller does too.

---

## 12. What is absent, on purpose

- **No rasteriser here — and one above.** `raster` is **1** since 11.12, and it names **RoboRender**:
  an owned pixel target, a run over the whole command vocabulary, an execution record and
  region-limited execution (`robo_render_rasterize_regions`), driven through this module's own walk by
  the **bounded** door `robo_paint_decoration_paint_bounded`. This phase still produces the regions
  and executes nothing; what changed is that the caller's loop now has a name, and that the bound is a
  **clip** rather than a filter over the set's own **union** — one run, never one per region, because a
  set may name overlapping regions and a second pass over an overlap would composite a
  semi-transparent drawing twice.
- **No partial display-list mutation — and 11.12 did not add one either**, which is the answer to
  the question this paragraph used to leave open: the list is rebuilt whole, and a region bounds the
  **pixels** that are re-executed (`robo_render_rasterize_regions`, one run over the set's union) and
  never an edit to the list.
- **No damage tracking, no tile tree, no frame history.** The set describes **one transition** between
  two states a caller hands over. There is no accumulation across frames, no notion of what is
  currently on screen, no tile size and no damage region merging.
- **No compositor, no layers, no GPU scheduling, no VSync, no frame scheduling.** Every one of them
  would want this module's output and none of them exists in this build.
- **No scrolling.** There are no scroll offsets anywhere in the stack, so a region is in the same plane
  as the drawings.
- **No animation or transition invalidation.** No layer consumes `transition` or `animation`, so
  `classify_property` answers `NONE` for them — the same rule that makes `visibility` answer `NONE`.
- **No hit-test, input, focus or selection invalidation.** Those are Phase 12's and beyond.
- **No `visibility` support at any layer**, which is why there is no `VISIBILITY` reason: a property no
  layer consumes cannot change a pixel, and claiming otherwise would be a repaint owed for an
  identical picture.
- **No merging of overlapping or adjacent regions.** Correctness before rectangle count — see §6.
- **No viewport or surface ownership.** `extent` is always the caller's argument.
