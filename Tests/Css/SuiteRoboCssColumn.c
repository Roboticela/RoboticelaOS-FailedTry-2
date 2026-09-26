/*
 * RoboCSS multi-column suite (Phase 18.6) — css.column.*
 *
 * The two closed vocabularies the fragmentation container is, the count and width
 * shapes, the rule decoration reused from the border family, and the registry rows
 * all of it is registered by.
 *
 * The property this suite is really about is that the family is assembled from
 * shapes that already exist. `column-width` is the shared length grammar with the
 * keyword `auto`; `column-rule-width`, `column-rule-style` and `column-rule-color`
 * are the border family's line width, border style and colour shapes; and
 * `column-gap` was already a row. Only `column-count` (auto or a positive integer),
 * `column-span` and `column-fill` are new, so the registry case reads the reuse as
 * an equality between computation enumerators rather than trusting a name.
 *
 * Nothing here asserts a layout. A column value at this phase is a *value* with
 * classifications in it, and where the columns land belongs to the phase with a box.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSColumn.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabularies                                                    */
/* ------------------------------------------------------------------ */

static void TestColumnSpanVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssColumnSpanCount, 2);
    RoboCheckEqInt((int)CssColumnSpanCount(), 2);
    RoboCheckStrEq(CssColumnSpanName(RoboCssColumnSpanNone), "none");
    RoboCheckStrEq(CssColumnSpanName(RoboCssColumnSpanAll), "all");
    RoboCheckEqInt(CssColumnSpanFromText("all"), (int)RoboCssColumnSpanAll);
    RoboCheckEqInt(CssColumnSpanFromText("ALL"), (int)RoboCssColumnSpanAll);
    RoboCheckEqInt(CssColumnSpanFromText(NULL), (int)RoboCssColumnSpanNone);
    RoboCheckEqInt(CssColumnSpanFromText("banana"), (int)RoboCssColumnSpanNone);
    RoboCheckEqInt(CssColumnSpanIsKnown((CssColumnSpan)99), 0);
    RoboCheckEqInt(CssColumnSpanSliceIsKnown(CssSliceFromText("all")), 1);
    RoboCheckEqInt(CssColumnSpanSliceIsKnown(CssSliceFromText("banana")), 0);

    /* `all` takes the box out of the column flow; `none` does not. */
    RoboCheckEqInt(CssColumnSpanSpansAll(RoboCssColumnSpanNone), 0);
    RoboCheckEqInt(CssColumnSpanSpansAll(RoboCssColumnSpanAll), 1);
    RoboCheckEqInt(CssColumnSpanSpansAll((CssColumnSpan)99), 0);

    {
        char buffer[RoboCssColumnSpanTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssColumnSpanWrite(RoboCssColumnSpanAll, buffer,
                                              sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "all");
        RoboCheckEqInt(CssColumnSpanWrite((CssColumnSpan)99, buffer,
                                              sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestColumnFillVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssColumnFillCount, 2);
    RoboCheckEqInt((int)CssColumnFillCount(), 2);
    RoboCheckStrEq(CssColumnFillName(RoboCssColumnFillAuto), "auto");
    RoboCheckStrEq(CssColumnFillName(RoboCssColumnFillBalance), "balance");
    RoboCheckEqInt(CssColumnFillFromText("balance"),
                      (int)RoboCssColumnFillBalance);
    RoboCheckEqInt(CssColumnFillFromText(NULL), (int)RoboCssColumnFillAuto);
    RoboCheckEqInt(CssColumnFillIsKnown((CssColumnFill)99), 0);
    RoboCheckEqInt(CssColumnFillSliceIsKnown(CssSliceFromText("balance")), 1);
    RoboCheckEqInt(CssColumnFillSliceIsKnown(CssSliceFromText("banana")), 0);

    RoboCheckEqInt(CssColumnFillBalances(RoboCssColumnFillAuto), 0);
    RoboCheckEqInt(CssColumnFillBalances(RoboCssColumnFillBalance), 1);
    RoboCheckEqInt(CssColumnFillBalances((CssColumnFill)99), 0);

    {
        char buffer[RoboCssColumnFillTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssColumnFillWrite(RoboCssColumnFillBalance, buffer,
                                              sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "balance");
        /* A too-small buffer is refused with the length it needed. */
        RoboCheckEqInt(CssColumnFillWrite(RoboCssColumnFillBalance, buffer, 4u,
                                              &length, NULL),
                          RoboNotSupported);
        RoboCheckStrEq(buffer, "");
        RoboCheckEqInt((int)length, 7);
    }
}

/* ------------------------------------------------------------------ */
/* The computed values                                                 */
/* ------------------------------------------------------------------ */

static void TestColumnComputedShapes(void)
{
    CssFixture();

    /* `column-count`: `auto`, or a positive whole number. */
    CssComputeEquals(RoboCssPropertyComputationColumnCount, "o: auto", "auto");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColumnCount,
                                        "o: auto"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationColumnCount, "o: 3", "3");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColumnCount,
                                        "o: 3"),
                      "number");
    /* Zero and negative counts are not smaller counts: they are not counts. */
    CssComputeEquals(RoboCssPropertyComputationColumnCount, "o: 0",
                       "a column count must be at least one");
    CssComputeEquals(RoboCssPropertyComputationColumnCount, "o: -2",
                       "a column count must be at least one");
    CssComputeEquals(RoboCssPropertyComputationColumnCount, "o: 2.5",
                       "a column count must be a whole number");
    CssComputeEquals(RoboCssPropertyComputationColumnCount, "o: banana",
                       "not auto or a positive whole number");

    /* `column-width`: a length, or `auto` — the shared length grammar. */
    CssComputeEquals(RoboCssPropertyComputationColumnWidth, "o: auto", "auto");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColumnWidth,
                                        "o: auto"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationColumnWidth, "o: 10px", "10px");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColumnWidth,
                                        "o: 10px"),
                      "length");
    CssComputeEquals(RoboCssPropertyComputationColumnWidth, "o: 1in", "96px");
    CssComputeEquals(RoboCssPropertyComputationColumnWidth, "o: 25%", "25%");
    CssComputeEquals(RoboCssPropertyComputationColumnWidth, "o: banana",
                       "not a length or auto");

    /* The two closed vocabularies. */
    CssComputeEquals(RoboCssPropertyComputationColumnSpan, "o: all", "all");
    CssComputeEquals(RoboCssPropertyComputationColumnSpan, "o: none", "none");
    CssComputeEquals(RoboCssPropertyComputationColumnSpan, "o: banana",
                       "not a column-span value");
    CssComputeEquals(RoboCssPropertyComputationColumnFill, "o: balance",
                       "balance");
    CssComputeEquals(RoboCssPropertyComputationColumnFill, "o: banana",
                       "not a column-fill value");

    /* `column-rule-style` is the border style vocabulary and nothing else: the same
     * set, so a value the border family would refuse is refused here. */
    CssComputeEquals(RoboCssPropertyComputationBorderStyle, "o: dashed",
                       "dashed");
    CssComputeEquals(RoboCssPropertyComputationBorderStyle, "o: banana",
                       "not a border style value");
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestColumnRegistry(void)
{
    static const struct {
        const char *name;
        CssPropertyComputation computation;
        const char *shape;
    } expected[] = {
        {"column-count", RoboCssPropertyComputationColumnCount, "auto"},
        {"column-fill", RoboCssPropertyComputationColumnFill, "balance"},
        {"column-rule", RoboCssPropertyComputationPreserve, "medium,_,none,_,currentColor"},
        {"column-rule-color", RoboCssPropertyComputationColor, "currentcolor"},
        {"column-rule-style", RoboCssPropertyComputationBorderStyle, "none"},
        {"column-rule-width", RoboCssPropertyComputationLineWidth, "medium"},
        {"column-span", RoboCssPropertyComputationColumnSpan, "none"},
        {"column-width", RoboCssPropertyComputationColumnWidth, "auto"},
        {"columns", RoboCssPropertyComputationPreserve, "auto,_,auto"}
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
        /* A fragmentation container's count, width, span, fill and decoration are the
         * box's own: a child that inherited a count would split itself. */
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckEqInt(definition->computation, expected[index].computation);
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }

    /* The two shorthands are marked as such. */
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "column-rule");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheck(CssPropertyFlagsShorthand(definition->flags) != 0);
        }
    }
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "columns");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheck(CssPropertyFlagsShorthand(definition->flags) != 0);
        }
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestColumnCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.multiColumn, 1);
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 32);
    RoboCheckStrEq(CssVersionString(), "0.32.0");

    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationColumnCount),
                      "column-count");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationColumnFill),
                      1);
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainColumn), "column");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainColumn), 1);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestColumnDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: 3", &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationColumnCount,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationColumnCount,
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

void TestRegisterCssColumn(void);

void TestRegisterCssColumn(void)
{
    TestRegister("css.column.span_vocabulary", TestColumnSpanVocabulary);
    TestRegister("css.column.fill_vocabulary", TestColumnFillVocabulary);
    TestRegister("css.column.computed", TestColumnComputedShapes);
    TestRegister("css.column.registry", TestColumnRegistry);
    TestRegister("css.column.capabilities", TestColumnCapabilities);
    TestRegister("css.column.determinism", TestColumnDeterminism);
}
