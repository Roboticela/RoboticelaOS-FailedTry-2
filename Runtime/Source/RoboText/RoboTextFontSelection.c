/*
 * RoboText's font selection and coverage (Phase 12.5) — see RoboTextFontSelection.h.
 *
 * Two mechanisms, and they are separable on purpose:
 *
 *   **Coverage** is RoboFont's, wrapped once. `RoboTextFontFaceSupportsCodePoint` parses the face's
 *   coverage lazily (idempotent in RoboFont) and asks the immutable range table; nothing here re-derives a
 *   `cmap`. What the selector adds on top is a 256-slot direct-mapped **memo** keyed by (face, code point),
 *   because the pattern a text run produces is a handful of repeated questions — the same space, the same
 *   punctuation, the same digits — and a memo makes the repeat O(1) instead of a binary search. The
 *   counters exist so a test can prove the memo is exercised rather than merely present.
 *
 *   **Selection** is a deterministic walk: primary first, then the chain in its own order, first candidate
 *   whose coverage answers wins. No scoring, no tie-break by anything but index, no randomness — which is
 *   what makes `text.analysis.determinism` a meaningful assertion rather than a flaky one.
 *
 * The scalar and range doors differ in exactly one place: the range door treats a run as a **unit**, so a
 * candidate that covers the run's base but not every scalar still wins (with `complete = 0`) rather than
 * splitting a base character from its combining mark across two faces. That is the phase's combining-mark
 * requirement expressed as a selection policy rather than as a special case in the shaper.
 */

#include "RoboText/RoboTextFontSelection.h"

#include "RoboMem/RoboMem.h"
#include "RoboTextInternal.h"

#include <string.h>

/** Memo slots. A power of two and small on purpose: this is a repeat-catcher, not a database. */
#define RoboTextCoverageMemoSlots 256u

typedef struct RoboTextCoverageMemoEntry {
    const FontFace *face;
    uint32_t codePoint;
    uint8_t valid;
    uint8_t covered;
} RoboTextCoverageMemoEntry;

struct RoboTextFontSelector {
    RoboTextFontContext primary;
    RoboTextFontFallback chain;
    RoboTextCoverageMemoEntry memo[RoboTextCoverageMemoSlots];
    RoboTextCoverageCacheStats stats;
};

/* ------------------------------------------------------------------ */
/* Resolution vocabulary                                               */
/* ------------------------------------------------------------------ */

int RoboTextFontSourceIsKnown(RoboTextFontSource source)
{
    return source == RoboTextFontSourceNone || source == RoboTextFontSourcePrimary ||
           source == RoboTextFontSourceFallback;
}

const char *RoboTextFontSourceName(RoboTextFontSource source)
{
    switch (source) {
        case RoboTextFontSourcePrimary:
            return "primary";
        case RoboTextFontSourceFallback:
            return "fallback";
        case RoboTextFontSourceNone:
        default:
            return "none";
    }
}

void RoboTextResolvedFontInit(RoboTextResolvedFont *font)
{
    if (!font) {
        return;
    }
    memset(font, 0, sizeof(*font));
    font->family = "";
    font->source = RoboTextFontSourceNone;
    font->candidateIndex = RoboTextFontNoCandidate;
}

int RoboTextResolvedFontIsSet(const RoboTextResolvedFont *font)
{
    return font != NULL && font->face != NULL && font->source != RoboTextFontSourceNone;
}

int RoboTextResolvedFontEqual(const RoboTextResolvedFont *a, const RoboTextResolvedFont *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->faceId == b->faceId && a->sizePx == b->sizePx && a->weight == b->weight &&
           a->style == b->style && a->stretch == b->stretch && a->source == b->source &&
           a->candidateIndex == b->candidateIndex;
}

int RoboTextResolvedFontCompare(const RoboTextResolvedFont *a, const RoboTextResolvedFont *b)
{
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    if (a->faceId != b->faceId) {
        return a->faceId < b->faceId ? -1 : 1;
    }
    if (a->sizePx != b->sizePx) {
        return a->sizePx < b->sizePx ? -1 : 1;
    }
    if (a->weight != b->weight) {
        return a->weight < b->weight ? -1 : 1;
    }
    if ((int)a->style != (int)b->style) {
        return (int)a->style < (int)b->style ? -1 : 1;
    }
    if (a->stretch != b->stretch) {
        return a->stretch < b->stretch ? -1 : 1;
    }
    if ((int)a->source != (int)b->source) {
        return (int)a->source < (int)b->source ? -1 : 1;
    }
    if (a->candidateIndex != b->candidateIndex) {
        return a->candidateIndex < b->candidateIndex ? -1 : 1;
    }
    return 0;
}

