/*
 * RoboRender frame dump (Phase 11.12) — see RoboRenderRasterizer.h.
 *
 * Five lines, one per group of facts, and the grouping is the point: a reader comparing two frames
 * reads the *lines* (target and region, the walk, the drawing kinds, the two states, the checksum)
 * rather than a field list, so a difference that matters is a line that differs rather than a number
 * to find.
 *
 * ## Why there is a writer here rather than a call into RoboPaint's
 *
 * `RoboPaintInternal.h` has a `robo_paint_writer_t` and this file does not use it, which is worth
 * stating because the reflexive reading is "duplication". That header is **not installed**: it is one
 * module's private text builder, and a second module reaching into it would be reaching past a
 * boundary the module keeps for a reason -- its own comment says "not installed: nothing public
 * includes this", and the boundary scan enforces the same direction for names. What is duplicated
 * here is a hundred lines of integer-to-text that `stdio` cannot be used for (the guest target has
 * none) -- and the alternative, *installing* one module's private builder so another could print a
 * number, would be a public type nobody outside the two would ever name.
 *
 * ## The rules this file inherits, and keeps
 *
 *   **no address, ever.** The dump prints counts, rectangles, and one checksum -- every one of which
 *   is a function of the input. A pointer in a dump is a value that changes between two runs of the
 *   same document, which would make the deterministic thing about the dump the thing that is not;
 *
 *   **the needed length is always reported.** A buffer that is too small returns `ROBO_NOT_SUPPORTED`
 *   with `out_length` set to what the text *would* have taken, which is the contract every `_write`
 *   door in this tree uses and the reason the writer keeps counting after it stops storing;
 *
 *   **a frame is a value, so a dump cannot fail for a reason about the document.** The only failures
 *   here are a NULL argument and a short buffer.
 */

#include "RoboRender/RoboRenderRasterizer.h"

#include <string.h>

/** The text builder, private to this file (see the header comment). */
typedef struct render_writer {
    char *buffer;
    /** Bytes available, including the NUL that is always kept at @ref length. */
    size_t capacity;
    /** Bytes the text needs, whether or not they fit. */
    size_t length;
} render_writer_t;

static void render_writer_init(render_writer_t *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0u;
    if (buffer && capacity > 0u) {
        buffer[0] = '\0';
    }
}

static void render_writer_char(render_writer_t *text, char c)
{
    if (text->buffer && text->length + 1u < text->capacity) {
        text->buffer[text->length] = c;
        text->buffer[text->length + 1u] = '\0';
    }
    text->length++;
}

static void render_writer_string(render_writer_t *text, const char *string)
{
    if (!string) {
        return;
    }
    while (*string != '\0') {
        render_writer_char(text, *string);
        string++;
    }
}

static void render_writer_u64(render_writer_t *text, uint64_t value)
{
    char digits[24];
    size_t count = 0u;

    if (value == 0u) {
        render_writer_char(text, '0');
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count] = (char)('0' + (int)(value % 10u));
        value /= 10u;
        count++;
    }
    while (count > 0u) {
        count--;
        render_writer_char(text, digits[count]);
    }
}

static void render_writer_i64(render_writer_t *text, int64_t value)
{
    if (value < 0) {
        render_writer_char(text, '-');
        /* Negated through the unsigned range, so the most negative value prints rather than
         * overflowing on the way. */
        render_writer_u64(text, (uint64_t)0u - (uint64_t)value);
        return;
    }
    render_writer_u64(text, (uint64_t)value);
}

static void render_writer_hex64(render_writer_t *text, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    unsigned shift = 60u;

    for (;;) {
        render_writer_char(text, digits[(value >> shift) & 0xFu]);
        if (shift == 0u) {
            break;
        }
        shift -= 4u;
    }
}

/** Append `x,y,w,h`. */
static void render_writer_rect(render_writer_t *text, robo_layout_rect_t rect)
{
    render_writer_i64(text, (int64_t)rect.x);
    render_writer_char(text, ',');
    render_writer_i64(text, (int64_t)rect.y);
    render_writer_char(text, ',');
    render_writer_i64(text, (int64_t)rect.width);
    render_writer_char(text, ',');
    render_writer_i64(text, (int64_t)rect.height);
}

/** Append ` name=value` for a 64-bit count. */
static void render_writer_field(render_writer_t *text, const char *name, uint64_t value)
{
    render_writer_char(text, ' ');
    render_writer_string(text, name);
    render_writer_char(text, '=');
    render_writer_u64(text, value);
}

