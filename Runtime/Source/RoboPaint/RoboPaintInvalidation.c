/*
 * RoboPaint invalidation (Phase 11.11) — see RoboPaintInvalidation.h.
 *
 * Three tables, one growable array and the arithmetic that fills it, and the arithmetic is
 * where every decision lives. Three of them are worth stating before the code, because they
 * are the ones a later reader would otherwise "simplify":
 *
 *   **A region is widened, never narrowed, when this module is not certain.** The one
 *   narrowing this file performs is against a boundary it can read *both axes* of
 *   (`PaintClipRect`), because that intersection can only remove pixels the boundary
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
typedef struct paintInvalidationReasonRow {
    PaintInvalidationReason reason;
    const char *name;
} PaintInvalidationReasonRow;

/*
 * Row per enumerator, as every table in this tree is written: a name added without a row, or
 * a row without a name, is a compile-time omission rather than a value that prints "unknown"
 * in a bug report. The names are the ones a dump prints, so they are spelled the way a person
 * would say the fact (`opacity`, `clip`, `geometry`).
 */
static const PaintInvalidationReasonRow paintInvalidationReasonTable[] = {
    { RoboPaintInvalidationReasonNone, "none" },
    { RoboPaintInvalidationReasonStyle, "style" },
    { RoboPaintInvalidationReasonBackground, "background" },
    { RoboPaintInvalidationReasonBorder, "border" },
    { RoboPaintInvalidationReasonText, "text" },
    { RoboPaintInvalidationReasonImage, "image" },
    { RoboPaintInvalidationReasonGeometry, "geometry" },
    { RoboPaintInvalidationReasonOpacity, "opacity" },
    { RoboPaintInvalidationReasonClip, "clip" },
    { RoboPaintInvalidationReasonContent, "content" },
    { RoboPaintInvalidationReasonViewport, "viewport" }
};

/** One class row. */
typedef struct paintInvalidationClassRow {
    PaintInvalidationClass klass;
    const char *name;
} PaintInvalidationClassRow;

static const PaintInvalidationClassRow paintInvalidationClassTable[] = {
    { RoboPaintInvalidationClassNone, "none" },
    { RoboPaintInvalidationClassPaint, "paint" },
    { RoboPaintInvalidationClassLayout, "layout" }
};

/** One scope row. */
typedef struct paintInvalidationScopeRow {
    PaintInvalidationScope scope;
    const char *name;
} PaintInvalidationScopeRow;

static const PaintInvalidationScopeRow paintInvalidationScopeTable[] = {
    { RoboPaintInvalidationScopeNone, "none" },
    { RoboPaintInvalidationScopePartial, "partial" },
    { RoboPaintInvalidationScopeFull, "full" }
};

int PaintInvalidationReasonIsKnown(PaintInvalidationReason reason)
{
    return reason >= RoboPaintInvalidationReasonNone &&
           reason < (PaintInvalidationReason)RoboPaintInvalidationReasonCount;
}

const char *PaintInvalidationReasonName(PaintInvalidationReason reason)
{
    size_t index;

    for (index = 0u;
         index < sizeof(paintInvalidationReasonTable) / sizeof(paintInvalidationReasonTable[0]);
         index++) {
        if (paintInvalidationReasonTable[index].reason == reason) {
            return paintInvalidationReasonTable[index].name;
        }
    }
    return "unknown";
}

int PaintInvalidationClassIsKnown(PaintInvalidationClass klass)
{
    return klass >= RoboPaintInvalidationClassNone &&
           klass < (PaintInvalidationClass)RoboPaintInvalidationClassCount;
}

const char *PaintInvalidationClassName(PaintInvalidationClass klass)
{
    size_t index;

    for (index = 0u;
         index < sizeof(paintInvalidationClassTable) / sizeof(paintInvalidationClassTable[0]);
         index++) {
        if (paintInvalidationClassTable[index].klass == klass) {
            return paintInvalidationClassTable[index].name;
        }
    }
    return "unknown";
}

int PaintInvalidationScopeIsKnown(PaintInvalidationScope scope)
{
    return scope >= RoboPaintInvalidationScopeNone &&
           scope < (PaintInvalidationScope)RoboPaintInvalidationScopeCount;
}

