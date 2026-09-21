/*
 * RoboCSS parser: declarations (Phase 9.3).
 *
 * The declaration is where the grammar stops being about shape and starts being
 * about *content*: `color: red` is a name, a colon, a value and — sometimes — an
 * importance flag, and every one of those four has a way of being subtly wrong
 * that a rule-level test cannot see.
 *
 * Three properties are asserted over and over here, so they are worth stating
 * once:
 *
 *   - **A name is preserved as written and resolved separately.** `COLOR` stays
 *     `COLOR` on the record; property *identity* is the table's business, and a
 *     build with no property families registered resolves nothing — which is a
 *     result, not a failure.
 *   - **A value is a tree, and trivia at its ends is not part of it.** The
 *     interior whitespace is kept (a descendant combinator depends on it) while
 *     `color:  red  ` and `color:red` produce the same value.
 *   - **A malformed declaration is a record, not a crash and not a silence.**
 *     It appears in the list with state INVALID, it records why, and the valid
 *     declarations around it are still parsed.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

static void TestCssParseDeclarationNames(void)
{
    CssError error = RoboCssCleared;
    CssDeclarationList list = RoboCssCleared;
    char name[64];

    /* Hyphenated, custom-property and multi-hyphen names all survive as
     * written: the tokenizer produced one IDENT for each, and the grammar
     * neither split it nor renamed it. */
    RoboCheckEqInt(CssParseDeclarations(
                          "font-size: 12px; --main-color: #06c; -webkit-box: none;"
                          " background-color: white;",
                          &list, &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 4u);

    CssDeclarationNameOf(&list, 0u, name, sizeof(name));
    RoboCheckStrEq(name, "font-size");
    CssDeclarationNameOf(&list, 1u, name, sizeof(name));
    RoboCheckStrEq(name, "--main-color");
    CssDeclarationNameOf(&list, 2u, name, sizeof(name));
    RoboCheckStrEq(name, "-webkit-box");
    CssDeclarationNameOf(&list, 3u, name, sizeof(name));
    RoboCheckStrEq(name, "background-color");

    /* Nothing was registered in the property table this build carries, so every
     * one of them resolves to NONE. A parser that claimed an identity here
     * would be inventing a property database 9.16 has not built yet. */
    RoboCheckEqInt(CssDeclarationListAt(&list, 0u)->property,
                      RoboCssPropertyIdNone);
    RoboCheckEqInt(CssDeclarationListAt(&list, 1u)->property,
                      RoboCssPropertyIdNone);

    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    CssDeclarationListDispose(&list);
}

