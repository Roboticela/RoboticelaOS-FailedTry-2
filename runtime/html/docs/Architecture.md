# RoboHTML Architecture (Phases 8.1–8.9)

This document is the normative design record for Goals 8.1 and 8.2. Later
phases (8.3 tree builder, DOM, CSS, layout, paint) must extend it, not
contradict it. Section 9 covers the tokenizer.

## 1. Module boundary and allowed dependencies

```text
RoboHTML
    depends on →  RoboBase, RoboMem, RoboContainers (headers only today)
    consumed by → RoboCSS / RoboLayout / RoboPaint / RoboRender (future)
    parallel to → RoboDOM (future module; boundary stubbed in RoboHTMLDom.h)
```

- RoboHTML is a **lower module**: no includes of graphics/window/display or
  any future CSS/layout/paint module. Enforced by
  `Assert-RoboHTMLChromiumIndependence` in `runtime/Tools/Build.ps1`.
- Chromium is not a node in this graph (`CHROMIUM_REFERENCE.md`).

## 2. Identity model

- `robo_html_document_id_t` / `robo_html_node_id_t` are runtime-owned
  `uint64_t` tokens; `0` is always invalid ("none").
- Document ids increase monotonically and are **never reused**.
- Node handles carry `(document_id, node_id, generation)`. The generation
  guards the document's registry slots: after teardown, an application-held
  handle resolves as **stale** (`ROBO_NOT_FOUND` +
  `robo_html_node_handle_is_stale`), never as a newer node.

## 3. Ownership and lifetime

| Object | Owner | Contract |
|---|---|---|
| Document | RoboHTML module (fixed slot table) | Created via `robo_html_document_create`; destroyed via `..._destroy` or module shutdown |
| Nodes | Owning document | Registered via `robo_html_document_register_node`; retired wholesale at document teardown |
| Parser | Caller (heap object) | Leased exclusively to one document; never outlives it — document destroy detaches the lease |
| Source bytes | Application | Borrowed view only; RoboHTML copies metadata, never bytes |
| `robo_html_diag_t` | Caller | Optional; result codes are authoritative |

## 4. Lifecycle state machine

```text
UNINITIALIZED → CREATED → LOADING → READY → DESTROYING → DESTROYED
                    │          │    │
                    │          │    └──→ LOADING (replacement source)
                    │          └──→ FAILED (sticky)
                    └──→ FAILED / DESTROYING
```

- `CREATED` = empty, valid document (root node registered).
- Empty source attach completes immediately (`LOADING → READY`);
  non-empty sources stay `LOADING` until parsing (8.2/8.3) marks ready.
- `FAILED` is sticky until destroy; destroy is the only exit.
- Invalid transitions return `ROBO_INVALID_ARGUMENT`; `DESTROYED → DESTROYED`
  is the only idempotent edge.
- Content operations require `CREATED/LOADING/READY`; source attach requires
  `CREATED/READY`.

## 5. Source / encoding / parser pipeline

```text
application bytes ──▶ robo_html_source_t (borrow)
                        │ attach (document or parser path)
                        ▼
        document metadata (kind/length/name copied)
                        │
        encoding boundary (declared label → state; decode later)
                        │
        parser lease (FEEDING; COMPLETE vs INCREMENTAL mode)
                        │
        future tokenizer (8.2) → tree builder (8.3) → node registry
```

- One writer at a time: while a parser lease holds the source, direct
  document attachment is refused (`BUSY`); while `LOADING`, attach is
  refused (`BUSY`).
- `INCREMENTAL`/`STREAM` sources select incremental parser mode for 8.2+.

## 6. Integration hooks

- **Mutation hooks** (`RoboHTMLDom.h`): single document-wide hook, fires on
  node registration (lifecycle events modeled). Foundation only.
- **Client hooks** (`RoboHTMLClient.h`): `on_document_lifecycle` and
  `on_invalidate` are the *only* surface future CSS/layout/paint/RoboRender
  layers use. RoboHTML never pulls graphics/window headers.
- **Resource boundary** (`RoboHTMLResource.h`): a future loader (RoboNetwork
  or application) delegates acquisition via `robo_html_resource_begin/end/
  cancel`; direct attach is refused while acquisition is outstanding. No
  HTTP/DNS/TLS/fetch code exists in RoboHTML.

## 7. Diagnostics

`robo_result_t` stays authoritative; `robo_html_diag_t` refines failures
with a `robo_html_diagnostic_domain_t` (document / lifecycle / node /
source / encoding / parser / resource / client / mutation / render) and a
message. No global error state, no browser-style legacy error behavior.

