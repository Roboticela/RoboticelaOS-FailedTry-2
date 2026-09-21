/*
 * RoboRender presentation (Phase 11.13) — see RoboRenderPresenter.h.
 *
 * ## The whole of the work, and the two decisions it is made of
 *
 * A presentation is **one rectangle arithmetic and one composite call**. The arithmetic answers "where
 * does the frame land", the composite writes it, and both are the graphics module's own:
 * `robo_gfx_rect_intersect` for the first, `robo_gfx_surface_composite_rect` for the second. This file
 * therefore contains no pixel loop, no blend, and no format conversion -- and that is not a
 * simplification taken for speed but the boundary 11.12 drew and this phase keeps: pixels are
 * RoboGraphics' business, and a presenter that composited by hand would be a second blend
 * implementation that would disagree with the first the day a format or an alpha rule changed.
 *
 * The two decisions are these.
 *
 * **The rectangle is computed once and used for both answers.** `robo_render_present_region` is the
 * arithmetic on its own (a caller asking before it presents), and `robo_render_present` calls it and
 * then composites the *frame's own bounds* at the requested origin rather than the intersection it just
 * computed. That looks like doing it twice and is the opposite: handing the full source rectangle to
 * `composite_rect` means the clip is applied by the call that writes the pixels -- one authority for
 * where they land -- while the intersection is used only for the *record*. A presenter that pre-clipped
 * its source rectangle would be re-deriving the composite's rule, and the two would drift the first time
 * a destination had a stride, a format, or a bounds rule this file did not think of.
 *
 * **Nothing is retained, and there is no state to get wrong.** The frame is read; the destination is
 * written; the call returns. There is no "last presented frame", no dirty flag, no queue and no partial
 * frame: the pixels arrive whole or the call failed before writing anything it did not count. That is
 * why this door needs no lifecycle and no `init` -- the same argument `RoboRender.c` makes for the
 * module as a whole, one level down.
 *
 * ## The record's arithmetic is checked, not trusted
 *
 * `presented_pixels` and `clipped_pixels` are computed so that they sum to the frame's pixel count, and
 * `robo_render_present_record_is_valid` refuses a record where they do not. A count that could be
 * wrong quietly would be worse than no count: this is the number a caller would use to decide whether a
 * resize lost part of a picture.
 */

#include "RoboRender/RoboRenderPresenter.h"

/* The blend and the format conversion this file deliberately does not implement. The composite header
 * is included here rather than re-exported by the presenter header, because no *caller* of a present
 * door needs to know that RoboGraphics' composite is what performs it -- the same reason
 * `RoboRenderRasterizer.h` names no graphics header it does not state in a signature. */
#include "RoboGraphics/RoboGraphicsComposite.h"

/* For the release flag `robo_render_presentation_is_implemented` reads, exactly as the rasteriser's
 * door reads its own one file over. */
#include "RoboRender/RoboRenderVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The text builder                                                    */
/* ------------------------------------------------------------------ */

/*
 * Private to this file, and duplicated from the frame dump's for the reason that file states: the
 * alternative is installing one module's private text builder so another could print a number, which
 * would be a public type nobody outside the two would ever name. What is duplicated is integer-to-text
 * that `stdio` cannot provide (the guest target has none).
 */
typedef struct present_writer {
    char *buffer;
    size_t capacity;
    size_t length;
} present_writer_t;

static void present_writer_init(present_writer_t *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0u;
    if (buffer && capacity > 0u) {
        buffer[0] = '\0';
    }
}

static void present_writer_char(present_writer_t *text, char c)
{
    if (text->buffer && text->length + 1u < text->capacity) {
        text->buffer[text->length] = c;
        text->buffer[text->length + 1u] = '\0';
    }
    text->length++;
}

static void present_writer_string(present_writer_t *text, const char *string)
{
    if (!string) {
        return;
    }
    while (*string != '\0') {
        present_writer_char(text, *string);
        string++;
    }
}

static void present_writer_u64(present_writer_t *text, uint64_t value)
{
    char digits[24];
    size_t count = 0u;

    if (value == 0u) {
        present_writer_char(text, '0');
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count] = (char)('0' + (int)(value % 10u));
        value /= 10u;
        count++;
    }
    while (count > 0u) {
        count--;
        present_writer_char(text, digits[count]);
    }
}

static void present_writer_i64(present_writer_t *text, int64_t value)
{
    if (value < 0) {
        present_writer_char(text, '-');
        present_writer_u64(text, (uint64_t)0u - (uint64_t)value);
        return;
    }
    present_writer_u64(text, (uint64_t)value);
}

