/*
 * RoboLayout tree (Phase 10.1) — see RoboLayoutTree.h.
 *
 * The arena, the links, and the lookup. Every validation rule the header states
 * lives here rather than in the builder, because the builder is one caller and a
 * rule enforced in the container is a rule no future caller can forget.
 */

#include "RoboLayout/RoboLayoutTree.h"

#include "RoboLayoutInternal.h"

#include "RoboCSS/RoboCSSDisplay.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

void robo_layout_object_info_init(robo_layout_object_info_t *info)
{
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->parent = ROBO_LAYOUT_NODE_ID_NONE;
    /*
     * The common case, stated as the default so a caller cannot get a third
     * "kind nobody set" state: a descriptor describes an element box unless it
     * says otherwise. The display below is deliberately the opposite default --
     * NONE rather than `inline` -- because a descriptor that forgot its display
     * must NOT accidentally claim a box, while a descriptor that forgot its kind
     * is simply the ordinary one.
     */
    info->kind = ROBO_LAYOUT_OBJECT_BOX;
    info->display = ROBO_CSS_DISPLAY_NONE;
}

int robo_layout_object_index(const robo_layout_tree_t *tree,
                             robo_layout_node_id_t id,
                             size_t *out_index)
{
    size_t index;

    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return 0;
    }
    /*
     * Ids are dense, so the id *is* the index plus one. The comparison against
     * `count` is what makes arithmetic safe: an id handed out before a clear, or
     * never handed out at all, is past the end and therefore a miss rather than an
     * alias -- which is the whole reason ids restart on clear instead of being
     * recycled.
     */
    index = (size_t)(id - 1u);
    if (index >= tree->count) {
        return 0;
    }
    if (tree->objects[index].id != id) {
        return 0;
    }
    if (out_index) {
        *out_index = index;
    }
    return 1;
}

const robo_layout_object_t *robo_layout_object_slot_const(const robo_layout_tree_t *tree,
                                                          robo_layout_node_id_t id)
{
    size_t index;

    if (!robo_layout_object_index(tree, id, &index)) {
        return NULL;
    }
    return &tree->objects[index];
}

robo_layout_object_t *robo_layout_object_slot(robo_layout_tree_t *tree,
                                              robo_layout_node_id_t id)
{
    size_t index;

    if (!robo_layout_object_index(tree, id, &index)) {
        return NULL;
    }
    return &tree->objects[index];
}

const robo_layout_object_t *robo_layout_object_array(const robo_layout_tree_t *tree)
{
    return tree ? tree->objects : NULL;
}

robo_result_t robo_layout_tree_create(robo_layout_tree_t **out_tree,
                                      robo_layout_diag_t *diag)
{
    robo_layout_tree_t *tree;

    if (!out_tree) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_INVALID_ARGUMENT,
                                "out_tree is NULL");
    }
    *out_tree = NULL;
    tree = (robo_layout_tree_t *)robo_calloc(1u, sizeof(*tree));
    if (!tree) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_OUT_OF_MEMORY,
                                "could not allocate the layout tree");
    }
    /* One allocation up front, so a tree that is never used costs nothing more and
     * a tree that is used does not start with a realloc. */
    tree->objects = (robo_layout_object_t *)robo_calloc(ROBO_LAYOUT_INITIAL_CAPACITY,
                                                       sizeof(robo_layout_object_t));
    if (!tree->objects) {
        robo_free(tree);
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_OUT_OF_MEMORY,
                                "could not allocate the layout object arena");
    }
    tree->capacity = ROBO_LAYOUT_INITIAL_CAPACITY;
    tree->count = 0u;
    tree->next_id = 1u;
    /* No context arena, and that is deliberate rather than an oversight: a tree
     * that is built and never classified owns no contexts, so the second
     * allocation waits for the pass that needs it (see the internal header). */
    tree->contexts = NULL;
    tree->context_count = 0u;
    tree->context_capacity = 0u;
    tree->classified = 0;
    /*
     * And no line boxes either, for the same reason: a tree that is built and never
     * laid out owns no lines, so the third and fourth allocations wait for the pass
     * that makes them (the internal header).
     */
    tree->lines = NULL;
    tree->line_count = 0u;
    tree->line_capacity = 0u;
    tree->members = NULL;
    tree->member_count = 0u;
    tree->member_capacity = 0u;
    *out_tree = tree;
    return ROBO_OK;
}

void robo_layout_tree_destroy(robo_layout_tree_t *tree)
{
    if (!tree) {
        return;
    }
    /* One free per arena: no recursion, no per-node teardown to get wrong, and
     * nothing borrowed is touched. Four frees since 10.6, and the line and member
     * arenas are released by their own reset so that this one place and the
     * invalidation path cannot disagree about what releasing a line means. */
    robo_layout_tree_lines_reset(tree);
    robo_free(tree->contexts);
    tree->contexts = NULL;
    tree->context_count = 0u;
    tree->context_capacity = 0u;
    tree->classified = 0;
    robo_free(tree->objects);
    tree->objects = NULL;
    tree->count = 0u;
    tree->capacity = 0u;
    tree->next_id = 1u;
    robo_free(tree);
}