Result RoboTextResolvedFontWrite(const RoboTextResolvedFont *font,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength)
{
    char line[176];
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!font || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    length = RoboTextWriterText(line, sizeof(line), length, "source=");
    length = RoboTextWriterText(line, sizeof(line), length, RoboTextFontSourceName(font->source));
    length = RoboTextWriterText(line, sizeof(line), length, " face=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)font->faceId);
    length = RoboTextWriterText(line, sizeof(line), length, " family=");
    length = RoboTextWriterText(line, sizeof(line), length, font->family ? font->family : "");
    length = RoboTextWriterText(line, sizeof(line), length, " size=");
    length = RoboTextWriterFloat(line, sizeof(line), length, font->sizePx);
    length = RoboTextWriterText(line, sizeof(line), length, " weight=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)font->weight);
    length = RoboTextWriterText(line, sizeof(line), length, " style=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)font->style);
    length = RoboTextWriterText(line, sizeof(line), length, " stretch=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)font->stretch);
    length = RoboTextWriterText(line, sizeof(line), length, " candidate=");
    if (font->candidateIndex == RoboTextFontNoCandidate) {
        length = RoboTextWriterText(line, sizeof(line), length, "-");
    } else {
        length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)font->candidateIndex);
    }
    length = RoboTextWriterText(line, sizeof(line), length, " complete=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)(font->complete ? 1 : 0));
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    if (length + 1u > capacity) {
        if (outLength) {
            *outLength = length;
        }
        return RoboNotSupported;
    }
    memcpy(buffer, line, length + 1u);
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Coverage                                                            */
/* ------------------------------------------------------------------ */

Result RoboTextFontFaceSupportsCodePoint(FontFace *face,
                                         RoboTextCodePoint codePoint,
                                         int *outSupported,
                                         RoboTextDiag *diag)
{
    const FontCoverage *coverage;

    if (!face || !outSupported) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a face and an out flag are required");
    }
    *outSupported = 0;
    if (!RoboTextCodePointIsScalar(codePoint)) {
        /* A value that is not a scalar is not a character, so no face covers it. Answering rather than
         * refusing keeps a caller walking a buffer from having to special-case a decoder it already trusts. */
        return RoboOk;
    }
    if (FontFaceParseCoverage(face) != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboError,
                               "the face's coverage could not be built");
    }
    coverage = FontFaceCoverage(face);
    if (!coverage) {
        /* No usable `cmap`: the face covers nothing. That is an answer, and it is the same answer a face
         * with an empty cmap gives. */
        return RoboOk;
    }
    /* `FontCoverageContains` is exact membership for a *usable* mapping: a scalar mapped to glyph 0 is a
     * `.notdef` and is deliberately not coverage. */
    *outSupported = FontCoverageContains(coverage, (uint32_t)codePoint) ? 1 : 0;
    return RoboOk;
}

Result RoboTextFontFaceSupportsRange(FontFace *face,
                                     const uint32_t *codePoints,
                                     size_t count,
                                     size_t *outMissing,
                                     RoboTextDiag *diag)
{
    if (!outMissing) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "an out count is required");
    }
    *outMissing = 0u;
    if (!codePoints && count != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "code points are required for a non-empty range");
    }
    for (size_t index = 0u; index < count; index++) {
        int covered = 0;
        Result status = RoboTextFontFaceSupportsCodePoint(face, (RoboTextCodePoint)codePoints[index],
                                                          &covered, diag);

        if (status != RoboOk) {
            return status;
        }
        if (!covered) {
            (*outMissing)++;
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The memo                                                            */
/* ------------------------------------------------------------------ */

static size_t CoverageMemoSlot(const FontFace *face, RoboTextCodePoint codePoint)
{
    uintptr_t mix = (uintptr_t)face;

    mix ^= (uintptr_t)codePoint * (uintptr_t)2654435761u;
    mix ^= mix >> 15;
    return (size_t)(mix % RoboTextCoverageMemoSlots);
}

/**
 * The selector's coverage question, memoised.
 *
 * The face's own door is the answer; the memo only remembers it. A stale entry is impossible while a face
 * lives — RoboFont's coverage is immutable after its lazy build — so the memo needs no invalidations and
 * the only mutation is a slot overwrite.
 */
static Result SelectorCovers(RoboTextFontSelector *selector,
                             FontFace *face,
                             RoboTextCodePoint codePoint,
                             int *outCovered,
                             RoboTextDiag *diag)
{
    size_t slot = CoverageMemoSlot(face, codePoint);
    RoboTextCoverageMemoEntry *entry = &selector->memo[slot];

    selector->stats.queries++;
    if (entry->valid && entry->face == face && entry->codePoint == (uint32_t)codePoint) {
        selector->stats.hits++;
        *outCovered = entry->covered ? 1 : 0;
        return RoboOk;
    }
    selector->stats.misses++;
    {
        int covered = 0;
        Result status = RoboTextFontFaceSupportsCodePoint(face, codePoint, &covered, diag);

        if (status != RoboOk) {
            return status;
        }
        if (!entry->valid) {
            selector->stats.entries++;
        }
        entry->valid = 1;
        entry->face = face;
        entry->codePoint = (uint32_t)codePoint;
        entry->covered = (uint8_t)(covered ? 1 : 0);
        *outCovered = covered;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The selector                                                        */
/* ------------------------------------------------------------------ */

Result RoboTextFontSelectorCreate(RoboTextFontSelector **out, RoboTextDiag *diag)
{
    RoboTextFontSelector *selector;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "an out pointer is required");
    }
    *out = NULL;
    selector = (RoboTextFontSelector *)RoboCalloc(1u, sizeof(*selector));
    if (!selector) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboOutOfMemory,
                               "the font selector could not be allocated");
    }
    RoboTextFontContextInit(&selector->primary);
    *out = selector;
    return RoboOk;
}

