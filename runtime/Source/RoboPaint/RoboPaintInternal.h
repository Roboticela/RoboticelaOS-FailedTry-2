/*
 * RoboPaint internals (Phase 11.1). Not installed: nothing public includes this.
 *
 * It holds one thing, and it holds it because it is the rule this module's arithmetic
 * has to obey in three places and the rule is not a detail.
 *
 * **Every accumulation is bounded.** RoboPaint's own arithmetic is accumulation: a page
 * origin is an ancestor's origin plus a box's position, a padding box edge is a border
 * box edge plus a border width, and a clip start is the larger of two starts. Each
 * operand is a bounded `int32` and none of the sums is, so every one of them goes
 * through these helpers -- added in 64 bits and saturated at the coordinate's own range.
 *
 * Saturated rather than refused, and the reason is RoboLayout's, restated here because
 * this module makes the same choice for its own sums: there is no document an author can
 * write that some number cannot describe, so a coordinate at the edge of the plane is a
 * wrong answer that is still *drawable*, while a wrapped one is a box at the wrong end
 * of the page and a refusal would fail a document that is merely extreme. Phase 10.15
 * found the cost of getting this wrong the hard way -- in the layout pass's own cursor,
 * where a wrapped extent became a *negative* size and failed the whole pass -- which is
 * why the rule is stated in one place here rather than at each site.
 *
 * Why not call RoboLayout's helper: it is deliberately private to that module
 * (`RoboLayoutInternal.h` is not installed), and reaching into another module's
 * internals is the thing the boundary scan exists to catch. The *rule* is shared; the
 * code is not, and having two copies of a four-line clamp is what the tests are for.
 */

#ifndef ROBO_PAINT_INTERNAL_H
#define ROBO_PAINT_INTERNAL_H

#include "RoboPaint/RoboPaint.h"

#include <stdint.h>

/** The largest coordinate this module will carry: the geometry type's own maximum. */
#define ROBO_PAINT_COORDINATE_MAX ((int32_t)2147483647)

/** The smallest coordinate this module will carry. */
#define ROBO_PAINT_COORDINATE_MIN ((int32_t)(-2147483647 - 1))

/** @p value clamped to the coordinate range. */
static inline int32_t robo_paint_clamp_i64(int64_t value)
{
    if (value > (int64_t)ROBO_PAINT_COORDINATE_MAX) {
        return ROBO_PAINT_COORDINATE_MAX;
    }
    if (value < (int64_t)ROBO_PAINT_COORDINATE_MIN) {
        return ROBO_PAINT_COORDINATE_MIN;
    }
    return (int32_t)value;
}

/** @p a + @p b, saturated. */
static inline int32_t robo_paint_add_saturated(int32_t a, int32_t b)
{
    return robo_paint_clamp_i64((int64_t)a + (int64_t)b);
}

/** @p a - @p b, saturated. */
static inline int32_t robo_paint_sub_saturated(int32_t a, int32_t b)
{
    return robo_paint_clamp_i64((int64_t)a - (int64_t)b);
}

/**
 * An **extent** from a 64-bit value: clamped to `[0, maximum]`.
 *
 * A width or a height is never negative, so a subtraction that came out below zero means
 * the inner layer's insets exceeded the outer one's box -- a `width: 2147483647px` box
 * with a `border: 5px solid` -- and the answer is an empty layer rather than a refusal.
 * This is the shape RoboLayout's own three derived numbers use, and the floor is applied
 * to the result rather than to each term so that the three floors in that module and this
 * one agree.
 */
static inline int32_t robo_paint_extent(int64_t value)
{
    if (value < 0) {
        return 0;
    }
    if (value > (int64_t)ROBO_PAINT_COORDINATE_MAX) {
        return ROBO_PAINT_COORDINATE_MAX;
    }
    return (int32_t)value;
}