void robo_layout_tree_contexts_reset(robo_layout_tree_t *tree)
{
    size_t index;

    if (!tree) {
        return;
    }
    /* Released rather than zeroed: a classification is rebuilt wholesale by the
     * next pass, so keeping a grown arena to overwrite would be holding memory
     * for a shape nobody has asked for yet. */
    robo_free(tree->contexts);
    tree->contexts = NULL;
    tree->context_count = 0u;
    tree->context_capacity = 0u;
    tree->classified = 0;

    /*
     * And the geometry goes with it (Phase 10.3).
     *
     * Block layout is a function of the classification: the placement gate *is*
     * `object->context`, and a box's height is the extent of the children that gate
     * let through. So the moment a classification stops describing the tree, every
     * coordinate derived from it is a statement about a tree that no longer exists
     * -- and it would still be marked COMPUTED, which is the one thing that state
     * is not allowed to mean.
     *
     * This is the shared path on purpose. All three callers -- an explicit
     * invalidation, a clear, and an add -- are saying the same thing ("the box set
     * changed or the classification was withdrawn"), and a rule written three times
     * is a rule two of them will eventually disagree about. It also means a caller
     * that adds a box after a pass gets results that are honestly UNRESOLVED rather
     * than numbers computed with the box set of a moment ago.
     *
     * The boxes themselves are untouched: a result is not part of what a box *is*, it
     * is what a pass concluded about it, and "unresolved" is a conclusion this
     * module can state truthfully.
     *
     * What is withdrawn is a *conclusion*, so a box that never had one keeps the
     * one it has -- which is NONE, and is not the same fact as UNRESOLVED. Only a
     * result that claimed something is demoted; a fresh box in a fresh tree stays
     * NONE, because nobody has computed its geometry *and* nobody has said that
     * nobody has. A blunt `set_unresolved` over every slot would erase that
     * distinction: it would make NONE a state no path in this module produces
     * (every add would manufacture UNRESOLVED for every box, including the ones
     * the add did not touch), and it would make a tree built by eight adds
     * indistinguishable from one a pass had swept without placing anything. The
     * conditional is the whole of the difference, and it is what keeps
     * `robo_layout_result_init`'s state reachable.
     */
    for (index = 0u; index < tree->count; index++) {
        robo_layout_result_t *result = &tree->objects[index].result;

        if (robo_layout_result_state(result) != ROBO_LAYOUT_RESULT_NONE) {
            robo_layout_result_set_unresolved(result);
        }
    }

    /*
     * And the line boxes go with the geometry (Phase 10.6), which is the same
     * argument one step further on: a line box is a conclusion *derived from* the
     * results -- it groups the participants a pass placed and states the extent they
     * reached -- so a line box that outlived the classification would be a rectangle
     * about a layout that no longer holds, plus member links to objects whose results
     * had just been withdrawn. Both are refused by construction rather than by a
     * check somebody has to remember.
     */
    robo_layout_tree_lines_reset(tree);
}

void robo_layout_tree_invalidate_contexts(robo_layout_tree_t *tree)
{
    robo_layout_tree_contexts_reset(tree);
}

void robo_layout_tree_mark_classified(robo_layout_tree_t *tree)
{
    if (tree) {
        tree->classified = 1;
    }
}

void robo_layout_tree_clear(robo_layout_tree_t *tree)
{
    if (!tree) {
        return;
    }
    if (tree->objects && tree->capacity > 0u) {
        memset(tree->objects, 0, tree->capacity * sizeof(robo_layout_object_t));
    }
    tree->count = 0u;
    tree->next_id = 1u;
    /* A classification describes a box set, and the box set is gone. The ids on
     * the boxes are zeroed with them, so no box can be left holding an id that
     * names a context in an arena that no longer exists. */
    robo_layout_tree_contexts_reset(tree);
}

size_t robo_layout_tree_object_count(const robo_layout_tree_t *tree)
{
    return tree ? tree->count : 0u;
}

size_t robo_layout_tree_capacity(const robo_layout_tree_t *tree)
{
    return tree ? tree->capacity : 0u;
}

int robo_layout_tree_is_empty(const robo_layout_tree_t *tree)
{
    return !tree || tree->count == 0u;
}

robo_layout_node_id_t robo_layout_tree_root(const robo_layout_tree_t *tree)
{
    /*
     * The root is always slot 0: a root can only be added to an empty tree, and
     * nothing removes an object, so the first box ever added stays the first.
     */
    if (!tree || tree->count == 0u) {
        return ROBO_LAYOUT_NODE_ID_NONE;
    }
    return tree->objects[0].id;
}

robo_result_t robo_layout_object_id_at(const robo_layout_tree_t *tree,
                                       size_t index,
                                       robo_layout_node_id_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = ROBO_LAYOUT_NODE_ID_NONE;
    if (!tree) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= tree->count) {
        return ROBO_NOT_FOUND;
    }
    *out = tree->objects[index].id;
    return ROBO_OK;
}

const robo_layout_object_t *robo_layout_object(const robo_layout_tree_t *tree,
                                               robo_layout_node_id_t id)
{
    return robo_layout_object_slot_const(tree, id);
}

/**
 * Make room for one more object, doubling the arena.
 * @return ROBO_OK, or ROBO_OUT_OF_MEMORY leaving the tree untouched.
 */
