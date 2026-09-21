# RoboHTML Basic HTML Elements (Phase 8.11)

This document is the design record for Phase 8.11, the phase that gives an
element a **type**. It is normative for `RoboHTMLTag.h` / `RoboHTMLTag.c` and
for the element-side accessors that read it (`robo_html_element_tag`,
`robo_html_element_is_tag`).

Everything here builds on the phases before it and contradicts none of them:
a tag is derived from the `(namespace, name)` an 8.5 element already has, so
no payload changed, no arena grew, and every earlier contract — attribute
lifecycle, text bytes, traversal order, mutation rules, lifecycle states —
holds exactly as it did.

## 1. The problem this phase solves

An element has had a *name* since Phase 8.5. A name is bytes, and a type is a
symbol, and the difference is not cosmetic — before this phase, every consumer
that wanted to know "is this a heading?" compared strings:

- Phase 8.5's element model kept a 102-row table mapping a name to a coarse
  role, so `classify("div")` answered one question.
- Phase 8.3's tree builder kept its own 18-name list of void elements, so "is
  `<br>` void?" was answered in a *second* place.
- A consumer wanting anything finer than a role — is this a list? is it a list
  *item*? is it a block or inline? — wrote its own comparison against a string
  literal, or used `robo_html_element_is_named(element, "button")`.

Two tables plus per-consumer string literals is three answers to one question,
and they drift. Phase 8.11 replaces all of them with one table.

## 2. What the phase introduces

```text
RoboHTMLTag.h (8.11)
├── identity    robo_html_tag_t — one enumerator per recognised element
├── lookup      robo_html_tag_from_name(namespace, local_name)
│               robo_html_tag_name(tag) · robo_html_tag_class(tag)
│               robo_html_tag_flags(tag) · robo_html_tag_at(index)
├── predicates  is_void · is_root · is_document_structure · is_metadata
│               is_sectioning · is_region · is_heading · is_list
│               is_list_item · is_text_level
└── flags       ROBO_HTML_TAG_FLAG_* (the bitmask behind the predicates)

RoboHTMLElement.h (extends 8.5)
├── view.tag              the type, next to the role
├── robo_html_element_tag(handle)
└── robo_html_element_is_tag(handle, tag)
```

The vocabulary covers 105 elements: the 32 this phase names as *basic HTML
elements* (document structure, structural content, headings, lists, inline
semantics, generic regions) plus every element name the 8.5 role table already
recognised, promoted to real identity — decided deliberately, see §5.

## 3. Identity is derived, never stored

`robo_html_element_tag` is a pure function of the element's namespace and
local name, exactly as `robo_html_element_classify` was. Nothing is added to a
node payload, so:

- a name that arrived before this phase classifies identically to one that
  arrives after it;
- the arena does not grow and no new failure mode (out of memory during
  lookup) exists;
- there is no cached-but-stale tag to invalidate when a document is re-parsed,
  reset or mutated — the answer is computed from what the node *is*.

The cost is a bounded linear scan at read time: 105 case-insensitive name
comparisons, in the same shape and at the same cost the 8.5 classification
lookup already had, and it runs when an element is inspected rather than once
per painted pixel. Phase 8.12+ can add an index if a profile ever asks for one;
correctness first.

`robo_html_tag_name`, `robo_html_tag_class` and `robo_html_tag_flags` *are*
constant-time, because the table is declared in enum order and the enumerators
are dense from 1 — the row for a tag is the row at `tag - 1`. That is a
load-bearing property, not an accident, so `html.tag.table_is_dense` asserts
row order, ids, round-trip, name uniqueness and a defined role per row. If the
table and the enum ever disagree, that test is the one that says so, and every
other assertion in the phase is then meaningless rather than misleading.

## 4. One table, three facts per row

A row is everything the runtime knows about an element type:

```c
{ROBO_HTML_TAG_DIV, "div", ROBO_HTML_ELEMENT_CLASS_SECTION,
 ROBO_HTML_TAG_FLAG_NONE},
```

- **name** — the canonical spellings. Lookup is ASCII case-insensitive in both
  directions, so a caller may pass `DIV` or `div`; storage and comparison are
  one rule the model owns, and every caller agrees with it.
