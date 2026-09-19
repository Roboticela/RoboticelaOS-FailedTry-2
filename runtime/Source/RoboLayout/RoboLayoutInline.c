/*
 * RoboLayout inline layout (Phase 10.4) — see RoboLayoutInline.h.
 *
 * One walk over an inline formatting context's participants, and three ideas in
 * it. The header argues all three; this file is where they have to survive
 * contact with a tree:
 *
 *   a participant is placed at the *cursor*, and the cursor advances by its
 *   width. A participant that does not fit after the cursor starts a new line,
 *   because the cursor is the only thing that carries from one to the next;
 *
 *   a **transparent** inline box (display `inline`, establishing nothing) is not
 *   a unit: its children are participants of the same context, so they are placed
 *   by the same loop and the box's own rectangle is the *union* of what they
 *   reached. That is what the scope chain below is for — a handful of integers on
 *   the C stack, nested and unwound by the recursion itself;
 *
 *   an **atomic** inline (display `inline-block`) *is* a unit, so its width has to
 *   be known before it is placed — which is what makes it the one kind the wrap
 *   rule can act on — and its contents are laid out by the block pass, in the
 *   box's own space, through the door 10.3 exposes for exactly this call.
 *
 * Nothing here allocates, caches or reads a previous pass: the flow state lives on
 * the stack of `robo_layout_inline_walk_contents` and the scope chain is the call
 * stack's own shape. That is why a caller can run the same tree twice and get the
 * same numbers back, and why there is no per-context scratch to release.
 *
 * The one place this file could quietly go wrong is the *ordering* of the writes,
 * and it is the mirror of the one 10.3 names: a transparent inline box's geometry
 * cannot be written until its last participant has been placed, because until then
 * its own rectangle is still growing. So a parent is written after its children —
 * the same order a block container's height forces — and the third step below
 * (`rebase`) exists because of what that costs: a child was written in the *line's*
 * space, and its containing block is the box that turned out to surround it.
 */

#include "RoboLayout/RoboLayoutInline.h"

#include "RoboCSS/RoboCSSDisplay.h"

#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutResult.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The preliminary line                                                */
/* ------------------------------------------------------------------ */

/**
 * The line the cursor is on.
 *
 * Deliberately four numbers and not a box. A line *box* has identity, alignment,
 * a baseline and a height that comes from fonts, and it is Phase 10.6's — so what
 * runs here is a cursor, the top of the line it is on, the tallest participant
 * placed on it so far, and how many lines the context has started. The header
 * calls it preliminary in the same words, and `robo_layout_inline_stats_t` reports
 * the *count* and nothing about any individual line.
 *
 * `top` and `height` are in the owner's content-box space, so `top + height` at the
 * end of a context is the extent the block pass needs for the owner's height.
 */
typedef struct layout_inline_line {
    /** x the next participant starts at. */
    int32_t cursor;
    /** y of the current line, in the owner's content-box space. */
    robo_gfx_pixel_y_t top;
    /** Tallest participant on the current line so far. */
    robo_gfx_pixel_y_t height;
    /** Lines started: 0 until something is placed, then 1 and up. */
    size_t count;
} layout_inline_line_t;

/**
 * One transparent inline box, open while its contents are being placed.
 *
 * The union that becomes the box's rectangle. `left`/`top` are taken from the
 * *first* placement rather than from where the box began, and that is a decision
 * rather than an oversight: a box whose contents all landed on a later line — a
 * box that wrapped immediately — begins above and to the right of everything it
 * contains, and a rectangle anchored on its start edge would not hold its own
 * contents. Anchoring on the first fragment that exists is what makes the
 * rectangle a bounding box.
 *
 * The chain is the recursion's own nesting (RoboLayoutInline.h argues why the
 * `span`-inside-`span` case must not be flattened), and it lives on the C stack:
 * no allocation, no fixed depth, and nothing to release when a call returns early.
 */
typedef struct layout_inline_scope {
    struct layout_inline_scope *outer;
    int32_t left;
    int32_t right;
    robo_gfx_pixel_y_t top;
    robo_gfx_pixel_y_t bottom;
    /** Non-zero once anything inside this box has been placed. */
    int placed;
} layout_inline_scope_t;

