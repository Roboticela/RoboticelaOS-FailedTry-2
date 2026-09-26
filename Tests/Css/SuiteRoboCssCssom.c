/*
 * RoboCSS object-model suite (Phase 18.2) — css.cssom.*
 *
 * Phase 18 is the CSS engine's completion, and its second goal is the half of
 * CSS this module had never had: the *object model*. Every phase before it read
 * structure; this one turns structure back into text and lets a caller change
 * it. The suite is the contract for both halves:
 *
 *   serialization   `cssText` over component values, selector lists, rules and
 *                   whole stylesheets — including the property that matters, a
 *                   round trip through text that lands on the same model, and
 *                   the buffer discipline (measure first, then fit, and never
 *                   leave a partial rendering behind);
 *   the declaration a self-contained style declaration that owns its bytes, so
 *                   parse, read, set, replace, remove and render all work after
 *                   the text it was built from is gone;
 *   rule editing    `insertRule`/`deleteRule`, text in and a rule count out.
 */

#include "RoboCssTestSupport.h"

#include "RoboCSS/RoboCSSOM.h"
#include "RoboCSS/RoboCSSSelectorParser.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/** The canonical text of the first declaration's value in @p text. */
static void CssomValueText(const char *text, char *out, size_t capacity)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    size_t length = 0u;
    const CssDeclaration *declaration;

    RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    if (declaration) {
        RoboCheckEqInt(CssOMWriteValue(CssDeclarationValue(declaration), out,
                                       capacity, &length, NULL),
                       RoboOk);
    }
    CssDeclarationListDispose(&list);
}

/** Assert the selector list spelled by @p text serialises back to @p expected. */
static void CssomSelectorText(const char *text, const char *expected)
{
    CssSelectorList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    char buffer[128];
    size_t length = 0u;

    CssFixture();
    RoboCheckEqInt(CssSelectorListParseText(&list, text, &error, NULL), RoboOk);
    RoboCheckEqInt(CssOMWriteSelectorList(&list, buffer, sizeof buffer, &length,
                                          NULL),
                   RoboOk);
    RoboCheckStrEq(buffer, expected);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* The capability                                                      */
/* ------------------------------------------------------------------ */

static void TestCssomCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();

    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.cssom, 1);
    /* The flags beside it are unchanged, and the two boundaries are still 0. */
    RoboCheckEqInt(caps.customProperties, 1);
    RoboCheckEqInt(caps.variableSubstitution, 1);
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    /* A caller that zeroes first gets a build that overwrites the new flag. */
    CssCapabilitiesInit(&caps);
    RoboCheckEqInt(caps.cssom, 0);
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.cssom, 1);
}

/* ------------------------------------------------------------------ */
/* Value serialization                                                 */
/* ------------------------------------------------------------------ */

static void TestCssomValueText(void)
{
    char buffer[128];

    CssFixture();

    CssomValueText("a: 10px", buffer, sizeof buffer);
    RoboCheckStrEq(buffer, "10px");

    /* A list keeps the separators CSS needs: a space where the author wrote
     * one, and no space where they did not. */
    CssomValueText("a: 10px solid red", buffer, sizeof buffer);
    RoboCheckStrEq(buffer, "10px solid red");

    CssomValueText("a: rgb(1,2,3)", buffer, sizeof buffer);
    RoboCheckStrEq(buffer, "rgb(1, 2, 3)");

    CssomValueText("a: calc(1px + 2px)", buffer, sizeof buffer);
    RoboCheckStrEq(buffer, "calc(1px + 2px)");
}

static void TestCssomValueBufferDiscipline(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    const CssValue *value;
    char buffer[4];
    size_t length = 123u;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("a: 10px", &list, &error), RoboOk);
    value = CssDeclarationValue(CssDeclarationListAt(&list, 0u));

    /* Measure without a buffer: RoboOk, and the length it would need. */
    RoboCheckEqInt(CssOMWriteValue(value, NULL, 0u, &length, NULL), RoboOk);
    RoboCheckEqInt((int)length, 4);

    /* No room for the NUL terminator: refused, and the buffer is untouched. */
    strcpy(buffer, "ZZZ");
    length = 0u;
    RoboCheckEqInt(CssOMWriteValue(value, buffer, sizeof buffer, &length, NULL),
                   RoboNotSupported);
    RoboCheckEqInt((int)length, 4);
    RoboCheckStrEq(buffer, "ZZZ");

    /* One byte more: written and terminated. */
    {
        char exact[5];

        RoboCheckEqInt(CssOMWriteValue(value, exact, sizeof exact, &length,
                                       NULL),
                       RoboOk);
        RoboCheckStrEq(exact, "10px");
    }

    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Selector serialization                                              */
