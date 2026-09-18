/*
 * RoboLayout's DOM + computed-style bridge (Phase 10.1).
 *
 * This is the first module in the runtime that depends on RoboHTML *and*
 * RoboCSS, which DEPENDENCIES.md allows precisely here and nowhere below it: the
 * two are peers and must not know each other, so whoever needs both sits above
 * both. What it does is exactly that joining, and it is deliberately the smallest
 * thing that can do it:
 *
 *   RoboCSS asks its host about one element at a time (RoboCSSStyle.h).
 *   RoboHTML owns the elements, their attributes and their tree.
 *   The bridge answers the first with the second, and hands RoboLayout the
 *   computed styles the first produces.
 *
 * Three decisions make it work, and each one exists because the obvious
 * alternative does not.
 *
 * **The element reference is the DOM node id.** RoboCSS identifies an element by
 * an opaque `robo_css_element_ref_t` and never dereferences it; the bridge answers
 * with the node id and resolves it back with
 * `robo_html_document_node_by_id`. The alternative -- a private index into a
 * mirror array -- costs a scan on every callback and a second table to keep in
 * step, and buys nothing: the DOM's own ids are dense, stable for the document's
 * lifetime and already the identity the tree stores.
 *
 * **Element facts are captured once, because RoboCSS borrows them.** An element
 * view points at arrays of *borrowed* slices, and the matcher holds one view
 * while it recurses into `:not()`. So the classes and attributes of every element
 * are materialised into the bridge's own storage before any style is computed, and
 * the bytes they point at are the document's payload arena, which stops moving
 * once the tree is built. A bridge that built the slices on the stack would hand
 * the matcher a view whose arrays died at the end of the call -- the failure mode
 * is a selector that matches nothing, not a crash.
 *
 * **Styles are owned here, so the tree can borrow them.** `robo_layout_style_source`
 * stores the pointer it is given (RoboLayoutStyle.h), so something must own one
 * computed style per element for as long as any tree built from this bridge
 * lives. RoboLayoutDom is that owner. The same fact is why the style *source*
 * answers from a lookup rather than computing on demand: a source that computed a
 * fresh style per call would leak one per element per build.
 *
 * Two passes, in this order: capture every element's facts, then compute every
 * element's style. The order is load-bearing, not tidy -- computing an element's
 * style walks its *ancestors*, so the host must already be able to answer about
 * them when the first computation starts.
 *
 * What it does not do: it does not build a tree (RoboLayoutBuilder.h does), does
 * not parse CSS, does not own the style context or the stylesheets, and does not
 * resolve anything. RoboLayout's own geometry is nobody's business here.
 */

#ifndef ROBO_LAYOUT_DOM_H
#define ROBO_LAYOUT_DOM_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSStyle.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLNode.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutStyle.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Caps, and they are caps rather than truncation: an element with more classes or
 * more attributes than this fails the refresh loudly. A bridge that silently
 * dropped the thirty-third attribute would turn `[data-x]` into a wrong answer
 * instead of an unsupported one.
 *
 * The numbers are far above anything a document this runtime builds uses, and far
 * below anything that would make the per-element storage large.
 */
#define ROBO_LAYOUT_MAX_CLASSES_PER_ELEMENT 32u
#define ROBO_LAYOUT_MAX_ATTRIBUTES_PER_ELEMENT 64u

/** An opaque bridge: one document, one borrowed style context, owned styles. */
typedef struct robo_layout_dom robo_layout_dom_t;

/**
 * Create a bridge over @p document, using @p context to compute styles.
 *
 * The context is **borrowed** and must outlive the bridge. Its sheets, property
 * table, registry and the element host are installed here over @p document, which
 * is the whole point of the object: a caller that already has a configured style
 * context does not have to know that this module needs to answer four callbacks
 * to use it.
 *
 * @p context may have no property table and no registry -- the styles computed
 * then are thin, which is 9.7's documented behaviour and not a failure. It may
 * not be NULL.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a NONE document;
 *         ROBO_NOT_FOUND for a document that is not live; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_layout_dom_create(robo_css_style_context_t *context,
                                     robo_html_document_id_t document,
                                     robo_layout_dom_t **out_dom,
                                     robo_layout_diag_t *diag);

/**
 * Destroy @p dom: every computed style it owns, every captured class and
 * attribute array, and the bridge. The style context and the document are the
 * caller's and are not touched.
 *
 * **A tree built from this bridge's style source must be destroyed first.** The
 * tree borrows the styles, so destroying the bridge under it leaves the tree
 * pointing at freed memory -- the one lifetime rule in this module that a compiler
 * cannot check, and therefore the one that is written down.
 */
void robo_layout_dom_destroy(robo_layout_dom_t *dom);

/**
 * Capture every element in @p document, then compute its style.
 *
 * Called once by a caller that wants styles, and it is idempotent in the sense
 * that matters: a second refresh releases what the first produced before
 * building again, so a document that was rebuilt is not described by two
 * generations of styles at once.
 *
 * An element whose style cannot be computed keeps a NULL style rather than a
 * partial one, and the count is reported -- so "this element has no style" and
 * "this element's style came out empty" stay different facts.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL bridge; ROBO_NOT_FOUND when
 *         the document has gone; ROBO_OUT_OF_MEMORY; ROBO_ERROR when the
 *         document has more elements than it has nodes, or an element exceeds a
 *         cap above -- both of which mean the bridge would have to guess.
 */
robo_result_t robo_layout_dom_refresh(robo_layout_dom_t *dom,
                                      robo_layout_diag_t *diag);

/** Number of elements captured (0 before the first refresh). */
size_t robo_layout_dom_element_count(const robo_layout_dom_t *dom);

/** Non-zero once a refresh has captured at least one element. */
int robo_layout_dom_is_ready(const robo_layout_dom_t *dom);

/**
 * The element at @p index of the capture order, which is document order.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a NONE document;
 *         ROBO_NOT_FOUND for an index at or past the count.
 */
robo_result_t robo_layout_dom_element_at(const robo_layout_dom_t *dom,
                                         size_t index,
                                         robo_html_node_handle_t *out,
                                         robo_layout_diag_t *diag);

/**
 * The computed style captured for @p element, or NULL.
 *
 * Borrowed, and it lives exactly as long as @p dom does (until the next refresh,
 * which releases the previous generation). This is the pointer the tree stores.
 */
const robo_css_computed_style_t *robo_layout_dom_style_for(
    const robo_layout_dom_t *dom,
    const robo_html_node_handle_t *element);

/** How many elements a refresh could not compute a style for (0 before one). */
size_t robo_layout_dom_without_style_count(const robo_layout_dom_t *dom);

/** The document this bridge describes. */
robo_html_document_id_t robo_layout_dom_document(const robo_layout_dom_t *dom);

/**
 * The style source to hand to robo_layout_tree_build_from_document.
 *
 * The returned struct borrows @p dom as its userdata, and answers from the
 * captured styles -- so @p dom must outlive every tree built with it. The struct
 * is written to @p out by value and owns nothing.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments. The source is
 *         usable before a refresh and answers nothing until one has run, which is
 *         why it does not require one: a caller that wants a tree with no styles
 *         should get one with no styles rather than an error.
 */
robo_result_t robo_layout_dom_style_source(robo_layout_dom_t *dom,
                                           robo_layout_style_source_t *out,
                                           robo_layout_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_DOM_H */
