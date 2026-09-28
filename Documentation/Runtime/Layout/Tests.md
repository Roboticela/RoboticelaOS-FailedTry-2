# RoboLayout Test Organisation (through Phase 19.6)

Twenty-eight suites, 471 tests, one framework. Each phase adds no test runner, no reporter
and no assertion vocabulary: everything here goes through
`runtime/Tests/Framework` (`RoboCheck*` and `TestRegister`), the same as
every other subsystem.

They live in `layout/tests/`, beside the module rather than in `Tests/Suites/`, for
the same reason `css/tests/` sits where it does: RoboLayout is the first module
allowed to see both RoboHTML and RoboCSS, so its integration suites belong next to
it — and a suite in its own directory resolves its fixture header relatively, which
is what `RoboLayoutTestSupport.h` is.

`Tools/Build.ps1` adds `layout/tests` to the host include path for the one suite
shape that needs it, and the suites are listed explicitly in `$SuiteSrcs`. There is
no glob: a suite that is not on the list is not built, which is the same
"explicit over discovered" rule the rest of the tree follows.

§§10.1–10.6 below were written when the module computed nothing, and their text is
kept as it stood rather than back-dated. Where a case has since changed meaning —
`layout.conformance.statement` and the old `geometry == 0` claims, 10.4's
re-pinning of the same two cases again, and 10.5's, 10.6's and 10.7's re-pinning of
the version and the capability table — the change is recorded in §8–§19, under the
phase that made it.

---

## 1. Suite map

| Suite | Domain | Cases | Asks |
|---|---|---|---|
| `SuiteRoboLayout.c` | `layout.module.*` | 11 | does the module's own surface say what it claims? |
| `SuiteRoboLayoutTree.c` | `layout.tree.*` | 14 | is a box, its links and its identity what the header says? |
| `SuiteRoboLayoutConstraints.c` | `layout.constraints.*` | 9 | can a constraint say "unset", and is that different from zero? |
| `SuiteRoboLayoutContext.c` | `layout.context.*` | 11 | does a pass do exactly what §10 of the design record says? |
| `SuiteRoboLayoutBuild.c` | `layout.build.*` | 10 | does the builder apply its one rule, and only it? |
| `SuiteRoboLayoutDom.c` | `layout.dom.*` | 9 | does the bridge join the two peers, and own what it says it owns? |
| `SuiteRoboLayoutFormattingContext.c` | `layout.fc.*` | 13 | is the classification a table, and is the context tree the shape the rules imply? |
| `SuiteRoboLayoutBlock.c` | `layout.block.*` | 16 | does the block pass place each box inside its parent, at the right y, with an honest height? |
| `SuiteRoboLayoutInline.c` | `layout.inline.*` | 23 | does the inline pass advance a cursor, wrap when one does not fit, and write a transparent box as the union of its contents? |
| `SuiteRoboLayoutText.c` | `layout.text.*` | 26 | does a text node become an object, does a real font measure it, and does a run placed on a line get the width the font said? |
| `SuiteRoboLayoutLineBox.c` | `layout.line.*` | 12 | is the line being built not a line box, and are a line's members exactly the objects that advanced its cursor? |
| `SuiteRoboLayoutBaseline.c` | `layout.baseline.*` | 16 | does every line get a floor, are its members placed on it, and is "not placed" a different answer from "placed with no extent"? |
| `SuiteRoboLayoutSizing.c` | `layout.sizing.*`, `layout.percentage.*` | 22 | does a declaration reach geometry, does a box's resolved width reach the children laid out *into* it, and does a percentage keep its magnitude and resolve against the containing block the pass hands over? |
| `SuiteRoboLayoutMargin.c` | `layout.margins.*` | 25 | does a margin move a box without resizing it, and does each of the four sides get the value its policy asks for? |
| `SuiteRoboLayoutPadding.c` | `layout.paddings.*` | 20 | does a padding grow a box without moving it, does an `auto` width fill what it left, and is a transparent inline box left un-inflated? |
| `SuiteRoboLayoutBorder.c` | `layout.borders.*` | 22 | is a border's *used* width zero unless its style draws, is the box a reader sees the border box, and does a content origin compose two insets? |
| `SuiteRoboLayoutPosition.c` | `layout.positioning.*` | 21 | does a relative box move without leaving its flow space, does `static` stay exactly what it was, and does a percentage inset resolve against the right axis's base? |
| `SuiteRoboLayoutInvalidation.c` | `layout.invalidation.*` | 25 | does the module know which of its own conclusions are still true, and does it skip a pass exactly when nothing needs one? |
| `SuiteRoboLayoutOverflow.c` | `layout.overflow.*` | 26 | is an overflow a *record* rather than a size, does a child contribute its region rather than its contents, and does a clipping box contribute only what it kept? |
| `SuiteRoboLayoutFontRequest.c` | `layout.fontrequest.*` | 7 | is a CSS declaration turned into a value record — an ordered, classified family list plus size, weight, stretch, slope and direction — with no font system invented to hold it? |
| `SuiteRoboLayoutFontSelect.c` | `layout.fontselect.*` | 8 | is the request offered to a caller-supplied provider in author order, does the first answer win, and is a miss told apart from a failure? |
| `SuiteRoboLayoutConformance.c` | `layout.conformance.*` | 7 | the whole-product questions no single suite can ask |
| `SuiteRoboLayoutIntegration.c` | `layout.integration.*` | 12 | do the fourteen layers agree with *each other* on one document, and is a whole tree's geometry the geometry the layers promise? |
| `SuiteRoboLayoutIntrinsic.c` | `layout.intrinsic.*` | 16 | are min-content and max-content measured through the metrics seam rather than a byte count, do a box's children combine the way CSS says, and does an intrinsic dimension resolve from the answer? |
| `SuiteRoboLayoutFlex.c` | `layout.flex.*` | 24 | does `display: flex` establish a flex context, and does the pass lay its in-flow children out along a main axis with `flex-basis`/grow/shrink, the automatic minimum, wrapping, `auto` margins, baseline alignment, RTL and `order`? |
| `SuiteRoboLayoutGrid.c` | `layout.grid.*` | 24 | does `display: grid` establish a grid context, and does the pass size its two track lists from declarations and contents, create and size implicit tracks, place and span its items by the longhands and the auto-placement cursor (dense included), align them in their areas, and reverse the columns under `direction: rtl`? |
| `SuiteRoboLayoutTable.c` | `layout.table.*` | 18 | does `display: table` establish a table context, and does the pass read CSS 2.1's fixup rather than generating it — sizing its columns by the used `table-layout` algorithm, taking a row's height from its tallest cell, placing a caption on the block-start or block-end edge, using `border-spacing` as the gutters and zeroing them under `border-collapse: collapse` — with a cell that is a direct child of the table placed as an anonymous row and a column box given a declared width and no geometry? |

### 10.1 — the module surface (`layout.module.*`)

Eleven cases, and their subject is that the module's *self-description* is true:

- `lifecycle` — init twice is `RoboAlreadyExists`, shutdown is idempotent, and a
  shutdown frees nothing that was never the module's.
- `version` — `0.1.0` by string and by number, and the two agree.
- `capabilities` — every field, and the two that are 0 are asserted as 0.
- `geometry_flag` — `RoboLayoutGeometryImplemented`,
  `LayoutGeometryIsImplemented()` and `capabilities.geometry` agree.
- `geometry_aliases` — the alias identity is asserted at **compile time**, so a
  future edit that quietly defined a second point type fails the build.
- `object_kinds` — the enumeration walks the vocabulary and pins its count.
- `result_states` — same, for the three result states.
- `pass_outcomes` — same, for the three pass outcomes.
- `diagnostic_domains` — same, for the eleven domains.
- `diagnostics` — a reset record, a set record, a record that carries the domain.
- `allocator_balance` — the suite leaks nothing.

The pattern is 9.1's and 9.16's, reused: **enumerations are walked, not
spot-checked**, so an enumerator added later without a name is a failure rather
than a gap.

### 10.2 — the tree (`layout.tree.*`)

Fourteen cases, split between "one box is right" and "the arena is right":

| Case | Subject |
|---|---|
| `add.root` | the first box is the root, and its id is 1 |
| `add.second_root` | a second root is `RoboAlreadyExists` |
| `add.validation` | every refusal in §4.2 of the design record, one per `ROBO_*` |
| `relationships` | parent, both child ends, both sibling directions, `child_count`, `depth` |
| `object.style` | the borrowed style is the pointer that was handed in, not a copy |
| `object.display` | the display the box was created under |
| `misses` | a stale id, a NONE id and an id past the count are three reported misses |
| `reverse_lookup` | element → box, and `NONE` for an element that generated none |
| `borrowed_pointer` | a borrowed object pointer names what it named when it was taken |
| `growth` | **the case that found the realloc bug** — see §5 |
| `clear` | clearing releases every box and restarts ids at 1 |
| `lifecycle` | create, use, destroy; plus destroy-on-NULL |
| `result.before_pass` | a box's result is `NONE` before any pass has run |
| `allocator_balance` | 200 builds and destroys, under the Debug tracker |

`add.validation` is deliberately a *table* over the refusals rather than eleven
separate tests: the property being pinned is that each rule has a distinct answer,
which is a statement about the set.

### 10.3 — constraints (`layout.constraints.*`)

Nine cases, and the whole subject is one distinction:

```text
a field that was never set   ≠   a field that was set to zero
```

| Case | Subject |
|---|---|
| `empty` | cleared means "nothing is known", and every `has_*` is 0 |
| `set_get` | set then get, round-tripping every field |
| `zero_is_known` | a zero size **is accepted** and reads back as a known zero |
| `negative_refused` | a negative size is refused on all three setters |
| `validity` | min > max is *invalid* and is not repaired |
| `clamp_bounds` | min wins, max wins, neither is known, and `available` is not a cap |
| `clamp_contradiction` | a contradictory pair makes `clamp` answer `RoboNotFound` |
| `equality` | equality includes **which fields are set**, not only their values |
| `allocator_balance` | — |

`clamp_bounds` is where the design record's §8.1 claim is tested rather than
asserted in prose: a desired size larger than `available` comes back unchanged,
because filling available space is 10.3's decision and not this one's.

### 10.4 — the context and the pass (`layout.context.*`)

Eleven cases. This is the suite that pins the phase's central honesty claim.

| Case | Subject |
|---|---|
| `lifecycle` | create/destroy/reset, and that a context destroys no tree |
| `reset` | reset returns counters, constraints, viewport and outcome, and `passes` to 0 |
| `constraints` | set-and-read the record, including "none set" answering cleared |
| `viewport` | the viewport is recorded as available space, and an unset one answers `RoboNotFound` |
| `user_data` | the seam stores and returns what it was given, and dereferences nothing |
| `run` | a pass over a small tree: outcome `WALKED`, every counter |
| `run.empty` | a pass over an empty tree is legitimate and reports zero |
| `run.failures` | a NULL tree is outcome `FAILED` with counters at zero |
| `run.repeated` | **`passes` is cumulative, and the second pass re-unresolves** |
| `run.styles` | `without_style` counts the boxes that have no style |
| `allocator_balance` | — |

`run.repeated` is the case that found the non-cumulative `passes` bug, and its
other half is worth naming: it asserts that a box computed to a geometry by hand
between two passes comes back **`UNRESOLVED`** after the second. That is the
property `set_unresolved` exists for, and nothing else tests it.

### 10.5 — the builder (`layout.build.*`)

Ten cases. The rule under test is one line wide, so the suite is about its edges:

| Case | Subject |
|---|---|
| `root` | the root box is the document element, not the document node |
| `order` | children are linked in **document order**, and text nodes are not boxes |
| `deep` | a nested tree's depths, parent links and child counts |
| `display_none` | an element with `display: none` generates no box, and its element children still do |
| `root_display_none` | a `display: none` root produces an empty tree and a report, not a failure |
| `no_element` | a document with no element produces no boxes and no boxes' worth of error |
| `styles` | each box carries its element's style, and the style it carries is the one the source gave |
| `validation` | the builder's refusals: NULL tree, NULL document, a document that is not live |
| `rebuild` | a second build replaces the first, and ids restart |
| `allocator_balance` | — |

`display_none` is the load-bearing case: it asserts both halves of the rule at once
(the element with `none` has no box; its element child, which has no `display`
declaration, does). A builder that skipped the whole subtree would pass a naive
version of this test and fail the real one.

### 10.6 — the DOM + style bridge (`layout.dom.*`)

Nine cases, and they are the only ones in the module that stand up a real style
context:

| Case | Subject |
|---|---|
| `capture` | every element is captured, in document order |
| `styles` | every element that can have a style has one |
| `styles.uncovered` | an element the bridge does not cover keeps `NULL` rather than a partial style |
| `style_source` | the source answers from the captured styles, and answers nothing before a refresh |
| `inline_style` | an inline declaration reaches the computed style through the bridge |
| `relations` | the DOM relation between two elements survives the round trip through `CssElementRef` |
| `pipeline` | DOM + CSS → bridge → build → the tree's boxes carry the right computed `display` |
| `lifecycle` | **a tree is destroyed before its bridge**, and a second refresh releases the first generation |
| `allocator_balance` | every style the bridge owns is released exactly once |

`relations` is the case that pins §7.1 of the design record: the element reference
is the DOM node id, and an implementation that had built a private index would have
to keep a second table in step to pass it.

### 10.8 — formatting contexts (`layout.fc.*`)

