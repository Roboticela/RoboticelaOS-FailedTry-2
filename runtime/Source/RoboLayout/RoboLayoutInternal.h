/*
 * RoboLayout internals (Phase 10.1).
 *
 * The one thing two translation units must agree on that the public headers
 * deliberately do not state: what a tree *is*. RoboLayoutTree.c owns the arena and
 * RoboLayoutObject.c reads it, so the struct lives here and nowhere else, and no
 * public header learns its layout.
 *
 * The arena is a single reallocating array of objects rather than a node-per-
 * allocation tree, and the choice is what makes the rest of the module simple:
 *
 *   - ids are indices plus one, so a lookup is arithmetic and a walk is a loop;
 *   - disposing a tree is one `robo_free`, so there is no recursive free to get
 *     wrong on a deep document (RoboHTML's 4096-node ceiling is the bound);
 *   - the links are ids rather than pointers, so a growth does not have to
 *     rewrite them -- which is the property that keeps realloc safe at all.
 *
 * The price is stated in the public header: a borrowed object pointer dies at the
 * next add.
 *
 * The second arena (Phase 10.2) is here for the same reason the first is: the
 * tree owns its formatting contexts, and the *only* producer is the pass -- which
 * means the two translation units that touch the arena are this file's and
 * RoboLayoutContext.c's. The public header exposes the read side; nothing public
 * can create or mutate a context, because a context is a conclusion the pass
 * draws and not a thing a caller may invent. That is why the doors below are
 * internal and why there is no `context_info` descriptor mirroring
 * robo_layout_object_info_t: a descriptor exists so several callers can build one
 * kind of thing, and here there is exactly one caller.
 */

#ifndef ROBO_LAYOUT_INTERNAL_H
#define ROBO_LAYOUT_INTERNAL_H

#include "RoboLayout/RoboLayoutBlock.h"
#include "RoboLayout/RoboLayoutInline.h"
#include "RoboLayout/RoboLayoutTree.h"

/** Objects the arena holds before its first growth. */
#define ROBO_LAYOUT_INITIAL_CAPACITY 8u

/**
 * Contexts the second arena holds before its first growth.
 *
 * Smaller than the object arena on purpose: a context exists only for a box that
 * establishes one, and most documents are mostly transparent boxes. A size that
 * guessed high would be a size the small fixtures pay for and the deep ones do
 * not.
 */
#define ROBO_LAYOUT_INITIAL_CONTEXT_CAPACITY 4u

/*
 * The walk (Phase 10.3, shared with 10.4)
 *
 * One pass lays both directions out, and it is split across two translation units
 * along the line the two algorithms divide on: the block direction stacks in
 * RoboLayoutBlock.c, the inline direction places along a line in
 * RoboLayoutInline.c, and each calls the other. The coupling is not a convenience
 * -- an inline formatting context's height *is* the block extent of the box that
 * owns it, so the two cannot be separate passes without re-stacking every
 * ancestor afterwards -- and what crosses the boundary is this record, holding the
 * tree, the two stats records and the diagnostic sink, plus the two doors below,
 * one defined per file.
 */

/**
 * One pass's shared state.
 *
 * Borrowed throughout and owned by `robo_layout_block_layout`, which fills it
 * once and hands the same pointer to every level of both recursions. A record on
 * the caller's stack rather than a heap object, because nothing here outlives the
 * pass that made it -- and because a pass that allocated its own scratch would be
 * a pass that could fail for a reason its caller cannot see.
 */
typedef struct robo_layout_walk {
    robo_layout_tree_t *tree;
    robo_layout_block_stats_t *block;
    robo_layout_inline_stats_t *inline_flow;
    robo_layout_diag_t *diag;
} robo_layout_walk_t;

/**
 * Lay out @p owner's contents in the **block** direction and report the extent
 * they reached. Defined by RoboLayoutBlock.c; called by the inline pass for an
 * atomic inline, whose contents are a formatting context of their own.
 *
 * It also records whether the owner stacked anything, because that is the same
 * fact the block pass records for every box whose contents it stacked.
 */
robo_result_t robo_layout_block_walk_children(robo_layout_walk_t *walk,
                                              robo_layout_node_id_t owner,
                                              int32_t content_width,
                                              robo_gfx_pixel_y_t *out_extent);

/**
 * Lay out @p owner's contents as an **inline** formatting context and report the
 * extent the lines reached. Defined by RoboLayoutInline.c; called by the block
 * pass for every box that establishes one (RoboLayoutInline.h).
 */
robo_result_t robo_layout_inline_walk_contents(robo_layout_walk_t *walk,
                                               robo_layout_node_id_t owner,
                                               int32_t content_width,
                                               robo_gfx_pixel_y_t *out_extent);

