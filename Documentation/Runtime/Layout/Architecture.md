# RoboLayout — architecture (through Phase 10.15, Phase 10 complete)

This is the design record for RoboLayout's architecture: what the module is, the
decisions that shaped it, the alternatives that were rejected, and the boundaries
Phase 11 onwards may not cross without changing this document.

It is not a tutorial. It is the thing a reviewer reads to answer "why is it like
this", and the thing a later goal reads to find out what it is allowed to assume.

For the parts a later phase added, this document describes the *frame* — where a
context lives, what owns it, how a pass classifies and then lays out — and eight
documents are the records for the parts themselves: `FormattingContexts.md` for the
classification, `BlockLayout.md` for the block direction, `InlineLayout.md` for the
inline one, `TextLayout.md` for measurement and the text object's arrival,
`LineBoxes.md` for the line record, `Baselines.md` for the floor a line rests on,
`Sizing.md` for what a box's declarations turn into, `Margins.md` for the gap beside
a box and why it moves one without resizing it, `Padding.md` for the inset inside one
and why it resizes a box without moving it, `Borders.md` for the inset that finishes
the box model and is the only layer whose *used* value is not its declared one,
`Positioning.md` for the displacement that moves a box without resizing it,
`OutOfFlow.md` for the box a declaration takes *out* of flow entirely (14.0),
`Overflow.md` for the second rectangle a box gets when its contents do not fit,
`Invalidation.md` for the states that say which of those conclusions are still true,
`Tests.md` for the suites.

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
        LayoutTreeBuildFromDocument
                              │
                              ▼
                      the layout tree  ──────────►  LayoutContextRun
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
owns it. 10.8 clamps through a constraints record that already existed, and 10.9
resolves margins through the sizing layer's own seam rather than beside it. 10.10
adds no door of its own to either pass: it reads the four longhands, subtracts the
padding from what the margins left, and derives the border box from the sizing
layer's answer — one call in each direction, and the width/height question is left
where 10.8 put it. 10.11 is the same shape one layer out and closes the box model: it
subtracts the borders from what the margins left *before* the padding is subtracted
from that, adds them to the padding box in the other direction, and leaves the
width/height question exactly where 10.8 put it — so the two phases together turned a
one-inset box into a four-box model without touching the sizing layer's seam.

10.1 bought no coordinate, and the module said so in three independent places
rather than leaving a caller to find out. 10.3 flipped all three, and the shape of
that flip is the part worth keeping:

| Door | 10.1 / 10.2 | 10.3 | 10.4 | 10.5 | 10.6 | 10.7 | 10.8 | 10.9 | 10.10 | 10.11 | 10.12 | 10.13 | 10.14 | 12.17 | What it means now |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `RoboLayoutGeometryImplemented` | `0` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | — | a compile-time constant per direction, and the same one for both |
| `LayoutCapabilities.geometry` | `0` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | — | *this build* computes coordinates |
| `LayoutCapabilities.block_layout` | — | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | — | the block direction |
| `LayoutCapabilities.inline_layout` | `0` | `0` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | — | the inline direction |
| `LayoutCapabilities.block_and_inline_layout` | `0` | `0` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | — | derived: their conjunction |
| `LayoutCapabilities.text_layout` | — | — | — | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | — | this build can **measure a run**, not that a pass was given a source |
| `LayoutCapabilities.line_boxes` | — | — | — | — | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | — | this build can **produce a line a caller reads** |
| `LayoutCapabilities.baselines` | — | — | — | — | — | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | — | this build gives a line a **floor** and places its members on it |
| `LayoutCapabilities.sizing` | — | — | — | — | — | — | `1` | `1` | `1` | `1` | `1` | `1` | `1` | — | this build turns a declared `px` `width`/`height` into geometry — **not** that sizing is finished |
| `LayoutCapabilities.margins` | — | — | — | — | — | — | — | `1` | `1` | `1` | `1` | `1` | `1` | — | this build places a box at its **margin** origin and stacks by its children's margin boxes — **not** that collapsing is implemented |
| `LayoutCapabilities.padding` | — | — | — | — | — | — | — | — | `1` | `1` | `1` | `1` | `1` | — | this build's `size` is a **padding** box built on the sizing layer's answer, and an `auto` width fills what the margins and the padding left — **not** that `box-sizing: border-box` exists |
| `LayoutCapabilities.border` | — | — | — | — | — | — | — | — | — | `1` | `1` | `1` | `1` | — | this build's `size` is the **border** box, a side's used width is zero unless its style draws, and the whole record — width, style, colour — is readable for paint — **not** that `border-radius` exists, and not that a border paints |
| `LayoutCapabilities.positioning` | — | — | — | — | — | — | — | — | — | — | `1` | `1` | `1` | — | a `relative` box is **drawn** where its offset puts it while its size and its flow contribution are untouched, and the used offset is readable from the object — **not** that `absolute`/`fixed`/`sticky` place, and not that an **out-of-flow** percentage inset resolves (an in-flow one does since 19.2) |
| `LayoutCapabilities.overflow` | — | — | — | — | — | — | — | — | — | — | — | `1` | `1` | — | the contents that do not fit are **recorded beside the box** as four extents measured from its padding box, with a mode per axis — **not** that anything scrolls, clips a pixel, or turns `visible` into `auto` |
| `LayoutCapabilities.invalidation` | — | — | — | — | — | — | — | — | — | — | — | — | `1` | — | a box carries a **state** saying what about it is stale and a reason for a reader, a property name is classified into what changing it costs, and a pass can be skipped when nothing needs one — **not** that partial layout exists: the dirty root is computed and reported, and the pass still recomputes from the tree root |
| `LayoutCapabilities.font_request` | — | — | — | — | — | — | — | — | — | — | — | — | — | `1` | a computed style's font properties become one **value** request — an ordered, classified family list plus size, weight, stretch, slope and direction — an installed `LayoutFontProvider` answers its entries in author order, and the answering face is recorded on the run — **not** that layout owns a font database: no coverage walk, no fallback chain, no per-scalar splitting, no `@font-face`, and no provider means the single attached face |
| `LayoutGeometryIsImplemented()` | `0` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | `1` | — | a function a caller cannot read past |
| `LayoutResultState()` | — | — | — | — | — | — | — | — | — | — | — | — | `—` | — | *this box* has one — the per-box authority |

Three doors for one fact is deliberate: the constant is what a later goal flips,
the field is what a feature query sees, and the function is what a caller that has
already forgotten to query the struct will call.

**10.15 adds no column, and that is the phase's smallest claim rather than an
omission.** It is the first goal since 10.2 to put nothing on this surface — no door,
no enumerator, no capability field — because its subject is whether the fourteen layers
above are right *about each other*, which is a question about tests and not about the
build. The asymmetry 10.2's header named for formatting contexts applies here twice
over: a `layout_tests` field would invite a caller to ask whether the *build* is tested,
and there is no such thing to ask. What the phase does change is underneath the table:
the pass's own accumulations are now bounded the way every inset layer's already were
(item 34), so a saturated extent is a rectangle a renderer can draw rather than a
negative one the sizing layer refuses.

But every answer in that table is about the **build**, and 10.3 made that
distinction load-bearing rather than pedantic. `geometry == 1` is true of a tree
whose boxes are all `UNRESOLVED` — a context that recorded no viewport placed
nothing, and a `<div>` whose content is a text node is zero tall. So the per-box
`state` is the authority a consumer reads, and the capability is the answer to "does
this build lay boxes out at all". `layout.conformance.no_viewport` asserts the two
side by side over one tree, because a build that conflated them would have failed
exactly there.

