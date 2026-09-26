/*
 * RoboCSS architecture suite (Phase 18.1) — css.architecture.*
 *
 * Phase 18 is the CSS engine's completion, and its first goal is the part of
 * the architecture 9.x deliberately left as a boundary: custom properties were
 * already first-class — stored as opaque token sequences, ordered by the
 * cascade, copied down by inheritance — and a `var()` reference was recognised,
 * carried intact and flagged unresolved, but nothing ever *substituted* it.
 * This suite is the contract for the half that now exists
 * (`RoboCSSVariable.h`), and it is written so that each case names the fact it
 * proves rather than restating the implementation.
 *
 * Four groups:
 *
 *   the capability    the two flags this phase flips, read field by field;
 *   the detection     `CssValueHasVarReference` over built values, including a
 *                     reference nested inside a `calc()`;
 *   the substitution  every resolution rule: a defined reference, a fallback, a
 *                     chain, a cycle, and the two shapes of "invalid at
 *                     computed-value time" — `unset` for a non-inherited
 *                     property (its initial value) and for an inherited one
 *                     (the parent's computed value);
 *   the boundary      a shorthand whose expansion would change under
 *                     substitution keeps its own entry and takes `unset`, which
 *                     is the pass-order limit this phase names rather than
 *                     hides.
 */

#include "RoboCssTestSupport.h"

#include "RoboCSS/RoboCSSVariable.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style through the real context door, asserting success. */
static void ArchComputeOf(CssInheritFixture *fixture,
                          CssElementRef ref,
                          CssComputedStyle *out)
{
    CssError error = RoboCssCleared;

    RoboCheckEqInt(CssStyleContextComputeStyle(CssInheritContext(fixture), ref,
                                               out, &error, NULL),
                   RoboOk);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
}

/**
 * Copy the first declaration's value in @p text into @p out.
 *
 * The value is parsed through the real declaration grammar and then copied,
 * because the declaration list owns the tree and frees it on dispose — a
 * borrowed pointer would be dangling the moment this helper returned. The
 * caller disposes @p out.
 */
static void ArchValueOf(const char *text, CssValue *out)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    const CssDeclaration *declaration;
    const CssValue *value;

    CssValueInit(out);
    RoboCheckEqInt(CssDeclarationListParseText(text, strlen(text),
                                               RoboCssOriginInline, &list,
                                               &error),
                   RoboOk);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    value = declaration ? CssDeclarationValue(declaration) : NULL;
    if (value) {
        RoboCheckEqInt(CssValueCopy(out, value, NULL), RoboOk);
    }
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* The capability                                                      */
/* ------------------------------------------------------------------ */

static void TestArchitectureCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();

    /* The struct is filled, not merged. */
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.customProperties, 1);
    RoboCheckEqInt(caps.variableSubstitution, 1);

    /* The two boundaries beside them are still 0 — a flag is a promise, and
     * these have not been kept. */
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    /* And a caller that zeroes the struct first gets a build that overwrites
     * both new flags rather than one that only happens to set them. */
    CssCapabilitiesInit(&caps);
    RoboCheckEqInt(caps.customProperties, 0);
    RoboCheckEqInt(caps.variableSubstitution, 0);
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.customProperties, 1);
    RoboCheckEqInt(caps.variableSubstitution, 1);
}

/* ------------------------------------------------------------------ */
/* Detection                                                           */
/* ------------------------------------------------------------------ */

/** Assert @p text's first declaration value is (or is not) a reference. */
static void ArchExpectReference(const char *text, int expected)
{
    CssValue value = RoboCssCleared;

    ArchValueOf(text, &value);
    RoboCheckEqInt(CssValueHasVarReference(&value), expected);
    CssValueDispose(&value);
}