/** Append ` name=value` for a boolean-shaped field, printed as `0` or `1`. */
static void render_writer_flag(render_writer_t *text, const char *name, int value)
{
    render_writer_char(text, ' ');
    render_writer_string(text, name);
    render_writer_char(text, '=');
    render_writer_char(text, value ? '1' : '0');
}

/** Begin a line: two spaces of indent, then the label. */
static void render_writer_line(render_writer_t *text, const char *label)
{
    render_writer_string(text, "  ");
    render_writer_string(text, label);
}

static int render_writer_fits(const render_writer_t *text)
{
    return text->length + 1u <= text->capacity;
}

robo_result_t robo_render_frame_write(const robo_render_frame_t *frame,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_render_diag_t *diag)
{
    render_writer_t text;
    const robo_paint_decoration_stats_t *pass;

    if (!frame || !buffer || !out_length || capacity == 0u) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_DUMP, ROBO_INVALID_ARGUMENT,
                                "a frame, a buffer and an out_length are required");
    }

    render_writer_init(&text, buffer, capacity);
    pass = &frame->pass;

    render_writer_string(&text, "frame ");
    render_writer_u64(&text, (uint64_t)frame->width);
    render_writer_char(&text, 'x');
    render_writer_u64(&text, (uint64_t)frame->height);
    render_writer_char(&text, ' ');
    /* From the format's own metadata rather than a literal, so the name a reader sees is the name
     * the pixel-format layer uses. */
    {
        robo_gfx_pixel_format_info_t info;

        memset(&info, 0, sizeof(info));
        if (robo_gfx_pixel_format_info(frame->format, &info) == ROBO_OK && info.name) {
            render_writer_string(&text, info.name);
        } else {
            render_writer_string(&text, "unknown");
        }
    }
    render_writer_field(&text, "stride", (uint64_t)frame->stride);
    render_writer_char(&text, '\n');

    render_writer_line(&text, "pixels=");
    render_writer_u64(&text, (uint64_t)frame->target_pixels);
    render_writer_string(&text, " region=");
    render_writer_rect(&text, frame->region);
    render_writer_field(&text, "region_pixels", (uint64_t)frame->region_pixels);
    render_writer_field(&text, "regions", (uint64_t)frame->regions);
    render_writer_flag(&text, "limited", frame->limited);
    render_writer_char(&text, '\n');

    render_writer_line(&text, "commands=");
    render_writer_u64(&text, (uint64_t)pass->commands);
    render_writer_field(&text, "draws", (uint64_t)pass->draws);
    render_writer_field(&text, "skipped", (uint64_t)pass->skipped);
    render_writer_field(&text, "bounded_out", (uint64_t)pass->bounded_out);
    render_writer_char(&text, '\n');

    render_writer_line(&text, "backgrounds=");
    render_writer_u64(&text, (uint64_t)pass->backgrounds);
    render_writer_field(&text, "borders", (uint64_t)pass->borders);
    render_writer_field(&text, "texts", (uint64_t)pass->texts);
    render_writer_field(&text, "images", (uint64_t)pass->images);
    render_writer_field(&text, "filled", (uint64_t)pass->filled);
    render_writer_char(&text, '\n');

    render_writer_line(&text, "clip_scopes=");
    render_writer_u64(&text, (uint64_t)pass->clip_scopes);
    render_writer_field(&text, "clip_depth", (uint64_t)pass->clip_depth_max);
    render_writer_field(&text, "opacity_scopes", (uint64_t)pass->opacity_scopes);
    render_writer_field(&text, "opacity_depth", (uint64_t)pass->opacity_depth_max);
    render_writer_field(&text, "faded", (uint64_t)pass->faded);
    render_writer_char(&text, '\n');

    render_writer_line(&text, "glyphs=");
    render_writer_u64(&text, (uint64_t)pass->glyphs);
    render_writer_field(&text, "glyphs_missing", (uint64_t)pass->glyphs_missing);
    render_writer_field(&text, "unshaped", (uint64_t)pass->unshaped);
    render_writer_field(&text, "clipped", (uint64_t)pass->clipped);
    render_writer_field(&text, "empty", (uint64_t)pass->empty);
    render_writer_field(&text, "no_resource", (uint64_t)pass->no_resource);
    render_writer_field(&text, "no_font", (uint64_t)pass->no_font);
    render_writer_char(&text, '\n');

    render_writer_line(&text, "checksum=");
    render_writer_hex64(&text, frame->checksum);
    render_writer_char(&text, '\n');

    *out_length = text.length;
    if (!render_writer_fits(&text)) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_DUMP, ROBO_NOT_SUPPORTED,
                                "the frame dump does not fit the buffer");
    }
    return ROBO_OK;
}
