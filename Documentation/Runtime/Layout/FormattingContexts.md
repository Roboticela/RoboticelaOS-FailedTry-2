# RoboLayout — formatting contexts (Phase 10.2)

This is the design record for the second phase of RoboLayout: what a formatting
context is in this module, the two claims a box makes about one, the classification
rules and where they came from, and what 10.3 onwards is entitled to assume.

`docs/Architecture.md` remains the record for the module's architecture. This
document is the record for the one thing 10.2 added to it — and it is unchanged by
10.4, which consumed the same two ids a second way rather than adding a fact. §9
records both consumptions.

---

## 1. What the phase is

10.1 built a tree of boxes and a pass that walks it. 10.2 answers the question
every layout algorithm starts with:

```text
                 which boxes' layout am I coordinating, and in which direction?
```

Concretely, one pass now does three things where it used to do two:

```text
LayoutContextRun(context, tree, diag)
        │
        ├── walk the boxes  ──────────────► every result UNRESOLVED   (10.1)
        ├── count what it saw ────────────► the stats record          (10.1)
        └── classify every box ───────────► the context tree          (10.2)
                    │
                    ▼
        LayoutTreeRootContext()  ─►  the initial formatting context
        LayoutTreeContext(id)     ─►  any context, by id
        LayoutObjectContext()     ─►  where a box participates
        LayoutObjectContextOwned() ─► what a box establishes, if anything
```

What it still does **not** do is compute a coordinate. 10.2 shipped with
`geometry` at `0` and `RoboLayoutGeometryImplemented` at `0`, and no function in
that phase took or returned a rectangle. What it changed is that 10.3 and 10.4
begin with a question already answered rather than a question to re-derive — and
10.3 has since spent exactly that: the block pass reads its placement gate straight
out of the context a box participates in, and never asks a display value which
boxes it owns.

The capability that flipped is `formatting_contexts`, which 10.1 shipped as `0`
with `10.2` in its "filled by" column. It is `1` now, and
`layout.conformance.capabilities` — the positional walk added in 9.16 — is what
notices if it stops being true.

---

## 2. A box has two relationships to formatting contexts, not one

The tempting model is one fact per box:

```text
box.panel  →  "is a block formatting context"   ← one boolean
```

It is wrong, and it is wrong in a way that would cost a later phase a rewrite. A
box does two independent things with formatting contexts:

- it **participates** in one, which is where its own layout decisions take place.
  Every live box has exactly one, and it is the nearest context established by an
  ancestor.
- it **establishes** one, for its *contents*. Most boxes establish none, and then
  their contents take part in whatever *they* take part in.

```text
box.panel    display: inline-block
   ├── participates in   the root context      (its own box is laid out there)
   └── establishes       a block context       (its contents are laid out there)

box.body     display: block, and holds a block-level child
   ├── participates in   the root context
   └── establishes       nothing — it is transparent
```

Transparency is why one boolean is not enough. There are three states a box can be
in — establishes / transparent / inline — and a single flag holds two. So
`LayoutObject` carries two ids:

| Field | Means | Set when |
|---|---|---|
| `context` | where this box participates | in `add_object`, from the parent |
| `context_owned` | what this box establishes, or `NONE` | by the pass, when the rules say so |

and a child resolves its own participation with one expression: **the parent's
owned context if it has one, otherwise the parent's own.** That is the whole of the
module's notion of transparency.

> **The one thing a reader will want asserted.** A tree that has never been
> classified has every box at `NONE`/`NONE`, which is a legal-looking state that
> means "no pass has run". `LayoutTreeIsClassified` is the door that tells
> it apart from "a pass ran and this box participates nowhere", because the second
> never happens and a caller that conflated them would blame the classifier for a
> pass that never ran.

---

## 3. The four kinds, and why there is no fifth

```text
NONE     no context. Not a value a live box has: it is the absence, and what a
         cleared record and an unclassified tree read as.
ROOT     the initial formatting context, established by the root box and by
         nothing else. No parent, and its coordinate space is the initial
         containing block's.
BLOCK    participants are laid out in the block direction, one after another.
INLINE   participants are laid out in the inline direction, along a line.
```

`flex`, `grid`, `table`, `ruby` and `multicol` are deliberately **not** enumerators,
for exactly the reason Phase 9.12 refused to ship `display: flex`: an enumerator
with no row behind it is a name the module can spell and cannot honour. A caller
that asked for `ROBO_LAYOUT_FC_GRID` would get a kind no algorithm in the build
produces. They join as rows when their contexts exist, exactly as `display` values
did.

`RoboLayoutFcKindCount` is therefore `4`, and it counts `NONE`. A later phase
that adds a kind widens an array and adds a row; nothing else in the module needs
to move.

