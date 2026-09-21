/*
 * RoboLayout baselines (Phase 10.7) -- the vocabulary, the two rules, and the
 * arithmetic that turns a measured run into a participant's baseline.
 *
 * Three functions carry the phase and none of them touches a tree: `from_run` is
 * where a font's line metrics become half-leading, `from_extent` is the fallback
 * rule stated once, and `offset` is the one conversion a renderer needs. Everything
 * else here is the vocabulary discipline the module applies to every closed
 * enumeration (RoboLayoutTypes.c, RoboLayoutFormattingContext.c): a walkable table,
 * a name for each row, and a "one past the last" refusal that makes the vocabulary
 * closed rather than merely observed to have a size.
 *
 * The sign convention is fixed here and nowhere else: ascent and descent are
 * magnitudes, both non-negative, and `ascent + descent == extent` holds for every
 * value this file produces. A caller never has to ask which way a font authored its
 * descent -- RoboLayoutTextFont.c normalised that once, at the table, and this file
 * only ever grows the two numbers.
 */

#include "RoboLayout/RoboLayoutBaseline.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Kind                                                                */
/* ------------------------------------------------------------------ */

int LayoutBaselineKindIsKnown(LayoutBaselineKind kind)
{
    return (int)kind >= 0 && (int)kind < (int)RoboLayoutBaselineKindCount;
}

const char *LayoutBaselineKindName(LayoutBaselineKind kind)
{
    switch (kind) {
    case RoboLayoutBaselineNone:
        return "none";
    case RoboLayoutBaselineText:
        return "text";
    case RoboLayoutBaselineBox:
        return "box";
    default:
        /* A kind past the last row: named, not guessed. The suites walk the
         * vocabulary, so this string is what an enumerator added without a row
         * reads as -- which is the visible omission the walk exists to produce. */
        return "unknown";
    }
}

