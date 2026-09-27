/*
 * RoboCSS image vocabulary (Phase 18.16).
 *
 * The image family is the sixth property *family* this build types, and it is the
 * first whose central fact is that **an `<image>` is not a file**. A `url("a.png")`
 * names one, a `linear-gradient(...)` computes one, an `image-set(...)` chooses
 * between several, and `cross-fade(...)` composes them. What every one of those has
 * in common is not a byte stream but a *description of where pixels come from*, and
 * the one fact the value itself carries is the name of the function that describes
 * it. So the family's largest table is the function vocabulary — thirteen functions
 * with the classifications a consumer dispatches on: is this a gradient, is it
 * repeating, which axis does a gradient run on, does the value itself state an
 * intrinsic size, does it name a resource outside the stylesheet.
 *
 * The three facts worth reading twice:
 *
 *   A gradient has no intrinsic size. `object-fit: contain` and a `background-size:
 *   auto` mean "use the image's own size", and a `linear-gradient()` has no own
 *   size — it can be any, so the box decides. A consumer that assumed every image
 *   had an intrinsic size would size a gradient to zero.
 *
 *   `object-fit`'s five words are not one scale factor. `fill` stretches and loses
 *   the ratio, `none` does not scale at all and clips, `contain` and `cover` both
 *   preserve the ratio and differ only in which side fills first, and `scale-down`
 *   is the one value whose answer depends on comparing the image's intrinsic size
 *   with the box — the value does not carry that number, so a consumer must have it.
 *
 *   `image-rendering: auto` is not `smooth`. `auto` leaves the choice to the user
 *   agent, which may pick a pixel-preserving algorithm for a small image; `smooth`
 *   promises interpolation and `pixelated` promises the opposite. And `crisp-edges`
 *   is not a synonym for `pixelated`: the first promises "do not blur" (any
 *   non-smoothing algorithm), the second promises "show the source's pixels"
 *   (nearest-neighbour, and the pixel grid may be visible).
 *
 * Four closed sets live here, each a table of rows every accessor reads:
 *
 *   typed — the property's value *is* the closed set:
 *     image-rendering         auto | smooth | high-quality | crisp-edges | pixelated
 *     object-fit              fill | contain | cover | none | scale-down
 *
 *   classified — the value's grammar is *wider* than the set, so the declaration is
 *   carried and the set types what is inside it:
 *     image-orientation       from-image | none   (plus an angle and a `flip`)
 *
 *   the value type — the name of the function a carried `<image>` reference is:
 *     url, src, image, image-set, cross-fade, element, paint,
 *     linear-gradient, radial-gradient, conic-gradient,
 *     repeating-linear-gradient, repeating-radial-gradient, repeating-conic-gradient
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not an image loader, decoder or renderer: no function
 * below opens a file, reads a header, resolves a URL, samples a colour stop,
 * interpolates a gradient, scales or filters a pixel, or measures a box. Not layout
 * or paint either: `object-fit`, `object-position` and `image-resolution` are
 * registered so the cascade and the computed style stay correct, and *placing* a
 * replaced element belongs to the phase that has a box. A value here says *which
 * image* and *what rule* a renderer follows; the pixels belong to the phase that
 * draws them.
 *
 * One family is deliberately **not typed** and is named rather than implied: the
 * `<gradient>` arguments themselves — an angle, a list of colour stops, a shape and
 * size for a radial gradient. Their grammar is a function's argument list rather
 * than a property's value, this build carries the whole reference unresolved, and the
 * vocabulary here types the *function name* a consumer switches on.
 */

#ifndef ROBO_CSS_IMAGE_H
#define ROBO_CSS_IMAGE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* image-rendering                                                     */
/* ------------------------------------------------------------------ */

/**
 * A computed `image-rendering`.
 *
 * AUTO is 0 because it is the property's initial value, so a cleared record holds the
 * state an author who declared nothing finds their image in — and it is also the
 * value that makes the *user agent* choose, which is not the same statement as any of
 * the four algorithms below it.
 */
typedef enum CssImageRendering {
    /** `auto`: the user agent's choice, which may be neither of the two groups. */
    RoboCssImageRenderingAuto = 0,
    /** `smooth`: interpolate, for a smooth result rather than a sharp one. */
    RoboCssImageRenderingSmooth = 1,
    /** `high-quality`: interpolate, allowing a higher-quality and slower algorithm. */
    RoboCssImageRenderingHighQuality = 2,
    /** `crisp-edges`: do not blur; any non-smoothing algorithm is acceptable. */
    RoboCssImageRenderingCrispEdges = 3,
    /** `pixelated`: show the source's pixels; nearest-neighbour, grid may show. */
    RoboCssImageRenderingPixelated = 4
} CssImageRendering;

