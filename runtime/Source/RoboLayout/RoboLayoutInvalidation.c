/*
 * RoboLayout invalidation (Phase 10.14) — see RoboLayoutInvalidation.h.
 *
 * Three things live here and they are worth keeping apart:
 *
 *   the **vocabulary** — the state bits, the reasons, the three style effects, and
 *   their names. Nothing above this layer holds a second copy of any of them;
 *
 *   the **classifier** — a table of property names and what changing one costs
 *   layout. It is a table rather than a switch so that the question "does this
 *   module read that property?" is answerable by reading one page;
 *
 *   the **policy** — four marking doors and the propagation each one performs.
 *   The policy is the phase, and every rule in it is stated once, here, with the
 *   reason it exists.
 *
 * The reason a box is dirty is kept under a rule rather than being overwritten
 * blindly, and the rule is worth stating because two propagation paths can reach one
 * box: an **explicit** reason (a caller said "this box's geometry changed") replaces
 * a NONE or a PROPAGATED one and is never replaced by either, while a second explicit
 * reason arriving at an already-explicitly-dirty box keeps the first. That is
 * deterministic in the marking order -- which the doors fix -- and it means the
 * reason a caller reads is always either the cause or "something below you", never a
 * later propagation overwriting the interesting answer.
 */

#include "RoboLayout/RoboLayoutInvalidation.h"

#include <string.h>

#include "RoboLayoutInternal.h"

#include "RoboLayout/RoboLayoutObject.h"

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

int robo_layout_invalidation_state_is_known(uint32_t state)
{
    return (state & ~(uint32_t)ROBO_LAYOUT_INVALIDATION_ALL) == 0u;
}

const char *robo_layout_invalidation_state_name(uint32_t state)
{
    static const struct {
        uint32_t bit;
        const char *name;
    } parts[] = {
        { ROBO_LAYOUT_INVALIDATION_STYLE, "style" },
        { ROBO_LAYOUT_INVALIDATION_LAYOUT, "layout" },
        { ROBO_LAYOUT_INVALIDATION_SUBTREE, "subtree" },
        { ROBO_LAYOUT_INVALIDATION_OVERFLOW, "overflow" }
    };
    /*
     * A threaded scratch buffer, in the same spirit as the diagnostic module's: the
     * answer is a *string* and the caller is not handed a lifetime. It is
     * deliberately not reentrant -- this is a debug door, and a caller that formatted
     * two states into one expression would be relying on argument evaluation order,
     * which is the kind of undisciplined thing this module refuses elsewhere.
     */
    static char buffer[64];
    size_t used = 0u;
    size_t i;

    if (state == ROBO_LAYOUT_INVALIDATION_CLEAN) {
        return "clean";
    }
    if (!robo_layout_invalidation_state_is_known(state)) {
        return "unknown";
    }
    buffer[0] = '\0';
    for (i = 0u; i < sizeof(parts) / sizeof(parts[0]); i++) {
        size_t length;

        if ((state & parts[i].bit) == 0u) {
            continue;
        }
        length = strlen(parts[i].name);
        if (used != 0u) {
            if (used + 1u >= sizeof(buffer)) {
                break;
            }
            buffer[used++] = '+';
        }
        if (used + length + 1u > sizeof(buffer)) {
            break;
        }
        memcpy(buffer + used, parts[i].name, length);
        used += length;
        buffer[used] = '\0';
    }
    return buffer;
}

int robo_layout_invalidation_reason_is_known(robo_layout_invalidation_reason_t reason)
{
    return reason >= ROBO_LAYOUT_INVALIDATION_REASON_NONE &&
           reason < (robo_layout_invalidation_reason_t)ROBO_LAYOUT_INVALIDATION_REASON_COUNT;
}

const char *robo_layout_invalidation_reason_name(robo_layout_invalidation_reason_t reason)
{
    switch (reason) {
    case ROBO_LAYOUT_INVALIDATION_REASON_NONE: return "none";
    case ROBO_LAYOUT_INVALIDATION_REASON_PAINT: return "paint";
    case ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY: return "geometry";
    case ROBO_LAYOUT_INVALIDATION_REASON_POSITION: return "position";
    case ROBO_LAYOUT_INVALIDATION_REASON_CONTENT: return "content";
    case ROBO_LAYOUT_INVALIDATION_REASON_VIEWPORT: return "viewport";
    case ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED: return "propagated";
    default: break;
    }
    return "unknown";
}