/* ------------------------------------------------------------------ */

static void TestCssomSelectorText(void)
{
    CssFixture();

    CssomSelectorText("form > input + label", "form > input + label");
    CssomSelectorText("h1, h2, h3", "h1, h2, h3");
    CssomSelectorText(".card", ".card");
    CssomSelectorText("#main", "#main");
    CssomSelectorText("*", "*");
    CssomSelectorText("input[type=\"text\"]", "input[type=\"text\"]");
    CssomSelectorText(":not(.x)", ":not(.x)");
    CssomSelectorText("a.b", "a.b");
    /* An escape the parser resolved is re-escaped on the way out, so the text
     * re-parses to the same selector rather than a different one. */
    CssomSelectorText(".a\\3a b", ".a\\3a b");
}

/* ------------------------------------------------------------------ */
/* Rule and stylesheet serialization                                   */
/* ------------------------------------------------------------------ */

static void TestCssomRuleText(void)
{
    CssStylesheetId sheet;
    CssError error = RoboCssCleared;
    char buffer[128];
    size_t length = 0u;

    CssFixture();
    sheet = CssParseSheet("p { color: red; width: 10px }", &error);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);

    RoboCheckEqInt(CssOMWriteRule(CssStylesheetRule(sheet, 0u), buffer,
                                  sizeof buffer, &length, NULL),
                   RoboOk);
    RoboCheckStrEq(buffer, "p { color: red; width: 10px; }");

    /* An empty block spells both braces rather than vanishing. */
    {
        CssStylesheetId empty = CssParseSheet("p { }", &error);

        RoboCheckEqInt(CssOMWriteRule(CssStylesheetRule(empty, 0u), buffer,
                                      sizeof buffer, &length, NULL),
                       RoboOk);
        RoboCheckStrEq(buffer, "p { }");
        CssStylesheetDestroy(empty, NULL);
    }
    CssStylesheetDestroy(sheet, NULL);
}

static void TestCssomStylesheetText(void)
{
    CssStylesheetId sheet;
    CssError error = RoboCssCleared;
    char buffer[256];
    size_t length = 0u;

    CssFixture();
    sheet = CssParseSheet("p { color: red }\n@media screen { p { color: blue } }",
                          &error);
    RoboCheckEqInt(CssOMWriteStylesheet(sheet, buffer, sizeof buffer, &length,
                                        NULL),
                   RoboOk);
    RoboCheckStrEq(buffer,
                   "p { color: red; }\n@media screen { p { color: blue; } }");

    /* A stale handle is an answer, not an empty sheet. */
    CssStylesheetDestroy(sheet, NULL);
    RoboCheckEqInt(CssOMWriteStylesheet(sheet, buffer, sizeof buffer, &length,
                                        NULL),
                   RoboNotFound);
}

static void TestCssomRoundTrip(void)
{
    CssStylesheetId first;
    CssStylesheetId second;
    CssError error = RoboCssCleared;
    char one[512];
    char two[512];
    size_t length = 0u;

    CssFixture();
    first = CssParseSheet(
        "p, h1 { color: red !important; margin: 0 auto }\n"
        "@media screen and (min-width: 30em) { a[href^=\"http\"] { color: blue } }",
        &error);

    RoboCheckEqInt(CssOMWriteStylesheet(first, one, sizeof one, &length, NULL),
                   RoboOk);
    /* The rendering is re-parseable: feeding it back yields the same text,
     * which is the round-trip property rather than a byte-for-byte match. */
    second = CssParseSheet(one, &error);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssOMWriteStylesheet(second, two, sizeof two, &length, NULL),
                   RoboOk);
    RoboCheckStrEq(two, one);

    CssStylesheetDestroy(second, NULL);
    CssStylesheetDestroy(first, NULL);
}

/* ------------------------------------------------------------------ */
/* The mutable style declaration                                       */
/* ------------------------------------------------------------------ */

