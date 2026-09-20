/*
 * RoboPaint context and the deterministic traversal (Phase 11.1–11.2) — see
 * RoboPaintContext.h.
 *
 * This file is the pass: the walk, the accumulation and the stats. Everything it
 * concludes comes from RoboLayout's own read doors -- there is no width, height, margin,
 * padding, border, position or overflow arithmetic here beyond the two things layout
 * cannot know, the page-space accumulation and the per-axis clip intersection, and both
 * of those live in RoboPaintGeometry.c so that this file stays a traversal.
 *
 * The walk is deliberately the dumbest correct thing: depth-first, document order, no
 * stack of its own beyond the call stack, and one operation per fact the box states. The
 * three properties the phase claims are consequences of that shape rather than checks
 * bolted onto it:
 *
 *   **determinism** -- the order is the tree's (`first_child` then `next_sibling`) and
 *   every number is a bounded record's, so two passes over one tree produce byte-identical
 *   lists (`robo_paint_list_equal`);
 *
 *   **purity** -- every door on the left-hand side takes the tree by `const` pointer, so
 *   "the painter does not write layout" is the signature;
 *
 *   **no re-derivation** -- the boxes come from `robo_paint_boxes_of`, the colours from
 *   the box's own style and its *used* border record, and the clip from the overflow the
 *   layout pass measured. Paint decides only *whether* something draws, which is the
 *   question layout does not ask.
 */

#include "RoboPaint/RoboPaintContext.h"

#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSValue.h"

#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintOperation.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_paint_context {
    robo_paint_stats_t stats;
    robo_paint_pass_outcome_t outcome;
    /* Borrowed and const as read back: the pass read the tree, it did not write it. */
    const robo_layout_tree_t *tree;
    void *user_data;
};

/* ------------------------------------------------------------------ */
/* The pass outcome                                                    */
/* ------------------------------------------------------------------ */

int robo_paint_pass_outcome_is_known(robo_paint_pass_outcome_t outcome)
{
    return outcome >= ROBO_PAINT_PASS_NONE &&
           outcome < (robo_paint_pass_outcome_t)ROBO_PAINT_PASS_OUTCOME_COUNT;
}

