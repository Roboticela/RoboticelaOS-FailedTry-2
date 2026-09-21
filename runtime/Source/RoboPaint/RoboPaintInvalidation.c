/*
 * RoboPaint invalidation (Phase 11.11) — see RoboPaintInvalidation.h.
 *
 * Three tables, one growable array and the arithmetic that fills it, and the arithmetic is
 * where every decision lives. Three of them are worth stating before the code, because they
 * are the ones a later reader would otherwise "simplify":
 *
 *   **A region is widened, never narrowed, when this module is not certain.** The one
 *   narrowing this file performs is against a boundary it can read *both axes* of
 *   (`robo_paint_clip_rect`), because that intersection can only remove pixels the boundary
 *   already prevents from being drawn. Everything else -- a free axis, a boundary that is not
 *   a record, a counter that says how many boxes changed but not where -- produces a
 *   rectangle that is too large, which costs work, rather than one that is too small, which
 *   leaves the previous picture on screen.
 *
 *   **Geometry first, then material.** The reason for a difference is decided in a fixed
 *   order -- a different node, then a different boundary, then a different strength, then a
 *   different rectangle, then a different kind -- so a box that moved is `geometry` (its
 *   rectangle is the fact that changed) while a box that kept its rectangle and changed its
 *   colour is `background`, `border`, `text` or `image`. Walking that order the other way
 *   would report a colour for a move, which is a reason that sends a reader to the wrong
 *   phase.
 *
 *   **The set is an array, and the order is the contract.** Regions are appended in the order
 *   their producers ran, that order is a function of the document and the artifacts, and
 *   nothing here hashes, sorts by address or reads a clock -- so the same transition twice
 *   produces equal sets, which is what `..._set_equal` and the determinism cases rely on.
 */

#include "RoboPaint/RoboPaintInvalidation.h"

#include "RoboPaint/RoboPaintOpacity.h"

#include "RoboPaintInternal.h"

#include "RoboMem/RoboMem.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Vocabularies                                                        */
/* ------------------------------------------------------------------ */

/** One reason row. */
typedef struct paint_invalidation_reason_row {
    robo_paint_invalidation_reason_t reason;
    const char *name;
} paint_invalidation_reason_row_t;

/*
 * Row per enumerator, as every table in this tree is written: a name added without a row, or
 * a row without a name, is a compile-time omission rather than a value that prints "unknown"
 * in a bug report. The names are the ones a dump prints, so they are spelled the way a person
 * would say the fact (`opacity`, `clip`, `geometry`).
 */
static const paint_invalidation_reason_row_t paint_invalidation_reason_table[] = {
    { ROBO_PAINT_INVALIDATION_REASON_NONE, "none" },
    { ROBO_PAINT_INVALIDATION_REASON_STYLE, "style" },
    { ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, "background" },
    { ROBO_PAINT_INVALIDATION_REASON_BORDER, "border" },
    { ROBO_PAINT_INVALIDATION_REASON_TEXT, "text" },
    { ROBO_PAINT_INVALIDATION_REASON_IMAGE, "image" },
    { ROBO_PAINT_INVALIDATION_REASON_GEOMETRY, "geometry" },
    { ROBO_PAINT_INVALIDATION_REASON_OPACITY, "opacity" },
    { ROBO_PAINT_INVALIDATION_REASON_CLIP, "clip" },
    { ROBO_PAINT_INVALIDATION_REASON_CONTENT, "content" },
    { ROBO_PAINT_INVALIDATION_REASON_VIEWPORT, "viewport" }
};

/** One class row. */
typedef struct paint_invalidation_class_row {
    robo_paint_invalidation_class_t klass;
    const char *name;
} paint_invalidation_class_row_t;

static const paint_invalidation_class_row_t paint_invalidation_class_table[] = {
    { ROBO_PAINT_INVALIDATION_CLASS_NONE, "none" },
    { ROBO_PAINT_INVALIDATION_CLASS_PAINT, "paint" },
    { ROBO_PAINT_INVALIDATION_CLASS_LAYOUT, "layout" }
};

/** One scope row. */
typedef struct paint_invalidation_scope_row {
    robo_paint_invalidation_scope_t scope;
    const char *name;
} paint_invalidation_scope_row_t;

static const paint_invalidation_scope_row_t paint_invalidation_scope_table[] = {
    { ROBO_PAINT_INVALIDATION_SCOPE_NONE, "none" },
    { ROBO_PAINT_INVALIDATION_SCOPE_PARTIAL, "partial" },
    { ROBO_PAINT_INVALIDATION_SCOPE_FULL, "full" }
};

int robo_paint_invalidation_reason_is_known(robo_paint_invalidation_reason_t reason)
{
    return reason >= ROBO_PAINT_INVALIDATION_REASON_NONE &&
           reason < (robo_paint_invalidation_reason_t)ROBO_PAINT_INVALIDATION_REASON_COUNT;
}

const char *robo_paint_invalidation_reason_name(robo_paint_invalidation_reason_t reason)
{
    size_t index;

    for (index = 0u;
         index < sizeof(paint_invalidation_reason_table) / sizeof(paint_invalidation_reason_table[0]);
         index++) {
        if (paint_invalidation_reason_table[index].reason == reason) {
            return paint_invalidation_reason_table[index].name;
        }
    }
    return "unknown";
}

int robo_paint_invalidation_class_is_known(robo_paint_invalidation_class_t klass)
{
    return klass >= ROBO_PAINT_INVALIDATION_CLASS_NONE &&
           klass < (robo_paint_invalidation_class_t)ROBO_PAINT_INVALIDATION_CLASS_COUNT;
}

const char *robo_paint_invalidation_class_name(robo_paint_invalidation_class_t klass)
{
    size_t index;

    for (index = 0u;
         index < sizeof(paint_invalidation_class_table) / sizeof(paint_invalidation_class_table[0]);
         index++) {
        if (paint_invalidation_class_table[index].klass == klass) {
            return paint_invalidation_class_table[index].name;
        }
    }
    return "unknown";
}

int robo_paint_invalidation_scope_is_known(robo_paint_invalidation_scope_t scope)
{
    return scope >= ROBO_PAINT_INVALIDATION_SCOPE_NONE &&
           scope < (robo_paint_invalidation_scope_t)ROBO_PAINT_INVALIDATION_SCOPE_COUNT;
}

