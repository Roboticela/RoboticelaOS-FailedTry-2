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