void RoboTextFontSelectorDestroy(RoboTextFontSelector *selector)
{
    RoboFree(selector);
}

int RoboTextFontSelectorIsUsable(const RoboTextFontSelector *selector)
{
    if (!selector) {
        return 0;
    }
    if (selector->primary.face != NULL) {
        return 1;
    }
    return selector->chain.count != 0u && selector->chain.entries != NULL;
}

Result RoboTextFontSelectorSetPrimary(RoboTextFontSelector *selector,
                                      const RoboTextFontContext *primary,
                                      RoboTextDiag *diag)
{
    if (!selector || !primary) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a selector and a primary context are required");
    }
    if (!RoboTextFontStyleIsValid(&primary->style)) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "the primary setting is not one this build can draw");
    }
    selector->primary = *primary;
    return RoboOk;
}

const RoboTextFontContext *RoboTextFontSelectorPrimary(const RoboTextFontSelector *selector)
{
    return selector ? &selector->primary : NULL;
}

Result RoboTextFontSelectorSetFallback(RoboTextFontSelector *selector,
                                       const RoboTextFontFallback *chain,
                                       RoboTextDiag *diag)
{
    if (!selector) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument, "a selector is required");
    }
    if (!chain || (!chain->entries && chain->count != 0u)) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a chain with entries needs entries");
    }
    selector->chain = *chain;
    return RoboOk;
}

const RoboTextFontFallback *RoboTextFontSelectorFallback(const RoboTextFontSelector *selector)
{
    return selector ? &selector->chain : NULL;
}

/** Candidates in selection order: index 0 is the primary, `1 + i` is chain entry @p i. */
static size_t SelectorCandidateCount(const RoboTextFontSelector *selector)
{
    return 1u + selector->chain.count;
}

static FontFace *SelectorCandidateFace(const RoboTextFontSelector *selector, size_t index)
{
    if (index == 0u) {
        return selector->primary.face;
    }
    if (index - 1u < selector->chain.count) {
        return selector->chain.entries[index - 1u].face;
    }
    return NULL;
}

