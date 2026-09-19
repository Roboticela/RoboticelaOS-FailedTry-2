/*
 * RoboLayout object accessors (Phase 10.1) — see RoboLayoutObject.h.
 *
 * Every door here validates the id through the same three lines: resolve it
 * against the tree, and answer ROBO_NOT_FOUND when it names no box. That is the
 * whole reason the id-to-object conversion lives in one place
 * (RoboLayoutInternal.h) instead of being repeated per accessor with a different
 * amount of care each time.
 */

#include "RoboLayout/RoboLayoutObject.h"

#include "RoboLayoutInternal.h"

#include <string.h>

robo_result_t robo_layout_object_get(const robo_layout_tree_t *tree,
                                     robo_layout_node_id_t id,
                                     robo_layout_object_t *out,
                                     robo_layout_diag_t *diag)
{
    const robo_layout_object_t *object;

    if (!out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_OBJECT, ROBO_INVALID_ARGUMENT,
                                "out is NULL");
    }
    memset(out, 0, sizeof(*out));
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_OBJECT, ROBO_INVALID_ARGUMENT,
                                "tree or id is invalid");
    }
    object = robo_layout_object_slot_const(tree, id);
    if (!object) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_OBJECT, ROBO_NOT_FOUND,
                                "the id names no box in this tree");
    }
    /* A copy, so the caller's value outlives the next add to the tree. */
    *out = *object;
    return ROBO_OK;
}

int robo_layout_object_is_live(const robo_layout_tree_t *tree,
                               robo_layout_node_id_t id)
{
    return robo_layout_object_slot_const(tree, id) != NULL;
}

robo_layout_object_kind_t robo_layout_object_kind(const robo_layout_tree_t *tree,
                                                  robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);

    return object ? object->kind : ROBO_LAYOUT_OBJECT_NONE;
}

robo_result_t robo_layout_object_dom_node(const robo_layout_tree_t *tree,
                                          robo_layout_node_id_t id,
                                          robo_html_node_handle_t *out)
{
    const robo_layout_object_t *object;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_html_node_handle_clear(out);
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_INVALID_ARGUMENT;
    }
    object = robo_layout_object_slot_const(tree, id);
    if (!object) {
        return ROBO_NOT_FOUND;
    }
    *out = object->dom_node;
    return ROBO_OK;
}

const robo_css_computed_style_t *robo_layout_object_style(
    const robo_layout_tree_t *tree,
    robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);

    return object ? object->style : NULL;
}

robo_css_display_t robo_layout_object_display(const robo_layout_tree_t *tree,
                                              robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);

    /* NONE for an id that names no box, which is the same value `display: none`
     * carries -- and that is why a caller asks robo_layout_object_is_live first
     * when the difference matters. */
    return object ? object->display : ROBO_CSS_DISPLAY_NONE;
}

robo_result_t robo_layout_object_text_run(const robo_layout_tree_t *tree,
                                         robo_layout_node_id_t id,
                                         robo_layout_text_run_t *out,
                                         robo_layout_diag_t *diag)
{
    const robo_layout_object_t *object;

    if (!out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_OBJECT, ROBO_INVALID_ARGUMENT,
                                "reading a text run needs an out");
    }
    robo_layout_text_run_init(out);
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_OBJECT, ROBO_INVALID_ARGUMENT,
                                "reading a text run needs a tree and an id");
    }
    object = robo_layout_object_slot_const(tree, id);
    if (!object) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_OBJECT, ROBO_NOT_FOUND,
                                "the id names no object in this tree");
    }
    /*
     * A box is refused rather than answered, and the refusal is the point: a box
     * holds a zeroed run, and a zeroed run is a *shape* a caller could read a width
     * out of. "This object is not a text run" has to be a miss.
     */
    if (object->kind != ROBO_LAYOUT_OBJECT_TEXT) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_OBJECT, ROBO_NOT_FOUND,
                                "the object is not a text run");
    }
    /*
     * And a run that was not *placed* is refused too, one level down from the same
     * reason: a text object a pass reached but declined -- a text run in a
     * block-direction context, which waits for the anonymous box -- holds a zeroed run
     * because nothing measured it, and a zeroed run read as an answer is exactly the
     * "plausible number" this module refuses to hand out. The result state is the
     * per-object authority, here as everywhere: COMPUTED means the run was placed and
     * measured, and anything else is a miss.
     */
    if (object->result.state != ROBO_LAYOUT_RESULT_COMPUTED) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_OBJECT, ROBO_NOT_FOUND,
                                "no pass has placed this text run");
    }
    *out = object->text_run;
    return ROBO_OK;
}