const char *PaintInvalidationScopeName(PaintInvalidationScope scope)
{
    size_t index;

    for (index = 0u;
         index < sizeof(paintInvalidationScopeTable) / sizeof(paintInvalidationScopeTable[0]);
         index++) {
        if (paintInvalidationScopeTable[index].scope == scope) {
            return paintInvalidationScopeTable[index].name;
        }
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* The classifier                                                      */
/* ------------------------------------------------------------------ */

/** Lower-case ASCII comparison of @p a and @p b, both NUL-terminated. */
static int PaintInvalidationNameEquals(const char *a, const char *b)
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
typedef struct paintInvalidationDrawnRow {
    const char *name;
    PaintInvalidationReason reason;
} PaintInvalidationDrawnRow;

/*
 * **The properties this module draws from**, written out by hand, and the list is short on
 * purpose: it names what a *painting pass* reads, which is a different question from the one
 * `LayoutStyleEffectForProperty` answers ("does layout measure it?") and a much
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
static const PaintInvalidationDrawnRow paintInvalidationDrawnTable[] = {
    { "color", RoboPaintInvalidationReasonText },
    { "border-color", RoboPaintInvalidationReasonBorder },
    { "border-top-color", RoboPaintInvalidationReasonBorder },
    { "border-right-color", RoboPaintInvalidationReasonBorder },
    { "border-bottom-color", RoboPaintInvalidationReasonBorder },
    { "border-left-color", RoboPaintInvalidationReasonBorder },
    { "opacity", RoboPaintInvalidationReasonOpacity },
    { "background", RoboPaintInvalidationReasonBackground },
    { "background-color", RoboPaintInvalidationReasonBackground }
};

/** The reason @p propertyName's change needs, or NONE when this module draws nothing from it. */
static PaintInvalidationReason PaintInvalidationReasonOfDrawn(const char *name)
{
    size_t index;

    if (!name) {
        return RoboPaintInvalidationReasonNone;
    }
    for (index = 0u;
         index < sizeof(paintInvalidationDrawnTable) / sizeof(paintInvalidationDrawnTable[0]);
         index++) {
        if (PaintInvalidationNameEquals(name, paintInvalidationDrawnTable[index].name)) {
            return paintInvalidationDrawnTable[index].reason;
        }
    }
    return RoboPaintInvalidationReasonNone;
}

PaintInvalidationClass PaintInvalidationClassifyProperty(
    const char *propertyName)
{
    /*
     * One question is asked of RoboLayout and one of this table, and the order is what makes
     * the answer right: a property layout *measures* is a geometry change whatever it paints
     * (a `width` that repaints must first be re-laid-out), and only what layout does not
     * measure is asked of the drawn list.
     */
    if (LayoutStyleEffectForProperty(propertyName) ==
        RoboLayoutStyleEffectLayout) {
        return RoboPaintInvalidationClassLayout;
    }
    if (PaintInvalidationReasonOfDrawn(propertyName) !=
        RoboPaintInvalidationReasonNone) {
        return RoboPaintInvalidationClassPaint;
    }
    /*
     * Everything else: a property layout does not measure and this module does not draw from.
     * `NONE` is the sound answer rather than the lazy one -- the table above names every
     * property any drawing in this build reads, so a name outside it cannot have changed a
     * pixel -- and it is the answer that keeps a `cursor` change or a `visibility` change from
     * asking for a repaint of a picture that did not move.
     */
    return RoboPaintInvalidationClassNone;
}

PaintInvalidationReason PaintInvalidationReasonForProperty(
    const char *propertyName)
{
    PaintInvalidationReason drawn = PaintInvalidationReasonOfDrawn(propertyName);

    if (drawn != RoboPaintInvalidationReasonNone) {
        return drawn;
    }
    /*
     * A property layout measures repaints as a **coordinate** rather than as a material: the
     * declaration that moved the box is `width`, and the fact a repaint has to cover is that
     * the box is somewhere else. The reason therefore names the geometry, and the caller that
     * wants the declaration reads its own cascade.
     */
    if (LayoutStyleEffectForProperty(propertyName) ==
        RoboLayoutStyleEffectLayout) {
        return RoboPaintInvalidationReasonGeometry;
    }
    return RoboPaintInvalidationReasonNone;
}

/* ------------------------------------------------------------------ */
/* Regions                                                             */
/* ------------------------------------------------------------------ */

void PaintInvalidationRegionInit(PaintInvalidationRegion *region)
{
    if (!region) {
        return;
    }
    memset(region, 0, sizeof(*region));
    region->node = RoboLayoutNodeIdNone;
    region->reason = RoboPaintInvalidationReasonNone;
    PaintClipInit(&region->clip);
}

int PaintInvalidationRegionIsValid(const PaintInvalidationRegion *region)
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
    if (region->reason == RoboPaintInvalidationReasonNone ||
        !PaintInvalidationReasonIsKnown(region->reason)) {
        return 0;
    }
    if (!PaintClipIsValid(&region->clip)) {
        return 0;
    }
    if (region->narrowed && !PaintClipIsActive(&region->clip)) {
        return 0;
    }
    return 1;
}

int PaintInvalidationRegionEqual(const PaintInvalidationRegion *a,
                                         const PaintInvalidationRegion *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->rect.x != b->rect.x || a->rect.y != b->rect.y ||
        a->rect.width != b->rect.width || a->rect.height != b->rect.height) {
        return 0;
    }
    return a->node == b->node && a->reason == b->reason &&
           PaintClipEqual(&a->clip, &b->clip) && a->narrowed == b->narrowed &&
           a->full == b->full;
}

/* ------------------------------------------------------------------ */
/* Rectangle arithmetic                                                */
/* ------------------------------------------------------------------ */

/** Non-zero when @p rect can hold a pixel. */
static int PaintInvalidationRectHasArea(LayoutRect rect)
{
    return rect.width > 0 && rect.height > 0;
}

/** Clamp one 64-bit coordinate into the record's range. */
static int32_t PaintInvalidationClamp(int64_t value)
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
static int64_t PaintInvalidationRight(const LayoutRect *rect)
{
    return (int64_t)rect->x + (int64_t)rect->width;
}

/** The 64-bit upper edge of @p rect. */
static int64_t PaintInvalidationBottom(const LayoutRect *rect)
{
    return (int64_t)rect->y + (int64_t)rect->height;
}