/** One context's flow state, for the length of one call. */
typedef struct layout_inline_flow {
    /** The owner's content width: the line's width, and the fallback a
     *  width-less atomic inline fills (see the header). */
    int32_t available_width;
    layout_inline_line_t line;
    /** Innermost open inline box, or NULL at the context's own level. */
    layout_inline_scope_t *scope;
} layout_inline_flow_t;

/* ------------------------------------------------------------------ */
/* Placing                                                             */
/* ------------------------------------------------------------------ */

/** Count a participant: the one place the three per-box facts are recorded. */
static void layout_inline_count(robo_layout_walk_t *walk,
                                layout_inline_flow_t *flow,
                                const robo_layout_object_t *object)
{
    walk->inline_flow->participants_placed++;
    if (object->depth > walk->inline_flow->depth_max) {
        walk->inline_flow->depth_max = object->depth;
    }
    if (flow->line.count == 0u) {
        flow->line.count = 1u;
    }
}

/**
 * Whether @p width would not fit after the cursor.
 *
 * The `cursor > 0` guard is the whole of CSS's rule about a box wider than its
 * containing block: an object that does not fit on an *empty* line is placed
 * anyway and overflows, rather than starting lines forever. Overflow is 10.13's
 * fact and this pass sets no flag for it — a box that runs past its line is a
 * fact the result's two flag bits deliberately do not carry yet.
 */
static int layout_inline_would_overflow(const layout_inline_flow_t *flow, int32_t width)
{
    return flow->line.cursor > 0 && (flow->line.cursor + width) > flow->available_width;
}

/** Move the cursor to a new line, below the tallest thing the last one held. */
static void layout_inline_wrap(layout_inline_flow_t *flow,
                               robo_layout_inline_stats_t *stats)
{
    flow->line.top += flow->line.height;
    flow->line.height = 0;
    flow->line.cursor = 0;
    flow->line.count++;
    stats->wraps++;
}

/**
 * Write a participant's geometry and advance the line.
 *
 * The one door a participant's result is written through, so the three things that
 * must happen together — the geometry, the enclosing boxes' unions, and the
 * cursor — cannot drift apart in one path and not another.
 */
static robo_result_t layout_inline_place(robo_layout_walk_t *walk,
                                         layout_inline_flow_t *flow,
                                         robo_layout_object_t *object,
                                         int32_t x,
                                         robo_gfx_pixel_y_t y,
                                         int32_t width,
                                         int32_t height)
{
    robo_layout_point_t position;
    robo_layout_size_t size;
    layout_inline_scope_t *scope;
    robo_result_t status;

    position.x = x;
    position.y = y;
    size.width = width;
    size.height = height;
    status = robo_layout_result_set_geometry(&object->result, position, size, walk->diag);
    if (status != ROBO_OK) {
        return status;
    }

    /* Every enclosing transparent box grows to hold this; the outermost of them
     * is the context's own level, whose scope pointer is NULL. */
    for (scope = flow->scope; scope; scope = scope->outer) {
        if (!scope->placed) {
            scope->placed = 1;
            scope->left = x;
            scope->top = y;
        }
        if (x < scope->left) {
            scope->left = x;
        }
        if (x + width > scope->right) {
            scope->right = x + width;
        }
        if (y + height > scope->bottom) {
            scope->bottom = y + height;
        }
    }

    flow->line.cursor = x + width;
    if (height > flow->line.height) {
        flow->line.height = height;
    }
    layout_inline_count(walk, flow, object);
    return ROBO_OK;
}

/**
 * Place a participant whose advance width is known before it is placed: an atomic
 * inline, or a non-atomic one with no contents at all.
 *
 * Both cases end at the same cursor arithmetic, and they differ in where the
 * numbers come from — which is the header's central distinction, in code:
 *
 *   an **atomic** inline is sized by its declarations, through the same rule 10.3
 *   reads a declared height with (`robo_layout_style_px_length`): a computed
 *   length in `px` is usable and nothing else is. A width it cannot resolve takes
 *   the containing block's content width, which is CSS's `width: auto` and what
 *   10.3 does for a block box; a height it cannot resolve comes from what it
 *   contained, laid out by the block pass through 10.3's own door.
 *
 *   a **non-atomic** inline with no contents is placed with no extent at all, and
 *   the header argues why that zero is the honest answer rather than a placeholder:
 *   nothing in this build measures text, and 10.5 is the goal that will.
 */
