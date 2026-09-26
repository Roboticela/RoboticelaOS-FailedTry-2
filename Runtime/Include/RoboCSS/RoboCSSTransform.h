/*
 * RoboCSS transform vocabulary (Phase 18.7).
 *
 * CSS Transforms gives a box three closed facts beyond the transform list itself:
 * how it treats its children's 3D space, whether it draws when turned away, and
 * — the one that is a *set* rather than a keyword — the function names a
 * `transform` list may be built from.
 *
 *   transform-style       flat | preserve-3d
 *   backface-visibility   visible | hidden
 *   <transform-function>  matrix, matrix3d, translate, …, rotate3d, skew, perspective
 *
 * The function table is the interesting one: it is what a consumer reads to know
 * whether a list needs the 3D machinery (`matrix3d`, `translateZ`, `rotate3d`, …)
 * and whether one of its members is a matrix that already fixes the whole
 * transform. `transform` itself is a *grammar* — an ordered list of functions with
 * arguments — so it is registered as a carried declaration rather than parsed
 * here, exactly as a grid track list is; what this module supplies is the closed
 * set of names that grammar is built from and the two classifications a later
 * pass dispatches on.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not a matrix library and not a layout: nothing here
 * multiplies two matrices, composes a list, resolves an origin or turns anything on
 * a screen. A value says *which function* an author wrote and *what kind of
 * transform* the box is in; the module that has a renderer composes it.
 */

#ifndef ROBO_CSS_TRANSFORM_H
#define ROBO_CSS_TRANSFORM_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* transform-style                                                     */
/* ------------------------------------------------------------------ */

/** A computed `transform-style`. FLAT is 0 — the initial value. */
typedef enum CssTransformStyle {
    /** `flat`: children are flattened into the box's own plane. */
    RoboCssTransformStyleFlat = 0,
    /** `preserve-3d`: children share the box's 3D space. */
    RoboCssTransformStylePreserve3d = 1
} CssTransformStyle;

/** Number of defined `transform-style` values. */
#define RoboCssTransformStyleCount 2u

int CssTransformStyleIsKnown(CssTransformStyle style);
const char *CssTransformStyleName(CssTransformStyle style);
size_t CssTransformStyleCount(void);
CssTransformStyle CssTransformStyleFromSlice(CssSlice keyword);
CssTransformStyle CssTransformStyleFromText(const char *keyword);
int CssTransformStyleSliceIsKnown(CssSlice keyword);

/**
 * Whether @p style establishes a 3D rendering context (`preserve-3d`).
 *
 * The one classification a rendering pass reads: a preserving box composites its
 * children together rather than flattening each into the plane. Unknown answers 0.
 */
int CssTransformStyleCreates3DContext(CssTransformStyle style);

Result CssTransformStyleWrite(CssTransformStyle style,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              CssDiag *diag);

/** Capacity @ref CssTransformStyleWrite needs ("preserve-3d" plus a NUL). */
#define RoboCssTransformStyleTextMax 16u

/* ------------------------------------------------------------------ */
/* backface-visibility                                                 */
/* ------------------------------------------------------------------ */

/** A computed `backface-visibility`. VISIBLE is 0 — the initial value. */
typedef enum CssBackfaceVisibility {
    /** `visible`: the element is drawn when turned away from the viewer. */
    RoboCssBackfaceVisibilityVisible = 0,
    /** `hidden`: it is not. */
    RoboCssBackfaceVisibilityHidden = 1
} CssBackfaceVisibility;

/** Number of defined `backface-visibility` values. */
#define RoboCssBackfaceVisibilityCount 2u

int CssBackfaceVisibilityIsKnown(CssBackfaceVisibility visibility);
const char *CssBackfaceVisibilityName(CssBackfaceVisibility visibility);
size_t CssBackfaceVisibilityCount(void);
CssBackfaceVisibility CssBackfaceVisibilityFromSlice(CssSlice keyword);
CssBackfaceVisibility CssBackfaceVisibilityFromText(const char *keyword);
int CssBackfaceVisibilitySliceIsKnown(CssSlice keyword);

/** Whether @p visibility draws the back face (`visible`). Unknown answers 0. */
int CssBackfaceVisibilityShowsBackface(CssBackfaceVisibility visibility);

Result CssBackfaceVisibilityWrite(CssBackfaceVisibility visibility,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag);

/** Capacity @ref CssBackfaceVisibilityWrite needs ("visible" plus a NUL). */
#define RoboCssBackfaceVisibilityTextMax 12u

/* ------------------------------------------------------------------ */
/* transform functions                                                 */
/* ------------------------------------------------------------------ */

/**
 * The closed set of `<transform-function>` names.
 *
 * Not a transform *list* and not a matrix: this is the name vocabulary a list is
 * built from, with the two classifications a consumer reads. It is the set the
 * `transform` grammar's function tokens are checked against, and the reason
 * `transform: frobnicate(1)` is a mistake rather than a carried token.
 */
typedef enum CssTransformFunction {
    RoboCssTransformFunctionMatrix = 0,
    RoboCssTransformFunctionMatrix3d,
    RoboCssTransformFunctionTranslate,
    RoboCssTransformFunctionTranslate3d,
    RoboCssTransformFunctionTranslateX,
    RoboCssTransformFunctionTranslateY,
    RoboCssTransformFunctionTranslateZ,
    RoboCssTransformFunctionScale,
    RoboCssTransformFunctionScale3d,
    RoboCssTransformFunctionScaleX,
    RoboCssTransformFunctionScaleY,
    RoboCssTransformFunctionScaleZ,
    RoboCssTransformFunctionRotate,
    RoboCssTransformFunctionRotate3d,
    RoboCssTransformFunctionRotateX,
    RoboCssTransformFunctionRotateY,
    RoboCssTransformFunctionRotateZ,
    RoboCssTransformFunctionSkew,
    RoboCssTransformFunctionSkewX,
    RoboCssTransformFunctionSkewY,
    RoboCssTransformFunctionPerspective
} CssTransformFunction;

/** Number of defined transform functions. */
#define RoboCssTransformFunctionCount 21u

int CssTransformFunctionIsKnown(CssTransformFunction function);
const char *CssTransformFunctionName(CssTransformFunction function);
size_t CssTransformFunctionCount(void);
CssTransformFunction CssTransformFunctionFromSlice(CssSlice name);
CssTransformFunction CssTransformFunctionFromText(const char *name);
int CssTransformFunctionSliceIsKnown(CssSlice name);

/**
 * Whether @p function operates in 3D and needs the 3D machinery.
 *
 * True for `matrix3d`, the four `*3d` forms and the two Z forms. Unknown answers 0.
 */
int CssTransformFunctionIs3D(CssTransformFunction function);

/**
 * Whether @p function is a matrix that already fixes the whole transform
 * (`matrix`, `matrix3d`). Unknown answers 0.
 */
int CssTransformFunctionIsMatrix(CssTransformFunction function);

Result CssTransformFunctionWrite(CssTransformFunction function,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 CssDiag *diag);

/** Capacity @ref CssTransformFunctionWrite needs ("perspective" plus a NUL). */
#define RoboCssTransformFunctionTextMax 16u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_TRANSFORM_H */