/** The rectangle containing both @p a and @p b, in 64 bits and saturated. */
static LayoutRect PaintInvalidationRectUnion(LayoutRect a,
                                                      LayoutRect b)
{
    LayoutRect out;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    left = (a.x < b.x) ? (int64_t)a.x : (int64_t)b.x;
    top = (a.y < b.y) ? (int64_t)a.y : (int64_t)b.y;
    right = (PaintInvalidationRight(&a) > PaintInvalidationRight(&b))
                ? PaintInvalidationRight(&a)
                : PaintInvalidationRight(&b);
    bottom = (PaintInvalidationBottom(&a) > PaintInvalidationBottom(&b))
                 ? PaintInvalidationBottom(&a)
                 : PaintInvalidationBottom(&b);

    out.x = PaintInvalidationClamp(left);
    out.y = PaintInvalidationClamp(top);
    {
        int64_t width = right - left;
        int64_t height = bottom - top;

        if (width < 0) {
            width = 0;
        }
        if (height < 0) {
            height = 0;
        }
        out.width = PaintInvalidationClamp(width);
        out.height = PaintInvalidationClamp(height);
    }
    return out;
}

/** The overlap of @p a and @p b; an empty rectangle when they do not meet. */
static LayoutRect PaintInvalidationRectIntersect(LayoutRect a,
                                                           LayoutRect b)
{
    LayoutRect out;
    int64_t left = ((int64_t)a.x > (int64_t)b.x) ? (int64_t)a.x : (int64_t)b.x;
    int64_t top = ((int64_t)a.y > (int64_t)b.y) ? (int64_t)a.y : (int64_t)b.y;
    int64_t right = (PaintInvalidationRight(&a) < PaintInvalidationRight(&b))
                        ? PaintInvalidationRight(&a)
                        : PaintInvalidationRight(&b);
    int64_t bottom = (PaintInvalidationBottom(&a) < PaintInvalidationBottom(&b))
                         ? PaintInvalidationBottom(&a)
                         : PaintInvalidationBottom(&b);
    int64_t width = right - left;
    int64_t height = bottom - top;

    if (width < 0) {
        width = 0;
    }
    if (height < 0) {
        height = 0;
    }
    out.x = PaintInvalidationClamp(left);
    out.y = PaintInvalidationClamp(top);
    out.width = PaintInvalidationClamp(width);
    out.height = PaintInvalidationClamp(height);
    return out;
}

/* ------------------------------------------------------------------ */
/* The set                                                             */
/* ------------------------------------------------------------------ */

struct PaintInvalidationSet {
    PaintInvalidationRegion *regions;
    size_t count;
    size_t capacity;
    PaintInvalidationStats stats;
};

void PaintInvalidationStatsInit(PaintInvalidationStats *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

int PaintInvalidationStatsEqual(const PaintInvalidationStats *a,
                                        const PaintInvalidationStats *b)
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
static Result PaintInvalidationSetReserve(PaintInvalidationSet *set,
                                                    size_t needed,
                                                    PaintDiag *diag)
{
    PaintInvalidationRegion *regions;
    size_t capacity;

    if (needed <= set->capacity) {
        return RoboOk;
    }
    capacity = set->capacity ? set->capacity : RoboPaintInvalidationInitialCapacity;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2u) {
            return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboOutOfMemory,
                                   "the invalidation set cannot grow further");
        }
        capacity *= 2u;
    }
    regions = (PaintInvalidationRegion *)Realloc(
        set->regions, capacity * sizeof(*regions));
    if (!regions) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboOutOfMemory,
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
    return RoboOk;
}

Result PaintInvalidationSetCreate(PaintInvalidationSet **outSet,
                                                PaintDiag *diag)
{
    PaintInvalidationSet *set;
    PaintInvalidationRegion *regions;

    if (!outSet) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "out_set is NULL");
    }
    *outSet = NULL;

    set = (PaintInvalidationSet *)RoboCalloc(1u, sizeof(*set));
    if (!set) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboOutOfMemory,
                               "could not allocate the invalidation set");
    }
    regions = (PaintInvalidationRegion *)RoboCalloc(
        RoboPaintInvalidationInitialCapacity, sizeof(*regions));
    if (!regions) {
        RoboFree(set);
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboOutOfMemory,
                               "could not allocate the invalidation set's first block");
    }
    set->regions = regions;
    set->count = 0u;
    set->capacity = RoboPaintInvalidationInitialCapacity;
    PaintInvalidationStatsInit(&set->stats);
    *outSet = set;
    return RoboOk;
}

void PaintInvalidationSetDestroy(PaintInvalidationSet *set)
{
    if (!set) {
        return;
    }
    RoboFree(set->regions);
    RoboFree(set);
}

void PaintInvalidationSetClear(PaintInvalidationSet *set)
{
    if (!set) {
        return;
    }
    set->count = 0u;
    PaintInvalidationStatsInit(&set->stats);
}

size_t PaintInvalidationSetCount(const PaintInvalidationSet *set)
{
    return set ? set->count : 0u;
}

size_t PaintInvalidationSetCapacity(const PaintInvalidationSet *set)
{
    return set ? set->capacity : 0u;
}

int PaintInvalidationSetIsEmpty(const PaintInvalidationSet *set)
{
    return set == NULL || set->count == 0u;
}

Result PaintInvalidationSetAt(const PaintInvalidationSet *set,
                                            size_t index,
                                            PaintInvalidationRegion *outRegion,
                                            PaintDiag *diag)
{
    if (!set || !outRegion) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the set and an out_region are required");
    }
    if (index >= set->count) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboNotFound,
                               "the index is past the end of the set");
    }
    *outRegion = set->regions[index];
    return RoboOk;
}