const char *robo_paint_invalidation_scope_name(robo_paint_invalidation_scope_t scope)
{
    size_t index;

    for (index = 0u;
         index < sizeof(paint_invalidation_scope_table) / sizeof(paint_invalidation_scope_table[0]);
         index++) {
        if (paint_invalidation_scope_table[index].scope == scope) {
            return paint_invalidation_scope_table[index].name;
        }
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* The classifier                                                      */
/* ------------------------------------------------------------------ */

/** Lower-case ASCII comparison of @p a and @p b, both NUL-terminated. */
static int paint_invalidation_name_equals(const char *a, const char *b)
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

/** One property this build draws from, and the reason a change to it needs a repaint. */
typedef struct paint_invalidation_drawn_row {
    const char *name;
    robo_paint_invalidation_reason_t reason;
} paint_invalidation_drawn_row_t;

/*
 * **The properties this module draws from**, written out by hand, and the list is short on
 * purpose: it names what a *painting pass* reads, which is a different question from the one
 * `robo_layout_style_effect_for_property` answers ("does layout measure it?") and a much
 * smaller one.
 *
 * What is deliberately absent is the phase's own finding, and it is worth reading twice:
 * RoboLayout's PAINT row means "drawn, not measured", and three of its members are drawn by
 * **nobody** in this runtime -- `visibility` (registered, consumed by no layer), the
 * `background-image` longhand (no image background painting exists) and `text-decoration`
 * (no decoration painting exists). Answering `PAINT` for them would make a change that
 * produces an *identical picture* ask for a repaint, so they are absent here and the
 * classifier answers `NONE` for them. The case that pins it is
 * `paint.invalidation.unconsumed_properties`, and the goal that consumes one of them adds
 * its row here -- which is the same rule the module follows for an enumerator with no code
 * behind it.
 */
static const paint_invalidation_drawn_row_t paint_invalidation_drawn_table[] = {
    { "color", ROBO_PAINT_INVALIDATION_REASON_TEXT },
    { "border-color", ROBO_PAINT_INVALIDATION_REASON_BORDER },
    { "border-top-color", ROBO_PAINT_INVALIDATION_REASON_BORDER },
    { "border-right-color", ROBO_PAINT_INVALIDATION_REASON_BORDER },
    { "border-bottom-color", ROBO_PAINT_INVALIDATION_REASON_BORDER },
    { "border-left-color", ROBO_PAINT_INVALIDATION_REASON_BORDER },
    { "opacity", ROBO_PAINT_INVALIDATION_REASON_OPACITY },
    { "background", ROBO_PAINT_INVALIDATION_REASON_BACKGROUND },
    { "background-color", ROBO_PAINT_INVALIDATION_REASON_BACKGROUND }
};

/** The reason @p property_name's change needs, or NONE when this module draws nothing from it. */
static robo_paint_invalidation_reason_t paint_invalidation_reason_of_drawn(const char *name)
{
    size_t index;

    if (!name) {
        return ROBO_PAINT_INVALIDATION_REASON_NONE;
    }
    for (index = 0u;
         index < sizeof(paint_invalidation_drawn_table) / sizeof(paint_invalidation_drawn_table[0]);
         index++) {
        if (paint_invalidation_name_equals(name, paint_invalidation_drawn_table[index].name)) {
            return paint_invalidation_drawn_table[index].reason;
        }
    }
    return ROBO_PAINT_INVALIDATION_REASON_NONE;
}

robo_paint_invalidation_class_t robo_paint_invalidation_classify_property(
    const char *property_name)
{
    /*
     * One question is asked of RoboLayout and one of this table, and the order is what makes
     * the answer right: a property layout *measures* is a geometry change whatever it paints
     * (a `width` that repaints must first be re-laid-out), and only what layout does not
     * measure is asked of the drawn list.
     */
    if (robo_layout_style_effect_for_property(property_name) ==
        ROBO_LAYOUT_STYLE_EFFECT_LAYOUT) {
        return ROBO_PAINT_INVALIDATION_CLASS_LAYOUT;
    }
    if (paint_invalidation_reason_of_drawn(property_name) !=
        ROBO_PAINT_INVALIDATION_REASON_NONE) {
        return ROBO_PAINT_INVALIDATION_CLASS_PAINT;
    }
    /*
     * Everything else: a property layout does not measure and this module does not draw from.
     * `NONE` is the sound answer rather than the lazy one -- the table above names every
     * property any drawing in this build reads, so a name outside it cannot have changed a
     * pixel -- and it is the answer that keeps a `cursor` change or a `visibility` change from
     * asking for a repaint of a picture that did not move.
     */
    return ROBO_PAINT_INVALIDATION_CLASS_NONE;
}

robo_paint_invalidation_reason_t robo_paint_invalidation_reason_for_property(
    const char *property_name)
{
    robo_paint_invalidation_reason_t drawn = paint_invalidation_reason_of_drawn(property_name);

    if (drawn != ROBO_PAINT_INVALIDATION_REASON_NONE) {
        return drawn;
    }
    /*
     * A property layout measures repaints as a **coordinate** rather than as a material: the
     * declaration that moved the box is `width`, and the fact a repaint has to cover is that
     * the box is somewhere else. The reason therefore names the geometry, and the caller that
     * wants the declaration reads its own cascade.
     */
    if (robo_layout_style_effect_for_property(property_name) ==
        ROBO_LAYOUT_STYLE_EFFECT_LAYOUT) {
        return ROBO_PAINT_INVALIDATION_REASON_GEOMETRY;
    }
    return ROBO_PAINT_INVALIDATION_REASON_NONE;
}

/* ------------------------------------------------------------------ */
/* Regions                                                             */
/* ------------------------------------------------------------------ */

void robo_paint_invalidation_region_init(robo_paint_invalidation_region_t *region)
{
    if (!region) {
        return;
    }
    memset(region, 0, sizeof(*region));
    region->node = ROBO_LAYOUT_NODE_ID_NONE;
    region->reason = ROBO_PAINT_INVALIDATION_REASON_NONE;
    robo_paint_clip_init(&region->clip);
}

int robo_paint_invalidation_region_is_valid(const robo_paint_invalidation_region_t *region)
{
    if (!region) {
        return 0;
    }
    if (region->rect.width < 0 || region->rect.height < 0) {
        return 0;
    }
    /*
     * A region always says **why** it is there: `NONE` is the cleared record's value and not a
     * reason a producer may hand in, which is the same rule `RoboLayout`'s per-box record
     * states for its own reason field.
     */
    if (region->reason == ROBO_PAINT_INVALIDATION_REASON_NONE ||
        !robo_paint_invalidation_reason_is_known(region->reason)) {
        return 0;
    }
    if (!robo_paint_clip_is_valid(&region->clip)) {
        return 0;
    }
    if (region->narrowed && !robo_paint_clip_is_active(&region->clip)) {
        return 0;
    }
    return 1;
}

int robo_paint_invalidation_region_equal(const robo_paint_invalidation_region_t *a,
                                         const robo_paint_invalidation_region_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->rect.x != b->rect.x || a->rect.y != b->rect.y ||
        a->rect.width != b->rect.width || a->rect.height != b->rect.height) {
        return 0;
    }
    return a->node == b->node && a->reason == b->reason &&
           robo_paint_clip_equal(&a->clip, &b->clip) && a->narrowed == b->narrowed &&
           a->full == b->full;
}

/* ------------------------------------------------------------------ */
/* Rectangle arithmetic                                                */
/* ------------------------------------------------------------------ */

/** Non-zero when @p rect can hold a pixel. */
static int paint_invalidation_rect_has_area(robo_layout_rect_t rect)
{
    return rect.width > 0 && rect.height > 0;
}

/** Clamp one 64-bit coordinate into the record's range. */
static int32_t paint_invalidation_clamp(int64_t value)
{
    if (value > (int64_t)INT32_MAX) {
        return INT32_MAX;
    }
    if (value < (int64_t)INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)value;
}

/** The 64-bit lower edge of @p rect. */
static int64_t paint_invalidation_right(const robo_layout_rect_t *rect)
{
    return (int64_t)rect->x + (int64_t)rect->width;
}

/** The 64-bit upper edge of @p rect. */
static int64_t paint_invalidation_bottom(const robo_layout_rect_t *rect)
{
    return (int64_t)rect->y + (int64_t)rect->height;
}

/** The rectangle containing both @p a and @p b, in 64 bits and saturated. */
static robo_layout_rect_t paint_invalidation_rect_union(robo_layout_rect_t a,
                                                      robo_layout_rect_t b)
{
    robo_layout_rect_t out;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    left = (a.x < b.x) ? (int64_t)a.x : (int64_t)b.x;
    top = (a.y < b.y) ? (int64_t)a.y : (int64_t)b.y;
    right = (paint_invalidation_right(&a) > paint_invalidation_right(&b))
                ? paint_invalidation_right(&a)
                : paint_invalidation_right(&b);
    bottom = (paint_invalidation_bottom(&a) > paint_invalidation_bottom(&b))
                 ? paint_invalidation_bottom(&a)
                 : paint_invalidation_bottom(&b);

    out.x = paint_invalidation_clamp(left);
    out.y = paint_invalidation_clamp(top);
    {
        int64_t width = right - left;
        int64_t height = bottom - top;

        if (width < 0) {
            width = 0;
        }
        if (height < 0) {
            height = 0;
        }
        out.width = paint_invalidation_clamp(width);
        out.height = paint_invalidation_clamp(height);
    }
    return out;
}

/** The overlap of @p a and @p b; an empty rectangle when they do not meet. */
static robo_layout_rect_t paint_invalidation_rect_intersect(robo_layout_rect_t a,
                                                           robo_layout_rect_t b)
{
    robo_layout_rect_t out;
    int64_t left = ((int64_t)a.x > (int64_t)b.x) ? (int64_t)a.x : (int64_t)b.x;
    int64_t top = ((int64_t)a.y > (int64_t)b.y) ? (int64_t)a.y : (int64_t)b.y;
    int64_t right = (paint_invalidation_right(&a) < paint_invalidation_right(&b))
                        ? paint_invalidation_right(&a)
                        : paint_invalidation_right(&b);
    int64_t bottom = (paint_invalidation_bottom(&a) < paint_invalidation_bottom(&b))
                         ? paint_invalidation_bottom(&a)
                         : paint_invalidation_bottom(&b);
    int64_t width = right - left;
    int64_t height = bottom - top;

    if (width < 0) {
        width = 0;
    }
    if (height < 0) {
        height = 0;
    }
    out.x = paint_invalidation_clamp(left);
    out.y = paint_invalidation_clamp(top);
    out.width = paint_invalidation_clamp(width);
    out.height = paint_invalidation_clamp(height);
    return out;
}

/* ------------------------------------------------------------------ */
/* The set                                                             */
/* ------------------------------------------------------------------ */

struct robo_paint_invalidation_set {
    robo_paint_invalidation_region_t *regions;
    size_t count;
    size_t capacity;
    robo_paint_invalidation_stats_t stats;
};

void robo_paint_invalidation_stats_init(robo_paint_invalidation_stats_t *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

int robo_paint_invalidation_stats_equal(const robo_paint_invalidation_stats_t *a,
                                        const robo_paint_invalidation_stats_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->added == b->added && a->narrowed == b->narrowed &&
           a->conservative == b->conservative && a->excluded == b->excluded &&
           a->full == b->full && a->unions == b->unions && a->spans == b->spans &&
           a->compared == b->compared && a->matched == b->matched &&
           a->differed == b->differed && a->inserted == b->inserted &&
           a->removed == b->removed;
}

/** Grow @p set so that @p needed regions fit. */
static robo_result_t paint_invalidation_set_reserve(robo_paint_invalidation_set_t *set,
                                                    size_t needed,
                                                    robo_paint_diag_t *diag)
{
    robo_paint_invalidation_region_t *regions;
    size_t capacity;

    if (needed <= set->capacity) {
        return ROBO_OK;
    }
    capacity = set->capacity ? set->capacity : ROBO_PAINT_INVALIDATION_INITIAL_CAPACITY;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2u) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_OUT_OF_MEMORY,
                                   "the invalidation set cannot grow further");
        }
        capacity *= 2u;
    }
    regions = (robo_paint_invalidation_region_t *)robo_realloc(
        set->regions, capacity * sizeof(*regions));
    if (!regions) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_OUT_OF_MEMORY,
                               "could not grow the invalidation set");
    }
    /*
     * The new tail is cleared rather than left as whatever the allocator held, because a
     * region is a record compared field by field by `..._region_equal` and a suite that built
     * one from uninitialised memory would be comparing bytes rather than decisions.
     */
    memset(regions + set->capacity, 0,
           (capacity - set->capacity) * sizeof(*regions));
    set->regions = regions;
    set->capacity = capacity;
    return ROBO_OK;
}