static void TestArchitectureDetectReference(void)
{
    CssFixture();

    ArchExpectReference("a: var(--x)", 1);
    ArchExpectReference("a: red var(--x)", 1);
    /* Nested inside a function that is *not* the reference: a `calc()` with a
     * reference in it is still a reference, and a consumer that only looked at
     * the top-level node would substitute nothing. */
    ArchExpectReference("a: calc(1px + var(--x))", 1);
    /* A fallback that itself contains a reference. */
    ArchExpectReference("a: var(--x, var(--y))", 1);

    /* Not references: a `calc()` alone, a custom property's own name, and an
     * `env()` — which this phase carries and does not substitute. */
    ArchExpectReference("a: calc(1px + 2px)", 0);
    ArchExpectReference("a: 10px", 0);
    ArchExpectReference("a: --x", 0);
    ArchExpectReference("a: env(safe-area)", 0);
    RoboCheckEqInt(CssValueHasVarReference(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* Substitution: the resolution rules                                  */
/* ------------------------------------------------------------------ */

static void TestArchitectureVarDefined(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                    "div { --brand: #336699; color: var(--brand) }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    ArchComputeOf(&fixture, div, style);

    /* The reference is gone and the colour is the custom property's value. */
    CssComputedIs(style, "color", "#336699");
    CssComputedFlagsAre(style, "color", "declared");
    /* The custom property itself keeps its tokens: substitution reads a custom
     * property, it does not rewrite one. */
    CssComputedIs(style, "--brand", "#336699");
    CssComputedFlagsAre(style, "--brand", "declared+custom");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

static void TestArchitectureVarFallback(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                    "div { color: var(--missing, #112233) }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    ArchComputeOf(&fixture, div, style);

    /* An undefined reference with a fallback is *not* invalid: the fallback is
     * the value, and nothing is flagged because nothing is waiting. */
    CssComputedIs(style, "color", "#112233");
    CssComputedFlagsAre(style, "color", "declared");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

static void TestArchitectureVarChain(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                    "div { --a: var(--b); --b: #445566; color: var(--a) }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    ArchComputeOf(&fixture, div, style);

    /* A reference to a custom property that is itself a reference resolves to
     * the end of the chain, however long it is. */
    CssComputedIs(style, "color", "#445566");
    CssComputedFlagsAre(style, "color", "declared");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

static void TestArchitectureVarCycleFallback(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    /* A cycle, and a fallback written where it is *used*: the fallback is what
     * a cycle resolves to, because a cycle is answered as "no value here"
     * rather than as an error. */
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                    "div { --a: var(--b); --b: var(--a);"
                    " color: var(--a, #778899) }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    ArchComputeOf(&fixture, div, style);

    CssComputedIs(style, "color", "#778899");
    CssComputedFlagsAre(style, "color", "declared");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

static void TestArchitectureVarUndefinedNonInherited(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                    "div { width: var(--missing) }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    ArchComputeOf(&fixture, div, style);

    /* Invalid at computed-value time: the declaration takes no value, so the
     * property follows `unset` — and `width` does not inherit, so that is its
     * initial value. It is not INVALID, because the declaration was legal. */
    CssComputedIs(style, "width", "auto");
    CssComputedFlagsAre(style, "width", "declared+default");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

static void TestArchitectureVarUndefinedInherited(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parent = NULL;
    CssComputedStyle *child = NULL;
    CssElementRef div;
    CssElementRef paragraph;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    paragraph = CssHostAdd(CssInheritHost(&fixture), "p", NULL, NULL);
    CssHostSetParent(CssInheritHost(&fixture), paragraph, div);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                    "div { color: #123456 } p { color: var(--missing) }");
    RoboCheckEqInt(CssComputedStyleCreate(&parent, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&child, NULL), RoboOk);

    ArchComputeOf(&fixture, div, parent);
    ArchComputeOf(&fixture, paragraph, child);

    /* The same `unset` rule, on the other side: `color` *does* inherit, so the
     * declaration that produced nothing takes the parent's **computed** colour,
     * which is the intermediate `#123456` and not a recomputation of anything. */
    CssComputedIs(parent, "color", "#123456");
    CssComputedIs(child, "color", "#123456");
    CssComputedFlagsAre(child, "color", "declared+inherited");

    CssComputedStyleDestroy(child);
    CssComputedStyleDestroy(parent);
    CssInheritEnd(&fixture);
}

static void TestArchitectureVarInsideCalc(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef div;
    const CssComputedValue *value;
    char text[RoboCssComputedValueTextMax];
    size_t length = 0u;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                    "div { --w: 5px; width: calc(var(--w) + 10px) }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    ArchComputeOf(&fixture, div, style);

    /* The reference inside the `calc()` is substituted and the `calc()` is then
     * carried whole: substitution is not evaluation, and reducing the sum here
     * would be a lie because a `calc()` may still hold a percentage. */
    value = CssComputedStyleComputedByName(style, CssSliceFromText("width"));
    RoboCheck(value != NULL);
    if (value) {
        RoboCheckEqInt(value->kind, (int)RoboCssComputedValuePreserved);
        RoboCheckEqInt(value->unresolved, 1);
        text[0] = '\0';
        RoboCheckEqInt(CssComputedValueWrite(value, text, sizeof(text), &length,
                                            NULL),
                       RoboOk);
        /* `5px` is present because the reference was replaced; `var(--w)` is
         * present only if it was not. */
        RoboCheck(strstr(text, "5px") != NULL);
        RoboCheck(strstr(text, "var(--w)") == NULL);
    }
    CssComputedFlagsAre(style, "width", "declared+unresolved");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The stated boundary                                                 */
/* ------------------------------------------------------------------ */

static void TestArchitectureShorthandBoundary(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef element;

    CssFixture();
    CssInheritBegin(&fixture);
    element = CssHostAdd(CssInheritHost(&fixture), "div", "box", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    /* A shorthand whose substitution would change its *component count* is
     * expanded by the inheritance pass before substitution runs, so it keeps
     * its own single entry. */
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                    "#box { margin: var(--edges) }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    ArchComputeOf(&fixture, element, style);

    /* The four longhands never got an entry — nothing was guessed — and the
     * shorthand takes `unset` at computed-value time because the reference
     * produced nothing. That is the boundary stated in RoboCSSVariable.h. */
    CssComputedIs(style, "margin-top", "<absent>");
    CssComputedIs(style, "margin", "0");
    CssComputedFlagsAre(style, "margin", "declared+default");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

static void TestArchitectureDeterminism(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *first = NULL;
    CssComputedStyle *second = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", NULL, NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                    "div { --a: #abcdef; color: var(--a);"
                    " width: var(--missing, 12px); height: var(--gone) }");
    RoboCheckEqInt(CssComputedStyleCreate(&first, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&second, NULL), RoboOk);

    ArchComputeOf(&fixture, div, first);
    ArchComputeOf(&fixture, div, second);

    /* Two computations of one document agree, value for value and flag for
     * flag: replacement is a pure function of the cascade and the parent, so a
     * second pass cannot see something the first did not. */
    {
        static const char *const names[] = {"--a", "color", "width", "height"};
        size_t index;

        for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
            const CssComputedValue *a =
                CssComputedStyleComputedByName(first, CssSliceFromText(names[index]));
            const CssComputedValue *b =
                CssComputedStyleComputedByName(second, CssSliceFromText(names[index]));

            RoboCheck((a != NULL) == (b != NULL));
            if (a && b) {
                RoboCheckEqInt(CssComputedValueEquals(a, b), 1);
            }
            RoboCheckEqInt(CssComputedStyleFlags(first, CssSliceFromText(names[index])),
                           CssComputedStyleFlags(second, CssSliceFromText(names[index])));
        }
    }
    CssComputedIs(first, "color", "#abcdef");
    CssComputedIs(first, "width", "12px");
    CssComputedIs(first, "height", "auto");

    CssComputedStyleDestroy(second);
    CssComputedStyleDestroy(first);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterCssArchitecture(void);

void TestRegisterCssArchitecture(void)
{
    TestRegister("css.architecture.capabilities", TestArchitectureCapabilities);
    TestRegister("css.architecture.detect_reference",
                 TestArchitectureDetectReference);
    TestRegister("css.architecture.var_defined", TestArchitectureVarDefined);
    TestRegister("css.architecture.var_fallback", TestArchitectureVarFallback);
    TestRegister("css.architecture.var_chain", TestArchitectureVarChain);
    TestRegister("css.architecture.var_cycle_fallback",
                 TestArchitectureVarCycleFallback);
    TestRegister("css.architecture.var_undefined_noninherited",
                 TestArchitectureVarUndefinedNonInherited);
    TestRegister("css.architecture.var_undefined_inherited",
                 TestArchitectureVarUndefinedInherited);
    TestRegister("css.architecture.var_inside_calc",
                 TestArchitectureVarInsideCalc);
    TestRegister("css.architecture.shorthand_boundary",
                 TestArchitectureShorthandBoundary);
    TestRegister("css.architecture.determinism", TestArchitectureDeterminism);
}
