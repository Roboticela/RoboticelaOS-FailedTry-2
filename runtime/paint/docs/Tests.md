# RoboPaint Test Organisation (through Phase 11.3)

Five suites, 65 tests, one framework. Each phase adds no test runner, no reporter and no
assertion vocabulary: everything here goes through `runtime/Tests/Framework` (`ROBO_CHECK*`
and `robo_test_register`), the same as every other subsystem.

They live in `paint/tests/`, beside the module rather than in `Tests/Suites/`, for the same
reason `layout/tests/` sits where it does and `css/tests/` before it: a suite in its own
directory resolves its fixture header relatively, which is what `RoboPaintTestSupport.h` is.

`Tools/Build.ps1` adds `paint/tests` to the host include path and lists the five suites
explicitly in `$SuiteSrcs`. There is no glob: a suite that is not on the list is not built,
which is the "explicit over discovered" rule the rest of the tree follows.

---

## 1. Suite map

| Suite | Domain | Cases | Asks |
|---|---|---|---|
| `SuiteRoboPaint.c` | `paint.module.*`, `paint.types.*`, `paint.diagnostics.*`, `paint.geometry.*`, `paint.operation.*`, `paint.list.*`, `paint.dump.*` | 18 | is each piece a record *on its own* — the vocabularies, the derivation, the clip, the operation's two coordinate spaces, the list's ownership, the dump's format? |
| `SuiteRoboPaintTraversal.c` | `paint.traversal.*` | 16 | does one pass over a real document produce the operations, in the order, under the boundaries the architecture says it should? |
| `SuiteRoboPaintConformance.c` | `paint.conformance.*` | 5 | does the build's own surface — capabilities, vocabulary names, the drawing claim — say what the module claims? |
| `SuiteRoboPaintRecords.c` | `paint.records.*` | 13 | is the record model what it claims *as a model* — a regular sequence, an explicit content boundary, a snapshot that does not move, ownership that copies? |
| `SuiteRoboPaintDisplayList.c` | `paint.display.*` | 13 | does the display list *resolve* the box model (one rectangle per command), *preserve* the order and identity, *refuse* a record list that is not a document, and *snapshot* what it consumes? |

The unit suite and the traversal suite are deliberately not the same suite because they catch
different things: a hand-built record catches an arithmetic or a rule that a document would
produce by accident, and a document catches a rule that is right in isolation and wrong when a
box is nested four deep.

### 1.1 — the module surface and the records (`paint.module.*`, `paint.types.*`, `paint.diagnostics.*`)

`paint.module.lifecycle` runs `init`/`shutdown`/`is_initialized` and asserts a double init is
`ROBO_ALREADY_EXISTS`. `paint.types.kind_vocabulary` and `paint.types.domain_vocabulary` walk
each enumeration against its `_COUNT`, assert `is_known`/`name` agree with the walk, and pin the
derived predicates — that the four border kinds are exactly the ones `is_border` accepts, that
`op_kind_side` is total over them and refuses the rest, and that the two markers are exactly the
ones `is_marker` accepts. `paint.diagnostics.record` pins the domain-and-message record.

### 1.2 — geometry (`paint.geometry.*`, 6 cases)

- `boxes_record` — the three-box record's validity rule: nesting, the zero-size case, and the
  `insets_measured` rule (a record with the flag clear must have all three rectangles equal).
- `boxes_from_layers` — the one derivation, including the extreme case a saturated border box
  produces: the layers still nest, every extent is finite, and none is negative.
- `boxes_translate` — the page accumulation, including the saturated offset.
- `clip_record` — the two-axis record, the cleared value (`visible` on both), and the validity
  rule (a known mode per axis, an interval that does not run backwards on an axis that hides).
- `clip_rect` — the one case where a rectangle answers, and the **refusal** when an axis is
  unbounded.
- `clip_intersect` — the per-axis accumulation: a boundary that hides neither axis leaves the
  clip unchanged; a hidden axis intersects; an intersection that empties is success with an
  empty interval, not a failure.

