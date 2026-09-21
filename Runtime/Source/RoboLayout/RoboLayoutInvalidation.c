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

int LayoutInvalidationStateIsKnown(uint32_t state)
{
    return (state & ~(uint32_t)RoboLayoutInvalidationAll) == 0u;
}

const char *LayoutInvalidationStateName(uint32_t state)
{
    static const struct {
        uint32_t bit;
        const char *name;
    } parts[] = {
        { RoboLayoutInvalidationStyle, "style" },
        { RoboLayoutInvalidationLayout, "layout" },
        { RoboLayoutInvalidationSubtree, "subtree" },
        { RoboLayoutInvalidationOverflow, "overflow" }
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

    if (state == RoboLayoutInvalidationClean) {
        return "clean";
    }
    if (!LayoutInvalidationStateIsKnown(state)) {
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

int LayoutInvalidationReasonIsKnown(LayoutInvalidationReason reason)
{
    return reason >= RoboLayoutInvalidationReasonNone &&
           reason < (LayoutInvalidationReason)RoboLayoutInvalidationReasonCount;
}

const char *LayoutInvalidationReasonName(LayoutInvalidationReason reason)
{
    switch (reason) {
    case RoboLayoutInvalidationReasonNone: return "none";
    case RoboLayoutInvalidationReasonPaint: return "paint";
    case RoboLayoutInvalidationReasonGeometry: return "geometry";
    case RoboLayoutInvalidationReasonPosition: return "position";
    case RoboLayoutInvalidationReasonContent: return "content";
    case RoboLayoutInvalidationReasonViewport: return "viewport";
    case RoboLayoutInvalidationReasonPropagated: return "propagated";
    default: break;
    }
    return "unknown";
}

int LayoutStyleEffectIsKnown(LayoutStyleEffect effect)
{
    return effect >= RoboLayoutStyleEffectNone &&
           effect < (LayoutStyleEffect)RoboLayoutStyleEffectCount;
}

const char *LayoutStyleEffectName(LayoutStyleEffect effect)
{
    switch (effect) {
    case RoboLayoutStyleEffectNone: return "none";
    case RoboLayoutStyleEffectPaint: return "paint";
    case RoboLayoutStyleEffectLayout: return "layout";
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
    LayoutStyleEffect effect;
} layoutStyleEffectTable[] = {
    /* Geometry: the box itself. */
    { "display", RoboLayoutStyleEffectLayout },
    { "position", RoboLayoutStyleEffectLayout },
    { "top", RoboLayoutStyleEffectLayout },
    { "right", RoboLayoutStyleEffectLayout },
    { "bottom", RoboLayoutStyleEffectLayout },
    { "left", RoboLayoutStyleEffectLayout },
    { "width", RoboLayoutStyleEffectLayout },
    { "height", RoboLayoutStyleEffectLayout },
    { "min-width", RoboLayoutStyleEffectLayout },
    { "min-height", RoboLayoutStyleEffectLayout },
    { "max-width", RoboLayoutStyleEffectLayout },
    { "max-height", RoboLayoutStyleEffectLayout },
    { "box-sizing", RoboLayoutStyleEffectLayout },

    /* Geometry: the box model. */
    { "margin", RoboLayoutStyleEffectLayout },
    { "margin-top", RoboLayoutStyleEffectLayout },
    { "margin-right", RoboLayoutStyleEffectLayout },
    { "margin-bottom", RoboLayoutStyleEffectLayout },
    { "margin-left", RoboLayoutStyleEffectLayout },
    { "padding", RoboLayoutStyleEffectLayout },
    { "padding-top", RoboLayoutStyleEffectLayout },
    { "padding-right", RoboLayoutStyleEffectLayout },
    { "padding-bottom", RoboLayoutStyleEffectLayout },
    { "padding-left", RoboLayoutStyleEffectLayout },
    { "border", RoboLayoutStyleEffectLayout },
    { "border-width", RoboLayoutStyleEffectLayout },
    { "border-style", RoboLayoutStyleEffectLayout },
    { "border-top", RoboLayoutStyleEffectLayout },
    { "border-right", RoboLayoutStyleEffectLayout },
    { "border-bottom", RoboLayoutStyleEffectLayout },
    { "border-left", RoboLayoutStyleEffectLayout },
    { "border-top-width", RoboLayoutStyleEffectLayout },
    { "border-right-width", RoboLayoutStyleEffectLayout },
    { "border-bottom-width", RoboLayoutStyleEffectLayout },
    { "border-left-width", RoboLayoutStyleEffectLayout },
    { "border-top-style", RoboLayoutStyleEffectLayout },
    { "border-right-style", RoboLayoutStyleEffectLayout },
    { "border-bottom-style", RoboLayoutStyleEffectLayout },
    { "border-left-style", RoboLayoutStyleEffectLayout },

    /* Geometry: what is not the box, but changes where its contents go. */
    { "overflow", RoboLayoutStyleEffectLayout },
    { "overflow-x", RoboLayoutStyleEffectLayout },
    { "overflow-y", RoboLayoutStyleEffectLayout },
    { "font-family", RoboLayoutStyleEffectLayout },
    { "font-size", RoboLayoutStyleEffectLayout },
    { "font-weight", RoboLayoutStyleEffectLayout },
    { "font-style", RoboLayoutStyleEffectLayout },
    { "font-stretch", RoboLayoutStyleEffectLayout },
    { "line-height", RoboLayoutStyleEffectLayout },
    { "letter-spacing", RoboLayoutStyleEffectLayout },
    { "word-spacing", RoboLayoutStyleEffectLayout },
    { "direction", RoboLayoutStyleEffectLayout },
    { "writing-mode", RoboLayoutStyleEffectLayout },

    /* Drawn, not measured. */
    { "color", RoboLayoutStyleEffectPaint },
    { "background", RoboLayoutStyleEffectPaint },
    { "background-color", RoboLayoutStyleEffectPaint },
    { "background-image", RoboLayoutStyleEffectPaint },
    { "border-color", RoboLayoutStyleEffectPaint },
    { "border-top-color", RoboLayoutStyleEffectPaint },
    { "border-right-color", RoboLayoutStyleEffectPaint },
    { "border-bottom-color", RoboLayoutStyleEffectPaint },
    { "border-left-color", RoboLayoutStyleEffectPaint },
    { "visibility", RoboLayoutStyleEffectPaint },
    { "opacity", RoboLayoutStyleEffectPaint },
    { "text-decoration", RoboLayoutStyleEffectPaint }
};

/** Lower-case ASCII comparison of @p a and @p b, both NUL-terminated. */
static int LayoutNameEquals(const char *a, const char *b)
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

LayoutStyleEffect LayoutStyleEffectForProperty(const char *propertyName)
{
    size_t i;

    if (!propertyName) {
        return RoboLayoutStyleEffectNone;
    }
    for (i = 0u; i < sizeof(layoutStyleEffectTable) / sizeof(layoutStyleEffectTable[0]); i++) {
        if (LayoutNameEquals(propertyName, layoutStyleEffectTable[i].name)) {
            return layoutStyleEffectTable[i].effect;
        }
    }
    return RoboLayoutStyleEffectNone;
}

/* ------------------------------------------------------------------ */
/* Records                                                             */
/* ------------------------------------------------------------------ */

void LayoutInvalidationInit(LayoutInvalidation *record)
{
    if (record) {
        record->state = RoboLayoutInvalidationClean;
        record->reason = RoboLayoutInvalidationReasonNone;
    }
}

int LayoutInvalidationIsClean(const LayoutInvalidation *record)
{
    return record == NULL || record->state == RoboLayoutInvalidationClean;
}

uint32_t GetLayoutInvalidationState(const LayoutInvalidation *record)
{
    return record ? record->state : RoboLayoutInvalidationClean;
}

LayoutInvalidationReason MakeLayoutInvalidationReason(
    const LayoutInvalidation *record)
{
    return record ? record->reason : RoboLayoutInvalidationReasonNone;
}

int LayoutInvalidationEqual(const LayoutInvalidation *a,
                                   const LayoutInvalidation *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->state == b->state && a->reason == b->reason;
}

void LayoutInvalidationStatsInit(LayoutInvalidationStats *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
        stats->root = RoboLayoutNodeIdNone;
        stats->reason = RoboLayoutInvalidationReasonNone;
    }
}

int LayoutInvalidationStatsEqual(const LayoutInvalidationStats *a,
                                         const LayoutInvalidationStats *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->invalidated == b->invalidated && a->styleOnly == b->styleOnly &&
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
 * `LayoutTreeIsClean` a read rather than a walk. A box that was clean and is
 * no longer increments it; a box that was dirty and is now clean decrements it. The
 * transition is tested on the state alone, so a box marked twice is counted once --
 * which is the property a suite asserts by invalidating one box twice.
 */
static void LayoutInvalidationApply(LayoutTree *tree,
                                      LayoutObject *object,
                                      uint32_t bits,
                                      LayoutInvalidationReason reason)
{
    int wasClean;

    if (!tree || !object || bits == 0u) {
        return;
    }
    wasClean = object->invalidation.state == RoboLayoutInvalidationClean;
    if (wasClean) {
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
    if (reason != RoboLayoutInvalidationReasonPropagated &&
        (object->invalidation.reason == RoboLayoutInvalidationReasonNone ||
         object->invalidation.reason == RoboLayoutInvalidationReasonPropagated)) {
        object->invalidation.reason = reason;
    }
}

/** Mark @p id and every descendant of it, depth-first through the child links. */
static void LayoutInvalidationMarkDescendants(LayoutTree *tree,
                                                 LayoutNodeId id,
                                                 uint32_t bits,
                                                 LayoutInvalidationReason reason)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);
    LayoutNodeId child;

    if (!object) {
        return;
    }
    LayoutInvalidationApply(tree, LayoutObjectSlot(tree, id), bits, reason);
    for (child = object->firstChild; child != RoboLayoutNodeIdNone; ) {
        const LayoutObject *current = LayoutObjectSlotConst(tree, child);
        LayoutNodeId next = RoboLayoutNodeIdNone;

        if (!current) {
            break;
        }
        next = current->NextSibling;
        LayoutInvalidationMarkDescendants(tree, child, bits, reason);
        child = next;
    }
}

/** Every ancestor of @p id gets @p bits, up to the root. */
static void LayoutInvalidationMarkAncestors(LayoutTree *tree,
                                               LayoutNodeId id,
                                               uint32_t bits)
{
    LayoutNodeId parent = id;

    while (parent != RoboLayoutNodeIdNone) {
        const LayoutObject *object = LayoutObjectSlotConst(tree, parent);

        if (!object) {
            return;
        }
        parent = object->parent;
        if (parent == RoboLayoutNodeIdNone) {
            return;
        }
        LayoutInvalidationApply(tree, LayoutObjectSlot(tree, parent), bits,
                                  RoboLayoutInvalidationReasonPropagated);
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
static void LayoutInvalidationMarkFollowing(LayoutTree *tree,
                                               LayoutNodeId id)
{
    LayoutNodeId node = id;

    while (node != RoboLayoutNodeIdNone) {
        const LayoutObject *object = LayoutObjectSlotConst(tree, node);
        LayoutNodeId sibling;

        if (!object) {
            return;
        }
        for (sibling = object->NextSibling; sibling != RoboLayoutNodeIdNone; ) {
            const LayoutObject *current =
                LayoutObjectSlotConst(tree, sibling);
            LayoutNodeId next = RoboLayoutNodeIdNone;

            if (!current) {
                break;
            }
            next = current->NextSibling;
            LayoutInvalidationMarkDescendants(tree, sibling,
                                                 RoboLayoutInvalidationLayoutBits,
                                                 RoboLayoutInvalidationReasonPropagated);
            sibling = next;
        }
        node = object->parent;
    }
}

void LayoutInvalidationMarkNew(LayoutTree *tree,
                                       LayoutObject *object)
{
    LayoutInvalidationApply(tree, object,
                              (uint32_t)RoboLayoutInvalidationLayoutBits,
                              RoboLayoutInvalidationReasonContent);
}

/* ------------------------------------------------------------------ */
/* Reading the tree                                                    */
/* ------------------------------------------------------------------ */

uint32_t LayoutTreeInvalidationState(const LayoutTree *tree,
                                             LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);

    return object ? object->invalidation.state : RoboLayoutInvalidationClean;
}

LayoutInvalidationReason LayoutTreeInvalidationReason(
    const LayoutTree *tree,
    LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);

    if (!object || object->invalidation.state == RoboLayoutInvalidationClean) {
        return RoboLayoutInvalidationReasonNone;
    }
    return object->invalidation.reason;
}

int LayoutTreeIsClean(const LayoutTree *tree)
{
    return tree == NULL || tree->invalidated == 0u;
}

static int LayoutTreeHasLayoutBits(const LayoutTree *tree,
                                       size_t *outCount,
                                       size_t *outStyleOnly)
{
    size_t index;
    size_t count = LayoutTreeObjectCount(tree);
    size_t dirty = 0u;
    size_t styleOnly = 0u;

    for (index = 0u; index < count; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        uint32_t state;

        if (LayoutObjectIdAt(tree, index, &id) != RoboOk) {
            break;
        }
        state = LayoutTreeInvalidationState(tree, id);
        if ((state & (uint32_t)RoboLayoutInvalidationLayoutBits) != 0u) {
            dirty++;
        } else if ((state & (uint32_t)RoboLayoutInvalidationStyle) != 0u) {
            styleOnly++;
        }
    }
    if (outCount) {
        *outCount = dirty;
    }
    if (outStyleOnly) {
        *outStyleOnly = styleOnly;
    }
    return dirty != 0u;
}

int LayoutTreeNeedsLayout(const LayoutTree *tree)
{
    if (!tree) {
        return 0;
    }
    return LayoutTreeHasLayoutBits(tree, NULL, NULL);
}

size_t LayoutTreeInvalidationCount(const LayoutTree *tree)
{
    size_t count = 0u;

    if (!tree) {
        return 0u;
    }
    (void)LayoutTreeHasLayoutBits(tree, &count, NULL);
    return count;
}

size_t LayoutInvalidationStyleOnlyCount(const LayoutTree *tree)
{
    size_t styleOnly = 0u;

    if (!tree) {
        return 0u;
    }
    (void)LayoutTreeHasLayoutBits(tree, NULL, &styleOnly);
    return styleOnly;
}

/** Non-zero when @p candidate is @p id itself or one of its ancestors. */
static int LayoutInvalidationContains(const LayoutTree *tree,
                                        LayoutNodeId candidate,
                                        LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, candidate);
    const LayoutObject *walk = LayoutObjectSlotConst(tree, id);
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
        walk = LayoutObjectSlotConst(tree, walk->parent);
        if (!walk) {
            return 0;
        }
    }
    return walk->id == candidate;
}

LayoutNodeId LayoutTreeInvalidationRoot(const LayoutTree *tree)
{
    size_t index;
    size_t count;
    LayoutNodeId first = RoboLayoutNodeIdNone;
    LayoutNodeId candidate;

    if (!tree) {
        return RoboLayoutNodeIdNone;
    }
    count = LayoutTreeObjectCount(tree);

    /*
     * The first box whose own geometry is stale, in creation order. It is used only as a
     * *starting point* for the walk below: the answer is the deepest ancestor-or-self of
     * this box that also contains every other one, so the choice of starting box cannot
     * change the result -- which is the determinism this door promises.
     */
    for (index = 0u; index < count; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        const LayoutObject *object;

        if (LayoutObjectIdAt(tree, index, &id) != RoboOk) {
            break;
        }
        object = LayoutObjectSlotConst(tree, id);
        if (object && (object->invalidation.state &
                       (uint32_t)RoboLayoutInvalidationOwnBits) != 0u) {
            first = id;
            break;
        }
    }
    if (first == RoboLayoutNodeIdNone) {
        return RoboLayoutNodeIdNone;
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
    for (candidate = first; candidate != RoboLayoutNodeIdNone; ) {
        const LayoutObject *object = LayoutObjectSlotConst(tree, candidate);
        LayoutNodeId parent;
        int containsAll = 1;

        if (!object) {
            break;
        }
        for (index = 0u; index < count; index++) {
            LayoutNodeId id = RoboLayoutNodeIdNone;
            const LayoutObject *other;

            if (LayoutObjectIdAt(tree, index, &id) != RoboOk) {
                break;
            }
            other = LayoutObjectSlotConst(tree, id);
            if (!other || (other->invalidation.state &
                           (uint32_t)RoboLayoutInvalidationOwnBits) == 0u) {
                continue;
            }
            if (!LayoutInvalidationContains(tree, candidate, id)) {
                containsAll = 0;
                break;
            }
        }
        if (containsAll) {
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

Result LayoutTreeMarkClean(LayoutTree *tree)
{
    size_t index;
    size_t count;

    if (!tree) {
        return RoboOk;
    }
    count = LayoutTreeObjectCount(tree);
    for (index = 0u; index < count; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;

        if (LayoutObjectIdAt(tree, index, &id) != RoboOk) {
            break;
        }
        LayoutInvalidationInit(&LayoutObjectSlot(tree, id)->invalidation);
    }
    tree->invalidated = 0u;
    return RoboOk;
}

Result LayoutTreeMarkRecomputed(LayoutTree *tree)
{
    size_t index;
    size_t count;
    size_t remaining = 0u;

    if (!tree) {
        return RoboOk;
    }
    count = LayoutTreeObjectCount(tree);
    for (index = 0u; index < count; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        LayoutObject *object;

        if (LayoutObjectIdAt(tree, index, &id) != RoboOk) {
            break;
        }
        object = LayoutObjectSlot(tree, id);
        /*
         * The layout bits are satisfied by a recomputation; STYLE is not, because a
         * repaint is not something a layout pass performed. A box left holding only
         * STYLE is therefore *not* clean and *not* in need of layout -- which is the
         * whole reason the two questions have two doors.
         */
        object->invalidation.state &= (uint32_t)RoboLayoutInvalidationStyle;
        if (object->invalidation.state == RoboLayoutInvalidationClean) {
            object->invalidation.reason = RoboLayoutInvalidationReasonNone;
        } else {
            remaining++;
        }
    }
    tree->invalidated = remaining;
    return RoboOk;
}

Result LayoutTreeMarkAll(LayoutTree *tree,
                                        LayoutInvalidationReason reason,
                                        LayoutDiag *diag)
{
    size_t index;
    size_t count;

    if (!tree || !LayoutInvalidationReasonIsKnown(reason)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboInvalidArgument,
                                "tree is NULL or the reason is undefined");
    }
    count = LayoutTreeObjectCount(tree);
    for (index = 0u; index < count; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;

        if (LayoutObjectIdAt(tree, index, &id) != RoboOk) {
            break;
        }
        LayoutInvalidationApply(tree, LayoutObjectSlot(tree, id),
                                  (uint32_t)RoboLayoutInvalidationLayoutBits, reason);
    }
    return RoboOk;
}

Result LayoutTreeMarkStyle(LayoutTree *tree,
                                          LayoutNodeId id,
                                          LayoutStyleEffect effect,
                                          LayoutDiag *diag)
{
    if (!tree || id == RoboLayoutNodeIdNone || !LayoutStyleEffectIsKnown(effect)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboInvalidArgument,
                                "tree is NULL, the id is NONE, or the effect is undefined");
    }
    if (!LayoutObjectSlot(tree, id)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboNotFound,
                                "no box has that id");
    }
    if (effect == RoboLayoutStyleEffectNone) {
        /* Nothing this build reads changed. Marking would be the conservative answer,
         * and the conservative answer here is a whole document's geometry. */
        return RoboOk;
    }
    if (effect == RoboLayoutStyleEffectPaint) {
        LayoutInvalidationApply(tree, LayoutObjectSlot(tree, id),
                                  (uint32_t)RoboLayoutInvalidationStyle,
                                  RoboLayoutInvalidationReasonPaint);
        return RoboOk;
    }
    return LayoutTreeMarkLayoutDirty(tree, id,
                                              RoboLayoutInvalidationReasonGeometry, diag);
}

Result LayoutTreeMarkStyleProperty(LayoutTree *tree,
                                                   LayoutNodeId id,
                                                   const char *propertyName,
                                                   LayoutDiag *diag)
{
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboInvalidArgument,
                                "tree is NULL or the id is NONE");
    }
    if (!LayoutObjectSlot(tree, id)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboNotFound,
                                "no box has that id");
    }
    return LayoutTreeMarkStyle(tree, id,
                                       LayoutStyleEffectForProperty(propertyName),
                                       diag);
}

