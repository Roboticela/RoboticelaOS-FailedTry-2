/*
 * RoboRender rasterizer (Phase 11.12).
 *
 * 	display list  →  RoboRasterizer  →  pixel target
 *
 * The first release in this tree that ends in pixels. Everything below answers *what* and *where*;
 * this module spends those answers into a buffer it owns and hands back a record of what it did.
 *
 * ## The three things a frame needs, and which of them is whose
 *
 * A **target** is owned by its caller and by this module's convention: `robo_render_target_create`
 * allocates a write-capable RGBA8 surface and remembers its facts, and nothing in the pipeline can
 * reach it except through the doors below. It is created here rather than accepted as a RoboGraphics
 * surface because a *frame* is the thing this module's callers make decisions about -- and because a
 * door that took a bare surface could be handed one that is read-only, of another format, or (worse)
 * borrowed from a window and about to vanish. `robo_render_target_surface` is the door for the other
 * direction: a caller that wants to encode, measure or eventually present the pixels borrows the
 * surface and borrows nothing else.
 *
 * A **listing** (`robo_paint_display_list_t`) is the rasteriser's whole input contract, and this
 * module reads it through `const` and never rebuilds it. Nothing here knows what a DOM node is: the
 * boundary scan refuses the names, and the design refuses the need -- a display command carries the
 * one rectangle it acts on, the boundary in force, the strength in force and (for a run) the
 * characters, so an executor needs no tree, no computed style and no box model.
 *
 * A **run** is one call, and it is deliberately *not* incremental: `robo_render_rasterize` executes
 * the whole list, `robo_render_rasterize_regions` executes the same list bounded by the regions a
 * paint invalidation set names, and both are the same walk (`robo_paint_decoration_paint_bounded`).
 * There is no `begin`/`draw`/`end` triple because there is no state to hold between calls -- and a
 * one-shot call is what makes "two runs over the same list produce the same bytes" a statement about
 * the *inputs* rather than about a session.
 *
 * ## Clearing is the caller's, and that is a decision rather than an omission
 *
 * A run **does not clear the target**. `robo_render_target_clear` does, explicitly, and the two are
 * separate for a reason a region run makes sharp: a partial repaint *must not* clear, or it would
 * erase the pixels the regions were computed to preserve. So the frame contract is: clear once per
 * frame if the frame is new (`robo_render_target_clear`, or a target straight from creation, which
 * is zero-filled), then execute the list either wholly or by regions. Conflating the two would make
 * the correct partial repaint unexpressible, which is exactly the shape a first implementation gets
 * wrong and then keeps for years.
 *
 * ## Clipping and strength: read from the list, enforced by the raster
 *
 * The list states a boundary and a strength **once per scope**, as a `push-clip`/`pop-clip` and
 * `push-opacity`/`pop-opacity` pair, and once per command as a fallback for a hand-built list with
 * no scope commands in it. Executing them is the pass's job (`RoboPaintDecoration.c`), which applies
 * the effective boundary and the effective strength to the graphics raster before each drawing --
 * so *nested* scopes, a scope with nothing inside it, a box whose own decoration is drawn outside
 * the scope its contents sit in (CSS's arrangement for `overflow`) and the product of nested
 * opacities are all properties of the walk, not of this module. What this door adds is the region
 * bound: `robo_render_rasterize_regions` passes the set's rectangle down and every drawing is
 * clipped to it as well.
 *
 * There is **no `SAVE_STATE`/`RESTORE_STATE` pair** and there never was: 11.3 declined to add them
 * for the reason this header restates by not needing them -- there is no graphics state to save,
 * because every drawing writes the boundary and the strength it is drawn under rather than inheriting
 * them. A rasteriser with a real state stack is a rasteriser with transforms and a transform stack,
 * and both are named as absent.
 *
 * ## All-or-nothing, and why a frame has a preflight
 *
 * Before the first pixel, a run walks the list **once** counting scope depth and checking that every
 * command's kind is one this rasteriser executes. A list that fails -- a command kind this build does
 * not know, or a `pop` that closes a scope nothing opened -- is **refused** with a diagnostic and the
 * target is left exactly as it was. Two properties follow and both are load-bearing:
 *
 *   an unsupported command is never *silently reinterpreted*. Brief §5's requirement and this
 *   module's own: a command a rasteriser does not understand is a document it must not draw half of,
 *   because half of a picture is not a picture;
 *
 *   a run that returns `ROBO_OK` **drew the whole list it was given**. That is what makes the region
 *   equality claim checkable: a bounded pass and a full pass differ only in the clip, so a frame that
 *   could stop early would make "identical inside the region" unprovable rather than merely untested.
 *
 * The preflight costs one pass over values and allocates nothing. The equivalence it *does not* try to
 * prove is a list that is a valid command sequence but not a well-formed document -- a `CONTENT`
 * marker under the wrong box, a `push-clip` whose rectangle is wider than the one outside it. Those
 * are the display list **builder's** refusals (`RoboPaintDisplayListBuilder.h`), where they belong:
 * a rasteriser that re-derived them would be a second authority on what a list may contain.
 *
 * ## Determinism, stated as bytes
 *
 * For identical input -- the same list, the same target size and format, the same initial pixels --
 * the target's **bytes** are identical, and `robo_render_target_checksum` / `_equal` are the doors
 * that let a caller check it rather than trust it. Nothing here reads a clock, an address, an
 * allocation order or a hash table; the execution record is a struct of counts and rectangles, so two
 * runs can be compared field by field; and the one piece of state a run carries between commands is
 * the pass's own two stacks, whose contents are the list's values. The suite asserts this the
 * strongest way available to it: the same list rasterised twice into two independently created
 * targets, compared byte for byte, with the *record* and the *dump* compared beside them.
 *
 * ## What this module is not
 *
 * It is not a compositor: a *run* produces a buffer, and 11.13's `robo_render_present` (in
 * `RoboRenderPresenter.h`) is what moves one buffer into one destination -- still one frame into one
 * place, with no layer order and no queue. It has no tiles, no damage history, no frame scheduler, no
 * VSync, no GPU, no thread pool, no layer tree, no transform stack, no filter, no mask and no blend
 * mode beyond the source-over the graphics raster already implements. `RoboRender.h` names each of
 * those as a capability that is **0**, and `render/docs/Rasterization.md` says what each one would
 * need and why none of them is a small addition.
 */
