# RoboHTML DOM Mutation (Phase 8.9)

Phase 8.3 built the tree, 8.4 gave it an owner, 8.5/8.6/8.7 modelled its
nodes, and 8.8 made it navigable. Until 8.9 it was also **closed**: the only
child link in the runtime belonged to the parser, and it refuses once a build
finishes. This phase makes the tree editable.

```text
RoboHTMLMutation.h (8.9)

  append_child  insert_before  remove_child  replace_child  remove_children
        │
        ▼
  robo_html_node_insert_relative   ← the one place a child link is made
        │
        ▼
  node registry (document-owned, fixed capacity, sibling links by id)
```

## 1. The surface

| Operation | Effect |
|---|---|
| `robo_html_node_append_child(parent, child)` | `child` becomes the last child |
| `robo_html_node_insert_before(parent, new, ref)` | `new` takes the position immediately before `ref`, which must be a child of `parent` |
| `robo_html_node_remove_child(parent, child)` | `child` is detached; `parent` must be the node that lists it |
| `robo_html_node_replace_child(parent, new, old)` | `new` takes `old`'s exact position; `old` is detached |
| `robo_html_node_remove_children(parent, &n)` | every child detached, `parent` left valid and childless |
| `robo_html_node_contains(a, b, &yes)` | is `b` inside `a`'s subtree? (reflexive) |
| `robo_html_node_tree_version(id, &v)` | the document's structural version |

Operands are `robo_html_node_handle_t` — identity, not access. Nothing here
exposes the registry, the sibling links, the payload arena or the child-count
field, so the storage behind a child list stays free to change.

Deliberately absent: `createElement` / `createTextNode` (node *creation* is
still the construction-gated path — see §5), `cloneNode`, and the DOM
`ChildNode`/`ParentNode` conveniences.

## 2. One linking primitive

`robo_html_node_insert_relative(document, parent, child, reference)` in
`RoboHTMLNode.c` is the only place a child link is written. The parser's
`robo_html_node_link_child` is a thin wrapper that adds one thing — the
construction gate — and delegates. So a parser-built tree and a DOM-built tree
cannot have different shapes for the same intent, and every structural rule
below has exactly one implementation.

The primitive returns **only after every rejection has been decided**: the
first write happens once nothing can fail. That is what makes a refused
mutation provably inert, and it is why the tests can assert that a failed call
did not move the tree version.

## 3. Invariants

**One parent.** A node has at most one parent. Inserting a node that is
already attached is `ROBO_ALREADY_EXISTS` — this API never silently steals a
node from its current parent. An ownership transfer is therefore two explicit
steps (`remove`, then `insert`), which is what makes it visible in the
caller's code and in the version history.

**Acyclic.** Inserting a node beneath itself or beneath one of its own
descendants is `ROBO_INVALID_ARGUMENT`. The check walks the parent chain from
the target parent (`robo_html_node_is_ancestor_of`, bounded by the node
ceiling), so the cost is one step per ancestor and the result is always a
tree. Without it, a consumer that terminates on the tree shape — traversal,
layout, paint, a query engine — would not be able to.

**The document node is never a child.** A document has exactly one root, and
8.4 establishes it. `ROBO_NOT_SUPPORTED`, so the root cannot be replaced,
moved or detached by any path here.

**Parent kind.** Only a document, element or fragment can hold children — one
predicate (`robo_html_node_kind_can_hold_children`) shared by the parser link,
the DOM API and the primitive.

**Ordering.** Stored order is the child order. `append` goes last,
`insert_before` takes the referenced position exactly (first before the first
child, middle, or immediately before the last — leaving the last child
unchanged), `remove` leaves the remaining order untouched, and `replace` hands
the slot over in place.

## 4. Detachment is not destruction

A removed node stays live in its document with its whole subtree, attributes
and text intact, and can be inserted again — the document owns every node it
ever registered, so there is no orphan to free and no lifetime to manage. A
detached subtree is a tree in its own right: `subtree_text`, `contains`,
`depth` (restarting at 0 from the detached root) and a walk all work on it.

There is no API to destroy a node, because there is no state in which one
needs destroying: the arena and the registry are released together when the
document is.

## 5. Mutation versus construction

`robo_html_document_finalize` closes *tree construction*. It does not lock the
tree: the 8.4 docs say so explicitly, and the 8.9 suite pins it by parsing a
document (which finalizes), asserting `allows_construction == 0`, and then
mutating it.

What finalization does still control is node **creation**: `register_node` and
the two creation seams refuse after it. So this phase rearranges the nodes a
document already has. Creating brand-new nodes on a closed document belongs
with the document-lifecycle work that owns the construction gate, and the
suite pins the boundary (`html.mutate.creation_stays_gated`) so the seam is a
decision rather than an accident.

## 6. Cross-document mutation

Every operand must belong to the same document. A handle naming a *different*
document is `ROBO_NOT_SUPPORTED` — adopting or importing a node into another
document is a separate operation with its own rules, and this phase does not
invent a partial version of it. A handle that belongs to the right document
but no longer resolves (stale generation, unknown id, dropped tree) is
`ROBO_NOT_FOUND`, the same answer every other accessor gives.

The check order matters: the handle's claimed document id is compared *before*
the registry is consulted, so a handle from another document is reported as
the ownership mistake it is rather than aliasing a live node with the same id
in this one.

## 7. Errors, and which domain they land in

