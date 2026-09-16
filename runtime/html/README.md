# RoboHTML

HTML document subsystem for Roboticela Runtime (**Phase 8.1 architecture +
Phase 8.2 tokenizer + Phase 8.3 tree builder + Phase 8.4 document model +
Phase 8.5 element model + Phase 8.6 attribute model + Phase 8.7 text model +
Phase 8.8 traversal + Phase 8.9 DOM mutation + Phase 8.10 document lifecycle +
Phase 8.11 basic HTML elements + Phase 8.12 images + Phase 8.13 links +
Phase 8.14 forms**).

**Version:** 0.14.0 — `ROBO_HTML_API_FROZEN=0` (freezes in a later Phase 8 goal)

Public RoboHTML never depends on Chromium, and — per `DEPENDENCIES.md` — never
depends on RoboCSS / RoboLayout / RoboPaint / RoboRender / RoboWindow /
RoboGraphics / RoboDisplay. Those layers sit *above* RoboHTML and consume it.

## Scope

**8.1 (architecture):** module boundary, document model, node identity, parser
boundary, source/encoding/resource boundaries, lifecycle, diagnostics, and
integration hooks.

**8.2 (tokenizer):** HTML source → deterministic token stream (character /
start-tag / end-tag / comment / doctype / EOF), attributes, comments,
doctypes with public+system identifiers, character references (named /
decimal / hex, legacy semicolonless rules), an explicit ~74-state machine,
source positions, bounded diagnostics, error-tolerant recovery, and the
controlled raw-content switch entry (`RAWTEXT` / `RCDATA` / script data /
`PLAINTEXT`) that the tree builder will drive.

**8.3 (tree builder):** consumes the 8.2 token stream and constructs the real
document tree — `Document`, `DocumentType`, `Element`, `Text` and `Comment`
nodes with document-owned lifetimes, an explicit open-element stack, the
foundational insertion modes (`initial` … `in body`, `after after body`, plus
text), implied `html`/`head`/`body`, element payload storage (names,
attributes, text) in the document's payload arena, bounded error-tolerant
recovery, and a tree dump for debugging and tests.

**8.3 is not the full HTML5 tree-construction algorithm.** Deliberately absent,
with the seam kept open: active-formatting elements / adoption agency, tables
and foster parenting, foreign content (SVG/MathML), fragment parsing, and
DOCTYPE document-mode behaviour. See `docs/TreeBuilder.md`.

**8.4 (document model):** the document becomes the authoritative model of the
tree it owns — an identity-and-state metadata snapshot, explicit root
ownership that survives re-parsing, node ownership and cross-document checks,
generation-guarded staleness, explicit construction closure (`finalize`) and
reuse (`reset`), and on-demand whole-tree invariant auditing
(`validate_tree`). Ids restart dense on a tree clear so id lookups stay linear
however often a document is re-parsed, while generations — not ids — are what
make a stale handle detectable. See `docs/DocumentModel.md`.

**8.5 (element model):** the element becomes a first-class part of the model
without becoming a second ownership system — an element *is* a node. Adds a
per-element namespace (`HTML`/`SVG`/`MathML`, with URIs as constants) and a
derived classification (`root`/`metadata`/`section`/`heading`/`paragraph`/
`text`/`replaced`/`control`, `generic` for anything unrecognised), an element
state mask (parser-created / connected / self-closing), the attribute read
boundary that Phase 8.6 will replace, and `robo_html_element_create` as the
single construction seam the tree builder already goes through and the future
DOM will. Stored names are lowercase ASCII for the DOM seam as well as for
parser input, and a name too long to normalize is stored verbatim rather than
truncated. See `docs/ElementModel.md`.

**8.6 (attribute model):** attributes stop being a payload detail and become
the runtime's single attribute representation — one `robo_html_attribute_t`
staged by the tokenizer, stored by the tree builder, owned by the element and
returned by the readers. Deterministic stored order, `set`/`add`/`remove` with
defined duplicate and case semantics, presence that is a *record* rather than
a non-empty value (`<input disabled>` and `<input disabled="">` are both
present-and-empty, and both distinct from absent), a bounded table that
refuses rather than truncates, and a mutation implemented as a measured
payload rewrite so borrowed bytes never dangle. See `docs/AttributeModel.md`.

