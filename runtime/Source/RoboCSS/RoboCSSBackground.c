/*
 * RoboCSS background vocabulary (Phase 9.14) — see RoboCSSBackground.h.
 *
 * Three tables and one classifier, and everything below reads them:
 *
 *   the repeat rows       one per value, carrying the two axes so `repeat-x` is
 *                         data rather than a fifth branch
 *   the attachment rows   one per value
 *   the longhand table    the six properties `background` stands for, in the order
 *                         a serialisation writes them
 *
 * The classifier is the piece that makes this a different machine from the box
 * model's positional mapping: a box shorthand's components map to sides *by
 * position*, and a background shorthand's components map to longhands *by kind*.
 * Nothing here weighs a level or compares a specificity — which of a shorthand and
 * a longhand wins is the cascade's answer, asked during resolution, not this
 * module's.
 *
 * The one place a spelling appears is a table row. Every predicate below is a
 * lookup, so a value added later answers the questions that mention it without a
 * function being touched.
 */

#include "RoboCSS/RoboCSSBackground.h"

#include "RoboCSS/RoboCSSColor.h"
#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The repeat vocabulary                                               */
/* ------------------------------------------------------------------ */

/*
 * `horizontal` and `vertical` are the value, not a description of it: `repeat-x`
 * *is* "tiles horizontally, does not tile vertically". Asked as two columns
 * because they are two questions — a caller that wants to know about one axis
 * should not have to know which of four values implies it.
 */
typedef struct background_repeat_row {
    robo_css_background_repeat_t value;
    const char *keyword;
    int horizontal;
    int vertical;
} background_repeat_row_t;

static const background_repeat_row_t background_repeat_vocabulary[] = {
    { ROBO_CSS_BACKGROUND_REPEAT,     "repeat",    1, 1 },
    { ROBO_CSS_BACKGROUND_REPEAT_X,   "repeat-x",  1, 0 },
    { ROBO_CSS_BACKGROUND_REPEAT_Y,   "repeat-y",  0, 1 },
    { ROBO_CSS_BACKGROUND_NO_REPEAT,  "no-repeat", 0, 0 }
};

#define BACKGROUND_REPEAT_VOCABULARY_COUNT \
    (sizeof(background_repeat_vocabulary) / sizeof(background_repeat_vocabulary[0]))

/** The row for @p repeat, or NULL when it is not one of them. */
static const background_repeat_row_t *background_repeat_row_of(
    robo_css_background_repeat_t repeat)
{
    size_t index;

    for (index = 0u; index < BACKGROUND_REPEAT_VOCABULARY_COUNT; index++) {
        if (background_repeat_vocabulary[index].value == repeat) {
            return &background_repeat_vocabulary[index];
        }
    }
    return NULL;
}

int robo_css_background_repeat_is_known(robo_css_background_repeat_t repeat)
{
    return background_repeat_row_of(repeat) != NULL;
}

const char *robo_css_background_repeat_name(robo_css_background_repeat_t repeat)
{
    const background_repeat_row_t *row = background_repeat_row_of(repeat);

    return row ? row->keyword : "";
}

size_t robo_css_background_repeat_count(void)
{
    return BACKGROUND_REPEAT_VOCABULARY_COUNT;
}

robo_css_background_repeat_t robo_css_background_repeat_from_slice(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return ROBO_CSS_BACKGROUND_REPEAT;
    }
    for (index = 0u; index < BACKGROUND_REPEAT_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                          background_repeat_vocabulary[index].keyword)) {
            return background_repeat_vocabulary[index].value;
        }
    }
    /* Not one of them. `repeat` is the answer a caller reads *after* asking
     * robo_css_background_repeat_slice_is_known — see the header. */
    return ROBO_CSS_BACKGROUND_REPEAT;
}

robo_css_background_repeat_t robo_css_background_repeat_from_text(const char *keyword)
{
    if (!keyword) {
        return ROBO_CSS_BACKGROUND_REPEAT;
    }
    return robo_css_background_repeat_from_slice(
        robo_css_slice_from_bytes((const uint8_t *)keyword, strlen(keyword)));
}

int robo_css_background_repeat_slice_is_known(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < BACKGROUND_REPEAT_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                          background_repeat_vocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

robo_result_t robo_css_background_repeat_write(robo_css_background_repeat_t repeat,
                                               char *buffer,
                                               size_t capacity,
                                               size_t *out_length,
                                               robo_css_diag_t *diag)
{
    const background_repeat_row_t *row;
    size_t length;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *out_length = 0u;
    row = background_repeat_row_of(repeat);
    if (!row) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "not a background repeat value");
    }
    length = strlen(row->keyword);
    if (length >= capacity) {
        /* The length it would have needed, so a caller can size a buffer from the
         * refusal rather than from a constant it has to keep in step. */
        *out_length = length;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                             "the repeat keyword does not fit");
    }
    memcpy(buffer, row->keyword, length + 1u);
    *out_length = length;
    return ROBO_OK;
}

int robo_css_background_repeat_repeats_on_axis(robo_css_background_repeat_t repeat,
                                               robo_css_background_axis_t axis)
{
    const background_repeat_row_t *row = background_repeat_row_of(repeat);

    if (!row) {
        /* Not the first row's answer: a value nothing defined does not tile
         * anything, and claiming it tiles both axes would be inventing one. */
        return 0;
    }
    if (axis == ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL) {
        return row->horizontal;
    }
    if (axis == ROBO_CSS_BACKGROUND_AXIS_VERTICAL) {
        return row->vertical;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The axis                                                            */
/* ------------------------------------------------------------------ */

int robo_css_background_axis_is_known(robo_css_background_axis_t axis)
{
    return axis == ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL ||
           axis == ROBO_CSS_BACKGROUND_AXIS_VERTICAL;
}

const char *robo_css_background_axis_name(robo_css_background_axis_t axis)
{
    switch (axis) {
    case ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL:
        return "horizontal";
    case ROBO_CSS_BACKGROUND_AXIS_VERTICAL:
        return "vertical";
    default:
        return "";
    }
}

/* ------------------------------------------------------------------ */
/* The attachment vocabulary                                           */
/* ------------------------------------------------------------------ */

typedef struct background_attachment_row {
    robo_css_background_attachment_t value;
    const char *keyword;
} background_attachment_row_t;

static const background_attachment_row_t background_attachment_vocabulary[] = {
    { ROBO_CSS_BACKGROUND_ATTACHMENT_SCROLL, "scroll" },
    { ROBO_CSS_BACKGROUND_ATTACHMENT_FIXED,  "fixed" }
};

#define BACKGROUND_ATTACHMENT_VOCABULARY_COUNT \
    (sizeof(background_attachment_vocabulary) / sizeof(background_attachment_vocabulary[0]))

static const background_attachment_row_t *background_attachment_row_of(
    robo_css_background_attachment_t attachment)
{
    size_t index;

    for (index = 0u; index < BACKGROUND_ATTACHMENT_VOCABULARY_COUNT; index++) {
        if (background_attachment_vocabulary[index].value == attachment) {
            return &background_attachment_vocabulary[index];
        }
    }
    return NULL;
}

int robo_css_background_attachment_is_known(robo_css_background_attachment_t attachment)
{
    return background_attachment_row_of(attachment) != NULL;
}

const char *robo_css_background_attachment_name(robo_css_background_attachment_t attachment)
{
    const background_attachment_row_t *row = background_attachment_row_of(attachment);

    return row ? row->keyword : "";
}

size_t robo_css_background_attachment_count(void)
{
    return BACKGROUND_ATTACHMENT_VOCABULARY_COUNT;
}

robo_css_background_attachment_t robo_css_background_attachment_from_slice(
    robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return ROBO_CSS_BACKGROUND_ATTACHMENT_SCROLL;
    }
    for (index = 0u; index < BACKGROUND_ATTACHMENT_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                          background_attachment_vocabulary[index].keyword)) {
            return background_attachment_vocabulary[index].value;
        }
    }
    return ROBO_CSS_BACKGROUND_ATTACHMENT_SCROLL;
}

