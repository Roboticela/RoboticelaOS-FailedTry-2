/*
 * RoboDesktop application documents (Phase 11.13) — the bridge from markup to a window's contents.
 *
 *     HTML text + CSS text
 *          ↓  RoboHTML → RoboCSS → RoboLayout → RoboPaint
 *     display list  →  a shell window's contents
 *
 * ## Why this file exists separately from the shell
 *
 * A desktop shell has no business parsing markup: `RoboDesktop.h` says so, the shell's capability record
 * reports `documents == 0`, and `Assert-RoboDesktopBoundaries` refuses a document-layer include anywhere
 * under `Source/RoboDesktop` **except here**. This is where that exception lives, and the shape of it is the
 * point:
 *
 *   an application's picture is built **once**, into a value (`DesktopDocument`) that owns its own
 *   DOM, style context, layout tree and paint list;
 *
 *   what leaves this object is a `const PaintDisplayList *` — the same display list RoboRender
 *   executes and the same one a shell window borrows. Nothing downstream of a document knows that markup
 *   was involved;
 *
 *   so a document can be built, laid out, painted, and handed to **any** window, or rasterised into a pixel
 *   target in a suite, without a desktop existing at all.
 *
 * That last property is why the document is not a member of the shell. A desktop with no document is a
 * desktop (the shell's own smoke test), and a document with no desktop is a picture (a suite's checkpoint);
 * a type that required both would make each of those unexpressible, which is the same argument
 * `RoboDesktopTypes.h` makes for keeping shell window ids and native window ids apart.
 *
 * ## What it owns, and the order it must be released in
 *
 * A parsed HTML document, a CSS style context (with its property table, registry and author sheet), the
 * layout bridge and its computed styles, a layout tree that **borrows** those styles, the paint record list,
 * and the display list built from it. The ownership chain is why the whole object is opaque: a caller that
 * could see the fields could destroy one of them in the wrong order, and the tree's borrow of the bridge's
 * styles — stated in `RoboLayoutStyle.h` and again in `RoboLayoutTestSupport.h` — is the one rule that makes
 * the order load-bearing rather than tidy. `DesktopDocumentDestroy` is the only door that releases
 * anything, and it releases in reverse.
 *
 * ## The face is borrowed and the choice is the caller's
 *
 * `config.face` is borrowed, never retained: the same rule `RenderRasterize` states one layer up and
 * `LayoutTextFontAttach` states one layer down. It is used for two things — measuring text runs
 * (RoboLayout's own font-backed measurer) and drawing them (the rasteriser's) — and a NULL face is a
 * supported build where every text run is measured as nothing and drawn as nothing. Which face a document
 * should use is a *selection* question (`font-family`, RoboFont's own Goals 5.11/5.12/5.15), and a document
 * object that picked one would be answering a question its caller owns.
 *
 * ## Since 12.3: a typography role, and the defaults that come with it
 *
 * `config.typography` (optional) is how a document answers that selection question the *system's* way: the
 * document is measured and drawn with the resolver's `DocumentBody` face, and a **runtime-origin sheet**
 * generated from the typography scale is added to the document before the author's own — so `body`, `h1`,
 * `h2`, `h3`, a caption and a code span start from one central table rather than from each document's own
 * markup, and the author's rules still win (a runtime sheet is lower priority than an author sheet, which
 * is exactly what `RoboCssOriginRuntime` means).
 *
 * Two consequences are worth stating because they are the phase's requirements rather than this file's
 * conveniences:
 *
 *   **measurement and paint use one face object.** `DesktopDocumentFace` returns the borrowed face the
 *   document resolved, and a caller that hands *it* to the window's contents (rather than its own copy of
 *   the same family) has made "the run was measured with the face it is drawn with" a property of the
 *   data. Measuring with one face and painting with another is the failure `RoboLayoutTextFont.h` says no
 *   later phase could detect;
 *
 *   **the role table is the only place the defaults are written.** A document with a `typography` resolver
 *   and no author sheet of its own gets *the system's* heading sizes, and changing them means changing
 *   `RoboTypographyScale.h` — not editing every document.
 *
 * `config.face` remains the fallback for a role the resolver cannot name a face for, exactly as it is in
 * the shell's chrome (`RoboDesktop.h`) — so a document built with both keeps drawing if the pack lost its
 * family.
 *
 * ## The viewport is the page box
 *
 * `width` and `height` are the pass's viewport, and in this build they are also the document's page box:
 * there is no scrolling, no `body` height growth and no second layout pass, so a document is exactly as large
 * as the window it was built for. Reporting a "content height" that differed from the viewport would be
 * reporting a number from a subsystem with different semantics, so this object reports the viewport it ran
 * with (`DesktopDocumentViewport`) and nothing else.
 */
#ifndef ROBO_DESKTOP_DOCUMENT_H
#define ROBO_DESKTOP_DOCUMENT_H

#include "RoboBase/RoboError.h"
#include "RoboDesktop/RoboDesktopDiagnostics.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontRegistry.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboTypography/RoboTypographyResolver.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** An application document: owned markup, style, layout, records and a display list. */
typedef struct DesktopDocument DesktopDocument;