### 3.1 `ROOT` is a block container, in the module's own opinion

`LayoutFcKindIsBlockContainer(kind)` answers `1` for `ROOT` and `BLOCK`,
`0` for `INLINE` and for an undefined enumerator. It exists as a predicate rather
than as a comparison a caller writes, because the two later passes ask it *first*:

```text
10.3 block layout   is this context laid out in the block direction?
10.4 inline layout  is this context laid out in the inline direction?
```

A build where the block pass believed `ROOT` was not a block context would stack
the root's children in the wrong direction while every other assertion in the
module still passed. Stating it once, in the module, is what keeps the two passes
from disagreeing about it.

---

## 4. What establishes what

The whole of this phase's CSS is one pure function with no tree, no pass and no
allocation:

```c
LayoutFcKind LayoutFcEstablishes(CssDisplay display,
                                                int has_block_level_child,
                                                int is_root);
```

Three inputs, because the rules are stated in terms of three facts: what the box's
**computed** display is, whether it has a block-level child, and whether it is the
root. The complete table:

| display | outer / inner | root? | block-level child? | establishes |
|---|---|---|---|---|
| `none` | — (generates no box) | — | — | `NONE` |
| `inline` | inline / flow | no | — | `NONE` |
| `block` | block / flow | **yes** | — | `ROOT` |
| `inline` | inline / flow | **yes** | — | `ROOT` |
| `block` | block / flow | no | yes | `NONE` — transparent |
| `block` | block / flow | no | no | `INLINE` |
| `inline-block` | inline / flow-root | no | — | `BLOCK` |

Two rows carry more than they look like they do.

**A root box establishes `ROOT` whatever it computes to**, which is why the root
question is asked before any display question. A document whose root element is
`display: inline` still has an initial formatting context; a build that fell
through to the display rules would have handed it an inline context and nothing to
participate in.

**`inline-block` establishes `BLOCK` because its inner display is `flow-root`**,
and this phase never names `inline-block`. 9.12 records the outer/inner pair so
that this module can read *one* inner value and be right about the value and every
future one that shares it — `flow-root` itself, when 9.12 adds it, arrives here
with no change at all. A phase that special-cased `inline-block` by name would have
to be edited again for the next value whose inner display is `flow-root`.

The order of the questions inside the function *is* the logic, which is why
`layout.fc.classify.establishes` walks the table rather than a representative of
each shape. A build that asked about children first would classify a root with a
block-level child as transparent; a build that asked about `flow-root` after the
child question would replace a CSS fact with a structural one. Both produce a
plausible-looking context tree.

### 4.1 Two predicates that are not the classifier

```text
LayoutFcDisplayIsBlockContainer(display)
        is this box a *block container* -- the box type that can hold a
        block-level child and that can establish an inline context?

LayoutFcDisplayCanEstablish(display)
        could this box ever establish a context in this build?
```

They exist because the classifier answers a *composed* question and both parts of
it are asked independently. `inline` answers `can_establish` `0` for a reason that
will never change, while `block` answers `1` and then answers `NONE` for one
particular child composition — and a caller diagnosing "why did nothing establish a
context here?" needs to tell those two apart without re-deriving the rule.

`is_block_container` is about the display *value*, not about the context it
produces, and the difference is real: the inner box of an `inline-block` is a block
container that never appears as a box in this build.

---

## 5. Ownership, nesting and depth

| Question | Answer |
|---|---|
| Who owns the root context? | the root **box**, always, and nothing else establishes `ROOT` |
| Does the owner participate in its own context? | yes, for `ROOT` only — it has no enclosing context to participate in instead |
| When is a context created? | during the pass, when a box's classification says it establishes one |
| When is it reused? | never within a pass: one box establishes at most one context, so a pass produces exactly one context per establishing box |
| What does a child participate in? | its parent's owned context if the parent has one, else the parent's own |
| What is a new context's parent? | the context its **owner** participates in |
| What is its depth? | the parent's depth plus one; `ROOT` is `0` |

Because a new context's parent is the one its owner participates in, contexts form
a tree whose shape is a **subsequence** of the box tree's: every box that
establishes nothing is absent from it. That is what makes the nesting claim
checkable — a `<p>` inside an atomic inline nests in the atomic inline's block
context, not beside it:

```text
ROOT  (owner: <html>)        participants: html, head, body, div.panel, span
  └── BLOCK (owner: div.panel, depth 1)   participants: p
        └── INLINE (owner: p, depth 2)
  └── INLINE (owner: span, depth 1)
```

A build that parented every context to the root would still report four contexts
of the right three kinds. `layout.dom.pipeline` therefore asserts the **parent link
and the depth** of the nested pair, not just the counts, because the counts do not
distinguish the two builds.

### 5.1 Why `participants` is stored rather than counted

