# RoboHTML Architecture (Phases 8.1–8.2)

This document is the normative design record for Goals 8.1 and 8.2. Later
phases (8.3 tree builder, DOM, CSS, layout, paint) must extend it, not
contradict it. Section 9 covers the tokenizer.

## 1. Module boundary and allowed dependencies

```text
RoboHTML
    depends on →  RoboBase, RoboMem, RoboContainers (headers only today)
    consumed by → RoboCSS / RoboLayout / RoboPaint / RoboRender (future)
    parallel to → RoboDOM (future module; boundary stubbed in RoboHTMLDom.h)
```

- RoboHTML is a **lower module**: no includes of graphics/window/display or
  any future CSS/layout/paint module. Enforced by
  `Assert-RoboHTMLChromiumIndependence` in `runtime/Tools/Build.ps1`.
- Chromium is not a node in this graph (`CHROMIUM_REFERENCE.md`).

## 2. Identity model

- `robo_html_document_id_t` / `robo_html_node_id_t` are runtime-owned
  `uint64_t` tokens; `0` is always invalid ("none").
- Document ids increase monotonically and are **never reused**.
- Node handles carry `(document_id, node_id, generation)`. The generation
  guards the document's registry slots: after teardown, an application-held
  handle resolves as **stale** (`ROBO_NOT_FOUND` +
  `robo_html_node_handle_is_stale`), never as a newer node.

## 3. Ownership and lifetime

| Object | Owner | Contract |
|---|---|---|
| Document | RoboHTML module (fixed slot table) | Created via `robo_html_document_create`; destroyed via `..._destroy` or module shutdown |
| Nodes | Owning document | Registered via `robo_html_document_register_node`; retired wholesale at document teardown |
| Parser | Caller (heap object) | Leased exclusively to one document; never outlives it — document destroy detaches the lease |
| Source bytes | Application | Borrowed view only; RoboHTML copies metadata, never bytes |
| `robo_html_diag_t` | Caller | Optional; result codes are authoritative |

## 4. Lifecycle state machine

```text
UNINITIALIZED → CREATED → LOADING → READY → DESTROYING → DESTROYED
                    │          │    │
                    │          │    └──→ LOADING (replacement source)
                    │          └──→ FAILED (sticky)
                    └──→ FAILED / DESTROYING
```

- `CREATED` = empty, valid document (root node registered).
- Empty source attach completes immediately (`LOADING → READY`);
  non-empty sources stay `LOADING` until parsing (8.2/8.3) marks ready.
- `FAILED` is sticky until destroy; destroy is the only exit.
- Invalid transitions return `ROBO_INVALID_ARGUMENT`; `DESTROYED → DESTROYED`
  is the only idempotent edge.
- Content operations require `CREATED/LOADING/READY`; source attach requires
  `CREATED/READY`.

## 5. Source / encoding / parser pipeline

```text
application bytes ──▶ robo_html_source_t (borrow)
                        │ attach (document or parser path)
                        ▼
        document metadata (kind/length/name copied)
                        │
        encoding boundary (declared label → state; decode later)
                        │
        parser lease (FEEDING; COMPLETE vs INCREMENTAL mode)
                        │
        future tokenizer (8.2) → tree builder (8.3) → node registry
```

- One writer at a time: while a parser lease holds the source, direct
  document attachment is refused (`BUSY`); while `LOADING`, attach is
  refused (`BUSY`).
- `INCREMENTAL`/`STREAM` sources select incremental parser mode for 8.2+.

## 6. Integration hooks

- **Mutation hooks** (`RoboHTMLDom.h`): single document-wide hook, fires on
  node registration (lifecycle events modeled). Foundation only.
- **Client hooks** (`RoboHTMLClient.h`): `on_document_lifecycle` and
  `on_invalidate` are the *only* surface future CSS/layout/paint/RoboRender
  layers use. RoboHTML never pulls graphics/window headers.
- **Resource boundary** (`RoboHTMLResource.h`): a future loader (RoboNetwork
  or application) delegates acquisition via `robo_html_resource_begin/end/
  cancel`; direct attach is refused while acquisition is outstanding. No
  HTTP/DNS/TLS/fetch code exists in RoboHTML.

## 7. Diagnostics

`robo_result_t` stays authoritative; `robo_html_diag_t` refines failures
with a `robo_html_diagnostic_domain_t` (document / lifecycle / node /
source / encoding / parser / resource / client / mutation / render) and a
message. No global error state, no browser-style legacy error behavior.

## 8. What Phase 8.1 deliberately does not do

No tokenizer states, no character references, no parsing algorithms, no
tree construction, no element/attribute storage, no CSS, no layout, no
paint, no JavaScript, no networking, no storage, no cookies/CORS, no
IndexedDB/localStorage, no browser-origin security machinery.

## 9. Tokenizer (Phase 8.2)

