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
    /* The cleared run is the "no face" state: no glyphs, and the token that says so. */
    run->glyphs = PaintGlyphSpanAt(0u);
    run->fontToken = PaintFontTokenNone;
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
    /*
     * The glyphs and the face they came from travel **together** (12.16): either the run was
     * recorded without a face -- an empty span and the `None` token, which is the state `noFont`
     * names -- or it carries glyphs and a token this build can name. Glyphs with no face are ids a
     * caller would have to guess a font for; a face with no glyphs is a claim about a run that has
     * nothing to draw. Both are refused here rather than sorted out one layer down.
     */
    if (PaintGlyphSpanIsEmpty(run->glyphs)) {
        return run->fontToken == PaintFontTokenNone;
    }
    return run->fontToken != PaintFontTokenNone;
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
    /* The shaped half: the same glyphs in the same pool places, from the same face. Two runs
     * that agree about their characters and their style and disagree about their glyphs are
     * different drawings (a `dlig` enabled on one of them, say), so the comparison has to
     * reach them. */
    if (a->glyphs.first != b->glyphs.first || a->glyphs.count != b->glyphs.count) {
        return 0;
    }
    if (a->fontToken != b->fontToken) {
        return 0;
    }
    return 1;
}

PaintGlyphSpan PaintTextRunGlyphs(const PaintTextRun *run)
{
    if (!run) {
        return PaintGlyphSpanAt(0u);
    }
    return run->glyphs;
}