static void present_writer_hex64(present_writer_t *text, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    unsigned shift = 60u;

    for (;;) {
        present_writer_char(text, digits[(value >> shift) & 0xFu]);
        if (shift == 0u) {
            break;
        }
        shift -= 4u;
    }
}

static void present_writer_size(present_writer_t *text, uint32_t width, uint32_t height)
{
    present_writer_u64(text, (uint64_t)width);
    present_writer_char(text, 'x');
    present_writer_u64(text, (uint64_t)height);
}

static void present_writer_rect(present_writer_t *text, robo_layout_rect_t rect)
{
    present_writer_i64(text, (int64_t)rect.x);
    present_writer_char(text, ',');
    present_writer_i64(text, (int64_t)rect.y);
    present_writer_char(text, ',');
    present_writer_i64(text, (int64_t)rect.width);
    present_writer_char(text, ',');
    present_writer_i64(text, (int64_t)rect.height);
}

static void present_writer_field(present_writer_t *text, const char *name, uint64_t value)
{
    present_writer_char(text, ' ');
    present_writer_string(text, name);
    present_writer_char(text, '=');
    present_writer_u64(text, value);
}

static void present_writer_line(present_writer_t *text, const char *label)
{
    present_writer_string(text, "  ");
    present_writer_string(text, label);
}

/** The format's own name, from the pixel-format layer rather than from a table here. */
static void present_writer_format(present_writer_t *text, robo_gfx_pixel_format_t format)
{
    robo_gfx_pixel_format_info_t info;

    memset(&info, 0, sizeof(info));
    if (robo_gfx_pixel_format_info(format, &info) == ROBO_OK && info.name) {
        present_writer_string(text, info.name);
    } else {
        present_writer_string(text, "unknown");
    }
}

/* ------------------------------------------------------------------ */
/* Validation shared by the three doors                                */
/* ------------------------------------------------------------------ */

/** Non-zero when @p dest may be written (WRITE or READ_WRITE). */
static int present_dest_is_writable(const robo_gfx_surface_t *dest)
{
    robo_gfx_surface_access_t access = robo_gfx_surface_access(dest);

    return access == ROBO_GFX_SURFACE_ACCESS_WRITE || access == ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
}

/**
 * The frame's own rectangle at the requested origin, and the destination's pixel bounds.
 * Both are needed by the region door and the present door, so they are resolved once here.
 */
static robo_result_t present_resolve(const robo_render_target_t *frame,
                                    const robo_gfx_surface_t *dest,
                                    int32_t x,
                                    int32_t y,
                                    robo_gfx_rect_t *out_frame_bounds,
                                    robo_gfx_rect_t *out_dest_bounds,
                                    robo_render_diag_t *diag)
{
    robo_gfx_pixel_format_t format;

    if (!frame || !dest || !out_frame_bounds || !out_dest_bounds) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_INVALID_ARGUMENT,
                                "a frame and a destination are required");
    }

    format = robo_gfx_surface_format(dest);
    if (!robo_gfx_pixel_format_is_implemented(format)) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_NOT_SUPPORTED,
                                "the destination format is not one this build can write");
    }

    *out_frame_bounds = robo_gfx_rect(x, y, (int32_t)robo_render_target_width(frame),
                                      (int32_t)robo_render_target_height(frame));
    *out_dest_bounds = robo_gfx_rect(0, 0, (int32_t)robo_gfx_surface_width(dest),
                                     (int32_t)robo_gfx_surface_height(dest));
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void robo_render_present_record_init(robo_render_present_record_t *record)
{
    if (record) {
        memset(record, 0, sizeof(*record));
        record->dest_format = ROBO_GFX_FORMAT_UNKNOWN;
        record->presented = robo_gfx_rect_empty();
    }
}

int robo_render_present_record_is_valid(const robo_render_present_record_t *record)
{
    size_t frame_pixels;

    if (!record) {
        return 0;
    }
    if (record->frame_width == 0u || record->frame_height == 0u) {
        return 0;
    }
    if (record->frame_width > ROBO_RENDER_MAX_DIMENSION ||
        record->frame_height > ROBO_RENDER_MAX_DIMENSION) {
        return 0;
    }
    /* A target this module could have made is in the one format it makes. A record describing a
     * presentation *out of* something else is describing something that cannot exist. */
    if (record->dest_format != ROBO_RENDER_TARGET_FORMAT) {
        return 0;
    }
    if (!robo_gfx_rect_is_valid(record->presented)) {
        return 0;
    }
    /* The counts are the frame's, split in two: anything else is a record whose fields disagree. */
    frame_pixels = (size_t)record->frame_width * (size_t)record->frame_height;
    if (record->presented_pixels + record->clipped_pixels != frame_pixels) {
        return 0;
    }
    if (record->presented_pixels >
        (size_t)record->presented.width * (size_t)record->presented.height) {
        return 0;
    }
    if (record->complete != (record->clipped_pixels == 0u ? 1 : 0)) {
        return 0;
    }
    return 1;
}