static robo_result_t layout_tree_reserve(robo_layout_tree_t *tree,
                                         robo_layout_diag_t *diag)
{
    size_t wanted;
    robo_layout_object_t *grown;

    if (tree->count < tree->capacity) {
        return ROBO_OK;
    }
    wanted = tree->capacity * 2u;
    if (wanted <= tree->capacity) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_OUT_OF_MEMORY,
                                "layout tree capacity overflowed");
    }
    grown = (robo_layout_object_t *)robo_realloc(tree->objects,
                                                wanted * sizeof(robo_layout_object_t));
    if (!grown) {
        /* The old arena is still valid and still owned, so a failed add leaves a
         * usable tree rather than a dangling one. */
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_OUT_OF_MEMORY,
                                "could not grow the layout object arena");
    }
    /* Zero the new half so that no field of a fresh slot is inherited from
     * whatever the allocator had. */
    memset(grown + tree->capacity, 0,
           (wanted - tree->capacity) * sizeof(robo_layout_object_t));
    tree->objects = grown;
    tree->capacity = wanted;
    return ROBO_OK;
}

robo_result_t robo_layout_tree_add_object(robo_layout_tree_t *tree,
                                          const robo_layout_object_info_t *info,
                                          robo_layout_node_id_t *out_id,
                                          robo_layout_diag_t *diag)
{
    robo_layout_object_t *object;
    robo_layout_object_t *parent;
    robo_layout_node_id_t id;
    size_t index;
    int is_root;

    if (out_id) {
        *out_id = ROBO_LAYOUT_NODE_ID_NONE;
    }
    if (!tree || !info || !out_id) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_INVALID_ARGUMENT,
                                "tree, info or out_id is NULL");
    }
    if (!robo_layout_object_kind_is_known(info->kind) ||
        info->kind == ROBO_LAYOUT_OBJECT_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_INVALID_ARGUMENT,
                                "the object kind is not a defined kind");
    }
    if (!robo_html_node_handle_is_wellformed(&info->dom_node)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_INVALID_ARGUMENT,
                                "the node handle is not well formed");
    }
    /*
     * The node must be the *right kind* of node for the object being built, and
     * the check is here rather than in the builder because it is the invariant the
     * arena is allowed to rely on afterwards: a text object whose handle named an
     * element would read an empty payload and measure nothing, and an element box
     * whose handle named a text node would have a `display` nobody computed. Both
     * would build, and both would be wrong several phases later, so the mismatch
     * fails where it is made.
     */
    {
        robo_html_node_view_t node_view;

        if (robo_html_node_get_view(&info->dom_node, &node_view, NULL) != ROBO_OK) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_NOT_FOUND,
                                    "the node handle did not resolve");
        }
        if (info->kind == ROBO_LAYOUT_OBJECT_TEXT) {
            if (node_view.kind != ROBO_HTML_NODE_KIND_TEXT) {
                return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_INVALID_ARGUMENT,
                                        "a text object's node must be a text node");
            }
        } else if (node_view.kind != ROBO_HTML_NODE_KIND_ELEMENT) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_INVALID_ARGUMENT,
                                    "a box's node must be an element");
        }
    }
    if (info->kind == ROBO_LAYOUT_OBJECT_BOX && !robo_css_display_generates_box(info->display)) {
        /* `display: none` means no box. Accepting one would make this tree a copy
         * of the DOM's element list, and every later pass would have to re-filter
         * what the container had already failed to. */
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_INVALID_ARGUMENT,
                                "the display does not generate a box");
    }
    if ((info->flags & ~(uint32_t)ROBO_LAYOUT_OBJECT_FLAG_ALL) != 0u) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_INVALID_ARGUMENT,
                                "object flags carry an undefined bit");
    }

    is_root = info->parent == ROBO_LAYOUT_NODE_ID_NONE;
    if (is_root) {
        if (tree->count != 0u) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_ALREADY_EXISTS,
                                    "the tree already has a root box");
        }
    } else if (!robo_layout_object_slot(tree, info->parent)) {
        /* Existence only: the *pointer* is taken below, after the reserve. */
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_NOT_FOUND,
                                "the parent id names no box in this tree");
    }

    if (layout_tree_reserve(tree, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }

    /*
     * Resolved *after* the reserve, and that ordering is load-bearing rather than
     * tidy. The reserve may realloc the arena, so a parent slot taken before it is
     * a dangling pointer the moment the arena moves -- and the arena only moves
     * once a tree outgrows ROBO_LAYOUT_INITIAL_CAPACITY, so the symptom is a tree
     * that builds correctly for eight boxes and corrupts the eighth parent after
     * that. Reading the parent through a stale pointer is the one way an add-only
     * tree with realloc-backed storage can be wrong.
     */
    parent = is_root ? NULL : robo_layout_object_slot(tree, info->parent);

    /*
     * The classification is released by the reserve above, which may have moved
     * the context arena as well as the object one. Done here rather than at the
     * top so a *refused* add -- a bad display, a second root, an unknown flag --
     * leaves the tree exactly as it was: a classification is invalid only because
     * the box set changed, and a refused add does not change it.
     */
    robo_layout_tree_contexts_reset(tree);

    id = tree->next_id++;
    index = tree->count;
    object = &tree->objects[index];
    memset(object, 0, sizeof(*object));
    object->id = id;
    object->kind = info->kind;
    object->dom_node = info->dom_node;
    object->style = info->style;
    /*
     * A text object's display is *derived*, not copied: its content is
     * inline-level by construction and no declaration could make it otherwise
     * (RoboLayoutObject.h). Deriving it here rather than asking the caller to set
     * it is what keeps the two passes free of a special case -- they dispatch on
     * this field, and a text object reads as an inline participant because that is
     * what it is.
     */
    object->display = info->kind == ROBO_LAYOUT_OBJECT_TEXT ? ROBO_CSS_DISPLAY_INLINE
                                                           : info->display;
    object->parent = is_root ? ROBO_LAYOUT_NODE_ID_NONE : info->parent;
    object->first_child = ROBO_LAYOUT_NODE_ID_NONE;
    object->last_child = ROBO_LAYOUT_NODE_ID_NONE;
    object->prev_sibling = ROBO_LAYOUT_NODE_ID_NONE;
    object->next_sibling = ROBO_LAYOUT_NODE_ID_NONE;
    object->child_count = 0u;
    object->depth = parent ? parent->depth + 1u : 0u;
    object->flags = info->flags;
    if (!info->style) {
        /* Stated, not inferred: a box with no style is a box a formatting context
         * must not read a property off. */
        object->flags |= ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING;
    }
    /* No pass has spoken about this box yet. The *pass* is what moves it to
     * UNRESOLVED, so "the tree was built" and "a pass saw it" stay different
     * facts -- see RoboLayoutResult.h. */
    robo_layout_result_init(&object->result);

    if (parent) {
        if (parent->first_child == ROBO_LAYOUT_NODE_ID_NONE) {
            parent->first_child = id;
        } else {
            robo_layout_object_t *previous =
                robo_layout_object_slot(tree, parent->last_child);

            previous->next_sibling = id;
            object->prev_sibling = previous->id;
        }
        parent->last_child = id;
        parent->child_count++;
    }

    tree->count = index + 1u;
    *out_id = id;
    return ROBO_OK;
}

