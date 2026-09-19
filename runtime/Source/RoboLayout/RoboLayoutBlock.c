/*
 * RoboLayout block layout (Phase 10.3) — see RoboLayoutBlock.h.
 *
 * One recursion, three questions per box, and the header argues each of them:
 * what is my containing block, how wide am I, how tall am I. The file is short
 * because the phase is narrow, and it is written as a recursion rather than a
 * loop over the flat arena because the two facts a box needs are its parent's
 * *origin* and its parent's *content width* -- neither of which is on the box --
 * so a flat walk would have to recompute them or keep a stack, which is what the
 * call stack is.
 *
 * Everything here is a pure function of the tree, the viewport and the borrowed
 * styles: nothing is cached between calls, nothing reads a previous pass, and the
 * traversal order is the tree's own -- creation order for boxes, sibling order for
 * children. That is what makes the pass deterministic (the header's last section)
 * and it is why there is no accumulator struct threaded through the recursion
 * beyond the stats record, which is a count and not an input.
 *
 * The one place this file could quietly go wrong is the ordering of the writes.
 * A box's height is not known until its children are laid out, so its own geometry
 * is written *after* theirs -- and a reader that expected parents-before-children
 * would be reading a box whose result is still UNRESOLVED while its children's are
 * COMPUTED. That is stated in the header rather than papered over by writing a
 * provisional geometry first: a provisional `(0, 0)` is exactly the lie 10.1 built
 * the result *state* to avoid, and writing one would make `UNRESOLVED` mean "not
 * yet" instead of "not known".
 */

#include "RoboLayout/RoboLayoutBlock.h"

#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSDisplay.h"
#include "RoboCSS/RoboCSSUnit.h"

#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutResult.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The declared height this build can resolve                          */
/* ------------------------------------------------------------------ */

/**
 * The computed LENGTH in px a style declares for @p property, when there is one.
 *
 * The whole of this phase's property reading, and it is deliberately four checks
 * rather than a resolver:
 *
 *   the entry must exist. A style with no `height` entry is an element nothing
 *   declared one for -- `auto`, by CSS's initial value.
 *
 *   the kind must be LENGTH. `auto` is a KEYWORD, `50%` is a PERCENTAGE and
 *   `2em` is a LENGTH in a relative unit: all three are values this build cannot
 *   turn into pixels without a containing block or a font, and 9.10 says so by
 *   keeping them in their own kinds and units rather than flattening them.
 *
 *   the unit must be px. 9.8's computed rule normalises an *absolute* unit to px
 *   exactly, so `1in` arrives as 96px and is usable; anything still spelled in
 *   another unit is relative and is not.
 *
 *   the number must be finite, non-negative and inside the range the geometry
 *   can hold. `robo_gfx_size_t` is `int32_t`, and a declared height of `3e9px`
 *   or `-1px` is not a size a box can have.
 *
 * A value that fails any check is treated exactly like `auto` and the height comes
 * from the content. That is not a fallback in the apologetic sense: it is the
 * honest answer, because in this build those values *are* unresolved, and the
 * alternative -- refusing the whole pass over one declaration an author wrote
 * legally -- would make a document with `height: 50%` unlayoutable.
 */
int robo_layout_style_px_length(const robo_css_computed_style_t *style,
                                const char *property,
                                double *out_value)
{
    const robo_css_computed_value_t *value;

    if (!style || !property || !out_value) {
        return 0;
    }
    value = robo_css_computed_style_computed_by_name(style,
                                                     robo_css_slice_from_text(property));
    if (!value) {
        return 0;
    }
    if (value->kind != ROBO_CSS_COMPUTED_VALUE_LENGTH) {
        return 0;
    }
    if (value->unit != ROBO_CSS_UNIT_PX) {
        return 0;
    }
    if (value->unresolved) {
        return 0;
    }
    if (!(value->number >= 0.0) || !(value->number <= 2147483647.0)) {
        /* Deliberately written as a negated pair so that a NaN -- which compares
         * false against everything -- fails the first test rather than slipping
         * through a `number < 0` check. */
        return 0;
    }
    *out_value = value->number;
    return 1;
}

/**
 * Round a resolved CSS length to the integer pixel space the geometry uses.
 *
 * Half away from zero, which for the non-negative values the check above admits
 * is half up. Stated because it is a *decision*: `height: 10.5px` has to become
 * one of two integers, and a build that truncated would make every fractional
 * declared height come out one pixel short of what the author wrote.
 */
robo_gfx_pixel_x_t robo_layout_px_round(double value)
{
    return (robo_gfx_pixel_x_t)(value + 0.5);
}