**8.7 (text model):** character data becomes a modelled node rather than a
payload detail. Adds `robo_html_text_create` as the construction seam (the
text twin of `robo_html_element_create`), a view with content / length /
parent / state, `set` and `append` mutation, and explicit answers for the
not-a-text-node case (`content` returns `NULL`, not `""`). Whitespace and
Unicode are preserved byte-for-byte — nothing collapses, trims, normalises or
re-encodes — and a character run coalesces per *run*, so the tokenizer's
segmentation is never visible in the tree. The tree dump labels text nodes so
a whitespace run is legible. See `docs/TextModel.md`.

**8.8 (traversal):** the tree becomes navigable as a boundary rather than a
set of accessors — relationships in both directions, absolute depth and
rootmost-ancestor traversal, deterministic pre-order/document-order
enumeration (iterative, allocation-free, with a cycle bound that reports
rather than truncates), an O(n) child cursor, the all/elements/text filters,
and bounded search (`find_first` at-or-below vs `find_descendant` inside).
Three separate copies of the pre-order successor collapsed into one shared
implementation, and child iteration by index (O(n²) per child list) moved
onto the cursor. See `docs/Traversal.md`.

**8.9 (DOM mutation):** the tree becomes editable — append, insert before,
remove, replace and bulk child removal, all on a finalized document. Every
rule lives in one linking primitive shared with the parser: one parent per
node (inserting an attached node is refused, never silently moved), acyclic
results (no node beneath its own descendant), the document node never a
child, and only a document/element/fragment as a parent. Removal detaches
without destroying — the node keeps its subtree, attributes and text and can
be re-inserted — so detachment is a legal, reported state rather than
corruption. Cross-document mutation is refused rather than guessed. Every
structural change bumps a monotonic per-document tree version, which is what
lets traversal report an iteration invalidated by a mutation instead of
handing out a node that is no longer where it was read from. See
`docs/Mutation.md`.

**8.10 (document lifecycle):** the document gains an explicit lifecycle in the
web platform's own `readyState` vocabulary — `created`, `loading`,
`interactive`, `complete`, plus `failed`/`destroying`/`destroyed`. The two ideas
it turns on are that INTERACTIVE is a statement about the *tree* (parsing is
done, the DOM is live) and COMPLETE is a statement about the *input* (no further
input can arrive). So completion never closes the DOM — reading, traversal,
mutation, a replacement source and reset all keep working — and the graph has no
shortcut into it: `LOADING → COMPLETE` and `CREATED → COMPLETE` are refused,
because a document cannot be finished loading before it has finished parsing.
The rules that decide which entry point settles where follow from the same idea,
and a truncated build settles at INTERACTIVE rather than claiming completion for
input it never read. Transitions are not structural changes: the tree version
does not move, so an in-flight traversal survives one. See `docs/Lifecycle.md`.

**8.11 (basic HTML elements):** an element gains a *type*, not just a name.
The tag vocabulary in `RoboHTMLTag.h` names every element this runtime
recognises — the 32 basic elements (document structure, structural content,
headings, lists, inline semantics, generic regions) plus the whole vocabulary
Phase 8.5 already classified by name — and gives each one its canonical
spelling, its coarse role and its intrinsic flags (void, heading, list, list
item, sectioning, region, metadata, text-level, document structure) in a
single row. Identity is *derived* from `(namespace, name)` exactly as the role
already was, so no payload grew and nothing can go stale, and it is read
through a handle (`robo_html_element_tag` / `robo_html_element_is_tag`) so a
consumer names a symbol instead of comparing a string. Two private copies of
that knowledge were deleted rather than kept in step: the element model's
name→role table (now a delegation) and the tree builder's void-element list
(now a flag), so "what is `<div>`?" and "is `<br>` void?" each have one answer.
An unrecognised element — a custom element, a future one, anything outside the
HTML namespace — is `UNKNOWN`, which is a supported state: it keeps its name,
attributes, children, traversal and mutation behaviour and is never coerced
into a known type. See `docs/Elements.md`.