10.5 and 10.6 are the same distinction asked twice more, and each time about a
*seam* rather than a direction: `text_layout == 1` while a pass given no metrics
source measures every run as zero and counts it, and `line_boxes == 1` while a
document with no inline content produces no lines at all. 10.8 asks it about a
*value* rather than a seam, which is why its capability is worded as narrowly as it
is: `sizing == 1` is true of a build in which `width: 50%` is read, recorded
`UNRESOLVED`, and filled from the containing block. "Are there lines?" is
`LayoutTreeLineCount`'s; the capability says the module can *produce* them.
10.9 asks it one level further out still: `margins == 1` is true of a build that adds
adjacent margins rather than collapsing them, so the field's wording had to name what
it *does* — a margin origin, an `auto` distribution, a margin-box stack — instead of
the CSS notion it is one rule short of. Those are the two phases that retired the
paragraph which used to stand here about a
`<span>` holding only text being zero-wide, and what replaced it is not a bullet but
the measurement existing — the same way 10.3 replaced `no_geometry` with
`no_viewport` rather than deleting it.

10.10 asks it about a *model* rather than a seam or a value, and that is a fourth kind
of narrowness: `padding == 1` is true of a build whose `size` is a **border** box and
whose contents begin `padding-left`/`padding-top` inside it, while
`box-sizing: border-box` does not exist, a `padding: 5%` is read, counted
`UNRESOLVED` and used as zero, and a **transparent inline box** is not inflated by
one. What the field names, then, is the two facts a caller may rely on — the size on a
result is the box a reader sees, and the content box is recoverable from the object —
rather than the property it is named for.

10.11 asks it about a **rule** rather than a seam, a value or a model, and that is the
fifth kind of narrowness: `border == 1` is true of a build in which a side's *used*
width is zero unless its `border-style` generates a border, so `border: 5px none`
costs no geometry at all — and in which the whole record a paint pass needs (used
width, style and colour per side) is on the object, so Phase 11 re-reads no
declaration. What it does **not** say is that a border paints: `painting` is still 0,
and `border-radius`, `hidden`'s collapsing meaning and any border on a transparent
inline box are each a later goal's. This is the one capability in the struct whose
claim includes a CSS **rule** rather than the reach of a reader, which is why the
suite asserts it against the *style vocabulary* (`layout.borders.side_record` walks
all ten) rather than against a representative.

10.12 asks it about **where** rather than about **how big**, and that is the sixth
kind of narrowness: `positioning == 1` is true of a build in which a relatively
positioned box is drawn where its offset puts it *and* the following sibling starts
exactly where flow says — the pair is the claim, and a build that moved the sibling
would satisfy the first half and fail the second. What it does **not** say is that
`absolute`, `fixed` or `sticky` place (their facts exist, 9.13; placing one is 18.5's),
that an **out-of-flow** percentage inset resolves, or that any stacking order moves; the positioning
suite asserts it against the *policy* (`layout.positioning.flow_preserved` places the
box 30/20 away and the next box exactly where it would have been).

10.13 asks it about **what the contents did to the space around the box** rather than
about the box at all, and that is the seventh kind of narrowness: `overflow == 1` is
true of a build that measures four extents against a box's padding box and records
them **beside** an unchanged size — and false of the tempting build that grows the box
instead, which would satisfy every flattering reading of the word "overflow" and be
wrong in the one place it matters. What it does **not** say is that anything scrolls
(no scrollport, no scrollbar, no scroll offset, and `overflow: auto` is not a mode this
build has), that CSS's axis promotion applies (`overflow-x: visible` beside a clipping
axis stays `visible` here), that a pixel is clipped anywhere, or that a **text run** or
a **transparent inline box** has a record of its own — the object door declines both,
and both still contribute their region to their parent. The suite asserts the field
against the *pair* (`layout.overflow.pass_size_unchanged` measures a 200px child inside
a 100px box and asserts the unchanged width and the 100px extent together), because
the two halves are the claim.

10.14 asks it about a **conclusion's shelf life** rather than about a box, and that is
the eighth kind of narrowness: `invalidation == 1` is true of a build that can *say*
what about a box is stale, classify a property name into what changing it costs, and
skip a pass when nothing needs one -- and false of the build the word invites, the one
that lays out *less*. Nothing about any coordinate changes in this phase: the dirty
root is computed and reported (`LayoutTreeInvalidationRoot`) and the pass still
recomputes from the tree root, because the brief that asks for invalidation also
forbids the incremental recursion that would make it faster, and a build that guessed a
dirty region would be fast and wrong. What it does **not** say is that a partial pass
exists, that a dependency graph exists, that anything is multithreaded, or that paint
invalidation exists beyond the one `STYLE` bit a paint phase would consume. The suite
asserts the field against the *shape of the dirty set* rather than against a saving
(`layout.invalidation.position_dirty_is_narrow` marks one box and asserts that its
following sibling is still clean), because the model is the claim and the skip is only
what the model is for.

12.17 has the ninth kind, and it is the first one that is about a **declaration this
module did not author**: `fontRequest == 1` is true of a build that turns five computed
font properties into one value record and offers it to a provider the caller installs,
and false of the build the word invites — the one that *knows* which face a family is.
Layout keeps the request and the seam; the answer belongs to whoever owns the font
database, which is why `LayoutFontProvider` is a pair of a function and an opaque
pointer and not a registry this module links. A caller with one system face installs
nothing and every run is measured with that face, exactly as before the phase — so
the claim is about a *seam*, not about a feature being on. What it does **not** say is
that layout walks coverage, chooses a fallback chain, splits a run per script, reads
`@font-face`, or synthesizes a weight or a slope the face does not have. The suite
asserts it against the *shape of the walk* rather than against a resolution
(`layout.fontselect.fallback` offers two families to a provider that answers only the
second and asserts the count of asks as well as the answer), because the order and the
number of questions are what the seam promises.

10.15 has no ninth kind of narrowness, and the absence is the phase: it adds no field to
this table at all, because what it asks is whether the fourteen answers above are
*consistent with each other* — a question whose answer is a suite rather than a flag.
What it does add to the module is a *rule* rather than a claim (item 34, §17): the pass's
own accumulations are bounded exactly as the inset layers' are, so the negative extent
that a tall document used to hand the sizing layer is unreachable. A phase that says
nothing new about a box but makes a promise about all of them is what a consolidation
goal should look like, and it is the reason the flag table above has fourteen columns and
will never have a fifteenth.

10.4 added a field to that table rather than changing one, and the shape of the
addition is the part worth keeping: `inline_layout` was written as `0` in **10.2**,
with a field and no code behind it, and flipped only when the code existed. The
constant `RoboLayoutInlineLayoutImplemented` did not exist until then, which is
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
| RoboBase, RoboMem, RoboGraphics (geometry only), RoboHTML, RoboCSS | RoboWindow, RoboDisplay, RoboPaint, RoboRender, RoboCompositor, RoboJS, RoboShell, RoboAPI, Chromium |

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
  borrowed `LayoutObject *` for one call — the whole struct at once, which
  is what makes a walk of a thousand boxes a thousand lookups rather than six
  thousand — and that pointer is invalidated by the next `add_object`. A caller
  that must keep a box keeps its id.
