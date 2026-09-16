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
| RoboHTML traversal / mutation / lifecycle (8.8–8.10) | yes — the walk reads through traversal, and a rebuilt page shows a mutation |
| RoboHTML element vocabulary (8.11) | yes — styles are chosen by tag symbol, not by name |
| RoboHTML image resources (8.12) | yes — a decoded PNG reaches the surface through the resolver seam |
| RoboHTML anchors / navigation (8.13) | yes — links are hit-tested and activated |
| RoboHTML forms state (8.14) | yes — every control's block is built from what the model reports it holds |
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
<a href="/Settings">Settings</a>
<a href="#Overview">Overview</a>
<p id="Overview">RoboHTML links resolve to real elements.</p>
<form action="/Register" method="post" enctype="application/x-www-form-urlencoded">
<label for="DisplayName">Display name</label>
<input id="DisplayName" type="text" name="displayName" placeholder="Your name" required>
<label for="Passphrase">Passphrase</label>
<input id="Passphrase" type="password" name="passphrase">
<label for="Revision">Build revision</label>
<input id="Revision" type="number" name="revision" min="1" max="999" value="8">
<label for="Subscribed">Platform notes</label>
<input id="Subscribed" type="checkbox" name="subscribed" checked>
<label for="ReleaseRing">Release ring</label>
<input id="ReleaseRing" type="radio" name="ring" value="Stable" checked>
<label for="Notes">Notes</label>
<textarea id="Notes" name="notes" rows="3" cols="40" placeholder="Anything else"></textarea>
<label for="Country">Country</label>
<select id="Country" name="country">
<option value="pk">Pakistan</option>
<option value="us" selected>United States</option>
</select>
<button type="submit">Register</button>
</form>
<button>Demo Button</button>
</body>
</html>
```

Every control in that form exists so the checkpoint can show a *state* rather than
a tag: a declared value that is displayed (`value="8"`), a placeholder that must
not be mistaken for one, a box that is checked, a radio whose value comes from
its attribute, a select that reports the option it is on rather than its first,
and a password field with nothing to show at all.

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
   - `a` → a link block holding the `href` bytes and the element, so the run
     can be hit-tested and activated (8.13);
   - `img` → an image block, when the element has a decoded resource (8.12);
   - `label` / `input` / `textarea` / `select` → a field block whose every
     reported fact comes from the 8.14 form model (see the 8.14 paragraph
     below);
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
4. The document stays with the page. It owns the one it built (since Phase
   8.13), so nothing borrows a document that has been freed — and the tree is
   still there for a link activation to resolve against. A page built from a
   caller's document borrows it instead and never destroys it.

   Since Phase 8.9 there is a second entry point,
   `robo_demo_page_create_from_document()`, which walks a document that
   already exists and neither parses nor destroys it. Because the page is a
   *copy* of the parts of the tree the adapter understands, it can be rebuilt
   from the same document after the 8.9 DOM mutation API has changed the
   tree — which is how a mutation becomes visible in the picture instead of
   only in the tree. `demo.mutation.changes_picture` is that test: parse
   `<h1>First</h1><p>Second</p>`, render it, move the paragraph ahead of the
   heading with `robo_html_node_remove_child` + `robo_html_node_insert_before`,
   render the *same* document again, and assert both the block order and that
   the painted pixels differ.
   Since Phase 8.10 there is a third question the same seam can answer: does
   moving a document through its *lifecycle* change what it renders? It must
   not. `demo.lifecycle.advance_keeps_picture` parses a document, renders it,
   then drives the same document through `COMPLETE → LOADING → INTERACTIVE →
   COMPLETE` (a replacement source is attached and settled again, with no parse
   in between), renders it once more, and asserts the picture is pixel-identical
   — because the adapter is a copy of the tree, an identical picture means no
   node moved. The test then *reloads* the document with a source that parses to
   a different tree and asserts the picture does change, which is what keeps the
   first half from being vacuous. A lifecycle that quietly touched the tree
   would still report the right state; only the pixels catch it.
   Since Phase 8.11 the subset is *typed* rather than spelled: the style table
   asks for `ROBO_HTML_TAG_BUTTON` / `ROBO_HTML_TAG_TITLE` by symbol, a
   division holding text and nothing else paints as a text block, and
   `demo.paint.element_kinds` renders a document built from the basic element
   types (`title`, `h1`, `p`, `div`, `span`) — asserting the styles the tags
   imply, the caption that came from the real `<title>`, pixel probes, and a
   deterministic second paint. A bare `span` is deliberately unpainted (the
   demo has no inline flow) and the test asserts that count, so the limit is
   recorded rather than latent.
   Since Phase 8.12 an `<img>` is a *resource* path rather than a tag: the walk
   asks `robo_html_element_image_get` for an element's attached resource and,
   when there are real decoded pixels, emits an image block that retains it.
   The painting step draws the actual bitmap at one image pixel per surface
   pixel — this adapter has no scaling — so what reaches the framebuffer is the
   decoded PNG, not a rectangle standing in for one. A recognised-but-
   unresolved image (no `src`, a failed fetch) is counted as unsupported markup,
   which keeps that limit stated and lets a document with a broken `src` render
   everything else. Bytes reach the element through the 8.12 resolver seam;
   `demo.image.reaches_framebuffer` installs an in-memory asset table, resolves
   a real PNG, renders, mutates the DOM, renders again and asserts the frame
   changed — a pixel fact, not a guess about a draw call.
   Since Phase 8.13 an `<a>` is a *navigation target* path rather than a tag:
   the walk reads the anchor's attributes, keeps the `href` bytes verbatim and
   remembers the element, so a link block can be hit-tested and activated. The
   painting step draws it in the brand colour with a hairline rule under the
   run — temporary adapter styling, not a style engine — and
   `demo.link.visual` asserts the difference is a colour fact (the link rect
   contains brand pixels, the paragraph rect does not) rather than "something
   was drawn". The checkpoint document carries one route link and one fragment
   link plus the `id` the fragment names, and both the offscreen probe and the
   windowed run activate them for real: the route reports `unsupported` and the
   fragment reports `fragment` with the element it found. No handler is
   installed, which is the point — a run that claimed either one had been
   navigated would be the failure this checkpoint exists to catch.
   Since Phase 8.14 a control is a *state* rather than a tag: the walk asks the
   form model what the element holds — its real tag, its *effective* input type,
   whether it is checked, and its current value through
   `robo_html_element_control_value` — and remembers the answers, never the
   handles. One field style covers `label`, `input`, `textarea` and `select`,
   because what a caller needs to tell apart is the state, not the element:
   `demo.field.checkpoint` asserts every caption is a `<label>` block with no
   value, no checkedness and no options, that the number input reports `8`, that
   the checkbox and the radio report themselves checked while nothing else does,
   that the password field reports no value *and* no text, and that the select
   reports `us` / `United States` with an option count of two — all read back
   from the page, so a block that invented a value or defaulted a box would fail
   here rather than pass as a screenshot. The pairing of caption to control in
   the picture is *positional* (document order), not a resolved `for` relation:
   this adapter paints a `<label>` as its own text row, and resolving a label to
   the control it names is the model's job, covered by `html.form.label_target`.
   The painting side is asserted on pixels by `demo.field.visual`: a caption
   draws no box while a field draws a white bordered one, a control whose text is
   its placeholder — or which is disabled — is drawn muted and a value in the
   text colour, a checked box draws the brand mark, and a select draws a chevron
   a text field does not. `demo.field.mutation` rebuilds the page from
   the same document after each of `set_control_value`,
   `clear_control_value`, an attribute rewrite, `set_control_checked` and
   `clear_control_checked`, which is where the declared/current split becomes
   visible: with no override in effect the current value tracks the `value`
   attribute, and with one it does not. The field rows are also why the
   checkpoint's metrics are what they are — the document now needs 800 device
   pixels to fit, so the host runner's offscreen surface is 900x800 and the field
   heights, box size and row gap are chosen so the sum stays under the QEMU
   framebuffer's 1280x800 rather than running off a real display.
5. `robo_demo_page_layout()` flows the blocks vertically for the target size.
6. `robo_demo_page_paint()` clears the surface and draws background, accent
   bar, heading, paragraphs, links, image blocks, form controls and the buttons
   (states: normal / hover / pressed) with RoboGraphics primitives and the
   shared RoboDemo bitmap font.
7. RoboWindow presents the surface; the event loop feeds mouse motion and
   clicks back into the button state, and a click that lands on a link is
   hit-tested and activated (Phase 8.13). The press index is remembered, so an
   activation is a press *and* a release on the same link — a release elsewhere
   is not a click.

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
source: 1397 bytes of built-in demo document
page:  title="RoboticelaOS Runtime" blocks=22 supported=29 ignored=0 diagnostics=0
offscreen: 900x800 RGBA8
paint: blocks=22 rects=50 lines=3 text_runs=19
probe OK   accent-bar       (450,2) = 14,122,255
probe OK   page-background  (896,796) = 245,245,247
probe OK   button-fill      (40,744) = 14,122,255
probe OK   button-border    (32,722) = 7,87,199
probe OK   heading-ink      ink at (35,32)
probe OK   paragraph-ink    ink at (36,76)
probe OK   paragraph-ink    ink at (34,106)
probe OK   link-ink         ink at (36,136)
probe OK   link-ink         ink at (36,166)
probe OK   paragraph-ink    ink at (34,196)
link: block 3 href="/Settings" outcome=unsupported resolved="/Settings"
link: block 4 href="#Overview" outcome=fragment resolved=""
probe OK   link-seam       2 links: 1 fragment, 1 unsupported (no handler installed, so nothing claims navigation)
checks: 15 passed=15 failed=0
RESULT: PASS
```

