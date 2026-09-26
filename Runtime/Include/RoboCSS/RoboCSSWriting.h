/*
 * RoboCSS writing-mode, direction and text-orientation (Phase 18.13).
 *
 * CSS Writing Modes gives three properties that together decide which physical
 * direction is "inline", which is "block", and how a character stands inside a
 * vertical line. They are three closed sets and each is a row per value:
 *
 *   writing-mode      horizontal-tb | vertical-rl | vertical-lr | sideways-rl |
 *                     sideways-lr
 *   direction         ltr | rtl
 *   text-orientation  mixed | upright | sideways
 *
 * The interesting one is `writing-mode`, because its values are not a simple
 * vertical/horizontal pair: `sideways-rl` and `sideways-lr` are vertical *and*
 * set `text-orientation`'s behaviour to `sideways` for the whole box, so "is this
 * vertical" and "is this sideways" are two questions with two answers, and the
 * table carries both columns rather than deriving one from the other.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not a layout, a shaper or a text engine: this is the
 * *CSS property* vocabulary, and the modules that turn a writing mode into a run, a
 * line or a caret are RoboText's (`RoboTextWritingMode`). Nothing here shapes,
 * measures, reorders or places anything; a value says *which axis is which*, and a
 * consumer that has a box reads it. `writing-mode: sideways-rl` is a value that the
 * build's own text engine does not yet implement, and it is carried here because
 * declining it would lose the two logical properties beside it rather than because
 * anything lays it out.
 */

#ifndef ROBO_CSS_WRITING_H
#define ROBO_CSS_WRITING_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* direction                                                           */
/* ------------------------------------------------------------------ */

/** A computed `direction`. LTR is 0 — the initial value. */
typedef enum CssDirection {
    /** `ltr`: the inline axis starts at the left edge. */
    RoboCssDirectionLtr = 0,
    /** `rtl`: the inline axis starts at the right edge. */
    RoboCssDirectionRtl = 1
} CssDirection;

/** Number of defined `direction` values. */
#define RoboCssDirectionCount 2u

int CssDirectionIsKnown(CssDirection direction);
const char *CssDirectionName(CssDirection direction);
size_t CssDirectionCount(void);
CssDirection CssDirectionFromSlice(CssSlice keyword);
CssDirection CssDirectionFromText(const char *keyword);
int CssDirectionSliceIsKnown(CssSlice keyword);

/**
 * Whether @p direction makes the inline axis run right to left.
 *
 * The one classification the bidi and logical-property layers read: an `rtl`
 * paragraph's inline-start is its right edge. An unknown value answers 0.
 */
int CssDirectionIsRightToLeft(CssDirection direction);

Result CssDirectionWrite(CssDirection direction,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag);

/** Capacity @ref CssDirectionWrite needs ("ltr" plus a NUL). */
#define RoboCssDirectionTextMax 8u

/* ------------------------------------------------------------------ */
/* writing-mode                                                        */
/* ------------------------------------------------------------------ */

/**
 * A computed `writing-mode`. HORIZONTAL_TB is 0 — the initial value.
 *
 * The four vertical values are one axis with three spellings of the text inside it:
 * `vertical-rl`/`vertical-lr` turn the block axis and keep the characters mixed
 * (`text-orientation: mixed`), while `sideways-rl`/`sideways-lr` turn the *text*
 * too. So the row carries "vertical" and "sideways" as two columns.
 */
typedef enum CssWritingMode {
    /** `horizontal-tb`: inline left-to-right, block top-to-bottom (the default). */
    RoboCssWritingModeHorizontalTb = 0,
    /** `vertical-rl`: block axis right-to-left, characters mixed. */
    RoboCssWritingModeVerticalRl = 1,
    /** `vertical-lr`: block axis left-to-right, characters mixed. */
    RoboCssWritingModeVerticalLr = 2,
    /** `sideways-rl`: vertical with the text turned to match the block axis. */
    RoboCssWritingModeSidewaysRl = 3,
    /** `sideways-lr`: vertical with the text turned to match the block axis. */
    RoboCssWritingModeSidewaysLr = 4
} CssWritingMode;

/** Number of defined `writing-mode` values. */
#define RoboCssWritingModeCount 5u

int CssWritingModeIsKnown(CssWritingMode mode);
const char *CssWritingModeName(CssWritingMode mode);
size_t CssWritingModeCount(void);
CssWritingMode CssWritingModeFromSlice(CssSlice keyword);
CssWritingMode CssWritingModeFromText(const char *keyword);
int CssWritingModeSliceIsKnown(CssSlice keyword);

/** Whether @p mode is horizontal (`horizontal-tb`, the only one). */
int CssWritingModeIsHorizontal(CssWritingMode mode);

/**
 * Whether @p mode's block axis is vertical.
 *
 * All four non-horizontal values are vertical, `sideways-*` included, so this is
 * **not** the negation of @ref CssWritingModeIsSideways and the two are asked
 * separately. An unknown value answers 0.
 */
int CssWritingModeIsVertical(CssWritingMode mode);

/** Whether @p mode turns the text sideways (`sideways-rl`, `sideways-lr`). */
int CssWritingModeIsSideways(CssWritingMode mode);

Result CssWritingModeWrite(CssWritingMode mode,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag);

/** Capacity @ref CssWritingModeWrite needs ("horizontal-tb" plus a NUL). */
#define RoboCssWritingModeTextMax 16u

/* ------------------------------------------------------------------ */
/* text-orientation                                                    */
/* ------------------------------------------------------------------ */

/**
 * A computed `text-orientation`. MIXED is 0 — the initial value.
 *
 * Only meaningful in a vertical writing mode; in `horizontal-tb` it is carried and
 * inert, exactly as `background-attachment: fixed` is a value with no scrollport.
 */
typedef enum CssTextOrientation {
    /** `mixed`: upright for upright scripts, rotated for the rest (the default). */
    RoboCssTextOrientationMixed = 0,
    /** `upright`: every character stands upright. */
    RoboCssTextOrientationUpright = 1,
    /** `sideways`: every character is turned to the block flow. */
    RoboCssTextOrientationSideways = 2
} CssTextOrientation;

/** Number of defined `text-orientation` values. */
#define RoboCssTextOrientationCount 3u

int CssTextOrientationIsKnown(CssTextOrientation orientation);
const char *CssTextOrientationName(CssTextOrientation orientation);
size_t CssTextOrientationCount(void);
CssTextOrientation CssTextOrientationFromSlice(CssSlice keyword);
CssTextOrientation CssTextOrientationFromText(const char *keyword);
int CssTextOrientationSliceIsKnown(CssSlice keyword);

/** Whether @p orientation turns every character (`sideways`). An unknown answers 0. */
int CssTextOrientationIsSideways(CssTextOrientation orientation);

Result CssTextOrientationWrite(CssTextOrientation orientation,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag);

/** Capacity @ref CssTextOrientationWrite needs ("sideways" plus a NUL). */
#define RoboCssTextOrientationTextMax 12u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_WRITING_H */
