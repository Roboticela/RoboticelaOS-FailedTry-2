/*
 * RoboLayout font-request suite (Phase 12.17) — layout.fontrequest.*
 *
 * The subject is the *resolved font request*: the CSS-side record that sits between a computed
 * style and a font selection. Six groups, in the order the phase is built:
 *
 *   initial values   16px, weight 400, stretch normal, upright, LTR, and the registry's own
 *                    `serif` initial family — one place, no second answer;
 *
 *   the family list  parsing a CSS list (commas, quotes, whitespace), the **keyword** test through
 *                    RoboFont's own vocabulary (`sans-serif` stays a generic, `Inter` stays a name),
 *                    empty entries, and the bounded overflow;
 *
 *   the adapter      five properties read from a real computed style, the four scalars delegated to
 *                    the measurer's resolver so the two records cannot disagree;
 *
 *   stretch          a keyword, a percentage, an out-of-range percentage and its absence all answer
 *                    the documented value;
 *
 *   equality         the field-for-field rule the selector's run-splitting walk depends on;
 *
 *   diagnostics      the one-line writer, complete or absent.
 *
 * Everything is built from a real `CssComputedStyle` through 9.8's makers, so a case asserts against
 * the computed-value model rather than against a hand-set struct.
 */

#include "RoboLayoutTestSupport.h"

#include "RoboFont/RoboFontStyle.h"
#include "RoboLayout/RoboLayoutFontRequest.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void LayoutFontRequestSetFamilyList(CssComputedStyle *style,
                                           const char *families)
{
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    RoboCheckEqInt(CssComputedValueMakeFamilyList(&value,
                                                  CssSliceFromText(families), NULL),
                   RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                            CssSliceFromText("font-family"), 0u,
                                            &specified, &value, NULL),
                   RoboOk);
}

static void LayoutFontRequestSetLength(CssComputedStyle *style,
                                       const char *property,
                                       double number,
                                       CssUnit unit)
{
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    RoboCheckEqInt(CssComputedValueMakeLength(&value, number, unit, 0, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                            CssSliceFromText(property), 0u,
                                            &specified, &value, NULL),
                   RoboOk);
}

static void LayoutFontRequestSetNumber(CssComputedStyle *style,
                                       const char *property,
                                       double number)
{
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    RoboCheckEqInt(CssComputedValueMakeNumber(&value, number, 1, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                            CssSliceFromText(property), 0u,
                                            &specified, &value, NULL),
                   RoboOk);
}

static void LayoutFontRequestSetKeyword(CssComputedStyle *style,
                                        const char *property,
                                        const char *keyword)
{
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    RoboCheckEqInt(CssComputedValueMakeKeyword(&value, CssSliceFromText(keyword),
                                               NULL),
                   RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                            CssSliceFromText(property), 0u,
                                            &specified, &value, NULL),
                   RoboOk);
}

/* ------------------------------------------------------------------ */
/* Initial values                                                      */
/* ------------------------------------------------------------------ */

