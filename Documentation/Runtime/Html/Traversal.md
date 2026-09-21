# RoboHTML Traversal (Phase 8.8)

The tree was already navigable before this phase — `child_at`, `next_sibling`,
`find_element` and `subtree_text` all existed in `RoboHTMLNode.h`. What 8.8
adds is the **authoritative traversal surface over every node kind**: one
header that answers "what is next", "what is above", "what is inside", "in
what order", and "where is the first match" without any caller knowing how
children are stored.

Three things made a phase worth spending on it rather than leaving the
accessors alone:

1. **The same walk existed three times.** Element search (8.3), subtree text
   (8.3) and the debug dump (8.3, recursively) each carried its own copy of
   the pre-order successor. Three copies of a successor loop are three places
   a cycle bound, a depth convention or an off-by-one can drift.
2. **Child iteration by index was quadratic.** `child_at(parent, i)` resolves
   child `0` and then follows `next_sibling` `i` times, so walking a child
   list by index costs O(n²).
3. **The dump recursed.** A tree 4096 nodes deep — which the node ceiling
   permits — is a stack-overflow vector aimed at the one consumer whose job is
   to describe a tree that may be hostile.

This header is not a second tree. It is a read-only projection of the links
`RoboHTMLNode.h` already owns.

```
RoboHTMLTraversal.h (8.8)
├── relationships     parent / first_child / last_child / next / prev
├── depth & root      absolute depth from the document node; rootmost ancestor
├── child cursor      one-pass O(n) iteration (child_at is O(index))
├── filters           all · elements · text
├── walkers           pre-order / descendants, depth-aware, visitor-driven
└── search            find_first / find_descendant with a predicate

Document (8.4)
  └── html (8.5)  element + attribute model (8.6)
        └── body
              ├── Text (8.7)   ← leaves are nodes like any other
              └── div
                    └── span
                          └── Text
```

## Handles, not pointers

Every accessor takes and returns `HtmlNodeHandle`. A handle is inert
identity: it names a node, it does not grant access to one, and a stale one
simply fails to resolve.

Two consequences are worth stating because they are the reason this API can be
a boundary at all:

- **No internal storage is exposed.** A caller cannot learn that children are
  stored as first-child/next-sibling links, cannot hold the registry, and
  cannot see the payload arena. That is what lets the storage change later
  without touching a caller.
- **Nothing borrowed can dangle.** There is no iterator object holding a
  reference and no callback receiving an owned one. A visitor gets a
  `HtmlNodeView` — the snapshot the node read side already returns —
  borrowed for the duration of its call, and the child cursor holds two
  integers.

## Relationships

| Call | Meaning | Missing link |
|---|---|---|
| `HtmlNodeParent` | parent of any node kind | `RoboNotFound` (the document node has none) |
| `HtmlNodeFirstChild` | first child in sibling order | `RoboNotFound` |
| `HtmlNodeLastChild` | last child | `RoboNotFound` |
| `HtmlNodeNextSibling` | the sibling after | `RoboNotFound` at the end |
| `HtmlNodePrevSibling` | the sibling before | `RoboNotFound` at the start |
| `HtmlNodeDepth` | depth from the document node (document = 0) | — |
| `HtmlNodeRoot` | rootmost ancestor; the document node | — |

Two rules hold across all of them.

**"Nothing there" is an answer, not an error.** A missing link returns
`RoboNotFound` *and clears the output*, mirroring `child_at`'s out-of-range
contract. A caller can never receive a zeroed handle as a success. `NULL`
arguments are `RoboInvalidArgument`; a handle that does not resolve is
`RoboNotFound`. Every accessor here clears `out` before it can fail, so a
caller that ignores the return code still cannot mistake a stale handle for a
fresh one.

**A link is looked up, not trusted.** The target id is resolved through the
registry rather than turned into a handle directly, so a corrupted link — or a
slot retired by a tree clear — reports a miss instead of naming whatever node
next occupies that id.

