/*
 * RoboLayout's builder (Phase 10.1).
 *
 * DOM tree in, layout tree out. It is the only place the two meet, and the
 * translation it performs is exactly one rule wide:
 *
 *   an element generates a box unless its computed `display` says otherwise
 *
 * Everything else is structure. Children are linked in document order, each box
 * gets its element's computed style if there is one, and every node that is not
 * an element is skipped -- text and comments are not boxes, and the ones that
 * will be (a line box, a text run) belong to 10.5, which will know what to do
 * with them. A builder that made text nodes into boxes today would be inventing
 * the shape of a box it cannot fill.
 *
 * and, since 10.5, a second rule beside it:
 *
 *   a text node generates a **text object**, unless its content is entirely
 *   collapsible whitespace
 *
 * The first rule is about *boxes* and still holds: text is not a box, and an inline
 * `display` declaration on a text node is not a thing CSS has. The second is what
 * 10.5 adds, and it is the reason `objects` and `boxesCreated` stopped being the
 * same number. The escape clause is not whitespace collapsing -- that is a line-box
 * algorithm this build does not have -- it is the thing that keeps the newline
 * between two elements in a pretty-printed document from becoming a run of spaces,
 * and it is argued where the predicate lives (RoboLayoutText.h).
 *
 * A text object borrows its **parent element's** computed style, because every
 * property a run needs is inherited and RoboCSS already distributed inherited values
 * to descendants -- so a text node's computed style *is* its parent's for exactly the
 * properties in question, and pointing at the parent's is the one representation that
 * cannot go stale (RoboLayoutText.h). The style is threaded down the recursion rather
 * than looked up again, so a document with text in it does not ask the style source
 * one extra question per text node.
 *
 * **The tree is replaced, not merged.** A build clears the tree first. A
 * rebuild is what a DOM mutation leads to -- the tree is derived state, and there
 * is no incremental path in 10.1 because nothing yet consumes a tree finely
 * enough to need one.
 *
 * **The root box is the document element.** Not the document node: a document is
 * not an element, generates nothing, and had a box of its own it would be a box
 * with no style and no parent. Starting at the document node and taking its first
 * element child is what makes the layout root the same thing a caller would name
 * in CSS (`:root`).
 */

#ifndef ROBO_LAYOUT_BUILDER_H
#define ROBO_LAYOUT_BUILDER_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDocument.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutStyle.h"
#include "RoboLayout/RoboLayoutTree.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How a build should behave. A record by value; the caller owns both pointers. */
typedef struct LayoutBuildOptions {
    /**
     * Where computed styles come from, or NULL for "none". The source is
     * *borrowed* for the duration of the call only, but the styles it returns are
     * stored in the tree and must outlive it (RoboLayoutStyle.h).
     */
    const LayoutStyleSource *styles;
} LayoutBuildOptions;

/** Clear @p options to "no styles": every box is built with STYLE_MISSING. Safe
 *  on NULL. */
void LayoutBuildOptionsInit(LayoutBuildOptions *options);

/** What one build saw. A record by value, owned by nobody. */
typedef struct LayoutBuildReport {
    /** Elements walked, including the root and the ones that generated no box. */
    size_t elementsSeen;
    /** Boxes added to the tree. */
    size_t boxesCreated;
    /** Elements whose computed display was `none`. */
    size_t skippedDisplayNone;
    /** Boxes built without a computed style (a fact, not a failure). */
    size_t boxesWithoutStyle;
    /**
     * Text objects added to the tree (10.5).
     *
     * One per text node that carries something, with the two counter-examples below
     * -- and counted separately from `boxesCreated` because a text object is not a
     * box: it has no element, no children and no `display` declaration of its own
     * (RoboLayoutText.h). A suite that added these into the box count would be
     * asserting a number that is two different facts.
     */
    size_t textsCreated;
    /**
     * Text nodes that generated no object because their content is *entirely*
     * collapsible whitespace.
     *
     * Not whitespace collapsing -- which is a line-box algorithm this build does not
     * have -- but its first consequence, and the reason a pretty-printed document does
     * not put a run of spaces between every pair of elements. An **empty** text node
     * counts here too, because "carries nothing" and "carries only spaces" are the
     * same non-content to a line.
     */
    size_t textsSkippedWhitespace;
    /** Deepest object created (0 when the root is the only one). */
    uint32_t depthMax;
} LayoutBuildReport;

/**
 * Build @p tree from @p document.
 *
 * The tree is cleared first, so a build always leaves the boxes of exactly one
 * document. An empty result is not a failure: a document with no element, or one
 * whose root element is `display: none`, generates no boxes and reports zero.
 * @p outReport is optional and is zeroed before the build, so a caller that reads
 * it after a failure sees what was done rather than a stale count.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL tree or document; RoboNotFound
 *         for a document that is not live; RoboOutOfMemory; or the DOM's own
 *         failure when the walk cannot proceed.
 */
Result LayoutTreeBuildFromDocument(LayoutTree *tree,
                                                   HtmlDocumentId document,
                                                   const LayoutBuildOptions *options,
                                                   LayoutBuildReport *outReport,
                                                   LayoutDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_BUILDER_H */
