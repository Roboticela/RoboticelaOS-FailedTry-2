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
 *                     after the run. Nothing in it is derived twice: `region_pixels` is computed from
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

struct robo_render_target {
    /** The pixels. Owned: created here and released on destroy. */
    robo_gfx_surface_t *surface;
    uint32_t width;
    uint32_t height;
    robo_gfx_pixel_format_t format;
    size_t stride;
    size_t byte_size;
};

/* ------------------------------------------------------------------ */
/* Small arithmetic                                                    */
/* ------------------------------------------------------------------ */

/** FNV-1a's 64-bit parameters, stated rather than inlined: they are the algorithm. */
#define ROBO_RENDER_FNV_OFFSET_BASIS 14695981039346656037ull
#define ROBO_RENDER_FNV_PRIME 1099511628211ull

/**
 * FNV-1a over @p size bytes.
 *
 * The checksum door's whole implementation, and the choice of a *non-cryptographic* hash is
 * deliberate: this is a comparison and a test's expectation, not a signature, and a hash whose speed
 * depended on the platform's crypto library would make a frame's cost depend on something a caller
 * never asked about. The arithmetic is 64-bit unsigned, so it is defined by the standard rather than
 * by the implementation -- which is what makes a checksum a *value* two builds can agree on.
 */
static uint64_t render_checksum(const uint8_t *bytes, size_t size)
{
    uint64_t hash = ROBO_RENDER_FNV_OFFSET_BASIS;
    size_t index;

    for (index = 0u; index < size; index++) {
        hash ^= (uint64_t)bytes[index];
        hash *= ROBO_RENDER_FNV_PRIME;
    }
    return hash;
}

/** The pixels of @p target, or NULL for a NULL target. */
static const uint8_t *render_target_bytes(const robo_render_target_t *target)
{
    if (!target || !target->surface) {
        return NULL;
    }
    return robo_gfx_surface_pixels_const(target->surface);
}

/** Bytes per pixel of @p format, or 0 when the format is not one this build can measure. */
static size_t render_format_bpp(robo_gfx_pixel_format_t format)
{
    uint8_t bpp = 0u;

    if (robo_gfx_pixel_format_bytes_per_pixel(format, &bpp) != ROBO_OK) {
        return 0u;
    }
    return (size_t)bpp;
}

/**
 * The intersection of @p rect and @p bounds, both in paint coordinates; **empty when they miss**.
 *
 * Written out rather than taken from a helper because the two operands are different kinds of
 * rectangle: @p rect is a region a caller named (a set's union, or a rectangle of their own), and
 * @p bounds is the target's own pixel area -- and the answer is what `region_pixels` counts, so this
 * is the one piece of arithmetic in the region path that must not be approximate. The four edges are
 * summed in 64 bits because each operand is a bounded `int32` and a sum of two of them is not.
 *
 * A miss is reported as an **empty rectangle** rather than as a negative extent, which is the shape
 * every rectangle in this stack uses for "nowhere": it keeps the record a *rectangle* (`width >= 0`,
 * which `robo_render_frame_is_valid` requires) and it keeps the region-run shortcut a single test. It
 * also means the non-empty case needs no clamping -- `left < right <= bounds_right` bounds every edge
 * by the target, which is at most `ROBO_RENDER_MAX_DIMENSION`.
 */
static robo_layout_rect_t render_intersect(robo_layout_rect_t rect, robo_layout_rect_t bounds)
{
    int64_t left = rect.x > bounds.x ? (int64_t)rect.x : (int64_t)bounds.x;
    int64_t top = rect.y > bounds.y ? (int64_t)rect.y : (int64_t)bounds.y;
    int64_t rect_right = (int64_t)rect.x + (int64_t)rect.width;
    int64_t bounds_right = (int64_t)bounds.x + (int64_t)bounds.width;
    int64_t rect_bottom = (int64_t)rect.y + (int64_t)rect.height;
    int64_t bounds_bottom = (int64_t)bounds.y + (int64_t)bounds.height;
    int64_t right = rect_right < bounds_right ? rect_right : bounds_right;
    int64_t bottom = rect_bottom < bounds_bottom ? rect_bottom : bounds_bottom;
    robo_layout_rect_t out;

    if (right <= left || bottom <= top) {
        return robo_gfx_rect_empty();
    }
    out.x = (robo_gfx_pixel_x_t)left;
    out.y = (robo_gfx_pixel_y_t)top;
    out.width = (int32_t)(right - left);
    out.height = (int32_t)(bottom - top);
    return out;
}

