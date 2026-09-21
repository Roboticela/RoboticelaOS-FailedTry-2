/*
 * RoboRender rasterizer (Phase 11.12) — see RoboRenderRasterizer.h.
 *
 * The module is four things and the file is organised as those four, because each one has one
 * decision in it and the decisions are what a later reader would otherwise "simplify":
 *
 *   **the target**    an owned surface plus the four facts a frame is described in (width, height,
 *                     format, stride) and two doors that make a picture comparable (`checksum`,
 *                     `equal`). It is the *only* allocation this module makes, and it belongs to the
 *                     caller -- which is why there is no module lifecycle to go with it.
 *
 *   **the preflight** one pass over values, before the first pixel, answering "can this rasteriser
 *                     execute all of this list?". A refusal here is what makes a returned frame mean
 *                     *the whole list was drawn*, and that is the property the region path's equality
 *                     claim rests on: a bounded pass and a full pass differ in the clip and in nothing
 *                     else, which is only checkable if neither can stop early.
 *
 *   **the region policy**  one run over the **union** of a set's regions, never one run per region.
 *                     Overlapping regions are legal in a set, and executing the list once per region
 *                     would composite a half-transparent drawing twice over the overlap. This is the
 *                     single most important line in the file to *not* optimise without thinking: the
 *                     cheaper loop produces a wrong pixel, not a slower frame.
 *
 *   **the frame record**  a struct of counts, filled from the pass's own record (embedded, not
 *                     copied field by field), plus the region arithmetic and the target's checksum
 *                     after the run. Nothing in it is derived twice: `regionPixels` is computed from
 *                     the rectangle that was actually executed *after* clamping, and `checksum` is
 *                     read from the target rather than predicted.
 *
 * The one thing this file does **not** have is a walk of the display list. That walks once, in
 * `RoboPaintDecoration.c`, and this module calls it through two doors -- which is the decision the
 * module's boundary is built on: this module owns what a *frame* is, the module below owns what a
 * *command* means, and a second executor would be a second answer to the second question.
 */

#include "RoboRender/RoboRenderRasterizer.h"

#include "RoboRender/RoboRenderVersion.h"

#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboMem/RoboMem.h"
#include "RoboPaint/RoboPaintDecoration.h"

#include <string.h>

struct RoboRenderTarget {
    /** The pixels. Owned: created here and released on destroy. */
    GfxSurface *surface;
    uint32_t width;
    uint32_t height;
    GfxPixelFormat format;
    size_t stride;
    size_t byteSize;
};

/* ------------------------------------------------------------------ */
/* Small arithmetic                                                    */
/* ------------------------------------------------------------------ */

/** FNV-1a's 64-bit parameters, stated rather than inlined: they are the algorithm. */
#define RoboRenderFnvOffsetBasis 14695981039346656037ull
#define RoboRenderFnvPrime 1099511628211ull

/**
 * FNV-1a over @p size bytes.
 *
 * The checksum door's whole implementation, and the choice of a *non-cryptographic* hash is
 * deliberate: this is a comparison and a test's expectation, not a signature, and a hash whose speed
 * depended on the platform's crypto library would make a frame's cost depend on something a caller
 * never asked about. The arithmetic is 64-bit unsigned, so it is defined by the standard rather than
 * by the implementation -- which is what makes a checksum a *value* two builds can agree on.
 */
static uint64_t RenderChecksum(const uint8_t *bytes, size_t size)
{
    uint64_t hash = RoboRenderFnvOffsetBasis;
    size_t index;

    for (index = 0u; index < size; index++) {
        hash ^= (uint64_t)bytes[index];
        hash *= RoboRenderFnvPrime;
    }
    return hash;
}

/** The pixels of @p target, or NULL for a NULL target. */
static const uint8_t *RenderTargetPixelBytes(const RenderTarget *target)
{
    if (!target || !target->surface) {
        return NULL;
    }
    return GfxSurfacePixelsConst(target->surface);
}

/** Bytes per pixel of @p format, or 0 when the format is not one this build can measure. */
static size_t RenderFormatBpp(GfxPixelFormat format)
{
    uint8_t bpp = 0u;

    if (GfxPixelFormatBytesPerPixel(format, &bpp) != RoboOk) {
        return 0u;
    }
    return (size_t)bpp;
}

