/*
 * RoboCSS logical-property suite (Phase 18.12) — css.logical.*
 *
 * The logical side/axis vocabulary, the resolver that maps a role to a physical
 * `CssBoxSide` under a writing mode and direction, and the registered logical rows.
 *
 * The resolver is the substance: the block axis is the writing mode's, the inline
 * axis is the writing mode's turned by `direction`, and the two `sideways-*` modes
 * resolve exactly as their vertical counterparts because sideways turns the *text*
 * and not the axes.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSLogical.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabularies                                                    */
/* ------------------------------------------------------------------ */

static void TestLogicalSideVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssLogicalSideCount, 4);
    RoboCheckEqInt((int)CssLogicalSideCount(), 4);
    RoboCheckStrEq(CssLogicalSideName(RoboCssLogicalSideBlockStart),
                   "block-start");
    RoboCheckStrEq(CssLogicalSideName(RoboCssLogicalSideBlockEnd),
                   "block-end");
    RoboCheckStrEq(CssLogicalSideName(RoboCssLogicalSideInlineStart),
                   "inline-start");
    RoboCheckStrEq(CssLogicalSideName(RoboCssLogicalSideInlineEnd),
                   "inline-end");
    RoboCheckEqInt(CssLogicalSideFromText("inline-start"),
                   (int)RoboCssLogicalSideInlineStart);
    RoboCheckEqInt(CssLogicalSideFromText("BLOCK-END"),
                   (int)RoboCssLogicalSideBlockEnd);
    RoboCheckEqInt(CssLogicalSideFromText(NULL),
                   (int)RoboCssLogicalSideBlockStart);
    RoboCheckEqInt(CssLogicalSideIsKnown((CssLogicalSide)99), 0);
    RoboCheckEqInt(CssLogicalSideSliceIsKnown(CssSliceFromText("inline-end")),
                   1);
    RoboCheckEqInt(CssLogicalSideSliceIsKnown(CssSliceFromText("banana")), 0);

    /* The axis and the start/end half are two questions. */
    RoboCheckEqInt(CssLogicalSideAxis(RoboCssLogicalSideBlockStart),
                   (int)RoboCssLogicalAxisBlock);
    RoboCheckEqInt(CssLogicalSideAxis(RoboCssLogicalSideInlineEnd),
                   (int)RoboCssLogicalAxisInline);
    RoboCheckEqInt(CssLogicalSideAxis((CssLogicalSide)99),
                   (int)RoboCssLogicalAxisBlock);
    RoboCheckEqInt(CssLogicalSideIsStart(RoboCssLogicalSideBlockStart), 1);
    RoboCheckEqInt(CssLogicalSideIsStart(RoboCssLogicalSideBlockEnd), 0);
    RoboCheckEqInt(CssLogicalSideIsStart(RoboCssLogicalSideInlineStart), 1);
    RoboCheckEqInt(CssLogicalSideIsStart((CssLogicalSide)99), 0);

    RoboCheckEqInt((int)RoboCssLogicalAxisCount, 2);
    RoboCheckStrEq(CssLogicalAxisName(RoboCssLogicalAxisBlock), "block");
    RoboCheckStrEq(CssLogicalAxisName(RoboCssLogicalAxisInline), "inline");
    RoboCheckEqInt(CssLogicalAxisFromText("inline"),
                   (int)RoboCssLogicalAxisInline);
    RoboCheckEqInt(CssLogicalAxisSliceIsKnown(CssSliceFromText("block")), 1);
    RoboCheckEqInt(CssLogicalAxisSliceIsKnown(CssSliceFromText("banana")), 0);

    {
        char buffer[RoboCssLogicalSideTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssLogicalSideWrite(RoboCssLogicalSideInlineStart, buffer,
                                                 sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "inline-start");
        RoboCheckEqInt(CssLogicalSideWrite((CssLogicalSide)99, buffer,
                                                 sizeof(buffer), &length,
                                                 NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The resolver                                                        */
/* ------------------------------------------------------------------ */

static void TestLogicalResolver(void)
{
    CssFixture();

    /* horizontal-tb, ltr: block top→bottom, inline left→right. */
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockStart,
                                               RoboCssWritingModeHorizontalTb,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideTop);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockEnd,
                                               RoboCssWritingModeHorizontalTb,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideBottom);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideInlineStart,
                                               RoboCssWritingModeHorizontalTb,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideLeft);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideInlineEnd,
                                               RoboCssWritingModeHorizontalTb,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideRight);

    /* horizontal-tb, rtl: only the inline axis flips. */
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideInlineStart,
                                               RoboCssWritingModeHorizontalTb,
                                               RoboCssDirectionRtl),
                   (int)RoboCssBoxSideRight);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideInlineEnd,
                                               RoboCssWritingModeHorizontalTb,
                                               RoboCssDirectionRtl),
                   (int)RoboCssBoxSideLeft);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockStart,
                                               RoboCssWritingModeHorizontalTb,
                                               RoboCssDirectionRtl),
                   (int)RoboCssBoxSideTop);

    /* vertical-rl, ltr: block right→left, inline top→bottom. */
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockStart,
                                               RoboCssWritingModeVerticalRl,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideRight);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockEnd,
                                               RoboCssWritingModeVerticalRl,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideLeft);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideInlineStart,
                                               RoboCssWritingModeVerticalRl,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideTop);

    /* vertical-rl, rtl: the inline axis runs bottom→top. */
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideInlineStart,
                                               RoboCssWritingModeVerticalRl,
                                               RoboCssDirectionRtl),
                   (int)RoboCssBoxSideBottom);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideInlineEnd,
                                               RoboCssWritingModeVerticalRl,
                                               RoboCssDirectionRtl),
                   (int)RoboCssBoxSideTop);
    /* The block axis is unchanged by direction. */
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockStart,
                                               RoboCssWritingModeVerticalRl,
                                               RoboCssDirectionRtl),
                   (int)RoboCssBoxSideRight);

    /* vertical-lr, ltr: block left→right. */
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockStart,
                                               RoboCssWritingModeVerticalLr,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideLeft);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockEnd,
                                               RoboCssWritingModeVerticalLr,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideRight);

    /* A `sideways-*` mode resolves as its vertical counterpart: sideways turns the
     * *text*, not the axes. */
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockStart,
                                               RoboCssWritingModeSidewaysRl,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideRight);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockStart,
                                               RoboCssWritingModeSidewaysLr,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideLeft);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideInlineStart,
                                               RoboCssWritingModeSidewaysRl,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideTop);

    /* An unknown writing mode is read as horizontal-tb, the safe answer. */
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideBlockStart,
                                               (CssWritingMode)99,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideTop);
    RoboCheckEqInt(CssLogicalResolveSide(RoboCssLogicalSideInlineStart,
                                               (CssWritingMode)99,
                                               RoboCssDirectionLtr),
                   (int)RoboCssBoxSideLeft);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestLogicalRegistry(void)
{
    static const struct {
        const char *name;
        CssPropertyComputation computation;
        const char *shape;
    } expected[] = {
        {"margin-block-start", RoboCssPropertyComputationEdgeOffset, "0"},
        {"margin-block-end", RoboCssPropertyComputationEdgeOffset, "0"},
        {"margin-inline-start", RoboCssPropertyComputationEdgeOffset, "0"},
        {"margin-inline-end", RoboCssPropertyComputationEdgeOffset, "0"},
        {"padding-block-start", RoboCssPropertyComputationLength, "0"},
        {"padding-block-end", RoboCssPropertyComputationLength, "0"},
        {"padding-inline-start", RoboCssPropertyComputationLength, "0"},
        {"padding-inline-end", RoboCssPropertyComputationLength, "0"},
        {"inset-block-start", RoboCssPropertyComputationEdgeOffset, "auto"},
        {"inset-block-end", RoboCssPropertyComputationEdgeOffset, "auto"},
        {"inset-inline-start", RoboCssPropertyComputationEdgeOffset, "auto"},
        {"inset-inline-end", RoboCssPropertyComputationEdgeOffset, "auto"},
        {"block-size", RoboCssPropertyComputationLength, "auto"},
        {"inline-size", RoboCssPropertyComputationLength, "auto"},
        {"margin-block", RoboCssPropertyComputationPreserve, "0"},
        {"margin-inline", RoboCssPropertyComputationPreserve, "0"},
        {"padding-block", RoboCssPropertyComputationPreserve, "0"},
        {"padding-inline", RoboCssPropertyComputationPreserve, "0"}
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

    /* The four shorthands are marked as such. */
    {
        static const char *const shorthands[] = {
            "margin-block", "margin-inline", "padding-block", "padding-inline"
        };
        size_t i;

        for (i = 0u; i < sizeof(shorthands) / sizeof(shorthands[0]); i++) {
            const CssPropertyDefinition *definition =
                CssPropertyRegistryFind(registry, shorthands[i]);

            RoboCheck(definition != NULL);
            if (definition) {
                RoboCheck(CssPropertyFlagsShorthand(definition->flags) != 0);
            }
        }
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestLogicalCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.logicalProperties, 1);
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 35);
    RoboCheckStrEq(CssVersionString(), "0.35.0");
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainLogical), "logical");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainLogical), 1);
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void TestRegisterCssLogical(void);

void TestRegisterCssLogical(void)
{
    TestRegister("css.logical.side_vocabulary", TestLogicalSideVocabulary);
    TestRegister("css.logical.resolver", TestLogicalResolver);
    TestRegister("css.logical.registry", TestLogicalRegistry);
    TestRegister("css.logical.capabilities", TestLogicalCapabilities);
}