Thirteen cases, and the phase has one idea to defend: **a box has two relationships
to formatting contexts, and the classification is a pure function of three facts.**
Every case below is one of those claims.

| Case | Subject |
|---|---|
| `kinds` | the four kinds walked, their names, and `is_block_container` for each — including an undefined enumerator |
| `classify.establishes` | **the classification table, in full**: every combination of display × block-level child × root |
| `classify.display` | the two predicates that are not the classifier: `is_block_container` and `can_establish`, per display value |
| `record` | a context record's fields, and field-by-field equality that includes identity |
| `tree.root` | the root box owns the initial context **and participates in it**; its kind, owner, parent, depth and participant count |
| `tree.transparent` | a block container holding a block-level child establishes nothing, and its child reaches the context above it |
| `tree.inline_boxes` | an inline box establishes nothing and participates in its parent's context |
| `tree.display_none` | a box that generates no box establishes nothing, and `establishes` answers `NONE` for the display |
| `tree.root_variants` | a root that computes to `inline` still establishes `ROOT` |
| `tree.add_invalidates` | **adding a box to a classified tree invalidates the classification**, and a context id read across it answers `RoboNotFound` |
| `lifecycle` | a pass on a tree with no root box, and classification released and rebuilt cleanly |
| `validation` | NULL arguments, a NONE id, an index past the count, and an unclassified tree |
| `allocator_balance` | the suite leaks nothing |

`classify.establishes` is the case the phase was built around. It walks the table
in §4 of the design record rather than a representative of each shape, because the
function's *order of questions* is the logic: a build that asked about children
before the root flag would classify a root with a block-level child as transparent,
and a build that asked about `flow-root` after the child question would replace a
CSS fact with a structural one. Both produce a context tree that looks plausible
and is wrong about one row, which is exactly what a table finds and a spot check
does not.

`tree.add_invalidates` found a production bug (§6.1).

### 10.9 — block layout (`layout.block.*`)

Sixteen cases, and they are the first in this directory that assert a **number**.
While the module computed nothing, a case could only ask whether a box had geometry
at all; asking *which* geometry was impossible. Now the assertions are arithmetic,
and they are exact — `x == 0`, `y == 100`, `height == 50` — because a block
algorithm's entire output is arithmetic over a document and "greater than zero"
would pin none of it.

| Case | Subject |
|---|---|
| `display_table` | the pipeline's multi-tag document: every box's exact rectangle |
| `stats` | the counters of one pass, and the record's equality |
| `preconditions` | a NULL tree, a negative viewport, and an **unclassified** tree |
| `single` | one box: it fills the viewport and its height is its content's |
| `siblings` | three siblings stack — y advances by each one's height |
| `nested` | a child is placed at **its parent's** origin, three levels deep |
| `explicit_height` | a declared `px` height is honoured, and is counted as `heights_explicit` |
| `unresolvable_height` | `50%`, `2em` and `10vw` each take the `auto` path rather than a guess |
| `inline_skipped` | an inline box is not placed, and is counted rather than dropped |
| `display_none` | a `none` box has no box at all, so it is absent from the counts too |
| `context_gate` | **the invariant**, walked over every box: block-level ⟹ block-direction context, and non-block context ⟹ never placed |
| `no_viewport` | a pass without a viewport places nothing and reports zero, without failing |
| `determinism` | two passes over one tree, and two trees from one document, agree box for box |
| `invalidation` | adding a box to a laid-out tree demotes its result — see §8 |
| `pipeline` | the real chain: tokenizer → parser → cascade → computed style → bridge → build → pass, over a document with margins, padding and borders that this phase must *not* apply |
| `allocator_balance` | — |

`context_gate` is the case the phase is built around, and its shape is what makes
it worth having: rather than asserting one expected instance of the invariant, it
walks every box in a tree containing a block box, a text-bearing block, an inline
box and a block-level box *inside* that inline box — and checks the implication at
each one. The last of those is the subtle one: it is block-level **and** in the
root's context, so both of the pass's gates say yes, and it still has no geometry
because nothing containing it was placed. It is counted as `unreached`, which is
the state 10.4's anonymous box exists to end.

`pipeline` is the only case here that goes through CSS rather than a scripted
style source, and its document deliberately carries `margin: 10px 20px`,
`padding: 8px 12px` and `border: 2px solid` alongside the geometry assertions —
because "this phase reads those values and applies none of them" is a claim worth
testing *with the values present*, and testing it against a document that omitted
them would prove nothing.

### 10.10 — inline layout (`layout.inline.*`)

Twenty-three cases, in five groups, in the order the phase is built: the
vocabulary, placement, wrapping, nesting, and the pass.

| Case | Subject |
|---|---|
| `stats` | the record's fields, its equality, and the invariant that `boxes_computed` is the two placement counts added |
| `display_table` | the three answers to "what is on the line" — atomic, transparent, declined — walked rather than spot-checked |
| `empty_context` | a block container holding only text establishes an inline context with **no participant and no line** |
| `single_atomic` | one `inline-block`: it is placed at the origin and its width is the viewport's |
| `siblings` | three atomics: the cursor advances by each one's width — `B.x == A.x + A.width`, using the results |
| `non_atomic_union` | a transparent box's rectangle is the union of what its contents reached, and the cursor advanced **once** |
| `textless` | **a `<span>` holding only text is zero-wide, and the case says why** |
| `atomic_fill` | an atomic inline with no resoluble width takes the containing block's content width, and with one takes it |
| `wrapping` | a participant that does not fit starts a new line, and the line moves down by the last one's height |
| `overflow_does_not_wrap` | a box wider than its line is placed anyway and overflows — the `cursor > 0` guard |
| `transparent_wraps_inside` | a transparent box is **not** wrapped as a unit: its contents wrap individually and it is the union of their fragments |
| `nested_rebase` | a participant inside a transparent box is rebased onto it, and the rebase stops at an atomic inline |
| `nested_two_levels` | the same through two depths, where the coordinate convention has to survive twice |
| `mixed_declined` | a block-level box among inline ones is counted as `block_level_declined`, not placed |
| `block_level_declined` | the mirror of 10.3's `inline_level_skipped`, asserted as its own fact |
| `atomic_contents_are_blocks` | an `inline-block`'s contents are stacked **by the block pass inside it**: a `<p>` inside a 300px span is exactly 300 wide and inside it, and the span's height is what the `<p>` reached |
| `display_none` | a `display: none` element generates no box, so it neither advances the cursor nor contributes an extent |
| `no_viewport` | an inline context has no line width without a containing block, so such a pass places nothing in **both** directions |
| `determinism` | two passes on one context, and a second tree from the same document — the first is what rules out a cumulative cursor |
| `invalidation` | adding a box to a laid-out tree demotes the geometry derived from the old classification |
| `viewport_changes_layout` | a narrower viewport re-places and re-wraps, so `lines` and `wraps` *move* |
| `pipeline` | the real chain — tokenizer → parser → cascade → computed style → bridge → build → pass — over a document whose `display` values come from a stylesheet |
| `allocator_balance` | — |

`non_atomic_union` and `textless` are the pair the phase is built around. The
first asserts the union arithmetic; the second asserts the zero it produces when
there is nothing to union — and asserting the zero *with the reason written into
the case* is what keeps it from being read as a bug. A suite that tested only the
union would leave the zero looking like an oversight, which is exactly the reading
`InlineLayout.md` §3.1 exists to prevent.

`transparent_wraps_inside` is the subtlest: it is the case a build that wrapped
non-atomic boxes as units would pass and a build that wrapped their *contents*
would pass differently, so the assertion is on the fragments rather than on the
box.

`determinism` is the first case in this directory that runs two passes on **one
context**. Every earlier determinism case called a helper that made a fresh context,
which proves the geometry is reproducible and says nothing about whether a second
pass stacks on the first — the failure a cumulative cursor has.

### 10.11 — text layout (`layout.text.*`)

Twenty-six cases, in six groups, in the order the phase is built: the model, reading
text, the builder's second rule, the font, the pass, and the pipeline. It is the
largest suite in the module and the reason is structural rather than thoroughness
for its own sake — three different questions now live under one name, and each has
its own fixtures.

| Case | Subject |
|---|---|
| `build` | a text node becomes a TEXT object, and the object's own model: no box, a derived inline display, its parent's style |
| `object_model` | `robo_layout_object_box` / `_text` refuse the other's kind, and the kind name is walked against the vocabulary |
| `font_measure` | **a synthetic face built in the suite** — cmap, hmtx, hhea, head, maxp, loca, glyf — asked for real advances, so "sum in font units and scale once" is asserted against arithmetic rather than a scripted number |
| `font_source` | the four answers to "measure this": no source, no callback, a detached font, and a real answer — the last two told apart by `RoboNotSupported` versus a width |
| `pass_measured` | a scripted measurer: the cursor advances by the width it answered, and the counters say `measured` |
| `pass_unmeasured` | **the zero, with its reason in the case** — the same document with no source places every run at width 0 and counts them `unmeasured` |
| `two_runs_and_rebase` | a run inside a transparent inline box is rebased onto it, and the box is the union of what its runs reached |
| `wrapping` | a run that does not fit starts a new line, and the run is a whole object rather than a fragment |
| `direction_in_pass` | LTR and RTL runs are counted, and direction came from the style rather than from the text |
| `block_context` | a text run in a **block**-direction context is `inline_level_skipped`, which is the identity of §3 closing over text objects |
| `relayout` | changed text, changed style, and a second pass all recompute the geometry |
| `pipeline` | the real chain: `font-size` through the cascade, resolved into font inputs, measured by the real font, read back out of a result |
| `pipeline_direction` | the same, with `direction` coming through the cascade |
| `accounting` | the identity over a document that holds text, inline boxes and atomic inlines at once |
| `allocator_balance` | — |

`font_measure` is the case worth naming. Every other case in this directory either
scripts a measurement or asserts a count; this one builds a **real face** — a
minimal but well-formed SFNT with its own tables — and asks RoboFont for the advance
of a code point it maps. A suite that only scripted the numbers could not tell
"the measurement seam works" from "the seam is called", and the arithmetic is what
makes the difference visible: a build that scaled per glyph rather than summing in
font units would fail it by a rounding error on a multi-glyph run.

`pass_unmeasured` is the counterpart, and it is deliberately the *same document*
run with no source. The module is legitimate in both configurations, so the two
cases differ by one argument rather than by a fixture, and the assertion is on the
counter that names which one happened.

### 10.12 — line boxes (`layout.line.*`)

Twelve cases. They are the first in the directory whose subject is a **record the
tree owns**, so most of them are about a relationship rather than a number: which
objects are on which line, and whether the answer is reachable from both ends.

| Case | Subject |
|---|---|
| `state_vocabulary` | the two states walked, their names, an undefined enumerator refused, and `FINAL` the only one a read can produce |
| `record` | the record's fields, its equality, and its rectangle formed from origin and size |
| `single_run` | one line for one run: `x == 0`, `y == 0`, width from the font, and `index == 0` |
| `members_are_placements` | **the invariant of LineBoxes.md §9** — the members summed over all lines equal the placements, and are **smaller** than the participant count, because a transparent box with contents is not a member |
| `empty_span_is_a_member` | a transparent inline box with **no** contents *is* a member, so the rule is about contents rather than about the display value |
| `wrapping` | three lines from content that does not fit, each with its own `index`, and `y` advancing by the previous line's height |
| `repeat_pass_replaces` | a second pass replaces the arena rather than appending to it, and ids restart at 1 |
| `empty_context` | an inline formatting context with nothing on it produces **no** line boxes, and the count and the record agree |
| `unmeasured_run` | a line whose only member is unmeasured exists, with width 0 and height 0 — distinct from no line at all |
| `released` | a released classification withdraws the lines with it, and a read across the invalidation answers `RoboNotFound` |
| `validation` | NULL arguments, a NONE id, an index past the count, and a member index past the line's own count |
| `allocator_balance` | four passes over a settled tree leave the live count where the settling pass found it, and destroying the tree returns the allocator to where the fixture found it |

`members_are_placements` is the case the phase is built around, and its shape is
what makes it worth having: it walks a document that **nests** inline boxes, where
the participant count and the member count differ, and asserts the difference rather
than the equality. A build that recorded a transparent box as a member would fail it
by double-counting; a build that skipped an empty transparent box would fail it by
one. Both are plausible implementations.

`allocator_balance` is also not the boilerplate case it looks like. The first pass
is the one that may grow the line arena and every later one must not, so the
baseline is taken **after** a settled pass rather than before it — an arena that
reserved on demand and kept what it reserved is not a leak, and a case that called
it one would fail as soon as the capacity fitted. What the loop asserts is the
*repeat*: four further passes over an unchanged tree leave the live count exactly
where the settled one found it.

### 10.13 — baselines (`layout.baseline.*`)

Sixteen cases in four groups: the record and its arithmetic, the two rules that
produce a metrics, the line and its members after a real pass, and the pass itself.
They are the first cases in the directory that read a *placement* rather than a
rectangle: what a member's y means is now a function of its own ascent, so most of
them are about a relationship between two objects.

**The vocabulary and the arithmetic** — `kind_vocabulary` (the three kinds walked,
with `NONE` the one the validity predicate refuses), `metrics_record` (the record's
fields, its equality, the extent, and the offset conversion), `metrics_from_run`
(the half-leading rule: an even gap splits evenly, an **odd** gap puts its spare
pixel below and still sums to the run's height, no gap leaves the terms alone, an
unmeasured run is a TEXT baseline of zero extent rather than a NONE one, a height
smaller than `ascent + descent` is read as no gap, and negative terms are clamped
**before** the gap is recovered), and `metrics_from_extent` (the bottom-edge
fallback, and the clamping of its extent).