## 8. What Phase 8.1 deliberately does not do

No tokenizer states, no character references, no parsing algorithms, no
tree construction, no element/attribute storage, no CSS, no layout, no
paint, no JavaScript, no networking, no storage, no cookies/CORS, no
IndexedDB/localStorage, no browser-origin security machinery.

## 9. Tokenizer (Phase 8.2)

```text
robo_html_source_t (borrowed bytes)
        │
        ▼
robo_html_tokenizer_next()          state machine (RoboHTMLTokenizerStates.h)
        │  ── DATA / tag / attribute / comment / doctype / reference states
        │  ── raw content families via switch_state (8.3 drives this)
        ▼
robo_html_token_t   CHARACTER | START_TAG | END_TAG | COMMENT | DOCTYPE | EOF
        │
        ▼
tree builder (8.3) → document node registry (see §11)
```

### Token model and ownership

- Six token kinds; each carries `robo_html_token_pos_t` (byte offset, 1-based
  line, 1-based column in code points) for the token's first source byte.
- A token **borrows** tokenizer-owned storage (tag name, attribute array,
  decoded characters, comment payload, doctype fields) and is valid only
  until the next `next()` / `reset()` / tokenizer destroy. The attribute
  array and the character buffer are tokenizer-owned so no pool internals
  ever escape.
- Callers that must keep data use `robo_html_token_copy_tag_name` or
  `robo_html_token_copy_attr`; those copies are caller-owned and outlive the
  tokenizer.
- All token text is UTF-8. Ill-formed UTF-8 is emitted as U+FFFD (RoboUnicode
  REPLACE mode) with a diagnostic — never read out of bounds, never fatal.

### Error tolerance

Only NULL arguments, invalid state, and allocation failure return errors.
Malformed HTML recovers and continues: unterminated tags at EOF emit the EOF
token (with the partial construct dropped and a diagnostic), unclosed
comments emit the partial comment, `<?` and `<![CDATA[` become bogus comments,
and diagnostics are bounded per tokenizer
(`ROBO_HTML_TOKENIZER_MAX_DIAGNOSTICS`).

### Character references

Named references are matched with the HTML longest-match rule against the
catalogue in `RoboHTMLCharRefData.c`; the catalogue is a curated,
category-grouped set whose order is **not** significant, so the engine picks
the winner by name length (`&amp;` beats `&amp` regardless of table position).
Numeric references follow the standard value mapping: 0x00 / out-of-range /
surrogates → U+FFFD, 0x80–0x9F → the legacy remap table, and C0 controls are
parse errors that keep their code point. Inside attribute values the legacy
semicolonless rule applies (`&ampx=1` stays literal).

### Raw content families

The tokenizer is data-driven for `DATA`, but `RAWTEXT`, `RCDATA`, script data
and `PLAINTEXT` are entered only through
`robo_html_tokenizer_switch_state()`, validated to those five entry points.
The expected end-tag name is seeded from the last emitted start tag (the
`<textarea> … </textarea>` shape 8.3 will use) or learned from a leading
start tag; `RCDATA` decodes references, the others stay byte-literal, and
only the matching end tag closes the region.

## 10. What the tokenizer deliberately does not do

The tokenizer constructs nothing: no tree, no nodes, no element semantics (an
end tag does not close anything), no CSS, layout, paint, JavaScript,
networking, filesystem, storage, navigation, cookies or CORS. It also does not
accept incremental/chunked input yet — the machine is written so a source
feeding in chunks plugs in without redesign. Turning tokens into structure is
the tree builder's job, and it consumes this stream without the tokenizer
knowing anything about documents.

## 11. Phase 8.3 — tree builder

The tree builder consumes the token stream and constructs the document tree
that 8.1 reserved room for:

```
source (8.1) → tokenizer (8.2) → tree builder (8.3) → document tree
                                                       ├── nodes: Document,
                                                       │   DocumentType,
                                                       │   Element, Text,
                                                       │   Comment
                                                       ├── parent/child/sibling
                                                       │   links as node ids
                                                       └── payload arena:
                                                           names, attributes,
                                                           text, identifiers
```

- **Ownership is unchanged from 8.1:** the document owns every node and every
  byte of payload. The builder holds ids, never registry pointers, and only
  ever creates nodes through `robo_html_document_register_node` — there is no
  second ownership model.
- **Modes and the open-element stack** implement the foundational insertion
  modes (`initial` … `in-body`, `after-body`, `after-after-body`, plus a text
  mode for RCDATA/RAWTEXT/script/plaintext), so later goals extend the machine
  instead of replacing it.