robo_result_t robo_paint_invalidation_set_create(robo_paint_invalidation_set_t **out_set,
                                                robo_paint_diag_t *diag)
{
    robo_paint_invalidation_set_t *set;
    robo_paint_invalidation_region_t *regions;

    if (!out_set) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "out_set is NULL");
    }
    *out_set = NULL;

    set = (robo_paint_invalidation_set_t *)robo_calloc(1u, sizeof(*set));
    if (!set) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_OUT_OF_MEMORY,
                               "could not allocate the invalidation set");
    }
    regions = (robo_paint_invalidation_region_t *)robo_calloc(
        ROBO_PAINT_INVALIDATION_INITIAL_CAPACITY, sizeof(*regions));
    if (!regions) {
        robo_free(set);
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_OUT_OF_MEMORY,
                               "could not allocate the invalidation set's first block");
    }
    set->regions = regions;
    set->count = 0u;
    set->capacity = ROBO_PAINT_INVALIDATION_INITIAL_CAPACITY;
    robo_paint_invalidation_stats_init(&set->stats);
    *out_set = set;
    return ROBO_OK;
}

void robo_paint_invalidation_set_destroy(robo_paint_invalidation_set_t *set)
{
    if (!set) {
        return;
    }
    robo_free(set->regions);
    robo_free(set);
}

void robo_paint_invalidation_set_clear(robo_paint_invalidation_set_t *set)
{
    if (!set) {
        return;
    }
    set->count = 0u;
    robo_paint_invalidation_stats_init(&set->stats);
}