`depth` and `root` walk the parent chain with a step bound of the node
ceiling. A parent chain cannot legitimately be longer than the registry, so
exceeding it means the links form a cycle and the walk reports `RoboError`
rather than spinning. That bound is the only reason those two can *promise*
termination rather than hope for it — and it is the same number the walkers
use.

## The child cursor

```c
HtmlChildCursor cursor;
HtmlNodeHandle child;

if (HtmlChildCursorOpen(&cursor, &parent, NULL) == RoboOk) {
    while (HtmlChildCursorNext(&cursor, &child, NULL) == RoboOk) {
        ...
    }
}
```

The cursor carries the id of the next child instead of an index, so a full
pass is **O(n) with one registry lookup per child** where `child_at`-by-index
was O(n²) over the same list.

- **Opening a childless node succeeds** and produces an immediately exhausted
  cursor. An empty iteration is a normal result, so a caller needs no special
  case before its loop.
- **It holds no pointer and borrows nothing.** It may be copied, and a copy
  continues independently; there is nothing to free.
- **It does not pin its node.** A cursor whose document is destroyed reports
  `RoboNotFound` and re-initialises itself, so an abandoned cursor is inert
  rather than dangerous.
- **A sibling cycle runs out rather than looping.** The step budget is the
  registry size; tripping it is `RoboError`, because a corrupted child list
  is not an empty one.

## Filters

```c
typedef enum HtmlNodeFilter {
    RoboHtmlNodeFilterAll      = 0,
    RoboHtmlNodeFilterElements = 1,
    RoboHtmlNodeFilterText     = 2
} HtmlNodeFilter;
```

**A filter selects what is *visited*, never what is *stepped through*.** A
walk for elements still descends into the positions of text nodes, so a text
node between two elements cannot hide the second one:

```
<div>Hello <b>Roboti</b>cela</div>
  ALL       → div, Text("Hello "), b, Text("Roboti"), Text("cela")
  ELEMENTS  → div, b              (a subsequence of the ALL walk)
  TEXT      → Text("Hello "), Text("Roboti"), Text("cela")
```

Every filtered walk is therefore a subsequence of the same single traversal
order, which is what makes the ordering claim falsifiable rather than a
convention. `HtmlNodeMatchesFilter` exposes the rule so a caller
writing its own loop applies the same one instead of a second copy of it, and
`HtmlNodeFilterName` gives the filters printable names for
diagnostics.

## Walking

```c
typedef Result (*HtmlNodeVisitFn)(
    const HtmlNodeView *view,
    size_t depth,
    void *user);

Result HtmlNodeWalkPreOrder(start, filter, visit, user, diag);
Result HtmlNodeWalkDescendants(start, filter, visit, user, diag);
```

`pre_order` visits `start` and everything below it; `descendants` skips
`start` itself but still requires it to resolve — an invalid start is an
error rather than an empty walk, because a caller that asked for "the contents
of this node" should be told when there is no such node.

**Order is pre-order (document order):** a node, then its children in sibling
order, each followed by its own subtree. The order is a pure function of the
tree links, so it cannot depend on hash iteration, pointer values, allocator
behaviour or memory layout. Two walks over the same tree visit the same nodes
in the same order — which is what later layout, paint, accessibility and query
layers need, and what makes it testable.

**`depth` is absolute**, measured from the document node (document = 0), not
relative to the walk's start. A subtree walk started on a node at depth 4
reports 4 for that node. Receiving it in the visitor is free — the walk
already knows whether it descended or ascended — so a visitor never has to
walk the parent chain to indent, bucket or bound its work.

**The visitor's code is the walk's result.** Return `RoboOk` to continue, or
any other code to stop — that code becomes the walk's return value. Early exit
is therefore a normal, deliberate outcome and needs no separate cancel
channel. `find_first` is built exactly on this.