/** Pixels in @p rect: its area, or 0 for a rectangle with no extent. */
static size_t render_area(robo_layout_rect_t rect)
{
    if (rect.width <= 0 || rect.height <= 0) {
        return 0u;
    }
    /* Both factors are bounded and positive, and a rectangle that reached here was clamped to a
     * target whose every dimension creation checked against ROBO_RENDER_MAX_DIMENSION -- so the
     * product cannot overflow. */
    return (size_t)rect.width * (size_t)rect.height;
}

/* ------------------------------------------------------------------ */
/* The target                                                          */
/* ------------------------------------------------------------------ */

robo_result_t robo_render_target_create(robo_render_target_t **out_target,
                                       uint32_t width,
                                       uint32_t height,
                                       robo_render_diag_t *diag)
{
    robo_gfx_surface_desc_t desc;
    robo_render_target_t *target;

    if (!out_target) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                                "an out_target is required");
    }
    if (width == 0u || height == 0u) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                                "a target with no pixels cannot hold a frame");
    }
    if (width > ROBO_RENDER_MAX_DIMENSION || height > ROBO_RENDER_MAX_DIMENSION) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                                "a target dimension is above ROBO_RENDER_MAX_DIMENSION");
    }

    robo_gfx_surface_desc_init(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = ROBO_RENDER_TARGET_FORMAT;
    /* Tightly packed: `0` asks the graphics layer for its minimum stride, which is what makes a
     * frame's bytes a function of its size and format rather than of an alignment this module
     * chose. */
    desc.stride_bytes = 0u;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    {
        robo_gfx_surface_t *surface = NULL;
        robo_result_t status = robo_gfx_surface_create(&desc, &surface);

        if (status == ROBO_OUT_OF_MEMORY) {
            return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_TARGET, ROBO_OUT_OF_MEMORY,
                                    "the target's pixels could not be allocated");
        }
        if (status != ROBO_OK || !surface) {
            /* Unreachable: the format is this module's own constant and the dimensions were checked
             * above. Reported rather than trusted, because a silent NULL here would be a target that
             * exists and holds nothing. */
            return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_TARGET, ROBO_NOT_SUPPORTED,
                                    "the graphics layer refused a target of this format");
        }

        target = (robo_render_target_t *)robo_realloc(NULL, sizeof(*target));
        if (!target) {
            robo_gfx_surface_release(surface);
            return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_TARGET, ROBO_OUT_OF_MEMORY,
                                    "the target record could not be allocated");
        }
        target->surface = surface;
        target->width = robo_gfx_surface_width(surface);
        target->height = robo_gfx_surface_height(surface);
        target->format = robo_gfx_surface_format(surface);
        target->stride = robo_gfx_surface_stride(surface);
        target->byte_size = robo_gfx_surface_byte_size(surface);
    }

    *out_target = target;
    return ROBO_OK;
}

void robo_render_target_destroy(robo_render_target_t *target)
{
    if (!target) {
        return;
    }
    robo_gfx_surface_release(target->surface);
    robo_free(target);
}

uint32_t robo_render_target_width(const robo_render_target_t *target)
{
    return target ? target->width : 0u;
}

uint32_t robo_render_target_height(const robo_render_target_t *target)
{
    return target ? target->height : 0u;
}

robo_gfx_pixel_format_t robo_render_target_format(const robo_render_target_t *target)
{
    return target ? target->format : ROBO_GFX_FORMAT_UNKNOWN;
}

size_t robo_render_target_stride(const robo_render_target_t *target)
{
    return target ? target->stride : 0u;
}

size_t robo_render_target_byte_size(const robo_render_target_t *target)
{
    return target ? target->byte_size : 0u;
}

size_t robo_render_target_pixel_count(const robo_render_target_t *target)
{
    if (!target) {
        return 0u;
    }
    return (size_t)target->width * (size_t)target->height;
}

robo_layout_rect_t robo_render_target_bounds(const robo_render_target_t *target)
{
    if (!target) {
        return robo_gfx_rect_empty();
    }
    return robo_gfx_rect(0, 0, (int32_t)target->width, (int32_t)target->height);
}

robo_gfx_surface_t *robo_render_target_surface(robo_render_target_t *target)
{
    return target ? target->surface : NULL;
}

const robo_gfx_surface_t *robo_render_target_surface_const(const robo_render_target_t *target)
{
    return target ? target->surface : NULL;
}

const uint8_t *robo_render_target_bytes(const robo_render_target_t *target, size_t *out_size)
{
    const uint8_t *bytes = render_target_bytes(target);

    if (out_size) {
        if (bytes) {
            *out_size = target->byte_size;
        } else {
            *out_size = 0u;
        }
    }
    return bytes;
}

