# RoboLayout — architecture (through Phase 10.4)

This is the design record for RoboLayout's architecture: what the module is, the
decisions that shaped it, the alternatives that were rejected, and the boundaries
Phase 10.4 onwards may not cross without changing this document.

It is not a tutorial. It is the thing a reviewer reads to answer "why is it like
this", and the thing a later goal reads to find out what it is allowed to assume.

For the parts a later phase added, this document describes the *frame* — where a
context lives, what owns it, how a pass classifies and then lays out — and four
documents are the records for the parts themselves: `FormattingContexts.md` for the
classification, `BlockLayout.md` for the block direction, `InlineLayout.md` for the
inline one, `Tests.md` for the suites.

---

## 1. What the phase is

Phase 10.1 builds the **shape of a layout pass** and nothing that resolves
anything. Concretely:

```text
RoboHTML document                          RoboCSS style context
        │                                          │
        └──────────────► RoboLayoutDom ◄───────────┘
                              │
                              │  one computed style per element,
                              │  owned by the bridge, borrowed by the tree
                              ▼
        robo_layout_tree_build_from_document
                              │
                              ▼
                      the layout tree  ──────────►  robo_layout_context_run
                    (boxes, links, ids)                   │
                                                          ├── classify every box
                                                          │
                                                          └── if a viewport was
                                                              recorded, lay **both
                                                              directions** out
```

What that buys the phases that follow: **every model they extend already exists**.
10.2 added formatting contexts to an object kind that was already typed. 10.3 filled
the result record that already had a state, and its pass is the only consumer of
the context model. 10.4 added a second *branch* to that same pass rather than a
second pass, because an inline context's height is the block extent of the box that
owns it. 10.8 clamps through a constraints record that already exists. 10.11 paints
an edge whose width already came from 9.15.

10.1 bought no coordinate, and the module said so in three independent places
rather than leaving a caller to find out. 10.3 flipped all three, and the shape of
that flip is the part worth keeping:

| Door | 10.1 / 10.2 | 10.3 | 10.4 | What it means now |
|---|---|---|---|---|
| `ROBO_LAYOUT_GEOMETRY_IMPLEMENTED` | `0` | `1` | `1` | a compile-time constant per direction, and the same one for both |
| `robo_layout_capabilities_t.geometry` | `0` | `1` | `1` | *this build* computes coordinates |
| `robo_layout_capabilities_t.block_layout` | — | `1` | `1` | the block direction |
| `robo_layout_capabilities_t.inline_layout` | `0` | `0` | `1` | the inline direction |
| `robo_layout_capabilities_t.block_and_inline_layout` | `0` | `0` | `1` | derived: their conjunction |
| `robo_layout_geometry_is_implemented()` | `0` | `1` | `1` | a function a caller cannot read past |
| `robo_layout_result_state()` | — | — | — | *this box* has one — the per-box authority |

Three doors for one fact is deliberate: the constant is what a later goal flips,
the field is what a feature query sees, and the function is what a caller that has
already forgotten to query the struct will call.

But every answer in that table is about the **build**, and 10.3 made that
distinction load-bearing rather than pedantic. `geometry == 1` is true of a tree
whose boxes are all `UNRESOLVED` — a context that recorded no viewport placed
nothing, a `<span>` holding only text is zero-wide because nothing here measures
text, and a `<div>` whose content is a text node is zero tall. So the per-box
`state` is the authority a consumer reads, and the capability is the answer to "does
this build lay boxes out at all". `layout.conformance.no_viewport` asserts the two
side by side over one tree, because a build that conflated them would have failed
exactly there.

10.4 added a field to that table rather than changing one, and the shape of the
addition is the part worth keeping: `inline_layout` was written as `0` in **10.2**,
with a field and no code behind it, and flipped only when the code existed. The
constant `ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED` did not exist until then, which is
the asymmetry 10.3's header argued for in its own text: a *field* may name a feature
yet to land, because a caller reads it and gets a truthful `0`; a *constant* may
not, because a name with no code behind it is a promise rather than a query.

---

## 2. The dependency position: the first module that may see both peers

`DEPENDENCIES.md` states that RoboHTML and RoboCSS are **peers**. Each may depend
on RoboDOM; neither may depend on the other. That rule alone would make layout
impossible: a layout pass needs the element tree *and* the computed style of every
element in it.

The resolution is positional rather than exceptional:

> **Whoever needs both sits above both.**

RoboLayout is that module, and it is the lowest one that needs it. Nothing below
RoboLayout may reach for both; everything above it may, because it is above
RoboLayout anyway.

The module's own dependencies are stated as a list, and `-Verify` enforces the
top of it as a source scan:

| May depend on | Must not depend on |
|---|---|
| RoboBase, RoboMem, RoboGraphics (geometry only), RoboHTML, RoboCSS | RoboWindow, RoboDisplay, RoboPaint, RoboRender, RoboCompositor, RoboJS, RoboShell, RoboAPI, RoboDemo, Chromium |

