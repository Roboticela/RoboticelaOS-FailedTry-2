# RoboPaint — clipping (Phase 11.8)

The phase that turned the boundary in force from a **field** into a **scope**.

Every release since 11.1 has carried a clip: `robo_paint_clip_t` sits on each paint record and each
display command, and the decoration pass has bounded a drawing by it since 11.4. That was enough to
answer *"what bounds this drawing?"* and not enough to answer *"where does this boundary begin and
end?"* — a flat list carried the same two intervals on every command and nowhere said that the box
under command 12 is what narrowed them.

This phase adds the missing statement — a clip **stack** and the two display commands that make a
scope visible in the artifact — and hardens the arithmetic underneath it. It does **not** add a
clipping feature: what is clipped is the same rectangle of the same box that 11.1 already derived.
What changes is that the boundary is now *explicit*, *nested*, *checked* and *enforced from one
place* instead of being repeated per command and trusted.

The record's own argument — why a clip is two intervals and not one rectangle — is at
`RoboPaintGeometry.h` and in `paint/docs/Architecture.md`; the traversal is there too; the pass is
`paint/docs/Decoration.md`; the command model is `paint/docs/DisplayList.md`; the record model is
`paint/docs/Operations.md`; the case list is `paint/docs/Tests.md` §1.11 and its findings §12.

```text
HTML / DOM
  → RoboCSS            overflow-x / overflow-y computed per box
  → RoboLayout         used overflow modes + the padding box (10.13)
  → RoboPaint          the traversal intersects them, per box, down the tree
  → records            every record carries the clip in force; a *changed* one carries the pair
  → display list       PushClip(boundary) …contents… PopClip(boundary)
  → the pass           one stack, one application point
  → RoboGraphics       robo_gfx_raster_set_clip, reset-then-replace per drawing
  → the caller's surface
```

---

## 1. The clip record, and the one thing it is not

```c
typedef struct robo_paint_clip_axis {
    robo_css_overflow_t mode;   /* `hidden` means this axis is bounded */
    int32_t             start;  /* half-open, in paint coordinates */
    int32_t             end;    /* never below start */
} robo_paint_clip_axis_t;

typedef struct robo_paint_clip {
    robo_paint_clip_axis_t horizontal;   /* overflow-x */
    robo_paint_clip_axis_t vertical;     /* overflow-y */
} robo_paint_clip_t;
```

**Two axes rather than one rectangle**, which is 11.1's decision and not this phase's, restated here
because clipping is where a reader meets it. `overflow-x: hidden` beside `overflow-y: visible` is a
legal declaration, and one rectangle cannot carry it: a consumer handing four numbers to a rasteriser
would clip the axis the author left open. `robo_paint_clip_rect` answers the rectangle in the one
case where a rectangle is the whole truth and **refuses** otherwise, so the answer a consumer gets is
never a number that means "free" wearing the shape of a bound.

The three predicates are the record's public surface and each is one question:
`robo_paint_clip_axis_is_active` (does this axis have a boundary?), `robo_paint_clip_is_empty` (does
this scope bound *nothing*?), and `robo_paint_clip_is_valid` (is this a record this build produced?).
An unbounded record is **not** empty; an empty one is **not** invalid. Those three states are exactly
the three a consumer must keep apart, and a phase that collapsed any two of them would make the
`pop-clip` of an excluded box indistinguishable from a malformed one.

## 2. The stack

`robo_paint_clip_stack_t` (opaque, `paint/Include/RoboPaint/RoboPaintClip.h`) is a LIFO of whole
clip **records**:

| Door | Answers |
|---|---|
| `_create` / `_destroy` | ownership; the caller's, like every other list in this module |
| `_reset` | empty, **keeping capacity** — what a pass calls before reusing itself |
| `_push` | from here on, this is the clip in force (the record is *copied*) |
| `_pop` | the innermost scope is closed |
| `_current` | the clip in force; the cleared record when the stack is empty |
| `_depth` / `_capacity` / `_is_empty` | 0 / grown-or-not / nothing in force |