static void TestFontRequestInit(void)
{
    LayoutFontRequest request;

    LayoutFontRequestInit(&request);
    RoboCheck(request.sizePx == RoboLayoutTextInitialFontSizePx);
    RoboCheckEqInt(request.weight, RoboLayoutTextWeightNormal);
    RoboCheckEqInt((int)request.stretch, (int)RoboFontStretchNormal);
    RoboCheckEqInt(request.italic, 0);
    RoboCheckEqInt((int)request.direction, (int)RoboLayoutTextDirectionLtr);
    RoboCheckEqInt(request.familyUnresolved, 0);

    /* The one initial family is the registry's own spelled initial: `serif`, as a generic. */
    RoboCheckEqInt((int)request.families.count, 1);
    RoboCheckEqInt((int)request.families.entries[0].kind,
                   (int)LayoutFontFamilyGeneric);
    RoboCheckEqInt((int)request.families.entries[0].generic,
                   (int)RoboFontGenericSerif);

    /* A NULL request is a no-op rather than a crash. */
    LayoutFontRequestInit(NULL);

    /* Resolving NULL yields the same initial record, not a failure. */
    RoboCheckEqInt(LayoutFontRequestResolve(NULL, &request), RoboOk);
    RoboCheckEqInt((int)request.families.count, 1);
    RoboCheck(request.sizePx == RoboLayoutTextInitialFontSizePx);
    RoboCheckEqInt(request.familyUnresolved, 1);
    RoboCheckEqInt(LayoutFontRequestResolve(NULL, NULL), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The family list                                                     */
/* ------------------------------------------------------------------ */

static void TestFontRequestFamilyParse(void)
{
    LayoutFontFamilyList list;

    /* A named, a named with a space, and a generic — author order preserved. */
    RoboCheckEqInt(LayoutFontFamilyListParse("UI Text, Inter, sans-serif", 0u, &list),
                   RoboOk);
    RoboCheckEqInt((int)list.count, 3);
    RoboCheckEqInt((int)list.entries[0].kind, (int)LayoutFontFamilyNamed);
    RoboCheckStrEq(list.entries[0].name, "UI Text");
    RoboCheckEqInt((int)list.entries[1].kind, (int)LayoutFontFamilyNamed);
    RoboCheckStrEq(list.entries[1].name, "Inter");
    RoboCheckEqInt((int)list.entries[2].kind, (int)LayoutFontFamilyGeneric);
    RoboCheckEqInt((int)list.entries[2].generic, (int)RoboFontGenericSansSerif);

    /* Quotes are stripped, and a quoted name keeps its internal space. */
    RoboCheckEqInt(LayoutFontFamilyListParse("\"Gill Sans\", monospace", 0u, &list),
                   RoboOk);
    RoboCheckEqInt((int)list.count, 2);
    RoboCheckEqInt((int)list.entries[0].kind, (int)LayoutFontFamilyNamed);
    RoboCheckStrEq(list.entries[0].name, "Gill Sans");
    RoboCheckEqInt((int)list.entries[1].generic, (int)RoboFontGenericMonospace);

    /* Every generic and ui-* keyword is classified, not flattened to a name. */
    RoboCheckEqInt(LayoutFontFamilyListParse(
                       "system-ui, ui-monospace, emoji, math, ui-rounded", 0u, &list),
                   RoboOk);
    RoboCheckEqInt((int)list.count, 5);
    RoboCheckEqInt((int)list.entries[0].generic, (int)RoboFontGenericSystemUi);
    RoboCheckEqInt((int)list.entries[1].generic, (int)RoboFontGenericUiMonospace);
    RoboCheckEqInt((int)list.entries[2].generic, (int)RoboFontGenericEmoji);
    RoboCheckEqInt((int)list.entries[3].generic, (int)RoboFontGenericMath);
    RoboCheckEqInt((int)list.entries[4].generic, (int)RoboFontGenericUiRounded);

    /* Case folds: `SANS-SERIF` is the same keyword. */
    RoboCheckEqInt(LayoutFontFamilyListParse("SANS-SERIF", 0u, &list), RoboOk);
    RoboCheckEqInt((int)list.count, 1);
    RoboCheckEqInt((int)list.entries[0].kind, (int)LayoutFontFamilyGeneric);
    RoboCheckEqInt((int)list.entries[0].generic, (int)RoboFontGenericSansSerif);

    /* Empty entries and stray whitespace are skipped rather than failing. */
    RoboCheckEqInt(LayoutFontFamilyListParse("  Inter ,, serif ", 0u, &list), RoboOk);
    RoboCheckEqInt((int)list.count, 2);
    RoboCheckStrEq(list.entries[0].name, "Inter");
    RoboCheckEqInt((int)list.entries[1].generic, (int)RoboFontGenericSerif);

    /* Empty and NULL text leave the list empty (not the initial list — that is the request's job). */
    RoboCheckEqInt(LayoutFontFamilyListParse("", 0u, &list), RoboOk);
    RoboCheckEqInt((int)list.count, 0);
    RoboCheckEqInt(LayoutFontFamilyListParse(NULL, 4u, &list), RoboOk);
    RoboCheckEqInt((int)list.count, 0);
    RoboCheckEqInt(LayoutFontFamilyListParse("serif", 0u, NULL), RoboInvalidArgument);

    /* The bounded overflow: past the cap the rest is dropped, and the call still succeeds. */
    {
        char many[512];
        size_t i;
        size_t cursor = 0u;

        many[0] = '\0';
        for (i = 0u; i < RoboLayoutFontMaxFamilies + 5u; i++) {
            cursor += (size_t)snprintf(many + cursor, sizeof(many) - cursor, "%sf%d",
                                       i == 0u ? "" : ", ", (int)i);
        }
        RoboCheckEqInt(LayoutFontFamilyListParse(many, cursor, &list), RoboOk);
        RoboCheckEqInt((int)list.count, (int)RoboLayoutFontMaxFamilies);
    }

    /* The append door refuses a full list rather than growing it. */
    {
        LayoutFontFamilyList full;
        LayoutFontFamily family;

        LayoutFontFamilyListInit(&full);
        LayoutFontFamilyInit(&family);
        full.count = RoboLayoutFontMaxFamilies;
        RoboCheckEqInt(LayoutFontFamilyListAppend(&full, &family), RoboAlreadyExists);
        RoboCheckEqInt(LayoutFontFamilyListAppend(NULL, &family), RoboInvalidArgument);
        RoboCheckEqInt(LayoutFontFamilyListAppend(&full, NULL), RoboInvalidArgument);
    }
}

static void TestFontRequestFamilyEqual(void)
{
    LayoutFontFamily a;
    LayoutFontFamily b;
    LayoutFontFamilyList la;
    LayoutFontFamilyList lb;

    LayoutFontFamilyInit(&a);
    LayoutFontFamilyInit(&b);
    RoboCheck(LayoutFontFamilyEqual(&a, &b));
    RoboCheck(LayoutFontFamilyEqual(NULL, NULL));
    RoboCheck(!LayoutFontFamilyEqual(&a, NULL));

    /* Case-insensitive name equality: `inter` and `Inter` are the same family. */
    RoboCheckEqInt(LayoutFontFamilyListParse("inter", 0u, &la), RoboOk);
    RoboCheckEqInt(LayoutFontFamilyListParse("Inter", 0u, &lb), RoboOk);
    RoboCheck(LayoutFontFamilyListEqual(&la, &lb));

    /* Order matters. */
    RoboCheckEqInt(LayoutFontFamilyListParse("A, B", 0u, &la), RoboOk);
    RoboCheckEqInt(LayoutFontFamilyListParse("B, A", 0u, &lb), RoboOk);
    RoboCheck(!LayoutFontFamilyListEqual(&la, &lb));

    /* A named and a generic with the same spelling are different entries. */
    LayoutFontFamilyInit(&a);
    a.kind = LayoutFontFamilyNamed;
    strcpy(a.name, "serif");
    LayoutFontFamilyInit(&b);
    b.kind = LayoutFontFamilyGeneric;
    b.generic = RoboFontGenericSerif;
    RoboCheck(!LayoutFontFamilyEqual(&a, &b));
    RoboCheckStrEq(LayoutFontFamilyText(&a), "serif");
    RoboCheckStrEq(LayoutFontFamilyText(&b), "serif");
    RoboCheckStrEq(LayoutFontFamilyText(NULL), "none");

    RoboCheck(LayoutFontFamilyListIsEmpty(NULL));
}

/* ------------------------------------------------------------------ */
/* The CSS adapter                                                     */
/* ------------------------------------------------------------------ */

static void TestFontRequestResolve(void)
{
    CssComputedStyle *style = NULL;
    LayoutFontRequest request;

    LayoutFixture();
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheck(style != NULL);

    /* An empty style: every property's initial value, the initial family included. */
    RoboCheckEqInt(LayoutFontRequestResolve(style, &request), RoboOk);
    RoboCheck(request.sizePx == RoboLayoutTextInitialFontSizePx);
    RoboCheckEqInt(request.weight, RoboLayoutTextWeightNormal);
    RoboCheckEqInt((int)request.stretch, (int)RoboFontStretchNormal);
    RoboCheckEqInt((int)request.direction, (int)RoboLayoutTextDirectionLtr);
    RoboCheckEqInt(request.familyUnresolved, 0);
    RoboCheckEqInt((int)request.families.count, 1);
    RoboCheckEqInt((int)request.families.entries[0].generic, (int)RoboFontGenericSerif);

    /* Every font property, from real computed values. */
    LayoutFontRequestSetLength(style, "font-size", 24.0, RoboCssUnitPx);
    LayoutFontRequestSetNumber(style, "font-weight", 600.0);
    LayoutFontRequestSetKeyword(style, "font-style", "italic");
    LayoutFontRequestSetKeyword(style, "direction", "rtl");
    LayoutFontRequestSetFamilyList(style, "UI Text, Inter, sans-serif");

    RoboCheckEqInt(LayoutFontRequestResolve(style, &request), RoboOk);
    RoboCheck(request.sizePx == 24.0);
    RoboCheckEqInt(request.weight, 600);
    RoboCheckEqInt(request.italic, 1);
    RoboCheckEqInt((int)request.direction, (int)RoboLayoutTextDirectionRtl);
    RoboCheckEqInt(request.familyUnresolved, 0);
    RoboCheckEqInt((int)request.families.count, 3);
    RoboCheckStrEq(request.families.entries[0].name, "UI Text");
    RoboCheckStrEq(request.families.entries[1].name, "Inter");
    RoboCheckEqInt((int)request.families.entries[2].generic,
                   (int)RoboFontGenericSansSerif);

    /* A keyword weight is the resolver's own answer (one implementation, not two). */
    LayoutFontRequestSetKeyword(style, "font-weight", "bold");
    RoboCheckEqInt(LayoutFontRequestResolve(style, &request), RoboOk);
    RoboCheckEqInt(request.weight, RoboLayoutTextWeightBold);

    /* An unusable family value is *told apart* from an absent one. */
    LayoutFontRequestSetKeyword(style, "font-family", "serif");
    RoboCheckEqInt(LayoutFontRequestResolve(style, &request), RoboOk);
    RoboCheckEqInt(request.familyUnresolved, 1);
    RoboCheckEqInt((int)request.families.count, 1);
    RoboCheckEqInt((int)request.families.entries[0].generic, (int)RoboFontGenericSerif);

    RoboCheckEqInt(LayoutFontRequestResolve(style, NULL), RoboInvalidArgument);
    CssComputedStyleDestroy(style);
}

/* ------------------------------------------------------------------ */
/* Stretch                                                             */
/* ------------------------------------------------------------------ */

static void TestFontRequestStretch(void)
{
    CssComputedStyle *style = NULL;
    LayoutFontRequest request;

    LayoutFixture();
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    /* Absent → normal. */
    RoboCheckEqInt(LayoutFontRequestResolve(style, &request), RoboOk);
    RoboCheckEqInt((int)request.stretch, (int)RoboFontStretchNormal);

    /* The nine keywords. */
    LayoutFontRequestSetKeyword(style, "font-stretch", "condensed");
    RoboCheckEqInt(LayoutFontRequestResolve(style, &request), RoboOk);
    RoboCheckEqInt((int)request.stretch, (int)RoboFontStretchCondensed);

    LayoutFontRequestSetKeyword(style, "font-stretch", "ultra-expanded");
    RoboCheckEqInt(LayoutFontRequestResolve(style, &request), RoboOk);
    RoboCheckEqInt((int)request.stretch, (int)RoboFontStretchUltraExpanded);

    /* A percentage in range. */
    {
        CssComputedValue value = RoboCssCleared;
        CssValue specified = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueMakePercentage(&value, 125.0, 1, NULL), RoboOk);
        RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                CssSliceFromText("font-stretch"), 0u,
                                                &specified, &value, NULL),
                       RoboOk);
    }
    RoboCheckEqInt(LayoutFontRequestResolve(style, &request), RoboOk);
    RoboCheckEqInt((int)request.stretch, (int)RoboFontStretchExpanded);

    /* An out-of-range percentage falls back to normal rather than to a nonsense stretch. */
    {
        CssComputedValue value = RoboCssCleared;
        CssValue specified = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueMakePercentage(&value, 900.0, 1, NULL), RoboOk);
        RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                CssSliceFromText("font-stretch"), 0u,
                                                &specified, &value, NULL),
                       RoboOk);
    }
    RoboCheckEqInt(LayoutFontRequestResolve(style, &request), RoboOk);
    RoboCheckEqInt((int)request.stretch, (int)RoboFontStretchNormal);

    CssComputedStyleDestroy(style);
}