int robo_layout_style_effect_is_known(robo_layout_style_effect_t effect)
{
    return effect >= ROBO_LAYOUT_STYLE_EFFECT_NONE &&
           effect < (robo_layout_style_effect_t)ROBO_LAYOUT_STYLE_EFFECT_COUNT;
}

const char *robo_layout_style_effect_name(robo_layout_style_effect_t effect)
{
    switch (effect) {
    case ROBO_LAYOUT_STYLE_EFFECT_NONE: return "none";
    case ROBO_LAYOUT_STYLE_EFFECT_PAINT: return "paint";
    case ROBO_LAYOUT_STYLE_EFFECT_LAYOUT: return "layout";
    default: break;
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* The classifier                                                      */
/* ------------------------------------------------------------------ */

/*
 * What changing one property costs **layout**.
 *
 * Grouped by *why* rather than alphabetically, because the grouping is the argument:
 * every name in the first run below is read by a phase that computes a coordinate or
 * a length, every name in the second is read by nothing that moves, and the two empty
 * answers are for the opposite cases.
 *
 * The border family is the one that needs its split spelled out: `border-width` and
 * `border-style` are both LAYOUT, because 10.11's used width is *zero unless the
 * style draws a border* -- so `border-style: none` is a geometry change and not a
 * cosmetic one -- while the three colour longhands and their shorthand are paint
 * only.
 *
 * `font-*`, `line-height`, `letter-spacing` and `word-spacing` are LAYOUT because a
 * text run's advance and its ascent/descent come from a font through 10.5's seam and
 * decide where every participant on a line goes. `direction` and `writing-mode` are
 * LAYOUT because 10.4 places participants along a line in a declared direction.
 */
static const struct {
    const char *name;
    robo_layout_style_effect_t effect;
} layout_style_effect_table[] = {
    /* Geometry: the box itself. */
    { "display", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "position", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "top", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "right", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "bottom", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "left", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "width", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "height", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "min-width", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "min-height", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "max-width", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "max-height", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "box-sizing", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },

    /* Geometry: the box model. */
    { "margin", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "margin-top", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "margin-right", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "margin-bottom", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "margin-left", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "padding", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "padding-top", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "padding-right", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "padding-bottom", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "padding-left", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-width", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-style", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-top", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-right", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-bottom", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-left", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-top-width", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-right-width", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-bottom-width", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-left-width", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-top-style", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-right-style", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-bottom-style", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "border-left-style", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },

    /* Geometry: what is not the box, but changes where its contents go. */
    { "overflow", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "overflow-x", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "overflow-y", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "font-family", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "font-size", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "font-weight", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "font-style", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "font-stretch", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "line-height", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "letter-spacing", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "word-spacing", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "direction", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },
    { "writing-mode", ROBO_LAYOUT_STYLE_EFFECT_LAYOUT },

    /* Drawn, not measured. */
    { "color", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "background", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "background-color", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "background-image", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "border-color", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "border-top-color", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "border-right-color", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "border-bottom-color", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "border-left-color", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "visibility", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "opacity", ROBO_LAYOUT_STYLE_EFFECT_PAINT },
    { "text-decoration", ROBO_LAYOUT_STYLE_EFFECT_PAINT }
};

/** Lower-case ASCII comparison of @p a and @p b, both NUL-terminated. */
static int layout_name_equals(const char *a, const char *b)
{
    size_t i = 0u;

    for (;;) {
        unsigned char left = (unsigned char)a[i];
        unsigned char right = (unsigned char)b[i];

        if (left >= (unsigned char)'A' && left <= (unsigned char)'Z') {
            left = (unsigned char)(left - (unsigned char)'A' + (unsigned char)'a');
        }
        if (right >= (unsigned char)'A' && right <= (unsigned char)'Z') {
            right = (unsigned char)(right - (unsigned char)'A' + (unsigned char)'a');
        }
        if (left != right) {
            return 0;
        }
        if (left == (unsigned char)'\0') {
            return 1;
        }
        i++;
    }
}

robo_layout_style_effect_t robo_layout_style_effect_for_property(const char *property_name)
{
    size_t i;

    if (!property_name) {
        return ROBO_LAYOUT_STYLE_EFFECT_NONE;
    }
    for (i = 0u; i < sizeof(layout_style_effect_table) / sizeof(layout_style_effect_table[0]); i++) {
        if (layout_name_equals(property_name, layout_style_effect_table[i].name)) {
            return layout_style_effect_table[i].effect;
        }
    }
    return ROBO_LAYOUT_STYLE_EFFECT_NONE;
}

/* ------------------------------------------------------------------ */
/* Records                                                             */
/* ------------------------------------------------------------------ */

void robo_layout_invalidation_init(robo_layout_invalidation_t *record)
{
    if (record) {
        record->state = ROBO_LAYOUT_INVALIDATION_CLEAN;
        record->reason = ROBO_LAYOUT_INVALIDATION_REASON_NONE;
    }
}

int robo_layout_invalidation_is_clean(const robo_layout_invalidation_t *record)
{
    return record == NULL || record->state == ROBO_LAYOUT_INVALIDATION_CLEAN;
}

uint32_t robo_layout_invalidation_state(const robo_layout_invalidation_t *record)
{
    return record ? record->state : ROBO_LAYOUT_INVALIDATION_CLEAN;
}

robo_layout_invalidation_reason_t robo_layout_invalidation_reason(
    const robo_layout_invalidation_t *record)
{
    return record ? record->reason : ROBO_LAYOUT_INVALIDATION_REASON_NONE;
}

int robo_layout_invalidation_equal(const robo_layout_invalidation_t *a,
                                   const robo_layout_invalidation_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->state == b->state && a->reason == b->reason;
}

void robo_layout_invalidation_stats_init(robo_layout_invalidation_stats_t *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
        stats->root = ROBO_LAYOUT_NODE_ID_NONE;
        stats->reason = ROBO_LAYOUT_INVALIDATION_REASON_NONE;
    }
}

int robo_layout_invalidation_stats_equal(const robo_layout_invalidation_stats_t *a,
                                         const robo_layout_invalidation_stats_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->invalidated == b->invalidated && a->style_only == b->style_only &&
           a->root == b->root && a->reason == b->reason &&
           a->recomputed == b->recomputed;
}

/* ------------------------------------------------------------------ */
/* Marking internals                                                   */
/* ------------------------------------------------------------------ */

/**
 * Apply @p bits and @p reason to one box, keeping the tree's count in step.
 *
 * The count is a count of boxes with **any** bit, which is what makes
 * `robo_layout_tree_is_clean` a read rather than a walk. A box that was clean and is
 * no longer increments it; a box that was dirty and is now clean decrements it. The
 * transition is tested on the state alone, so a box marked twice is counted once --
 * which is the property a suite asserts by invalidating one box twice.
 */
static void layout_invalidation_apply(robo_layout_tree_t *tree,
                                      robo_layout_object_t *object,
                                      uint32_t bits,
                                      robo_layout_invalidation_reason_t reason)
{
    int was_clean;

    if (!tree || !object || bits == 0u) {
        return;
    }
    was_clean = object->invalidation.state == ROBO_LAYOUT_INVALIDATION_CLEAN;
    if (was_clean) {
        object->invalidation.state = bits;
        object->invalidation.reason = reason;
        tree->invalidated++;
        return;
    }

    object->invalidation.state |= bits;
    /*
     * The reason rule. An explicit cause replaces "nothing" or "something below you";
     * a propagated cause never replaces an explicit one; and two explicit causes keep
     * the first, which makes the answer a function of the marking order rather than of
     * the last walk to reach the box.
     */
    if (reason != ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED &&
        (object->invalidation.reason == ROBO_LAYOUT_INVALIDATION_REASON_NONE ||
         object->invalidation.reason == ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED)) {
        object->invalidation.reason = reason;
    }
}

/** Mark @p id and every descendant of it, depth-first through the child links. */
static void layout_invalidation_mark_descendants(robo_layout_tree_t *tree,
                                                 robo_layout_node_id_t id,
                                                 uint32_t bits,
                                                 robo_layout_invalidation_reason_t reason)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);
    robo_layout_node_id_t child;

    if (!object) {
        return;
    }
    layout_invalidation_apply(tree, robo_layout_object_slot(tree, id), bits, reason);
    for (child = object->first_child; child != ROBO_LAYOUT_NODE_ID_NONE; ) {
        const robo_layout_object_t *current = robo_layout_object_slot_const(tree, child);
        robo_layout_node_id_t next = ROBO_LAYOUT_NODE_ID_NONE;

        if (!current) {
            break;
        }
        next = current->next_sibling;
        layout_invalidation_mark_descendants(tree, child, bits, reason);
        child = next;
    }
}

