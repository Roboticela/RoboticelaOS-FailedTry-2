/*
 * RoboCSS fragmentation break vocabulary (Phase 19.7).
 *
 * A fragmentation container is a box that splits its content into a sequence of
 * *fragments* — columns, pages, regions. Two properties decide *where* the splits
 * fall, and both are closed sets, so both live here:
 *
 *   break-before, break-after   the `break-between` set: one vocabulary for two
 *                               properties, because the same thirteen spellings mean
 *                               the same thing on either side of a box's boundary;
 *   break-inside                the `break-inside` set: five spellings, because
 *                               whether *inside* a box may be split is a smaller and
 *                               different question from whether its boundary is a
 *                               break.
 *
 * Each row carries the keyword, the enumerator, and the classifications a
 * fragmentation pass dispatches on rather than comparing a spelling:
 *
 *   forced          the value names a break that *must* happen (`page`, `column`,
 *                   `region`, `always`, `left`, `right`, `recto`, `verso`), as
 *                   against `auto` and the four `avoid` values;
 *   avoided         the value forbids a break (`avoid`, `avoid-page`,
 *                   `avoid-column`, `avoid-region`);
 *   columnBreak     a **forced** break a *column* container honours (`column`,
 *                   and `always`, which is the generic forced break). `page` is a
 *                   forced break that a column container does not honour, which is
 *                   the distinction a pass gets wrong by treating "forced" as one
 *                   thing;
 *   pageBreak       a **forced** break a *page* container honours (`page`, the
 *                   four page-side values, and `always`).
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h) — both properties are non-inherited, and every
 * initial value is `auto`. Not a fragmentation: nothing here splits a box, sizes a
 * fragmentainer, counts lines, balances columns or applies `orphans`/`widows`, and
 * no function below takes or returns a rectangle or an extent. A value here says
 * *which rule* a fragmentation pass follows at one boundary; where the fragment
 * ends belongs to the phase that has a box. The two `page-break-*` spellings CSS
 * still accepts as aliases of `break-before`/`break-after` are **not** registered
 * and are not a second vocabulary here — an alias is a registry row, and this
 * build has none.
 */

#ifndef ROBO_CSS_FRAGMENT_H
#define ROBO_CSS_FRAGMENT_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* break-before / break-after                                          */
/* ------------------------------------------------------------------ */

/**
 * A computed `break-before` or `break-after`.
 *
 * AUTO is 0, and it is also the initial value of both properties, so the zero of
 * the enumerator and the registry's initial value agree here — which is written
 * down rather than assumed, because `column-fill` is the family's counter-example
 * and the two are close enough in the tree to be confused.
 */
typedef enum CssBreakBetween {
    /** `auto`: neither forces nor forbids a break. */
    RoboCssBreakBetweenAuto = 0,
    /** `avoid`: no break at this boundary, whatever the container would prefer. */
    RoboCssBreakBetweenAvoid = 1,
    /** `avoid-page`: no break here in a paged container. */
    RoboCssBreakBetweenAvoidPage = 2,
    /** `avoid-column`: no break here in a column container. */
    RoboCssBreakBetweenAvoidColumn = 3,
    /** `avoid-region`: no break here between regions. */
    RoboCssBreakBetweenAvoidRegion = 4,
    /** `page`: a break is forced, and a paged container honours it. */
    RoboCssBreakBetweenPage = 5,
    /** `column`: a break is forced, and a column container honours it. */
    RoboCssBreakBetweenColumn = 6,
    /** `region`: a break is forced between regions. */
    RoboCssBreakBetweenRegion = 7,
    /** `always`: a break is forced, and every container honours it. */
    RoboCssBreakBetweenAlways = 8,
    /** `left`: force a break and begin on a left page. */
    RoboCssBreakBetweenLeft = 9,
    /** `right`: force a break and begin on a right page. */
    RoboCssBreakBetweenRight = 10,
    /** `recto`: force a break and begin on a recto page. */
    RoboCssBreakBetweenRecto = 11,
    /** `verso`: force a break and begin on a verso page. */
    RoboCssBreakBetweenVerso = 12
} CssBreakBetween;

