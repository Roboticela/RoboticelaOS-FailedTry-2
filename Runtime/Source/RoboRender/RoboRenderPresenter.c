/*
 * RoboRender presentation (Phase 11.13) — see RoboRenderPresenter.h.
 *
 * ## The whole of the work, and the two decisions it is made of
 *
 * A presentation is **one rectangle arithmetic and one composite call**. The arithmetic answers "where
 * does the frame land", the composite writes it, and both are the graphics module's own:
 * `GfxRectIntersect` for the first, `GfxSurfaceCompositeRect` for the second. This file
 * therefore contains no pixel loop, no blend, and no format conversion -- and that is not a
 * simplification taken for speed but the boundary 11.12 drew and this phase keeps: pixels are
 * RoboGraphics' business, and a presenter that composited by hand would be a second blend
 * implementation that would disagree with the first the day a format or an alpha rule changed.
 *
 * The two decisions are these.
 *
 * **The rectangle is computed once and used for both answers.** `RenderPresentRegion` is the
 * arithmetic on its own (a caller asking before it presents), and `RenderPresent` calls it and
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
 * `presentedPixels` and `clippedPixels` are computed so that they sum to the frame's pixel count, and
 * `RenderPresentRecordIsValid` refuses a record where they do not. A count that could be
 * wrong quietly would be worse than no count: this is the number a caller would use to decide whether a
 * resize lost part of a picture.
 */

#include "RoboRender/RoboRenderPresenter.h"

/* The blend and the format conversion this file deliberately does not implement. The composite header
 * is included here rather than re-exported by the presenter header, because no *caller* of a present
 * door needs to know that RoboGraphics' composite is what performs it -- the same reason
 * `RoboRenderRasterizer.h` names no graphics header it does not state in a signature. */
#include "RoboGraphics/RoboGraphicsComposite.h"

/* For the release flag `RenderPresentationIsImplemented` reads, exactly as the rasteriser's
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
typedef struct presentWriter {
    char *buffer;
    size_t capacity;
    size_t length;
} PresentWriter;

static void PresentWriterInit(PresentWriter *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0u;
    if (buffer && capacity > 0u) {
        buffer[0] = '\0';
    }
}

static void PresentWriterChar(PresentWriter *text, char c)
{
    if (text->buffer && text->length + 1u < text->capacity) {
        text->buffer[text->length] = c;
        text->buffer[text->length + 1u] = '\0';
    }
    text->length++;
}

static void PresentWriterString(PresentWriter *text, const char *string)
{
    if (!string) {
        return;
    }
    while (*string != '\0') {
        PresentWriterChar(text, *string);
        string++;
    }
}

static void PresentWriterU64(PresentWriter *text, uint64_t value)
{
    char digits[24];
    size_t count = 0u;

    if (value == 0u) {
        PresentWriterChar(text, '0');
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count] = (char)('0' + (int)(value % 10u));
        value /= 10u;
        count++;
    }
    while (count > 0u) {
        count--;
        PresentWriterChar(text, digits[count]);
    }
}

static void PresentWriterI64(PresentWriter *text, int64_t value)
{
    if (value < 0) {
        PresentWriterChar(text, '-');
        PresentWriterU64(text, (uint64_t)0u - (uint64_t)value);
        return;
    }
    PresentWriterU64(text, (uint64_t)value);
}

static void PresentWriterHex64(PresentWriter *text, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    unsigned shift = 60u;

    for (;;) {
        PresentWriterChar(text, digits[(value >> shift) & 0xFu]);
        if (shift == 0u) {
            break;
        }
        shift -= 4u;
    }
}

static void PresentWriterSize(PresentWriter *text, uint32_t width, uint32_t height)
{
    PresentWriterU64(text, (uint64_t)width);
    PresentWriterChar(text, 'x');
    PresentWriterU64(text, (uint64_t)height);
}

static void PresentWriterRect(PresentWriter *text, LayoutRect rect)
{
    PresentWriterI64(text, (int64_t)rect.x);
    PresentWriterChar(text, ',');
    PresentWriterI64(text, (int64_t)rect.y);
    PresentWriterChar(text, ',');
    PresentWriterI64(text, (int64_t)rect.width);
    PresentWriterChar(text, ',');
    PresentWriterI64(text, (int64_t)rect.height);
}

static void PresentWriterField(PresentWriter *text, const char *name, uint64_t value)
{
    PresentWriterChar(text, ' ');
    PresentWriterString(text, name);
    PresentWriterChar(text, '=');
    PresentWriterU64(text, value);
}

static void PresentWriterLine(PresentWriter *text, const char *label)
{
    PresentWriterString(text, "  ");
    PresentWriterString(text, label);
}

/** The format's own name, from the pixel-format layer rather than from a table here. */
static void PresentWriterFormat(PresentWriter *text, GfxPixelFormat format)
{
    GfxPixelFormatInfo info;

    memset(&info, 0, sizeof(info));
    if (GetGfxPixelFormatInfo(format, &info) == RoboOk && info.name) {
        PresentWriterString(text, info.name);
    } else {
        PresentWriterString(text, "unknown");
    }
}