int LayoutBaselineKindIsUsable(LayoutBaselineKind kind)
{
    /*
     * Written as a pair of positive cases rather than as `kind != NONE`, because the
     * question is "can something be placed on this?" and a *future* kind whose answer
     * is no -- a `TOP` used as an alignment rather than a baseline, say -- must land
     * on 0 here without a second edit. A negative or out-of-range enumerator answers
     * 0 through the default arm, which is the same refusal `IsKnown` makes.
     */
    switch (kind) {
    case RoboLayoutBaselineText:
    case RoboLayoutBaselineBox:
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void LayoutBaselineMetricsInit(LayoutBaselineMetrics *metrics)
{
    if (!metrics) {
        return;
    }
    memset(metrics, 0, sizeof(*metrics));
    metrics->kind = RoboLayoutBaselineNone;
}

int LayoutBaselineMetricsEqual(const LayoutBaselineMetrics *a,
                                       const LayoutBaselineMetrics *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->kind == b->kind && a->ascent == b->ascent && a->descent == b->descent;
}

int LayoutBaselineMetricsIsValid(const LayoutBaselineMetrics *metrics)
{
    if (!metrics) {
        return 0;
    }
    /*
     * The kind is checked through `is_usable` and not through `IsKnown`, and the
     * difference is the point of having both: a NONE record is a well-formed *kind*
     * and an unusable *baseline*, so a caller that asked "is this valid?" about a
     * cleared record gets the answer that stops it dividing by the extent.
     *
     * A zero ascent with a zero descent is valid -- an unmeasured run is anchored at
     * its line's baseline with no extent -- so the terms are checked for sign and not
     * for magnitude.
     */
    return LayoutBaselineKindIsUsable(metrics->kind) && metrics->ascent >= 0 &&
           metrics->descent >= 0;
}

int32_t LayoutBaselineMetricsExtent(const LayoutBaselineMetrics *metrics)
{
    if (!metrics) {
        return 0;
    }
    /*
     * Summed in 64 bits and clamped, because a pair of large int32 magnitudes can
     * overflow the sum and a *negative* extent would then be handed to a caller that
     * trusts the invariant this file exists to keep. A document cannot reach the
     * clamp (both terms are scaled font metrics against a 4096-node ceiling), so the
     * clamp costs nothing and makes the return type total.
     */
    {
        int64_t sum = (int64_t)metrics->ascent + (int64_t)metrics->descent;

        if (sum < 0) {
            return 0;
        }
        if (sum > (int64_t)INT32_MAX) {
            return INT32_MAX;
        }
        return (int32_t)sum;
    }
}

Result LayoutBaselineOffset(const LayoutBaselineMetrics *metrics,
                                          int32_t *outOffset)
{
    if (!metrics || !outOffset) {
        return RoboInvalidArgument;
    }
    if (!LayoutBaselineMetricsIsValid(metrics)) {
        *outOffset = 0;
        return RoboNotFound;
    }
    *outOffset = metrics->ascent;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The two rules                                                       */
/* ------------------------------------------------------------------ */

Result LayoutBaselineMetricsFromRun(const LayoutTextRun *run,
                                                   LayoutBaselineMetrics *out)
{
    int32_t gap;
    int32_t half;

    if (!run || !out) {
        return RoboInvalidArgument;
    }

    LayoutBaselineMetricsInit(out);

    /*
     * The gap is a *difference* rather than a field, because that is what the run
     * record stores: 10.5 froze `height = ascent + descent + gap` so that a caller
     * reading a line extent got the number the font's own layout metrics give, and
     * splitting the gap is this phase's job. Recovering it here rather than adding a
     * field to a frozen record is the smaller change and the more honest one -- the
     * gap is not a second measurement, it is the part of the height that is neither
     * above nor below the baseline.
     *
     * A third-party measurer can produce a height *smaller* than asc+descent (the
     * record's contract does not forbid it, and a scripted measurer in a suite is
     * exactly where it happens). That is read as "no gap" rather than as a negative
     * one, so the clamp is what keeps the invariant: the two halves below still sum to
     * the *larger* of the run's height and its ascent plus descent, which means a
     * member's metrics extent can only ever equal or exceed its stored height. The
     * line that contains it therefore still encloses it, which is the property
     * RoboLayoutLineBox.h's rectangle claim depends on.
     */
    /*
     * The two terms are clamped *before* the gap is recovered from them, and the order
     * is load-bearing rather than cosmetic: a record carrying negative terms would
     * otherwise widen the gap by the very amount it is about to discard, so a zero-
     * height run with `ascent = descent = -4` would report an extent of 8 above and
     * below a baseline that spans nothing. Recovering the gap from the clamped terms
     * makes such a record fall back to the no-gap reading, which is what the
     * "extent is the larger of height and asc + desc" invariant asks for.
     */
    out->kind = RoboLayoutBaselineText;
    out->ascent = run->ascent < 0 ? 0 : run->ascent;
    out->descent = run->descent < 0 ? 0 : run->descent;

    gap = run->height - out->ascent - out->descent;
    if (gap < 0) {
        gap = 0;
    }

    /*
     * Half the gap above, the remainder below -- and the remainder rather than a
     * second division, so that an odd gap does not lose its odd pixel and the two
     * terms sum to `height` exactly. A gap of 1 puts its pixel *below* the baseline,
     * which is the same choice CSS's half-leading makes for an odd leading and the
     * reason it cannot be written as two independent halvings.
     */
    half = gap / 2;
    out->ascent += half;
    out->descent += gap - half;
    return RoboOk;
}

Result LayoutBaselineMetricsFromExtent(int32_t extent,
                                                     LayoutBaselineMetrics *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }

    LayoutBaselineMetricsInit(out);
    if (extent < 0) {
        extent = 0;
    }

    /*
     * The fallback, in one line: the whole extent is above the baseline, so the
     * object's bottom edge *is* its baseline. See the header for what CSS specifies
     * for an inline-block, which of that this build implements, and which phase owns
     * the rest.
     */
    out->kind = RoboLayoutBaselineBox;
    out->ascent = extent;
    out->descent = 0;
    return RoboOk;
}