- **Restarted by `clear`**, exactly as RoboHTML's node ids are, and for the same
  reason: a tree is rebuilt wholesale, and a stale id must not alias whatever box
  now occupies that slot.

The builder's build is therefore *replace*, not *merge*: `LayoutTreeBuildFromDocument`
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
| `kind` | `RoboLayoutObjectBox` and, since 10.5, `RoboLayoutObjectText`. Shipping `BLOCK` / `INLINE` / `LINE` enumerators would be claiming a distinction the tree cannot make — the tree knows a box's `display` and the context it established, which is where that distinction already lives |
| `dom_node` | An inert RoboHTML **handle** — a token, not a pointer. Asking "which element is this box?" is a comparison; asking "which box is this element?" is a lookup |
| `style` | A **borrowed** `CssComputedStyle *`. The tree never disposes it, never copies it, and never parses CSS to make one |
| `display` | **The one CSS value that is copied.** The box would not exist without it — the builder creates a box exactly when the computed display generates one — and the first question a formatting context asks is what kind of box it has, before it has any reason to reach into a style it may not have |
| `parent`, `first_child`, `last_child`, `prev_sibling`, `next_sibling`, `child_count` | Ids, not pointers, for the reason in §3.1. A parent whose children are linked from both ends is what makes "append in document order" O(1) and a reverse walk possible |
| `depth` | Distance from the root box, which is 0. Cheap here (it is one increment while building) and awkward later (it is a walk) |
| `flags` | `STYLE_MISSING`, and the argument for it being a flag rather than an inference is §4.1 |
| `result` | By value, because a result is a record with no allocation, and because a box without a result would be a second lookup on the hot path of every pass |

Everything else about the element — `position`, border widths, the background, the
insets — is read from the borrowed style when a later goal needs it. That is what
keeps the number of copies of any fact at one. The two records that are kept on the
object instead (`margins`, `padding`) are not copies of declarations: they are the
**used values** 10.9 and 10.10 resolved, kept beside the box because they are
conclusions a result cannot carry rather than facts a style already holds.

### 4.3 A text object, and why the vocabulary widened only here

10.5 added the second kind and it is worth recording why *that* was the phase that
did it, because three phases before it had refused. A box's kind would have to be
`BLOCK` or `INLINE` and the tree already knows both facts — the computed `display`
is on the object and the context it establishes is on 10.2's arena — so a second
kind would be a second copy of an answer. A **text node is not an element**: it has
no display declaration to derive from, no element for `dom_node` to reference, and
no declaration for the cascade to have filled. That is exactly the condition 10.1's
record named as the one that justifies a second kind, and it is the only one in the
module.

What a text object borrows is its **parent element's** computed style, which is
what CSS says a text node has and what the style seam already answers for an
element. Its `display` is derived as inline rather than stored, and
`robo_layout_object_box` refuses it, so an object of the new kind cannot be mistaken
for one with an element behind it.

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
| a parent id must name a live box **in this tree** | `RoboNotFound` |
| a parent id of `NONE` only when the tree is empty | `RoboAlreadyExists` for a second root |
| the element handle must be well formed | `RoboInvalidArgument` |
| the display must generate a box | `RoboInvalidArgument` |
| no undefined flag bit | `RoboInvalidArgument` |

The display check is the interesting one. Creating a box for a `display: none`
element would make the tree a **copy of the DOM's element list** instead of the
thing layout walks — and it would do it silently, because a `none` box is perfectly
well-formed as a struct. The tree refuses it at the only door that can.

---

## 5. The builder's rules

```text
1. an element generates a box unless its computed display says otherwise
2. a text node generates a TEXT object unless it holds only collapsible whitespace
```

Rule 1 was the only one through 10.4. Rule 2 arrived with 10.5 and its exception is
narrow on purpose: whitespace-only text between two elements is DOM structure rather
than content, and nothing in this module collapses whitespace yet, so an object for
every newline in a formatted document would put a participant on the line for the
document's indentation. The predicate is
`LayoutTextIsCollapsibleWhitespace`, which is a **module** door rather
than a suite's, because the builder and the eventual collapsing rule have to agree
about what counts; the builder counts what it skipped
(`whitespace_nodes_skipped`) so that "no text object" and "no text" stay two
different quantities.

Rule 1's consequences, each of which was a decision:

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
`elements_seen > 0, boxes_created == 0` and returns `RoboOk`, because "this
document has nothing to lay out" is an answer. It is also the case that makes an
empty pass legitimate — `LayoutContextRun` reports zero objects rather than
an error — and `layout.build.root_display_none` is the case that pins it.

The build report is the fourth thing it produces and it exists because the tree
alone cannot distinguish two situations a caller cares about:

| Question | Where the answer is |
|---|---|
| how many boxes are there? | `LayoutTreeObjectCount` |
| how many elements were there? | `build_report.elements_seen` |
| how many elements generated no box? | `build_report.skipped_display_none` |
| which box is this element's? | `LayoutObjectIdForDomNode` — `NONE` means "none", and the report is what tells you *why* none |

---

## 6. The style seam

RoboLayout consumes computed styles. It never parses CSS, never holds a
stylesheet, never normalises a value, and never knows a property name beyond
`display`. The whole boundary is one callback and one struct:

```c
typedef const CssComputedStyle *(*LayoutStyleFn)(
    void *userdata,
    const HtmlNodeHandle *element);

typedef struct LayoutStyleSource {
    void *userdata;
    LayoutStyleFn computed_style_for;
} LayoutStyleSource;
```

### 6.1 Why a callback and not a `CssComputedStyle *`

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

`LayoutStyleDisplay` reads `display` out of a style using 9.12's own
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

RoboCSS identifies an element by an opaque `CssElementRef` and never
dereferences it. The bridge answers with the node id, and resolves it back with
`HtmlDocumentNodeById`.

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

`LayoutStyleSource` stores the pointer it is given, so *something* must own
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

`RoboLayoutMaxClassesPerElement` (32) and
`RoboLayoutMaxAttributesPerElement` (64) **fail the refresh loudly**. A bridge
that silently dropped the thirty-third attribute would turn `[data-x]` into a
*wrong* answer instead of an *unsupported* one. The numbers are far above anything
a document this runtime builds uses and far below anything that would make the
per-element storage large.

---

## 8. Constraints: unset is a flag, not a sentinel

```c
typedef struct LayoutConstraints {
    LayoutSize available;
    LayoutSize minimum;
    LayoutSize maximum;
    int has_available;
    int has_minimum;
    int has_maximum;
} LayoutConstraints;
```

The obvious alternative is to write "unbounded" as a huge number. It is wrong for a
reason that shows up immediately: **an unbounded height and a height nobody has
worked out yet produce the same arithmetic and are different answers.** A caller
that has not been told the available width must be able to say so, because there is
no containing block in 10.1 to ask, and inventing one is precisely the failure this
phase exists to avoid.

The read doors follow the same discipline: `LayoutConstraintsAvailable`
answers `RoboNotFound` when the field was never set, which is a different answer
from a recorded **zero**. A recorded zero is a legitimate, known zero — a box in a
collapsed container has one — and that is why `set_available` accepts a zero size
and refuses a negative one.

**A negative size is refused, never reinterpreted.** `available = -1` is a caller
bug, and the call says so. For the same reason nothing here clamps an input into
range silently.

### 8.1 What `clamp` is, and what it deliberately is not

