# RoboLayout Invalidation — Phase 10.14

The design record for RoboLayout's invalidation model: why it exists, what a state
means, how a style change is classified, what each marking policy propagates, what a
pass consumes, and what is deliberately absent.

It is not a tutorial. It is the thing a reviewer reads to answer "why is it like
this", and the thing a later goal reads to find out what it may assume. Where this
document and `RoboLayoutInvalidation.h` disagree, the header wins.

---

## 1. What the phase is

Every phase before this one made an answer *better*. This one makes an answer
**expire**.

A pass is a pure function of the tree, the viewport and the borrowed styles
(`BlockLayout.md` §7 says so and 10.13 relied on it). What that buys is
reproducibility; what it does not buy is a way to say "nothing you depend on changed,
so do not compute any of it again". This phase is that way, and it arrives last among
the layout phases for a reason worth naming: invalidation is a claim *about* a set of
conclusions, so it can only be written once there are conclusions to be about -- and
once their shape has stopped moving. 10.12 and 10.13 both added records to the object
and a rectangle to the result; a model that had been written at 10.9 would have had to
be rewritten twice.

**An invalidation is a claim about a conclusion, not about an input.** The tempting
model -- record which declarations changed and re-resolve those -- is wrong at the
second box, because a box's geometry is a function of its *ancestors'* conclusions as
well as of its own declarations: a leaf whose author changed nothing moves when its
parent's padding changes. So what is tracked is not "which inputs changed" but "which
conclusions cannot be trusted", which is a property of **boxes** and composes when it
propagates.

```text
a declaration changes  ──►  what does layout read?
                                   │
                    ┌──────────────┼──────────────┐
                    ▼              ▼              ▼
                  nothing      paint only     geometry
                    │              │              │
                 (mark nothing)  STYLE on     the layout policy:
                                  the box      box + ancestors + following flow
```

---

## 2. The four states

```c
typedef enum LayoutInvalidationState {
    RoboLayoutInvalidationClean    = 0u,      /* nothing is stale */
    RoboLayoutInvalidationStyle    = 1u << 0, /* layout does not read the change */
    RoboLayoutInvalidationLayout   = 1u << 1, /* this box's own geometry */
    RoboLayoutInvalidationSubtree  = 1u << 2, /* its descendants' layout */
    RoboLayoutInvalidationOverflow = 1u << 3  /* its measurement, alone */
} LayoutInvalidationState;
```

`CLEAN` is **0 and not a bit**, so a cleared record needs no setting up and a zeroed
object is a clean object -- the convention the result record's flags already use.

Four bits rather than one, and each of the four is load-bearing:

| Bit | Why it is separate |
|---|---|
| `STYLE` | A colour change must not pull geometry. It is a real state rather than a discarded one because paint (11) is the consumer, and because the *point* of the phase is that "needs a repaint" and "needs a relayout" are two questions. |
| `LAYOUT` | The bit that says a box must be *placed* again. Only this bit is in `RoboLayoutInvalidationOwnBits`, and only that macro answers "where would a relayout start?" |
| `SUBTREE` | Propagates differently from `LAYOUT`: an ancestor whose own origin is stable but whose extent may change carries `SUBTREE` and **not** `LAYOUT`, because the marking policy never claims an ancestor's own geometry moved. |
| `OVERFLOW` | 10.13 made overflow a second rectangle *beside* the box, so a change can leave the box correct and its measurement wrong. A `position: relative` offset does exactly that to every ancestor it does not otherwise touch, and an `overflow` mode change does it alone. |

The bits compose, and the module derives three masks from them:

```c
RoboLayoutInvalidationLayoutBits  = LAYOUT | SUBTREE | OVERFLOW   /* a pass is needed */
RoboLayoutInvalidationOwnBits     = LAYOUT                        /* a region starts here */
RoboLayoutInvalidationAll          = STYLE | LAYOUT_BITS           /* nothing is clean */
```

Three masks for three questions, and the difference between them is the phase's
shape rather than a convenience:

```text
LayoutTreeNeedsLayout        LAYOUT_BITS  must a pass run?
LayoutTreeInvalidationRoot   OWN_BITS     where would it start?
LayoutTreeIsClean            ALL          is anything owed at all?
```