/*
 * The one style read both directions make, defined by RoboLayoutBlock.c because
 * that is where it was first needed and where its rules are argued.
 *
 * Shared rather than copied, for the reason the module shares everything: "a
 * declared value this build can resolve is a computed LENGTH in px, finite,
 * non-negative and inside the geometry's range" is one rule, and two passes with
 * their own copies of it would be two answers to one question the day 10.8
 * changes it.
 */

/** The computed px length @p style declares for @p property, or 0 when there is
 *  none this build can resolve. */
int robo_layout_style_px_length(const robo_css_computed_style_t *style,
                                const char *property,
                                double *out_value);

/** Round a resolved length to the integer pixel space (half away from zero). */
robo_gfx_pixel_x_t robo_layout_px_round(double value);

struct robo_layout_tree {
    robo_layout_object_t *objects;
    /** Live objects; also the number of ids handed out. */
    size_t count;
    /** Slots allocated; always >= count. */
    size_t capacity;
    /** The next id to hand out; ids are 1-based and dense. */
    robo_layout_node_id_t next_id;

    /*
     * The formatting-context arena (Phase 10.2). Lazily allocated -- a tree that
     * is built and never classified owns no contexts and costs no allocation --
     * and *released* rather than reused when the classification is invalidated,
     * because a stale participant count is exactly the kind of half-truth this
     * module refuses to keep.
     */
    robo_layout_formatting_context_t *contexts;
    /** Live contexts; also the number of ids handed out. */
    size_t context_count;
    /** Slots allocated; always >= context_count. */
    size_t context_capacity;
    /** Non-zero once a pass has classified this tree. */
    int classified;
};

/**
 * The object as stored, or NULL when @p id names none.
 *
 * Deliberately not public: it is the accessor that skips the id-to-index
 * conversion's validation, and every public door in RoboLayoutObject.h does that
 * validation itself so that a caller cannot reach a box by an id it was never
 * given.
 */
robo_layout_object_t *robo_layout_object_slot(robo_layout_tree_t *tree,
                                              robo_layout_node_id_t id);

/** Same, for the read-only doors. */
const robo_layout_object_t *robo_layout_object_slot_const(const robo_layout_tree_t *tree,
                                                          robo_layout_node_id_t id);

/** The array itself, for the walk a pass does. NULL when empty. */
const robo_layout_object_t *robo_layout_object_array(const robo_layout_tree_t *tree);

/** Resolve @p id, filling @p out_index. 0 when the id names no box. */
int robo_layout_object_index(const robo_layout_tree_t *tree,
                             robo_layout_node_id_t id,
                             size_t *out_index);

/*
 * The context arena, internal because the pass is its only writer.
 */

/** Release every context and clear the classified flag. Independent of the box
 *  arena: the boxes are untouched, because invalidating a classification is not
 *  the same act as clearing a tree. */
void robo_layout_tree_contexts_reset(robo_layout_tree_t *tree);

/**
 * Append one context, growing the arena if it must.
 *
 * The parent is *not* validated here, because the caller is the pass and the pass
 * establishes contexts in creation order -- a parent is therefore always already
 * present, and a second walk to prove it would be work that cannot fail. Owners,
 * on the other hand, are validated: an owner id that names no box would leave a
 * context nothing can reach.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments, a NONE owner, an
 *         undefined kind or a NONE parent for a non-ROOT kind; ROBO_NOT_FOUND for
 *         an owner that names no box; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_layout_tree_context_add(robo_layout_tree_t *tree,
                                           robo_layout_fc_kind_t kind,
                                           robo_layout_node_id_t owner,
                                           robo_layout_fc_id_t parent,
                                           uint32_t depth,
                                           robo_layout_fc_id_t *out_id,
                                           robo_layout_diag_t *diag);

/** Count one more participant in @p id. A NONE or unknown id is ignored: the
 *  pass calls this once per box and a bug there must not corrupt a count. */
void robo_layout_tree_context_add_participant(robo_layout_tree_t *tree,
                                              robo_layout_fc_id_t id);

/** Mark the tree's classification complete. */
void robo_layout_tree_mark_classified(robo_layout_tree_t *tree);

/** Mutable context slot, or NULL when @p id names none. Internal for the same
 *  reason the object slot accessor is: it skips the validation the public doors
 *  perform, so it must not be reachable from outside the module. */
robo_layout_formatting_context_t *robo_layout_fc_slot(robo_layout_tree_t *tree,
                                                      robo_layout_fc_id_t id);

/** Same, for the read-only doors -- the pair mirrors the object arena's, so that
 *  a const tree is never read through a cast-away. */
const robo_layout_formatting_context_t *robo_layout_fc_slot_const(
    const robo_layout_tree_t *tree,
    robo_layout_fc_id_t id);

#endif /* ROBO_LAYOUT_INTERNAL_H */
