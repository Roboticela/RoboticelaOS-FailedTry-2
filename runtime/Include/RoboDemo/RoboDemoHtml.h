/*
 * RoboDemo HTML — temporary tree→picture adapter (visual checkpoint).
 *
 * WHY THIS EXISTS
 *   Phases 6, 7, 8.1 and 8.2 are complete and 8.3 added the real tree
 *   builder, but there is still no CSS (9), layout (10) or paint (11).
 *   This module is a deliberately narrow, disposable adapter that proves
 *   the finished foundation is alive end to end:
 *
 *     HTML source
 *        → RoboHTML document/source boundary (8.1)
 *        → RoboHTML tokenizer (8.2)          ← the REAL tokenizer
 *        → RoboHTML tree builder (8.3)       ← the REAL tree builder
 *        → document tree
 *        → block list (this file)            ← temporary, ~1 layout pass
 *        → RoboGraphics (6.x)
 *        → RoboWindow present (7.14)
 *
 *   The same page model paints the host window and the QEMU display: both
 *   backends share this one paint path, so there is no second renderer.
 *
 * WHAT IT IS NOT
 *   - Not a DOM: no mutation, no events, no traversal API of its own; it
 *     only reads the document tree through the 8.3 read accessors.
 *   - Not a layout engine: one fixed vertical flow with per-element
 *     constants, and no text wrapping.
 *   - Not CSS: no selectors, cascade, or computed styles.
 *   - Not paint architecture: no compositor, no display list, no damage.
 *
 *   Phase 9/10/11 replace the constants, the walk and the painting below.
 *   Nothing else in the runtime may depend on this module — it depends on
 *   RoboHTML and RoboGraphics, never the other way around.
 *
 * SUPPORTED SUBSET
 *   Recognised: html, head, title, body, h1, p, button.
 *   Anything else is skipped safely and counted (see
 *   robo_demo_page_ignored_tag_count) — unsupported markup never fails the
 *   page, and supported markup nested inside it is still painted.
 *   Text comes from the tree's text nodes, so character references
 *   (&amp; &#65;) arrive already decoded by the 8.2 tokenizer.
 *
 * LIFETIME
 *   - robo_demo_page_create copies the text it keeps; the source bytes are
 *     borrowed only for the duration of the call.
 *   - Blocks are owned by the page; their text is owned by the page.
 *   - Page is single-threaded; layout state is cached and recomputed when
 *     the target size changes.
 *
 * See demo/README.md and demo/docs/VisualCheckpoint.md.
 */

#ifndef ROBO_DEMO_HTML_H
#define ROBO_DEMO_HTML_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Recognised element styles                                           */
/* ------------------------------------------------------------------ */

typedef enum robo_demo_block_style {
    /** <h1> — the page headline. */
    ROBO_DEMO_BLOCK_HEADING = 1,
    /** <p> — a paragraph of body copy. */
    ROBO_DEMO_BLOCK_PARAGRAPH = 2,
    /** <button> — the one interactive element the demo supports. */
    ROBO_DEMO_BLOCK_BUTTON = 3,
    /** <title> — becomes the window title, never painted. */
    ROBO_DEMO_BLOCK_DOCUMENT_TITLE = 4
} robo_demo_block_style_t;

/** Human-readable style name; never NULL. */
const char *robo_demo_block_style_name(robo_demo_block_style_t style);

/* ------------------------------------------------------------------ */
/* Page model                                                          */
/* ------------------------------------------------------------------ */

typedef struct robo_demo_page robo_demo_page_t;

/**
 * Build a page from borrowed UTF-8 HTML source.
 *
 * Runs the real 8.2 tokenizer and 8.3 tree builder over @p html, then walks
 * the resulting document tree to collect the supported subset into an
 * ordered block list. Malformed markup is error-tolerant: the tree builder
 * recovers, unsupported elements are counted and skipped, and the call
 * still succeeds.
 *
 * @param html    Borrowed UTF-8 source; may be NULL only when length == 0.
 * @param length  Byte length of @p html.
 * @param out     Receives the owned page; untouched on failure.
 * @param diag    Optional out-parameter for a hard failure (may be NULL).
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out or (NULL @p html
 *         with non-zero @p length); ROBO_OUT_OF_MEMORY; ROBO_INIT_ERROR
 *         when the RoboHTML module has not been initialized.
 */
robo_result_t robo_demo_page_create(const char *html,
                                    size_t length,
                                    robo_demo_page_t **out,
                                    robo_html_diag_t *diag);

/** Same, taking a RoboHTML 8.1 source descriptor (borrowed bytes). */
robo_result_t robo_demo_page_create_from_source(
    const robo_html_source_t *src,
    robo_demo_page_t **out,
    robo_html_diag_t *diag);

/**
 * Build a page from a document that already exists (Phase 8.9).
 *
 * The page is a copy of the parts of the tree the adapter understands, so it
 * can be rebuilt from the same document after the DOM has changed the tree —
 * which is how a mutation becomes visible in the picture rather than only in
 * the tree. The document is borrowed and never destroyed or modified here.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out; ROBO_NOT_FOUND
 *         when @p document_id is unknown or destroyed; ROBO_INIT_ERROR when
 *         the RoboHTML module is not initialized; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_demo_page_create_from_document(
    robo_html_document_id_t document_id,
    robo_demo_page_t **out,
    robo_html_diag_t *diag);

/** Release the page and every block/text it owns. Safe on NULL. */
void robo_demo_page_destroy(robo_demo_page_t *page);

