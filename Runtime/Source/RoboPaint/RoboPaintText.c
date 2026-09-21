/*
 * RoboPaint text vocabulary (Phase 11.6) — see RoboPaintText.h.
 *
 * Two records and one container. The records are values and their doors are the ones
 * every model record in this tree has (init, validate, equal, read); the container is the
 * module's **second growable array**, and it is written the way the first one is
 * (`RoboPaintList.c`) because the properties a caller relies on are the same ones:
 *
 *   the array is contiguous and owned, so a span is two numbers rather than a pointer and
 *   nothing can dangle;
 *   `clear` moves the count and keeps the capacity, so a per-frame renderer reuses the
 *   storage and never the characters;
 *   growth doubles, and the block beyond the count is zeroed, so a consumer that walks
 *   `capacity` slots sees cleared code points rather than the allocator's leftovers;
 *   and **every span is validated against this pool** before it is read, because a span
 *   is two numbers that can name anything and only the pool knows what it holds.
 *
 * The one arithmetic worth stating is the span check, which is written as a subtraction
 * rather than an addition on purpose: `first + count` is the sum of two bounded numbers
 * and their sum is not bounded, so a span near the top of `uint32_t` would wrap and read
 * as *inside* a small pool. `count > total - first` cannot wrap, because `first <= total`
 * is established first.
 */

#include "RoboPaint/RoboPaintText.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/** Code points a pool holds before its first growth. */
#define RoboPaintTextInitialCapacity 16u

/** The pool: a contiguous array and its two counts. */
struct PaintTextPool {
    uint32_t *codePoints;
    /** Code points held. */
    size_t count;
    /** Code points the array has room for. */
    size_t capacity;
};

/* ------------------------------------------------------------------ */
/* The span                                                            */
/* ------------------------------------------------------------------ */

PaintTextSpan PaintTextSpanAt(uint32_t first)
{
    PaintTextSpan span;

    span.first = first;
    span.count = 0u;
    return span;
}

int PaintTextSpanIsEmpty(PaintTextSpan span)
{
    return span.count == 0u;
}

/* ------------------------------------------------------------------ */
/* The run                                                             */
/* ------------------------------------------------------------------ */

void PaintTextRunInit(PaintTextRun *run)
{
    if (!run) {
        return;
    }
    run->span = PaintTextSpanAt(0u);
    run->baselineOffset = 0;
    LayoutTextStyleInit(&run->font);
}

int PaintTextRunIsValid(const PaintTextRun *run)
{
    if (!run) {
        return 0;
    }
    /* A drawing with no characters is not a drawing: the rule the colour rule states one
     * field along, and the reason a run is refused here rather than skipped later. */
    if (PaintTextSpanIsEmpty(run->span)) {
        return 0;
    }
    /* A baseline above the run's own top is not a placement 10.7 can produce. */
    if (run->baselineOffset < 0) {
        return 0;
    }
    /*
     * The font inputs, held to the same three conditions `RoboLayoutText.h`'s own record
     * states -- a finite positive size and one of the two directions -- rather than to a
     * range on the weight, which CSS allows to be any number and which this layer has no
     * basis to bound (`RoboFont`'s selection is a later phase's).
     */
    /*
     * A **finite, positive** size, and neither test needs a maths library. `!(x > 0.0)`
     * is false for a NaN -- the shape `RoboLayoutSizing.c` uses deliberately, because a
     * NaN compares false against everything and so fails a test rather than slipping
     * through one -- and the upper bound is the geometry's own int32 range, so a size
     * this build could not place is refused here rather than saturating one layer down.
     * The size was already bounded by the layer that resolved it
     * (`LayoutTextStyleResolve`); this only says so.
     */
    if (!(run->font.fontSizePx > 0.0) || run->font.fontSizePx > (double)INT32_MAX) {
        return 0;
    }
    if (run->font.direction != RoboLayoutTextDirectionLtr &&
        run->font.direction != RoboLayoutTextDirectionRtl) {
        return 0;
    }
    return 1;
}