size_t robo_paint_invalidation_set_count(const robo_paint_invalidation_set_t *set)
{
    return set ? set->count : 0u;
}

size_t robo_paint_invalidation_set_capacity(const robo_paint_invalidation_set_t *set)
{
    return set ? set->capacity : 0u;
}

int robo_paint_invalidation_set_is_empty(const robo_paint_invalidation_set_t *set)
{
    return set == NULL || set->count == 0u;
}

robo_result_t robo_paint_invalidation_set_at(const robo_paint_invalidation_set_t *set,
                                            size_t index,
                                            robo_paint_invalidation_region_t *out_region,
                                            robo_paint_diag_t *diag)
{
    if (!set || !out_region) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the set and an out_region are required");
    }
    if (index >= set->count) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_NOT_FOUND,
                               "the index is past the end of the set");
    }
    *out_region = set->regions[index];
    return ROBO_OK;
}

int robo_paint_invalidation_set_equal(const robo_paint_invalidation_set_t *a,
                                      const robo_paint_invalidation_set_t *b)
{
    size_t index;

    if (!a || !b) {
        return a == b;
    }
    if (a->count != b->count) {
        return 0;
    }
    for (index = 0u; index < a->count; index++) {
        if (!robo_paint_invalidation_region_equal(&a->regions[index], &b->regions[index])) {
            return 0;
        }
    }
    return 1;
}

robo_paint_invalidation_scope_t robo_paint_invalidation_set_scope(
    const robo_paint_invalidation_set_t *set)
{
    size_t index;

    if (!set || set->count == 0u) {
        return ROBO_PAINT_INVALIDATION_SCOPE_NONE;
    }
    for (index = 0u; index < set->count; index++) {
        if (set->regions[index].full) {
            return ROBO_PAINT_INVALIDATION_SCOPE_FULL;
        }
    }
    return ROBO_PAINT_INVALIDATION_SCOPE_PARTIAL;
}

robo_result_t robo_paint_invalidation_set_bounds(const robo_paint_invalidation_set_t *set,
                                                robo_layout_rect_t *out_rect)
{
    robo_layout_rect_t bounds;
    size_t index;

    if (!set || !out_rect) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (set->count == 0u) {
        return ROBO_NOT_FOUND;
    }
    bounds = set->regions[0].rect;
    for (index = 1u; index < set->count; index++) {
        bounds = paint_invalidation_rect_union(bounds, set->regions[index].rect);
    }
    *out_rect = bounds;
    return ROBO_OK;
}

void robo_paint_invalidation_set_stats(const robo_paint_invalidation_set_t *set,
                                      robo_paint_invalidation_stats_t *out_stats)
{
    if (!out_stats) {
        return;
    }
    if (!set) {
        robo_paint_invalidation_stats_init(out_stats);
        return;
    }
    *out_stats = set->stats;
}

robo_result_t robo_paint_invalidation_set_add_region(robo_paint_invalidation_set_t *set,
                                                    const robo_paint_invalidation_region_t *region,
                                                    robo_paint_diag_t *diag)
{
    if (!set || !region) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the set and a region are required");
    }
    if (!robo_paint_invalidation_region_is_valid(region)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the region is not a record this build may store");
    }
    /*
     * A region with no area is **produced and dropped**: a box with no size that changed its
     * background changed no pixel, and a region that cannot hold one would make every
     * consumer downstream ask a question with one answer. It is success rather than a refusal
     * because the conclusion is legitimate -- `excluded` is where a reader finds out how often
     * it happened.
     */
    if (!paint_invalidation_rect_has_area(region->rect)) {
        set->stats.excluded++;
        return ROBO_OK;
    }
    if (paint_invalidation_set_reserve(set, set->count + 1u, diag) != ROBO_OK) {
        return ROBO_ERROR;
    }
    set->regions[set->count] = *region;
    set->count++;
    set->stats.added++;
    if (region->narrowed) {
        set->stats.narrowed++;
    } else {
        set->stats.conservative++;
    }
    if (region->full) {
        set->stats.full++;
    }
    return ROBO_OK;
}

/**
 * Build a region from @p rect and add it, narrowing it only when @p boundary names both axes.
 *
 * The one place the "read a boundary, never invent one" rule is written, and every explicit
 * door goes through it. Three outcomes, and the third is the one worth reading:
 *
 *   the boundary is non-NULL, is a record, and bounds both axes -- the region is its
 *   intersection with @p rect and `narrowed` is set. An intersection with no area is
 *   *excluded* rather than added, because a boundary that excludes the whole rectangle means
 *   no pixel of it was ever drawn;
 *
 *   the boundary is non-NULL and leaves an axis free -- the rectangle is kept **whole** and the
 *   boundary is carried as it is, so a consumer that can clip can still do so;
 *
 *   the boundary is non-NULL and is **not a record** this build produced -- the rectangle is
 *   kept whole and the boundary is dropped (`visible`/`visible`), because a region may not
 *   carry a clip that would make `region_is_valid` refuse it and because shrinking by a
 *   boundary nobody can interpret is exactly the mistake the phase forbids.
 */
static robo_result_t paint_invalidation_add(robo_paint_invalidation_set_t *set,
                                           robo_layout_rect_t rect,
                                           robo_layout_node_id_t node,
                                           robo_paint_invalidation_reason_t reason,
                                           const robo_paint_clip_t *boundary,
                                           robo_paint_diag_t *diag)
{
    robo_paint_invalidation_region_t region;

    robo_paint_invalidation_region_init(&region);
    region.rect = rect;
    region.node = node;
    region.reason = reason;

    if (boundary != NULL && robo_paint_clip_is_valid(boundary)) {
        robo_layout_rect_t bounded;

        region.clip = *boundary;
        if (robo_paint_clip_rect(boundary, &bounded) == ROBO_OK) {
            robo_layout_rect_t overlap = paint_invalidation_rect_intersect(rect, bounded);

            if (!paint_invalidation_rect_has_area(overlap)) {
                set->stats.excluded++;
                return ROBO_OK;
            }
            region.rect = overlap;
            region.narrowed = 1;
        }
        /* An axis with no extent: the rectangle stays whole and `narrowed` stays clear. */
    }

    if (robo_paint_invalidation_set_add_region(set, &region, diag) != ROBO_OK) {
        return ROBO_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_paint_invalidation_set_add_rect(robo_paint_invalidation_set_t *set,
                                                  robo_layout_rect_t rect,
                                                  robo_layout_node_id_t node,
                                                  robo_paint_invalidation_reason_t reason,
                                                  const robo_paint_clip_t *boundary,
                                                  robo_paint_diag_t *diag)
{
    if (!set) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the set is required");
    }
    if (reason == ROBO_PAINT_INVALIDATION_REASON_NONE ||
        !robo_paint_invalidation_reason_is_known(reason)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the reason is NONE or undefined");
    }
    if (rect.width < 0 || rect.height < 0) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the rectangle has a negative size");
    }
    return paint_invalidation_add(set, rect, node, reason, boundary, diag);
}

