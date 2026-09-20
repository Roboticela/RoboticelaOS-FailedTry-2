# RoboPaint Test Organisation (through Phase 11.8)

Nine suites, 134 tests, one framework. Each phase adds no test runner, no reporter and no
assertion vocabulary: everything here goes through `runtime/Tests/Framework` (`ROBO_CHECK*`
and `robo_test_register`), the same as every other subsystem.

They live in `paint/tests/`, beside the module rather than in `Tests/Suites/`, for the same
reason `layout/tests/` sits where it does and `css/tests/` before it: a suite in its own
directory resolves its fixture header relatively, which is what `RoboPaintTestSupport.h` is.

`Tools/Build.ps1` adds `paint/tests` to the host include path and lists the nine suites
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
| `SuiteRoboPaintBackground.c` | `paint.background.*` | 14 | does the colour cross into RoboGraphics, does a document's `background-color` land **where the list said it would** (read back from the surface), and do the pass's counters add up on a document and on a hand-built list? |
| `SuiteRoboPaintBorder.c` | `paint.border.*` | 14 | does a border side's **band** get filled (read back from the surface), does its **style** survive record → command, do four sides of four different widths and colours land exactly where the box model says, and does a child spilling over its parent draw over the parent's border rather than under it? |
| `SuiteRoboPaintText.c` | `paint.text.*` | 17 | do a run's **characters** travel record → pool → display pool → pixels, are they counted in **code points**, is the baseline layout's rather than a second one, does an unmapped code point become a counted `.notdef`, and does a list keep drawing after the records it was built from are gone? |
| `SuiteRoboPaintClip.c` | `paint.clip.*` | 24 | is the boundary a **scope** — does the intersection hold, does a scope appear exactly when the boundary *changed*, is the box's own decoration outside it, is the effective clip inside a nested scope the intersection of the two, does every drawing kind stop at it, and does an empty one suppress drawing without poisoning the walk? |

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

A `draw-border`'s line carries one field no other kind has: `style=`, the style that shapes the
band (11.5). It is the one piece of a command a reader could not otherwise tell from a `solid`
one, which is exactly the fact the field exists to carry.

### 1.8 — the background pass (`paint.background.*`, 14 cases)

The Phase 11.4 suite, and it is the first in this module that **reads a surface back**:

- **the colour bridge** — `color_bridge`: a resolved colour copies field for field (alpha
  included), a fully transparent colour converts and does not draw, and the three kinds that are
  not colours a surface can take each get their own answer (`currentColor` refused, `none`
  absent, `invalid` a mistake).
- **a colour reaches a surface** — `pixel_fill`: a document's box is painted, its colour is read
  at the centre of the rectangle the display list named, and one pixel past that rectangle is
  still untouched.
- **order** — `ordering`: a parent's fill comes first in the list, so where the two overlap the
  child's colour is on the surface -- depth-first traversal, demonstrated on pixels.
- **clipping** — `clipping`: the same document with `overflow: hidden` and with `visible`, one
  declaration apart, where a point far outside the ancestor but inside the spill's own
  rectangle is red under `visible` and untouched under `hidden`; plus the axis-split mechanism
  read from a hand-built command (bounded horizontally, free vertically).
- **nothing to paint** — `nothing_to_paint` (no background declared, and `transparent`: no
  command exists at all, so `skipped == commands`), `border_only_document` (a border-only
  document: four bands, four fills, no backgrounds -- the case that used to pin the opposite and
  now pins where the fence moved at 11.5), `zero_area` (an opaque background on a zero-height
  box: a valid record, an `empty` non-fill).
- **positioned rectangles** — `positions`: two documents one declaration apart, where the
  relative offset moves the fill by exactly `(20, 10)`, does not change its size, and leaves the
  control's corner untouched.
- **the counters** — `counters`: the three identities (`commands = draws + skipped`,
  `draws = backgrounds + borders`, `draws = filled + contextual + transparent + empty`) plus the
  pixels checked against the list's own record of who writes where, through an oracle that
  follows every **drawing** command rather than backgrounds alone.
- **the painter's own contract** — `hand_built_kinds`: a hand-built list exercising the four
  "draws nothing" reasons and one that draws, with each counter asserted and the clip applied.
- **determinism and isolation** — `determinism` (two surfaces, every fill's centre identical,
  and a repeat pass in place unchanged), `mutation_isolation` (clear the record list and
  re-lay-out the tree; the display list paints exactly as it did, proved by a second build that
  *does* differ).
