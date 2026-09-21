# RoboHTML tree builder (Goal 8.3)

The tree builder is the first real implementation of the Phase 8.1 parser
boundary. It leases the document, owns a tokenizer, consumes the 8.2 token
stream, and constructs the document tree through document-scoped node
registration. Nothing above it needs to know how tokens map to nodes.

```
RoboHTML source (8.1)
     │
     ▼
RoboHTML tokenizer (8.2) ── tokens ──▶ tree builder (8.3)
     │                                      │
     │                                      ▼
     └───────────────────────▶ document tree (nodes + payload arena)
                                            │
                                            ▼
                             read accessors: RoboHTMLNode.h / RoboHTMLElement.h
                                            │
                                            ▼
                             a document pipeline: RoboCSS → RoboLayout → RoboPaint
                             → RoboRender → RoboDesktop (RoboDemo's stand-in
                             walk was deleted in Phase 11.13)
```

## 1. Objects and ownership

| Object | Owner | Notes |
|---|---|---|
| `HtmlTreeBuilder` | caller (`create` / `destroy`) | Holds the tokenizer, the open-element stack, the diagnostics list and the parser lease |
| `HtmlTokenizer` | the builder | Destroyed with the builder |
| `HtmlParser` (lease) | the builder | The 8.1 exclusive lease: a second builder on one document is refused with `RoboBusy` |
| every node | the document | The builder only ever holds node **ids**, never pointers into the registry |
| element/attribute/text bytes | the document's payload arena | Bump-allocated, append-only, freed as a unit |

Destroying the builder after a build leaves the tree in the document intact
and usable. Destroying the builder mid-build releases the parser lease, which
returns the document to `INTERACTIVE` — a half-built tree cannot leave the
document stuck in `LOADING`, and abandoning a build is not finishing it, so the
document does not claim to be complete.

## 2. Public API

| Call | Purpose |
|---|---|
| `HtmlTreeBuilderCreate(document_id, &builder, diag)` | Lease the document, allocate the builder |
| `HtmlTreeBuilderBegin(builder, &src, diag)` | Clear any previous tree, attach the source, create the tokenizer, go to `FEEDING` |
| `HtmlTreeBuilderNext(builder, diag)` | Pull one token and consume it; consuming `EOF` finishes the build |
| `HtmlTreeBuilderConsume(builder, &token, diag)` | Consume an externally supplied token (same rules, no tokenizer) |
| `HtmlTreeBuilderFinish(builder, diag)` | Close open elements, count the result, settle the document: `INTERACTIVE`, and `COMPLETE` when the whole source was consumed |
| `HtmlTreeBuilderBuild(builder, &src, diag)` | `begin` + drive to EOF + `finish` |
| `HtmlBuildTree(document_id, &src, &result, diag)` | One-shot: build with a temporary builder and hand the tree to the document |

Introspection: `get_state` (parser boundary state), `get_mode` (insertion
mode), `get_text_mode`, `current_node`, `open_element_count` /
`open_element(index)`, `result`, `diagnostic_count` / `get_diagnostic`.

`HtmlTreeBuilderSetTextMode()` is the controlled hook later phases
need: it switches the builder's text family (and the corresponding tokenizer
state) without going through an element name.

### Determinism

`build()` is deterministic: the same source always produces the same tree,
the same counters and the same diagnostics. Nothing in the builder reads
locale, time, environment or the filesystem.

## 3. Node kinds produced

- `RoboHtmlNodeKindDocument` — the document node registered at document
  creation; the tree root.
- `RoboHtmlNodeKindDocumentType` — from a `DOCTYPE` token (`name`,
  `public_id`, `system_id`; absent parts stored as empty strings).
- `RoboHtmlNodeKindElement` — name, attribute pairs (source order,
  duplicates preserved), self-closing flag.
- `RoboHtmlNodeKindText` — decoded UTF-8 text.
- `RoboHtmlNodeKindComment` — raw comment payload, never interpreted.

`RoboHtmlNodeKindFragment` exists in the taxonomy but the builder never
creates one: fragment parsing is a later goal, and the design does not forbid
it (a fragment root would simply be registered instead of the document node).

### Payload layout

One definition lives in `RoboHTMLPayload.c` so writers never hand-roll offsets
and readers never guess them:

```
header                HtmlPayloadHeader {length, attr_count, flags, reserved}
ELEMENT               name '\0' then per attribute
                      [u32 name_len][u32 value_len] name '\0' value '\0'
TEXT / COMMENT        bytes '\0'
DOCUMENT_TYPE         name '\0' public '\0' system '\0'
DOCUMENT              no payload
```

Every string is NUL-terminated so readers can hand out borrowed C strings,
while the explicit `u32` lengths let the walk skip a string without scanning
it — an attribute value containing a NUL byte from a malformed source still
round-trips without desynchronising the reader.

Payloads are written once and never edited, with one exception: appending to a
text payload that currently ends the arena grows it in place, which is how
adjacent character tokens collapse into a single text node instead of one node
per token.

## 4. Insertion modes

```
initial → before-html → before-head → in-head → after-head → in-body
                                                              ↓
                                                    after-body → after-after-body
                                                              ↓
                                                    text (RCDATA/RAWTEXT/script/plaintext)
```