Three decisions worth stating:

- **Records, not derivations.** A push is a whole `robo_paint_clip_t` — both axes, both modes, both
  intervals — so "the clip in force" is the top of the stack and needs no boundary rectangle, no
  element geometry and no layout tree to work out. A stack of *boundaries* would have to re-run the
  intersection against a box's padding box to answer the same question, which is arithmetic
  `RoboPaintGeometry.h` already did once, at the box that owned it.
- **A pop with nothing to pop is a refusal (`ROBO_NOT_FOUND`), not a no-op.** It is the one malformed
  operation a stack can be handed, and answering `ROBO_OK` would let a consumer that lost track of
  its own pushes go on drawing under a boundary that is not in force. The refusal is also *safe*: the
  stack is untouched, so a caller may report it and carry on.
- **`_current` on an empty stack — and on NULL — is the cleared record, not an error.** "Nothing is
  in force" is the ordinary state of a document that declares no overflow, so a caller may draw
  under it without a branch. A `visible`/`visible` record is legal to push as well: the stack is a
  container of values and has no opinion about CSS. The *command* vocabulary does refuse such a push,
  because a scope that establishes nothing is a command whose consumer could only skip
  (`RoboPaintOperation.h`) — a policy about the document, stated where documents are described.

**Where a stack is used and where a value is used instead.** The traversal is recursive and carries
its clip as a **value** (`paint_walk`'s parameter): a recursive walk's "what is in force here" is the
call's own argument, and a shared mutable stack would be a second mechanism for one fact. The stack
is for the two consumers that read a **flat** sequence — the display-list builder, which uses it to
check that every command inside a scope agrees with it and that the scopes pair up, and the
decoration pass, which uses it to know what bounds a drawing without re-deriving anything. Two
readers, one sequence, one stack each. There is no global clip state anywhere in the build.

## 3. Rectangle intersection, and the canonical empty interval

`robo_paint_clip_intersect(clip, boundary, horizontal, vertical, out)` is the traversal's door: a
boundary rectangle plus the two modes a box declared, answered per axis. An axis the boundary hides
is intersected; an axis it leaves open is **carried, not cleared** — a `visible` axis does not
*unclip* what an ancestor bounded. A boundary that hides neither axis leaves the clip unchanged,
which is the case every box in an ordinary document produces and the reason the traversal may call
this for every box without asking whether it needs to.

`robo_paint_clip_intersect_clip(outer, inner, out)` is the consumer's door: two records, the smaller
interval per axis, `inner`'s mode on an axis it bounds. It exists for a reader replaying a flat
sequence, and both doors route through the same per-axis helper — the two axes differ only in which
numbers they are given, and a transposed pair of copies is the classic way this arithmetic goes
wrong.

**The canonicalization this phase added.** The intersection is a `max` of two starts and a `min` of
two ends, and those can come out **backwards**: a box an ancestor's boundary entirely excluded gives
`start = 200`, `end = 40`. Left alone, that is a record `robo_paint_clip_is_valid` refuses — so the
one cell of the model that means *"nothing can be drawn here"* would be the one cell that is not a
record, and every consumer would have to choose between trusting the type and trusting the
arithmetic. It is canonicalized instead to the zero-length interval `[max(start), max(start)]`:

| | |
|---|---|
| what it is | a valid record that bounds nothing; `robo_paint_clip_is_empty` answers 1 |
| where it sits | the upper start — the later of the two starts, a function of the two intervals and nothing else |
| what it survives | a push, a pop, a display command, a clip stack, a projection to a rectangle of zero width |

So `[0, 40]` intersected with a box at `[200, 210]` is `hidden[200, 200]` — empty, valid, and stable
under further intersection with anything (the fixed point is pinned in
`paint.clip.intersection`). "A box outside its ancestor's boundary" is a statement about a document,
not a failure, and now it is a statement the *type* can carry.

## 4. Where a scope is emitted

There is exactly **one** condition, and it is a comparison rather than a flag:

```c
clip_scope = !robo_paint_clip_equal(&child_clip, clip);   /* what this box puts in force != what it was entered under */
```

The two cases where it is false are different documents and both matter: a box whose `overflow` is
`visible` does not clip at all, and a box whose `overflow` is `hidden` *inside an ancestor that
already bounded the same region* narrows nothing. Neither opens a scope, because a scope that
establishes nothing is two commands a consumer could only skip. `stats.clipped - stats.clip_scopes`
is exactly that count — boxes whose own `overflow` says "this axis hides" but where the intersection
came out equal to what was already in force.

The arrangement around a box:

```text
BEGIN_BOX        node, depth d          ← the box's own decorations are OUTSIDE its scope
BACKGROUND       node, depth d
BORDER_*         node, depth d
CONTENT          node, depth d
PUSH_CLIP        node, depth d          ← only when the boundary changed, with the child clip
  …the box's children and text runs…
POP_CLIP         node, depth d          ← the same clip the push carried
END_BOX          node, depth d
```

**The box's own background and border are painted under the boundary the box was *entered* under, not
its own.** CSS clips a box's overflowing *contents* at its padding edge — the box's own decoration is
not its contents. A flat list makes this the one arrangement a reader would get backwards, so it is
asserted twice: by command index (`paint.clip.scopes_emitted`) and by pixel
(`paint.clip.background`, `paint.clip.border`).

**The pop carries the same clip the push carried, not the boundary being restored.** That is what
makes the pair self-identifying: the builder matches a pop to the innermost open scope by comparing
the two records, and a consumer replaying the list restores the enclosing boundary by popping its own
stack rather than by reading a second value out of the payload.

## 5. What the builder checks

The display-list builder keeps the same stack while it converts records, and refuses a build whose
scopes do not hold together — in **eleven** messages, each its own refusal rather than one that
happens to catch several, because each names a different way a list can lie:

| Refusal | What it caught |
|---|---|
| `a clip scope belongs to no open box` | a push with no box to own it |
| `a clip scope names a box other than the innermost open one` | a push whose node/depth is not the innermost open box |
| `a clip scope precedes its box's content boundary` | a push before its own `CONTENT` |
| `a box opens two clip scopes` | a second push for one box |
| `a clip scope establishes nothing new` | a push whose clip equals the one in force |
| `a clip scope is wider than the boundary it is inside` | a scope that bounds *more* than what encloses it |
| `a clip command disagrees with its scope` | a drawing whose own `clip` field differs from the scope it stands in |
| `a pop with no push` / `a pop with another boundary` / `a pop naming another box` | three pairing failures, counted separately above because a pop can go wrong in three independent ways |
| `a scope never closed` | a push with no pop by the end of the list |

Any refusal **empties the output list** and leaves the caller without a partial artifact. The
narrowing check is the interesting one and it needed correcting during this phase (see §8).

## 6. The pass

The decoration pass (`paint/docs/Decoration.md`) keeps **one** stack for the whole walk, created
after the raster so that a target the graphics layer refuses costs no allocation:

```text
push-clip  → the command's clip becomes the boundary in force
pop-clip   → the one outside it is restored (a pop on an empty stack is a reported error)
drawing    → draw under the scope's clip, or under the command's own clip outside any scope
```

Neither a push nor a pop touches the raster — a scope with nothing inside it costs two commands and
no graphics work at all — and the clip a drawing is bounded by is applied *when that drawing is
drawn*, from the stack, at one point (`decoration_apply_clip`, reset-then-replace into
`robo_gfx_raster_set_clip`: the axis the boundary leaves open keeps the surface's own interval).

Outside any scope the pass reads the **command's own** `clip`, which is the only statement of
clipping a list without clip commands carries — a hand-built one. The builder refuses a command
inside a scope that disagrees with it, so for every list this build produced the two values are
equal and the choice is not observable, which is exactly what makes reading the stack safe.
`paint.clip.hand_built_scope` is what proves the stack is load-bearing: a hand-built list whose
drawing carries *no* boundary of its own, standing inside a scope whose clip is empty, draws nothing.

The clip is therefore applied **once, centrally**, for every drawing kind the build has —
backgrounds, the four border bands and text runs. No individual painter knows about clipping; there
is no second implementation to drift.

## 7. Counters

| Counter | Where | Meaning |
|---|---|---|
| `clipped` (traversal) | `RoboPaintContext.h` | boxes whose used overflow modes say an axis hides |
| `clip_scopes` (traversal and pass) | both | scopes actually opened — the boundary *changed* |
| `clip_depth_max` (pass) | `RoboPaintDecoration.h` | the deepest the stack went during the pass |

`clipped - clip_scopes` is the number of boundaries that were already in force, and the pass's
`clip_scopes` is asserted equal to the traversal's, so "the artifact states the scopes the traversal
decided on" is a checked identity rather than a hope. `skipped` keeps its own identity at
`3 × nodes + 2 × clip_scopes`, because a scope is a push **and** a pop and `clip_scopes` counts
scopes.

## 8. Findings the phase recorded

Three, and the first is the kind that only a test which recomputes its expectation can find:

1. **A containment test written backwards passes every document with a scope and refuses the ones
   with an *inset* scope.** The builder's narrowing check — "a scope must be at least as tight as the
   boundary it is inside" — was spelled `narrow.start > outer.start || narrow.end < outer.end`, which
   is the *opposite* predicate: `narrow.start > outer.start` is exactly what an inset child produces
   (`[20, 200]` inside `[0, 200]`), so every nested scope was refused with *"a clip scope is wider
   than the boundary it is inside"*, and the eight-character fix was `outer.start <= narrow.start &&
   narrow.end <= outer.end`. It survived the whole first build because a document with a *single*
   scope never reaches the check.
2. **A disjoint intersection has to be canonicalized, or the record type lies.** An excluded box
   produced `[200, 40]`, which `robo_paint_clip_is_valid` refuses — so the case the model most needs
   to express was the one case it could not. §3 is the fix, and its shape is a decision (which
   interval the empty one lands on) rather than a clamp to hide.
3. **`filled` counts fills *attempted*, and an empty clip is a no-op inside the raster.** The
   phase's own new case asserted `filled == 0` for a fill inside an empty scope and was wrong about
   the counter, not about the pixels: the pass executes the fill, the raster bounds it out, and the
   pixel stays transparent. The claim is the pixel and `clipped`; `filled` keeps meaning "fills this
   pass executed", and the header now says so where a reader meets the counter.

## 9. Debug dump

The record and display writers already print the boundary as two modes with the interval appended
only on an axis that has one (`clip=hidden[0,100]/visible`). A scope now shows up as **two lines
among the drawings**, which is what lets a reader follow nesting by indentation alone:

The **display** dump, `.outer { width: 200px; height: 100px; overflow: hidden }` with a 400px child
inside it (the names are padded to `ROBO_PAINT_DISPLAY_NAME_COLUMN`, 16, and the indentation is the
command's own `depth` at two spaces a level):

```text
#0006  content          node=2 depth=1 clip=visible/visible rect=0,0,200,100
#0007  push-clip        node=2 depth=1 clip=hidden[0,200]/hidden[0,100] rect=0,0,200,100
#0008    begin-node     node=3 depth=2 clip=hidden[0,200]/hidden[0,100] rect=0,0,400,20
#0009    background     node=3 depth=2 clip=hidden[0,200]/hidden[0,100] rect=0,0,400,20 color=#ff0000ff
#0010    content        node=3 depth=2 clip=hidden[0,200]/hidden[0,100] rect=0,0,400,20
#0011    end-box        node=3 depth=2 clip=hidden[0,200]/hidden[0,100] rect=0,0,400,20
#0012  pop-clip         node=2 depth=1 clip=hidden[0,200]/hidden[0,100] rect=0,0,200,100
#0013  end-box          node=2 depth=1 clip=visible/visible rect=0,0,200,100
```

A consumer can read the whole scope off those eight lines: the boundary in force changes at the
`push-clip`, the child's commands all say the same boundary, and the `pop-clip` **returns to what
the enclosing box had** — which is why the box's own `content` and `end-box` lines above the push say
`visible/visible` while its `push-clip` line does not.

The **record** dump is the same shape one stage down, and its one difference is that it names
*which of the three boxes* a kind acts on — `band=`, `content=`, `border=` and, since 11.8,
`padding=` for a scope, where a generic case would have printed the border box and told a reader the
scope came from a rectangle it did not come from:

```text
#0007  push-clip      node=2 depth=1 clip=hidden[0,200]/hidden[0,100] padding=0,0,200,100
```

A clip command's **rectangle** is the box's padding box — the boundary the box's `overflow` resolved
to, and the answer to "where did this scope come from?" when a dump is being read. It is deliberately
**not** the effective clip's rectangle, and the reason is that the effective rectangle does not always
exist: one bounded axis is a legal document, so `robo_paint_clip_rect` refuses exactly the case where
a reader would most like a rectangle, and a command that answered anyway would be inventing one. The
boundary itself travels in the `clip` field, where the axis split survives. No addresses, no
iteration-order dependence: two identical documents produce byte-identical dumps
(`paint.clip.determinism`).

## 10. Coordinate space

Clip intervals are in **paint coordinates** — the single page space the traversal accumulates, the
same space a command's `rect` and a record's `boxes` are in. The traversal derives each boundary from
layout's **padding box in that space**, so nothing is converted at the clip boundary at all: the
numbers a rasteriser eventually bounds a fill by are the numbers RoboLayout wrote, with the page
origin already accumulated into them. There are no general transforms in this build, so there is no
other space for a clip to be in, and the record does not carry a space tag to imply that there could
be.

## 11. Visual smoke test

`.outer { width: 200px; height: 100px; overflow: hidden }` with a 400px-wide child inside it is the
phase's showcase document, painted through the whole chain into a real 800×600 RGBA8 surface
(`paint.clip.pixels`), with the *same* document compiled a second time with `overflow: visible`:

| Sample | `hidden` | `visible` |
|---|---|---|
| inside the box | red (the child's own background) | red |
| the spill beyond the boundary, still inside the child's box | untouched | red |
| the boundary's last pixel / first pixel outside it | red / untouched | red / red |
| scopes in the display list | one push, one pop | **none** |

The visible document is the stronger half of the pair: it does not carry an inactive boundary, it
carries **no scope at all**, and its list is exactly two commands shorter. The hidden surface is then
encoded through RoboGraphics' own PNG encoder, whose signature and 800-pixel width are checked — so
the picture the assertions describe is a real image and not just a buffer. Every drawing kind in the
build is covered (`paint.clip.background`, `.border`, `.text`), and the full list is compared against
the surface pixel by pixel (`paint.clip.pixels`). The guest path is unchanged by this phase: the
QEMU visual runtime display gate passes, and the demo document declares no `overflow`, so no scope is
emitted there.

## 12. What is absent, on purpose

`border-radius` and rounded clipping, `clip-path`, masks, filters, transforms, opacity, `mix-blend`,
scrolling and scroll containers (a clipped box does not scroll in this build — the boundary is a
boundary and nothing more), overflow clipping for absolute/fixed/sticky positioning (11.12+ has no
such positioning yet), compositor-level and tile-level clipping, GPU clip scheduling, hit testing
that consults the boundary (11.10), paint invalidation (11.11), display-list replay and rasterisation
architecture. `raster` stays **0** and `ROBO_PAINT_DRAWING_IMPLEMENTED` stays **1**: this phase
removes none of the drawings and adds none.

The shape an advanced phase fills in is already right:

```text
this build      box overflow → per-axis record → push/pop scope → rectangle per drawing
Phase 11.12     the same record, carried into a stacking context and a display-list replay
```

**Next:** 11.9 — image painting, the fourth drawing kind and the second non-numeric payload, which
has to decide whether an image is a pool of encoded bytes or a reference to a decoded surface — and
which now inherits a clip it gets for free.