/**
 * The intersection of @p rect and @p bounds, both in paint coordinates; **empty when they miss**.
 *
 * Written out rather than taken from a helper because the two operands are different kinds of
 * rectangle: @p rect is a region a caller named (a set's union, or a rectangle of their own), and
 * @p bounds is the target's own pixel area -- and the answer is what `regionPixels` counts, so this
 * is the one piece of arithmetic in the region path that must not be approximate. The four edges are
 * summed in 64 bits because each operand is a bounded `int32` and a sum of two of them is not.
 *
 * A miss is reported as an **empty rectangle** rather than as a negative extent, which is the shape
 * every rectangle in this stack uses for "nowhere": it keeps the record a *rectangle* (`width >= 0`,
 * which `RenderFrameIsValid` requires) and it keeps the region-run shortcut a single test. It
 * also means the non-empty case needs no clamping -- `left < right <= boundsRight` bounds every edge
 * by the target, which is at most `RoboRenderMaxDimension`.
 */
static LayoutRect RenderIntersect(LayoutRect rect, LayoutRect bounds)
{
    int64_t left = rect.x > bounds.x ? (int64_t)rect.x : (int64_t)bounds.x;
    int64_t top = rect.y > bounds.y ? (int64_t)rect.y : (int64_t)bounds.y;
    int64_t rectRight = (int64_t)rect.x + (int64_t)rect.width;
    int64_t boundsRight = (int64_t)bounds.x + (int64_t)bounds.width;
    int64_t rectBottom = (int64_t)rect.y + (int64_t)rect.height;
    int64_t boundsBottom = (int64_t)bounds.y + (int64_t)bounds.height;
    int64_t right = rectRight < boundsRight ? rectRight : boundsRight;
    int64_t bottom = rectBottom < boundsBottom ? rectBottom : boundsBottom;
    LayoutRect out;

    if (right <= left || bottom <= top) {
        return GfxRectEmpty();
    }
    out.x = (GfxPixelX)left;
    out.y = (GfxPixelY)top;
    out.width = (int32_t)(right - left);
    out.height = (int32_t)(bottom - top);
    return out;
}

/** Pixels in @p rect: its area, or 0 for a rectangle with no extent. */
static size_t RenderArea(LayoutRect rect)
{
    if (rect.width <= 0 || rect.height <= 0) {
        return 0u;
    }
    /* Both factors are bounded and positive, and a rectangle that reached here was clamped to a
     * target whose every dimension creation checked against RoboRenderMaxDimension -- so the
     * product cannot overflow. */
    return (size_t)rect.width * (size_t)rect.height;
}

/* ------------------------------------------------------------------ */
/* The target                                                          */
/* ------------------------------------------------------------------ */