- **role** — the coarse role from Phase 8.5, unchanged for every name that was
  already classified (asserted, not assumed: `html.tag.classify_agrees_with_
  vocabulary` compares `robo_html_element_classify` against `robo_html_tag_class`
  for every tag, in both cases). `robo_html_element_classify` is now a
  delegation, so the role and the identity cannot disagree.
- **flags** — the intrinsic facts: void, root, document structure, metadata,
  sectioning content, page region, heading, list, list item, text-level. The
  named predicates are the readable form; the mask is exposed because a future
  consumer may need a grouping that is not one of them, and re-deriving it from
  names would put the vocabulary back in a second place.

`html.tag.predicates_match_the_mask` walks the whole table and asserts each
predicate agrees with its bit, so a predicate cannot quietly become something
other than what it says.

## 5. The vocabulary is the full one, on purpose

The phase could have defined a small enum for the ~32 elements it names and
left the 8.5 role table alone. That would have been two tables again: one for
"which elements exist" and one for "what role does it play", with the second
silently authoritative for everything the first did not mention. So the role
table *moved* into the tag rows and was deleted, and every name it recognised
became a known tag with its role intact.

The one visible consequence: `<col>`, `<frame>` and `<keygen>` are now
*recognised tags with a GENERIC role*. Before this phase they were unclassified
names; they are still GENERIC (their flags say VOID, which is what the parser
needs to know about them), and they are now distinguishable from a custom
element — which is the point of having an identity in the first place.

## 6. What the parser now reads from here — and what it does not

Void-ness and heading-ness moved, because both are facts about the *element
type* rather than parsing decisions: `<br>` has no children because `br` is
void and the parser merely obeys, and "which elements are headings" has one
answer whether the asker is the parser, the DOM or a renderer. The tree
builder's `is_void_element` and `is_heading_name` now ask this table, and 18
lines of duplicated truth were deleted rather than kept in step.

Three parser tables deliberately stayed where they were:

- the start-tag list that closes an open `<p>`,
- the head-content list,
- the text-family table (`RCDATA` / `RAWTEXT` / script / `PLAINTEXT`).

Each is parse *policy* with contextual entries — `noscript` is head content in
one mode and a paragraph in another; which tags close a `<p>` is a rule of the
algorithm, not a property of an element — and moving parser policy into an
element-identity table would confuse the two things the phase exists to keep
apart. `is_p_closing_start_tag`, `is_head_content_element` and
`text_mode_for_element` are therefore still name tables, and are still the
right place for that knowledge.

## 7. Unknown is a state, not an error

`ROBO_HTML_TAG_UNKNOWN` is the absence of an identity: a custom element
(`<my-widget>`), an element from a future version of HTML, or any name in a
non-HTML namespace. An unknown element is a *fully working* element — it keeps
its name, attributes, children, text, traversal and mutation behaviour, and
`html.elements.unknown_element` asserts all of it. What it does not have is a
type, and nothing coerces it into one.

Two consequences worth stating explicitly, because both are easy to get wrong:

- **`is_tag(element, UNKNOWN)` is answerable** and means "this element has no
  recognised type", which is a real question. It is *not* a shared identity: two
  custom elements both satisfy it and are still distinct elements, and neither
  satisfies `is_tag(element, DIV)`.
- **A non-element handle satisfies no tag, including UNKNOWN.** A text node, the
  document node, a stale handle and NULL all answer `UNKNOWN` to
  `robo_html_element_tag` (there is no tag to report) but `false` to
  `is_tag(..., UNKNOWN)`, because "no recognised identity" is a statement about
  an element and cannot be true of something that is not one.
- **An undefined enumerator is refused, not treated as UNKNOWN.** A bogus
  `(robo_html_tag_t)9999` matches nothing, including custom elements; otherwise
  a mistyped tag would silently match every unrecognised element in a document.

Namespaces are respected: `robo_html_tag_from_name` answers `UNKNOWN` for
anything outside the HTML namespace, because an SVG `<a>` is not an HTML `<a>`
and saying otherwise would be a lie. The field exists so a later foreign-content
phase fills it in rather than changing the type.

## 8. The element-side seam