The counts are worth reading as facts rather than noise: 22 blocks are 6 runs
above the form, 7 captions, 7 controls and 2 buttons; 14 field blocks are the
captions plus the controls; and `supported=29` counts every element the walk
recognises, of which one is the `<title>` that becomes the caption rather than a
block.

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
ROBO_QEMU_HTML_SOURCE_BYTES=0x00000575
ROBO_QEMU_HTML_TITLE=RoboticelaOS Runtime
ROBO_QEMU_HTML_BLOCKS=0x00000016
ROBO_QEMU_HTML_SUPPORTED=0x0000001D
ROBO_QEMU_HTML_IGNORED=0x00000000
ROBO_QEMU_HTML_LINKS=0x00000002
ROBO_QEMU_HTML_LINKS_HIT=0x00000002
ROBO_QEMU_HTML_LINK_FRAGMENT=0x00000001
ROBO_QEMU_HTML_LINK_UNSUPPORTED=0x00000001
ROBO_QEMU_HTML_FIELDS=0x0000000E
ROBO_QEMU_HTML_FIELDS_STATE=0x00000007
ROBO_QEMU_HTML_FIELDS_CHECKED=0x00000002
ROBO_QEMU_HTML_FIELDS_DECLARED=0x00000003
ROBO_QEMU_HTML_FIELDS_PLACEHOLDER=0x00000002
ROBO_QEMU_HTML_FIELD_OPTIONS=0x00000002
```

The exit code is 0 only when every marker appeared.

The four link markers (Phase 8.13) are the guest running the *same* navigation
model the host checkpoint drives — hit-testing each link block and activating
it — so the phase is validated in QEMU and not only in the host window. With no
handler installed the route link refuses (`UNSUPPORTED`) and the fragment link
resolves to the element it names (`FRAGMENT`); a marker that claimed otherwise
would be the guest reporting navigation that did not happen.

The field markers (Phase 8.14) are the same idea one level down, and each one is
a fact the guest could only get from the model: 14 field blocks of which 7 carry
state (the captions are blocks with no state to report, and are excluded), 2 are
checked (the checkbox and the radio — nothing defaulted itself), 3 carry a
declared value (`8`, `Stable`, `us`), 2 have no value and only a placeholder, and
the select holds 2 options. `test-qemu-display` asserts these with expected
values, so a control whose state stopped reaching the picture fails the gate
instead of passing as a picture nobody diffs. The framebuffer is 1280x800, which
is the height the checkpoint's field metrics are budgeted for.

## 6. Tests

| Filter | Coverage |
|---|---|
| `demo.version` | Version pin and block-style names |
| `demo.source.*` | Built-in document, asset byte-match, invalid args, empty input |
| `demo.tree.*` | Character references, unsupported markup, malformed corpus + every prefix of the demo document |
| `demo.ownership.text` | The page owns its text; two pages are independent |
| `demo.layout.*` | Flow, button rect, hit-test, resize re-flow |
| `demo.paint.*` | Pixel probes, button states, determinism, clipping in a small target, invalid args, and the typed multi-element document |
| `demo.export.png` | PNG export of the rendered frame |
| `demo.mutation.changes_picture` | A real DOM mutation reaching the picture: the same document rendered before and after, with the block order and the painted output changing |
| `demo.field.*` | The form checkpoint: every control in the shipped document reporting the state the form model holds (kind, effective type, checkedness, value, option count), a caption's field resolution, a placeholder drawn muted versus a value in the text colour, a checked box's brand mark, a textarea taller than a single-line control, a select's chevron versus a text field, state re-read from the DOM after each mutation including the declared/current rule for both value and checkedness, and an unmodelled `type` degrading to `text` |
| `demo.link.*` | The link checkpoint: the built-in document's anchors as real DOM elements with their `href` bytes, hit-testing through block rects, a route refusing to navigate with no handler, a fragment resolving to the `id` it names, the link's own paint colour and underline versus body copy, activation after the document is destroyed, and `href` mutation reaching the picture |
| `demo.lifecycle.advance_keeps_picture` | A lifecycle transition leaves the picture pixel-identical; a reload changes it |
| `demo.image.reaches_framebuffer` | A real decoded PNG travels source → `<img>` → resource → decoder → surface, and a mutation changes the frame |
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
- **Image scaling** — an image paints 1:1 and is clipped to the target, so
  `width`/`height` are model data this adapter does not yet honour. Intrinsic
  sizing belongs to CSS and layout.
- **No text wrapping, no scrolling, no focus** — the demo fits in one screen
  by construction, and the field metrics are the reason the checkpoint document
  still does: it needs 800 device pixels, which is exactly the QEMU
  framebuffer's height, so a document that grew much further would be clipped
  rather than scrolled.

## 8. Known limitations

- The demo paints a fixed subset; anything else is counted, not rendered. A
  bare inline element (a `span` whose parent is not itself a painted block) is
  in that set: the demo has no inline flow, so text-level elements contribute
  their text only when they sit inside a block's subtree.
- The host runner opens a real window, so it needs a desktop session. Use
  `--no-window` for headless/CI-style runs (paint, probes and PNG still run).
- QEMU runs need `qemu-system-x86_64` on `PATH` (or the default install path)
  and take a couple of minutes to build and boot.
- Pixel probes assume the documented demo palette; changing the palette means
  changing the probes deliberately.
