# RoboHTML

HTML document subsystem for Roboticela Runtime (**Phase 8.1 architecture +
Phase 8.2 tokenizer + Phase 8.3 tree builder + Phase 8.4 document model +
Phase 8.5 element model + Phase 8.6 attribute model + Phase 8.7 text model +
Phase 8.8 traversal + Phase 8.9 DOM mutation + Phase 8.10 document lifecycle**).

**Version:** 0.10.0 — `ROBO_HTML_API_FROZEN=0` (freezes in a later Phase 8 goal)

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
`Mutation.md`.

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
| `demo.mutation.*` | A DOM mutation reaching the picture: the same document rendered before and after, with the block order and the painted output changing |

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
