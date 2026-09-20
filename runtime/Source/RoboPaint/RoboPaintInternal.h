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
 * (Named for what it is rather than for its subject since 11.6: this module now has a
 * **text** vocabulary of its own, and a builder called `robo_paint_text_t` sitting beside
 * a `robo_paint_text_run_t` that means something else entirely is a name that costs a
 * reader a second look every time.)
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
} robo_paint_writer_t;

/** Start building into @p buffer. A NULL buffer or zero capacity counts only. */
static inline void robo_paint_writer_init(robo_paint_writer_t *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0u;
    if (buffer && capacity > 0u) {
        buffer[0] = '\0';
    }
}

/** Append @p c. */
static inline void robo_paint_writer_char(robo_paint_writer_t *text, char c)
{
    if (text->buffer && text->length + 1u < text->capacity) {
        text->buffer[text->length] = c;
        text->buffer[text->length + 1u] = '\0';
    }
    text->length++;
}

/** Append @p string (nothing for NULL). */
static inline void robo_paint_writer_string(robo_paint_writer_t *text, const char *string)
{
    if (!string) {
        return;
    }
    while (*string != '\0') {
        robo_paint_writer_char(text, *string);
        string++;
    }
}

/** Append @p value in base 10. */
static inline void robo_paint_writer_u64(robo_paint_writer_t *text, uint64_t value)
{
    char digits[24];
    size_t count = 0u;

    if (value == 0u) {
        robo_paint_writer_char(text, '0');
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count] = (char)('0' + (int)(value % 10u));
        value /= 10u;
        count++;
    }
    while (count > 0u) {
        count--;
        robo_paint_writer_char(text, digits[count]);
    }
}

/** Append @p value in base 10, with a leading `-` when negative. */
static inline void robo_paint_writer_i64(robo_paint_writer_t *text, int64_t value)
{
    if (value < 0) {
        robo_paint_writer_char(text, '-');
        /* Negated through the unsigned range, so the most negative value is written
         * rather than overflowing on the way. */
        robo_paint_writer_u64(text, (uint64_t)0u - (uint64_t)value);
        return;
    }
    robo_paint_writer_u64(text, (uint64_t)value);
}

/** Append `x,y,w,h`. */
static inline void robo_paint_writer_rect(robo_paint_writer_t *text, robo_layout_rect_t rect)
{
    robo_paint_writer_i64(text, (int64_t)rect.x);
    robo_paint_writer_char(text, ',');
    robo_paint_writer_i64(text, (int64_t)rect.y);
    robo_paint_writer_char(text, ',');
    robo_paint_writer_i64(text, (int64_t)rect.width);
    robo_paint_writer_char(text, ',');
    robo_paint_writer_i64(text, (int64_t)rect.height);
}

/**
 * Append a **number with a fractional part**, to @p decimals places.
 *
 * The one thing this file has that the integer writers do not, and it exists because a
 * dump has to print a font size: `font-size: 24px` is a `double` in
 * `robo_layout_text_style_t`, and `24` would be a different number from `24.5` the
 * moment an author writes a fractional size. So the value is scaled to an integer at the
 * requested precision and printed through the integer writer, which is deterministic,
 * `stdio`-free and rounding-stated rather than `printf`'s locale-dependent behaviour.
 *
 * A non-finite value is written as `nan`/`inf` rather than as a number: it cannot be
 * stored in a valid record (`robo_paint_text_run_is_valid` refuses one), so it can only
 * arrive here from a record a caller built by hand, and printing it as a plausible number
 * is the one answer a reader could not act on.
 */
static inline void robo_paint_writer_number(robo_paint_writer_t *text,
                                            double value,
                                            unsigned decimals)
{
    double scale = 1.0;
    double scaled;
    uint64_t whole;
    uint64_t fraction;
    unsigned index;

    if (!(value == value)) {
        robo_paint_writer_string(text, "nan");
        return;
    }
    if (value > 1.0e30 || value < -1.0e30) {
        /* Finitely printed, honestly labelled: a value this large is outside anything a
         * dump line can carry meaningfully, and "inf" says so rather than printing
         * eighteen digits. */
        robo_paint_writer_string(text, "inf");
        return;
    }
    for (index = 0u; index < decimals; index++) {
        scale *= 10.0;
    }
    scaled = value * scale;
    /* Rounded half away from zero, which is the policy `RoboFont`'s own rounding states
     * and the one this tree uses wherever a continuous value becomes an integer. */
    scaled = scaled < 0.0 ? scaled - 0.5 : scaled + 0.5;
    whole = (uint64_t)(scaled < 0.0 ? -scaled : scaled);
    if (value < 0.0 && whole != 0u) {
        robo_paint_writer_char(text, '-');
    }
    if (decimals == 0u) {
        robo_paint_writer_u64(text, whole);
        return;
    }
    fraction = whole % (uint64_t)scale;
    robo_paint_writer_u64(text, (whole - fraction) / (uint64_t)scale);
    robo_paint_writer_char(text, '.');
    /* Leading zeroes inside the fraction, so `24.05` does not print as `24.5`. */
    for (index = (unsigned)scale / 10u; index > 1u; index /= 10u) {
        if (fraction < (uint64_t)index) {
            robo_paint_writer_char(text, '0');
        } else {
            break;
        }
    }
    robo_paint_writer_u64(text, fraction);
}