/** Every ancestor of @p id gets @p bits, up to the root. */
static void layout_invalidation_mark_ancestors(robo_layout_tree_t *tree,
                                               robo_layout_node_id_t id,
                                               uint32_t bits)
{
    robo_layout_node_id_t parent = id;

    while (parent != ROBO_LAYOUT_NODE_ID_NONE) {
        const robo_layout_object_t *object = robo_layout_object_slot_const(tree, parent);

        if (!object) {
            return;
        }
        parent = object->parent;
        if (parent == ROBO_LAYOUT_NODE_ID_NONE) {
            return;
        }
        layout_invalidation_apply(tree, robo_layout_object_slot(tree, parent), bits,
                                  ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED);
    }
}

/**
 * The **following-flow** rule: every box that comes after @p id in normal flow, and
 * everything inside it.
 *
 * Walking up from @a id and, at each step, marking the siblings that follow, is the
 * transitive closure of "the flow after this box" without a second pass over the
 * flow: an ancestor's later sibling *is* the rest of the document after the ancestor,
 * so marking it and its subtree covers everything that could have been positioned from
 * a box that moved.
 *
 * The alternative -- marking the whole tree -- would be correct and would make the
 * phase's central claim untestable, since a suite could not tell the two apart.
 */
static void layout_invalidation_mark_following(robo_layout_tree_t *tree,
                                               robo_layout_node_id_t id)
{
    robo_layout_node_id_t node = id;

    while (node != ROBO_LAYOUT_NODE_ID_NONE) {
        const robo_layout_object_t *object = robo_layout_object_slot_const(tree, node);
        robo_layout_node_id_t sibling;

        if (!object) {
            return;
        }
        for (sibling = object->next_sibling; sibling != ROBO_LAYOUT_NODE_ID_NONE; ) {
            const robo_layout_object_t *current =
                robo_layout_object_slot_const(tree, sibling);
            robo_layout_node_id_t next = ROBO_LAYOUT_NODE_ID_NONE;

            if (!current) {
                break;
            }
            next = current->next_sibling;
            layout_invalidation_mark_descendants(tree, sibling,
                                                 ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS,
                                                 ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED);
            sibling = next;
        }
        node = object->parent;
    }
}