robo_css_background_attachment_t robo_css_background_attachment_from_text(
    const char *keyword)
{
    if (!keyword) {
        return ROBO_CSS_BACKGROUND_ATTACHMENT_SCROLL;
    }
    return robo_css_background_attachment_from_slice(
        robo_css_slice_from_bytes((const uint8_t *)keyword, strlen(keyword)));
}

int robo_css_background_attachment_slice_is_known(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < BACKGROUND_ATTACHMENT_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                          background_attachment_vocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

robo_result_t robo_css_background_attachment_write(robo_css_background_attachment_t attachment,
                                                   char *buffer,
                                                   size_t capacity,
                                                   size_t *out_length,
                                                   robo_css_diag_t *diag)
{
    const background_attachment_row_t *row;
    size_t length;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *out_length = 0u;
    row = background_attachment_row_of(attachment);
    if (!row) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "not a background attachment value");
    }
    length = strlen(row->keyword);
    if (length >= capacity) {
        *out_length = length;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                             "the attachment keyword does not fit");
    }
    memcpy(buffer, row->keyword, length + 1u);
    *out_length = length;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The edge                                                            */
/* ------------------------------------------------------------------ */

/*
 * One table, two axes: the axis picks the *word*, so `START` is `left`
 * horizontally and `top` vertically. Spelling is looked up rather than built,
 * because a caller printing `left bottom` must get `bottom` and not a word
 * assembled out of the axis's name.
 */
static const char *const background_edge_names[ROBO_CSS_BACKGROUND_AXIS_COUNT]
                                             [ROBO_CSS_BACKGROUND_EDGE_COUNT] = {
    /* horizontal */ { "center", "left", "right"  },
    /* vertical   */ { "center", "top",  "bottom" }
};

int robo_css_background_edge_is_known(robo_css_background_edge_t edge)
{
    return edge == ROBO_CSS_BACKGROUND_EDGE_CENTER ||
           edge == ROBO_CSS_BACKGROUND_EDGE_START ||
           edge == ROBO_CSS_BACKGROUND_EDGE_END;
}

const char *robo_css_background_edge_name(robo_css_background_edge_t edge,
                                          robo_css_background_axis_t axis)
{
    if (!robo_css_background_edge_is_known(edge) ||
        !robo_css_background_axis_is_known(axis)) {
        return "";
    }
    return background_edge_names[(size_t)axis][(size_t)edge];
}

robo_css_background_edge_t robo_css_background_edge_from_slice(robo_css_slice_t keyword,
                                                               robo_css_background_axis_t axis)
{
    robo_css_background_edge_t edge;

    if (!robo_css_background_axis_is_known(axis)) {
        return ROBO_CSS_BACKGROUND_EDGE_CENTER;
    }
    for (edge = (robo_css_background_edge_t)0;
         (int)edge < (int)ROBO_CSS_BACKGROUND_EDGE_COUNT;
         edge = (robo_css_background_edge_t)((int)edge + 1)) {
        if (robo_css_bytes_equal_ascii_ci(
                keyword.bytes, keyword.length,
                background_edge_names[(size_t)axis][(size_t)edge])) {
            return edge;
        }
    }
    /* Not an edge on *this* axis — a different fact from "not an edge", and why
     * the caller asks the `_is_known` door first. */
    return ROBO_CSS_BACKGROUND_EDGE_CENTER;
}

int robo_css_background_edge_slice_is_known(robo_css_slice_t keyword,
                                            robo_css_background_axis_t axis)
{
    robo_css_background_edge_t edge;

    if (!robo_css_background_axis_is_known(axis) ||
        robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (edge = (robo_css_background_edge_t)0;
         (int)edge < (int)ROBO_CSS_BACKGROUND_EDGE_COUNT;
         edge = (robo_css_background_edge_t)((int)edge + 1)) {
        if (robo_css_bytes_equal_ascii_ci(
                keyword.bytes, keyword.length,
                background_edge_names[(size_t)axis][(size_t)edge])) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* A position component                                                */
/* ------------------------------------------------------------------ */

int robo_css_background_component_kind_is_known(robo_css_background_component_kind_t kind)
{
    return kind == ROBO_CSS_BACKGROUND_COMPONENT_NONE ||
           kind == ROBO_CSS_BACKGROUND_COMPONENT_EDGE ||
           kind == ROBO_CSS_BACKGROUND_COMPONENT_LENGTH ||
           kind == ROBO_CSS_BACKGROUND_COMPONENT_PERCENTAGE;
}

const char *robo_css_background_component_kind_name(robo_css_background_component_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_BACKGROUND_COMPONENT_NONE:
        return "none";
    case ROBO_CSS_BACKGROUND_COMPONENT_EDGE:
        return "edge";
    case ROBO_CSS_BACKGROUND_COMPONENT_LENGTH:
        return "length";
    case ROBO_CSS_BACKGROUND_COMPONENT_PERCENTAGE:
        return "percentage";
    default:
        return "unknown";
    }
}

robo_css_background_component_t robo_css_background_component_none(void)
{
    robo_css_background_component_t component;

    memset(&component, 0, sizeof(component));
    component.kind = ROBO_CSS_BACKGROUND_COMPONENT_NONE;
    component.edge = ROBO_CSS_BACKGROUND_EDGE_CENTER;
    component.numeric = robo_css_numeric_none();
    return component;
}

int robo_css_background_component_is_present(robo_css_background_component_t component)
{
    return component.kind != ROBO_CSS_BACKGROUND_COMPONENT_NONE;
}

int robo_css_background_component_equals(robo_css_background_component_t a,
                                         robo_css_background_component_t b)
{
    /* Two absent components are *not* equal: "no component" is not a value, and a
     * caller that wanted "neither is set" has is_present for that question. */
    if (!robo_css_background_component_is_present(a) ||
        !robo_css_background_component_is_present(b)) {
        return 0;
    }
    if (a.kind != b.kind) {
        /* Kind-aware, so `left` and `0px` are different however equal they look
         * once something resolves them, and a length never equals a percentage
         * even at the same magnitude. */
        return 0;
    }
    if (a.kind == ROBO_CSS_BACKGROUND_COMPONENT_EDGE) {
        return a.edge == b.edge;
    }
    return robo_css_numeric_equals(a.numeric, b.numeric);
}

robo_result_t robo_css_background_component_write(robo_css_background_component_t component,
                                                  robo_css_background_axis_t axis,
                                                  char *buffer,
                                                  size_t capacity,
                                                  size_t *out_length,
                                                  robo_css_diag_t *diag)
{
    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *out_length = 0u;
    if (!robo_css_background_component_is_present(component)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "there is no component to write");
    }
    if (component.kind == ROBO_CSS_BACKGROUND_COMPONENT_EDGE) {
        const char *keyword;
        size_t length;

        if (!robo_css_background_axis_is_known(axis)) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                                 "an edge is spelled by its axis");
        }
        keyword = robo_css_background_edge_name(component.edge, axis);
        length = strlen(keyword);
        if (length == 0u) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                                 "not an edge on that axis");
        }
        if (length >= capacity) {
            *out_length = length;
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                                 "the edge keyword does not fit");
        }
        memcpy(buffer, keyword, length + 1u);
        *out_length = length;
        return ROBO_OK;
    }
    /* A length or a percentage: one canonical spelling, shared with every other
     * numeric value in this subsystem. */
    return robo_css_numeric_write(component.numeric, buffer, capacity, out_length, diag);
}