static void TestCssomDeclarationParse(void)
{
    CssStyleDeclaration *declaration = NULL;
    CssError error = RoboCssCleared;
    char buffer[128];
    size_t length = 0u;

    CssFixture();
    RoboCheckEqInt(CssStyleDeclarationCreate(&declaration, NULL), RoboOk);
    RoboCheckEqInt(CssStyleDeclarationParseText(
                       declaration, "color: red; width: 10px !important",
                       strlen("color: red; width: 10px !important"), &error,
                       NULL),
                   RoboOk);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);

    RoboCheckEqInt((int)CssStyleDeclarationLength(declaration), 2);
    RoboCheck(CssSliceEqualsAscii(CssStyleDeclarationNameAt(declaration, 0u),
                                  "color"));
    RoboCheck(CssSliceEqualsAscii(CssStyleDeclarationNameAt(declaration, 1u),
                                  "width"));
    RoboCheck(CssSliceIsAbsent(CssStyleDeclarationNameAt(declaration, 5u)));

    RoboCheck(CssStyleDeclarationContains(declaration, "color"));
    RoboCheck(!CssStyleDeclarationContains(declaration, "height"));

    RoboCheckEqInt(CssStyleDeclarationGetPropertyValue(
                       declaration, "color", buffer, sizeof buffer, &length,
                       NULL),
                   RoboOk);
    RoboCheckStrEq(buffer, "red");
    RoboCheckEqInt(CssStyleDeclarationGetPropertyValue(
                       declaration, "width", buffer, sizeof buffer, &length,
                       NULL),
                   RoboOk);
    RoboCheckStrEq(buffer, "10px");

    RoboCheckEqInt(CssStyleDeclarationGetPropertyPriority(declaration, "color"),
                   0);
    RoboCheckEqInt(CssStyleDeclarationGetPropertyPriority(declaration, "width"),
                   1);

    RoboCheckEqInt(CssStyleDeclarationCssText(declaration, buffer,
                                              sizeof buffer, &length, NULL),
                   RoboOk);
    RoboCheckStrEq(buffer, "color: red; width: 10px !important;");

    CssStyleDeclarationDestroy(declaration);
}