`LayoutConstraintsClamp` applies the recorded minimum and maximum to a size
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
problem once both exist. CSS resolves the conflict in the minimum's favour, and
10.8's sizing layer inherits that answer rather than re-deciding it.

So `LayoutConstraintsIsValid` is the predicate that lets 10.8 *see* the
conflict and lets a caller refuse a nonsensical pair up front, and `clamp` answers
`RoboNotFound` rather than handing back a size no one can defend — which is why
`layout_sizing_clamp` delegates to it instead of comparing two numbers itself
(`Sizing.md` §6).

What is absent from this header, and must stay absent: percentage resolution,
`auto` filling, intrinsic sizing, `box-sizing`. 10.8 resolves a declared `px` length
and fills `auto` from the containing block, but it does all of that **through** the
constraints record rather than by growing one. If the arithmetic grew here, every
later goal would have two places to look. (19.2 *did* add percentage resolution, but
it lives in the sizing layer's own branch and the four property vocabularies, not in
the constraints record: a percentage is a *ratio* recorded on the dimension, and the
clamp still sees only numbers — see `PercentageResolution.md`.)

---

## 9. Results: a state, not a rectangle

```c
typedef enum LayoutResultState {
    RoboLayoutResultNone = 0,       /* no result at all */
    RoboLayoutResultUnresolved = 1, /* a box exists; nobody computed it */
    RoboLayoutResultComputed = 2    /* position and size are final */
} LayoutResultState;
```

### 9.1 Why not just a `GfxRect`

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
  10.1 does not compute it; it states what the number will mean. 10.10 is the phase
  that made the statement true of a document that declares a padding: `size` is now
  the content box plus the padding, and the content box is recoverable from the
  object rather than re-derived from a style.

Stating the convention now is the whole value: a convention agreed after four goals
have written code is a migration.

### 9.3 Flags are validated, and there are two of them

`OVERFLOW_X` and `OVERFLOW_Y` exist because the fact a consumer needs about a box
that does not fit is **which direction** it overflowed — a scrollable axis is a
different box from a clipped one. Both stay 0 in 10.1, and their existence meant
10.13 filled them in rather than changing the struct's layout — which is what
happened: both are set by the block and inline passes from a *measured* extent, so a
box whose declared height is smaller than its contents sets no bit until something is
actually outside it. This paragraph is kept as 10.1 wrote it.

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
`LayoutGeometryIsImplemented()` answered 0, so a caller could not mistake
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
Result LayoutContextRun(LayoutContext *context,
                                      LayoutTree *tree,
                                      LayoutDiag *diag);
