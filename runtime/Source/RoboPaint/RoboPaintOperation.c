/*
 * RoboPaint operations (Phase 11.1–11.2) — see RoboPaintOperation.h.
 *
 * The record, the validity rule that decides whether it may enter a list, the two
 * coordinate spaces in one call, and the band a border side occupies. The validity rule
 * is the one piece of policy here and it lives in one function so that
 * `robo_paint_list_append` and every read door share it rather than each deciding what
 * "a record" means.
 */

#include "RoboPaint/RoboPaintOperation.h"

#include "RoboPaintInternal.h"

#include <string.h>

/** Spaces the kind name is padded to, so that a two-list diff lines up. */
#define ROBO_PAINT_OP_NAME_COLUMN 14u

void robo_paint_operation_init(robo_paint_operation_t *operation)
{
    if (!operation) {
        return;
    }
    memset(operation, 0, sizeof(*operation));
    operation->kind = ROBO_PAINT_OP_NONE;
    operation->node = ROBO_LAYOUT_NODE_ID_NONE;
    robo_paint_boxes_init(&operation->boxes);
    operation->origin = robo_gfx_point(0, 0);
    robo_paint_clip_init(&operation->clip);
    operation->color = robo_css_color_none();
}

int robo_paint_operation_is_valid(const robo_paint_operation_t *operation)
{
    if (!operation) {
        return 0;
    }
    if (!robo_paint_op_kind_is_known(operation->kind)) {
        return 0;
    }
    if (operation->kind == ROBO_PAINT_OP_NONE) {
        /* A cleared record is a state, not an operation. Refusing it here is what keeps
         * "the list is empty" and "the list has one meaningless entry" apart. */
        return 0;
    }
    if (operation->node == ROBO_LAYOUT_NODE_ID_NONE) {
        return 0;
    }
    if (!robo_paint_boxes_is_valid(&operation->boxes)) {
        return 0;
    }
    if (!robo_paint_clip_is_valid(&operation->clip)) {
        return 0;
    }
    /*
     * The colour rule, and it is two rules because the two halves of the vocabulary have
     * different answers. A **structural** record draws nothing, so it must carry the
     * colour that says nothing was declared -- `robo_css_color_none()`, which is
     * RoboCSS's own "no colour", not a black that a consumer might draw. A **drawing**
     * must carry a colour a rasteriser can use: resolved, or `currentColor` that this
     * layer could not resolve (`robo_css_color_is_valid` is exactly that pair). `none` on
     * a drawing is refused because a fill or a border with no colour draws nothing, and
     * an operation that draws nothing is not a statement about the document.
     *
     * The split is `robo_paint_op_kind_draws` rather than `is_marker`, so that `CONTENT`
     * -- a structural record that is not a box marker -- is held to the "carries no
     * colour" side of the rule instead of being asked for a colour it has no use for.
     */
    if (robo_paint_op_kind_draws(operation->kind)) {
        return robo_css_color_is_valid(operation->color);
    }
    return operation->color.kind == ROBO_CSS_COLOR_NONE;
}

int robo_paint_operation_equal(const robo_paint_operation_t *a,
                               const robo_paint_operation_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->kind == b->kind && a->node == b->node && a->depth == b->depth &&
           robo_paint_boxes_equal(&a->boxes, &b->boxes) &&
           robo_gfx_point_equal(a->origin, b->origin) &&
           robo_paint_clip_equal(&a->clip, &b->clip) &&
           robo_css_color_equals(a->color, b->color);
}

robo_paint_op_kind_t robo_paint_operation_kind(const robo_paint_operation_t *operation)
{
    return operation ? operation->kind : ROBO_PAINT_OP_NONE;
}

robo_layout_node_id_t robo_paint_operation_node(const robo_paint_operation_t *operation)
{
    return operation ? operation->node : ROBO_LAYOUT_NODE_ID_NONE;
}

size_t robo_paint_operation_depth(const robo_paint_operation_t *operation)
{
    return operation ? operation->depth : 0u;
}

void robo_paint_operation_boxes(const robo_paint_operation_t *operation,
                                robo_paint_boxes_t *out)
{
    if (!out) {
        return;
    }
    if (!operation) {
        robo_paint_boxes_init(out);
        return;
    }
    *out = operation->boxes;
}

void robo_paint_operation_origin(const robo_paint_operation_t *operation,
                                 robo_layout_point_t *out)
{
    if (!out) {
        return;
    }
    *out = operation ? operation->origin : robo_gfx_point(0, 0);
}

