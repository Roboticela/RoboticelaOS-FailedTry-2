# RoboHTML Document Lifecycle

Phase 8.10. The document owns exactly one lifecycle, and every question about
"how far along is this document" is answered from it.

```
UNINITIALIZED → CREATED → LOADING → INTERACTIVE → COMPLETE
                    │         │            │           │
                    └─────────┴────────────┴───────────┴──→ FAILED
                                                                │
                                  ┌─────────────────────────────┘
                                  ▼
                            DESTROYING → DESTROYED
```

The vocabulary is the web platform's own `document.readyState`, because that is
what the states are and a caller already knows what the words mean.

## The two ideas the phase turns on

**`INTERACTIVE` is a statement about the tree.** Parsing has finished and the
DOM is live: readable, traversable and mutable. This is what `ROBO_HTML_…_READY`
meant in 8.1–8.9; it is renamed because `interactive` is the name the platform
uses for exactly this point.

**`COMPLETE` is a statement about the input.** No further input can arrive.
Nothing about the tree changes — and that is deliberate: **completion describes
input, not permission.** Reading, traversal, mutation, a replacement source and
reset all keep working after a document completes. A document that stopped being
editable at COMPLETE would be a different state machine, and a worse one.

So the graph has two edges that only exist one way, and they are the whole
point:

- **`LOADING → INTERACTIVE`** — a parse finished.
- **`INTERACTIVE → COMPLETE`** — the input settled.

There is no `LOADING → COMPLETE` and no `CREATED → COMPLETE`. A document cannot
be finished loading before it has finished parsing, so the shortcut is not
merely unused, it is refused (`ROBO_INVALID_ARGUMENT`). `COMPLETE → INTERACTIVE`
does not exist either: completion is not a state you step back from.

## Which entry point settles where

The rules follow from "COMPLETE means the input is finished":

| Situation | Settles at | Why |
|---|---|---|
| One-shot whole-document load (`robo_html_build_tree`) | **COMPLETE** | The source is a complete source: it was consumed to the end, so nothing can follow. |
| A finished build over a source that was **truncated** at the node ceiling | **INTERACTIVE** | The source was abandoned, not consumed. Claiming completion would be saying "no further input can arrive" about input that was never read. |
| A finished build over a **stream** source | **INTERACTIVE** | More bytes may still be delivered. |
| `robo_html_document_detach_source` | **INTERACTIVE** | Detaching abandons input rather than finishing it. |
| `robo_html_parser_detach` | **INTERACTIVE** | Same: the tree is whatever was parsed so far. |
| An **empty** source | **COMPLETE** | Nothing to parse, so nothing can be outstanding. It still passes *through* INTERACTIVE rather than jumping. |
| `robo_html_document_reset` on a document that had loaded | **COMPLETE** | The resting state after a reset is the same state an empty source produces. |
| `robo_html_document_reset` on a document that never loaded | **CREATED** | It still has no source and never had one. |
| `robo_html_document_mark_interactive` / `mark_complete` | **INTERACTIVE** / **COMPLETE** | The caller says so explicitly. |

The tree builder drives the first two itself; only the stream case needs a
source that no public constructor produces yet (`ROBO_HTML_SOURCE_KIND_STREAM`
is recognised by the parser for mode selection but nothing creates one). That
condition is stated anyway because it *is* the rule, and it is exercised today
through the truncation half.

A document built entirely through the element API and never given a source has
no load to settle, so it stays **CREATED** — which already permits everything the
DOM needs (content, mutation, traversal). There is deliberately no
`CREATED → INTERACTIVE` edge: every state above CREATED is a statement about a
load, and a second route into INTERACTIVE would blur what `created` means.
`robo_test_register_html_lifecycle`'s `hand_built_document_stays_created` states
this as a rule with a test rather than leaving it to be discovered.

## Predicates