int PaintInvalidationSetEqual(const PaintInvalidationSet *a,
                                      const PaintInvalidationSet *b)
{
    size_t index;

    if (!a || !b) {
        return a == b;
    }
    if (a->count != b->count) {
        return 0;
    }
    for (index = 0u; index < a->count; index++) {
        if (!PaintInvalidationRegionEqual(&a->regions[index], &b->regions[index])) {
            return 0;
        }
    }
    return 1;
}

PaintInvalidationScope PaintInvalidationSetScope(
    const PaintInvalidationSet *set)
{
    size_t index;

    if (!set || set->count == 0u) {
        return RoboPaintInvalidationScopeNone;
    }
    for (index = 0u; index < set->count; index++) {
        if (set->regions[index].full) {
            return RoboPaintInvalidationScopeFull;
        }
    }
    return RoboPaintInvalidationScopePartial;
}

Result PaintInvalidationSetBounds(const PaintInvalidationSet *set,
                                                LayoutRect *outRect)
{
    LayoutRect bounds;
    size_t index;

    if (!set || !outRect) {
        return RoboInvalidArgument;
    }
    if (set->count == 0u) {
        return RoboNotFound;
    }
    bounds = set->regions[0].rect;
    for (index = 1u; index < set->count; index++) {
        bounds = PaintInvalidationRectUnion(bounds, set->regions[index].rect);
    }
    *outRect = bounds;
    return RoboOk;
}

void PaintInvalidationSetStats(const PaintInvalidationSet *set,
                                      PaintInvalidationStats *outStats)
{
    if (!outStats) {
        return;
    }
    if (!set) {
        PaintInvalidationStatsInit(outStats);
        return;
    }
    *outStats = set->stats;
}

Result PaintInvalidationSetAddRegion(PaintInvalidationSet *set,
                                                    const PaintInvalidationRegion *region,
                                                    PaintDiag *diag)
{
    if (!set || !region) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the set and a region are required");
    }
    if (!PaintInvalidationRegionIsValid(region)) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the region is not a record this build may store");
    }
    /*
     * A region with no area is **produced and dropped**: a box with no size that changed its
     * background changed no pixel, and a region that cannot hold one would make every
     * consumer downstream ask a question with one answer. It is success rather than a refusal
     * because the conclusion is legitimate -- `excluded` is where a reader finds out how often
     * it happened.
     */
    if (!PaintInvalidationRectHasArea(region->rect)) {
        set->stats.excluded++;
        return RoboOk;
    }
    if (PaintInvalidationSetReserve(set, set->count + 1u, diag) != RoboOk) {
        return RoboError;
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
    return RoboOk;
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
static Result PaintInvalidationAdd(PaintInvalidationSet *set,
                                           LayoutRect rect,
                                           LayoutNodeId node,
                                           PaintInvalidationReason reason,
                                           const PaintClip *boundary,
                                           PaintDiag *diag)
{
    PaintInvalidationRegion region;

    PaintInvalidationRegionInit(&region);
    region.rect = rect;
    region.node = node;
    region.reason = reason;

    if (boundary != NULL && PaintClipIsValid(boundary)) {
        LayoutRect bounded;

        region.clip = *boundary;
        if (PaintClipRect(boundary, &bounded) == RoboOk) {
            LayoutRect overlap = PaintInvalidationRectIntersect(rect, bounded);

            if (!PaintInvalidationRectHasArea(overlap)) {
                set->stats.excluded++;
                return RoboOk;
            }
            region.rect = overlap;
            region.narrowed = 1;
        }
        /* An axis with no extent: the rectangle stays whole and `narrowed` stays clear. */
    }

    if (PaintInvalidationSetAddRegion(set, &region, diag) != RoboOk) {
        return RoboError;
    }
    return RoboOk;
}

Result PaintInvalidationSetAddRect(PaintInvalidationSet *set,
                                                  LayoutRect rect,
                                                  LayoutNodeId node,
                                                  PaintInvalidationReason reason,
                                                  const PaintClip *boundary,
                                                  PaintDiag *diag)
{
    if (!set) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the set is required");
    }
    if (reason == RoboPaintInvalidationReasonNone ||
        !PaintInvalidationReasonIsKnown(reason)) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the reason is NONE or undefined");
    }
    if (rect.width < 0 || rect.height < 0) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the rectangle has a negative size");
    }
    return PaintInvalidationAdd(set, rect, node, reason, boundary, diag);
}

Result PaintInvalidationSetAddChange(PaintInvalidationSet *set,
                                                    LayoutRect before,
                                                    LayoutRect after,
                                                    LayoutNodeId node,
                                                    PaintInvalidationReason reason,
                                                    const PaintClip *boundary,
                                                    PaintDiag *diag)
{
    Result status;
    LayoutRect united;

    if (before.width < 0 || before.height < 0 || after.width < 0 || after.height < 0) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "a rectangle has a negative size");
    }
    united = PaintInvalidationRectUnion(before, after);
    status = PaintInvalidationSetAddRect(set, united, node, reason, boundary, diag);
    if (status != RoboOk) {
        return status;
    }
    set->stats.unions++;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Marking one artifact                                                */
/* ------------------------------------------------------------------ */

/** The border box an operation acts on, in paint coordinates. */
static LayoutRect PaintInvalidationOpRect(const PaintOperation *operation)
{
    PaintBoxes boxes;

    memset(&boxes, 0, sizeof(boxes));
    PaintOperationBoxes(operation, &boxes);
    return boxes.border;
}