```

A pass **writes**: a box's result is where a pass puts what it concluded.
`LayoutContextRun` is the one component in the module that mutates a tree,
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
typedef GfxPoint  LayoutPoint;
typedef GfxSize   LayoutSize;
typedef GfxRect   LayoutRect;
typedef GfxRectF LayoutRectF;
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
`Result`, and an optional **caller-owned** record refines the failure with a
domain and a message. There is no global mutable error state, so a failure in one
thread cannot overwrite the description of another.

`RoboLayoutDomainCount` is 11, and `layout.module.diagnostic_domains` walks the
enum walking the registry against it — so a domain added later without a name is a
visible omission rather than a silent gap.

---

## 13. The capability model

`LayoutCapabilities` is twenty-six `int` fields in declaration order, and one
of them is deliberately 0:

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
| `text_layout` | 1 | 10.5 |
| `line_boxes` | 1 | 10.6 |
| `baselines` | 1 | 10.7 |
| `sizing` | 1 | 10.8 |
| `margins` | 1 | 10.9 |
| `padding` | 1 | 10.10 |
| `border` | 1 | 10.11 |
| `positioning` | 1 | 10.12 |
| `out_of_flow` | 1 | 14.0 |
| `overflow` | 1 | 10.13 |
| `invalidation` | 1 | 10.14 |
| `font_request` | 1 | 12.17 |
| `intrinsic_sizing` | 1 | 19.1 |
| `percentage_resolution` | 1 | 19.2 |
| `flex_layout` | 1 | 19.3 |
| `painting` | **0** | Phase 11 |

The table is in declaration order rather than in the order fields landed, so the
positional walk below stays a walk of the struct and not of a history.

`intrinsic_sizing` is 19.1's field and its wording is as narrow as the sizing one:
it says this build can **measure the contents** — min-content (the widest word),
max-content (the whole content on one line) and the fit-content clamp between them,
through the metrics seam rather than a character count — and that an `INTRINSIC`
dimension resolves from that measurement with `RoboLayoutDimensionSourceIntrinsic`.
It does **not** say intrinsic height as a function of width (that is line breaking),
replaced-element intrinsic dimensions (an image-loading seam that is 17.5's),
or grid/table intrinsic sizing (19.4, 19.6, each of which consumes these two
numbers). 19.3's flex pass *is* a consumer and is built: an item with no
`flex-basis` and no definite main size takes its measured content size as its flex
base (`FlexLayout.md`). A build with the sizing record and no measurement of what is inside it
would have satisfied every field above it, which is why the field exists
(`IntrinsicSizing.md`).

`percentage_resolution` is 19.2's field, and its wording is as narrow as the one
above: it says a `<percentage>` keeps its **magnitude** — as its own kind on a
dimension, a margin, a padding and an inset — and resolves against the containing
block's own dimension once a pass hands the base over. Every margin and padding
percentage takes the containing block's **width** (CSS's rule, though a vertical
margin would look right against the height in a square box), `width`/`height` take
the matching extent, and `left`/`right` take the width while `top`/`bottom` take the
height. A percentage with no base degrades to `UNRESOLVED` and is used as zero. It
does **not** say grid-track percentages (19.4), percentage `min-width`/`max-width`
bounds, a percentage inset on an **out-of-flow** box (14.0's contract, left untouched),
`calc()` or a viewport unit (`PercentageResolution.md`). A percentage `flex-basis` is
19.3's and is resolved by the flex pass against the container's main size when that is
definite (`FlexLayout.md`).

`flex_layout` is 19.3's field, and its wording is as narrow as the two above: it says a
box whose **inner** display is `flex` (`display: flex` or `display: inline-flex`) lays
its in-flow children out as **flex items** along a main axis — the direction from
`flex-direction` and the inherited `direction`, the base size from `flex-basis`/the
main-size property/the measured content, the **automatic minimum** from the item's
content when `min-width`/`min-height` is `auto`, `flex-grow`/`flex-shrink` distributing
free space with the min/max clamp and freezing, `flex-wrap` breaking lines, an `auto`
margin absorbing free space on its axis, and `justify-content`/`align-items`/
`align-self`/`align-content` positioning them (with `baseline` lining a row's first
baselines up and `stretch` growing an `auto` cross size) — and establishes a
`RoboLayoutFcFlex` context of its own. It does **not** say "flexbox is finished": there
is no flex **root** box (the tree root establishes the initial block context whatever
it computes to), no `baseline` or reversed inline axis on a **`column`**, no content
floor for a **scroll container**, no `gap`, no shorthand expansion, no replaced
elements and no vertical writing mode (`FlexLayout.md`).

A caller that checked only the version string would conclude that 0.1.0 lays boxes
out. The three doors §1 lists answer that together: `geometry` says **the build**
computes coordinates; `block_layout` and `inline_layout` say **which directions**;
and `block_and_inline_layout` is their conjunction, kept as a derived, *wider*
question rather than retired now that both halves exist — a caller asking "is layout
done?" should have one field to read that cannot be mistaken for "is half of it
done?". A phase that implemented one direction and flipped the conjunction instead
would have named a feature it did not have, which is exactly why 10.3 wrote it as 0
while only the block direction existed.

`text_layout` and `line_boxes` are **not** a third and fourth direction, and neither
is derived from the other. Measuring a run and producing a line are independent
claims: a build could measure text and place none of it in a line — which is what
this module was before 10.6 — and a line of atomic inlines and no text at all is a
line this build produces without measuring anything. `line_boxes` is also the
narrowest claim in the struct, deliberately: a line here has identity, geometry,
membership and an ordinal, and does **not** yet have a baseline, a `line-height` or
a font-derived extent. Naming the field for "text layout is finished" would have
claimed those.

`layout.conformance.capabilities` walks the struct **positionally** against a list
of names written out by hand, and pins the struct's size against the list's length.
So a field added without an assertion is a compile-time or test-time omission
rather than a silently untested flag — which is the failure that motivated the
conformance suite in 9.16, one level up.

---

## 14. Ownership and lifetime, in one table

| Object | Owned by | Released by | Outlives |
|---|---|---|---|
| `LayoutTree` and its boxes | the caller | `LayoutTreeDestroy` | — |
| a borrowed `LayoutObject *` | nobody | the next `add_object` | one call |
| `LayoutConstraints` | the caller, **by value** | — (no allocation) | — |
| `LayoutResult` | the box it is embedded in, by value | — (no allocation) | — |
| `LayoutContext` | the caller | `LayoutContextDestroy` | — |
| the styles a tree borrows | the **style supplier** | the supplier | the tree |
| the style supplier (`LayoutDom`) | the caller | `LayoutDomDestroy` | **every tree built from it** |
| `LayoutStyleSource` | the caller, by value | — | one build call |
| `HtmlNodeHandle` | by value, inert | — | as long as the document |
| a computed style owned by the bridge | the bridge | `LayoutDomDestroy`, or the next `refresh` | the tree that borrowed it |

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
| ~~baselines~~ **done in 10.7**; still absent: `line-height` as a resolved used value, alignment, `text-align`, `vertical-align` | 10.7, and a later goal for the rest | every line carries an ascent, a descent and the kind they came from, its height **is** their sum, and every member is placed from them (`Baselines.md`); what is not claimed is a `line-height` computation, a resolved used value, or any of the alignment properties |
| text **shaping** — glyph substitution, positioning, ligature, kerning, bidi reordering, Unicode line breaking, whitespace collapsing, fragmentation | Phase 14, and 10.6's successor for the last two | a run's width is the **sum of its glyphs' advances**, which is what an unshaped run is; a participant too wide for its line **overflows** rather than breaking, guarded by `cursor > 0`; `LayoutTextIsCollapsibleWhitespace` is the predicate the builder applies and the one a collapsing rule will have to agree with |
| `min-width`, `max-width`, `min-height`, `max-height` as **properties** (neither 9.11 nor a later CSS goal has registered them yet); `box-sizing`; ~~percentages~~ **done in 19.2** (a percentage `width`/`height`, `margin`, `padding` and inset keep their magnitude and resolve against the containing block's own dimension, degrading to `UNRESOLVED` when there is no base — `PercentageResolution.md`); relative units; **percentage min/max bounds**; applied shrink-to-fit | 10.11+, and a RoboCSS goal for the properties | the sizing layer *has* a minimum and a maximum and clamps through them (`Sizing.md` §6), so the arrival of the properties is a read rather than a redesign; `box-sizing: border-box` is one branch at the place 10.10 hands the layer a number; ~~intrinsic sizing~~ **done in 19.1** — min-content/max-content/fit-content are measured through the metrics seam and resolve as a fourth dimension kind, so what is still absent is intrinsic *height* as a function of width (line breaking), replaced-element dimensions, and grid/table intrinsic sizing (`IntrinsicSizing.md`; 19.3 consumes the two numbers for a flex item's base size — `FlexLayout.md`) |
| ~~declared **width** and **height**~~ **done in 10.8**; still absent: a declared size on a **block** box beyond a `px` length and a percentage, and `width` on a text object | 10.11+ | a block box lays its children out **into** its resolved width and is measured against its declared height (10.8); an atomic inline resolves width, contents and height in that order; since 19.2 a `%` keeps its magnitude as its own dimension kind and resolves against the containing block (or degrades to `UNRESOLVED` when there is no base) rather than being filled from it (`PercentageResolution.md`) |
| margin **collapsing** | a later goal | margins are *summed* and placed (10.9): a box sits at its margin origin, a horizontal `auto` distributes the leftover beside a declared width, and a parent stacks its children by their margin boxes — with the transparency rule collapsing needs already in *both* directions (10.3's transparent block container and 10.4's transparent inline box) and the four used values in one record, so collapsing becomes a rule **over those numbers** rather than over declarations (`Margins.md` §6) |
| ~~padding~~ **done in 10.10**; still absent: `box-sizing: border-box` and padding on a **transparent inline box** (a percentage padding **done in 19.2**: it resolves against the containing block's width) | a later goal each | a box's `size` is its **padding** box built on the sizing layer's answer, its contents begin inside it, an `auto` width fills what the margins and the padding left, and the used padding is readable from the object (`Padding.md`) |
| ~~border geometry~~ **done in 10.11**; still absent: `border-radius` and any corner geometry, `hidden`'s border-collapsing meaning (a table rule this build has no tables for), logical border properties, and a border on a **transparent inline box** | a later goal each | twelve longhands are read, a side's *used* width is zero unless its style draws, the border box is what a result carries, and the whole per-side record — used width, style, colour — is readable through `LayoutObjectBorders` so a paint pass re-decides nothing (`Borders.md`) |
| ~~basic **`position: relative`**~~ **done in 10.12**; still absent: `sticky` placement, `z-index` and stacking contexts (percentage insets **done in 19.2**: `left`/`right` resolve against the containing block's width and `top`/`bottom` against its height through `LayoutPositionResolveAgainst`) | a later goal each | the mode and four insets are read, resolved to one displacement by a stated precedence rule (`left` over `right`, `top` over `bottom`, an `auto` saying nothing), and applied so a `relative` box is drawn where its offset puts it while its size and flow contribution are untouched; the used offset is readable through `LayoutObjectPosition` so the flow position is `final − offset` (`Positioning.md`) |
| ~~out-of-flow **`absolute` and `fixed`** placement~~ **done in 14.0**; still absent: `z-index`, stacking contexts and paint order (nothing is reordered), `sticky`, a scrollport, `shrink-to-fit`, the static-position rule, percentage insets, and an out-of-flow **inline-level** box | a later goal each, and the shell that needed it is Phase 14 | a box is taken out of flow, placed against the nearest positioned ancestor's **padding** box or the initial containing block (always, for `fixed`), sized between two opposing insets when the declared dimension is `auto`, and positioned by its **far** side when the near side says nothing — which is why the origin is a second call that knows the box's own size (`OutOfFlow.md`) |
| ~~overflow~~ **done in 10.13**; still absent: everything that *acts* on it — scrolling, scrollbars, a scroll offset, `overflow: auto`/`scroll`, CSS's axis promotion, `text-overflow`, and any pixel clipping | the scrolling goals, and Phase 11 for clipping | the two flags are set from a **measured** extent beside a record of four extents measured from the padding box, and both the region a parent folds in and the clip rectangle a paint pass will draw against are readable through `LayoutObjectOverflow` and the overflow doors (`Overflow.md`) |
| damage, invalidation | a later goal | an invalidation that withdraws geometry rather than keeping a stale conclusion |
| painting anything | Phase 11 | `painting` capability = 0 |
| reading a property other than `display`, a declared `width`/`height`, the four margins, the four paddings, the twelve border longhands, `position` with its four insets, `overflow-x`/`overflow-y` with the `overflow` shorthand, and 18.3's flex longhands (`flex-direction`, `flex-wrap`, `flex-grow`, `flex-shrink`, `flex-basis`, `order`, `justify-content`, `align-items`, `align-self`, `align-content`) | a later goal | the borrowed style is there; the read is the later goal's, and since 10.8 through 10.13 (and 19.3 for the flex family) a read goes through the sizing, margin, padding, border, position, overflow or flex layer rather than into a result record |
| **flex** and **grid** and **table** layout beyond 19.3 | 19.4, 19.6 | a box whose inner display is `flex` establishes a `RoboLayoutFcFlex` context and has its in-flow children placed by the flex pass (`FlexLayout.md`); `grid` and the table display types are still refused by 9.12's vocabulary, and only flex is on this list's own account |

Three of these are worth a sentence more.

**The viewport is an argument, and 10.3 made it the root's containing block.** The
window and display layers already know the number; RoboLayout takes it as an
argument and hardcodes nothing, which is why a layout pass can be tested without a
window. 10.1 recorded it as *available space* and no more, because whether a box
filled it was a formatting-context decision; 10.3 answers that question for the
block direction and uses the viewport as the width the root box fills.
`layout.context.viewport` asserts the recorded value and the `RoboNotFound` a
context with no viewport gives; `layout.block.no_viewport` asserts that such a
context places nothing and is not thereby an error.

**Almost nothing reads a box's style.** The seam is in place and the style is
borrowed and reachable, so a later goal asks for what it needs without a new header.
Between them the two directions read exactly four things — `display` (through
9.12's accessors), a declared `width` and `height` on any box, and that box's four
margins — and 10.1's refusal to *pre-read* properties into fields still stands,
because that is the second copy of a fact §4 exists to avoid. 10.8 kept the refusal
and moved the *read*: the two directions no longer look at a length themselves, they
hand the borrowed style to `LayoutSizingResolve` and read the answer
(`Sizing.md` §5). 10.9 did the same for the margins, through
`LayoutMarginsOf` (`Margins.md` §2 for the kinds it classifies). The count of
properties read went from
three to six; the number of places that interpret one went from two to one, and then
from one to two — because a margin is not a size and the two reads answer two
different questions rather than one question twice. 10.10 read the four paddings
through `LayoutPaddingsOf` (`Padding.md` §2), and its count is the one that
finished the sentence 10.9 started: the reader is a third, because a padding is
neither a size nor a margin — it is the same *property family* as a margin read
against a different number, and the two numbers it derives (the width left for a
box's contents, and the border box a result carries) are answers about the **box**
rather than about its placement. 10.11 read the twelve border longhands through
`LayoutBordersOf` (`Borders.md` §2) and is the last reader in this family:
the count of properties read is now `display`, `width`, `height`, eight box insets
and twelve border longhands, and the number of places that interpret one is four —
one per layer that owns a property, which is the rule this module applies everywhere
else. What changed with the fourth is the *shape* of the answer: a sizing read
decides a dimension, a margin read distributes space and a padding read passes a
number through, while a border read decides whether the declaration is used at all.

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
`LayoutTreeAddObject` resolved the borrowed `parent` pointer *before*
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
`#if RoboMemDebug`, and two CSS suites asserted the Debug allocator's bookkeeping
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
`Tests.md` §8, 10.4's in `InlineLayout.md` §5 and `Tests.md` §9, 10.5's and 10.6's
in `Tests.md` §10–§11, 10.7's in `Baselines.md` §12 and `Tests.md` §12, 10.8's
in `Sizing.md` §8 and `Tests.md` §13, 10.9's in `Margins.md` §8 and
`Tests.md` §14, 10.10's in `Padding.md` §8 and `Tests.md` §15, 10.11's in
`Borders.md` §8 and `Tests.md` §16, and 10.12's in `Positioning.md` §10 and
`Tests.md` §17, 10.13's in
`Overflow.md` §11 and `Tests.md` §18, and 10.14's in `Invalidation.md` §11 and
`Tests.md` §19. 10.15's have no design record of their own, because the phase *is* a
test record: they are `Tests.md` §20, and the one of them that changed production code
is stated a second time as item 34 below. The reason to keep
them out of this document is
that this one describes what is *stable*; a finding is a thing that changed.

