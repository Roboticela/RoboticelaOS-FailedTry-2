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
 *
 * 10.8 changed what 10.3 *passed* here, and the change is one line with a long
 * shadow. Where this recursion used to hand every child its containing block's width,
 * it now hands it the box's **resolved** width (`RoboLayoutSizing.h`), because a box
 * that declares `width: 300px` is its children's containing block and 300 is what
 * they are laid out into. For an `auto` width the two are the same number -- which is
 * why every 10.3 expectation about an undeclared width still holds -- and the
 * recursion's *shape* is untouched: the width resolves first because it depends on
 * nothing, the children go in at it, and the height follows because it depends on
 * them. That ordering is what the two dimension resolvers exist for.
 */

#include "RoboLayout/RoboLayoutBlock.h"

#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSDisplay.h"

#include "RoboLayout/RoboLayoutSizing.h"

#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutResult.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* What a box's declarations ask for                                   */
/* ------------------------------------------------------------------ */

/**
 * The px length @p style names for @p property, when it names a usable one.
 *
 * Written on top of `robo_layout_sizing_read_dimension` rather than beside it, and
 * that is the phase: the four checks that used to live here -- an entry that exists,
 * a kind that is a length, a unit that is `px`, a number that is finite, 
 * non-negative and inside the geometry's range -- are now one classification in
 * RoboLayoutSizing.c. What is left here is the *bound* reading's shape: "a bound, or
 * none", where None covers `auto` and every value this build cannot resolve.
 */
static int layout_sizing_bound_px(const robo_css_computed_style_t *style,
                                  const char *property,
                                  robo_gfx_pixel_x_t *out)
{
    robo_layout_dimension_t dimension = robo_layout_dimension_auto();
    robo_gfx_pixel_x_t length = 0;

    if (!robo_layout_sizing_read_dimension(style, property, &dimension)) {
        return 0;
    }
    if (robo_layout_dimension_length(&dimension, &length) != ROBO_OK) {
        return 0;
    }
    *out = length;
    return 1;
}

/**
 * Fill @p sizing from @p object: what its containing block offers, what its
 * declarations ask for, and what its bounds allow.
 *
 * Defined here and called from both directions (RoboLayoutInternal.h), because the
 * question -- "what does *this* box's style ask for, into this containing block?" --
 * is the same one for a block box and for an atomic inline, and two implementations
 * of it would answer differently the day either gained a property read.
 *
 * Three reads, and the two dimension ones are deliberately unguarded. A box with no
 * computed style has made no declarations, and the record `init` wrote already says
 * `AUTO`; `robo_layout_sizing_read_dimension` fills only when it can answer, so the
 * unguarded form is the *contract* rather than a shortcut.
 *
 * Only the *width* is offered, and that asymmetry is CSS's: a block box's height is
 * its contents', never its containing block's, so there is no available height to
 * record. `has_available_height` exists for the goal that gives `height: 50%`
 * something to resolve against, and leaving it clear is how this build says "nobody
 * has told me, and I am not going to assume zero".
 *
 * The bounds are read through the same seam as the dimensions, and RoboCSS registers
 * no `min-width`, so for every document this build can parse there is no entry and no
 * bound recorded. That is not a stub: the clamping path is live and tested through
 * the sizing record directly, and the day the property table knows those names a
 * style that declares them reaches it here with no other change. A bound is recorded
 * only when **both** of its dimensions name a usable length, because 10.1's
 * constraints record holds a bound as one size and a half-known pair would silently
 * bound the other dimension to whatever the unknown half defaulted to -- a
 * `max-width` alone turning every height into zero.
 */