/** What a caller hands the pipeline. */
typedef struct DesktopDocumentConfig {
    /** The markup. Required; borrowed for the duration of the build only. */
    const char *html;
    /** An author stylesheet, or NULL for none. Borrowed for the duration of the build only. */
    const char *css;
    /** Viewport width in pixels. Must be positive; see the header comment. */
    int32_t width;
    /** Viewport height in pixels. Must be positive. */
    int32_t height;
    /**
     * The face text is measured and drawn with, or NULL for a textless build. Borrowed, never released.
     * Used when @ref typography is NULL, and as the fallback when it names no face for the body role.
     */
    FontFace *face;
    /**
     * Optional typography resolver (Phase 12.3): the document's face and its default type settings come
     * from it (see the header comment). Borrowed, and its pack must outlive the build.
     */
    TypographyResolver *typography;
    /**
     * Optional font registry (Phase 12.17): the faces a run's CSS `font-family` is resolved from.
     *
     * When set, each text run's **resolved font request** (`RoboLayoutFontRequest.h`) is walked against
     * this registry — a named family through `FontSelect`, a generic through the pack's own mappings — and
     * the first family that yields a face is the face the run is measured and drawn with. A document with
     * a registry therefore produces **mixed-font runs** (a bold heading's own face, a monospace code span's
     * own face); one without keeps the single-face behaviour and draws every run in @ref face.
     *
     * Borrowed, and it must outlive the build: the selected faces live in it. The generic mappings come
     * from @ref typography's pack when one is given.
     */
    FontRegistry *registry;
} DesktopDocumentConfig;

/** Fill @p config with the defaults: no markup, no stylesheet, 800x600, no face, no typography, no
 *  registry. */
void DesktopDocumentConfigInit(DesktopDocumentConfig *config);

/** Non-zero when this build can turn markup into a display list. See `RoboDesktopDocumentsImplemented`. */
int DesktopDocumentIsImplemented(void);

/** What one build saw, by value — the numbers a caller logs or a suite asserts. */
typedef struct DesktopDocumentReport {
    /** Elements the layout bridge captured. */
    size_t elements;
    /** Boxes the builder created. */
    size_t boxes;
    /** Text objects the builder created. */
    size_t texts;
    /** Paint operations the pass produced. */
    size_t records;
    /** Display-list commands built from them. */
    size_t commands;
    /**
     * Family lookups the per-run font resolver **matched** (Phase 12.17).
     *
     * Zero for a document built without @ref DesktopDocumentConfig::registry, and the number a caller reads
     * to tell "every family the document asked for resolved" from "the document has text and no family was
     * ever consulted". One lookup per `font-family` entry tried, so a two-family request whose first name
     * misses and generic hits counts two -- which is the point: the walk is visible.
     */
    size_t fontFamiliesResolved;
    /** Family lookups that matched nothing, and whose request moved on (or ended). */
    size_t fontFamiliesUnresolved;
} DesktopDocumentReport;

/**
 * Build a document: parse, style, lay out, paint, and build a display list.
 *
 * Every stage is the real subsystem — `HtmlBuildTree`, a real CSS style context with the standard
 * property registry, `LayoutTreeBuildFromDocument` with RoboLayout's own font-backed measurer, and
 * `PaintDisplayListBuilderBuild` — because this object's whole reason to exist is to be the one
 * place the chain is wired in production order. Nothing is simulated and nothing is stubbed: a document that
 * this call accepts is a document the rasteriser can execute.
 *
 * A document that lays out to nothing (a markup string with no elements, a `display: none` root) is
 * **success**: an empty display list is a legitimate picture, and the report says `commands == 0` rather
 * than the call failing.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL @p config or @p out, missing markup, or a
 *         non-positive viewport; RoboParseError with a `DOCUMENT` diagnostic when a stage refused the
 *         document (the message names the stage and the underlying reason); RoboOutOfMemory when a
 *         stage could not allocate.
 */
Result DesktopDocumentBuild(const DesktopDocumentConfig *config,
                                          DesktopDocument **out,
                                          DesktopDiag *diag);

/** Destroy @p document: the display list, the records, the tree, the bridge, the styles and the DOM, in
 *  that order. Safe on NULL. */
void DesktopDocumentDestroy(DesktopDocument *document);

/** Non-zero once a document exists and has not been destroyed. */
int DesktopDocumentIsLive(const DesktopDocument *document);

/**
 * The document's picture, **borrowed**, and the value a window's contents are set from.
 *
 * Valid until the document is destroyed, exactly as `DesktopWindowSetContent` requires of the list
 * it borrows. NULL for a NULL document; an empty list (never NULL) for a document that painted nothing.
 */
const PaintDisplayList *DesktopDocumentDisplayList(
    const DesktopDocument *document);

/**
 * The face the document was **measured and drawn with**, borrowed; NULL for a textless document.
 *
 * The door that makes measurement and paint one face by construction: a caller hands this to
 * `DesktopWindowSetContent`'s `contentFace` rather than a face of its own, and the window's contents are
 * then rasterised through the same object the layout pass measured them with (Phase 12.3, §13). A caller
 * that paints with a *different* face of the same family gets a run whose width was computed from metrics
 * the drawing pass never read — which is the defect this door exists to make unexpressible.
 */
FontFace *DesktopDocumentFace(const DesktopDocument *document);

/** The viewport the pass ran with — this build's page box. `0x0` for NULL. */
GfxSize DesktopDocumentViewport(const DesktopDocument *document);

/** What the build saw. RoboInvalidArgument for NULL arguments. */
Result GetDesktopDocumentReport(const DesktopDocument *document,
                                           DesktopDocumentReport *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_DOCUMENT_H */