/* ------------------------------------------------------------------ */
/* Equality                                                            */
/* ------------------------------------------------------------------ */

static void TestFontRequestEqual(void)
{
    LayoutFontRequest a;
    LayoutFontRequest b;

    LayoutFontRequestInit(&a);
    LayoutFontRequestInit(&b);
    RoboCheck(LayoutFontRequestEqual(&a, &b));
    RoboCheck(LayoutFontRequestEqual(NULL, NULL));
    RoboCheck(!LayoutFontRequestEqual(&a, NULL));

    b.sizePx = a.sizePx + 1.0;
    RoboCheck(!LayoutFontRequestEqual(&a, &b));
    b.sizePx = a.sizePx;

    b.weight = 700;
    RoboCheck(!LayoutFontRequestEqual(&a, &b));
    b.weight = a.weight;

    b.stretch = RoboFontStretchCondensed;
    RoboCheck(!LayoutFontRequestEqual(&a, &b));
    b.stretch = a.stretch;

    b.italic = 1;
    RoboCheck(!LayoutFontRequestEqual(&a, &b));
    b.italic = a.italic;

    b.direction = RoboLayoutTextDirectionRtl;
    RoboCheck(!LayoutFontRequestEqual(&a, &b));
    b.direction = a.direction;

    b.familyUnresolved = 1;
    RoboCheck(!LayoutFontRequestEqual(&a, &b));
    b.familyUnresolved = a.familyUnresolved;

    RoboCheckEqInt(LayoutFontFamilyListParse("Inter, sans-serif", 0u, &b.families),
                   RoboOk);
    RoboCheck(!LayoutFontRequestEqual(&a, &b));
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static void TestFontRequestWrite(void)
{
    LayoutFontRequest request;
    char buffer[256];
    size_t length = 0u;

    LayoutFontRequestInit(&request);
    RoboCheckEqInt(LayoutFontRequestWrite(&request, buffer, sizeof(buffer), &length),
                   RoboOk);
    RoboCheck(strstr(buffer, "families=serif") != NULL);
    RoboCheck(strstr(buffer, "weight=400") != NULL);
    RoboCheck(strstr(buffer, "ltr") != NULL);

    RoboCheckEqInt(LayoutFontFamilyListParse("UI Text, Inter, monospace", 0u,
                                             &request.families),
                   RoboOk);
    request.sizePx = 17.0;
    request.weight = 600;
    request.italic = 1;
    request.direction = RoboLayoutTextDirectionRtl;
    RoboCheckEqInt(LayoutFontRequestWrite(&request, buffer, sizeof(buffer), &length),
                   RoboOk);
    RoboCheck(strstr(buffer, "UI Text") != NULL);
    RoboCheck(strstr(buffer, "Inter") != NULL);
    RoboCheck(strstr(buffer, "monospace") != NULL);
    RoboCheck(strstr(buffer, "size=17.00") != NULL);
    RoboCheck(strstr(buffer, "weight=600") != NULL);
    RoboCheck(strstr(buffer, "italic") != NULL);
    RoboCheck(strstr(buffer, "rtl") != NULL);

    /* Complete or absent: a buffer too small reports it rather than truncating. */
    RoboCheckEqInt(LayoutFontRequestWrite(&request, buffer, 8u, &length),
                   RoboNotSupported);
    RoboCheckEqInt(LayoutFontRequestWrite(NULL, buffer, sizeof(buffer), &length),
                   RoboInvalidArgument);
    RoboCheckEqInt(LayoutFontRequestWrite(&request, NULL, sizeof(buffer), &length),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutFontRequest(void);
void TestRegisterLayoutFontRequest(void)
{
    TestRegister("layout.fontrequest.init", TestFontRequestInit);
    TestRegister("layout.fontrequest.family_parse", TestFontRequestFamilyParse);
    TestRegister("layout.fontrequest.family_equal", TestFontRequestFamilyEqual);
    TestRegister("layout.fontrequest.resolve", TestFontRequestResolve);
    TestRegister("layout.fontrequest.stretch", TestFontRequestStretch);
    TestRegister("layout.fontrequest.equal", TestFontRequestEqual);
    TestRegister("layout.fontrequest.write", TestFontRequestWrite);
}