Result RenderTargetCreate(RenderTarget **outTarget,
                                       uint32_t width,
                                       uint32_t height,
                                       RenderDiag *diag)
{
    GfxSurfaceDesc desc;
    RenderTarget *target;

    if (!outTarget) {
        return RoboRenderDiag(diag, RoboRenderDomainTarget, RoboInvalidArgument,
                                "an out_target is required");
    }
    if (width == 0u || height == 0u) {
        return RoboRenderDiag(diag, RoboRenderDomainTarget, RoboInvalidArgument,
                                "a target with no pixels cannot hold a frame");
    }
    if (width > RoboRenderMaxDimension || height > RoboRenderMaxDimension) {
        return RoboRenderDiag(diag, RoboRenderDomainTarget, RoboInvalidArgument,
                                "a target dimension is above ROBO_RENDER_MAX_DIMENSION");
    }

    GfxSurfaceDescInit(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = RoboRenderTargetFormat;
    /* Tightly packed: `0` asks the graphics layer for its minimum stride, which is what makes a
     * frame's bytes a function of its size and format rather than of an alignment this module
     * chose. */
    desc.strideBytes = 0u;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    {
        GfxSurface *surface = NULL;
        Result status = GfxSurfaceCreate(&desc, &surface);

        if (status == RoboOutOfMemory) {
            return RoboRenderDiag(diag, RoboRenderDomainTarget, RoboOutOfMemory,
                                    "the target's pixels could not be allocated");
        }
        if (status != RoboOk || !surface) {
            /* Unreachable: the format is this module's own constant and the dimensions were checked
             * above. Reported rather than trusted, because a silent NULL here would be a target that
             * exists and holds nothing. */
            return RoboRenderDiag(diag, RoboRenderDomainTarget, RoboNotSupported,
                                    "the graphics layer refused a target of this format");
        }

        target = (RenderTarget *)Realloc(NULL, sizeof(*target));
        if (!target) {
            GfxSurfaceRelease(surface);
            return RoboRenderDiag(diag, RoboRenderDomainTarget, RoboOutOfMemory,
                                    "the target record could not be allocated");
        }
        target->surface = surface;
        target->width = GfxSurfaceWidth(surface);
        target->height = GfxSurfaceHeight(surface);
        target->format = GfxSurfaceFormat(surface);
        target->stride = GfxSurfaceStride(surface);
        target->byteSize = GfxSurfaceByteSize(surface);
    }

    *outTarget = target;
    return RoboOk;
}

void RenderTargetDestroy(RenderTarget *target)
{
    if (!target) {
        return;
    }
    GfxSurfaceRelease(target->surface);
    RoboFree(target);
}

uint32_t RenderTargetWidth(const RenderTarget *target)
{
    return target ? target->width : 0u;
}

uint32_t RenderTargetHeight(const RenderTarget *target)
{
    return target ? target->height : 0u;
}

GfxPixelFormat RenderTargetFormat(const RenderTarget *target)
{
    return target ? target->format : RoboGfxFormatUnknown;
}

size_t RenderTargetStride(const RenderTarget *target)
{
    return target ? target->stride : 0u;
}

size_t RenderTargetByteSize(const RenderTarget *target)
{
    return target ? target->byteSize : 0u;
}

size_t RenderTargetPixelCount(const RenderTarget *target)
{
    if (!target) {
        return 0u;
    }
    return (size_t)target->width * (size_t)target->height;
}

LayoutRect RenderTargetBounds(const RenderTarget *target)
{
    if (!target) {
        return GfxRectEmpty();
    }
    return MakeGfxRect(0, 0, (int32_t)target->width, (int32_t)target->height);
}

GfxSurface *RenderTargetSurface(RenderTarget *target)
{
    return target ? target->surface : NULL;
}

const GfxSurface *RenderTargetSurfaceConst(const RenderTarget *target)
{
    return target ? target->surface : NULL;
}

const uint8_t *RoboRenderTargetBytes(const RenderTarget *target, size_t *outSize)
{
    const uint8_t *bytes = RenderTargetPixelBytes(target);

    if (outSize) {
        if (bytes) {
            *outSize = target->byteSize;
        } else {
            *outSize = 0u;
        }
    }
    return bytes;
}

Result RenderTargetClear(RenderTarget *target,
                                      GfxColorRgba8 color,
                                      RenderDiag *diag)
{
    if (!target || !target->surface) {
        return RoboRenderDiag(diag, RoboRenderDomainTarget, RoboInvalidArgument,
                                "a target is required");
    }
    /*
     * Replaced, never blended. `GfxSurfaceClearRgba8` packs the colour into every pixel
     * including its alpha, which is what makes a baseline a *state* rather than a step: a caller can
     * say what a frame starts from without knowing what was there, and a second clear is the same
     * picture as the first.
     */
    if (GfxSurfaceClearRgba8(target->surface, color) != RoboOk) {
        return RoboRenderDiag(diag, RoboRenderDomainTarget, RoboNotSupported,
                                "the target's surface could not be cleared");
    }
    return RoboOk;
}

uint64_t RenderTargetChecksum(const RenderTarget *target)
{
    const uint8_t *bytes = RenderTargetPixelBytes(target);

    if (!bytes) {
        return 0u;
    }
    return RenderChecksum(bytes, target->byteSize);
}

int RenderTargetEqual(const RenderTarget *a, const RenderTarget *b)
{
    const uint8_t *bytesA;
    const uint8_t *bytesB;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->width != b->width || a->height != b->height || a->format != b->format ||
        a->stride != b->stride) {
        return 0;
    }
    bytesA = RenderTargetPixelBytes(a);
    bytesB = RenderTargetPixelBytes(b);
    if (!bytesA || !bytesB) {
        /* Both surfaces exist by construction; a NULL here means one of them lost its backing, and
         * "not equal" is the honest answer rather than reading a wild pointer. */
        return 0;
    }
    return memcmp(bytesA, bytesB, a->byteSize) == 0;
}

