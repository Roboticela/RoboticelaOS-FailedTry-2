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
#define ROBO_PAINT_TEXT_INITIAL_CAPACITY 16u

/** The pool: a contiguous array and its two counts. */
struct robo_paint_text_pool {
    uint32_t *code_points;
    /** Code points held. */
    size_t count;
    /** Code points the array has room for. */
    size_t capacity;
};

/* ------------------------------------------------------------------ */
/* The span                                                            */
/* ------------------------------------------------------------------ */

robo_paint_text_span_t robo_paint_text_span_at(uint32_t first)
{
    robo_paint_text_span_t span;

    span.first = first;
    span.count = 0u;
    return span;
}

int robo_paint_text_span_is_empty(robo_paint_text_span_t span)
{
    return span.count == 0u;
}

/* ------------------------------------------------------------------ */
/* The run                                                             */
/* ------------------------------------------------------------------ */

void robo_paint_text_run_init(robo_paint_text_run_t *run)
{
    if (!run) {
        return;
    }
    run->span = robo_paint_text_span_at(0u);
    run->baseline_offset = 0;
    robo_layout_text_style_init(&run->font);
}

int robo_paint_text_run_is_valid(const robo_paint_text_run_t *run)
{
    if (!run) {
        return 0;
    }
    /* A drawing with no characters is not a drawing: the rule the colour rule states one
     * field along, and the reason a run is refused here rather than skipped later. */
    if (robo_paint_text_span_is_empty(run->span)) {
        return 0;
    }
    /* A baseline above the run's own top is not a placement 10.7 can produce. */
    if (run->baseline_offset < 0) {
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
     * (`robo_layout_text_style_resolve`); this only says so.
     */
    if (!(run->font.font_size_px > 0.0) || run->font.font_size_px > (double)INT32_MAX) {
        return 0;
    }
    if (run->font.direction != ROBO_LAYOUT_TEXT_DIRECTION_LTR &&
        run->font.direction != ROBO_LAYOUT_TEXT_DIRECTION_RTL) {
        return 0;
    }
    return 1;
}

int robo_paint_text_run_equal(const robo_paint_text_run_t *a,
                              const robo_paint_text_run_t *b)
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
    if (a->baseline_offset != b->baseline_offset) {
        return 0;
    }
    /* The four font inputs, compared field by field rather than as bytes: two records
     * built independently must compare equal, and padding between members is not a fact
     * about either of them. The size is a `double`, so it is compared as one. */
    if (a->font.font_size_px != b->font.font_size_px) {
        return 0;
    }
    if (a->font.font_weight != b->font.font_weight) {
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

robo_paint_text_span_t robo_paint_text_run_span(const robo_paint_text_run_t *run)
{
    if (!run) {
        return robo_paint_text_span_at(0u);
    }
    return run->span;
}

int32_t robo_paint_text_run_baseline_offset(const robo_paint_text_run_t *run)
{
    return run ? run->baseline_offset : 0;
}

robo_layout_text_style_t robo_paint_text_run_font(const robo_paint_text_run_t *run)
{
    robo_layout_text_style_t font;

    if (!run) {
        font.font_size_px = ROBO_LAYOUT_TEXT_INITIAL_FONT_SIZE_PX;
        font.font_weight = ROBO_LAYOUT_TEXT_WEIGHT_NORMAL;
        font.italic = 0;
        font.direction = ROBO_LAYOUT_TEXT_DIRECTION_LTR;
        return font;
    }
    return run->font;
}

/* ------------------------------------------------------------------ */
/* The pool                                                            */
/* ------------------------------------------------------------------ */

robo_result_t robo_paint_text_pool_create(robo_paint_text_pool_t **out_pool,
                                          robo_paint_diag_t *diag)
{
    robo_paint_text_pool_t *pool;
    uint32_t *code_points;

    if (!out_pool) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "out_pool is NULL");
    }
    *out_pool = NULL;

    pool = (robo_paint_text_pool_t *)robo_calloc(1u, sizeof(*pool));
    if (!pool) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the text pool");
    }
    code_points = (uint32_t *)robo_calloc(ROBO_PAINT_TEXT_INITIAL_CAPACITY,
                                          sizeof(*code_points));
    if (!code_points) {
        robo_free(pool);
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the text pool's first block");
    }
    pool->code_points = code_points;
    pool->count = 0u;
    pool->capacity = ROBO_PAINT_TEXT_INITIAL_CAPACITY;
    *out_pool = pool;
    return ROBO_OK;
}

void robo_paint_text_pool_destroy(robo_paint_text_pool_t *pool)
{
    if (!pool) {
        return;
    }
    /* Code points are values, so freeing the array is the whole of the release: there is
     * nothing to walk, and a record that carries a span into it carries two numbers that
     * mean nothing once the pool is gone -- which is why the doors that read a span are
     * handed the pool. */
    robo_free(pool->code_points);
    robo_free(pool);
}