/** Which link a caller asked for. */
typedef enum layout_link_pick {
    LAYOUT_LINK_PARENT = 0,
    LAYOUT_LINK_FIRST_CHILD = 1,
    LAYOUT_LINK_LAST_CHILD = 2,
    LAYOUT_LINK_PREVIOUS = 3,
    LAYOUT_LINK_NEXT = 4
} layout_link_pick_t;

/** The selected link of @p object (NONE when absent). */
static robo_layout_node_id_t layout_object_link_of(const robo_layout_object_t *object,
                                                   layout_link_pick_t pick)
{
    switch (pick) {
    case LAYOUT_LINK_FIRST_CHILD:
        return object->first_child;
    case LAYOUT_LINK_LAST_CHILD:
        return object->last_child;
    case LAYOUT_LINK_PREVIOUS:
        return object->prev_sibling;
    case LAYOUT_LINK_NEXT:
        return object->next_sibling;
    case LAYOUT_LINK_PARENT:
    default:
        return object->parent;
    }
}

/**
 * The link @p pick selects, as an id.
 *
 * One function for the five link accessors: they differ only in which field they
 * read and in what "absent" means, and five copies of the same three checks is
 * five chances for one of them to check a different subset.
 *
 * "Absent" is ROBO_NOT_FOUND for every one of them, which is deliberate: a box
 * whose parent is absent is the root, and a box whose previous sibling is absent
 * is the first box among its siblings. Neither is an error and neither is an id,
 * so returning an id of NONE through a successful call would be the same mistake
 * the DOM's relation callbacks avoid by having no sentinel at all.
 */
static robo_result_t layout_object_link(const robo_layout_tree_t *tree,
                                        robo_layout_node_id_t id,
                                        layout_link_pick_t pick,
                                        robo_layout_node_id_t *out)
{
    const robo_layout_object_t *object;
    robo_layout_node_id_t link;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = ROBO_LAYOUT_NODE_ID_NONE;
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_INVALID_ARGUMENT;
    }
    object = robo_layout_object_slot_const(tree, id);
    if (!object) {
        return ROBO_NOT_FOUND;
    }
    link = layout_object_link_of(object, pick);
    if (link == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_NOT_FOUND;
    }
    *out = link;
    return ROBO_OK;
}

robo_result_t robo_layout_object_parent(const robo_layout_tree_t *tree,
                                       robo_layout_node_id_t id,
                                       robo_layout_node_id_t *out)
{
    return layout_object_link(tree, id, LAYOUT_LINK_PARENT, out);
}

robo_result_t robo_layout_object_first_child(const robo_layout_tree_t *tree,
                                            robo_layout_node_id_t id,
                                            robo_layout_node_id_t *out)
{
    return layout_object_link(tree, id, LAYOUT_LINK_FIRST_CHILD, out);
}

robo_result_t robo_layout_object_last_child(const robo_layout_tree_t *tree,
                                           robo_layout_node_id_t id,
                                           robo_layout_node_id_t *out)
{
    return layout_object_link(tree, id, LAYOUT_LINK_LAST_CHILD, out);
}