/** Record @p candidateIndex's selection into @p out, drawing the rendering parameters as the header says. */
static void SelectorRecord(const RoboTextFontSelector *selector,
                           size_t candidateIndex,
                           int complete,
                           RoboTextResolvedFont *out)
{
    const RoboTextFontStyle *style = &selector->primary.style;

    RoboTextResolvedFontInit(out);
    out->sizePx = style->sizePx;
    out->weight = style->weight;
    out->style = style->style;
    out->stretch = style->stretch;
    out->candidateIndex = candidateIndex;
    out->complete = complete;
    if (candidateIndex == RoboTextFontNoCandidate) {
        /* Nothing answered. The rendering parameters are still the requested ones, because a caller
         * drawing `.notdef` for the text still needs to know what size it was set at. */
        out->face = NULL;
        out->faceId = 0u;
        out->family = "";
        out->source = RoboTextFontSourceNone;
        return;
    }
    if (candidateIndex == 0u) {
        out->face = selector->primary.face;
        out->family = "";
        out->source = (out->face != NULL) ? RoboTextFontSourcePrimary : RoboTextFontSourceNone;
        if (out->face == NULL) {
            out->candidateIndex = RoboTextFontNoCandidate;
        }
    } else {
        const RoboTextFontCandidate *candidate = &selector->chain.entries[candidateIndex - 1u];

        out->face = candidate->face;
        out->family = candidate->family ? candidate->family : "";
        out->source = (out->face != NULL) ? RoboTextFontSourceFallback : RoboTextFontSourceNone;
        if (out->face == NULL) {
            out->candidateIndex = RoboTextFontNoCandidate;
        }
        /* A candidate that declares a weight/stretch speaks for itself; one that does not leaves the
         * requested setting, so a coverage-only entry cannot silently claim a different weight. */
        if (candidate->weight != 0u) {
            out->weight = candidate->weight;
        }
        if (FontStretchIsValid(candidate->stretch)) {
            out->stretch = candidate->stretch;
        }
        out->style = candidate->style;
    }
    if (out->face) {
        out->faceId = GetFontFaceId(out->face);
    }
}

Result RoboTextFontSelectorSelectScalar(RoboTextFontSelector *selector,
                                        RoboTextCodePoint codePoint,
                                        RoboTextResolvedFont *out,
                                        RoboTextDiag *diag)
{
    size_t candidates;

    if (!selector || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a selector and an out record are required");
    }
    RoboTextResolvedFontInit(out);
    candidates = SelectorCandidateCount(selector);
    for (size_t index = 0u; index < candidates; index++) {
        FontFace *face = SelectorCandidateFace(selector, index);
        int covered = 0;
        Result status;

        if (!face) {
            continue;
        }
        status = SelectorCovers(selector, face, codePoint, &covered, diag);
        if (status != RoboOk) {
            return status;
        }
        if (covered) {
            SelectorRecord(selector, index, 1, out);
            return RoboOk;
        }
    }
    /* Nothing covers: the honest answer, recorded with the *primary's* rendering parameters so a caller
     * drawing the fallback `.notdef` still knows what size the text was set at. */
    SelectorRecord(selector, RoboTextFontNoCandidate, 0, out);
    out->source = RoboTextFontSourceNone;
    out->face = NULL;
    out->faceId = 0u;
    out->family = "";
    return RoboOk;
}