RoboWindow and RoboDisplay are worth naming separately: they sit **below**
RoboLayout in the graph, so reaching them would not be an upward edge and no
layering argument would catch it. They are nevertheless forbidden, because a
layout pass that consulted a window would be a layout pass that cannot be tested
without one — and the viewport is an *argument* to a pass, not something a pass
goes and asks a window for.

---

## 3. The tree, and why it is not the DOM

This is the phase's one structural decision, and the argument for it is that the
two trees **do not have the same members**:

| Case | In the DOM? | Generates a box? |
|---|---|---|
| `<div>` | yes | yes |
| `<div style="display: none">` | yes | **no** |
| a text node | yes | not in 10.1; something in 10.5 that is not an element box |
| a comment | yes | no |
| `<head>`, `<title>` | yes | no |
| a future anonymous block | no | yes |
| a future `::before` | no | yes |

A pass that walked the DOM would therefore re-decide, at every step, which nodes
it was allowed to look at. Each of the fourteen goals that follow would add another
such decision in another place — 10.3 has an opinion about which children it
descends into, 10.5 needs pseudo-elements, 10.12 needs to hoist an absolutely
positioned box out of flow. A tree built **once** by one builder means every pass
walks boxes and only boxes, and the "is this thing a box?" question has exactly one
answer in exactly one place.

The rejected alternative is worth recording, because it is the one every
implementation considers: walk the DOM and skip as you go. It is smaller today and
strictly worse in six months, because the builder's rule is a *property of the
document*, while a skip test inside a pass is a property of *that pass*. Two passes
with two skip lists disagree the first time one of them is changed.

### 3.1 Identity

Object ids are **dense and tree-scoped**:

- 1-based, assigned in creation order, so `id == index + 1` holds for the life of
  the tree and walking the whole tree is a loop, not a lookup.
- Tre-scoped, not document-scoped. A box id names a box *in a tree*, and the same
  number in two trees names two different boxes.
- **Not pointers**, because the arena moves when it grows. The tree hands out a
  borrowed `robo_layout_object_t *` for one call — the whole struct at once, which
  is what makes a walk of a thousand boxes a thousand lookups rather than six
  thousand — and that pointer is invalidated by the next `add_object`. A caller
  that must keep a box keeps its id.
- **Restarted by `clear`**, exactly as RoboHTML's node ids are, and for the same
  reason: a tree is rebuilt wholesale, and a stale id must not alias whatever box
  now occupies that slot.

The builder's build is therefore *replace*, not *merge*: `robo_layout_tree_build_from_document`
clears first. A rebuild is what a DOM mutation leads to, the tree is derived state,
and there is no incremental path in 10.1 because nothing yet consumes a tree finely
enough to need one.

---

## 4. The object

One box, and the rule that governs its contents is **references in, copies out,
exactly one copy of each fact**:

| Field | Why it is this and not something else |
|---|---|
| `id` | Dense, tree-scoped (above) |
| `kind` | `ROBO_LAYOUT_OBJECT_BOX` is the only value. Shipping `BLOCK` / `INLINE` / `LINE` enumerators now would be claiming a distinction the tree cannot yet make |
| `dom_node` | An inert RoboHTML **handle** — a token, not a pointer. Asking "which element is this box?" is a comparison; asking "which box is this element?" is a lookup |
| `style` | A **borrowed** `robo_css_computed_style_t *`. The tree never disposes it, never copies it, and never parses CSS to make one |
| `display` | **The one CSS value that is copied.** The box would not exist without it — the builder creates a box exactly when the computed display generates one — and the first question a formatting context asks is what kind of box it has, before it has any reason to reach into a style it may not have |
| `parent`, `first_child`, `last_child`, `prev_sibling`, `next_sibling`, `child_count` | Ids, not pointers, for the reason in §3.1. A parent whose children are linked from both ends is what makes "append in document order" O(1) and a reverse walk possible |
| `depth` | Distance from the root box, which is 0. Cheap here (it is one increment while building) and awkward later (it is a walk) |
| `flags` | `STYLE_MISSING`, and the argument for it being a flag rather than an inference is §4.1 |
| `result` | By value, because a result is a record with no allocation, and because a box without a result would be a second lookup on the hot path of every pass |

Everything else about the element — `position`, the four margin edges, padding,
border widths, the background, the insets — is read from the borrowed style when a
later goal needs it. That is what keeps the number of copies of any fact at one.

### 4.1 `STYLE_MISSING` is a flag, not `style == NULL`

They are not the same statement. `style == NULL` means "no style was supplied";
`STYLE_MISSING` means "no style was supplied **for this box**", which is the same
thing only while the builder is the only writer. The moment 10.2 builds an
anonymous box — which has no element and therefore no style, by construction — the
two diverge, and a formatting context that read `width` off such a box would be
reading a value nobody computed.

A flag also lets the *report* count it, and the *pass* count it again
(`boxes_unresolved` is a different fact from `without_style`), so a test can assert
"this build produced boxes with no style" as a number rather than as an inference.

### 4.2 `add_object` validates rather than trusts

Every rule it enforces is one a later goal would otherwise re-check while walking:

| Rule | Refusal |
|---|---|
| a parent id must name a live box **in this tree** | `ROBO_NOT_FOUND` |
| a parent id of `NONE` only when the tree is empty | `ROBO_ALREADY_EXISTS` for a second root |
| the element handle must be well formed | `ROBO_INVALID_ARGUMENT` |
| the display must generate a box | `ROBO_INVALID_ARGUMENT` |
| no undefined flag bit | `ROBO_INVALID_ARGUMENT` |

The display check is the interesting one. Creating a box for a `display: none`
element would make the tree a **copy of the DOM's element list** instead of the
thing layout walks — and it would do it silently, because a `none` box is perfectly
well-formed as a struct. The tree refuses it at the only door that can.

---

## 5. The builder's one rule

```text
an element generates a box unless its computed display says otherwise
```

Three consequences, each of which was a decision:

**Children are elements, not nodes.** The DOM stores the newline between two
elements as a text node. A builder that walked *nodes* would produce a tree whose
sibling order was half whitespace, and `:first-child`, `+` and `nth-child` would
then mean something different in layout than they do in the selector matcher. The
rule is applied by **asking the DOM** for element children rather than by filtering
bytes, so there is one definition of "element" in the runtime and it is RoboHTML's.

**The root box is the document element.** Not the document node: a document is not
an element, generates nothing, and had a box of its own it would be a box with no
style and no parent. Starting at the document node and taking its first element
child is what makes the layout root the same thing a caller would name in CSS
(`:root`).

**An empty result is not a failure.** A document with no element, or one whose root
element is `display: none`, generates no boxes. The build reports
`elements_seen > 0, boxes_created == 0` and returns `ROBO_OK`, because "this
document has nothing to lay out" is an answer. It is also the case that makes an
empty pass legitimate — `robo_layout_context_run` reports zero objects rather than
an error — and `layout.build.root_display_none` is the case that pins it.

The build report is the fourth thing it produces and it exists because the tree
alone cannot distinguish two situations a caller cares about:

| Question | Where the answer is |
|---|---|
| how many boxes are there? | `robo_layout_tree_object_count` |
| how many elements were there? | `build_report.elements_seen` |
| how many elements generated no box? | `build_report.skipped_display_none` |
| which box is this element's? | `robo_layout_object_id_for_dom_node` — `NONE` means "none", and the report is what tells you *why* none |

---

## 6. The style seam

RoboLayout consumes computed styles. It never parses CSS, never holds a
stylesheet, never normalises a value, and never knows a property name beyond
`display`. The whole boundary is one callback and one struct:

```c
typedef const robo_css_computed_style_t *(*robo_layout_style_fn)(
    void *userdata,
    const robo_html_node_handle_t *element);

typedef struct robo_layout_style_source {
    void *userdata;
    robo_layout_style_fn computed_style_for;
} robo_layout_style_source_t;
```

### 6.1 Why a callback and not a `robo_css_computed_style_t *`

**So the tree builder is testable without CSS.** A suite that wants to prove "a
`display: none` element generates no box" has no business standing up a style
context, a property table, a registry and a sheet. A phase that made it do so would
be a phase whose structural tests are really integration tests wearing a
structural test's name — and the purely structural suites (`layout.tree.*`,
`layout.constraints.*`) would not exist.

What a caller does **not** get is permission to skip CSS and still claim the boxes
are styled. A source that answers nothing produces boxes carrying `STYLE_MISSING`,
and that flag is exactly the fact a later formatting context checks before reading
a property.

### 6.2 The lifetime rule is the whole contract

A borrowed style pointer is **stored in the tree**. So it must stay valid for as
long as the tree holds it. The builder does not copy it, and RoboLayout has no way
to extend the life of something it does not own.

The real supplier meets that by owning one computed style per element for as long
as the bridge lives. A test meets it by keeping its styles on the stack. A supplier
that returned a temporary would be returning a dangling pointer the moment the next
element was asked about — and the failure mode is not a crash but a style that
reads as garbage, which is why the rule is stated once, in the header, rather than
discovered later.

### 6.3 One read goes *through* the seam

`robo_layout_style_display` reads `display` out of a style using 9.12's own
accessor. So RoboLayout holds no list of property names and no CSS semantics of its
own — and a `NULL` style answers `inline`, which is 9.12's *initial* value and
therefore what the specification says an element nothing styled is. That is not a
special case; it is the rule applied.

---

## 7. The bridge

RoboLayoutDom is the first module in the runtime that depends on RoboHTML *and*
RoboCSS. It is deliberately the smallest thing that can do the joining:

```text
RoboCSS asks its host about one element at a time   (RoboCSSStyle.h)
RoboHTML owns the elements, their attributes and their tree
the bridge answers the first with the second, and hands RoboLayout the computed
styles the first produces
```

### 7.1 The element reference is the DOM node id

RoboCSS identifies an element by an opaque `robo_css_element_ref_t` and never
dereferences it. The bridge answers with the node id, and resolves it back with
`robo_html_document_node_by_id`.

The alternative — a private index into a mirror array — costs a scan on every
callback and a second table to keep in step with the first, and buys nothing: the
DOM's ids are already dense, already stable for the document's lifetime, and
already the identity the tree stores.

