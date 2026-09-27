/*
 * RoboCSS transition suite (Phase 18.8) — css.transition.*
 *
 * The `transition-behavior` closed set and the keyword half of the timing-function
 * grammar, plus the carried declarations the rest of the family is.
 *
 * The timing-function vocabulary is deliberately the *keyword* half: the property
 * also admits `cubic-bezier()`, `steps()` and `linear()`, which this phase carries
 * rather than represents. So the thing under test here is that a consumer can read a
 * keyword easing's discrete/continuous nature without parsing a function.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSTransition.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabularies                                                    */
/* ------------------------------------------------------------------ */

static void TestTimingFunctionVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssTimingFunctionKeywordCount, 7);
    RoboCheckEqInt((int)CssTimingFunctionCount(), 7);
    RoboCheckStrEq(CssTimingFunctionName(RoboCssTimingFunctionEase), "ease");
    RoboCheckStrEq(CssTimingFunctionName(RoboCssTimingFunctionLinear),
                   "linear");
    RoboCheckStrEq(CssTimingFunctionName(RoboCssTimingFunctionEaseIn),
                   "ease-in");
    RoboCheckStrEq(CssTimingFunctionName(RoboCssTimingFunctionEaseOut),
                   "ease-out");
    RoboCheckStrEq(CssTimingFunctionName(RoboCssTimingFunctionEaseInOut),
                   "ease-in-out");
    RoboCheckStrEq(CssTimingFunctionName(RoboCssTimingFunctionStepStart),
                   "step-start");
    RoboCheckStrEq(CssTimingFunctionName(RoboCssTimingFunctionStepEnd),
                   "step-end");
    RoboCheckEqInt(CssTimingFunctionFromText("ease-in-out"),
                   (int)RoboCssTimingFunctionEaseInOut);
    RoboCheckEqInt(CssTimingFunctionFromText("STEP-START"),
                   (int)RoboCssTimingFunctionStepStart);
    RoboCheckEqInt(CssTimingFunctionFromText(NULL),
                   (int)RoboCssTimingFunctionEase);
    RoboCheckEqInt(CssTimingFunctionFromText("cubic-bezier"),
                   (int)RoboCssTimingFunctionEase);
    RoboCheckEqInt(CssTimingFunctionIsKnown((CssTimingFunction)99), 0);
    RoboCheckEqInt(CssTimingFunctionSliceIsKnown(CssSliceFromText("linear")), 1);
    RoboCheckEqInt(CssTimingFunctionSliceIsKnown(CssSliceFromText("banana")), 0);

    /* The discrete-step column: exactly the two step keywords. */
    RoboCheckEqInt(CssTimingFunctionIsStep(RoboCssTimingFunctionEase), 0);
    RoboCheckEqInt(CssTimingFunctionIsStep(RoboCssTimingFunctionLinear), 0);
    RoboCheckEqInt(CssTimingFunctionIsStep(RoboCssTimingFunctionStepStart), 1);
    RoboCheckEqInt(CssTimingFunctionIsStep(RoboCssTimingFunctionStepEnd), 1);
    RoboCheckEqInt(CssTimingFunctionIsStep((CssTimingFunction)99), 0);

    {
        char buffer[RoboCssTimingFunctionTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTimingFunctionWrite(RoboCssTimingFunctionEaseInOut,
                                                    buffer, sizeof(buffer),
                                                    &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "ease-in-out");
        RoboCheckEqInt(CssTimingFunctionWrite((CssTimingFunction)99, buffer,
                                                    sizeof(buffer), &length,
                                                    NULL),
                          RoboInvalidArgument);
    }
}