A document whose only change was a colour answers 0, NONE and 1 respectively -- needs
no layout, has no region, and is not clean -- and those three answers are all correct
at once. A build with one bit could not express it.

The reason is a separate field, not one reason per bit. It answers a different
question -- "what happened?" rather than "what is stale?" -- and it is the whole of the
debug surface this phase promises: closed, short (`paint`, `geometry`, `position`,
`content`, `viewport`, `propagated`), and carrying no strings and no history, so the
record stays a value with nothing to own.

---

## 3. The classifier: what a property name costs

`LayoutStyleEffectForProperty` answers, for a **name**, one of three things:

| Effect | Meaning | Example |
|---|---|---|
| `NONE` | no phase in this build reads it, so nothing is invalidated | `cursor`, `z-index`, `will-change`, and **any unknown name** |
| `PAINT` | visible, not measured: marks `STYLE` on the box and stops | `color`, `background-color`, `border-color`, `visibility` |
| `LAYOUT` | measured: the full geometry policy | `width`, `padding-left`, `font-size`, `overflow-x`, `border-style` |

Three decisions in that table are worth stating, because a naive grouping gets each
one wrong:

- **`border-style` is `LAYOUT` and `border-color` is not.** 10.11's used width is zero
  unless the style draws a border, so `border-style: none` is a *geometry* change and
  not a cosmetic one (`Borders.md` §3). A table that grouped all twelve longhands
  under "border" would be wrong in the direction this phase exists to prevent: it would
  repaint without relaying out, and the box would keep a width it no longer has.

- **The font and line properties are `LAYOUT`.** A run's advance and its ascent come
  from a font through 10.5's seam, and they decide where every participant on a line
  goes. `direction` and `writing-mode` are there for the same reason: 10.4 places
  participants along a line in a declared direction.

- **An unknown name is `NONE`, not `LAYOUT`.** The conservative-sounding answer is the
  wrong one: a property this build has never read cannot have changed a conclusion it
  made, and marking it would cost a whole document's geometry for `will-change`.

The table is **written out by hand** rather than derived from RoboCSS's property
registry. That is not laziness -- it is the only correct source. The question is not
"does RoboCSS have a row for this?" but "does **layout** read it?", and only this
module can answer that. A generated table would say every registered property costs
`LAYOUT`, which would repaint nothing and relayout everything.

Names are matched **case-insensitively**, because a caller reaching this from a
stylesheet has a spelling from the document rather than from a table (`WIDTH`,
`Padding-Left`).

---

## 4. The three marking policies, and the shape each one makes

A policy is not "mark the box". It is "mark exactly the boxes whose conclusions this
change can have falsified", and the phase has three because there are three genuinely
different shapes.

### 4.1 Box-model change (`mark_layout_dirty`, `mark_subtree_dirty`)

```text
the box            LAYOUT | SUBTREE | OVERFLOW
every ancestor     SUBTREE | OVERFLOW
following flow     LAYOUT | SUBTREE | OVERFLOW      (transitively)
```

Three rules, each with a reason:

1. **The box itself owes all three.** Its placement, everything it contains, and the
   rectangle its contents reached.

2. **An ancestor gets `SUBTREE | OVERFLOW` and *not* `LAYOUT`.** An ancestor's own
   origin is computed from *its* containing block and does not move because a
   descendant grew; its *extent* may, and its overflow almost certainly does. This is
   the one rule that makes the dirty root useful rather than always the document -- see
   §5 -- and it is deliberate that the model **declines** to claim an ancestor must be
   re-placed when it cannot know.

3. **The following flow is transitive.** A later sibling moves because an earlier one
   grew, and the sibling *after it* moves because the one between them did. The walk
   therefore goes up from the marked box and, at each ancestor, marks the siblings that
   follow -- which is the closure of "the flow after this box" without a second pass
   over the flow. A one-deep rule would be right for a two-box document and wrong for
   the third box of every other one, which is why
   `layout.invalidation.layout_dirty_propagation` asserts `footer` -- a sibling of an
   *ancestor* -- rather than only `section`.

`mark_subtree_dirty` is the same shape with one difference: the box **and every
descendant** are marked. That is the one policy whose meaning *is* "everything under
here", and it is what a rebuild, a structural edit, a released classification or a
font-metric change reaches for.

### 4.2 Displacement change (`mark_position_dirty`)