```c
robo_html_tag_t robo_html_element_tag(const robo_html_node_handle_t *element);
int robo_html_element_is_tag(const robo_html_node_handle_t *element,
                             robo_html_tag_t tag);
```

Identity is read through a handle, like every other accessor, so nothing here
exposes the registry, the payload arena or the node-entry layout. `is_tag` is
what a consumer writes instead of `robo_html_element_is_named(element, "div")`:
the caller names a symbol, the runtime owns the spelling, and a misspelling is
a compile error rather than a test that quietly returns false.

`robo_html_element_is_named` stays, and stays a *name* comparison — it answers
"does this element's name read as `name`?", which is true for a custom element
too. The two are different questions and the headers say so; a name test that
silently went through the tag table would answer `false` for any name the
vocabulary does not know, which would be a behaviour change hidden inside a
"plain" accessor.

## 9. Not in this phase

No CSS, selectors, layout, paint, JavaScript, events, custom-element upgrade,
foreign-content namespace switching, accessibility mapping, or exhaustive
element vocabulary. A consumer asks for a tag identity by *symbol*
(`ROBO_HTML_TAG_BUTTON`, `ROBO_HTML_TAG_TITLE`) rather than by comparing a name
string, and that is the whole of what this phase offers one. Nothing here styles
anything.

## 10. Test coverage

| Suite | Test |
|---|---|
| `SuiteRoboHTMLTag.c` | `html.tag.table_is_dense` — row order, dense ids, unique lowercase names, per-row defined role, name round-trip |
| | `html.tag.unknown_and_undefined` — UNKNOWN and out-of-range enumerators; `at()` bounds |
| | `html.tag.lookup_by_name` — case-insensitivity, unknown names, non-HTML namespaces |
| | `html.tag.name_and_role` — canonical spellings and the role column |
| | `html.tag.classify_agrees_with_vocabulary` — `classify` == tag role for every tag, both cases |
| | `html.tag.void` · `headings` · `lists` · `document_structure_and_metadata` · `sectioning_and_regions` · `text_level` — each predicate against its elements |
| | `html.tag.predicates_match_the_mask` — every predicate against its flag, for every tag |
| | `html.tag.parser_identity` — every element in a parsed document agrees with the name it stores |
| | `html.tag.uppercase_source_keeps_identity` — `<DIV CLASS="…">` → `div` + SECTION + attribute intact |
| | `html.tag.custom_element_has_no_identity` — unknown type, working element, `is_tag(UNKNOWN)` |
| | `html.tag.non_element_handles_have_no_identity` — text, document, NULL, stale |
| | `html.tag.undefined_tag_query_is_not_unknown` |
| | `html.tag.identity_survives_mutation` — a moved element keeps its type |
| `SuiteRoboHTMLElements.c` | `html.elements.*` — the 32 element types through the parser (`document_structure_`, `structural_content_`, `heading_`, `list_`, `inline_semantic_`, `generic_semantic_elements`), `creation_seam` (every type created through the element seam), `full_document_tree`, `void_br`, `attributes_on_new_elements`, `text_children`, `pre_preserves_whitespace`, `unicode_text`, `unicode_through_parser`, `traversal_order`, `mutation`, `lifecycle`, `malformed_markup` (23 inputs), `unknown_element`, `tree_dump_shows_types`, `dump_reflects_mutation` |
| `desktop/tests/SuiteRoboDesktopScene.c` | `desktop.scene.*` — the checkpoint document reaches a *frame* through the real pipeline, so the element vocabulary is exercised where it is actually consumed |

## 11. Deliberate boundaries worth remembering

- **Node creation stays gated.** Element creation through
  `robo_html_element_create` is a construction operation, so on a *finalized*
  document it is `ROBO_ERROR` — the boundary 8.9 pinned. The element suite
  therefore exercises creation on an unfinalized document and mutation by
  rearranging parsed nodes, which is also what a consumer does to a loaded
  page.
- **The debug dump was not reformatted.** It already names every element by its
  tag, which *is* its type in this architecture, so the phase adds coverage
  that every supported type appears rather than a new line format that would
  invalidate six suites to say the same thing.
- **`<span>` is real DOM with no style of its own.** An inline element is a
  typed element like any other, and what it looks like is a layout and style
  question — which is where it is answered, rather than in the parser or in the
  element model.
