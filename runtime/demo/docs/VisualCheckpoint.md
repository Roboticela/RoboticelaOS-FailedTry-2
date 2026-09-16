# Visual checkpoint — how to build, run and inspect

Developer guide for the RoboDemo visual vertical slice: the shortest honest
path from HTML source to pixels on a real screen, on both the host OS and QEMU.

## 1. What the checkpoint proves

| Layer | Real? |
|---|---|
| RoboHTML source/document boundary (8.1) | yes |
| RoboHTML tokenizer (8.2) | yes |
| RoboHTML tree builder (8.3) | yes — the walk reads a real document tree |
| RoboHTML document model (8.4) | yes — the walk reads the document the builder populated |
| RoboHTML element model (8.5) | yes — elements are identified through it, not by raw names |
| RoboHTML attribute model (8.6) | yes — the walk reads `hidden` from the real attribute set |
| RoboHTML text model (8.7) | yes — every painted string is read out of real Text nodes |
| RoboGraphics primitives, surface, PNG export (6.x) | yes |
| RoboWindow + RoboDisplay + host/QEMU backends (7.x) | yes |
| RoboWindow keyboard/mouse events (7.10/7.11) | yes — drives the button states |
| Demo block model, layout constants, painting | **no — temporary** |
| CSS / layout / paint / DOM / JavaScript | not implemented yet |

The demo document is not special-cased end to end: the same functions that
build a page for tests build it for the window.

## 2. Where the HTML lives

`runtime/demo/assets/Demo.html` is the editable copy. The compiled-in source
in `Source/RoboDemo/RoboDemoHtml.c` is kept byte-identical to it (the
`demo.source.asset_match` test fails if they drift), because the guest program
has no filesystem in this path.

```html
<!DOCTYPE html>
<html>
<head>
<title>RoboticelaOS Runtime</title>
</head>
<body>
<h1>RoboticelaOS Runtime</h1>
<p>Independent HTML rendering is working.</p>
<p>RoboGraphics + RoboWindow + RoboHTML</p>
<button>Demo Button</button>
</body>
</html>
```

## 3. How HTML becomes a picture

1. `robo_demo_page_create()` (or `…_from_source()`) describes the bytes through
   the 8.1 source boundary.
2. A temporary RoboHTML **document** is created and the **tree builder**
   (`robo_html_build_tree`) turns the source into a real document tree.
3. The page **walks that tree** in document order. An element is identified
   through the 8.5 **element model** — `robo_html_element_view_get` is the kind
   filter (it fails for text, comment, doctype and stale handles),
   `robo_html_element_is_named` does the comparison, and the role comes from
   `robo_html_element_classification`. For each element:
   - any `HEADING` (`h1` … `h6`) → heading block, text = the element's subtree
     text, whitespace-collapsed;
   - `PARAGRAPH` (`p`) → paragraph block, same text rule;
   - `button` → the button block;
   - `title` → the window caption;
   - `html` / `head` / `body` → recognised structure, counted;
   - anything else → counted as unsupported, and its subtree is still walked so
     supported markup nested inside it is painted. Non-HTML-namespace elements
     are not painted.

   The text comes from the 8.7 **text model**: `robo_html_node_subtree_text`
   concatenates the subtree's Text-node content in document order, so no
   painted string is hardcoded in the renderer — the window caption, the
   heading, the paragraphs and the button label all originate in the HTML.

   One rule is attribute-driven: an element carrying `hidden` (checked through
   the 8.6 attribute model) is recognised and counted but paints nothing, and
   its subtree is not entered. It is the walk's only use of the attribute set,
   and it is deliberately a rule with *visible* consequences — `id`/`class` on
   the demo document cannot change the picture, so they could not show that the
   seam works. Nothing in the shipped demo document carries `hidden`, so the
   checkpoint's output is unchanged; `demo.tree.attribute_hidden` covers it.
4. The temporary document is destroyed — the page owns copies of the text it
   keeps, so nothing borrows the document afterwards.
5. `robo_demo_page_layout()` flows the blocks vertically for the target size.
6. `robo_demo_page_paint()` clears the surface and draws background, accent
   bar, heading, paragraphs and the button (states: normal / hover / pressed)
   with RoboGraphics primitives and the shared RoboDemo bitmap font.