robo_result_t robo_paint_invalidation_set_add_change(robo_paint_invalidation_set_t *set,
                                                    robo_layout_rect_t before,
                                                    robo_layout_rect_t after,
                                                    robo_layout_node_id_t node,
                                                    robo_paint_invalidation_reason_t reason,
                                                    const robo_paint_clip_t *boundary,
                                                    robo_paint_diag_t *diag)
{
    robo_result_t status;
    robo_layout_rect_t united;

    if (before.width < 0 || before.height < 0 || after.width < 0 || after.height < 0) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "a rectangle has a negative size");
    }
    united = paint_invalidation_rect_union(before, after);
    status = robo_paint_invalidation_set_add_rect(set, united, node, reason, boundary, diag);
    if (status != ROBO_OK) {
        return status;
    }
    set->stats.unions++;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Marking one artifact                                                */
/* ------------------------------------------------------------------ */

/** The border box an operation acts on, in paint coordinates. */
static robo_layout_rect_t paint_invalidation_op_rect(const robo_paint_operation_t *operation)
{
    robo_paint_boxes_t boxes;

    memset(&boxes, 0, sizeof(boxes));
    robo_paint_operation_boxes(operation, &boxes);
    return boxes.border;
}

robo_result_t robo_paint_invalidation_set_mark_operation(robo_paint_invalidation_set_t *set,
                                                        const robo_paint_operation_t *operation,
                                                        robo_paint_invalidation_reason_t reason,
                                                        robo_paint_diag_t *diag)
{
    robo_paint_clip_t clip;
    robo_layout_rect_t rect;

    if (!operation || !robo_paint_operation_is_valid(operation)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the operation is NULL or is not a record");
    }
    rect = paint_invalidation_op_rect(operation);
    robo_paint_operation_clip(operation, &clip);
    return robo_paint_invalidation_set_add_rect(set, rect, robo_paint_operation_node(operation),
                                                reason, &clip, diag);
}

robo_result_t robo_paint_invalidation_set_mark_node(robo_paint_invalidation_set_t *set,
                                                   const robo_paint_list_t *list,
                                                   robo_layout_node_id_t node,
                                                   robo_paint_invalidation_reason_t reason,
                                                   robo_paint_diag_t *diag)
{
    robo_layout_rect_t rect;
    robo_paint_clip_t clip;
    size_t index;
    size_t count;
    int found = 0;

    if (!list || node == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the list is required and the id may not be NONE");
    }
    count = robo_paint_list_count(list);
    for (index = 0u; index < count; index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(list, index, &operation) != ROBO_OK) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_ERROR,
                                   "the list changed under the mark");
        }
        if (robo_paint_operation_node(&operation) != node) {
            continue;
        }
        if (!found) {
            rect = paint_invalidation_op_rect(&operation);
            robo_paint_operation_clip(&operation, &clip);
            found = 1;
        } else {
            rect = paint_invalidation_rect_union(rect, paint_invalidation_op_rect(&operation));
        }
    }
    if (!found) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_NOT_FOUND,
                               "no record in the list names that box");
    }
    return robo_paint_invalidation_set_add_rect(set, rect, node, reason, &clip, diag);
}

robo_result_t robo_paint_invalidation_set_mark_subtree(robo_paint_invalidation_set_t *set,
                                                      const robo_paint_list_t *list,
                                                      robo_layout_node_id_t node,
                                                      robo_paint_invalidation_reason_t reason,
                                                      robo_paint_diag_t *diag)
{
    robo_layout_rect_t rect;
    robo_paint_clip_t clip;
    size_t count;
    size_t start = (size_t)-1;
    size_t finish = 0u;
    size_t index;
    int found = 0;

    if (!list || node == ROBO_LAYOUT_NODE_ID_NONE) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the list is required and the id may not be NONE");
    }
    count = robo_paint_list_count(list);
    for (index = 0u; index < count; index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(list, index, &operation) != ROBO_OK) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_ERROR,
                                   "the list changed under the mark");
        }
        if (robo_paint_operation_node(&operation) == node) {
            if (start == (size_t)-1) {
                start = index;
            }
            finish = index;
        }
    }
    if (start == (size_t)-1) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_NOT_FOUND,
                               "no record in the list names that box");
    }
    /*
     * The subtree's records are the run from the box's own first record to its last, which is
     * the shape 11.1's structural convention guarantees: a box's records are contiguous, its
     * descendants' sit between its `CONTENT` boundary and its `END_BOX`, and a run -- one
     * record, naming itself -- has a range of exactly one. Nothing here walks parents or
     * depths, because the flat sequence already carries the answer.
     */
    for (index = start; index <= finish; index++) {
        robo_paint_operation_t operation;
        robo_layout_rect_t boxes;

        if (robo_paint_list_at(list, index, &operation) != ROBO_OK) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_ERROR,
                                   "the list changed under the mark");
        }
        boxes = paint_invalidation_op_rect(&operation);
        if (!found) {
            rect = boxes;
            robo_paint_operation_clip(&operation, &clip);
            found = 1;
        } else {
            rect = paint_invalidation_rect_union(rect, boxes);
        }
    }
    if (robo_paint_invalidation_set_add_rect(set, rect, node, reason, &clip, diag) != ROBO_OK) {
        return ROBO_ERROR;
    }
    set->stats.spans++;
    return ROBO_OK;
}

robo_result_t robo_paint_invalidation_set_mark_node_change(robo_paint_invalidation_set_t *set,
                                                          const robo_paint_list_t *before,
                                                          const robo_paint_list_t *after,
                                                          robo_layout_node_id_t node,
                                                          robo_paint_invalidation_reason_t reason,
                                                          robo_paint_diag_t *diag)
{
    robo_result_t first = ROBO_NOT_FOUND;
    robo_result_t second = ROBO_NOT_FOUND;

    if (!set || (!before && !after)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the set and at least one list are required");
    }
    if (before) {
        first = robo_paint_invalidation_set_mark_node(set, before, node, reason, NULL);
    }
    if (after) {
        second = robo_paint_invalidation_set_mark_node(set, after, node, reason, NULL);
    }
    /*
     * A node that is in one artifact and not the other is the **insertion** and the
     * **removal** case rather than a failure -- which is why each side is asked with the
     * diagnostic suppressed and only the pair is checked. A node in neither is a caller
     * asking about a box this document does not have, and that is the failure.
     */
    if (first != ROBO_OK && second != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_NOT_FOUND,
                               "neither artifact names that box");
    }
    return ROBO_OK;
}

