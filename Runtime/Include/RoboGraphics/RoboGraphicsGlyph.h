/*
 * RoboGraphics glyph coverage rasterization (Phase 12.1).
 *
 * The quality half of the text bridge. `RoboGraphicsText.h` converts a RoboFont
 * glyph *outline* into a device-space path and strokes it with one-pixel lines —
 * a placeholder that reads as hard-edged outlines at UI sizes. This module is the
 * replacement underneath the same text path: a single glyph outline becomes a
 * **grayscale coverage mask** whose values are the fraction of each pixel the ink
 * actually covers, and that mask composites through the module's existing
 * source-over blend.
 *
 *   outline (font units)                     RoboFont
 *        ↓  font units → device float        this module's geometry
 *        ↓  adaptive flattening              (quads/cubics, no snapping)
 *        ↓  1/256 px fixed point             determinism boundary
 *        ↓  scanline coverage rasterization  8×8 samples / pixel
 *   coverage mask (uint8 per pixel)          this module
 *        ↓  coverage → alpha (gamma model)   this module
 *        ↓  source-over + surface alpha      RoboGraphicsComposite
 *   RGBA8 framebuffer
 *
 * ## Coverage, alpha and opacity are three different numbers
 *
 * A mask holds **coverage** and nothing else: `0` is "the ink does not touch this
 * pixel", `255` is "the ink covers it entirely", and the values between are the
 * sampled area fraction. It carries no colour. The glyph's own **alpha** lives in
 * the colour a caller passes to `GfxRasterDrawGlyphMask`, and the raster's
 * **opacity** is a third factor the compositor already owns. The three multiply at
 * the single application point
 *
 *     effective = coverage × colour.alpha × raster.opacity
 *
 * so a half-covered pixel of a half-transparent glyph inside a half-opaque element
 * composites at `0.5 × 0.5 × 0.5` without any of the three being rewritten into
 * another. Folding opacity into the mask would lose exactly that distinction.
 *
 * ## Geometry and determinism
 *
 * Every flattened vertex is quantized to a **1/256 device-pixel fixed point**
 * lattice, and everything after that — bounding boxes, scanline intersections,
 * span-to-sample mapping, the coverage counts and the coverage→alpha tables — is
 * integer arithmetic. There is no `sqrt`, no `pow`, no `isnan` and no libm in this
 * module, because it is compiled for the freestanding guest target as well as the
 * host. The only floating point is the font-units→device-units mapping and the
 * adaptive curve subdivision that feeds the quantizer, and the *result* of that
 * stage is a fixed-point integer, so two runs of the same glyph, size, origin and
 * configuration produce byte-identical masks on a given build.
 *
 * ## Supersampling
 *
 * `RoboGfxGlyphSamplesPerPixel` is 8, so a pixel's coverage is the number of
 * covered samples in an 8×8 grid inside it, quantized to 0..255 by
 * `(count * 255 + 32) / 64`. The grid is evaluated as **scanline sampling** —
 * each of `8 × height` sub-scanlines is intersected with the outline's edges once
 * and the sorted crossings are walked with a non-zero winding rule — rather than by
 * testing every sample against every edge, which is the same answer for a fraction
 * of the work.
 *
 * ## Gamma
 *
 * Coverage is a **linear** area fraction. The framebuffer stores sRGB-encoded
 * 8-bit values and the compositor blends those values directly, so writing linear
 * coverage into the blend as if it were an sRGB alpha is a choice, not a fact.
 * `GfxGammaCoverageToAlpha` is the one place that choice is made, and
 * `GfxCoverageGamma` names three documented models for it:
 *
 *   `RoboGfxCoverageGammaLinear` (the default) writes coverage straight through.
 *   It is the polarity-neutral model: in sRGB-space compositing the exact
 *   linear-light alpha is *below* coverage for dark ink on a light surface and
 *   *above* it for light ink on a dark one, and the two errors are of comparable
 *   size and opposite sign — the same reason every mainstream rasterizer that
 *   blends grayscale coverage into an sRGB surface does the same thing.
 *
 *   `RoboGfxCoverageGammaDarkOnLight` applies `alpha = 1 − Esrgb(1 − coverage)`,
 *   which reproduces the linear-light mixture exactly when the glyph is the darker
 *   of the two colours (dark text on a light page).
 *
 *   `RoboGfxCoverageGammaLightOnDark` applies `alpha = Esrgb(coverage)`, the mirror
 *   image, exact when the glyph is the lighter of the two (light text on a dark
 *   panel).
 *
 * `Esrgb` is the sRGB opto-electronic transfer function, evaluated at table build
 * time and embedded as two `const` 256-entry tables — no runtime initialization,
 * no global mutable state, no floating point in the conversion. Nothing here is a
 * magic factor: each model is the transfer function the derivation names, and the
 * module's tests pin the endpoints and the monotonicity of each.
 *
 * ## What this module is not
 *
 * It knows nothing about text: no shaping, no cmap, no runs, no kerning, no
 * fallback, no font files. A mask is a rectangle of coverage produced from *one*
 * outline the caller already chose. `RoboGraphicsText.h` is what turns a glyph id
 * into that outline, and the phases above it are what choose the glyph id. Hinting
 * is not implemented (RoboFont exposes no TrueType bytecode interpreter), colour
 * fonts are not rendered, and there is no mask cache in this release — the outline
 * fetch is already cached one module down and a mask is produced in one pass over
 * its own ink, so a cache would be an optimization with no correctness benefit yet.
 * `Documentation/Runtime/Typography/TextRasterQuality.md` argues each of those.
 */

