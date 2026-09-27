/*
 * RoboCSS media-query suite (Phase 18.10) — css.media.*
 *
 * The media-type and media-feature vocabularies, the context a query is evaluated
 * against, and the matcher whose answer has three values: yes, no and unknown.
 *
 * The decisive cases are that a `min-`/`max-` prefix answers the *base* feature with
 * an operator (and is illegal on a discrete feature), that a feature the context
 * cannot answer is UNKNOWN rather than NO, and that a query's type is the first gate.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSMedia.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabularies                                                    */
/* ------------------------------------------------------------------ */

static void TestMediaTypeVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssMediaTypeCount, 3);
    RoboCheckEqInt((int)CssMediaTypeCount(), 3);
    RoboCheckStrEq(CssMediaTypeName(RoboCssMediaTypeAll), "all");
    RoboCheckStrEq(CssMediaTypeName(RoboCssMediaTypeScreen), "screen");
    RoboCheckStrEq(CssMediaTypeName(RoboCssMediaTypePrint), "print");
    RoboCheckEqInt(CssMediaTypeFromText("screen"),
                   (int)RoboCssMediaTypeScreen);
    RoboCheckEqInt(CssMediaTypeFromText("PRINT"), (int)RoboCssMediaTypePrint);
    RoboCheckEqInt(CssMediaTypeFromText(NULL), (int)RoboCssMediaTypeAll);
    RoboCheckEqInt(CssMediaTypeFromText("banana"), (int)RoboCssMediaTypeAll);
    RoboCheckEqInt(CssMediaTypeIsKnown((CssMediaType)99), 0);
    RoboCheckEqInt(CssMediaTypeSliceIsKnown(CssSliceFromText("print")), 1);
    RoboCheckEqInt(CssMediaTypeSliceIsKnown(CssSliceFromText("banana")), 0);

    {
        char buffer[RoboCssMediaTypeTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssMediaTypeWrite(RoboCssMediaTypeScreen, buffer,
                                               sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "screen");
        RoboCheckEqInt(CssMediaTypeWrite((CssMediaType)99, buffer,
                                               sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestMediaFeatureVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssMediaFeatureCount, 19);
    RoboCheckEqInt((int)CssMediaFeatureCount(), 19);
    RoboCheckStrEq(CssMediaFeatureName(RoboCssMediaFeatureWidth), "width");
    RoboCheckStrEq(CssMediaFeatureName(RoboCssMediaFeatureAspectRatio),
                   "aspect-ratio");
    RoboCheckStrEq(CssMediaFeatureName(RoboCssMediaFeatureOrientation),
                   "orientation");
    RoboCheckStrEq(
        CssMediaFeatureName(RoboCssMediaFeaturePrefersColorScheme),
        "prefers-color-scheme");
    RoboCheckStrEq(CssMediaFeatureName(RoboCssMediaFeatureHover), "hover");
    RoboCheckEqInt(CssMediaFeatureIsKnown((CssMediaFeature)99), 0);

    /* The kind of a feature is the feature's, not a syntax. */
    RoboCheckEqInt(CssMediaFeatureKindOf(RoboCssMediaFeatureWidth),
                   (int)RoboCssMediaFeatureKindLength);
    RoboCheckEqInt(CssMediaFeatureKindOf(RoboCssMediaFeatureAspectRatio),
                   (int)RoboCssMediaFeatureKindRatio);
    RoboCheckEqInt(CssMediaFeatureKindOf(RoboCssMediaFeatureResolution),
                   (int)RoboCssMediaFeatureKindResolution);
    RoboCheckEqInt(CssMediaFeatureKindOf(RoboCssMediaFeatureColor),
                   (int)RoboCssMediaFeatureKindInteger);
    RoboCheckEqInt(CssMediaFeatureKindOf(RoboCssMediaFeatureOrientation),
                   (int)RoboCssMediaFeatureKindKeyword);
    RoboCheckEqInt(CssMediaFeatureKindOf(RoboCssMediaFeatureGrid),
                   (int)RoboCssMediaFeatureKindBoolean);

    /* Range features admit `min-`/`max-`; discrete features do not. */
    RoboCheckEqInt(CssMediaFeatureIsRange(RoboCssMediaFeatureWidth), 1);
    RoboCheckEqInt(CssMediaFeatureIsRange(RoboCssMediaFeatureResolution), 1);
    RoboCheckEqInt(CssMediaFeatureIsRange(RoboCssMediaFeatureOrientation), 0);
    RoboCheckEqInt(CssMediaFeatureIsRange(RoboCssMediaFeatureHover), 0);

    /* A `min-`/`max-` spelling answers the *base* feature. */
    RoboCheckEqInt(CssMediaFeatureFromText("min-width"),
                   (int)RoboCssMediaFeatureWidth);
    RoboCheckEqInt(CssMediaFeatureFromText("max-resolution"),
                   (int)RoboCssMediaFeatureResolution);
    RoboCheckEqInt(CssMediaFeatureFromText("prefers-color-scheme"),
                   (int)RoboCssMediaFeaturePrefersColorScheme);
    RoboCheckEqInt(CssMediaFeatureSliceIsKnown(CssSliceFromText("min-width")),
                   1);
    RoboCheckEqInt(CssMediaFeatureSliceIsKnown(CssSliceFromText("max-height")),
                   1);
    /* A prefix on a discrete feature is not a feature spelling. */
    RoboCheckEqInt(CssMediaFeatureSliceIsKnown(CssSliceFromText("min-orientation")),
                   0);
    RoboCheckEqInt(CssMediaFeatureSliceIsKnown(CssSliceFromText("max-hover")), 0);
    RoboCheckEqInt(CssMediaFeatureSliceIsKnown(CssSliceFromText("banana")), 0);

    /* The operator is the spelling's, and both outputs come back together. */
    {
        CssMediaFeature feature = (CssMediaFeature)99;
        CssMediaOperator op = (CssMediaOperator)99;

        RoboCheckEqInt(CssMediaFeatureFromKeyword(CssSliceFromText("width"),
                                                        &feature, &op),
                       1);
        RoboCheckEqInt(feature, (int)RoboCssMediaFeatureWidth);
        RoboCheckEqInt(op, (int)RoboCssMediaOperatorNone);

        RoboCheckEqInt(CssMediaFeatureFromKeyword(CssSliceFromText("min-width"),
                                                        &feature, &op),
                       1);
        RoboCheckEqInt(feature, (int)RoboCssMediaFeatureWidth);
        RoboCheckEqInt(op, (int)RoboCssMediaOperatorMin);

        RoboCheckEqInt(CssMediaFeatureFromKeyword(CssSliceFromText("max-height"),
                                                        &feature, &op),
                       1);
        RoboCheckEqInt(feature, (int)RoboCssMediaFeatureHeight);
        RoboCheckEqInt(op, (int)RoboCssMediaOperatorMax);

        /* A prefix on a discrete feature is refused. */
        RoboCheckEqInt(CssMediaFeatureFromKeyword(
                           CssSliceFromText("min-orientation"), &feature, &op),
                       0);
        RoboCheckEqInt(CssMediaFeatureFromKeyword(CssSliceFromText("banana"),
                                                        &feature, &op),
                       0);
    }
}

/* ------------------------------------------------------------------ */
/* The matcher                                                         */
/* ------------------------------------------------------------------ */

static CssMediaContext ScreenContext(void)
{
    CssMediaContext context;

    CssMediaContextInit(&context);
    context.viewportWidthPx = 1024.0;
    context.viewportHeightPx = 768.0;
    context.resolutionDpi = 96.0;
    context.colorBits = 8;
    context.colorScheme = RoboCssMediaColorSchemeLight;
    context.reducedMotion = 0;
    context.hover = RoboCssMediaCapabilityFine;
    context.pointer = RoboCssMediaCapabilityFine;
    return context;
}

static void TestMediaConditionMatcher(void)
{
    CssMediaContext context = ScreenContext();
    CssMediaCondition condition;

    CssFixture();

    /* `(min-width: 800px)` against 1024. */
    memset(&condition, 0, sizeof(condition));
    condition.feature = RoboCssMediaFeatureWidth;
    condition.op = RoboCssMediaOperatorMin;
    condition.number = 800.0;
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchYes);
    condition.number = 1200.0;
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchNo);

    /* `(max-width: 1024px)` is inclusive. */
    condition.op = RoboCssMediaOperatorMax;
    condition.number = 1024.0;
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchYes);

    /* An exact width compares exactly. */
    condition.op = RoboCssMediaOperatorNone;
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchYes);
    condition.number = 1023.0;
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchNo);

    /* Orientation is derived, and the wide viewport is landscape. */
    memset(&condition, 0, sizeof(condition));
    condition.feature = RoboCssMediaFeatureOrientation;
    condition.keyword = CssSliceFromText("landscape");
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchYes);
    condition.keyword = CssSliceFromText("portrait");
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchNo);

    /* `prefers-color-scheme` and `prefers-reduced-motion` read the context. */
    memset(&condition, 0, sizeof(condition));
    condition.feature = RoboCssMediaFeaturePrefersColorScheme;
    condition.keyword = CssSliceFromText("light");
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchYes);
    condition.keyword = CssSliceFromText("dark");
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchNo);

    context.reducedMotion = 1;
    memset(&condition, 0, sizeof(condition));
    condition.feature = RoboCssMediaFeaturePrefersReducedMotion;
    condition.keyword = CssSliceFromText("reduce");
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchYes);

    /* `hover`/`pointer` read the capabilities. */
    memset(&condition, 0, sizeof(condition));
    condition.feature = RoboCssMediaFeatureHover;
    condition.keyword = CssSliceFromText("hover");
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchYes);
    condition.keyword = CssSliceFromText("none");
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchNo);

    /* A feature the context cannot answer is UNKNOWN, never NO. */
    memset(&condition, 0, sizeof(condition));
    condition.feature = RoboCssMediaFeaturePrefersContrast;
    condition.keyword = CssSliceFromText("more");
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchUnknown);
    condition.feature = RoboCssMediaFeatureDisplayMode;
    condition.keyword = CssSliceFromText("standalone");
    RoboCheckEqInt(CssMediaConditionMatches(&condition, &context),
                   (int)RoboCssMediaMatchUnknown);

    /* An unsigned context cannot answer a width either. */
    {
        CssMediaContext empty;

        CssMediaContextInit(&empty);
        memset(&condition, 0, sizeof(condition));
        condition.feature = RoboCssMediaFeatureWidth;
        condition.number = 800.0;
        condition.op = RoboCssMediaOperatorMin;
        RoboCheckEqInt(CssMediaConditionMatches(&condition, &empty),
                       (int)RoboCssMediaMatchUnknown);
    }
}