void robo_layout_invalidation_mark_new(robo_layout_tree_t *tree,
                                       robo_layout_object_t *object)
{
    layout_invalidation_apply(tree, object,
                              (uint32_t)ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS,
                              ROBO_LAYOUT_INVALIDATION_REASON_CONTENT);
}

/* ------------------------------------------------------------------ */
/* Reading the tree                                                    */
/* ------------------------------------------------------------------ */

uint32_t robo_layout_tree_invalidation_state(const robo_layout_tree_t *tree,
                                             robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);

    return object ? object->invalidation.state : ROBO_LAYOUT_INVALIDATION_CLEAN;
}

robo_layout_invalidation_reason_t robo_layout_tree_invalidation_reason(
    const robo_layout_tree_t *tree,
    robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, id);

    if (!object || object->invalidation.state == ROBO_LAYOUT_INVALIDATION_CLEAN) {
        return ROBO_LAYOUT_INVALIDATION_REASON_NONE;
    }
    return object->invalidation.reason;
}

int robo_layout_tree_is_clean(const robo_layout_tree_t *tree)
{
    return tree == NULL || tree->invalidated == 0u;
}

static int layout_tree_has_layout_bits(const robo_layout_tree_t *tree,
                                       size_t *out_count,
                                       size_t *out_style_only)
{
    size_t index;
    size_t count = robo_layout_tree_object_count(tree);
    size_t dirty = 0u;
    size_t style_only = 0u;

    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        uint32_t state;

        if (robo_layout_object_id_at(tree, index, &id) != ROBO_OK) {
            break;
        }
        state = robo_layout_tree_invalidation_state(tree, id);
        if ((state & (uint32_t)ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS) != 0u) {
            dirty++;
        } else if ((state & (uint32_t)ROBO_LAYOUT_INVALIDATION_STYLE) != 0u) {
            style_only++;
        }
    }
    if (out_count) {
        *out_count = dirty;
    }
    if (out_style_only) {
        *out_style_only = style_only;
    }
    return dirty != 0u;
}