/* ------------------------------------------------------------------ */
/* The position pair                                                   */
/* ------------------------------------------------------------------ */

robo_css_background_position_t robo_css_background_position_none(void)
{
    robo_css_background_position_t position;

    position.horizontal = robo_css_background_component_none();
    position.vertical = robo_css_background_component_none();
    return position;
}

int robo_css_background_position_is_present(robo_css_background_position_t position)
{
    /* Both halves, because both halves are what the value *is*: a half-filled
     * position is a state this module holds only while parsing, and a caller that
     * could hold one would have to remember which half was missing. */
    return robo_css_background_component_is_present(position.horizontal) &&
           robo_css_background_component_is_present(position.vertical);
}

int robo_css_background_position_equals(robo_css_background_position_t a,
                                        robo_css_background_position_t b)
{
    if (!robo_css_background_position_is_present(a) ||
        !robo_css_background_position_is_present(b)) {
        return 0;
    }
    return robo_css_background_component_equals(a.horizontal, b.horizontal) &&
           robo_css_background_component_equals(a.vertical, b.vertical);
}

robo_result_t robo_css_background_position_write(robo_css_background_position_t position,
                                                 char *buffer,
                                                 size_t capacity,
                                                 size_t *out_length,
                                                 robo_css_diag_t *diag)
{
    char horizontal[ROBO_CSS_NUMERIC_TEXT_MAX];
    char vertical[ROBO_CSS_NUMERIC_TEXT_MAX];
    size_t horizontal_length = 0u;
    size_t vertical_length = 0u;
    robo_result_t result;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *out_length = 0u;
    if (!robo_css_background_position_is_present(position)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "there is no position to write");
    }
    result = robo_css_background_component_write(
        position.horizontal, ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL, horizontal,
        sizeof(horizontal), &horizontal_length, diag);
    if (result != ROBO_OK) {
        return result;
    }
    result = robo_css_background_component_write(
        position.vertical, ROBO_CSS_BACKGROUND_AXIS_VERTICAL, vertical,
        sizeof(vertical), &vertical_length, diag);
    if (result != ROBO_OK) {
        return result;
    }
    /* Both components, always, separated by one space: the computed value's own
     * spelling whatever the declaration looked like — so `center` prints
     * `center center`, and two equal positions cannot print differently. */
    if (horizontal_length + 1u + vertical_length >= capacity) {
        *out_length = horizontal_length + 1u + vertical_length;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                             "the position does not fit");
    }
    memcpy(buffer, horizontal, horizontal_length);
    buffer[horizontal_length] = ' ';
    memcpy(buffer + horizontal_length + 1u, vertical, vertical_length + 1u);
    *out_length = horizontal_length + 1u + vertical_length;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The size pair                                                       */
/* ------------------------------------------------------------------ */

int robo_css_background_size_kind_is_known(robo_css_background_size_kind_t kind)
{
    return kind == ROBO_CSS_BACKGROUND_SIZE_AUTO ||
           kind == ROBO_CSS_BACKGROUND_SIZE_COVER ||
           kind == ROBO_CSS_BACKGROUND_SIZE_CONTAIN ||
           kind == ROBO_CSS_BACKGROUND_SIZE_PAIR;
}

const char *robo_css_background_size_kind_name(robo_css_background_size_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_BACKGROUND_SIZE_AUTO:
        return "auto";
    case ROBO_CSS_BACKGROUND_SIZE_COVER:
        return "cover";
    case ROBO_CSS_BACKGROUND_SIZE_CONTAIN:
        return "contain";
    case ROBO_CSS_BACKGROUND_SIZE_PAIR:
        return "pair";
    default:
        return "unknown";
    }
}

robo_css_background_size_t robo_css_background_size_none(void)
{
    robo_css_background_size_t size;

    memset(&size, 0, sizeof(size));
    size.kind = ROBO_CSS_BACKGROUND_SIZE_AUTO;
    size.width.is_auto = 0;
    size.width.numeric = robo_css_numeric_none();
    size.height.is_auto = 0;
    size.height.numeric = robo_css_numeric_none();
    return size;
}

int robo_css_background_size_is_present(robo_css_background_size_t size)
{
    /* A cleared record is indistinguishable from `auto` — the initial value — so
     * a caller reads this through the record that holds it, exactly as it does
     * for the display and positioning vocabularies. What this asks is that the
     * value is *coherent*: a whole-value keyword, or a pair with a real
     * component in at least one half. */
    if (!robo_css_background_size_kind_is_known(size.kind)) {
        return 0;
    }
    if (size.kind != ROBO_CSS_BACKGROUND_SIZE_PAIR) {
        return 1;
    }
    return size.width.is_auto || robo_css_numeric_is_present(size.width.numeric) ||
           size.height.is_auto || robo_css_numeric_is_present(size.height.numeric);
}

int robo_css_background_size_equals(robo_css_background_size_t a,
                                    robo_css_background_size_t b)
{
    if (!robo_css_background_size_is_present(a) ||
        !robo_css_background_size_is_present(b)) {
        return 0;
    }
    if (a.kind != b.kind) {
        return 0;
    }
    if (a.kind != ROBO_CSS_BACKGROUND_SIZE_PAIR) {
        return 1;
    }
    /* Component-wise, and `auto` is a flag rather than a numeric kind — so
     * `20px auto` differs from `20px 0px`, which is the distinction the pair
     * exists to keep. */
    if (a.width.is_auto != b.width.is_auto ||
        a.height.is_auto != b.height.is_auto) {
        return 0;
    }
    if (!a.width.is_auto && !robo_css_numeric_equals(a.width.numeric, b.width.numeric)) {
        return 0;
    }
    if (!a.height.is_auto && !robo_css_numeric_equals(a.height.numeric, b.height.numeric)) {
        return 0;
    }
    return 1;
}

