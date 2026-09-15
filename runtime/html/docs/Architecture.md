# RoboHTML Architecture (Phase 8.1)

This document is the normative design record for Goal 8.1. Later phases
(8.2 tokenizer, 8.3 tree builder, DOM, CSS, layout, paint) must extend it,
not contradict it.

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
