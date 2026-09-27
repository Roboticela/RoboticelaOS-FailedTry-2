/*
 * RoboCSS table suite (Phase 18.5) — css.table.*
 *
 * The five closed vocabularies the table family is, their classifications, the
 * shared closed-vocabulary computation body they are read through, and the registry
 * rows all of it is registered by.
 *
 * The property this suite is really about is that the family is *mostly inherited*.
 * A table's cells take the collapse, the spacing, the caption side and the
 * empty-cell rule from the table box rather than deciding them for themselves, and
 * exactly one property — `table-layout` — is the box's own. A build that made one
 * of the wrong rows non-inherited would lay every cell out by the rule an ancestor
 * wrote, which is a whole-table mistake that a per-value assertion would not see;
 * so the inheritance of every row is read here.
 *
 * Nothing here asserts a layout. A table value at this phase is a *value* with
 * classifications in it, and every expectation below is about the value, what it
 * classifies as, or where it came from. Where a cell lands belongs to the phase
 * with a box (19.6).
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSTable.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabularies                                                    */
/* ------------------------------------------------------------------ */

static void TestTableLayoutVocabulary(void)
{
    static const char *const keywords[] = { "auto", "fixed" };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssTableLayoutCount, 2);
    RoboCheckEqInt((int)CssTableLayoutCount(), 2);
    for (index = 0u; index < 2u; index++) {
        CssTableLayout layout = (CssTableLayout)index;

        RoboCheckEqInt(CssTableLayoutIsKnown(layout), 1);
        RoboCheckStrEq(CssTableLayoutName(layout), keywords[index]);
        RoboCheckEqInt(CssTableLayoutFromText(keywords[index]), (int)layout);
        RoboCheck(CssTableLayoutSliceIsKnown(CssSliceFromText(keywords[index])) == 1);
    }
    RoboCheckEqInt(CssTableLayoutIsKnown((CssTableLayout)99), 0);
    RoboCheckStrEq(CssTableLayoutName((CssTableLayout)99), "");
    /* Case-insensitive, as CSS keywords are. */
    RoboCheckEqInt(CssTableLayoutFromText("FIXED"), (int)RoboCssTableLayoutFixed);
    /* An absent slice and a word nothing defined answer the initial value, and the
     * slice door is what tells them apart from a genuine `auto`. */
    RoboCheckEqInt(CssTableLayoutFromText(NULL), (int)RoboCssTableLayoutAuto);
    RoboCheckEqInt(CssTableLayoutFromText("banana"), (int)RoboCssTableLayoutAuto);
    RoboCheckEqInt(CssTableLayoutSliceIsKnown(CssSliceFromText("banana")), 0);
    RoboCheckEqInt(CssTableLayoutSliceIsKnown(CssSliceEmpty()), 0);

    /* The classification: `auto` is content-driven, `fixed` is not. */
    RoboCheckEqInt(CssTableLayoutIsContentDriven(RoboCssTableLayoutAuto), 1);
    RoboCheckEqInt(CssTableLayoutIsContentDriven(RoboCssTableLayoutFixed), 0);
    RoboCheckEqInt(CssTableLayoutIsContentDriven((CssTableLayout)99), 0);

    {
        char buffer[RoboCssTableLayoutTextMax];
        size_t length = 0u;
        char tiny[4];

        RoboCheckEqInt(CssTableLayoutWrite(RoboCssTableLayoutFixed, buffer,
                                               sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "fixed");
        RoboCheckEqInt((int)length, 5);
        tiny[0] = 'x';
        RoboCheckEqInt(CssTableLayoutWrite(RoboCssTableLayoutFixed, tiny,
                                               sizeof(tiny), &length, NULL),
                          RoboNotSupported);
        RoboCheckStrEq(tiny, "");
        RoboCheckEqInt((int)length, 5);
        RoboCheckEqInt(CssTableLayoutWrite((CssTableLayout)99, buffer,
                                               sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestBorderCollapseVocabulary(void)
{
    static const char *const keywords[] = { "separate", "collapse" };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssBorderCollapseCount, 2);
    RoboCheckEqInt((int)CssBorderCollapseCount(), 2);
    for (index = 0u; index < 2u; index++) {
        CssBorderCollapse collapse = (CssBorderCollapse)index;

        RoboCheckEqInt(CssBorderCollapseIsKnown(collapse), 1);
        RoboCheckStrEq(CssBorderCollapseName(collapse), keywords[index]);
        RoboCheckEqInt(CssBorderCollapseFromText(keywords[index]), (int)collapse);
    }
    RoboCheckEqInt(CssBorderCollapseIsKnown((CssBorderCollapse)99), 0);
    RoboCheckStrEq(CssBorderCollapseName((CssBorderCollapse)99), "");
    RoboCheckEqInt(CssBorderCollapseFromText(NULL),
                      (int)RoboCssBorderCollapseSeparate);
    RoboCheckEqInt(CssBorderCollapseSliceIsKnown(CssSliceFromText("banana")), 0);

    /* `collapse` is the value that makes adjoining borders one; `separate` is not. */
    RoboCheckEqInt(CssBorderCollapseBordersAdjoin(RoboCssBorderCollapseSeparate), 0);
    RoboCheckEqInt(CssBorderCollapseBordersAdjoin(RoboCssBorderCollapseCollapse), 1);
    RoboCheckEqInt(CssBorderCollapseBordersAdjoin((CssBorderCollapse)99), 0);

    {
        char buffer[RoboCssBorderCollapseTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssBorderCollapseWrite(RoboCssBorderCollapseCollapse, buffer,
                                                  sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "collapse");
        RoboCheckEqInt(CssBorderCollapseWrite((CssBorderCollapse)99, buffer,
                                                  sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestCaptionSideVocabulary(void)
{
    static const char *const keywords[] = {
        "top", "bottom", "block-start", "block-end", "inline-start", "inline-end"
    };
    static const struct {
        int blockAxis;
        int inlineAxis;
    } axes[] = {
        {0, 0}, {0, 0}, {1, 0}, {1, 0}, {0, 1}, {0, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssCaptionSideCount, 6);
    RoboCheckEqInt((int)CssCaptionSideCount(), 6);
    for (index = 0u; index < 6u; index++) {
        CssCaptionSide side = (CssCaptionSide)index;

        RoboCheckEqInt(CssCaptionSideIsKnown(side), 1);
        RoboCheckStrEq(CssCaptionSideName(side), keywords[index]);
        RoboCheckEqInt(CssCaptionSideFromText(keywords[index]), (int)side);
        RoboCheck(CssCaptionSideSliceIsKnown(CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssCaptionSideIsBlockAxis(side), axes[index].blockAxis);
        RoboCheckEqInt(CssCaptionSideIsInlineAxis(side), axes[index].inlineAxis);
    }
    RoboCheckEqInt(CssCaptionSideIsKnown((CssCaptionSide)99), 0);
    RoboCheckEqInt(CssCaptionSideFromText(NULL), (int)RoboCssCaptionSideTop);
    RoboCheckEqInt(CssCaptionSideSliceIsKnown(CssSliceFromText("banana")), 0);
    /* The physical rows are neither axis: `top` is not \"block\", and the two
     * questions are asked separately rather than as one negation. */
    RoboCheckEqInt(CssCaptionSideIsBlockAxis(RoboCssCaptionSideTop), 0);
    RoboCheckEqInt(CssCaptionSideIsInlineAxis(RoboCssCaptionSideTop), 0);
    RoboCheckEqInt(CssCaptionSideIsBlockAxis((CssCaptionSide)99), 0);
    RoboCheckEqInt(CssCaptionSideIsInlineAxis((CssCaptionSide)99), 0);

    {
        char buffer[RoboCssCaptionSideTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssCaptionSideWrite(RoboCssCaptionSideInlineStart, buffer,
                                               sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "inline-start");
        RoboCheckEqInt((int)length, 12);
    }
}

static void TestEmptyCellsVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssEmptyCellsCount, 2);
    RoboCheckEqInt((int)CssEmptyCellsCount(), 2);
    RoboCheckStrEq(CssEmptyCellsName(RoboCssEmptyCellsShow), "show");
    RoboCheckStrEq(CssEmptyCellsName(RoboCssEmptyCellsHide), "hide");
    RoboCheckEqInt(CssEmptyCellsFromText("hide"), (int)RoboCssEmptyCellsHide);
    RoboCheckEqInt(CssEmptyCellsFromText(NULL), (int)RoboCssEmptyCellsShow);
    RoboCheckEqInt(CssEmptyCellsIsKnown((CssEmptyCells)99), 0);
    RoboCheckEqInt(CssEmptyCellsSliceIsKnown(CssSliceFromText("hide")), 1);
    RoboCheckEqInt(CssEmptyCellsSliceIsKnown(CssSliceFromText("banana")), 0);
    RoboCheckEqInt(CssEmptyCellsDrawsEmpty(RoboCssEmptyCellsShow), 1);
    RoboCheckEqInt(CssEmptyCellsDrawsEmpty(RoboCssEmptyCellsHide), 0);
    RoboCheckEqInt(CssEmptyCellsDrawsEmpty((CssEmptyCells)99), 0);
}

static void TestVisibilityVocabulary(void)
{
    static const char *const keywords[] = { "visible", "hidden", "collapse" };
    static const struct {
        int takesSpace;
        int paints;
    } expected[] = {
        {1, 1}, {1, 0}, {0, 0}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssVisibilityCount, 3);
    RoboCheckEqInt((int)CssVisibilityCount(), 3);
    for (index = 0u; index < 3u; index++) {
        CssVisibility visibility = (CssVisibility)index;

        RoboCheckEqInt(CssVisibilityIsKnown(visibility), 1);
        RoboCheckStrEq(CssVisibilityName(visibility), keywords[index]);
        RoboCheckEqInt(CssVisibilityFromText(keywords[index]), (int)visibility);
        RoboCheckEqInt(CssVisibilityTakesSpace(visibility),
                          expected[index].takesSpace);
        RoboCheckEqInt(CssVisibilityPaints(visibility), expected[index].paints);
    }
    /* `hidden` is the value that is easy to get wrong: it takes space and does not
     * paint, which is a different pair from `collapse`'s. */
    RoboCheckEqInt(CssVisibilityTakesSpace(RoboCssVisibilityHidden), 1);
    RoboCheckEqInt(CssVisibilityPaints(RoboCssVisibilityHidden), 0);
    /* An unknown value takes space (the safe direction) and does not paint. */
    RoboCheckEqInt(CssVisibilityTakesSpace((CssVisibility)99), 1);
    RoboCheckEqInt(CssVisibilityPaints((CssVisibility)99), 0);
    RoboCheckEqInt(CssVisibilityFromText(NULL), (int)RoboCssVisibilityVisible);
    RoboCheckEqInt(CssVisibilitySliceIsKnown(CssSliceFromText("banana")), 0);

    {
        char buffer[RoboCssVisibilityTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssVisibilityWrite(RoboCssVisibilityCollapse, buffer,
                                              sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "collapse");
    }
}

/* ------------------------------------------------------------------ */
/* The computed values                                                 */
/* ------------------------------------------------------------------ */

static void TestTableComputedShapes(void)
{
    CssFixture();

    /* Each vocabulary reads through the shared closed-vocabulary body, so a value
     * this build can read is a KEYWORD carrying the canonical spelling. */
    CssComputeEquals(RoboCssPropertyComputationTableLayout, "o: auto", "auto");
    CssComputeEquals(RoboCssPropertyComputationTableLayout, "o: fixed", "fixed");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationTableLayout,
                                        "o: fixed"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationBorderCollapse, "o: collapse",
                       "collapse");
    CssComputeEquals(RoboCssPropertyComputationCaptionSide, "o: block-start",
                       "block-start");
    CssComputeEquals(RoboCssPropertyComputationEmptyCells, "o: hide", "hide");
    CssComputeEquals(RoboCssPropertyComputationVisibility, "o: hidden",
                       "hidden");

    /* A reference is deferred -- legal CSS whose value is decided where the
     * reference is -- and it is carried rather than refused. */
    {
        uint32_t flags = 0u;

        CssComputeEquals(RoboCssPropertyComputationTableLayout, "o: var(--t)",
                           "var(--t)");
        CssComputeText(RoboCssPropertyComputationTableLayout, "o: var(--t)",
                         NULL, 0u, &flags);
        RoboCheckEqInt(flags, 1u);
    }

    /* The refusals: a word the vocabulary does not know, and something that is not
     * an identifier at all. Both refuse with their own sentence. */
    CssComputeEquals(RoboCssPropertyComputationTableLayout, "o: banana",
                       "not a table-layout value");
    /* A dimension is not an identifier at all, which the shared body says before
     * the vocabulary is even consulted. */
    CssComputeEquals(RoboCssPropertyComputationTableLayout, "o: 3px",
                       "not an identifier");
    /* Two identifiers are not *one* identifier, which is the other refusal. */
    CssComputeEquals(RoboCssPropertyComputationTableLayout, "o: auto fixed",
                       "not a single table-layout keyword");
    CssComputeEquals(RoboCssPropertyComputationVisibility, "o: banana",
                       "not a visibility value");

    /* `border-spacing` is *carried*, not read: the declaration survives intact and
     * the kind is PRESERVED rather than INVALID, which is what lets the phases that
     * have a table read it later. */
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: 4px 8px", "4px 8px");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPreserve,
                                        "o: 4px 8px"),
                      "preserved");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: 0", "0");
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestTableRegistry(void)
{
    static const struct {
        const char *name;
        const char *shape;
        int inherited;
    } expected[] = {
        {"border-collapse", "separate", 1},
        {"border-spacing", "0", 1},
        {"caption-side", "top", 1},
        {"empty-cells", "show", 1},
        {"table-layout", "auto", 0},
        {"visibility", "visible", 1}
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, expected[index].name);
        char shape[64];

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        /* The inherited rows are the family's point: five of six propagate from the
         * table box to its cells, and `table-layout` alone is the box's own. */
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition),
                          expected[index].inherited);
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }

    /* The five rows that take a vocabulary of the family, and the one that is
     * carried. The shape is read from the definition rather than inferred from the
     * name. */
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "table-layout");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              RoboCssPropertyComputationTableLayout);
        }
    }
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "border-spacing");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              RoboCssPropertyComputationPreserve);
        }
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestTableCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.tables, 1);
    /* What the flag does not claim: no default stylesheet and no loading. */
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 34);
    RoboCheckStrEq(CssVersionString(), "0.34.0");

    /* The shape names a diagnostic prints, and the domain the vocabulary reports
     * under. */
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationTableLayout),
                      "table-layout");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationBorderCollapse),
                      1);
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainTable), "table");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainTable), 1);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestTableDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: block-start", &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationCaptionSide,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationCaptionSide,
                          CssDeclarationValue(declaration), &second, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&first, &second), 1);

    {
        CssDeclarationList otherList = RoboCssCleared;
        CssError otherError = RoboCssCleared;
        CssComputedValue other = RoboCssCleared;

        RoboCheckEqInt(CssParseDeclarations("o: inline-end", &otherList,
                                                 &otherError), RoboOk);
        declaration = CssDeclarationListAt(&otherList, 0u);
        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationCaptionSide,
                              CssDeclarationValue(declaration), &other, NULL),
                          RoboOk);
        /* The two logical sides are different values, not two spellings of one. */
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

void TestRegisterCssTable(void);

void TestRegisterCssTable(void)
{
    TestRegister("css.table.layout_vocabulary", TestTableLayoutVocabulary);
    TestRegister("css.table.collapse_vocabulary", TestBorderCollapseVocabulary);
    TestRegister("css.table.caption_side_vocabulary", TestCaptionSideVocabulary);
    TestRegister("css.table.empty_cells_vocabulary", TestEmptyCellsVocabulary);
    TestRegister("css.table.visibility_vocabulary", TestVisibilityVocabulary);
    TestRegister("css.table.computed", TestTableComputedShapes);
    TestRegister("css.table.registry", TestTableRegistry);
    TestRegister("css.table.capabilities", TestTableCapabilities);
    TestRegister("css.table.determinism", TestTableDeterminism);
}