### 1.3 — operations and the list (`paint.operation.*`, `paint.list.*`, 6 cases)

- `operation.record` — the validity rule: known kind, live node, nesting boxes, valid clip, and
  the colour rule (`none` refused on a drawing kind, required on a marker).
- `operation.accessors` — every read door, and the `NONE`/zero answers for NULL.
- `operation.side_band` — the band between the border box and the padding box, derived rather
  than stored, and the refusal for a non-border kind.
- `operation.write` — the one-line writer, including `ROBO_NOT_SUPPORTED` reporting the length
  it would have needed.
- `list.lifecycle` — create/clear/append/at/operations, capacity retained across `clear`, and
  the append **refusing** an invalid operation (the list is left untouched).
- `list.equality` — `robo_paint_list_equal`: empty lists equal, two NULLs equal, and the
  operation-by-operation comparison.

### 1.4 — the dump (`paint.dump.*`, 2 cases)

- `dump.format` — the fixed line format, one line per operation, indented by depth.
- `dump.line_bound` — the widest line this build can actually produce is measured and asserted
  against `ROBO_PAINT_DUMP_LINE_MAX`, so a future field that pushes past the constant fails
  here rather than overflowing a caller's buffer.

### 1.5 — the traversal (`paint.traversal.*`, 16 cases)

A document is built through the real chain — `RoboHTML → RoboCSS → the bridge → the tree → the
layout pass` — and then painted. The cases ask:

- **shape and order** — `single_box`, `siblings_in_order` (children left to right),
  `empty_element`, `refusals_and_empty` (a stale tree refused with `STALE` and an empty list; an
  empty tree painted as zero objects).
- **what draws** — `background`, `border_sides` (only sides with a used width), an empty
  element emitting only its marker pair.
- **box-model geometry** — `box_model_geometry`: `robo_paint_operation_local_boxes` equals what
  `robo_paint_boxes_of` reads out of layout, field for field, for every operation.
- **the clip** — `clip_visible` (no boundary), `clip_hidden` (a boundary at the padding box),
  `clip_axis_split` (`overflow-x: hidden` beside `overflow-y: visible` is not collapsed).
- **text** — `text_counted_not_painted`: a run is visited, counted in `text_runs`, and no
  operation is emitted for it.
- **lifecycle and safety** — `context_lifecycle`, `counts_and_determinism`, `extreme_geometry`,
  `allocator_balance`.
- **initial values** — `border_color_initial`: a border whose colour is not declared draws in
  `currentColor`, resolved against the element's `color`.

### 1.6 — the record model (`paint.records.*`, 13 cases)

The Phase 11.2 suite, and it asks the *guarantees* rather than a document's output:

- **the sequence** — `content_sequence` (one structural triple per box, balanced, each
  `CONTENT` between its markers), `structural_unconditional` (a box that draws nothing and has
  no children still gets its whole skeleton), `siblings_order`, `nested_order`.
- **the content boundary** — `content_geometry` (the `CONTENT` record acts on the content box
  and carries the same three nested rectangles as the box's markers), and `fields` (every
  record's kind, identity, depth, boxes, clip and colour rule; the structural/drawing split as
  an exact partition).
- **snapshot and ownership** — `snapshot_isolation` (paint a list, lay the *same* tree out
  again at a different viewport, and show the first list's records did not move while the
  second's did), `ownership` (append copies by value, lists are independent, `clear` keeps the
  capacity, a non-record is refused).
- **empties and safety** — `empty_cases` (empty body, zero-sized box, empty dump),
  `geometry_safety` (a box at the geometry maximum with a border, a zero extent and a nested
  pair — every stored record is still one), `determinism` (two passes, and the same document
  built into a *second* tree, compared by list and by dump), `dump` (one line per record, a
  `content=` line, and a refusal that reports the length it needed), `allocator_balance`.

### 1.7 — the display list (`paint.display.*`, 13 cases)