int PaintTextRunEqual(const PaintTextRun *a,
                              const PaintTextRun *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->span.first != b->span.first || a->span.count != b->span.count) {
        return 0;
    }
    if (a->baselineOffset != b->baselineOffset) {
        return 0;
    }
    /* The four font inputs, compared field by field rather than as bytes: two records
     * built independently must compare equal, and padding between members is not a fact
     * about either of them. The size is a `double`, so it is compared as one. */
    if (a->font.fontSizePx != b->font.fontSizePx) {
        return 0;
    }
    if (a->font.fontWeight != b->font.fontWeight) {
        return 0;
    }
    if ((a->font.italic != 0) != (b->font.italic != 0)) {
        return 0;
    }
    if (a->font.direction != b->font.direction) {
        return 0;
    }
    return 1;
}

PaintTextSpan PaintTextRunSpan(const PaintTextRun *run)
{
    if (!run) {
        return PaintTextSpanAt(0u);
    }
    return run->span;
}

int32_t PaintTextRunBaselineOffset(const PaintTextRun *run)
{
    return run ? run->baselineOffset : 0;
}

LayoutTextStyle PaintTextRunFont(const PaintTextRun *run)
{
    LayoutTextStyle font;

    if (!run) {
        font.fontSizePx = RoboLayoutTextInitialFontSizePx;
        font.fontWeight = RoboLayoutTextWeightNormal;
        font.italic = 0;
        font.direction = RoboLayoutTextDirectionLtr;
        return font;
    }
    return run->font;
}

/* ------------------------------------------------------------------ */
/* The pool                                                            */
/* ------------------------------------------------------------------ */

Result PaintTextPoolCreate(PaintTextPool **outPool,
                                          PaintDiag *diag)
{
    PaintTextPool *pool;
    uint32_t *codePoints;

    if (!outPool) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "out_pool is NULL");
    }
    *outPool = NULL;

    pool = (PaintTextPool *)RoboCalloc(1u, sizeof(*pool));
    if (!pool) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the text pool");
    }
    codePoints = (uint32_t *)RoboCalloc(RoboPaintTextInitialCapacity,
                                          sizeof(*codePoints));
    if (!codePoints) {
        RoboFree(pool);
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the text pool's first block");
    }
    pool->codePoints = codePoints;
    pool->count = 0u;
    pool->capacity = RoboPaintTextInitialCapacity;
    *outPool = pool;
    return RoboOk;
}

void PaintTextPoolDestroy(PaintTextPool *pool)
{
    if (!pool) {
        return;
    }
    /* Code points are values, so freeing the array is the whole of the release: there is
     * nothing to walk, and a record that carries a span into it carries two numbers that
     * mean nothing once the pool is gone -- which is why the doors that read a span are
     * handed the pool. */
    RoboFree(pool->codePoints);
    RoboFree(pool);
}

void PaintTextPoolClear(PaintTextPool *pool)
{
    if (!pool) {
        return;
    }
    /* Only the count moves, exactly as the operation list's does: the storage is reused,
     * the old code points are unreachable through any door (every read clamps to the
     * count), and a `memset` would be work whose only effect is invisible. */
    pool->count = 0u;
}

/**
 * Make room for @p capacity code points, keeping the ones already there.
 *
 * Private, because growth is this type's business and not a caller's: a pool that let a
 * caller reserve would be a pool whose spans could be invalidated by a caller's choice
 * rather than by an append.
 */
static Result TextPoolReserve(PaintTextPool *pool,
                                       size_t capacity,
                                       PaintDiag *diag)
{
    uint32_t *grown;

    if (capacity <= pool->capacity) {
        return RoboOk;
    }
    grown = (uint32_t *)Realloc(pool->codePoints, capacity * sizeof(*grown));
    if (!grown) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not grow the text pool");
    }
    /* Zeroed from the old end onward, so a consumer that walks `capacity` code points
     * sees cleared values past the count rather than the allocator's leftovers. */
    memset(grown + pool->capacity, 0, (capacity - pool->capacity) * sizeof(*grown));
    pool->codePoints = grown;
    pool->capacity = capacity;
    return RoboOk;
}