robo_layout_node_id_t robo_layout_object_id_for_dom_node(
    const robo_layout_tree_t *tree,
    const robo_html_node_handle_t *dom_node)
{
    size_t index;

    if (!tree || !dom_node) {
        return ROBO_LAYOUT_NODE_ID_NONE;
    }
    for (index = 0u; index < tree->count; index++) {
        if (robo_html_node_handle_equal(&tree->objects[index].dom_node, dom_node)) {
            return tree->objects[index].id;
        }
    }
    return ROBO_LAYOUT_NODE_ID_NONE;
}

/* ------------------------------------------------------------------ */
/* The formatting-context arena (Phase 10.2)                           */
/* ------------------------------------------------------------------ */

const robo_layout_formatting_context_t *robo_layout_fc_slot_const(
    const robo_layout_tree_t *tree,
    robo_layout_fc_id_t id)
{
    size_t index;

    if (!tree || id == ROBO_LAYOUT_FC_ID_NONE) {
        return NULL;
    }
    /* Dense and 1-based, and the id is checked against the slot rather than only
     * against the count -- the same discipline the object arena uses, so an id
     * from a previous classification is a miss rather than an alias. */
    index = (size_t)(id - 1u);
    if (index >= tree->context_count) {
        return NULL;
    }
    if (tree->contexts[index].id != id) {
        return NULL;
    }
    return &tree->contexts[index];
}

robo_layout_formatting_context_t *robo_layout_fc_slot(robo_layout_tree_t *tree,
                                                     robo_layout_fc_id_t id)
{
    size_t index;

    if (!tree || id == ROBO_LAYOUT_FC_ID_NONE) {
        return NULL;
    }
    index = (size_t)(id - 1u);
    if (index >= tree->context_count) {
        return NULL;
    }
    if (tree->contexts[index].id != id) {
        return NULL;
    }
    return &tree->contexts[index];
}

/**
 * Make room for one more context, doubling the arena.
 * @return ROBO_OK, or ROBO_OUT_OF_MEMORY leaving the tree untouched.
 */
static robo_result_t layout_tree_context_reserve(robo_layout_tree_t *tree,
                                                robo_layout_diag_t *diag)
{
    size_t wanted;
    robo_layout_formatting_context_t *grown;

    if (tree->contexts == NULL) {
        tree->contexts = (robo_layout_formatting_context_t *)robo_calloc(
            ROBO_LAYOUT_INITIAL_CONTEXT_CAPACITY,
            sizeof(robo_layout_formatting_context_t));
        if (!tree->contexts) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                    ROBO_OUT_OF_MEMORY,
                                    "could not allocate the formatting-context arena");
        }
        tree->context_capacity = ROBO_LAYOUT_INITIAL_CONTEXT_CAPACITY;
        return ROBO_OK;
    }
    if (tree->context_count < tree->context_capacity) {
        return ROBO_OK;
    }
    wanted = tree->context_capacity * 2u;
    if (wanted <= tree->context_capacity) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_OUT_OF_MEMORY,
                                "formatting-context capacity overflowed");
    }
    grown = (robo_layout_formatting_context_t *)robo_realloc(
        tree->contexts, wanted * sizeof(robo_layout_formatting_context_t));
    if (!grown) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_OUT_OF_MEMORY,
                                "could not grow the formatting-context arena");
    }
    memset(grown + tree->context_capacity, 0,
           (wanted - tree->context_capacity) * sizeof(robo_layout_formatting_context_t));
    tree->contexts = grown;
    tree->context_capacity = wanted;
    return ROBO_OK;
}