/** One hex digit of @p value (0..15), lowercase. */
static inline char robo_paint_writer_hex(unsigned value)
{
    return (char)(value < 10u ? ('0' + value) : ('a' + (value - 10u)));
}

/**
 * Append ` text="..."` for the run @p span names in @p pool, at most @p limit code points.
 *
 * The one thing the two dumps have that a record's own writer cannot: **the characters**. A
 * record carries a span and two numbers, and the pool is the only thing that knows what they
 * name -- so this lives beside the writer rather than in either record, and both dumps call it
 * after the record's own line.
 *
 * Three rules, and all three exist so that "one line per operation" stays a fact a suite can
 * count lines against:
 *
 *   printable ASCII prints as itself, except `"` and `\`, which are backslash-escaped -- so
 *   the quoting is unambiguous and a reader can paste a run into a document;
 *   every other code point prints as `\uXXXX` (or `\UXXXXXXXX` above the BMP): a **newline**
 *   inside a run would otherwise turn one operation's line into two, and a capture that
 *   silently changed the line count is the kind of quiet wrongness a dump must not have;
 *   a run longer than @p limit prints its first @p limit code points and then `...`, because a
 *   dump line is bounded by a compile-time constant and a run's length is the document's to
 *   choose -- a bound that the document could exceed is not a bound.
 *
 * @p pool may be NULL or @p span may name nothing, and then **nothing is appended**: a
 * truncated dump is worse than a dump without characters, and the record's own line already
 * says how many code points there were.
 */
static inline void robo_paint_writer_text_run(robo_paint_writer_t *text,
                                              const robo_paint_text_pool_t *pool,
                                              robo_paint_text_span_t span,
                                              size_t limit)
{
    const uint32_t *code_points;
    size_t printed;
    size_t index;

    if (!pool || !robo_paint_text_pool_contains(pool, span) || span.count == 0u) {
        return;
    }
    code_points = robo_paint_text_pool_code_points(pool);
    if (!code_points) {
        return;
    }
    robo_paint_writer_string(text, " text=\"");
    printed = (size_t)span.count < limit ? (size_t)span.count : limit;
    for (index = 0u; index < printed; index++) {
        uint32_t code_point = code_points[(size_t)span.first + index];

        if (code_point == (uint32_t)'"') {
            robo_paint_writer_string(text, "\\\"");
        } else if (code_point == (uint32_t)'\\') {
            robo_paint_writer_string(text, "\\\\");
        } else if (code_point >= 0x20u && code_point <= 0x7Eu) {
            robo_paint_writer_char(text, (char)code_point);
        } else if (code_point <= 0xFFFFu) {
            unsigned shift;

            robo_paint_writer_string(text, "\\u");
            for (shift = 12u;; shift -= 4u) {
                robo_paint_writer_char(text,
                                       robo_paint_writer_hex((code_point >> shift) & 0xFu));
                if (shift == 0u) {
                    break;
                }
            }
        } else {
            unsigned shift;

            robo_paint_writer_string(text, "\\U");
            for (shift = 28u;; shift -= 4u) {
                robo_paint_writer_char(text,
                                       robo_paint_writer_hex((code_point >> shift) & 0xFu));
                if (shift == 0u) {
                    break;
                }
            }
        }
    }
    if (printed < (size_t)span.count) {
        robo_paint_writer_string(text, "...");
    }
    robo_paint_writer_char(text, '"');
}

/** Non-zero when the text fit the buffer it was built into. */
static inline int robo_paint_writer_fits(const robo_paint_writer_t *text)
{
    return text->length + 1u <= text->capacity;
}

#endif /* ROBO_PAINT_INTERNAL_H */
