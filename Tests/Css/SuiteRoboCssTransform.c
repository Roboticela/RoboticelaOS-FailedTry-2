/*
 * RoboCSS transform suite (Phase 18.7) — css.transform.*
 *
 * The two closed sets a transformed box is described by — `transform-style` and
 * `backface-visibility` — and the closed set of `<transform-function>` names a
 * `transform` list is built from, with its 3D and matrix classifications.
 *
 * The function-name set is the substance of the phase: it is what a transform list's
 * members are checked against, and the classifications are what a later pass reads to
 * decide whether the list needs the 3D machinery and whether one member already fixes
 * the whole transform. Nothing here composes a list or multiplies a matrix.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSTransform.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabularies                                                    */
/* ------------------------------------------------------------------ */

static void TestTransformStyleVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssTransformStyleCount, 2);
    RoboCheckEqInt((int)CssTransformStyleCount(), 2);
    RoboCheckStrEq(CssTransformStyleName(RoboCssTransformStyleFlat), "flat");
    RoboCheckStrEq(CssTransformStyleName(RoboCssTransformStylePreserve3d),
                   "preserve-3d");
    RoboCheckEqInt(CssTransformStyleFromText("preserve-3d"),
                   (int)RoboCssTransformStylePreserve3d);
    RoboCheckEqInt(CssTransformStyleFromText("PRESERVE-3D"),
                   (int)RoboCssTransformStylePreserve3d);
    RoboCheckEqInt(CssTransformStyleFromText(NULL),
                   (int)RoboCssTransformStyleFlat);
    RoboCheckEqInt(CssTransformStyleFromText("banana"),
                   (int)RoboCssTransformStyleFlat);
    RoboCheckEqInt(CssTransformStyleIsKnown((CssTransformStyle)99), 0);
    RoboCheckEqInt(CssTransformStyleSliceIsKnown(CssSliceFromText("flat")), 1);
    RoboCheckEqInt(CssTransformStyleSliceIsKnown(CssSliceFromText("banana")), 0);

    /* `preserve-3d` is the fact that establishes a 3D rendering context. */
    RoboCheckEqInt(CssTransformStyleCreates3DContext(RoboCssTransformStyleFlat),
                   0);
    RoboCheckEqInt(
        CssTransformStyleCreates3DContext(RoboCssTransformStylePreserve3d), 1);
    RoboCheckEqInt(CssTransformStyleCreates3DContext((CssTransformStyle)99), 0);

    {
        char buffer[RoboCssTransformStyleTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTransformStyleWrite(RoboCssTransformStylePreserve3d,
                                                   buffer, sizeof(buffer),
                                                   &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "preserve-3d");
        /* A too-small buffer is refused with the length it needed. */
        RoboCheckEqInt(CssTransformStyleWrite(RoboCssTransformStylePreserve3d,
                                                   buffer, 4u, &length, NULL),
                          RoboNotSupported);
        RoboCheckStrEq(buffer, "");
        RoboCheckEqInt((int)length, 11);
        RoboCheckEqInt(CssTransformStyleWrite((CssTransformStyle)99, buffer,
                                                   sizeof(buffer), &length,
                                                   NULL),
                          RoboInvalidArgument);
    }
}

static void TestBackfaceVisibilityVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssBackfaceVisibilityCount, 2);
    RoboCheckEqInt((int)CssBackfaceVisibilityCount(), 2);
    RoboCheckStrEq(CssBackfaceVisibilityName(RoboCssBackfaceVisibilityVisible),
                   "visible");
    RoboCheckStrEq(CssBackfaceVisibilityName(RoboCssBackfaceVisibilityHidden),
                   "hidden");
    RoboCheckEqInt(CssBackfaceVisibilityFromText("hidden"),
                   (int)RoboCssBackfaceVisibilityHidden);
    RoboCheckEqInt(CssBackfaceVisibilityFromText(NULL),
                   (int)RoboCssBackfaceVisibilityVisible);
    RoboCheckEqInt(CssBackfaceVisibilityFromText("banana"),
                   (int)RoboCssBackfaceVisibilityVisible);
    RoboCheckEqInt(CssBackfaceVisibilityIsKnown((CssBackfaceVisibility)99), 0);
    RoboCheckEqInt(
        CssBackfaceVisibilitySliceIsKnown(CssSliceFromText("hidden")), 1);
    RoboCheckEqInt(
        CssBackfaceVisibilitySliceIsKnown(CssSliceFromText("banana")), 0);

    RoboCheckEqInt(
        CssBackfaceVisibilityShowsBackface(RoboCssBackfaceVisibilityVisible), 1);
    RoboCheckEqInt(
        CssBackfaceVisibilityShowsBackface(RoboCssBackfaceVisibilityHidden), 0);
    RoboCheckEqInt(
        CssBackfaceVisibilityShowsBackface((CssBackfaceVisibility)99), 0);

    {
        char buffer[RoboCssBackfaceVisibilityTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssBackfaceVisibilityWrite(RoboCssBackfaceVisibilityHidden,
                                                        buffer, sizeof(buffer),
                                                        &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "hidden");
        RoboCheckEqInt(CssBackfaceVisibilityWrite((CssBackfaceVisibility)99,
                                                        buffer, sizeof(buffer),
                                                        &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestTransformFunctionVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssTransformFunctionCount, 21);
    RoboCheckEqInt((int)CssTransformFunctionCount(), 21);
    RoboCheckStrEq(CssTransformFunctionName(RoboCssTransformFunctionMatrix),
                   "matrix");
    RoboCheckStrEq(CssTransformFunctionName(RoboCssTransformFunctionMatrix3d),
                   "matrix3d");
    RoboCheckStrEq(CssTransformFunctionName(RoboCssTransformFunctionPerspective),
                   "perspective");
    RoboCheckStrEq(CssTransformFunctionName(RoboCssTransformFunctionRotate3d),
                   "rotate3d");
    RoboCheckEqInt(CssTransformFunctionFromText("translateZ"),
                   (int)RoboCssTransformFunctionTranslateZ);
    RoboCheckEqInt(CssTransformFunctionFromText("MATRIX3D"),
                   (int)RoboCssTransformFunctionMatrix3d);
    RoboCheckEqInt(CssTransformFunctionFromText("frobnicate"),
                   (int)RoboCssTransformFunctionMatrix);
    RoboCheckEqInt(CssTransformFunctionIsKnown(
                       (CssTransformFunction)99),
                   0);
    RoboCheckEqInt(
        CssTransformFunctionSliceIsKnown(CssSliceFromText("scale3d")), 1);
    RoboCheckEqInt(
        CssTransformFunctionSliceIsKnown(CssSliceFromText("frobnicate")), 0);

    /* The 3D column: the two `*3d` families and the two Z forms. */
    RoboCheckEqInt(CssTransformFunctionIs3D(RoboCssTransformFunctionMatrix), 0);
    RoboCheckEqInt(CssTransformFunctionIs3D(RoboCssTransformFunctionMatrix3d), 1);
    RoboCheckEqInt(CssTransformFunctionIs3D(RoboCssTransformFunctionTranslate),
                   0);
    RoboCheckEqInt(
        CssTransformFunctionIs3D(RoboCssTransformFunctionTranslateZ), 1);
    RoboCheckEqInt(CssTransformFunctionIs3D(RoboCssTransformFunctionScaleZ), 1);
    RoboCheckEqInt(CssTransformFunctionIs3D(RoboCssTransformFunctionRotate3d),
                   1);
    RoboCheckEqInt(CssTransformFunctionIs3D(RoboCssTransformFunctionRotateX), 1);
    RoboCheckEqInt(CssTransformFunctionIs3D(RoboCssTransformFunctionRotate), 0);
    RoboCheckEqInt(CssTransformFunctionIs3D(RoboCssTransformFunctionSkew), 0);
    RoboCheckEqInt(CssTransformFunctionIs3D(RoboCssTransformFunctionPerspective),
                   1);
    RoboCheckEqInt(CssTransformFunctionIs3D((CssTransformFunction)99), 0);

    /* The matrix column: exactly the two forms that already fix the transform. */
    RoboCheckEqInt(CssTransformFunctionIsMatrix(RoboCssTransformFunctionMatrix),
                   1);
    RoboCheckEqInt(
        CssTransformFunctionIsMatrix(RoboCssTransformFunctionMatrix3d), 1);
    RoboCheckEqInt(
        CssTransformFunctionIsMatrix(RoboCssTransformFunctionTranslate), 0);
    RoboCheckEqInt(CssTransformFunctionIsMatrix(RoboCssTransformFunctionRotate),
                   0);
    RoboCheckEqInt(CssTransformFunctionIsMatrix((CssTransformFunction)99), 0);

    {
        char buffer[RoboCssTransformFunctionTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTransformFunctionWrite(
                           RoboCssTransformFunctionPerspective, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "perspective");
        RoboCheckEqInt(CssTransformFunctionWrite((CssTransformFunction)99,
                                                       buffer, sizeof(buffer),
                                                       &length, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The computed values                                                 */
/* ------------------------------------------------------------------ */

static void TestTransformComputedShapes(void)
{
    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationTransformStyle, "o: preserve-3d",
                       "preserve-3d");
    CssComputeEquals(RoboCssPropertyComputationTransformStyle, "o: flat",
                       "flat");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationTransformStyle,
                                        "o: preserve-3d"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationTransformStyle, "o: banana",
                       "not a transform-style value");

    CssComputeEquals(RoboCssPropertyComputationBackfaceVisibility, "o: hidden",
                       "hidden");
    CssComputeEquals(RoboCssPropertyComputationBackfaceVisibility, "o: visible",
                       "visible");
    CssComputeEquals(RoboCssPropertyComputationBackfaceVisibility, "o: banana",
                       "not a backface-visibility value");
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestTransformRegistry(void)
{
    static const struct {
        const char *name;
        CssPropertyComputation computation;
        const char *shape;
    } expected[] = {
        {"transform", RoboCssPropertyComputationPreserve, "none"},
        {"transform-origin", RoboCssPropertyComputationPreserve, "50%,_,50%"},
        {"transform-style", RoboCssPropertyComputationTransformStyle, "flat"},
        {"perspective", RoboCssPropertyComputationPreserve, "none"},
        {"perspective-origin", RoboCssPropertyComputationPreserve,
         "50%,_,50%"},
        {"backface-visibility", RoboCssPropertyComputationBackfaceVisibility,
         "visible"},
        {"translate", RoboCssPropertyComputationPreserve, "none"},
        {"rotate", RoboCssPropertyComputationPreserve, "none"},
        {"scale", RoboCssPropertyComputationPreserve, "none"}
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, expected[index].name);
        char shape[128];

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        /* A transform is the box's own: a child that inherited its parent's would
         * move with the parent rather than with the layout. */
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckEqInt(definition->computation, expected[index].computation);
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestTransformCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.transforms, 1);
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 36);
    RoboCheckStrEq(CssVersionString(), "0.36.0");

    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationTransformStyle),
                      "transform-style");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationBackfaceVisibility),
                      1);
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainTransform),
                   "transform");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainTransform), 1);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestTransformDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: preserve-3d", &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationTransformStyle,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationTransformStyle,
                          CssDeclarationValue(declaration), &second, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&first, &second), 1);

    CssComputedValueDispose(&first);
    CssComputedValueDispose(&second);
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void TestRegisterCssTransform(void);

void TestRegisterCssTransform(void)
{
    TestRegister("css.transform.style_vocabulary", TestTransformStyleVocabulary);
    TestRegister("css.transform.backface_vocabulary",
                 TestBackfaceVisibilityVocabulary);
    TestRegister("css.transform.function_vocabulary",
                 TestTransformFunctionVocabulary);
    TestRegister("css.transform.computed", TestTransformComputedShapes);
    TestRegister("css.transform.registry", TestTransformRegistry);
    TestRegister("css.transform.capabilities", TestTransformCapabilities);
    TestRegister("css.transform.determinism", TestTransformDeterminism);
}