/** The smaller of @p a and @p b. */
static inline int32_t robo_paint_min(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

/** The larger of @p a and @p b. */
static inline int32_t robo_paint_max(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

/** The four used border widths of @p borders, indexed by `robo_css_box_side_t`. */
robo_result_t robo_paint_border_widths(const robo_layout_border_edges_t *borders,
                                      int32_t out_widths[ROBO_CSS_BOX_SIDE_COUNT],
                                      robo_paint_diag_t *diag);

/** The four used paddings of @p padding, indexed by `robo_css_box_side_t`. */
robo_result_t robo_paint_padding_widths(const robo_layout_padding_edges_t *padding,
                                       int32_t out_widths[ROBO_CSS_BOX_SIDE_COUNT],
                                       robo_paint_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Text building                                                       */
/* ------------------------------------------------------------------ */

/*
 * A tiny append-only text builder, shared by the two `_write` doors.
 *
 * It exists for one reason: both doors owe a caller the **length they would have
 * needed** when the text does not fit, which is the contract every `_write` in this tree
 * uses, and computing that with `snprintf` would return a truncated buffer and a number
 * the caller has to trust. So the builder keeps counting after the buffer runs out: the
 * bytes stop going in, the length keeps growing, and a refusal can tell a caller exactly
 * how much room to make.
 *
 * No `stdio` anywhere, deliberately: the runtime's guest target has none, and a dump
 * built with `snprintf` would be the one thing in this module that could not compile for
 * the OS it is written for.
 */
typedef struct robo_paint_text {
    char *buffer;
    /** Bytes available, including the NUL that is always kept at @ref length. */
    size_t capacity;
    /** Bytes the text needs, whether or not they fit. */
    size_t length;
} robo_paint_text_t;

/** Start building into @p buffer. A NULL buffer or zero capacity counts only. */
static inline void robo_paint_text_init(robo_paint_text_t *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0u;
    if (buffer && capacity > 0u) {
        buffer[0] = '\0';
    }
}

/** Append @p c. */
static inline void robo_paint_text_char(robo_paint_text_t *text, char c)
{
    if (text->buffer && text->length + 1u < text->capacity) {
        text->buffer[text->length] = c;
        text->buffer[text->length + 1u] = '\0';
    }
    text->length++;
}

/** Append @p string (nothing for NULL). */
static inline void robo_paint_text_string(robo_paint_text_t *text, const char *string)
{
    if (!string) {
        return;
    }
    while (*string != '\0') {
        robo_paint_text_char(text, *string);
        string++;
    }
}

/** Append @p value in base 10. */
static inline void robo_paint_text_u64(robo_paint_text_t *text, uint64_t value)
{
    char digits[24];
    size_t count = 0u;

    if (value == 0u) {
        robo_paint_text_char(text, '0');
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count] = (char)('0' + (int)(value % 10u));
        value /= 10u;
        count++;
    }
    while (count > 0u) {
        count--;
        robo_paint_text_char(text, digits[count]);
    }
}

/** Append @p value in base 10, with a leading `-` when negative. */
static inline void robo_paint_text_i64(robo_paint_text_t *text, int64_t value)
{
    if (value < 0) {
        robo_paint_text_char(text, '-');
        /* Negated through the unsigned range, so the most negative value is written
         * rather than overflowing on the way. */
        robo_paint_text_u64(text, (uint64_t)0u - (uint64_t)value);
        return;
    }
    robo_paint_text_u64(text, (uint64_t)value);
}

/** Append `x,y,w,h`. */
static inline void robo_paint_text_rect(robo_paint_text_t *text, robo_layout_rect_t rect)
{
    robo_paint_text_i64(text, (int64_t)rect.x);
    robo_paint_text_char(text, ',');
    robo_paint_text_i64(text, (int64_t)rect.y);
    robo_paint_text_char(text, ',');
    robo_paint_text_i64(text, (int64_t)rect.width);
    robo_paint_text_char(text, ',');
    robo_paint_text_i64(text, (int64_t)rect.height);
}

/** Non-zero when the text fit the buffer it was built into. */
static inline int robo_paint_text_fits(const robo_paint_text_t *text)
{
    return text->length + 1u <= text->capacity;
}

#endif /* ROBO_PAINT_INTERNAL_H */