- **Implied structure:** a missing `<html>`, `<head>` or `<body>` is created,
  which is why an empty document is not an empty tree.
- **Recovery is bounded and non-fatal:** diagnostics for everything
  recoverable, a hard stop only at the node ceiling (which truncates and still
  reports success), and a `ROBO_OK`-with-`truncated` contract callers can
  detect.
- **Mutation retires identities safely:** re-building a document clears the
  tree but moves the generation cursor forward, so a stale handle from the
  previous tree can never alias a new node (see §12 for how ids and
  generations divide that job).

Full design, limits and the assembled/deliberately-absent rule list:
[`TreeBuilder.md`](TreeBuilder.md).

## 12. Phase 8.4 — document model

8.3 produced a tree; 8.4 makes the document the authoritative model *of* that
tree. No building behaviour changes — what changes is what the document
promises about what it owns, and what a caller can verify.

```
Document (8.4)
├── identity + state        metadata snapshot (lifecycle, previous, counts…)
├── root                    explicit, owned, survives re-parsing
├── node registry           dense ids + generations; ownership queries
├── construction closure    finalize() closes register/link; reset/attach reopen
├── source / encoding       association lifetime made explicit
└── invariant auditing      validate_tree() reports, never trusts
```

- **The generation, not the id, is the guard.** Node ids restart dense when a
  tree is cleared, which is what keeps `id == slot + 1` true and node lookup
  linear across re-parses; a moving generation cursor is what keeps stale
  handles detectable. Consequently the **root survives a rebuild** — same id,
  same generation — while every other node from the old tree goes stale.
- **Ownership is asked for, never assumed.** `robo_html_document_owns_node`
  covers "whose node is this?" and "is it still live?" in one call, so a
  cross-document handle is a plain "no" rather than an aliased lookup.
- **Construction is explicitly opened and closed.** A completed build closes
  it (`finalize`); `reset`, a tree clear and a new source reopen it. Closing
  construction is deliberately not a lock against the future DOM, which
  mutates a tree rather than constructing one.
- **Invariants are auditable on demand** — `robo_html_document_validate_tree`
  walks the tree iteratively and reports orphans, cycles, bad links, bad
  parent/sibling links and count mismatches as independent observations. An
  inconsistent tree is a reported verdict, not an error return.

Full design, the three defects this phase fixed, and the exact contracts:
[`DocumentModel.md`](DocumentModel.md).

## 13. Phase 8.5 — element model

8.5 makes the element a first-class part of the model **without** making it a
second ownership system: an element *is* a node, so it inherits identity,
ownership, staleness detection and teardown rather than reimplementing them.

The only structural addition is one namespace word in the element payload
header; everything else is derived at read time.

```
Element (8.5)
├── identity          the node handle (document id, node id, generation)
├── namespace         HTML / SVG / MathML + URI (stored: one word per element)
├── classification    derived from (namespace, name); stored nowhere
├── state             parser-created / connected / self-closing; derived
├── attributes        the 8.6 boundary: count, index, lookup, presence
└── creation          robo_html_element_create — the one seam
```

- **One seam.** `robo_html_element_create` is where an element comes into
  existence, so lifecycle, cross-document rejection and the name rule live in
  one function instead of being a convention every caller has to honour. It
  always attaches the new element under a parent, so "an element with no
  document" cannot be expressed.
- **Derived, not cached.** Classification, `CONNECTED` and `SELF_CLOSING` are
  computed from identity, tree links and payload flags. Nothing has to be kept
  in step by a future mutation path, and a parent-chain walk that would go
  stale cannot exist.
- **Namespace is per element, the URI is per namespace.** Storage stays one
  word per element; the future DOM/CSS layers compare against constants.
- **The attribute boundary was a read boundary.** Count, index, lookup and
  presence only — 8.6 replaced it with the real model.

Full design and the defect this phase fixed:
[`ElementModel.md`](ElementModel.md).

## 14. Phase 8.6 — attribute model

8.6 replaces the temporary attribute table with the authoritative model, and
the organising idea is that there is exactly **one** attribute type:

```
tokenizer staging            robo_html_attribute_t   (borrows tokenizer buffers)
tree builder / element       robo_html_attribute_t   (borrows the payload arena)
payload bytes                the same fields, encoded
element API                  the same fields, borrowed back to the caller
```

Only the lifetime differs from stage to stage, and each stage states it. No
separate parser-only record survives, so nothing has to be translated between
the parser and the tree, and the tokenizer's duplicates and source order reach
storage unchanged.