int robo_layout_tree_needs_layout(const robo_layout_tree_t *tree)
{
    if (!tree) {
        return 0;
    }
    return layout_tree_has_layout_bits(tree, NULL, NULL);
}

size_t robo_layout_tree_invalidation_count(const robo_layout_tree_t *tree)
{
    size_t count = 0u;

    if (!tree) {
        return 0u;
    }
    (void)layout_tree_has_layout_bits(tree, &count, NULL);
    return count;
}

size_t robo_layout_invalidation_style_only_count(const robo_layout_tree_t *tree)
{
    size_t style_only = 0u;

    if (!tree) {
        return 0u;
    }
    (void)layout_tree_has_layout_bits(tree, NULL, &style_only);
    return style_only;
}

/** Non-zero when @p candidate is @p id itself or one of its ancestors. */
static int layout_invalidation_contains(const robo_layout_tree_t *tree,
                                        robo_layout_node_id_t candidate,
                                        robo_layout_node_id_t id)
{
    const robo_layout_object_t *object = robo_layout_object_slot_const(tree, candidate);
    const robo_layout_object_t *walk = robo_layout_object_slot_const(tree, id);
    uint32_t steps;

    if (!object || !walk) {
        return 0;
    }
    if (walk->depth < object->depth) {
        return 0;
    }
    /* Up by exactly the depth difference, then one comparison: a box's ancestors are
     * uniquely determined by its parent links, so this is a walk and not a search. */
    for (steps = walk->depth - object->depth; steps > 0u; steps--) {
        walk = robo_layout_object_slot_const(tree, walk->parent);
        if (!walk) {
            return 0;
        }
    }
    return walk->id == candidate;
}

robo_layout_node_id_t robo_layout_tree_invalidation_root(const robo_layout_tree_t *tree)
{
    size_t index;
    size_t count;
    robo_layout_node_id_t first = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_node_id_t candidate;

    if (!tree) {
        return ROBO_LAYOUT_NODE_ID_NONE;
    }
    count = robo_layout_tree_object_count(tree);

    /*
     * The first box whose own geometry is stale, in creation order. It is used only as a
     * *starting point* for the walk below: the answer is the deepest ancestor-or-self of
     * this box that also contains every other one, so the choice of starting box cannot
     * change the result -- which is the determinism this door promises.
     */
    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        const robo_layout_object_t *object;

        if (robo_layout_object_id_at(tree, index, &id) != ROBO_OK) {
            break;
        }
        object = robo_layout_object_slot_const(tree, id);
        if (object && (object->invalidation.state &
                       (uint32_t)ROBO_LAYOUT_INVALIDATION_OWN_BITS) != 0u) {
            first = id;
            break;
        }
    }
    if (first == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_LAYOUT_NODE_ID_NONE;
    }

    /*
     * Walk up from the starting box and answer the **first** ancestor-or-self that
     * contains every stale box. That is the lowest common ancestor of the region, and it
     * is what a partial pass would have to start from: it is the only single box whose
     * subtree spans everything that needs placing. The root box contains everything, so
     * the loop always terminates with an answer.
     *
     * A shallowest-alone rule would be cheaper and wrong -- invalidating a leaf marks the
     * following flow as well, and the following flow is routinely *shallower* than the
     * leaf (a later sibling of an ancestor sits higher in the tree), so "shallowest"
     * could name a box whose subtree does not contain the change at all.
     */
    for (candidate = first; candidate != ROBO_LAYOUT_NODE_ID_NONE; ) {
        const robo_layout_object_t *object = robo_layout_object_slot_const(tree, candidate);
        robo_layout_node_id_t parent;
        int contains_all = 1;

        if (!object) {
            break;
        }
        for (index = 0u; index < count; index++) {
            robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
            const robo_layout_object_t *other;

            if (robo_layout_object_id_at(tree, index, &id) != ROBO_OK) {
                break;
            }
            other = robo_layout_object_slot_const(tree, id);
            if (!other || (other->invalidation.state &
                           (uint32_t)ROBO_LAYOUT_INVALIDATION_OWN_BITS) == 0u) {
                continue;
            }
            if (!layout_invalidation_contains(tree, candidate, id)) {
                contains_all = 0;
                break;
            }
        }
        if (contains_all) {
            return candidate;
        }
        parent = object->parent;
        candidate = parent;
    }
    return first;
}

