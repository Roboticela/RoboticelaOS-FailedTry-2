/*
 * RoboPaint image payload (Phase 11.7) — the resource an `<img>` paints.
 *
 *   HTML / DOM            <img src="...">, resolved to a robo_html_image_t
 *        │
 *        ▼
 *   RoboCSS               declared size (`img { width: 64px }`)
 *        │
 *        ▼
 *   RoboLayout            the box's content box — a replaced element's content area
 *        │
 *        ▼
 *   RoboPaint             IMAGE record  ── resource + source rectangle + snapshot
 *        │
 *        ▼
 *   display list          draw-image    ── one destination rectangle, resolved
 *        │
 *        ▼
 *   RoboGraphics          robo_gfx_raster_draw_image
 *
 * This header is the third field on an operation whose meaning varies by kind (after the
 * border style of 11.5 and the text run of 11.6), and the *shape* of it is the phase's one
 * structural decision: **an image record names a resource and a rectangle; it never names a
 * document.** Everything a consumer needs is either a value it can copy or a refcounted
 * pixel buffer it can retain, so a display list built from these records stays valid after
 * the DOM is mutated, the styles are recomputed and the tree is thrown away.
 *
 * ## What is stored, and why each field is here
 *
 *   `resource`  the decoded pixels, borrowed by this record and **retained by whoever
 *               stores it** — the paint list and the display list each hold a reference
 *               (`robo_paint_image_retain` is the one door that spells that rule out).
 *               RoboGraphics already owns the memory and the refcount; a second ownership
 *               model here would be a second thing to get wrong;
 *
 *   `identity`  RoboGraphics' own id for the resource, copied at snapshot time. It is what
 *               two records are compared by — **not the pointer** — which is what keeps
 *               `robo_paint_operation_equal` a question about the drawing rather than about
 *               the address it happens to live at;
 *
 *   `width`/`height`/`format`
 *               the resource's own facts, copied rather than queried. A dump, a validity
 *               rule and a consumer all want them, and reading them back through the
 *               resource at that moment would make the record's meaning depend on when it
 *               was read;
 *
 *   `source`    the rectangle of the image that is drawn, in **image pixels**. This build
 *               only ever produces the full source (`0, 0, width, height`), because nothing
 *               in the runtime establishes a crop — but the field exists now for the reason
 *               `RoboPaintText.h` states about spans: a display command is supposed to be
 *               render-ready, and the day a crop arrives the artifact's shape must already
 *               be able to say it. `robo_paint_image_is_full_source` is how a reader tells
 *               the two apart.
 *
 * ## What the phase deliberately does not carry
 *
 * No URL, no bytes, no loader, no decoder, no cache, no `srcset` and no placeholder. The
 * reference is a *document* fact (RoboHTML's `RoboHTMLImage.h` owns it, along with the
 * fetch seam and the resource's failure state) and this layer only ever sees the outcome
 * that is paintable: a decoded resource. An image that is missing, still pending, failed or
 * unsupported produces **no record at all** — there is nothing to draw, and a record that
 * drew a box instead would be inventing a broken-image presentation this runtime does not
 * have. The traversal counts those instead, so "this document has an `<img>` and nothing
 * was painted for it" is a number a reader sees.
 *
 * No `object-fit`, no `object-position`, no nine-slice, no filters, no effects: the mapping
 * this build implements is *the full source into the destination rectangle layout
 * produced*, which is the one `object-fit: fill` describes for a replaced element whose
 * sizing was resolved. Scaling is RoboGraphics' own (nearest-neighbour, stated in
 * `RoboGraphicsRaster.h`) and this module passes the two rectangles through untouched.
 *
 * ## Ownership, stated once
 *
 * | Where | Holds | Released by |
 * |---|---|---|
 * | A payload a caller builds and hands to `robo_paint_list_append` | borrowed | the caller |
 * | An operation in a paint list | one reference | `robo_paint_list_clear`/`_destroy` |
 * | A command in a display list | one reference | `robo_paint_display_list_clear`/`_destroy` |
 *
 * The builder copying a record's payload into a command therefore *adds* a reference rather
 * than moving one, which is what makes the two lists independent: destroying the record
 * list cannot change a pixel the display list draws, and neither can outlive the DOM.
 *
 * See paint/docs/Images.md.
 */

#ifndef ROBO_PAINT_IMAGE_H
#define ROBO_PAINT_IMAGE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"

#include "RoboPaint/RoboPaintDiagnostics.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The largest resource dimension this build will carry into a record.
 *
 * A bound rather than a clamp, and it is a **robustness** bound instead of a policy: a
 * source rectangle is a pair of `int32_t` extents, so a resource wider than the coordinate
 * range could not be named by one even if a decoder produced it; and a destination
 * rectangle derived from such a source would sit close enough to the range's edge that the
 * 64-bit mapping in `robo_gfx_raster_draw_image` would be doing its saturating arithmetic
 * on a real document rather than on a hostile one. `2^14` is far above anything this
 * runtime's decoder will hand over (its own default cap is 4096) and far below the range,
 * so a resource that trips it is a decode outcome worth refusing rather than a document
 * worth drawing.
 */
