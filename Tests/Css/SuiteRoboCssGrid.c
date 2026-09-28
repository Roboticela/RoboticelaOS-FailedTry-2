/*
 * RoboCSS grid suite (Phase 18.4) — css.grid.*
 *
 * The flow vocabulary and its reader, the gutter length family, the carried
 * declarations the rest of the family is, and the registry rows all of it is
 * registered by.
 *
 * The property this suite is really about is that `grid-auto-flow` is *two words*
 * for three of its four values. That is the first vocabulary in this build whose
 * rows are not all one identifier, so the reader half is where the assertions earn
 * their keep: `row dense`, `dense row` and `dense` are one value, `row column` is
 * not a value, and a single-identifier door would have got both wrong.
 *
 * Nothing here asserts a layout. A grid value at this phase is a *value* with
 * classifications in it — which axis auto-placement follows, whether it packs
 * densely, how large a gutter is — and every expectation below is about the value,
 * what it classifies as, or where it came from. Where the tracks land belongs to
 * the phase with a box.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSGrid.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* grid-auto-flow                                                      */
/* ------------------------------------------------------------------ */

static void TestGridAutoFlowVocabulary(void)
{
    static const char *const keywords[] = {
        "row", "column", "row dense", "column dense"
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssGridAutoFlowCount, 4);
    RoboCheckEqInt((int)CssGridAutoFlowCount(), 4);
    for (index = 0u; index < 4u; index++) {
        CssGridAutoFlow flow = (CssGridAutoFlow)index;

        RoboCheckEqInt(CssGridAutoFlowIsKnown(flow), 1);
        RoboCheckStrEq(CssGridAutoFlowName(flow), keywords[index]);
        RoboCheckEqInt(CssGridAutoFlowFromText(keywords[index]), (int)flow);
        RoboCheck(
            CssGridAutoFlowSliceIsKnown(CssSliceFromText(keywords[index])) == 1);
    }
    /* An unknown enumerator is nobody's value: NONE's equivalent here is 99. */
    RoboCheckEqInt(CssGridAutoFlowIsKnown((CssGridAutoFlow)99), 0);
    RoboCheckStrEq(CssGridAutoFlowName((CssGridAutoFlow)99), "");

    /* `dense` alone is `row dense` — the special case the vocabulary records, and
     * the one spelling that is not the name of a row. */
    RoboCheckEqInt(CssGridAutoFlowFromText("dense"),
                      (int)RoboCssGridAutoFlowRowDense);
    RoboCheckEqInt(CssGridAutoFlowSliceIsKnown(CssSliceFromText("dense")), 1);
    /* And it is *not* the answer for "not one": `denserow` answers the default,
     * which is exactly why the slice door exists. */
    RoboCheckEqInt(CssGridAutoFlowFromText("denserow"),
                      (int)RoboCssGridAutoFlowRow);
    RoboCheckEqInt(CssGridAutoFlowSliceIsKnown(CssSliceFromText("denserow")), 0);
}

static void TestGridAutoFlowParsing(void)
{
    CssFixture();
    /* Case-insensitive, because CSS keywords are. */
    RoboCheckEqInt(CssGridAutoFlowFromText("COLUMN"),
                      (int)RoboCssGridAutoFlowColumn);
    RoboCheckEqInt(CssGridAutoFlowFromText("Row Dense"),
                      (int)RoboCssGridAutoFlowRowDense);
    /* Absence answers the initial value, and the slice door is what tells it apart
     * from a genuine `row`. */
    RoboCheckEqInt(CssGridAutoFlowFromText(NULL), (int)RoboCssGridAutoFlowRow);
    RoboCheckEqInt(CssGridAutoFlowFromSlice(CssSliceEmpty()),
                      (int)RoboCssGridAutoFlowRow);
    RoboCheckEqInt(CssGridAutoFlowSliceIsKnown(CssSliceEmpty()), 0);
    /* `banana` answers the default too, and is not known. */
    RoboCheckEqInt(CssGridAutoFlowFromText("banana"),
                      (int)RoboCssGridAutoFlowRow);
    RoboCheckEqInt(CssGridAutoFlowSliceIsKnown(CssSliceFromText("banana")), 0);

    /* The axis-and-density door builds the same four values without text. */
    RoboCheckEqInt(CssGridAutoFlowFromParts(0, 0),
                      (int)RoboCssGridAutoFlowRow);
    RoboCheckEqInt(CssGridAutoFlowFromParts(1, 0),
                      (int)RoboCssGridAutoFlowColumn);
    RoboCheckEqInt(CssGridAutoFlowFromParts(0, 1),
                      (int)RoboCssGridAutoFlowRowDense);
    RoboCheckEqInt(CssGridAutoFlowFromParts(1, 1),
                      (int)RoboCssGridAutoFlowColumnDense);
}