static void TestMediaQueryMatcher(void)
{
    CssMediaQuery query;
    CssMediaQueryContext context;
    CssMediaCondition conditions[2];

    CssFixture();
    memset(&context, 0, sizeof(context));
    context.mediaType = RoboCssMediaTypeScreen;
    context.metrics = ScreenContext();

    /* A type gate: `screen` matches the screen context. */
    memset(&query, 0, sizeof(query));
    query.type = RoboCssMediaTypeScreen;
    RoboCheckEqInt(CssMediaQueryMatches(&query, &context),
                   (int)RoboCssMediaMatchYes);
    query.type = RoboCssMediaTypePrint;
    RoboCheckEqInt(CssMediaQueryMatches(&query, &context),
                   (int)RoboCssMediaMatchNo);
    query.type = RoboCssMediaTypeAll;
    RoboCheckEqInt(CssMediaQueryMatches(&query, &context),
                   (int)RoboCssMediaMatchYes);

    /* An AND-list of conditions. */
    memset(conditions, 0, sizeof(conditions));
    conditions[0].feature = RoboCssMediaFeatureWidth;
    conditions[0].op = RoboCssMediaOperatorMin;
    conditions[0].number = 800.0;
    conditions[1].feature = RoboCssMediaFeatureOrientation;
    conditions[1].keyword = CssSliceFromText("landscape");
    memset(&query, 0, sizeof(query));
    query.type = RoboCssMediaTypeAll;
    query.conditions = conditions;
    query.conditionCount = 2u;
    RoboCheckEqInt(CssMediaQueryMatches(&query, &context),
                   (int)RoboCssMediaMatchYes);

    /* One failing condition makes the whole query NO. */
    conditions[1].keyword = CssSliceFromText("portrait");
    RoboCheckEqInt(CssMediaQueryMatches(&query, &context),
                   (int)RoboCssMediaMatchNo);

    /* An undecidable condition makes the whole query UNKNOWN, not NO and not YES. */
    conditions[1].feature = RoboCssMediaFeaturePrefersContrast;
    conditions[1].keyword = CssSliceFromText("more");
    RoboCheckEqInt(CssMediaQueryMatches(&query, &context),
                   (int)RoboCssMediaMatchUnknown);

    /* `not` swaps YES and NO but leaves UNKNOWN alone. */
    query.negated = 1;
    RoboCheckEqInt(CssMediaQueryMatches(&query, &context),
                   (int)RoboCssMediaMatchUnknown);
    /* Back to a decidable condition: the `not` now has a real match to invert. */
    conditions[1].feature = RoboCssMediaFeatureOrientation;
    conditions[1].keyword = CssSliceFromText("landscape");
    RoboCheckEqInt(CssMediaQueryMatches(&query, &context),
                   (int)RoboCssMediaMatchNo);
    query.negated = 0;
    RoboCheckEqInt(CssMediaQueryMatches(&query, &context),
                   (int)RoboCssMediaMatchYes);
}