```
walk(start, include_start, filter, visit, user, diag)
   → RoboOk            walk completed
   → visitor's code     the visitor stopped it
   → RoboInvalidArgument   NULL start/visit, or an unknown filter
   → RoboNotFound     start does not resolve, or the document was destroyed
   → RoboError         the walk exceeded the node ceiling (the links are not a tree)
```

**Iterative, bounded, allocation-free.** The walker descends and ascends the
existing links with a bounded step counter — no recursion, so an adversarial
depth cannot exhaust the C stack; no allocation, so a walk cannot fail halfway
for memory reasons; O(n) over the nodes it visits. The step bound is the
document's live node count plus one; a correct tree cannot be walked past it,
so exceeding it means a cycle. That is reported rather than silently
truncated, because handing a consumer a *partial* tree without saying so is
worse than failing.

**A visitor may walk reentrantly** — the walker holds no document-wide state,
so a nested walk is fine. What it must not do is destroy or reset the
document. That is detected (the walk stops and reports `RoboNotFound`) in
both places it can happen: at the top of every step, and immediately after the
visitor returns.

## Search

```c
Result HtmlNodeFindFirst(start, filter, match, user, out, diag);
Result HtmlNodeFindDescendant(start, filter, match, user, out, diag);
```

`match` may be `NULL`, in which case the filter alone decides — which makes
this the plain "first element / first text node in this subtree" query. A
non-`NULL` predicate receives the same view a visitor would and answers
non-zero for "this is the one".

The two exist because both questions are real and they differ **exactly when
the start node would match**: `find_first` is "the first element at or below
here", `find_descendant` is "the first element inside here". This is also the
reason `HtmlNodeFindElement` — a name search — is built on
`find_descendant`: started on an element of that name, it must find the one
*inside* rather than answer with the node the caller already holds.

`out` is cleared first and set only on `RoboOk`. `RoboNotFound` means
"nothing matched" (or the start does not resolve); the walk's error codes
propagate unchanged. A name search and a filter search can never disagree
about document order, because both are the one walker.

## Depth and out-of-bounds behaviour in one table

```
Document            depth 0
  html              depth 1
    body            depth 2
      Text("Hello ")   depth 3
      div              depth 3
        Text("Roboticela ")  depth 4
        span                 depth 4
          Text("Runtime")     depth 5
      Text("!")        depth 3
```

- `HtmlNodeRoot` of the deepest text → the document node.
- Parent traversal from it: span → div → body → html → document → `RoboNotFound`.
- Pre-order from `html` visits exactly the eight nodes above, in that order.
- `ELEMENTS` from `html` visits `html, body, div, span`.
- `TEXT` from `html` visits `Text("Hello ")`, `Text("Roboticela ")`,
  `Text("Runtime")`, `Text("!")`.

## The dump now runs on the walker

`HtmlTreeDump` used to be the one consumer that walked the tree
recursively. It is a visitor now: depth arrives from the walker, so
indentation costs no recursion, and the order, the cycle bound and the
no-recursion rule are shared instead of re-derived.

```
Document
  doctype html
  html
    head
      title
        Text: "RoboticelaOS Runtime"
    body
      h1
        Text: "Heading"
      div id="main"
        span
          Text: "deep"
```

The depth cut is now a *dump policy* rather than a traversal limit: past
`DUMP_DEPTH_MAX` one `... (deeper nodes elided)` line marks where it stopped,
and the walk keeps stepping (so nothing is silently skipped in a way that
would desynchronise the rest). The old sibling-cycle truncation message is
gone, because a cycle is now reported by the walker as the corruption it is.

## Iterating children with the cursor

The cursor (`HtmlChildCursor`) is the API a consumer that walks a node's
children uses, and the reason it exists is cost: `child_count` + `child_at`
resolves child 0 and then follows the sibling links *index* times, so iterating
one child list that way is O(n²) over that list. The cursor hands out one child
at a time — O(n), one registry lookup per child, no index bookkeeping in the
loop.