static void TestGridAutoFlowClassification(void)
{
    CssFixture();
    /* Which axis auto-placement follows, and whether holes are backfilled — the two
     * columns of the rows, asked as doors rather than compared as spellings. */
    RoboCheckEqInt(CssGridAutoFlowIsColumn(RoboCssGridAutoFlowRow), 0);
    RoboCheckEqInt(CssGridAutoFlowIsColumn(RoboCssGridAutoFlowColumn), 1);
    RoboCheckEqInt(CssGridAutoFlowIsColumn(RoboCssGridAutoFlowRowDense), 0);
    RoboCheckEqInt(CssGridAutoFlowIsColumn(RoboCssGridAutoFlowColumnDense), 1);

    RoboCheckEqInt(CssGridAutoFlowIsDense(RoboCssGridAutoFlowRow), 0);
    RoboCheckEqInt(CssGridAutoFlowIsDense(RoboCssGridAutoFlowColumn), 0);
    RoboCheckEqInt(CssGridAutoFlowIsDense(RoboCssGridAutoFlowRowDense), 1);
    RoboCheckEqInt(CssGridAutoFlowIsDense(RoboCssGridAutoFlowColumnDense), 1);

    /* An unknown value claims neither axis: a value this build cannot read must not
     * say auto-placement follows columns. */
    RoboCheckEqInt(CssGridAutoFlowIsColumn((CssGridAutoFlow)99), 0);
    RoboCheckEqInt(CssGridAutoFlowIsDense((CssGridAutoFlow)99), 0);
}