---

## 17. What Phase 11 onwards may assume

*(Written as "10.7 onwards" when 10.6 closed and re-headed by 10.7, then by 10.8,
which left items 1–5, 7–9 and 11–22 as they were and replaced 6: a box's `size` no
longer has one rule but a three-kind vocabulary, and the reader who follows item 6
into the module should read `Sizing.md` first. Item 10 gained a sentence and item 18
lost its future tense. 10.9 left every one of those as they were and re-headed this
section to 10.10, replacing item 8 — a box's content origin is no longer its
top-left corner — and adding two: item 25, where a box sits now that a margin moves
it, and item 26, what the four margin kinds are. It is the first phase to close an
item *out* rather than sharpen one. 10.10 re-headed it to 10.11 and added item 27,
what a padding does to a box and what it deliberately does not. 10.11 re-headed it to
10.12 and did two things to item 8: a box's `size` is now its **border** box rather
than the padding box 10.10 left it as, and a content origin is a *composed* number —
the border's `top`/`left` and then the padding's — which is why item 8 now states
both terms rather than one. It adds item 28, the one rule that is 10.11's own.
10.12 re-headed it to **10.13** and adds item 29 — a box's result position is its
**final** position, the flow position lives one subtraction away — and item 30, the
inset reader the phase added and the one parameter by which it differs from the
three before it. 10.13 re-headed it to **10.14** and adds item 31, the phase's own
rule: an overflow is a record *beside* the box, measured from the padding box, and the
thing a parent folds in is the child's region rather than its contents. 10.14 re-headed
it to **10.15** and adds two more: item 32, which says that a conclusion now *expires*
and that the expiry is per box rather than per document, and item 33, the one thing a
caller must not assume about a box it has not laid out. It is also the first phase to
write a rule about the *order* of the module's own output -- a pass consumes the layout
bits and leaves `STYLE` -- which is why item 32 names both halves. 10.15 is the last goal
of Phase 10 and re-heads it to **Phase 11**, adding item 34 -- the rule the phase *found*
rather than wrote: every accumulation in a pass is bounded, so no extent this module
reports can be negative -- and leaving items 1–33 as they were, which is what a
consolidation phase that changed no capability should be able to say.)*

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
6. A box's `size` is what the **sizing layer** says, and its answer is one of three
   kinds rather than a number: `AUTO` — no declaration, or the keyword — which is the
   containing block's content width on the inline axis and the contents' extent on
   the block axis; `DEFINITE` — a `px` length this build can use, clamped through the
   constraints record; and `UNRESOLVED` — declared and not resolvable here, which is
   what a `%` currently is, *filled* from the available space and reported as filled
   rather than silently turned into that number. `LayoutSizingResolve` is the
   pair a block box uses. An **atomic inline** cannot use it: its `auto` height is the
   extent of contents that could only be laid out *at* a width, so it resolves width,
   walks its contents, then asks for height — one door would have forced it to lie
   about contents it does not have yet. A **transparent inline** is sized by neither
   branch and remains the union of what its contents reached.