PaintFontToken PaintTextRunFontToken(const PaintTextRun *run)
{
    return run ? run->fontToken : PaintFontTokenNone;
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

/**
 * The wrap-safe containment test **the two pools share** (Phase 12.16).
 *
 * Subtraction rather than addition. `first + count` can wrap and would then report a span
 * *inside* a small pool; `count > total - first` cannot, because `first < total` is
 * established first.
 *
 * Written once here rather than twice, because a second copy of *this* is exactly the kind
 * of arithmetic that drifts -- and the two pools differ only in what they hold and in what
 * an **empty** span means, which is the caller's question and not this function's. A text
 * pool's empty span is a position (what a caret is); a glyph pool's is the "no face" state;
 * neither is a run, and both are answered by the caller before this is reached.
 */
static int PoolSpanContains(uint32_t first, uint32_t count, size_t total)
{
    if ((size_t)first >= total) {
        return 0;
    }
    return (size_t)count <= total - (size_t)first;
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
    return PoolSpanContains(span.first, span.count, pool->count);
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

/* ------------------------------------------------------------------ */
/* The glyph                                                           */
/* ------------------------------------------------------------------ */

void PaintGlyphInit(PaintGlyph *glyph)
{
    if (!glyph) {
        return;
    }
    memset(glyph, 0, sizeof(*glyph));
}

/**
 * @p value is a number a raster may receive: `v == v` and a magnitude bound rather than
 * `isfinite`, because this file is compiled into the guest program and the freestanding
 * target has no `math.h` -- the same three lines `RoboTextGlyphs.c`, `RoboGraphicsText.c`
 * and `RoboFontStyle.c` each write for the same reason.
 */
static int GlyphValueIsFinite(float value)
{
    return (value == value) && (value <= 3.402823466e+38f) && (value >= -3.402823466e+38f);
}

int PaintGlyphIsValid(const PaintGlyph *glyph)
{
    if (!glyph) {
        return 0;
    }
    /*
     * The vectors, first, because they are what a raster consumes: a `NaN` origin reaches a
     * coverage blend as a coordinate and a `NaN` advance reaches the pen, and neither is a
     * thing a caller can diagnose from a picture.
     */
    if (!GlyphValueIsFinite(glyph->advanceX) || !GlyphValueIsFinite(glyph->advanceY) ||
        !GlyphValueIsFinite(glyph->offsetX) || !GlyphValueIsFinite(glyph->offsetY)) {
        return 0;
    }
    /*
     * The source range, relative to the run: a glyph that came from a control covers no
     * scalar, an ordinary glyph covers one, and a ligature covers several -- and the source
     * must start where the run's own span starts or later. `sourceFirst + sourceCount` is
     * deliberately **not** bounded here: the bound is the recording run's own length, which
     * this record does not carry, and the pass that built it is the pass that knows it.
     */
    if (glyph->sourceCount != 0u && glyph->sourceFirst > (uint32_t)0x7FFFFFFFu) {
        return 0;
    }
    return 1;
}

int PaintGlyphEqual(const PaintGlyph *a, const PaintGlyph *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    /* Field by field, like every record here: two glyphs built independently must compare
     * equal and padding between members is not a fact about either of them. The `flags` word
     * is compared as a word, which is what makes "the same glyph, differently flagged" a
     * difference rather than a coincidence. */
    return a->glyphId == b->glyphId && a->cluster == b->cluster &&
           a->sourceFirst == b->sourceFirst && a->sourceCount == b->sourceCount &&
           a->advanceX == b->advanceX && a->advanceY == b->advanceY &&
           a->offsetX == b->offsetX && a->offsetY == b->offsetY && a->flags == b->flags;
}

int PaintGlyphIsControl(const PaintGlyph *glyph)
{
    return glyph && (glyph->flags & (uint32_t)PaintGlyphFlagControl) != 0u;
}

PaintGlyphSpan PaintGlyphSpanAt(uint32_t first)
{
    PaintGlyphSpan span;

    span.first = first;
    span.count = 0u;
    return span;
}

int PaintGlyphSpanIsEmpty(PaintGlyphSpan span)
{
    return span.count == 0u;
}

/* ------------------------------------------------------------------ */
/* The glyph pool                                                      */
/* ------------------------------------------------------------------ */

/** Glyphs a pool holds before its first growth. */
#define RoboPaintGlyphInitialCapacity 32u

struct PaintGlyphPool {
    PaintGlyph *glyphs;
    /** Glyphs held. */
    size_t count;
    /** Glyphs the array has room for. */
    size_t capacity;
};

Result PaintGlyphPoolCreate(PaintGlyphPool **outPool,
                            PaintDiag *diag)
{
    PaintGlyphPool *pool;

    if (!outPool) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "out_pool is NULL");
    }
    *outPool = NULL;
    pool = (PaintGlyphPool *)RoboCalloc(1u, sizeof(*pool));
    if (!pool) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the glyph pool");
    }
    pool->glyphs = (PaintGlyph *)RoboCalloc(RoboPaintGlyphInitialCapacity,
                                             sizeof(*pool->glyphs));
    if (!pool->glyphs) {
        RoboFree(pool);
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the glyph pool's first block");
    }
    pool->capacity = RoboPaintGlyphInitialCapacity;
    *outPool = pool;
    return RoboOk;
}

void PaintGlyphPoolDestroy(PaintGlyphPool *pool)
{
    if (!pool) {
        return;
    }
    /* A glyph is a value with no pointers in it, so freeing the array is the whole of the
     * release -- the same argument `PaintTextPoolDestroy` makes one type up. */
    RoboFree(pool->glyphs);
    RoboFree(pool);
}

void PaintGlyphPoolClear(PaintGlyphPool *pool)
{
    if (!pool) {
        return;
    }
    pool->count = 0u;
}