/* ------------------------------------------------------------------ */
/* The recursion                                                       */
/* ------------------------------------------------------------------ */

static robo_result_t layout_block_box(robo_layout_walk_t *walk,
                                      robo_layout_node_id_t id,
                                      robo_layout_point_t origin,
                                      int32_t available_width,
                                      robo_gfx_pixel_y_t *out_height);

/**
 * Whether this pass places @p object, and the reason when it does not.
 *
 * The three-way answer is the whole placement rule, and the two rejections are
 * *different facts* that a single boolean would merge: "10.4 lays this out along a
 * line" is a normal document, and "this is block-level but the context it
 * participates in is not a block-direction one" is a contradiction between 10.2's
 * classification and CSS that cannot happen.
 */
typedef enum layout_block_verdict {
    /** Place it: block-level, in a block-direction context. */
    LAYOUT_BLOCK_PLACE = 0,
    /** Inline-level, or in an inline context. 10.4 owns it. */
    LAYOUT_BLOCK_INLINE_LEVEL = 1,
    /** Block-level but not in a block-direction context. Impossible; reported. */
    LAYOUT_BLOCK_CONTRADICTION = 2
} layout_block_verdict_t;

static layout_block_verdict_t layout_block_verdict_for(const robo_layout_tree_t *tree,
                                                      const robo_layout_object_t *object)
{
    robo_layout_formatting_context_t context;
    int in_block_flow = 0;

    if (!robo_css_display_is_block_level(object->display)) {
        return LAYOUT_BLOCK_INLINE_LEVEL;
    }
    /*
     * The gate 10.2 exists to provide. Asked of the context the box *participates*
     * in, not the one it owns: a box establishes a context for its contents, and
     * its own layout happens in somebody else's.
     */
    if (robo_layout_tree_context(tree, object->context, &context, NULL) == ROBO_OK) {
        in_block_flow = robo_layout_fc_kind_is_block_container(context.kind);
    }
    return in_block_flow ? LAYOUT_BLOCK_PLACE : LAYOUT_BLOCK_CONTRADICTION;
}

/**
 * Stack @p id's block-level children from its content origin and report the
 * extent they reached.
 *
 * The cursor is local, so it is the *content* extent and not a coordinate: the
 * caller has already decided where this box's content origin is, and adding it
 * here would put the origin in two places.
 *
 * Children are visited in sibling order, which the builder made document order.
 * A child that is not placed is counted and **not descended into**: an inline box
 * has no geometry in this build, so a descendant placed inside it would be placed
 * inside nothing. Those descendants are counted as unreached by the caller's
 * difference rather than walked to be counted, which keeps the pass linear in the
 * boxes it places rather than in the boxes that exist.
 */
static robo_result_t layout_block_children(robo_layout_walk_t *walk,
                                           const robo_layout_object_t *object,
                                           int32_t content_width,
                                           robo_gfx_pixel_y_t *out_extent)
{
    robo_layout_node_id_t child = object->first_child;
    robo_gfx_pixel_y_t cursor = 0;
    int placed_any = 0;

    /*
     * The inline direction first, because it is a different *question* about the
     * same box and not a different kind of child: when the box established an
     * INLINE formatting context, its contents are participants of a line and 10.4
     * owns every one of them. The distinction is 10.2's, read here through the
     * context the box owns -- which is the same door 10.2's classifier wrote, so
     * there is no second rule about which boxes have lines in them.
     *
     * A box that establishes an inline context has no block-level child, so the
     * block direction has nothing to stack and the `empty_containers` count below is
     * exactly what the loop would have produced: the extent this call hands back is
     * the lines' instead of a cursor's, and the box's own height rule (declared, or
     * the content's extent) is unchanged.
     */
    if (object->context_owned != ROBO_LAYOUT_FC_ID_NONE) {
        robo_layout_formatting_context_t owned;

        if (robo_layout_tree_context(walk->tree, object->context_owned, &owned, NULL) ==
                ROBO_OK &&
            owned.kind == ROBO_LAYOUT_FC_INLINE) {
            walk->block->empty_containers++;
            return robo_layout_inline_walk_contents(walk, object->id, content_width,
                                                    out_extent);
        }
    }

    while (child != ROBO_LAYOUT_NODE_ID_NONE) {
        const robo_layout_object_t *child_object = robo_layout_object_slot_const(walk->tree,
                                                                                child);

        if (!child_object) {
            return ROBO_LAYOUT_DIAG(walk->diag, ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT, ROBO_ERROR,
                                    "a box's child link names no box in this tree");
        }

        switch (layout_block_verdict_for(walk->tree, child_object)) {
        case LAYOUT_BLOCK_PLACE: {
            robo_layout_point_t child_origin;
            robo_gfx_pixel_y_t child_height = 0;

            child_origin.x = 0;
            child_origin.y = cursor;
            if (layout_block_box(walk, child, child_origin, content_width, &child_height) !=
                ROBO_OK) {
                return walk->diag ? walk->diag->code : ROBO_ERROR;
            }
            cursor += child_height;
            placed_any = 1;
            break;
        }
        case LAYOUT_BLOCK_INLINE_LEVEL:
            /*
             * Inline-level, in a **block-direction** context — so there is no line
             * to put it on and this is the mixed-content case the header describes:
             * CSS wraps the inline run in an anonymous block box, this build makes
             * none, and the honest answer is to count it and leave it UNRESOLVED.
             * The inline pass never sees one of these, because it walks the contents
             * of inline contexts and this box is not in one.
             */
            walk->block->inline_level_skipped++;
            break;
        case LAYOUT_BLOCK_CONTRADICTION:
        default:
            return ROBO_LAYOUT_DIAG(
                walk->diag, ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT, ROBO_ERROR,
                "a block-level box participates in a context that is not a block "
                "formatting context");
        }

        child = child_object->next_sibling;
    }

    if (placed_any) {
        walk->block->containers++;
    } else {
        walk->block->empty_containers++;
    }
    *out_extent = cursor;
    return ROBO_OK;
}