Result PaintInvalidationSetMarkOperation(PaintInvalidationSet *set,
                                                        const PaintOperation *operation,
                                                        PaintInvalidationReason reason,
                                                        PaintDiag *diag)
{
    PaintClip clip;
    LayoutRect rect;

    if (!operation || !PaintOperationIsValid(operation)) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the operation is NULL or is not a record");
    }
    rect = PaintInvalidationOpRect(operation);
    PaintOperationClip(operation, &clip);
    return PaintInvalidationSetAddRect(set, rect, PaintOperationNode(operation),
                                                reason, &clip, diag);
}

Result PaintInvalidationSetMarkNode(PaintInvalidationSet *set,
                                                   const PaintList *list,
                                                   LayoutNodeId node,
                                                   PaintInvalidationReason reason,
                                                   PaintDiag *diag)
{
    LayoutRect rect;
    PaintClip clip;
    size_t index;
    size_t count;
    int found = 0;

    if (!list || node == RoboLayoutNodeIdNone) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the list is required and the id may not be NONE");
    }
    count = PaintListCount(list);
    for (index = 0u; index < count; index++) {
        PaintOperation operation;

        if (PaintListAt(list, index, &operation) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboError,
                                   "the list changed under the mark");
        }
        if (PaintOperationNode(&operation) != node) {
            continue;
        }
        if (!found) {
            rect = PaintInvalidationOpRect(&operation);
            PaintOperationClip(&operation, &clip);
            found = 1;
        } else {
            rect = PaintInvalidationRectUnion(rect, PaintInvalidationOpRect(&operation));
        }
    }
    if (!found) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboNotFound,
                               "no record in the list names that box");
    }
    return PaintInvalidationSetAddRect(set, rect, node, reason, &clip, diag);
}

Result PaintInvalidationSetMarkSubtree(PaintInvalidationSet *set,
                                                      const PaintList *list,
                                                      LayoutNodeId node,
                                                      PaintInvalidationReason reason,
                                                      PaintDiag *diag)
{
    LayoutRect rect;
    PaintClip clip;
    size_t count;
    size_t start = (size_t)-1;
    size_t finish = 0u;
    size_t index;
    int found = 0;

    if (!list || node == RoboLayoutNodeIdNone) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the list is required and the id may not be NONE");
    }
    count = PaintListCount(list);
    for (index = 0u; index < count; index++) {
        PaintOperation operation;

        if (PaintListAt(list, index, &operation) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboError,
                                   "the list changed under the mark");
        }
        if (PaintOperationNode(&operation) == node) {
            if (start == (size_t)-1) {
                start = index;
            }
            finish = index;
        }
    }
    if (start == (size_t)-1) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboNotFound,
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
        PaintOperation operation;
        LayoutRect boxes;

        if (PaintListAt(list, index, &operation) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboError,
                                   "the list changed under the mark");
        }
        boxes = PaintInvalidationOpRect(&operation);
        if (!found) {
            rect = boxes;
            PaintOperationClip(&operation, &clip);
            found = 1;
        } else {
            rect = PaintInvalidationRectUnion(rect, boxes);
        }
    }
    if (PaintInvalidationSetAddRect(set, rect, node, reason, &clip, diag) != RoboOk) {
        return RoboError;
    }
    set->stats.spans++;
    return RoboOk;
}

Result PaintInvalidationSetMarkNodeChange(PaintInvalidationSet *set,
                                                          const PaintList *before,
                                                          const PaintList *after,
                                                          LayoutNodeId node,
                                                          PaintInvalidationReason reason,
                                                          PaintDiag *diag)
{
    Result first = RoboNotFound;
    Result second = RoboNotFound;

    if (!set || (!before && !after)) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the set and at least one list are required");
    }
    if (before) {
        first = PaintInvalidationSetMarkNode(set, before, node, reason, NULL);
    }
    if (after) {
        second = PaintInvalidationSetMarkNode(set, after, node, reason, NULL);
    }
    /*
     * A node that is in one artifact and not the other is the **insertion** and the
     * **removal** case rather than a failure -- which is why each side is asked with the
     * diagnostic suppressed and only the pair is checked. A node in neither is a caller
     * asking about a box this document does not have, and that is the failure.
     */
    if (first != RoboOk && second != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboNotFound,
                               "neither artifact names that box");
    }
    return RoboOk;
}

Result PaintInvalidationSetMarkFull(PaintInvalidationSet *set,
                                                   LayoutRect extent,
                                                   PaintInvalidationReason reason,
                                                   PaintDiag *diag)
{
    PaintInvalidationRegion region;

    if (!set) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the set is required");
    }
    if (reason == RoboPaintInvalidationReasonNone ||
        !PaintInvalidationReasonIsKnown(reason)) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the reason is NONE or undefined");
    }
    if (extent.width < 0 || extent.height < 0) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the extent has a negative size");
    }
    PaintInvalidationRegionInit(&region);
    region.rect = extent;
    region.reason = reason;
    region.full = 1;
    return PaintInvalidationSetAddRegion(set, &region, diag);
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
static PaintInvalidationReason PaintInvalidationReasonForDifference(
    LayoutNodeId nodeBefore,
    LayoutNodeId nodeAfter,
    int clipDiffers,
    int opacityDiffers,
    int rectDiffers,
    PaintInvalidationReason material)
{
    if (nodeBefore != nodeAfter) {
        return RoboPaintInvalidationReasonContent;
    }
    if (clipDiffers) {
        return RoboPaintInvalidationReasonClip;
    }
    if (opacityDiffers) {
        return RoboPaintInvalidationReasonOpacity;
    }
    if (rectDiffers) {
        return RoboPaintInvalidationReasonGeometry;
    }
    if (material == RoboPaintInvalidationReasonNone) {
        return RoboPaintInvalidationReasonStyle;
    }
    return material;
}