### 7.2 Element facts are captured once, because RoboCSS borrows them

An element view points at arrays of **borrowed** slices, and the matcher holds one
view while it recurses into `:not()`. So the classes and attributes of every
element are materialised into the bridge's own storage *before* any style is
computed, and the bytes they point at are the document's payload arena, which stops
moving once the tree is built.

A bridge that built the slices on the stack at callback time would hand the matcher
a view whose arrays died at the end of the call. The failure mode is worth naming
because it is not a crash: it is **a selector that matches nothing**, which is the
kind of bug that survives review.

### 7.3 Styles are owned here, so the tree can borrow them

`robo_layout_style_source` stores the pointer it is given, so *something* must own
one computed style per element for as long as any tree built from this bridge
lives. RoboLayoutDom is that owner.

This is also why the style source answers from a **lookup** rather than computing
on demand: a source that computed a fresh style per call would leak one per element
per build, and leak it invisibly.

### 7.4 Two passes, and the order is load-bearing

1. capture every element's facts,
2. compute every element's style.

Not tidiness: computing an element's style walks its **ancestors** for inheritance
(9.7), so the host must already be able to answer about them when the first
computation starts.

### 7.5 Caps are caps, not truncation

`ROBO_LAYOUT_MAX_CLASSES_PER_ELEMENT` (32) and
`ROBO_LAYOUT_MAX_ATTRIBUTES_PER_ELEMENT` (64) **fail the refresh loudly**. A bridge
that silently dropped the thirty-third attribute would turn `[data-x]` into a
*wrong* answer instead of an *unsupported* one. The numbers are far above anything
a document this runtime builds uses and far below anything that would make the
per-element storage large.

---

## 8. Constraints: unset is a flag, not a sentinel

```c
typedef struct robo_layout_constraints {
    robo_layout_size_t available;
    robo_layout_size_t minimum;
    robo_layout_size_t maximum;
    int has_available;
    int has_minimum;
    int has_maximum;
} robo_layout_constraints_t;
```

The obvious alternative is to write "unbounded" as a huge number. It is wrong for a
reason that shows up immediately: **an unbounded height and a height nobody has
worked out yet produce the same arithmetic and are different answers.** A caller
that has not been told the available width must be able to say so, because there is
no containing block in 10.1 to ask, and inventing one is precisely the failure this
phase exists to avoid.

The read doors follow the same discipline: `robo_layout_constraints_available`
answers `ROBO_NOT_FOUND` when the field was never set, which is a different answer
from a recorded **zero**. A recorded zero is a legitimate, known zero — a box in a
collapsed container has one — and that is why `set_available` accepts a zero size
and refuses a negative one.

**A negative size is refused, never reinterpreted.** `available = -1` is a caller
bug, and the call says so. For the same reason nothing here clamps an input into
range silently.

### 8.1 What `clamp` is, and what it deliberately is not

`robo_layout_constraints_clamp` applies the recorded minimum and maximum to a size
*somebody else decided*. It has no opinion about what that size should have been,
and it consults neither `available` nor any property. It exists so that "min wins
over max" is decided **once** rather than in every later goal.

`available` is deliberately **not** an implicit maximum. Filling the available space
is a formatting-context decision — a block box does, an inline box does not — so a
constraint object that treated it as a cap would be answering a question that
belongs to 10.3.

### 8.2 A contradiction is visible, not repaired

A minimum above a maximum is neither repaired nor refused at set time, because the
two may legitimately be set in either order and the combination only becomes a
problem once both exist. CSS resolves the conflict in the minimum's favour (10.8).

So `robo_layout_constraints_is_valid` is the predicate that lets 10.8 *see* the
conflict and lets a caller refuse a nonsensical pair up front, and `clamp` answers
`ROBO_NOT_FOUND` rather than handing back a size no one can defend.

What is absent from this header, and must stay absent: percentage resolution,
`auto` filling, intrinsic sizing, `box-sizing`. Those are 10.8. If they grew here,
every later goal would have two places to look.

---

## 9. Results: a state, not a rectangle

```c
typedef enum robo_layout_result_state {
    ROBO_LAYOUT_RESULT_NONE = 0,       /* no result at all */
    ROBO_LAYOUT_RESULT_UNRESOLVED = 1, /* a box exists; nobody computed it */
    ROBO_LAYOUT_RESULT_COMPUTED = 2    /* position and size are final */
} robo_layout_result_state_t;
```

### 9.1 Why not just a `robo_gfx_rect_t`

Because a rectangle **cannot express *unresolved***, and every consumer above —
paint (11), compositing (15), hit testing (12) — has a decision to make about a box
whose geometry is not known:

| Representation | What the consumer must do | Result |
|---|---|---|
| a zero rectangle | paint a degenerate box | a silent wrong answer |
| `UNRESOLVED` | skip this box and count it | a decision |

A zero rectangle forces the wrong branch. A state forces the right one.

### 9.2 The coordinate space is stated, not implied

