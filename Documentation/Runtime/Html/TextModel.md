# RoboHTML Text Model (Phase 8.7)

Character data was already reaching the tree before this phase — the 8.3 tree
builder created `RoboHtmlNodeKindText` nodes and the demo renderer already
painted text read out of them. What 8.7 adds is the **model over that node
kind**: one header that says what a text node is, how it is created, how its
bytes are read, how they are changed, and which of those questions have "no"
as an answer.

The organising decision is the same one 8.5 made for elements: **a text node
is a node.** Its identity is the node handle, its owner is the document, and
where it sits is the tree's business. This header adds no second hierarchy, no
second ownership system and no second traversal.

```
Document (8.4)
  └── Node (8.1 / 8.3)
        ├── Element (8.5)
        │     ├── attribute model (8.6)
        │     └── children ─┐
        └── Text    (8.7) ←─┘

HTML source
   ↓  tokenizer (8.2)
character tokens (borrowed, however the tokenizer chose to split them)
   ↓  tree builder (8.3)
HtmlTextCreate (8.7)  →  payload arena byte run
   ↓  text model
content / length / parent / set / append
   ↓  consumers
tree dump · demo renderer · future layout · future JavaScript
```

## The type

```c
typedef struct HtmlTextView {
    HtmlNodeHandle handle;   /* identity: the node handle */
    HtmlNodeHandle parent;   /* invalid when detached */
    const char *content;              /* borrowed; never NULL; "" when empty */
    size_t      length;               /* bytes, excluding the terminator */
    unsigned int state;               /* PARSER_CREATED | CONNECTED */
} HtmlTextView;
```

A view is a **snapshot, not a handle**. `content` points into the owning
document's payload arena, so it is valid until the next mutation or rebuild on
that document, or until it is destroyed — the same contract node views and
attribute values have. There is nothing to go stale, and callers that must
keep the bytes copy them.

Siblings and children are deliberately *not* repeated here. A text node is a
leaf, and where it sits among its siblings is a question `RoboHTMLNode.h`
already answers for every node kind (`node_get_view`, `child_at`,
`next_sibling`). Mirroring the links would create a second traversal surface
to keep in step with the first; 8.8 later made that surface authoritative —
see [`Traversal.md`](Traversal.md).

## Stored form

Unchanged from 8.3: a text payload is a header followed by one byte run plus a
terminator, with `hdr->length` including the terminator.

```
TEXT   [u32 body_len][u32 attr_count=0][u32 flags=0][u32 ns=NONE] bytes '\0'
```

The declared length is the authority, so no reader scans for a terminator and
a run containing a NUL byte cannot desynchronise the walk. The bytes are
whatever the tokenizer produced after character-reference decoding — already
UTF-8, never re-encoded, never normalised.

Nothing here is a per-node allocation: the run lives in the document's
bump-allocated arena alongside element names and attribute pairs, so a
half-built tree cannot leak a string and one teardown frees everything.

## The construction seam

```c
Result HtmlTextCreate(HtmlDocumentId document_id,
                                    const HtmlTextCreateInfo *info,
                                    HtmlNodeHandle *out,
                                    HtmlDiag *diag);
```

This is the text-side twin of `HtmlElementCreate`, and for the same
reason: the rules live in one function instead of being a convention every
call site has to honour. A text node is always created **inside** a document,
already linked under a parent, so "a text node with no document" cannot be
expressed.

The checks, in the order they run:

| Rejected | Code |
|---|---|
| `out` or `info` NULL | `RoboInvalidArgument` |
| `content` NULL with a non-zero `length` | `RoboInvalidArgument` |
| origin `NONE` or undefined | `RoboInvalidArgument` |
| parent handle ill-formed | `RoboInvalidArgument` |
| parent claims **another** document | `RoboInvalidArgument` |
| parent not a live node of this document | `RoboNotFound` |
| unknown document, lifecycle refuses construction, tree finalized | `RoboNotFound` / `RoboError` |
| node registry or payload arena exhausted | `RoboOutOfMemory` |

The cross-document line is worth its own sentence: the parent's *claimed*
document id is compared before anything is resolved. That is what turns a
handle from document A used on document B into a detected error instead of an
aliased node — two documents may legitimately issue the same id/generation
pair, and only the document id separates them.

The tree builder is the first caller. It used to register, encode and link a
text node by hand; now it fills a descriptor and calls this.

## Reading

| Call | Answer for a live text node | Answer for anything else |
|---|---|---|
| `HtmlTextIsText` | `1` | `0` |
| `HtmlTextContent` | borrowed bytes (never NULL) | `NULL` |
| `HtmlTextLength` | byte count | `0` |
| `HtmlTextIsEmpty` | `1` when the run is zero bytes | `0` |
| `HtmlTextParent` | the parent handle | `RoboNotFound` |
| `HtmlTextViewGet` | fills the view | `RoboNotFound` |

`NULL` versus `""` is the distinction the content accessor exists for:
`NULL` means "not a text node", `""` with length `0` means "a text node whose
run is empty". It is the same shape
`HtmlElementAttributeValue` uses for absent versus present-and-empty,
and it is why `is_empty` is a separate call rather than `length == 0`.

A well-formed handle that names nothing live — cleared, stale, or from a
destroyed document — resolves as `RoboNotFound` rather than
`RoboInvalidArgument`. The handle is not malformed; it simply names nothing
any more. Only `NULL` arguments are argument errors. Every accessor therefore
lets a caller test a handle before trusting it, and a stale handle is harmless
rather than a crash.

## Mutation

```c
HtmlTextSet(text, bytes, length, diag);     /* replace the whole run */
HtmlTextAppend(text, bytes, length, diag);  /* extend the run */
```