- **empties and refusals** — `empty_cases` (an empty list, a structural-only list, `NULL`
  arguments, and a **read-only** target refused with `ROBO_NOT_SUPPORTED`).
- **end to end** — `end_to_end`: the whole chain for a document using the box model, a nested
  child, a relative offset and both overflow modes.

### 1.9 — the border pass (`paint.border.*`, 14 cases)

The Phase 11.5 suite, and `paint/docs/Border.md` §7 lists what each case pins. What it adds to
11.4's shape:

- **the style, end to end** — `style_reaches_the_command`: for `solid`, `dashed`, `dotted` and
  `double`, the **record's** accessor and the **command's** field agree on all four sides, and
  every other kind carries `none`. This is the phase's one new field, asserted at both ends of
  the chain because the interesting failure is a style that survives the traversal and is
dropped by the builder.
- **the band's geometry, derived rather than assumed** — `four_bands` and `per_side_widths`
  compute the four expected rectangles from the box's own border box and its declared insets, so
  the assertion is about the derivation and not about this build's default margin.
- **the order that required one pass** — `child_over_parent_border`: a relatively offset child
  spilling over its parent's border, asserted on the pixels. Two passes, one per drawing kind,
  would fail this case and nothing else in the suite.
- **what a band is not** — `absent_sides` (six documents one declaration apart: `none`, zero
  width, `hidden`, a single-side shorthand, a per-side override) and `with_background` (the
  ring, the interior and the outside, all probed on one box).
- **the simplification as a number** — `pattern_styles`: each of the seven pattern styles
  asserts both the command's style *and* that the band was still filled, which is what keeps
  `unshaped` a count rather than a claim.
- **the corner rule, pinned where it is visible** — `colors_and_corners`: the four corner pixels
  say which band was drawn last.

### 1.10 — the text pass (`paint.text.*`, 17 cases)

The Phase 11.6 suite, and the first one whose expectation is a **string** rather than a number
or a colour:

- **the characters travelled** — `record_shape` (one run produces one record, carrying a
  non-empty span, a baseline and the measurer's font inputs), `characters_are_code_points` (an
  accented word is four code points and five UTF-8 bytes, and the span and the pool both say
  four), `command_and_span` (the record list's pool *and* the display list's **own** pool hold the
  same characters, and the two runs are equal field for field), `multiple_runs` (two elements'
  runs are two records, in document order, with disjoint spans).
- **the geometry is layout's** — every text record's local boxes are checked against the tree
  with the same helper the box suites use, and `order_and_depth` asserts a `draw-text` sits at
  exactly one past its parent box's depth, names its own node, and appears after that box's
  `CONTENT`.
- **two lines, two placements** — `wrapped_lines`: two participants in one inline context, the
  second unable to follow the first, land at two different line tops and read back as two runs
  — because a text node is measured **as a unit** in this build and therefore wraps whole rather
  than fragmenting.
- **colour** — `color` (the record's accessor and the command's field are the document's
  `color`) and the same `paint_color_eq` bridge the background suite uses.
- **glyphs reach a surface** — `pixels`: a synthetic face (a cmap, an hmtx, an hhea and one
  square outline) is supplied, the run's two code points resolve through it, their outlines
  stroke into a real surface, and the run's own colour appears where the command said it would.
  `missing_glyph` pins the other half: an unmapped code point draws `.notdef` and is **counted**.
- **no face is a number** — `no_font`: a document with text painted with no face paints nothing
  where the text was, reports `no_font`, and the structural and background commands are
  unaffected.
- **logical order, stated** — `rtl`: an RTL run's characters are drawn left to right from its
  origin and the run is counted in `rtl`, which is how a build with no bidi says so.
- **the pool is a boundary** — `mutation_isolation`: the display list is written to text, the
  **records are freed**, and it writes again byte for byte and still paints the same pixels.
- **empties, hand-built rules, the doors** — `empty_cases` (an empty body, an empty element, a
  whitespace-only run, a zero-width box), `hand_built_rules` (a `text` record with an empty span,
  a negative baseline offset or non-record font inputs is refused, and one with real characters is
  not), `pool_doors` (append/read/contains/copy: a `count` of zero refused, a span past the end
  refused, a too-small output buffer reporting the count it needed).