On the lifecycle value (`RoboHTMLLifecycle.h`), for code that already holds a
snapshot — a client lifecycle callback, a diagnostic:

| Predicate | True for |
|---|---|
| `allows_content` | CREATED, LOADING, INTERACTIVE, COMPLETE |
| `allows_source_attach` | CREATED, INTERACTIVE, COMPLETE |
| `is_settled` | INTERACTIVE, COMPLETE |
| `is_terminal` | FAILED, DESTROYING, DESTROYED |
| `is_destroyed` | DESTROYED |

On a document id (`RoboHTMLDocument.h`), for code that has not fetched the enum:
`is_loading`, `is_interactive`, `is_complete`, `is_settled` — each true for
*exactly* its state, and each **false for an unknown or destroyed id**, so a
retired id answers "no" to every state question rather than reporting a state it
no longer has. A caller that needs to tell "not that state" apart from "not a
document" uses `robo_html_document_get_lifecycle`, which is `ROBO_NOT_FOUND` for
a retired id. That is on purpose: retired ids are not probeable, so the query
cannot become an oracle for recycled ids.

## What a lifecycle change must not do

Two invariants are asserted after every transition in the test suite, because a
lifecycle that changed the tree would be worse than no lifecycle at all:

1. **The whole-tree audit stays clean** (`robo_html_document_validate_tree`).
2. **The root is the same node** — a transition is not a rebuild.

And one about the traversal contract: **a lifecycle transition does not move the
document's tree version.** The 8.9 cursor and walkers invalidate when the
version moves, because their remaining steps would describe a tree that no
longer exists. Not one node moves during a lifecycle change, so an in-flight
iteration keeps working — `is_not_a_structural_change` pins that. A *reload* is
different: it destroys and rebuilds the tree, so old handles go stale and the
existing invalidation rules apply unchanged.

## Failure and destruction

`FAILED` is sticky: the first failure wins, a later `mark_failed` is a no-op
success rather than an error about the first, and a failed document refuses a new
source with `ROBO_ERROR` instead of silently discarding it. Failure is not
teardown — the tree is still there to inspect and audit.

`DESTROYING` is entered only through `robo_html_document_destroy`; `DESTROYED` is
terminal and the id is retired, never reused. After destruction every handle into
the document is stale, so mutation and traversal report `ROBO_NOT_FOUND` rather
than writing into freed state, and a second `destroy` reports `ROBO_NOT_FOUND`
rather than double-freeing.

## Debug output

The tree dump's root line carries the document's own state, read from the
document rather than assumed:

```
Document lifecycle=complete
  html
    body
      p
        Text: "Hello"
```

It is on the `Document` line rather than a line beneath it because the document
node *is* the root: an indented `lifecycle:` line would claim a node that does
not exist. The dump is the one place a document's state is visible without
asking for it, so a dump taken mid-load says so.

## Where the code lives

| File | Contents |
|---|---|
| `Include/RoboHTML/RoboHTMLLifecycle.h` / `Source/RoboHTML/RoboHTMLLifecycle.c` | The state enum, the transition graph, the predicates, the names. |
| `Source/RoboHTML/RoboHTMLDocument.c` | The transitions each document operation drives, `mark_interactive`, `mark_complete`, the id predicates. |
| `Source/RoboHTML/RoboHTMLTreeBuilder.c` | The parse-end transition and `build_consumed_whole_source`. |
| `Source/RoboHTML/RoboHTMLParser.c`, `RoboHTMLDocumentModel.c` | Detach and reset settling. |
| `html/tests/SuiteRoboHTMLLifecycle.c` | `html.lifecycle.*` — the graph, every entry point, refused transitions, observability, mutation and traversal in every state, malformed input, ownership, teardown, the dump. |
| `layout/tests/SuiteRoboLayoutBuild.c` | `layout.build.rebuild` — a rebuild replaces the tree a picture was built from, which is the transition that has a visible consequence. |