**A line, after a real pass** — `line_from_text` (one run, one line: the kind is
TEXT, the two terms are the font's with the gap split, and the three numbers cannot
disagree because the height *is* their sum), and `object_metrics` (a run's own
metrics through the object door; a transparent box that placed nothing reported
`RoboNotFound` rather than a zero floor — while *still* having geometry, which is
what makes that a miss about membership and not about placement).

| Case | Subject |
|---|---|
| `shared_floor` | two runs of **different fonts** — 20/5 against 8/2, from a measurer that answers per code point — whose own baselines land on one floor, read through `layout_baseline_y_in_owner` because a nested member's stored y is parent-relative |
| `mixed_size_is_not_top_aligned` | two atomics of different declared heights share the floor, so their **feet coincide**; before this phase the shorter one's bottom edge was 30px above the taller one's, which is the failure the case names |
| `descent_deepens_the_line` | a run whose descent is large and ascent small moves the line's **floor** down, and the member sits at the line's top because the line's ascent *is* its ascent — the check that the maximum is read the right way round |
| `multiple_lines` | three lines from content that wraps, each with its own baseline, stacked by `ascent + descent`, and each counted as a text-baseline line |
| `empty_content` | the three different empty answers: no inline content (no line at all), an empty inline box (a line, `BOX`, 0/0, one fallback line), and unmeasured text (a line, `TEXT`, 0/0) — then the same text **measured** as zero, so the difference is the measurement and not the numbers |
| `relayout` | a second pass with different metrics moves the floor on the **same line id**, which pins that a baseline is replaced rather than accumulated; a narrower viewport then re-wraps to three lines with three baselines |
| `determinism` | the same pass twice with unchanged input gives an equal line record and equal inline stats, and the line's kind is `BOX` because the 18px atomic out-reaches both text runs — the first-member-among-the-tallest rule, asserted so it cannot be quietly rewritten into "prefer text" |
| `accounting` | the phase's invariant, over **every** line of a wrapped document (§12 of the design record) |
| `validation` | NULL and NONE arguments on every door, and a hand-built line record refused by both baseline doors — because the arena would not have stored it |
| `allocator_balance` | the phase added no arena: a settled pass's live count is unchanged by four more, and destroying the tree returns the allocator to where the fixture found it |

The fixture carries a lesson of its own, and it is recorded here because it is the
kind of mistake that reads as an implementation bug. A rule array grew an entry while
the call site kept its old count literal, so the **last rules were silently
dropped** — and a dropped rule shows up as a box with the initial display (or an
atomic with no declared size), which looks exactly like a layout defect. The array
size is now derived from the array (`LAYOUT_BASELINE_RULE_COUNT`), and
`layout_baseline_style_add` reads each rule's display back out of the style it just
built, so a rule the container refused fails at the rule rather than three assertions
later in the geometry.

### 10.14 — sizing (`layout.sizing.*`)

Seventeen cases in three groups: the vocabulary, the record, and the pass. The
third group is the one that is different from every other group in this directory,
because it drives a **fabricated computed style** rather than either a stylesheet or
the shared scripted fixture.

That is not a shortcut. No property RoboCSS registers feeds the clamping path, so
the only way to prove that path is **live** rather than merely present is to hand
the pass a style that declares `min-width` and its three siblings. A case built from
a stylesheet could only prove that a property the property table does not know does
nothing, which is a fact about the table.

**The vocabulary** — `dimension` (the three kinds walked, with a cleared record
reading as `auto` rather than as a zero length, a non-definite length read as a
*miss* rather than a zero, and a negative length refused as a value) and
`vocabulary` (the kind and source predicates, the names, and a count asserted for
each).

**The record** — `record` (the two availability flags as separate facts, the
refusals on every setter, a minimum above a maximum reading as *invalid* rather than
as an answer, and every getter answering for NULL instead of failing), `width` and
`height` (the three resolution branches each, the two *different* failures — no
available width against no contents extent — and a known zero resolving where an
absent input does not), `clamp` (both bounds, minimum winning a tie, the clamped
flags, a contradictory pair reported rather than resolved), and `reresolve` (a
changed available width, a declared width arriving on top of a filled one with the
source following it, and five resolves over unchanged inputs giving an equal
record).

**The seam** — `seam` (all four classifications against fabricated styles: absent
and `auto` as one kind, `inherit` as `UNRESOLVED`, `10.5px` rounded **up** to 11, a
percentage and an `em` as `UNRESOLVED`, a property the style does not declare as
`auto` even in a rich style, and `out` left untouched when the question cannot be
asked).

**The pass** — the group that reads geometry:

| Case | Subject |
|---|---|
| `block_declared_width` | a 300px box inside an 800px viewport, **and its child**: 300 both times, with the counters splitting one declared width from three filled ones |
| `block_auto_width` | an `auto` chain fills the containing block at every level, and a second pass at another viewport is another document |
| `block_unresolved_width` | `width: 50%` read as `UNRESOLVED` **through the seam on the very style the pass used**, and resolved by filling the containing block — the two halves of the merge, asserted separately |
| `block_keyword_and_unit` | a declared `width: auto` and a `2em` height, which are the same two branches reached by *declaring* rather than by omitting — the case that proves the keyword and the relative unit are read, not guessed |
| `block_bounds` | the clamping path driven end to end by a fabricated style, with all four bounds declared: 300 clamped down to 200, 0 raised to 44, and the parent unchanged |
| `block_half_bound` | a `max-width` on its own changing **nothing**, because a bound is recorded only when both of its dimensions name a length |
| `atomic_inline` | an `inline-block`'s declared width and its height from the contents it had to be sized *before* walking |
| `line_regression` | over a document that wraps: every line is as wide as what it **placed** (150, then 90) where the width it had to fit is 200, so the sizing layer did not turn each line into a CSS box |
| `allocator_balance` | two passes at two viewports over one tree, with the declared 250 unmoved — and nothing leaked |

**The fixture's own lesson, and it is the second phase in a row to learn it.** The
first cut of these cases indexed the arena (`layout_sizing_box_at(&fixture, 2u)`) on
the assumption that the third box is the `<div>`. It is not: RoboHTML's tree builder
inserts an implicit `<head>`, so index 2 is `<body>` — and a declared width that
"does not reach geometry" is exactly what a measurement of the wrong box looks like.
The suite now resolves elements **by tag** (`layout_sizing_box_for`), which is what
the block suite does for its card and for the same reason: a fixture's *indices* are
a fact about the parser, and counting them asserts the wrong thing the day the parser
gains one more element.

### 10.15 — margins (`layout.margins.*`)

Twenty-five cases in four groups: the vocabulary, the seam and its policies, the
record, and the two passes that consume it. Like `layout.sizing.*` this suite drives
**fabricated computed styles** where the subject is a policy rather than a parse —
no stylesheet reaches the `auto`-margin branch with a containing block that makes the
answer interesting, so the cases that care hand the pass the style they mean.

**The vocabulary** — `kind_vocabulary` walks the four kinds (declaring each name and
predicate, with an absent side as a **fourth** kind rather than a declared zero, and
a count asserted), and `one_side`/`sides` read a single margin and a four-side record
back through its doors.

**The seam and the policies** — `seam` covers the one CSS read the phase adds: a
declared `%`, `em` or `auto` classified rather than folded, a negative `px` length
kept **signed** where the sizing seam refuses one, and an `out` left untouched when
the question cannot be asked. `edges_record` (the used-edges record and its one
clamped number), `block_basic`/`block_sides` (the block policy over each side),
`block_negative`, `block_auto_center`, `block_auto_odd`,
`block_auto_under_auto_width`, `block_overconstrained`, `block_unresolved` and
`block_vertical_auto` are the policy's edge cases, and `inline_policy` is the other
policy — the one that does not distribute an `auto`.

**The pass** — the group that reads geometry:

| Case | Subject |
|---|---|
| `block_geometry` | a box at its **margin origin** rather than its border origin, and its own content size unmoved by the margin — the phase's central claim, seen in a coordinate |
| `auto_center_geometry` | `margin: 0 auto` in an 800px parent putting a 300px box at 250, which is where a symmetric distribution lands |
| `margin_box_flows_down` | the next sibling placed at `top + height + bottom`, where the box's own extent is only `height` |
| `block_stacking` | several stacked blocks, so a bottom margin that reached the position of the block *after* the next one is a failure |
| `block_no_collapse` | **two 20px margins meeting at 40**, and not 20 — the non-collapsing behavior asserted as a value, which is what makes "collapsing is not implemented" a claim rather than an omission |
| `inline_advance` | an atomic inline's advance including its horizontal margins, because the text after it starts where the margin ends |
| `inline_baseline` | an atomic inline's baseline at its bottom **margin** edge, so that an inline-block with a bottom margin sits higher than one without |
| `object_door` | the used margins kept on the object and read back through the object's own accessor rather than a side table |
| `relayout` | a changed margin recomputing the affected geometry on a second pass over the same tree |
| `allocator_balance` | two passes at two viewports over one tree, with nothing leaked |

**What the group is shaped against.** Every case that reads a coordinate does so
through `layout_margin_origin_of`/`layout_margin_box_for`, which resolve an element
**by tag** — the lesson 10.8 learned twice in a row and the reason these cases were
written that way on the first build.

### 10.16 — borders (`layout.borders.*`)

Twenty-two cases in five groups, in the order the phase was built: the vocabularies,
the seam, the used values, the two derived numbers, and the passes. Like the two
suites before it, it drives **fabricated computed styles** — the rules it needs
(`thin`, `none`, a `%`, a `var()`-flagged length) reach the branch under test through
the real computed-value makers, so a case about the seam fails because the seam is
wrong and not because a stylesheet parsed differently.

**The vocabularies** — `kind_vocabulary` (the three kinds, their names, and the three
width-keyword constants asserted as **values**), `width_record` (the length door, the
negative refusal, and an equality that ignores a stale length on a side nobody
declared) and `side_record`, which walks `generates` over **all ten** styles rather
than a representative, because the rule the field claims *is* a table over that
vocabulary.

**The seam** — `seam` is the twelve-longhand read: three width keywords becoming
1/3/5, the colour record carried unaltered, `border-color: red blue green black`
landing on four sides, `currentColor` surviving as contextual, and every declaration
this build refuses arriving as `UNRESOLVED` — an `em`, a `%`, a negative length, the
forbidden `auto`, and a `var()`-flagged px, which is a fifth reason on the same branch.

**The used values** — `edges_record` is the fourth rule in one record: a declared five
whose style does not draw is a used zero, and the three counters are asserted beside
the four widths. It also walks every way a hand-built record can fail validation,
including the invariant rule 4 *is* (a used width above zero implies the style draws).

**The two numbers** — `derived_boxes` drives both directions, the zero floor, the
saturation at the seam's maximum, and the refusal of a record no size can come from.

**The pass** — the group that reads geometry, and it follows the margins and padding
suites' convention of resolving elements **by tag**:

| Case | Subject |
|---|---|
| `block_declared_width` | `width: 200px` with 5 of border each side is **210** on screen and **0** from its containing block's origin — the phase's central claim in two numbers |
| `block_auto_width` | an `auto` width still filling the containing block exactly, which is the case a build that grew the box *outward* fails |
| `block_sides` | four different widths at once, so each contributes its own term and none is read from another |
| `block_vertical` | `border-top` moving the contents down, `border-bottom` growing the box, and the origin unchanged |
| `block_stacking` | the next sibling at `top + height + bottom` of the child's *border* box plus its margin |
| `block_unresolved` | a border this build cannot use taking no room and being **counted** |
| `none_style` | twelve declarations, none drawing, and both counters asserted beside the geometry — the case that fails a build which used the declared width |
| `margin_interaction` | margin outside, border inside, in one box: origin 10, size 120 × 40, next sibling at 60 |
| `padding_interaction` | the content origin as a **two-term sum**: `border-left: 5px` with `padding-left: 10px` is 15, not 10 and not 5 |
| `children_get_content_width` | the border charged twice down the tree, once at each level |
| `inline_atomic` | an atomic inline's advance including its borders, and an `auto` width filling what they left |
| `inline_transparent_not_applied` | the transparent inline box taking none, with the door saying so rather than answering a zero |
| `object_door` | the used record read back through the object's own accessor, and the three refusals |
| `relayout` | a width change moving the geometry and a **style-only** change moving none of it while still showing up in the counters |
| `determinism`, `allocator_balance` | identical input, identical output; and no leak across passes |

**What the group is shaped against.** `robo_layout_border_rebuild` is the one step a
changed declaration needs, and it is in the fixture rather than in the pass: the tree
**borrows** one computed style per element, so a replacement that left the old tree
standing would leave it holding a destroyed style. The margins and padding suites
learned that; this one copies it rather than rediscovering it.

### 10.7 — conformance (`layout.conformance.*`)

Seven cases, and the shape is 9.16's: ask the questions no single-module suite can.

