/*
 * RoboRender presentation (Phase 11.13).
 *
 * 		frame (owned pixels)  →  destination surface  →  the window / display path
 *
 * Phase 11.12 ended in a buffer and said so: `robo_render_target_surface` was named as *the door* for
 * presentation and the work was left to this phase. This is that work, and it is deliberately the
 * smallest thing that can be called presentation rather than a compositor or a swap chain.
 *
 * ## Why the door takes a surface and not a window
 *
 * The destination is a **write-capable RoboGraphics surface**, which is the narrowest type that can
 * mean "somewhere these pixels could go": a window's owned render surface, a display framebuffer
 * wrapped by its own module, an offscreen surface in a test, or the intermediate a later compositor
 * would use. Taking a window id would make this module depend on RoboWindow -- which
 * `Assert-RoboRenderBoundaries` refuses on purpose, because a rasteriser that could reach a window
 * could not be tested without one -- and taking a QEMU framebuffer would make it depend on RoboDisplay
 * for the same wrong reason. **A frame is pixels; where they land is the caller's choice**, and the two
 * callers this build has (the desktop's window path, the guest's framebuffer path) each reach this
 * door with the surface their own backend gave them.
 *
 * ## Clipping, not refusal, and why the record says which happened
 *
 * A frame that does not fit its destination is **clipped**: the rectangle outside the destination is
 * dropped and the rest is written. This is the tree's rule for every drawing (`robo_gfx_raster_fill_rect`
 * clips, `robo_gfx_surface_composite_rect` clips, and `robo_paint_decoration_paint_bounded` clips), and
 * it is the rule a resize needs: a window that shrank between the frame being rasterised and the frame
 * being presented must show the part of the picture that still fits rather than fail or blank itself.
 * What would *not* be acceptable is silence about it, so @ref robo_render_present_record_t carries both
 * the rectangle that was written and the number of frame pixels that were not -- a caller that wants
 * "the whole frame landed or nothing did" asks `complete`, and a caller that wants the truth about a
 * smaller window reads `presented`.
 *
 * ## What this is not
 *
 * It is not a compositor: it composites **one** frame into **one** destination at **one** offset, with
 * the source-over blend the graphics module already owns, and it has no layer order, no opacity group,
 * no damage union and no notion of "the previous frame". It is not VSync, and it does not wait for
 * anything: the call is synchronous and the pixels are in the destination when it returns. It is not a
 * format converter of its own -- a BGRA8 destination is served by
 * `robo_gfx_surface_composite_rect`'s existing conversion, and a format the graphics module cannot
 * write is refused rather than guessed at. It is not a swap chain: there is one destination, the
 * caller owns it, and this module retains nothing after the call.
 */
#ifndef ROBO_RENDER_PRESENTER_H
#define ROBO_RENDER_PRESENTER_H

#include "RoboBase/RoboError.h"

#include "RoboGraphics/RoboGraphicsFramebuffer.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboPaint/RoboPaintGeometry.h"

#include "RoboRender/RoboRenderDiagnostics.h"
#include "RoboRender/RoboRenderRasterizer.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What one presentation did.
 *
 * A record by value, and its fields are the pairs a caller asks about:
 *
 *   `presented` against `frame_width`/`frame_height` is *how much of the picture landed*, in destination
 *   coordinates, which is the rectangle a caller that keeps a damage history would record;
 *
 *   `presented_pixels` against `clipped_pixels` is *how much did not*, and they are **exhaustive**: with
 *   a valid frame and destination the two sum to the frame's pixel count, so a caller can check the
 *   arithmetic against `robo_render_target_pixel_count` rather than trust it;
 *
 *   `complete` is the one-bit summary of that pair, for the caller that only wants to know whether the
 *   whole picture is on the destination;
 *
 *   `frame_checksum` is the *picture* this call put there, taken from the frame rather than computed
 *   from the destination afterwards -- which is what makes a presented frame identifiable in a log
 *   without a second read of the pixels it was copied into.
 *
 * `offset_x`/`offset_y` echo the origin as requested rather than the one actually used, because the
 * destination rectangle already reports where the pixels went: a record that clamped the offset and
 * reported the clamped one would hide the caller's mistake rather than describe the result.
 */
typedef struct robo_render_present_record {
    /** Frame width in pixels. */
    uint32_t frame_width;
    /** Frame height in pixels. */
    uint32_t frame_height;
    /** Destination width in pixels. */
    uint32_t dest_width;
    /** Destination height in pixels. */
    uint32_t dest_height;
    /** Destination pixel format. */
    robo_gfx_pixel_format_t dest_format;
    /** The destination pixels written: `frame`'s bounds translated by the origin and clipped. */
    robo_layout_rect_t presented;
    /** Pixels inside @ref presented (0 when the frame missed the destination entirely). */
    size_t presented_pixels;
    /** Frame pixels that fell outside the destination. `presented_pixels + clipped_pixels` is the frame. */
    size_t clipped_pixels;
    /** Origin the frame was placed at, in destination coordinates. */
    int32_t offset_x;
    int32_t offset_y;
    /** Non-zero when every frame pixel landed. */
    int complete;
    /** The frame's own checksum: *what* was presented, not where it went. */
    uint64_t frame_checksum;
} robo_render_present_record_t;

/** Clear @p record to a presentation that wrote nothing. Safe on NULL. */
void robo_render_present_record_init(robo_render_present_record_t *record);