static robo_result_t layout_inline_place_leaf(robo_layout_walk_t *walk,
                                              layout_inline_flow_t *flow,
                                              robo_layout_node_id_t id)
{
    robo_layout_object_t *object = robo_layout_object_slot(walk->tree, id);
    int atomic;
    int32_t width = 0;
    int32_t height = 0;
    double declared = 0.0;

    if (!object) {
        return ROBO_LAYOUT_DIAG(walk->diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_ERROR,
                                "a box vanished under the inline pass");
    }

    atomic = object->context_owned != ROBO_LAYOUT_FC_ID_NONE;

    if (atomic) {
        if (robo_layout_style_px_length(object->style, "width", &declared)) {
            width = (int32_t)robo_layout_px_round(declared);
            walk->inline_flow->widths_declared++;
        } else {
            width = flow->available_width;
            walk->inline_flow->widths_filled++;
        }
        if (width < 0) {
            width = 0;
        }

        if (robo_layout_style_px_length(object->style, "height", &declared)) {
            height = (int32_t)robo_layout_px_round(declared);
            walk->inline_flow->heights_declared++;
        } else {
            robo_gfx_pixel_y_t extent = 0;

            if (object->child_count > 0u) {
                robo_result_t status = robo_layout_block_walk_children(walk, id, width, &extent);

                if (status != ROBO_OK) {
                    return status;
                }
            }
            height = (int32_t)extent;
            walk->inline_flow->heights_from_content++;
        }
        if (height < 0) {
            height = 0;
        }
        walk->inline_flow->atomic_placed++;
    } else {
        walk->inline_flow->non_atomic_placed++;
    }

    if (layout_inline_would_overflow(flow, width)) {
        layout_inline_wrap(flow, walk->inline_flow);
    }
    return layout_inline_place(walk, flow, object, flow->line.cursor, flow->line.top, width,
                               height);
}

static robo_result_t layout_inline_object(robo_layout_walk_t *walk,
                                          layout_inline_flow_t *flow,
                                          robo_layout_node_id_t id);

/**
 * Place a **transparent** inline box and everything inside it, then write the box
 * itself as the union of what its contents reached.
 *
 * The recursion is into the *same* flow, which is the whole difference between this
 * and the atomic path: a `span`'s children are participants of the same formatting
 * context, at the same cursor, on the same lines.
 *
 * A box that placed nothing — an empty `span`, or one whose only contents were
 * blocks this phase declines — is written as a zero-size box at the cursor, which
 * is what "it is here and it is empty" looks like in geometry.
 */
static robo_result_t layout_inline_place_transparent(robo_layout_walk_t *walk,
                                                     layout_inline_flow_t *flow,
                                                     robo_layout_object_t *object)
{
    layout_inline_scope_t scope;
    robo_layout_point_t position;
    robo_layout_size_t size;
    robo_layout_node_id_t child = object->first_child;
    robo_result_t status;

    scope.outer = flow->scope;
    scope.left = flow->line.cursor;
    scope.right = flow->line.cursor;
    scope.top = flow->line.top;
    scope.bottom = flow->line.top;
    scope.placed = 0;

    flow->scope = &scope;
    while (child != ROBO_LAYOUT_NODE_ID_NONE) {
        const robo_layout_object_t *child_object = robo_layout_object_slot_const(walk->tree,
                                                                                child);

        if (!child_object) {
            return ROBO_LAYOUT_DIAG(walk->diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_ERROR,
                                    "a box's child link names no box in this tree");
        }
        status = layout_inline_object(walk, flow, child);
        if (status != ROBO_OK) {
            return status;
        }
        child = child_object->next_sibling;
    }
    flow->scope = scope.outer;

    if (scope.placed) {
        position.x = scope.left;
        position.y = scope.top;
        size.width = scope.right - scope.left;
        size.height = scope.bottom - scope.top;
    } else {
        position.x = flow->line.cursor;
        position.y = flow->line.top;
        size.width = 0;
        size.height = 0;
    }
    if (size.width < 0) {
        size.width = 0;
    }
    if (size.height < 0) {
        size.height = 0;
    }

    /*
     * Written directly rather than through `layout_inline_place`, and both halves
     * of that are deliberate: the cursor has already been advanced by the children
     * (advancing it again would count this box's contents twice), and the enclosing
     * scopes have already been updated by the very placements this union is made
     * of. What is left is the box's own result and its own two counters.
     */
    status = robo_layout_result_set_geometry(&object->result, position, size, walk->diag);
    if (status != ROBO_OK) {
        return status;
    }
    walk->inline_flow->non_atomic_placed++;
    layout_inline_count(walk, flow, object);
    return ROBO_OK;
}

