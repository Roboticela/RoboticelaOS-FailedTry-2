# RoboHTML Element Model (Phase 8.5)

An element is a **node**. It does not get an identity of its own, a registry of
its own, or a lifetime of its own: it is an element-kind node inside a
document's registry, and `RoboHTMLElement.h` is the *view* over those nodes.
That single decision is what keeps this phase additive — nothing below it
changed, and nothing above it has to re-ask "which element is this?".

```
Document (8.4)                     owns everything, closes construction
  └── Node (8.1 / 8.3)             identity (id, generation), tree links
        └── Element (8.5)          namespace, classification, state,
                                   attribute model, creation seam
```

---

## 1. Identity

An element's identity is a `HtmlNodeHandle` — `(document id, node id,
generation)`. There is no `robo_html_element_id_t`, no element table, and no
address-based identity anywhere in the public surface.

| Question | Call |
|---|---|
| Is this handle a live element? | `HtmlElementIsElement` |
| Whose element is it? | `HtmlNodeHandleDocument`, `HtmlDocumentOwnsNode` |
| Is this handle stale? | `HtmlNodeHandleIsStale` |
| Is it the element I mean? | `HtmlElementIsNamed` (ASCII case-insensitive) |

`is_element` answers `0` for NULL, ill-formed, stale and non-element handles
alike, so a caller may test before treating. The accessors that return data
instead of a verdict (`view_get`, `attribute_count`, `parent`) return
`RoboNotFound` for a non-element and `RoboInvalidArgument` for a NULL
argument, which is how a caller tells "wrong kind" from "wrong call".

---

## 2. Namespace

Namespaces are stored per element, not folded into the name string:

```c
RoboHtmlNamespaceNone / HTML / SVG / MATHML
```

- `HtmlNamespaceUrl` gives the URI (`http://www.w3.org/1999/xhtml`, …).
  It is a constant per namespace rather than 28 bytes copied onto every
  element — storage stays small, and the future DOM/CSS layers compare
  against a constant.
- Only `HTML` is produced in this phase: the tree builder never switches
  namespace, so `<svg>` is an ordinary HTML-namespace element named `svg`
  and there is no foreign-content switch yet. The enumerator exists so a
  later phase adds behaviour rather than a type.
- The dump prints a namespace prefix (`svg:rect`) for anything outside HTML,
  so a same-name-different-element case is visible in debug output.

---

## 3. Classification

`HtmlElementClassify(namespace, local_name)` is a pure function of
identity, computed at read time and stored nowhere. It is deliberately **not**
the HTML content-category specification; it is the small vocabulary a renderer
or a later phase needs for one decision:

`GENERIC`, `ROOT`, `METADATA`, `SECTION`, `HEADING`, `PARAGRAPH`, `TEXT`,
`REPLACED`, `CONTROL`.

- Anything unrecognised is `GENERIC` — never an error. Custom elements, SVG,
  MathML and future elements are therefore safe to classify.
- A non-HTML namespace always classifies as `GENERIC`, because an SVG `<a>`
  is not an HTML `<a>` and pretending otherwise would be a lie.

**Since Phase 8.11 the role is a column of the tag vocabulary, not a table of
its own.** `HtmlElementClassify` is now a delegation to
`HtmlTagClass(HtmlTagFromName(ns, name))`: the same rows that
define which elements exist also define what role each plays, so a role and a
type cannot disagree. What this goal moved here is only *where the answer
lives* — the behaviour is unchanged name for name, and
`html.tag.classify_agrees_with_vocabulary` asserts that equality for every row
in both cases rather than trusting it.

Element **identity** — which tag, is it a heading, is it void — is
`HtmlTag`, documented in [`Elements.md`](Elements.md). The role answers
"what kind of thing is this?"; the tag answers "which thing is it?".

---

## 4. State

`HtmlElementView.state` is a mask of three bits, each derived rather
than stored as a flag that every future mutation would have to maintain:

| Bit | Meaning | How it is derived |
|---|---|---|
| `PARSER_CREATED` | created by parsing | the node registry entry's recorded origin |
| `CONNECTED` | reachable from the document root | parent-chain walk to the root, bounded by the node ceiling |
| `SELF_CLOSING` | the source wrote `/>` | the element payload flag |

`CONNECTED` is walked, not cached: a cached flag would go stale the moment a
future DOM moved a subtree, and the walk is one step per ancestor.

---

## 5. Creation — the one seam

```c
HtmlElementCreate(document_id, &info, &out, diag)
```

This is the single place an element comes into existence, so every rule is
enforced *here* rather than at each call site:

1. the document must exist and accept construction (not finalized, lifecycle
   not FAILED/DESTROYED);
2. the parent must be well-formed, must name **this** document, and must be
   live in it — the cross-document check is three lines in one function
   instead of a convention every caller remembers;
3. the name must be non-empty, and the namespace must be live and not `NONE`;
4. identity is allocated by the document, and the element is linked as the
   parent's last child **in the same call**.

Because creation always attaches, "an element with no document" is not an
expressible state — there are no detached elements in this phase. Ownership
stays what Phase 8.4 made it: the document owns every node and every payload
byte.

`HtmlElementCreateInfo` is a descriptor rather than nine positional
arguments, because this is the seam the attribute model (8.6) and the DOM's
`createElement` both extend.

### Names

Stored element and attribute names are **lowercase ASCII**. The tokenizer
already lowercases parser input; creation lowercases what it is given, so the
invariant holds for the DOM seam too.

The parser path deliberately does **not** validate the character set: a
malformed tag name must be tolerated, not rejected, or a stray byte in a tag
would abort a parse. `HtmlElementNameValidate` /
`HtmlElementNameNormalize` remain the strict policy helpers for names
an *application* supplies.

A name longer than the normalization buffer is stored **verbatim** rather than
truncated — shrinking a tag name would silently change the element's identity,
which is worse than an unlowercased absurdity.

---

## 6. Attributes

8.5 added the minimum read side the model and any renderer need — count,
index, lookup, presence — and **8.6 replaced the storage beneath it with the
real attribute model** without changing those four calls or the element
around them:

| Call | Answers |
|---|---|
| `HtmlElementAttributeCount` | how many |
| `HtmlElementAttributeAt` | the *index*'th, in stored order, duplicates preserved |
| `HtmlElementFindAttribute` | the first named (case-insensitive) |
| `HtmlElementHasAttribute` | presence only |
| `HtmlElementAttributeValue` | the value, or `NULL` when absent |
| `HtmlElementSetAttribute` | rewrite, collapse duplicates, append when new |
| `HtmlElementAddAttribute` | append unconditionally (the parser's verb) |
| `HtmlElementRemoveAttribute` | drop every match; absent is a no-op |

The type crossing the boundary is `HtmlAttribute`, staged by the
tokenizer, stored by the element, and returned borrowed by the readers.
Presence stays deliberately separate from value lookup: `<input disabled>` is
*present with an empty value*, and "no such attribute" and "present but empty"
are different answers.

Not here, by design: `NamedNodeMap`, namespace-aware attributes,
`classList`/`style`/`dataset`, CSS selector matching. The model itself is
documented in [`AttributeModel.md`](AttributeModel.md).

---

## 7. Tree-builder integration

The builder no longer registers, encodes and links an element by hand. Its
`create_element` fills a descriptor and calls `HtmlElementCreate`:

```c
info.parent          = <handle of the parent node>
info.origin          = RoboHtmlNodeOriginParser
info.namespace_id    = RoboHtmlNamespaceHtml
info.local_name      = <tokenizer tag name>
info.attributes      = <staged HtmlAttribute>
info.self_closing    = <"/" seen>
```

Open-element-stack behaviour is unchanged and still the builder's decision
(void and self-closing elements are never pushed), so start/end-tag handling,
insertion modes and recovery are the 8.3 implementation, not a rewrite.

---

## 8. The first consumer, and what it became

The consumer that exercised this seam first was a visual demo adapter, and it is
gone: Phase 11.13 deleted the demo layer when the desktop shell landed, and the
consumer now is the desktop's document bridge (`RoboDesktopDocument.c`), which
feeds the real pipeline — markup → style → layout → paint → display list.

The model's part did not change with it. Its rules are the ones a consumer has to
obey, and they are asserted through this module's own suites rather than through
a renderer:

- `HtmlElementViewGet` **is** the filter — it fails for text, comment,
  doctype and stale handles, which is exactly what a walk must skip;
- names compare through `HtmlElementIsNamed`;
- roles come from `HtmlElementClassification`, so `<h1>` … `<h6>` are all
  headings to a layout pass;
- a non-HTML-namespace element has no role in this model, and a layout pass that
  reached one would be asking a question the model answers `UNKNOWN` to.

The painted subset is otherwise the same subset, so the picture is stable: the
fixture's own reading is `blocks=4 supported=8 ignored=0`, unchanged from the 8.4
checkpoint.

The step from a tree to a picture is `RoboDesktopDocument.c` (Phase 11.13): the
document a shell window shows, built by this model and painted through
RoboCSS → RoboLayout → RoboPaint. The QEMU device path asserts that pipeline's
*outcome* rather than these counts — the guest program reports what the frame
carried (`ROBO_QEMU_DESKTOP_LABELS`, `_UNSHAPED`, `_CONTENT_RUNS`), because a
guest that re-printed the tree's own numbers would be checking the tree against
itself.

---

## 9. Defects fixed in this phase

1. **Pooled attribute buffers were reused without being reset.**
   `HtmlTokenPoolNext` returned `entries[count++]` and only
   `HtmlTokenPoolClear` rewound the cursor — it never cleared the
   entries. Since the tokenizer *appends* into an entry, a reused slot carried
   the previous token's bytes forward: `<div class=… id=…>` followed by
   `<input disabled value=…>` produced the attribute name `idvalue` with value
   `mainhello world`. Every attribute past the first attributed tag was
   silently corrupt. The pool's own contract ("entries reset per token") was
   never implemented; `pool_next` now clears the entry it hands out, which is
   safe because every call starts a *new* attribute. Regression test:
   `html.element.attributes`.

Two further "defects" turned out to be test mistakes and are recorded because
they document real semantics:

2. A handle is **inert identity, not a capability**: two documents can issue
   the same `(node id, generation)` pair, and the document id in the triple is
   what separates them. Overwriting a handle's document id therefore produces
   a handle for whatever node occupies that pair in the other document — not
   a rejected operation. Ownership compares the whole triple.
3. The builder implies the structural elements: a document that names only
   `html`/`body`/`p` still gets a `<head>`, so a node count of 6 (document,
   html, implied head, body, p, text) is correct for that source.

---

## 10. What this phase deliberately does not do

Not implemented, with the seam kept clean: the Element DOM surface
(`getElementById`, `querySelector`, `classList`), custom elements,
foreign-content parsing, CSS/selectors/cascade, layout, paint, events,
JavaScript, and any browser-origin machinery (no cookies, CORS, IndexedDB,
`localStorage`, session or navigation security). Attribute mutation exists as
the model's own verbs (8.6), not as a DOM API.

Text nodes left this list in **8.7**, which gave them their own model over the
same node kind — see [`TextModel.md`](TextModel.md). An element's children are
still a mixed list of element and text nodes; 8.7 changed how the text half is
read and written, not how it is stored or owned. 8.8 then made navigating that
list a boundary in its own right — see [`Traversal.md`](Traversal.md) — so
consumer code asks the traversal surface where a node sits rather than reading
links or indices itself.

Roboticela's application identity and capability model is implemented
elsewhere; nothing here invents a second one.