#ifndef ROBO_RENDER_RASTERIZER_H
#define ROBO_RENDER_RASTERIZER_H

#include "RoboBase/RoboError.h"

#include "RoboFont/RoboFontFace.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
/*
 * The rectangle type arrives through **RoboPaint's** geometry re-export rather than by including
 * RoboLayout directly, and the difference is a boundary rather than a style: this module's input is a
 * display list, so the header it may name is the one belonging to the layer that produces one. The
 * canonical type is RoboGraphics' own (`robo_layout_rect_t` is a typedef of `robo_gfx_rect_t`, defined
 * in `RoboGraphicsGeometry.h` and re-exported by RoboLayout), which is why "geometry is borrowed, not
 * defined" keeps holding through the extra hop -- and `Assert-RoboRenderBoundaries` in
 * `Tools/Build.ps1` refuses a document-layer include in this module on purpose: a rasteriser that
 * could reach a layout tree could not be tested without one.
 */
#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboPaint/RoboPaintInvalidation.h"

#include "RoboRender/RoboRenderDiagnostics.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The pixel format every render target is in. **One format, and it is stated** rather than
 * negotiated: a frame's byte layout is what a determinism claim is about, so a caller reads the
 * layout here instead of discovering it from a stride.
 *
 * RGBA8 because it is the format this pipeline *thinks* in: paint resolves CSS colours to sRGB
 * 8-bit per channel with an alpha channel, RoboGraphics composites in that space, and RoboGraphics'
 * own pixel-format layer can pack and unpack it. A target in the window's format would be a
 * conversion at the boundary that the presentation phase (11.13) owns -- and RoboGraphics already
 * implements the five formats a window might want, so this is a boundary to cross rather than a
 * gap to fill.
 */
#define ROBO_RENDER_TARGET_FORMAT ROBO_GFX_FORMAT_RGBA8

