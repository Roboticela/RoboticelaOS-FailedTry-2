# RoboHTML Images (Phase 8.12)

This document is the design record for Phase 8.12. It is normative for
`RoboHTMLImage.h` / `RoboHTMLImage.c`, for the RoboGraphics decoder it builds
on (`graphics/docs/ImageDecode.md`), and for the demo adapter's image painting.

Phase 8.11 already gave `img` a tag identity and void behaviour, so the parser
was building `<img>` elements before this phase began. What was missing is
everything *after* the element: an `<img>` is only interesting because of the
resource its `src` names.

```text
<img src="Assets/Logo.png" alt="Roboticela" width="128" height="64">
     │
     ├── attributes   robo_html_element_image_attributes
     │      src       what to fetch
     │      alt       text, kept verbatim
     │      width / height   declared dimensions, parsed safely
     │
     └── resource     robo_html_image_t
            ├── state  NONE │ PENDING │ DECODED │ FAILED │ UNSUPPORTED
            └── pixels robo_gfx_image_t (RGBA8, RoboGraphics)
```

## 1. Bytes come through a seam, never through this module

Phase 8.1 drew a line: RoboHTML does not open files, resolve host names or
speak protocols. This phase keeps it. A caller installs a
`robo_html_image_resolver_t`; RoboHTML hands it the reference and receives
**borrowed bytes for the duration of the call**:

```c
typedef robo_result_t (*robo_html_image_fetch_fn)(
    void *userdata,
    const robo_html_image_request_t *request,
    const uint8_t **out_bytes,
    size_t *out_length,
    robo_html_diag_t *diag);
```

That single sentence — *borrowed for the duration of the call* — is what makes
the future network loader an **implementation of a resolver** rather than a
change to this model. Ownership of bytes never crosses the boundary, so a
resolver may serve them from static storage, a stack buffer, an arena or an
mmap with no lifetime contract to honour. The request also carries the `alt`
text and the document name/URL, because a loader that needs them should not
have to reach back into the DOM to get them.

The fetch result maps onto state, deterministically:

| Resolver returns | Element state |
|---|---|
| `ROBO_OK` + bytes | DECODED or UNSUPPORTED, depending on what the decoder says about the bytes |
| `ROBO_NOT_FOUND` | FAILED (the bytes you asked for are not here) |
| `ROBO_NOT_SUPPORTED` | UNSUPPORTED (this runtime will not fetch that) |
| `ROBO_BUSY` | PENDING (a fetch is outstanding) |

## 2. The one resolver this phase ships

`robo_html_image_assets_t` is an in-memory table of named byte blobs the
application registered. It is the *safe local/test mechanism*: it cannot touch
the filesystem, so there is no path to traverse and nothing to sandbox.

The scheme check is where policy is stated rather than implied. A reference
that names a scheme — `http:`, `file:`, `data:`, `//host/path` — is refused as
`UNSUPPORTED`, **not** reported as "not found". The two mean different things:

- `UNSUPPORTED` is *"this runtime will not fetch that"* — a policy statement,
  true regardless of whether the resource exists.
- `FAILED` is *"the bytes you asked for are not here"* — a fact about the
  world.

Conflating them would make a future network phase's work look like a bug fix.

Asset names must be non-empty and unique; a duplicate is `ROBO_INVALID_ARGUMENT`
at create and `ROBO_ALREADY_EXISTS` at add. Silent shadowing would make a
missing image look like a present one, which is exactly the class of bug that
wastes an afternoon.

## 3. Failure is state, not an exception

```text
NONE         no usable src: the attribute is absent or empty. Valid, not an error.
PENDING      src names something; no resolver has run for it yet.
DECODED      bytes fetched and decoded; pixels available.
FAILED       fetching or decoding failed — not found, malformed, truncated.
UNSUPPORTED  refused by policy or unimplemented — a scheme, or a format.
```

`robo_html_image_create_decoded` returns `ROBO_OK` with a state of FAILED or
UNSUPPORTED for bad bytes. That is deliberate: the object then exists precisely
so the caller has somewhere to read *why* from, via
`robo_html_image_failure`. `ROBO_INVALID_ARGUMENT` and `ROBO_OUT_OF_MEMORY`
mean the call could not be attempted at all — a different kind of answer.

The same reasoning applies at the element level. A missing asset, undecodable
bytes and a refused scheme all leave the DOM **exactly as it was** and move the
image's state. That is the only shape that keeps an `<img>` from being able to
break a document, and it is the decoder's safe-refusal policy one level up.

Note the states are about *the resource*, not the element. `<img>` with no
`src` is an element whose resource is NONE — a valid state and not an error,
and `robo_html_element_image_state` reports it without needing a resolver.
That last property is what makes the accessor usable from a renderer handed a
document it did not load.

## 4. Declared dimensions are parsed, not trusted

`robo_html_image_parse_dimension` implements the HTML attribute rule minus
percentages: optional surrounding ASCII whitespace, then digits, and nothing
else. No `px` suffix, no signs, no exponent, no overflow.

The attribute view splits two facts that are easy to collapse:

- `width_set` — "the attribute is present and non-empty",
- `width_valid` — "and it parsed as a dimension this phase accepts".