/* ------------------------------------------------------------------ */
/* The frame record                                                    */
/* ------------------------------------------------------------------ */

void RenderFrameInit(RenderFrame *frame)
{
    if (!frame) {
        return;
    }
    memset(frame, 0, sizeof(*frame));
    frame->format = RoboGfxFormatUnknown;
}

int RenderFrameIsValid(const RenderFrame *frame)
{
    size_t bpp;
    size_t pixels;

    if (!frame) {
        return 0;
    }
    if (frame->width == 0u || frame->height == 0u) {
        return 0;
    }
    if (frame->width > RoboRenderMaxDimension || frame->height > RoboRenderMaxDimension) {
        return 0;
    }
    if (frame->format != RoboRenderTargetFormat) {
        return 0;
    }
    bpp = RenderFormatBpp(frame->format);
    if (bpp == 0u || frame->stride < (size_t)frame->width * bpp) {
        return 0;
    }
    pixels = (size_t)frame->width * (size_t)frame->height;
    if (frame->targetPixels != pixels) {
        return 0;
    }
    if (frame->region.width < 0 || frame->region.height < 0) {
        return 0;
    }
    if (frame->regionPixels > frame->targetPixels) {
        return 0;
    }
    /* The two shapes a record can be, and nothing in between: a run that executed the whole target,
     * or a bounded one. A record that claimed to be bounded while reporting no regions and a whole
     * target's worth of pixels would be describing a full frame with the wrong flag. */
    if (!frame->limited) {
        if (frame->regions != 0u) {
            return 0;
        }
        if (frame->region.x != 0 || frame->region.y != 0 ||
            frame->region.width != (int32_t)frame->width ||
            frame->region.height != (int32_t)frame->height) {
            return 0;
        }
        if (frame->regionPixels != frame->targetPixels) {
            return 0;
        }
    }
    return 1;
}

int RenderFrameEqual(const RenderFrame *a, const RenderFrame *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return memcmp(a, b, sizeof(*a)) == 0;
}

/* ------------------------------------------------------------------ */
/* The preflight                                                       */
/* ------------------------------------------------------------------ */

/**
 * Answer whether this rasteriser can execute **all** of @p list, before a pixel is written.
 *
 * Two rules, and each is one the walk below it would otherwise discover too late:
 *
 *   every command must be a **record this build executes** -- a known kind whose fields agree with
 *   it (`PaintDisplayCommandIsValid`). A command carrying a kind this build does not define
 *   is refused, which is the brief's §5 stated as behaviour: an unsupported command is never silently
 *   reinterpreted, and the list is not drawn half-way;
 *
 *   the two **scope pairs must balance** and must not close what nothing opened. The paint walk
 *   refuses an unbalanced pair too (it must, or a consumer would draw under a boundary that is not in
 *   force), but it meets the list *while drawing*, so by the time it refused, earlier drawings would
 *   already be in the target. Refusing here is what makes a failed frame leave the target exactly as
 *   it was found -- and "the target holds either the whole frame or nothing" is what a caller
 *   comparing frames, or a suite asserting equality, is entitled to assume.
 *
 * It is one pass over values and allocates nothing. The equivalent it deliberately does **not**
 * attempt is proving a list is a well-formed *document* -- a `content` marker under the wrong box, a
 * scope wider than the boundary outside it, a clip command whose rectangle disagrees with its
 * contents. Every one of those is the **builder's** refusal (`RoboPaintDisplayListBuilder.h`), where
 * the tree is still available; re-deriving them from a flat list here would be a second authority on
 * what a list may contain, and the two would disagree about a document before they disagreed about a
 * pixel.
 */