/**
 * Largest width or height a target may have, in pixels.
 *
 * A bound rather than an overflow guard bolted on afterwards: at 16384 a full 32-bit frame is
 * `16384 * 16384 * 4` bytes = 2^30, so the stride, the byte size and every pixel offset a door
 * computes for an accepted target fit comfortably in `size_t` on the 64-bit targets this build has
 * -- and a caller that asks for more is told so before an allocation is attempted. It is also the
 * largest edge a GPU texture guarantees, which is a coincidence this build does not depend on but a
 * later one would not have to undo.
 */
#define ROBO_RENDER_MAX_DIMENSION 16384u

/** An owned pixel target. Opaque: its facts are read through doors, never through fields. */
typedef struct robo_render_target robo_render_target_t;

/* ------------------------------------------------------------------ */
/* The target                                                          */
/* ------------------------------------------------------------------ */

/**
 * Create an owned RGBA8 target of @p width by @p height pixels.
 *
 * The target is **zero-filled** (transparent black in every pixel), which is the one initial state a
 * caller can predict without knowing anything about allocator behaviour, and it is the state
 * `robo_render_target_checksum` reports for a frame that has drawn nothing. A caller that wants a
 * page colour calls `robo_render_target_clear` -- the explicit baseline this module insists on rather
 * than a hidden one.
 *
 * A **zero** dimension is refused rather than accepted as an empty target, and that is the one place
 * this module's rectangle rule does *not* apply: an empty *rectangle* is a document's shape ("nowhere
 * to draw"), while a target with no pixels cannot hold a frame at all and is a caller's mistake --
 * and the graphics layer refuses one for the same reason. Both dimensions are checked against
 * @ref ROBO_RENDER_MAX_DIMENSION before anything is allocated.
 *
 * @return ROBO_OK and @p out_target; ROBO_INVALID_ARGUMENT for NULL @p out_target (which is left
 *         untouched), a zero dimension, or a dimension above @ref ROBO_RENDER_MAX_DIMENSION;
 *         ROBO_OUT_OF_MEMORY with a `TARGET` diagnostic when the pixels could not be taken.
 */
robo_result_t robo_render_target_create(robo_render_target_t **out_target,
                                       uint32_t width,
                                       uint32_t height,
                                       robo_render_diag_t *diag);

/** Destroy @p target and the pixels it owns. Safe on NULL. */
void robo_render_target_destroy(robo_render_target_t *target);

/** Width in pixels (0 for NULL). */
uint32_t robo_render_target_width(const robo_render_target_t *target);

/** Height in pixels (0 for NULL). */
uint32_t robo_render_target_height(const robo_render_target_t *target);

/** Pixel format, always @ref ROBO_RENDER_TARGET_FORMAT (`UNKNOWN` for NULL). */
robo_gfx_pixel_format_t robo_render_target_format(const robo_render_target_t *target);

/** Bytes in one row, **including** any padding (0 for NULL). */
size_t robo_render_target_stride(const robo_render_target_t *target);

/** Bytes the whole target occupies (0 for NULL). */
size_t robo_render_target_byte_size(const robo_render_target_t *target);

/** Pixels in the target: `width * height` (0 for NULL). */
size_t robo_render_target_pixel_count(const robo_render_target_t *target);

/**
 * The target's pixels as a paint-space rectangle: `(0, 0, width, height)`.
 *
 * The coordinate statement as a value. Paint coordinates are device pixels in this build and the
 * mapping is the identity (`RoboRenderVersion.h` argues it), so this rectangle is *simultaneously*
 * the target's pixel bounds and the region a full frame executes -- which is why one door serves both
 * a caller computing "how much of the frame did I repaint?" and a caller clamping a region to the
 * surface it is drawing into. A cleared rectangle for NULL.
 */
robo_layout_rect_t robo_render_target_bounds(const robo_render_target_t *target);

/**
 * The target's surface, **borrowed**; NULL for NULL.
 *
 * The door for everything this module does not do: reading a pixel (`robo_gfx_surface_get_pixel`),
 * encoding a frame to a file (`robo_gfx_surface_encode`), presenting it through the window path
 * (Phase 11.13), or handing it to a future compositor. The surface is retained by the target and
 * must not be released by the caller; it is valid for as long as the target is.
 */
robo_gfx_surface_t *robo_render_target_surface(robo_render_target_t *target);