robo_result_t robo_layout_tree_context_add(robo_layout_tree_t *tree,
                                           robo_layout_fc_kind_t kind,
                                           robo_layout_node_id_t owner,
                                           robo_layout_fc_id_t parent,
                                           uint32_t depth,
                                           robo_layout_fc_id_t *out_id,
                                           robo_layout_diag_t *diag)
{
    robo_layout_formatting_context_t *context;
    robo_layout_fc_id_t id;

    if (out_id) {
        *out_id = ROBO_LAYOUT_FC_ID_NONE;
    }
    if (!tree || !out_id) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "tree or out_id is NULL");
    }
    if (!robo_layout_fc_kind_is_known(kind) || kind == ROBO_LAYOUT_FC_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "the kind is not a context kind");
    }
    if (owner == ROBO_LAYOUT_NODE_ID_NONE || !robo_layout_object_slot(tree, owner)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_NOT_FOUND, "the owner id names no box in this tree");
    }
    /* ROOT is the only context with no enclosing one. Every other kind is
     * established *inside* something, so a NONE parent would be a context floating
     * outside the tree it describes. */
    if (kind == ROBO_LAYOUT_FC_ROOT) {
        if (parent != ROBO_LAYOUT_FC_ID_NONE) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                    ROBO_INVALID_ARGUMENT,
                                    "the initial formatting context has no parent");
        }
    } else if (robo_layout_fc_slot(tree, parent) == NULL) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_NOT_FOUND,
                                "the parent context id names no context in this tree");
    }

    if (layout_tree_context_reserve(tree, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }

    id = (robo_layout_fc_id_t)(tree->context_count + 1u);
    context = &tree->contexts[tree->context_count];
    robo_layout_formatting_context_init(context);
    context->id = id;
    context->kind = kind;
    context->owner = owner;
    context->parent = (kind == ROBO_LAYOUT_FC_ROOT) ? ROBO_LAYOUT_FC_ID_NONE : parent;
    context->participants = 0u;
    context->depth = depth;
    tree->context_count++;
    *out_id = id;
    return ROBO_OK;
}

void robo_layout_tree_context_add_participant(robo_layout_tree_t *tree,
                                              robo_layout_fc_id_t id)
{
    robo_layout_formatting_context_t *context = robo_layout_fc_slot(tree, id);

    if (context) {
        context->participants++;
    }
}

size_t robo_layout_tree_context_count(const robo_layout_tree_t *tree)
{
    return tree ? tree->context_count : 0u;
}

int robo_layout_tree_is_classified(const robo_layout_tree_t *tree)
{
    return tree && tree->classified;
}

robo_result_t robo_layout_tree_context_at(const robo_layout_tree_t *tree,
                                         size_t index,
                                         robo_layout_formatting_context_t *out,
                                         robo_layout_diag_t *diag)
{
    if (!out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    robo_layout_formatting_context_init(out);
    if (!tree) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "tree is NULL");
    }
    if (index >= tree->context_count) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_NOT_FOUND, "the index is past the context count");
    }
    *out = tree->contexts[index];
    return ROBO_OK;
}

robo_result_t robo_layout_tree_context(const robo_layout_tree_t *tree,
                                      robo_layout_fc_id_t id,
                                      robo_layout_formatting_context_t *out,
                                      robo_layout_diag_t *diag)
{
    if (!out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    robo_layout_formatting_context_init(out);
    if (!tree || id == ROBO_LAYOUT_FC_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "tree is NULL or the id is NONE");
    }
    {
        const robo_layout_formatting_context_t *context = robo_layout_fc_slot_const(tree, id);

        if (!context) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                    ROBO_NOT_FOUND,
                                    "the id names no context in this tree");
        }
        *out = *context;
    }
    return ROBO_OK;
}

robo_result_t robo_layout_tree_root_context(const robo_layout_tree_t *tree,
                                           robo_layout_fc_id_t *out,
                                           robo_layout_diag_t *diag)
{
    if (!out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    *out = ROBO_LAYOUT_FC_ID_NONE;
    if (!tree) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_INVALID_ARGUMENT, "tree is NULL");
    }
    if (!robo_layout_tree_is_classified(tree)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                ROBO_NOT_FOUND, "the tree has not been classified");
    }
    /* The initial context is the first one the pass establishes -- the root box is
     * created first, so it is established first. Stated as a search rather than as
     * "slot 0" because the ordering is a property of the pass and this door should
     * keep answering correctly if a later goal establishes something before it. */
    {
        size_t index;

        for (index = 0u; index < tree->context_count; index++) {
            if (tree->contexts[index].kind == ROBO_LAYOUT_FC_ROOT) {
                *out = tree->contexts[index].id;
                return ROBO_OK;
            }
        }
    }
    return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT, ROBO_NOT_FOUND,
                            "the tree has no root box, so it has no initial context");
}

/* ------------------------------------------------------------------ */
/* The line-box arena (Phase 10.6)                                     */
/* ------------------------------------------------------------------ */