/** The reason a change to a record of @p kind needs, or NONE for a structural kind. */
static PaintInvalidationReason PaintInvalidationReasonOfKind(
    PaintOpKind kind)
{
    switch (kind) {
    case RoboPaintOpBackground:
        return RoboPaintInvalidationReasonBackground;
    case RoboPaintOpBorderTop:
    case RoboPaintOpBorderRight:
    case RoboPaintOpBorderBottom:
    case RoboPaintOpBorderLeft:
        return RoboPaintInvalidationReasonBorder;
    case RoboPaintOpText:
        return RoboPaintInvalidationReasonText;
    case RoboPaintOpImage:
        return RoboPaintInvalidationReasonImage;
    default:
        return RoboPaintInvalidationReasonNone;
    }
}

/** The reason a change to a display command of @p kind needs, or NONE for a structural kind. */
static PaintInvalidationReason PaintInvalidationReasonOfCommandKind(
    PaintDisplayCommandKind kind)
{
    switch (kind) {
    case RoboPaintDisplayCmdDrawBackground:
        return RoboPaintInvalidationReasonBackground;
    case RoboPaintDisplayCmdDrawBorder:
        return RoboPaintInvalidationReasonBorder;
    case RoboPaintDisplayCmdDrawText:
        return RoboPaintInvalidationReasonText;
    case RoboPaintDisplayCmdDrawImage:
        return RoboPaintInvalidationReasonImage;
    case RoboPaintDisplayCmdPushClip:
    case RoboPaintDisplayCmdPopClip:
        return RoboPaintInvalidationReasonClip;
    case RoboPaintDisplayCmdPushOpacity:
    case RoboPaintDisplayCmdPopOpacity:
        return RoboPaintInvalidationReasonOpacity;
    default:
        return RoboPaintInvalidationReasonNone;
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
static int PaintInvalidationOpPaints(const PaintOperation *operation)
{
    return PaintOpKindDraws(PaintOperationKind(operation));
}

/** Copy @p rect and @p clip into a region, add it, and report whether anything was added. */
static Result PaintInvalidationAddSide(PaintInvalidationSet *set,
                                                LayoutRect rect,
                                                LayoutNodeId node,
                                                PaintInvalidationReason reason,
                                                const PaintClip *clip,
                                                PaintDiag *diag)
{
    if (rect.width <= 0 || rect.height <= 0) {
        /* Nothing was ever drawn there, so nothing there can be stale. */
        return RoboOk;
    }
    return PaintInvalidationAdd(set, rect, node, reason, clip, diag);
}

Result PaintInvalidationSetFromLists(PaintInvalidationSet *set,
                                                    const PaintList *before,
                                                    const PaintList *after,
                                                    PaintDiag *diag)
{
    size_t beforeCount;
    size_t afterCount;
    size_t count;
    size_t index;

    if (!set) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the set is required");
    }
    /* A NULL list is an empty artifact rather than an error: "there was no previous frame" is
     * a real call, and its answer is that everything in the other list is new. */
    beforeCount = PaintListCount(before);
    afterCount = PaintListCount(after);
    count = beforeCount > afterCount ? beforeCount : afterCount;

    for (index = 0u; index < count; index++) {
        PaintOperation oldOp;
        PaintOperation newOp;
        int hasOld = index < beforeCount;
        int hasNew = index < afterCount;

        memset(&oldOp, 0, sizeof(oldOp));
        memset(&newOp, 0, sizeof(newOp));
        if (hasOld && PaintListAt(before, index, &oldOp) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboError,
                                   "the before list changed under the comparison");
        }
        if (hasNew && PaintListAt(after, index, &newOp) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboError,
                                   "the after list changed under the comparison");
        }

        if (hasOld && hasNew) {
            PaintClip oldClip;
            PaintClip newClip;
            PaintOpacity oldOpacity;
            PaintOpacity newOpacity;
            LayoutRect oldRect;
            LayoutRect newRect;
            LayoutNodeId oldNode;
            LayoutNodeId newNode;
            PaintInvalidationReason reason;

            set->stats.compared++;
            if (PaintOperationEqual(&oldOp, &newOp)) {
                set->stats.matched++;
                continue;
            }
            set->stats.differed++;
            if (!PaintInvalidationOpPaints(&oldOp) &&
                !PaintInvalidationOpPaints(&newOp)) {
                continue;
            }

            PaintOperationClip(&oldOp, &oldClip);
            PaintOperationClip(&newOp, &newClip);
            PaintOperationOpacity(&oldOp, &oldOpacity);
            PaintOperationOpacity(&newOp, &newOpacity);
            oldRect = PaintInvalidationOpRect(&oldOp);
            newRect = PaintInvalidationOpRect(&newOp);
            oldNode = PaintOperationNode(&oldOp);
            newNode = PaintOperationNode(&newOp);

            reason = PaintInvalidationReasonForDifference(
                oldNode, newNode,
                !PaintClipEqual(&oldClip, &newClip),
                !PaintOpacityEqual(&oldOpacity, &newOpacity),
                oldRect.x != newRect.x || oldRect.y != newRect.y ||
                    oldRect.width != newRect.width || oldRect.height != newRect.height,
                PaintInvalidationReasonOfKind(PaintOperationKind(&newOp)));

            /*
             * One region when the two rectangles agree (a colour or a strength changed in
             * place) and **two** when they do not, because the old rectangle and the new one
             * were drawn under boundaries that need not be the same and each must be narrowed
             * by its own. Collapsing them into their union here would be the mistake this phase
             * exists to avoid: it would repaint the middle of the move under a boundary that
             * describes only one end of it.
             */
            if (oldRect.x == newRect.x && oldRect.y == newRect.y &&
                oldRect.width == newRect.width && oldRect.height == newRect.height) {
                if (PaintInvalidationAddSide(set, newRect, newNode, reason, &newClip,
                                                diag) != RoboOk) {
                    return RoboError;
                }
            } else {
                if (PaintInvalidationAddSide(set, oldRect, oldNode, reason, &oldClip,
                                                diag) != RoboOk) {
                    return RoboError;
                }
                if (PaintInvalidationAddSide(set, newRect, newNode, reason, &newClip,
                                                diag) != RoboOk) {
                    return RoboError;
                }
            }
        } else if (hasNew) {
            PaintClip clip;

            set->stats.inserted++;
            if (!PaintInvalidationOpPaints(&newOp)) {
                continue;
            }
            PaintOperationClip(&newOp, &clip);
            if (PaintInvalidationAddSide(set, PaintInvalidationOpRect(&newOp),
                                            PaintOperationNode(&newOp),
                                            RoboPaintInvalidationReasonContent, &clip,
                                            diag) != RoboOk) {
                return RoboError;
            }
        } else {
            PaintClip clip;

            set->stats.removed++;
            if (!PaintInvalidationOpPaints(&oldOp)) {
                continue;
            }
            PaintOperationClip(&oldOp, &clip);
            if (PaintInvalidationAddSide(set, PaintInvalidationOpRect(&oldOp),
                                            PaintOperationNode(&oldOp),
                                            RoboPaintInvalidationReasonContent, &clip,
                                            diag) != RoboOk) {
                return RoboError;
            }
        }
    }
    return RoboOk;
}

