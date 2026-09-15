# RoboHTML Attribute Model (Phase 8.6)

The attribute model is the runtime's **single** attribute representation. Before
this phase, attributes existed twice: the tokenizer staged
`robo_html_token_attr_t`, the tree builder translated them into a
payload table, and readers walked that table with raw byte offsets. Now there
is one type — `robo_html_attribute_t` — that the tokenizer stages, the tree
builder stores, an element owns, the readers return, and the mutation API
rewrites.

```
HTML source
   ↓  tokenizer (8.2)
start tag + staged robo_html_attribute_t
   ↓  tree builder (8.3)
robo_html_element_create (8.5)  →  element payload (8.3 layout, 8.6 model)
   ↓  element
lookup / presence / iteration / add / set / remove
```

## The type

```c
typedef struct robo_html_attribute {
    const char *name;                  /* borrowed, lowercase ASCII */
    size_t      name_length;
    const char *value;                 /* borrowed; "" for <input disabled> */
    size_t      value_length;
    robo_html_namespace_t namespace_id;/* NONE in this phase */
} robo_html_attribute_t;
```

An attribute is a **value snapshot, not a handle**. `name` and `value` point
into the owning document's payload arena, so they are valid until the next
mutation or rebuild on that document, or until it is destroyed — exactly the
contract node views already have. There is nothing to go stale, and callers
that need to keep bytes copy them. `robo_html_attribute_init` produces the
absent state and is safe on `NULL`.

`namespace_id` is the attribute *name's* namespace, always
`ROBO_HTML_NAMESPACE_NONE` here: an attribute's identity is
(namespace, local name) the way an element's is, so the foreign-content phase
fills the field in rather than changing the type.

## Stored form

The payload layout (unchanged since 8.3) is the encoder for that type:

```
ELEMENT   name '\0'
          then attr_count × ([u32 name_len][u32 value_len] name '\0' value '\0')
```

Lengths are the authority, so no walk ever scans for a terminator and a value
containing a NUL byte cannot desynchronise the reader. The writers accept
either a measured length or the convention that `0` means "measure with
`strlen`" — which is what lets a plain C string be handed over without
counting first, and is unambiguous because the genuinely empty string measures
to the same zero.

## Contracts

### Order

Attributes keep the order they were given. Parsing preserves source order;
`set` keeps the first match's position; `add` appends; `remove` preserves the
relative order of everything left. Iteration is `0 … count-1` through
`robo_html_element_attribute_at`, so it is deterministic for any input.

### Duplicates

The tokenizer does not fold repeated names and the model does not invent a
different answer: `<div id="one" id="two">` stores both, in order. Lookup
answers the **first** match — folding would hide what the source said, and the
later record stays reachable by index.

* `set` collapses every match to one, at the first match's position;
* `remove` drops every match;
* `add` appends unconditionally, and is the **only** verb that can introduce a
  duplicate. It exists so the parser and the mutation API share one encoder;
  an application adding attributes wants `set`.

### Case

Names are stored lowercase ASCII and lookup is ASCII case-insensitive, so
storage and lookup semantics are separate concerns: the stored form is cheap
to compare and a caller may pass any case. Bytes outside `A–Z` are never
folded, so a non-ASCII name compares byte-exactly. Preserving original case
for foreign content is a later concern — the parser never switches namespace
yet, so the tokenizer's lowercasing is lossless here.

### Presence

An attribute is present because a **record exists** for it, never because its
value is non-empty. That is what makes

```html
<input>                 attribute absent
<input disabled>        present, value ""
<input disabled="">     present, value ""
```

three distinguishable states:

```c
robo_html_element_has_attribute(e, "disabled");        /* 1 / 0        */
robo_html_element_attribute_value(e, "disabled", &n);  /* NULL, or "" */
```

`NULL` from `attribute_value` means absent; a non-`NULL` pointer with length 0
means present and empty. `remove_attribute` deliberately does *not* double as
a presence test: removing an absent name is a successful no-op, matching
`removeAttribute`.

### Names on the way in

Mutation validates the name the way an application-supplied name must be
validated (`robo_html_element_name_normalize`: non-empty, `[A-Za-z0-9_-]`,
≤ `ROBO_HTML_NAME_MAX`) and lowercases it, so storage and lookup agree. The
**parser path does not validate**, because tolerating a malformed name is
required behaviour rather than an error — a stray byte must not abort a parse.

### Bounds

One element holds at most `ROBO_HTML_MAX_ATTRS_PER_ELEMENT` attributes. Beyond
that the write is refused with `ROBO_NOT_SUPPORTED` and the element is left
exactly as it was — never truncated, never corrupted. A `remove` frees a slot
for a later `add`, and updating an existing name still works at the limit, so
reaching the cap does not make the element read-only.

## Mutation is a payload rewrite

An element payload is written once and never edited in place. A mutation
therefore:

1. **measures** what survives (so the cap is enforced before any byte is
   written, and a no-op `remove` costs nothing);
2. **builds** the replacement body in a zeroed scratch block sized from the
   old table plus the one entry a mutation can add, copying kept entries
   **verbatim** so nothing can drift;
3. **appends** it to the arena and adopts it on the node entry.

Consequences worth stating plainly:

* the superseded bytes stay in the arena as dead space until document
  teardown — the arena is append-only and freed as a unit, which is what keeps
  an element's payload describable by two integers however often it is
  rewritten;
* borrowed attribute bytes taken *before* a mutation are no longer meaningful
  afterwards (the arena may have moved) — re-read them;
* node identity, generation, children, sibling links and the element's place
  in the tree are untouched: a mutation is not a tree operation, so it stays
  available after the document is finalized (8.4 closes tree construction,
  not the DOM seam).

The scratch block is zeroed on purpose: every string in the layout ends at a
terminator the writer does not copy, so the buffer's contents are part of the
encoding. Uninitialised heap here gave names a stray byte and drifted every
following offset — a defect this phase found and now has a regression test
for (`html.attr.mutation_many`, `html.attr.teardown_with_many_mutations`).

## Debug output

`robo_html_tree_dump` reads attributes through the model, so the dump shows
exactly what a lookup returns, in stored order:

```
Document
  html
    body
      div id="main" class="container"
      input disabled=""
```

Presence with an empty value is visible as `=""`; an unknown/foreign namespace
prefixes the element name (`svg:rect`).

## Consumer

The visual demo adapter is the first consumer: it honours the real `hidden`
attribute — the element is recognised and counted but paints nothing, and its
subtree is not entered — precisely because `id`/`class` on the demo document
have no effect on the picture, and a rule with visible consequences is the
only way to show the seam works. `demo.tree.attribute_hidden` covers it.

## Not in this phase

Text nodes (8.7), the DOM surface (`NamedNodeMap`, `getElementById`,
`querySelector`, `classList`, `dataset`), CSS selectors/cascade/CSSOM, layout,
paint, JavaScript bindings, `MutationObserver`, Shadow DOM, Web Components,
SVG/MathML attribute semantics, foreign-content parsing, and complete HTML
conformance. Attribute *values* are data: nothing here resolves them as URLs,
styles, scripts, paths or commands.

## Tests

`html/tests/SuiteRoboHTMLAttribute.c` — 21 `html.attr.*` suites covering the
type, parser integration, stored order, presence vs empty, duplicates,
lookup, the four mutation verbs, validation, the attribute limit, Unicode
names and values, malformed input, the dump, ownership/lifetime, and rebuild
semantics. `demo/tests/SuiteRoboDemoHtml.c` adds
`demo.tree.attribute_hidden`.