#define ROBO_PAINT_IMAGE_MAX_DIMENSION 16384u

/** One image drawing: the resource, the rectangle of it to draw, and the resource's facts. */
typedef struct robo_paint_image {
    /**
     * The decoded resource, borrowed by this record.
     *
     * It is **not retained here** — `robo_paint_image_retain` is the door that takes the
     * reference a *storing* container owes, and it exists so that the one place the rule
     * lives is not four call sites that each remembered it differently. A record a caller
     * builds on the stack owns nothing and needs no cleanup; the list it is appended to
     * does the retaining.
     */
    robo_gfx_image_t *resource;

    /** The resource's RoboGraphics id at snapshot time (the identity two records compare by). */
    robo_gfx_image_id_t identity;

    /** The resource's intrinsic width in pixels, copied at snapshot time. */
    uint32_t width;

    /** The resource's intrinsic height in pixels, copied at snapshot time. */
    uint32_t height;

    /** The resource's pixel format, copied at snapshot time. */
    robo_gfx_pixel_format_t format;

    /**
     * The rectangle of the resource that is drawn, in image pixels, half-open.
     *
     * This build always produces the full source; the field is what makes a crop a later
     * addition rather than a change of shape (`robo_paint_image_is_full_source`).
     */
    robo_gfx_rect_t source;
} robo_paint_image_t;

/** Clear @p image: no resource, no rectangle, zeroed facts. Safe on NULL. */
void robo_paint_image_init(robo_paint_image_t *image);

/**
 * Non-zero when @p image names nothing at all.
 *
 * The state a cleared record is in, and the state every kind **except** an image record
 * must be in (`robo_paint_operation_is_valid`), so "nobody set this" and "this drawing has
 * no image" are one state rather than two that could disagree. A payload that names a
 * resource but whose source rectangle is degenerate is *not* cleared — it is invalid, and
 * `robo_paint_image_is_valid` is the door that says so. NULL is cleared.
 */
int robo_paint_image_is_cleared(const robo_paint_image_t *image);

/**
 * Non-zero when @p image is a payload this build may draw.
 *
 *   a **resource with pixels**: non-NULL, and of a kind whose surface exists. A
 *   metadata-only image (`EMPTY`, `EXTERNAL`) is a resource that cannot be drawn, and a
 *   record carrying one is refused one stage before a rasteriser would have to;
 *   **positive intrinsic dimensions**, because a zero-sized resource is a decode outcome
 *   this layer cannot draw and must not pretend to;
 *   a **source rectangle inside the resource** with a positive area, which is the same
 *   rule `robo_gfx_raster_draw_image` enforces one stage along: the record layer refuses
 *   what the primitive would refuse, so a caller reading a valid record knows the draw
 *   can be attempted.
 */
int robo_paint_image_is_valid(const robo_paint_image_t *image);

/** Non-zero when @p image's source rectangle is the whole resource. */
int robo_paint_image_is_full_source(const robo_paint_image_t *image);

/**
 * Field-by-field equality, **with the resource compared by identity and not by address**.
 *
 * The distinction is the reason `identity` is stored. Two records built from the same
 * resource are equal; two records built from the same *bytes* decoded twice are not, and
 * that is a statement about the resource rather than a defect in the comparison — a
 * document that resolves its images twice has two images
 * (`paint/docs/Images.md` §determinism).
 */
int robo_paint_image_equal(const robo_paint_image_t *a, const robo_paint_image_t *b);

/**
 * Take a reference on @p image's resource, for a container that is about to store it.
 *
 * Safe on a cleared payload (nothing to retain) and idempotent in effect. The one door
 * through which the ownership rule is expressed, so that appending to a list and building
 * a display list cannot disagree about who owes the release.
 */
void robo_paint_image_retain(robo_paint_image_t *image);

/** Release the reference @ref robo_paint_image_retain took (safe on NULL/cleared). */
void robo_paint_image_release(robo_paint_image_t *image);

/**
 * Snapshot @p resource into @p out: the phase's one constructor.
 *
 * Copies the resource's identity, dimensions and format, sets the source rectangle to the
 * **full image**, and borrows the resource — it takes **no** reference, exactly as
 * `robo_paint_image_init` leaves a cleared payload owed by nobody. A caller that stores the
 * result retains it.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_SUPPORTED for a
 *         resource with no pixels or a zero dimension — the answer this build gives to
 *         "can this be painted at all?", and the reason an `<img>` whose decode produced
 *         nothing produces no record rather than an unusable one.
 */
robo_result_t robo_paint_image_from_resource(robo_gfx_image_t *resource,
                                             robo_paint_image_t *out,
                                             robo_paint_diag_t *diag);

/**
 * The resource's identity, or 0 for a cleared payload.
 *
 * The door a dump and a comparison use, so that neither has to know that the identity is a
 * field: a reader that printed an address would make two runs of one document differ.
 */
robo_gfx_image_id_t robo_paint_image_identity(const robo_paint_image_t *image);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_IMAGE_H */
