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
    *out_tree = tree;
    return ROBO_OK;
}

void robo_layout_tree_destroy(robo_layout_tree_t *tree)
{
    if (!tree) {
        return;
    }
    /* One free for the whole arena: no recursion, no per-node teardown to get
     * wrong, and nothing borrowed is touched. */
    robo_free(tree->objects);
    tree->objects = NULL;
    tree->count = 0u;
    tree->capacity = 0u;
    tree->next_id = 1u;
    robo_free(tree);
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
    if (!robo_html_node_handle_is_wellformed(&info->dom_node)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TREE, ROBO_INVALID_ARGUMENT,
                                "the element handle is not well formed");
    }
    if (!robo_css_display_generates_box(info->display)) {
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

    id = tree->next_id++;
    index = tree->count;
    object = &tree->objects[index];
    memset(object, 0, sizeof(*object));
    object->id = id;
    object->kind = ROBO_LAYOUT_OBJECT_BOX;
    object->dom_node = info->dom_node;
    object->style = info->style;
    object->display = info->display;
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