The Phase 11.3 suite, and it asks the artifact's guarantees rather than a document's output:

- **the mapping and the resolution** — `single_node_mapping`, `ordering`, `end_to_end`: a real
  document is painted and then built into a display list, and the two are walked in lockstep
  asserting the kind mapping and, for every pair, that the command's `rect` is exactly the
  record's resolved rectangle — the border box for a marker/background, the **band** (checked
  against `robo_paint_operation_side_band`) for a border, the content box for `CONTENT`. A
  concrete case pins that a 1px border's top band is exactly one high where the background is
  the whole box.
- **the vocabulary** — `command_vocabulary`: names, the partition into structural/drawing,
  and the marker predicate as the strict subset that excludes `CONTENT`.
- **the structural invariant** — `builder_refuses_malformed`: six hand-built record lists
  (an unclosed box, a wrong-depth begin, a boundary with no box, a close before the boundary,
  a marker naming the wrong node, a decoration after the boundary) are each **refused** with
  an empty output list, and a well-formed hand-built list is **not** refused — so the
  refusals are the rules rather than the builder rejecting everything it did not produce.
- **the clip** — `clip_carried`: the boundary a box's overflow put in force travels onto the
  descendants' commands.
- **empty and degenerate** — `empty_list` (an empty record list builds an empty display list
  with an empty dump), `empty_and_degenerate` (an empty body, a zero-sized box).
- **ownership and safety** — `ownership_and_balance` (create/clear/rebuild, capacity
  retained), `geometry_safety` (a cleared command, a negative rectangle and a colourless
  drawing are all refused by the list), `mutation_isolation` (clear the record list and
  re-lay-out the tree; the display list does not move, proved by a second build that *does*),
  `determinism` (two independent builds equal by list and by dump), `dump` (one line per
  command, and a refusal reporting the length it needed).

### 1.8 — conformance (`paint.conformance.*`, 5 cases)

- `capabilities` — the struct read positionally against a list of names, so a field added
  without an assertion is a visible omission.
- `drawing_absent` — `ROBO_PAINT_DRAWING_IMPLEMENTED`, `capabilities.drawing` and
  `robo_paint_drawing_is_implemented()` all agree, and all say 0.
- `vocabulary_exhaustive` — every kind and every domain prints a name that is not "unknown".
- `document_list` — a whole document's list, pinned operation by operation.
- `hostile_inputs` — NULL and malformed records refused rather than read.

---

## 2. Fixtures

`RoboPaintTestSupport.h` is the one fixture header, beside the suites. It builds a document
(the real RoboHTML/CSS chain) into a laid-out tree and paints it, so every traversal case starts
from a document rather than from a hand-built tree — a hand-built tree could agree with a bug in
the builder. The unit suite fabricates only the module's own **value types** (a border box, an
inset record, a clip), which are inputs a caller may legitimately construct; it never fabricates
a *layout result*.

The fixture's base stylesheet gives its containers `display: block`, because a container that
computes to `display: inline` makes layout decline its insets — a legitimate layout conclusion
that would look like a paint bug.

---

## 3. What the suites do and do not assert

They assert **records and relationships** — nesting, the two coordinate spaces agreeing, the
predicate tables, the refusal cases — rather than magic numbers. A paint list for a document is
pinned in `document_list`, but the arithmetic cases assert the property (three layers nest, an
axis is bounded) rather than a constant that would have to be re-derived by hand.

They do **not** assert anything about pixels, rasterisation, display lists, text or image
operations, opacity, transforms, scrolling, paint invalidation or hit testing: none of those
exists in this build, and a suite that asserted them would be asserting a later phase.

---

## 4. Running

```bash
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Verify -Test   # build + whole suite
./Out/RoboTest.host.exe --filter paint.                  # every paint case
./Out/RoboTest.host.exe --filter paint.geometry.         # one group
./Out/RoboTest.host.exe --filter paint.traversal.        # the traversal suite
./Out/RoboTest.host.exe --filter paint.records.          # the record-model suite
./Out/RoboTest.host.exe --filter paint.display.          # the display-list suite
```