7. An inline formatting context's height **is** the block extent of the box that
   owns it, which is why there is no second entry point beside
   `LayoutBlockLayout`: `LayoutInlineWalkContents` is internal and
   reached only by the block pass. Since 10.6 the lines themselves are **records on
   the tree** (`LayoutTreeLineCount`, `_line_at`, `_line`, `_line_member`,
   and `LayoutObjectLine` the other way), so `lines` and `wraps` are counts a
   caller checks against the arena rather than the only way to learn anything.
7a. **Every stored line has a usable baseline**, and every object the pass placed
   carries the metrics it was placed with: read a line's floor with
   `LayoutLineBoxBaseline` (and its position in either space with
   `_baseline_offset` / `_baseline_y`), and a member's own with
   `LayoutObjectBaseline`. A member's `y` is
   `line.origin.y + (line.ascent - member.ascent)`, its two terms sum **exactly** to
   the line's height, and the four invariants are written out in `Baselines.md` §12.
7b. A reader comparing a **nested** member with its line must add the chain of boxes
   between them: a participant's coordinates are relative to the box that contains it
   (item 5), and a line's are relative to the context owner. For a direct child the
   two spaces coincide.
8. A box's `size` **is its border box**, and its content box is that inset by the
   border (item 28) and then the padding (item 27) — so a content origin is no longer
   a top-left corner but a **sum of two terms**, and a `width: 200px` box with
   `padding: 5px` and `border: 1px solid` is 200 wide inside and 212 outside. Its
   `position` is its **margin origin**: its containing block's content origin plus its
   own `margin-top` and `margin-left`, while its size is unchanged by any margin
   (item 25). Under this build's `box-sizing: content-box` — CSS's initial value — a
   declared `width`/`height` is the *content* box, which is why neither 10.10 nor
   10.11 changed a declaration's meaning and only the box a reader sees. The box model
   is now **complete**: all four of CSS's boxes (content, padding, border, margin)
   are distinguishable, and `margin collapsing` is still not a thing this build does
   (item 26).
9. `LayoutContextRun` is the only tree-mutating door in the module, and a
   later goal extends it rather than adding a second one.
10. Constraints can say "unset" honestly, and `clamp` is the only bounds arithmetic
    in the module — which 10.8 honoured by *calling* it rather than comparing two
    numbers itself, so "min wins over max" still has one author.
11. The user-data seam is where scratch state goes; the context will not grow a
    field per formatting context.
12. An invalidation withdraws geometry along with the classification, so a caller
    that adds a box after a pass reads honest `UNRESOLVED` states rather than
    numbers computed with the previous box set. **The withdrawal is the rule, and
    it lives in one place** (`LayoutTreeContextsReset`), because geometry
    is a *function of the classification* and three callers say the same thing.
13. `LayoutGeometryIsImplemented()` is the single question a caller asks
    before reading geometry, and flipping it is a phase's *last* act — after the
    numbers it promises exist.
14. **Every live box participates in exactly one formatting context** once a pass
    has run, and `LayoutTreeIsClassified` is how a caller knows a pass has.
    `LayoutObjectContext` answers where a box participates;
    `LayoutObjectContextOwned` answers what it establishes, which is `NONE`
    for most boxes and is not a failure.
15. **A context's parent is the one its owner participates in**, so the context
    tree's shape is a subsequence of the box tree's, and `depth` is real.
16. **`LayoutTreeInvalidateContexts` is how a classification is released**,
    and a build or an `add_object` releases it implicitly. A box's context id read
    across an invalidation answers `RoboNotFound` rather than resolving against
    the new classification.
17. **A transparent box establishes nothing**, and three phases now lean on it: a
    margin collapses *through* it (10.9), 10.13 declines it an overflow record of its
    own because its rectangle *is* the union of its contents, and the float goals need
    it to let a float escape. A later goal that needs it to be a barrier is asking for
    an anonymous box, which is 10.3's machinery rather than a change here.
18. **Atomicity is derived, not stored.** A participant of an inline context is
    atomic exactly when it established a context of its own; there is no third flag
    and no "is this an inline-block" predicate in this module. The two facts come
    from 10.2's ids and 9.12's outer display, and the header stated the rule as a
    table so 10.8 could re-derive it rather than re-decide it — and 10.8 did.
19. **A box's rectangle is in its containing block's space in both directions**,
    and for a participant inside a transparent inline box that required a rebase
    pass. A later goal that writes into a participant's position must respect that
    the rebase happens once, at the end of the context's walk, and that an atomic
    inline's contents are *already* relative to it.
20. **Every object with no geometry is explained by one of four counters** —
    `block.inline_level_skipped`, `block.unreached`, `inline.block_level_declined`,
    and "it generated no object at all". The identity
    `blocks_placed + inline_level_skipped + participants_placed + unreached ==
    objects` is asserted over every laid-out tree, so a build that dropped an object
    would be visible as a document that did not add up. Since 10.5 this closes over
    **every kind of object at once**, not only boxes: `objects` is the tree's
    population including text objects, and a text run in a block-direction context is
    an `inline_level_skipped` like any other inline-level object.
21. **A text run is an ordinary participant of the line**, measured before it is
    placed and placed by the same call an atomic inline is. There is no
    text-specific branch in the inline pass, and a pass given no metrics source
    places every run with width 0 and **counts** it (`text.unmeasured`) rather than
    failing — which is a legitimate way to run the module, not a degraded one.
22. **A line is a record, not a live object.** A caller reads line boxes through the
    tree's arena and can create or mutate none; the line being built never leaves the
    pass's stack. A line's **members are the objects it placed**, which is a smaller
    set than the context's participants whenever a transparent inline box enclosed
    something — `LayoutObjectLine` answers `RoboNotFound` for such a box,
    and that is the documented answer rather than an omission. Line geometry is in
    the **owner's** content-box space, so a line is not rebased the way a participant
    is (item 19).
23. **One resolved width, two uses**, and separating them is the phase. A box's
    declared size is what the box *is*; it is also what its children are laid out
    *into*. 10.3 resolved a single width from the containing block and spent it on
    both, so a `width: 300px` box in an 800px parent came out 800 wide *and* gave its
    children 800. The sizing layer settles the width first and recursion uses that
    number, which is why a declared width now reaches the children rather than
    stopping at the box. For an `auto` width the two uses coincide, so no expectation
    about a box with no declaration moved.
24. **A pass given no declared size still resolves**, and a dimension the layer
    cannot use is never a zero: `has_available_width` is asked rather than assumed,
    and an unresolvable declaration is filled and flagged. The layer is a pure
    function of (style, available size, contents) — the same three inputs give the
    same record — and it holds no global state for a later goal to invalidate.
25. **A margin moves a box and never resizes it.** A box's rectangle starts at its
    **margin origin** — the containing block's content origin plus its own
    `margin-top` and `margin-left` — its `size` is the sizing layer's answer
    untouched by any margin, and what its parent stacks is its **margin box**,
    `top + height + bottom`, which is why a following sibling sits at
    `top + height + bottom` rather than at `top + height`. The order inside the pass
    is forced and is not an implementation detail: the margins resolve **before** the
    width, because the width a declared `auto` fills is what the margins left.