const char *robo_paint_pass_outcome_name(robo_paint_pass_outcome_t outcome)
{
    switch (outcome) {
    case ROBO_PAINT_PASS_NONE:
        return "none";
    case ROBO_PAINT_PASS_PAINTED:
        return "painted";
    case ROBO_PAINT_PASS_FAILED:
        return "failed";
    case ROBO_PAINT_PASS_STALE:
        return "stale";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Reading a colour out of a computed style                            */
/* ------------------------------------------------------------------ */

/*
 * The one place this module reads a property RoboLayout deliberately ignored, and it is
 * the *paint-visible* half of 10.14's classification rather than a boundary violation:
 * `background-color` and the border colours are the properties 10.14 says layout does
 * not read and paint does. The read is by **name** through
 * `robo_css_computed_style_computed_by_name`, which is the same door
 * `RoboLayoutBorder.c` uses for the same reason: no property id has to have been
 * resolved for the property to be found.
 */

/** The `COLOR` value of @p property in @p style, or a `NONE` colour. */
static robo_css_color_t paint_property_color(const robo_css_computed_style_t *style,
                                            const char *property)
{
    const robo_css_computed_value_t *value;

    if (!style || !property) {
        return robo_css_color_none();
    }
    value = robo_css_computed_style_computed_by_name(style, robo_css_slice_from_text(property));
    if (value && value->kind == ROBO_CSS_COMPUTED_VALUE_COLOR) {
        return robo_css_computed_value_color(value);
    }
    /* Absent, or present as something that is not a colour. Both answer "no colour was
     * declared", which is the statement a caller of this helper can act on. */
    return robo_css_color_none();
}

/** The element's own computed `color`, or a `NONE` colour. */
static robo_css_color_t paint_element_color(const robo_css_computed_style_t *style)
{
    return paint_property_color(style, "color");
}

/**
 * @p color with `currentColor` resolved against @p current, and whether it draws.
 *
 * The resolution rule is `robo_css_color_resolve_current`'s and this wrapper exists for
 * the two decisions beside it:
 *
 *   a `NONE` colour asked to draw becomes `currentColor` **only** when @p default_for_none
 *   is set. That is the difference between the background (`background-color`'s initial
 *   value is `transparent`, so an undeclared background draws nothing) and a border
 *   (`border-*-color`'s initial value is `currentColor`, so an undeclared colour draws in
 *   the element's text colour). Encoding it as a flag keeps one code path for the
 *   resolution and states the two initial values where they differ;
 *
 *   a colour that could not be resolved is **still a colour** the operation may carry
 *   (`currentColor` is legal in a record), so the caller learns "contextual" and draws
 *   anyway. That is what `stats.contextual_colors` counts.
 *
 * @param out_color    receives the colour the operation will carry.
 * @param out_draws    receives 1 when the colour puts a pixel down.
 * @param out_contextual receives 1 when the colour stayed `currentColor`.
 */
static void paint_resolve_color(robo_css_color_t color,
                                robo_css_color_t current,
                                int default_for_none,
                                robo_css_color_t *out_color,
                                int *out_draws,
                                int *out_contextual)
{
    robo_css_color_t resolved;

    if (out_color) {
        *out_color = robo_css_color_none();
    }
    if (out_draws) {
        *out_draws = 0;
    }
    if (out_contextual) {
        *out_contextual = 0;
    }

    if (color.kind == ROBO_CSS_COLOR_NONE) {
        if (!default_for_none) {
            /* An undeclared background: nothing to draw, and nothing contextual either --
             * `transparent` is a colour the author effectively asked for. */
            return;
        }
        /* An undeclared border colour is `currentColor` by CSS's initial value. */
        color = robo_css_color_current();
    }
    if (color.kind == ROBO_CSS_COLOR_INVALID) {
        return;
    }

    if (robo_css_color_resolve_current(color, current, &resolved)) {
        if (out_color) {
            *out_color = resolved;
        }
        if (out_draws) {
            /* A fully transparent colour occupies no pixel, which is a statement about
             * the document rather than an omission -- so it is counted as "does not
             * draw" and no operation is emitted for it. */
            *out_draws = resolved.alpha != 0u;
        }
        return;
    }

    /*
     * It stayed contextual: there was no `color` to substitute. The operation still
     * carries it -- a `currentColor` record is valid (RoboPaintOperation.h) -- and the
     * stat says how many did, because a document where *every* colour is contextual is a
     * document whose styles were not computed.
     */
    if (out_color) {
        *out_color = color;
    }
    if (out_draws) {
        *out_draws = 1;
    }
    if (out_contextual) {
        *out_contextual = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Building one operation                                              */
/* ------------------------------------------------------------------ */

/** The four border kinds, indexed by `robo_css_box_side_t`. */
static const robo_paint_op_kind_t paint_border_kinds[ROBO_CSS_BOX_SIDE_COUNT] = {
    ROBO_PAINT_OP_BORDER_TOP,
    ROBO_PAINT_OP_BORDER_RIGHT,
    ROBO_PAINT_OP_BORDER_BOTTOM,
    ROBO_PAINT_OP_BORDER_LEFT
};

/*
 * Emission is three helpers rather than one with a kind switch, because the three kinds
 * of record carry different fields and filling the wrong ones is the mistake worth
 * making impossible: a structural record fills a colour that says "nothing", a drawing
 * fills a colour a rasteriser can use, and neither can be spelled by accident from the
 * other's helper.
 */

/** Append a **structural** record: a `BEGIN_BOX`/`CONTENT`/`END_BOX`. */
static robo_result_t paint_emit_structural(robo_paint_list_t *list,
                                      robo_paint_op_kind_t kind,
                                      robo_layout_node_id_t node,
                                      size_t depth,
                                      const robo_paint_boxes_t *boxes,
                                      robo_layout_point_t origin,
                                      const robo_paint_clip_t *clip,
                                      robo_paint_diag_t *diag)
{
    robo_paint_operation_t operation;

    robo_paint_operation_init(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = *boxes;
    operation.origin = origin;
    operation.clip = *clip;
    operation.color = robo_css_color_none();
    return robo_paint_list_append(list, &operation, diag);
}

/** Append a drawing operation: a background, or one border side. */
static robo_result_t paint_emit_drawing(robo_paint_list_t *list,
                                       robo_paint_op_kind_t kind,
                                       robo_layout_node_id_t node,
                                       size_t depth,
                                       const robo_paint_boxes_t *boxes,
                                       robo_layout_point_t origin,
                                       const robo_paint_clip_t *clip,
                                       robo_css_color_t color,
                                       robo_paint_diag_t *diag)
{
    robo_paint_operation_t operation;

    robo_paint_operation_init(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = *boxes;
    operation.origin = origin;
    operation.clip = *clip;
    operation.color = color;
    return robo_paint_list_append(list, &operation, diag);
}

/* ------------------------------------------------------------------ */
/* The decorations of one placed box                                   */
/* ------------------------------------------------------------------ */

/**
 * Append the background and border operations a placed box's own declarations ask for.
 *
 * Four rules, and each one is "emit only when it draws" applied to a different property:
 *
 *   a background is emitted when `background-color` resolved to a colour that puts a
 *   pixel down. An undeclared background is `transparent` and emits nothing -- which is
 *   why a plain `<div>` has no background operation rather than an invisible one;
 *
 *   a border side is emitted when its **used** width is above zero *and* its colour
 *   draws. The width comes from layout's own used record, so a `border: 5px none` side
 *   -- a declared five that costs no room -- is already a zero here and needs no second
 *   decision;
 *
 *   an undeclared border colour is `currentColor`, because that is CSS's initial value
 *   for the longhand and the reason `RoboLayoutBorder.h` reports `NONE` rather than
 *   inventing `transparent`;
 *
 *   `currentColor` that nothing could resolve is still emitted, and counted.
 *
 * The border record is asked for once and its four used sides read through the layer's
 * own width door, so "which field of the used record is the width" is asked where
 * RoboLayout answers it rather than three times here.
 */
static robo_result_t paint_box_decorations(robo_paint_context_t *context,
                                          const robo_layout_tree_t *tree,
                                          robo_layout_node_id_t id,
                                          size_t depth,
                                          const robo_paint_boxes_t *boxes,
                                          robo_layout_point_t origin,
                                          const robo_paint_clip_t *clip,
                                          robo_paint_list_t *list,
                                          robo_paint_diag_t *diag)
{
    const robo_css_computed_style_t *style = robo_layout_object_style(tree, id);
    robo_css_color_t element_color;
    robo_css_color_t color;
    int draws;
    int contextual;
    robo_layout_border_edges_t borders;
    size_t side;

    if (!style) {
        /* No declaration could be read from this box at all. A fact about the build --
         * the element had no computed style -- and not a failure, so the box contributes
         * a marker pair and no decoration. */
        context->stats.without_style++;
        return ROBO_OK;
    }
    element_color = paint_element_color(style);

    /* The background: `background-color`, which is `transparent` until an author says
     * otherwise. Painted over the **border box**, CSS's initial `background-clip`. */
    color = paint_property_color(style, "background-color");
    paint_resolve_color(color, element_color, 0, &color, &draws, &contextual);
    if (contextual) {
        context->stats.contextual_colors++;
    }
    if (draws) {
        robo_result_t result = paint_emit_drawing(list, ROBO_PAINT_OP_BACKGROUND, id,
                                                  depth, boxes, origin, clip, color, diag);

        if (result != ROBO_OK) {
            return result;
        }
        context->stats.backgrounds++;
    }

    if (robo_layout_object_borders(tree, id, &borders, NULL) != ROBO_OK) {
        /* Declined (a text run or a transparent inline box never reaches here -- both are
         * refused by `robo_paint_boxes_of` first) or absent. Either way there is no used
         * border to draw, which is an answer and not a failure. */
        return ROBO_OK;
    }

    for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
        robo_gfx_pixel_x_t width = 0;

        if (robo_layout_border_edges_width(&borders, (robo_css_box_side_t)side, &width) !=
            ROBO_OK) {
            continue;
        }
        if (width <= 0) {
            /* The used width is the whole of "does this side draw": a zero-width border,
             * a `none` style and an undeclared side are one number here, which is the
             * conclusion RoboLayout already reached. */
            continue;
        }

        switch (side) {
        case ROBO_CSS_BOX_SIDE_TOP:
            color = borders.top.color;
            break;
        case ROBO_CSS_BOX_SIDE_RIGHT:
            color = borders.right.color;
            break;
        case ROBO_CSS_BOX_SIDE_BOTTOM:
            color = borders.bottom.color;
            break;
        default:
            color = borders.left.color;
            break;
        }

        paint_resolve_color(color, element_color, 1, &color, &draws, &contextual);
        if (contextual) {
            context->stats.contextual_colors++;
        }
        if (!draws) {
            continue;
        }
        {
            robo_result_t result = paint_emit_drawing(list, paint_border_kinds[side], id,
                                                      depth, boxes, origin, clip, color,
                                                      diag);

            if (result != ROBO_OK) {
                return result;
            }
        }
        context->stats.border_sides++;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The walk                                                            */
/* ------------------------------------------------------------------ */

/** The boundary @p box's overflow puts in force for its contents, or @p clip unchanged.
 *
 * The clip edge is the **padding box**: CSS clips a box's overflowing contents at its
 * padding edge, and `robo_layout_overflow_edges_clip_rect` reads the same box one layer
 * down. Nothing here decides whether to clip -- the box's own used modes say that, and a
 * boundary that hides neither axis leaves the clip in force untouched, which is the case
 * every box in an ordinary document produces.
 */
static robo_result_t paint_child_clip(robo_paint_context_t *context,
                                     const robo_layout_tree_t *tree,
                                     robo_layout_node_id_t id,
                                     const robo_paint_boxes_t *boxes,
                                     const robo_paint_clip_t *clip,
                                     robo_paint_clip_t *out,
                                     robo_paint_diag_t *diag)
{
    robo_layout_overflow_edges_t overflow;

    *out = *clip;
    if (robo_layout_object_overflow(tree, id, &overflow, NULL) != ROBO_OK) {
        return ROBO_OK;
    }
    if (!robo_layout_overflow_edges_clips_any(&overflow)) {
        return ROBO_OK;
    }
    context->stats.clipped++;
    return robo_paint_clip_intersect(clip, &boxes->padding, overflow.horizontal,
                                    overflow.vertical, out, diag);
}

/**
 * Paint @p id and, under it, everything it contains.
 *
 * @param placed  non-zero when @p origin is a real containing-block content origin --
 *                that is, when every ancestor was a placement. A box reached under a box
 *                nothing placed has no origin to be painted at, so it is counted and its
 *                subtree is walked for the counts and emitted nowhere.
 */
static robo_result_t paint_walk(robo_paint_context_t *context,
                                const robo_layout_tree_t *tree,
                                robo_layout_node_id_t id,
                                size_t depth,
                                int placed_ancestor,
                                robo_layout_point_t origin,
                                const robo_paint_clip_t *clip,
                                robo_paint_list_t *list,
                                robo_paint_diag_t *diag)
{
    robo_layout_object_kind_t kind = robo_layout_object_kind(tree, id);
    robo_layout_node_id_t child;
    int placed = 0;
    robo_paint_boxes_t local;
    robo_paint_boxes_t boxes;
    robo_layout_point_t child_origin = origin;
    robo_paint_clip_t child_clip = *clip;

    if (!robo_layout_object_kind_is_known(kind) || kind == ROBO_LAYOUT_OBJECT_NONE) {
        /* A child link that names nothing. Reported rather than skipped: a pass that
         * painted the rest of the document and said nothing would hand back a list
         * nobody asked for (RoboPaintContext.h). */
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, ROBO_ERROR,
                               "a child link names no object in this tree");
    }

    context->stats.visited++;
    if (depth > context->stats.depth_max) {
        context->stats.depth_max = (uint32_t)depth;
    }

    if (kind == ROBO_LAYOUT_OBJECT_TEXT) {
        /*
         * Visited, counted, and not painted -- and the reason is stated in the stats
         * record rather than here: a run's rectangle is a line fragment with no box
         * layers and painting one means shaping, which Phase 14 owns. A text object is a
         * leaf, so there is nothing below it to walk.
         */
        context->stats.text_runs++;
        return ROBO_OK;
    }

    /* The one number a later paint-invalidation pass owes, counted where it can be:
     * a box whose only staleness is paint-visible. Read through layout's own door, so
     * "does this box need layout?" is answered by the model that maintains the bits. */
    {
        uint32_t state = robo_layout_tree_invalidation_state(tree, id);

        if ((state & ROBO_LAYOUT_INVALIDATION_STYLE) != 0u &&
            (state & ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS) == 0u) {
            context->stats.style_only++;
        }
    }

    if (placed_ancestor &&
        robo_paint_boxes_of(tree, id, &local, NULL) == ROBO_OK) {
        placed = 1;
    }
    if (!placed) {
        /* Nothing placed this box, so there is no rectangle and no content origin for
         * anything inside it. Counted, walked for the counts, and emitted nowhere. */
        context->stats.unplaced++;
        child = ROBO_LAYOUT_NODE_ID_NONE;
        if (robo_layout_object_first_child(tree, id, &child) != ROBO_OK) {
            return ROBO_OK;
        }
        while (child != ROBO_LAYOUT_NODE_ID_NONE) {
            robo_result_t result = paint_walk(context, tree, child, depth + 1u, 0, origin,
                                              clip, list, diag);

            if (result != ROBO_OK) {
                return result;
            }
            if (robo_layout_object_next_sibling(tree, child, &child) != ROBO_OK) {
                break;
            }
        }
        return ROBO_OK;
    }

    /* Into one page space: the accumulated origin added to what layout wrote. */
    (void)robo_paint_boxes_translate(&local, origin.x, origin.y, &boxes, NULL);

    {
        robo_result_t result = paint_emit_structural(list, ROBO_PAINT_OP_BEGIN_BOX, id,
                                                     depth, &boxes, origin, clip, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    context->stats.boxes++;

    /*
     * The decorations carry the box's own depth, so that a decoration and its marker can
     * never disagree about how deep the box is.
     */
    {
        robo_result_t result = paint_box_decorations(context, tree, id, depth, &boxes,
                                                     origin, clip, list, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }

    /*
     * The **content boundary**, emitted for every placed box whether or not it declared
     * a background, drew a border or has children -- because what it states is a fact
     * about the sequence. It is the explicit end of this box's own records and the start
     * of its subtree's: a consumer reads a box's own painting as `[BEGIN_BOX .. CONTENT]`
     * and its contents as `(CONTENT .. END_BOX)` without having to infer the boundary
     * from the depth of the next child's marker, and a childless box has a boundary just
     * as a parent does. It is a structural record, so it draws nothing and carries no
     * colour (RoboPaintTypes.h).
     */
    {
        robo_result_t result = paint_emit_structural(list, ROBO_PAINT_OP_CONTENT, id, depth,
                                                     &boxes, origin, clip, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }

    if (paint_child_clip(context, tree, id, &boxes, clip, &child_clip, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_ERROR;
    }
    /* The containing block's content **origin**, in paint coordinates: the child's
     * layout geometry is relative to this point, which is what makes `local = boxes -
     * origin` the space layout actually wrote (RoboPaintOperation.h). */
    child_origin = robo_gfx_point(boxes.content.x, boxes.content.y);

    child = ROBO_LAYOUT_NODE_ID_NONE;
    if (robo_layout_object_first_child(tree, id, &child) == ROBO_OK) {
        while (child != ROBO_LAYOUT_NODE_ID_NONE) {
            robo_result_t result = paint_walk(context, tree, child, depth + 1u, placed,
                                              child_origin, &child_clip, list, diag);

            if (result != ROBO_OK) {
                return result;
            }
            if (robo_layout_object_next_sibling(tree, child, &child) != ROBO_OK) {
                break;
            }
        }
    }

    {
        robo_result_t result = paint_emit_structural(list, ROBO_PAINT_OP_END_BOX, id,
                                                     depth, &boxes, origin, clip, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The context                                                         */
/* ------------------------------------------------------------------ */

robo_result_t robo_paint_context_create(robo_paint_context_t **out_context,
                                        robo_paint_diag_t *diag)
{
    robo_paint_context_t *context;

    if (!out_context) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CONTEXT, ROBO_INVALID_ARGUMENT,
                               "out_context is NULL");
    }
    *out_context = NULL;
    context = (robo_paint_context_t *)robo_calloc(1u, sizeof(*context));
    if (!context) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CONTEXT, ROBO_OUT_OF_MEMORY,
                               "could not allocate the paint context");
    }
    /* calloc already gives the empty state -- zeroed counters, outcome NONE, no tree.
     * The explicit assignment is here so that the definition of "empty" lives in one
     * place rather than in the allocator's behaviour. */
    context->outcome = ROBO_PAINT_PASS_NONE;
    context->tree = NULL;
    context->user_data = NULL;
    *out_context = context;
    return ROBO_OK;
}

void robo_paint_context_destroy(robo_paint_context_t *context)
{
    /* The tree, the list and the caller's user data are references, so there is nothing
     * to release but the struct itself -- which is what makes the destroy safe in any
     * order relative to the things it was used with. */
    robo_free(context);
}

void robo_paint_context_reset(robo_paint_context_t *context)
{
    if (!context) {
        return;
    }
    memset(&context->stats, 0, sizeof(context->stats));
    context->outcome = ROBO_PAINT_PASS_NONE;
    context->tree = NULL;
    /* user_data is deliberately left alone: it belongs to the caller, and a reset that
     * dropped it would break a caller that installs it once and resets per pass -- the
     * normal way to use this door, and RoboLayout's context makes the same call. */
}

void robo_paint_context_set_user_data(robo_paint_context_t *context, void *user_data)
{
    if (context) {
        context->user_data = user_data;
    }
}

void *robo_paint_context_user_data(const robo_paint_context_t *context)
{
    return context ? context->user_data : NULL;
}

robo_result_t robo_paint_context_run(robo_paint_context_t *context,
                                    const robo_layout_tree_t *tree,
                                    robo_paint_list_t *list,
                                    robo_paint_diag_t *diag)
{
    size_t completed;
    robo_layout_node_id_t root;
    robo_result_t result;

    if (!context || !tree || !list) {
        if (context) {
            context->outcome = ROBO_PAINT_PASS_FAILED;
        }
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CONTEXT, ROBO_INVALID_ARGUMENT,
                               "context, tree and list are all required");
    }

    /*
     * Per-pass fields are reset; `passes` is cumulative and is not -- the same split
     * RoboLayout's context uses, and for the same reason: a caller reading stats after a
     * second pass wants that pass's numbers, and a caller asking how many passes have run
     * wants the total. Zeroing the record and then incrementing makes `passes`
     * permanently 1, which reads as "this context ran once" for a context that ran five
     * times.
     */
    completed = context->stats.passes;
    memset(&context->stats, 0, sizeof(context->stats));
    context->stats.passes = completed;
    context->stats.objects = robo_layout_tree_object_count(tree);
    context->tree = tree;

    /*
     * The list is cleared first, so a caller may reuse one list across passes and never
     * sees a previous document's operations. Clearing before the staleness check rather
     * than after is deliberate: a refused pass hands back an **empty** list rather than
     * the previous pass's, which is the only answer a caller can act on.
     */
    robo_paint_list_clear(list);

    /*
     * A tree whose geometry is not final is refused rather than painted
     * (RoboPaintContext.h): a list built from half-computed geometry is a wrong list, and
     * a caller that wanted a preview can run a layout pass first.
     */
    if (robo_layout_tree_needs_layout(tree)) {
        /*
         * A **fourth outcome, not a fifth error**: the walk did not run, the list is
         * empty, and `ROBO_OK` is the honest result code because nothing went wrong that
         * a caller cannot fix. A caller acts on the outcome; one that reads only the code
         * sees success with an empty list, which is exactly what a refused pass is.
         */
        context->outcome = ROBO_PAINT_PASS_STALE;
        context->stats.passes++;
        return ROBO_OK;
    }

    root = robo_layout_tree_root(tree);
    if (root == ROBO_LAYOUT_NODE_ID_NONE) {
        /*
         * No root box: a document whose root element generates no box, or one that builds
         * no boxes at all. A legitimate pass that paints nothing -- `objects` is already
         * 0 and every counter is already right -- rather than an error.
         */
        context->outcome = ROBO_PAINT_PASS_PAINTED;
        context->stats.passes++;
        return ROBO_OK;
    }

    result = paint_walk(context, tree, root, 0u, 1, robo_gfx_point(0, 0),
                        &(robo_paint_clip_t){ { ROBO_CSS_OVERFLOW_VISIBLE, 0, 0 },
                                              { ROBO_CSS_OVERFLOW_VISIBLE, 0, 0 } },
                        list, diag);
    if (result != ROBO_OK) {
        context->outcome = ROBO_PAINT_PASS_FAILED;
        return result;
    }

    context->stats.ops = robo_paint_list_count(list);
    context->outcome = ROBO_PAINT_PASS_PAINTED;
    context->stats.passes++;
    return ROBO_OK;
}

void robo_paint_context_stats(const robo_paint_context_t *context, robo_paint_stats_t *out)
{
    if (!out) {
        return;
    }
    if (!context) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = context->stats;
}

robo_paint_pass_outcome_t robo_paint_context_outcome(const robo_paint_context_t *context)
{
    return context ? context->outcome : ROBO_PAINT_PASS_NONE;
}

const robo_layout_tree_t *robo_paint_context_tree(const robo_paint_context_t *context)
{
    return context ? context->tree : NULL;
}
