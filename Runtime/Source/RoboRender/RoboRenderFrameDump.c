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
 * `RoboPaintInternal.h` has a `PaintWriter` and this file does not use it, which is worth
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
 *   **the needed length is always reported.** A buffer that is too small returns `RoboNotSupported`
 *   with `outLength` set to what the text *would* have taken, which is the contract every `_write`
 *   door in this tree uses and the reason the writer keeps counting after it stops storing;
 *
 *   **a frame is a value, so a dump cannot fail for a reason about the document.** The only failures
 *   here are a NULL argument and a short buffer.
 */

#include "RoboRender/RoboRenderRasterizer.h"

#include <string.h>

/** The text builder, private to this file (see the header comment). */
typedef struct renderWriter {
    char *buffer;
    /** Bytes available, including the NUL that is always kept at @ref length. */
    size_t capacity;
    /** Bytes the text needs, whether or not they fit. */
    size_t length;
} RenderWriter;

static void RenderWriterInit(RenderWriter *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0u;
    if (buffer && capacity > 0u) {
        buffer[0] = '\0';
    }
}

static void RenderWriterChar(RenderWriter *text, char c)
{
    if (text->buffer && text->length + 1u < text->capacity) {
        text->buffer[text->length] = c;
        text->buffer[text->length + 1u] = '\0';
    }
    text->length++;
}

static void RenderWriterString(RenderWriter *text, const char *string)
{
    if (!string) {
        return;
    }
    while (*string != '\0') {
        RenderWriterChar(text, *string);
        string++;
    }
}

static void RenderWriterU64(RenderWriter *text, uint64_t value)
{
    char digits[24];
    size_t count = 0u;

    if (value == 0u) {
        RenderWriterChar(text, '0');
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count] = (char)('0' + (int)(value % 10u));
        value /= 10u;
        count++;
    }
    while (count > 0u) {
        count--;
        RenderWriterChar(text, digits[count]);
    }
}

static void RenderWriterI64(RenderWriter *text, int64_t value)
{
    if (value < 0) {
        RenderWriterChar(text, '-');
        /* Negated through the unsigned range, so the most negative value prints rather than
         * overflowing on the way. */
        RenderWriterU64(text, (uint64_t)0u - (uint64_t)value);
        return;
    }
    RenderWriterU64(text, (uint64_t)value);
}

static void RenderWriterHex64(RenderWriter *text, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    unsigned shift = 60u;

    for (;;) {
        RenderWriterChar(text, digits[(value >> shift) & 0xFu]);
        if (shift == 0u) {
            break;
        }
        shift -= 4u;
    }
}

/** Append `x,y,w,h`. */
static void RenderWriterRect(RenderWriter *text, LayoutRect rect)
{
    RenderWriterI64(text, (int64_t)rect.x);
    RenderWriterChar(text, ',');
    RenderWriterI64(text, (int64_t)rect.y);
    RenderWriterChar(text, ',');
    RenderWriterI64(text, (int64_t)rect.width);
    RenderWriterChar(text, ',');
    RenderWriterI64(text, (int64_t)rect.height);
}

/** Append ` name=value` for a 64-bit count. */
static void RenderWriterField(RenderWriter *text, const char *name, uint64_t value)
{
    RenderWriterChar(text, ' ');
    RenderWriterString(text, name);
    RenderWriterChar(text, '=');
    RenderWriterU64(text, value);
}

/** Append ` name=value` for a boolean-shaped field, printed as `0` or `1`. */
static void RenderWriterFlag(RenderWriter *text, const char *name, int value)
{
    RenderWriterChar(text, ' ');
    RenderWriterString(text, name);
    RenderWriterChar(text, '=');
    RenderWriterChar(text, value ? '1' : '0');
}

/** Begin a line: two spaces of indent, then the label. */
static void RenderWriterLine(RenderWriter *text, const char *label)
{
    RenderWriterString(text, "  ");
    RenderWriterString(text, label);
}

static int RenderWriterFits(const RenderWriter *text)
{
    return text->length + 1u <= text->capacity;
}

