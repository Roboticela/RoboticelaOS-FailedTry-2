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
 */

#ifndef ROBO_LAYOUT_INTERNAL_H
#define ROBO_LAYOUT_INTERNAL_H

#include "RoboLayout/RoboLayoutTree.h"

/** Objects the arena holds before its first growth. */
#define ROBO_LAYOUT_INITIAL_CAPACITY 8u

struct robo_layout_tree {
    robo_layout_object_t *objects;
    /** Live objects; also the number of ids handed out. */
    size_t count;
    /** Slots allocated; always >= count. */
    size_t capacity;
    /** The next id to hand out; ids are 1-based and dense. */
    robo_layout_node_id_t next_id;
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

#endif /* ROBO_LAYOUT_INTERNAL_H */