`./scripts/robo test-paint` runs the module's group through the project pipeline. The filter is
a **substring** match, not a prefix, so `--filter paint.` also matches the demo layer's six
`demo.paint.*` cases: the group reports **71** for a module with **65** cases of its own. That
superset is recorded in `scripts/pipeline/commands.test.ps1` beside the group, the same way the
`layout` group's two-case superset is.

---

## 5. Sanitizers

The module and its suites run clean under ASan and UBSan. The only allocation is the list's
array, and `paint.traversal.allocator_balance` checks the module's allocation balance across a
pass, so a leak in the list's growth is caught rather than merely reported by a sanitizer.

---

## 6. What the 11.1 suites found

1. **A forward per-layer derivation can put the padding box outside its border box** at the
   extreme of the coordinate range. `paint.geometry.boxes_from_layers` caught it; the fix works
   inward with saturating arithmetic and floors each extent at zero.
2. **The two coordinate spaces need an assertion, not just a field each.** Nothing about writing
   `boxes` and `origin` keeps them consistent — `paint.traversal.box_model_geometry` holds them
   together by checking `local == boxes - origin` against layout, for every operation.
3. **A one-rectangle clip silently clips an axis the author left open.**
   `paint.traversal.clip_axis_split` is the case that requires the two-axis record.
4. **A guessed dump bound is a latent overflow.** `paint.dump.line_bound` measures the widest
   real line rather than trusting the derivation.
5. **The fixture needed `display: block` on its containers.** A container computing to
   `display: inline` makes layout legitimately decline its insets, which reads as a paint
   failure until the suite's own base stylesheet is fixed.

---

## 7. What the 11.2 suite found

1. **"Marker" did not partition the vocabulary.** 11.1's colour rule split kinds on
   `is_marker`, which had exactly two members — so `CONTENT`, a structural kind that is not a
   box marker, fell through to the drawing half and was asked for a colour it has no use for.
   Two failing conformance checks (`is_marker + is_drawing == 1`, and the document-list colour
   assertion) are what exposed it; the split is now `robo_paint_op_kind_draws`, and
   `paint.records.fields` asserts structural and drawing are exact complements.
2. **A snapshot test has to prove its mutation.** A box with a declared width would have made
   the second layout pass identical to the first, and `snapshot_isolation` would have passed
   for the wrong reason — the case now uses a block with no declared width, which fills its
   containing block, and asserts the two lists **differ** as well as that the first did not
   move.
3. **A count assertion is a calibration, not a constant.** Two cases (`nested_order`,
   `single_box`) named an operation total that omitted the decorations the document declares;
   the fix is to state the arithmetic in the case — structural triples plus drawing records —
   rather than a bare number, so the next kind added does not silently move it.

---

## 8. What the 11.3 suite found

1. **`RoboDisplay` is already taken.** The obvious header names for a display list —
   `RoboDisplayList.h` and its siblings — collide with the low-level **RoboDisplay** module's
   namespace *and* with the boundary scan, which forbids RoboPaint from including anything
   named `RoboDisplay`. The display list lives in RoboPaint's own namespace instead
   (`RoboPaintDisplay*.h`, `robo_paint_display_*`), which is where it belongs: it is
   RoboPaint's artifact, not the framebuffer module's.
2. **The version and domain-count pins are real work.** Bumping to `0.3.0` and adding the
   `display-list` diagnostic domain failed two existing cases (`paint.module.lifecycle`,
   `paint.types.domain_vocabulary`) until their pins were updated — which is the pins doing
   their job rather than a nuisance: a phase that changed a version or a vocabulary without
   noticing would have shipped a doc that disagreed with the build.
3. **The builder's stack is why it is an object.** The structural check needs the *node
   identity* of every open box, not just a depth counter, so a box's content boundary and its
   closing marker can be checked to name the box its opening marker named. That stack is the
   real state a reused builder carries.