/** Window title from <title>, or "" when the document has none. Never NULL. */
const char *robo_demo_page_title(const robo_demo_page_t *page);

/** Number of paintable blocks (excludes the document title). */
size_t robo_demo_page_block_count(const robo_demo_page_t *page);

/** Style of block @p index; out-of-range → ROBO_NOT_FOUND. */
robo_result_t robo_demo_page_block_style(const robo_demo_page_t *page,
                                         size_t index,
                                         robo_demo_block_style_t *out);

/** Borrowed UTF-8 text of block @p index ("" when empty). */
const char *robo_demo_page_block_text(const robo_demo_page_t *page,
                                      size_t index);

/**
 * Count of recognised elements in the built document tree.
 * Implied structure counts: an empty document still has html, head and
 * body, so an empty page reports 3 rather than 0.
 */
size_t robo_demo_page_supported_element_count(const robo_demo_page_t *page);

/** Count of elements the painted subset does not support. */
size_t robo_demo_page_ignored_tag_count(const robo_demo_page_t *page);

/** Parse diagnostics recorded while building the tree (bounded). */
size_t robo_demo_page_diagnostic_count(const robo_demo_page_t *page);

/** Copy diagnostic @p index; out-of-range → ROBO_NOT_FOUND. */
robo_result_t robo_demo_page_get_diagnostic(const robo_demo_page_t *page,
                                            size_t index,
                                            robo_html_diag_t *out);

/** Non-zero when a <button> block exists. */
int robo_demo_page_has_button(const robo_demo_page_t *page);

/* ------------------------------------------------------------------ */
/* Layout                                                             */
/* ------------------------------------------------------------------ */

/**
 * Compute block rectangles for a @p width × @p height target.
 *
 * Deterministic vertical flow: fixed outer padding, per-style type scale
 * and spacing, button sized to its label with a minimum width. Overlong
 * text is clipped by the painter, not wrapped (wrapping is real layout
 * work and belongs to Phase 10).
 *
 * Re-running with the same size is a no-op; a different size re-flows.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL page or non-positive size.
 */
robo_result_t robo_demo_page_layout(robo_demo_page_t *page,
                                    int32_t width,
                                    int32_t height);

/** Last layout size (0×0 before the first layout). */
robo_gfx_size_t robo_demo_page_layout_size(const robo_demo_page_t *page);

/**
 * Painted rect of block @p index in device pixels; out-of-range → false.
 * Only meaningful after a successful layout.
 */
int robo_demo_page_block_rect(const robo_demo_page_t *page,
                              size_t index,
                              robo_gfx_rect_t *out);

/** Button rect; returns 0 when the page has no button or is not laid out. */
int robo_demo_page_button_rect(const robo_demo_page_t *page,
                               robo_gfx_rect_t *out);

/**
 * Hit-test the button. Half-open rect containment, matching
 * robo_gfx_rect_contains_point. Returns 0 when there is no button.
 */
int robo_demo_page_hit_button(const robo_demo_page_t *page, robo_gfx_point_t p);

/* ------------------------------------------------------------------ */
/* Painting                                                           */
/* ------------------------------------------------------------------ */

typedef enum robo_demo_button_state {
    ROBO_DEMO_BUTTON_NORMAL = 0,
    ROBO_DEMO_BUTTON_HOVER = 1,
    ROBO_DEMO_BUTTON_PRESSED = 2
} robo_demo_button_state_t;

typedef struct robo_demo_paint_options {
    /** Visual state of the demo button; NORMAL when options are NULL. */
    robo_demo_button_state_t button_state;
} robo_demo_paint_options_t;

/** What the painter actually emitted (tests + diagnostics). */
typedef struct robo_demo_paint_stats {
    size_t blocks_painted;
    size_t rects_painted;
    size_t lines_painted;
    size_t text_runs_painted;
} robo_demo_paint_stats_t;

void robo_demo_paint_options_init(robo_demo_paint_options_t *options);

/**
 * Paint the page onto @p surface, laying out first if needed.
 *
 * Clears the whole surface first, so the result is deterministic and does
 * not depend on previous buffer contents.
 *
 * @param options  Optional; NULL selects the defaults.
 * @param out_stats Optional statistics; NULL is fine.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL args or a write-incapable
 *         surface; ROBO_NOT_SUPPORTED for an unimplemented pixel format.
 */
robo_result_t robo_demo_page_paint(robo_demo_page_t *page,
                                   robo_gfx_surface_t *surface,
                                   const robo_demo_paint_options_t *options,
                                   robo_demo_paint_stats_t *out_stats);

/* ------------------------------------------------------------------ */
/* Built-in demo document                                             */
/* ------------------------------------------------------------------ */

/**
 * The built-in demo document.
 *
 * Kept byte-identical to demo/assets/Demo.html — engine/tests/… asserts
 * that invariant so the file stays the editable copy without drifting from
 * the compiled-in source (the guest has no filesystem in this path).
 */
const char *robo_demo_default_html(void);

/** Byte length of robo_demo_default_html(). */
size_t robo_demo_default_html_length(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DEMO_HTML_H */