| Case | Subject |
|---|---|
| `capabilities` | the capability struct walked **positionally** against a hand-written name list, with the struct's size pinned against the list's length — now twenty-one fields, with `geometry`, `formatting_contexts`, `block_layout`, `inline_layout`, the derived `block_and_inline_layout`, `text_layout`, `line_boxes`, `baselines`, `sizing`, `margins`, `padding`, `border`, `positioning` and `overflow` at 1 and `painting` at 0, and the count of implemented fields asserted as twenty |
| `statement` | the module's central claims, each an assertion: `geometry == 1`, version `0.11.0`, API unfrozen, three object kinds and four context kinds |
| `no_viewport` | **`geometry == 1` while nothing was placed**: a context with no recorded viewport runs no layout at all, and every box stays `UNRESOLVED` |
| `pipeline` | the whole chain over a realistic document: tokenizer → parser → match → cascade → inheritance → computation → bridge → build → pass |
| `determinism` | two builds of the same document produce equal trees, compared box by box |
| `robust` | a corpus of hostile and malformed documents: every one is either a clean failure or a clean empty build, and none crashes |
| `allocator_balance` | the whole chain, built and torn down repeatedly |

`capabilities` is a copy of the 9.16 finding applied one level up. In 9.16, two
CSS capability flags had flipped to 1 with no assertion anywhere in the build;
this suite walks every RoboLayout field, so the same omission cannot happen here —
and 10.2 and 10.3 are the two phases that *used* it, flipping
`formatting_contexts`, then `geometry` and `block_layout`, with the walk already in
place.

The case 10.3 had to **replace** is worth recording, because the way it went is the
way this file §3 said these claims should go. 10.1 and 10.2 shipped
`layout.conformance.no_geometry`, whose whole subject was that after a real pass
every box was `UNRESOLVED`. The day a pass computed a coordinate it failed —
exactly as intended — and its replacement is `no_viewport`, which asserts the
*sharper* fact that was true all along and is still true: a build that computes
coordinates and a pass that had no containing block to compute them into are two
different statements, and the capability field is the wider one. The claim was
replaced rather than weakened, and the numbers took over `layout.block.*`.

---

## 2. Fixtures

`RoboLayoutTestSupport.h` is the only fixture header, and it supplies four things:

| Helper | Purpose |
|---|---|
| `layout_document_of(markup)` | parse markup into a live document and fail the test if it cannot |
| `layout_stub_style(name)` / a stub style source | build a tree **with no CSS at all**, which is what makes §10.2–10.5 possible |
| a real-DOM style fixture | stand up a style context and its bridge, for §10.6–10.7 only |
| equality helpers | compare two trees box by box, for `determinism` |
| a scripted text measurer | answer a width per code point, so a case about cursor arithmetic fails because the arithmetic is wrong and not because a font differed — `layout.text.*` and `layout.line.*` |
| a synthetic SFNT face | cmap, hmtx, hhea, head, maxp, loca and glyf built in the suite, for the one case whose subject *is* the real RoboFont answer |

The second row is the design record's §6.1 argument made concrete. Twelve of the
seventeen suites never instantiate a style context at all — `layout.borders.*` is the
newest of the twelve, and the closest relative of the one before it: twelve longhands
read out of a fabricated style source directly, with no `auto` to distribute and
therefore no second policy to script. `layout.margins.*` is next, and the *most* scripted of
them: a four-side record, an `auto` side and a **signed** length are read out of a
fabricated style source directly, neither a stylesheet nor the shared scripted
fixture. `layout.sizing.*` is the next
newest: the declaration it needs (`width: 300px`) goes through the real RoboCSS
specified-value and computation doors and then into a style source **directly** —
neither a stylesheet nor the shared scripted fixture (§10.14) — and the five that do
reach for one do so solely in the cases whose subject *is* the joining: `layout.dom.*` and
`layout.conformance.*` throughout, and the single `pipeline` case in
`layout.block.*`, `layout.inline.*` and `layout.text.*`. `layout.fc.*` is one of the six, which is
what makes §10.8 able to assert the classification table as a table — a rule that
could only be observed by standing up a stylesheet and walking a document is a rule
whose table cannot be written down. `layout.inline.*` is another for all of its
twenty-two remaining cases: its fixtures are *scripted* computed styles, so a case
whose subject is cursor arithmetic fails because the arithmetic is wrong and not
because a selector did not match.

The per-context stub source answers **only the elements it was told about**, and
this is not a detail. An early version answered every element, which made
`run.styles` pass for the wrong reason: the test intended "this box has no style"
and the fixture supplied one. It now answers by name, so `without_style` counts
what the test meant it to count.

---

## 3. What the suites do and do not assert

They **do** assert:

- exact `Result` values for every refusal, because "it failed" is not a
  contract;
- that a record's *unset* state is distinguishable from its zero value;
- that a borrowed pointer is the pointer that was handed in, not an equal copy;
- that ids are dense, tree-scoped, and restarted by a clear;
- that a second pass re-unresolves what the first left;
- that the module leaks nothing, against the Debug allocator tracker.

They **do not** assert:

- CSS behaviour beyond `display` and a declared `height`, which 9.1–9.16 tests in
  its own 609 cases;
- a geometry the pass does not produce: no percentage resolved against a containing
  *dimension*, no intrinsic keyword, no shrink-to-fit, no margin, padding or border
  arithmetic and no **fragmentation**. Those numbers do not exist yet, and a suite
  that asserted one would be asserting a number the module does not compute. (Two
  entries this bullet used to carry have since arrived — a **baseline** in 10.7 and a
  declared `width` in the 10.8 step below — which is how the list is meant to work:
  a claim leaves it when a case can assert the number instead.) The inline direction's
  numbers *do* exist and are asserted exactly — and 10.6's suite names the zero it
  produces for a line whose only member is unmeasured as a *fact* rather than
  omitting the case, which is the same discipline 10.4 applied to a text-bearing
  `<span>` and 10.5 retired by making the measurement exist.

That last bullet is the *shape* of §§10.1–10.3's honesty claims, and it is worth
stating how a claim like that is supposed to die. `layout.conformance.no_geometry`
asserted, in 10.1 and 10.2, that after a real pass every box was `UNRESOLVED`. It
failed the moment 10.3 computed a coordinate — and it was **replaced** rather than
weakened, by `no_viewport`, which asserts the sharper fact that is still true (a
build that computes coordinates versus a pass that had no containing block to
compute them into), while the numbers moved to `layout.block.*`. A claim that only
gets deleted leaves nothing behind; a claim that gets replaced by the sharper one
keeps what was actually being defended.

They **do** assert, since 10.2, the context topology: a parent link, a depth, a
participant count and a kind — because those are facts this build produces, and the
counts alone do not distinguish a correctly nested context tree from a flat one.

And they **do** assert, since 10.3, the geometry: exact `x`, `y`, `width` and
`height` per box, plus the per-box result **state**, which is what distinguishes
"placed here" from "not placed at all" from "no pass has run". Since 10.4 that is
true of both directions, and the assertions are grouped by direction because the two
records are — `layout.block.*` asks about the block extent, `layout.inline.*` about
the line.

---

## 4. Running

```sh
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Test
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Release -Test
./Out/RoboTest.host.exe --filter layout.
./Out/RoboTest.host.exe --filter layout.tree
./Out/RoboTest.host.exe --filter layout.conformance
```

| Filter | Cases |
|---|---|
| `layout.` | 346 — this module's 344, plus `demo.layout.flow` and `demo.layout.resize` |
| `layout` (no dot) | 348 — the above, plus `font.ot.layout` and `graphics.text.layout`, which are other modules' |
| `layout.module` | 11 |
| `layout.tree` | 14 |
| `layout.constraints` | 9 |
| `layout.context` | 11 |
| `layout.build` | 10 |
| `layout.dom` | 9 |
| `layout.fc` | 13 |
| `layout.block` | 16 |
| `layout.inline` | 23 |
| `layout.text` | 26 |
| `layout.line` | 12 |
| `layout.baseline` | 16 |
| `layout.sizing` | 17 |
| `layout.margins` | 25 |
| `layout.paddings` | 20 |
| `layout.borders` | 22 |
| `layout.positioning` | 20 |
| `layout.overflow` | 26 |
| `layout.invalidation` | 25 |
| `layout.conformance` | 7 |
| `layout.integration` | 12 |

Whole suite: **2367** tests, green in both configurations.

```sh
./Out/RoboTest.host.exe --filter layout.block
./Out/RoboTest.host.exe --filter layout.block.context_gate
```

---

## 5. Sanitizers

```sh
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Test -Sanitize
```

ASan and UBSan run the same 2367 cases. In 10.15 the sanitized **build** reported
`ALL TESTS PASS (Debug)` — 2367/2367 with `SANITIZE PASS` and no sanitizer diagnostic —
and a direct run of the same binary minutes later was **2365/2367**, with
`window.host.keyboard` and `window.host.mouse` the only red cases (§20). Under the
sanitizers `layout.` was 346/346 and `layout.integration.*` 12/12. In 10.14's run the
sanitized suite was
**2355/2355** with **no sanitizer diagnostic and no red case at all**, and
`layout.overflow.*` was 26/26 and `css.overflow.*` 7/7 under them. In 10.12's run the
sanitized suite was
**2296/2296** with **no sanitizer diagnostic and no red case at all**, and
`layout.positioning.*` was 19/19 under them. In 10.11's run the sanitized suite was
**2277/2277** with **no sanitizer diagnostic and no red case at all**, and
`layout.borders.*` was 22/22 under them. In 10.10's run the sanitized suite was 2255
with **no sanitizer diagnostic** and `window.host.keyboard` and `window.host.mouse`
as its only red cases — both the documented environment-dependent flake rather than a
finding, since both pass in isolation and both fail with `RoboNotSupported` from
`SetForegroundWindow`; `layout.paddings.*` was 20/20 under the sanitizers. In 10.9's
run the sanitized suite was 2235 with no sanitizer diagnostic and
`window.host.keyboard` as its only red case. 10.8's run was
**2210/2210** the same way. 10.7's run was
**2193/2193** the same way. 10.6's run was the
same count; in 10.5's run the sanitized suite was 2177 with no sanitizer diagnostic
and the two host-window cases below as its only red cases. In 10.4's run the
sanitized suite was **2139/2139** — no sanitizer diagnostic, and no red case. 10.3's
run was 2116/2116 the same way.

That is worth stating precisely, because an earlier phase's sanitized run
reported `window.host.keyboard` and `window.host.mouse` and both are
environment-dependent rather than sanitizer findings: `docs/Build.md` §6 records
them as a host-desktop flake (`SetForegroundWindow` declining with `err=0`, so
`win32_focus` reports `RoboNotSupported` rather than faking success). The same
flake turned up in one unsanitized `robo test-runtime` run during 10.2 — 2099 of
2100, with `window.host.keyboard` the only red case — while the direct suite runs,
both unsanitized configurations and the sanitized run were all fully green.

In 10.3 it turned up in three consecutive `robo test-runtime` runs (2115 of 2116,
`window.host.keyboard` every time), and the evidence is now as clean as it is going
to get:

| How the suite was run | Result |
|---|---|
| `robo test-runtime` (the pipeline: build, then run) | **2115/2116** — three times in 10.3; **2138/2139** once in 10.4; **2177/2179** once in 10.6; **2193/2193** in 10.7; **2254/2255** in 10.10; **2276/2277** in 10.11; **2254/2255** in 10.12; **2329/2330** twice in 10.13; **2354/2355** once in 10.14 |
| the same binary, run directly afterwards | **2116/2116** — twice in 10.3; **2139/2139** twice in 10.4; **2177/2177** twice in 10.6, and **2175/2177** once when the console held the foreground; **2193/2193** in 10.7; **2254/2255** in 10.10; **2276/2277** on 10.11's first Release run and **2277/2277** on the next; **2295/2295** in 10.12 (direct); **2330/2330** in 10.13, in Debug and Release both; **2355/2355** in 10.14, in Debug and on Release's first retry |
| `--filter=window.host` in isolation | **19/19** — three times in each phase |
| the binary as a child of `pwsh` with its output captured | **passes** |

`SuiteRoboWindowHost.c` and everything under `RoboWindow/platform/win32/` are
untouched by either phase — 10.3's diff is `LayoutBlock.c`, five edits in the
layout module, and the layout suites; 10.4's is `RoboLayoutInline.c`, `git status`
reporting **no** file matching `window` — so there is no causal path from either to
the failure, and the case's own subject (keyboard routing) passes every time it is
run against a session whose console holds the foreground. The fourth row is the one that
rules out the obvious explanation in the other direction: it is not the redirection,
not the parent process, and not the flag — it is that the pipeline's whole-suite run
begins *immediately after a six-minute build*, and Windows grants foreground
activation on a rule about who received the last input event.

The project already names the case in its own build script — `test-html` passes
`-BuildOnly` so that "an unrelated failure (a host window suite with no foreground
session) would not be reported against the component the developer actually asked
about" — and `win32_focus` reports `RoboNotSupported` rather than faking success,
which is the behaviour the case is asserting. So it is recorded here rather than
worked around, and **not** weakened: the assertion is right and the OS denial is the
anomaly. Nothing in `layout.*` touches it and nothing in `window.host.*` touches
RoboLayout.

No `layout.*` case touches it, and no `window.host.*` case touches RoboLayout.

10.4's run is the second phase in a row to find it and the first to find **only**
it: `robo test-runtime` aborted at the Debug step with 2138 of 2139 and
`window.host.keyboard` as the single red case, so the pipeline never reached
Release. That is the failure mode the build script's own comment describes, and
since the *same binary* was 2139/2139 twice within the minute and `--filter=window.host`
was 19/19 three times, it was recorded rather than worked around.