Two DOM-shaped verbs, not parser-shaped ones. They exist because the DOM phase
and later editing need one place to go.

**`set` writes a fresh payload.** The old bytes become dead arena space —
bounded, and reclaimed wholesale — instead of something to splice around, and
no payload is ever resized under a borrowed pointer. This is the same choice
the attribute model's mutation makes.

**`append` is one operation with two implementations.** When the run already
ends the arena it grows in place: no copy, no allocation, and the terminator
is overwritten rather than moved. When other payloads sit behind the run it
copies both parts once into one buffer and writes a fresh payload. The result
is indistinguishable; the fast path exists only because the parser's merge
depends on it.

**Node identity, parent, siblings and child order are untouched by both.**
The node is not re-registered, so its generation is unchanged and any handle
already taken stays valid. That is also why mutation works after
`finalize`: 8.4 closed tree *construction*, not the document.

## Runs and merging

The tokenizer hands character data over in as many pieces as it likes —
buffer boundaries, character references, split-up tokens. **The tree builder,
not the tokenizer, owns the decision of how many text nodes a run becomes**,
because that is a tree question.

A run stays open while `text_node`/`text_parent` still name it. It closes on
an element, a comment, a mode change or a stack pop. Inside an open run,
`HtmlTextAppend` always succeeds, so a run becomes **exactly one node**:

```
<p>Hello <b>world</b>!</p>
   → p
       ├── Text("Hello ")     run closed by <b>
       ├── b
       │     └── Text("world")
       └── Text("!")          run closed by </p>
```

Before 8.7 the merge was conditional on the run's payload still ending the
arena. In practice that condition always held during a parse (nothing else
allocates mid-run), so behaviour is unchanged for every parser input — the
difference the model makes is that the merge is now guaranteed rather than
incidental, and the tree builder has no fallback branch that would silently
produce a second node.

## Empty text (the policy, stated once)

- **The model permits an empty text node.** A zero-length run is a legal
  value, and the future DOM's `createTextNode("")` must be expressible through
  the same seam as any other creation.
- **The parser never produces one.** `insert_text` returns early on a
  zero-length run rather than registering a node. That is not a loss — there
  is no character there to lose — and it keeps a whitespace-heavy document
  from accumulating empty nodes. `<p></p>` has *no* children, which is a
  fact the 8.7 suite asserts.
- **Neither is inferred at read time.** A node either exists (and its length
  may be `0`) or it does not.

## What is preserved, byte for byte

Nothing in this layer normalises, trims, collapses, resamples, case-folds or
re-encodes:

| Input | Stored run |
|---|---|
| `<div>Hello    world</div>` | `Hello    world` (four spaces) |
| `<div>a\tb\nc</div>` | `a\tb\nc` |
| `<div>  padded  </div>` | `  padded  ` (not trimmed) |
| `<p>&amp;&lt;&gt;&#65;&#x42;</p>` | `&<>AB` (decoded by the tokenizer) |
| `<p>مرحبا 😀 中文</p>` | the same UTF-8 bytes |
| `<p>` + 6000 characters | one node, 6000 bytes, one append away from 6004 |

Whitespace collapsing, shaping, bidi, line breaking and font selection are
later phases that read these bytes. Doing any of it here would destroy
information no later layer could recover — there is no "the original
whitespace" to go back to once it is gone.

## Tree dump

A text line is labelled and its whitespace escaped, which is what makes a run
legible without a viewer:

```
Document
  doctype html
  html
    body
      h1
        Text: "Heading"
      p
        Text: "line one\nline two\tend"
```

The label separates a text node from the `#comment "..."` line below it, and
`Text: ""` is how an empty run appears — visibly present, visibly empty. The
dump reads through `HtmlTextContent`, so it cannot drift from the
accessor.

## Consumers

The demo renderer walks the tree and pulls each block's text with
`HtmlNodeSubtreeText`, which concatenates text-kind nodes in document
order. Nothing in the renderer hardcodes a string that exists in the HTML: the
window caption, the heading, the paragraphs and the button label all come from
real text nodes, and `<button disabled>` gating reads the attribute model.
That is the whole vertical slice, unchanged except for where the bytes come
from.

## What this phase is not

`CharacterData` (`substringData` / `insertData` / `deleteData` /
`replaceData`), `splitText`, `normalize`, the `Range` and `Selection` APIs,
`contenteditable`, mutation observers, JavaScript bindings, CDATA and
foreign-content text, whitespace processing, text shaping, bidi, line
breaking, font selection and layout measurement. Text nodes carry bytes;
every one of those consumers reads them later.

## Regressions

No new defects surfaced in this phase, and one *inconsistency* did: the first
draft of `set`/`append` rejected an ill-formed handle with
`RoboInvalidArgument` while every other accessor resolved the same handle as
`RoboNotFound`. The 8.7 suite caught it (a cleared handle is a lookup that
missed, not an argument error), and the model now answers consistently. The
payload-layer defect the 8.6 phase fixed — uninitialised mutation scratch —
is behind `set`/`append` too, and the suite's 32-iteration and long-run
mutation cases exercise exactly that path.

## Test coverage

`runtime/html/tests/SuiteRoboHTMLText.c`, filter `html.text.` — 23 cases:
descriptor init, creation (basic, empty, validation), kind identification and
the not-text answers, `set`, `append` (including the slow path with a payload
behind the run), mutation after finalization, parser integration, runs and
merging, whitespace, character references, Unicode (Arabic / Persian / Urdu /
Hindi / Bengali / Chinese / Japanese / Korean / emoji), long runs, traversal
over a mixed child list, ownership and stale handles across `reset` and
destroy, teardown, large-tree teardown, malformed input, the tree dump, the
capability flag, and the visual demo document.