That split is what lets a caller distinguish `<img width>` from
`<img width="abc">` from `<img>` without guessing, and it is why an invalid
dimension is *reported* rather than quietly dropped. A value above
`ROBO_HTML_IMAGE_MAX_DECLARED_DIMENSION` is treated as invalid rather than
clamped, so a typo cannot silently resize anything.

CSS pixels, device pixels, DPI, percentage sizing and intrinsic sizing are all
out of scope — those belong to CSS and layout, and half-implementing them here
would be a behavioural claim this phase cannot back.

## 5. Attachment and teardown

Decoded resources live in a per-document table
(`robo_html_document_slot_t.images`) keyed by node handle. The design points:

- **One resource per element.** `robo_html_element_image_resolve` replaces any
  earlier resource, so a mutated `src` is honoured simply by calling it again.
  There is no reload scheduling in this phase.
- **The table is freed as a unit** in `robo_html_document_images_free`, called
  from both `clear_tree` and `destroy`, so a re-parse and a teardown both
  release their resources and neither can leak.
- **The images follow their own refcount.** A caller that retained a resource
  keeps a valid image after the document is torn down; the table's release is
  one reference, not a forced free. The payload arena is a different story —
  it is freed and re-bumped as a unit — and the attachment table deliberately
  does not live inside it for exactly this reason.
- **`detach` is idempotent** and touches nothing but the attachment: attributes
  and children are not modified.

## 6. Parser behaviour

`img` is void, so the phase needed no tokenizer or tree-builder change. The
suite pins the consequence that is easiest to get wrong: an `<img>` must not
consume what follows it.

```html
<div>
  <img src="Assets/Logo.png" alt="Example">
  <span>After image</span>
</div>
```

`html.image.mutation_and_traversal` asserts the `span` is a **sibling**, not a
child, and that document order is `div, img, span, Text("After image")`.
`html.image.parser_element` covers the raw element: tag identity, all four
attributes, void behaviour, parent and sibling links.

## 7. What the renderer does

The demo adapter gained `ROBO_DEMO_BLOCK_IMAGE`, which retains the element's
resource and paints the real decoded bitmap at one image pixel per surface
pixel — this adapter has no scaling. A `<div>` that holds text and nothing else
paints as a text block, so the visual checkpoint renders

```html
<h1>…</h1><p>…</p><div>…</div><img src="Assets/Logo.png" width="64" height="64">
```

and the regression test asks whether the frame **changed** after mutating the
DOM, rather than whether a fixed rectangle appeared. A recognised-but-unresolved
image is counted as unsupported markup, which keeps that limit stated instead of
latent, and it means a document with a broken `src` still renders everything
else.

## 8. Not in this phase

No CSS or CSS image properties, no layout or intrinsic sizing, no `srcset` or
`<picture>`, no responsive selection, no `load`/`error` events, no JavaScript
bindings, no accessibility tree, no image cache, no off-thread decoding, and no
network, file or `data:` access of any kind.

## 9. Test coverage

`SuiteRoboHTMLImage.c` (filter `html.image`):

| Test | What it pins |
|---|---|
| `html.image.parser_element` | tag identity, src/alt/width/height, void behaviour, parent and sibling links |
| `html.image.attributes` | present/absent/empty, arbitrary extra attributes preserved, alt verbatim |
| `html.image.dimensions` | valid values, whitespace, `px` suffix refused, empty, huge, non-negative, overflow |
| `html.image.alt_unicode` | ASCII, empty, Arabic, Persian, Urdu, Hindi, Bengali, CJK, emoji |
| `html.image.resolve_decoded` | real bytes through the asset resolver → DECODED, intrinsic size, pixels |
| `html.image.resolve_failures` | missing asset → FAILED, truncated bytes → FAILED, unsupported format → UNSUPPORTED |
| `html.image.resolve_schemes` | `http:`, `file:`, `data:`, `//host` refused as UNSUPPORTED, not FAILED |
| `html.image.resolve_without_source` | no `src`, empty `src`, unusable resolver — all NONE/PENDING, never a crash |
| `html.image.reresolve_replaces` | a mutated `src` is honoured by resolving again; the old resource is released |
| `html.image.detach` | detach leaves attributes and children untouched, is idempotent |
| `html.image.argument_safety` | NULL and non-element handles on every entry point |
| `html.image.document_teardown` | resources are released on `clear_tree` and on `destroy`; no leak, no use-after-free |
| `html.image.mutation_and_traversal` | append/insert/remove/replace an `img`, document order, the `span` stays a sibling |
| `html.image.debug_describe` | `robo_html_image_describe` output reflects real state |
| `html.image.capability_reported` | `robo_html_dom_query_capabilities().images == 1`, with `dom_nodes`/`events` still 0 — a consumer discovers the model the way it discovers every other one, and the flag stays a narrow claim |

`SuiteRoboDemoHtml.c` adds `demo.image.reaches_framebuffer`, the visual
regression: parse, advance the lifecycle, resolve a real PNG through the asset
resolver, render, mutate the DOM, render again, and assert the frame changed —
proved on pixels rather than on a draw call having been made.