Result PaintGlyphPoolAppend(PaintGlyphPool *pool,
                            const PaintGlyph *glyphs,
                            size_t count,
                            PaintGlyphSpan *outSpan,
                            PaintDiag *diag)
{
    PaintGlyph *grown;
    size_t wanted;

    if (outSpan) {
        *outSpan = PaintGlyphSpanAt(0u);
    }
    if (!pool || !outSpan) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the pool and the output span are required");
    }
    if (count == 0u) {
        /* An empty glyph span is the "no face" state, and it is spelled by the sentinel
         * rather than by an append -- so the door that would hand one out refuses, exactly
         * as the text pool's does for its own empty span. */
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "an appended run must cover at least one glyph");
    }
    if (!glyphs) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "glyphs is NULL");
    }
    /* The span's two halves are 32-bit indices, so a pool that could not name a run is
     * refused rather than truncated -- the text pool's own rule, for the same reason. */
    if (pool->count > (size_t)0xFFFFFFFFu ||
        count > (size_t)0xFFFFFFFFu - pool->count) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the glyph pool cannot index a run this long");
    }
    if (pool->count + count > pool->capacity) {
        wanted = pool->capacity;
        if (wanted < 1u) {
            wanted = count;
        }
        while (wanted < pool->count + count) {
            wanted = wanted * 2u;
        }
        grown = (PaintGlyph *)Realloc(pool->glyphs, wanted * sizeof(*grown));
        if (!grown) {
            return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                                   "could not grow the glyph pool");
        }
        /* Zeroed from the old end onward, so a consumer walking `capacity` slots sees
         * cleared glyphs past the count rather than the allocator's leftovers. */
        memset(grown + pool->capacity, 0, (wanted - pool->capacity) * sizeof(*grown));
        pool->glyphs = grown;
        pool->capacity = wanted;
    }
    memcpy(pool->glyphs + pool->count, glyphs, count * sizeof(*glyphs));
    outSpan->first = (uint32_t)pool->count;
    outSpan->count = (uint32_t)count;
    pool->count += count;
    return RoboOk;
}

Result PaintGlyphPoolCopy(PaintGlyphPool *pool,
                          const PaintGlyphPool *source,
                          PaintGlyphSpan span,
                          PaintGlyphSpan *outSpan,
                          PaintDiag *diag)
{
    if (outSpan) {
        *outSpan = PaintGlyphSpanAt(0u);
    }
    if (!pool || !source || !outSpan) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the destination pool, the source pool and the output span "
                               "are required");
    }
    if (!PaintGlyphPoolContains(source, span) || span.count == 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the span does not name a run in the source pool");
    }
    return PaintGlyphPoolAppend(pool, source->glyphs + span.first, (size_t)span.count, outSpan,
                                diag);
}

size_t PaintGlyphPoolCount(const PaintGlyphPool *pool)
{
    return pool ? pool->count : 0u;
}

size_t PaintGlyphPoolCapacity(const PaintGlyphPool *pool)
{
    return pool ? pool->capacity : 0u;
}

const PaintGlyph *PaintGlyphPoolGlyphs(const PaintGlyphPool *pool)
{
    if (!pool || pool->count == 0u) {
        return NULL;
    }
    return pool->glyphs;
}

int PaintGlyphPoolContains(const PaintGlyphPool *pool,
                           PaintGlyphSpan span)
{
    if (!pool) {
        return 0;
    }
    if (span.count == 0u) {
        /* An empty glyph span is a **state**, not a position: it means "this run was
         * recorded without a face". It is not a run, so it is not contained -- and it is
         * legal on a record all the same, which is why the record's own validity rule is
         * the one that admits it (`PaintTextRunIsValid`) and the container's check is only
         * ever asked about a non-empty span. */
        return 0;
    }
    return PoolSpanContains(span.first, span.count, pool->count);
}

Result PaintGlyphPoolRead(const PaintGlyphPool *pool,
                          PaintGlyphSpan span,
                          PaintGlyph *out,
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
    if (!PaintGlyphPoolContains(pool, span)) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the span does not name a run in this pool");
    }
    if (outCapacity < (size_t)span.count) {
        *outCount = (size_t)span.count;
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboNotSupported,
                               "the destination cannot hold the run");
    }
    memcpy(out, pool->glyphs + span.first, (size_t)span.count * sizeof(*out));
    *outCount = (size_t)span.count;
    return RoboOk;
}