robo_result_t robo_paint_invalidation_set_mark_full(robo_paint_invalidation_set_t *set,
                                                   robo_layout_rect_t extent,
                                                   robo_paint_invalidation_reason_t reason,
                                                   robo_paint_diag_t *diag)
{
    robo_paint_invalidation_region_t region;

    if (!set) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the set is required");
    }
    if (reason == ROBO_PAINT_INVALIDATION_REASON_NONE ||
        !robo_paint_invalidation_reason_is_known(reason)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the reason is NONE or undefined");
    }
    if (extent.width < 0 || extent.height < 0) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the extent has a negative size");
    }
    robo_paint_invalidation_region_init(&region);
    region.rect = extent;
    region.reason = reason;
    region.full = 1;
    return robo_paint_invalidation_set_add_region(set, &region, diag);
}

/* ------------------------------------------------------------------ */
/* Comparing two artifacts                                             */
/* ------------------------------------------------------------------ */

/**
 * Why a position whose two artifacts differ needs repainting.
 *
 * The order is the decision, and it is the same order for both artifact kinds so that a
 * reader meeting one of them has already read the other: a different **node** means the
 * sequence itself moved (`content`); a different **boundary** means what is drawn there
 * changed without the drawing moving (`clip`); a different **strength** means the same
 * (`opacity`); a different **rectangle** means the thing moved (`geometry`, whatever it
 * paints); and only when all four agree does the difference belong to the drawing's own
 * material (`background`, `border`, `text`, `image`), with a structural kind that differs in
 * no other way reported as `style` -- a change this build can see and cannot attribute.
 */
static robo_paint_invalidation_reason_t paint_invalidation_reason_for_difference(
    robo_layout_node_id_t node_before,
    robo_layout_node_id_t node_after,
    int clip_differs,
    int opacity_differs,
    int rect_differs,
    robo_paint_invalidation_reason_t material)
{
    if (node_before != node_after) {
        return ROBO_PAINT_INVALIDATION_REASON_CONTENT;
    }
    if (clip_differs) {
        return ROBO_PAINT_INVALIDATION_REASON_CLIP;
    }
    if (opacity_differs) {
        return ROBO_PAINT_INVALIDATION_REASON_OPACITY;
    }
    if (rect_differs) {
        return ROBO_PAINT_INVALIDATION_REASON_GEOMETRY;
    }
    if (material == ROBO_PAINT_INVALIDATION_REASON_NONE) {
        return ROBO_PAINT_INVALIDATION_REASON_STYLE;
    }
    return material;
}

/** The reason a change to a record of @p kind needs, or NONE for a structural kind. */
static robo_paint_invalidation_reason_t paint_invalidation_reason_of_kind(
    robo_paint_op_kind_t kind)
{
    switch (kind) {
    case ROBO_PAINT_OP_BACKGROUND:
        return ROBO_PAINT_INVALIDATION_REASON_BACKGROUND;
    case ROBO_PAINT_OP_BORDER_TOP:
    case ROBO_PAINT_OP_BORDER_RIGHT:
    case ROBO_PAINT_OP_BORDER_BOTTOM:
    case ROBO_PAINT_OP_BORDER_LEFT:
        return ROBO_PAINT_INVALIDATION_REASON_BORDER;
    case ROBO_PAINT_OP_TEXT:
        return ROBO_PAINT_INVALIDATION_REASON_TEXT;
    case ROBO_PAINT_OP_IMAGE:
        return ROBO_PAINT_INVALIDATION_REASON_IMAGE;
    default:
        return ROBO_PAINT_INVALIDATION_REASON_NONE;
    }
}

/** The reason a change to a display command of @p kind needs, or NONE for a structural kind. */
static robo_paint_invalidation_reason_t paint_invalidation_reason_of_command_kind(
    robo_paint_display_command_kind_t kind)
{
    switch (kind) {
    case ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND:
        return ROBO_PAINT_INVALIDATION_REASON_BACKGROUND;
    case ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER:
        return ROBO_PAINT_INVALIDATION_REASON_BORDER;
    case ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT:
        return ROBO_PAINT_INVALIDATION_REASON_TEXT;
    case ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE:
        return ROBO_PAINT_INVALIDATION_REASON_IMAGE;
    case ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP:
    case ROBO_PAINT_DISPLAY_CMD_POP_CLIP:
        return ROBO_PAINT_INVALIDATION_REASON_CLIP;
    case ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY:
    case ROBO_PAINT_DISPLAY_CMD_POP_OPACITY:
        return ROBO_PAINT_INVALIDATION_REASON_OPACITY;
    default:
        return ROBO_PAINT_INVALIDATION_REASON_NONE;
    }
}

/**
 * Non-zero when @p operation's kind is one that puts pixels on the surface.
 *
 * The rule the walk is built on, and the one thing about the diff worth stating before it is
 * read: **a record that paints nothing cannot leave a stale pixel**, so only drawing records
 * produce regions. `11.1`'s structural records -- the box markers, the content boundary, the
 * clip and opacity scopes -- are still *compared* and still accounted for in the pass counts,
 * because a structural difference really is a difference; it simply has no rectangle to
 * repaint, and inventing one out of the marker's box would put a region in the set for every
 * box that moved even when the box draws nothing at all.
 *
 * A scope is not lost by the rule either, and that is the part that would be easy to get
 * wrong: `PUSH_CLIP` carries no region, but every record **inside** it carries the boundary in
 * its own `clip`, and the same is true of `PUSH_OPACITY` and the strength in its own
 * `opacity`. So a clip that moves and an element that fades both reach the set through the
 * drawings they bound -- which is also where a rasteriser would have to look.
 */
static int paint_invalidation_op_paints(const robo_paint_operation_t *operation)
{
    return robo_paint_op_kind_draws(robo_paint_operation_kind(operation));
}

/** Copy @p rect and @p clip into a region, add it, and report whether anything was added. */
static robo_result_t paint_invalidation_add_side(robo_paint_invalidation_set_t *set,
                                                robo_layout_rect_t rect,
                                                robo_layout_node_id_t node,
                                                robo_paint_invalidation_reason_t reason,
                                                const robo_paint_clip_t *clip,
                                                robo_paint_diag_t *diag)
{
    if (rect.width <= 0 || rect.height <= 0) {
        /* Nothing was ever drawn there, so nothing there can be stale. */
        return ROBO_OK;
    }
    return paint_invalidation_add(set, rect, node, reason, clip, diag);
}