/* ------------------------------------------------------------------ */
/* Marking doors                                                       */
/* ------------------------------------------------------------------ */

robo_result_t robo_layout_tree_mark_clean(robo_layout_tree_t *tree)
{
    size_t index;
    size_t count;

    if (!tree) {
        return ROBO_OK;
    }
    count = robo_layout_tree_object_count(tree);
    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

        if (robo_layout_object_id_at(tree, index, &id) != ROBO_OK) {
            break;
        }
        robo_layout_invalidation_init(&robo_layout_object_slot(tree, id)->invalidation);
    }
    tree->invalidated = 0u;
    return ROBO_OK;
}

robo_result_t robo_layout_tree_mark_recomputed(robo_layout_tree_t *tree)
{
    size_t index;
    size_t count;
    size_t remaining = 0u;

    if (!tree) {
        return ROBO_OK;
    }
    count = robo_layout_tree_object_count(tree);
    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_object_t *object;

        if (robo_layout_object_id_at(tree, index, &id) != ROBO_OK) {
            break;
        }
        object = robo_layout_object_slot(tree, id);
        /*
         * The layout bits are satisfied by a recomputation; STYLE is not, because a
         * repaint is not something a layout pass performed. A box left holding only
         * STYLE is therefore *not* clean and *not* in need of layout -- which is the
         * whole reason the two questions have two doors.
         */
        object->invalidation.state &= (uint32_t)ROBO_LAYOUT_INVALIDATION_STYLE;
        if (object->invalidation.state == ROBO_LAYOUT_INVALIDATION_CLEAN) {
            object->invalidation.reason = ROBO_LAYOUT_INVALIDATION_REASON_NONE;
        } else {
            remaining++;
        }
    }
    tree->invalidated = remaining;
    return ROBO_OK;
}

robo_result_t robo_layout_tree_mark_all(robo_layout_tree_t *tree,
                                        robo_layout_invalidation_reason_t reason,
                                        robo_layout_diag_t *diag)
{
    size_t index;
    size_t count;

    if (!tree || !robo_layout_invalidation_reason_is_known(reason)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                                "tree is NULL or the reason is undefined");
    }
    count = robo_layout_tree_object_count(tree);
    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

        if (robo_layout_object_id_at(tree, index, &id) != ROBO_OK) {
            break;
        }
        layout_invalidation_apply(tree, robo_layout_object_slot(tree, id),
                                  (uint32_t)ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS, reason);
    }
    return ROBO_OK;
}

robo_result_t robo_layout_tree_mark_style(robo_layout_tree_t *tree,
                                          robo_layout_node_id_t id,
                                          robo_layout_style_effect_t effect,
                                          robo_layout_diag_t *diag)
{
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE || !robo_layout_style_effect_is_known(effect)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                                "tree is NULL, the id is NONE, or the effect is undefined");
    }
    if (!robo_layout_object_slot(tree, id)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_NOT_FOUND,
                                "no box has that id");
    }
    if (effect == ROBO_LAYOUT_STYLE_EFFECT_NONE) {
        /* Nothing this build reads changed. Marking would be the conservative answer,
         * and the conservative answer here is a whole document's geometry. */
        return ROBO_OK;
    }
    if (effect == ROBO_LAYOUT_STYLE_EFFECT_PAINT) {
        layout_invalidation_apply(tree, robo_layout_object_slot(tree, id),
                                  (uint32_t)ROBO_LAYOUT_INVALIDATION_STYLE,
                                  ROBO_LAYOUT_INVALIDATION_REASON_PAINT);
        return ROBO_OK;
    }
    return robo_layout_tree_mark_layout_dirty(tree, id,
                                              ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY, diag);
}