Result PaintInvalidationSetFromDisplayLists(
    PaintInvalidationSet *set,
    const PaintDisplayList *before,
    const PaintDisplayList *after,
    PaintDiag *diag)
{
    size_t beforeCount;
    size_t afterCount;
    size_t count;
    size_t index;

    if (!set) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the set is required");
    }
    beforeCount = PaintDisplayListCount(before);
    afterCount = PaintDisplayListCount(after);
    count = beforeCount > afterCount ? beforeCount : afterCount;

    for (index = 0u; index < count; index++) {
        PaintDisplayCommand oldCommand;
        PaintDisplayCommand newCommand;
        int hasOld = index < beforeCount;
        int hasNew = index < afterCount;

        memset(&oldCommand, 0, sizeof(oldCommand));
        memset(&newCommand, 0, sizeof(newCommand));
        if (hasOld && PaintDisplayListAt(before, index, &oldCommand) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboError,
                                   "the before list changed under the comparison");
        }
        if (hasNew && PaintDisplayListAt(after, index, &newCommand) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboError,
                                   "the after list changed under the comparison");
        }

        if (hasOld && hasNew) {
            PaintClip oldClip;
            PaintClip newClip;
            PaintOpacity oldOpacity;
            PaintOpacity newOpacity;
            LayoutRect oldRect;
            LayoutRect newRect;
            LayoutNodeId oldNode;
            LayoutNodeId newNode;
            PaintInvalidationReason reason;

            set->stats.compared++;
            if (PaintDisplayCommandEqual(&oldCommand, &newCommand)) {
                set->stats.matched++;
                continue;
            }
            set->stats.differed++;
            if (!PaintDisplayCommandIsDrawing(&oldCommand) &&
                !PaintDisplayCommandIsDrawing(&newCommand)) {
                continue;
            }

            PaintDisplayCommandClip(&oldCommand, &oldClip);
            PaintDisplayCommandClip(&newCommand, &newClip);
            PaintDisplayCommandOpacity(&oldCommand, &oldOpacity);
            PaintDisplayCommandOpacity(&newCommand, &newOpacity);
            oldRect = PaintDisplayCommandRect(&oldCommand);
            newRect = PaintDisplayCommandRect(&newCommand);
            oldNode = PaintDisplayCommandNode(&oldCommand);
            newNode = PaintDisplayCommandNode(&newCommand);

            reason = PaintInvalidationReasonForDifference(
                oldNode, newNode,
                !PaintClipEqual(&oldClip, &newClip),
                !PaintOpacityEqual(&oldOpacity, &newOpacity),
                oldRect.x != newRect.x || oldRect.y != newRect.y ||
                    oldRect.width != newRect.width || oldRect.height != newRect.height,
                PaintInvalidationReasonOfCommandKind(
                    MakePaintDisplayCommandKind(&newCommand)));

            if (oldRect.x == newRect.x && oldRect.y == newRect.y &&
                oldRect.width == newRect.width && oldRect.height == newRect.height) {
                if (PaintInvalidationAddSide(set, newRect, newNode, reason, &newClip,
                                                diag) != RoboOk) {
                    return RoboError;
                }
            } else {
                if (PaintInvalidationAddSide(set, oldRect, oldNode, reason, &oldClip,
                                                diag) != RoboOk) {
                    return RoboError;
                }
                if (PaintInvalidationAddSide(set, newRect, newNode, reason, &newClip,
                                                diag) != RoboOk) {
                    return RoboError;
                }
            }
        } else if (hasNew) {
            PaintClip clip;

            set->stats.inserted++;
            if (!PaintDisplayCommandIsDrawing(&newCommand)) {
                continue;
            }
            PaintDisplayCommandClip(&newCommand, &clip);
            if (PaintInvalidationAddSide(
                    set, PaintDisplayCommandRect(&newCommand),
                    PaintDisplayCommandNode(&newCommand),
                    RoboPaintInvalidationReasonContent, &clip, diag) != RoboOk) {
                return RoboError;
            }
        } else {
            PaintClip clip;

            set->stats.removed++;
            if (!PaintDisplayCommandIsDrawing(&oldCommand)) {
                continue;
            }
            PaintDisplayCommandClip(&oldCommand, &clip);
            if (PaintInvalidationAddSide(
                    set, PaintDisplayCommandRect(&oldCommand),
                    PaintDisplayCommandNode(&oldCommand),
                    RoboPaintInvalidationReasonContent, &clip, diag) != RoboOk) {
                return RoboError;
            }
        }
    }
    return RoboOk;
}