/** Number of defined `image-rendering` values. */
#define RoboCssImageRenderingCount 5u

int CssImageRenderingIsKnown(CssImageRendering rendering);
const char *CssImageRenderingName(CssImageRendering rendering);
size_t CssImageRenderingCount(void);
CssImageRendering CssImageRenderingFromSlice(CssSlice keyword);
CssImageRendering CssImageRenderingFromText(const char *keyword);
int CssImageRenderingSliceIsKnown(CssSlice keyword);

/**
 * Whether @p rendering leaves the choice to the user agent (`auto`).
 *
 * The row worth reading before the other two: `auto` is not `smooth`, and a consumer
 * that treated it as the default *algorithm* would pin a choice the property
 * deliberately leaves open. An unknown value answers **1**, because the initial value
 * is `auto` and a rendering this build cannot read must not be read as one of the two
 * promises below.
 */
int CssImageRenderingIsAutomatic(CssImageRendering rendering);

/**
 * Whether @p rendering promises an **interpolating** (smoothing) algorithm
 * (`smooth`, `high-quality`).
 *
 * A different question from "not pixelated": `auto` is in neither group. An unknown
 * value answers 0.
 */
int CssImageRenderingIsSmoothing(CssImageRendering rendering);

/**
 * Whether @p rendering promises to **preserve the source's pixels**
 * (`crisp-edges`, `pixelated`).
 *
 * Asked separately from @ref CssImageRenderingIsSmoothing rather than as its
 * negation, because `auto` is in neither group — and `crisp-edges` and `pixelated`
 * are two different promises rather than two strengths of one. An unknown value
 * answers 0.
 */
int CssImageRenderingIsPixelated(CssImageRendering rendering);

/**
 * Whether @p rendering requires **nearest-neighbour** specifically (`pixelated`).
 *
 * `crisp-edges` is the weaker promise — "do not blur", any non-smoothing algorithm —
 * while `pixelated` names the algorithm and may show the pixel grid, which is why it
 * is a separate column rather than the same flag. An unknown value answers 0.
 */
int CssImageRenderingShowsPixelGrid(CssImageRendering rendering);

Result CssImageRenderingWrite(CssImageRendering rendering,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              CssDiag *diag);

/** Capacity @ref CssImageRenderingWrite needs ("high-quality" plus a NUL). */
#define RoboCssImageRenderingTextMax 13u

/* ------------------------------------------------------------------ */
/* object-fit                                                          */
/* ------------------------------------------------------------------ */

/**
 * A computed `object-fit`, which says how a replaced element's content is fitted to
 * its box.
 *
 * The property's initial is `fill`, so FILL is 0 and a cleared record holds the
 * initial — but `fill` is also the only value that *distorts* the content, which is
 * why the classifications below exist rather than a single scale factor.
 */
typedef enum CssObjectFit {
    /** `fill`: stretch to the box, whatever that does to the aspect ratio. */
    RoboCssObjectFitFill = 0,
    /** `contain`: scale to fit inside, preserving the ratio. */
    RoboCssObjectFitContain = 1,
    /** `cover`: scale to cover, preserving the ratio; the overflow is clipped. */
    RoboCssObjectFitCover = 2,
    /** `none`: the intrinsic size, no scaling; the overflow is clipped. */
    RoboCssObjectFitNone = 3,
    /** `scale-down`: `none` or `contain`, whichever is smaller. */
    RoboCssObjectFitScaleDown = 4
} CssObjectFit;

/** Number of defined `object-fit` values. */
#define RoboCssObjectFitCount 5u

int CssObjectFitIsKnown(CssObjectFit fit);
const char *CssObjectFitName(CssObjectFit fit);
size_t CssObjectFitCount(void);
CssObjectFit CssObjectFitFromSlice(CssSlice keyword);
CssObjectFit CssObjectFitFromText(const char *keyword);
int CssObjectFitSliceIsKnown(CssSlice keyword);

/**
 * Whether @p fit preserves the content's **aspect ratio** (`contain`, `cover`,
 * `scale-down`, `none`).
 *
 * `none` counts, and it is the row that makes the predicate worth asking twice: it
 * does not scale, so the ratio it shows is the source's. Only `fill` distorts. An
 * unknown value answers 0 — the initial is `fill`, which does not preserve.
 */
