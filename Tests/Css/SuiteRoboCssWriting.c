/*
 * RoboCSS writing-mode suite (Phase 18.13) — css.writing.*
 *
 * The three closed vocabularies that together decide which physical edge is
 * inline-start and how a character stands inside a vertical line: `direction`,
 * `writing-mode` and `text-orientation`. The interesting assertions are the two
 * classification columns on `writing-mode` — vertical and sideways are separate
 * questions — and the fact that `direction`, which had the bare KEYWORD shape
 * since 9.7, now refuses an identifier that is not a direction.
 *
 * Nothing here asserts a layout or a shaped run. A writing-mode value at this
 * phase is a *value* with classifications in it; turning it into a run, a line
 * or a caret belongs to RoboText, and turning it into a fragment belongs to the
 * phase that has a box.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSWriting.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabularies                                                    */
/* ------------------------------------------------------------------ */

static void TestDirectionVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssDirectionCount, 2);
    RoboCheckEqInt((int)CssDirectionCount(), 2);
    RoboCheckStrEq(CssDirectionName(RoboCssDirectionLtr), "ltr");
    RoboCheckStrEq(CssDirectionName(RoboCssDirectionRtl), "rtl");
    RoboCheckEqInt(CssDirectionFromText("rtl"), (int)RoboCssDirectionRtl);
    RoboCheckEqInt(CssDirectionFromText("RTL"), (int)RoboCssDirectionRtl);
    RoboCheckEqInt(CssDirectionFromText(NULL), (int)RoboCssDirectionLtr);
    RoboCheckEqInt(CssDirectionFromText("banana"), (int)RoboCssDirectionLtr);
    RoboCheckEqInt(CssDirectionIsKnown((CssDirection)99), 0);
    RoboCheckEqInt(CssDirectionSliceIsKnown(CssSliceFromText("ltr")), 1);
    RoboCheckEqInt(CssDirectionSliceIsKnown(CssSliceFromText("banana")), 0);

    /* The one classification the bidi and logical-property layers read. */
    RoboCheckEqInt(CssDirectionIsRightToLeft(RoboCssDirectionLtr), 0);
    RoboCheckEqInt(CssDirectionIsRightToLeft(RoboCssDirectionRtl), 1);
    RoboCheckEqInt(CssDirectionIsRightToLeft((CssDirection)99), 0);

    {
        char buffer[RoboCssDirectionTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssDirectionWrite(RoboCssDirectionRtl, buffer,
                                             sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "rtl");
        /* A too-small buffer is refused with the length it needed. */
        RoboCheckEqInt(CssDirectionWrite(RoboCssDirectionRtl, buffer, 2u,
                                             &length, NULL),
                          RoboNotSupported);
        RoboCheckStrEq(buffer, "");
        RoboCheckEqInt((int)length, 3);
        RoboCheckEqInt(CssDirectionWrite((CssDirection)99, buffer,
                                             sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestWritingModeVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssWritingModeCount, 5);
    RoboCheckEqInt((int)CssWritingModeCount(), 5);
    RoboCheckStrEq(CssWritingModeName(RoboCssWritingModeHorizontalTb),
                   "horizontal-tb");
    RoboCheckStrEq(CssWritingModeName(RoboCssWritingModeVerticalRl),
                   "vertical-rl");
    RoboCheckStrEq(CssWritingModeName(RoboCssWritingModeVerticalLr),
                   "vertical-lr");
    RoboCheckStrEq(CssWritingModeName(RoboCssWritingModeSidewaysRl),
                   "sideways-rl");
    RoboCheckStrEq(CssWritingModeName(RoboCssWritingModeSidewaysLr),
                   "sideways-lr");
    RoboCheckEqInt(CssWritingModeFromText("vertical-rl"),
                   (int)RoboCssWritingModeVerticalRl);
    RoboCheckEqInt(CssWritingModeFromText("SIDEWAYS-LR"),
                   (int)RoboCssWritingModeSidewaysLr);
    RoboCheckEqInt(CssWritingModeFromText(NULL),
                   (int)RoboCssWritingModeHorizontalTb);
    RoboCheckEqInt(CssWritingModeFromText("banana"),
                   (int)RoboCssWritingModeHorizontalTb);
    RoboCheckEqInt(CssWritingModeIsKnown((CssWritingMode)99), 0);
    RoboCheckEqInt(CssWritingModeSliceIsKnown(CssSliceFromText("sideways-rl")),
                   1);
    RoboCheckEqInt(CssWritingModeSliceIsKnown(CssSliceFromText("banana")), 0);

    /* Horizontal and vertical are the axis question. */
    RoboCheckEqInt(CssWritingModeIsHorizontal(RoboCssWritingModeHorizontalTb), 1);
    RoboCheckEqInt(CssWritingModeIsHorizontal(RoboCssWritingModeVerticalRl), 0);
    RoboCheckEqInt(CssWritingModeIsVertical(RoboCssWritingModeHorizontalTb), 0);
    RoboCheckEqInt(CssWritingModeIsVertical(RoboCssWritingModeVerticalRl), 1);
    RoboCheckEqInt(CssWritingModeIsVertical(RoboCssWritingModeSidewaysLr), 1);
    RoboCheckEqInt(CssWritingModeIsHorizontal((CssWritingMode)99), 0);

    /* Sideways is a *separate* question: the vertical pair is not sideways, and a
     * sideways mode is vertical *and* sideways. This is the assertion a build that
     * derived one column from the other would fail. */
    RoboCheckEqInt(CssWritingModeIsSideways(RoboCssWritingModeVerticalRl), 0);
    RoboCheckEqInt(CssWritingModeIsSideways(RoboCssWritingModeVerticalLr), 0);
    RoboCheckEqInt(CssWritingModeIsSideways(RoboCssWritingModeSidewaysRl), 1);
    RoboCheckEqInt(CssWritingModeIsSideways(RoboCssWritingModeSidewaysLr), 1);
    RoboCheckEqInt(CssWritingModeIsSideways(RoboCssWritingModeHorizontalTb), 0);
    RoboCheckEqInt(CssWritingModeIsSideways((CssWritingMode)99), 0);

    {
        char buffer[RoboCssWritingModeTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssWritingModeWrite(RoboCssWritingModeSidewaysLr, buffer,
                                                sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "sideways-lr");
        RoboCheckEqInt(CssWritingModeWrite((CssWritingMode)99, buffer,
                                                sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestTextOrientationVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssTextOrientationCount, 3);
    RoboCheckEqInt((int)CssTextOrientationCount(), 3);
    RoboCheckStrEq(CssTextOrientationName(RoboCssTextOrientationMixed),
                   "mixed");
    RoboCheckStrEq(CssTextOrientationName(RoboCssTextOrientationUpright),
                   "upright");
    RoboCheckStrEq(CssTextOrientationName(RoboCssTextOrientationSideways),
                   "sideways");
    RoboCheckEqInt(CssTextOrientationFromText("upright"),
                   (int)RoboCssTextOrientationUpright);
    RoboCheckEqInt(CssTextOrientationFromText(NULL),
                   (int)RoboCssTextOrientationMixed);
    RoboCheckEqInt(CssTextOrientationFromText("banana"),
                   (int)RoboCssTextOrientationMixed);
    RoboCheckEqInt(CssTextOrientationIsKnown((CssTextOrientation)99), 0);
    RoboCheckEqInt(CssTextOrientationSliceIsKnown(CssSliceFromText("sideways")),
                   1);
    RoboCheckEqInt(CssTextOrientationSliceIsKnown(CssSliceFromText("banana")), 0);

    RoboCheckEqInt(CssTextOrientationIsSideways(RoboCssTextOrientationMixed), 0);
    RoboCheckEqInt(CssTextOrientationIsSideways(RoboCssTextOrientationUpright),
                   0);
    RoboCheckEqInt(CssTextOrientationIsSideways(RoboCssTextOrientationSideways),
                   1);
    RoboCheckEqInt(CssTextOrientationIsSideways((CssTextOrientation)99), 0);

    {
        char buffer[RoboCssTextOrientationTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTextOrientationWrite(RoboCssTextOrientationSideways,
                                                     buffer, sizeof(buffer),
                                                     &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "sideways");
        RoboCheckEqInt(CssTextOrientationWrite((CssTextOrientation)99, buffer,
                                                     sizeof(buffer), &length,
                                                     NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The computed values                                                 */
/* ------------------------------------------------------------------ */

static void TestWritingComputedShapes(void)
{
    CssFixture();

    /* `direction`: the shape upgraded in place from the bare KEYWORD shape. */
    CssComputeEquals(RoboCssPropertyComputationDirection, "o: rtl", "rtl");
    CssComputeEquals(RoboCssPropertyComputationDirection, "o: LTR", "ltr");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationDirection,
                                        "o: rtl"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationDirection, "o: banana",
                       "not a direction value");

    /* `writing-mode`: five values, and `sideways-rl` is a value it carries. */
    CssComputeEquals(RoboCssPropertyComputationWritingMode, "o: vertical-rl",
                       "vertical-rl");
    CssComputeEquals(RoboCssPropertyComputationWritingMode, "o: sideways-lr",
                       "sideways-lr");
    CssComputeEquals(RoboCssPropertyComputationWritingMode, "o: horizontal-tb",
                       "horizontal-tb");
    CssComputeEquals(RoboCssPropertyComputationWritingMode, "o: banana",
                       "not a writing-mode value");

    /* `text-orientation`. */
    CssComputeEquals(RoboCssPropertyComputationTextOrientation, "o: upright",
                       "upright");
    CssComputeEquals(RoboCssPropertyComputationTextOrientation, "o: sideways",
                       "sideways");
    CssComputeEquals(RoboCssPropertyComputationTextOrientation, "o: banana",
                       "not a text-orientation value");
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestWritingRegistry(void)
{
    static const struct {
        const char *name;
        CssPropertyComputation computation;
        const char *shape;
        int inherited;
    } expected[] = {
        {"direction", RoboCssPropertyComputationDirection, "ltr", 1},
        {"writing-mode", RoboCssPropertyComputationWritingMode,
         "horizontal-tb", 1},
        {"text-orientation", RoboCssPropertyComputationTextOrientation, "mixed",
         1},
        /* `direction` keeps its old name spelling and its old initial; the row is the
         * one that already existed, upgraded. */
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
        /* All three are inherited: a child on axes its parent was not on cannot be
         * described by not inheriting them. */
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition),
                       expected[index].inherited);
        RoboCheckEqInt(definition->computation, expected[index].computation);
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestWritingCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.writingModes, 1);
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 36);
    RoboCheckStrEq(CssVersionString(), "0.36.0");

    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationWritingMode),
                      "writing-mode");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationTextOrientation),
                      1);
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainWriting), "writing");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainWriting), 1);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestWritingDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: vertical-rl", &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationWritingMode,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationWritingMode,
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

void TestRegisterCssWriting(void);

void TestRegisterCssWriting(void)
{
    TestRegister("css.writing.direction_vocabulary", TestDirectionVocabulary);
    TestRegister("css.writing.mode_vocabulary", TestWritingModeVocabulary);
    TestRegister("css.writing.orientation_vocabulary",
                 TestTextOrientationVocabulary);
    TestRegister("css.writing.computed", TestWritingComputedShapes);
    TestRegister("css.writing.registry", TestWritingRegistry);
    TestRegister("css.writing.capabilities", TestWritingCapabilities);
    TestRegister("css.writing.determinism", TestWritingDeterminism);
}