/**
 * Place one participant of the current inline formatting context.
 *
 * Three answers, and the middle one is the reason this function is not a loop:
 *
 *   a **block-level** box is declined and counted. CSS would have wrapped the
 *   inline run in an anonymous block box before this point and this build makes
 *   none (RoboLayoutInline.h), so placing it here would stack a block in the middle
 *   of a line;
 *
 *   a **transparent** inline box with contents opens a scope and recurses;
 *
 *   anything else — an atomic inline, or an empty non-atomic one — is a leaf.
 */
static robo_result_t layout_inline_object(robo_layout_walk_t *walk,
                                          layout_inline_flow_t *flow,
                                          robo_layout_node_id_t id)
{
    robo_layout_object_t *object = robo_layout_object_slot(walk->tree, id);

    if (!object) {
        return ROBO_LAYOUT_DIAG(walk->diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_ERROR,
                                "a box vanished under the inline pass");
    }

    if (!robo_css_display_is_inline_level(object->display)) {
        walk->inline_flow->block_level_declined++;
        return ROBO_OK;
    }

    if (object->child_count > 0u && object->context_owned == ROBO_LAYOUT_FC_ID_NONE) {
        return layout_inline_place_transparent(walk, flow, object);
    }
    return layout_inline_place_leaf(walk, flow, id);
}

/* ------------------------------------------------------------------ */
/* Rebasing nested participants                                        */
/* ------------------------------------------------------------------ */

/**
 * Move a participant's position from the line's space into its parent's.
 *
 * The third step, and the price of writing unions after contents. Every
 * participant is placed in the *owner's* content-box space while the context runs,
 * because that is the space a line lives in; but a box's containing block is its
 * parent (RoboLayoutBlock.h), and for a participant inside a transparent inline
 * box that parent is the inline box — not the context owner. So when the context is
 * complete, each participant that has a parent *within* the context has the
 * parent's own origin subtracted from its position.
 *
 * It is a second walk rather than a running adjustment because an inline box's
 * origin is a union: a later sibling that wraps can move the box's left edge, and a
 * child written before that happened would be left holding a stale offset. Two
 * passes over one context is the honest cost of a bounding box.
 *
 * The recursion stops at an atomic inline: its contents were laid out by the block
 * pass in the box's own space, so they are already stated relative to it. And it
 * stops at anything without geometry — a box this phase declined is UNRESOLVED, and
 * there is nothing to move.
 */
static void layout_inline_rebase(robo_layout_walk_t *walk,
                                 robo_layout_node_id_t id,
                                 int32_t parent_x,
                                 int32_t parent_y)
{
    robo_layout_object_t *object = robo_layout_object_slot(walk->tree, id);
    robo_layout_node_id_t child;
    int32_t absolute_x;
    int32_t absolute_y;

    if (!object || !robo_layout_result_is_computed(&object->result)) {
        return;
    }

    absolute_x = object->result.position.x;
    absolute_y = object->result.position.y;
    object->result.position.x = absolute_x - parent_x;
    object->result.position.y = absolute_y - parent_y;

    if (!robo_css_display_is_inline_level(object->display)) {
        return;
    }
    if (object->context_owned != ROBO_LAYOUT_FC_ID_NONE) {
        return;
    }

    child = object->first_child;
    while (child != ROBO_LAYOUT_NODE_ID_NONE) {
        const robo_layout_object_t *child_object = robo_layout_object_slot_const(walk->tree,
                                                                                child);

        if (!child_object) {
            break;
        }
        layout_inline_rebase(walk, child, absolute_x, absolute_y);
        child = child_object->next_sibling;
    }
}