```
Element (8.5/8.6)
├── identity          the node handle (unchanged)
├── namespace         HTML / SVG / MathML (unchanged)
├── classification    derived from (namespace, name) (unchanged)
├── state             parser-created / connected / self-closing (unchanged)
├── attributes        the model: order, duplicates, case, presence,
│                     iteration, and add / set / remove
└── creation          robo_html_element_create — the one seam
```

- **Presence is a record, not a value.** `<input>`, `<input disabled>` and
  `<input disabled="">` are three distinguishable answers, which is the whole
  reason the lookup returns `NULL` (absent) separately from `""` (present and
  empty).
- **Order and duplicates are passed through.** The tokenizer does not fold and
  the model does not invent: lookup answers the first match, the rest stay
  reachable by index, and only `add` can introduce a duplicate.
- **Case is a storage decision, not a lookup rule.** Names are stored
  lowercase; lookup is ASCII case-insensitive; bytes outside `A-Z` are never
  folded.
- **The parser tolerates, the DOM seam validates.** A malformed name must not
  abort a parse, so the parser stores what it was given; an application-supplied
  name is validated and normalised.
- **A mutation is a payload rewrite**, staged in zeroed scratch memory and
  measured from the old table so it is exact, bounded, and cannot dangle
  borrowed bytes. Node identity, children and the element's place in the tree
  are untouched — so attribute mutation survives finalization, which closes
  tree construction rather than the document.

Full design, contracts and the defect this phase fixed:
[`AttributeModel.md`](AttributeModel.md).

## 15. Phase 8.7 — text model

8.7 completes the node kinds the tree builder can produce by giving character
data a real model. The organising idea is the one 8.5 established for
elements: **a text node is a node**, so it inherits identity, ownership,
staleness detection, sibling links and teardown instead of reimplementing
them. Nothing new is stored per node.

```
Text (8.7)
├── identity          the node handle (unchanged)
├── content           the payload arena byte run (unchanged layout)
├── state             parser-created / connected; derived
├── parent            the tree link (siblings stay in RoboHTMLNode.h)
└── creation          robo_html_text_create — the one seam
```

- **One seam.** `robo_html_text_create` is where a text node comes into
  existence, so lifecycle, cross-document rejection and the origin rule live
  in one function. The tree builder now fills a descriptor and calls it
  rather than registering, encoding and linking by hand — the same shape
  `robo_html_element_create` already had.
- **`NULL` is not `""`.** `robo_html_text_content` returns `NULL` for "not a
  text node" and a non-NULL empty string for "a text node whose run is
  empty". `robo_html_text_is_empty` is a separate question from "is this
  text at all".
- **A stale handle is a miss, not misuse.** A well-formed handle naming
  nothing live resolves as `ROBO_NOT_FOUND`; only `NULL` arguments are
  argument errors. Every accessor lets a caller test before trusting.
- **`set` writes a fresh payload, `append` has two implementations and one
  result.** Neither touches node identity, parent, siblings or child order, so
  character data stays mutable after finalization — 8.4 closed tree
  *construction*, not the document.
- **A run coalesces per run, not per token.** The tokenizer's segmentation is
  not observable in the tree: inside an open run `robo_html_text_append`
  always succeeds, so `<p>Hello <b>world</b>!</p>` is three children rather
  than one node per token.
- **Bytes are preserved exactly.** No normalisation, trimming, collapsing,
  resampling or re-encoding — whitespace and Unicode survive byte-for-byte
  because every later consumer (layout, shaping, bidi, JavaScript) reads
these bytes, and a value destroyed here is unrecoverable.
- **Empty text is permitted by the model and never produced by the parser.**
  A zero-length run is legal (the future DOM's `createTextNode("")` must be
  expressible); a zero-length character token carries nothing, so no node is
  registered for it.

Full design, contracts and the test coverage:
[`TextModel.md`](TextModel.md).

## 16. Phase 8.8 — traversal

8.8 makes the tree navigable as a *boundary* rather than as a set of
accessors. The same pre-order successor had grown in three places (element
search, subtree text, and the debug dump — recursively), child iteration by
index was O(n²), and the recursive dump was a stack-overflow vector on a
tree the node ceiling permits. Those are now one implementation the consumers
share.

```
RoboHTMLTraversal.h (8.8)
├── relationships   parent / first_child / last_child / next / prev
├── depth & root    absolute depth from the document node; rootmost ancestor
├── child cursor    one-pass O(n) iteration
├── filters         all · elements · text
├── walkers         pre-order / descendants, depth-aware, visitor-driven
└── search          find_first / find_descendant
```