Result RenderFrameWrite(const RenderFrame *frame,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     RenderDiag *diag)
{
    RenderWriter text;
    const PaintDecorationStats *pass;

    if (!frame || !buffer || !outLength || capacity == 0u) {
        return RoboRenderDiag(diag, RoboRenderDomainDump, RoboInvalidArgument,
                                "a frame, a buffer and an out_length are required");
    }

    RenderWriterInit(&text, buffer, capacity);
    pass = &frame->pass;

    RenderWriterString(&text, "frame ");
    RenderWriterU64(&text, (uint64_t)frame->width);
    RenderWriterChar(&text, 'x');
    RenderWriterU64(&text, (uint64_t)frame->height);
    RenderWriterChar(&text, ' ');
    /* From the format's own metadata rather than a literal, so the name a reader sees is the name
     * the pixel-format layer uses. */
    {
        GfxPixelFormatInfo info;

        memset(&info, 0, sizeof(info));
        if (GetGfxPixelFormatInfo(frame->format, &info) == RoboOk && info.name) {
            RenderWriterString(&text, info.name);
        } else {
            RenderWriterString(&text, "unknown");
        }
    }
    RenderWriterField(&text, "stride", (uint64_t)frame->stride);
    RenderWriterChar(&text, '\n');

    RenderWriterLine(&text, "pixels=");
    RenderWriterU64(&text, (uint64_t)frame->targetPixels);
    RenderWriterString(&text, " region=");
    RenderWriterRect(&text, frame->region);
    RenderWriterField(&text, "region_pixels", (uint64_t)frame->regionPixels);
    RenderWriterField(&text, "regions", (uint64_t)frame->regions);
    RenderWriterFlag(&text, "limited", frame->limited);
    RenderWriterChar(&text, '\n');

    RenderWriterLine(&text, "commands=");
    RenderWriterU64(&text, (uint64_t)pass->commands);
    RenderWriterField(&text, "draws", (uint64_t)pass->draws);
    RenderWriterField(&text, "skipped", (uint64_t)pass->skipped);
    RenderWriterField(&text, "bounded_out", (uint64_t)pass->boundedOut);
    RenderWriterChar(&text, '\n');

    RenderWriterLine(&text, "backgrounds=");
    RenderWriterU64(&text, (uint64_t)pass->backgrounds);
    RenderWriterField(&text, "borders", (uint64_t)pass->borders);
    RenderWriterField(&text, "texts", (uint64_t)pass->texts);
    RenderWriterField(&text, "images", (uint64_t)pass->images);
    RenderWriterField(&text, "filled", (uint64_t)pass->filled);
    RenderWriterChar(&text, '\n');

    RenderWriterLine(&text, "clip_scopes=");
    RenderWriterU64(&text, (uint64_t)pass->clipScopes);
    RenderWriterField(&text, "clip_depth", (uint64_t)pass->clipDepthMax);
    RenderWriterField(&text, "opacity_scopes", (uint64_t)pass->opacityScopes);
    RenderWriterField(&text, "opacity_depth", (uint64_t)pass->opacityDepthMax);
    RenderWriterField(&text, "faded", (uint64_t)pass->faded);
    RenderWriterChar(&text, '\n');

    RenderWriterLine(&text, "glyphs=");
    RenderWriterU64(&text, (uint64_t)pass->glyphs);
    RenderWriterField(&text, "glyphs_missing", (uint64_t)pass->glyphsMissing);
    RenderWriterField(&text, "unshaped", (uint64_t)pass->unshaped);
    RenderWriterField(&text, "clipped", (uint64_t)pass->clipped);
    RenderWriterField(&text, "empty", (uint64_t)pass->empty);
    RenderWriterField(&text, "no_resource", (uint64_t)pass->noResource);
    RenderWriterField(&text, "no_font", (uint64_t)pass->noFont);
    RenderWriterChar(&text, '\n');

    RenderWriterLine(&text, "checksum=");
    RenderWriterHex64(&text, frame->checksum);
    RenderWriterChar(&text, '\n');

    *outLength = text.length;
    if (!RenderWriterFits(&text)) {
        return RoboRenderDiag(diag, RoboRenderDomainDump, RoboNotSupported,
                                "the frame dump does not fit the buffer");
    }
    return RoboOk;
}
