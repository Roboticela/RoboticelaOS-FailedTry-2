# RoboHTML

HTML document subsystem for Roboticela Runtime (**Phase 8.1 — architecture only**).

**Version:** 0.1.0 — `ROBO_HTML_API_FROZEN=0` (freezes after tokenizer/tree-builder land)

Public RoboHTML never depends on Chromium, and — per `DEPENDENCIES.md` — never
depends on RoboCSS / RoboLayout / RoboPaint / RoboRender / RoboWindow /
RoboGraphics / RoboDisplay. Those layers sit *above* RoboHTML and consume it.

## Phase 8.1 scope

Architecture only: module boundary, document model, node identity, parser
boundary, source/encoding/resource boundaries, lifecycle, diagnostics, and
integration hooks. **No tokenizer, no tree builder, no DOM behavior** — those
are Goals 8.2 / 8.3 and the DOM phase.

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
| `RoboHTMLResource.h` | Delegated acquisition slot (no I/O in RoboHTML) |
| `RoboHTMLClient.h` | CSS/layout/paint/render integration hooks |
| `RoboHTMLVersion.h` | Version pin |

## Tests

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "html."
```
