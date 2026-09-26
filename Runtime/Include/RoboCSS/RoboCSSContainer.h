/*
 * RoboCSS container queries (Phase 18.11).
 *
 * A container query is `@container`'s prelude, evaluated against the nearest
 * ancestor that establishes a *query container*. This module owns the closed set
 * that says which box does so and the matcher over the container's size:
 *
 *   container-type   normal | size | inline-size
 *
 * `size` establishes a container for both axes; `inline-size` establishes one for
 * the inline axis only (which is the cheap one, because it does not depend on the
 * block axis and so cannot make layout circular). `normal` establishes none.
 *
 * The *condition* model is the media-query module's (§18.10, `RoboCSSMedia.h`): a
 * container query's condition is the same `CssMediaCondition` list, evaluated with
 * `CssMediaConditionMatches` against a `CssMediaContext` that holds the container's
 * own numbers. This is the reuse the media module's own comment promised — the
 * condition grammar is one, and only the box it is measured against differs.
 *
 * `inline-size` and `block-size` as *feature names* are deliberately not built here:
 * a physical `width`/`height` condition is what this build can answer, and a logical
 * feature needs the writing mode a container does not carry until 18.12's resolution
 * does it. Naming that is the honest boundary.
 *
 * What this module is not
 *
 * Not the `@container` grammar: nothing here tokenizes a prelude or walks an
 * ancestor chain to find the query container — the caller that has a box does that
 * and hands the size here. Not a layout: nothing here measures a container.
 */

#ifndef ROBO_CSS_CONTAINER_H
#define ROBO_CSS_CONTAINER_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSMedia.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* container-type                                                      */
/* ------------------------------------------------------------------ */

/** A computed `container-type`. NORMAL is 0 — the initial value. */
typedef enum CssContainerType {
    /** `normal`: the box is not a query container. */
    RoboCssContainerTypeNormal = 0,
    /** `size`: a query container for both axes. */
    RoboCssContainerTypeSize,
    /** `inline-size`: a query container for the inline axis only. */
    RoboCssContainerTypeInlineSize
} CssContainerType;

/** Number of defined `container-type` values. */
#define RoboCssContainerTypeCount 3u

int CssContainerTypeIsKnown(CssContainerType type);
const char *CssContainerTypeName(CssContainerType type);
size_t CssContainerTypeCount(void);
CssContainerType CssContainerTypeFromSlice(CssSlice keyword);
CssContainerType CssContainerTypeFromText(const char *keyword);
int CssContainerTypeSliceIsKnown(CssSlice keyword);

/** Whether @p type establishes a query container at all (`size`, `inline-size`). */
int CssContainerTypeEstablishes(CssContainerType type);

/** Whether @p type answers the inline axis only (`inline-size`). Unknown answers 0. */
int CssContainerTypeIsInlineOnly(CssContainerType type);

Result CssContainerTypeWrite(CssContainerType type,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag);

/** Capacity @ref CssContainerTypeWrite needs ("inline-size" plus a NUL). */
#define RoboCssContainerTypeTextMax 16u

/* ------------------------------------------------------------------ */
/* matching                                                            */
/* ------------------------------------------------------------------ */

/**
 * Evaluate a container query's condition list against a container's size.
 *
 * @p size is a `CssMediaContext` holding the *container's* numbers — the same value
 * record a media query is measured against, because the condition grammar is one.
 * The result is the media matcher's: every condition must be YES, one UNKNOWN makes
 * the whole query UNKNOWN unless a NO already decided it.
 *
 * A `container-type: normal` box is **not** a query container: a caller that runs a
 * query against it gets UNKNOWN rather than a match against a box that never
 * declared itself one.
 */
CssMediaMatch CssContainerQueryMatches(const CssMediaCondition *conditions,
                                       size_t conditionCount,
                                       CssContainerType containerType,
                                       const CssMediaContext *size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_CONTAINER_H */
