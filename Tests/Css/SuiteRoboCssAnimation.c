/*
 * RoboCSS animation suite (Phase 18.9) — css.animation.*
 *
 * The four closed sets an animation is described by — direction, fill mode, play
 * state and composition — the `animation-iteration-count` shape, and the carried
 * declarations the rest of the family is.
 *
 * Two facts are the substance. `animation-direction` carries two classification
 * columns because reversing and alternating are different questions (an alternating
 * animation reverses only on odd iterations). And `animation-iteration-count` accepts
 * a *fraction*: a half iteration is meaningful, so the bound is a floor and a
 * non-integer is a value rather than a mistake.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSAnimation.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabularies                                                    */
/* ------------------------------------------------------------------ */

static void TestAnimationDirectionVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssAnimationDirectionCount, 4);
    RoboCheckEqInt((int)CssAnimationDirectionCount(), 4);
    RoboCheckStrEq(CssAnimationDirectionName(RoboCssAnimationDirectionNormal),
                   "normal");
    RoboCheckStrEq(CssAnimationDirectionName(RoboCssAnimationDirectionReverse),
                   "reverse");
    RoboCheckStrEq(
        CssAnimationDirectionName(RoboCssAnimationDirectionAlternate),
        "alternate");
    RoboCheckStrEq(
        CssAnimationDirectionName(RoboCssAnimationDirectionAlternateReverse),
        "alternate-reverse");
    RoboCheckEqInt(CssAnimationDirectionFromText("alternate-reverse"),
                   (int)RoboCssAnimationDirectionAlternateReverse);
    RoboCheckEqInt(CssAnimationDirectionFromText("ALTERNATE"),
                   (int)RoboCssAnimationDirectionAlternate);
    RoboCheckEqInt(CssAnimationDirectionFromText(NULL),
                   (int)RoboCssAnimationDirectionNormal);
    RoboCheckEqInt(CssAnimationDirectionFromText("banana"),
                   (int)RoboCssAnimationDirectionNormal);
    RoboCheckEqInt(CssAnimationDirectionIsKnown((CssAnimationDirection)99), 0);
    RoboCheckEqInt(
        CssAnimationDirectionSliceIsKnown(CssSliceFromText("alternate")), 1);
    RoboCheckEqInt(
        CssAnimationDirectionSliceIsKnown(CssSliceFromText("banana")), 0);

    /* Reversing and alternating are two separate answers. */
    RoboCheckEqInt(CssAnimationDirectionReverses(
                       RoboCssAnimationDirectionNormal),
                   0);
    RoboCheckEqInt(CssAnimationDirectionReverses(
                       RoboCssAnimationDirectionReverse),
                   1);
    RoboCheckEqInt(CssAnimationDirectionReverses(
                       RoboCssAnimationDirectionAlternate),
                   0);
    RoboCheckEqInt(CssAnimationDirectionReverses(
                       RoboCssAnimationDirectionAlternateReverse),
                   1);
    RoboCheckEqInt(CssAnimationDirectionReverses((CssAnimationDirection)99), 0);

    RoboCheckEqInt(CssAnimationDirectionAlternates(
                       RoboCssAnimationDirectionNormal),
                   0);
    RoboCheckEqInt(CssAnimationDirectionAlternates(
                       RoboCssAnimationDirectionReverse),
                   0);
    RoboCheckEqInt(CssAnimationDirectionAlternates(
                       RoboCssAnimationDirectionAlternate),
                   1);
    RoboCheckEqInt(CssAnimationDirectionAlternates(
                       RoboCssAnimationDirectionAlternateReverse),
                   1);
    RoboCheckEqInt(CssAnimationDirectionAlternates((CssAnimationDirection)99),
                   0);

    {
        char buffer[RoboCssAnimationDirectionTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssAnimationDirectionWrite(
                           RoboCssAnimationDirectionAlternateReverse, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "alternate-reverse");
        RoboCheckEqInt(CssAnimationDirectionWrite((CssAnimationDirection)99,
                                                        buffer, sizeof(buffer),
                                                        &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestAnimationFillModeVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssAnimationFillModeCount, 4);
    RoboCheckEqInt((int)CssAnimationFillModeCount(), 4);
    RoboCheckStrEq(CssAnimationFillModeName(RoboCssAnimationFillModeNone),
                   "none");
    RoboCheckStrEq(CssAnimationFillModeName(RoboCssAnimationFillModeForwards),
                   "forwards");
    RoboCheckStrEq(CssAnimationFillModeName(RoboCssAnimationFillModeBackwards),
                   "backwards");
    RoboCheckStrEq(CssAnimationFillModeName(RoboCssAnimationFillModeBoth),
                   "both");
    RoboCheckEqInt(CssAnimationFillModeFromText("backwards"),
                   (int)RoboCssAnimationFillModeBackwards);
    RoboCheckEqInt(CssAnimationFillModeFromText("banana"),
                   (int)RoboCssAnimationFillModeNone);
    RoboCheckEqInt(CssAnimationFillModeIsKnown((CssAnimationFillMode)99), 0);
    RoboCheckEqInt(CssAnimationFillModeSliceIsKnown(CssSliceFromText("both")),
                   1);
    RoboCheckEqInt(CssAnimationFillModeSliceIsKnown(CssSliceFromText("banana")),
                   0);

    RoboCheckEqInt(
        CssAnimationFillModeFillsForwards(RoboCssAnimationFillModeNone), 0);
    RoboCheckEqInt(
        CssAnimationFillModeFillsForwards(RoboCssAnimationFillModeForwards), 1);
    RoboCheckEqInt(
        CssAnimationFillModeFillsForwards(RoboCssAnimationFillModeBackwards),
        0);
    RoboCheckEqInt(
        CssAnimationFillModeFillsForwards(RoboCssAnimationFillModeBoth), 1);
    RoboCheckEqInt(
        CssAnimationFillModeFillsBackwards(RoboCssAnimationFillModeNone), 0);
    RoboCheckEqInt(
        CssAnimationFillModeFillsBackwards(RoboCssAnimationFillModeForwards),
        0);
    RoboCheckEqInt(
        CssAnimationFillModeFillsBackwards(RoboCssAnimationFillModeBackwards),
        1);
    RoboCheckEqInt(
        CssAnimationFillModeFillsBackwards(RoboCssAnimationFillModeBoth), 1);
    RoboCheckEqInt(
        CssAnimationFillModeFillsForwards((CssAnimationFillMode)99), 0);
    RoboCheckEqInt(
        CssAnimationFillModeFillsBackwards((CssAnimationFillMode)99), 0);

    {
        char buffer[RoboCssAnimationFillModeTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssAnimationFillModeWrite(
                           RoboCssAnimationFillModeBackwards, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "backwards");
        RoboCheckEqInt(CssAnimationFillModeWrite((CssAnimationFillMode)99,
                                                       buffer, sizeof(buffer),
                                                       &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestAnimationPlayStateVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssAnimationPlayStateCount, 2);
    RoboCheckEqInt((int)CssAnimationPlayStateCount(), 2);
    RoboCheckStrEq(CssAnimationPlayStateName(RoboCssAnimationPlayStateRunning),
                   "running");
    RoboCheckStrEq(CssAnimationPlayStateName(RoboCssAnimationPlayStatePaused),
                   "paused");
    RoboCheckEqInt(CssAnimationPlayStateFromText("paused"),
                   (int)RoboCssAnimationPlayStatePaused);
    RoboCheckEqInt(CssAnimationPlayStateFromText("banana"),
                   (int)RoboCssAnimationPlayStateRunning);
    RoboCheckEqInt(CssAnimationPlayStateIsKnown((CssAnimationPlayState)99), 0);
    RoboCheckEqInt(
        CssAnimationPlayStateSliceIsKnown(CssSliceFromText("paused")), 1);
    RoboCheckEqInt(
        CssAnimationPlayStateSliceIsKnown(CssSliceFromText("banana")), 0);

    RoboCheckEqInt(
        CssAnimationPlayStateIsRunning(RoboCssAnimationPlayStateRunning), 1);
    RoboCheckEqInt(
        CssAnimationPlayStateIsRunning(RoboCssAnimationPlayStatePaused), 0);
    RoboCheckEqInt(CssAnimationPlayStateIsRunning((CssAnimationPlayState)99),
                   0);

    {
        char buffer[RoboCssAnimationPlayStateTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssAnimationPlayStateWrite(
                           RoboCssAnimationPlayStatePaused, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "paused");
        RoboCheckEqInt(CssAnimationPlayStateWrite((CssAnimationPlayState)99,
                                                        buffer, sizeof(buffer),
                                                        &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestAnimationCompositionVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssAnimationCompositionCount, 3);
    RoboCheckEqInt((int)CssAnimationCompositionCount(), 3);
    RoboCheckStrEq(
        CssAnimationCompositionName(RoboCssAnimationCompositionReplace),
        "replace");
    RoboCheckStrEq(CssAnimationCompositionName(RoboCssAnimationCompositionAdd),
                   "add");
    RoboCheckStrEq(
        CssAnimationCompositionName(RoboCssAnimationCompositionAccumulate),
        "accumulate");
    RoboCheckEqInt(CssAnimationCompositionFromText("accumulate"),
                   (int)RoboCssAnimationCompositionAccumulate);
    RoboCheckEqInt(CssAnimationCompositionFromText("banana"),
                   (int)RoboCssAnimationCompositionReplace);
    RoboCheckEqInt(CssAnimationCompositionIsKnown(
                       (CssAnimationComposition)99),
                   0);
    RoboCheckEqInt(
        CssAnimationCompositionSliceIsKnown(CssSliceFromText("add")), 1);
    RoboCheckEqInt(
        CssAnimationCompositionSliceIsKnown(CssSliceFromText("banana")), 0);

    RoboCheckEqInt(
        CssAnimationCompositionCombines(RoboCssAnimationCompositionReplace), 0);
    RoboCheckEqInt(
        CssAnimationCompositionCombines(RoboCssAnimationCompositionAdd), 1);
    RoboCheckEqInt(CssAnimationCompositionCombines(
                       RoboCssAnimationCompositionAccumulate),
                   1);
    RoboCheckEqInt(
        CssAnimationCompositionCombines((CssAnimationComposition)99), 0);

    {
        char buffer[RoboCssAnimationCompositionTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssAnimationCompositionWrite(
                           RoboCssAnimationCompositionAccumulate, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "accumulate");
        RoboCheckEqInt(CssAnimationCompositionWrite(
                           (CssAnimationComposition)99, buffer, sizeof(buffer),
                           &length, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The computed values                                                 */
/* ------------------------------------------------------------------ */

static void TestAnimationComputedShapes(void)
{
    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationAnimationDirection,
                       "o: alternate-reverse", "alternate-reverse");
    CssComputeEquals(RoboCssPropertyComputationAnimationDirection, "o: banana",
                       "not an animation-direction value");
    CssComputeEquals(RoboCssPropertyComputationAnimationFillMode, "o: both",
                       "both");
    CssComputeEquals(RoboCssPropertyComputationAnimationFillMode, "o: banana",
                       "not an animation-fill-mode value");
    CssComputeEquals(RoboCssPropertyComputationAnimationPlayState, "o: paused",
                       "paused");
    CssComputeEquals(RoboCssPropertyComputationAnimationPlayState, "o: banana",
                       "not an animation-play-state value");
    CssComputeEquals(RoboCssPropertyComputationAnimationComposition, "o: add",
                       "add");
    CssComputeEquals(RoboCssPropertyComputationAnimationComposition,
                       "o: banana", "not an animation-composition value");

    /* The one numeric shape: `infinite`, a whole count, and a *fraction*, with the
     * one refusal (a negative count). */
    CssComputeEquals(RoboCssPropertyComputationAnimationIterationCount,
                       "o: infinite", "infinite");
    RoboCheckStrEq(CssComputeKind(
                       RoboCssPropertyComputationAnimationIterationCount,
                       "o: infinite"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationAnimationIterationCount, "o: 3",
                       "3");
    CssComputeEquals(RoboCssPropertyComputationAnimationIterationCount,
                       "o: 2.5", "2.5");
    RoboCheckStrEq(CssComputeKind(
                       RoboCssPropertyComputationAnimationIterationCount,
                       "o: 2.5"),
                      "number");
    CssComputeEquals(RoboCssPropertyComputationAnimationIterationCount,
                       "o: -1", "an iteration count cannot be negative");
    CssComputeEquals(RoboCssPropertyComputationAnimationIterationCount,
                       "o: banana", "not infinite or a non-negative number");
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestAnimationRegistry(void)
{
    static const struct {
        const char *name;
        CssPropertyComputation computation;
        const char *shape;
    } expected[] = {
        {"animation", RoboCssPropertyComputationPreserve,
         "none,_,0s,_,ease,_,0s,_,1,_,normal,_,none,_,running"},
        {"animation-composition", RoboCssPropertyComputationAnimationComposition,
         "replace"},
        {"animation-delay", RoboCssPropertyComputationPreserve, "0s"},
        {"animation-direction", RoboCssPropertyComputationAnimationDirection,
         "normal"},
        {"animation-duration", RoboCssPropertyComputationPreserve, "0s"},
        {"animation-fill-mode", RoboCssPropertyComputationAnimationFillMode,
         "none"},
        {"animation-iteration-count",
         RoboCssPropertyComputationAnimationIterationCount, "1"},
        {"animation-name", RoboCssPropertyComputationPreserve, "none"},
        {"animation-play-state", RoboCssPropertyComputationAnimationPlayState,
         "running"},
        {"animation-timing-function", RoboCssPropertyComputationPreserve,
         "ease"}
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
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckEqInt(definition->computation, expected[index].computation);
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }

    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "animation");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheck(CssPropertyFlagsShorthand(definition->flags) != 0);
        }
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestAnimationCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.animations, 1);
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 31);
    RoboCheckStrEq(CssVersionString(), "0.31.0");

    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationAnimationDirection),
                      "animation-direction");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationAnimationIterationCount),
                      1);
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainAnimation),
                   "animation");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainAnimation), 1);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestAnimationDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: 2.5", &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationAnimationIterationCount,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationAnimationIterationCount,
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

void TestRegisterCssAnimation(void);

void TestRegisterCssAnimation(void)
{
    TestRegister("css.animation.direction_vocabulary",
                 TestAnimationDirectionVocabulary);
    TestRegister("css.animation.fill_vocabulary",
                 TestAnimationFillModeVocabulary);
    TestRegister("css.animation.play_state_vocabulary",
                 TestAnimationPlayStateVocabulary);
    TestRegister("css.animation.composition_vocabulary",
                 TestAnimationCompositionVocabulary);
    TestRegister("css.animation.computed", TestAnimationComputedShapes);
    TestRegister("css.animation.registry", TestAnimationRegistry);
    TestRegister("css.animation.capabilities", TestAnimationCapabilities);
    TestRegister("css.animation.determinism", TestAnimationDeterminism);
}