- `position` is the box's **top-left corner in its containing block's coordinate
  space**. It is not document-space and not viewport-space: those are derived by
  walking the chain of containing blocks, and each step of that walk is a later
  goal's job. What 10.1 fixes is that a result *can* say where a box is, so
  10.2 onwards has one place to put the answer.
- `size` is the **border-box** size — the thing a box's edges enclose — because
  that is the only size paint can draw without re-deriving padding and border.
  10.1 does not compute it; it states what the number will mean.

Stating the convention now is the whole value: a convention agreed after four goals
have written code is a migration.

### 9.3 Flags are validated, and there are two of them

`OVERFLOW_X` and `OVERFLOW_Y` exist because the fact a consumer needs about a box
that does not fit is **which direction** it overflowed — a scrollable axis is a
different box from a clipped one. Both stay 0 in 10.1, and their existence means
10.13 fills them in rather than changing the struct's layout.

`set_flags` refuses a bit outside the defined set, so a typo cannot travel through
the pipeline looking like a laid-out fact.

### 9.4 Geometry is refused if it is impossible

`set_geometry` refuses a negative width or height. A box cannot enclose a negative
area, and a result that carried one would be a wrong answer discovered much later,
in paint.

`set_unresolved` clears geometry and flags, so a second pass cannot inherit the
first pass's numbers. That is what makes a repeated pass idempotent —
`layout.context.run.repeated` asserts exactly this.

---

## 10. The context, and what a pass is

A context exists as an object rather than as arguments to a call because **a pass
accumulates**: constraints are refined as the walk descends, a formatting context
needs scratch space that lives for the whole pass, and the counters are read after
it ends. Passing all of that down a call stack is passing a struct through every
function anyway, so it is allocated once, here, and owned by the caller.

It owns **nothing borrowed**. The tree, the constraints (a record by value, copied
in) and the caller's temporary data are all references. Destroying a context frees
the context and nothing else — which is what makes it safe to destroy in any order
relative to the tree it ran on, and is asserted by `layout.context.lifecycle`.

### 10.1 The pass contract

The contract as 10.1 wrote it, and what 10.3 added to it:

```text
walks the tree in creation order
   (which is document order, because the builder adds parents before children
    and children left to right)
marks every box UNRESOLVED
classifies every box                       (10.2)
counts what it saw
                                           (10.3) and then, if the context recorded
                                           a viewport: lays the block direction out,
                                           moving every box it placed to COMPUTED
```

Until 10.3, `boxes_computed` was 0 after a pass and
`robo_layout_geometry_is_implemented()` answered 0, so a caller could not mistake
"the pass ran" for "the boxes were laid out". The sentence is now the other way
round in the same spirit: the pass **does** lay boxes out, and the boxes it did not
lay out say so individually. `boxes_computed` therefore counts exactly what the
block pass placed, and it is *derived* from `stats.block.blocks_placed` rather than
counted again — one source of truth for one fact, so the two numbers cannot
disagree by construction.

The one condition on the second half is worth its own line: **the block pass runs
only when the context recorded a viewport**. The root box's containing block is the
initial containing block, which is the viewport — and RoboLayout owns no window to
ask (the viewport is an argument, §3.1 of `BlockLayout.md`). A context without one
therefore places nothing and reports zero, which is **not an error**: a caller that
only wants the classification should not have to invent a viewport to get it, and a
build that refused would make the phase that *classifies* depend on the phase that
*sizes*. It is not silent either — the per-box states, `boxes_computed` and
`stats.block.blocks_placed` all say nothing was placed.

A pending pass is a real pass. Every phase here has shipped one, and each one's
whole output was a truthful statement about what had not been computed yet.

### 10.2 The tree is taken mutable, and that is deliberate

```c
robo_result_t robo_layout_context_run(robo_layout_context_t *context,
                                      robo_layout_tree_t *tree,
                                      robo_layout_diag_t *diag);
```

A pass **writes**: a box's result is where a pass puts what it concluded.
`robo_layout_context_run` is the one component in the module that mutates a tree,
and it is the reason the non-`const` qualifier is there.

Taking a `const` tree and casting the qualifier away inside would compile, would
read as "this changes nothing", and would be undefined behaviour the first time a
caller passed an object that really was `const`. The structure is not editable —
there is no add, remove or reorder here — but the results are, and the signature
says so rather than hiding it.

### 10.3 Counters

| Field | Meaning | 10.1 → 10.3 |
|---|---|---|
| `objects` | boxes in the tree | the count |
| `visited` | boxes the pass reached | == `objects` |
| `boxes_computed` | boxes whose geometry is final | **0** in 10.1; since 10.3, `stats.block.blocks_placed` |
| `boxes_unresolved` | boxes deliberately left without geometry | == `objects` in 10.1; since 10.3, what is left |
| `without_style` | boxes carrying `STYLE_MISSING` | the count |
| `depth_max` | deepest box seen | the depth |
| `passes` | passes this context has completed, **cumulative** | 1, then 2, … |

`passes` is cumulative rather than "the last pass's value" because that is what the
word means and because a caller that wants "has this context run" already has
`outcome` for it. The distinction is not free: `layout.context.run.repeated` is the
case that pins it, and it was written because the first implementation got it
wrong.