int robo_render_present_record_equal(const robo_render_present_record_t *a,
                                     const robo_render_present_record_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->frame_width == b->frame_width && a->frame_height == b->frame_height &&
           a->dest_width == b->dest_width && a->dest_height == b->dest_height &&
           a->dest_format == b->dest_format &&
           robo_gfx_rect_equal(a->presented, b->presented) &&
           a->presented_pixels == b->presented_pixels &&
           a->clipped_pixels == b->clipped_pixels && a->offset_x == b->offset_x &&
           a->offset_y == b->offset_y && a->complete == b->complete &&
           a->frame_checksum == b->frame_checksum;
}

/* ------------------------------------------------------------------ */
/* The region door                                                     */
/* ------------------------------------------------------------------ */

robo_result_t robo_render_present_region(const robo_render_target_t *frame,
                                         const robo_gfx_surface_t *dest,
                                         int32_t x,
                                         int32_t y,
                                         robo_layout_rect_t *out,
                                         robo_render_diag_t *diag)
{
    robo_gfx_rect_t frame_bounds;
    robo_gfx_rect_t dest_bounds;
    robo_gfx_rect_t meeting;
    robo_result_t status;

    if (!out) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_INVALID_ARGUMENT,
                                "an out rectangle is required");
    }
    status = present_resolve(frame, dest, x, y, &frame_bounds, &dest_bounds, diag);
    if (status != ROBO_OK) {
        return status;
    }

    /* An empty intersection is an answer rather than a failure: a frame and a destination that do not
     * meet is a legitimate state (a window scrolled entirely off, a zero-height work area), and every
     * drawing in this tree treats it the same way. */
    if (robo_gfx_rect_intersect(frame_bounds, dest_bounds, &meeting) != ROBO_OK) {
        *out = robo_gfx_rect_empty();
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_ERROR,
                                "the frame and destination rectangles could not be intersected");
    }
    *out = meeting;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The present door                                                    */
/* ------------------------------------------------------------------ */

robo_result_t robo_render_present(const robo_render_target_t *frame,
                                  robo_gfx_surface_t *dest,
                                  int32_t x,
                                  int32_t y,
                                  robo_render_present_record_t *out_record,
                                  robo_render_diag_t *diag)
{
    robo_gfx_rect_t frame_bounds;
    robo_gfx_rect_t dest_bounds;
    robo_gfx_rect_t meeting;
    robo_render_present_record_t record;
    size_t frame_pixels;
    robo_result_t status;

    status = present_resolve(frame, dest, x, y, &frame_bounds, &dest_bounds, diag);
    if (status != ROBO_OK) {
        return status;
    }
    if (!present_dest_is_writable(dest)) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_INVALID_ARGUMENT,
                                "the destination surface is not write-capable");
    }

    robo_render_present_record_init(&record);
    record.frame_width = robo_render_target_width(frame);
    record.frame_height = robo_render_target_height(frame);
    record.dest_width = robo_gfx_surface_width(dest);
    record.dest_height = robo_gfx_surface_height(dest);
    record.dest_format = robo_gfx_surface_format(dest);
    record.offset_x = x;
    record.offset_y = y;
    record.frame_checksum = robo_render_target_checksum(frame);

    frame_pixels = robo_render_target_pixel_count(frame);

    if (robo_gfx_rect_intersect(frame_bounds, dest_bounds, &meeting) != ROBO_OK) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_ERROR,
                                "the frame and destination rectangles could not be intersected");
    }
    record.presented = meeting;

    if (robo_gfx_rect_is_empty(meeting)) {
        /* Nothing to write and nothing to say but the truth about it: the whole frame was clipped. */
        record.presented_pixels = 0u;
        record.clipped_pixels = frame_pixels;
        record.complete = 0;
        if (out_record) {
            *out_record = record;
        }
        return ROBO_OK;
    }

    /*
     * The frame's **own** bounds as the *source* rectangle, and the origin as the *destination* point,
     * so the clip is entirely the composite's decision (see the file comment). The two rectangles are
     * in two coordinate spaces and conflating them is the one bug this call can have: `frame_bounds`
     * above is the frame *placed* on the destination -- which is what the intersection was computed
     * from -- while `robo_render_target_bounds(frame)` is the frame in its own pixels. Passing the
     * former as the source would apply the origin twice and read outside the frame.
     *
     * The opacity is full because the frame already carries whatever alpha it was drawn with.
     */
    status = robo_gfx_surface_composite_rect(robo_render_target_surface_const(frame),
                                             robo_render_target_bounds(frame), dest,
                                             robo_gfx_point(x, y), 255u);
    if (status != ROBO_OK) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, status,
                                "the frame could not be composited into the destination");
    }

    record.presented_pixels =
        (size_t)meeting.width * (size_t)meeting.height;
    record.clipped_pixels = frame_pixels - record.presented_pixels;
    record.complete = (record.clipped_pixels == 0u) ? 1 : 0;

    if (out_record) {
        *out_record = record;
    }
    return ROBO_OK;
}

