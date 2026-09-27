/*
 * RoboCSS container-query suite (Phase 18.11) — css.container.*
 *
 * The `container-type` closed set, its registry rows, and the matcher that evaluates
 * a container query's condition list — the media module's own condition model —
 * against a container's size.
 *
 * The decisive cases are that `size` and `inline-size` establish a container while
 * `normal` does not (and a query against a `normal` box answers UNKNOWN), and that the
 * matcher reuses `CssMediaConditionMatches` so the condition semantics are one.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSContainer.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestContainerTypeVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssContainerTypeCount, 3);
    RoboCheckEqInt((int)CssContainerTypeCount(), 3);
    RoboCheckStrEq(CssContainerTypeName(RoboCssContainerTypeNormal), "normal");
    RoboCheckStrEq(CssContainerTypeName(RoboCssContainerTypeSize), "size");
    RoboCheckStrEq(CssContainerTypeName(RoboCssContainerTypeInlineSize),
                   "inline-size");
    RoboCheckEqInt(CssContainerTypeFromText("size"),
                   (int)RoboCssContainerTypeSize);
    RoboCheckEqInt(CssContainerTypeFromText("INLINE-SIZE"),
                   (int)RoboCssContainerTypeInlineSize);
    RoboCheckEqInt(CssContainerTypeFromText(NULL),
                   (int)RoboCssContainerTypeNormal);
    RoboCheckEqInt(CssContainerTypeFromText("banana"),
                   (int)RoboCssContainerTypeNormal);
    RoboCheckEqInt(CssContainerTypeIsKnown((CssContainerType)99), 0);
    RoboCheckEqInt(CssContainerTypeSliceIsKnown(CssSliceFromText("size")), 1);
    RoboCheckEqInt(CssContainerTypeSliceIsKnown(CssSliceFromText("banana")), 0);

    /* `normal` establishes nothing; the other two do, and one is inline-only. */
    RoboCheckEqInt(CssContainerTypeEstablishes(RoboCssContainerTypeNormal), 0);
    RoboCheckEqInt(CssContainerTypeEstablishes(RoboCssContainerTypeSize), 1);
    RoboCheckEqInt(CssContainerTypeEstablishes(RoboCssContainerTypeInlineSize),
                   1);
    RoboCheckEqInt(CssContainerTypeEstablishes((CssContainerType)99), 0);
    RoboCheckEqInt(CssContainerTypeIsInlineOnly(RoboCssContainerTypeNormal), 0);
    RoboCheckEqInt(CssContainerTypeIsInlineOnly(RoboCssContainerTypeSize), 0);
    RoboCheckEqInt(CssContainerTypeIsInlineOnly(RoboCssContainerTypeInlineSize),
                   1);
    RoboCheckEqInt(CssContainerTypeIsInlineOnly((CssContainerType)99), 0);

    {
        char buffer[RoboCssContainerTypeTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssContainerTypeWrite(RoboCssContainerTypeInlineSize,
                                                   buffer, sizeof(buffer),
                                                   &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "inline-size");
        RoboCheckEqInt(CssContainerTypeWrite((CssContainerType)99, buffer,
                                                   sizeof(buffer), &length,
                                                   NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The matcher                                                         */
/* ------------------------------------------------------------------ */

static void TestContainerMatcher(void)
{
    CssMediaContext size;
    CssMediaCondition conditions[2];
    CssMediaMatch match;

    CssFixture();
    CssMediaContextInit(&size);
    size.viewportWidthPx = 600.0;
    size.viewportHeightPx = 800.0;

    /* `(min-width: 400px)` against a 600px container. */
    memset(conditions, 0, sizeof(conditions));
    conditions[0].feature = RoboCssMediaFeatureWidth;
    conditions[0].op = RoboCssMediaOperatorMin;
    conditions[0].number = 400.0;

    match = CssContainerQueryMatches(conditions, 1u, RoboCssContainerTypeSize,
                                     &size);
    RoboCheckEqInt(match, (int)RoboCssMediaMatchYes);

    /* A `normal` box never established a container: unknown, not a match. */
    match = CssContainerQueryMatches(conditions, 1u, RoboCssContainerTypeNormal,
                                     &size);
    RoboCheckEqInt(match, (int)RoboCssMediaMatchUnknown);

    /* An AND-list, with one failing condition making it No. */
    conditions[1].feature = RoboCssMediaFeatureOrientation;
    conditions[1].keyword = CssSliceFromText("portrait");
    match = CssContainerQueryMatches(conditions, 2u, RoboCssContainerTypeSize,
                                     &size);
    RoboCheckEqInt(match, (int)RoboCssMediaMatchYes);
    conditions[1].keyword = CssSliceFromText("landscape");
    match = CssContainerQueryMatches(conditions, 2u, RoboCssContainerTypeSize,
                                     &size);
    RoboCheckEqInt(match, (int)RoboCssMediaMatchNo);

    /* A feature the size cannot answer is unknown, not no. */
    memset(conditions, 0, sizeof(conditions));
    conditions[0].feature = RoboCssMediaFeaturePrefersContrast;
    conditions[0].keyword = CssSliceFromText("more");
    match = CssContainerQueryMatches(conditions, 1u, RoboCssContainerTypeSize,
                                     &size);
    RoboCheckEqInt(match, (int)RoboCssMediaMatchUnknown);

    /* A NULL size is unknown rather than a crash. */
    match = CssContainerQueryMatches(conditions, 1u, RoboCssContainerTypeSize,
                                     NULL);
    RoboCheckEqInt(match, (int)RoboCssMediaMatchUnknown);
}

/* ------------------------------------------------------------------ */
/* The computed shape and registration                                 */
/* ------------------------------------------------------------------ */

static void TestContainerComputedAndRegistry(void)
{
    static const struct {
        const char *name;
        CssPropertyComputation computation;
        const char *shape;
    } expected[] = {
        {"container-type", RoboCssPropertyComputationContainerType, "normal"},
        {"container-name", RoboCssPropertyComputationPreserve, "none"},
        {"container", RoboCssPropertyComputationPreserve, "none,_,/,_,normal"}
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationContainerType, "o: size", "size");
    CssComputeEquals(RoboCssPropertyComputationContainerType, "o: inline-size",
                       "inline-size");
    CssComputeEquals(RoboCssPropertyComputationContainerType, "o: banana",
                       "not a container-type value");

    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, expected[index].name);
        char shape[128];

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckEqInt(definition->computation, expected[index].computation);
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }
    CssPropertyRegistryDestroy(registry);
}

static void TestContainerCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.containerQueries, 1);
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 36);
    RoboCheckStrEq(CssVersionString(), "0.36.0");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationContainerType),
                      "container-type");
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainContainer),
                   "container");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainContainer), 1);
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void TestRegisterCssContainer(void);

void TestRegisterCssContainer(void)
{
    TestRegister("css.container.type_vocabulary", TestContainerTypeVocabulary);
    TestRegister("css.container.matcher", TestContainerMatcher);
    TestRegister("css.container.computed", TestContainerComputedAndRegistry);
    TestRegister("css.container.capabilities", TestContainerCapabilities);
}
