# Application Documents (Phase 11.13)

```text
HTML text + CSS text
     ↓  RoboHTML → RoboCSS → RoboLayout → RoboPaint
display list  →  a shell window's contents
```

`RoboDesktopDocument.h` is the **bridge**: the one place in this module where markup becomes a picture.

---

## 1. Why it is a separate file rather than a part of the shell

A desktop shell has no business parsing markup. `desktop/README.md` states the split, the shell's
capability record reports `documents == 0`, and `Assert-RoboDesktopBoundaries` refuses a document-layer
include anywhere under `Source/RoboDesktop` **except here** — the exemption is a *file*, so it is visible
in the build's own output rather than implied by a directory listing.

The shape of the exception is the point:

- an application's picture is built **once**, into a value (`DesktopDocument`) that owns its own
  DOM, style context, layout tree and paint list;
- what leaves that object is a `const PaintDisplayList *` — the same display list RoboRender
  executes and the same one a shell window borrows. **Nothing downstream of a document knows that markup
  was involved**;
- so a document can be built, laid out, painted and handed to **any** window, or rasterised into a pixel
  target in a suite, without a desktop existing at all.

That last property is why a document is not a member of the shell. A desktop with no document is a
desktop (the shell's smoke test), and a document with no desktop is a picture (the suites' checkpoint); a
type that required both would make each of those unexpressible — the same argument `RoboDesktopTypes.h`
makes for keeping shell-window ids and native-window ids apart.

---

## 2. The chain, in production order

`DesktopDocumentBuild` runs the real subsystems, in the order a browser runs them, because this
object's whole reason to exist is to be the one place the chain is wired correctly. Nothing is simulated
and nothing is stubbed: a document this call accepts is a document the rasteriser can execute.

| # | Stage | Why the order matters here |
|---|---|---|
| 1 | `HtmlDocumentCreate` + `HtmlBuildTree` | The DOM is what every later stage asks questions about |
| 2 | the CSS context: a style context, a property table, the **standard** property registry applied to it, then the author sheet parsed | A context with no registry resolves no property, so the registry is applied *before* the sheet is attached |
| 2b | **the sheet's selectors are parsed separately** (one loop over its rules) | A sheet whose selectors were never parsed **matches nothing** — the difference between a styled document and a silent unstyled one. This is not tidiness |
| 3 | the layout bridge, then `refresh` | The style source is taken from a *refreshed* bridge; a source taken from an unrefreshed one resolves nothing |
| 4 | the tree, built with that source, then **one layout pass** with RoboLayout's own font-backed measurer attached to the caller's face | The measurer is the same door RoboPaint's text painting uses, so a run is measured and drawn by one rule |
| 5 | the paint pass into a record list, then `PaintDisplayListBuilderBuild` | The display list is the artifact that leaves; the records are its input and are owned alongside it |

The **viewport** (`config.width` × `config.height`) is the pass's viewport and, in this build, also the
document's page box: there is no scrolling, no `body` height growth and no second layout pass, so a
document is exactly as large as the window it was built for. Reporting a "content height" that differed
from the viewport would be reporting a number from a subsystem with different semantics, so this object
reports the viewport it ran with (`DesktopDocumentViewport`) and nothing else.

That is why `desktop_set_window_content` in the suites reads the client rectangle from the **shell**
before building a document: the document is sized to the window, not the other way round.

---

## 3. Ownership, and the release order that makes it load-bearing

The object owns: the DOM, the CSS style context (with its property table, registry and author sheet), the
layout bridge and its computed styles, the layout tree, the paint record list, and the display list built
from them. It is opaque because a caller that could see the fields could destroy one of them in the wrong
order.

The rule underneath is that **the layout tree borrows the bridge's computed styles** (stated in
`RoboLayoutStyle.h`, again in `RoboLayoutTestSupport.h`, and visible in the order this file releases
things): the tree goes before the bridge, the bridge before the CSS context, and the texts' measurer is
detached before the tree's styles stop existing. `DesktopDocumentDestroy` is the only door that
releases anything, and it releases in reverse.

