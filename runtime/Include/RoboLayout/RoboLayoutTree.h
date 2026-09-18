/*
 * RoboLayout tree (Phase 10.1).
 *
 * The container: an owned arena of boxes, the links between them, and the
 * lookup the rest of the module goes through.
 *
 * **Why a tree at all, when there is already a DOM.** Because the two trees do
 * not have the same members. An element with `display: none` is a DOM node and
 * generates no box; a text node is a DOM node and will generate something in
 * 10.5 that is not an element box; a future `::before` is a box with no element
 * behind it. A layout pass that walked the DOM would have to re-decide, at every
 * step, which nodes it was allowed to look at -- and every later goal (line
 * breaking, anonymous blocks, fragments) would add another such decision in
 * another place. The tree is built *once*, by the builder, so that the pass walks
 * boxes and only boxes.
 *
 * **Identity is dense and tree-scoped.** Object ids are 1-based and assigned in
 * creation order, so `id == index + 1` holds until the tree is cleared, and
 * `robo_layout_object_id_at` makes walking the whole tree a loop. Ids are not
 * reused: clearing a tree restarts the cursor, which is why a tree is cleared and
 * rebuilt rather than edited.
 *
 * **The arena grows; borrowed pointers do not survive it.** `robo_layout_object`
 * hands out a borrowed pointer for the same reason RoboHTML's node view does --
 * one call gets every field -- and it is invalidated by the next `add_object`,
 * because the arena may move. Keeping an id and looking it up again is the
 * supported way to hold on to a box.
 *
 * **Building is add-only.** There is no remove, no re-parent and no reorder. A
 * tree that could be edited while a pass walks it would need the pass to be told
 * about it, and nothing in 10.1 needs the capability: DOM mutation rebuilds the
 * tree, which is the honest model for a phase whose tree is derived state.
 *
 * The second arena (Phase 10.2)
 *
 * A tree owns *two* arrays: the boxes, and the formatting contexts a pass
 * classified them into. The second is derived state exactly as a box's result is,
 * and it lives here rather than in the pass for one reason: a formatting context
 * is a fact about the *tree*, so a caller that has a tree must be able to ask
 * about it without also holding the pass that produced it. A pass that owned its
 * contexts would make "what context does this box participate in?" a question
 * with a lifetime, and every later phase would have to be handed the pass.
 *
 * The arena is *invalidated*, not merged: adding an object releases it and marks
 * the tree unclassified, because a participant count and a child-composition
 * classification are both statements about a box set that has just changed. The
 * alternative -- leaving a stale classification in place and hoping the caller
 * re-runs a pass -- is the one failure mode that would be invisible until a paint
 * phase drew the wrong thing.
 *
 * `robo_layout_tree_is_classified` is therefore the door that distinguishes "no
 * pass has run" from "a pass ran over an empty tree", and it is why the two ids
 * on a box (RoboLayoutObject.h) can stay NONE without being ambiguous.
 */

#ifndef ROBO_LAYOUT_TREE_H
#define ROBO_LAYOUT_TREE_H

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An opaque, caller-owned arena of layout objects. */
typedef struct robo_layout_tree robo_layout_tree_t;

/**
 * Everything needed to create one box.
 *
 * A descriptor rather than six positional arguments, for the same reason
 * RoboHTML's element-create descriptor is one: the builder is the only caller
 * today, and the goals that follow will extend it (a kind, an anonymous flag, a
 * text payload) without every call site changing shape.
 */
typedef struct robo_layout_object_info {
    /** Parent to link under as its last child. ROBO_LAYOUT_NODE_ID_NONE means
     *  "this is the root box", which is only accepted for an empty tree. */
    robo_layout_node_id_t parent;
    /** The element this box comes from. Must be well formed. */
    robo_html_node_handle_t dom_node;
    /** Borrowed computed style, or NULL. Must outlive the tree. */
    const robo_css_computed_style_t *style;
    /** The computed display this box is created under. Must generate a box. */
    robo_css_display_t display;
    /** ROBO_LAYOUT_OBJECT_FLAG_* bits; unset bits are added by the tree when it
     *  can tell (a NULL style sets STYLE_MISSING). */
    uint32_t flags;
} robo_layout_object_info_t;

/** Zero @p info: no parent (a root candidate), no element, no style. */
void robo_layout_object_info_init(robo_layout_object_info_t *info);

/**
 * Create an empty tree.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL output; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_layout_tree_create(robo_layout_tree_t **out_tree,
                                      robo_layout_diag_t *diag);

/** Destroy @p tree and every box in it. Borrowed styles and DOM handles are the
 *  caller's and are not touched. Safe on NULL. */
void robo_layout_tree_destroy(robo_layout_tree_t *tree);

/** Release every box, keeping the tree usable. Borrowed styles are not touched.
 *  After this the tree is empty and ids restart at 1. Safe on NULL. */
void robo_layout_tree_clear(robo_layout_tree_t *tree);

/** Number of boxes (0 for NULL). */
size_t robo_layout_tree_object_count(const robo_layout_tree_t *tree);

/** Capacity of the arena; >= the count. Exposed because a test that asserts the
 *  arena *grew* is asserting the one thing about it a caller can observe. */
size_t robo_layout_tree_capacity(const robo_layout_tree_t *tree);

/** Non-zero when the tree holds no box (also true for NULL). */
int robo_layout_tree_is_empty(const robo_layout_tree_t *tree);

/** Id of the root box, or ROBO_LAYOUT_NODE_ID_NONE when the tree is empty. */
robo_layout_node_id_t robo_layout_tree_root(const robo_layout_tree_t *tree);