7. RoboWindow presents the surface; the event loop feeds mouse motion and
   clicks back into the button state.

Diagnostics from the tree builder are kept on the page
(`robo_demo_page_diagnostic_count`) rather than hidden, so malformed input is
visible as recovery instead of silence.

## 4. Build and run

```powershell
# build + full test suite
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test

# just the demo suites
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "demo."

# real host window: opens, renders, injects keyboard+mouse, resizes, closes
.\runtime\Out\RoboTest.host.exe --demo-html

# flags
#   --hold <ms>        how long the window stays up (default 4000)
#   --no-window        offscreen only: paint + probes + PNG, no Win32 window
#   --no-inject-input  skip the synthetic keyboard/mouse/resize sequence
#   --shot <path>      write a PNG of the rendered frame
```

The runner prints the page summary, the pixel probes and a final
`RESULT: PASS/FAIL`, and exits non-zero on failure — so it doubles as an
automated smoke test.

Example:

```
page:  title="RoboticelaOS Runtime" blocks=4 supported=8 ignored=0 diagnostics=0
offscreen: 900x600 RGBA8
paint: blocks=4 rects=6 lines=1 text_runs=4
probe OK   accent-bar       (450,2) = 14,122,255
probe OK   page-background  (896,596) = 245,245,247
probe OK   button-fill      (40,174) = 14,122,255
probe OK   button-border    (32,152) = 7,87,199
probe OK   heading-ink      ink at (35,32)
checks: 15 passed=15 failed=0
RESULT: PASS
```

## 5. QEMU

```powershell
scripts\robo test-qemu-display            # rebuild the image and run
scripts\robo test-qemu-display --skip-build
```

The guest program (`user/robo-runtime/main.c`) embeds the same demo document,
runs the same tokenizer → tree builder → RoboDemo path, presents into the QEMU
framebuffer, feeds keyboard and mouse input, and prints markers the script
asserts:

```
ROBO_QEMU_DISPLAY_OK / ROBO_QEMU_HTML_OK / ROBO_QEMU_PRESENT_OK
ROBO_QEMU_INPUT_OK   / ROBO_QEMU_DISPLAY_SHUTDOWN_OK
ROBO_QEMU_HTML_TITLE=RoboticelaOS Runtime
ROBO_QEMU_HTML_BLOCKS=0x00000004
ROBO_QEMU_HTML_SUPPORTED=0x00000008
ROBO_QEMU_HTML_IGNORED=0x00000000
```

The exit code is 0 only when every marker appeared.

## 6. Tests

| Filter | Coverage |
|---|---|
| `demo.version` | Version pin and block-style names |
| `demo.source.*` | Built-in document, asset byte-match, invalid args, empty input |
| `demo.tree.*` | Character references, unsupported markup, malformed corpus + every prefix of the demo document |
| `demo.ownership.text` | The page owns its text; two pages are independent |
| `demo.layout.*` | Flow, button rect, hit-test, resize re-flow |
| `demo.paint.*` | Pixel probes, button states, determinism, clipping in a small target, invalid args |
| `demo.export.png` | PNG export of the rendered frame |
| `demo.lifetime.*` | Repeated create/destroy; refusal when the module is down |
| `demo.font.helpers` | Atlas metrics and helper names |

The tree-builder side of the same path is covered by the `html.tree.*` suites
in `runtime/html/tests/SuiteRoboHTMLTreeBuilder.c`, including the demo
document's expected tree.

## 7. Intentionally temporary

- **The walk** (`walk_subtree`) — replaced by CSS matching + layout + paint.
- **The layout constants** (`DEMO_*`) — stand-ins for Phase 9/10 computed styles.
- **The block model and the bitmap font** — the demo has no text shaping; it
  uses the shared RoboDemo atlas rather than RoboFont/RoboText.
- **No text wrapping, no scrolling, no focus** — the demo fits in one screen
  by construction.

## 8. Known limitations

- The demo paints a fixed subset; anything else is counted, not rendered.
- The host runner opens a real window, so it needs a desktop session. Use
  `--no-window` for headless/CI-style runs (paint, probes and PNG still run).
- QEMU runs need `qemu-system-x86_64` on `PATH` (or the default install path)
  and take a couple of minutes to build and boot.
- Pixel probes assume the documented demo palette; changing the palette means
  changing the probes deliberately.