/**
 * Lay out @p owner's contents in the block direction — the door the inline pass
 * calls for an atomic inline's contents (RoboLayoutInternal.h).
 *
 * A box is looked up and the child loop is the one above: an `inline-block`'s
 * contents are an ordinary block formatting context (10.2's rule for `flow-root`),
 * so there is no second walk and no second set of counters.
 */
robo_result_t robo_layout_block_walk_children(robo_layout_walk_t *walk,
                                              robo_layout_node_id_t owner,
                                              int32_t content_width,
                                              robo_gfx_pixel_y_t *out_extent)
{
    const robo_layout_object_t *object;

    if (!walk || !walk->tree || !walk->block || !out_extent) {
        return ROBO_LAYOUT_DIAG(walk ? walk->diag : NULL, ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT,
                                ROBO_INVALID_ARGUMENT, "the pass needs a walk and an out");
    }
    object = robo_layout_object_slot_const(walk->tree, owner);
    if (!object) {
        return ROBO_LAYOUT_DIAG(walk->diag, ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT, ROBO_ERROR,
                                "the box that owns a block context is not in the tree");
    }
    return layout_block_children(walk, object, content_width, out_extent);
}

/**
 * Place one block-level box at @p origin, lay its contents out, and report its
 * height.
 *
 * @p origin is in the box's *containing block's* space, which is the parent's
 * content origin -- so a child's position is `(0, cursor)` and the parent's
 * absolute position is already accounted for one level up. That is the same
 * convention `robo_layout_result_t.position` documents, and it is why no
 * coordinate in this file is ever a document-space one.
 *
 * @p available_width is the containing block's *content* width, which today is
 * the same number as its border-box width because padding and borders are not
 * applied (10.10, 10.11). It is passed as `int32_t` rather than as a size because
 * a box has no height available to it in this phase: a block box's height is
 * decided by its contents, never by its containing block.
 */