/**
 * Fill @p out with info for the object at @p index in creation order.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL args; ROBO_NOT_FOUND for an
 *         index at or past the count. Ids are dense, so the id of the object at
 *         @p index is `index + 1`; this door exists so a walk needs no
 *         arithmetic of its own.
 */
robo_result_t robo_layout_object_id_at(const robo_layout_tree_t *tree,
                                       size_t index,
                                       robo_layout_node_id_t *out);

/**
 * Append one box.
 *
 * Validates rather than trusts, because every rule here is one a later goal
 * would otherwise have to re-check while walking:
 *
 *   - @p info->parent must be NONE only when the tree is empty (one root); a
 *     parent id must name a live box in *this* tree.
 *   - @p info->dom_node must be well formed (a box always came from something
 *     the DOM can be asked about).
 *   - @p info->display must generate a box (`robo_css_display_generates_box`);
 *     a `display: none` element has no box by definition, so creating one would
 *     make the tree a copy of the DOM's element list instead of what layout
 *     walks.
 *   - @p info->flags may not carry an undefined bit.
 *
 * A NULL style is accepted and sets STYLE_MISSING rather than failing: a caller
 * building a tree without CSS is doing something legitimate (a structural-only
 * walk), and it is told what it has.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL arguments, a NONE or ill-formed
 *         element handle, a display that generates no box, an unknown flag, a
 *         parent id of NONE in a non-empty tree); ROBO_NOT_FOUND (a parent id
 *         that names no box in this tree); ROBO_ALREADY_EXISTS (a second root);
 *         ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_layout_tree_add_object(robo_layout_tree_t *tree,
                                          const robo_layout_object_info_t *info,
                                          robo_layout_node_id_t *out_id,
                                          robo_layout_diag_t *diag);

/**
 * Borrowed object for @p id, or NULL when the id names no box.
 *
 * The whole struct in one call, and the reason a walk of a thousand boxes is a
 * thousand lookups rather than six thousand. Valid until the next add.
 */
const robo_layout_object_t *robo_layout_object(const robo_layout_tree_t *tree,
                                               robo_layout_node_id_t id);

/**
 * The box built for @p dom_node, or ROBO_LAYOUT_NODE_ID_NONE when the element
 * generated none.
 *
 * The reverse of robo_layout_object_dom_node, and the door that makes a
 * `display: none` element distinguishable from an element nobody built: the
 * first answers NONE, the second answers NONE too, and a caller that needs the
 * difference compares the element against the report the build returned
 * (RoboLayoutBuilder.h).
 */
robo_layout_node_id_t robo_layout_object_id_for_dom_node(
    const robo_layout_tree_t *tree,
    const robo_html_node_handle_t *dom_node);

/* ------------------------------------------------------------------ */
/* Formatting contexts (Phase 10.2)                                    */
/* ------------------------------------------------------------------ */

/**
 * Number of formatting contexts, or 0 when no pass has classified this tree.
 *
 * Zero is therefore also the answer for a classified but empty tree, which is why
 * @ref robo_layout_tree_is_classified exists: "nothing to classify" and "not
 * classified" are different statements, and a caller that conflated them would
 * silently treat an unclassifiable document as one with no contexts.
 */
size_t robo_layout_tree_context_count(const robo_layout_tree_t *tree);

/** Non-zero once a pass has classified this tree. A build, a clear and an add
 *  all make it 0 again. */
int robo_layout_tree_is_classified(const robo_layout_tree_t *tree);

/**
 * Release the classification, keeping every box.
 *
 * The door a caller reaches for when a box's *style* changed without the box set
 * changing -- e.g. a stylesheet mutation followed by a style refresh
 * (RoboLayoutDom.h) on a document whose element tree is unchanged. The tree keeps
 * each box's `display` from the build, so a caller that changed what that display
 * would be must rebuild the tree; what this door releases is the classification
 * derived from the boxes as they were.
 *
 * Safe on NULL. Invalidating an unclassified tree is a no-op rather than an
 * error, because "make sure nothing is classified" is a legitimate thing to want
 * idempotently.
 */
void robo_layout_tree_invalidate_contexts(robo_layout_tree_t *tree);

/**
 * Context at @p index in creation order.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND for
 *         an index at or past the count. Contexts are dense and 1-based, so the
 *         context at @p index has id `index + 1`; this door exists so a walk needs
 *         no arithmetic of its own, exactly as `robo_layout_object_id_at` does for
 *         boxes.
 */
robo_result_t robo_layout_tree_context_at(const robo_layout_tree_t *tree,
                                         size_t index,
                                         robo_layout_formatting_context_t *out,
                                         robo_layout_diag_t *diag);

/**
 * Context by id, by value.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a NONE id;
 *         ROBO_NOT_FOUND for an id that names no context in @p tree. @p out is
 *         cleared first, so a failure never leaves a half-filled copy.
 */
robo_result_t robo_layout_tree_context(const robo_layout_tree_t *tree,
                                      robo_layout_fc_id_t id,
                                      robo_layout_formatting_context_t *out,
                                      robo_layout_diag_t *diag);

/**
 * The tree's initial formatting context.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND when
 *         the tree has not been classified or has no root box -- a document that
 *         generated nothing has no initial context, which is the truth rather
 *         than a default to invent.
 */
robo_result_t robo_layout_tree_root_context(const robo_layout_tree_t *tree,
                                           robo_layout_fc_id_t *out,
                                           robo_layout_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_TREE_H */
