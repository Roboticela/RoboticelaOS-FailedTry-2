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

void robo_layout_line_box_init(robo_layout_line_box_t *line)
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
    line->id = ROBO_LAYOUT_LINE_ID_NONE;
    line->state = ROBO_LAYOUT_LINE_OPEN;
    line->context = ROBO_LAYOUT_FC_ID_NONE;
    line->owner = ROBO_LAYOUT_NODE_ID_NONE;
    line->index = 0u;
    line->origin.x = 0;
    line->origin.y = 0;
    line->size.width = 0;
    line->size.height = 0;
    line->available_width = 0;
    line->members = 0u;
}

int robo_layout_line_box_equal(const robo_layout_line_box_t *a,
                               const robo_layout_line_box_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->id == b->id && a->state == b->state && a->context == b->context &&
           a->owner == b->owner && a->index == b->index && a->origin.x == b->origin.x &&
           a->origin.y == b->origin.y && a->size.width == b->size.width &&
           a->size.height == b->size.height &&
           a->available_width == b->available_width && a->members == b->members;
}

/*
 * The vocabulary, walked by index below rather than by a switch with a default:
 * `robo_layout_line_state_is_known` is a range check because the enumerators are
 * dense, and the *name* switch is exhaustive with no default so that adding a state
 * is a compiler warning rather than a string that silently reads "unknown".
 */

int robo_layout_line_state_is_known(robo_layout_line_state_t state)
{
    return (int)state >= 0 && (unsigned int)state < ROBO_LAYOUT_LINE_STATE_COUNT;
}

const char *robo_layout_line_state_name(robo_layout_line_state_t state)
{
    switch (state) {
    case ROBO_LAYOUT_LINE_OPEN:
        return "open";
    case ROBO_LAYOUT_LINE_FINAL:
        return "final";
    }
    return "unknown";
}

robo_result_t robo_layout_line_box_rect(const robo_layout_line_box_t *line,
                                        robo_layout_rect_t *out_rect)
{
    if (!line || !out_rect) {
        return ROBO_INVALID_ARGUMENT;
    }
    /*
     * Through RoboGraphics, not by building the four fields here: the half-open
     * convention is RoboGraphics' (RoboLayoutGeometry.h says so) and a second place
     * that decided what `width` means would be a second answer to it.
     */
    return robo_gfx_rect_from_point_size(line->origin, line->size, out_rect);
}