/** Number of defined `break-between` values. */
#define RoboCssBreakBetweenCount 13u

int CssBreakBetweenIsKnown(CssBreakBetween value);
const char *CssBreakBetweenName(CssBreakBetween value);
size_t CssBreakBetweenCount(void);
CssBreakBetween CssBreakBetweenFromSlice(CssSlice keyword);
CssBreakBetween CssBreakBetweenFromText(const char *keyword);
int CssBreakBetweenSliceIsKnown(CssSlice keyword);

/**
 * Whether @p value names a break that **must** happen.
 *
 * The one classification a pass asks first: a forced break is not a preference, so
 * a boundary marked with one is a break even when the fragmentainer has room left.
 * An unknown value answers 0, which reads as `auto` rather than as a forced break.
 */
int CssBreakBetweenIsForced(CssBreakBetween value);

/** Whether @p value forbids a break at this boundary. An unknown value answers 0. */
int CssBreakBetweenIsAvoided(CssBreakBetween value);

/**
 * Whether @p value is a forced break a **column** container honours.
 *
 * `column` and `always` — and deliberately not `page`, which is just as forced and
 * belongs to a container this build does not lay out. An unknown value answers 0.
 */
int CssBreakBetweenForcesColumn(CssBreakBetween value);

/**
 * Whether @p value is a forced break a **page** container honours: `page`, the four
 * page-side values, and `always`. An unknown value answers 0.
 */
int CssBreakBetweenForcesPage(CssBreakBetween value);

Result CssBreakBetweenWrite(CssBreakBetween value,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            CssDiag *diag);

/** Capacity @ref CssBreakBetweenWrite needs ("avoid-column" plus a NUL). */
#define RoboCssBreakBetweenTextMax 16u

/* ------------------------------------------------------------------ */
/* break-inside                                                        */
/* ------------------------------------------------------------------ */

/** A computed `break-inside`. AUTO is 0, and it is the initial value. */
typedef enum CssBreakInside {
    /** `auto`: the container may split this box wherever it likes. */
    RoboCssBreakInsideAuto = 0,
    /** `avoid`: do not split this box, whatever the container would prefer. */
    RoboCssBreakInsideAvoid = 1,
    /** `avoid-page`: do not split this box across a page boundary. */
    RoboCssBreakInsideAvoidPage = 2,
    /** `avoid-column`: do not split this box across a column boundary. */
    RoboCssBreakInsideAvoidColumn = 3,
    /** `avoid-region`: do not split this box across a region boundary. */
    RoboCssBreakInsideAvoidRegion = 4
} CssBreakInside;

/** Number of defined `break-inside` values. */
#define RoboCssBreakInsideCount 5u

int CssBreakInsideIsKnown(CssBreakInside value);
const char *CssBreakInsideName(CssBreakInside value);
size_t CssBreakInsideCount(void);
CssBreakInside CssBreakInsideFromSlice(CssSlice keyword);
CssBreakInside CssBreakInsideFromText(const char *keyword);
int CssBreakInsideSliceIsKnown(CssSlice keyword);

/**
 * Whether @p value forbids splitting the box.
 *
 * One predicate rather than four, and that is the *contract* rather than a
 * simplification: which container a `avoid-column` is about is a question the
 * container answers, and a box that says "do not split me" has said one thing. A
 * pass that wants to know whether the refusal applies to *it* asks its own kind
 * next to this. An unknown value answers 0.
 */
int CssBreakInsideIsAvoided(CssBreakInside value);

Result CssBreakInsideWrite(CssBreakInside value,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag);

/** Capacity @ref CssBreakInsideWrite needs ("avoid-column" plus a NUL). */
#define RoboCssBreakInsideTextMax 16u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_FRAGMENT_H */