void robo_paint_operation_clip(const robo_paint_operation_t *operation,
                               robo_paint_clip_t *out)
{
    if (!out) {
        return;
    }
    if (!operation) {
        robo_paint_clip_init(out);
        return;
    }
    *out = operation->clip;
}

robo_css_color_t robo_paint_operation_color(const robo_paint_operation_t *operation)
{
    return operation ? operation->color : robo_css_color_none();
}

int robo_paint_operation_is_structural(const robo_paint_operation_t *operation)
{
    return operation != NULL && robo_paint_op_kind_is_structural(operation->kind);
}

int robo_paint_operation_is_marker(const robo_paint_operation_t *operation)
{
    return operation != NULL && robo_paint_op_kind_is_marker(operation->kind);
}

int robo_paint_operation_is_drawing(const robo_paint_operation_t *operation)
{
    return operation != NULL && robo_paint_op_kind_draws(operation->kind);
}

robo_result_t robo_paint_operation_side(const robo_paint_operation_t *operation,
                                        robo_css_box_side_t *out)
{
    if (!operation || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_paint_op_kind_side(operation->kind, out);
}

robo_result_t robo_paint_operation_local_boxes(const robo_paint_operation_t *operation,
                                               robo_paint_boxes_t *out,
                                               robo_paint_diag_t *diag)
{
    if (!operation || !out) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "the operation and an output are required");
    }
    if (!robo_paint_boxes_is_valid(&operation->boxes)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_ERROR,
                               "the operation's boxes are not a record");
    }
    return robo_paint_boxes_translate(&operation->boxes, -operation->origin.x,
                                      -operation->origin.y, out, diag);
}

robo_result_t robo_paint_operation_side_band(const robo_paint_operation_t *operation,
                                             robo_layout_rect_t *out,
                                             robo_paint_diag_t *diag)
{
    robo_css_box_side_t side;
    const robo_layout_rect_t *border;
    const robo_layout_rect_t *padding;
    int32_t border_right;
    int32_t border_bottom;
    int32_t padding_right;
    int32_t padding_bottom;

    if (!operation || !out) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "the operation and an output are required");
    }
    if (robo_paint_op_kind_side(operation->kind, &side) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "only a border operation occupies a band");
    }
    border = &operation->boxes.border;
    padding = &operation->boxes.padding;
    border_right = robo_paint_add_saturated(border->x, border->width);
    border_bottom = robo_paint_add_saturated(border->y, border->height);
    padding_right = robo_paint_add_saturated(padding->x, padding->width);
    padding_bottom = robo_paint_add_saturated(padding->y, padding->height);

    /*
     * The band on one side is the region of the border box outside the padding box on
     * that edge, and the two axes are answered differently on purpose:
     *
     *   a **top** or **bottom** band spans the border box's full width and stops where
     *   the padding box begins or ends vertically;
     *   a **left** or **right** band spans the border box's full height and stops where
     *   the padding box begins or ends horizontally.
     *
     * Both faces of an axis contribute to the *other* axis's extent, which is why the
     * full width (or height) is used rather than the difference on both axes: a box with
     * a left border and a right border has two bands, each as tall as the box.
     *
     * Every extent is floored at zero, so a box whose insets were saturated or declined
     * produces an empty band rather than a negative one -- and an empty band is a
     * correct answer for a border that occupies no room.
     */
    switch (side) {
    case ROBO_CSS_BOX_SIDE_TOP:
        out->x = border->x;
        out->y = border->y;
        out->width = border->width;
        out->height = robo_paint_extent((int64_t)padding->y - (int64_t)border->y);
        break;
    case ROBO_CSS_BOX_SIDE_RIGHT:
        out->x = padding_right;
        out->y = border->y;
        out->width = robo_paint_extent((int64_t)border_right - (int64_t)padding_right);
        out->height = border->height;
        break;
    case ROBO_CSS_BOX_SIDE_BOTTOM:
        out->x = border->x;
        out->y = padding_bottom;
        out->width = border->width;
        out->height = robo_paint_extent((int64_t)border_bottom - (int64_t)padding_bottom);
        break;
    case ROBO_CSS_BOX_SIDE_LEFT:
        out->x = border->x;
        out->y = border->y;
        out->width = robo_paint_extent((int64_t)padding->x - (int64_t)border->x);
        out->height = border->height;
        break;
    default:
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "not a border side");
    }
    return ROBO_OK;
}