robo_result_t robo_render_present_framebuffer(const robo_render_target_t *frame,
                                              robo_gfx_framebuffer_t *fb,
                                              int32_t x,
                                              int32_t y,
                                              robo_render_present_record_t *out_record,
                                              robo_render_diag_t *diag)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *borrowed = NULL;
    robo_result_t status;

    if (!fb) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_INVALID_ARGUMENT,
                                "a framebuffer is required");
    }
    if (robo_gfx_framebuffer_pixels(fb) == NULL) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_INVALID_ARGUMENT,
                                "the framebuffer has no storage to write");
    }

    /*
     * A **borrowed** surface: the mapping belongs to whoever built the framebuffer, and nothing here
     * outlives the call. The geometry comes from the framebuffer's own accessors rather than from a
     * caller's restatement of them, so a framebuffer whose stride is not its width times its pixel size
     * still presents correctly.
     */
    robo_gfx_surface_desc_init(&desc);
    desc.width = robo_gfx_framebuffer_width(fb);
    desc.height = robo_gfx_framebuffer_height(fb);
    desc.format = robo_gfx_framebuffer_format(fb);
    desc.stride_bytes = robo_gfx_framebuffer_stride(fb);
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;

    status = robo_gfx_surface_create_borrowed(&desc, robo_gfx_framebuffer_pixels(fb), &borrowed);
    if (status != ROBO_OK || !borrowed) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, status,
                                "the framebuffer could not be viewed as a surface");
    }

    status = robo_render_present(frame, borrowed, x, y, out_record, diag);
    robo_gfx_surface_release(borrowed);
    return status;
}

int robo_render_presentation_is_implemented(void)
{
    return ROBO_RENDER_PRESENTATION_IMPLEMENTED;
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

robo_result_t robo_render_present_write(const robo_render_present_record_t *record,
                                       char *buffer,
                                       size_t capacity,
                                       size_t *out_length,
                                       robo_render_diag_t *diag)
{
    present_writer_t text;

    if (!record || !buffer || !out_length || capacity == 0u) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_DUMP, ROBO_INVALID_ARGUMENT,
                                "a record, a buffer and an out_length are required");
    }

    present_writer_init(&text, buffer, capacity);

    present_writer_string(&text, "present ");
    present_writer_size(&text, record->frame_width, record->frame_height);
    present_writer_char(&text, ' ');
    present_writer_format(&text, ROBO_RENDER_TARGET_FORMAT);
    present_writer_string(&text, " -> ");
    present_writer_size(&text, record->dest_width, record->dest_height);
    present_writer_char(&text, ' ');
    present_writer_format(&text, record->dest_format);
    present_writer_string(&text, " at ");
    present_writer_i64(&text, (int64_t)record->offset_x);
    present_writer_char(&text, ',');
    present_writer_i64(&text, (int64_t)record->offset_y);
    present_writer_char(&text, '\n');

    present_writer_line(&text, "presented=");
    present_writer_rect(&text, record->presented);
    present_writer_field(&text, "pixels", (uint64_t)record->presented_pixels);
    present_writer_field(&text, "clipped", (uint64_t)record->clipped_pixels);
    present_writer_char(&text, ' ');
    present_writer_string(&text, "complete=");
    present_writer_char(&text, record->complete ? '1' : '0');
    present_writer_char(&text, '\n');

    present_writer_line(&text, "frame_checksum=");
    present_writer_hex64(&text, record->frame_checksum);
    present_writer_char(&text, '\n');

    present_writer_line(&text, "record=");
    present_writer_string(&text, robo_render_present_record_is_valid(record) ? "valid" : "invalid");
    present_writer_char(&text, '\n');

    *out_length = text.length;
    if (text.length + 1u > text.capacity) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_DUMP, ROBO_NOT_SUPPORTED,
                                "the presentation dump does not fit the buffer");
    }
    return ROBO_OK;
}