static void TestCssParseDeclarationNameCase(void)
{
    CssError error = RoboCssCleared;
    CssDeclarationList list = RoboCssCleared;
    char name[64];

    /* The record keeps the author's spelling; case-insensitivity is a property
     * of *lookup* (and lives in the property table, which is case-insensitive).
     * Folding here would make `--Main-Color` collide with `--main-color`, and a
     * custom property is case-*sensitive* by specification. */
    RoboCheckEqInt(CssParseDeclarations("COLOR: red; --Main-Color: blue;",
                                             &list, &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 2u);

    CssDeclarationNameOf(&list, 0u, name, sizeof(name));
    RoboCheckStrEq(name, "COLOR");
    CssDeclarationNameOf(&list, 1u, name, sizeof(name));
    RoboCheckStrEq(name, "--Main-Color");

    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

static void TestCssParseDeclarationValueShapes(void)
{
    static const struct {
        const char *text;
        const char *shape;
    } cases[] = {
        { "color: red;", "red" },
        { "font-family: \"Roboto\";", "\"Roboto\"" },
        { "z-index: 10;", "10" },
        { "width: 50%;", "50%" },
        { "margin: 10px 20px;", "10px,_,20px" },
        { "color: #06c;", "#06c" },
        { "width: calc(100% - 20px);", "calc(100%,_,-,_,20px)" },
        { "transform: translate(calc(10px + 5px), 20px);",
          "translate(calc(10px,_,+,_,5px),,,_,20px)" },
        { "font-family: \"Roboto\", sans-serif;", "\"Roboto\",,,_,sans-serif" },
        { "content: \"a,b\";", "\"a,b\"" },
        { "background: url(image.png);", "url(image.png)" },
        { "cursor: url(\"x.png\"), auto;", "url(\"x.png\"),,,_,auto" },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;
        char shape[256];

        RoboCheckEqInt(CssParseDeclarations(cases[index].text, &list, &error),
                          RoboOk);
        /* One declaration per case — a case that produced two would be a case
         * whose value was split on something it should not have been. */
        RoboCheckEqInt(CssDeclarationListCount(&list), 1u);
        RoboCheckEqInt(CssDeclarationListAt(&list, 0u)->state,
                          RoboCssDeclarationStateParsed);
        CssDeclarationShape(&list, 0u, shape, sizeof(shape));
        RoboCheckStrEq(shape, cases[index].shape);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        CssDeclarationListDispose(&list);
    }
}

static void TestCssParseDeclarationValueTrivia(void)
{
    CssError error = RoboCssCleared;
    CssDeclarationList tight = RoboCssCleared;
    CssDeclarationList loose = RoboCssCleared;
    CssDeclarationList inner = RoboCssCleared;
    char tightShape[64];
    char looseShape[64];
    char innerShape[64];

    /* Leading and trailing whitespace is not part of the value... */
    RoboCheckEqInt(CssParseDeclarations("color:red", &tight, &error), RoboOk);
    RoboCheckEqInt(CssParseDeclarations("color:  red  ", &loose, &error),
                      RoboOk);
    CssDeclarationShape(&tight, 0u, tightShape, sizeof(tightShape));
    CssDeclarationShape(&loose, 0u, looseShape, sizeof(looseShape));
    RoboCheckStrEq(tightShape, "red");
    RoboCheckStrEq(looseShape, "red");

    /* ...but the value's own extent still starts at the first real byte, so the
     * range is the value and not the whitespace around it. */
    RoboCheckEqInt(CssDeclarationListAt(&loose, 0u)->valueRange.begin, 8u);
    RoboCheckEqInt(CssDeclarationListAt(&loose, 0u)->valueRange.end, 11u);

    /* Interior whitespace *is* part of the value, and it is what tells `10px
     * 20px` from `10px20px` — one whitespace *token* covering the run, so the
     * shape shows three underscores and not three nodes. */
    RoboCheckEqInt(CssParseDeclarations("margin: 10px   20px", &inner, &error),
                      RoboOk);
    CssDeclarationShape(&inner, 0u, innerShape, sizeof(innerShape));
    RoboCheckStrEq(innerShape, "10px,___,20px");

    CssDeclarationListDispose(&tight);
    CssDeclarationListDispose(&loose);
    CssDeclarationListDispose(&inner);
}

/* ------------------------------------------------------------------ */
/* `!important`                                                        */
/* ------------------------------------------------------------------ */

static void TestCssParseDeclarationImportant(void)
{
    static const struct {
        const char *text;
        int important;
        const char *shape;
    } cases[] = {
        { "color: red !important;", 1, "red" },
        { "color: red!important;", 1, "red" },
        { "color: red !IMPORTANT;", 1, "red" },
        { "color: red !  important;", 1, "red" },
        { "color: red !important ", 1, "red" },
        { "color: red;", 0, "red" },
        { "color: important;", 0, "important" },
        { "color: red important;", 0, "red,_,important" },
        /* Not at the end, so not the flag: which of the two readings this
         * declaration should get — `red !important` with a stray `blue`, or an
         * invalid declaration — is a property-level question (9.8), and an
         * answer here would be 9.8's decision made in the wrong place. What
         * 9.3 must do is not call it important. */
        { "color: red !important blue;", 0, "red,_,!,important,_,blue" },
        { "color: !important;", 1, "" },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;
        char shape[128];

        RoboCheckEqInt(CssParseDeclarations(cases[index].text, &list, &error),
                          RoboOk);
        RoboCheckEqInt(CssDeclarationListCount(&list), 1u);
        RoboCheckEqInt(CssDeclarationListAt(&list, 0u)->important,
                          cases[index].important);
        CssDeclarationShape(&list, 0u, shape, sizeof(shape));
        RoboCheckStrEq(shape, cases[index].shape);
        CssDeclarationListDispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* Semicolons                                                          */
/* ------------------------------------------------------------------ */

static void TestCssParseDeclarationSemicolons(void)
{
    CssError error = RoboCssCleared;
    CssDeclarationList list = RoboCssCleared;
    char shape[128];

    /* The final semicolon is optional — a block's last declaration ends at the
     * `}` — and it is optional in an inline style too, where the text simply
     * ends. */
    RoboCheckEqInt(CssParseDeclarations("color: red", &list, &error), RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 1u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    CssDeclarationListDispose(&list);

    /* Empty declarations between real ones are legal and produce nothing:
     * `;;` is two separators with nothing to separate. */
    RoboCheckEqInt(CssParseDeclarations(
                          "  ; ; color: red ;; margin: 0 ;  ;", &list, &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 2u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    CssDeclarationShape(&list, 0u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "red");
    CssDeclarationShape(&list, 1u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "0");
    CssDeclarationListDispose(&list);

    /* Whitespace before the colon is fine, and so is a value with trailing
     * whitespace before the terminator. */
    RoboCheckEqInt(CssParseDeclarations("color : red ;", &list, &error), RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 1u);
    CssDeclarationShape(&list, 0u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "red");
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Custom properties                                                   */
/* ------------------------------------------------------------------ */

static void TestCssParseDeclarationCustomProperties(void)
{
    static const char *text =
        ":root {\n"
        "    --main-color: #06c;\n"
        "    --spacing: 10px;\n"
        "}\n"
        "body {\n"
        "    color: var(--main-color);\n"
        "    margin: var(--spacing, 4px);\n"
        "}\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssStyleRule *root = CssSheetStyleRule(sheet, 0u);
    const CssStyleRule *body = CssSheetStyleRule(sheet, 1u);
    char shape[128];

    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 2u);
    RoboCheck(root != NULL);
    RoboCheck(body != NULL);

    /* `:root` is a prelude, not a selector: 9.3 preserved it and matched
     * nothing. The custom properties themselves are declarations like any
     * other, in source order, with their values unresolved. */
    CssPreludeShape(root, shape, sizeof(shape));
    RoboCheckStrEq(shape, ":,root");
    RoboCheckEqInt(CssStyleRuleDeclarationCount(root), 2u);

    CssDeclarationNameOf(&root->declarations, 0u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "--main-color");
    CssDeclarationShape(&root->declarations, 0u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "#06c");
    CssDeclarationNameOf(&root->declarations, 1u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "--spacing");
    CssDeclarationShape(&root->declarations, 1u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "10px");

    /* `var()` is a function component value. Nothing substituted, nothing
     * resolved, and the fallback after the comma is still there. */
    CssDeclarationShape(&body->declarations, 0u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "var(--main-color)");
    CssDeclarationShape(&body->declarations, 1u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "var(--spacing,,,_,4px)");

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseDeclarationCustomPropertyValueIsRaw(void)
{
    CssError error = RoboCssCleared;
    CssDeclarationList list = RoboCssCleared;
    char shape[256];

    /* A custom property's value may be anything, including things that are not
     * values anywhere else. The grammar keeps the component values as they are
     * rather than validating them against a property that has no grammar — and
     * that is why `--x` can hold a bare `,` or a `{ }` block. */
    RoboCheckEqInt(CssParseDeclarations(
                          "--list: a, b, c; --block: { color: red }; --empty: ;",
                          &list, &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 3u);

    CssDeclarationShape(&list, 0u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "a,,,_,b,,,_,c");
    /* A block's contents include the trivia *inside* it — the declaration-level
     * trim stops at the block's own braces, because the whitespace is part of
     * what the author wrote between them. */
    CssDeclarationShape(&list, 1u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "{_,color,:,_,red,_}");
    /* An empty custom-property value is empty, and still a declaration. */
    CssDeclarationShape(&list, 2u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "");
    RoboCheckEqInt(CssDeclarationListAt(&list, 2u)->state,
                      RoboCssDeclarationStateParsed);

    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Malformed declarations                                              */
/* ------------------------------------------------------------------ */

static void TestCssParseDeclarationMalformed(void)
{
    static const char *text =
        "p {\n"
        "    color: red;\n"
        "    broken;\n"
        "    margin: 0;\n"
        "}\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssStyleRule *rule = CssSheetStyleRule(sheet, 0u);
    char shape[64];

    /* The malformed declaration is *in* the list, marked INVALID, and the two
     * valid ones around it were still parsed: recovery that dropped the rest of
     * the block would be recovery that lost the stylesheet. */
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    RoboCheckEqInt(rule->declarations.count, 3u);
    RoboCheckEqInt(rule->declarations.items[0].state,
                      RoboCssDeclarationStateParsed);
    RoboCheckEqInt(rule->declarations.items[1].state,
                      RoboCssDeclarationStateInvalid);
    RoboCheckEqInt(rule->declarations.items[2].state,
                      RoboCssDeclarationStateParsed);
    RoboCheckEqInt(CssDeclarationIsUsable(&rule->declarations.items[1]), 0);
    RoboCheckEqInt(CssDeclarationIsUsable(&rule->declarations.items[0]), 1);

    CssDeclarationNameOf(&rule->declarations, 1u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "broken");
    CssDeclarationShape(&rule->declarations, 2u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "0");

    /* And the problem is on the record, with a position: a caller can say
     * *where* the stylesheet was wrong, not merely that it was. */
    RoboCheckEqInt(error.kind, RoboCssErrorKindMalformedDeclaration);
    RoboCheckEqInt(error.position.offset, strlen("p {\n    color: red;\n    "));

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseDeclarationMalformedShapes(void)
{
    static const struct {
        const char *text;
        size_t count;
        CssDeclarationState state;
    } cases[] = {
        /* A bare ident is declaration-shaped and has no colon. */
        { "color", 1u, RoboCssDeclarationStateInvalid },
        /* A colon with no name is a nameless declaration: recorded, named "". */
        { ": red", 1u, RoboCssDeclarationStateInvalid },
        /* A name and a colon with no value is *not* malformed — `color:;` is an
         * empty value, and an empty value is a value. */
        { "color:", 1u, RoboCssDeclarationStateParsed },
        { "color: ;", 1u, RoboCssDeclarationStateParsed },
        /* Two declarations in a row with no separator: the second one's name is
         * part of the first one's value, because that is how CSS eats it. */
        { "color: red margin: 0", 1u, RoboCssDeclarationStateParsed },
        /* A block where a declaration was expected is not a declaration at all:
         * it is consumed whole (so its contents cannot be mistaken for
         * declarations) and dropped, which is what makes a `{` at the start of
         * an inline-style fragment produce nothing rather than nonsense. The
         * problem is still recorded; the case table asserts the count only. */
        { "{ color: red }", 0u, RoboCssDeclarationStateNone },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;

        RoboCheckEqInt(CssParseDeclarations(cases[index].text, &list, &error),
                          RoboOk);
        RoboCheckEqInt(CssDeclarationListCount(&list), cases[index].count);
        if (cases[index].count > 0u) {
            RoboCheckEqInt(CssDeclarationListAt(&list, 0u)->state,
                              cases[index].state);
        }
        CssDeclarationListDispose(&list);
    }
}

static void TestCssParseDeclarationDuplicatesAreKept(void)
{
    CssError error = RoboCssCleared;
    CssDeclarationList list = RoboCssCleared;
    char shape[64];
    size_t index;

    /* Two `color`s stay two `color`s, in source order. Dropping the first is the
     * cascade's last-one-wins rule, and implementing that here would mean the
     * cascade could never be implemented correctly later. */
    RoboCheckEqInt(CssParseDeclarations(
                          "color: red; color: blue; color: red !important;",
                          &list, &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 3u);
    CssDeclarationShape(&list, 0u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "red");
    CssDeclarationShape(&list, 1u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "blue");
    CssDeclarationShape(&list, 2u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "red");
    for (index = 0u; index < 3u; index++) {
        char name[32];

        CssDeclarationNameOf(&list, index, name, sizeof(name));
        RoboCheckStrEq(name, "color");
    }
    RoboCheckEqInt(CssDeclarationListAt(&list, 2u)->important, 1);

    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Ranges                                                             */
/* ------------------------------------------------------------------ */

static void TestCssParseDeclarationRanges(void)
{
    static const char *text = "p { color: red; }";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssDeclaration *declaration =
        CssStyleRuleDeclaration(CssSheetStyleRule(sheet, 0u), 0u);

    RoboCheck(declaration != NULL);
    /* `color` at 4, the colon at 9, `red` at 11..13, the `;` at 14 and outside
     * the declaration's own extent. Every one of these is a plain byte offset
     * into the caller's source, which is what makes them usable in a diagnostic
     * without a mapping step. */
    RoboCheckEqInt(declaration->nameRange.begin, 4u);
    RoboCheckEqInt(declaration->nameRange.end, 9u);
    RoboCheckEqInt(declaration->valueRange.begin, 11u);
    RoboCheckEqInt(declaration->valueRange.end, 14u);
    RoboCheckEqInt(declaration->range.begin, 4u);
    RoboCheckEqInt(declaration->range.end, 14u);
    RoboCheck(CssSliceEqualsAscii(declaration->name, "color"));

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseDeclarationInlineStyle(void)
{
    static const char *attribute = "color: red; background: blue ; margin: 0";
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    char shape[64];
    char name[32];

    /* The inline-style boundary (9.1's declaration-list entry point) parses for
     * real as of 9.3 and through the same code a block uses. */
    RoboCheckEqInt(CssDeclarationListParseText(
                          attribute, strlen(attribute), RoboCssOriginInline, &list,
                          &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 3u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);

    CssDeclarationNameOf(&list, 0u, name, sizeof(name));
    RoboCheckStrEq(name, "color");
    CssDeclarationShape(&list, 0u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "red");
    CssDeclarationNameOf(&list, 1u, name, sizeof(name));
    RoboCheckStrEq(name, "background");
    CssDeclarationShape(&list, 1u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "blue");
    CssDeclarationShape(&list, 2u, shape, sizeof(shape));
    RoboCheckStrEq(shape, "0");

    /* Borrowed, not copied: the declaration's name points *into* the caller's
     * attribute text. RoboCSS owns no second attribute store, so the assertion
     * that says so is a pointer comparison. */
    RoboCheck(CssDeclarationListAt(&list, 0u)->name.bytes ==
               (const uint8_t *)attribute);
    RoboCheck(CssDeclarationListAt(&list, 1u)->name.bytes ==
               (const uint8_t *)attribute + strlen("color: red; "));

    CssDeclarationListDispose(&list);
}

static void TestCssParseDeclarationInlineStyleEdgeCases(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;

    /* An empty `style` attribute is legal HTML and an empty declaration list is
     * the honest parse of it: nothing was wrong, there was nothing there. */
    RoboCheckEqInt(CssDeclarationListParseText(
                          "", 0u, RoboCssOriginInline, &list, &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 0u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);

    /* Whitespace-only, likewise. */
    RoboCheckEqInt(CssDeclarationListParseText(
                          "   \t\n ", strlen("   \t\n "), RoboCssOriginInline,
                          &list, &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 0u);

    /* A malformed fragment recovers the same way a block does: the good
     * declaration survives, the bad one is recorded. */
    RoboCheckEqInt(CssDeclarationListParseText(
                          "broken; color: red", strlen("broken; color: red"),
                          RoboCssOriginInline, &list, &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 2u);
    RoboCheckEqInt(CssDeclarationListAt(&list, 0u)->state,
                      RoboCssDeclarationStateInvalid);
    RoboCheckEqInt(CssDeclarationListAt(&list, 1u)->state,
                      RoboCssDeclarationStateParsed);
    RoboCheckEqInt(error.kind, RoboCssErrorKindMalformedDeclaration);

    /* Rejected arguments, not crashes: an unknown origin, and a NULL text with
     * a non-zero length. */
    RoboCheckEqInt(CssDeclarationListParseText(
                          "color: red", strlen("color: red"),
                          (CssOrigin)99, &list, &error),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssDeclarationListParseText(
                          NULL, 5u, RoboCssOriginInline, &list, &error),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssDeclarationListParseText(
                          "color: red", strlen("color: red"), RoboCssOriginInline,
                          NULL, &error),
                      RoboInvalidArgument);
    /* A NULL error record is allowed: the parse still happens. */
    RoboCheckEqInt(CssDeclarationListParseText(
                          "color: red", strlen("color: red"), RoboCssOriginInline,
                          &list, NULL),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 1u);

    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void TestCssParseDeclarationLifetime(void)
{
    static const char *text = "p { color: red !important; margin: 10px 20px; }";
    int weEnabled = CssAllocatorEnsureEnabled();
    size_t baseline = MemDebugLiveCount();
    int round;

    /* Forty parses of a sheet whose declarations own value trees, each one
     * disposed: a leak of any node type shows up as a rising live count at the
     * end, and a stale range shows up as a crash on the second round. */
    for (round = 0; round < 40; round++) {
        CssError error = RoboCssCleared;
        CssStylesheetId sheet = CssParseSheet(text, &error);
        const CssStyleRule *rule = CssSheetStyleRule(sheet, 0u);

        RoboCheckEqInt(CssStyleRuleDeclarationCount(rule), 2u);
        RoboCheckEqInt(CssStyleRuleDeclaration(rule, 0u)->important, 1);
        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    }

    /* Parsing the same sheet twice is idempotent, and the sheet owns its source
     * copy, so the second parse reads the same bytes the first one did. */
    {
        CssStylesheetId id = RoboCssStylesheetIdNone;
        CssError error = RoboCssCleared;

        RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor, NULL),
                          RoboOk);
        RoboCheckEqInt(CssStylesheetCopySource(
                              id, (const uint8_t *)text, strlen(text), NULL),
                          RoboOk);
        RoboCheckEqInt(CssStylesheetParse(id, &error, NULL), RoboOk);
        RoboCheckEqInt(CssStylesheetParse(id, &error, NULL), RoboOk);
        RoboCheckEqInt(CssStylesheetRuleCount(id), 1u);
        RoboCheckEqInt(
            CssStyleRuleDeclaration(CssSheetStyleRule(id, 0u), 0u)->important,
            1);
        RoboCheckEqInt(CssStylesheetDestroy(id, NULL), RoboOk);
    }

    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(weEnabled);
}

/* ------------------------------------------------------------------ */

void TestRegisterCssParseDeclaration(void);

void TestRegisterCssParseDeclaration(void)
{
    TestRegister("css.parse.declaration.names",
                       TestCssParseDeclarationNames);
    TestRegister("css.parse.declaration.name_case",
                       TestCssParseDeclarationNameCase);
    TestRegister("css.parse.declaration.value_shapes",
                       TestCssParseDeclarationValueShapes);
    TestRegister("css.parse.declaration.value_trivia",
                       TestCssParseDeclarationValueTrivia);
    TestRegister("css.parse.declaration.important",
                       TestCssParseDeclarationImportant);
    TestRegister("css.parse.declaration.semicolons",
                       TestCssParseDeclarationSemicolons);
    TestRegister("css.parse.declaration.custom_properties",
                       TestCssParseDeclarationCustomProperties);
    TestRegister("css.parse.declaration.custom_property_raw_value",
                       TestCssParseDeclarationCustomPropertyValueIsRaw);
    TestRegister("css.parse.declaration.malformed",
                       TestCssParseDeclarationMalformed);
    TestRegister("css.parse.declaration.malformed_shapes",
                       TestCssParseDeclarationMalformedShapes);
    TestRegister("css.parse.declaration.duplicates_are_kept",
                       TestCssParseDeclarationDuplicatesAreKept);
    TestRegister("css.parse.declaration.ranges",
                       TestCssParseDeclarationRanges);
    TestRegister("css.parse.declaration.inline_style",
                       TestCssParseDeclarationInlineStyle);
    TestRegister("css.parse.declaration.inline_style_edge_cases",
                       TestCssParseDeclarationInlineStyleEdgeCases);
    TestRegister("css.parse.declaration.lifetime",
                       TestCssParseDeclarationLifetime);
}