void robo_layout_object_sizing(robo_layout_sizing_t *sizing,
                               const robo_layout_object_t *object,
                               int32_t available_width)
{
    robo_gfx_pixel_x_t minimum_width = 0;
    robo_gfx_pixel_x_t minimum_height = 0;
    robo_gfx_pixel_x_t maximum_width = 0;
    robo_gfx_pixel_x_t maximum_height = 0;
    robo_layout_size_t bound;

    robo_layout_sizing_init(sizing);
    (void)robo_layout_sizing_set_available_width(sizing, available_width, NULL);

    (void)robo_layout_sizing_read_dimension(object->style, "width", &sizing->width);
    (void)robo_layout_sizing_read_dimension(object->style, "height", &sizing->height);

    if (layout_sizing_bound_px(object->style, "min-width", &minimum_width) &&
        layout_sizing_bound_px(object->style, "min-height", &minimum_height)) {
        bound.width = minimum_width;
        bound.height = minimum_height;
        (void)robo_layout_sizing_set_minimum(sizing, bound, NULL);
    }
    if (layout_sizing_bound_px(object->style, "max-width", &maximum_width) &&
        layout_sizing_bound_px(object->style, "max-height", &maximum_height)) {
        bound.width = maximum_width;
        bound.height = maximum_height;
        (void)robo_layout_sizing_set_maximum(sizing, bound, NULL);
    }
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
             *
             * A **text run** takes this branch too, and that is the same case rather
             * than a second one: text is inline-level by construction
             * (RoboLayoutObject.h), so a text node between two blocks is a run with no
             * line to be on, and it waits for the same anonymous box. Counting it
             * here — rather than measuring it and then having nowhere to put the
             * measurement — is what keeps `unreached` a statement about *structure*
             * and not a bucket of things nobody got round to.
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
    robo_layout_sizing_t sizing;
    robo_layout_margin_edges_t edges;
    robo_layout_borders_t declared_border;
    robo_layout_border_edges_t borders;
    robo_layout_paddings_t declared_padding;
    robo_layout_padding_edges_t padding;
    robo_layout_position_t declared_position;
    robo_layout_position_offset_t position_offset;
    robo_layout_size_t content;
    robo_layout_size_t content_size;
    robo_layout_size_t padding_box;
    robo_layout_size_t size;
    robo_layout_point_t margin_origin;
    robo_layout_point_t final_origin;
    robo_gfx_pixel_y_t content_extent = 0;
    robo_result_t status;

    if (!object) {
        return ROBO_LAYOUT_DIAG(walk->diag, ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT, ROBO_ERROR,
                                "a box vanished under the pass");
    }

    robo_layout_object_sizing(&sizing, object, available_width);

    /*
     * The margins next (10.9), and *before* the width for a reason that is forced
     * rather than tidy: an `auto` width fills whatever the margins leave, so the two
     * horizontal margins have to be settled before there is a number to fill. The
     * declared dimension is what the margin arithmetic reads to decide whether there
     * is anything to distribute -- `auto` margins have nothing to take beside an
     * `auto` width -- which is why the sizing record is built first and *not* resolved
     * yet.
     *
     * The containing block a margin resolves against is the one this box's width
     * resolves against: 10.3's rule that a box's containing block is its parent, with
     * the viewport standing in for the root. One number, two uses, and the second use
     * is the one whose answer the first has to wait for.
     */
    {
        robo_layout_margins_t declared = robo_layout_margins_of(object->style);

        status = robo_layout_margins_resolve_block(&declared, available_width, &sizing.width,
                                                   &edges, walk->diag);
        if (status != ROBO_OK) {
            return status;
        }
    }

    /*
     * Then the box's own padding (10.10), and here it is *not* an ordering decision the
     * way the margins were: a padding neither distributes nor resolves against the
     * containing block, so it is read from the style and used, and the one door it has is
     * the one policy. What it does change is the number the width is resolved against, and
     * that is the next step rather than this one.
     */
    /*
     * Then the box's two insets (10.10, 10.11), and neither is an ordering decision the
     * way the margins were: a border and a padding distribute nothing and resolve against
     * nothing, so both are read from the style and used, and each has one door rather than
     * two policies. What they change is the number the width is resolved against, which is
     * the next step rather than this one.
     *
     * The border resolves first only because it is the outer of the two -- the two calls are
     * independent, and a reader following the arithmetic down the box wants the order the
     * box is built in.
     */
    declared_border = robo_layout_borders_of(object->style);
    status = robo_layout_border_resolve(&declared_border, &borders, walk->diag);
    if (status != ROBO_OK) {
        return status;
    }
    declared_padding = robo_layout_paddings_of(object->style);
    status = robo_layout_padding_resolve(&declared_padding, &padding, walk->diag);
    if (status != ROBO_OK) {
        return status;
    }

    /*
     * What the box's own box is resolved against, and the phase's first consequence:
     * the containing block less the margins (10.9), less the **border** (10.11) and less
     * the padding (10.10), because CSS's constraint equation has a term for each. So a box
     * with `border: 5px solid; padding: 10px` and no declared width is exactly as wide as
     * its containing block rather than 30px wider, and both insets come out of the
     * contents' room instead of out of the parent.
     *
     * One subtraction per layer, in the order the box is built, and each subtraction belongs
     * to the layer that owns the property: the border layer turns the containing block into
     * the **padding box** width and the padding layer turns that into the **content** width.
     * It happens *here* rather than inside the sizing layer, because the sizing layer does
     * not know either property exists -- "how much room is there" is the caller's question
     * and the answer is a number.
     */
    {
        int32_t padding_box_available = 0;
        int32_t content_available = 0;

        status = robo_layout_border_edges_padding_box_width(&borders,
                                                           edges.available_content_width,
                                                           &padding_box_available, walk->diag);
        if (status != ROBO_OK) {
            return status;
        }
        status = robo_layout_padding_edges_content_width(&padding,
                                                        padding_box_available,
                                                        &content_available, walk->diag);
        if (status != ROBO_OK) {
            return status;
        }
        status = robo_layout_sizing_set_available_width(&sizing, content_available,
                                                       walk->diag);
        if (status != ROBO_OK) {
            return status;
        }
    }

    /*
     * The width next (10.8), because the children are laid out *into* this box's
     * content box and so the number they get is the resolved one rather than the
     * containing block's. That is the difference the phase adds, and it is one line:
     * for an `auto` width the two are the same number, because `auto` fills the
     * containing block, and every 10.3 expectation about a box that declares no width
     * is therefore untouched.
     *
     * And what the number *is* is 10.10's question, whose answer is one word:
     * `content-box`. This build is CSS's initial `box-sizing` value, so a declared
     * `width` is the **content** box and the resolved width is exactly what this box's
     * children are laid out into -- unchanged from 10.8, and unchanged *because* of the
     * model rather than in spite of it. The border box a result carries is that number
     * plus the padding, derived below. A build that had read the declaration as a border
     * box would subtract here and give the children less than their author asked for.
     */
    status = robo_layout_sizing_resolve_width(&sizing, walk->diag);
    if (status != ROBO_OK) {
        return status;
    }

    /*
     * The children next, because an `auto` height *is* their extent -- and because if
     * this box declares a height, the children still have to be laid out inside it.
     */
    status = layout_block_children(walk, object, (int32_t)sizing.resolved.width,
                                   &content_extent);
    if (status != ROBO_OK) {
        return status;
    }

    /*
     * Then the height, from what the contents turned out to be. The extent is
     * reported at the *resolved* width as well as the stacked height, because a
     * contents extent is what would hold the children and a later goal that grows a
     * child beyond its parent needs both numbers to say so.
     */
    content.width = sizing.resolved.width;
    content.height = content_extent;
    status = robo_layout_sizing_set_content(&sizing, content, walk->diag);
    if (status != ROBO_OK) {
        return status;
    }
    status = robo_layout_sizing_resolve_height(&sizing, walk->diag);
    if (status != ROBO_OK) {
        return status;
    }
    /*
     * And the resolved pair is the box's **content** box, which is what the sizing layer
     * knows about; the box a reader sees is that plus the padding and the border. That split
     * is the phase in two lines -- three since 10.11 -- and it is why the numbers are
     * computed here rather than inside the layer: the layer answers "how big did this box's
     * declarations ask it to be", and "how big is the box" is the answer of the two layers
     * that own the properties the layer has never heard of.
     *
     * One addition per layer, in the order the box is built, and it is the exact mirror of
     * the subtraction above: the padding layer turns the content box into the **padding box**
     * and the border layer turns that into the **border box**, which is what a result's
     * `size` carries. A build that composed them the other way round would still get the
     * same number here -- addition commutes -- which is why the *downward* direction is
     * where the order is load-bearing and why the suite drives both.
     */
    status = robo_layout_sizing_resolved(&sizing, &content_size);
    if (status != ROBO_OK) {
        return status;
    }
    status = robo_layout_padding_edges_padding_box(&padding, content_size, &padding_box,
                                                    walk->diag);
    if (status != ROBO_OK) {
        return status;
    }
    status = robo_layout_border_edges_border_box(&borders, padding_box, &size, walk->diag);
    if (status != ROBO_OK) {
        return status;
    }

    /* Counted from the *source* rather than from the declaration, so the two pairs of
     * counters mean the same thing on every path: a width is declared or it filled
     * what it was given, and a height is declared or it came from the contents. A
     * declaration this build could not resolve lands in the second bucket, which is
     * what 10.3's counters said and what the `UNRESOLVED` kind makes checkable. */
    if (sizing.width_source == ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED) {
        walk->block->widths_declared++;
    } else {
        walk->block->widths_filled++;
    }
    if (sizing.height_source == ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED) {
        walk->block->heights_explicit++;
    } else {
        walk->block->heights_from_content++;
    }

    /*
     * The margin counters, summed over sides rather than boxes for the reason the
     * record's own counters are: a document's margins arrive four at a time and a
     * count of *sides* is what can be checked against a stylesheet by hand. They are
     * counted here rather than from the declarations because the *resolution* is what
     * they describe -- an `auto` that distributed is not a declaration this build
     * found, it is one it used.
     */
    walk->block->margins_applied += edges.resolved;
    walk->block->margins_auto_used += edges.autos_used;
    walk->block->margins_unresolved += edges.unresolved;
    /* And the padding's two, summed over sides for the same reason: a document's padding
     * arrives four at a time and `padding: 5px` is four declarations one layer down. */
    walk->block->paddings_applied += padding.resolved;
    walk->block->paddings_unresolved += padding.unresolved;
    /* And the border's three, which are three rather than two because a border's *used*
     * width is not its declared one: a document can declare four `border-width: 5px` and
     * draw nothing, and only a counter that asks the second question can say so. */
    walk->block->borders_applied += borders.widths_resolved;
    walk->block->borders_unresolved += borders.widths_unresolved;
    walk->block->borders_drawn += borders.borders_drawn;
    /*
     * And the box's own geometry is written at its **margin origin**, which is the
     * whole of what a margin does to a box: `origin` is where its containing block's
     * content box starts, and the box starts `left` and `top` further in.
     *
     * The `size` written here is the **border** box -- the content box the sizing layer
     * resolved plus the padding (10.10) and the border (10.11) -- which is what
     * `RoboLayoutResult.h` has said this field means since 10.1 and what makes it a number
     * a renderer can draw without re-deriving anything. A margin still never changes it,
     * which is why the two numbers the box's own box is placed by (`margin_origin`, and the
     * margins' contribution to the parent's stacking) are not added into it anywhere.
     *
     * The **content origin** is this origin plus the border and the padding, and it is
     * deliberately not written anywhere: a child's position is already relative to its
     * containing block's *content* origin (`RoboLayoutResult.h`), so the children below
     * need no shift and a reader obtains the content origin from @ref
     * robo_layout_object_borders and @ref robo_layout_object_padding rather than from a
     * second copy of the same fact. Two terms since 10.11, and the two records compose in
     * one order: the border first, then the padding.
     */
    margin_origin.x = origin.x + edges.left;
    margin_origin.y = (robo_gfx_pixel_y_t)(origin.y + edges.top);

    /*
     * The position next (10.12), and it is the only phase in this module that changes
     * *where* a box is without changing anything about the box. Two steps rather than one
     * because the two coordinate spaces are the phase: `margin_origin` is where flow put
     * the box -- the number every sibling's placement was computed from -- and
     * `final_origin` is where it is drawn, which is `margin_origin` displaced by the
     * resolved offset.
     *
     * A `static` box resolves to a zero offset and the two are the same number, which is
     * why every 10.3–10.11 expectation about a position still holds: the three inset
     * phases added their numbers *inside* `size`, and this one adds a number *outside* it
     * and leaves the size alone. The `*out_height` below is computed from the flow
     * geometry for the same reason, so the following sibling starts where it always would
     * have whatever this box did with its offset.
     */
    declared_position = robo_layout_position_of(object->style);
    status = robo_layout_position_resolve(&declared_position, &position_offset, walk->diag);
    if (status != ROBO_OK) {
        return status;
    }
    status = robo_layout_position_apply(margin_origin, &position_offset, &final_origin,
                                        walk->diag);
    if (status != ROBO_OK) {
        return status;
    }
    status = robo_layout_result_set_geometry(&object->result, final_origin, size,
                                             walk->diag);
    if (status != ROBO_OK) {
        return status;
    }
    /* And the position's counters (10.12), which are the first counters in this record
     * that are about *where* rather than about *how big* -- and the first to count a
     * fact about the box rather than about a declaration (`boxes_shifted`).
     * `offsets_declined` is the one that would otherwise be invisible: a `static` box
     * with four insets and a `relative` box with four autos both resolve to a zero
     * displacement, and only this counter tells the first document from the second.
     * Counted *after* the resolve above so the record always describes this box rather
     * than whatever a previous box left on the stack -- the ordering bug a determinism
     * case caught as pointer-sized garbage in the counters. */
    walk->block->offsets_applied += position_offset.insets_resolved;
    walk->block->offsets_declined += position_offset.insets_declined;
    walk->block->offsets_unresolved += position_offset.insets_unresolved;
    if (position_offset.dx != 0 || position_offset.dy != 0) {
        walk->block->boxes_shifted++;
    }

    /*
     * The used margins are kept on the box (RoboLayoutObject.h), because the record a
     * later phase wants is a *margin box* and the result cannot carry one: the result
     * holds the border box by definition, and adding the four numbers back is exactly
     * the arithmetic 10.9 exists to stop a caller from re-deriving.
     */
    object->margins = edges;
    /*
     * And the used padding, kept on the box for the reason the margins are and one more:
     * it is the **only** way to recover the content box. A result carries the border box,
     * so `padding-left`/`padding-top` are what turn one into the other, and re-reading them
     * from the style would mean re-deciding which declarations this build could use.
     */
    object->padding = padding;
    /*
     * And the used border, kept on the box for the padding's reason and paint's: it is the
     * **outermost** of the two insets that belong to the box, so it is the second term of a
     * content origin beside `padding`, and it carries the style and colour of each side as
     * well as the width -- which is every fact a pass that draws an edge needs, decided once
     * here rather than re-decided from a style later.
     */
    object->borders = borders;
    /*
     * And the displacement it was placed with (10.12), which is what makes the box's
     * **flow** position recoverable: the result above carries the final position, so a
     * caller that wants where flow put the box subtracts this record. Kept on the box for
     * the reason the three insets are and one more -- a displacement is not derivable from
     * a size or an inset, so without this field the flow position would exist nowhere.
     */
    object->position_offset = position_offset;

    walk->block->blocks_placed++;
    if (object->depth > walk->block->depth_max) {
        walk->block->depth_max = object->depth;
    }

    /*
     * And the extent the parent stacks by is the **margin box**, which is the second
     * thing a margin does: `cursor` in `layout_block_children` is a bottom edge, and the
     * next box starts below this one's bottom *margin*. Reported as one number rather
     * than as a size because that is all a containing block needs from a child in this
     * direction -- the parent never looks at where its child's content box ended, only
     * at where the next one may start.
     *
     * And 10.12 does not touch it, which is the phase in one line: a displacement moves
     * the box and never its flow contribution, so `size.height` and the two margins here
     * are the *flow* geometry and a box shifted 100px down still leaves the hole flow gave
     * it. A build that added the offset here would look identical in every single-box test
     * and move every following sibling, which is why the suite asserts the pair.
     *
     * `size.height` is the **border** box since 10.10 and stays it in 10.11, so this one
     * expression spans all **four** of a box's boxes: the padding and the border are already
     * inside `size` (that is what the two derived doors added), the margins are the two terms
     * around it, and the content box is inside both and not needed. Which is worth stating,
     * because it is the cleanest consequence either phase has -- a border changes this number
     * without changing its formula, and a build that had added the border here *again* would
     * double it. The formula is the same one 10.9 wrote when `size` was the content box, which
     * is the point: every inset phase since has added its number inside `size` rather than
     * beside it.
     *
     * No collapsing (the header says so): two adjacent margins add rather than
     * overlap, so a box with `margin-bottom: 20px` above one with `margin-top: 30px`
     * leaves 50px between them here where CSS would leave 30. That is the one place
     * this phase knowingly differs from the specification, it is stated in
     * `layout/docs/Margins.md` §6, and `layout.margins.block_no_collapse` asserts the
     * sum so that the goal that implements collapsing has a case to change.
     */
    *out_height = (robo_gfx_pixel_y_t)(edges.top + size.height + edges.bottom);
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
           a->widths_declared == b->widths_declared &&
           a->widths_filled == b->widths_filled &&
           a->heights_explicit == b->heights_explicit &&
           a->heights_from_content == b->heights_from_content &&
           a->margins_applied == b->margins_applied &&
           a->margins_auto_used == b->margins_auto_used &&
           a->margins_unresolved == b->margins_unresolved &&
           a->paddings_applied == b->paddings_applied &&
           a->paddings_unresolved == b->paddings_unresolved &&
           a->borders_applied == b->borders_applied &&
           a->borders_unresolved == b->borders_unresolved &&
           a->borders_drawn == b->borders_drawn &&
           a->offsets_applied == b->offsets_applied &&
           a->offsets_declined == b->offsets_declined &&
           a->offsets_unresolved == b->offsets_unresolved &&
           a->boxes_shifted == b->boxes_shifted &&
           a->empty_containers == b->empty_containers &&
           a->depth_max == b->depth_max;
}