Result PaintInvalidationSetFromStats(PaintInvalidationSet *set,
                                                    const PaintStats *stats,
                                                    LayoutRect extent,
                                                    PaintDiag *diag)
{
    if (!set || !stats) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "the set and the pass record are required");
    }
    /*
     * `styleOnly == 0` is the whole point of reading the number: a pass that visited boxes
     * whose staleness is paint-visible and counted **none** of them has produced a picture that
     * is already current, and the answer is an empty set -- the case a caller turns into "skip
     * the frame" rather than "repaint everything to be safe".
     */
    if (stats->styleOnly == 0u) {
        return RoboOk;
    }
    return PaintInvalidationSetMarkFull(set, extent, RoboPaintInvalidationReasonStyle,
                                                diag);
}

/* ------------------------------------------------------------------ */
/* The developer's view                                                */
/* ------------------------------------------------------------------ */

/** Write one axis of @p clip the way the record dumps do: the mode, then the interval. */
static void PaintInvalidationWriterClipAxis(PaintWriter *text,
                                               const PaintClipAxis *axis)
{
    PaintWriterString(text, CssOverflowName(axis->mode));
    if (PaintClipAxisIsActive(axis)) {
        PaintWriterChar(text, '[');
        PaintWriterI64(text, (int64_t)axis->start);
        PaintWriterChar(text, ',');
        PaintWriterI64(text, (int64_t)axis->end);
        PaintWriterChar(text, ']');
    }
}

Result PaintInvalidationSetWrite(const PaintInvalidationSet *set,
                                               char *buffer,
                                               size_t capacity,
                                               size_t *outLength,
                                               PaintDiag *diag)
{
    PaintWriter text;
    PaintInvalidationStats stats;
    size_t index;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboInvalidArgument,
                               "a buffer and an out_length are required");
    }

    PaintWriterInit(&text, buffer, capacity);
    PaintInvalidationSetStats(set, &stats);
    for (index = 0u; index < PaintInvalidationSetCount(set); index++) {
        PaintInvalidationRegion region;

        if (PaintInvalidationSetAt(set, index, &region, NULL) != RoboOk) {
            *outLength = text.length;
            return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboError,
                                   "the set changed under the dump");
        }
        PaintWriterU64(&text, (uint64_t)index);
        PaintWriterString(&text, " reason=");
        PaintWriterString(&text, PaintInvalidationReasonName(region.reason));
        PaintWriterString(&text, " rect=");
        PaintWriterRect(&text, region.rect);
        PaintWriterString(&text, " node=");
        PaintWriterU64(&text, (uint64_t)region.node);
        PaintWriterString(&text, " clip=");
        PaintInvalidationWriterClipAxis(&text, &region.clip.horizontal);
        PaintWriterChar(&text, '/');
        PaintInvalidationWriterClipAxis(&text, &region.clip.vertical);
        PaintWriterString(&text, " narrowed=");
        PaintWriterString(&text, region.narrowed ? "yes" : "no");
        PaintWriterString(&text, " full=");
        PaintWriterString(&text, region.full ? "yes" : "no");
        PaintWriterChar(&text, '\n');
    }

    /* The summary line last, so a reader scrolling the dump has the header at the top and the
     * accounting at the bottom, and so a dump whose lines are cut short still ends with the
     * numbers that say what it was describing. */
    PaintWriterString(&text, "scope=");
    PaintWriterString(&text,
                             PaintInvalidationScopeName(PaintInvalidationSetScope(set)));
    PaintWriterString(&text, " regions=");
    PaintWriterU64(&text, (uint64_t)stats.added);
    PaintWriterString(&text, " narrowed=");
    PaintWriterU64(&text, (uint64_t)stats.narrowed);
    PaintWriterString(&text, " conservative=");
    PaintWriterU64(&text, (uint64_t)stats.conservative);
    PaintWriterString(&text, " excluded=");
    PaintWriterU64(&text, (uint64_t)stats.excluded);
    PaintWriterString(&text, " full=");
    PaintWriterU64(&text, (uint64_t)stats.full);
    PaintWriterChar(&text, '\n');

    *outLength = text.length;
    if (!PaintWriterFits(&text)) {
        return RoboPaintDiag(diag, RoboPaintDomainInvalidation, RoboNotSupported,
                               "the dump does not fit the buffer");
    }
    return RoboOk;
}