A pass that cannot proceed — a NULL or unusable tree — sets outcome `FAILED` and
leaves the counters at zero rather than at a partly-filled count, so "the pass
failed" and "the pass ran over nothing" stay different answers.

---

## 11. Geometry is borrowed

RoboLayout defines **no geometry types**. From `RoboLayoutGeometry.h`:

```c
typedef robo_gfx_point_t  robo_layout_point_t;
typedef robo_gfx_size_t   robo_layout_size_t;
typedef robo_gfx_rect_t   robo_layout_rect_t;
typedef robo_gfx_rect_f_t robo_layout_rect_f_t;
```

RoboGraphics already defines canonical pixel geometry, and a second
point/size/rectangle would be the worst kind of duplication: two types that agree
today and differ subtly in a year, with paint converting between them at the one
boundary where a mistake is a visible artifact.

The aliases are not a formality. They make a layout signature read as layout —
`out_rect` cannot be accidentally understood as a device rectangle — while an
implementation still passes one object to a paint phase with **no conversion**.

Three facts about the borrowed geometry are load-bearing for layout and are stated
once, in `RoboGraphicsGeometry.h`, so they cannot drift here:

- the origin is top-left with `+Y` down;
- integer rectangles are half-open, `[x, x+w) × [y, y+h)`;
- a rectangle with a non-positive width or height is **empty**, not invalid.

Emptiness must not be an error in layout — a box with no content has zero height —
which is exactly why it is a predicate and not a failure.

10.1 uses these types as *outputs*, never as inputs: nothing in the module
constructs a rectangle except the test that proves the reuse is real
(`layout.module.geometry_aliases`, which asserts the alias identity at compile
time).

---

## 12. Diagnostics

The discipline RoboHTML and RoboCSS use, unchanged: a call returns a plain
`robo_result_t`, and an optional **caller-owned** record refines the failure with a
domain and a message. There is no global mutable error state, so a failure in one
thread cannot overwrite the description of another.

`ROBO_LAYOUT_DOMAIN_COUNT` is 11, and `layout.module.diagnostic_domains` walks the
enum walking the registry against it — so a domain added later without a name is a
visible omission rather than a silent gap.

---

## 13. The capability model

`robo_layout_capabilities_t` is twelve `int` fields in declaration order, and one of
them is deliberately 0:

| Field | this build | Filled by |
|---|---|---|
| `object_model` | 1 | — |
| `constraints` | 1 | — |
| `result_model` | 1 | — |
| `context` | 1 | — |
| `dom_build` | 1 | — |
| `computed_style_link` | 1 | — |
| `geometry` | 1 | 10.3 |
| `formatting_contexts` | 1 | 10.2 |
| `block_layout` | 1 | 10.3 |
| `inline_layout` | 1 | 10.4 |
| `block_and_inline_layout` | 1 | derived: the two above |
| `painting` | **0** | Phase 11 |

The table is in declaration order rather than in the order fields landed, so the
positional walk below stays a walk of the struct and not of a history.

A caller that checked only the version string would conclude that 0.1.0 lays boxes
out. The three doors §1 lists answer that together: `geometry` says **the build**
computes coordinates; `block_layout` and `inline_layout` say **which directions**;
and `block_and_inline_layout` is their conjunction, kept as a derived, *wider*
question rather than retired now that both halves exist — a caller asking "is layout
done?" should have one field to read that cannot be mistaken for "is half of it
done?". A phase that implemented one direction and flipped the conjunction instead
would have named a feature it did not have, which is exactly why 10.3 wrote it as 0
while only the block direction existed.

`layout.conformance.capabilities` walks the struct **positionally** against a list
of names written out by hand, and pins the struct's size against the list's length.
So a field added without an assertion is a compile-time or test-time omission
rather than a silently untested flag — which is the failure that motivated the
conformance suite in 9.16, one level up.

---

## 14. Ownership and lifetime, in one table

| Object | Owned by | Released by | Outlives |
|---|---|---|---|
| `robo_layout_tree_t` and its boxes | the caller | `robo_layout_tree_destroy` | — |
| a borrowed `robo_layout_object_t *` | nobody | the next `add_object` | one call |
| `robo_layout_constraints_t` | the caller, **by value** | — (no allocation) | — |
| `robo_layout_result_t` | the box it is embedded in, by value | — (no allocation) | — |
| `robo_layout_context_t` | the caller | `robo_layout_context_destroy` | — |
| the styles a tree borrows | the **style supplier** | the supplier | the tree |
| the style supplier (`robo_layout_dom_t`) | the caller | `robo_layout_dom_destroy` | **every tree built from it** |
| `robo_layout_style_source_t` | the caller, by value | — | one build call |
| `robo_html_node_handle_t` | by value, inert | — | as long as the document |
| a computed style owned by the bridge | the bridge | `robo_layout_dom_destroy`, or the next `refresh` | the tree that borrowed it |

The one ordering constraint — tree before bridge — is the row in this table that a
compiler cannot check, which is why it is written down in the header, in
`README.md`, and asserted by `layout.dom.lifecycle`.