```text
the box            LAYOUT | SUBTREE | OVERFLOW
every ancestor     OVERFLOW
following flow     CLEAN                            (deliberately)
```

The narrowest of the three, and the one the phase's brief is built on: relative
positioning "must not unnecessarily recompute unrelated normal-flow sizing when only
the final offset changed".

- `SUBTREE` on the box, because a displacement moves the contents with it.
- **No `SUBTREE` on ancestors**, because the box still occupies the space flow gave it
  -- 10.12's contract (`Positioning.md` §4) -- so an ancestor has nothing to re-place.
- **No sibling marks at all**, because a following sibling's placement is computed from
  the *flow* position, which did not move. A sibling that was correct before
  `left: 10px` is correct after it.

Ancestors take `OVERFLOW` because an offset can push a box out of its padding box: the
box did not move for flow, and the *measurement* of everything above it did.

### 4.3 Overflow-mode change (`mark_overflow_dirty`)

```text
the box            OVERFLOW
every ancestor     OVERFLOW
following flow     CLEAN
```

A mode is not a size. `overflow: hidden` becoming `visible` changes what a *reader* of
the record concludes and changes nothing about any box's geometry -- 10.13 says so at
length (§9). So nothing carries `LAYOUT` and nothing carries `SUBTREE`: the dirty set
is the box and its ancestors, every one of them a measurement.
`layout.invalidation.overflow_dirty_is_narrowest` walks the whole tree and asserts
that no box carries an own-geometry bit, which is the one assertion that makes the
`OVERFLOW` bit worth having as a bit of its own.

### 4.4 The reason rule

Two propagation paths can reach one box, so the reason is kept under a rule rather
than overwritten:

- an **explicit** reason replaces `NONE` or `PROPAGATED`;
- a **propagated** reason never replaces an explicit one;
- two explicit reasons keep the **first**.

The third rule makes the answer a function of the marking order -- which the doors fix
-- rather than of the last walk to reach the box, and the second is the one that
matters for a reader: a subtree mark arriving after a geometry change must not erase
the answer to "what happened?".

---

## 5. The dirty root, and why it is a common ancestor

```c
LayoutNodeId LayoutTreeInvalidationRoot(const LayoutTree *tree);
```

The answer is the **lowest common ancestor** of the boxes whose own geometry is stale
(`OWN_BITS`), or `NONE`. It is the region a partial pass would have to start from: the
one box whose subtree contains every stale box, and therefore the smallest thing a
relayout could begin at and still reach everything that needs placing.

Two design points, and the second was a defect the suite caught:

- **Ancestors carrying `SUBTREE` are not in the question.** Every ancestor of a
  reflowed leaf is marked; if any of them counted, the answer would be the root box
  every time a leaf moved -- a number that says nothing about where geometry changed.

- **A *common ancestor* and not the *shallowest stale box*.** The first implementation
  was "shallowest", and `layout.invalidation.layout_dirty_propagation` failed it with
  a real counterexample: invalidating `div` marks `footer` too, and `footer` is
  `main`'s sibling and therefore sits *higher* than `div`. A shallowest-alone rule
  names a box whose subtree does not contain the change at all. The common ancestor --
  `body`, in that document -- does.

The walk is deterministic by construction: it starts from the first stale box in
creation order (document order) and answers the deepest ancestor-or-self that contains
every one of them, so **which box the walk started from cannot change the result**
(requirement 12). Ties at one depth are answered by creation order.

`NONE` therefore means "no box needs placing again", which is a different statement
from `needs_layout` answering 0 and from `is_clean` answering 1.

---

## 6. Where the state lives, and why on the box

The record is a field of `LayoutObject`; the count of dirty boxes is a field of
the tree.

Everything else in this module that is *derived from a pass* lives in an arena beside
the tree -- contexts, line boxes, the member list -- and is **released** with the pass
that produced it. This is the one thing that must survive from one pass to the next by
design, so it cannot live in a pass-scoped arena, and the object is the only structure
in the module that outlives a pass.

It is the second field a non-builder writes, after `result`, and the same argument
covers both: the *structure* is add-only and read-only, but the fields that describe
what has been *concluded* must be mutable, or there would be no way to conclude
anything. A newly created object is marked `REASON_CONTENT` dirty by `add_object`
itself, because a caller that forgot would produce a box a pass skipped.