A caller asking "how many boxes does this context coordinate?" would otherwise walk
the tree, and every consumer that asks it — a block pass estimating work, a
diagnostic, a later damage pass — would walk it again. The pass is already visiting
every box, so it increments the count there. The owner is counted **only for
`ROOT`**, because it is the only case where the owner participates in what it
established; everywhere else the owner belongs to its parent's context.

---

## 6. Where the classification lives

On the **tree**, not on the pass:

```text
LayoutTreeContextCount(tree)     how many contexts
LayoutTreeIsClassified(tree)     has a pass classified this tree?
LayoutTreeContextAt(tree, i)     the context at an index (id == i + 1)
LayoutTreeContext(tree, id)       any context, by id, by value
LayoutTreeRootContext(tree, &id) the initial one
LayoutTreeInvalidateContexts(tree) release the classification, keep the boxes
```

The reason is that a later phase needs to ask about contexts *after* the pass that
produced them: 10.12 resolves a containing block and 10.13 decides what a scroll
container is, and neither should have to hold a pass object to ask which context a
box is in. The pass is a thing that runs; the classification is a fact about the
tree.

A context is a **record by value** — an id, a kind, an owner, a parent, a
participant count and a depth — with no allocation of its own, living in a second
arena the tree owns. Reading one copies it and the tree is unaffected.

### 6.1 Invalidation is a door, and it is idempotent

`LayoutTreeInvalidateContexts` releases the classification and keeps every
box. A build and an `add_object` both do it implicitly, because a box arriving
changes the child composition that one of the classification's three inputs is
made of — and a box added after a pass would otherwise sit in a tree whose
classification no longer describes it.

What it is for: a **stylesheet** mutation followed by a style refresh on a document
whose element tree is unchanged. The tree keeps each box's `display` from the
build, so a caller that changed what that display *would be* must rebuild the tree;
what this door releases is the classification derived from the boxes as they were.

Invalidating an unclassified tree is a no-op rather than an error, because "make
sure nothing is classified" is a legitimate thing to want idempotently. And the
stale-id trap is closed explicitly: a box whose `context` id was read before an
invalidation reports `RoboNotFound` afterwards rather than resolving the id
against the *new* classification, where it would name a context that is not the one
the caller asked about. `LayoutObjectContext` and its owned twin say so in
their headers, and `layout.fc.tree.add_invalidates` asserts it — that assertion is
what found the bug in §8.

---

## 7. The simplifications, stated

Each of these is a deliberate narrowing, not an oversight, and each is a place
where 10.3 can extend the answer without undoing it.

**No anonymous boxes.** CSS gives a block container with *mixed* content — a block
child, plus text or an inline child — anonymous block boxes wrapping each inline
run, after which the container holds only block-level children and the *anonymous*
boxes establish the inline contexts. This build creates none, because a box with no
element and no style is exactly what 10.1's builder cannot produce. A mixed
container is therefore classified **transparent**, and when a later goal introduces
the anonymous boxes they will establish the inline contexts and this classification
will already be right about them.

10.4 kept that decision and made its *consequence* visible rather than silent: a
block-level box inside an inline context is counted by the inline pass as
`block_level_declined` and left `UNRESOLVED`, which is the sibling of 10.3's
`unreached`. Neither is a dropped box. And it turned out the wrapper was not the
entry condition for inline layout at all — a block container with no block-level
child establishes an inline context by *this* document's own rule, which is the
ordinary case in any document (`docs/InlineLayout.md` §2.1).

**A text node is not a box, and does not count as a block-level child.** So
`<p>one</p>` — a block container holding one text node and nothing else — has no
block-level child and establishes an `INLINE` context, which is precisely where
10.6's line boxes will need somewhere to go. The rule reads as a vacuous truth and
is doing real work.

**`has_block_level_child` looks at element children.** It is asked of the child
boxes' computed displays, not of their tag names, and it is false for a box with no
element children at all. A container holding only `display: inline` children is in
the same state as one holding only text.

**The root's containing block is not created here.** 10.1 takes the viewport as an
*argument*; whether a box fills it is a formatting-context question, and 10.12 owns
it. This phase records the context tree and not a coordinate space.

---

## 8. What the phase found

Three things, found by tests, and the third is the reason the first two are worth
recording.

**1. A stale context id read after invalidation.**
`LayoutObjectContext` resolved an id without checking whether the
classification it came from still existed, so a box read after an invalidation and
a re-classification answered with a context that was not the one the caller had
been told about. Found by `layout.fc.tree.add_invalidates`, the one case whose
stated purpose is to add a box to a classified tree. Fixed by making both doors
report `RoboNotFound` for a box that is not currently classified, and the header
now says which door to ask about *which* state.