Result LayoutTreeMarkLayoutDirty(LayoutTree *tree,
                                                 LayoutNodeId id,
                                                 LayoutInvalidationReason reason,
                                                 LayoutDiag *diag)
{
    if (!tree || id == RoboLayoutNodeIdNone ||
        !LayoutInvalidationReasonIsKnown(reason)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboInvalidArgument,
                                "tree is NULL, the id is NONE, or the reason is undefined");
    }
    if (!LayoutObjectSlot(tree, id)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboNotFound,
                                "no box has that id");
    }
    LayoutInvalidationApply(tree, LayoutObjectSlot(tree, id),
                              (uint32_t)RoboLayoutInvalidationLayoutBits, reason);
    LayoutInvalidationMarkAncestors(tree, id,
                                       RoboLayoutInvalidationSubtree |
                                           RoboLayoutInvalidationOverflow);
    LayoutInvalidationMarkFollowing(tree, id);
    return RoboOk;
}

Result LayoutTreeMarkSubtreeDirty(LayoutTree *tree,
                                                  LayoutNodeId id,
                                                  LayoutInvalidationReason reason,
                                                  LayoutDiag *diag)
{
    if (!tree || id == RoboLayoutNodeIdNone ||
        !LayoutInvalidationReasonIsKnown(reason)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboInvalidArgument,
                                "tree is NULL, the id is NONE, or the reason is undefined");
    }
    if (!LayoutObjectSlot(tree, id)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboNotFound,
                                "no box has that id");
    }
    LayoutInvalidationMarkDescendants(tree, id,
                                         (uint32_t)RoboLayoutInvalidationLayoutBits,
                                         reason);
    LayoutInvalidationMarkAncestors(tree, id,
                                       RoboLayoutInvalidationSubtree |
                                           RoboLayoutInvalidationOverflow);
    LayoutInvalidationMarkFollowing(tree, id);
    return RoboOk;
}