---

## 6. What the 10.1 suites found

Five things, and the first two are production bugs.

**1. A dangling parent when the arena grows** (`layout.tree.growth`).
`LayoutTreeAddObject` resolved the borrowed `parent` pointer *before* the
reserve that can move the arena. A tree that outgrew its initial capacity linked a
new box to a freed address — invisible below the growth boundary, a segfault above
it. Fixed by resolving the parent after the reserve.

The general lesson is worth the space: `growth` is the only case in the suite whose
stated purpose is to push a container past a boundary, and it is the *only* one
that could have found this. A suite of cases that each stay inside comfortable
limits tests the arithmetic and none of the allocation.

**2. `passes` was not cumulative** (`layout.context.run.repeated`).
It reported the last pass's count. Every single-pass case passed, because every
other case runs one pass. The case was written to ask the question, not because
anything looked wrong.

**3. The test registry had a fixed cap** (`runtime/Tests/Framework/RoboTest.c`).
Seven new suites overflowed a fixed-size array of registered suites. Two options
existed: grow the array, or fail loudly. It now fails loudly — a suite that is
silently not registered is a suite that is silently not run, which is the worst
possible outcome for a test framework, and this one had been one suite away from
it for some time.

**4. Two Release-only test defects, pre-existing** (`SuiteRoboHTMLDom.c`,
`SuiteRoboCssCascadeCollect.c`, `SuiteRoboCssComputedValue.c`).
A debug-only variable whose only use is inside `#if RoboMemDebug`, and two
assertions on the Debug allocator's bookkeeping written unconditionally. Because
the project's default configuration is Debug, Release `-Test` had never been
exercised across the whole suite. All three are now guarded, and both
configurations are green.

This is the finding with the widest reach: Phase 10.1 is the first thing to have
run 2087 tests in Release, and it found three defects that have nothing to do with
layout. Keeping both configurations in the gate is worth more than the extra build.

**5. The `-Verify` boundary scan needed a RoboLayout arm** (`Tools/Build.ps1`).
RoboCSS and RoboDisplay each had one. RoboLayout is the first module whose
forbidden set includes layers **below** it — RoboWindow and RoboDisplay — for a
reason that is not layering: a layout pass that consulted a window could not be
tested without one. Its scan is therefore the first that is not purely "no upward
edges", and the reason the viewport is an argument rather than a query is now
enforced rather than merely documented.

---

## 7. What the 10.2 suites found

Three things, and the third is the reason the first two are worth the space.

**1. A stale context id read across an invalidation** (`layout.fc.tree.add_invalidates`).
A box's context id kept its value after the classification was released, so a read
*after* a re-classification resolved the old id against the new context array —
answering with a context that is not the one the caller asked about, and answering
`RoboOk` while doing it. `tree.add_invalidates` is the only case whose stated
purpose is to add a box to an already-classified tree. Fixed by having both context
doors report `RoboNotFound` for a box in an unclassified tree, which is the
truth; the headers now say which door distinguishes "no pass has run" from
"classified and since released".

**2. A `const` tree in a signature that mutated through it** (`RoboLayoutContext.c`).
One internal helper took the tree `const` while writing every box's result, and the
compiler had nothing to say because the write went through a `const`-stripped
pointer one level down. Corrected rather than cast away: §10.2 of the design record
already argues that a pass takes its tree mutable, so the signature now says so at
every level in the call graph.

**3. Two wrong expectations in the phase's own suite, both about nesting**
(`layout.dom.pipeline`). The case first asserted that the deepest context in the
pipeline document sat at depth 1 and that the root context had six participants.
The tree says 2 and 5, and the tree is right: `<p>` sits inside the atomic inline,
so its inline context nests inside the panel's block context; and the root's
participants are its own owner plus the three boxes that reach it through a
transparent `<body>`.

The expectations were replaced rather than merely corrected. The case now asserts
the **parent link and the depth** of the nested pair as well as the counts, and
that is the assertion a flat-but-correctly-counted context tree fails. A phase that
had only counted contexts would have shipped with both facts wrong and found out in
10.12.

---

## 8. What the 10.3 suites found

One production bug and two fixture problems, and the first is the one the phase is
worth.

**1. An invalidation released the classification but kept the geometry derived from
it** (`layout.block.invalidation`). 10.2's `LayoutTreeContextsReset`
dropped the context arena and cleared the classified flag, and left every box's
`LayoutResult` exactly as it was. That was *correct* while nothing derived
geometry from the classification. From 10.3 on the block pass reads its placement
gate straight out of `object->context` and takes a box's height from the extent of
the children that gate let through — so a result surviving an invalidation was a
`COMPUTED` state asserting a coordinate about a tree that no longer existed, which
is the one thing that state is not allowed to mean.

The fix went into the **shared** path rather than the invalidation door, because
all three callers of `contexts_reset` — an explicit invalidation, a clear, and an
add — are saying the same thing, and a rule written in three places is a rule two
of them eventually disagree about. The case that found it is the only one whose
stated subject is "place boxes, then add one", which is *exactly* the sequence 10.2's
`layout.fc.tree.add_invalidates` covers one level up: both phases found a stale fact
on the same code path, and the second one only became reachable when something
started depending on the first.

**2. A fixture that promised a state the builder cannot produce**
(`RoboLayoutTestSupport.h`). The block suite's invalidation case needed a document
it could *add an element to*, and the fixture's first attempt built one through the
tree builder — except the builder finalizes at EOF, so no document it produces can
ever accept a new element. The attempt to fix that by leaving the document
un-finalized turned out to depend on a state the builder never produces. The
fixture took the honest tool instead (`robo_html_document_create_element` over a
live document), and the production builder was left alone: a fixture working around
a limitation is one thing, a fixture *editing production to suit itself* is how a
suite stops testing the thing it names.

**3. Three stale suites the phase's numbers invalidated.** `layout.module`'s
version and capability pins, `layout.conformance.statement`'s `geometry == 0`
claims, and `layout.conformance`'s `no_geometry` case all had to move with the
phase. None was weakened and none was deleted: the version pins were re-pinned, the
`statement` case now asserts `1`, and `no_geometry` was replaced by `no_viewport`
(§3, §10.7) — the sharper claim that survived.

There was no fourth finding. The block algorithm itself came out right on the first
build — which is reported as a fact about the phase and not as a virtue: the nine
suites around it had already pinned every input the pass reads (the placement gate,
the containing-block convention, the display vocabulary), so the arithmetic had
nowhere to hide.

---

## 9. What the 10.4 suites found

No production bug, and two mistakes in the phase's own suite — which is the
interesting shape, because 10.3's single production bug was in the *shared* path and
this phase inherited the fix rather than repeating the mistake.

**1. A fixture that declared one tag twice, and a lookup that silently absorbed it**
(`SuiteRoboLayoutInline.c`). The suite's shared `LAYOUT_INLINE_BLOCK_RULES` macro
declares the three containers every case shares — `html`, `body`, `p` — and one case
that needed a *sized* paragraph appended a second `p` rule after it. The fixture's
style lookup answers with the **first** match by tag name, so the case silently ran
against the macro's height-less `<p>`, and the failure surfaced as a span that was
zero tall rather than as a rule that never applied.

The rule in the fixture rather than in the case was the right place to fix it, and
the fix is a **refusal**: `layout_inline_style_add` now asserts the tag is not
already declared, so two rules for one element is an immediate failure with a
readable message instead of a shadowed declaration. A fixture is a script, and a
script with two lines for one element is a mistake in the test rather than a shape
a document can have.

**2. Object counts that ignored the parser's own `<head>`**
(`layout.inline.no_viewport`). The case asserted four boxes for a four-tag markup
string; RoboHTML's tree builder inserts the `<head>` a well-formed document
requires, so the answer is five. The fix is the one
`RoboLayoutTestSupport.h` already argued for in its own text — the element count of
a fixture is a fact about the *parser*, so assert the number **and say which
elements are being counted** — and the case now carries that list rather than a
bare 5.

Both are cases where the suite was wrong rather than the code, and both were fixed
by making the fixture refuse to be wrong rather than by relaxing an assertion. The
first is the second time this directory has found a failure mode *in the fixtures* —
10.3's §8.2 was a fixture promising a state the builder cannot produce — and it is
recorded here for the same reason: a suite is code, it has bugs, and the bugs it has
are the ones its author cannot see.

The inline algorithm itself came out right on the first build, for 10.3's reason:
the ten suites around it had already pinned every input the pass reads — the two
context ids, the atomic/transparent derivation, the containing-block convention — so
the cursor arithmetic had nowhere to hide.

---

## 10. What the 10.5 suites found

One fixture mistake and one honest degradation, and the second is the more useful
record because it is a case that was *kept* rather than fixed.

**1. A measurement seam that answered a width for a detached font**
(`layout.text.font_source`). The first version of
`LayoutTextFontMeasure` measured through the font with no check that one was
attached, so a detached measurer did not crash — it produced numbers, from a null
face, by luck of what the struct held. The case that found it is the second row of
TextLayout.md §4's table, and the fix is the row itself: a detached font answers
`RoboNotSupported`, because "I cannot measure" and "this is zero wide" are
different statements and 10.5 is the phase that has to keep them apart.

**2. `empty_context` stopped having an example, and the case was not weakened.**
`layout.inline.empty_context` used a `<p>` holding only text as its archetype, and
that container is no longer empty once a text run is a participant. The case was
kept with the same subject — an inline context with nothing to place produces no
line — and its *fixture* moved to a container with genuinely no content. This is the
honest version of a claim changing: the fact being defended is unchanged, the
evidence for it had to be replaced, and the replacement is written into the case
rather than left as a diff.

Three suites outside the phase's own had to move with it, and none was weakened: the
version pins in `layout.module` and `layout.conformance.statement`, the capability
table in both (a field was added), and the object-kind count in `statement`, which is
now three. That is the same set of four assertions 10.3 and 10.4 had to move, which
is the point of writing them down as assertions in the first place.

---

## 11. What the 10.6 suites found

One Release-only defect, this time **in the new suite**, and it is the third
instance of a shape §6 already named.

**1. An `allocator_balance` case asserted a fact that only exists in Debug**
(`layout.line.allocator_balance`). The case was first written to assert that
destroying the tree lowered the Debug allocator's live count — true, meaningful, and
impossible to state in a Release build, where `layout_live_blocks` reads 0 because
the counter behind it is a `RoboMemDebug` instrument. So it passed in Debug and
failed in Release at `layout_live_blocks() < before` with `0 != 1`, saying nothing
about line boxes in either configuration.

The fix is the one every other `allocator_balance` case in this directory already
uses: assert **equality**, which is the claim that is true in both — and in Debug it
is still the exact claim that matters. The case now also brackets the *whole fixture*
rather than only the passes, taking its baseline before the tree exists, so the
assertion covers the arenas' lifetime rather than only their reuse. Its second half —
four passes over a settled tree must not move the live count — is what makes it
worth having at all: it is the only case in the directory whose subject is an arena
that *could* accumulate per pass.

This is `Tests.md` §6's fourth finding recurring two phases later and one level down:
the same Debug-only instrument, the same unconditional assertion, and the same
Release run as the only thing that finds it. 10.6 is the first phase to make the
mistake in its *own* new case rather than inheriting it.

**2. Nothing else.** The line algorithm, the member chain and the block
extent-from-lines change were all right on the first build — for the reason 10.3
and 10.4 give, and with one more suite in the pile this time: twelve suites had
pinned the inputs (the flow record, the union arithmetic, the placement counts) so
the finalization arithmetic had nowhere to hide.

---

## 12. What the 10.7 suite found

Three findings, and the first one is a **defect in the implementation that the
suite caught** — which is the first time in this directory that a new case found a
wrong number rather than a wrong assumption:

**1. The half-leading rule clamped its two terms in the wrong order**
(`RoboLayoutBaseline.c`). `from_run` recovers a font's line gap as
`height - ascent - descent` and splits it across the terms. Written in the obvious
order — gap first, then clamp the terms — a run carrying negative terminology widens
the gap by exactly the amount about to be discarded, so a zero-height record with
`ascent = descent = -4` reported an extent of 8: a floor spanning something that
spans nothing. Clamping first makes it fall back to the no-gap reading, which is
what "the extent is the larger of the height and `asc + desc`" asks for. The case is
`metrics_from_run`'s negative-term row, and it is the one row of the four in that
case that no amount of reading the other three would imply.

**2. A rule array grew an entry while its call site kept the old count literal**, so
the last rules were silently dropped — and the symptom was a *layout* symptom: the
box whose rule went missing had the initial display, or an atomic had no declared
size, which reads as a placement bug rather than as a truncated fixture. This cost
the most time of anything in the phase. The array size is now derived from the array
(`LAYOUT_BASELINE_RULE_COUNT`), and `layout_baseline_style_add` reads each rule's
display back out of the style it just built, so a rule the container refused fails at
the rule rather than in the geometry three assertions later.