| Condition | Code |
|---|---|
| NULL argument, ill-formed handle | `ROBO_INVALID_ARGUMENT` |
| node is its own child, or an ancestor of the target parent | `ROBO_INVALID_ARGUMENT` |
| operand does not resolve (stale / unknown / dropped) | `ROBO_NOT_FOUND` |
| reference or removed child is not a child of the given parent | `ROBO_NOT_FOUND` |
| operand belongs to another document | `ROBO_NOT_SUPPORTED` |
| the document node as a child; parent kind cannot hold children | `ROBO_NOT_SUPPORTED` |
| node already has a parent | `ROBO_ALREADY_EXISTS` |

All of these carry the `ROBO_HTML_DOMAIN_MUTATION` diagnostic domain. The
linking primitive reports the same rules in the node domain (it serves the
parser too), so the public API applies its own taxonomy first
(`check_attachment`) and the primitive stays the backstop rather than the
messenger. The rules themselves are enforced in one place either way.

## 8. Structural change is observable

Every successful mutation bumps the document's **tree version**, a monotonic
`uint32_t` that is never reset to 0 (0 means "no document"):

- the shared linking primitive bumps on insert;
- `robo_html_node_unlink` bumps on detach, so `replace` (detach + insert) and
  `remove_children` (n per detach) each move the version, and any future
  internal unlink cannot forget to;
- `clear_tree` bumps, because dropping every node is a structural change like
  any other.

A **refused** mutation does not move it, and neither does a no-op (clearing an
already-childless parent). So the version doubles as proof that a failed call
changed nothing, and as the thing a consumer watches to know its view of the
tree is stale.

## 9. Traversal interaction

An iteration cannot survive the child list it is reading being rewritten: the
cursor's stored next-node id would name a node that is no longer a child (it
was detached, or moved elsewhere) and it would report a child the parent does
not have.

The rule is therefore stated once and enforced: **a traversal may not be live
across a structural mutation.** Both the 8.8 child cursor
(`robo_html_child_cursor_t.version`) and the walkers snapshot the tree version
and stop with `ROBO_ERROR` carrying a **MUTATION-domain** diagnostic the
moment it moves. The answer is "restart the iteration", never a silently wrong
node. Mutation from inside a walk visitor is detected immediately, right where
it can only have come from.

Three details that make it usable rather than merely safe:

- mutation of a *different* document cannot affect an iteration, because the
  version is per-document;
- a **refused** mutation does not invalidate an iteration, so a caller may
  probe and recover mid-walk;
- a destroy or reset from inside a visitor is still `ROBO_NOT_FOUND` (the
  document is gone entirely), which is why the two cases have distinct codes
  and domains instead of one "iteration broken" answer.

A cursor is invalidated, not paused: after the report, `next` returns
`ROBO_NOT_FOUND`. A fresh cursor on the mutated tree is correct.

## 10. The whole-tree audit

`robo_html_document_validate_tree` gained its 8.9 refinement: a live node the
root walk cannot reach is now classified by whether it claims a parent.

- **no parent** → `detached`: legal (a detached subtree), counted once at its
  root, with its own links audited and its descendants marked so they are not
  miscounted. Does not make `ok` false.
- **claims a parent** → `orphans`: always wrong — the node believes it is in
  the tree while the tree cannot reach it. This is exactly the shape a buggy
  mutation leaves behind when it writes a parent link without putting the node
  in a child list, which is why the counter keeps its meaning and its place in
  `ok`.

Without the split, every document with a detached node would report an unclean
audit — a false alarm that would train readers to ignore the audit entirely.

## 11. Performance

No allocation anywhere in this phase: a mutation rewrites registry linksin place and is O(1) except for the ancestor check, which is O(depth). No
element registry, no index, no cache, no invalidation machinery beyond the
version counter. `remove_children` re-reads the parent's first child each time (each
detach repairs the list) and is bounded by the node ceiling.

## 12. Visual checkpoint

`robo_demo_page_create_from_document` (new in 8.9) builds the demo page from a
document that already exists, so the same document can be rendered before and
after a mutation with no re-parse. `demo.mutation.changes_picture` uses it:
parse `<h1>First</h1><p>Second</p>`, render, move the paragraph ahead of the
heading with the real mutation API, render the same document again, and assert
both the block order and that the painted output differs.

## Definition of done

- [x] Real append / insert-before / remove / replace, plus bulk child removal
- [x] One linking primitive shared with the parser; every rule in one place
- [x] One-parent invariant, cycle prevention, root immutability, parent-kind rule
- [x] Positional correctness for first / middle / last / only child
- [x] Detached nodes legal, re-insertable, traversable, still document-owned
- [x] Cross-document mutation refused; stale handles miss
- [x] A refused mutation is inert, including the tree version
- [x] Mutation on a finalized (parsed) document, pinned as a test
- [x] Traversal invalidated by structural change, with a MUTATION-domain report
- [x] Whole-tree audit distinguishes detached from orphan
- [x] Mutation hook event (`TREE_MUTATED`) through the 8.1 hook foundation
- [x] Capability reports `mutation = 1`
- [x] `html.mutate.*` suite (34 cases) doubling every structural assertion with
      a whole-tree audit
- [x] Unicode text and attributes preserved across moves
- [x] Teardown, reset and destroy safe after mutation
- [x] Visual checkpoint still renders; mutation drives a visible change
- [x] Full suite green (1237/1237, Debug and Release)
- [x] No Chromium dependency
- [x] Stop after 8.9
