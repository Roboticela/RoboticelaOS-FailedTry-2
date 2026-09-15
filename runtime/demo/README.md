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
| `runtime/demo/assets/Demo.html` | The demo document (byte-identical to the compiled-in copy) |
| `runtime/demo/tests/SuiteRoboDemoHtml.c` | `demo.*` suites |
| `runtime/demo/docs/VisualCheckpoint.md` | How to build, run and inspect it |

## Supported subset

Recognised: `html`, `head`, `title`, `body`, `h1`, `p`, `button`.
Everything else is counted and skipped — and because the walk descends through
unsupported elements, supported markup nested inside them is still painted.
Text comes from the tree's text nodes, so character references (`&amp;`,
`&#65;`) arrive already decoded.

`<title>` becomes the window caption. `h1`, `p` and `button` become blocks:
one deterministic vertical flow with per-style type scale and spacing, and no
text wrapping (wrapping is real layout work, Phase 10). The button reacts to
hover and press through the existing RoboWindow mouse events.

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