**8.12 (images):** an `<img>` becomes a resource rather than a tag. Adds
`RoboHTMLImage.h`, which reads the image-relevant attributes
(`src`/`alt`/`width`/`height`) and models the resource behind them — a state
(`NONE`/`PENDING`/`DECODED`/`FAILED`/`UNSUPPORTED`) plus, when decoded, real
RGBA8 pixels from RoboGraphics' new PNG decoder. **Bytes arrive through a
seam, never through RoboHTML**: a caller installs a resolver, RoboHTML hands it
the reference and receives bytes borrowed for the duration of the call, so the
future network loader is an *implementation of a resolver* rather than a change
to this model. The one resolver this phase ships is an in-memory asset table —
it cannot touch the filesystem, so there is no path to traverse — and a
reference naming a scheme (`http:`, `file:`, `data:`) is refused as
UNSUPPORTED rather than reported as not-found, because "we will not fetch
that" and "those bytes are not here" are different facts. **Failure is a
state, not an exception**: a missing asset, undecodable bytes and a refused
scheme all leave the DOM exactly as it was, which is the only shape that keeps
an `<img>` from being able to break a document. Declared dimensions are parsed
by the HTML rule (ASCII digits, no `px`, no overflow) and an invalid one is
*reported* rather than clamped. No CSS, layout, intrinsic sizing, `srcset`,
`<picture>`, `load`/`error` events, accessibility tree or networking. See
`docs/Images.md`.

**8.13 (links):** an `<a>` becomes a navigation target. Adds
`RoboHTMLAnchor.h`, which reads the anchor attributes (`href`/`target`/`rel`/
`download`/`title`) without interpreting them, resolves a reference against the
document's own reference into a navigation target — `ROUTE`, `FRAGMENT`,
`EXTERNAL`, `INVALID` or `NONE` — classifies the `target` context, and reports
every activation through an outcome vocabulary **that has no "navigated"
kind**, so nothing can claim navigation this runtime did not perform. Two rules
carry the phase. First, **a scheme the runtime does not permit never reaches a
handler**: `javascript:`, `data:` and `file:` are refused by policy before the
seam is consulted, which is why an attribute cannot be used to reach a
subsystem the document was never granted. Second, **the outcome owns the
resolved target it reports**, because activation resolves the reference itself
and a borrowed target would dangle the moment the call returned. Fragments are
the one navigation this phase completes, and it completes them locally —
`#Overview` names an element in *this* document and finding one needs no I/O,
so it is answered here as `FRAGMENT` or `FRAGMENT_MISSING` rather than
delegated. `rel` is preserved and answerable, never interpreted; `download` is
an intent and an activation that would need to save something says
`DOWNLOAD_PENDING`; `_blank` is a distinct context, not a new tab. RoboticelaOS
is an application runtime, so this phase invents no origin, cookie jar or
session and adds no security model of its own — it refuses, and says why. See
`docs/Links.md`.