static Result RenderListPreflight(const PaintDisplayList *list,
                                           RenderDiag *diag)
{
    size_t count = PaintDisplayListCount(list);
    size_t clipDepth = 0u;
    size_t opacityDepth = 0u;
    size_t index;

    for (index = 0u; index < count; index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            /* Unreachable: the index came from the list's own count. */
            return RoboRenderDiag(diag, RoboRenderDomainList, RoboError,
                                    "the display list changed under the frame");
        }
        if (!PaintDisplayCommandKindIsKnown(command.kind)) {
            return RoboRenderDiag(diag, RoboRenderDomainList, RoboNotSupported,
                                    "the display list carries a command kind this rasteriser does "
                                    "not execute");
        }
        if (!PaintDisplayCommandIsValid(&command)) {
            return RoboRenderDiag(diag, RoboRenderDomainList, RoboNotSupported,
                                    "the display list carries a command whose fields do not "
                                    "describe one");
        }

        if (command.kind == RoboPaintDisplayCmdPushClip) {
            clipDepth++;
        } else if (command.kind == RoboPaintDisplayCmdPopClip) {
            if (clipDepth == 0u) {
                return RoboRenderDiag(diag, RoboRenderDomainList, RoboNotSupported,
                                        "the display list closes a clip scope nothing opened");
            }
            clipDepth--;
        } else if (command.kind == RoboPaintDisplayCmdPushOpacity) {
            opacityDepth++;
        } else if (command.kind == RoboPaintDisplayCmdPopOpacity) {
            if (opacityDepth == 0u) {
                return RoboRenderDiag(diag, RoboRenderDomainList, RoboNotSupported,
                                        "the display list closes an opacity scope nothing opened");
            }
            opacityDepth--;
        }
    }

    if (clipDepth != 0u || opacityDepth != 0u) {
        return RoboRenderDiag(diag, RoboRenderDomainList, RoboNotSupported,
                                "the display list leaves a scope open at its end");
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Running a frame                                                     */
/* ------------------------------------------------------------------ */

/**
 * The one run both doors are.
 *
 * @p bound is the region a region run executes inside, in paint coordinates, or NULL for the
 * whole-frame door. @p regions and @p limited are what the record should say about *why* the run was
 * bounded -- and they are separate from @p bound because the empty-set case is a bounded run with no
 * rectangle at all: nothing is owed, so nothing is walked, which the record states as
 * `limited == 1`, `regions == 0`, an empty region and zero commands.
 *
 * The bound handed to the pass is the **clamped** intersection (`frame.region`), not the caller's
 * rectangle, and that is a decision rather than tidiness: the pass clamps to the surface anyway, and
 * handing it the value the record will report means the rectangle a caller reads *is* the rectangle
 * that was executed -- so "did my region reach the target?" is answerable from the frame alone.
 */
static Result RenderRun(const PaintDisplayList *list,
                                RenderTarget *target,
                                FontFace *face,
                                const LayoutRect *bound,
                                size_t regions,
                                int limited,
                                RenderFrame *outFrame,
                                RenderDiag *diag)
{
    RenderFrame frame;
    LayoutRect bounds;
    PaintDiag paintDiag;
    Result status;

    RenderFrameInit(&frame);
    bounds = RenderTargetBounds(target);
    frame.width = target->width;
    frame.height = target->height;
    frame.format = target->format;
    frame.stride = target->stride;
    frame.targetPixels = (size_t)target->width * (size_t)target->height;
    frame.regions = regions;
    frame.limited = limited;
    frame.region = bound ? RenderIntersect(*bound, bounds) : bounds;
    frame.regionPixels = RenderArea(frame.region);

    status = RenderListPreflight(list, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * Nothing owed, or nothing that reaches the target: a successful run that executes **nothing**.
     *
     * Both are the same conclusion -- "there is no pixel in this frame's region" -- and both are
     * success rather than an error, because they are conclusions this build reaches about documents
     * and callers all the time: a set that names nothing (11.11's own answer to "is the picture
     * current?"), and a region a caller computed for a target it has since resized. The record says
     * which shape it was: `regions == 0` for the first, `regions > 0` with an empty region for the
     * second, and `pass.commands == 0` for both.
     */
    if (bound != NULL && frame.regionPixels == 0u) {
        frame.checksum = RenderTargetChecksum(target);
        if (outFrame) {
            *outFrame = frame;
        }
        return RoboOk;
    }

    /*
     * The walk, one module down. `NULL` for the bound is the whole-frame pass (the two doors of
     * `RoboPaintDecoration.h` are one code path), and the paint module's own record is written
     * straight into this frame's embedded copy of it -- so the two never exist separately and cannot
     * describe the same run differently.
     */
    PaintDiagReset(&paintDiag);
    status = PaintDecorationPaintBounded(list, target->surface, face,
                                                 bound ? &frame.region : NULL,
                                                 &frame.pass, &paintDiag);
    if (status != RoboOk) {
        /*
         * The pass refused. Its record is carried into this module's diagnostic rather than
         * translated into a generic message: the failure could be a drawing that would not fill, a
         * boundary that would not apply or a raster that would not allocate, and the *message* is what
         * tells a caller which -- while the *domain* stays this module's, because the frame is what
         * failed to happen. The pass's own code is preserved exactly, so a caller switching on
         * RoboOutOfMemory still sees it.
         */
        return RoboRenderDiag(diag, RoboRenderDomainFrame, status,
                                paintDiag.message ? paintDiag.message
                                                   : "the paint pass refused the frame");
    }

    /* Read from the target rather than predicted: a checksum is only a claim about a picture if
     * something looked at the picture. */
    frame.checksum = RenderTargetChecksum(target);
    if (outFrame) {
        *outFrame = frame;
    }
    return RoboOk;
}

Result RenderRasterize(const PaintDisplayList *list,
                                   RenderTarget *target,
                                   FontFace *face,
                                   RenderFrame *outFrame,
                                   RenderDiag *diag)
{
    if (!list || !target) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboInvalidArgument,
                                "the display list and the target are required");
    }
    return RenderRun(list, target, face, NULL, 0u, 0, outFrame, diag);
}

Result RenderRasterizeRegions(const PaintDisplayList *list,
                                            RenderTarget *target,
                                            FontFace *face,
                                            const PaintInvalidationSet *set,
                                            RenderFrame *outFrame,
                                            RenderDiag *diag)
{
    LayoutRect bound;
    size_t regions = PaintInvalidationSetCount(set);

    if (!list || !target) {
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboInvalidArgument,
                                "the display list and the target are required");
    }

    /*
     * **Nothing owed.** 11.11's answer for an empty set is "the picture is current", so this is a
     * successful run that executes nothing: the target's bytes are untouched, the record says
     * `limited` with no regions and an empty rectangle, and the passed pass record stays zeroed.
     *
     * A caller that had to distinguish this from a failure would be reading a diagnostic to learn
     * something the record already states, which is the arrangement `paint/docs/Invalidation.md`
     * argues for one module down: a conclusion about a document is a value, not an error.
     */
    if (regions == 0u) {
        LayoutRect nothing = GfxRectEmpty();

        return RenderRun(list, target, face, &nothing, 0u, 1, outFrame, diag);
    }

    /*
     * **One run over the union**, and the reason is a wrong pixel rather than a slow frame: a set may
     * name overlapping regions, and a run per region would composite a semi-transparent drawing twice
     * over the overlap -- two half-transparent fills over one pixel are not one fill. The union can
     * only be too large (11.11 says so of this exact door), and a repaint over a superset of the
     * damage is correct for the same reason a full repaint is.
     */
    if (PaintInvalidationSetBounds(set, &bound) != RoboOk) {
        /* Unreachable: a non-empty set always has a bound. Reported rather than trusted, because a
         * silent empty rectangle here would be a region run that painted nothing. */
        return RoboRenderDiag(diag, RoboRenderDomainFrame, RoboError,
                                "a non-empty invalidation set produced no bounds");
    }
    return RenderRun(list, target, face, &bound, regions, 1, outFrame, diag);
}