The tree keeps **one counter** -- boxes with any bit -- so `is_clean` is a read rather
than a walk. There is deliberately **no stored dirty root**: the shallowest/common
ancestor is a function of the tree and is computed when asked, which is O(n) once per
pass instead of a field that five propagation steps have to keep in agreement.

---

## 7. What a pass consumes, and what it leaves

```text
LayoutContextRun
      │
      ├── snapshot what was dirty   ──►  stats.invalidation.{invalidated, style_only, root, reason}
      ├── release the classification (which re-marks the whole tree)
      ├── walk, classify, lay out
      └── if geometry was laid out:
              mark_recomputed(tree)  ──►  the layout bits are cleared, STYLE is NOT
              inputs_dirty = 0
```

Two halves of that are worth stating on their own.

**A pass consumes the layout bits and leaves `STYLE`.** A repaint is not something a
layout pass performed, so a box holding only `STYLE` stays *not clean* and stops
*needing layout* -- the pair the incremental door is built on. `mark_clean` is the
fuller reset, and it exists for the caller that has consumed the paint signal.

**A pass with no viewport clears nothing.** It classified the tree and computed no
coordinate, so clearing the bits there would leave the next pass believing work had
been done that never was. Requirement 11's stale-clean state is exactly that, and it
would be invisible until a later pass skipped work that had never been done.
`layout.invalidation.pass_without_viewport_clears_nothing` asserts it.

The snapshot is taken **before** the classification release, because that release marks
the whole tree and a snapshot taken after it would report a document nobody had
invalidated as a fully dirty one. The four numbers are the phase's evidence:
`invalidated` is what a partial pass would have redone, `root` is where it would have
started, `reason` says why, and `recomputed` is the honest statement of what this build
actually does with them.

---

## 8. The incremental decision

```c
Result LayoutContextRunIncremental(context, tree, diag);
```

The **only** use this build makes of the model, and it is one decision rather than a
partial recursion. The pass is skipped, and the outcome becomes
`RoboLayoutPassClean`, exactly when **all four** of these hold:

| Term | Question | Why it is needed |
|---|---|---|
| the context has completed a pass | `outcome != NONE` | a context that has run nothing has no basis for saying anything is up to date |
| it ran that pass on this tree | `context->tree == tree` | results are per tree |
| no input changed | `!inputs_dirty` | the viewport and the metrics source are not part of the tree and every box's geometry depends on them |
| no box carries a layout bit | `!needs_layout(tree)` | the model's own answer -- and *this* term, not `!is_clean`, is what makes requirement 13 true |

The third term is the one that has no counterpart in the tree, so the context owns it:
`set_viewport`, `set_constraints` and `set_text_metrics` each set an `inputs_dirty`
flag and a recomputing pass clears it. Comparing viewports and metric records instead
would mean comparing floats and a struct with function pointers and padding in it, and
the conservative direction -- a caller that re-records the same viewport pays for one
unnecessary pass -- is cheaper than a comparison nobody can prove correct.

A skipped pass still updates what a caller reads: `passes` advances (a pass that
concluded "nothing to do" is a pass), `objects` is the tree's count, `visited` is **0**
because nothing was walked, and `invalidation.style_only` reports how many boxes are
waiting for a repaint. `recomputed` is 0, which is the whole point.

---

## 9. What this phase does NOT do

- **No partial layout.** The dirty root is computed and reported and *nothing starts
  from it*: the pass recomputes from the tree root exactly as it did in 10.1. The
  phase's brief asks for invalidation and forbids incremental heuristics in the same
  breath, and a build that guessed a dirty region would be fast and wrong. What a
  later goal gets from this phase is the region and the policy, pre-argued.
- **No dependency graph.** A box's staleness is a property of the box, not an edge in
  a structure.
- **No multithreading, no per-CPU state, no speculative layout.**
- **No paint invalidation** beyond the `STYLE` bit -- the bit *is* the interface, and
  the consumer is Phase 11.
- **No change-detection on styles.** RoboLayout classifies a *name it is handed*; it
  does not diff two computed styles, which would require owning both and knowing what
  "changed" means per property. A caller that has a new computed style says so.