/*
 * Two arenas, one writer, four doors -- and the same three rules the context
 * arena above follows: released rather than zeroed, ids are `count + 1`, and
 * nothing public can create one.
 *
 * The one thing that is different is the *member* chain, and the internal header
 * argues why it has to be a chain: the placements of one line are interleaved with
 * another's, because laying out an atomic inline recurses into the block pass for
 * its contents before the inline itself is placed. So a member record names the
 * next member of its own line and the line names the head, which makes a line's
 * membership exact rather than merely usually-contiguous -- at the price of O(n) to
 * read the n-th member, which the public door says out loud.
 */

void robo_layout_tree_lines_reset(robo_layout_tree_t *tree)
{
    size_t index;

    if (!tree) {
        return;
    }
    /* Released rather than zeroed, like the contexts: the next pass rebuilds lines
     * wholesale, so keeping a grown arena would hold memory for a shape nobody has
     * asked for. */
    robo_free(tree->lines);
    tree->lines = NULL;
    tree->line_count = 0u;
    tree->line_capacity = 0u;
    robo_free(tree->members);
    tree->members = NULL;
    tree->member_count = 0u;
    tree->member_capacity = 0u;

    /*
     * And every object's `line` id goes with them. This is the one place this module
     * *does* sweep the boxes rather than validating on read, and the asymmetry with
     * the context field is deliberate: ids restart at 1 with every pass, so a stale
     * line id does not merely name nothing -- it names *a different line*, and a
     * reader would get a plausible rectangle for the wrong text. The context field
     * has no such failure mode (a released classification leaves nothing to read),
     * and it is also swept here, in the same loop, because the two facts are released
     * together and one loop is cheaper than two.
     */
    for (index = 0u; index < tree->count; index++) {
        tree->objects[index].line = ROBO_LAYOUT_LINE_ID_NONE;
        /*
         * And the baseline with it (10.7), for the same reason and in the same sweep:
         * a baseline is what a participant was *placed* with, so it is a conclusion of
         * the pass that produced the lines and not a property of the object. Leaving it
         * behind would be worse than leaving a line id behind -- a stale line id names a
         * *different* line, which a caller can at least notice, while a stale baseline
         * is a plausible-looking ascent for a document that is no longer laid out.
         */
        robo_layout_baseline_metrics_init(&tree->objects[index].baseline);
        /*
         * And the used margins too (10.9), in the same sweep and for the same reason:
         * a used margin is the number a pass *concluded* from a containing block, so a
         * record that outlived the classification would be a plausible-looking gap
         * about a layout that no longer holds -- the exact failure mode a stale
         * baseline has, one axis further out.
         *
         * Cleared to no margins beside no containing block, which is the record's own
         * cleared state rather than a claim: `robo_layout_object_margins` refuses an
         * object that was never placed, so "no margins were used" and "no pass has
         * said anything" stay distinguishable through the result's state.
         */
        robo_layout_margin_edges_init(&tree->objects[index].margins, 0);
        /*
         * And the used padding (10.10), in the same sweep and for the same reason, with
         * one difference worth stating: a margin is a conclusion *about a containing
         * block*, while a padding is a conclusion about the box's own style -- so it is
         * the steadiest of the three records to leave behind and still the wrong one to
         * leave. Its four numbers come from declarations that do not change between
         * passes, but "this box has padding" *is* a statement of a pass, and
         * `robo_layout_object_padding` refuses an object that was never placed precisely
         * so that the record's silence cannot be read as an answer.
         */
        robo_layout_padding_edges_init(&tree->objects[index].padding);
        /*
         * And the used border (10.11), the last of the four insets and the outermost of
         * the two that belong to the box. It is steadier than the padding even -- the
         * used widths come from declarations and a style that do not change between
         * passes -- and it is cleared for the reason every record above is: it carries
         * the *conclusion* that a side draws, and a conclusion that outlived the box set
         * it was drawn from would be a border a paint pass could believe. The door
         * refuses an object that was never placed so that the record's silence cannot be
         * read as an answer.
         */
        robo_layout_border_edges_init(&tree->objects[index].borders);
        /*
         * And the position offset (10.12), the one record here that is a *displacement*
         * rather than an extent or an inset. It is cleared in the same sweep and for the
         * same reason -- a displacement that outlived the pass that concluded it would be
         * a box drawn somewhere the current layout never put it -- and it needs the
         * clearing more than the three above it, because its zero is the *common* value:
         * every `static` box resolves to one, so "no offset" and "no pass" are the same
         * record and only the result's state tells them apart.
         */
        robo_layout_position_offset_init(&tree->objects[index].position_offset);
    }
}

/** Grow the line arena if it is full. 0 on success, ROBO_OUT_OF_MEMORY otherwise. */
static robo_result_t layout_tree_line_reserve(robo_layout_tree_t *tree,
                                              robo_layout_diag_t *diag)
{
    size_t wanted;
    layout_line_slot_t *grown;

    if (tree->lines == NULL) {
        tree->lines = (layout_line_slot_t *)robo_calloc(ROBO_LAYOUT_INITIAL_LINE_CAPACITY,
                                                       sizeof(layout_line_slot_t));
        if (!tree->lines) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_OUT_OF_MEMORY,
                                    "could not allocate the line-box arena");
        }
        tree->line_capacity = ROBO_LAYOUT_INITIAL_LINE_CAPACITY;
        return ROBO_OK;
    }
    if (tree->line_count < tree->line_capacity) {
        return ROBO_OK;
    }
    wanted = tree->line_capacity * 2u;
    if (wanted <= tree->line_capacity) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_OUT_OF_MEMORY,
                                "line-box capacity overflowed");
    }
    grown = (layout_line_slot_t *)robo_realloc(tree->lines,
                                               wanted * sizeof(layout_line_slot_t));
    if (!grown) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_OUT_OF_MEMORY,
                                "could not grow the line-box arena");
    }
    memset(grown + tree->line_capacity, 0,
           (wanted - tree->line_capacity) * sizeof(layout_line_slot_t));
    tree->lines = grown;
    tree->line_capacity = wanted;
    return ROBO_OK;
}