| Mode | Behaviour (subset implemented) |
|---|---|
| `initial` | Whitespace ignored; DOCTYPE → document-type node; a comment → document node; anything else → `before-html` and reprocess |
| `before-html` | Implied `<html>`; comments → document node; duplicate DOCTYPE recovers |
| `before-head` | Implied `<head>`; `<head>` creates it; duplicate `<html>` recovers |
| `in-head` | Whitespace text and comments in `head`; void head elements; `title` → RCDATA, `style`/`noscript` → RAWTEXT, `script` → script data; `</head>` closes; anything else closes `head` and reprocesses |
| `after-head` | Whitespace → `html`; comments → `html`; `<body>` creates body; anything else implies `<body>` and reprocesses |
| `in-body` | Text, comments, and the element rules below; `</body>` → `after-body` (body stays open); EOF ends the build |
| `after-body` | Whitespace → body; comments → `html`; `</html>` → `after-after-body`; anything else re-enters `in-body` |
| `after-after-body` | Comments → document node; anything else re-enters `in-body` |
| `text` | Element content for RCDATA / RAWTEXT / script data / plaintext; the matching end tag closes the element and restores the previous mode |

Every mode transition is explicit and testable:
`HtmlInsertionModeName()` and `HtmlTreeBuilderGetMode()`.

Mode re-dispatch is guarded (`REPROCESS_LIMIT = 24`): a token that cannot be
resolved by repeatedly switching modes is recorded and stops the build instead
of looping.

## 5. In-body element rules

- `html` / `head` / `body` start tags after the structural element exists are
  recoverable duplicates (diagnostic, ignored).
- Headings (`h1`…`h6`) close an open heading and then an open `p`.
- Block-level start tags close an open `p` (`close_p_if_open`).
- A second `<button>` while a button is open closes the outer one.
- `pre` / `listing` / `textarea` swallow one immediately following LF, per the
  HTML model.
- End tags: `</body>` / `</html>` follow the mode machine; `</br>` is re-run as
  `<br>`; a heading end tag pops back to the nearest matching heading; any
  other end tag pops back to the nearest open element with that name, and
  reports a diagnostic when there is none.

### Void and self-closing elements

`area base basefont bgsound br col embed frame hr img input keygen link meta
param source track wbr` are inserted and never pushed (so following content is
never swallowed). A source `/>` is recorded as the element's self-closing flag
and also suppresses the push.

## 6. Text handling

- Character tokens become text nodes under the current node.
- Adjacent character tokens **merge** when the previous text node's payload
  still ends the arena — the common case for references and raw-content
  chunks. Merging is attempted first and falls back to a sibling text node,
  so a merge never reorders the tree.
- Empty text is never materialised: an element with no text has no text child.
- Indentation whitespace is preserved as real text nodes (as in a real DOM);
  whitespace is only *ignored* in the early modes, where the HTML model says
  so. Consumers that paint text skip whitespace-only nodes.

## 7. Recovery and limits

Malformed input is never fatal. Recoverable problems (unexpected end tag,
duplicate structural tag, orphan content, unclosed element at EOF) are
recorded as diagnostics and the build continues. Only NULL/invalid arguments,
an uninitialized module, an unusable lifecycle, a destroyed document, or
allocation failure return an error code.

| Limit | Value | Behaviour when hit |
|---|---|---|
| Nodes per document | `RoboHtmlMaxNodesPerDocument` (4096) | Build stops, `result.truncated = 1`, the build still reports `RoboOk` and the document keeps a valid partial tree |
| Attributes per element | `RoboHtmlMaxAttrsPerElement` (64) | Extras dropped with a diagnostic |
| Retained diagnostics | `RoboHtmlTreeBuilderMaxDiagnostics` (64) | Extras dropped; `result.recovered_errors` still counts them |
| Tree-dump recursion | `DUMP_DEPTH_MAX` (64) | Deeper subtrees are elided with a marker |
| Dump text per node | `DUMP_TEXT_BUDGET` (72 bytes) | Truncated with `...` |

There is no separate nesting-depth cap: the node ceiling already bounds the
open-element stack (which is heap-allocated and grows geometrically), and the
dump — the one depth-sensitive consumer — caps its own recursion.

## 8. Tree dump

```c
HtmlTreeDump(document_id, sink, user, diag);
HtmlTreeDumpToBuffer(document_id, out, out_cap, &out_length, diag);
```

```text
Document
  doctype html
  html
    head
      title
        "RoboticelaOS Runtime"
    body
      h1
        "Heading"
      p
        "Body & text"
      button
        "Continue"
      img src="a.png" /
```

Text and comment payloads are quoted and escaped (`\n`, `\r`, `\t`, `\"`);
attributes print in source order; every iteration is bounded, so a corrupted
sibling link terminates the dump instead of hanging. The buffer variant writes
a safely truncated NUL-terminated string and reports the *untruncated* length
so callers can detect truncation.

## 9. Deliberately absent (with the seam kept open)

| Absent | Seam |
|---|---|
| Active formatting elements / adoption agency (`b`, `i`, `a` reconstruction) | Generic element insertion already pushes/pops; an active-formatting list would slot in beside the open-element stack |
| Tables, foster parenting | `insertion mode` enum is open-ended; `is_head_content_element` / mode handlers show where table modes attach |
| Foreign content (SVG / MathML) | Nothing assumes every element is HTML-namespaced in the payload layout — a namespace field would be added to the element payload |
| Fragment parsing | The design does not forbid it: a fragment root would be registered as `RoboHtmlNodeKindFragment` and the modes reused |
| DOCTYPE quirks/document-mode | `robo_html_token_token` already carries `force_quirks`; the document-type payload records the identifiers |
| Script execution, textarea value API, `template` content | Element payload + text nodes are already in place |

## 10. Not in this phase

No DOM mutation APIs, no events, no CSS or selectors, no layout, no paint, no
JavaScript, no networking, no storage, no cookies/CORS, no browser navigation.
RoboHTML remains independent of Chromium and of every higher runtime layer
(enforced by the `RoboHTML Chromium-independence source scan` in the build).