- **No re-classification shortcut.** A pass still releases the classification and
  rebuilds it. Reusing it when the box set is unchanged was considered and is a later
  goal's, because the classification is derived state that a pass replaces rather than
  refines (`FormattingContexts.md`).

---

## 10. Where it sits against the phases before it

| Phase | What it added | What this phase does with it |
|---|---|---|
| 10.1 | the tree, the object, the result state | `result.state` stays the per-box authority on *whether* geometry exists; the invalidation record is the separate claim on whether it is still true |
| 10.2 | formatting contexts, released per pass | `invalidate_contexts` now **marks the whole tree** dirty, because the placement gate *is* the formatting context |
| 10.3–10.7 | coordinates, lines, baselines | nothing re-derived: the policies are stated in terms of conclusions, not of the algorithms that reach them |
| 10.8–10.11 | the box model | the "box-model change" policy is one door for all four phases; the classifier splits `border-width`/`border-style` from `border-color` |
| 10.12 | the displacement | `mark_position_dirty`, the narrow policy, whose *shape* is 10.12's contract made testable |
| 10.13 | overflow extents | the `OVERFLOW` bit, `mark_overflow_dirty`, and the reason an ancestor takes `OVERFLOW` without `SUBTREE` |

The one thing this phase changed in an older phase's behaviour: `add_object` marks the
new box dirty, and `LayoutTreeInvalidateContexts` marks the whole tree. Both
are additions to doors that previously did nothing observable, and both are asserted
(`layout.invalidation.fresh_tree`, `layout.invalidation.pass_without_viewport_clears_nothing`).

---

## 11. What the suites prove

Twenty-five cases in `layout.invalidation.*`, and the map of what each group is for:

| Group | The claim |
|---|---|
| `vocabulary`, `classifier`, `records` | the bits partition, the masks relate as documented, the names answer, and the classifier's three decisions (border split, font family, unknown-is-nothing) hold over real spellings |
| `fresh_tree` | a build creates every box dirty with `REASON_CONTENT`, and the count equals the object count |
| `pass_clears_layout_bits`, `pass_without_viewport_clears_nothing`, `mark_recomputed_leaves_style` | the pass contract, both halves: the layout bits go, `STYLE` stays, and a pass that computed nothing clears nothing |
| `layout_dirty_propagation`, `position_dirty_is_narrow`, `overflow_dirty_is_narrowest`, `subtree_dirty`, `style_effects` | the four policies, box for box -- each case asserts the *whole* dirty set, because a build that marked the document would pass every weaker assertion |
| `mark_all`, `reason_rule`, `idempotence`, `refusals` | viewport-wide invalidation, the reason rule in both directions, one box counted once however many marks reach it, and every refusal leaving the tree untouched |
| `root_is_the_common_ancestor` | the region, including the case the first implementation got wrong |
| `incremental_skips_a_clean_tree`, `incremental_runs_after_a_mark`, `incremental_runs_when_inputs_change`, `incremental_skips_a_paint_only_change` | the four terms of the decision, one case each, plus the paint-only case that is *not clean* and *does not need layout* |
| `relayout_moves_the_following_flow`, `relayout_is_stable` | real geometry: a stacked document whose middle box grows, the incremental pass that follows, and the three statements the brief asks for |
| `determinism`, `allocator_balance` | one marking script over two trees compared bit for bit, and no allocation per mark or per pass |

The strongest single case is `relayout_moves_the_following_flow`, because it is the
only one where the *pass* has to be reached for the claim to mean anything: the middle
box's declaration is rewritten **on the style the tree already borrows**, so the same
tree is laid out again and the incremental decision is actually taken. It asserts the
changed box grew and did not move, the following flow moved by the delta, the box
*before* it is bit-for-bit unchanged, and no stale bit survives.

---

## 12. What is deferred, and to whom

| Deferred | Whose |
|---|---|
| partial layout from the dirty root | a later performance goal; the region and the policy are here |
| paint invalidation beyond the `STYLE` bit | 11 |
| reusing a classification when the box set is unchanged | a later phase, on top of 10.2's release-and-rebuild |
| change detection between two computed styles | a caller's, or RoboCSS's |
| threading, dependency graphs, speculative layout | not claimed anywhere |
| hit-testing's use of stale geometry | 12, which reads `result.state` and this record together |