/** Grow the member arena if it is full. 0 on success, ROBO_OUT_OF_MEMORY otherwise. */
static robo_result_t layout_tree_member_reserve(robo_layout_tree_t *tree,
                                                robo_layout_diag_t *diag)
{
    size_t wanted;
    layout_line_member_t *grown;

    if (tree->members == NULL) {
        tree->members = (layout_line_member_t *)robo_calloc(
            ROBO_LAYOUT_INITIAL_LINE_MEMBER_CAPACITY, sizeof(layout_line_member_t));
        if (!tree->members) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_OUT_OF_MEMORY,
                                    "could not allocate the line-member arena");
        }
        tree->member_capacity = ROBO_LAYOUT_INITIAL_LINE_MEMBER_CAPACITY;
        return ROBO_OK;
    }
    if (tree->member_count < tree->member_capacity) {
        return ROBO_OK;
    }
    wanted = tree->member_capacity * 2u;
    if (wanted <= tree->member_capacity) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_OUT_OF_MEMORY,
                                "line-member capacity overflowed");
    }
    grown = (layout_line_member_t *)robo_realloc(
        tree->members, wanted * sizeof(layout_line_member_t));
    if (!grown) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_OUT_OF_MEMORY,
                                "could not grow the line-member arena");
    }
    memset(grown + tree->member_capacity, 0,
           (wanted - tree->member_capacity) * sizeof(layout_line_member_t));
    tree->members = grown;
    tree->member_capacity = wanted;
    return ROBO_OK;
}

robo_result_t robo_layout_line_members_append(robo_layout_tree_t *tree,
                                             robo_layout_node_id_t node,
                                             layout_line_member_id_t *head,
                                             layout_line_member_id_t *tail,
                                             size_t *count,
                                             robo_layout_diag_t *diag)
{
    layout_line_member_t *member;
    layout_line_member_id_t id;

    if (!tree || !head || !tail || !count) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "a member append needs a tree and the chain's three fields");
    }
    if (node == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "the object id is NONE");
    }
    if (layout_tree_member_reserve(tree, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }

    /*
     * Allocated, linked and counted in one call, in that order, so an append either
     * happened or did not: a record that was allocated and not linked would be a member
     * no line names, and a link to an id that was never allocated would be a chain that
     * ends in nothing. The reserve above is what makes each of the three infallible
     * once reached, which is the only reason this can promise it.
     */
    id = (layout_line_member_id_t)(tree->member_count + 1u);
    member = &tree->members[tree->member_count];
    member->node = node;
    member->next = LAYOUT_LINE_MEMBER_ID_NONE;
    tree->member_count++;

    if (*tail != LAYOUT_LINE_MEMBER_ID_NONE) {
        layout_line_member_t *previous = &tree->members[*tail - 1u];

        previous->next = id;
    } else {
        *head = id;
    }
    *tail = id;
    *count += 1u;
    return ROBO_OK;
}

robo_result_t robo_layout_line_add(robo_layout_tree_t *tree,
                                   robo_layout_fc_id_t context,
                                   robo_layout_node_id_t owner,
                                   size_t index,
                                   robo_layout_point_t origin,
                                   robo_layout_size_t size,
                                   robo_layout_baseline_metrics_t baseline,
                                   int32_t available_width,
                                   layout_line_member_id_t first_member,
                                   size_t members,
                                   robo_layout_line_id_t *out_id,
                                   robo_layout_diag_t *diag)
{
    layout_line_slot_t *slot;
    robo_layout_line_id_t id;

    if (out_id) {
        *out_id = ROBO_LAYOUT_LINE_ID_NONE;
    }
    if (!tree || !out_id) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "tree or out_id is NULL");
    }
    /*
     * An owner that names no object is refused rather than stored, for the reason the
     * context arena refuses one: a line nothing can attribute is a line a reader can
     * only misread. A NONE *context* is refused for the same reason; a context id that
     * names none is refused too, because the pass reads it off the box that
     * established the context rather than inventing one, so a miss here is a bug in
     * the pass and not a document shape.
     */
    if (owner == ROBO_LAYOUT_NODE_ID_NONE || !robo_layout_object_slot(tree, owner)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_NOT_FOUND,
                                "the line's owner id names no object in this tree");
    }
    if (context == ROBO_LAYOUT_FC_ID_NONE || robo_layout_fc_slot(tree, context) == NULL) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_NOT_FOUND,
                                "the line names no live formatting context");
    }
    /* A line is finalized, so an open one cannot be stored: the construction record
     * is the pass's and never reaches here (RoboLayoutLineBox.h). What the pass passes
     * is the *state it is storing*, and the only state the arena can hold is FINAL. */
    if (members == 0u || first_member == LAYOUT_LINE_MEMBER_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "a finalized line has at least one member");
    }
    /*
     * Every stored line has a usable baseline (RoboLayoutLineBox.h), and this is the
     * one place that can be enforced rather than asserted. It is worth enforcing here
     * because the alternative failure is silent in a way the module's other refusals
     * are not: a line stored with a NONE baseline is a perfectly-shaped record whose
     * floor is *absent*, and a renderer that read it would place text at the line's
     * top edge instead of on the baseline -- a wrong picture, not a crash, and one
     * nobody would attribute to a missing enumerator.
     */
    if (!robo_layout_baseline_metrics_is_valid(&baseline)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "a finalized line needs a usable baseline");
    }

    if (layout_tree_line_reserve(tree, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }

    id = (robo_layout_line_id_t)(tree->line_count + 1u);
    slot = &tree->lines[tree->line_count];
    robo_layout_line_box_init(&slot->box);
    slot->box.id = id;
    slot->box.state = ROBO_LAYOUT_LINE_FINAL;
    slot->box.context = context;
    slot->box.owner = owner;
    slot->box.index = index;
    slot->box.origin = origin;
    slot->box.size = size;
    slot->box.baseline = baseline;
    slot->box.available_width = available_width;
    slot->box.members = members;
    slot->first_member = first_member;
    tree->line_count++;
    *out_id = id;
    return ROBO_OK;
}