---

## 15. Boundaries, and the deferral list

RoboLayout appears on this list once per goal, and each absence is deliberate
rather than unfinished:

| Absent | Belongs to | What exists instead |
|---|---|---|
| anonymous boxes | a later goal | every box knows which context it participates in and which it establishes (10.2); the pass counts the boxes it declined (`block.inline_level_skipped`, `inline.block_level_declined`) and the boxes nothing containing was placed for (`block.unreached`) |
| line **boxes**, text runs, baselines | 10.5–10.7 | a **preliminary line** — a cursor, a top, a running height and a count — and `lines`/`wraps` as counts; text nodes are not boxes |
| declared **width**, `min-width`, `max-width`, `box-sizing`; percentages and relative units for both axes; shrink-to-fit | 10.8 | a block box fills its containing block (10.3), an atomic inline is sized by its own declarations where they resolve and fills its containing block otherwise (10.4), and a declared height is honoured where it is a computed `px` length |
| margin collapsing, padding, border geometry | 10.9–10.11 | the values are readable from the borrowed style; a box's content box *is* its border box until then, and collapsing has the transparency rule it needs in *both* directions — 10.3's transparent block container and 10.4's transparent inline box |
| positioned containing blocks, offsets | 10.12 | a box's containing block is its parent (10.3, `BlockLayout.md` §2); `display` and `position` are readable |
| overflow, damage, invalidation | 10.13–10.14 | two result flags that stay 0 even when a participant is placed past its line or a child sits outside its box, and an invalidation that withdraws geometry rather than keeping a stale conclusion |
| painting anything | Phase 11 | `painting` capability = 0 |
| reading a property other than `display` and a declared `width`/`height` on an atomic inline | 10.8+ | the borrowed style is there; the read is the later goal's |

Three of these are worth a sentence more.

**The viewport is an argument, and 10.3 made it the root's containing block.** The
window and display layers already know the number; RoboLayout takes it as an
argument and hardcodes nothing, which is why a layout pass can be tested without a
window. 10.1 recorded it as *available space* and no more, because whether a box
filled it was a formatting-context decision; 10.3 answers that question for the
block direction and uses the viewport as the width the root box fills.
`layout.context.viewport` asserts the recorded value and the `ROBO_NOT_FOUND` a
context with no viewport gives; `layout.block.no_viewport` asserts that such a
context places nothing and is not thereby an error.