Result RoboTextFontSelectorSelectRange(RoboTextFontSelector *selector,
                                       const RoboTextCodePoint *codePoints,
                                       size_t count,
                                       RoboTextResolvedFont *out,
                                       RoboTextDiag *diag)
{
    size_t candidates;
    size_t baseIndex = 0u;

    if (!selector || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a selector and an out record are required");
    }
    RoboTextResolvedFontInit(out);
    if (!codePoints && count != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "code points are required for a non-empty range");
    }
    if (count == 0u) {
        /* Nothing to cover: the primary is the answer and it is trivially complete. */
        SelectorRecord(selector, (selector->primary.face != NULL) ? 0u : RoboTextFontNoCandidate, 1, out);
        return RoboOk;
    }

    /* The run's base: its first scalar that is not a combining mark. A run of nothing but marks takes its
     * first scalar, which is the only answer available and the one the caller's own base then governs. */
    for (size_t index = 0u; index < count; index++) {
        if (!RoboTextUnicodeIsCombiningMark((RoboTextCodePoint)codePoints[index])) {
            baseIndex = index;
            break;
        }
    }

    candidates = SelectorCandidateCount(selector);
    for (int pass = 0; pass < 2; pass++) {
        for (size_t index = 0u; index < candidates; index++) {
            FontFace *face = SelectorCandidateFace(selector, index);
            size_t missing = 0u;
            Result status;

            if (!face) {
                continue;
            }
            if (pass == 0) {
                status = RoboTextFontFaceSupportsRange(face, (const uint32_t *)codePoints, count, &missing,
                                                       diag);
                if (status != RoboOk) {
                    return status;
                }
                if (missing == 0u) {
                    SelectorRecord(selector, index, 1, out);
                    return RoboOk;
                }
            } else {
                int covered = 0;

                status = SelectorCovers(selector, face, (RoboTextCodePoint)codePoints[baseIndex], &covered,
                                        diag);
                if (status != RoboOk) {
                    return status;
                }
                if (covered) {
                    /* The base is covered but not the whole run: the honest record says so, because a
                     * caller that wants to count the misses (or a later shaper that will want to) reads
                     * `complete` rather than guessing. */
                    SelectorRecord(selector, index, 0, out);
                    return RoboOk;
                }
            }
        }
    }
    SelectorRecord(selector, RoboTextFontNoCandidate, 0, out);
    out->source = RoboTextFontSourceNone;
    out->face = NULL;
    out->faceId = 0u;
    out->family = "";
    return RoboOk;
}

Result RoboTextFontSelectorSelectRangeView(RoboTextFontSelector *selector,
                                           RoboTextView view,
                                           RoboTextRange range,
                                           RoboTextResolvedFont *out,
                                           RoboTextDiag *diag)
{
    RoboTextScalarIndex scalars = 0u;
    Uchar32 *codePoints = NULL;
    size_t count = 0u;
    Result status;

    if (!selector || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a selector and an out record are required");
    }
    status = RoboTextViewScalarCount(view, &scalars, diag);
    if (status != RoboOk) {
        return status;
    }
    if (RoboTextRangeIsInverted(range) || !RoboTextRangeIsWithin(range, scalars)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a text range does not lie inside the text");
    }
    count = (size_t)RoboTextRangeLength(range);
    if (count != 0u) {
        codePoints = (Uchar32 *)RoboCalloc(count, sizeof(*codePoints));
        if (!codePoints) {
            return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboOutOfMemory,
                                   "the run's code points could not be allocated");
        }
        status = RoboTextViewDecodeRange(view, range, codePoints, count, NULL, diag);
        if (status != RoboOk) {
            RoboFree(codePoints);
            return status;
        }
    }
    status = RoboTextFontSelectorSelectRange(selector, (const RoboTextCodePoint *)codePoints, count, out,
                                             diag);
    RoboFree(codePoints);
    return status;
}

Result RoboTextFontSelectorCacheStats(const RoboTextFontSelector *selector,
                                      RoboTextCoverageCacheStats *out)
{
    if (!selector || !out) {
        return RoboInvalidArgument;
    }
    *out = selector->stats;
    return RoboOk;
}

void RoboTextFontSelectorCacheClear(RoboTextFontSelector *selector)
{
    if (!selector) {
        return;
    }
    memset(selector->memo, 0, sizeof(selector->memo));
    memset(&selector->stats, 0, sizeof(selector->stats));
}

Result RoboTextFuzzFontSelection(const uint32_t *codePoints, size_t count)
{
    RoboTextFontSelector *selector = NULL;
    RoboTextResolvedFont font;

    if (!codePoints && count != 0u) {
        return RoboInvalidArgument;
    }
    if (RoboTextFontSelectorCreate(&selector, NULL) != RoboOk) {
        return RoboError;
    }
    if (RoboTextFontSelectorSelectRange(selector, (const RoboTextCodePoint *)codePoints, count, &font,
                                        NULL) != RoboOk) {
        RoboTextFontSelectorDestroy(selector);
        return RoboError;
    }
    RoboTextFontSelectorDestroy(selector);
    return RoboOk;
}