robo_result_t robo_layout_tree_mark_style_property(robo_layout_tree_t *tree,
                                                   robo_layout_node_id_t id,
                                                   const char *property_name,
                                                   robo_layout_diag_t *diag)
{
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                                "tree is NULL or the id is NONE");
    }
    if (!robo_layout_object_slot(tree, id)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_NOT_FOUND,
                                "no box has that id");
    }
    return robo_layout_tree_mark_style(tree, id,
                                       robo_layout_style_effect_for_property(property_name),
                                       diag);
}

robo_result_t robo_layout_tree_mark_layout_dirty(robo_layout_tree_t *tree,
                                                 robo_layout_node_id_t id,
                                                 robo_layout_invalidation_reason_t reason,
                                                 robo_layout_diag_t *diag)
{
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE ||
        !robo_layout_invalidation_reason_is_known(reason)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                                "tree is NULL, the id is NONE, or the reason is undefined");
    }
    if (!robo_layout_object_slot(tree, id)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_NOT_FOUND,
                                "no box has that id");
    }
    layout_invalidation_apply(tree, robo_layout_object_slot(tree, id),
                              (uint32_t)ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS, reason);
    layout_invalidation_mark_ancestors(tree, id,
                                       ROBO_LAYOUT_INVALIDATION_SUBTREE |
                                           ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    layout_invalidation_mark_following(tree, id);
    return ROBO_OK;
}

robo_result_t robo_layout_tree_mark_subtree_dirty(robo_layout_tree_t *tree,
                                                  robo_layout_node_id_t id,
                                                  robo_layout_invalidation_reason_t reason,
                                                  robo_layout_diag_t *diag)
{
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE ||
        !robo_layout_invalidation_reason_is_known(reason)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                                "tree is NULL, the id is NONE, or the reason is undefined");
    }
    if (!robo_layout_object_slot(tree, id)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_NOT_FOUND,
                                "no box has that id");
    }
    layout_invalidation_mark_descendants(tree, id,
                                         (uint32_t)ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS,
                                         reason);
    layout_invalidation_mark_ancestors(tree, id,
                                       ROBO_LAYOUT_INVALIDATION_SUBTREE |
                                           ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    layout_invalidation_mark_following(tree, id);
    return ROBO_OK;
}

robo_result_t robo_layout_tree_mark_position_dirty(robo_layout_tree_t *tree,
                                                   robo_layout_node_id_t id,
                                                   robo_layout_diag_t *diag)
{
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                                "tree is NULL or the id is NONE");
    }
    if (!robo_layout_object_slot(tree, id)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_NOT_FOUND,
                                "no box has that id");
    }
    /*
     * SUBTREE is included on the box itself because a displacement moves the contents
     * with it -- a descendant's *final* position is derived from its parent's content
     * origin -- while no ancestor takes SUBTREE, because the box still occupies the
     * space flow gave it (10.12's contract).
     */
    layout_invalidation_apply(tree, robo_layout_object_slot(tree, id),
                              (uint32_t)ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS,
                              ROBO_LAYOUT_INVALIDATION_REASON_POSITION);
    layout_invalidation_mark_ancestors(tree, id, (uint32_t)ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    return ROBO_OK;
}

robo_result_t robo_layout_tree_mark_overflow_dirty(robo_layout_tree_t *tree,
                                                   robo_layout_node_id_t id,
                                                   robo_layout_diag_t *diag)
{
    if (!tree || id == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                                "tree is NULL or the id is NONE");
    }
    if (!robo_layout_object_slot(tree, id)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_INVALIDATION, ROBO_NOT_FOUND,
                                "no box has that id");
    }
    layout_invalidation_apply(tree, robo_layout_object_slot(tree, id),
                              (uint32_t)ROBO_LAYOUT_INVALIDATION_OVERFLOW,
                              ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY);
    layout_invalidation_mark_ancestors(tree, id, (uint32_t)ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    return ROBO_OK;
}