/* ------------------------------------------------------------------ */
/* The context                                                         */
/* ------------------------------------------------------------------ */

void robo_layout_inline_stats_init(robo_layout_inline_stats_t *stats)
{
    if (!stats) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

int robo_layout_inline_stats_equal(const robo_layout_inline_stats_t *a,
                                   const robo_layout_inline_stats_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->contexts == b->contexts && a->empty_contexts == b->empty_contexts &&
           a->participants_placed == b->participants_placed &&
           a->atomic_placed == b->atomic_placed &&
           a->non_atomic_placed == b->non_atomic_placed &&
           a->block_level_declined == b->block_level_declined && a->lines == b->lines &&
           a->wraps == b->wraps && a->widths_declared == b->widths_declared &&
           a->widths_filled == b->widths_filled &&
           a->heights_declared == b->heights_declared &&
           a->heights_from_content == b->heights_from_content &&
           a->depth_max == b->depth_max;
}

robo_result_t robo_layout_inline_walk_contents(robo_layout_walk_t *walk,
                                               robo_layout_node_id_t owner,
                                               int32_t content_width,
                                               robo_gfx_pixel_y_t *out_extent)
{
    const robo_layout_object_t *object;
    layout_inline_flow_t flow;
    robo_layout_node_id_t child;
    size_t placed_before;
    robo_result_t status;

    if (!walk || !walk->tree || !walk->inline_flow || !out_extent) {
        return ROBO_LAYOUT_DIAG(walk ? walk->diag : NULL, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT,
                                ROBO_INVALID_ARGUMENT, "the inline pass needs a walk and an out");
    }

    object = robo_layout_object_slot_const(walk->tree, owner);
    if (!object) {
        return ROBO_LAYOUT_DIAG(walk->diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_ERROR,
                                "the box that owns an inline context is not in the tree");
    }
    if (content_width < 0) {
        content_width = 0;
    }

    memset(&flow, 0, sizeof(flow));
    flow.available_width = content_width;

    placed_before = walk->inline_flow->participants_placed;
    walk->inline_flow->contexts++;

    /*
     * The participants, in sibling order — which the builder made document order,
     * and which is what makes the line's contents ordered the way they are written.
     * The walk descends through transparent inline boxes to the leaves that actually
     * occupy the line, so a `span`'s children are reached here and not by a pass of
     * their own.
     */
    child = object->first_child;
    while (child != ROBO_LAYOUT_NODE_ID_NONE) {
        const robo_layout_object_t *child_object = robo_layout_object_slot_const(walk->tree,
                                                                                child);

        if (!child_object) {
            return ROBO_LAYOUT_DIAG(walk->diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_ERROR,
                                    "a box's child link names no box in this tree");
        }
        status = layout_inline_object(walk, &flow, child);
        if (status != ROBO_OK) {
            return status;
        }
        child = child_object->next_sibling;
    }

    walk->inline_flow->lines += flow.line.count;
    if (walk->inline_flow->participants_placed == placed_before) {
        walk->inline_flow->empty_contexts++;
    }

    /*
     * Then the rebase, over the same participants. The owner's own children are
     * placed relative to the owner, so they are rebased by nothing; a participant
     * further in is rebased by the transparent box it turned out to be inside.
     */
    child = object->first_child;
    while (child != ROBO_LAYOUT_NODE_ID_NONE) {
        const robo_layout_object_t *child_object = robo_layout_object_slot_const(walk->tree,
                                                                                child);

        if (!child_object) {
            break;
        }
        layout_inline_rebase(walk, child, 0, 0);
        child = child_object->next_sibling;
    }

    /*
     * The extent the block pass needs: the top of the last line plus the tallest
     * thing on it. Zero when nothing was placed, which is the answer for a container
     * holding only text and is what keeps `<p>one</p>` the height 10.3 gave it.
     */
    *out_extent = flow.line.top + flow.line.height;
    return ROBO_OK;
}