robo_result_t robo_paint_invalidation_set_from_lists(robo_paint_invalidation_set_t *set,
                                                    const robo_paint_list_t *before,
                                                    const robo_paint_list_t *after,
                                                    robo_paint_diag_t *diag)
{
    size_t before_count;
    size_t after_count;
    size_t count;
    size_t index;

    if (!set) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the set is required");
    }
    /* A NULL list is an empty artifact rather than an error: "there was no previous frame" is
     * a real call, and its answer is that everything in the other list is new. */
    before_count = robo_paint_list_count(before);
    after_count = robo_paint_list_count(after);
    count = before_count > after_count ? before_count : after_count;

    for (index = 0u; index < count; index++) {
        robo_paint_operation_t old_op;
        robo_paint_operation_t new_op;
        int has_old = index < before_count;
        int has_new = index < after_count;

        memset(&old_op, 0, sizeof(old_op));
        memset(&new_op, 0, sizeof(new_op));
        if (has_old && robo_paint_list_at(before, index, &old_op) != ROBO_OK) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_ERROR,
                                   "the before list changed under the comparison");
        }
        if (has_new && robo_paint_list_at(after, index, &new_op) != ROBO_OK) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_ERROR,
                                   "the after list changed under the comparison");
        }

        if (has_old && has_new) {
            robo_paint_clip_t old_clip;
            robo_paint_clip_t new_clip;
            robo_paint_opacity_t old_opacity;
            robo_paint_opacity_t new_opacity;
            robo_layout_rect_t old_rect;
            robo_layout_rect_t new_rect;
            robo_layout_node_id_t old_node;
            robo_layout_node_id_t new_node;
            robo_paint_invalidation_reason_t reason;

            set->stats.compared++;
            if (robo_paint_operation_equal(&old_op, &new_op)) {
                set->stats.matched++;
                continue;
            }
            set->stats.differed++;
            if (!paint_invalidation_op_paints(&old_op) &&
                !paint_invalidation_op_paints(&new_op)) {
                continue;
            }

            robo_paint_operation_clip(&old_op, &old_clip);
            robo_paint_operation_clip(&new_op, &new_clip);
            robo_paint_operation_opacity(&old_op, &old_opacity);
            robo_paint_operation_opacity(&new_op, &new_opacity);
            old_rect = paint_invalidation_op_rect(&old_op);
            new_rect = paint_invalidation_op_rect(&new_op);
            old_node = robo_paint_operation_node(&old_op);
            new_node = robo_paint_operation_node(&new_op);

            reason = paint_invalidation_reason_for_difference(
                old_node, new_node,
                !robo_paint_clip_equal(&old_clip, &new_clip),
                !robo_paint_opacity_equal(&old_opacity, &new_opacity),
                old_rect.x != new_rect.x || old_rect.y != new_rect.y ||
                    old_rect.width != new_rect.width || old_rect.height != new_rect.height,
                paint_invalidation_reason_of_kind(robo_paint_operation_kind(&new_op)));

            /*
             * One region when the two rectangles agree (a colour or a strength changed in
             * place) and **two** when they do not, because the old rectangle and the new one
             * were drawn under boundaries that need not be the same and each must be narrowed
             * by its own. Collapsing them into their union here would be the mistake this phase
             * exists to avoid: it would repaint the middle of the move under a boundary that
             * describes only one end of it.
             */
            if (old_rect.x == new_rect.x && old_rect.y == new_rect.y &&
                old_rect.width == new_rect.width && old_rect.height == new_rect.height) {
                if (paint_invalidation_add_side(set, new_rect, new_node, reason, &new_clip,
                                                diag) != ROBO_OK) {
                    return ROBO_ERROR;
                }
            } else {
                if (paint_invalidation_add_side(set, old_rect, old_node, reason, &old_clip,
                                                diag) != ROBO_OK) {
                    return ROBO_ERROR;
                }
                if (paint_invalidation_add_side(set, new_rect, new_node, reason, &new_clip,
                                                diag) != ROBO_OK) {
                    return ROBO_ERROR;
                }
            }
        } else if (has_new) {
            robo_paint_clip_t clip;

            set->stats.inserted++;
            if (!paint_invalidation_op_paints(&new_op)) {
                continue;
            }
            robo_paint_operation_clip(&new_op, &clip);
            if (paint_invalidation_add_side(set, paint_invalidation_op_rect(&new_op),
                                            robo_paint_operation_node(&new_op),
                                            ROBO_PAINT_INVALIDATION_REASON_CONTENT, &clip,
                                            diag) != ROBO_OK) {
                return ROBO_ERROR;
            }
        } else {
            robo_paint_clip_t clip;

            set->stats.removed++;
            if (!paint_invalidation_op_paints(&old_op)) {
                continue;
            }
            robo_paint_operation_clip(&old_op, &clip);
            if (paint_invalidation_add_side(set, paint_invalidation_op_rect(&old_op),
                                            robo_paint_operation_node(&old_op),
                                            ROBO_PAINT_INVALIDATION_REASON_CONTENT, &clip,
                                            diag) != ROBO_OK) {
                return ROBO_ERROR;
            }
        }
    }
    return ROBO_OK;
}

robo_result_t robo_paint_invalidation_set_from_display_lists(
    robo_paint_invalidation_set_t *set,
    const robo_paint_display_list_t *before,
    const robo_paint_display_list_t *after,
    robo_paint_diag_t *diag)
{
    size_t before_count;
    size_t after_count;
    size_t count;
    size_t index;

    if (!set) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the set is required");
    }
    before_count = robo_paint_display_list_count(before);
    after_count = robo_paint_display_list_count(after);
    count = before_count > after_count ? before_count : after_count;

    for (index = 0u; index < count; index++) {
        robo_paint_display_command_t old_command;
        robo_paint_display_command_t new_command;
        int has_old = index < before_count;
        int has_new = index < after_count;

        memset(&old_command, 0, sizeof(old_command));
        memset(&new_command, 0, sizeof(new_command));
        if (has_old && robo_paint_display_list_at(before, index, &old_command) != ROBO_OK) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_ERROR,
                                   "the before list changed under the comparison");
        }
        if (has_new && robo_paint_display_list_at(after, index, &new_command) != ROBO_OK) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_ERROR,
                                   "the after list changed under the comparison");
        }

        if (has_old && has_new) {
            robo_paint_clip_t old_clip;
            robo_paint_clip_t new_clip;
            robo_paint_opacity_t old_opacity;
            robo_paint_opacity_t new_opacity;
            robo_layout_rect_t old_rect;
            robo_layout_rect_t new_rect;
            robo_layout_node_id_t old_node;
            robo_layout_node_id_t new_node;
            robo_paint_invalidation_reason_t reason;

            set->stats.compared++;
            if (robo_paint_display_command_equal(&old_command, &new_command)) {
                set->stats.matched++;
                continue;
            }
            set->stats.differed++;
            if (!robo_paint_display_command_is_drawing(&old_command) &&
                !robo_paint_display_command_is_drawing(&new_command)) {
                continue;
            }

            robo_paint_display_command_clip(&old_command, &old_clip);
            robo_paint_display_command_clip(&new_command, &new_clip);
            robo_paint_display_command_opacity(&old_command, &old_opacity);
            robo_paint_display_command_opacity(&new_command, &new_opacity);
            old_rect = robo_paint_display_command_rect(&old_command);
            new_rect = robo_paint_display_command_rect(&new_command);
            old_node = robo_paint_display_command_node(&old_command);
            new_node = robo_paint_display_command_node(&new_command);

            reason = paint_invalidation_reason_for_difference(
                old_node, new_node,
                !robo_paint_clip_equal(&old_clip, &new_clip),
                !robo_paint_opacity_equal(&old_opacity, &new_opacity),
                old_rect.x != new_rect.x || old_rect.y != new_rect.y ||
                    old_rect.width != new_rect.width || old_rect.height != new_rect.height,
                paint_invalidation_reason_of_command_kind(
                    robo_paint_display_command_kind(&new_command)));

            if (old_rect.x == new_rect.x && old_rect.y == new_rect.y &&
                old_rect.width == new_rect.width && old_rect.height == new_rect.height) {
                if (paint_invalidation_add_side(set, new_rect, new_node, reason, &new_clip,
                                                diag) != ROBO_OK) {
                    return ROBO_ERROR;
                }
            } else {
                if (paint_invalidation_add_side(set, old_rect, old_node, reason, &old_clip,
                                                diag) != ROBO_OK) {
                    return ROBO_ERROR;
                }
                if (paint_invalidation_add_side(set, new_rect, new_node, reason, &new_clip,
                                                diag) != ROBO_OK) {
                    return ROBO_ERROR;
                }
            }
        } else if (has_new) {
            robo_paint_clip_t clip;

            set->stats.inserted++;
            if (!robo_paint_display_command_is_drawing(&new_command)) {
                continue;
            }
            robo_paint_display_command_clip(&new_command, &clip);
            if (paint_invalidation_add_side(
                    set, robo_paint_display_command_rect(&new_command),
                    robo_paint_display_command_node(&new_command),
                    ROBO_PAINT_INVALIDATION_REASON_CONTENT, &clip, diag) != ROBO_OK) {
                return ROBO_ERROR;
            }
        } else {
            robo_paint_clip_t clip;

            set->stats.removed++;
            if (!robo_paint_display_command_is_drawing(&old_command)) {
                continue;
            }
            robo_paint_display_command_clip(&old_command, &clip);
            if (paint_invalidation_add_side(
                    set, robo_paint_display_command_rect(&old_command),
                    robo_paint_display_command_node(&old_command),
                    ROBO_PAINT_INVALIDATION_REASON_CONTENT, &clip, diag) != ROBO_OK) {
                return ROBO_ERROR;
            }
        }
    }
    return ROBO_OK;
}

