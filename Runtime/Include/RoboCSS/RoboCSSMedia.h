/*
 * RoboCSS media queries (Phase 18.10).
 *
 * A media query is `@media`'s prelude: an optional media type, an optional `not`,
 * and a condition made of media features. This module owns the three closed
 * vocabularies that prelude is written from and the one question a consumer asks of
 * it — does this query match this context?
 *
 *   media type       all | screen | print
 *   media feature    width, height, aspect-ratio, orientation, resolution, color,
 *                    monochrome, prefers-color-scheme, prefers-reduced-motion,
 *                    prefers-contrast, hover, pointer, any-hover, any-pointer,
 *                    display-mode, color-gamut, scan, grid, update
 *   feature kind     boolean | length | ratio | resolution | integer | keyword
 *
 * A feature's *spelling* carries its operator: `width` is the feature `width` with
 * no bound, `min-width` and `max-width` are the same feature with a floor or a
 * ceiling. So `CssMediaFeatureFromKeyword` answers both the feature and the
 * operator, and the range forms of a feature are not rows of their own.
 *
 * The matcher is deliberately bounded and honest about it: it evaluates the
 * features a context can answer — viewport dimensions, orientation, resolution,
 * colour scheme and reduced motion — and reports a feature it cannot evaluate as
 * **unknown** rather than matching or failing it. A query that could not be decided
 * is a different answer from one that matched, and conflating them is how a
 * `(prefers-color-scheme: dark)` sheet styles a build that has no colour scheme.
 *
 * What this module is not
 *
 * Not the `@media` grammar: nothing here tokenizes a prelude or builds an at-rule.
 * Not a layout: nothing here knows a viewport except the numbers a caller puts in
 * `CssMediaContext`. Not `@container`: container queries are 18.11's, and they
 * reuse the *condition* model here rather than the context.
 */

#ifndef ROBO_CSS_MEDIA_H
#define ROBO_CSS_MEDIA_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* media types                                                         */
/* ------------------------------------------------------------------ */

/** A media type. ALL is 0 — the initial (and universal) value. */
typedef enum CssMediaType {
    /** `all`: every device. */
    RoboCssMediaTypeAll = 0,
    /** `screen`: a screen device. */
    RoboCssMediaTypeScreen,
    /** `print`: a paged/print device. */
    RoboCssMediaTypePrint
} CssMediaType;

/** Number of defined media types. */
#define RoboCssMediaTypeCount 3u

int CssMediaTypeIsKnown(CssMediaType type);
const char *CssMediaTypeName(CssMediaType type);
size_t CssMediaTypeCount(void);
CssMediaType CssMediaTypeFromSlice(CssSlice keyword);
CssMediaType CssMediaTypeFromText(const char *keyword);
int CssMediaTypeSliceIsKnown(CssSlice keyword);

Result CssMediaTypeWrite(CssMediaType type,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag);

/** Capacity @ref CssMediaTypeWrite needs ("screen" plus a NUL). */
#define RoboCssMediaTypeTextMax 8u

/* ------------------------------------------------------------------ */
/* media features                                                      */
/* ------------------------------------------------------------------ */

/**
 * What kind of value a media feature takes.
 *
 * The kind is a feature's, not a syntax the parser reads here: it says whether a
 * consumer compares numbers, ratios or keywords, which is the whole of what the
 * matcher below needs.
 */
typedef enum CssMediaFeatureKind {
    /** A bare feature, answered true/false (`(hover)`, `(grid)`). */
    RoboCssMediaFeatureKindBoolean = 0,
    /** A length (`width`, `height`). */
    RoboCssMediaFeatureKindLength,
    /** A ratio (`aspect-ratio`). */
    RoboCssMediaFeatureKindRatio,
    /** A resolution (`resolution`). */
    RoboCssMediaFeatureKindResolution,
    /** An integer (`color`, `monochrome`). */
    RoboCssMediaFeatureKindInteger,
    /** A keyword (`orientation`, `prefers-color-scheme`, `hover`, `pointer`, …). */
    RoboCssMediaFeatureKindKeyword
} CssMediaFeatureKind;

/** A media feature's base name. */
typedef enum CssMediaFeature {
    RoboCssMediaFeatureWidth = 0,
    RoboCssMediaFeatureHeight,
    RoboCssMediaFeatureAspectRatio,
    RoboCssMediaFeatureOrientation,
    RoboCssMediaFeatureResolution,
    RoboCssMediaFeatureColor,
    RoboCssMediaFeatureMonochrome,
    RoboCssMediaFeaturePrefersColorScheme,
    RoboCssMediaFeaturePrefersReducedMotion,
    RoboCssMediaFeaturePrefersContrast,
    RoboCssMediaFeatureHover,
    RoboCssMediaFeaturePointer,
    RoboCssMediaFeatureAnyHover,
    RoboCssMediaFeatureAnyPointer,
    RoboCssMediaFeatureDisplayMode,
    RoboCssMediaFeatureColorGamut,
    RoboCssMediaFeatureScan,
    RoboCssMediaFeatureGrid,
    RoboCssMediaFeatureUpdate
} CssMediaFeature;

/** Number of defined media features. */
#define RoboCssMediaFeatureCount 19u

int CssMediaFeatureIsKnown(CssMediaFeature feature);
const char *CssMediaFeatureName(CssMediaFeature feature);
size_t CssMediaFeatureCount(void);
CssMediaFeatureKind CssMediaFeatureKindOf(CssMediaFeature feature);

/**
 * Whether @p feature admits `min-`/`max-` prefixes (`width`, `resolution`, …).
 *
 * A range feature is compared with a bracket; a discrete feature is compared with
 * `=`. Unknown answers 0.
 */