Result LayoutTreeMarkPositionDirty(LayoutTree *tree,
                                                   LayoutNodeId id,
                                                   LayoutDiag *diag)
{
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboInvalidArgument,
                                "tree is NULL or the id is NONE");
    }
    if (!LayoutObjectSlot(tree, id)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboNotFound,
                                "no box has that id");
    }
    /*
     * SUBTREE is included on the box itself because a displacement moves the contents
     * with it -- a descendant's *final* position is derived from its parent's content
     * origin -- while no ancestor takes SUBTREE, because the box still occupies the
     * space flow gave it (10.12's contract).
     */
    LayoutInvalidationApply(tree, LayoutObjectSlot(tree, id),
                              (uint32_t)RoboLayoutInvalidationLayoutBits,
                              RoboLayoutInvalidationReasonPosition);
    LayoutInvalidationMarkAncestors(tree, id, (uint32_t)RoboLayoutInvalidationOverflow);
    return RoboOk;
}

Result LayoutTreeMarkOverflowDirty(LayoutTree *tree,
                                                   LayoutNodeId id,
                                                   LayoutDiag *diag)
{
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboInvalidArgument,
                                "tree is NULL or the id is NONE");
    }
    if (!LayoutObjectSlot(tree, id)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInvalidation, RoboNotFound,
                                "no box has that id");
    }
    LayoutInvalidationApply(tree, LayoutObjectSlot(tree, id),
                              (uint32_t)RoboLayoutInvalidationOverflow,
                              RoboLayoutInvalidationReasonGeometry);
    LayoutInvalidationMarkAncestors(tree, id, (uint32_t)RoboLayoutInvalidationOverflow);
    return RoboOk;
}