#ifndef ROBO_GRAPHICS_GLYPH_H
#define ROBO_GRAPHICS_GLYPH_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontOutline.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsRaster.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sub-pixel precision of the rasterization lattice: one device pixel is
 * `RoboGfxGlyphSubpixelScale` units, and every vertex is quantized to it before any
 * pixel is touched. 1/256 px is finer than the 1/8 px sample grid can observe, so
 * the quantization is invisible in the output while making the scanline
 * arithmetic exact.
 */
#define RoboGfxGlyphSubpixelBits 8
#define RoboGfxGlyphSubpixelScale 256

/** Samples per pixel axis (8×8 = 64 samples, 65 coverage levels). */
#define RoboGfxGlyphSamplesPerPixel 8

/** Largest mask this module will allocate when the caller passes no clip. */
#define RoboGfxGlyphMaxMaskPixels (4u * 1024u * 1024u)

/**
 * The coverage → blend-alpha model. See the header comment for the derivation of
 * each; `RoboGfxCoverageGammaLinear` is the default.
 */
typedef enum GfxCoverageGamma {
    RoboGfxCoverageGammaLinear = 0,
    RoboGfxCoverageGammaDarkOnLight = 1,
    RoboGfxCoverageGammaLightOnDark = 2
} GfxCoverageGamma;

/** The model a caller that has not thought about gamma gets. */
#define RoboGfxCoverageGammaDefault RoboGfxCoverageGammaLinear

/**
 * Rasterization parameters. A value record: no pointers, no ownership, so it can
 * live in a `GfxGlyphDraw`-adjacent struct or on the stack.
 *
 * @p supersample is samples per pixel axis and must be one of 1, 2, 4, 8 or 16
 * (a power of two so the sample lattice divides a pixel exactly). Zero means
 * "the default", `RoboGfxGlyphSamplesPerPixel`.
 */
typedef struct GfxGlyphRasterParams {
    GfxCoverageGamma gamma;
    uint8_t supersample;
    uint8_t reserved0;
    uint8_t reserved1;
} GfxGlyphRasterParams;

/**
 * What one rasterization did. Counts, not pointers, so a caller can report on a
 * mask it does not keep.
 *
 *   `pixels`    the mask's area (0 for an empty mask)
 *   `covered`   pixels with any coverage at all (the ink's footprint)
 *   `full`      pixels the ink covers completely (the interior)
 *   `samples`   samples per pixel, `supersample²`
 *   `segments`  flattened, non-horizontal edges the scanlines were tested against
 *   `clipped`   non-zero when the clip narrowed the mask rectangle
 */
typedef struct GfxGlyphRasterStats {
    uint32_t pixels;
    uint32_t covered;
    uint32_t full;
    uint32_t samples;
    uint32_t segments;
    uint32_t clipped;
} GfxGlyphRasterStats;

/** An owned rectangle of 8-bit coverage. Opaque: read it through the doors below. */
typedef struct GfxGlyphMask GfxGlyphMask;

void GfxGlyphRasterParamsInit(GfxGlyphRasterParams *params);

/**
 * The coverage → alpha conversion, and the whole of the gamma model in one call.
 *
 * `0 → 0` and `255 → 255` for every model (an uncovered pixel contributes nothing
 * and a fully covered one contributes its colour unchanged), and each model is
 * monotonic non-decreasing, which the module's suite asserts.
 *
 * @param coverage  0..255 linear area coverage.
 * @param gamma     the model; any value outside the enum is treated as
 *                  @ref RoboGfxCoverageGammaDefault rather than refused, because a
 *                  caller's bad configuration must not be able to produce a
 *                  *darker* pixel than the glyph's own colour (which a negative
 *                  index would).
 * @return the blend alpha, 0..255.
 */
uint8_t GfxGammaCoverageToAlpha(uint8_t coverage, GfxCoverageGamma gamma);