robo_result_t robo_paint_operation_write(const robo_paint_operation_t *operation,
                                         char *buffer,
                                         size_t capacity,
                                         size_t *out_length,
                                         robo_paint_diag_t *diag)
{
    robo_paint_text_t text;
    const char *name;
    char color_text[ROBO_CSS_COLOR_TEXT_MAX + 1u];
    size_t color_length = 0u;
    size_t index;
    robo_layout_rect_t rect;
    int have_rect = 0;

    if (!operation || !buffer || !out_length || capacity == 0u) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "the operation, a buffer and an out_length are required");
    }
    if (!robo_paint_op_kind_is_known(operation->kind) ||
        operation->kind == ROBO_PAINT_OP_NONE) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_INVALID_ARGUMENT,
                               "not an operation kind");
    }

    robo_paint_text_init(&text, buffer, capacity);
    name = robo_paint_op_kind_name(operation->kind);
    robo_paint_text_string(&text, name);
    for (index = strlen(name); index < ROBO_PAINT_OP_NAME_COLUMN; index++) {
        robo_paint_text_char(&text, ' ');
    }

    robo_paint_text_string(&text, "node=");
    robo_paint_text_u64(&text, (uint64_t)operation->node);
    robo_paint_text_string(&text, " depth=");
    robo_paint_text_u64(&text, (uint64_t)operation->depth);

    /* The clip, as two modes with the interval appended only on an axis that has one:
     * `clip=hidden[0,100]/visible`. */
    robo_paint_text_string(&text, " clip=");
    robo_paint_text_string(&text, robo_css_overflow_name(operation->clip.horizontal.mode));
    if (robo_paint_clip_axis_is_active(&operation->clip.horizontal)) {
        robo_paint_text_char(&text, '[');
        robo_paint_text_i64(&text, (int64_t)operation->clip.horizontal.start);
        robo_paint_text_char(&text, ',');
        robo_paint_text_i64(&text, (int64_t)operation->clip.horizontal.end);
        robo_paint_text_char(&text, ']');
    }
    robo_paint_text_char(&text, '/');
    robo_paint_text_string(&text, robo_css_overflow_name(operation->clip.vertical.mode));
    if (robo_paint_clip_axis_is_active(&operation->clip.vertical)) {
        robo_paint_text_char(&text, '[');
        robo_paint_text_i64(&text, (int64_t)operation->clip.vertical.start);
        robo_paint_text_char(&text, ',');
        robo_paint_text_i64(&text, (int64_t)operation->clip.vertical.end);
        robo_paint_text_char(&text, ']');
    }

    /* The rectangle this kind acts on: a marker and a background act on the border box,
     * a border acts on its band. Which one is printed is the kind's business, so a reader
     * of a dump does not have to remember that `background` means "the border box". */
    if (robo_paint_op_kind_is_border(operation->kind)) {
        if (robo_paint_operation_side_band(operation, &rect, NULL) == ROBO_OK) {
            have_rect = 1;
        }
        robo_paint_text_string(&text, " band=");
    } else if (operation->kind == ROBO_PAINT_OP_BACKGROUND) {
        rect = operation->boxes.border;
        have_rect = 1;
        robo_paint_text_string(&text, " rect=");
    } else if (operation->kind == ROBO_PAINT_OP_CONTENT) {
        /* The content boundary acts on the inner rectangle: the region a box's own
         * painting ends at and its contents begin in. Printed explicitly so a reader of
         * a dump sees which of the three rectangles the record is about, rather than
         * having to remember that `content` means "the content box". */
        rect = operation->boxes.content;
        have_rect = 1;
        robo_paint_text_string(&text, " content=");
    } else {
        rect = operation->boxes.border;
        have_rect = 1;
        robo_paint_text_string(&text, " border=");
    }
    if (have_rect) {
        robo_paint_text_rect(&text, rect);
    }

    if (robo_paint_operation_is_drawing(operation)) {
        if (robo_css_color_write(operation->color, color_text, sizeof(color_text),
                                 &color_length, NULL) == ROBO_OK) {
            robo_paint_text_string(&text, " color=");
            robo_paint_text_string(&text, color_text);
        }
    }

    *out_length = text.length;
    if (!robo_paint_text_fits(&text)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPERATION, ROBO_NOT_SUPPORTED,
                               "the operation text does not fit the buffer");
    }
    return ROBO_OK;
}