robo_result_t robo_layout_object_previous_sibling(const robo_layout_tree_t *tree,
                                                 robo_layout_node_id_t id,
                                                 robo_layout_node_id_t *out)
{
    return layout_object_link(tree, id, LAYOUT_LINK_PREVIOUS, out);
}

robo_result_t robo_layout_object_next_sibling(const robo_layout_tree_t *tree,
                                             robo_layout_node_id_t id,
                                             robo_layout_node_id_t *out)
{
    return layout_object_link(tree, id, LAYOUT_LINK_NEXT, out);
}

robo_result_t robo_layout_object_child_count(const robo_layout_tree_t *tree,
                                            robo_layout_node_id_t id,
                                            size_t *out)
{
    const robo_layout_object_t *object;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = 0u;
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_INVALID_ARGUMENT;
    }
    object = robo_layout_object_slot_const(tree, id);
    if (!object) {
        return ROBO_NOT_FOUND;
    }
    *out = object->child_count;
    return ROBO_OK;
}

robo_result_t robo_layout_object_depth(const robo_layout_tree_t *tree,
                                      robo_layout_node_id_t id,
                                      uint32_t *out)
{
    const robo_layout_object_t *object;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = 0u;
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_INVALID_ARGUMENT;
    }
    object = robo_layout_object_slot_const(tree, id);
    if (!object) {
        return ROBO_NOT_FOUND;
    }
    *out = object->depth;
    return ROBO_OK;
}

int robo_layout_object_is_root(const robo_layout_tree_t *tree,
                               robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);

    /*
     * Asked as "is this the tree's root" rather than "has this box no parent",
     * and the two answers are the same only because a root can only be added to an
     * empty tree. Stating it through the tree means a caller that has a *detached*
     * box someday does not silently start being told it is the root.
     */
    return object != NULL && robo_layout_tree_root(tree) == id;
}

int robo_layout_object_has_children(const robo_layout_tree_t *tree,
                                    robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);

    return object != NULL && object->child_count > 0u;
}

uint32_t robo_layout_object_flags(const robo_layout_tree_t *tree,
                                  robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);

    return object ? object->flags : 0u;
}

int robo_layout_object_has_flag(const robo_layout_tree_t *tree,
                                robo_layout_node_id_t id,
                                uint32_t flag)
{
    return flag != 0u &&
           (robo_layout_object_flags(tree, id) & flag) == flag;
}

robo_result_t robo_layout_object_result(const robo_layout_tree_t *tree,
                                       robo_layout_node_id_t id,
                                       robo_layout_result_t *out)
{
    const robo_layout_object_t *object;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_layout_result_init(out);
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_INVALID_ARGUMENT;
    }
    object = robo_layout_object_slot_const(tree, id);
    if (!object) {
        return ROBO_NOT_FOUND;
    }
    *out = object->result;
    return ROBO_OK;
}

const robo_layout_result_t *robo_layout_object_result_ref(
    const robo_layout_tree_t *tree,
    robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);

    return object ? &object->result : NULL;
}

/**
 * Whether @p id names a context that exists in @p tree *right now*.
 *
 * The ids on a box are a cache of a classification, and a classification is
 * invalidated whenever the box set changes (RoboLayoutTree.h). So the box's own
 * field is not enough to answer with: a box can hold an id from a classification
 * that has since been released, and handing that id back would be a stale answer
 * a caller could not tell from a live one.
 *
 * Validating here rather than zeroing every box's ids in the invalidation is a
 * deliberate trade. Zeroing is O(boxes) per invalidation, and invalidation happens
 * on *every add* -- so a build of a four-thousand-box document would be
 * quadratic in a walk that exists only to write two zeroes into each box. This
 * check is two comparisons, and it makes the property the module actually needs
 * true: an id that names nothing is a reported miss, wherever it came from.
 */
static int layout_object_context_is_live(const robo_layout_tree_t *tree,
                                        robo_layout_fc_id_t id)
{
    return id != ROBO_LAYOUT_FC_ID_NONE && robo_layout_fc_slot_const(tree, id) != NULL;
}