```text
robo_html_source_t (borrowed bytes)
        │
        ▼
robo_html_tokenizer_next()          state machine (RoboHTMLTokenizerStates.h)
        │  ── DATA / tag / attribute / comment / doctype / reference states
        │  ── raw content families via switch_state (8.3 drives this)
        ▼
robo_html_token_t   CHARACTER | START_TAG | END_TAG | COMMENT | DOCTYPE | EOF
        │
        ▼
tree builder (8.3) → document node registry (see §11)
```

### Token model and ownership

- Six token kinds; each carries `robo_html_token_pos_t` (byte offset, 1-based
  line, 1-based column in code points) for the token's first source byte.
- A token **borrows** tokenizer-owned storage (tag name, attribute array,
  decoded characters, comment payload, doctype fields) and is valid only
  until the next `next()` / `reset()` / tokenizer destroy. The attribute
  array and the character buffer are tokenizer-owned so no pool internals
  ever escape.
- Callers that must keep data use `robo_html_token_copy_tag_name` or
  `robo_html_token_copy_attr`; those copies are caller-owned and outlive the
  tokenizer.
- All token text is UTF-8. Ill-formed UTF-8 is emitted as U+FFFD (RoboUnicode
  REPLACE mode) with a diagnostic — never read out of bounds, never fatal.

### Error tolerance

Only NULL arguments, invalid state, and allocation failure return errors.
Malformed HTML recovers and continues: unterminated tags at EOF emit the EOF
token (with the partial construct dropped and a diagnostic), unclosed
comments emit the partial comment, `<?` and `<![CDATA[` become bogus comments,
and diagnostics are bounded per tokenizer
(`ROBO_HTML_TOKENIZER_MAX_DIAGNOSTICS`).

### Character references

Named references are matched with the HTML longest-match rule against the
catalogue in `RoboHTMLCharRefData.c`; the catalogue is a curated,
category-grouped set whose order is **not** significant, so the engine picks
the winner by name length (`&amp;` beats `&amp` regardless of table position).
Numeric references follow the standard value mapping: 0x00 / out-of-range /
surrogates → U+FFFD, 0x80–0x9F → the legacy remap table, and C0 controls are
parse errors that keep their code point. Inside attribute values the legacy
semicolonless rule applies (`&ampx=1` stays literal).

### Raw content families

The tokenizer is data-driven for `DATA`, but `RAWTEXT`, `RCDATA`, script data
and `PLAINTEXT` are entered only through
`robo_html_tokenizer_switch_state()`, validated to those five entry points.
The expected end-tag name is seeded from the last emitted start tag (the
`<textarea> … </textarea>` shape 8.3 will use) or learned from a leading
start tag; `RCDATA` decodes references, the others stay byte-literal, and
only the matching end tag closes the region.

## 10. What the tokenizer deliberately does not do

The tokenizer constructs nothing: no tree, no nodes, no element semantics (an
end tag does not close anything), no CSS, layout, paint, JavaScript,
networking, filesystem, storage, navigation, cookies or CORS. It also does not
accept incremental/chunked input yet — the machine is written so a source
feeding in chunks plugs in without redesign. Turning tokens into structure is
the tree builder's job, and it consumes this stream without the tokenizer
knowing anything about documents.

## 11. Phase 8.3 — tree builder

The tree builder consumes the token stream and constructs the document tree
that 8.1 reserved room for:

```
source (8.1) → tokenizer (8.2) → tree builder (8.3) → document tree
                                                       ├── nodes: Document,
                                                       │   DocumentType,
                                                       │   Element, Text,
                                                       │   Comment
                                                       ├── parent/child/sibling
                                                       │   links as node ids
                                                       └── payload arena:
                                                           names, attributes,
                                                           text, identifiers
```

- **Ownership is unchanged from 8.1:** the document owns every node and every
  byte of payload. The builder holds ids, never registry pointers, and only
  ever creates nodes through `robo_html_document_register_node` — there is no
  second ownership model.
- **Modes and the open-element stack** implement the foundational insertion
  modes (`initial` … `in-body`, `after-body`, `after-after-body`, plus a text
  mode for RCDATA/RAWTEXT/script/plaintext), so later goals extend the machine
  instead of replacing it.
- **Implied structure:** a missing `<html>`, `<head>` or `<body>` is created,
  which is why an empty document is not an empty tree.
- **Recovery is bounded and non-fatal:** diagnostics for everything
  recoverable, a hard stop only at the node ceiling (which truncates and still
  reports success), and a `ROBO_OK`-with-`truncated` contract callers can
  detect.
- **Mutation retires identities safely:** re-building a document clears the
  tree but keeps node ids and generations moving forward, so a stale handle
  from the previous tree can never alias a new node.

Full design, limits and the assembled/deliberately-absent rule list:
[`TreeBuilder.md`](TreeBuilder.md).