int CssObjectFitPreservesAspectRatio(CssObjectFit fit);

/**
 * Whether @p fit **scales** the content at all (`fill`, `contain`, `cover`,
 * `scale-down`).
 *
 * `none` is the only value that does not: it uses the intrinsic size and clips what
 * does not fit. An unknown value answers 1, because the initial (`fill`) scales. A
 * consumer that read "does not scale" as "is `none`" would be right today and wrong
 * the moment a fifth scaling value joins.
 */
int CssObjectFitScales(CssObjectFit fit);

/**
 * Whether @p fit may **clip** part of the content (`cover`, `none`).
 *
 * `fill` distorts and never clips, and `contain` always fits inside. An unknown value
 * answers 0.
 */
int CssObjectFitClips(CssObjectFit fit);

/**
 * Whether @p fit chooses between two other values by comparing the image's intrinsic
 * size with the box (`scale-down`).
 *
 * The one value whose answer depends on a number the value does not carry. A consumer
 * that read it as `contain` would scale a small image up when the property promised
 * not to. An unknown value answers 0.
 */
int CssObjectFitScalesDownOnly(CssObjectFit fit);

Result CssObjectFitWrite(CssObjectFit fit,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag);

/** Capacity @ref CssObjectFitWrite needs ("scale-down" plus a NUL). */
#define RoboCssObjectFitTextMax 11u

/* ------------------------------------------------------------------ */
/* image-orientation                                                   */
/* ------------------------------------------------------------------ */

/**
 * A `image-orientation` keyword.
 *
 * The property's grammar is `from-image | none | <angle>` (with an optional `flip`),
 * so the declaration is **carried** and this table types the two keywords, with the
 * angle form named as carried rather than refused. The pair is a genuine distinction
 * rather than two spellings of "use the file": `from-image` reads the image's own
 * EXIF orientation — a fact about the file rather than a choice — while `none` ignores
 * it. `from-image` is the initial, so `FromImage` is 0.
 */
typedef enum CssImageOrientation {
    /** `from-image`: honour the image's own recorded orientation. */
    RoboCssImageOrientationFromImage = 0,
    /** `none`: ignore any recorded orientation. */
    RoboCssImageOrientationNone = 1
} CssImageOrientation;

/** Number of defined `image-orientation` keywords. */
#define RoboCssImageOrientationCount 2u

int CssImageOrientationIsKnown(CssImageOrientation orientation);
const char *CssImageOrientationName(CssImageOrientation orientation);
size_t CssImageOrientationCount(void);
CssImageOrientation CssImageOrientationFromSlice(CssSlice keyword);
CssImageOrientation CssImageOrientationFromText(const char *keyword);
int CssImageOrientationSliceIsKnown(CssSlice keyword);

/**
 * Whether @p orientation reads the image's **own** recorded orientation
 * (`from-image`).
 *
 * A fact about the *file* rather than a policy of the user agent, which is why the
 * value exists beside `none` and why an unknown value answers **1** — the initial is
 * `from-image`, and a value this build cannot read must not silently discard an
 * author's rotated photo.
 */
int CssImageOrientationIsFromImage(CssImageOrientation orientation);

/** Whether @p orientation ignores any recorded orientation (`none`). Unknown answers 0. */
int CssImageOrientationIsNone(CssImageOrientation orientation);

Result CssImageOrientationWrite(CssImageOrientation orientation,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                CssDiag *diag);

/** Capacity @ref CssImageOrientationWrite needs ("from-image" plus a NUL). */
#define RoboCssImageOrientationTextMax 11u

/* ------------------------------------------------------------------ */
/* The image function vocabulary                                       */
/* ------------------------------------------------------------------ */

/**
 * The function a carried `<image>` value is.
 *
 * An `<image>` is a *description of where pixels come from*, and the one fact the
 * value itself carries is the name of the function that describes it. The thirteen
 * rows split by *kind* rather than by spelling:
 *
 *   a resource    `url()` names a file, `src()` names one inside an `image-set()`
 *   a computed    the six gradient functions compute pixels from colour stops
 *   a choice      `image-set()` picks between alternatives by resolution
 *   a composition `cross-fade()` blends two images
 *   a reference   `element()` copies a rendered element, `paint()` runs a worklet
 *   a sizing      `image()` and `image-set()` are the responsive spellings
 *
 * The classifications are what a consumer switches on, and none is the negation of
 * another: a gradient is repeating *and* on an axis, a non-gradient is on no axis,
 * and "does the value state an intrinsic size" is a separate question from which kind
 * it is.
 */