Result PaintTextPoolAppend(PaintTextPool *pool,
                                         const uint32_t *codePoints,
                                         size_t count,
                                         PaintTextSpan *outSpan,
                                         PaintDiag *diag)
{
    Result result;

    if (outSpan) {
        *outSpan = PaintTextSpanAt(0u);
    }
    if (!pool || !outSpan) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the pool and the output span are required");
    }
    if (count == 0u) {
        /* An empty span is a position rather than a run, and no record may carry one
         * (RoboPaintText.h) -- so the door that would hand one out refuses, which is what
         * keeps "every span in a record is non-empty" true by construction. */
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "an appended run must cover at least one code point");
    }
    if (!codePoints) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "code_points is NULL");
    }
    /* The span's two halves are 32-bit indices, so a pool that could not name a run is
     * refused rather than truncated: past four billion code points the span itself would
     * be a lie, and that is a document no surface could hold. */
    if (pool->count > (size_t)0xFFFFFFFFu ||
        count > (size_t)0xFFFFFFFFu - pool->count) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the text pool cannot index a run this long");
    }
    if (pool->count + count > pool->capacity) {
        size_t wanted = pool->capacity;
        size_t needed = pool->count + count;

        while (wanted < needed) {
            /* Doubled, and floored at the need so that a pool created with no room still
             * grows rather than spinning. */
            wanted = wanted < 1u ? needed : wanted * 2u;
        }
        result = TextPoolReserve(pool, wanted, diag);
        if (result != RoboOk) {
            return result;
        }
    }
    memcpy(pool->codePoints + pool->count, codePoints, count * sizeof(*codePoints));
    outSpan->first = (uint32_t)pool->count;
    outSpan->count = (uint32_t)count;
    pool->count += count;
    return RoboOk;
}

Result PaintTextPoolCopy(PaintTextPool *pool,
                                       const PaintTextPool *source,
                                       PaintTextSpan span,
                                       PaintTextSpan *outSpan,
                                       PaintDiag *diag)
{
    if (outSpan) {
        *outSpan = PaintTextSpanAt(0u);
    }
    if (!pool || !source || !outSpan) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the destination pool, the source pool and the output span "
                               "are required");
    }
    if (!PaintTextPoolContains(source, span) || span.count == 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the span does not name a run in the source pool");
    }
    /* The characters move straight from one owned array to the other; `append` does the
     * growth, the span renumbering and the empty-run refusal. */
    return PaintTextPoolAppend(pool, source->codePoints + span.first,
                                       (size_t)span.count, outSpan, diag);
}

size_t PaintTextPoolCount(const PaintTextPool *pool)
{
    return pool ? pool->count : 0u;
}

size_t PaintTextPoolCapacity(const PaintTextPool *pool)
{
    return pool ? pool->capacity : 0u;
}

const uint32_t *PaintTextPoolCodePoints(const PaintTextPool *pool)
{
    if (!pool || pool->count == 0u) {
        return NULL;
    }
    return pool->codePoints;
}

int PaintTextPoolContains(const PaintTextPool *pool,
                                  PaintTextSpan span)
{
    if (!pool) {
        return 0;
    }
    if (span.count == 0u) {
        /* An empty span names a *position*, and a position inside the pool -- including
         * the one past its end -- is a place a caller may legitimately name (it is what a
         * caret is). It is not a run, and no record carries one. */
        return (size_t)span.first <= pool->count;
    }
    if ((size_t)span.first >= pool->count) {
        return 0;
    }
    /*
     * Subtraction rather than addition. `first + count` can wrap and would then report a
     * span *inside* a small pool; `count > total - first` cannot, because `first < total`
     * is established above.
     */
    return (size_t)span.count <= pool->count - (size_t)span.first;
}

Result PaintTextPoolRead(const PaintTextPool *pool,
                                       PaintTextSpan span,
                                       uint32_t *out,
                                       size_t outCapacity,
                                       size_t *outCount,
                                       PaintDiag *diag)
{
    if (outCount) {
        *outCount = 0u;
    }
    if (!pool || !out || !outCount) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the pool, the destination and the count are required");
    }
    if (!PaintTextPoolContains(pool, span) || span.count == 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the span does not name a run in this pool");
    }
    if (outCapacity < (size_t)span.count) {
        /* The `_write` contract's sibling: the caller is told the room it would need
         * rather than handed a truncated run, because half a run is not a shorter run. */
        *outCount = (size_t)span.count;
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboNotSupported,
                               "the destination cannot hold the run");
    }
    memcpy(out, pool->codePoints + span.first, (size_t)span.count * sizeof(*out));
    *outCount = (size_t)span.count;
    return RoboOk;
}