- **determinism and dump** — `determinism` (the same document built into a *second* tree, compared
  by every run, every span and both dumps) and `dump` (a record line and a command line each carry
  `text="..."`, and the dump reads the characters through the pool's own door).

### 1.11 — the clip pass (`paint.clip.*`, 24 cases)

The widest phase suite, because clipping is the first thing that has to be right in **four** places
at once — the arithmetic, the traversal, the artifact and the pass — and because a boundary that is
right in three of them draws a wrong picture.

- **the arithmetic** — `intersection` (two bounded records, a contained one, one bounded axis each,
  two unbounded, **two disjoint** with the canonical empty interval and its fixed point, touching
  edges, a zero-size interval, negative coordinates, and the same canonicalization through the
  boundary door), `intersection_safety` (NULL, an unknown mode on either side, a record whose
  interval runs backwards, a boundary with a negative size — each refused, none inventing a result),
  and `empty_predicate` (unbounded vs empty vs invalid kept apart).
- **the stack** — `stack` (push/pop/current/depth/capacity/reset, a pop on an empty stack refused
  rather than silently no-op, growth past the first capacity) and `stack_nested` (three scopes deep,
  each pop restoring the one outside it, an empty scope legal and non-poisoning).
- **what the traversal emits** — `scopes_emitted` (one push and one pop, after the box's `CONTENT`
  and before its `END_BOX`, the push's rectangle the padding box and its clip the boundary) and
  `scope_only_when_changed` (a `visible` box opens none; a hidden box inside a hidden ancestor that
  already bounds the same region opens none; the two `clipped`/`clip_scopes` counters say so).
- **nesting, recomputed** — `nested_effective`: the effective clip inside a nested scope is
  **recomputed from the tree** through RoboLayout's own doors and compared to the pushed record, and
  the narrowing is pinned where it is real (both starts the inner box's own, both ends the enclosing
  boundary's).
- **one axis** — `axis_split`: `overflow-x: hidden` beside `overflow-y: visible` bounds one axis and
  leaves the other open, which is what one rectangle cannot say and what `robo_paint_clip_rect`
  refuses to flatten.
- **every drawing kind stops at the boundary** — `background`, `border` and `text` each paint the
  same document with and without a scope and read the surface back on both sides of the edge.
- **pixels** — `pixels`: the document is painted into a real 800×600 surface with `overflow: hidden`
  and again with `visible`, and the *same* assertions run on both: inside the box both agree; in the
  spill beyond the boundary only the visible one paints; the boundary's last pixel is painted and the
  first pixel outside it is not; the hidden surface encodes to a PNG whose signature and width are
  checked; and the visible document carries **no scope at all**, exactly two commands shorter.
- **the empty scope** — `empty_scope`: a box placed 200px outside its ancestor's 40px boundary opens
  a scope with an empty interval, paints nothing, and the sibling **after** it still paints — the
  walk is not poisoned. The interval is asserted in its canonical shape (`end == start`, valid).
- **the no-clip case** — `no_clip`: a document that declares no overflow produces byte-identical
  lists to one built before the phase's vocabulary existed, and no clip commands at all.
- **multiplication and malformed input** — `empty_and_malformed` (an empty document, an empty
  element, a zero-size box, a push with no box, a pop with no push), `hand_built_scope` (a hand-built
  list whose drawing carries **no** clip of its own, inside a scope whose clip is empty: nothing is
  filled, and with a bounded scope the same list fills exactly inside it — which is what proves the
  stack is load-bearing rather than decorative), `pass_refuses_unbalanced` and `builder_refusals`
  (nine scope refusals in **eleven** separate messages -- the three pop failures are distinct -- each
  leaving the output list **empty**).
- **snapshots and determinism** — `determinism` (the same document built twice, twice more from a
  second tree, compared record by record and dump by dump), `mutation_isolation` (the record list is
  cleared and the tree is laid out again at another viewport: the stored boundary is the one the
  first list was built with, the rebuilt list's is **different** — so the equality is a copy and not
  a mutation that failed to bite), `dump` (a scope appears as two named lines in both dumps, in
  order, with no addresses) and `end_to_end` (document → records → display list → surface, with both
  counters reported).

### 1.12 — conformance (`paint.conformance.*`, 5 cases)

- `capabilities` — the struct read positionally against a list of names, so a field added
  without an assertion is a visible omission.
- `drawing_present` — `ROBO_PAINT_DRAWING_IMPLEMENTED`, `capabilities.drawing` and
  `robo_paint_drawing_is_implemented()` all agree, and all say 1; `background_painting`,
  `border_painting`, `text_painting` and `clipping` say which parts of the pipeline exist and
  `raster` still says 0.
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

Since 11.4 they **do** assert pixels — but only where a pixel is the claim: the background
suite reads a real surface back, because "the colour reached RoboGraphics" is not a statement
about a record. The expectation at a point is the **last** drawable command whose rectangle
contains it and whose clip admits it, so the check is the list's own order rather than an
assumption that every fill is visible.

Since 11.6 they assert **characters**, and the interesting part is where the expectation comes
from: a run's text is compared against the *document's own* string, read back through the pool's
door, so a suite cannot pass by agreeing with the traversal's copy of it. Glyph expectations are
**arithmetic**, because the suite's face is synthetic and stated as numbers (`Text.md` §8 records
why: a face never prepared, or a cmap table that is *nearly* right, both make a font test pass for
the wrong reason).

They still do **not** assert anything about border shape rasterisation, image painting, real
clipping, opacity, transforms, scrolling, a whole-list rasteriser, paint invalidation or hit
testing: none of those exists in this build, and a suite that asserted them would be asserting a
later phase.

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
./Out/RoboTest.host.exe --filter paint.background.       # the background-painting suite
./Out/RoboTest.host.exe --filter paint.border.           # the border-painting suite
./Out/RoboTest.host.exe --filter paint.text.             # the text-painting suite
```

`./scripts/robo test-paint` runs the module's group through the project pipeline. The filter is
a **substring** match, not a prefix, so `--filter paint.` also matches the demo layer's six
`demo.paint.*` cases: the group reports **116** for a module with **110** cases of its own. That
superset is recorded in `scripts/pipeline/commands.test.ps1` beside the group, the same way the
`layout` group's two-case superset is.

---

## 5. Sanitizers

The module and its suites run clean under ASan and UBSan. The only allocation is the list's
array, and `paint.traversal.allocator_balance` checks the module's allocation balance across a
pass, so a leak in the list's growth is caught rather than merely reported by a sanitizer.

---

## 6. What the 11.1 suite found

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

---

## 9. What the 11.4 suite found

1. **A two-document comparison must spell out both documents.** The clipping case had been
   written to build its stylesheet from a variable, and the variable was left at one value — so
   both runs laid out the *same* document and the case "compared" a thing with itself. The fix
   is not a bug fix but a shape: the two sheets are written out in full, side by side, so a
   reader can see that the documents differ in exactly one declaration.
2. **A test that asserts "untouched" needs a surface nothing has touched.** The hand-built clip
   mechanism was checked on a surface the document's own fills were already on, so "transparent
   here" failed against a red that had every right to be there. The mechanism gets its own
   surface.
3. **"Every fill's centre is its own colour" is false the moment two fills overlap** — a
   child's background covers its parent's, which is the *correct* behaviour the ordering case
   asserts. A pixel expectation is therefore computed from the list (the last drawable command
   whose rectangle contains the point and whose clip admits it) rather than assumed per fill,
   and that oracle is also what makes a clipped document's pixels checkable at all.
4. **The read-only case is the one that found a real disagreement.** A read-only target is
   refused by the graphics layer with `ROBO_INVALID_ARGUMENT` where the painter's own header
   promised `ROBO_NOT_SUPPORTED` — the *test* is what made the two statements meet. See
   `paint/docs/Architecture.md` §14.8.

---

## 10. What the 11.5 suite found

1. **A corner belongs to whichever band is drawn last, and the assertion has to say which.**
   The corner case was written expecting `right` to own the bottom-right corner, because that is
   where the *right* band's edge is — but the four bands are emitted top, right, bottom, left and
   the bottom band spans the full width, so `bottom` is drawn over it. The build was right and
   the expectation was a guess; the four corners are now derived from the emission order and
   stated as such, with a comment saying the whole rule is this build's simplification rather
   than CSS's.
2. **A generalised pass makes an old counter assertion false, and that is the point.** Three
   cases that had pinned "a border band is skipped" (a border-only document's `skipped ==
   commands`, and two `skipped >= borders` bounds) went red the moment bands became drawings.
   Each was rewritten to the new truth rather than loosened, and the border-only case now pins the
   opposite fact — four bands, four fills, one per side — which is the fence moving one command
   kind along.
3. **A pixel oracle has to follow *every* drawing, not just the kind it was written for.**
   11.4's "the last command that covers this point" consulted backgrounds alone, because
   backgrounds were the only thing that drew. With bands in the list an oracle that ignored them
   would expect an ancestor's background *under* a band that is really drawn over it, so the
   oracle now follows both drawing kinds — which is the same one-line predicate the pass itself
   uses (`robo_paint_display_command_kind_draws`).
4. **A hand-built record needs the new field, and the suite is where that shows.** 11.5's style
   rule made `robo_paint_operation_is_valid` refuse a `BORDER_*` record with no style, which
   failed the dump-format and accessor cases in the unit suite — records built by hand for
   assertions *about* the record. The fix is the fixture's: `paint_drawing_make` now sets the style
   the kind requires (`solid` for a border, `none` for a background), and the validity case
   asserts both halves of the rule in both directions.

---

## 11. What the 11.6 suite found

1. **A structural check ordered before the rule it exempts is unreachable.** The display
   builder's text branch was written *after* the generic "a record's depth is its innermost box's"
   check — a rule a text run fails by construction, because a run is owed one level **down** from
   the box that owns it. Every document containing text was refused with that message and the
   text branch beneath it could never run. The fix is an ordering rather than a rule: a run's own
   checks come first, the generic checks follow for every other kind. It is recorded because the
   message named the rule the record was *supposed* to break, which is the most misleading a
   refusal can be.
2. **The pixel oracle had to learn a third kind — and the counter identities a fourth bucket.**
   11.4's oracle knew backgrounds, 11.5's learned bands, this one learned runs; and the text pass
   added `no_font` to `draws = filled + contextual + transparent + empty`, because "there was no
   font to draw with" is a fact about the *call* and not about the document. Each addition was
   caught by an identity assertion rather than by inspection.
3. **A synthetic face needs its cmap prepared, and a hand-written format-4 table needs exactly
   the bytes the format says.** The suite's cmap wrote one segment too many for two segments,
   which shifted `idRangeOffset` onto the wrong word; the parser read a plausible glyph array out
   of the extra bytes, every lookup resolved past `maxp`'s `numGlyphs`, and the pass reported
   `INVALID_FONT` per code point while drawing nothing. Both halves are recorded because they are
   the two ways a font test passes for the wrong reason — a face that was never prepared, and a
   table that is *nearly* right — and because the second looks exactly like a rendering bug from
   outside.
4. **A wrapped line is two participants, not one fragmented run.** The wrapping case was written
   expecting one text node to produce several line fragments. RoboLayout measures a text node **as
   a unit** and a run wider than its line wraps whole rather than breaking, so the case was
   asserting a line-breaking behaviour that belongs to a later phase. It now uses two participants
   in one inline context and asserts what the build does: two runs, two line tops, two records —
   which is a stronger statement than the original, because it pins that the wrap is layout's
   rather than paint's.

---

## 12. What the 11.8 suite found

Four, and the first two are one bug seen from two sides:

1. **A containment test written backwards refuses exactly the documents it was written to accept.**
   The builder's "a scope must be at least as tight as the boundary it is inside" check was spelled
   `narrow.start > outer.start || narrow.end < outer.end` — the *opposite* predicate. An inset child
   (`[20, 200]` inside `[0, 200]`) satisfies `20 > 0`, so every document with a **nested** scope was
   refused with *"a clip scope is wider than the boundary it is inside"*, while a document with a
   single scope never reached the check and passed. The fix is eight characters
   (`outer.start <= narrow.start && narrow.end <= outer.end`) plus the case that would have caught it
   immediately: a document with two scopes and an expectation **recomputed from the tree**.
2. **A disjoint intersection has to be canonicalized, or the record type lies.** An excluded box
   produced `[200, 40]`, which `robo_paint_clip_is_valid` refuses — so "nothing can be drawn here",
   the one cell a clip model most needs, was the one cell the model could not hold. Canonicalizing to
   `[max(start), max(start)]` makes it a valid empty record that survives a push, a pop, a command
   and a projection; the test pins the shape *and* the fixed point.
3. **`filled` counts fills attempted, and an empty clip is a no-op inside the raster.** The suite's
   empty-scope case asserted `filled == 0` and was wrong about the counter rather than about the
   pixels — the pass executes the fill, the raster bounds it out, the pixel stays transparent. The
   claim was rewritten to the pixel and `clipped`, and the counter's documentation now says what it
   counts.
4. **A no-clip case is weaker than it looks if it only checks that the clip is inactive.** The
   visible half of the pixel showcase initially asserted an inactive `clip` field — which a document
   carrying a redundant scope would also satisfy. It now asserts there is **no push and no pop**
   anywhere in the list and that the list is exactly two commands shorter than the clipped one, which
   is the fact 11.8's "avoid unnecessary clip commands" actually means.