/** The same surface, read-only, for a caller that only reads. NULL for NULL. */
const robo_gfx_surface_t *robo_render_target_surface_const(const robo_render_target_t *target);

/**
 * The target's bytes, read-only and **contiguous**: the whole buffer, padding included.
 *
 * The door a determinism test wants -- and the one that makes "the output is a buffer" a statement
 * about this module rather than about a graphics surface a later backend might move. @p out_size is
 * filled with the byte count when non-NULL. NULL for a NULL target, and @p out_size is left alone.
 */
const uint8_t *robo_render_target_bytes(const robo_render_target_t *target, size_t *out_size);

/**
 * Replace every pixel of @p target with @p color -- the frame's explicit baseline.
 *
 * **A replacement, not a composite**: the colour's own channels and its alpha are written as they
 * are, so a baseline of `rgba(255, 255, 255, 0.5)` leaves a target whose every pixel says exactly
 * that, and a later drawing composites over it the way the document says. A *clear* that blended
 * would make a frame's starting state depend on what was there before, which is the opposite of what
 * a baseline is for.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p target; ROBO_NOT_SUPPORTED with a `TARGET`
 *         diagnostic when the surface cannot be written (unreachable for a target this module made,
 *         and reported rather than trusted).
 */
robo_result_t robo_render_target_clear(robo_render_target_t *target,
                                      robo_gfx_color_rgba8_t color,
                                      robo_render_diag_t *diag);

/**
 * A 64-bit checksum of @p target's bytes (FNV-1a), for comparing frames cheaply and for pinning
 * determinism in a test. 0 for NULL.
 *
 * A **non-zero** value is what "the same list produced the same picture" looks like when a suite
 * asserts it twice -- though the suite this release ships still compares byte for byte, because a
 * checksum proves equality of the odds rather than of the bytes and the bytes were available.
 */
uint64_t robo_render_target_checksum(const robo_render_target_t *target);

/**
 * Non-zero when @p a and @p b hold the same pixels.
 *
 * Same dimensions, same format, same stride, and identical bytes -- so two targets made by different
 * calls, in different orders, at different times compare equal exactly when their pictures do. Two
 * NULLs are equal and a NULL is equal to nothing else, which is the rule every comparison door in this
 * tree follows.
 */
int robo_render_target_equal(const robo_render_target_t *a, const robo_render_target_t *b);

/* ------------------------------------------------------------------ */
/* The frame record                                                    */
/* ------------------------------------------------------------------ */

/**
 * What one run did.
 *
 * A record by value, owned by nobody, and its fields are the **pairs** rather than the totals --
 * because the questions a caller asks about a frame are all of the form "how much of it?":
 *
 *   `regions` against `limited` is *what kind of run this was*: a full frame, a region run over the
 *   regions of a set, or a run that was asked for nothing at all (an empty set -- `regions == 0`,
 *   `region_pixels == 0`, `pass.commands == 0`);
 *
 *   `region_pixels` against `target_pixels` is *how much of the frame was re-executed*, which is the
 *   number the whole region path exists to make small;
 *
 *   `pass.filled` against `pass.draws` is *how many drawings reached the surface*, and the paint
 *   module's own buckets say why the others did not -- this record deliberately does not repeat
 *   them, it **contains** them;
 *
 *   `pass.bounded_out` is *how many drawings were executed outside the region* -- a statement about
 *   work, not about pixels (see its field comment in `RoboPaintDecoration.h`).
 *
 * The pass's record is embedded by value rather than copied field by field, and the reason is the
 * one every clone in this tree refuses: a second copy of eleven counters is eleven places for the
 * two to drift, and this record's job is to be the one a caller holds.
 *
 * `checksum` is the target's own checksum **after** the run, taken by this module, so a frame record
 * identifies a picture rather than describing the intent to make one -- which is what makes it usable
 * as a test's expected value and as a compositor's "did anything change?" answer later.
 */