/**
 * Non-zero when @p record is a presentation record this build may read.
 *
 * A **closed** rule, and it is the same device `robo_render_frame_is_valid` uses: the two pixel counts
 * must agree with the frame's own size, the rectangle must be a real rectangle, the format must be one
 * this build's targets are in... and the record must not claim `complete` for a frame that lost pixels.
 * A record is what a caller logs; a record two fields of which disagree is a log that lies.
 */
int robo_render_present_record_is_valid(const robo_render_present_record_t *record);

/** Field-for-field equality. Safe on NULL. */
int robo_render_present_record_equal(const robo_render_present_record_t *a,
                                     const robo_render_present_record_t *b);

/**
 * Where @p frame would land in @p dest at (@p x, @p y): the destination rectangle, without writing.
 *
 * The question a caller asks *before* deciding to present -- and the reason it is a door rather than a
 * comment: a caller that keeps a dirty rectangle, a caller that wants to skip a frame whose target is
 * off the destination, and a caller sizing an intermediate all need the answer, and none of them needs
 * the pixels. Returns an empty rectangle when the frame and destination do not meet, which is a
 * legitimate answer rather than an error.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p frame, @p dest or @p out; ROBO_NOT_SUPPORTED
 *         with a `FRAME` diagnostic for a destination format the graphics module cannot write.
 */
robo_result_t robo_render_present_region(const robo_render_target_t *frame,
                                         const robo_gfx_surface_t *dest,
                                         int32_t x,
                                         int32_t y,
                                         robo_layout_rect_t *out,
                                         robo_render_diag_t *diag);

/**
 * Present @p frame into @p dest at (@p x, @p y), and describe what landed in @p out_record.
 *
 * The frame is read as a whole -- every pixel of it, in its own order -- and composited source-over with
 * full opacity, so a frame that carries alpha (a desktop composited over something, a window's own
 * translucent pixels) lands the way it was drawn rather than through a second blend decision. Where the
 * frame does not fit, the part that does is written and the rest is counted; see the header comment.
 *
 * **The frame is not consumed, cleared or modified**, and `presenting twice is presenting twice`: this
 * door has no "already presented" state, because the pixels are the same pixels and the caller is the
 * one that knows whether the destination still holds the previous frame's content.
 *
 * @p dest must be write-capable and in a format this build implements. @p out_record may be NULL, and
 * then the presentation happens and nothing is described.
 *
 * @return ROBO_OK, including the case of a frame that missed the destination entirely (a successful
 *         presentation of nothing, described rather than refused); ROBO_INVALID_ARGUMENT for a NULL
 *         @p frame or @p dest, or a read-only destination; ROBO_NOT_SUPPORTED with a `FRAME` diagnostic
 *         for an unimplemented destination format; ROBO_ERROR with a diagnostic when the composite
 *         failed, in which case the destination holds whatever the graphics module wrote before it
 *         failed -- this door does not roll back, and says so rather than implying an atomicity the
 *         module below it does not offer.
 */
robo_result_t robo_render_present(const robo_render_target_t *frame,
                                  robo_gfx_surface_t *dest,
                                  int32_t x,
                                  int32_t y,
                                  robo_render_present_record_t *out_record,
                                  robo_render_diag_t *diag);

/**
 * The same, into a **RoboGraphics framebuffer** -- the display-side shape of "the pixels are somewhere
 * else".
 *
 * Goal 6.3's framebuffer is this tree's owned-or-borrowed pixel store with an explicit stride, and the
 * QEMU/UEFI side of presentation is exactly that: a mapping some other authority owns, which a frame
 * must be written into without the runtime taking ownership of it. The framebuffer is wrapped in a
 * **borrowed surface for the duration of this call only** -- so nothing here outlives the call, the
 * caller keeps its mapping, and a framebuffer whose storage went away is the caller's problem in the
 * same way it was before the call. @p fb must be write-capable.
 *
 * @return as @ref robo_render_present, plus ROBO_INVALID_ARGUMENT for a NULL @p fb or one whose geometry
 *         cannot be described as a surface (a stride narrower than a row, an unimplemented format).
 */
robo_result_t robo_render_present_framebuffer(const robo_render_target_t *frame,
                                              robo_gfx_framebuffer_t *fb,
                                              int32_t x,
                                              int32_t y,
                                              robo_render_present_record_t *out_record,
                                              robo_render_diag_t *diag);

/** Non-zero when a frame can reach a destination in this build. See `ROBO_RENDER_PRESENTATION_IMPLEMENTED`. */
int robo_render_presentation_is_implemented(void);

/** Characters one line of a presentation dump can need (see @ref robo_render_present_write). */
#define ROBO_RENDER_PRESENT_DUMP_LINE_MAX 160u

/**
 * Write @p record as text: **four** lines, one per group of facts, each ending in a newline.
 *
 * The developer's door, in the shape the frame dump beside it uses: no `stdio`, no address anywhere in
 * the text, and a buffer too small reports the length the caller would have needed instead of
 * truncating. The shape:
 *
 *     present 1280x800 RGBA8 -> 1280x800 RGBA8 at 0,0
 *       presented=0,0,1280,800 pixels=1024000 clipped=0 complete=1
 *       frame_checksum=2f1c9e0b7a4d3355
 *       record=valid
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p record, @p buffer, @p out_length or a zero
 *         @p capacity; ROBO_NOT_SUPPORTED with a `DUMP` diagnostic when the text does not fit, in which
 *         case @p out_length receives the length it needed.
 */
robo_result_t robo_render_present_write(const robo_render_present_record_t *record,
                                       char *buffer,
                                       size_t capacity,
                                       size_t *out_length,
                                       robo_render_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_RENDER_PRESENTER_H */