/* ------------------------------------------------------------------ */
/* Validation shared by the three doors                                */
/* ------------------------------------------------------------------ */

/** Non-zero when @p dest may be written (WRITE or READ_WRITE). */
static int PresentDestIsWritable(const GfxSurface *dest)
{
    GfxSurfaceAccess access = MakeGfxSurfaceAccess(dest);

    return access == RoboGfxSurfaceAccessWrite || access == RoboGfxSurfaceAccessReadWrite;
}

/**
 * The frame's own rectangle at the requested origin, and the destination's pixel bounds.
 * Both are needed by the region door and the present door, so they are resolved once here.
 */
static Result PresentResolve(const RenderTarget *frame,
                                    const GfxSurface *dest,
                                    int32_t x,
                                    int32_t y,
                                    GfxRect *outFrameBounds,
                                    GfxRect *outDestBounds,
                                    RenderDiag *diag)
{
    GfxPixelFormat format;

    if (!frame || !dest || !outFrameBounds || !outDestBounds) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboInvalidArgument,
                                "a frame and a destination are required");
    }

    format = GfxSurfaceFormat(dest);
    if (!GfxPixelFormatIsImplemented(format)) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboNotSupported,
                                "the destination format is not one this build can write");
    }

    *outFrameBounds = MakeGfxRect(x, y, (int32_t)RenderTargetWidth(frame),
                                      (int32_t)RenderTargetHeight(frame));
    *outDestBounds = MakeGfxRect(0, 0, (int32_t)GfxSurfaceWidth(dest),
                                     (int32_t)GfxSurfaceHeight(dest));
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void RenderPresentRecordInit(RenderPresentRecord *record)
{
    if (record) {
        memset(record, 0, sizeof(*record));
        record->destFormat = RoboGfxFormatUnknown;
        record->presented = GfxRectEmpty();
    }
}

int RenderPresentRecordIsValid(const RenderPresentRecord *record)
{
    size_t framePixels;

    if (!record) {
        return 0;
    }
    if (record->frameWidth == 0u || record->frameHeight == 0u) {
        return 0;
    }
    if (record->frameWidth > RoboRenderMaxDimension ||
        record->frameHeight > RoboRenderMaxDimension) {
        return 0;
    }
    /* A target this module could have made is in the one format it makes. A record describing a
     * presentation *out of* something else is describing something that cannot exist. */
    if (record->destFormat != RoboRenderTargetFormat) {
        return 0;
    }
    if (!GfxRectIsValid(record->presented)) {
        return 0;
    }
    /* The counts are the frame's, split in two: anything else is a record whose fields disagree. */
    framePixels = (size_t)record->frameWidth * (size_t)record->frameHeight;
    if (record->presentedPixels + record->clippedPixels != framePixels) {
        return 0;
    }
    if (record->presentedPixels >
        (size_t)record->presented.width * (size_t)record->presented.height) {
        return 0;
    }
    if (record->complete != (record->clippedPixels == 0u ? 1 : 0)) {
        return 0;
    }
    return 1;
}

