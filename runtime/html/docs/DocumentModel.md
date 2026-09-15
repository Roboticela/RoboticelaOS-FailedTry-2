# RoboHTML document model (Phase 8.4)

Phase 8.1 declared the document the ownership root; Phase 8.3 built a real tree
under it. Phase 8.4 is the step that makes that ownership *usable and
checkable*: the document now answers who owns what, what state it is in,
whether its tree is internally consistent, and how it is closed and reused —
without a caller having to trust any of it.

Nothing in this phase changes how a tree is built. It changes what the
document promises about the tree it already owns.

## The rules, in one place

1. **The document owns the whole tree and every payload byte.** There is no
   second owner, no reference counting, and no detached-node lifetime. A node
   lives exactly as long as its document; destroying the document releases the
   payload arena and the registry in one step, so a half-built tree cannot
   leak.
2. **Node identity is `(document id, node id, generation)`.** A handle only
   resolves inside its own document.
3. **The root is explicit and belongs to the document.** It is the document
   node registered at creation, always a `DOCUMENT` kind with no parent, and
   it is never replaced by a rebuild.
4. **Construction is opened and closed explicitly.** `register_node` and the
   internal child-linking primitive work while the document is *not finalized*;
   `robo_html_document_finalize` closes construction. Reading, traversal and
   payload access are unaffected — and so will DOM mutation be, which is a
   different operation from tree construction.
5. **Invariants are verifiable on demand**, not enforced on every access:
   `robo_html_document_validate_tree` audits the whole tree and reports what it
   found instead of silently trusting it.
6. **Documents are reusable or disposable, deliberately**: `reset` drops the
   tree and keeps the identity, `destroy` retires the identity for good.

## Identity, and why the generation is the guard

Identity is generation-guarded. The important subtlety is *which* field does
the guarding, because ids are reused:

- **Document ids** are module-global, monotonic, and never reused. A destroyed
  document id stays dead.
- **Node ids** are dense and drawn from a per-document cursor that **restarts
  when the tree is cleared**. Node 1 is the root, node 2 the next
  registration, and so on, on every parse of that document.

Restarting ids is what keeps `id == registry slot + 1` true, and that
correspondence is what keeps registration and node lookup linear however many
times a document is re-parsed. If ids only ever grew, every access after the
first rebuild would degrade to a full registry scan — an O(n²) parse.

Safety comes from the **generation**, not the id: clearing a tree moves the
generation cursor forward, so node ids may repeat while no node identity ever
does. A handle copied out of a discarded tree therefore fails to resolve, which
is exactly what it must do.

Two consequences worth stating explicitly, because both are tested:

- `robo_html_node_handle_is_stale` compares the generation, not just the id. An
  id that resolves today can name a *different* node than the handle was made
  for, and staleness must agree with `robo_html_node_handle_resolve`.
- The **root survives a rebuild**. Clearing keeps the document node, with its
  id *and* its generation, so a holder's root handle stays valid across
  re-parses while every other node from the old tree goes stale. A long-lived
  holder (a window, a render tree, an application) may keep the root; it may
  not keep anything else.

## Ownership queries

| Call | Answers |
|---|---|
| `robo_html_document_owns_node(id, handle)` | Does this handle name a live node owned by this document? |
| `robo_html_document_node_by_id(id, node_id, &handle)` | Resolve a bare id into an inert handle, cleared on failure |
| `robo_html_node_handle_document(handle)` | Which document does this handle claim? |
| `robo_html_document_root(id)` | The tree root handle (invalid once destroyed) |
| `robo_html_document_allows_construction(id)` | May tree construction proceed right now? |

`owns_node` answers two questions in one call — does the handle claim this
document, and does the claimed identity still resolve — which is why a
cross-document handle is a plain "no" rather than a lookup that could alias a
same-numbered node in the other document. Cross-document insertion is
impossible by construction: the internal link primitive resolves both nodes
inside a single document id, so there is no way to express "child from another
document".

## Metadata