robo_result_t robo_render_target_clear(robo_render_target_t *target,
                                      robo_gfx_color_rgba8_t color,
                                      robo_render_diag_t *diag)
{
    if (!target || !target->surface) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                                "a target is required");
    }
    /*
     * Replaced, never blended. `robo_gfx_surface_clear_rgba8` packs the colour into every pixel
     * including its alpha, which is what makes a baseline a *state* rather than a step: a caller can
     * say what a frame starts from without knowing what was there, and a second clear is the same
     * picture as the first.
     */
    if (robo_gfx_surface_clear_rgba8(target->surface, color) != ROBO_OK) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_TARGET, ROBO_NOT_SUPPORTED,
                                "the target's surface could not be cleared");
    }
    return ROBO_OK;
}

uint64_t robo_render_target_checksum(const robo_render_target_t *target)
{
    const uint8_t *bytes = render_target_bytes(target);

    if (!bytes) {
        return 0u;
    }
    return render_checksum(bytes, target->byte_size);
}

int robo_render_target_equal(const robo_render_target_t *a, const robo_render_target_t *b)
{
    const uint8_t *bytes_a;
    const uint8_t *bytes_b;

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
    bytes_a = render_target_bytes(a);
    bytes_b = render_target_bytes(b);
    if (!bytes_a || !bytes_b) {
        /* Both surfaces exist by construction; a NULL here means one of them lost its backing, and
         * "not equal" is the honest answer rather than reading a wild pointer. */
        return 0;
    }
    return memcmp(bytes_a, bytes_b, a->byte_size) == 0;
}

/* ------------------------------------------------------------------ */
/* The frame record                                                    */
/* ------------------------------------------------------------------ */

void robo_render_frame_init(robo_render_frame_t *frame)
{
    if (!frame) {
        return;
    }
    memset(frame, 0, sizeof(*frame));
    frame->format = ROBO_GFX_FORMAT_UNKNOWN;
}

int robo_render_frame_is_valid(const robo_render_frame_t *frame)
{
    size_t bpp;
    size_t pixels;

    if (!frame) {
        return 0;
    }
    if (frame->width == 0u || frame->height == 0u) {
        return 0;
    }
    if (frame->width > ROBO_RENDER_MAX_DIMENSION || frame->height > ROBO_RENDER_MAX_DIMENSION) {
        return 0;
    }
    if (frame->format != ROBO_RENDER_TARGET_FORMAT) {
        return 0;
    }
    bpp = render_format_bpp(frame->format);
    if (bpp == 0u || frame->stride < (size_t)frame->width * bpp) {
        return 0;
    }
    pixels = (size_t)frame->width * (size_t)frame->height;
    if (frame->target_pixels != pixels) {
        return 0;
    }
    if (frame->region.width < 0 || frame->region.height < 0) {
        return 0;
    }
    if (frame->region_pixels > frame->target_pixels) {
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
        if (frame->region_pixels != frame->target_pixels) {
            return 0;
        }
    }
    return 1;
}