int RenderPresentRecordEqual(const RenderPresentRecord *a,
                                     const RenderPresentRecord *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->frameWidth == b->frameWidth && a->frameHeight == b->frameHeight &&
           a->destWidth == b->destWidth && a->destHeight == b->destHeight &&
           a->destFormat == b->destFormat &&
           GfxRectEqual(a->presented, b->presented) &&
           a->presentedPixels == b->presentedPixels &&
           a->clippedPixels == b->clippedPixels && a->offsetX == b->offsetX &&
           a->offsetY == b->offsetY && a->complete == b->complete &&
           a->frameChecksum == b->frameChecksum;
}

/* ------------------------------------------------------------------ */
/* The region door                                                     */
/* ------------------------------------------------------------------ */

Result RenderPresentRegion(const RenderTarget *frame,
                                         const GfxSurface *dest,
                                         int32_t x,
                                         int32_t y,
                                         LayoutRect *out,
                                         RenderDiag *diag)
{
    GfxRect frameBounds;
    GfxRect destBounds;
    GfxRect meeting;
    Result status;

    if (!out) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboInvalidArgument,
                                "an out rectangle is required");
    }
    status = PresentResolve(frame, dest, x, y, &frameBounds, &destBounds, diag);
    if (status != RoboOk) {
        return status;
    }

    /* An empty intersection is an answer rather than a failure: a frame and a destination that do not
     * meet is a legitimate state (a window scrolled entirely off, a zero-height work area), and every
     * drawing in this tree treats it the same way. */
    if (GfxRectIntersect(frameBounds, destBounds, &meeting) != RoboOk) {
        *out = GfxRectEmpty();
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboError,
                                "the frame and destination rectangles could not be intersected");
    }
    *out = meeting;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The present door                                                    */
/* ------------------------------------------------------------------ */

Result RenderPresent(const RenderTarget *frame,
                                  GfxSurface *dest,
                                  int32_t x,
                                  int32_t y,
                                  RenderPresentRecord *outRecord,
                                  RenderDiag *diag)
{
    GfxRect frameBounds;
    GfxRect destBounds;
    GfxRect meeting;
    RenderPresentRecord record;
    size_t framePixels;
    Result status;

    status = PresentResolve(frame, dest, x, y, &frameBounds, &destBounds, diag);
    if (status != RoboOk) {
        return status;
    }
    if (!PresentDestIsWritable(dest)) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboInvalidArgument,
                                "the destination surface is not write-capable");
    }

    RenderPresentRecordInit(&record);
    record.frameWidth = RenderTargetWidth(frame);
    record.frameHeight = RenderTargetHeight(frame);
    record.destWidth = GfxSurfaceWidth(dest);
    record.destHeight = GfxSurfaceHeight(dest);
    record.destFormat = GfxSurfaceFormat(dest);
    record.offsetX = x;
    record.offsetY = y;
    record.frameChecksum = RenderTargetChecksum(frame);

    framePixels = RenderTargetPixelCount(frame);

    if (GfxRectIntersect(frameBounds, destBounds, &meeting) != RoboOk) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboError,
                                "the frame and destination rectangles could not be intersected");
    }
    record.presented = meeting;

    if (GfxRectIsEmpty(meeting)) {
        /* Nothing to write and nothing to say but the truth about it: the whole frame was clipped. */
        record.presentedPixels = 0u;
        record.clippedPixels = framePixels;
        record.complete = 0;
        if (outRecord) {
            *outRecord = record;
        }
        return RoboOk;
    }

    /*
     * The frame's **own** bounds as the *source* rectangle, and the origin as the *destination* point,
     * so the clip is entirely the composite's decision (see the file comment). The two rectangles are
     * in two coordinate spaces and conflating them is the one bug this call can have: `frameBounds`
     * above is the frame *placed* on the destination -- which is what the intersection was computed
     * from -- while `RenderTargetBounds(frame)` is the frame in its own pixels. Passing the
     * former as the source would apply the origin twice and read outside the frame.
     *
     * The opacity is full because the frame already carries whatever alpha it was drawn with.
     */
    status = GfxSurfaceCompositeRect(RenderTargetSurfaceConst(frame),
                                             RenderTargetBounds(frame), dest,
                                             MakeGfxPoint(x, y), 255u);
    if (status != RoboOk) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, status,
                                "the frame could not be composited into the destination");
    }

    record.presentedPixels =
        (size_t)meeting.width * (size_t)meeting.height;
    record.clippedPixels = framePixels - record.presentedPixels;
    record.complete = (record.clippedPixels == 0u) ? 1 : 0;

    if (outRecord) {
        *outRecord = record;
    }
    return RoboOk;
}