There is **one** release routine rather than a destroy plus a failure-path variant with a flag, because
the partial state after a stage-4 failure is exactly the state after a stage-4 success minus two objects
— and a single routine that copes with NULLs is the smallest way to be sure of that. Every failure
releases everything built so far, in reverse, through it: a bridge that leaked its DOM on a layout failure
would leak one document per failed document, and this is a module a desktop calls in a loop.
`desktop.document.destroy_releases` is the case that keeps that honest — it asserts the live-block count
returns to its baseline across repeated build/destroy cycles (and, like 11.7's finding one phase down,
it runs the cycle **twice** and compares the two ends, because a single comparison measures what the
harness had already allocated).

---

## 4. The face is borrowed, and the choice is the caller's

`config.face` is borrowed and never retained — the same rule `RenderRasterize` states one layer up
and `LayoutTextFontAttach` one layer down. It is used for two things: measuring text runs (through
RoboLayout's font-backed measurer) and drawing them (through the rasteriser's glyph bridge).

A **NULL face is a supported build**: every text run is measured as nothing and drawn as nothing.
`desktop.document.with_face` compares the two builds' reports, so "a document with no font is a document
with no *drawn* text" is an assertion rather than an assumption.

Which face a document should use is a *selection* question (`font-family`, RoboFont's Goals 5.11/5.12/
5.15), and a document object that picked one would be answering a question its caller owns.

---

## 5. The report, and what a caller does with it

```c
typedef struct DesktopDocumentReport {
    size_t elements;   /* elements the layout bridge captured */
    size_t boxes;      /* boxes the builder created */
    size_t texts;      /* text objects the builder created */
    size_t records;    /* paint operations the pass produced */
    size_t commands;   /* display-list commands built from them */
} DesktopDocumentReport;
```

The five numbers are the pipeline's own counts, stage by stage, which is what makes the report useful
when something is wrong: `elements == 0` means the markup did not parse into a DOM this build recognises,
`boxes == 0` means nothing laid out (a `display: none` root, or a viewport of nothing), and
`commands == 0` with the other four non-zero means the document is real and empty of drawing.

**A document that lays out to nothing is success.** An empty display list is a legitimate picture, and the
report says so rather than the call failing. `desktop.document.without_style` pins the other side: markup
with **no author sheet at all** still produces boxes and commands (the user-agent-ish defaults of this
build), so "I forgot the CSS" is visible as a report rather than as a build error.

---

## 6. Failures, and which domain answers them

| Case | Result |
|---|---|
| NULL config or output | `RoboInvalidArgument`, `DOCUMENT` |
| no markup | `RoboInvalidArgument`, `DOCUMENT` |
| a non-positive or oversized viewport | `RoboInvalidArgument`, `DOCUMENT` |
| a stage refused the document | `RoboParseError`, `DOCUMENT`, **and the message names the stage and the underlying reason** |
| a stage could not allocate | `RoboOutOfMemory`, `DOCUMENT`, with everything built so far released |

The `DOCUMENT` domain exists so a reader is sent *to the document* rather than to the desktop: a
`SHELL` failure means "ask for a different size", and a document failure means "fix the application's
markup". `desktop.document.build_refusals` asserts the domain on the missing-markup path, and that the
out-pointer stays NULL rather than receiving a half-built object.

---

## 7. What a document in this build is not

- **No scripting.** There is no RoboJS in the pipeline yet; a document is markup, style, layout and paint.
- **No image loading.** `RoboPaint`'s image records need a **decoded resource** handed to them (11.7), and
  nothing in this build resolves one — an `<img>` therefore contributes no drawing and the paint pass
  counts it as a skipped image. The write half exists; the read half is a later phase's.
- **No events, no interaction, no focus, no selection.** The document is a *picture*. A shell window's
  contents are display-list commands; nothing maps a pointer position back into the document
  (`input_delivery` is a 0 in the capability table), and `RoboPaint`'s hit test is a door this module does
  not call.
- **No scrolling.** The viewport is the page box, which is why a document is exactly the size of the
  window it was built for.
- **No incremental relayout.** One pass, one picture: a document is built and destroyed, not mutated.
  A window whose contents change gets a new document, which is what "a frame is a frame" means one layer
  up.