static void TestGridAutoFlowSerialization(void)
{
    char buffer[RoboCssGridAutoFlowTextMax];
    size_t length = 0u;
    char tiny[4];

    CssFixture();
    RoboCheckEqInt(CssGridAutoFlowWrite(RoboCssGridAutoFlowColumnDense, buffer,
                                            sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "column dense");
    RoboCheckEqInt((int)length, 12);

    /* Too small: the buffer is left empty and the length reports what was needed,
     * so a caller learns rather than receiving half a keyword. */
    tiny[0] = 'x';
    RoboCheckEqInt(CssGridAutoFlowWrite(RoboCssGridAutoFlowColumnDense, tiny,
                                            sizeof(tiny), &length, NULL),
                      RoboNotSupported);
    RoboCheckStrEq(tiny, "");
    RoboCheckEqInt((int)length, 12);

    /* An unknown enumerator is refused rather than printed as something. */
    RoboCheckEqInt(CssGridAutoFlowWrite((CssGridAutoFlow)99, buffer,
                                            sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssGridAutoFlowWrite(RoboCssGridAutoFlowRow, NULL,
                                            sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Reading grid-auto-flow from a component value                       */
/* ------------------------------------------------------------------ */

/** What `grid-auto-flow: <text>` reads as a value, through the real grammar. */
static CssGridParse GridFlowOutcome(const char *text, CssGridAutoFlow *out)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    const CssDeclaration *declaration;
    CssGridParse outcome;

    RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    outcome = CssGridAutoFlowFromValue(CssDeclarationValue(declaration), out);
    CssDeclarationListDispose(&list);
    return outcome;
}

static void TestGridAutoFlowReader(void)
{
    CssGridAutoFlow flow = RoboCssGridAutoFlowRow;

    CssFixture();

    /* The one-word spellings, each read as itself. */
    RoboCheckEqInt(GridFlowOutcome("o: row", &flow), RoboCssGridParseFlow);
    RoboCheckEqInt((int)flow, (int)RoboCssGridAutoFlowRow);
    RoboCheckEqInt(GridFlowOutcome("o: column", &flow), RoboCssGridParseFlow);
    RoboCheckEqInt((int)flow, (int)RoboCssGridAutoFlowColumn);

    /* `dense` alone is `row dense`, and the two-word spellings are one value
     * whichever order they were written in — which is the whole reason the reader
     * works on the component value and not on one identifier. */
    RoboCheckEqInt(GridFlowOutcome("o: dense", &flow), RoboCssGridParseFlow);
    RoboCheckEqInt((int)flow, (int)RoboCssGridAutoFlowRowDense);
    RoboCheckEqInt(GridFlowOutcome("o: row dense", &flow),
                      RoboCssGridParseFlow);
    RoboCheckEqInt((int)flow, (int)RoboCssGridAutoFlowRowDense);
    RoboCheckEqInt(GridFlowOutcome("o: dense row", &flow),
                      RoboCssGridParseFlow);
    RoboCheckEqInt((int)flow, (int)RoboCssGridAutoFlowRowDense);
    RoboCheckEqInt(GridFlowOutcome("o: column dense", &flow),
                      RoboCssGridParseFlow);
    RoboCheckEqInt((int)flow, (int)RoboCssGridAutoFlowColumnDense);
    RoboCheckEqInt(GridFlowOutcome("o: dense column", &flow),
                      RoboCssGridParseFlow);
    RoboCheckEqInt((int)flow, (int)RoboCssGridAutoFlowColumnDense);

    /* A lone reference is deferred — legal CSS whose value is decided elsewhere —
     * and it is the one refusal that is not a mistake. */
    RoboCheckEqInt(GridFlowOutcome("o: var(--f)", &flow),
                      RoboCssGridParseDeferred);

    /* The refusals, each kept apart from the value. `row column` repeats the half
     * there is one of; `dense dense` repeats the other; a number and a keyword this
     * vocabulary does not know are simply not this value; and three components are
     * more than a grid-auto-flow can hold. */
    RoboCheckEqInt(GridFlowOutcome("o: row column", &flow),
                      RoboCssGridParseInvalid);
    RoboCheckEqInt(GridFlowOutcome("o: dense dense", &flow),
                      RoboCssGridParseInvalid);
    RoboCheckEqInt(GridFlowOutcome("o: banana", &flow),
                      RoboCssGridParseInvalid);
    RoboCheckEqInt(GridFlowOutcome("o: 3px", &flow), RoboCssGridParseInvalid);
    RoboCheckEqInt(GridFlowOutcome("o: row dense column", &flow),
                      RoboCssGridParseInvalid);
}

static void TestGridParseOutcome(void)
{
    CssFixture();
    RoboCheckEqInt(CssGridParseIsKnown(RoboCssGridParseFlow), 1);
    RoboCheckEqInt(CssGridParseIsKnown(RoboCssGridParseDeferred), 1);
    RoboCheckEqInt(CssGridParseIsKnown(RoboCssGridParseInvalid), 1);
    RoboCheckEqInt(CssGridParseIsKnown((CssGridParse)99), 0);
    RoboCheckStrEq(CssGridParseName(RoboCssGridParseInvalid), "invalid");
    RoboCheckStrEq(CssGridParseName(RoboCssGridParseFlow), "flow");
    RoboCheckStrEq(CssGridParseName(RoboCssGridParseDeferred), "deferred");
    RoboCheckStrEq(CssGridParseName((CssGridParse)99), "unknown");
}

/* ------------------------------------------------------------------ */
/* The computed values                                                 */
/* ------------------------------------------------------------------ */

static void TestGridAutoFlowShapes(void)
{
    CssFixture();

    /* Each spelling reads to its canonical value, and the two-word ones print with
     * their one canonical spacing however the author spaced them. */
    CssComputeEquals(RoboCssPropertyComputationGridAutoFlow, "o: row", "row");
    CssComputeEquals(RoboCssPropertyComputationGridAutoFlow, "o: column",
                       "column");
    CssComputeEquals(RoboCssPropertyComputationGridAutoFlow, "o: dense",
                       "row dense");
    CssComputeEquals(RoboCssPropertyComputationGridAutoFlow, "o: row dense",
                       "row dense");
    CssComputeEquals(RoboCssPropertyComputationGridAutoFlow, "o: dense  row",
                       "row dense");
    CssComputeEquals(RoboCssPropertyComputationGridAutoFlow, "o: column dense",
                       "column dense");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationGridAutoFlow,
                                        "o: column dense"),
                      "grid-auto-flow");

    /* A value this build can read is *not* flagged unresolved; a reference is, and
     * it carries its tokens. */
    {
        uint32_t flags = 0u;

        CssComputeText(RoboCssPropertyComputationGridAutoFlow, "o: row dense",
                         NULL, 0u, &flags);
        RoboCheckEqInt(flags, 0u);
        CssComputeEquals(RoboCssPropertyComputationGridAutoFlow,
                           "o: var(--f)", "var(--f)");
        CssComputeText(RoboCssPropertyComputationGridAutoFlow, "o: var(--f)",
                         NULL, 0u, &flags);
        RoboCheckEqInt(flags, 1u);
    }

    /* The refusals, each with its own sentence: a repeated half, a word the
     * vocabulary does not have, and something that is not two keywords at all. */
    CssComputeEquals(RoboCssPropertyComputationGridAutoFlow, "o: row column",
                       "not a grid-auto-flow value");
    CssComputeEquals(RoboCssPropertyComputationGridAutoFlow, "o: banana",
                       "not a grid-auto-flow value");
    CssComputeEquals(RoboCssPropertyComputationGridAutoFlow, "o: 3px",
                       "not a grid-auto-flow value");
}

static void TestGridGapShapes(void)
{
    CssFixture();

    /* A gutter is a length, a percentage or the keyword `normal`, and `normal` is a
     * keyword rather than zero — resolving it would lay every authored grid out with
     * no gaps at all. */
    CssComputeEquals(RoboCssPropertyComputationGridGap, "o: normal", "normal");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationGridGap, "o: normal"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationGridGap, "o: 10px", "10px");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationGridGap, "o: 10px"),
                      "length");
    CssComputeEquals(RoboCssPropertyComputationGridGap, "o: 2em", "2em");
    CssComputeEquals(RoboCssPropertyComputationGridGap, "o: 25%", "25%");
    /* An absolute unit arrives as its reference pixel, exactly as for every other
     * length-shaped property — that is the point of sharing one grammar. */
    CssComputeEquals(RoboCssPropertyComputationGridGap, "o: 1in", "96px");
    CssComputeEquals(RoboCssPropertyComputationGridGap, "o: 0", "0px");

    /* The refusals name the family rather than the layer. */
    CssComputeEquals(RoboCssPropertyComputationGridGap, "o: auto",
                       "not a length, a percentage or normal");
    CssComputeEquals(RoboCssPropertyComputationGridGap, "o: banana",
                       "not a length, a percentage or normal");
}