**3. Two fixtures encoded assumptions about the coordinate space and the document
language** that were true enough to pass and wrong enough to mislead. A member inside
a transparent inline box is stored relative to *that box* (10.4's rebase), so the
suite now reads members through a helper that sums the chain to the context owner
rather than comparing a parent-relative number against a line; and a fixture built on
an invented tag name (`<tall>`) turned out to be a claim about the parser, not about
layout, so the case uses real elements (`<i>`, `<u>`) and says why.

**And two 10.4 cases changed their numbers on purpose.** `layout.inline`'s
`non_atomic_union` and `wrapping` asserted that two atomics of different heights both
start at the line's top edge. They no longer do — that is the phase — so the y of the
shorter one moved (to 25 and to 20 respectively) while every width, count and
membership stayed exactly as 10.4 wrote them. The 10.4 suite's own prose was wrong in
one word after the change (`its position is where the first of them landed`, which was
the union's top and is now the topmost child's), and the note beside the assertion now
records the rule instead.

**Nothing else needed re-pinning beyond the expected four.** `layout.module.version`,
`layout.module.capabilities`, `layout.conformance.capabilities` and
`layout.conformance.statement` moved for the version bump and the new `baselines`
field — the same set of four 10.3, 10.4, 10.5 and 10.6 each moved, which is what
those assertions exist for.

---

## 13. What the 10.8 suites found

One finding, and it is a **fixture** finding rather than an implementation one — the
second phase in a row whose new suite cost more to get right than the code it tests,
and the first whose mistake produced a *layout* symptom that read as a missing
feature.

**1. The suite measured the wrong box, because it counted the parse instead of
naming the element** (`SuiteRoboLayoutSizing.c`). Five cases indexed the arena
directly — `layout_sizing_box_at(&fixture, 2u)` for "the `<div>`" — on the
assumption that a document `<html><body><div>…</div></body></html>` produces boxes in
that order. It does not: RoboHTML's tree builder inserts an implicit `<head>`, so
index 2 is `<body>`, and the assertion that "a declared width reaches geometry"
failed at `800 != 300` — a declared width being **ignored**, which is exactly what
the phase exists to fix and exactly what a measurement of the wrong box looks like.
The object-count assertion in the same fixture said `5 != 4` at the same time, which
was the evidence that should have been read first.

The fix is the one the block suite already uses: resolve the element **by tag**
(`layout_sizing_box_for`) and let the builder find it. A fixture's *indices* are a
fact about the parser, so a test that counts them is asserting the parser's element
list — and it passes right up until the parser gains one more synthesized element,
which is a change no layout phase should be able to break.

**2. Nothing in the implementation.** The phase's one structural change — resolving
the width *first* and laying the children out into it — was right on the first build,
and the reason is worth recording because it is not luck. 10.3 had written down that
a declared width was ignored, 10.6 and 10.7 had written down which width a line and a
baseline are derived at, and the three cases that pinned the old answer did so with
comments saying the phase which resolved a width must update them. The inputs were
pinned from three directions before the decision was made.

**3. And three cases changed on purpose, exactly as they said they would.**
`layout.block.pipeline` (card 800 → **200**), `layout.conformance.pipeline` (card
1280 → **200**) and the same case's `.title` (1280 → **200**), which is the phase seen
one level down: a build that resolved a box's own width and then handed its children
the containing block's would pass the first two and put the title at 1280. All three
carried a note predicting the change and naming the phase that would make it — which
is what those assertions are *for*, and the first time in this directory that a
predicted failure arrived.

**Four assertions moved for the version and the capability**, the same four every
phase moves: `layout.module.version`, `layout.module.capabilities`,
`layout.conformance.capabilities` and `layout.conformance.statement` — plus
`layout.module.diagnostic_domains`, which is the fifth field a phase can add and is
asserted by count, by name and by "the newest domain is the last enumerator".

---

## 14. What the 10.9 suites found

Three findings, and the first is the kind this file exists to write down: a **design
vocabulary** the new suite proved was incomplete, discovered by a *counter* rather
than by a geometry assertion.

**1. A kind was missing from the margin vocabulary, and the *accounting* is what
found it** (`RoboLayoutMargin.h`). The first draft had three kinds — a resolved
length (including a declared zero), `auto`, and `unresolved` — with a cleared side
reading as a resolved zero. That is CSS's initial value and it produces correct
geometry, which is exactly why it survives a suite that only reads coordinates. It is
the **counters** that cannot be built on it: "how many of this document's margins did
an author actually write?" has no answer, because a cleared record and `margin: 0`
are one value and two different facts. The fix is the `ABSENT` kind 10.8 had already
reached from the other direction for dimensions — a box's `AUTO` width and its
undeclared width are likewise one geometry and two facts — so the vocabulary was
looked up and reused rather than reinvented, and `ABSENT` is deliberately **0** for
the reason RoboCSS gives for its own first-slot defaults. The lesson is the one the
`clamp` case taught in the other direction: a counter is often the first reader to
notice a vocabulary is one kind short, because it asks a question geometry cannot.

**2. The suite's fixtures never reached their subject, twice, and both times the
symptom was a pass that looked right** (`SuiteRoboLayoutMargin.c`). Two cases drove a
real stylesheet and asserted zero geometry — and passed, because the document they
built had no `display: block` chain and no viewport, so no pass ran at all. A third
had a `settled` variable holding a *second* read of the same box, so its "determinism"
claim compared a value with itself. All three are the 10.8 fixture finding's family: a
case whose fixture never reaches its subject reports the absence of the subject as
the feature being correct. Every margin case that reads a coordinate now resolves the
element **by tag** and asserts that a pass actually placed something before it reads
the number.

**3. Three earlier assertions changed, and each said so before it did.**
`layout.block.pipeline` (card origin 0/0 → **20/10**, the `margin: 10px 20px` it had
recorded as ignored), `layout.conformance.pipeline` (the same card's origin, and
`<body>`'s and `<html>`'s extent 100 → **120** — the margin *box* the cursor reaches),
and `layout.block.pipeline`'s doc comment, which named `margin` among the ignored
declarations and now names the phase that stopped ignoring it. The card's own size is
unchanged at 200×100 in both cases, which is what makes the pair of assertions — a
moved origin and an unmoved size — the phase's claim rather than two numbers.

**Five assertions moved for the version and the capabilities**, the same set every
phase moves and one more than the previous one: `layout.module.version`,
`layout.module.capabilities`, `layout.conformance.capabilities`,
`layout.conformance.statement` and `layout.module.diagnostic_domains` — the last of
which is asserted by count, by name, by "the newest domain is the last enumerator"
*and* by "the one before it is the previous newest", a pair 10.9 added because a new
domain should not be able to leave its predecessor's ordering unstated.

---

## 15. What the 10.10 suites found

Three findings, and the first is an **arithmetic** one rather than a design one: a
magnitude the module's own seam permits and a size cannot hold.

**1. Two bounded numbers can sum out of range, and the sum is the size**
(`RoboLayoutPadding.c`). A padding is added to a content box, and each of the two is
bounded by the seam's maximum — `LAYOUT_STYLE_LENGTH_MAX`, which the classification
applies on the way in. Their **sum** is not bounded by it, so a
`width: 2147483647px` box with `padding: 10px` is a document an author can write and
a `GfxPixelX` cannot express. The first shape of the door added four
`GfxPixelX` values, which wraps into a **negative** rectangle — a box whose
padding made it smaller, and a number every consumer below would believe. The fix is
two lines: sum in `int64_t`, then clamp at the maximum. It is clamped rather than
refused because there is no document some number cannot describe, and a size that
saturates is still a drawable rectangle — which is a different decision from 10.9's
`available_content_width`, where the correct answer at the lower edge really is zero.
That the two derived numbers have **opposite** edge policies, one floor and one
ceiling, is the finding: they are the first pair in the module where "clamp it" and
"refuse it" are both wrong for one of them.

**2. A size assertion alone passes for a build that grows a box the wrong way**
(`SuiteRoboLayoutPadding.c`). The phase's claim is a pair — the box's `size` grows
**and** its `position` does not move — and half of it is invisible in the number a
suite reaches for first. A build that applied the padding *outward* from the content
origin would report exactly the same `size` and put the box in the wrong place, so
`layout.paddings.relayout` re-runs the pass after a padding change and asserts that
the origin is the one the **margins** asked for while the size moved by the padding.
The mirror case on the other side is `layout.paddings.block_auto_width`: a box with
`padding: 10px` and no declared width is exactly as wide as its containing block, and
a build that applied the padding *after* the width would report the containing block
plus 20 — the same size class of error, one level out.

**3. The four earlier assertions changed, and each said so before it did.**
`layout.block.pipeline` (card 200 × 100 → **210 × 110**), `layout.conformance.pipeline`
(the same card, and `<body>`'s and `<html>`'s extent 120 → **130**), plus the same
case's `paddings_applied`/`paddings_unresolved`, which went from *absent* to **4 and
0**. The one assertion in that group that deliberately **did not** move is the
paragraph inside the card, still 200 wide: the card's border box is 210 but its
content box is the 200 its author declared, so a build that read `width: 200px` as a
border box would fail there and nowhere else — which is the case paying for the
`content-box` model rather than merely documenting it. The card's own size in both
cases is worth its note for the same reason 10.9's was: 10.3 recorded the declared
width as *ignored*, 10.8 recorded it honoured, and 10.10 is the third phase to move
the number — not by the declaration again but by the padding.

**Six assertions moved for the version, the capabilities and the domains** — one more
than 10.9's five, because `layout.module.diagnostic_domains` now asserts *three*
positions rather than two: the newest domain is `padding`, `margins` is one behind it,
and `sizing` one behind that. 

**The flake, one phase on.** 10.10's `robo test-runtime` run was **2254/2255** with
`window.host.keyboard` the only red case, and the same binary was 2254/2255 directly
afterwards with the same case red — the documented host-desktop flake, unchanged by a
phase whose diff contains no file matching `window`. The Debug and Release `-Verify
-Test` runs were both green at **2255/2255**, and the case passes in isolation, which
is the pattern §5 records rather than a new finding.

---

## 16. What the 10.11 suites found

Four findings, and the first is a **vocabulary** one — the mirror of 10.10's
arithmetic finding, and the kind only the accounting can see.

**1. A record was one kind short of its own subject.** A padding's used value is its
declared value, so "this side drew a border" and "this side was declared at five" were
the same question — and a border makes them two. What exposed it was
`layout.borders.none_style` wanting to assert both facts at once: twelve declarations
arrived, and **none of them took room**, because `border-style: none` makes the used
width zero whatever `border-width` says. One counter cannot report a pair, so the
record carries three: `widths_resolved`, `widths_unresolved` and `borders_drawn`. The
third is the finding rather than the pair: it is the only number in this module that
answers a question about *geometry* rather than about a *declaration*, and it is what
makes "a document can declare four borders and draw none" a readable fact instead of
an inference from four zeros.

**2. A door's name outlived its meaning, and only a second layer could see it.**
`robo_layout_padding_edges_border_box` returned `content + padding` — the **padding**
box — and it was named for the box the module *hoped* to complete rather than the box
it computed. That was unobservable while a padding was the only inset, because the two
boxes were one number, and it became observable the moment a border existed: paint
draws the border box's edge and would have drawn it `border-width` too far in. The fix
is a rename (`..._padding_box`) plus `LayoutBorderEdgesBorderBox` composed
above it, and **no arithmetic changed** — every 10.10 case about those numbers passes
with the new spelling, which is how the phase could tell the name was the defect rather
than the code. The prediction 10.10 wrote down ("the doors below are named for what
they add so that 10.11 can extend them rather than rename them") was half right, and
this is the half that was wrong.

**3. A negative side is a caller bug, and the moment to see it is before a size is
derived from it.** 10.10 floored a negative available width at zero, because a
containing block narrower than a box's padding is a **document**. A negative *border
record* is not: this module's own resolver cannot produce one, so one arriving at a
derived size is a caller holding a record it built by hand. So the two derived doors
**refuse** with a diagnostic rather than flooring, through one shared
`layout_border_edges_require`, and `layout.borders.derived_boxes` asserts both the
refusal and the untouched output. It is the first place in the module where "this
record is impossible" and "this document is extreme" needed different answers.

**4. The same two numbers moved for the fourth time, and the phase that moved them is
recorded in each.** `layout.block.pipeline` (card 210 × 110 → **218 × 118**) and
`layout.conformance.pipeline` (212 × 112 and heights **132**) — the card declares a
border in both, and each assertion's comment now says which phase moved it and why.
The assertion that deliberately **did not** move is still the paragraph inside the
card: **200**, not 212 and not 188. The card's border box grew and its content box did
not, so a build that read `width: 200px` as a border box would fail there and nowhere
else, which is the case paying for the `content-box` model rather than documenting it.

**One finding about the flake, and it is the same one.** 10.11's `robo test-runtime`
run was **2276/2277** with `window.host.keyboard` the only red case, and the first
Release run was 2276/2277 the same way while the very next run of that binary was
**2277/2277** — the documented host-desktop flake, in the third consecutive phase, on
a diff that contains no file matching `window`. It is worth noting only because it now
blocks two otherwise-green pipeline commands (`test-runtime`, `run-runtime`) in every
unsanitized whole-suite run, which is a cost the module is paying rather than a
property of this phase.

---

## 17. What the 10.12 suites found

Four findings, and the first two are **ordering** ones — the phase's displacement
arithmetic was correct the whole time, and what it sat next to was wrong.

**1. A counter read a variable that was not written yet.** The block pass accumulated
the four position counters *before* the call that resolved the displacement, so on the
first box of every pass `offsets_applied` summed whatever the previous frame had left
on the stack — visible as pointer-sized garbage in a determinism case that compared two
passes' records. Every earlier counter was preceded by its own resolve, so this is the
first time in the module that an accumulator outlived its producer; the fix is the
ordering (count *after* the resolve, behind the two early returns) and the record
always describing this box rather than the last one. The kind of bug only a
*comparison* can see: both passes were wrong in the same way, so both records were
self-consistent and no single run caught it.

**2. A test freed a pointer the fixture had already freed.** The seam case held the
style pointer the first build handed back, then replaced that style three times — each
replace destroying the old row's style and building a new one — and ended by
destroying the *stale* first pointer: a double free that ended the whole suite in a
segfault under the Debug heap, mid-case and after every assertion in it had passed.
The fix is teardown over what the source *currently* holds, and the lesson is the
fixture's own rule restated: a handle handed back by a builder is a view of one
moment, and a replace invalidates it.

**3. A negative inset is honoured here and nowhere else, and the seam test is the
case that says so.** `layout.positioning.seam` reads `right: -4px` back as **-4** —
the one seam of the three inset readers where the sign survives, because CSS's
inset grammar (like margin's) makes the negative half of the value space and a
padding's or a border's does not. The first draft of the fixture stored the magnitude
and expected the layer to negate it; the run said otherwise, and the helper now
carries the sign it was handed.

**4. A displacement composes with a *flow* stack, not with the boxes after it.** The
box-model case's first draft expected a shifted sibling's `y` to restart from the
displaced box's final position; the engine put it where the *flow* position says it
belongs, and the case now asserts the pair — the box is drawn at margin origin plus
offset, the next sibling's flow `y` accumulates from the margin box as if the offset
did not exist, and the two numbers differ by exactly the offset. That is the phase in
one case: two coordinate spaces, one box, no third number.

**The flake, one phase on.** 10.12's `robo test-runtime` run was **2254/2255** with
`window.host.keyboard` the only red case — the documented host-desktop flake, in the
fourth consecutive phase — while the direct run of the same Debug binary was
**2295/2295** and the sanitized run **2296/2296**, both with no red case at all. The
diff touches no file matching `window`; the flake remains a property of the console
holding the foreground, not of the module.

**The flake, two phases on.** 10.13's `robo test-runtime` run was **2329/2330** with
`window.host.keyboard` the only red case, in the fifth consecutive phase — the run
itself taking seven minutes because the pipeline captures a 244 000-line DEBUG log
through a child PowerShell. The direct run of the same Debug binary was **2330/2330**,
Release was **2330/2330**, the sanitized run **2330/2330**, and `--filter=window.host` in
isolation **19/19**. The diff touches no file matching `window` (`git status` reports
`RoboCSS*`, `RoboLayout*` and the two new suite files), so there is still no causal
path, and the suite that owns the case was not edited.

---

## 18. What the 10.13 suites found

Four findings, and the first is a **shape** one: this phase's arithmetic was never the
hard part, and what had to change was three long-standing *assertions* that had been
standing in for facts they could not see.

**1. An assertion of silence had to become an assertion of agreement — in sixteen
cases at once.** Since 10.1 every block case has gone through one shared
`layout_block_expect`, whose last line read

```c
RoboCheckEqInt(result.flags & RoboLayoutResultFlagAll, 0);
```

and that was *right* while nothing set a flag: it was 10.3's way of saying "that a box
overflows is a fact a later phase owns". This phase is that phase, so the line could
no longer hold — but the honest replacement is not to delete it and not to hard-code a
value, because a hard-coded expectation in a shared helper is sixteen cases that have
to be re-derived by hand. It now asks the **record** and asserts the **agreement**:
`LayoutObjectOverflow` is called for the box, its two axes are folded into a
flag word, and the result's flags must equal it. One line, and it is a stronger
assertion than either alternative — a pass that set a bit the record does not justify,
or measured an extent and forgot the flag, now fails *in every block case* rather than
in the one case written to check flags. It also had to learn the door's own contract:
the door declines a text run and a transparent inline box, and a declined box
contributes **no** flags, so the helper treats `RoboNotFound` as a cleared record
rather than as an error. That the same helper serves boxes that are measured and boxes
that are not, without a second code path, is the recursion of §7 seen from the test
side.

**2. The shorthand could not ride the `PRESERVED` convention, and that is a value-model
finding rather than a registry one.** Four families registered before this phase —
`margin`, `padding`, `border`, `background` — are shorthands that stay
`PRESERVED`, carrying their token list while only their longhands are typed, because no
phase here expands a shorthand. `overflow` could have taken that road and the conformance
table would have stayed tidy. It deliberately does not: `overflow: hidden` is written on
nearly every box an author wants clipped, and a `PRESERVED` row would make it a
declaration nothing reads — a document that says "clip this" laying out as though nobody
had written anything. So the shorthand's *own name* is registered **on the overflow
vocabulary**, which is the first row in the table whose shorthand carries a typed value
rather than tokens, and the cost is written down where it is paid:
`overflow: hidden visible` is legal CSS this build cannot read, because reading it needs
the expansion this registration exists to work around. The conformance table asserts the
whole family — both longhands, both values, `banana` refused as a keyword and `20px`
refused as a non-identifier — which is how the phase could show that its one shortcut
has exactly one edge.

**3. "The newest domain is the last one" was an invariant with three pins, and two of
them were in other phases' suites.** `layout.module.diagnostic_domains` walks the
vocabulary and asserts the count; `layout.positioning.kind_vocabulary` and the overflow
suite's own vocabulary case each asserted that *their* domain was the newest. Adding
`RoboLayoutDomainOverflow` therefore moved three suites, not one — 20 → 21 in all
three, plus a `POSITION + 1 == OVERFLOW` pin in two of them so the two newest cannot be
reordered by accident. The pattern is worth naming because it is the module's fifth
re-pinning of the same kind: a *closed vocabulary* that a phase appends to is an
assertion in every suite that ever pinned its length, and the phase that appends pays
for all of them at once. The alternative — asserting `>= 20` — would have cost nothing
and proved nothing, which is why the module keeps the exact number.

**4. A child contributes its *region*, and only a case with a clipping child can tell.**
The recursion is one line (`LayoutOverflowEdgesInclude` on the child's region
rather than on its contents), and it is indistinguishable from the wrong version in
every document whose boxes fit. `layout.overflow.pass_recursion_clipped` is the case
that separates them: a clipping child whose own contents reach 100px past it must
contribute **its padding box** to its grandparent while its own record still says 100.
Fold the contents in instead and the grandparent's extent comes out 100 too — the leak
that makes a clipped subtree appear in an ancestor's scrollable area. It is the one case
in the suite that would fail for a build whose `region` and `extents` doors were each
individually correct.

**And one arithmetic decision the suite pins rather than finds.** Extents are clamped at
the boundary: a box whose contents reached *inside* it has an overflow of **zero**, not a
negative extent, and `include` is a per-side **maximum** computed in 64 bits and
saturated (`layout.overflow.include_saturates`). A signed extent would have made "reached
5px past the left edge" and "did not reach the left edge by 5px" the same number, which
is the mistake the non-negative rule exists to make impossible — and it is the reason a
box that overflows on one side and not the other has a record that says exactly that.

**The flake, fifth phase.** 10.13's `robo test-runtime` run was **2329/2330** with
`window.host.keyboard` the only red case, while the direct Debug run was **2330/2330**,
Release **2330/2330**, the sanitized run **2330/2330**, and `--filter=window.host`
**19/19**. The diff contains no file matching `window` and does not edit the suite that
owns the case.

---

## 19. What the 10.14 suite found

**1. "Shallowest stale box" is not the same question as "where does relayout start", and
the first implementation was wrong.** The dirty root was written as the shallowest box
carrying an own-geometry bit, which is cheaper and reads well —
`layout.invalidation.layout_dirty_propagation` failed it with a real document: marking
`div` also marks `footer`, `footer` is `main`'s sibling and therefore sits *higher* in the
tree than `div`, and the "shallowest" answer named a box whose subtree does not contain
the change at all. The fix is not a tie-break but a different question: the **lowest
common ancestor** of the boxes whose own geometry is stale, which is the smallest subtree
that contains every box needing placement. Three cases moved with it
(`layout_dirty_propagation`, `root_is_the_common_ancestor`,
`incremental_runs_after_a_mark`), and the reason the failure was worth keeping is that
every *weaker* assertion — "some box is dirty", "the count is right" — passes for either
rule.

**2. `SUBTREE` on an ancestor is a relayout bit that must not count as one.** The second
half of the same finding: the marking policy deliberately never gives an ancestor
`LAYOUT` (an ancestor's origin does not move when a descendant grows) *and* deliberately
gives it `SUBTREE` (its extent may). A root defined over `LAYOUT | SUBTREE` therefore
always answered the root box, which is the number that says nothing. This is why the
module has three masks rather than one: `LAYOUT_BITS` for "must a pass run",
`OWN_BITS` — `LAYOUT` alone — for "where would it start", and `ALL` for "is anything
owed". `layout.invalidation.vocabulary` pins the relation between them, and the
overflow-only case pins the third answer (`root == NONE`, `needs_layout == 1`).

**3. The propagation rule needed a *transitive* following-flow walk, and only a
three-level document can show it.** `section` is `div`'s following sibling and `footer`
is `main`'s; a one-deep rule would mark the first and miss the second, and a document
with two boxes could not tell the two rules apart. The walk therefore goes up from the
marked box and marks the following siblings at each level, and the case asserts both
levels plus the **transitively** preceding box staying clean.

**4. The incremental door cannot be tested through a rebuilt tree, so the fixture had to
rewrite a style the tree already borrows.** Replacing a style and rebuilding the tree is
the honest way to change what a pass reads — and it invalidates everything by
construction, which means the four-term decision is never reached. The suite therefore
gained `layout_inv_style_set_px`, which rewrites one declaration **on the pointer the
built tree already holds**, so the same tree can be laid out twice with different input.
That is what makes `layout.invalidation.relayout_moves_the_following_flow` the strongest
case in this suite: it is the only one where the pass actually has to run for the claim
to mean anything.

**5. A pass that computed no coordinate must clear nothing, and the first version had to
be talked out of a tidier rule.** "A completed pass clears the bits it satisfied" is
true only when the pass *satisfied* them: with no viewport the walk classifies and
computes nothing, so clearing there would leave a tree that claims to be current and has
no geometry at all — requirement 11's stale-clean state, and one that would be invisible
until a later pass skipped work that had never been done. The clearing is therefore
inside the condition that laid geometry out rather than beside the pass counter, and
`layout.invalidation.pass_without_viewport_clears_nothing` is the case that holds it
there.

**And one design point the suite pins rather than finds.** The `STYLE` bit is *not*
cleared by a recomputation, so a box can be **not clean** and **not need layout** at the
same time. That is not an oversight but the phase's smallest claim: a colour change must
not pull geometry, and it must not be silently dropped by the pass that declines to run
either. `layout.invalidation.incremental_skips_a_paint_only_change` asserts all three
halves — the door skips, the outcome is `CLEAN`, and the `STYLE` bit is still there
afterwards — because a build that cleared it would look correct until the repaint never
happened.

**The flake, sixth phase.** 10.14's `robo test-runtime` run was **2354/2355** with
`window.host.keyboard` the only red case, while the direct Debug run was **2355/2355**,
the Release suite **2355/2355** on its first retry, the sanitized run **2355/2355**, and
`--filter=window.host` **19/19**. The diff contains no file matching `window` and does not
edit the suite that owns the case — the sixth consecutive phase, which is why §14's
proposal to either fix the focus race or quarantine the case is now overdue rather than
optional.

---

## 20. What the 10.15 suite found

**1. The pass's own arithmetic was unbounded, and only an integration case could see
it.** Ten phases had stated the rule for their *own* sums — one clamp on the sum,
saturated rather than refused, "because there is no document an author can write that
some number cannot describe" — and the pass that calls them had not applied it to
itself. Three sites accumulated bounded numbers into an `int32`: the block cursor
(`cursor += child_height`), a margin origin (`origin + edges.top`), and the margin box
(`edges.top + size.height + edges.bottom`). Two of them are visible in the field and the
third is the one that bites: a `2000000000px` block above a `2147483647px` one sums past
`INT32_MAX`, the wrapped cursor is *negative*, and `LayoutSizingSetContent`
refuses a negative extent — correctly, because a size is never negative — so a document
that was merely tall made the whole pass **fail** rather than saturate.

What makes this a 10.15 finding rather than a 10.3 one is that no layer suite *could*
have caught it. `SuiteRoboLayoutPadding.c`, `SuiteRoboLayoutBorder.c`,
`SuiteRoboLayoutPosition.c` and `SuiteRoboLayoutOverflow.c` all test the geometry's
maximum already — one layer at a time, each against its own door. The overflow is a
property of an *accumulation across boxes*, so it needs a pass, a parent and two
children, which is the shape only an integration suite builds. The fix is one helper
(`layout_extent_clamp` in `RoboLayoutInternal.h`) applied at the three sites, so that a
layer and the pass cannot disagree about the bound.

**2. A passed case can be the wrong case, and the border regression proved it.**
`layout.integration.regression_border_style` was written to pin 10.11's finding — a
border width without a style draws nothing — and the first version of it laid out a
`div` with `border-width` set and asserted a *nonzero* height. It failed, and the
failure was the suite's, not the engine's: the document used a generic keyword value
where the reader needs the four `border-*-style` longhands, and a width with no style
resolves to used width zero. The distinction the case now pins is the one the phase's
brief calls "geometry-affecting versus paint-only": a `border-style` change moves
geometry, and the pair `width` + `style` is what makes it nonzero.

**3. Two coordinate spaces in one result, and the first assertions mixed them.**
10.12 gave a result a second rectangle — `position` is where a box is *drawn*, and the
parent still stacks by the flow geometry — and that makes an integration assertion easy
to write backwards: a child's `position.y` is in the *parent's* coordinate space, not
the page's, and a relatively positioned box that is displaced reports a `position`
unlike its flow origin while its contribution to its parent's extent is the flow one.
The suite's first pass compared page-space numbers against parent-space doors in four
cases, which is not a bug in the module but is exactly the mistake a caller of these
doors will make — so the fixture now carries a `page-origin` helper that composes the
chain explicitly, and the composition is asserted rather than assumed.

**4. The three regression cases found their reproductions in the *documents* rather
than the suites.** 10.11's border-style finding, 10.13's clipped-child finding (a
clipping child contributes its *region*, not its contents, which is the error that
compiles and leaks a clipped subtree into an ancestor's scrollable area) and 10.14's
dirty-root finding (the lowest common ancestor, not the shallowest dirty box) each
existed only as prose in `layout/docs/Borders.md`, `Overflow.md` and `Invalidation.md`.
A finding recorded in a document is a finding nobody runs, so each is now the minimal
reproduction that failed before its fix — three documents' worth of history that a
later phase fails *here* rather than in a paragraph.

**And one design point the suite pins rather than finds.** Determinism is asserted by
*re-building* the document into a second tree and comparing every result, inset, offset,
overflow record and line field by field — not by laying the same tree out twice. A
single tree laid out twice agrees with itself even when the pass reads uninitialized
memory, because the second pass overwrites the first; two independently built trees
agreeing field by field is the assertion that a build has no hidden state. That is why
`layout.integration.determinism` is the longest case in the suite and the only one that
touches the whole surface.

**5. The version pins are load-bearing, and the phase found that out by bumping the
version and leaving three of them stale.** `layout.module.version`,
`layout.module.allocator_balance` and `layout.conformance.statement` each assert
`"0.14.0"` by hand, so raising the module to 0.15.0 reddened all three — 3 failed cases,
with `allocator_balance` reporting *64* failed checks because its loop re-asserts the
version on every one of its sixty-four iterations. That is the pins working exactly as
intended (a later goal that flips a sentence must change the test that states it), but it
is also the phase's one self-inflicted red, and the shape is worth naming because every
remaining goal in Phase 10 will meet it: the *count* of a version-pinned case is not the
number of assertions a reader would write, it is the number of times the suite asks the
question.

**The flake, seventh phase.** 10.15's `robo test-runtime` run was **2365/2367** with
`window.host.keyboard` **and** `window.host.mouse` the only red cases, while the Debug
`-Verify -Test` build, the Release `-Verify -Test` build and the sanitized build each
reported `ALL TESTS PASS` at **2367/2367** in the same session. The diff touches
`robo_layout_*`, `Tests/Main.c` and `Tools/Build.ps1` and no file matching `window`. It
is the seventh consecutive phase — and this time the pair, not just the keyboard case
— so §14's proposal to either fix the focus race or quarantine the cases is now overdue
by five phases rather than one.

## 21. What the 12.17 suites found

Two suites, fifteen cases, and neither of them needed a font file: `layout.fontrequest.*` drives
`CssComputedStyle` through 9.8's makers, and `layout.fontselect.*` drives a **scripted provider** that
records how many times it was asked and in what order. That is the phase's testing decision — the seam is a
callback, so the honest way to test it is to be the callback.

**1. The seat of the claim is a *value*, and that is what the suites are shaped around.**
`LayoutTextStyle` is copied by value into a `PaintTextRun` (12.16), so the family list could not live on it.
`layout.fontrequest.resolve` asserts the resolved record and the measurer's `LayoutTextStyle` agree on the
four scalars *by asking the measurer's own resolver*, which is what makes "the request and the measurer
cannot disagree" a test rather than an intention.

**2. Two answers that look alike are kept apart, twice.** `familyUnresolved` separates "the declaration was
unusable" from "the declaration was absent", and both fall to the same initial list —
`layout.fontrequest.resolve` pins both sides. `layout.fontselect.none` does the same for a provider: a miss is
`RoboOk` with a NULL face and the walk continues, and a provider *failure* propagates, because reading a
broken database as "that family does not exist" would silently degrade a whole document.

**3. Order is the fallback policy, so it is asserted as a count.** `layout.fontselect.fallback` offers two
families to a provider that answers only the second and asserts the answer, `familyIndex == 1`, `fallback`,
*and* `stub.calls == 2` — the number of questions is the claim. `layout.fontselect.first_family` asserts
`calls == 1` for the other half: the walk stops at the first answer rather than resolving the tail it no
longer needs. A generic is asserted to arrive **as a generic** (`layout.fontselect.generic`), which is what
keeps `sans-serif → Inter` out of this module.

**4. The last version-pinned red, and it was three files again.** Raising the module to `0.16.0` for the
capability reddened `layout.module.version`, `layout.module.allocator_balance` (64 checks, because its loop
re-asserts the version on every iteration) and `layout.conformance.statement`, exactly as §20 predicted for
every remaining goal. `layoutCapabilityFlags[]` and `layoutConformanceCapabilities[]` also grew a row, and
the **positional** read (`sizeof(LayoutCapabilities) == count * sizeof(int)`) is what caught the new field
before any case did.

**5. The document-level case found a testing mistake rather than a defect.** `desktop.document.font_families`
resolves two paragraphs over the *real* registry — one naming the shipped face, one naming an unknown family
followed by a generic — and asserts the exact counts (2 resolved, 1 missed). Its first draft declared
`display:block` nowhere, and the document laid out **no text at all**: this build has no user-agent
stylesheet, so `<p>` is `display: inline` (CSS's own initial, which the registry spells), no inline formatting
context is established, and the measurer is never asked. The case passed its `texts >= 1` neighbour either
way, because a *text object* is built regardless. The lesson is the one §5 of `FontRequest.md` states for a
different reason: a document that measures nothing is not a document that fails to measure.

## 22. What the 19.1 suite found

Sixteen cases, four groups — the vocabulary, the record, the measurement, the sizing
plumbing — and the first suite in this directory whose subject is a *measurement of
the contents* rather than a placement.

**1. The seam must be reached with the right spans, and a call count is how that is
asserted.** `layout.intrinsic.text_min_max` measures `hello world` and asserts 110
max-content, 50 min-content, and `script.calls == 3`: the whole node, then each of
two words. A build that measured the whole node and called its width the widest word
would answer the same numbers for a one-word node and the wrong ones here, so the
count is what distinguishes "the words were found" from "a number was produced".
`layout.intrinsic.text_long_word` is the counterpart (one word: min equals max, two
calls — the whole node and its single word, because the whole node is always
measured for max-content).

**2. The unit is a code point, and the case proves the decode rather than agreeing
with it.** `layout.intrinsic.text_multilingual` reads the node's code-point count
back through `LayoutTextNodeCodePoints` (`héllo wörld` → 11) *and* measures it, so a
build that counted bytes would fail at 130 where this one answers 110. The
13-byte/11-code-point split is the smallest text that tells the two apart.

**3. The zero has its reason written into the case.**
`layout.intrinsic.text_whitespace` (a whitespace-only node: no word, so min 0 and
max 30) and `layout.intrinsic.unmeasured` (no metrics source, so every run measures
zero) are asserted with the reason in the case, in the same spirit as
`layout.text.pass_unmeasured` — a zero that is the documented degradation rather
than an oversight.

**4. Two `NONE`/miss distinctions, and the implementation was wrong on one of
them.** The first draft of `LayoutIntrinsicMeasureObject` answered `RoboNotFound` for
a `NONE` id *and* for a non-live one; `layout.intrinsic.refusals` asserted the
convention every other door in the module follows — `RoboInvalidArgument` for a
`NONE` id (a caller bug) and `RoboNotFound` for an id that names no live box (a
miss) — and the implementation was corrected to match. The suite is the thing that
found it.

**5. Production code must not call a test macro, and the first draft did.**
`RoboLayoutIntrinsic.c` used `RoboCheckEqInt` in its first version; the rewrite uses
none, which is the module's own rule (assertions live in suites, not in the module)
and would otherwise link the test framework into the runtime.

**6. The version pin is four places, and one of them is a loop.**
§21's story repeated: raising the module to `0.18.0` reddened
`layout.module.version`, `layout.module.allocator_balance` (64 checks, because its
loop re-asserts the version on every iteration), `layout.conformance.statement` and
`layout.conformance.capabilities`'s implemented count — plus the capability table in
three suites, `RoboLayoutDomainCount` in three, and both dimension
kind/source counts. §21 predicted exactly this shape for every remaining goal.

## 23. What the 19.2 suite found

Six new sizing cases (`layout.percentage.*`), one new positioning case
(`layout.positioning.percentage`) and a handful of renamed or re-pointed seam cases,
and the phase added a kind to four vocabularies rather than an engine.

**1. The ordering subtlety is the one finding, and it has no symptom until a nested
percentage is written.** A child's percentage-height base must be the parent's
**declared** (or resolved-percentage) height, computed *before* the children are laid
out — not the height the parent grew to once that child was placed, which is what a
build computing the base at the usual point naturally has. A `height: 50%` child under
an `auto`-height parent exposes it; nothing else does. `layout.percentage.height_nested`
and `layout.percentage.height_child` pin the pair.

**2. The two bases are separate on an inset, and a swapped-bases build looks
plausible.** `left`/`right` take the containing block's width while `top`/`bottom`
take its height, so `layout.positioning.percentage` asserts `left: 10%` of 200 → 20 and
`top: 25%` of 400 → 100 in one case. A build that resolved both axes against the width
would agree with the first and answer 50 for the second, which is why the two numbers
differ by design.

**3. A no-base percentage is a *counted* degradation, not a silent zero.**
`layout.percentage.height_indefinite` and the positioning case's no-base half both
assert `insetsUnresolved`/the unresolved counter rather than only the zero value, so a
build that dropped the declaration and one that resolved it to zero are told apart.

**4. A fixture disagreed with its own comment, and the case reported it as a pass.**
The margin seam's declared percentage was `25.0` while its comment (and its check)
read `5.0`; aligning the declaration fixed a case that had been asserting its own
typo. Recorded because it is the same class of mistake §5 of `IntrinsicSizing.md`
names: a fixture that does not reach the number the case claims.

**5. The version pin, again, and one library detail.** Raising the module to `0.19.0`
reddened the same four places as 19.1 (`layout.module.version`,
`layout.module.allocator_balance`, `layout.conformance.statement`,
`layout.conformance.capabilities`'s implemented count), and the capability table in two
suites grew a row. The one non-obvious edit was in `RoboLayoutAbsolute.c`: its
`LayoutInset` compound literals used positional initialization, and the new
`percentage` field made them `-Wmissing-field-initializers` errors — resolved by using
the `LayoutInsetResolved` maker, which is the door the struct's own comment points at.

## 24. What the 19.6 suite found

Eighteen new cases (`layout.table.*`) over the fifth formatting context, the ten display
values the phase made real, and the third `CssDisplayOuter` kind the eight internal ones
needed.

**1. A nested counters record was left out of the identity, and the record's own equality
found it.** `LayoutBlockStatsEqual` compared the flex and the grid records field by field and
did not mention `table` — so two block records that laid out different tables compared equal.
`layout.table.capability` asserts the pair, which is the only place in the module where the
comparison is a *claim* rather than an implementation detail: the phase that adds a record
must add it to the identity in the same change.

**2. `LayoutFcDisplayCanEstablish` answered 0 for `display: table`.** The predicate lists the
inner values that *are* a context — `flow-root`, `flex`, `grid` — and the table inner was not
added to that list when the branch that answers `TABLE` was. The two doors are asked by
different callers ("could this ever establish?" versus "which kind does it establish?"), so the
formatting-context suite's ten new rows are what caught the disagreement: `table` and
`inline-table` answer `can_establish` 1 *and* `is_a_block_container` 0, which no other pair of
rows in that table does.

**3. A container whose only child is a table-internal box establishes an `INLINE` context.**
An internal table box is neither block- nor inline-level, so a block container holding one and
nothing else has no **block-level** child at all and takes the last rule of the classifier —
inline content. The first draft of `layout.table.internal_outside_table` put a lone
`table-cell` in a block `body` and asserted the block pass's `LAYOUT_BLOCK_TABLE_INTERNAL`
count; it read 0, because 10.4's pass is the one that saw the cell and 10.4 counts it as a
declined block-level box. The case now states both facts: the box beside a block-level sibling
is the one the *block* pass declines, and the lone box is a different document with a different
answer — which is exactly the kind of "no geometry, for a stated reason" distinction the
`unreached` identity exists to keep visible.

**4. The version pin is four places, and this time the counts were the interesting half.** As
§21 and §22 predicted, raising the module to `0.22.0` reddened `layout.module.version`,
`layout.module.allocator_balance`, `layout.conformance.statement` and
`layout.conformance.capabilities`'s implemented count (28 of 29 now), and the capability table
in two suites grew a `table_layout` row. What was new is that a **vocabulary** grew rather than
a capability: `RoboLayoutDomainCount` moved in three suites, `RoboLayoutFcKindCount` in two, and
the CSS display suite's walks, counts and unknown-keyword list all moved — which is the cost of
a phase that adds ten display values, and the reason `SuiteRoboCssDisplay.c` is part of a
layout phase's gate.