/**
 * Rasterize an already-fetched outline into a coverage mask in device space.
 *
 * The geometry is the same mapping `GfxGlyphOutlineToPath` documents — font units
 * scaled by `sizePx / unitsPerEm`, +Y up becoming +Y down about @p origin on the
 * baseline — with one deliberate difference: **nothing is snapped to an integer
 * pixel**. A fractional @p origin therefore places the glyph at a fractional
 * device position, which is what makes a run of glyphs sit on its own advances
 * rather than on rounded ones.
 *
 * @p clip is a half-open device-pixel rectangle (the raster's clip, typically);
 * the mask is the intersection of the ink's bounds with it. **NULL means no
 * clip**, in which case the mask is bounded by @ref RoboGfxGlyphMaxMaskPixels and
 * a glyph larger than that is refused with `RoboNotSupported` instead of being
 * silently truncated — a caller that must not fail passes a clip, and the text
 * path always does.
 *
 * Curve flattening is adaptive: a quadratic or cubic is subdivided in half until
 * its control polygon is flat to within a quarter device pixel or the subdivision
 * depth cap is reached, so small sizes cost few segments and large sizes stay
 * smooth.
 *
 * Status mapping mirrors `GfxGlyphOutlineToPath`:
 *   OK              → rasterized (possibly to an empty mask for empty ink)
 *   EMPTY           → empty mask, `RoboOk`
 *   INVALID_ID      → `RoboInvalidArgument`
 *   MALFORMED       → `RoboInvalidArgument`
 *   LIMIT/OVERFLOW  → `RoboInvalidArgument`
 *   UNAVAILABLE /
 *   UNSUPPORTED     → `RoboNotSupported`
 *
 * A glyph with no area, a degenerate contour, coordinates far outside the clip and
 * a contour that flattened to more edges than @ref RoboGfxGlyphMaxEdges is allowed
 * to hold all resolve to an **empty mask** and `RoboOk`: nothing is drawn, which is
 * the truthful answer, and never a failed pass.
 *
 * @param outMask   required; receives the owned mask (release with
 *                  `GfxGlyphMaskRelease`). NULL on failure.
 * @param outStats  optional; receives the counts above.
 */
Result GfxGlyphMaskRasterizeOutline(const FontOutline *outline,
                                    uint16_t unitsPerEm,
                                    float sizePx,
                                    GfxPointF origin,
                                    const GfxRect *clip,
                                    const GfxGlyphRasterParams *params,
                                    GfxGlyphMask **outMask,
                                    GfxGlyphRasterStats *outStats);

void GfxGlyphMaskRetain(GfxGlyphMask *mask);
void GfxGlyphMaskRelease(GfxGlyphMask *mask);

int32_t GfxGlyphMaskX(const GfxGlyphMask *mask);
int32_t GfxGlyphMaskY(const GfxGlyphMask *mask);
uint32_t GfxGlyphMaskWidth(const GfxGlyphMask *mask);
uint32_t GfxGlyphMaskHeight(const GfxGlyphMask *mask);

/** 1 when the mask has no pixels at all (no ink, no overlap with the clip). */
int GfxGlyphMaskIsEmpty(const GfxGlyphMask *mask);

/**
 * Coverage at a mask-relative pixel, 0..255.
 * Out of range (or NULL) → 0, which is the "no ink here" answer a caller
 * iterating a rectangle wants, and never a read outside the mask.
 */
uint8_t GfxGlyphMaskCoverageAt(const GfxGlyphMask *mask,
                               uint32_t x,
                               uint32_t y);

/**
 * The mask's coverage bytes, row-major, `width * height`, **borrowed**. NULL for a
 * NULL or empty mask. Valid until the mask is released.
 */
const uint8_t *GfxGlyphMaskCoverage(const GfxGlyphMask *mask);

/**
 * Composite @p mask onto @p raster's target, in @p color, through the module's
 * existing source-over blend.
 *
 * The per-pixel arithmetic is exactly
 *
 *     alpha  = GfxGammaCoverageToAlpha(coverage, gamma)   // coverage, not opacity
 *     source = color                                       // its own alpha intact
 *     opacity= alpha × raster.opacity                      // raw bytes, 8-bit
 *
 * which the raster's own blend turns into `coverage × colour.alpha × opacity`.
 * Nothing about the mask is added to the colour and nothing about the colour is
 * folded into the mask — the two stay separate facts joined only here.
 *
 * Pixel writes are bounded by the raster's clip, which is itself always inside the
 * surface's pixel bounds, so no pixel outside the surface can be written even when
 * the mask's rectangle overhangs it. A pixel whose coverage, colour alpha or
 * opacity is zero is skipped rather than blended, so a fully transparent glyph
 * leaves the destination byte-identical.
 *
 * @return RoboOk (including the no-op cases); RoboInvalidArgument for a NULL
 *         raster, a NULL mask or an unwritable target.
 */
Result GfxRasterDrawGlyphMask(GfxRaster *raster,
                              const GfxGlyphMask *mask,
                              GfxColorRgba8 color,
                              GfxCoverageGamma gamma);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_GLYPH_H */
