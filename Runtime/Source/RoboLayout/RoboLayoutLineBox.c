/*
 * RoboLayout line boxes (Phase 10.6) — see RoboLayoutLineBox.h.
 *
 * The record, its two-value state vocabulary, and the one arithmetic fact a caller
 * should not have to re-derive: the rectangle. The arena lives in RoboLayoutTree.c
 * with the tree's other two, because a line box is owned by the tree and produced by
 * a pass, and the *read doors* that go with an arena are declared where the tree is
 * (RoboLayoutTree.h).
 *
 * What is not here, deliberately: anything that builds a line. Construction is the
 * inline pass's, and the record is what that pass has to say when it is finished.
 */

#include "RoboLayout/RoboLayoutLineBox.h"

#include <string.h>

void LayoutLineBoxInit(LayoutLineBox *line)
{
    if (!line) {
        return;
    }
    /*
     * Zeroed and then made explicit, which is not redundancy: memset would leave
     * `state` at OPEN by accident of value order and `context` at NONE by accident
     * of 0 being the absence. Both are the answers this module wants, and both are
     * written down so that reordering an enumerator cannot silently change what a
     * cleared record means.
     */
    memset(line, 0, sizeof(*line));
    line->id = RoboLayoutLineIdNone;
    line->state = RoboLayoutLineOpen;
    line->context = RoboLayoutFcIdNone;
    line->owner = RoboLayoutNodeIdNone;
    line->index = 0u;
    line->origin.x = 0;
    line->origin.y = 0;
    line->size.width = 0;
    line->size.height = 0;
    LayoutBaselineMetricsInit(&line->baseline);
    line->availableWidth = 0;
    line->members = 0u;
}

int LayoutLineBoxEqual(const LayoutLineBox *a,
                               const LayoutLineBox *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->id == b->id && a->state == b->state && a->context == b->context &&
           a->owner == b->owner && a->index == b->index && a->origin.x == b->origin.x &&
           a->origin.y == b->origin.y && a->size.width == b->size.width &&
           a->size.height == b->size.height &&
           LayoutBaselineMetricsEqual(&a->baseline, &b->baseline) &&
           a->availableWidth == b->availableWidth && a->members == b->members;
}

/*
 * The vocabulary, walked by index below rather than by a switch with a default:
 * `LayoutLineStateIsKnown` is a range check because the enumerators are
 * dense, and the *name* switch is exhaustive with no default so that adding a state
 * is a compiler warning rather than a string that silently reads "unknown".
 */

int LayoutLineStateIsKnown(LayoutLineState state)
{
    return (int)state >= 0 && (unsigned int)state < RoboLayoutLineStateCount;
}

const char *LayoutLineStateName(LayoutLineState state)
{
    switch (state) {
    case RoboLayoutLineOpen:
        return "open";
    case RoboLayoutLineFinal:
        return "final";
    }
    return "unknown";
}

Result LayoutLineBoxRect(const LayoutLineBox *line,
                                        LayoutRect *outRect)
{
    if (!line || !outRect) {
        return RoboInvalidArgument;
    }
    /*
     * Through RoboGraphics, not by building the four fields here: the half-open
     * convention is RoboGraphics' (RoboLayoutGeometry.h says so) and a second place
     * that decided what `width` means would be a second answer to it.
     */
    return GfxRectFromPointSize(line->origin, line->size, outRect);
}

Result LayoutLineBoxBaseline(const LayoutLineBox *line,
                                           LayoutBaselineMetrics *out)
{
    if (!line || !out) {
        return RoboInvalidArgument;
    }
    *out = line->baseline;
    return RoboOk;
}

Result LayoutLineBoxBaselineOffset(const LayoutLineBox *line,
                                                   int32_t *outOffset)
{
    if (!line || !outOffset) {
        return RoboInvalidArgument;
    }
    /*
     * Deferred to the metrics type rather than reading `line->baseline.ascent`
     * directly, and the indirection earns its keep twice: the NONE refusal is the
     * same one every other caller of the door gets, and the conversion from "ascent"
     * to "offset from the top" is stated in one place.
     */
    return LayoutBaselineOffset(&line->baseline, outOffset);
}

Result LayoutLineBoxBaselineY(const LayoutLineBox *line,
                                             int32_t *outY)
{
    int32_t offset = 0;
    Result status;

    if (!line || !outY) {
        return RoboInvalidArgument;
    }

    status = LayoutBaselineOffset(&line->baseline, &offset);
    if (status != RoboOk) {
        *outY = 0;
        return status;
    }

    /*
     * Summed in 64 bits and clamped, for the reason the metrics' own extent is: a line
     * whose origin and ascent are both near the pixel range's top would otherwise wrap
     * into a baseline *above* its own top edge, and a renderer would draw a floor in
     * the wrong place rather than be told the number does not fit. A document cannot
     * reach the clamp; the alternative is an invariant that is true by luck.
     */
    {
        int64_t y = (int64_t)line->origin.y + (int64_t)offset;

        if (y < 0) {
            *outY = 0;
        } else if (y > (int64_t)INT32_MAX) {
            *outY = INT32_MAX;
        } else {
            *outY = (int32_t)y;
        }
    }
    return RoboOk;
}