robo_result_t robo_css_background_size_write(robo_css_background_size_t size,
                                             char *buffer,
                                             size_t capacity,
                                             size_t *out_length,
                                             robo_css_diag_t *diag)
{
    char width[ROBO_CSS_NUMERIC_TEXT_MAX];
    char height[ROBO_CSS_NUMERIC_TEXT_MAX];
    size_t width_length = 0u;
    size_t height_length = 0u;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *out_length = 0u;
    if (!robo_css_background_size_is_present(size)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "there is no size to write");
    }
    if (size.kind != ROBO_CSS_BACKGROUND_SIZE_PAIR) {
        const char *keyword = robo_css_background_size_kind_name(size.kind);
        size_t keyword_length = strlen(keyword);

        if (keyword_length >= capacity) {
            *out_length = keyword_length;
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                                 "the size keyword does not fit");
        }
        memcpy(buffer, keyword, keyword_length + 1u);
        *out_length = keyword_length;
        return ROBO_OK;
    }
    if (size.width.is_auto) {
        memcpy(width, "auto", 5u);
        width_length = 4u;
    } else {
        robo_result_t result = robo_css_numeric_write(size.width.numeric, width,
                                                      sizeof(width), &width_length, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    if (size.height.is_auto) {
        memcpy(height, "auto", 5u);
        height_length = 4u;
    } else {
        robo_result_t result = robo_css_numeric_write(size.height.numeric, height,
                                                      sizeof(height), &height_length, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    if (width_length + 1u + height_length >= capacity) {
        *out_length = width_length + 1u + height_length;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                             "the size does not fit");
    }
    memcpy(buffer, width, width_length);
    buffer[width_length] = ' ';
    memcpy(buffer + width_length + 1u, height, height_length + 1u);
    *out_length = width_length + 1u + height_length;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Reading components out of a value                                   */
/* ------------------------------------------------------------------ */

/** The @p wanted-th significant component of @p value, or NULL. */
static const robo_css_value_t *background_component_at(const robo_css_value_t *value,
                                                       size_t wanted)
{
    size_t seen = 0u;
    size_t index;

    if (!value || value->kind == ROBO_CSS_VALUE_KIND_NONE) {
        return NULL;
    }
    if (value->kind != ROBO_CSS_VALUE_KIND_LIST) {
        return wanted == 0u && !robo_css_value_is_trivia(value) ? value : NULL;
    }
    for (index = 0u; index < value->children.count; index++) {
        const robo_css_value_t *child = &value->children.items[index];

        if (robo_css_value_is_trivia(child)) {
            continue;
        }
        if (seen == wanted) {
            return child;
        }
        seen++;
    }
    return NULL;
}

/** Number of significant components in @p value. */
static size_t background_component_count(const robo_css_value_t *value)
{
    size_t count = 0u;
    size_t index;

    if (!value || value->kind == ROBO_CSS_VALUE_KIND_NONE) {
        return 0u;
    }
    if (value->kind != ROBO_CSS_VALUE_KIND_LIST) {
        return robo_css_value_is_trivia(value) ? 0u : 1u;
    }
    for (index = 0u; index < value->children.count; index++) {
        if (!robo_css_value_is_trivia(&value->children.items[index])) {
            count++;
        }
    }
    return count;
}

/** The lowercased identifier @p value spells, or 0 when it is not an identifier. */
static int background_identifier(const robo_css_value_t *value,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *out_length)
{
    robo_css_token_t token;

    if (!value || value->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        return 0;
    }
    token = robo_css_value_token(value);
    if (token.type != ROBO_CSS_TOKEN_IDENT) {
        return 0;
    }
    return robo_css_token_keyword_text(&token, buffer, capacity, out_length);
}

/** Non-zero when @p value is a function whose substitution could change a count. */
static int background_component_is_substitution(const robo_css_value_t *value)
{
    return robo_css_value_function_named(value, "var") ||
           robo_css_value_function_named(value, "env");
}

/** Non-zero when @p value is the `/` delimiter. */
static int background_component_is_slash(const robo_css_value_t *value)
{
    robo_css_slice_t text;

    if (!value || value->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        return 0;
    }
    if (robo_css_value_token(value).type != ROBO_CSS_TOKEN_DELIM) {
        return 0;
    }
    text = robo_css_value_token_text(value);
    return text.length == 1u && text.bytes && text.bytes[0] == (uint8_t)'/';
}

/**
 * Whether @p keyword is `auto`, ASCII case-insensitively.
 *
 * Spelled once, because the size grammar asks it in three places (a whole-value
 * size, a component after a `/`, and a component of a pair) and three comparisons
 * are three chances for one of them to be case-sensitive.
 */
static int background_keyword_is(const char *keyword, size_t length, const char *wanted)
{
    return robo_css_bytes_equal_ascii_ci((const uint8_t *)keyword, length, wanted);
}

/**
 * What one component of a position or size value turned out to be.
 *
 * `named_axis` is the field the position grammar turns on, and it is why this is a
 * record rather than a return code: a component that is an edge keyword *knows
 * which axis it belongs to*, and one that is `center` or an offset does not. That
 * is the whole difference between `left top` (one edge per axis, in either order)
 * and `left 10px` (the edge-relative form, which this build refuses) — and it
 * cannot be recovered from the component alone, because `left` and `top` are both
 * the START edge.
 */
typedef struct background_reading {
    robo_css_background_component_t component;
    /** -1 when the component fits either axis; 0 horizontal; 1 vertical. */
    int named_axis;
    int is_auto;
} background_reading_t;

/**
 * @p numeric as the one spelling an absolute length has.
 *
 * `1in` and `96px` are the same length, so the pair holds one of them and it is
 * the reference pixel — the same conversion, by the same table, a length-shaped
 * property's computed value goes through (RoboCSSComputedValue.c). It is here
 * rather than at the write, because a value has one spelling and the write is not
 * the only reader: whoever measures a box reads the pair's numeric directly.
 *
 * Whether a unit converts at all is the unit module's answer
 * (`robo_css_unit_is_absolute`) rather than a list restated here, so a unit added
 * there converts here the day it is added. A *relative* length keeps its unit and
 * its number — `2em` is not a length until something says what the font size is —
 * and a percentage is not a length at all.
 */
static robo_css_numeric_t background_canonical_length(robo_css_numeric_t numeric)
{
    if (robo_css_numeric_is_present(numeric) &&
        numeric.kind == ROBO_CSS_NUMERIC_LENGTH &&
        robo_css_unit_is_absolute(numeric.unit)) {
        numeric.number = robo_css_unit_to_px(numeric.number, numeric.unit);
        numeric.unit = ROBO_CSS_UNIT_PX;
    }
    return numeric;
}

/**
 * Read @p value as one position-or-size component.
 *
 * One reader for both grammars, because the four questions it asks — is this an
 * edge, is this a length, is this a percentage, is this `auto` — are the same
 * questions both ask, and a second copy is how `20px` comes to be a length for one
 * property and something else for the other. What differs between the two is which
 * of the answers each *accepts*, and that is the caller's decision.
 *
 * @param allow_auto whether `auto` is a component here. A position has no `auto`;
 *                   a size's components do.
 * @return one of @ref robo_css_background_parse_t, and the reading in @p out.
 */
static robo_css_background_parse_t background_read_component(
    const robo_css_value_t *value,
    int allow_auto,
    background_reading_t *out)
{
    robo_css_numeric_t numeric = robo_css_numeric_none();
    robo_css_numeric_parse_t numeric_outcome;
    char keyword[64];
    size_t keyword_length = 0u;
    robo_css_slice_t text;

    memset(out, 0, sizeof(*out));
    out->component = robo_css_background_component_none();
    out->named_axis = -1;

    if (background_identifier(value, keyword, sizeof(keyword), &keyword_length)) {
        text = robo_css_slice_from_bytes((const uint8_t *)keyword, keyword_length);
        if (allow_auto && background_keyword_is(keyword, keyword_length, "auto")) {
            out->is_auto = 1;
            return ROBO_CSS_BACKGROUND_PARSE_SIZE;
        }
        if (robo_css_background_edge_slice_is_known(
                text, ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL)) {
            out->component.kind = ROBO_CSS_BACKGROUND_COMPONENT_EDGE;
            out->component.edge = robo_css_background_edge_from_slice(
                text, ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL);
            /* `center` is the one edge that fits both axes, and it is the whole
             * reason this field exists. */
            out->named_axis = out->component.edge == ROBO_CSS_BACKGROUND_EDGE_CENTER
                                  ? -1 : 0;
            return ROBO_CSS_BACKGROUND_PARSE_POSITION;
        }
        if (robo_css_background_edge_slice_is_known(
                text, ROBO_CSS_BACKGROUND_AXIS_VERTICAL)) {
            out->component.kind = ROBO_CSS_BACKGROUND_COMPONENT_EDGE;
            out->component.edge = robo_css_background_edge_from_slice(
                text, ROBO_CSS_BACKGROUND_AXIS_VERTICAL);
            out->named_axis = out->component.edge == ROBO_CSS_BACKGROUND_EDGE_CENTER
                                  ? -1 : 1;
            return ROBO_CSS_BACKGROUND_PARSE_POSITION;
        }
        /* An identifier, and neither an edge nor `auto`: `banana`, `lefty`. */
        return ROBO_CSS_BACKGROUND_PARSE_INVALID;
    }

    numeric_outcome = robo_css_numeric_from_value(value, &numeric);
    if (numeric_outcome == ROBO_CSS_NUMERIC_PARSE_DEFERRED) {
        /* `calc()`, `var()`, `min()`…: legal CSS this build does not evaluate. */
        return ROBO_CSS_BACKGROUND_PARSE_DEFERRED;
    }
    if (numeric_outcome != ROBO_CSS_NUMERIC_PARSE_RESOLVED) {
        return ROBO_CSS_BACKGROUND_PARSE_INVALID;
    }
    if (numeric.kind == ROBO_CSS_NUMERIC_LENGTH) {
        out->component.kind = ROBO_CSS_BACKGROUND_COMPONENT_LENGTH;
        out->component.edge = ROBO_CSS_BACKGROUND_EDGE_CENTER;
        out->component.numeric = background_canonical_length(numeric);
        return ROBO_CSS_BACKGROUND_PARSE_POSITION;
    }
    if (numeric.kind == ROBO_CSS_NUMERIC_PERCENTAGE) {
        out->component.kind = ROBO_CSS_BACKGROUND_COMPONENT_PERCENTAGE;
        out->component.edge = ROBO_CSS_BACKGROUND_EDGE_CENTER;
        out->component.numeric = numeric;
        return ROBO_CSS_BACKGROUND_PARSE_POSITION;
    }
    /* A bare number. It is a number, and it is not a position: CSS's
     * `<length-percentage>` has no unitless branch, so `background-position: 10`
     * is a mistake rather than ten pixels. */
    return ROBO_CSS_BACKGROUND_PARSE_INVALID;
}

/** The CENTER edge on @p axis, as a component. */
static robo_css_background_component_t background_center_component(void)
{
    robo_css_background_component_t component = robo_css_background_component_none();

    component.kind = ROBO_CSS_BACKGROUND_COMPONENT_EDGE;
    component.edge = ROBO_CSS_BACKGROUND_EDGE_CENTER;
    return component;
}

/* ------------------------------------------------------------------ */
/* The parse outcomes                                                  */
/* ------------------------------------------------------------------ */

/*
 * Four rows, and the third is the one that keeps this phase honest: DEFERRED is
 * "legal CSS this build has no representation for" and INVALID is "this means
 * nothing here at all". A diagnostic is addressed to an author, and an author who
 * wrote `background-position: var(--p)` needs a different sentence from one who
 * wrote `background-position: banana`.
 */
static const char *const background_parse_names[ROBO_CSS_BACKGROUND_PARSE_COUNT] = {
    "invalid", "position", "size", "deferred"
};

int robo_css_background_parse_is_known(robo_css_background_parse_t outcome)
{
    return outcome >= ROBO_CSS_BACKGROUND_PARSE_INVALID &&
           (size_t)outcome < (size_t)ROBO_CSS_BACKGROUND_PARSE_COUNT;
}

const char *robo_css_background_parse_name(robo_css_background_parse_t outcome)
{
    return robo_css_background_parse_is_known(outcome)
               ? background_parse_names[(size_t)outcome]
               : "unknown";
}

/* ------------------------------------------------------------------ */
/* Parsing a position                                                  */
/* ------------------------------------------------------------------ */

robo_css_background_parse_t robo_css_background_position_from_value(
    const robo_css_value_t *specified,
    robo_css_background_position_t *out)
{
    background_reading_t first_reading;
    background_reading_t second_reading;
    robo_css_background_parse_t outcome;
    const robo_css_value_t *first;
    const robo_css_value_t *second;
    size_t count;

    if (!out) {
        return ROBO_CSS_BACKGROUND_PARSE_INVALID;
    }
    *out = robo_css_background_position_none();
    count = background_component_count(specified);
    if (count == 0u || count > 2u) {
        /* Three and four components are the edge-relative grammar, whose *count*
         * decides the meaning; this build does not represent it. */
        return ROBO_CSS_BACKGROUND_PARSE_INVALID;
    }
    first = background_component_at(specified, 0u);
    second = count == 2u ? background_component_at(specified, 1u) : NULL;

    outcome = background_read_component(first, 0, &first_reading);
    if (outcome == ROBO_CSS_BACKGROUND_PARSE_DEFERRED) {
        return ROBO_CSS_BACKGROUND_PARSE_DEFERRED;
    }
    if (outcome != ROBO_CSS_BACKGROUND_PARSE_POSITION) {
        return ROBO_CSS_BACKGROUND_PARSE_INVALID;
    }
    if (count == 1u) {
        /* The single-component form, folded: a keyword that names the vertical
         * axis (`top`) leaves the horizontal one at centre, and everything else —
         * an offset, `left`, `right`, `center` — is the horizontal one with the
         * vertical left at centre. That asymmetry is CSS's, and it is why this is
         * a branch on `named_axis` rather than the same assignment twice. */
        if (first_reading.named_axis == 1) {
            out->horizontal = background_center_component();
            out->vertical = first_reading.component;
        } else {
            out->horizontal = first_reading.component;
            out->vertical = background_center_component();
        }
        return ROBO_CSS_BACKGROUND_PARSE_POSITION;
    }

    outcome = background_read_component(second, 0, &second_reading);
    if (outcome == ROBO_CSS_BACKGROUND_PARSE_DEFERRED) {
        return ROBO_CSS_BACKGROUND_PARSE_DEFERRED;
    }
    if (outcome != ROBO_CSS_BACKGROUND_PARSE_POSITION) {
        return ROBO_CSS_BACKGROUND_PARSE_INVALID;
    }

    if (first_reading.component.kind == ROBO_CSS_BACKGROUND_COMPONENT_EDGE &&
        second_reading.component.kind == ROBO_CSS_BACKGROUND_COMPONENT_EDGE) {
        int first_home = first_reading.named_axis;
        int second_home = second_reading.named_axis;

        if (first_home == 0 && second_home == 0) {
            /* `left right`: two horizontal edges and no vertical one. */
            return ROBO_CSS_BACKGROUND_PARSE_INVALID;
        }
        if (first_home == 1 && second_home == 1) {
            return ROBO_CSS_BACKGROUND_PARSE_INVALID;
        }
        if (first_home == 1 || second_home == 0) {
            /* Either the first names the vertical axis (`top left`) or the second
             * names the horizontal one (`center left`), and in both readings the
             * second component is the horizontal one. */
            out->horizontal = second_reading.component;
            out->vertical = first_reading.component;
        } else {
            /* `left top`, `left center`, `center center`, `center top`. */
            out->horizontal = first_reading.component;
            out->vertical = second_reading.component;
        }
        return ROBO_CSS_BACKGROUND_PARSE_POSITION;
    }

    if (first_reading.component.kind != ROBO_CSS_BACKGROUND_COMPONENT_EDGE &&
        second_reading.component.kind != ROBO_CSS_BACKGROUND_COMPONENT_EDGE) {
        /* Two offsets: horizontal first, vertical second, in the order written. */
        out->horizontal = first_reading.component;
        out->vertical = second_reading.component;
        return ROBO_CSS_BACKGROUND_PARSE_POSITION;
    }

    /* One keyword and one offset. `center 10px` is a position — horizontal centre,
     * ten pixels down — and `left 10px` is not: that is the edge-relative form,
     * where the offset hangs off the named edge, and there the *order* carries the
     * meaning. Reading it as `left` + `10px` would be a plausible wrong answer, so
     * it is refused instead. */
    {
        background_reading_t edge_reading =
            first_reading.component.kind == ROBO_CSS_BACKGROUND_COMPONENT_EDGE
                ? first_reading : second_reading;

        if (edge_reading.component.edge != ROBO_CSS_BACKGROUND_EDGE_CENTER) {
            return ROBO_CSS_BACKGROUND_PARSE_INVALID;
        }
    }
    out->horizontal = first_reading.component;
    out->vertical = second_reading.component;
    return ROBO_CSS_BACKGROUND_PARSE_POSITION;
}

/* ------------------------------------------------------------------ */
/* Parsing a size                                                      */
/* ------------------------------------------------------------------ */

robo_css_background_parse_t robo_css_background_size_from_value(
    const robo_css_value_t *specified,
    robo_css_background_size_t *out)
{
    robo_css_background_size_t size = robo_css_background_size_none();
    background_reading_t first_reading;
    background_reading_t second_reading;
    robo_css_background_parse_t outcome;
    const robo_css_value_t *first;
    const robo_css_value_t *second;
    size_t count;

    if (!out) {
        return ROBO_CSS_BACKGROUND_PARSE_INVALID;
    }
    *out = robo_css_background_size_none();
    count = background_component_count(specified);
    if (count == 0u || count > 2u) {
        return ROBO_CSS_BACKGROUND_PARSE_INVALID;
    }
    first = background_component_at(specified, 0u);
    second = count == 2u ? background_component_at(specified, 1u) : NULL;

    if (count == 1u) {
        char keyword[64];
        size_t keyword_length = 0u;

        if (background_identifier(first, keyword, sizeof(keyword), &keyword_length)) {
            /* `cover` and `contain` are whole values, not components: neither is a
             * width, and a pair of them would be a value the property does not
             * have. */
            if (background_keyword_is(keyword, keyword_length, "cover")) {
                size.kind = ROBO_CSS_BACKGROUND_SIZE_COVER;
                *out = size;
                return ROBO_CSS_BACKGROUND_PARSE_SIZE;
            }
            if (background_keyword_is(keyword, keyword_length, "contain")) {
                size.kind = ROBO_CSS_BACKGROUND_SIZE_CONTAIN;
                *out = size;
                return ROBO_CSS_BACKGROUND_PARSE_SIZE;
            }
        }
    }

    outcome = background_read_component(first, 1, &first_reading);
    if (outcome == ROBO_CSS_BACKGROUND_PARSE_DEFERRED) {
        return ROBO_CSS_BACKGROUND_PARSE_DEFERRED;
    }
    if (outcome != ROBO_CSS_BACKGROUND_PARSE_SIZE &&
        outcome != ROBO_CSS_BACKGROUND_PARSE_POSITION) {
        return ROBO_CSS_BACKGROUND_PARSE_INVALID;
    }
    if (first_reading.is_auto) {
        size.width.is_auto = 1;
    } else if (first_reading.component.kind == ROBO_CSS_BACKGROUND_COMPONENT_EDGE) {
        /* An edge is not a size: `background-size: left` is a mistake. */
        return ROBO_CSS_BACKGROUND_PARSE_INVALID;
    } else {
        size.width.numeric = first_reading.component.numeric;
    }

    if (count == 1u) {
        /* CSS: a single width leaves the height `auto`. */
        size.height.is_auto = 1;
    } else {
        outcome = background_read_component(second, 1, &second_reading);
        if (outcome == ROBO_CSS_BACKGROUND_PARSE_DEFERRED) {
            return ROBO_CSS_BACKGROUND_PARSE_DEFERRED;
        }
        if (outcome != ROBO_CSS_BACKGROUND_PARSE_SIZE &&
            outcome != ROBO_CSS_BACKGROUND_PARSE_POSITION) {
            return ROBO_CSS_BACKGROUND_PARSE_INVALID;
        }
        if (second_reading.is_auto) {
            size.height.is_auto = 1;
        } else if (second_reading.component.kind == ROBO_CSS_BACKGROUND_COMPONENT_EDGE) {
            return ROBO_CSS_BACKGROUND_PARSE_INVALID;
        } else {
            size.height.numeric = second_reading.component.numeric;
        }
    }

    if (size.width.is_auto && size.height.is_auto) {
        /* `auto auto` and `auto` are one computed value with one spelling, so they
         * are one *value* here rather than two that print differently. */
        size.kind = ROBO_CSS_BACKGROUND_SIZE_AUTO;
        size.width.is_auto = 0;
        size.height.is_auto = 0;
        *out = size;
        return ROBO_CSS_BACKGROUND_PARSE_SIZE;
    }
    size.kind = ROBO_CSS_BACKGROUND_SIZE_PAIR;
    *out = size;
    return ROBO_CSS_BACKGROUND_PARSE_SIZE;
}

/* ------------------------------------------------------------------ */
/* The shorthand                                                       */
/* ------------------------------------------------------------------ */

/*
 * The six longhands, in the order CSS serialises `background`: image, position,
 * size, repeat, attachment, colour. The order is load-bearing for the *output
 * array* — the caller indexes parts by the same order — so it is written once.
 */
static const char *const background_longhands[ROBO_CSS_BACKGROUND_LONGHAND_COUNT] = {
    "background-image",
    "background-position",
    "background-size",
    "background-repeat",
    "background-attachment",
    "background-color"
};

/** Index of a longhand in @ref background_longhands. */
#define BACKGROUND_PART_IMAGE 0u
#define BACKGROUND_PART_POSITION 1u
#define BACKGROUND_PART_SIZE 2u
#define BACKGROUND_PART_REPEAT 3u
#define BACKGROUND_PART_ATTACHMENT 4u
#define BACKGROUND_PART_COLOR 5u

/** How many components each longhand can take from one layer. */
static const size_t background_part_capacity[ROBO_CSS_BACKGROUND_LONGHAND_COUNT] = {
    1u, /* image      */
    2u, /* position   */
    2u, /* size       */
    1u, /* repeat     */
    1u, /* attachment */
    1u  /* colour     */
};

size_t robo_css_background_longhand_count(void)
{
    return ROBO_CSS_BACKGROUND_LONGHAND_COUNT;
}

const char *robo_css_background_longhand_name(size_t index)
{
    return index < ROBO_CSS_BACKGROUND_LONGHAND_COUNT ? background_longhands[index] : NULL;
}

int robo_css_background_is_longhand(robo_css_slice_t name)
{
    size_t index;

    if (robo_css_slice_is_absent(name) || name.length == 0u) {
        return 0;
    }
    for (index = 0u; index < ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(name.bytes, name.length,
                                          background_longhands[index])) {
            return 1;
        }
    }
    return 0;
}

const char *robo_css_background_shorthand_name(void)
{
    return "background";
}

/**
 * Which longhand @p component belongs to — by *kind*, which is what makes this a
 * different machine from the box model's positional mapping.
 *
 * @param after_slash non-zero when the component follows a `/`, where the size
 *                    grammar is the only one that applies.
 * @param out_part    receives the longhand index.
 * @return 0 when the component belongs to no longhand, in which case @p out_reason
 *         names what was wrong with it.
 */
static int background_classify(const robo_css_value_t *component,
                               int after_slash,
                               size_t *out_part,
                               const char **out_reason)
{
    robo_css_token_t token;
    char keyword[64];
    size_t keyword_length = 0u;
    robo_css_slice_t text;

    *out_reason = "not a background value";

    if (after_slash) {
        /* After a `/`, exactly the size grammar: `auto`, `cover`, `contain`, a
         * length or a percentage — and nothing else, because everything else has
         * a longhand of its own that the `/` does not reach. */
        if (background_identifier(component, keyword, sizeof(keyword), &keyword_length) &&
            (background_keyword_is(keyword, keyword_length, "auto") ||
             background_keyword_is(keyword, keyword_length, "cover") ||
             background_keyword_is(keyword, keyword_length, "contain"))) {
            *out_part = BACKGROUND_PART_SIZE;
            return 1;
        }
        if (component && component->kind == ROBO_CSS_VALUE_KIND_TOKEN) {
            token = robo_css_value_token(component);
            if (token.type == ROBO_CSS_TOKEN_PERCENTAGE ||
                token.type == ROBO_CSS_TOKEN_DIMENSION) {
                *out_part = BACKGROUND_PART_SIZE;
                return 1;
            }
        }
        *out_reason = "not a background size";
        return 0;
    }

    if (component && component->kind == ROBO_CSS_VALUE_KIND_FUNCTION) {
        robo_css_color_t color = robo_css_color_none();

        if (background_component_is_substitution(component)) {
            /* A reference whose substitution can change how many components there
             * are: `--layer: url(a.png) no-repeat` makes one component three, so
             * distributing now would assign the whole reference to one longhand
             * and be wrong on the first real declaration. */
            *out_reason = "a reference whose substitution can change the number of components";
            return 0;
        }
        /* A colour function *is* the colour — `rgb(10, 20, 30)`, `hsl(0 100% 50%)`
         * — and every other function is an image reference this build carries
         * rather than resolves. */
        if (robo_css_color_from_function(component, &color) ==
            ROBO_CSS_COLOR_PARSE_RESOLVED) {
            *out_part = BACKGROUND_PART_COLOR;
            return 1;
        }
        *out_part = BACKGROUND_PART_IMAGE;
        return 1;
    }

    if (!component || component->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        return 0;
    }
    token = robo_css_value_token(component);
    switch (token.type) {
    case ROBO_CSS_TOKEN_IDENT:
        if (!robo_css_token_keyword_text(&token, keyword, sizeof(keyword),
                                         &keyword_length)) {
            return 0;
        }
        text = robo_css_slice_from_bytes((const uint8_t *)keyword, keyword_length);
        if (robo_css_background_repeat_slice_is_known(text)) {
            *out_part = BACKGROUND_PART_REPEAT;
            return 1;
        }
        if (robo_css_background_attachment_slice_is_known(text)) {
            *out_part = BACKGROUND_PART_ATTACHMENT;
            return 1;
        }
        if (background_keyword_is(keyword, keyword_length, "none")) {
            *out_part = BACKGROUND_PART_IMAGE;
            return 1;
        }
        if (background_keyword_is(keyword, keyword_length, "auto") ||
            background_keyword_is(keyword, keyword_length, "cover") ||
            background_keyword_is(keyword, keyword_length, "contain")) {
            /* CSS's grammar puts these in `<bg-size>`, and `<bg-size>` appears
             * only after a `/`. So `background: cover` is not a shorthand missing
             * its other parts — it is a declaration with a size where a size
             * cannot go. */
            *out_reason = "a size keyword needs a `/`";
            return 0;
        }
        if (background_keyword_is(keyword, keyword_length, "transparent") ||
            background_keyword_is(keyword, keyword_length, "currentcolor")) {
            *out_part = BACKGROUND_PART_COLOR;
            return 1;
        }
        {
            robo_css_color_t named = robo_css_color_none();

            if (robo_css_color_from_name(text, &named)) {
                *out_part = BACKGROUND_PART_COLOR;
                return 1;
            }
        }
        if (robo_css_background_edge_slice_is_known(
                text, ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL) ||
            robo_css_background_edge_slice_is_known(
                text, ROBO_CSS_BACKGROUND_AXIS_VERTICAL)) {
            *out_part = BACKGROUND_PART_POSITION;
            return 1;
        }
        *out_reason = "not a background value";
        return 0;
    case ROBO_CSS_TOKEN_HASH:
        /* The fourth way to spell a colour, and it takes the same road as the
         * other three: `#fff` is a colour the 9.9 module already parses, so
         * `background: #fff` belongs to `background-color` for the same reason
         * `background: white` does. Refusing it would make one spelling of a
         * colour a mistake and another one correct, which is the sort of
         * difference an author has no way to see. */
        {
            robo_css_color_t hex = robo_css_color_none();

            if (robo_css_color_from_value(component, &hex) ==
                ROBO_CSS_COLOR_PARSE_RESOLVED) {
                *out_part = BACKGROUND_PART_COLOR;
                return 1;
            }
        }
        *out_reason = "not a background value";
        return 0;
    case ROBO_CSS_TOKEN_PERCENTAGE:
    case ROBO_CSS_TOKEN_DIMENSION:
        *out_part = BACKGROUND_PART_POSITION;
        return 1;
    case ROBO_CSS_TOKEN_NUMBER:
        /* Unitless, and therefore neither a position nor a size. */
        *out_reason = "a bare number is not a background value";
        return 0;
    case ROBO_CSS_TOKEN_URL:
        /* `url(x.png)` unquoted arrives as a URL token rather than as a function,
         * and it is an image reference like any other. */
        *out_part = BACKGROUND_PART_IMAGE;
        return 1;
    case ROBO_CSS_TOKEN_COMMA:
        *out_reason = "multiple background layers are not represented";
        return 0;
    default:
        return 0;
    }
}

/** Fold the assignments into one list per longhand, or leave KIND_NONE. */
static robo_result_t background_build_parts(
    const robo_css_value_t *assigned[ROBO_CSS_BACKGROUND_LONGHAND_COUNT][2],
    const size_t assigned_count[ROBO_CSS_BACKGROUND_LONGHAND_COUNT],
    robo_css_value_t out_parts[ROBO_CSS_BACKGROUND_LONGHAND_COUNT],
    robo_css_diag_t *diag)
{
    size_t part_index;

    for (part_index = 0u; part_index < ROBO_CSS_BACKGROUND_LONGHAND_COUNT; part_index++) {
        size_t index;
        robo_css_value_t list = ROBO_CSS_CLEARED;

        if (assigned_count[part_index] == 0u) {
            /* KIND_NONE: the shorthand did not mention this longhand, so its
             * initial value applies. Deliberately not supplied here — the initial
             * values are the registry's (see the header). */
            continue;
        }
        robo_css_value_init(&list);
        if (robo_css_value_make_list(&list, diag) != ROBO_OK) {
            robo_css_value_dispose(&list);
            return ROBO_OUT_OF_MEMORY;
        }
        for (index = 0u; index < assigned_count[part_index]; index++) {
            robo_css_value_t copy = ROBO_CSS_CLEARED;
            robo_result_t result;

            robo_css_value_init(&copy);
            result = robo_css_value_copy(&copy, assigned[part_index][index], diag);
            if (result == ROBO_OK) {
                /* Built as a list rather than as the bare component, so the
                 * result has the shape a *declared* longhand's value has — which
                 * is what lets it go through the same normalisers with no special
                 * case. `append` moves, so `copy` owns nothing afterwards. */
                result = robo_css_value_append(&list, &copy, diag);
            }
            if (result != ROBO_OK) {
                robo_css_value_dispose(&copy);
                robo_css_value_dispose(&list);
                return result;
            }
        }
        out_parts[part_index] = list;
    }
    return ROBO_OK;
}

robo_result_t robo_css_background_expand(
    const robo_css_value_t *value,
    robo_css_value_t out_parts[ROBO_CSS_BACKGROUND_LONGHAND_COUNT],
    robo_css_diag_t *diag)
{
    const robo_css_value_t *assigned[ROBO_CSS_BACKGROUND_LONGHAND_COUNT][2];
    size_t assigned_count[ROBO_CSS_BACKGROUND_LONGHAND_COUNT];
    size_t count;
    size_t index;
    size_t part_index;
    size_t slash = 0u;
    int has_slash = 0;
    size_t size_start = 0u;
    size_t size_end = 0u;
    robo_result_t result;

    if (!out_parts) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "the output array is NULL");
    }
    for (part_index = 0u; part_index < ROBO_CSS_BACKGROUND_LONGHAND_COUNT; part_index++) {
        robo_css_value_init(&out_parts[part_index]);
        assigned_count[part_index] = 0u;
        assigned[part_index][0] = NULL;
        assigned[part_index][1] = NULL;
    }
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_INVALID_ARGUMENT,
                             "the shorthand value is NULL");
    }
    count = background_component_count(value);
    if (count == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                             "a background shorthand has no components");
    }

    /* First pass: find the `/` and the size group it binds. */
    for (index = 0u; index < count; index++) {
        if (background_component_is_slash(background_component_at(value, index))) {
            if (has_slash) {
                return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                                     "more than one `/` in a background shorthand");
            }
            has_slash = 1;
            slash = index;
        }
    }
    if (has_slash) {
        const char *reason = NULL;
        size_t part = 0u;

        if (slash == 0u || slash + 1u >= count) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                                 "a `/` needs a position before it and a size after");
        }
        /* CSS's grammar is `<bg-position> [ / <bg-size> ]?` and not the reverse,
         * so the component immediately before the `/` has to be a position. */
        if (!background_classify(background_component_at(value, slash - 1u), 0, &part,
                                 &reason)) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                                 "a `/` must follow a position");
        }
        /* The size is the components immediately after the `/`: one, or two when
         * the second is size-shaped as well. */
        size_start = slash + 1u;
        size_end = size_start;
        if (size_start + 1u < count &&
            background_classify(background_component_at(value, size_start + 1u), 1, &part,
                                &reason)) {
            size_end = size_start + 1u;
        }
    }

    /* Second pass: classify and assign. */
    for (index = 0u; index < count; index++) {
        const robo_css_value_t *component = background_component_at(value, index);
        const char *reason = NULL;
        size_t part = 0u;

        if (background_component_is_slash(component)) {
            continue;
        }
        if (!background_classify(component, has_slash && index >= size_start &&
                                            index <= size_end, &part, &reason)) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                                 reason);
        }
        if (assigned_count[part] >= background_part_capacity[part]) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BACKGROUND, ROBO_NOT_SUPPORTED,
                                 "two components the same longhand cannot take");
        }
        assigned[part][assigned_count[part]] = component;
        assigned_count[part]++;
    }

    result = background_build_parts(assigned, assigned_count, out_parts, diag);
    if (result != ROBO_OK) {
        /* All or nothing: a caller that ignored the result code must not be handed
         * four of a shorthand's six longhands. */
        for (part_index = 0u; part_index < ROBO_CSS_BACKGROUND_LONGHAND_COUNT;
             part_index++) {
            robo_css_value_dispose(&out_parts[part_index]);
            robo_css_value_init(&out_parts[part_index]);
        }
        return result;
    }
    return ROBO_OK;
}