**2. A `const` tree in the pass's signature that was a lie.**
`LayoutContextRun` takes its tree mutable, and one internal helper took it
`const` while mutating every box's result through it. The compiler had nothing to
say because the mutation went through a `const`-stripped pointer one level down.
Corrected rather than cast away — §10.2 of `docs/Architecture.md` already argues
that a pass mutates its tree, so the signature now says so everywhere.

**3. Two wrong expectations in the phase's own suite, and both were about
nesting.** The classification suite first asserted that the deepest context in the
pipeline document was at depth 1 and that the root context had six participants.
The tree says 2 and 5, and the tree is right: `<p>` is inside the atomic inline, so
its inline context nests, and the root's participants are its owner plus the three
boxes that reach it through a transparent body. The expectations were corrected and
the **parent link and depth** assertions were added in their place. A phase that
had only counted contexts would have shipped with both.

---

## 9. What 10.5–10.13 will consume

| Later goal | What it asks this phase | Which door |
|---|---|---|
| 10.3 block layout | which contexts are laid out in the block direction | `LayoutFcKindIsBlockContainer` |
| 10.3 | which boxes a context coordinates | `participants` + the box walk |
| 10.4 inline layout | where an inline context is, one per block container holding inline content | `LayoutObjectContextOwned`, read for its *kind* |
| 10.4 atomicity | whether an inline-level box is opaque to the line or transparent to it | `LayoutObjectContextOwned` — a participant is atomic exactly when it established one |
| 10.5 text layout | how often a document depends on a participant with no measured width | the `INLINE` context a block container with no block-level child establishes |
| 10.6 line boxes | where a `<p>`'s lines go | the same `INLINE` context, and its owner's content origin |
| 10.9 margin collapsing | which boxes a margin may collapse *through* | the transparency rule, §4 |
| 10.12 containing blocks | what a positioned box's containing block is | `parent` and `depth` on the context tree |
| 10.13 overflow (**done**) | which box is a scroll container | 10.13 records overflow **beside** a box rather than restructuring anything, so the answer this model gives is only ever "which box *would* be one" — and the phase deliberately did not consume it: `visible` beside a clipping axis stays `visible` here because CSS's axis promotion exists for scrolling, and this build has none (`Overflow.md` §9) |
| anonymous boxes | which containers are mixed, and so need wrapping | a block container whose children are both inline and block level — what 10.4 counts as `block_level_declined` |

None of these needs a new header for the classification itself, and neither 10.3 nor
10.4 changed the context model at all. 10.3 consumed
`LayoutFcKindIsBlockContainer` as its single placement gate. 10.4 consumed
the *same two ids* a second way: the context a box **participates in** says whether
it is on a line at all, and whether the box **established** one says whether it is
opaque to that line or transparent to it. So §2's two relationships became the
deciding fact of both directions, from one field pair and with no third flag — which
is the payoff the phase predicted when it refused a single boolean on the box.

What the table's 10.3 rows predicted was the **anonymous box**, and neither 10.3 nor
10.4 built it, on purpose. An anonymous box exists to give an inline *run* a
block-level home — the mixed container, not the pure one — and 10.4 turned out not
to need it as an entry condition: 10.2 already makes an inline formatting context the
ordinary case, because a block container with no block-level child establishes one.
So `<div><span></span><span></span></div>` was placeable without any wrapper, and
the mixed case is counted (`inline.block_level_declined`) and left `UNRESOLVED`
rather than placed badly. `docs/InlineLayout.md` §2.1 is the argument, and
`docs/BlockLayout.md` §5 is what happens instead: `RoboLayoutObjectKindCount`
stays **2**, because an anonymous box is the one kind that would have no element and
therefore no computed style.

---

## 10. Boundaries

- **No layout.** Nothing here measures anything. `establishes` answers *which kind
  of context*, never how to lay one out.
- **No CSS parsing.** The display arrives computed, and the only thing read out of
  it is the outer/inner pair 9.12 records.
- **No anonymous box, no text box, no fragment.** A later goal, and 10.5. Neither
  10.3 nor 10.4 added one (see §9).
- **No float, no clearance.** A later goal. `position` (10.12) and `overflow` (10.13)
  are done, both without a row in the classification: neither changes a box's outer
  or inner display, which is the only thing this model reads.
- **No geometry *of its own*.** A context still has a depth and a participant
  count; it has no rectangle, and nothing here computes one. Both directions
  compute geometry by reading this model to decide which boxes they own — the
  dependency is the one §9's table lists, and it runs one way.
- **No Chromium.** Blink's `LayoutBlock`/`LayoutInline` split and its
  `FormattingContext` are temporary technical reference only; the vocabulary, the
  record and the classifier here are Roboticela's, and `Tools/Build.ps1 -Verify`
  scans this directory for a Chromium include.