The consumer that first showed this was the deleted demo adapter's tree walk
(`RoboDemoHtml.c`, Phase 11.13), which recursed for a reason of its own (it
filtered by style, and an unsupported element still had its subtree walked) but
no longer knew how children were stored. That property — a walk that does not
know the storage — is the one this API exists to provide, and it is what the
surviving consumers rely on.

## Lifetime

- A walk **borrows nothing**: a visitor gets a view valid for its call, and
  the child cursor holds a document id, a node id, a step budget and a tree
  version.
- Nothing here keeps a document alive, so a walk cannot extend a document's
  lifetime past what the caller already holds.
- The one restriction this implies — **the tree must not be structurally
  changed during a walk** — was largely inexpressible when this phase shipped,
  because 8.8 added no mutation API at all. 8.9 closed that gap, and the
  restriction is now enforced rather than described: `mutation` reports `1`,
  and the cursor and the walkers stop with a MUTATION-domain `RoboError` the
  moment the document's tree version moves (see `Mutation.md` §9). That is the
  reason the cursor carries a version at all.
- The other path — destroying or resetting the document from inside a
  visitor — stays a distinct answer, `RoboNotFound`, because the document is
  gone rather than merely reshaped.
- A cursor or handle whose document went away reports `RoboNotFound`;
  nothing is retained to leak.

## Diagnostics

Failures report the traversal domain, `RoboHtmlDomainTraversal` (14):
a walk or search started from a handle that does not resolve, an unusable
visitor/predicate/output, an unknown filter, and a link graph that is not a
tree.

`HtmlDomQueryCapabilities` reports `traversal = 1` as of this phase,
alongside `tree_building`, `attributes` and `text_nodes`; `events` stays `0`.
`mutation` was `0` here — this phase is **read-only by construction**, a claim
about what the code can do rather than a promise about how it is used — and
became `1` in 8.9.

## What this phase is not

DOM mutation (`appendChild`, `removeChild`, `insertBefore`, `replaceChild`)
lives in `Mutation.md` (8.9): this header reads the tree, it does not write
it. Still absent after both phases: `cloneNode`, `createElement`,
CSS selectors, `querySelector`/`All`, the CSSOM, JavaScript bindings,
mutation observers, `Range`, `Selection`, Shadow DOM and Web Components.
Traversal answers *where things are*; none of those questions are answered
here.

## Regressions

No new defects surfaced in this phase. The refactor of the four existing
consumers did surface a real *contract* fix: the walker originally checked
whether the document still existed at the top of each step, which meant a
visitor that destroyed the document could still complete the walk through a
final `advance` and report `RoboOk` — a walk reported as finished over a tree
that no longer existed. The check now also runs immediately after the visitor,
and at the "finished" exit, so `RoboNotFound` is unconditional.

The suite also caught four wrong expectations of its own during development
(depth reported as relative when it is absolute, `find_descendant` expected to
match the start, a cursor copy expected to share state, and dump indentation
off by one level). Each was a body-of-the-test error, not a model bug, and
each is now pinned by the literal value the model produces.

## Test coverage

`runtime/html/tests/SuiteRoboHTMLTraversal.c`, filter `html.traverse.` — 20
cases: child relationships and the miss contract, parent/root/depth, the exact
eight-node document order plus its determinism across two walks, pre-order
from the document and from a subtree, the element and text filters, the filter
API, the child cursor (order, exhaustion, copies, empty parents, misuse), the
cursor after destroy, search (first vs descendant, NULL predicate, misses,
invalid start), parser-built trees and attribute reachability, Unicode across
Arabic / Persian / Urdu / Hindi / Bengali / Chinese / Japanese / Korean /
emoji plus untrimmed whitespace, a 1000-level tree walked and ascended, an
empty document, defensive input (NULL arguments, cleared handles, stale
handles, unknown filters), visitor control (early stop, refusal, reentrancy,
destroy-from-visitor), the dump on both a parsed and an explicitly built tree,
the capability flag and diagnostic domain, and the checkpoint document
(`html.traverse.checkpoint_document`).