robo_result_t robo_layout_object_context(const robo_layout_tree_t *tree,
                                        robo_layout_node_id_t id,
                                        robo_layout_fc_id_t *out,
                                        robo_layout_diag_t *diag)
{
    const robo_layout_object_t *object;

    if (!out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    *out = ROBO_LAYOUT_FC_ID_NONE;
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "tree is NULL or the id is NONE");
    }
    object = robo_layout_object_slot_const(tree, id);
    if (!object) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_NOT_FOUND, "the id names no box in this tree");
    }
    /* NOT_FOUND covers both "nobody has classified this tree" and "the
     * classification this box carries has been released", because they are the
     * same statement to a caller: there is no context to read. Which one it was is
     * a fact about the *tree*, answered by `robo_layout_tree_is_classified`. */
    if (!layout_object_context_is_live(tree, object->context)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_NOT_FOUND,
                                "the box participates in no live formatting context");
    }
    *out = object->context;
    return ROBO_OK;
}

robo_result_t robo_layout_object_context_owned(const robo_layout_tree_t *tree,
                                               robo_layout_node_id_t id,
                                               robo_layout_fc_id_t *out,
                                               robo_layout_diag_t *diag)
{
    const robo_layout_object_t *object;

    if (!out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    *out = ROBO_LAYOUT_FC_ID_NONE;
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "tree is NULL or the id is NONE");
    }
    object = robo_layout_object_slot_const(tree, id);
    if (!object) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_NOT_FOUND, "the id names no box in this tree");
    }
    if (!layout_object_context_is_live(tree, object->context_owned)) {
        /* The common answer, and not a failure: this box is transparent to
         * formatting context, or it is an inline box, or the classification that
         * gave it one has been released. Either way its contents are not formatted
         * by a context of its own that a caller can inspect. */
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_NOT_FOUND,
                                "the box establishes no live formatting context of its own");
    }
    *out = object->context_owned;
    return ROBO_OK;
}

int robo_layout_object_establishes_context(const robo_layout_tree_t *tree,
                                           robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);

    return object != NULL && layout_object_context_is_live(tree, object->context_owned);
}

/**
 * Whether @p id names a line this tree actually holds.
 *
 * The same rule the context accessor above states, applied to the third arena: a
 * box's `line` field is a cache of a pass's conclusion, and a released line must be a
 * reported miss rather than a number that happens to name a different line -- which
 * it would, because ids restart at 1 with each pass.
 */
static int layout_object_line_is_live(const robo_layout_tree_t *tree,
                                      robo_layout_line_id_t id)
{
    return id != ROBO_LAYOUT_LINE_ID_NONE && robo_layout_line_slot_const(tree, id) != NULL;
}

robo_result_t robo_layout_object_line(const robo_layout_tree_t *tree,
                                      robo_layout_node_id_t id,
                                      robo_layout_line_id_t *out,
                                      robo_layout_diag_t *diag)
{
    const robo_layout_object_t *object;

    if (!out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "out is NULL");
    }
    *out = ROBO_LAYOUT_LINE_ID_NONE;
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT,
                                ROBO_INVALID_ARGUMENT, "tree is NULL or the id is NONE");
    }
    object = robo_layout_object_slot_const(tree, id);
    if (!object) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_NOT_FOUND,
                                "the id names no object in this tree");
    }
    /*
     * Three routes to one answer, and the door's own note in RoboLayoutObject.h lists
     * them: never placed, placed by a released pass, or a transparent inline box with
     * contents -- which is a participant of its context and a member of no single
     * line, because its rectangle is the union of what it encloses.
     */
    if (!layout_object_line_is_live(tree, object->line)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_NOT_FOUND,
                                "the object was placed on no line this tree holds");
    }
    *out = object->line;
    return ROBO_OK;
}
