/*
 * RoboCSS filter vocabulary (Phase 18.19).
 *
 * The filter family is the first this build types whose *value* is a **list of
 * functions applied in order**, and the order is the whole meaning: `blur(2px)
 * grayscale(100%)` blurs then drains the colour, while `grayscale(100%) blur(2px)`
 * drains then blurs — different pixels. So the property itself is a carried list,
 * exactly as `transform`'s is, and what this module types is the **name vocabulary**
 * the list is built from, with the classifications a consumer reads.
 *
 * Eleven functions, and they split by *what they take* and *what they do*:
 *
 *   a reference   `url()` names an SVG filter, which this build can hold and not fetch
 *   a spatial     `blur()` and `drop-shadow()` change where the pixels are
 *   a colour      the other eight change what the pixels are, per pixel
 *
 * The second split is the one a layout or paint phase asks first. A `blur()` grows
 * the element's painted area, so a consumer that has to know whether a filter can
 * change geometry reads @ref CssFilterFunctionIsSpatial; the eight colour filters
 * never move an edge. `url()` is in neither group — it is a reference whose effect is
 * the referenced filter's business.
 *
 * And the third split is what a *grammar* reads: which functions take a `<length>`,
 * which a `<angle>`, which a colour, and which a bare number-or-percentage *amount*.
 * A `brightness()` takes an amount and a `blur()` takes a length, and a consumer that
 * parsed one as the other would accept `blur(50%)` or refuse `brightness(2px)`.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not a filter engine: no function below blurs a pixel,
 * shifts a shadow, mixes a colour matrix, fetches an SVG filter or measures anything.
 * Not the `filter` *list* either — that grammar is wider than any closed set (an
 * ordered, comma-free sequence of functions) and is carried, with the name vocabulary
 * here being the set its function tokens are checked against. A value says *which
 * filter* a renderer applies; the pixels belong to the phase that has a surface.
 */

#ifndef ROBO_CSS_FILTER_H
#define ROBO_CSS_FILTER_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The closed set of `<filter-function>` names.
 *
 * Not a filter *list* and not a pixel operation: this is the name vocabulary a list is
 * built from, with the classifications a consumer reads. It is the set the `filter`
 * grammar's function tokens are checked against, and the reason
 * `filter: frobnicate(1)` is a mistake rather than a carried token — the same role
 * `CssTransformFunction` plays for `transform`.
 *
 * The order is CSS's own list order, and it is append-only: an enumerator is a contract
 * a stored value carries, so a new function arrives at the end.
 */
typedef enum CssFilterFunction {
    /** `url()`: a reference to an SVG filter. */
    RoboCssFilterFunctionUrl = 0,
    /** `blur()`: a Gaussian blur by a `<length>`. */
    RoboCssFilterFunctionBlur = 1,
    /** `brightness()`: a linear multiplier (default 1). */
    RoboCssFilterFunctionBrightness = 2,
    /** `contrast()`: a linear contrast (default 1). */
    RoboCssFilterFunctionContrast = 3,
    /** `drop-shadow()`: a `<color>` and two or three `<length>`s. */
    RoboCssFilterFunctionDropShadow = 4,
    /** `grayscale()`: an amount in `[0, 1]` (default 1). */
    RoboCssFilterFunctionGrayscale = 5,
    /** `hue-rotate()`: an `<angle>` (default `0deg`). */
    RoboCssFilterFunctionHueRotate = 6,
    /** `invert()`: an amount in `[0, 1]` (default 1). */
    RoboCssFilterFunctionInvert = 7,
    /** `opacity()`: an amount in `[0, 1]` (default 1). */
    RoboCssFilterFunctionOpacity = 8,
    /** `saturate()`: a multiplier (default 1). */
    RoboCssFilterFunctionSaturate = 9,
    /** `sepia()`: an amount in `[0, 1]` (default 1). */
    RoboCssFilterFunctionSepia = 10
} CssFilterFunction;

/** Number of defined filter functions. */
#define RoboCssFilterFunctionCount 11u

int CssFilterFunctionIsKnown(CssFilterFunction function);
const char *CssFilterFunctionName(CssFilterFunction function);
size_t CssFilterFunctionCount(void);
CssFilterFunction CssFilterFunctionFromSlice(CssSlice name);
CssFilterFunction CssFilterFunctionFromText(const char *name);
int CssFilterFunctionSliceIsKnown(CssSlice name);

/**
 * Whether @p function names a **resource outside the stylesheet** (`url`).
 *
 * The one function that is not an operation this build describes: an SVG filter is a
 * reference, and a consumer that has no SVG pipeline carries it rather than pretending
 * to apply it. An unknown value answers 0.
 */
int CssFilterFunctionIsReference(CssFilterFunction function);

/**
 * Whether @p function changes where the pixels are (`blur`, `drop-shadow`).
 *
 * The question a phase that has to know whether a filter can change geometry asks
 * first: a blur grows the painted area and a drop-shadow offsets a copy of it, while
 * every other filter leaves the edges exactly where they were. An unknown value
 * answers 0.
 */
int CssFilterFunctionIsSpatial(CssFilterFunction function);

/**
 * Whether @p function changes **what the pixels are**, per pixel (the eight colour
 * filters).
 *
 * Asked separately from @ref CssFilterFunctionIsSpatial rather than as its negation,
 * because `url()` is in neither group: it is a reference whose effect is the
 * referenced filter's. An unknown value answers 0.
 */
int CssFilterFunctionIsColor(CssFilterFunction function);

/** Whether @p function takes a `<length>` argument (`blur`, `drop-shadow`). Unknown → 0. */
int CssFilterFunctionTakesLength(CssFilterFunction function);

/** Whether @p function takes an `<angle>` argument (`hue-rotate`). Unknown → 0. */
int CssFilterFunctionTakesAngle(CssFilterFunction function);

/**
 * Whether @p function takes a bare number-or-percentage **amount**
 * (`brightness`, `contrast`, `grayscale`, `invert`, `opacity`, `saturate`, `sepia`).
 *
 * The seven whose omitted argument defaults to `1` (or `100%`), which is why the
 * grammar may see the function with no argument at all. An unknown value answers 0.
 */
int CssFilterFunctionTakesAmount(CssFilterFunction function);

/**
 * Whether @p function takes a `<color>` argument (`drop-shadow`).
 *
 * `drop-shadow` is the only one, and it is also the only function that takes both a
 * colour and lengths — the shape a future type-checked filter grammar reads. An unknown
 * value answers 0.
 */
int CssFilterFunctionTakesColor(CssFilterFunction function);

Result CssFilterFunctionWrite(CssFilterFunction function,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              CssDiag *diag);

/** Capacity @ref CssFilterFunctionWrite needs ("drop-shadow" plus a NUL). */
#define RoboCssFilterFunctionTextMax 13u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_FILTER_H */