static void TestGridCarriedDeclarations(void)
{
    CssFixture();

    /* The track templates and line placements are carried, not read: their value is
     * the author's declaration, structurally intact, and the kind is PRESERVED
     * rather than INVALID. That is what lets a document's grid declarations survive
     * to the phase that has a track grammar. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPreserve,
                                        "o: 1fr 2fr"),
                      "preserved");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: 1fr 2fr",
                       "1fr 2fr");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: 1 / span 2",
                       "1 / span 2");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: auto",
                       "auto");
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestGridRegistry(void)
{
    static const struct {
        const char *name;
        const char *shape;
    } expected[] = {
        {"column-gap", "normal"},
        {"gap", "normal"},
        {"grid", "none"},
        {"grid-area", "auto"},
        {"grid-auto-columns", "auto"},
        {"grid-auto-flow", "row"},
        {"grid-auto-rows", "auto"},
        {"grid-column", "auto"},
        {"grid-column-end", "auto"},
        {"grid-column-gap", "normal"},
        {"grid-column-start", "auto"},
        {"grid-row", "auto"},
        {"grid-row-end", "auto"},
        {"grid-row-gap", "normal"},
        {"grid-row-start", "auto"},
        {"grid-template", "none"},
        {"grid-template-areas", "none"},
        {"grid-template-columns", "none"},
        {"grid-template-rows", "none"},
        {"row-gap", "normal"}
    };
    /* The two that take a shape of the family rather than being carried, and the
     * six that are shorthands over the rest. Both are decisions, so both are read
     * here rather than inferred from a name. */
    static const char *const typed[] = {
        "column-gap", "grid-auto-flow", "grid-column-gap", "grid-row-gap",
        "row-gap"
    };
    static const char *const shorthands[] = {
        "gap", "grid", "grid-area", "grid-column", "grid-row", "grid-template"
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
        /* None of the twenty inherits: a grid container's flow, template,
         * placements and gutters are the box's own, and a child that inherited a
         * template would place itself into its parent's grid. */
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }

    /* The five that take the family's own shapes. */
    for (index = 0u; index < sizeof(typed) / sizeof(typed[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, typed[index]);

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(
                definition->computation ==
                        RoboCssPropertyComputationGridAutoFlow ||
                    definition->computation ==
                        RoboCssPropertyComputationGridGap,
                1);
        }
    }
    /* And the six that are carried declarations, marked as shorthands because that
     * is what they are over the rest of the family. */
    for (index = 0u; index < sizeof(shorthands) / sizeof(shorthands[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, shorthands[index]);

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              RoboCssPropertyComputationPreserve);
            RoboCheck(CssPropertyFlagsShorthand(definition->flags) != 0);
        }
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestGridCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.grid, 1);
    /* What the flag does not claim: no default stylesheet and no loading. */
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 40);
    RoboCheckStrEq(CssVersionString(), "0.40.0");

    RoboCheckEqInt((int)RoboCssComputedValueKindCount, 20);
    RoboCheckStrEq(CssComputedValueKindName(RoboCssComputedValueGridAutoFlow),
                      "grid-auto-flow");
    RoboCheckEqInt(CssComputedValueKindIsKnown(
                          RoboCssComputedValueGridAutoFlow),
                      1);

    /* The shape names a diagnostic prints. */
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationGridAutoFlow),
                      "grid-auto-flow");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationGridGap),
                      "grid-gap");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationGridGap),
                      1);

    /* The diagnostic domain the vocabulary reports under. */
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainGrid), "grid");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainGrid), 1);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestGridDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: dense row", &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationGridAutoFlow,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationGridAutoFlow,
                          CssDeclarationValue(declaration), &second, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&first, &second), 1);
    /* And the accessor answers the value, not a spelling: `dense row` is `row
     * dense`. */
    RoboCheck(CssComputedValueGridAutoFlow(&first) ==
              RoboCssGridAutoFlowRowDense);
    /* A cleared record answers the initial flow, which is what makes the accessor
     * safe to read without the kind check doing damage. */
    {
        CssComputedValue cleared = RoboCssCleared;

        RoboCheck(CssComputedValueGridAutoFlow(&cleared) ==
                  RoboCssGridAutoFlowRow);
    }

    {
        CssDeclarationList otherList = RoboCssCleared;
        CssError otherError = RoboCssCleared;
        CssComputedValue other = RoboCssCleared;

        RoboCheckEqInt(CssParseDeclarations("o: column", &otherList,
                                                 &otherError), RoboOk);
        declaration = CssDeclarationListAt(&otherList, 0u);
        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationGridAutoFlow,
                              CssDeclarationValue(declaration), &other, NULL),
                          RoboOk);
        /* `column` and `row dense` are still different values: equality compares
         * the enumerator and not the classification. */
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

void TestRegisterCssGrid(void);

void TestRegisterCssGrid(void)
{
    TestRegister("css.grid.flow_vocabulary", TestGridAutoFlowVocabulary);
    TestRegister("css.grid.flow_parsing", TestGridAutoFlowParsing);
    TestRegister("css.grid.flow_classification", TestGridAutoFlowClassification);
    TestRegister("css.grid.flow_serialization", TestGridAutoFlowSerialization);
    TestRegister("css.grid.flow_reader", TestGridAutoFlowReader);
    TestRegister("css.grid.parse_outcome", TestGridParseOutcome);
    TestRegister("css.grid.computed.auto_flow", TestGridAutoFlowShapes);
    TestRegister("css.grid.computed.gap", TestGridGapShapes);
    TestRegister("css.grid.computed.carried", TestGridCarriedDeclarations);
    TestRegister("css.grid.registry", TestGridRegistry);
    TestRegister("css.grid.capabilities", TestGridCapabilities);
    TestRegister("css.grid.determinism", TestGridDeterminism);
}