static robo_result_t layout_block_box(robo_layout_walk_t *walk,
                                      robo_layout_node_id_t id,
                                      robo_layout_point_t origin,
                                      int32_t available_width,
                                      robo_gfx_pixel_y_t *out_height)
{
    robo_layout_object_t *object = robo_layout_object_slot(walk->tree, id);
    robo_gfx_pixel_y_t content_extent = 0;
    robo_layout_size_t size;
    double declared = 0.0;
    int has_declared;
    robo_result_t status;

    if (!object) {
        return ROBO_LAYOUT_DIAG(walk->diag, ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT, ROBO_ERROR,
                                "a box vanished under the pass");
    }

    /* The children first, because an `auto` height *is* their extent -- and
     * because if this box declares a height, the children still have to be laid
     * out inside it. */
    status = layout_block_children(walk, object, available_width, &content_extent);
    if (status != ROBO_OK) {
        return status;
    }

    has_declared = robo_layout_style_px_length(object->style, "height", &declared);
    size.width = available_width;
    if (has_declared) {
        size.height = robo_layout_px_round(declared);
        walk->block->heights_explicit++;
    } else {
        size.height = content_extent;
        walk->block->heights_from_content++;
    }

    status = robo_layout_result_set_geometry(&object->result, origin, size, walk->diag);
    if (status != ROBO_OK) {
        return status;
    }

    walk->block->blocks_placed++;
    if (object->depth > walk->block->depth_max) {
        walk->block->depth_max = object->depth;
    }
    *out_height = size.height;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

void robo_layout_block_stats_init(robo_layout_block_stats_t *stats)
{
    if (!stats) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

int robo_layout_block_stats_equal(const robo_layout_block_stats_t *a,
                                  const robo_layout_block_stats_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->containers == b->containers && a->blocks_placed == b->blocks_placed &&
           a->inline_level_skipped == b->inline_level_skipped &&
           a->unreached == b->unreached &&
           a->heights_explicit == b->heights_explicit &&
           a->heights_from_content == b->heights_from_content &&
           a->empty_containers == b->empty_containers &&
           a->depth_max == b->depth_max;
}

robo_result_t robo_layout_block_layout(robo_layout_tree_t *tree,
                                       robo_layout_size_t viewport,
                                       robo_layout_block_stats_t *out_stats,
                                       robo_layout_inline_stats_t *out_inline_stats,
                                       robo_layout_diag_t *diag)
{
    robo_layout_walk_t walk;
    robo_layout_inline_stats_t scratch;
    robo_layout_node_id_t root;
    robo_layout_point_t origin;
    robo_gfx_pixel_y_t height = 0;

    if (!tree) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "tree is NULL");
    }
    if (viewport.width < 0 || viewport.height < 0) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "viewport is negative");
    }

    /*
     * Both records are cleared together and at the same point, which is the rule
     * 10.3's header states for the block one: an *argument* refusal above happens
     * before a caller's records are touched, and everything from here on -- including
     * the refusal below -- leaves them describing *this* call rather than a previous
     * one. A caller that asked only for the block numbers passes NULL and the inline
     * record is a local that nothing reads.
     */
    robo_layout_block_stats_init(out_stats);
    robo_layout_inline_stats_init(out_inline_stats);
    robo_layout_inline_stats_init(&scratch);

    /*
     * The classification first, because it is the placement gate. Reported rather
     * than repaired: a caller that asked for block layout without asking for
     * classification has skipped the phase that decides which boxes this pass
     * owns, and inventing an answer would be inventing a second classifier.
     */
    if (!robo_layout_tree_is_classified(tree)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT, ROBO_ERROR,
                                "block layout needs a classified tree");
    }

    root = robo_layout_tree_root(tree);
    if (root == ROBO_LAYOUT_NODE_ID_NONE) {
        /* A document whose root element generates no box. There is nothing to
         * place, and that is an answer about the document rather than a failure --
         * but it does mean this call placed nothing, and the caller is told so
         * instead of being handed an empty stats record that looks the same as a
         * pass over a viewport-free tree. */
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT, ROBO_NOT_FOUND,
                                "the tree has no root box");
    }

    walk.tree = tree;
    walk.block = out_stats;
    walk.inline_flow = out_inline_stats ? out_inline_stats : &scratch;
    walk.diag = diag;

    /*
     * The root's containing block is the initial containing block, which is the
     * viewport. Every other box's containing block is its parent -- reached by
     * the recursion below and never by asking a formatting context.
     */
    origin.x = 0;
    origin.y = 0;
    {
        robo_result_t status = layout_block_box(&walk, root, origin,
                                               (int32_t)viewport.width, &height);

        if (status != ROBO_OK) {
            return status;
        }
    }

    /*
     * The boxes this pass did not reach, counted here rather than during the
     * recursion: a box inside an unplaced box is never visited, so counting it
     * would mean walking it, and the arena already knows how many boxes there
     * are. `unreached` is therefore a *difference* and its definition is
     * "boxes with no geometry that neither pass declined for a stated reason" --
     * a block-level box inside an inline box, which needs the anonymous box this
     * build does not make.
     *
     * Both records are subtracted, because they are both answers to "why has this
     * box no geometry?": 10.3's `inline_level_skipped` is an inline-level box in a
     * block-direction context, and 10.4's `block_level_declined` is its mirror. A
     * box the inline pass *placed* is subtracted too, so the identity below closes
     * over both directions at once:
     *
     *   blocks_placed + inline_level_skipped + participants_placed + unreached
     *       == objects
     *
     * and the suite asserts it, because a build that dropped a box would have four
     * right-looking counts and a document that did not add up.
     */
    {
        size_t boxes = robo_layout_tree_object_count(tree);
        size_t explained = out_stats->inline_level_skipped;

        explained += walk.inline_flow->participants_placed;
        if (boxes > out_stats->blocks_placed + explained) {
            out_stats->unreached = boxes - out_stats->blocks_placed - explained;
        }
    }

    return ROBO_OK;
}
