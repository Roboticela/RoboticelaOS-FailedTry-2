# RoboLayout — architecture (Phase 10.1)

This is the design record for RoboLayout's architecture: what the module is, the
decisions that shaped it, the alternatives that were rejected, and the boundaries
Phase 10.2 onwards may not cross without changing this document.

It is not a tutorial. It is the thing a reviewer reads to answer "why is it like
this", and the thing a later goal reads to find out what it is allowed to assume.

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
                    (boxes, links, ids)                        │
                                                               ▼
                                              results: every box UNRESOLVED
```

What that buys the phases that follow: **every model they extend already exists**.
10.2 adds formatting contexts to an object kind that is already typed. 10.3 fills a
result that already has a state. 10.8 clamps through a constraints record that
already exists. 10.11 paints an edge whose width already came from 9.15.

What it does **not** buy is a single coordinate, and the module says so in three
independent places rather than leaving a caller to find out:

| Door | Answer |
|---|---|
| `ROBO_LAYOUT_GEOMETRY_IMPLEMENTED` | `0` — a compile-time constant |
| `robo_layout_capabilities_t.geometry` | `0` — a field in the capability struct |
| `robo_layout_geometry_is_implemented()` | `0` — a function a caller cannot read past |

Three doors for one fact is deliberate: the constant is what a later goal flips,
the field is what a feature query sees, and the function is what a caller that has
already forgotten to query the struct will call. `layout.conformance.no_geometry`
asserts all three agree, because three doors that can disagree is a worse failure
than one door.

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

## 10. The context, and what a 10.1 pass is

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

```text
walks the tree in creation order
   (which is document order, because the builder adds parents before children
    and children left to right)
marks every box UNRESOLVED
counts what it saw
computes no coordinate
```

`boxes_computed` is 0 after a pass and `robo_layout_geometry_is_implemented()`
answers 0, so a caller cannot mistake "the pass ran" for "the boxes were laid out".

It is not a placeholder standing in for a real pass. It is the pass 10.1 has, and
its whole output is a truthful statement about what has not been computed yet.

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

| Field | Meaning | 10.1 value |
|---|---|---|
| `objects` | boxes in the tree | the count |
| `visited` | boxes the pass reached | == `objects` |
| `boxes_computed` | boxes whose geometry is final | **0** |
| `boxes_unresolved` | boxes deliberately left without geometry | == `objects` |
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

`robo_layout_capabilities_t` is nine `int` fields in declaration order, and two of
them are deliberately 0:

| Field | 10.1 | Filled by |
|---|---|---|
| `object_model` | 1 | — |
| `constraints` | 1 | — |
| `result_model` | 1 | — |
| `context` | 1 | — |
| `dom_build` | 1 | — |
| `computed_style_link` | 1 | — |
| `geometry` | **0** | 10.8 |
| `formatting_contexts` | **0** | 10.2 |
| `painting` | **0** | Phase 11 |

A caller that checked only the version string would conclude that 0.1.0 lays boxes
out. `geometry` is the field that says it does not.

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

RoboLayout 10.1 contains **no** algorithm. What is absent is not unfinished; it is
the next fourteen goals, and each absence is deliberate:

| Absent | Belongs to | What exists instead |
|---|---|---|
| formatting contexts, block/inline boxes | 10.2–10.4 | one object kind, `BOX` |
| line boxes, text runs, baselines | 10.5–10.7 | text nodes are not boxes |
| width/height resolution, `auto`, percentages | 10.8 | typed constraints that can say "unset" |
| margin collapsing, padding, border geometry | 10.9–10.11 | the values are readable from the borrowed style |
| positioning, containing blocks, offsets | 10.12 | `display` and `position` are readable; nothing resolves |
| overflow, damage, invalidation | 10.13–10.14 | two result flags that stay 0 |
| painting anything | Phase 11 | `painting` capability = 0 |
| a containing block for the root | 10.2 | the viewport is an *argument*; the context records it |
| reading a property other than `display` | 10.3+ | the borrowed style is there; the read is the later goal's |

Two of these are worth a sentence more.

**The viewport is recorded as available space and as nothing else.** Whether a box
fills it is a formatting-context decision (10.2), so a context that also treated it
as a maximum would be answering 10.3's question. The window and display layers
already know the number; RoboLayout takes it as an argument and hardcodes nothing —
`layout.context.viewport` asserts both the recorded value and the `ROBO_NOT_FOUND`
a context with no viewport gives.

**Nothing reads a box's style except `display`.** The seam is in place and the
style is borrowed and reachable, so 10.3 can ask for `width` without a new header.
What 10.1 refuses to do is *pre-read* properties into fields, because that is the
second copy of a fact §4 exists to avoid.

---

## 16. What the phase found

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

---

## 17. What 10.2 onwards may assume

1. A layout tree exists, is separate from the DOM, and is walked in document order.
2. Every box has a valid id, a parent link consistent with its children, and a
   `depth` equal to its distance from the root.
3. Every box has a `display`, copied, and a borrowed style it may read — or
   `STYLE_MISSING`, which must be checked before reading one.
4. Every box has a result, and that result is `UNRESOLVED` after 10.1's pass.
5. `robo_layout_context_run` is the only tree-mutating door in the module, and a
   later goal extends it rather than adding a second one.
6. Constraints can say "unset" honestly, and `clamp` is the only bounds arithmetic
   in the module.
7. The user-data seam is where scratch state goes; the context will not grow a
   field per formatting context.
8. `robo_layout_geometry_is_implemented()` is the single question a caller asks
   before reading geometry, and flipping it is a phase's *last* act — after the
   numbers it promises exist.