static void TestCssomDeclarationSetAndReplace(void)
{
    CssStyleDeclaration *declaration = NULL;
    CssError error = RoboCssCleared;
    char buffer[64];
    size_t length = 0u;

    CssFixture();
    RoboCheckEqInt(CssStyleDeclarationCreate(&declaration, NULL), RoboOk);

    RoboCheckEqInt(CssStyleDeclarationSetProperty(declaration, "color", "red",
                                                  NULL, &error, NULL),
                   RoboOk);
    RoboCheckEqInt(CssStyleDeclarationSetProperty(declaration, "width", "10px",
                                                  NULL, &error, NULL),
                   RoboOk);
    RoboCheckEqInt((int)CssStyleDeclarationLength(declaration), 2);

    /* Setting a property again replaces it *in place*: the order is the one
     * the properties were first declared in, which is CSSOM's rule and the
     * reason `setProperty` is not an append. */
    RoboCheckEqInt(CssStyleDeclarationSetProperty(declaration, "color", "blue",
                                                  NULL, &error, NULL),
                   RoboOk);
    RoboCheckEqInt((int)CssStyleDeclarationLength(declaration), 2);
    RoboCheck(CssSliceEqualsAscii(CssStyleDeclarationNameAt(declaration, 0u),
                                  "color"));
    RoboCheckEqInt(CssStyleDeclarationGetPropertyValue(
                       declaration, "color", buffer, sizeof buffer, &length,
                       NULL),
                   RoboOk);
    RoboCheckStrEq(buffer, "blue");

    /* Names compare ASCII case-insensitively, as CSS property names do. */
    RoboCheck(CssStyleDeclarationContains(declaration, "COLOR"));

    /* A custom property is a property here as anywhere. */
    RoboCheckEqInt(CssStyleDeclarationSetProperty(declaration, "--brand",
                                                  "#336699", NULL, &error,
                                                  NULL),
                   RoboOk);
    RoboCheckEqInt(CssStyleDeclarationGetPropertyValue(
                       declaration, "--brand", buffer, sizeof buffer, &length,
                       NULL),
                   RoboOk);
    RoboCheckStrEq(buffer, "#336699");

    /* A priority that is neither empty nor `important` is refused, and an
     * unusable name is refused, rather than stored in a form nothing reads. */
    RoboCheckEqInt(CssStyleDeclarationSetProperty(declaration, "color", "red",
                                                  "urgent", &error, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(CssStyleDeclarationSetProperty(declaration, "a b", "red",
                                                  NULL, &error, NULL),
                   RoboInvalidArgument);

    /* A value that parses to nothing leaves the declaration alone. */
    RoboCheckEqInt((int)CssStyleDeclarationLength(declaration), 3);
    RoboCheckEqInt(CssStyleDeclarationSetProperty(declaration, "height", "   ",
                                                  NULL, &error, NULL),
                   RoboOk);
    RoboCheckEqInt((int)CssStyleDeclarationLength(declaration), 3);
    RoboCheck(!CssStyleDeclarationContains(declaration, "height"));

    CssStyleDeclarationDestroy(declaration);
}

static void TestCssomDeclarationRemove(void)
{
    CssStyleDeclaration *declaration = NULL;
    CssError error = RoboCssCleared;
    char removed[64];
    size_t length = 0u;

    CssFixture();
    RoboCheckEqInt(CssStyleDeclarationCreate(&declaration, NULL), RoboOk);
    RoboCheckEqInt(CssStyleDeclarationParseText(
                       declaration, "color: red; width: 10px",
                       strlen("color: red; width: 10px"), &error, NULL),
                   RoboOk);

    /* Removal returns the value it removed. */
    RoboCheckEqInt(CssStyleDeclarationRemoveProperty(
                       declaration, "color", removed, sizeof removed, &length,
                       NULL),
                   RoboOk);
    RoboCheckStrEq(removed, "red");
    RoboCheckEqInt((int)CssStyleDeclarationLength(declaration), 1);
    RoboCheck(!CssStyleDeclarationContains(declaration, "color"));

    /* A property that is not there is a fact, not a failure of the call. */
    RoboCheckEqInt(CssStyleDeclarationRemoveProperty(
                       declaration, "color", NULL, 0u, NULL, NULL),
                   RoboNotFound);

    /* Setting an empty value is CSSOM's removal, and removes nothing when
     * there was nothing to remove. */
    RoboCheckEqInt(CssStyleDeclarationSetProperty(declaration, "width", "",
                                                  NULL, &error, NULL),
                   RoboOk);
    RoboCheckEqInt((int)CssStyleDeclarationLength(declaration), 0);
    RoboCheckEqInt(CssStyleDeclarationSetProperty(declaration, "width", "",
                                                  NULL, &error, NULL),
                   RoboOk);

    CssStyleDeclarationDestroy(declaration);
}

static void TestCssomDeclarationVarIsSpecified(void)
{
    CssStyleDeclaration *declaration = NULL;
    CssError error = RoboCssCleared;
    char buffer[64];
    size_t length = 0u;

    CssFixture();
    RoboCheckEqInt(CssStyleDeclarationCreate(&declaration, NULL), RoboOk);
    RoboCheckEqInt(CssStyleDeclarationParseText(declaration,
                                                "color: var(--brand)",
                                                strlen("color: var(--brand)"),
                                                &error, NULL),
                   RoboOk);

    /* A CSSOM value is the *specified* one: substitution is 18.1's and happens
     * at computed-value time, so the reference is what comes back — exactly as
     * a browser's `style.getPropertyValue` answers. */
    RoboCheckEqInt(CssStyleDeclarationGetPropertyValue(
                       declaration, "color", buffer, sizeof buffer, &length,
                       NULL),
                   RoboOk);
    RoboCheckStrEq(buffer, "var(--brand)");

    CssStyleDeclarationDestroy(declaration);
}

static void TestCssomDeclarationCssTextBufferDiscipline(void)
{
    CssStyleDeclaration *declaration = NULL;
    char buffer[4];
    size_t length = 0u;

    CssFixture();
    RoboCheckEqInt(CssStyleDeclarationCreate(&declaration, NULL), RoboOk);
    RoboCheckEqInt(CssStyleDeclarationSetProperty(declaration, "color", "red",
                                                  NULL, NULL, NULL),
                   RoboOk);

    strcpy(buffer, "ZZZ");
    RoboCheckEqInt(CssStyleDeclarationCssText(declaration, buffer,
                                              sizeof buffer, &length, NULL),
                   RoboNotSupported);
    RoboCheckStrEq(buffer, "ZZZ");
    RoboCheckEqInt((int)length, 11);

    CssStyleDeclarationDestroy(declaration);
}

/* ------------------------------------------------------------------ */
/* Rule editing                                                        */
/* ------------------------------------------------------------------ */

static void TestCssomInsertAndDeleteRule(void)
{
    CssStylesheetId sheet;
    CssError error = RoboCssCleared;
    char buffer[256];
    size_t length = 0u;

    CssFixture();
    sheet = CssParseSheet("p { color: red }", &error);

    /* Insert at the front. */
    RoboCheckEqInt(CssOMStylesheetInsertRule(sheet, "div { color: blue }",
                                             strlen("div { color: blue }"), 0u,
                                             &error, NULL),
                   RoboOk);
    RoboCheckEqInt((int)CssStylesheetRuleCount(sheet), 2);
    RoboCheckEqInt(CssOMWriteStylesheet(sheet, buffer, sizeof buffer, &length,
                                        NULL),
                   RoboOk);
    RoboCheckStrEq(buffer, "div { color: blue; }\np { color: red; }");

    /* An index past the end appends — the `cssRules.length` idiom. */
    RoboCheckEqInt(CssOMStylesheetInsertRule(sheet, "span { color: green }",
                                             strlen("span { color: green }"),
                                             99u, &error, NULL),
                   RoboOk);
    RoboCheckEqInt((int)CssStylesheetRuleCount(sheet), 3);

    /* Text that is not exactly one rule is refused and changes nothing. */
    RoboCheckEqInt(CssOMStylesheetInsertRule(sheet, "a { } b { }",
                                             strlen("a { } b { }"), 0u, &error,
                                             NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt((int)CssStylesheetRuleCount(sheet), 3);

    /* Delete the middle rule. */
    RoboCheckEqInt(CssOMStylesheetDeleteRule(sheet, 1u, NULL), RoboOk);
    RoboCheckEqInt((int)CssStylesheetRuleCount(sheet), 2);
    RoboCheckEqInt(CssOMWriteStylesheet(sheet, buffer, sizeof buffer, &length,
                                        NULL),
                   RoboOk);
    RoboCheckStrEq(buffer, "div { color: blue; }\nspan { color: green; }");

    RoboCheckEqInt(CssOMStylesheetDeleteRule(sheet, 9u, NULL), RoboNotFound);

    CssStylesheetDestroy(sheet, NULL);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

static void TestCssomDeterminism(void)
{
    CssStylesheetId first;
    CssStylesheetId second;
    CssError error = RoboCssCleared;
    char one[256];
    char two[256];
    size_t length = 0u;

    CssFixture();
    first = CssParseSheet("h1, h2 { color: red } a[href] { color: blue }", &error);
    second = CssParseSheet("h1, h2 { color: red } a[href] { color: blue }",
                           &error);

    /* Two parses of one source render identically: serialization is a function
     * of the model, not of a parse's private state. */
    RoboCheckEqInt(CssOMWriteStylesheet(first, one, sizeof one, &length, NULL),
                   RoboOk);
    RoboCheckEqInt(CssOMWriteStylesheet(second, two, sizeof two, &length, NULL),
                   RoboOk);
    RoboCheckStrEq(two, one);

    CssStylesheetDestroy(second, NULL);
    CssStylesheetDestroy(first, NULL);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterCssCssom(void);

void TestRegisterCssCssom(void)
{
    TestRegister("css.cssom.capabilities", TestCssomCapabilities);
    TestRegister("css.cssom.value_text", TestCssomValueText);
    TestRegister("css.cssom.value_buffer_discipline",
                 TestCssomValueBufferDiscipline);
    TestRegister("css.cssom.selector_text", TestCssomSelectorText);
    TestRegister("css.cssom.rule_text", TestCssomRuleText);
    TestRegister("css.cssom.stylesheet_text", TestCssomStylesheetText);
    TestRegister("css.cssom.round_trip", TestCssomRoundTrip);
    TestRegister("css.cssom.declaration_parse", TestCssomDeclarationParse);
    TestRegister("css.cssom.declaration_set_and_replace",
                 TestCssomDeclarationSetAndReplace);
    TestRegister("css.cssom.declaration_remove", TestCssomDeclarationRemove);
    TestRegister("css.cssom.declaration_var_is_specified",
                 TestCssomDeclarationVarIsSpecified);
    TestRegister("css.cssom.declaration_css_text_buffer_discipline",
                 TestCssomDeclarationCssTextBufferDiscipline);
    TestRegister("css.cssom.insert_and_delete_rule",
                 TestCssomInsertAndDeleteRule);
    TestRegister("css.cssom.determinism", TestCssomDeterminism);
}