- **Handles, not pointers.** Every accessor takes and returns a node handle,
  so no caller can see the registry, the sibling links, the payload arena or
  any container layout. Nothing borrowed can outlive its call: a visitor
  receives a view borrowed for that call, and the child cursor holds two
  integers.
- **"Nothing there" is an answer.** A missing link is `ROBO_NOT_FOUND` with a
  cleared output, mirroring `child_at`'s out-of-range contract. `NULL`
  arguments are `ROBO_INVALID_ARGUMENT`; a handle that does not resolve is
  `ROBO_NOT_FOUND`. A link target is looked up rather than trusted, so a
  corrupted or retired link reports a miss instead of aliasing a new node.
- **A filter selects what is visited, never what is stepped through.** A walk
  for elements still descends past text nodes, so every filtered walk is a
  subsequence of the same single document order — which is what makes the
  ordering claim checkable.
- **Iterative, bounded, allocation-free.** No recursion (an adversarial depth
  cannot exhaust the stack), no allocation (a walk cannot fail halfway for
  memory reasons), O(n), and a step bound of the document's live node count
  so a cycle is reported as `ROBO_ERROR` instead of silently truncating.
- **The visitor's code is the walk's result**, so early exit needs no cancel
  channel; `find_first` is built on exactly that. A visitor may walk
  reentrantly; destroying the document mid-walk is detected, not
  dereferenced.
- **Depth is absolute and free.** The walk already knows whether it descended
  or ascended, so a visitor indents, buckets or bounds its work without
  walking the parent chain.
- **Read-only by construction.** `robo_html_dom_query_capabilities` reports
  `traversal = 1`; `events` stays `0` because this phase adds no event model.
  (`mutation` was `0` here in 8.8 and became `1` in 8.9 — which is why the
  cursor and the walkers snapshot the document's tree version: they were
  written against a tree that only now can change under them.)

The dump is a visitor now, so indentation costs no recursion, and the depth
cut is a dump policy rather than a traversal limit.

Full design, contracts and the test coverage:
[`Traversal.md`](Traversal.md).

## 17. Phase 8.9 — DOM mutation

8.9 makes the tree editable. The only child link in the runtime belonged to
 the parser and refused once a build finished, so the document model was a
read-only tree with an owner. Mutation adds five operations and, more
importantly, one place where a child link is written — the parser's link is
now a wrapper over it, so a parse and a DOM edit cannot disagree about what a
tree is.

```
RoboHTMLMutation.h (8.9)
├── append_child      child becomes the last child
├── insert_before     anchored to a sibling the parent actually lists
├── remove_child      detach, keeping the subtree
├── replace_child     hand the slot over in place
├── remove_children   bulk detach
└── queries           contains (the cycle predicate) · tree_version
```

- **One parent.** Inserting an attached node is `ROBO_ALREADY_EXISTS`: the API
  never silently steals a node. Moving is `remove` then `insert` — two
  explicit steps, both visible in the version history.
- **Always a tree.** A node beneath itself or beneath its own descendant is
  refused, and the document node can never become a child. The rule is a
  bounded parent-chain walk, so the invariant costs O(depth) and nothing
  downstream has to defend against a cycle.
- **Detachment is not destruction.** A removed node keeps its subtree,
  attributes and text, stays owned by its document, and can be re-inserted;
  its own subtree is traversable in the meantime. There is no node-destroy API
  because there is no state in which one is needed.
- **Construction closure is not a lock.** Mutation works on a finalized
  document — the state a parsed page is in. Node *creation* stays gated, so
  this phase rearranges what a document has; `html.mutate.creation_stays_gated`
  pins that boundary.
- **Cross-document is refused, not guessed.** A foreign handle is
  `ROBO_NOT_SUPPORTED` (adoption/import is a separate operation); the same
  document with a stale handle is `ROBO_NOT_FOUND`.
- **A refusal is inert.** Every rejection is decided before the first write,
  so a failed mutation leaves the tree — including its version — byte-for-byte
  as it was.
- **Structural change is observable.** A monotonic per-document tree version
  bumps on every insert, detach, replace and clear. Traversal snapshots it, so
  an iteration interrupted by a mutation stops with a MUTATION-domain
  `ROBO_ERROR` instead of returning a node that is no longer a child.
- **The whole-tree audit learned the difference.** An unreachable live node
  with no parent is now `detached` (legal, counted once at its root); one that
  *claims* a parent stays `orphans`, which is the shape a buggy mutation
  leaves behind.

Full design, contracts and the test coverage:
[`Mutation.md`](Mutation.md).
