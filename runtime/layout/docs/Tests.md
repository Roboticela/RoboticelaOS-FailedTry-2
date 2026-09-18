# RoboLayout Test Organisation (Phase 10.1)

Seven suites, 71 tests, one framework. The phase adds no test runner, no reporter
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

### 10.7 — conformance (`layout.conformance.*`)

Seven cases, and the shape is 9.16's: ask the questions no single-module suite can.

| Case | Subject |
|---|---|
| `capabilities` | the capability struct walked **positionally** against a hand-written name list, with the struct's size pinned against the list's length |
| `statement` | the module's central claim, asserted three ways: `geometry == 0`, the function answers 0, and a pass leaves `boxes_computed == 0` |
| `no_geometry` | after a real pass over a real document, **every** box is `UNRESOLVED` and reading a position is `ROBO_NOT_FOUND` |
| `pipeline` | the whole chain over a realistic document: tokenizer → parser → match → cascade → inheritance → computation → bridge → build → pass |
| `determinism` | two builds of the same document produce equal trees, compared box by box |
| `robust` | a corpus of hostile and malformed documents: every one is either a clean failure or a clean empty build, and none crashes |
| `allocator_balance` | the whole chain, built and torn down repeatedly |

`capabilities` is a copy of the 9.16 finding applied one level up. In 9.16, two
CSS capability flags had flipped to 1 with no assertion anywhere in the build;
this suite walks all nine RoboLayout fields, so the same omission cannot happen
here. `no_geometry` is the case a future phase must *delete*, not weaken: the day
10.8 computes a coordinate it fails, and its replacement is a case that asserts the
numbers — which is the correct way for a claim like this to die.

---

## 2. Fixtures

`RoboLayoutTestSupport.h` is the only fixture header, and it supplies four things:

| Helper | Purpose |
|---|---|
| `layout_document_of(markup)` | parse markup into a live document and fail the test if it cannot |
| `layout_stub_style(name)` / a stub style source | build a tree **with no CSS at all**, which is what makes §10.2–10.5 possible |
| a real-DOM style fixture | stand up a style context and its bridge, for §10.6–10.7 only |
| equality helpers | compare two trees box by box, for `determinism` |

The second row is the design record's §6.1 argument made concrete: five of the
seven suites never instantiate a style context, and the two that do are exactly the
two whose subject is the joining.

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

- any coordinate, size, or rectangle construction — there are none to assert;
- CSS behaviour beyond `display`, which 9.1–9.16 tests in its own 609 cases;
- that the tree is *right* about anything CSS did not tell it, because there is
  nothing it claims.

A suite that asserted a coordinate in this phase would be asserting a number the
module does not produce. `no_geometry` asserts the *absence* instead, which is the
thing that is actually true.

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
| `layout.` | 73 — this module's 71, plus `demo.layout.flow` and `demo.layout.resize` |
| `layout` (no dot) | 75 — the above, plus `font.ot.layout` and `graphics.text.layout`, which are other modules' |
| `layout.tree` | 14 |
| `layout.constraints` | 9 |
| `layout.context` | 11 |
| `layout.build` | 10 |
| `layout.dom` | 9 |
| `layout.conformance` | 7 |

Whole suite: **2087** tests, green in both configurations.

---

## 5. Sanitizers

```sh
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Test -Sanitize
```

ASan and UBSan run the same 2087 cases. RoboLayout's own suites are clean under
both. The one red case in a sanitized run is `window.host.keyboard`, which
`docs/Build.md` §6 records as a host-desktop flake (`SetForegroundWindow` declining
with `err=0`, so `win32_focus` reports `ROBO_NOT_SUPPORTED` rather than faking
success) and which passes 3/3 standalone and in both unsanitized configurations.
No `layout.*` case touches it.

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