**8.14 (forms):** the form controls gain a model. Adds `RoboHTMLForm.h`, which
reads `action`/`method`/`enctype`/`name`/`target` off a `<form>`, `for` off a
`<label>` and resolves it to the control it names, the typed attribute set of an
`<input>`/`<button>`/`<textarea>`/`<select>`/`<option>` (with `min`/`max`/`step`
parsed as numbers rather than compared), the input type vocabulary and its
`unknown`→`text` degradation, the select/option ordering and selection rules, and
— the point of the phase — the split between a control's *declared* value (the
attribute, or a textarea's children) and its *current* value (an explicit
override kept in a per-document table beside the element that owns it), the same
for checkedness, with the two overrides independent because in HTML they are
separate dirty flags. So `<input value="a">` then `.value = "b"` leaves the
attribute saying `a` and the control saying `b`, and the answer reports which of
the two it gave rather than making a caller compare strings. **Nothing is
cached**: a form owner, a label's target and a select's selected option are
derived on demand from the live tree, so no mutation can leave a stale
association and there is no hook to forget. Two distinctions carry the model —
`present` versus `usable` (`<form action>` is present and unusable) and `NONE`
versus `UNKNOWN` ("no attribute" versus "an attribute naming something we do not
recognise"), because both would submit as GET in a browser and that defaulting is
a *submission* rule this phase deliberately does not apply. Reading the wrong
element is a refusal rather than an empty view, and an unmodelled `type`
degrades without destroying anything: the element keeps its attributes, its name
and its place in the tree. No submission, HTTP, encodings, validation, file or
date/range/color inputs, user interaction, events, CSS, layout or accessibility
tree. See `docs/Forms.md`.

**No DOM behaviour** — `NamedNodeMap`, `getElementById`, `querySelector`,
`classList`, `dataset`, events and the rest of the DOM surface come later, and
so does node *creation* on a closed document (`createElement` /
`createTextNode`): finalization closes tree construction, and registering a
new node is a construction operation. What it deliberately does not do is lock
the tree — mutation, traversal and reads all stay available after it, which is
the state a parsed page is mutated in.
The tokenizer stays independent of the tree builder: it never constructs a
tree, never knows an element's meaning, and never touches
CSS/layout/paint/JS/network. The tree builder is the only consumer that turns
tokens into structure, and it does so purely through document-scoped node
registration.

## Layout

| Path | Role |
|---|---|
| `runtime/Include/RoboHTML/` | Public API (no OS headers, no Chromium) |
| `runtime/Source/RoboHTML/` | Platform-independent implementation |
| `runtime/html/tests/` | Suites (`html.*`, acceptance `html.p8.*`) |
| `runtime/html/docs/` | Design docs |

Design docs: `Architecture.md`, `TreeBuilder.md`, `DocumentModel.md`,
`ElementModel.md`, `AttributeModel.md`, `TextModel.md`, `Traversal.md`,
`Mutation.md`, `Lifecycle.md`, `Elements.md`, `Images.md`, `Links.md`,
`Forms.md`, `Tests.md`.

`Tests.md` is the map of the suite: which suite owns which surface, what the
cross-cutting `html.dom.*` tests pin and why they exist separately, the
parsed-versus-hand-built fixture rule, how to run a focused suite, and what the
sanitizer builds found.

## Public headers

| Header | Boundary |
|---|---|
| `RoboHTML.h` | Module init/shutdown/version |
| `RoboHTMLTypes.h` | Document/node identity tokens, node kinds, diagnostic domains |
| `RoboHTMLDiagnostics.h` | `robo_html_diag_t` record (optional, caller-owned) |
| `RoboHTMLLifecycle.h` | Document lifecycle state machine |
| `RoboHTMLDocument.h` | Document create / attach / register / metadata / finalize / reset / ownership / validate / destroy |
| `RoboHTMLNode.h` | Node handles, generation-guarded registry |
| `RoboHTMLDom.h` | Future-DOM boundary, origins, mutation hooks, capabilities |
| `RoboHTMLElement.h` | Element model: creation seam, view/state, namespace, classification, name policy, and the attribute model (lookup, presence, iteration, add/set/remove) |
| `RoboHTMLText.h` | Text model: creation seam, view/content/length/parent, empty-text policy, and set/append mutation |
| `RoboHTMLTraversal.h` | Traversal: relationships, depth/root, the child cursor, the all/elements/text filters, pre-order and descendant walkers, bounded search |
| `RoboHTMLMutation.h` | DOM mutation: append/insert-before/remove/replace, bulk child removal, `contains`, the tree version |
| `RoboHTMLTag.h` | Element vocabulary: tag identity, name/class/flags lookup, intrinsic predicates |
| `RoboHTMLImage.h` | Image resources: attribute view, dimension parsing, resource states, the fetch-resolver seam, the in-memory asset resolver, per-document attachment |
| `RoboHTMLAnchor.h` | Anchors and navigation: attribute view, reference resolution, target kinds, scheme policy, fragment lookup, target context, `rel` tokens, the navigation request/handler/outcome, activation |
| `RoboHTMLForm.h` | Forms: form/label/input/button/textarea/select/option attribute views, the input and button type vocabularies with their degradation, label→control and control→form resolution, select option ordering and selection, and control state (declared vs current value and checkedness) |
| `RoboHTMLSource.h` | Source/input abstraction (borrowing view) |
| `RoboHTMLEncoding.h` | Encoding boundary (labels + state; no decoding yet) |
| `RoboHTMLParser.h` | Parser lease between source and future tokenizer |
| `RoboHTMLToken.h` | Token record: kinds, fields, positions, borrow contract |
| `RoboHTMLTokenizer.h` | Tokenizer create/next/reset/state/diagnostics API |
| `RoboHTMLTokenizerStates.h` | Explicit state enumerators + raw-content switch contract |
| `RoboHTMLTreeBuilder.h` | Tree builder: modes, open-element stack, build/finish, tree dump |
| `RoboHTMLResource.h` | Delegated acquisition slot (no I/O in RoboHTML) |
| `RoboHTMLClient.h` | CSS/layout/paint/render integration hooks |
| `RoboHTMLVersion.h` | Version pin |

## Tests

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "html."
```

| Filter | Coverage |
|---|---|
| `html.` (8.1) | Module, document, node identity, source, parser, hooks, acceptance |
| `html.token.*` | Tokens, tags, attributes, comments, doctype, positions, misuse, lifetime, malformed-corpus robustness |
| `html.charref.*` | Named / numeric / hex references, legacy rules, error mapping |
| `html.tokstate.*` | State model and the raw-content switch entry points |
| `html.tree.*` | Tree structure, implied elements, nesting, text merging, attributes, void elements, lifetimes, recovery, node ceiling, Unicode, dump, demo document |
| `html.docmodel.*` | Metadata, root ownership and stability across rebuilds, node identity/lookup, staleness, cross-document separation, tree invariants (clean, orphan, deep), source/encoding association, finalization, reset, failure, teardown, pipeline and demo integration |
| `html.element.*` | Namespaces, classification, creation (valid, refused, cross-document), identity/validity, tag names, state bits, relationships, nesting/siblings, ownership, attributes, start/end tags, malformed recovery, Unicode text, teardown, tree dump, demo document |
| `html.attr.*` | Attribute type and lengths, parser integration and stored order, presence vs empty, duplicates, lookup/case/Unicode, the mutation verbs and their validation, the attribute limit, Unicode names and values, malformed input, the dump, ownership/lifetime, rebuild semantics |
| `html.text.*` | Text creation (basic/empty/validation), kind identification and the not-text answers, `set`/`append` including the slow path, mutation after finalization, parser integration, runs and merging, whitespace, character references, Unicode, long runs, traversal, ownership/stale handles, teardown, malformed input, the dump, capability flag, demo document |
| `html.traverse.*` | Relationship accessors and the miss contract, parent/root/depth, exact document order and its determinism, pre-order and descendant walks, the element/text filters, the child cursor, search (`find_first` vs `find_descendant`), parser-built trees with attributes intact, Unicode, a 1000-level tree, an empty document, defensive input, visitor control including destroy-from-visitor, the dump on the walker, capabilities and diagnostics, demo document |
| `html.mutate.*` | Append/insert-before/remove/replace positional cases, bulk child removal, parent/first/last/sibling/order integrity (every mutating case ends in a whole-tree audit), duplicate ownership, self-insertion and ancestor cycles, cross-document refusal, detached-subtree behaviour, the tree version, parser-created (finalized) trees, the creation boundary, attribute and Unicode preservation, the mutation hook, traversal invalidation, teardown/reset/destroy |
| `html.lifecycle.*` | Initial state, the valid transition graph, every invalid transition refused without partial state, the restrictive edges (no `LOADING → COMPLETE`, no `CREATED → COMPLETE`), repeated transitions and destruction, parser integration, malformed source, mutation and traversal in every state, ownership/root/sibling integrity across a transition |
| `html.tag.*` | Table density and row order, name lookup and round-trip, role/flag agreement, every predicate against its flag, parser identity, custom and out-of-range tags, non-element handles |
| `html.elements.*` | All 32 basic element types through the parser, the creation seam, void behaviour, attributes, text and whitespace, Unicode (Arabic/Persian/Urdu/Hindi/Bengali/CJK/emoji), traversal, mutation, lifecycle, 23 malformed inputs, the dump |
| `html.image.*` | `<img>` through the parser (identity, four attributes, void, siblings), dimension parsing, alt verbatim including Unicode, decode through the asset resolver, failure and scheme refusal as distinct states, re-resolve on a mutated `src`, detach, teardown, mutation and traversal, debug describe |
| `html.anchor.*` | `<a>` through the parser, the attribute view (present vs usable), the resolution matrix with and without a base, fragments, external URIs, scheme knowledge vs permission, target contexts, `rel` tokens, activation for every outcome kind including the refusals, request contents, no fake success, structure, mutation, lifecycle and destroyed-document safety, Unicode link text, debug describe, capability flag |
| `html.form.*` | Forms: the elements through the parser, the form and control attribute views (present vs usable, `method`/`enctype` classification), the input type vocabulary and its degradation, value and checkedness state (declared vs current, overrides set/cleared, independence), numeric constraints parsed not compared, button, textarea, select/option ordering, the option value fallback, label→control, form ownership by attribute and by ancestry, refusals for non-form elements, malformed input, mutation including option restructuring, lifecycle including a control mutated after the document settled, a destroyed document's state, structure, Unicode |
| `html.dom.*` | Cross-cutting DOM invariants (8.15): document isolation (identical bytes, two documents, every cross-document operation refused), the three `id` consumers (fragment lookup, a label's `for`, a control's `form`) agreeing and moving together under an `id` rename, void as a *parser* rule versus a node-kind rule in the mutation API, malformed input still auditing clean and rebuilding deterministically, attribute mutation sequences with the dependent state (anchor, image, declared/current value) checked at every step, 200-level and 300-sibling structure, a thirteen-family Unicode matrix through both the parser and the construction seam, allocator-balanced document churn over 24 lifetimes, and one document exercising every subsystem at once |
| `demo.field.*` | The form checkpoint: every control in the shipped document reporting the state the model holds, a caption drawing no box while a field draws a white bordered one, a placeholder drawn muted versus a value drawn in the text colour, a checked box drawing a brand mark, a textarea taller than a single-line control, a select's chevron, state re-read from the DOM after mutation (including the attribute-tracking override rule), and an unmodelled `type` degrading to `text` |
| `demo.link.*` | The link checkpoint end to end: the built-in document's two anchors as real DOM elements, hit-testing through block rects, a route refusing to navigate with no handler, a fragment resolving to the `id` it names, link paint colour and underline versus body copy, activation after the document is destroyed, and `href` mutation reaching the picture |
| `demo.mutation.*` | A DOM mutation reaching the picture: the same document rendered before and after, with the block order and the painted output changing |
| `demo.paint.*` | The visual checkpoint: element kinds, the lifecycle not changing what is rendered, and a decoded PNG reaching the surface |

Two ownership rules in one line each:

- A returned `robo_html_token_t` **borrows** tokenizer storage and is valid
  until the next `next()`/`reset()`/destroy;
  `robo_html_token_copy_tag_name` / `robo_html_token_copy_attr` produce
  caller-owned copies that survive both.
- The document owns every node and every byte of payload. Node handles are
  inert; `robo_html_node_*` / `robo_html_element_*` / `robo_html_payload_*`
  return pointers **into** the document's payload arena, valid until the next
  build on that document (the arena grows) or until it is destroyed.
- An element is an element-kind node, and `robo_html_element_create` is the
  only way one is made — it always attaches the new element under a parent in
  the same call, so there is no detached state. A handle is identity, not a
  capability: it names a node, it does not grant access to one.
- A text node is likewise just a node, created only through
  `robo_html_text_create`. An empty run is a legal value, but the parser never
  produces one: a zero-length character token carries nothing, so `<p></p>`
  has no children rather than an empty text child.
- A node has at most one parent. Mutation never steals a node from its current
  parent: moving one is `remove_child` then `append_child`, two explicit steps.
  A removed node is **detached, not destroyed** — it keeps its subtree and
  stays owned by its document — and a structural change invalidates any
  traversal that was in flight, reported as a MUTATION-domain
  `ROBO_ERROR` rather than a wrong answer.
- The document's **root** survives a rebuild; every other node handle from a
  previous tree becomes stale. Ownership is asked for, never assumed:
  `robo_html_document_owns_node` is the single check that covers "whose node
  is this?" and "is it still live?".
