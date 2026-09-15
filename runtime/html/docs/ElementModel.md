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

An element's identity is a `robo_html_node_handle_t` — `(document id, node id,
generation)`. There is no `robo_html_element_id_t`, no element table, and no
address-based identity anywhere in the public surface.

| Question | Call |
|---|---|
| Is this handle a live element? | `robo_html_element_is_element` |
| Whose element is it? | `robo_html_node_handle_document`, `robo_html_document_owns_node` |
| Is this handle stale? | `robo_html_node_handle_is_stale` |
| Is it the element I mean? | `robo_html_element_is_named` (ASCII case-insensitive) |

`is_element` answers `0` for NULL, ill-formed, stale and non-element handles
alike, so a caller may test before treating. The accessors that return data
instead of a verdict (`view_get`, `attribute_count`, `parent`) return
`ROBO_NOT_FOUND` for a non-element and `ROBO_INVALID_ARGUMENT` for a NULL
argument, which is how a caller tells "wrong kind" from "wrong call".

---

## 2. Namespace

Namespaces are stored per element, not folded into the name string:

```c
ROBO_HTML_NAMESPACE_NONE / HTML / SVG / MATHML
```

- `robo_html_namespace_url` gives the URI (`http://www.w3.org/1999/xhtml`, …).
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

`robo_html_element_classify(namespace, local_name)` is a pure function of
identity, computed at read time and stored nowhere. It is deliberately **not**
the HTML content-category specification; it is the small vocabulary a renderer
or a later phase needs for one decision:

`GENERIC`, `ROOT`, `METADATA`, `SECTION`, `HEADING`, `PARAGRAPH`, `TEXT`,
`REPLACED`, `CONTROL`.

- Anything unrecognised is `GENERIC` — never an error. Custom elements, SVG,
  MathML and future elements are therefore safe to classify.
- A non-HTML namespace always classifies as `GENERIC`, because an SVG `<a>`
  is not an HTML `<a>` and pretending otherwise would be a lie.
- The whole vocabulary is one reviewable table; classification is not on a
  painted-pixel path, so a linear scan costs nothing measurable.

---

## 4. State

`robo_html_element_view_t.state` is a mask of three bits, each derived rather
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
robo_html_element_create(document_id, &info, &out, diag)
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

`robo_html_element_create_info_t` is a descriptor rather than nine positional
arguments, because this is the seam the attribute model (8.6) and the DOM's
`createElement` both extend.

### Names

Stored element and attribute names are **lowercase ASCII**. The tokenizer
already lowercases parser input; creation lowercases what it is given, so the
invariant holds for the DOM seam too.

The parser path deliberately does **not** validate the character set: a
malformed tag name must be tolerated, not rejected, or a stray byte in a tag
would abort a parse. `robo_html_element_name_validate` /
`robo_html_element_name_normalize` remain the strict policy helpers for names
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
| `robo_html_element_attribute_count` | how many |
| `robo_html_element_attribute_at` | the *index*'th, in stored order, duplicates preserved |
| `robo_html_element_find_attribute` | the first named (case-insensitive) |
| `robo_html_element_has_attribute` | presence only |
| `robo_html_element_attribute_value` | the value, or `NULL` when absent |
| `robo_html_element_set_attribute` | rewrite, collapse duplicates, append when new |
| `robo_html_element_add_attribute` | append unconditionally (the parser's verb) |
| `robo_html_element_remove_attribute` | drop every match; absent is a no-op |

The type crossing the boundary is `robo_html_attribute_t`, staged by the
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
`create_element` fills a descriptor and calls `robo_html_element_create`:

```c
info.parent          = <handle of the parent node>
info.origin          = ROBO_HTML_NODE_ORIGIN_PARSER
info.namespace_id    = ROBO_HTML_NAMESPACE_HTML
info.local_name      = <tokenizer tag name>
info.attributes      = <staged robo_html_attribute_t>
info.self_closing    = <"/" seen>
```

Open-element-stack behaviour is unchanged and still the builder's decision
(void and self-closing elements are never pushed), so start/end-tag handling,
insertion modes and recovery are the 8.3 implementation, not a rewrite.

---

## 8. The demo renderer

`RoboDemoHtml.c` walks the real document tree and now identifies elements
through the element model rather than a raw name field:

- `robo_html_element_view_get` **is** the filter — it fails for text, comment,
  doctype and stale handles, which is exactly what the walk must skip;
- names compare through `robo_html_element_is_named`;
- roles come from `robo_html_element_classification`, so `<h1>` … `<h6>` all
  paint as headings (previously only `<h1>` did) and nothing else changed;
- non-HTML-namespace elements are not painted.

The painted subset is otherwise the same subset, so the picture is stable:
host `blocks=4 supported=8 ignored=0` and QEMU `ROBO_QEMU_HTML_BLOCKS=4`
`SUPPORTED=8` `IGNORED=0`, unchanged from the 8.4 checkpoint.

---

## 9. Defects fixed in this phase

1. **Pooled attribute buffers were reused without being reset.**
   `robo_html_token_pool_next` returned `entries[count++]` and only
   `robo_html_token_pool_clear` rewound the cursor — it never cleared the
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
(`getElementById`, `querySelector`, `classList`), text nodes (8.7), custom
elements, foreign-content parsing, CSS/selectors/cascade, layout, paint,
events, JavaScript, and any browser-origin machinery (no cookies, CORS,
IndexedDB, `localStorage`, session or navigation security). Attribute
mutation exists as the model's own verbs (8.6), not as a DOM API.

Roboticela's application identity and capability model is implemented
elsewhere; nothing here invents a second one.