robo_result_t robo_layout_block_layout(robo_layout_tree_t *tree,
                                       robo_layout_size_t viewport,
                                       const robo_layout_text_metrics_t *text_metrics,
                                       robo_layout_block_stats_t *out_stats,
                                       robo_layout_inline_stats_t *out_inline_stats,
                                       robo_layout_text_stats_t *out_text_stats,
                                       robo_layout_diag_t *diag)
{
    robo_layout_walk_t walk;
    robo_layout_inline_stats_t scratch;
    robo_layout_text_stats_t text_scratch;
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
    robo_layout_text_stats_init(out_text_stats);
    robo_layout_text_stats_init(&text_scratch);

    /*
     * And the line boxes this pass is about to produce replace whatever the last one
     * left (Phase 10.6). The reset is here -- at the top of the pass, with the records
     * -- rather than inside the inline pass, because it is the same rule those records
     * follow: everything from this point on describes *this* call. A repeated pass over
     * an unchanged tree therefore produces the same lines, and a pass over a tree whose
     * viewport changed produces a different set rather than two sets appended.
     *
     * It is also why the *classification* does not have to be re-run for lines to be
     * correct: a caller may classify once and lay out many times, and each pass still
     * starts from no lines.
     */
    robo_layout_tree_lines_reset(tree);

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
    /*
     * The text record has a scratch for the same reason the inline one does: the pass
     * fills three records and a caller may want any subset of them, so "not asked
     * for" must still be a place to write -- never a NULL the inline pass would have
     * to test at every text run. The metrics, by contrast, are passed through as they
     * came: NULL is a *meaning* here ("nothing can measure"), not an omission.
     */
    walk.text = out_text_stats ? out_text_stats : &text_scratch;
    walk.text_metrics = text_metrics;
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
     * object no geometry?": 10.3's `inline_level_skipped` is an inline-level box -- or
     * a text run, which is inline-level by construction -- in a block-direction
     * context, and 10.4's `block_level_declined` is its mirror. An object the inline
     * pass *placed* is subtracted too, so the identity below closes over every
     * direction and every kind of object at once:
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