static void TestTransitionBehaviorVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssTransitionBehaviorCount, 2);
    RoboCheckEqInt((int)CssTransitionBehaviorCount(), 2);
    RoboCheckStrEq(CssTransitionBehaviorName(RoboCssTransitionBehaviorNormal),
                   "normal");
    RoboCheckStrEq(
        CssTransitionBehaviorName(RoboCssTransitionBehaviorAllowDiscrete),
        "allow-discrete");
    RoboCheckEqInt(CssTransitionBehaviorFromText("allow-discrete"),
                   (int)RoboCssTransitionBehaviorAllowDiscrete);
    RoboCheckEqInt(CssTransitionBehaviorFromText("ALLOW-DISCRETE"),
                   (int)RoboCssTransitionBehaviorAllowDiscrete);
    RoboCheckEqInt(CssTransitionBehaviorFromText(NULL),
                   (int)RoboCssTransitionBehaviorNormal);
    RoboCheckEqInt(CssTransitionBehaviorFromText("banana"),
                   (int)RoboCssTransitionBehaviorNormal);
    RoboCheckEqInt(CssTransitionBehaviorIsKnown((CssTransitionBehavior)99), 0);
    RoboCheckEqInt(
        CssTransitionBehaviorSliceIsKnown(CssSliceFromText("normal")), 1);
    RoboCheckEqInt(
        CssTransitionBehaviorSliceIsKnown(CssSliceFromText("banana")), 0);

    RoboCheckEqInt(
        CssTransitionBehaviorAllowsDiscrete(RoboCssTransitionBehaviorNormal), 0);
    RoboCheckEqInt(CssTransitionBehaviorAllowsDiscrete(
                       RoboCssTransitionBehaviorAllowDiscrete),
                   1);
    RoboCheckEqInt(
        CssTransitionBehaviorAllowsDiscrete((CssTransitionBehavior)99), 0);

    {
        char buffer[RoboCssTransitionBehaviorTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTransitionBehaviorWrite(
                           RoboCssTransitionBehaviorAllowDiscrete, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "allow-discrete");
        RoboCheckEqInt(CssTransitionBehaviorWrite((CssTransitionBehavior)99,
                                                        buffer, sizeof(buffer),
                                                        &length, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The computed value                                                  */
/* ------------------------------------------------------------------ */

static void TestTransitionComputedShapes(void)
{
    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationTransitionBehavior,
                       "o: allow-discrete", "allow-discrete");
    CssComputeEquals(RoboCssPropertyComputationTransitionBehavior, "o: normal",
                       "normal");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationTransitionBehavior,
                                        "o: allow-discrete"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationTransitionBehavior,
                       "o: banana", "not a transition-behavior value");
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestTransitionRegistry(void)
{
    static const struct {
        const char *name;
        CssPropertyComputation computation;
        const char *shape;
    } expected[] = {
        /* The typed row. */
        {"transition-behavior", RoboCssPropertyComputationTransitionBehavior,
         "normal"},
        /* The carried grammar rows, with their own initials: `all`, two `0s`, and
         * the shorthand's concatenation. */
        {"transition-delay", RoboCssPropertyComputationPreserve, "0s"},
        {"transition-duration", RoboCssPropertyComputationPreserve, "0s"},
        {"transition-property", RoboCssPropertyComputationPreserve, "all"},
        {"transition-timing-function", RoboCssPropertyComputationPreserve,
         "ease"},
        {"transition", RoboCssPropertyComputationPreserve,
         "all,_,0s,_,ease,_,0s"}
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

    /* The shorthand is marked as such. */
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "transition");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheck(CssPropertyFlagsShorthand(definition->flags) != 0);
        }
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestTransitionCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.transitions, 1);
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 35);
    RoboCheckStrEq(CssVersionString(), "0.35.0");

    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationTransitionBehavior),
                      "transition-behavior");
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainTransition),
                   "transition");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainTransition), 1);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestTransitionDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: allow-discrete", &list, &error),
                   RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationTransitionBehavior,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationTransitionBehavior,
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

void TestRegisterCssTransition(void);

void TestRegisterCssTransition(void)
{
    TestRegister("css.transition.timing_vocabulary",
                 TestTimingFunctionVocabulary);
    TestRegister("css.transition.behavior_vocabulary",
                 TestTransitionBehaviorVocabulary);
    TestRegister("css.transition.computed", TestTransitionComputedShapes);
    TestRegister("css.transition.registry", TestTransitionRegistry);
    TestRegister("css.transition.capabilities", TestTransitionCapabilities);
    TestRegister("css.transition.determinism", TestTransitionDeterminism);
}
