# RoboHTML

HTML document subsystem for Roboticela Runtime (**Phase 8.1 architecture +
Phase 8.2 tokenizer**).

**Version:** 0.2.0 — `ROBO_HTML_API_FROZEN=0` (freezes after the tree builder lands)

Public RoboHTML never depends on Chromium, and — per `DEPENDENCIES.md` — never
depends on RoboCSS / RoboLayout / RoboPaint / RoboRender / RoboWindow /
RoboGraphics / RoboDisplay. Those layers sit *above* RoboHTML and consume it.

## Scope

**8.1 (architecture):** module boundary, document model, node identity, parser
boundary, source/encoding/resource boundaries, lifecycle, diagnostics, and
integration hooks.

**8.2 (tokenizer):** HTML source → deterministic token stream (character /
start-tag / end-tag / comment / doctype / EOF), attributes, comments,
doctypes with public+system identifiers, character references (named /
decimal / hex, legacy semicolonless rules), an explicit ~74-state machine,
source positions, bounded diagnostics, error-tolerant recovery, and the
controlled raw-content switch entry (`RAWTEXT` / `RCDATA` / script data /
`PLAINTEXT`) that the tree builder will drive.

**No tree builder, no DOM behavior** — those are Goal 8.3 and the DOM phase.
The tokenizer is independent of the tree builder: it never constructs a tree,
never knows an element's meaning, and never touches CSS/layout/paint/JS/network.

## Layout

| Path | Role |
|---|---|
| `runtime/Include/RoboHTML/` | Public API (no OS headers, no Chromium) |
| `runtime/Source/RoboHTML/` | Platform-independent implementation |
| `runtime/html/tests/` | Suites (`html.*`, acceptance `html.p8.*`) |
| `runtime/html/docs/` | Design docs |

## Public headers

| Header | Boundary |
|---|---|
| `RoboHTML.h` | Module init/shutdown/version |
| `RoboHTMLTypes.h` | Document/node identity tokens, node kinds, diagnostic domains |
| `RoboHTMLDiagnostics.h` | `robo_html_diag_t` record (optional, caller-owned) |
| `RoboHTMLLifecycle.h` | Document lifecycle state machine |
| `RoboHTMLDocument.h` | Document create / attach / register / destroy |
| `RoboHTMLNode.h` | Node handles, generation-guarded registry |
| `RoboHTMLDom.h` | Future-DOM boundary, origins, mutation hooks, capabilities |
| `RoboHTMLElement.h` | Element/attribute name policy stubs |
| `RoboHTMLSource.h` | Source/input abstraction (borrowing view) |
| `RoboHTMLEncoding.h` | Encoding boundary (labels + state; no decoding yet) |
| `RoboHTMLParser.h` | Parser lease between source and future tokenizer |
| `RoboHTMLToken.h` | Token record: kinds, fields, positions, borrow contract |
| `RoboHTMLTokenizer.h` | Tokenizer create/next/reset/state/diagnostics API |
| `RoboHTMLTokenizerStates.h` | Explicit state enumerators + raw-content switch contract |
| `RoboHTMLResource.h` | Delegated acquisition slot (no I/O in RoboHTML) |
| `RoboHTMLClient.h` | CSS/layout/paint/render integration hooks |
| `RoboHTMLVersion.h` | Version pin |

## Tests

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "html."
```

| Filter | Coverage |
|---|---|
| `html.` (8.1) | Module, document, node identity, source, parser, hooks, acceptance |
| `html.token.*` | Tokens, tags, attributes, comments, doctype, positions, misuse, lifetime, malformed-corpus robustness |
| `html.charref.*` | Named / numeric / hex references, legacy rules, error mapping |
| `html.tokstate.*` | State model and the raw-content switch entry points |

Tokenizer ownership in one line: a returned `robo_html_token_t` **borrows**
tokenizer storage and is valid until the next `next()`/`reset()`/destroy;
`robo_html_token_copy_tag_name` / `robo_html_token_copy_attr` produce
caller-owned copies that survive both.