typedef enum CssImageFunction {
    /** `url()`: a resource outside the stylesheet. */
    RoboCssImageFunctionUrl = 0,
    /** `src()`: a resource inside an `image-set()`. */
    RoboCssImageFunctionSrc = 1,
    /** `image()`: the responsive image function. */
    RoboCssImageFunctionImage = 2,
    /** `image-set()`: a resolution-dependent choice between alternatives. */
    RoboCssImageFunctionImageSet = 3,
    /** `cross-fade()`: a blend of two or more images. */
    RoboCssImageFunctionCrossFade = 4,
    /** `element()`: a copy of a rendered element's own painting. */
    RoboCssImageFunctionElement = 5,
    /** `paint()`: a call into a paint worklet. */
    RoboCssImageFunctionPaint = 6,
    /** `linear-gradient()`: a gradient along a line. */
    RoboCssImageFunctionLinearGradient = 7,
    /** `radial-gradient()`: a gradient from a centre. */
    RoboCssImageFunctionRadialGradient = 8,
    /** `conic-gradient()`: a gradient swept around a centre. */
    RoboCssImageFunctionConicGradient = 9,
    /** `repeating-linear-gradient()`. */
    RoboCssImageFunctionRepeatingLinearGradient = 10,
    /** `repeating-radial-gradient()`. */
    RoboCssImageFunctionRepeatingRadialGradient = 11,
    /** `repeating-conic-gradient()`. */
    RoboCssImageFunctionRepeatingConicGradient = 12
} CssImageFunction;

/** Number of defined image functions. */
#define RoboCssImageFunctionCount 13u

int CssImageFunctionIsKnown(CssImageFunction function);
const char *CssImageFunctionName(CssImageFunction function);
size_t CssImageFunctionCount(void);
CssImageFunction CssImageFunctionFromSlice(CssSlice keyword);
CssImageFunction CssImageFunctionFromText(const char *keyword);
int CssImageFunctionSliceIsKnown(CssSlice keyword);

/**
 * Whether @p function computes pixels from colour stops (a gradient).
 *
 * The six `*-gradient()` functions do; every other image is either a resource, a
 * choice or a composition. An unknown value answers 0.
 */
int CssImageFunctionIsGradient(CssImageFunction function);

/**
 * Whether @p function repeats its stops indefinitely (a `repeating-*` gradient).
 *
 * A separate question from being a gradient, because a repeating gradient is still
 * a gradient: the pair of predicates is two columns rather than one tree. An unknown
 * value answers 0.
 */
int CssImageFunctionIsRepeating(CssImageFunction function);

/** Whether @p function runs along a line (`linear-gradient`, repeating). Unknown answers 0. */
int CssImageFunctionIsLinear(CssImageFunction function);

/** Whether @p function runs from a centre (`radial-gradient`, repeating). Unknown answers 0. */
int CssImageFunctionIsRadial(CssImageFunction function);

/**
 * Whether @p function sweeps around a centre (`conic-gradient`, repeating).
 *
 * The third axis, and it is *not* radial: a conic gradient's colour depends on the
 * angle around the centre rather than on the distance from it, so a consumer that
 * read "not linear" as "radial" would sample the wrong stop. An unknown value
 * answers 0.
 */
int CssImageFunctionIsConic(CssImageFunction function);

/**
 * Whether the *value itself* states an **intrinsic size**.
 *
 * `url()`, `src()`, `image()` and `element()` can all describe an image whose own
 * size is known — a decoded file, or a rendered element — while a gradient, an
 * `image-set()`, a `cross-fade()` and a `paint()` do not: the first is whatever the
 * box says, the second inherits the chosen alternative's size, the third is the
 * composed size, and the fourth is the worklet's business. This is the predicate that
 * makes an "auto" size mean two different things. An unknown value answers 0.
 */
int CssImageFunctionHasIntrinsicSize(CssImageFunction function);

/**
 * Whether @p function names a **resource outside the stylesheet** (`url`, `src`).
 *
 * The two spellings a loader has to fetch, as opposed to the ones a renderer computes
 * or composes. An unknown value answers 0.
 */
int CssImageFunctionIsResourceReference(CssImageFunction function);

Result CssImageFunctionWrite(CssImageFunction function,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag);

/** Capacity @ref CssImageFunctionWrite needs ("repeating-linear-gradient" + NUL). */
#define RoboCssImageFunctionTextMax 26u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_IMAGE_H */