void robo_paint_text_pool_clear(robo_paint_text_pool_t *pool)
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
static robo_result_t text_pool_reserve(robo_paint_text_pool_t *pool,
                                       size_t capacity,
                                       robo_paint_diag_t *diag)
{
    uint32_t *grown;

    if (capacity <= pool->capacity) {
        return ROBO_OK;
    }
    grown = (uint32_t *)robo_realloc(pool->code_points, capacity * sizeof(*grown));
    if (!grown) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_OUT_OF_MEMORY,
                               "could not grow the text pool");
    }
    /* Zeroed from the old end onward, so a consumer that walks `capacity` code points
     * sees cleared values past the count rather than the allocator's leftovers. */
    memset(grown + pool->capacity, 0, (capacity - pool->capacity) * sizeof(*grown));
    pool->code_points = grown;
    pool->capacity = capacity;
    return ROBO_OK;
}

robo_result_t robo_paint_text_pool_append(robo_paint_text_pool_t *pool,
                                         const uint32_t *code_points,
                                         size_t count,
                                         robo_paint_text_span_t *out_span,
                                         robo_paint_diag_t *diag)
{
    robo_result_t result;

    if (out_span) {
        *out_span = robo_paint_text_span_at(0u);
    }
    if (!pool || !out_span) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "the pool and the output span are required");
    }
    if (count == 0u) {
        /* An empty span is a position rather than a run, and no record may carry one
         * (RoboPaintText.h) -- so the door that would hand one out refuses, which is what
         * keeps "every span in a record is non-empty" true by construction. */
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "an appended run must cover at least one code point");
    }
    if (!code_points) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "code_points is NULL");
    }
    /* The span's two halves are 32-bit indices, so a pool that could not name a run is
     * refused rather than truncated: past four billion code points the span itself would
     * be a lie, and that is a document no surface could hold. */
    if (pool->count > (size_t)0xFFFFFFFFu ||
        count > (size_t)0xFFFFFFFFu - pool->count) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
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
        result = text_pool_reserve(pool, wanted, diag);
        if (result != ROBO_OK) {
            return result;
        }
    }
    memcpy(pool->code_points + pool->count, code_points, count * sizeof(*code_points));
    out_span->first = (uint32_t)pool->count;
    out_span->count = (uint32_t)count;
    pool->count += count;
    return ROBO_OK;
}

robo_result_t robo_paint_text_pool_copy(robo_paint_text_pool_t *pool,
                                       const robo_paint_text_pool_t *source,
                                       robo_paint_text_span_t span,
                                       robo_paint_text_span_t *out_span,
                                       robo_paint_diag_t *diag)
{
    if (out_span) {
        *out_span = robo_paint_text_span_at(0u);
    }
    if (!pool || !source || !out_span) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "the destination pool, the source pool and the output span "
                               "are required");
    }
    if (!robo_paint_text_pool_contains(source, span) || span.count == 0u) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "the span does not name a run in the source pool");
    }
    /* The characters move straight from one owned array to the other; `append` does the
     * growth, the span renumbering and the empty-run refusal. */
    return robo_paint_text_pool_append(pool, source->code_points + span.first,
                                       (size_t)span.count, out_span, diag);
}

size_t robo_paint_text_pool_count(const robo_paint_text_pool_t *pool)
{
    return pool ? pool->count : 0u;
}

size_t robo_paint_text_pool_capacity(const robo_paint_text_pool_t *pool)
{
    return pool ? pool->capacity : 0u;
}

const uint32_t *robo_paint_text_pool_code_points(const robo_paint_text_pool_t *pool)
{
    if (!pool || pool->count == 0u) {
        return NULL;
    }
    return pool->code_points;
}

int robo_paint_text_pool_contains(const robo_paint_text_pool_t *pool,
                                  robo_paint_text_span_t span)
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

robo_result_t robo_paint_text_pool_read(const robo_paint_text_pool_t *pool,
                                       robo_paint_text_span_t span,
                                       uint32_t *out,
                                       size_t out_capacity,
                                       size_t *out_count,
                                       robo_paint_diag_t *diag)
{
    if (out_count) {
        *out_count = 0u;
    }
    if (!pool || !out || !out_count) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "the pool, the destination and the count are required");
    }
    if (!robo_paint_text_pool_contains(pool, span) || span.count == 0u) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "the span does not name a run in this pool");
    }
    if (out_capacity < (size_t)span.count) {
        /* The `_write` contract's sibling: the caller is told the room it would need
         * rather than handed a truncated run, because half a run is not a shorter run. */
        *out_count = (size_t)span.count;
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_NOT_SUPPORTED,
                               "the destination cannot hold the run");
    }
    memcpy(out, pool->code_points + span.first, (size_t)span.count * sizeof(*out));
    *out_count = (size_t)span.count;
    return ROBO_OK;
}