typedef struct robo_render_frame {
    /** Target width in pixels. */
    uint32_t width;
    /** Target height in pixels. */
    uint32_t height;
    /** Target pixel format, always @ref ROBO_RENDER_TARGET_FORMAT. */
    robo_gfx_pixel_format_t format;
    /** Target row stride in bytes. */
    size_t stride;
    /** The paint pass's own record, by value: what the list carried and what landed. */
    robo_paint_decoration_stats_t pass;
    /**
     * Regions the run was given: `0` for a full frame, the set's region count for a region run
     * (including a **non-empty** set whose regions are all covered by their bounds), and `0`
     * beside `limited == 1` for a set that named nothing at all -- the "no repaint is owed" case,
     * which executes nothing and leaves the target's bytes untouched.
     */
    size_t regions;
    /** Non-zero when the run was bounded: the regions door, whatever the set held. */
    int limited;
    /** The rectangle actually executed, in paint coordinates. The whole target for a full frame. */
    robo_layout_rect_t region;
    /** Pixels inside @ref region after clamping to the target (0 for an empty run). */
    size_t region_pixels;
    /** Pixels in the target: `width * height`. The denominator @ref region_pixels is read against. */
    size_t target_pixels;
    /** The target's checksum after the run. */
    uint64_t checksum;
} robo_render_frame_t;

/** Clear @p frame to a run that did nothing. Safe on NULL. */
void robo_render_frame_init(robo_render_frame_t *frame);

/**
 * Non-zero when @p frame is a record this build may read: a target size that is non-zero and inside
 * the maximum, the format and stride of a target this module could have made, a rectangle with a
 * non-negative size, and a pixel count that agrees with the size.
 *
 * A **closed** rule rather than a "looks plausible" one, because the write door and a suite both
 * read a record a caller may have built by hand: a frame whose `region_pixels` exceeds its
 * `target_pixels`, or whose stride is smaller than a row of its own format, is a record two fields of
 * which disagree about what it is.
 */
int robo_render_frame_is_valid(const robo_render_frame_t *frame);

/** Field-for-field equality, including the embedded pass record. Safe on NULL. */
int robo_render_frame_equal(const robo_render_frame_t *a, const robo_render_frame_t *b);

/* ------------------------------------------------------------------ */
/* Running a frame                                                     */
/* ------------------------------------------------------------------ */

/**
 * Execute **every** command of @p list into @p target, and describe the run in @p out_frame.
 *
 * The whole-frame door, and the one a caller reaches for when it has no idea what changed: the list
 * is walked once in its own order (`robo_paint_decoration_paint`), each drawing is bounded by the
 * boundary in force and drawn at the strength in force, and nothing is cleared -- a fresh target is
 * zero-filled and a reused one is cleared by the caller (`robo_render_target_clear`).
 *
 * @p face is the caller's, exactly as @p target is: this door retains nothing, resolves no
 * `font-family` and falls nothing back. It may be **NULL**, and then every text command is counted
 * (`pass.no_font`) and skipped -- which is the honest answer for a textless document and the reason
 * a caller that draws no text never needs a font. Which face a document's text should use is a
 * selection question with owners elsewhere (`RoboFont`'s Goals 5.11/5.12/5.15 and CSS's
 * `font-family`), and it is the same argument `RoboPaintDecoration.h` makes one module down.
 *
 * @p out_frame may be NULL, and then the run happens and nothing is described -- a caller that only
 * wants pixels is not made to hold a record it will not read. **Nothing about the target is written
 * when the call fails**: the preflight refuses a list this rasteriser cannot execute before the first
 * pixel, so the target holds either the whole frame or the state it had.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p list or @p target; ROBO_NOT_SUPPORTED with a
 *         `LIST` diagnostic for a command kind this build does not execute, or a scope pair that does
 *         not balance; ROBO_NOT_SUPPORTED with a `FRAME` diagnostic when the target cannot be
 *         rasterised; ROBO_OUT_OF_MEMORY when the pass could not allocate; ROBO_ERROR with a
 *         diagnostic when a drawing failed.
 */
robo_result_t robo_render_rasterize(const robo_paint_display_list_t *list,
                                   robo_render_target_t *target,
                                   robo_font_face_t *face,
                                   robo_render_frame_t *out_frame,
                                   robo_render_diag_t *diag);