int CssMediaFeatureIsRange(CssMediaFeature feature);

/** The base feature @p keyword names, ignoring any `min-`/`max-` prefix. */
CssMediaFeature CssMediaFeatureFromSlice(CssSlice keyword);
CssMediaFeature CssMediaFeatureFromText(const char *keyword);

/** Non-zero when @p keyword is a feature spelling or a `min-`/`max-` form of one. */
int CssMediaFeatureSliceIsKnown(CssSlice keyword);

/**
 * The feature and operator @p keyword spells.
 *
 * `width` yields `(width, none)`, `min-width` yields `(width, min)` and `max-width`
 * yields `(width, max)`. A non-range feature with a prefix, or an unknown spelling,
 * answers 0 and leaves both outputs alone; a known spelling answers 1.
 */
typedef enum CssMediaOperator {
    /** No bracket: equality for a discrete feature, or a bare feature. */
    RoboCssMediaOperatorNone = 0,
    /** `min-`: the context's value must be at least the query's. */
    RoboCssMediaOperatorMin,
    /** `max-`: the context's value must be at most the query's. */
    RoboCssMediaOperatorMax
} CssMediaOperator;

int CssMediaFeatureFromKeyword(CssSlice keyword,
                               CssMediaFeature *outFeature,
                               CssMediaOperator *outOperator);

/* ------------------------------------------------------------------ */
/* the context                                                         */
/* ------------------------------------------------------------------ */

/** A colour-scheme preference. LIGHT is 0 — the initial. */
typedef enum CssMediaColorScheme {
    RoboCssMediaColorSchemeLight = 0,
    RoboCssMediaColorSchemeDark
} CssMediaColorScheme;

/** A pointer/hover capability. NONE is 0 — no such device. */
typedef enum CssMediaCapability {
    RoboCssMediaCapabilityNone = 0,
    RoboCssMediaCapabilityCoarse,
    RoboCssMediaCapabilityFine
} CssMediaCapability;

/**
 * What a media query is evaluated against.
 *
 * A value record a caller fills in: the numbers it knows and the flags it has. A
 * field a caller leaves cleared means "not known", and the matcher answers *unknown*
 * for a feature that depends on it rather than guessing. Nothing here is a viewport
 * object; the caller that owns a viewport copies four numbers into it.
 */
typedef struct CssMediaContext {
    /** Viewport width in CSS pixels; 0 when unknown. */
    double viewportWidthPx;
    /** Viewport height in CSS pixels; 0 when unknown. */
    double viewportHeightPx;
    /** Device resolution in dots per inch; 0 when unknown. */
    double resolutionDpi;
    /** Colour depth in bits per colour component; 0 when unknown. */
    int colorBits;
    /** Monochrome depth in bits; 0 when unknown. */
    int monochromeBits;
    /** The preferred colour scheme. */
    CssMediaColorScheme colorScheme;
    /** Whether the user prefers reduced motion. */
    int reducedMotion;
    /** The hover capability. */
    CssMediaCapability hover;
    /** The pointer capability. */
    CssMediaCapability pointer;
} CssMediaContext;

/** Zero every field: a context that knows nothing. Safe on NULL. */
void CssMediaContextInit(CssMediaContext *context);

/* ------------------------------------------------------------------ */
/* conditions and queries                                              */
/* ------------------------------------------------------------------ */

/** The outcome of evaluating a condition or query. */
typedef enum CssMediaMatch {
    /** The condition is false. */
    RoboCssMediaMatchNo = 0,
    /** The condition is true. */
    RoboCssMediaMatchYes,
    /** A feature the context cannot answer: neither true nor false. */
    RoboCssMediaMatchUnknown
} CssMediaMatch;

/**
 * One media feature with its comparison.
 *
 * `number` is the query's value for a range feature (and is meaningful only when
 * `hasValue` is set); `keyword` is the query's value for a keyword feature (borrowed,
 * and meaningful only when not absent). A bare feature leaves both unset.
 */
typedef struct CssMediaCondition {
    CssMediaFeature feature;
    CssMediaOperator op;
    double number;
    int hasValue;
    CssSlice keyword;
} CssMediaCondition;

/**
 * A whole `@media` prelude: a type, an optional negation and an AND-list of
 * conditions (borrowed array). An empty condition list matches on the type alone.
 */
typedef struct CssMediaQuery {
    CssMediaType type;
    int negated;
    const CssMediaCondition *conditions;
    size_t conditionCount;
} CssMediaQuery;

/**
 * Evaluate @p condition against @p context.
 *
 * Returns NO/YES/UNKNOWN. A condition whose feature the context does not answer is
 * UNKNOWN, never NO — the two are different sentences.
 */
CssMediaMatch CssMediaConditionMatches(const CssMediaCondition *condition,
                                       const CssMediaContext *context);

/**
 * Evaluate @p query against @p context.
 *
 * The type is the first gate: `all` matches any context, `screen` and `print`
 * require the context's `mediaType` to agree. Then every condition must be YES; one
 * UNKNOWN makes the whole query UNKNOWN (unless it is already NO), and one NO makes
 * it NO. A negated query swaps YES and NO and leaves UNKNOWN alone.
 */
typedef struct CssMediaQueryContext {
    /** The device's media type. */
    CssMediaType mediaType;
    /** The metric context. */
    CssMediaContext metrics;
} CssMediaQueryContext;

CssMediaMatch CssMediaQueryMatches(const CssMediaQuery *query,
                                   const CssMediaQueryContext *context);

const char *CssMediaMatchName(CssMediaMatch match);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_MEDIA_H */