robo_result_t robo_paint_invalidation_set_from_stats(robo_paint_invalidation_set_t *set,
                                                    const robo_paint_stats_t *stats,
                                                    robo_layout_rect_t extent,
                                                    robo_paint_diag_t *diag)
{
    if (!set || !stats) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "the set and the pass record are required");
    }
    /*
     * `style_only == 0` is the whole point of reading the number: a pass that visited boxes
     * whose staleness is paint-visible and counted **none** of them has produced a picture that
     * is already current, and the answer is an empty set -- the case a caller turns into "skip
     * the frame" rather than "repaint everything to be safe".
     */
    if (stats->style_only == 0u) {
        return ROBO_OK;
    }
    return robo_paint_invalidation_set_mark_full(set, extent, ROBO_PAINT_INVALIDATION_REASON_STYLE,
                                                diag);
}

/* ------------------------------------------------------------------ */
/* The developer's view                                                */
/* ------------------------------------------------------------------ */

/** Write one axis of @p clip the way the record dumps do: the mode, then the interval. */
static void paint_invalidation_writer_clip_axis(robo_paint_writer_t *text,
                                               const robo_paint_clip_axis_t *axis)
{
    robo_paint_writer_string(text, robo_css_overflow_name(axis->mode));
    if (robo_paint_clip_axis_is_active(axis)) {
        robo_paint_writer_char(text, '[');
        robo_paint_writer_i64(text, (int64_t)axis->start);
        robo_paint_writer_char(text, ',');
        robo_paint_writer_i64(text, (int64_t)axis->end);
        robo_paint_writer_char(text, ']');
    }
}

robo_result_t robo_paint_invalidation_set_write(const robo_paint_invalidation_set_t *set,
                                               char *buffer,
                                               size_t capacity,
                                               size_t *out_length,
                                               robo_paint_diag_t *diag)
{
    robo_paint_writer_t text;
    robo_paint_invalidation_stats_t stats;
    size_t index;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_INVALID_ARGUMENT,
                               "a buffer and an out_length are required");
    }

    robo_paint_writer_init(&text, buffer, capacity);
    robo_paint_invalidation_set_stats(set, &stats);
    for (index = 0u; index < robo_paint_invalidation_set_count(set); index++) {
        robo_paint_invalidation_region_t region;

        if (robo_paint_invalidation_set_at(set, index, &region, NULL) != ROBO_OK) {
            *out_length = text.length;
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_ERROR,
                                   "the set changed under the dump");
        }
        robo_paint_writer_u64(&text, (uint64_t)index);
        robo_paint_writer_string(&text, " reason=");
        robo_paint_writer_string(&text, robo_paint_invalidation_reason_name(region.reason));
        robo_paint_writer_string(&text, " rect=");
        robo_paint_writer_rect(&text, region.rect);
        robo_paint_writer_string(&text, " node=");
        robo_paint_writer_u64(&text, (uint64_t)region.node);
        robo_paint_writer_string(&text, " clip=");
        paint_invalidation_writer_clip_axis(&text, &region.clip.horizontal);
        robo_paint_writer_char(&text, '/');
        paint_invalidation_writer_clip_axis(&text, &region.clip.vertical);
        robo_paint_writer_string(&text, " narrowed=");
        robo_paint_writer_string(&text, region.narrowed ? "yes" : "no");
        robo_paint_writer_string(&text, " full=");
        robo_paint_writer_string(&text, region.full ? "yes" : "no");
        robo_paint_writer_char(&text, '\n');
    }

    /* The summary line last, so a reader scrolling the dump has the header at the top and the
     * accounting at the bottom, and so a dump whose lines are cut short still ends with the
     * numbers that say what it was describing. */
    robo_paint_writer_string(&text, "scope=");
    robo_paint_writer_string(&text,
                             robo_paint_invalidation_scope_name(robo_paint_invalidation_set_scope(set)));
    robo_paint_writer_string(&text, " regions=");
    robo_paint_writer_u64(&text, (uint64_t)stats.added);
    robo_paint_writer_string(&text, " narrowed=");
    robo_paint_writer_u64(&text, (uint64_t)stats.narrowed);
    robo_paint_writer_string(&text, " conservative=");
    robo_paint_writer_u64(&text, (uint64_t)stats.conservative);
    robo_paint_writer_string(&text, " excluded=");
    robo_paint_writer_u64(&text, (uint64_t)stats.excluded);
    robo_paint_writer_string(&text, " full=");
    robo_paint_writer_u64(&text, (uint64_t)stats.full);
    robo_paint_writer_char(&text, '\n');

    *out_length = text.length;
    if (!robo_paint_writer_fits(&text)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_INVALIDATION, ROBO_NOT_SUPPORTED,
                               "the dump does not fit the buffer");
    }
    return ROBO_OK;
}