int robo_render_frame_equal(const robo_render_frame_t *a, const robo_render_frame_t *b)
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
 *   it (`robo_paint_display_command_is_valid`). A command carrying a kind this build does not define
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
static robo_result_t render_list_preflight(const robo_paint_display_list_t *list,
                                           robo_render_diag_t *diag)
{
    size_t count = robo_paint_display_list_count(list);
    size_t clip_depth = 0u;
    size_t opacity_depth = 0u;
    size_t index;

    for (index = 0u; index < count; index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            /* Unreachable: the index came from the list's own count. */
            return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_LIST, ROBO_ERROR,
                                    "the display list changed under the frame");
        }
        if (!robo_paint_display_command_kind_is_known(command.kind)) {
            return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_LIST, ROBO_NOT_SUPPORTED,
                                    "the display list carries a command kind this rasteriser does "
                                    "not execute");
        }
        if (!robo_paint_display_command_is_valid(&command)) {
            return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_LIST, ROBO_NOT_SUPPORTED,
                                    "the display list carries a command whose fields do not "
                                    "describe one");
        }

        if (command.kind == ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP) {
            clip_depth++;
        } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_POP_CLIP) {
            if (clip_depth == 0u) {
                return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_LIST, ROBO_NOT_SUPPORTED,
                                        "the display list closes a clip scope nothing opened");
            }
            clip_depth--;
        } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY) {
            opacity_depth++;
        } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_POP_OPACITY) {
            if (opacity_depth == 0u) {
                return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_LIST, ROBO_NOT_SUPPORTED,
                                        "the display list closes an opacity scope nothing opened");
            }
            opacity_depth--;
        }
    }

    if (clip_depth != 0u || opacity_depth != 0u) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_LIST, ROBO_NOT_SUPPORTED,
                                "the display list leaves a scope open at its end");
    }
    return ROBO_OK;
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
static robo_result_t render_run(const robo_paint_display_list_t *list,
                                robo_render_target_t *target,
                                robo_font_face_t *face,
                                const robo_layout_rect_t *bound,
                                size_t regions,
                                int limited,
                                robo_render_frame_t *out_frame,
                                robo_render_diag_t *diag)
{
    robo_render_frame_t frame;
    robo_layout_rect_t bounds;
    robo_paint_diag_t paint_diag;
    robo_result_t status;

    robo_render_frame_init(&frame);
    bounds = robo_render_target_bounds(target);
    frame.width = target->width;
    frame.height = target->height;
    frame.format = target->format;
    frame.stride = target->stride;
    frame.target_pixels = (size_t)target->width * (size_t)target->height;
    frame.regions = regions;
    frame.limited = limited;
    frame.region = bound ? render_intersect(*bound, bounds) : bounds;
    frame.region_pixels = render_area(frame.region);

    status = render_list_preflight(list, diag);
    if (status != ROBO_OK) {
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
    if (bound != NULL && frame.region_pixels == 0u) {
        frame.checksum = robo_render_target_checksum(target);
        if (out_frame) {
            *out_frame = frame;
        }
        return ROBO_OK;
    }

    /*
     * The walk, one module down. `NULL` for the bound is the whole-frame pass (the two doors of
     * `RoboPaintDecoration.h` are one code path), and the paint module's own record is written
     * straight into this frame's embedded copy of it -- so the two never exist separately and cannot
     * describe the same run differently.
     */
    robo_paint_diag_reset(&paint_diag);
    status = robo_paint_decoration_paint_bounded(list, target->surface, face,
                                                 bound ? &frame.region : NULL,
                                                 &frame.pass, &paint_diag);
    if (status != ROBO_OK) {
        /*
         * The pass refused. Its record is carried into this module's diagnostic rather than
         * translated into a generic message: the failure could be a drawing that would not fill, a
         * boundary that would not apply or a raster that would not allocate, and the *message* is what
         * tells a caller which -- while the *domain* stays this module's, because the frame is what
         * failed to happen. The pass's own code is preserved exactly, so a caller switching on
         * ROBO_OUT_OF_MEMORY still sees it.
         */
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, status,
                                paint_diag.message ? paint_diag.message
                                                   : "the paint pass refused the frame");
    }

    /* Read from the target rather than predicted: a checksum is only a claim about a picture if
     * something looked at the picture. */
    frame.checksum = robo_render_target_checksum(target);
    if (out_frame) {
        *out_frame = frame;
    }
    return ROBO_OK;
}

robo_result_t robo_render_rasterize(const robo_paint_display_list_t *list,
                                   robo_render_target_t *target,
                                   robo_font_face_t *face,
                                   robo_render_frame_t *out_frame,
                                   robo_render_diag_t *diag)
{
    if (!list || !target) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_INVALID_ARGUMENT,
                                "the display list and the target are required");
    }
    return render_run(list, target, face, NULL, 0u, 0, out_frame, diag);
}

robo_result_t robo_render_rasterize_regions(const robo_paint_display_list_t *list,
                                            robo_render_target_t *target,
                                            robo_font_face_t *face,
                                            const robo_paint_invalidation_set_t *set,
                                            robo_render_frame_t *out_frame,
                                            robo_render_diag_t *diag)
{
    robo_layout_rect_t bound;
    size_t regions = robo_paint_invalidation_set_count(set);

    if (!list || !target) {
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_INVALID_ARGUMENT,
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
        robo_layout_rect_t nothing = robo_gfx_rect_empty();

        return render_run(list, target, face, &nothing, 0u, 1, out_frame, diag);
    }

    /*
     * **One run over the union**, and the reason is a wrong pixel rather than a slow frame: a set may
     * name overlapping regions, and a run per region would composite a semi-transparent drawing twice
     * over the overlap -- two half-transparent fills over one pixel are not one fill. The union can
     * only be too large (11.11 says so of this exact door), and a repaint over a superset of the
     * damage is correct for the same reason a full repaint is.
     */
    if (robo_paint_invalidation_set_bounds(set, &bound) != ROBO_OK) {
        /* Unreachable: a non-empty set always has a bound. Reported rather than trusted, because a
         * silent empty rectangle here would be a region run that painted nothing. */
        return ROBO_RENDER_DIAG(diag, ROBO_RENDER_DOMAIN_FRAME, ROBO_ERROR,
                                "a non-empty invalidation set produced no bounds");
    }
    return render_run(list, target, face, &bound, regions, 1, out_frame, diag);
}