Result RenderPresentFramebuffer(const RenderTarget *frame,
                                              GfxFramebuffer *fb,
                                              int32_t x,
                                              int32_t y,
                                              RenderPresentRecord *outRecord,
                                              RenderDiag *diag)
{
    GfxSurfaceDesc desc;
    GfxSurface *borrowed = NULL;
    Result status;

    if (!fb) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboInvalidArgument,
                                "a framebuffer is required");
    }
    if (GfxFramebufferPixels(fb) == NULL) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboInvalidArgument,
                                "the framebuffer has no storage to write");
    }

    /*
     * A **borrowed** surface: the mapping belongs to whoever built the framebuffer, and nothing here
     * outlives the call. The geometry comes from the framebuffer's own accessors rather than from a
     * caller's restatement of them, so a framebuffer whose stride is not its width times its pixel size
     * still presents correctly.
     */
    GfxSurfaceDescInit(&desc);
    desc.width = GfxFramebufferWidth(fb);
    desc.height = GfxFramebufferHeight(fb);
    desc.format = GfxFramebufferFormat(fb);
    desc.strideBytes = GfxFramebufferStride(fb);
    desc.access = RoboGfxSurfaceAccessReadWrite;

    status = GfxSurfaceCreateBorrowed(&desc, GfxFramebufferPixels(fb), &borrowed);
    if (status != RoboOk || !borrowed) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, status,
                                "the framebuffer could not be viewed as a surface");
    }

    status = RenderPresent(frame, borrowed, x, y, outRecord, diag);
    GfxSurfaceRelease(borrowed);
    return status;
}

int RenderPresentationIsImplemented(void)
{
    return RoboRenderPresentationImplemented;
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

Result RenderPresentWrite(const RenderPresentRecord *record,
                                       char *buffer,
                                       size_t capacity,
                                       size_t *outLength,
                                       RenderDiag *diag)
{
    PresentWriter text;

    if (!record || !buffer || !outLength || capacity == 0u) {
        return RoboRenderDiag(diag, RoboRenderDomainDump, RoboInvalidArgument,
                                "a record, a buffer and an out_length are required");
    }

    PresentWriterInit(&text, buffer, capacity);

    PresentWriterString(&text, "present ");
    PresentWriterSize(&text, record->frameWidth, record->frameHeight);
    PresentWriterChar(&text, ' ');
    PresentWriterFormat(&text, RoboRenderTargetFormat);
    PresentWriterString(&text, " -> ");
    PresentWriterSize(&text, record->destWidth, record->destHeight);
    PresentWriterChar(&text, ' ');
    PresentWriterFormat(&text, record->destFormat);
    PresentWriterString(&text, " at ");
    PresentWriterI64(&text, (int64_t)record->offsetX);
    PresentWriterChar(&text, ',');
    PresentWriterI64(&text, (int64_t)record->offsetY);
    PresentWriterChar(&text, '\n');

    PresentWriterLine(&text, "presented=");
    PresentWriterRect(&text, record->presented);
    PresentWriterField(&text, "pixels", (uint64_t)record->presentedPixels);
    PresentWriterField(&text, "clipped", (uint64_t)record->clippedPixels);
    PresentWriterChar(&text, ' ');
    PresentWriterString(&text, "complete=");
    PresentWriterChar(&text, record->complete ? '1' : '0');
    PresentWriterChar(&text, '\n');

    PresentWriterLine(&text, "frame_checksum=");
    PresentWriterHex64(&text, record->frameChecksum);
    PresentWriterChar(&text, '\n');

    PresentWriterLine(&text, "record=");
    PresentWriterString(&text, RenderPresentRecordIsValid(record) ? "valid" : "invalid");
    PresentWriterChar(&text, '\n');

    *outLength = text.length;
    if (text.length + 1u > text.capacity) {
        return RoboRenderDiag(diag, RoboRenderDomainDump, RoboNotSupported,
                                "the presentation dump does not fit the buffer");
    }
    return RoboOk;
}