26. **A margin is one of four kinds, and "nothing was declared" is one of them.**
    `ABSENT` (geometry reads it as CSS's initial `0`, and it is *counted* rather than
    reported as a declared zero), `RESOLVED` (a signed `px` length — the one place
    this module accepts a negative, because `margin: -10px` is legal CSS), `AUTO`,
    and `UNRESOLVED` (a `%`, an `em`, a `calc()`, which contributes a recorded zero
    and is filled from the available space exactly as a dimension is). Two doors, not
    one with a flag: a block box distributes a horizontal `auto` as its share of the
    leftover *beside a declared width* and treats a vertical `auto` as zero, while an
    atomic inline turns every `auto` into zero — and a flag would let a caller pass
    the wrong rule and get a plausible answer.
27. **A padding grows a box and never moves it**, and it is the exact mirror of item
    25. A box's `size` is its **border** box — the content box the sizing layer
    resolved plus the padding on each axis — its contents begin
    `padding-left`/`padding-top` inside that, and its `position` is unchanged by any
    padding. The order inside the pass is forced in one direction and chosen in the
    other: the padding resolves **after** the margins and **before** the width,
    because CSS's constraint equation has a padding term on each side, so an `auto`
    width fills what the margins *and* the padding left. A padding is one of **three**
    kinds rather than the margin vocabulary's four — `ABSENT`, `RESOLVED`
    (non-negative, which is the single parameter by which the two readers differ over
    a shared classification) and `UNRESOLVED` — because the CSS grammar has no
    `padding: auto`, and there is **one** resolution door rather than two because with
    no `auto` there is nothing for a block box and an atomic inline to disagree about.
    What it deliberately does not do is inflate a **transparent inline box**, whose
    rectangle is the union of its contents rather than a placed size.
28. **A border's *used* width is not its declared one**, and that is the only rule in
    this module that throws a declaration away rather than passing it through. A side
    is used at its declared `px` length only when `CssBorderStyleGeneratesBorder`
    says its style draws — so `border: 5px none` is a declared five and a used **zero**,
    and a box with twelve border declarations and eight `none` styles is exactly as big
    as one with no border at all. Two consequences a later goal inherits rather than
    rediscovers: the record on the object carries *used* widths (read a side and compare
    it to zero; never re-read a style to find out whether this build could use it), and
    the pass counts a declaration that resolved (`borders_applied`) **separately** from
    a border that draws (`borders_drawn`), because those two questions have different
    answers and a single counter would report the wrong one for a `none`-styled
document. `thin`, `medium` and `thick` are the three numbers this build picks (1, 3 and
    5, the browsers' choice), stated as named constants because 9.15 keeps the keywords
    as keywords and a used width is a rendering decision rather than a parse.
29. **A result's `position` is where the box is *drawn*, not where flow put it**, and
    the two are related by one subtraction. Every box is placed at its **flow**
    position — the containing block's content origin plus its margins, the number a
    sibling's placement was computed from — and a relatively positioned box's result
    then carries the **final** position, flow displaced by the resolved offset. The
    used offset is on the object (`LayoutObjectPosition`), so `final − offset`
    recovers flow, and the parent stacks by the **flow** geometry: a box shifted
    100px down still leaves the hole flow gave it, and the next sibling sits exactly
    where it would have. The offset is applied in 64 bits and **saturated** — a flow
    position and a displacement are each bounded by the seam's maximum and their sum
    is not — and a `static` box's offset is the zero identity, which is why every
    earlier expectation about a position still holds. `absolute`, `fixed` and
    `sticky` are read and **declined**: their insets are counted as declined rather
    than dropped, because "this box has insets" and "this box was displaced" are two
    facts a renderer and a diagnostic both want.
30. **An inset is a signed length**, and that is the one parameter by which the
    position reader differs from the padding and border readers over the same shared
    classification. `top: -7px` is legal CSS and is honoured — the negative half of
    the value space is the grammar's, not a caller's — where a padding and a border
    refuse theirs. The precedence rule is stated once and is the phase's own:
    `left` wins over `right`, `top` wins over `bottom`, and an `auto` side says
    nothing — the direction-dependent over-constrained cases are exactly what 18.5
    replaces, and the counters (`offsets_applied` / `_auto` / `_unresolved` /
    `_declined`, plus `boxes_shifted`) are what make a later phase's change to the
    rule visible without rewriting a pass.
31. **An overflow is a record beside the box, not a size of it**, and that is 10.13's
    own rule. A box keeps the size its declarations and its contents asked for; what
    the phase adds is four non-negative **extents** measured from its **padding box**
    — CSS's clipping edge, and the one rectangle a box's own declarations cannot move
    — plus the used mode per axis (`visible` or `hidden`, resolved longhands-first and
    then from the `overflow` shorthand, which this build *reads* rather than expands).
    Two rectangles follow from that, and the difference between them is the phase: the
    **region** is the padding box grown by the extents and clipped back on an axis
    whose mode clips, and it is what a **parent** folds in
    (`LayoutOverflowEdgesInclude`, a per-side maximum computed in 64 bits and
    saturated), while the **clip rectangle** is what a *paint* pass will clip to
    (`LayoutOverflowEdgesClipRect`, which refuses rather than answering when
    neither axis clips). Folding in a child's *contents* instead of its region is the
    one mistake that compiles, looks right in every fitting document, and leaks a
    clipped subtree into an ancestor's scrollable area.
32. A box carries an **invalidation state** (10.14) and it is per box, not per
    document: `STYLE` for a change layout does not read, `LAYOUT` for its own stale
    geometry, `SUBTREE` for its descendants', and `OVERFLOW` for its measurement alone.
    A pass consumes the three layout bits and **leaves** `STYLE`, so a box can be *not
    clean* and *not need layout* at the same time -- which is the pair
    `LayoutTreeNeedsLayout` and `LayoutTreeIsClean` exist to keep apart.
    A marking policy never gives an ancestor `LAYOUT`, so
    `LayoutTreeInvalidationRoot` -- the lowest common ancestor of the boxes whose
    own geometry is stale -- is the region and not the document. A box with no
    invalidation layer run over it is **not** implicitly clean: a build creates every box
    dirty (`REASON_CONTENT`) and a released classification re-marks the whole tree.
33. `LayoutContextRunIncremental` may skip the walk, and it does so when **all
    four** hold: the context has completed a pass, on *this* tree, with no input changed
    since, and no box carries a layout bit. It is therefore never a substitute for
    `LayoutContextRun` in a caller that must recompute; it is the caller that
    wants "leave it alone if nothing moved" writing one call instead of four checks.
34. Every **accumulation** this module performs is bounded (10.15). The padding, border,
    margin, position and overflow layers each sum their own numbers in 64 bits and
    saturate at the geometry's maximum, and since 10.15 the pass does too: the block
    cursor, a margin origin and a margin box are all clamped through
    `layout_extent_clamp` (`RoboLayoutInternal.h`), so the module's rule and its caller's
    rule cannot disagree about the bound. What a later phase may therefore assume is
    narrow and useful: every extent a result, an inset record or an overflow record
    carries is **finite and non-negative**, and a document at the geometry's limit
    saturates rather than failing a pass.
