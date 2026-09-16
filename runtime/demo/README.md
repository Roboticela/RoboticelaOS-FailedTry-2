# RoboDemo — visual checkpoint

The throwaway adapter that makes the finished foundation **visible**: HTML
source goes through the real RoboHTML tokenizer and the real 8.3 tree builder,
and the resulting document tree is walked into a tiny block model that
RoboGraphics paints into a RoboWindow — on the host OS and in QEMU.

```
HTML source
   → RoboHTML source boundary (8.1)
   → RoboHTML tokenizer (8.2)
   → RoboHTML tree builder (8.3)        ← real document tree
   → RoboDemo walk (this module)        ← temporary: subset → block list
   → RoboGraphics (6.x) → RoboWindow present (7.14)
   → host Win32 window / QEMU display
```

One paint path feeds both backends: there is no QEMU-only renderer and no
second graphics stack.

## What it is not

- Not a DOM, not layout, not CSS, not paint architecture.
- Not a browser: no navigation, networking, storage, cookies or CORS.
- Not depended on by anything: `RoboDemo` depends on RoboHTML + RoboGraphics,
  never the other way around (enforced by the *RoboDemo isolation* build scan).

Phases 9/10/11 replace the walk, the layout constants and the painting. The
document model underneath stays.

## Files

| Path | Role |
|---|---|
| `runtime/Include/RoboDemo/` | Public demo API (`RoboDemoHtml.h`, `RoboDemoFont.h`, `RoboDemoVersion.h`, `RoboDemoHost.h`) |
| `runtime/Source/RoboDemo/` | Implementation (`RoboDemoHtml.c`, `RoboDemoFont.c`) |
| `runtime/Source/RoboDemo/platform/host/` | Real Win32 window runner (`--demo-html`) |
| `runtime/demo/assets/Demo.html` | The demo document (byte-identical to the compiled-in copy; 1397 bytes) |
| `runtime/demo/tests/SuiteRoboDemoHtml.c` | `demo.*` suites |
| `runtime/demo/docs/VisualCheckpoint.md` | How to build, run and inspect it |

## Supported subset

**Painted blocks.** Any heading (`h1` … `h6`) and any `p`; a `div` that holds
text and nothing else; `button`; `a` with an `href`; `img` with a decoded
resource; and the form controls — a `label` that wraps nothing (a caption),
plus `input`, `textarea` and `select`.

**Structure** (recognised, counted, descended into, not blocks of their own):
`html`, `head`, `body`, `title` (it becomes the window caption), `form`,
`fieldset`, `legend`, `option`, `optgroup`, and a `label` that wraps a control.

Everything else is counted as unsupported markup and skipped — and because the
walk descends through unsupported elements, supported markup nested inside them
is still painted. An element carrying `hidden` is recognised and counted but
paints nothing, subtree included. Elements outside the HTML namespace are not
painted. Text comes from the tree's text nodes, so character references
(`&amp;`, `&#65;`) arrive already decoded.

Blocks flow vertically with per-style type scale and spacing, and there is no
text wrapping (wrapping is real layout work, Phase 10). The button reacts to
hover and press through the existing RoboWindow mouse events; a link can be
hit-tested and activated; and a form control reports the state the RoboHTML form
model holds rather than a value re-derived from the markup — a declared value in
the text colour, a placeholder (or a disabled control) muted, a checked box
marked, and a select showing the option it is on.

## Run it

```powershell
# tests
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "demo."

# real host window (default: opens, renders, injects input, closes)
.\runtime\Out\RoboTest.host.exe --demo-html

# optional: keep it up longer / write a PNG of the rendered frame
.\runtime\Out\RoboTest.host.exe --demo-html --hold 3000 --shot .\demo.png
```

QEMU runs the same page through the guest program:

```powershell
scripts\robo test-qemu-display
```

Both paths print the same page summary
(`title=… blocks=… supported=… ignored=… diagnostics=…`) and the same pixel
probes, which is how the two backends are kept honest.