/* ------------------------------------------------------------------ */
/* Capabilities and version                                            */
/* ------------------------------------------------------------------ */

static void TestMediaCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.mediaQueries, 1);
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 39);
    RoboCheckStrEq(CssVersionString(), "0.39.0");
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainMedia), "media");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainMedia), 1);
    RoboCheckStrEq(CssMediaMatchName(RoboCssMediaMatchUnknown), "unknown");

    /* Media queries add no registry row, so the count is the one the family before
     * the phase left -- and it moves only when a phase adds rows, which 18.14's
     * nineteen font rows did. The assertion is deliberately the *whole* registry
     * rather than "no new row": it is the one number that says whether a phase
     * touched the table at all. */
    {
        CssPropertyRegistry *registry = NULL;

        RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL),
                       RoboOk);
        RoboCheckEqInt((int)CssPropertyRegistryCount(registry), 226);
        CssPropertyRegistryDestroy(registry);
    }
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void TestRegisterCssMedia(void);

void TestRegisterCssMedia(void)
{
    TestRegister("css.media.type_vocabulary", TestMediaTypeVocabulary);
    TestRegister("css.media.feature_vocabulary", TestMediaFeatureVocabulary);
    TestRegister("css.media.condition_matcher", TestMediaConditionMatcher);
    TestRegister("css.media.query_matcher", TestMediaQueryMatcher);
    TestRegister("css.media.capabilities", TestMediaCapabilities);
}