`robo_html_document_metadata` returns a snapshot: identity **and** the previous
lifecycle state, the finalization flag, empty/root health, per-kind node
counts, the source association (`kind`, byte length, borrowed name), the
encoding state, and the failure record. Borrowed strings point into the
document's own storage and follow the same rule as the node accessors.

Counting is a registry scan, so a snapshot is only as fresh as the call — which
is the right trade for a diagnostic view and the wrong thing to do per frame.

## Construction closure and reuse

| Operation | Effect on construction |
|---|---|
| `finalize` | Closes it (idempotent). Registering afterwards is `ROBO_ERROR` + `TREE` domain |
| tree builder `finish` | Finalizes — a completed build closes the tree |
| `reset` | Reopens it, drops the tree, keeps identity, clears source/encoding/failure |
| `clear_tree` (internal) | Reopens it and keeps the root |
| `attach_source` | Reopens it: a new source is about to be parsed |

`reset` is refused with `ROBO_BUSY` while a parser or tree builder holds the
document lease. That is not politeness: the builder holds tree invariants in
its own state (the open-element stack, the trailing text run), so dropping the
tree underneath it would leave it linking into freed ids.

Reset's resting state follows the 8.1 lifecycle machine rather than fighting
it: **CREATED** for a document that never loaded anything, **READY (empty)** for
one that had loaded. There is no edge back to CREATED, and failure stays sticky,
so a FAILED document rejects reset until it is destroyed.

## Invariant auditing

`robo_html_document_validate_tree` walks the tree from the root and reports
independent observations:

| Field | Meaning |
|---|---|
| `root_ok` | The root handle resolves, is a `DOCUMENT` node, and has no parent |
| `nodes` / `registered` | Reachable nodes vs live registry entries |
| `max_depth` | Deepest node depth below the root (the root is depth 0) |
| `orphans` | Live nodes the tree cannot reach |
| `bad_links` | Links to non-live ids |
| `cycles` | A node reached a second time |
| `bad_parents` | A child whose parent link is not the node listing it |
| `bad_counts` | `child_count` / `last_child` disagreeing with the sibling chain |
| `sibling_mismatches` | `prev_sibling` links out of order |
| `ok` | Every observation above is clean |

Design notes:

- An inconsistent tree is **not** an error return. The audit succeeded and the
  verdict is `out->ok`; only bad arguments, an unknown document or a scratch
  allocation failure produce a non-`ROBO_OK` code.
- The walk is iterative with a per-visit reachability bitmap, so it is linear
  and cannot exhaust the C stack on a 4096-deep chain; a corrupted link cycle
  terminates the walk with `cycles` set instead of hanging.
- It is safe on a half-built tree, and it allocates one bitmap bounded by
  `ROBO_HTML_MAX_NODES_PER_DOCUMENT` rather than trusting the caller's stack.
- Ordinary rendering never pays for it: it is a diagnostic call, deliberately
  outside the read paths.

Its practical value in this phase was immediate — it is how the reworked clear
path was proven to preserve the root while retiring everything else, and how
these tests detect an unreachable node that a plain tree walk would never
notice.

## Defects found and fixed

Three real defects surfaced while building the model, each fixed minimally
with a regression test:

1. **`is_stale` ignored the generation.** A handle whose node id was live but
   whose generation was not reported itself as *live*, disagreeing with
   `resolve`. Latent while ids were never reused; a correctness bug the moment
   they are.
2. **`robo_html_parser_create` refused a LOADING document.** LOADING is the
   state that means "a source is attached and awaiting parsing", so refusing
   the lease there made the state unusable — the documented attach-then-parse
   flow could not be expressed.
3. **`robo_html_tree_builder_create` refused a LOADING document** for the same
   reason. Exclusivity is the parser lease, not the lifecycle; the builder's
   `begin` already settles a LOADING document before attaching its own source.

## What this phase does not do

No element model, no attribute mutation, no DOM APIs, no CSS, no layout, no
paint, no JavaScript, no networking, no storage, no browser-origin machinery
(no cookies, CORS, sessions, `localStorage`, `IndexedDB`). No fragment parsing
and no detached nodes: the architecture leaves room for both, and neither is
implemented or needed yet.