/**
 * Execute the same list into @p target, bounded by the regions @p set says are stale (11.11).
 *
 * The region door, and the composition this phase exists for: `RoboPaint` answers which pixels went
 * stale, `RoboRender` re-executes exactly those. The walk, the scopes, the strengths and the list are
 * the whole-frame door's; the one difference is that the raster's clip is narrowed to the rectangle
 * below, so nothing outside it is touched and the pixels outside it keep the previous frame's bytes
 * -- which is what makes a partial repaint a repaint rather than a repaint-then-guess.
 *
 * **One run over the set's own bounds**, not one run per region, and the reason is a wrong pixel
 * rather than a performance argument: a set can name overlapping regions, and executing the list once
 * per region would composite a semi-transparent drawing **twice** where two regions overlap. Two
 * half-transparent fills over one pixel do not make one fill. The union is documented by 11.11 as
 * *sufficient* (`robo_paint_invalidation_set_bounds`: it can only be too large), and a run over a
 * superset of the damage is correct for exactly the reason a run over the whole frame is. The
 * tighter answer -- merge into disjoint rectangles and run once per rectangle -- is the region tree
 * 11.11 deliberately did not build, and this door does not pretend to have it.
 *
 * A **NULL or empty set** is the "no repaint is owed" case and is **success with nothing executed**:
 * `regions == 0`, `limited == 1`, an empty `region`, `region_pixels == 0`, `pass.commands == 0`, and a
 * `checksum` that is the target's own. That is not an error because 11.11's own answer to an empty set
 * is "the picture is current", and a caller that has to distinguish "nothing was owed" from "something
 * went wrong" should not have to read a diagnostic to do it.
 *
 * The set is read through `const` and nothing in it is retained: a region is a value, and the run's
 * only borrow of the caller's memory ends when the call returns.
 *
 * @return as @ref robo_render_rasterize, plus ROBO_INVALID_ARGUMENT for a NULL @p target. A NULL
 *         @p set is **not** an error (see above).
 */
robo_result_t robo_render_rasterize_regions(const robo_paint_display_list_t *list,
                                            robo_render_target_t *target,
                                            robo_font_face_t *face,
                                            const robo_paint_invalidation_set_t *set,
                                            robo_render_frame_t *out_frame,
                                            robo_render_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Reading a frame                                                     */
/* ------------------------------------------------------------------ */

/**
 * Characters one line of the frame dump can need.
 *
 * A bound rather than a guess: the widest line is the one carrying seven counters, each at most
 * twenty digits with a name, and the sum is comfortably inside this. It exists so a caller -- and the
 * suite -- can size one buffer for the whole dump and count lines against a known number rather than
 * discovering both from a refusal.
 */
#define ROBO_RENDER_FRAME_DUMP_LINE_MAX 200u

/**
 * Write @p frame as text: **seven** lines, one per group of facts, each ending in a newline.
 *
 * The developer's door, in the shape this tree's other dumps use (`robo_paint_list_write`,
 * `robo_paint_display_list_write`): the text is built without `stdio`, **no address or pointer ever
 * appears in it**, and a buffer that is too small reports the length the caller would have needed
 * instead of truncating silently. Every number in it is a count the record carries, so a dump is a
 * rendering of the record rather than a second opinion about it.
 *
 * The grouping is the reader's, not the struct's: a caller comparing two frames compares **lines**,
 * so the target and the region sit together, the walk's counts together, the drawing kinds together,
 * the two states together, and the checksum alone on the last line where a diff shows it.
 *
 * The shape:
 *
 *     frame 320x240 rgba8 stride=1280
 *       pixels=76800 region=0,0,320,240 region_pixels=76800 regions=0 limited=0
 *       commands=42 draws=18 skipped=24 bounded_out=0
 *       backgrounds=6 borders=4 texts=3 images=1 filled=14
 *       clip_scopes=2 clip_depth=2 opacity_scopes=1 opacity_depth=1 faded=3
 *       glyphs=12 glyphs_missing=0 unshaped=0 clipped=8 empty=2 no_resource=0 no_font=0
 *       checksum=2f1c9e0b7a4d3355
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p frame, @p buffer, @p out_length or a zero
 *         @p capacity; ROBO_NOT_SUPPORTED with a `DUMP` diagnostic when the text does not fit, in
 *         which case @p out_length receives the length it needed.
 */
robo_result_t robo_render_frame_write(const robo_render_frame_t *frame,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_render_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_RENDER_RASTERIZER_H */
