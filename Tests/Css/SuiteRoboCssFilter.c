/*
 * RoboCSS filter suite (Phase 18.19) — css.filter.*
 *
 * The `<filter-function>` name vocabulary, its three independent classifications, and
 * the two carried rows the names are checked against.
 *
 * The classifications are the whole of what this suite is about, because they are the
 * facts a consumer dispatches on and none of them is another's negation. `url()` is in
 * neither the spatial group nor the colour group — it is a reference — so a consumer
 * that read "not spatial" as "colour" would apply a colour matrix to an SVG filter.
 * And the functions split by what they *take*: a `<length>`, an `<angle>`, a colour, or
 * a bare amount — which is the difference between accepting `blur(50%)` and refusing
 * `brightness(2px)`.
 *
 * Nothing here blurs a pixel, shifts a shadow, mixes a colour matrix or fetches an SVG
 * filter. A value at this phase is a name with classifications in it, and every
 * expectation below is about the name, what it takes, or where it came from.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSFilter.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The function vocabulary                                             */
/* ------------------------------------------------------------------ */

static void TestFilterFunctionVocabulary(void)
{
    static const char *const names[] = {
        "url", "blur", "brightness", "contrast", "drop-shadow", "grayscale",
        "hue-rotate", "invert", "opacity", "saturate", "sepia"
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFilterFunctionCount, 11);
    RoboCheckEqInt((int)CssFilterFunctionCount(), 11);
    for (index = 0u; index < 11u; index++) {
        CssFilterFunction function = (CssFilterFunction)index;

        RoboCheckEqInt(CssFilterFunctionIsKnown(function), 1);
        RoboCheckStrEq(CssFilterFunctionName(function), names[index]);
        RoboCheckEqInt(CssFilterFunctionFromText(names[index]), (int)index);
        RoboCheck(CssFilterFunctionSliceIsKnown(
                      CssSliceFromText(names[index])) == 1);
    }
    RoboCheckEqInt(CssFilterFunctionIsKnown((CssFilterFunction)99), 0);
    RoboCheckStrEq(CssFilterFunctionName((CssFilterFunction)99), "");
    RoboCheckEqInt(CssFilterFunctionFromText("HUE-ROTATE"),
                      (int)RoboCssFilterFunctionHueRotate);
    RoboCheckEqInt(CssFilterFunctionFromText(NULL),
                      (int)RoboCssFilterFunctionUrl);
    RoboCheckEqInt(CssFilterFunctionFromText("frobnicate"),
                      (int)RoboCssFilterFunctionUrl);
    RoboCheckEqInt(CssFilterFunctionSliceIsKnown(CssSliceFromText("frobnicate")),
                      0);
    RoboCheckEqInt(CssFilterFunctionSliceIsKnown(CssSliceFromText("huerotate")),
                      0);
    RoboCheckEqInt(CssFilterFunctionSliceIsKnown(CssSliceEmpty()), 0);

    {
        char buffer[RoboCssFilterFunctionTextMax];
        size_t length = 0u;
        char tiny[4];

        RoboCheckEqInt(CssFilterFunctionWrite(RoboCssFilterFunctionDropShadow,
                                              buffer, sizeof(buffer), &length,
                                              NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "drop-shadow");
        RoboCheckEqInt((int)length, 11);
        /* A buffer too small reports the length it needed rather than truncating. */
        RoboCheckEqInt(CssFilterFunctionWrite(RoboCssFilterFunctionDropShadow,
                                              tiny, sizeof(tiny), &length, NULL),
                          RoboNotSupported);
        RoboCheckEqInt((int)length, 11);
        RoboCheckEqInt(CssFilterFunctionWrite((CssFilterFunction)99, buffer,
                                              sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The classifications                                                 */
/* ------------------------------------------------------------------ */

static void TestFilterFunctionClassifications(void)
{
    CssFixture();

    /*
     * What they do: `url` is a reference and neither spatial nor colour; `blur` and
     * `drop-shadow` are spatial; the other eight are colour. The three groups are three
     * columns, so `url` in neither is the fact the pair exists to keep.
     */
    RoboCheckEqInt(CssFilterFunctionIsReference(RoboCssFilterFunctionUrl), 1);
    RoboCheckEqInt(CssFilterFunctionIsReference(RoboCssFilterFunctionBlur), 0);
    RoboCheckEqInt(CssFilterFunctionIsSpatial(RoboCssFilterFunctionUrl), 0);
    RoboCheckEqInt(CssFilterFunctionIsColor(RoboCssFilterFunctionUrl), 0);

    RoboCheckEqInt(CssFilterFunctionIsSpatial(RoboCssFilterFunctionBlur), 1);
    RoboCheckEqInt(CssFilterFunctionIsColor(RoboCssFilterFunctionBlur), 0);
    RoboCheckEqInt(CssFilterFunctionIsSpatial(RoboCssFilterFunctionDropShadow), 1);
    RoboCheckEqInt(CssFilterFunctionIsColor(RoboCssFilterFunctionDropShadow), 0);

    RoboCheckEqInt(CssFilterFunctionIsColor(RoboCssFilterFunctionGrayscale), 1);
    RoboCheckEqInt(CssFilterFunctionIsSpatial(RoboCssFilterFunctionGrayscale), 0);
    RoboCheckEqInt(CssFilterFunctionIsColor(RoboCssFilterFunctionHueRotate), 1);
    RoboCheckEqInt(CssFilterFunctionIsColor(RoboCssFilterFunctionOpacity), 1);
    RoboCheckEqInt(CssFilterFunctionIsColor(RoboCssFilterFunctionSepia), 1);

    /*
     * What they take: a length for `blur` and `drop-shadow`, an angle for
     * `hue-rotate`, a colour for `drop-shadow`, and a bare amount for the seven whose
     * omitted argument defaults to 1. The four columns are independent — `drop-shadow`
     * takes both a length and a colour; `hue-rotate` takes an angle and no amount.
     */
    RoboCheckEqInt(CssFilterFunctionTakesLength(RoboCssFilterFunctionBlur), 1);
    RoboCheckEqInt(CssFilterFunctionTakesLength(
                       RoboCssFilterFunctionDropShadow), 1);
    RoboCheckEqInt(CssFilterFunctionTakesLength(
                       RoboCssFilterFunctionBrightness), 0);
    RoboCheckEqInt(CssFilterFunctionTakesAngle(RoboCssFilterFunctionHueRotate), 1);
    RoboCheckEqInt(CssFilterFunctionTakesAngle(RoboCssFilterFunctionBlur), 0);
    RoboCheckEqInt(CssFilterFunctionTakesColor(
                       RoboCssFilterFunctionDropShadow), 1);
    RoboCheckEqInt(CssFilterFunctionTakesColor(RoboCssFilterFunctionBlur), 0);

    RoboCheckEqInt(CssFilterFunctionTakesAmount(
                       RoboCssFilterFunctionBrightness), 1);
    RoboCheckEqInt(CssFilterFunctionTakesAmount(RoboCssFilterFunctionContrast), 1);
    RoboCheckEqInt(CssFilterFunctionTakesAmount(RoboCssFilterFunctionGrayscale), 1);
    RoboCheckEqInt(CssFilterFunctionTakesAmount(RoboCssFilterFunctionInvert), 1);
    RoboCheckEqInt(CssFilterFunctionTakesAmount(RoboCssFilterFunctionOpacity), 1);
    RoboCheckEqInt(CssFilterFunctionTakesAmount(RoboCssFilterFunctionSaturate), 1);
    RoboCheckEqInt(CssFilterFunctionTakesAmount(RoboCssFilterFunctionSepia), 1);
    /* `blur`, `hue-rotate`, `drop-shadow` and `url` are not amount functions. */
    RoboCheckEqInt(CssFilterFunctionTakesAmount(RoboCssFilterFunctionBlur), 0);
    RoboCheckEqInt(CssFilterFunctionTakesAmount(
                       RoboCssFilterFunctionHueRotate), 0);
    RoboCheckEqInt(CssFilterFunctionTakesAmount(
                       RoboCssFilterFunctionDropShadow), 0);
    RoboCheckEqInt(CssFilterFunctionTakesAmount(RoboCssFilterFunctionUrl), 0);

    /* An unknown function claims nothing at all. */
    RoboCheckEqInt(CssFilterFunctionIsReference((CssFilterFunction)99), 0);
    RoboCheckEqInt(CssFilterFunctionIsSpatial((CssFilterFunction)99), 0);
    RoboCheckEqInt(CssFilterFunctionIsColor((CssFilterFunction)99), 0);
    RoboCheckEqInt(CssFilterFunctionTakesLength((CssFilterFunction)99), 0);
    RoboCheckEqInt(CssFilterFunctionTakesAngle((CssFilterFunction)99), 0);
    RoboCheckEqInt(CssFilterFunctionTakesAmount((CssFilterFunction)99), 0);
    RoboCheckEqInt(CssFilterFunctionTakesColor((CssFilterFunction)99), 0);
}

/* ------------------------------------------------------------------ */
/* Computation                                                         */
/* ------------------------------------------------------------------ */

static void TestFilterComputedShapes(void)
{
    CssFixture();

    /* A filter list is carried whole, *order included*: an ordered sequence of
     * functions is wider than any closed set. */
    CssComputeEquals(RoboCssPropertyComputationPreserve,
                       "o: blur(2px) grayscale(100%)",
                       "blur(2px) grayscale(100%)");
    CssComputeEquals(RoboCssPropertyComputationPreserve,
                       "o: grayscale(100%) blur(2px)",
                       "grayscale(100%) blur(2px)");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: none", "none");
    CssComputeEquals(RoboCssPropertyComputationPreserve,
                       "o: drop-shadow(2px 4px red)", "drop-shadow(2px 4px red)");
    CssComputeEquals(RoboCssPropertyComputationPreserve,
                       "o: url(#f) brightness(0.5)", "url(#f) brightness(0.5)");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPreserve,
                                        "o: none"),
                      "preserved");
    /* A deferred reference is carried with its flag. */
    {
        uint32_t flags = 0u;

        CssComputeText(RoboCssPropertyComputationPreserve, "o: var(--f)",
                         NULL, 0u, &flags);
        RoboCheckEqInt(flags, 1u);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestFilterRegistry(void)
{
    static const struct {
        const char *name;
        const char *initial;
    } rows[] = {
        { "filter", "none" },
        { "backdrop-filter", "none" }
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, rows[index].name);

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(definition->computation,
                          RoboCssPropertyComputationPreserve);
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckStrEq(definition->initialText, rows[index].initial);
    }
    CssPropertyRegistryDestroy(registry);
}

static void TestFilterCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();

    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.filters, 1);
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainFilter), "filter");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainFilter), 1);
    RoboCheckEqInt(RoboCssDomainCount, 37);
    RoboCheckEqInt(RoboCssVersionMinor, 39);
    RoboCheckStrEq(CssVersionString(), "0.39.0");
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestFilterDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: blur(2px) sepia(1)",
                                             &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationPreserve,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationPreserve,
                          CssDeclarationValue(declaration), &second, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&first, &second), 1);

    {
        /* The order is the value: the same functions the other way round are not
         * equal. */
        CssDeclarationList otherList = RoboCssCleared;
        CssError otherError = RoboCssCleared;
        CssComputedValue other = RoboCssCleared;

        RoboCheckEqInt(CssParseDeclarations("o: sepia(1) blur(2px)",
                                                 &otherList, &otherError), RoboOk);
        declaration = CssDeclarationListAt(&otherList, 0u);
        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationPreserve,
                              CssDeclarationValue(declaration), &other, NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedValueEquals(&first, &other), 0);
        CssComputedValueDispose(&other);
        CssDeclarationListDispose(&otherList);
    }

    CssComputedValueDispose(&first);
    CssComputedValueDispose(&second);
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void TestRegisterCssFilter(void);

void TestRegisterCssFilter(void)
{
    TestRegister("css.filter.function_vocabulary", TestFilterFunctionVocabulary);
    TestRegister("css.filter.classifications", TestFilterFunctionClassifications);
    TestRegister("css.filter.computed", TestFilterComputedShapes);
    TestRegister("css.filter.registry", TestFilterRegistry);
    TestRegister("css.filter.capabilities", TestFilterCapabilities);
    TestRegister("css.filter.determinism", TestFilterDeterminism);
}