const layout_line_slot_t *robo_layout_line_slot_const(const robo_layout_tree_t *tree,
                                                      robo_layout_line_id_t id)
{
    if (!tree || id == ROBO_LAYOUT_LINE_ID_NONE || id > tree->line_count) {
        return NULL;
    }
    return &tree->lines[id - 1u];
}

const layout_line_member_t *robo_layout_line_member_slot_const(
    const robo_layout_tree_t *tree,
    layout_line_member_id_t id)
{
    if (!tree || id == LAYOUT_LINE_MEMBER_ID_NONE || id > tree->member_count) {
        return NULL;
    }
    return &tree->members[id - 1u];
}

/* ------------------------------------------------------------------ */
/* The read side                                                       */
/* ------------------------------------------------------------------ */

size_t robo_layout_tree_line_count(const robo_layout_tree_t *tree)
{
    return tree ? tree->line_count : 0u;
}

robo_result_t robo_layout_tree_line_at(const robo_layout_tree_t *tree,
                                       size_t index,
                                       robo_layout_line_id_t *out_id,
                                       robo_layout_diag_t *diag)
{
    if (!out_id) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "out_id is NULL");
    }
    *out_id = ROBO_LAYOUT_LINE_ID_NONE;
    if (!tree) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "tree is NULL");
    }
    if (index >= tree->line_count) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_NOT_FOUND,
                                "the index is past the line count");
    }
    /* Ids are dense and ordered, so the n-th line's id *is* n + 1 -- which is what
     * makes walking the lines a loop rather than a lookup (RoboLayoutLineBox.h). */
    *out_id = (robo_layout_line_id_t)(index + 1u);
    return ROBO_OK;
}

robo_result_t robo_layout_tree_line(const robo_layout_tree_t *tree,
                                    robo_layout_line_id_t id,
                                    robo_layout_line_box_t *out,
                                    robo_layout_diag_t *diag)
{
    const layout_line_slot_t *slot;

    if (!out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "out is NULL");
    }
    robo_layout_line_box_init(out);
    if (!tree || id == ROBO_LAYOUT_LINE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT,
                                ROBO_INVALID_ARGUMENT, "tree is NULL or the id is NONE");
    }
    slot = robo_layout_line_slot_const(tree, id);
    if (!slot) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_NOT_FOUND,
                                "the id names no line in this tree");
    }
    *out = slot->box;
    return ROBO_OK;
}

robo_result_t robo_layout_line_member(const robo_layout_tree_t *tree,
                                      robo_layout_line_id_t id,
                                      size_t index,
                                      robo_layout_node_id_t *out_node,
                                      robo_layout_diag_t *diag)
{
    const layout_line_slot_t *slot;
    const layout_line_member_t *member;
    size_t walked = 0u;

    if (!out_node) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "out_node is NULL");
    }
    *out_node = ROBO_LAYOUT_NODE_ID_NONE;
    if (!tree || id == ROBO_LAYOUT_LINE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT,
                                ROBO_INVALID_ARGUMENT, "tree is NULL or the id is NONE");
    }
    slot = robo_layout_line_slot_const(tree, id);
    if (!slot) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_NOT_FOUND,
                                "the id names no line in this tree");
    }
    if (index >= slot->box.members) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_NOT_FOUND,
                                "the index is past the line's member count");
    }
    /*
     * The walk, and the reason the door is O(index) rather than O(1): a line's
     * members are a chain, because the placements of two lines interleave (the
     * internal header). The count in the record is what bounds it, so a corrupt link
     * cannot walk off the end -- the loop ends on the index, and the *count* was
     * validated above.
     */
    member = robo_layout_line_member_slot_const(tree, slot->first_member);
    while (member && walked < index) {
        member = robo_layout_line_member_slot_const(tree, member->next);
        walked++;
    }
    if (!member) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT, ROBO_ERROR,
                                "a line's member chain ended before its own count");
    }
    *out_node = member->node;
    return ROBO_OK;
}