**Almost nothing reads a box's style.** The seam is in place and the style is
borrowed and reachable, so a later goal asks for what it needs without a new header.
Between them the two directions read exactly three things — `display` (through
9.12's accessors), and a declared `height` for any box and a declared `width` for an
*atomic inline*, each one kind check and one unit check — and 10.1's refusal to
*pre-read* properties into fields still stands, because that is the second copy of a
fact §4 exists to avoid.

**Both directions are a *function of the classification*.** That is what makes an
invalidation's second half necessary: withdrawing a classification without
withdrawing the geometry derived from it would leave results marked `COMPUTED`
about a tree that no longer exists. §7.1 of `BlockLayout.md` is the argument;
`layout.block.invalidation` is the case. 10.4 is the same function asked the other
way round — its `wrap` and `union` arithmetic is a consequence of *which context*
a box participates in — so the rule was fixed once, in the shared path, rather than
twice.

---

## 16. What 10.1 found

Three of these were found by tests failing, and two of them were **production
bugs**, not test bugs. They are recorded because they are the argument for the
suites existing at all.

**1. A dangling parent when the arena grows.**
`robo_layout_tree_add_object` resolved the borrowed `parent` pointer *before*
calling the reserve that can `realloc` the arena. A tree that outgrew its initial
capacity therefore linked a new box to a freed address. It is invisible below the
first growth boundary and a segfault above it — found by
`layout.tree.growth`, which is the one test whose stated purpose is to push a tree
past a reserve. Fixed by resolving the parent *after* the reserve; the ordering is
now the first thing in the function.

**2. `passes` was not cumulative.**
It reported the last pass's count rather than the total, contradicting its own
header comment. Every single-pass test passed. `layout.context.run.repeated` was
written specifically to ask the question, and it is now the case that pins it.

**3. The test registry had a fixed cap.**
`runtime/Tests/Framework/RoboTest.c` held registered suites in a fixed-size array
that 7 new suites overflowed. Raised with the same "fail loudly rather than
truncate" discipline the rest of the runtime uses — a suite that is silently not
registered is a suite that is silently not run.

**4. Two Release-only build breaks, pre-existing.**
`SuiteRoboHTMLDom.c` had a debug-only variable whose only use is inside
`#if ROBO_MEM_DEBUG`, and two CSS suites asserted the Debug allocator's bookkeeping
unconditionally. The project's default configuration is Debug, so Release `-Test`
had never been exercised. All three are now guarded, and both configurations are
green — which means the phase is the first thing to have run the whole suite in
Release, and it found three defects that were nothing to do with layout.

**5. The `-Verify` boundary scan needed a RoboLayout arm.**
RoboCSS and RoboDisplay each had one; RoboLayout is the first module whose
forbidden set includes layers *below* it (window, display) for non-layering
reasons, so its scan is the first that is not purely "no upward edges". It has
already paid for itself: it is what makes the "layout pass must be testable without
a window" decision in §2 enforced rather than merely documented.

Later phases' findings are recorded with their phases rather than appended here:
10.2's in `FormattingContexts.md` §8, 10.3's in `BlockLayout.md` §7.1 and
`Tests.md` §8, 10.4's in `InlineLayout.md` §5 and `Tests.md` §9. The reason to keep
them out of this document is that this one describes what is *stable*; a finding is
a thing that changed.

---

## 17. What 10.5 onwards may assume

1. A layout tree exists, is separate from the DOM, and is walked in document order.
2. Every box has a valid id, a parent link consistent with its children, and a
   `depth` equal to its distance from the root.
3. Every box has a `display`, copied, and a borrowed style it may read — or
   `STYLE_MISSING`, which must be checked before reading one.
4. Every box has a result, and its **state** is the authority: `NONE` before any
   pass, `UNRESOLVED` when a pass did not place it, `COMPUTED` when the pass did —
   in **either** direction, because 10.3 and 10.4 are one pass.
5. After a pass with a recorded viewport, every block-level box in a
   block-direction context — and every participant of an inline formatting context
   — holds a real rectangle written in its **parent's** coordinate space: `x`/`y`
   are relative to the containing block's content origin, not to the viewport. A
   participant nested inside a transparent inline box has been **rebased** onto it,
   so the convention survives nesting.
6. A block box's width after a pass is its containing block's content width, and
   its height is a declared `px` length or the stacked extent of its children. An
   atomic inline is instead sized by its own declarations where they resolve, and
   falls back to the containing block's content width; a transparent inline is sized
   by *neither* and is the union of what its contents reached. All of it is replaced
   wholesale by 10.8.
7. An inline formatting context's height **is** the block extent of the box that
   owns it (`lines` and `wraps` are counts, and a preliminary line is not a line
   box — 10.6 owns that), which is why there is no second entry point beside
   `robo_layout_block_layout`: `robo_layout_inline_walk_contents` is internal and
   reached only by the block pass.
8. A box's content box *is* its border box, so its content origin is its top-left
   corner until 10.9–10.11 say otherwise; margins, padding and borders are readable
   and unapplied.
9. `robo_layout_context_run` is the only tree-mutating door in the module, and a
   later goal extends it rather than adding a second one.
10. Constraints can say "unset" honestly, and `clamp` is the only bounds arithmetic
    in the module.
11. The user-data seam is where scratch state goes; the context will not grow a
    field per formatting context.
12. An invalidation withdraws geometry along with the classification, so a caller
    that adds a box after a pass reads honest `UNRESOLVED` states rather than
    numbers computed with the previous box set. **The withdrawal is the rule, and
    it lives in one place** (`robo_layout_tree_contexts_reset`), because geometry
    is a *function of the classification* and three callers say the same thing.
13. `robo_layout_geometry_is_implemented()` is the single question a caller asks
    before reading geometry, and flipping it is a phase's *last* act — after the
    numbers it promises exist.
14. **Every live box participates in exactly one formatting context** once a pass
    has run, and `robo_layout_tree_is_classified` is how a caller knows a pass has.
    `robo_layout_object_context` answers where a box participates;
    `robo_layout_object_context_owned` answers what it establishes, which is `NONE`
    for most boxes and is not a failure.
15. **A context's parent is the one its owner participates in**, so the context
    tree's shape is a subsequence of the box tree's, and `depth` is real.
16. **`robo_layout_tree_invalidate_contexts` is how a classification is released**,
    and a build or an `add_object` releases it implicitly. A box's context id read
    across an invalidation answers `ROBO_NOT_FOUND` rather than resolving against
    the new classification.
17. **A transparent box establishes nothing**, and that is a fact 10.9 and 10.13
    depend on: a margin collapses *through* it and a float escapes it. A later goal
    that needs it to be a barrier is asking for an anonymous box, which is 10.3's
    machinery rather than a change here.
18. **Atomicity is derived, not stored.** A participant of an inline context is
    atomic exactly when it established a context of its own; there is no third flag
    and no "is this an inline-block" predicate in this module. The two facts come
    from 10.2's ids and 9.12's outer display, and the header states the rule as a
    table so 10.8 can re-derive it rather than re-decide it.
19. **A box's rectangle is in its containing block's space in both directions**,
    and for a participant inside a transparent inline box that required a rebase
    pass. A later goal that writes into a participant's position must respect that
    the rebase happens once, at the end of the context's walk, and that an atomic
    inline's contents are *already* relative to it.
20. **Every box with no geometry is explained by one of four counters** —
    `block.inline_level_skipped`, `block.unreached`, `inline.block_level_declined`,
    and "it generated no box at all". The identity
    `blocks_placed + inline_level_skipped + participants_placed + unreached ==
    objects` is asserted over every laid-out tree, so a build that dropped a box
    would be visible as a document that did not add up.
