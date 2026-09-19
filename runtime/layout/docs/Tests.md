# RoboLayout Test Organisation (through Phase 10.4)

Ten suites, 123 tests, one framework. Each phase adds no test runner, no reporter
and no assertion vocabulary: everything here goes through
`runtime/Tests/Framework` (`ROBO_CHECK*` and `robo_test_register`), the same as
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
`layout.conformance.statement` and the old `geometry == 0` claims, and 10.4's
re-pinning of the same two cases again — the change is recorded in §8 and §9, under
the phase that made it.

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
| `SuiteRoboLayoutConformance.c` | `layout.conformance.*` | 7 | the whole-product questions no single suite can ask |

### 10.1 — the module surface (`layout.module.*`)

Eleven cases, and their subject is that the module's *self-description* is true:

- `lifecycle` — init twice is `ROBO_ALREADY_EXISTS`, shutdown is idempotent, and a
  shutdown frees nothing that was never the module's.
- `version` — `0.1.0` by string and by number, and the two agree.
- `capabilities` — every field, and the two that are 0 are asserted as 0.
- `geometry_flag` — `ROBO_LAYOUT_GEOMETRY_IMPLEMENTED`,
  `robo_layout_geometry_is_implemented()` and `capabilities.geometry` agree.
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
| `add.second_root` | a second root is `ROBO_ALREADY_EXISTS` |
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
| `clamp_contradiction` | a contradictory pair makes `clamp` answer `ROBO_NOT_FOUND` |
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
| `viewport` | the viewport is recorded as available space, and an unset one answers `ROBO_NOT_FOUND` |
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
| `relations` | the DOM relation between two elements survives the round trip through `robo_css_element_ref_t` |
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
| `tree.add_invalidates` | **adding a box to a classified tree invalidates the classification**, and a context id read across it answers `ROBO_NOT_FOUND` |
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

### 10.7 — conformance (`layout.conformance.*`)

Seven cases, and the shape is 9.16's: ask the questions no single-module suite can.

| Case | Subject |
|---|---|
| `capabilities` | the capability struct walked **positionally** against a hand-written name list, with the struct's size pinned against the list's length — now twelve fields, with `geometry`, `formatting_contexts`, `block_layout`, `inline_layout` and the derived `block_and_inline_layout` at 1 and `painting` at 0 |
| `statement` | the module's central claims, each an assertion: `geometry == 1`, version `0.4.0`, API unfrozen, two object kinds and four context kinds |
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

The second row is the design record's §6.1 argument made concrete. Six of the ten
suites never instantiate a style context at all, and the four that do reach for one
solely in the cases whose subject *is* the joining: `layout.dom.*` and
`layout.conformance.*` throughout, and the single `pipeline` case in
`layout.block.*` and `layout.inline.*`. `layout.fc.*` is one of the six, which is
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

- exact `robo_result_t` values for every refusal, because "it failed" is not a
  contract;
- that a record's *unset* state is distinguishable from its zero value;
- that a borrowed pointer is the pointer that was handed in, not an equal copy;
- that ids are dense, tree-scoped, and restarted by a clear;
- that a second pass re-unresolves what the first left;
- that the module leaks nothing, against the Debug allocator tracker.

They **do not** assert:

- CSS behaviour beyond `display` and a declared `height`, which 9.1–9.16 tests in
  its own 609 cases;
- a geometry the pass does not produce: no declared width in the *block*
direction, no percentage, no shrink-to-fit, no margin, padding or border
  arithmetic, and no **line-box** position or text extent. Those numbers do not
  exist yet, and a suite that asserted one would be asserting a number the module
  does not compute. The inline direction's numbers *do* exist now and are asserted
  exactly — 10.4's suite names the zero it produces for a text-bearing `<span>` as
  a fact rather than omitting the case.

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
| `layout.` | 125 — this module's 123, plus `demo.layout.flow` and `demo.layout.resize` |
| `layout` (no dot) | 127 — the above, plus `font.ot.layout` and `graphics.text.layout`, which are other modules' |
| `layout.module` | 11 |
| `layout.tree` | 14 |
| `layout.constraints` | 9 |
| `layout.context` | 11 |
| `layout.build` | 10 |
| `layout.dom` | 9 |
| `layout.fc` | 13 |
| `layout.block` | 16 |
| `layout.inline` | 23 |
| `layout.conformance` | 7 |

Whole suite: **2139** tests, green in both configurations.

```sh
./Out/RoboTest.host.exe --filter layout.block
./Out/RoboTest.host.exe --filter layout.block.context_gate
```

---

## 5. Sanitizers

```sh
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Test -Sanitize
```

ASan and UBSan run the same 2139 cases. In 10.4's run the sanitized suite was
**2139/2139** — no sanitizer diagnostic, and no red case. 10.3's run was
2116/2116 the same way.

That is worth stating precisely, because an earlier phase's sanitized run
reported `window.host.keyboard` and `window.host.mouse` and both are
environment-dependent rather than sanitizer findings: `docs/Build.md` §6 records
them as a host-desktop flake (`SetForegroundWindow` declining with `err=0`, so
`win32_focus` reports `ROBO_NOT_SUPPORTED` rather than faking success). The same
flake turned up in one unsanitized `robo test-runtime` run during 10.2 — 2099 of
2100, with `window.host.keyboard` the only red case — while the direct suite runs,
both unsanitized configurations and the sanitized run were all fully green.

In 10.3 it turned up in three consecutive `robo test-runtime` runs (2115 of 2116,
`window.host.keyboard` every time), and the evidence is now as clean as it is going
to get:

| How the suite was run | Result |
|---|---|
| `robo test-runtime` (the pipeline: build, then run) | **2115/2116** — three times in 10.3, **2138/2139** once in 10.4 |
| the same binary, run directly afterwards | **2116/2116** — twice in 10.3, **2139/2139** twice in 10.4 |
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
about" — and `win32_focus` reports `ROBO_NOT_SUPPORTED` rather than faking success,
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
`robo_layout_tree_add_object` resolved the borrowed `parent` pointer *before* the
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
A debug-only variable whose only use is inside `#if ROBO_MEM_DEBUG`, and two
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
`ROBO_OK` while doing it. `tree.add_invalidates` is the only case whose stated
purpose is to add a box to an already-classified tree. Fixed by having both context
doors report `ROBO_NOT_FOUND` for a box in an unclassified tree, which is the
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
it** (`layout.block.invalidation`). 10.2's `robo_layout_tree_contexts_reset`
dropped the context arena and cleared the classified flag, and left every box's
`robo_layout_result_t` exactly as it was. That was *correct* while nothing derived
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
