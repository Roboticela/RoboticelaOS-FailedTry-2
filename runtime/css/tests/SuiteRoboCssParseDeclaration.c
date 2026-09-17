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

static void test_css_parse_declaration_names(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    char name[64];

    /* Hyphenated, custom-property and multi-hyphen names all survive as
     * written: the tokenizer produced one IDENT for each, and the grammar
     * neither split it nor renamed it. */
    ROBO_CHECK_EQ_INT(css_parse_declarations(
                          "font-size: 12px; --main-color: #06c; -webkit-box: none;"
                          " background-color: white;",
                          &list, &error),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 4u);

    css_declaration_name_of(&list, 0u, name, sizeof(name));
    ROBO_CHECK_STR_EQ(name, "font-size");
    css_declaration_name_of(&list, 1u, name, sizeof(name));
    ROBO_CHECK_STR_EQ(name, "--main-color");
    css_declaration_name_of(&list, 2u, name, sizeof(name));
    ROBO_CHECK_STR_EQ(name, "-webkit-box");
    css_declaration_name_of(&list, 3u, name, sizeof(name));
    ROBO_CHECK_STR_EQ(name, "background-color");

    /* Nothing was registered in the property table this build carries, so every
     * one of them resolves to NONE. A parser that claimed an identity here
     * would be inventing a property database 9.16 has not built yet. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 0u)->property,
                      ROBO_CSS_PROPERTY_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 1u)->property,
                      ROBO_CSS_PROPERTY_ID_NONE);

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    robo_css_declaration_list_dispose(&list);
}

static void test_css_parse_declaration_name_case(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    char name[64];

    /* The record keeps the author's spelling; case-insensitivity is a property
     * of *lookup* (and lives in the property table, which is case-insensitive).
     * Folding here would make `--Main-Color` collide with `--main-color`, and a
     * custom property is case-*sensitive* by specification. */
    ROBO_CHECK_EQ_INT(css_parse_declarations("COLOR: red; --Main-Color: blue;",
                                             &list, &error),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 2u);

    css_declaration_name_of(&list, 0u, name, sizeof(name));
    ROBO_CHECK_STR_EQ(name, "COLOR");
    css_declaration_name_of(&list, 1u, name, sizeof(name));
    ROBO_CHECK_STR_EQ(name, "--Main-Color");

    robo_css_declaration_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

static void test_css_parse_declaration_value_shapes(void)
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
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        char shape[256];

        ROBO_CHECK_EQ_INT(css_parse_declarations(cases[index].text, &list, &error),
                          ROBO_OK);
        /* One declaration per case — a case that produced two would be a case
         * whose value was split on something it should not have been. */
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 0u)->state,
                          ROBO_CSS_DECLARATION_STATE_PARSED);
        css_declaration_shape(&list, 0u, shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, cases[index].shape);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        robo_css_declaration_list_dispose(&list);
    }
}

static void test_css_parse_declaration_value_trivia(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t tight = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t loose = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t inner = ROBO_CSS_CLEARED;
    char tight_shape[64];
    char loose_shape[64];
    char inner_shape[64];

    /* Leading and trailing whitespace is not part of the value... */
    ROBO_CHECK_EQ_INT(css_parse_declarations("color:red", &tight, &error), ROBO_OK);
    ROBO_CHECK_EQ_INT(css_parse_declarations("color:  red  ", &loose, &error),
                      ROBO_OK);
    css_declaration_shape(&tight, 0u, tight_shape, sizeof(tight_shape));
    css_declaration_shape(&loose, 0u, loose_shape, sizeof(loose_shape));
    ROBO_CHECK_STR_EQ(tight_shape, "red");
    ROBO_CHECK_STR_EQ(loose_shape, "red");

    /* ...but the value's own extent still starts at the first real byte, so the
     * range is the value and not the whitespace around it. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&loose, 0u)->value_range.begin, 8u);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&loose, 0u)->value_range.end, 11u);

    /* Interior whitespace *is* part of the value, and it is what tells `10px
     * 20px` from `10px20px` — one whitespace *token* covering the run, so the
     * shape shows three underscores and not three nodes. */
    ROBO_CHECK_EQ_INT(css_parse_declarations("margin: 10px   20px", &inner, &error),
                      ROBO_OK);
    css_declaration_shape(&inner, 0u, inner_shape, sizeof(inner_shape));
    ROBO_CHECK_STR_EQ(inner_shape, "10px,___,20px");

    robo_css_declaration_list_dispose(&tight);
    robo_css_declaration_list_dispose(&loose);
    robo_css_declaration_list_dispose(&inner);
}

/* ------------------------------------------------------------------ */
/* `!important`                                                        */
/* ------------------------------------------------------------------ */

static void test_css_parse_declaration_important(void)
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
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        char shape[128];

        ROBO_CHECK_EQ_INT(css_parse_declarations(cases[index].text, &list, &error),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 0u)->important,
                          cases[index].important);
        css_declaration_shape(&list, 0u, shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, cases[index].shape);
        robo_css_declaration_list_dispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* Semicolons                                                          */
/* ------------------------------------------------------------------ */

static void test_css_parse_declaration_semicolons(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    char shape[128];

    /* The final semicolon is optional — a block's last declaration ends at the
     * `}` — and it is optional in an inline style too, where the text simply
     * ends. */
    ROBO_CHECK_EQ_INT(css_parse_declarations("color: red", &list, &error), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    robo_css_declaration_list_dispose(&list);

    /* Empty declarations between real ones are legal and produce nothing:
     * `;;` is two separators with nothing to separate. */
    ROBO_CHECK_EQ_INT(css_parse_declarations(
                          "  ; ; color: red ;; margin: 0 ;  ;", &list, &error),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 2u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    css_declaration_shape(&list, 0u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "red");
    css_declaration_shape(&list, 1u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "0");
    robo_css_declaration_list_dispose(&list);

    /* Whitespace before the colon is fine, and so is a value with trailing
     * whitespace before the terminator. */
    ROBO_CHECK_EQ_INT(css_parse_declarations("color : red ;", &list, &error), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);
    css_declaration_shape(&list, 0u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "red");
    robo_css_declaration_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Custom properties                                                   */
/* ------------------------------------------------------------------ */

static void test_css_parse_declaration_custom_properties(void)
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
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_style_rule_t *root = css_sheet_style_rule(sheet, 0u);
    const robo_css_style_rule_t *body = css_sheet_style_rule(sheet, 1u);
    char shape[128];

    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 2u);
    ROBO_CHECK(root != NULL);
    ROBO_CHECK(body != NULL);

    /* `:root` is a prelude, not a selector: 9.3 preserved it and matched
     * nothing. The custom properties themselves are declarations like any
     * other, in source order, with their values unresolved. */
    css_prelude_shape(root, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, ":,root");
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(root), 2u);

    css_declaration_name_of(&root->declarations, 0u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "--main-color");
    css_declaration_shape(&root->declarations, 0u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "#06c");
    css_declaration_name_of(&root->declarations, 1u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "--spacing");
    css_declaration_shape(&root->declarations, 1u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "10px");

    /* `var()` is a function component value. Nothing substituted, nothing
     * resolved, and the fallback after the comma is still there. */
    css_declaration_shape(&body->declarations, 0u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "var(--main-color)");
    css_declaration_shape(&body->declarations, 1u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "var(--spacing,,,_,4px)");

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_declaration_custom_property_value_is_raw(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    char shape[256];

    /* A custom property's value may be anything, including things that are not
     * values anywhere else. The grammar keeps the component values as they are
     * rather than validating them against a property that has no grammar — and
     * that is why `--x` can hold a bare `,` or a `{ }` block. */
    ROBO_CHECK_EQ_INT(css_parse_declarations(
                          "--list: a, b, c; --block: { color: red }; --empty: ;",
                          &list, &error),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 3u);

    css_declaration_shape(&list, 0u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "a,,,_,b,,,_,c");
    /* A block's contents include the trivia *inside* it — the declaration-level
     * trim stops at the block's own braces, because the whitespace is part of
     * what the author wrote between them. */
    css_declaration_shape(&list, 1u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "{_,color,:,_,red,_}");
    /* An empty custom-property value is empty, and still a declaration. */
    css_declaration_shape(&list, 2u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "");
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 2u)->state,
                      ROBO_CSS_DECLARATION_STATE_PARSED);

    robo_css_declaration_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Malformed declarations                                              */
/* ------------------------------------------------------------------ */

static void test_css_parse_declaration_malformed(void)
{
    static const char *text =
        "p {\n"
        "    color: red;\n"
        "    broken;\n"
        "    margin: 0;\n"
        "}\n";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, 0u);
    char shape[64];

    /* The malformed declaration is *in* the list, marked INVALID, and the two
     * valid ones around it were still parsed: recovery that dropped the rest of
     * the block would be recovery that lost the stylesheet. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
    ROBO_CHECK_EQ_INT(rule->declarations.count, 3u);
    ROBO_CHECK_EQ_INT(rule->declarations.items[0].state,
                      ROBO_CSS_DECLARATION_STATE_PARSED);
    ROBO_CHECK_EQ_INT(rule->declarations.items[1].state,
                      ROBO_CSS_DECLARATION_STATE_INVALID);
    ROBO_CHECK_EQ_INT(rule->declarations.items[2].state,
                      ROBO_CSS_DECLARATION_STATE_PARSED);
    ROBO_CHECK_EQ_INT(robo_css_declaration_is_usable(&rule->declarations.items[1]), 0);
    ROBO_CHECK_EQ_INT(robo_css_declaration_is_usable(&rule->declarations.items[0]), 1);

    css_declaration_name_of(&rule->declarations, 1u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "broken");
    css_declaration_shape(&rule->declarations, 2u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "0");

    /* And the problem is on the record, with a position: a caller can say
     * *where* the stylesheet was wrong, not merely that it was. */
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION);
    ROBO_CHECK_EQ_INT(error.position.offset, strlen("p {\n    color: red;\n    "));

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_declaration_malformed_shapes(void)
{
    static const struct {
        const char *text;
        size_t count;
        robo_css_declaration_state_t state;
    } cases[] = {
        /* A bare ident is declaration-shaped and has no colon. */
        { "color", 1u, ROBO_CSS_DECLARATION_STATE_INVALID },
        /* A colon with no name is a nameless declaration: recorded, named "". */
        { ": red", 1u, ROBO_CSS_DECLARATION_STATE_INVALID },
        /* A name and a colon with no value is *not* malformed — `color:;` is an
         * empty value, and an empty value is a value. */
        { "color:", 1u, ROBO_CSS_DECLARATION_STATE_PARSED },
        { "color: ;", 1u, ROBO_CSS_DECLARATION_STATE_PARSED },
        /* Two declarations in a row with no separator: the second one's name is
         * part of the first one's value, because that is how CSS eats it. */
        { "color: red margin: 0", 1u, ROBO_CSS_DECLARATION_STATE_PARSED },
        /* A block where a declaration was expected is not a declaration at all:
         * it is consumed whole (so its contents cannot be mistaken for
         * declarations) and dropped, which is what makes a `{` at the start of
         * an inline-style fragment produce nothing rather than nonsense. The
         * problem is still recorded; the case table asserts the count only. */
        { "{ color: red }", 0u, ROBO_CSS_DECLARATION_STATE_NONE },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(css_parse_declarations(cases[index].text, &list, &error),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), cases[index].count);
        if (cases[index].count > 0u) {
            ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 0u)->state,
                              cases[index].state);
        }
        robo_css_declaration_list_dispose(&list);
    }
}

static void test_css_parse_declaration_duplicates_are_kept(void)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    char shape[64];
    size_t index;

    /* Two `color`s stay two `color`s, in source order. Dropping the first is the
     * cascade's last-one-wins rule, and implementing that here would mean the
     * cascade could never be implemented correctly later. */
    ROBO_CHECK_EQ_INT(css_parse_declarations(
                          "color: red; color: blue; color: red !important;",
                          &list, &error),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 3u);
    css_declaration_shape(&list, 0u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "red");
    css_declaration_shape(&list, 1u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "blue");
    css_declaration_shape(&list, 2u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "red");
    for (index = 0u; index < 3u; index++) {
        char name[32];

        css_declaration_name_of(&list, index, name, sizeof(name));
        ROBO_CHECK_STR_EQ(name, "color");
    }
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 2u)->important, 1);

    robo_css_declaration_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Ranges                                                             */
/* ------------------------------------------------------------------ */

static void test_css_parse_declaration_ranges(void)
{
    static const char *text = "p { color: red; }";
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
    const robo_css_declaration_t *declaration =
        robo_css_style_rule_declaration(css_sheet_style_rule(sheet, 0u), 0u);

    ROBO_CHECK(declaration != NULL);
    /* `color` at 4, the colon at 9, `red` at 11..13, the `;` at 14 and outside
     * the declaration's own extent. Every one of these is a plain byte offset
     * into the caller's source, which is what makes them usable in a diagnostic
     * without a mapping step. */
    ROBO_CHECK_EQ_INT(declaration->name_range.begin, 4u);
    ROBO_CHECK_EQ_INT(declaration->name_range.end, 9u);
    ROBO_CHECK_EQ_INT(declaration->value_range.begin, 11u);
    ROBO_CHECK_EQ_INT(declaration->value_range.end, 14u);
    ROBO_CHECK_EQ_INT(declaration->range.begin, 4u);
    ROBO_CHECK_EQ_INT(declaration->range.end, 14u);
    ROBO_CHECK(robo_css_slice_equals_ascii(declaration->name, "color"));

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_parse_declaration_inline_style(void)
{
    static const char *attribute = "color: red; background: blue ; margin: 0";
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    char shape[64];
    char name[32];

    /* The inline-style boundary (9.1's declaration-list entry point) parses for
     * real as of 9.3 and through the same code a block uses. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(
                          attribute, strlen(attribute), ROBO_CSS_ORIGIN_INLINE, &list,
                          &error),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 3u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);

    css_declaration_name_of(&list, 0u, name, sizeof(name));
    ROBO_CHECK_STR_EQ(name, "color");
    css_declaration_shape(&list, 0u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "red");
    css_declaration_name_of(&list, 1u, name, sizeof(name));
    ROBO_CHECK_STR_EQ(name, "background");
    css_declaration_shape(&list, 1u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "blue");
    css_declaration_shape(&list, 2u, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "0");

    /* Borrowed, not copied: the declaration's name points *into* the caller's
     * attribute text. RoboCSS owns no second attribute store, so the assertion
     * that says so is a pointer comparison. */
    ROBO_CHECK(robo_css_declaration_list_at(&list, 0u)->name.bytes ==
               (const uint8_t *)attribute);
    ROBO_CHECK(robo_css_declaration_list_at(&list, 1u)->name.bytes ==
               (const uint8_t *)attribute + strlen("color: red; "));

    robo_css_declaration_list_dispose(&list);
}

static void test_css_parse_declaration_inline_style_edge_cases(void)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;

    /* An empty `style` attribute is legal HTML and an empty declaration list is
     * the honest parse of it: nothing was wrong, there was nothing there. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(
                          "", 0u, ROBO_CSS_ORIGIN_INLINE, &list, &error),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 0u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);

    /* Whitespace-only, likewise. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(
                          "   \t\n ", strlen("   \t\n "), ROBO_CSS_ORIGIN_INLINE,
                          &list, &error),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 0u);

    /* A malformed fragment recovers the same way a block does: the good
     * declaration survives, the bad one is recorded. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(
                          "broken; color: red", strlen("broken; color: red"),
                          ROBO_CSS_ORIGIN_INLINE, &list, &error),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 2u);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 0u)->state,
                      ROBO_CSS_DECLARATION_STATE_INVALID);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_at(&list, 1u)->state,
                      ROBO_CSS_DECLARATION_STATE_PARSED);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION);

    /* Rejected arguments, not crashes: an unknown origin, and a NULL text with
     * a non-zero length. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(
                          "color: red", strlen("color: red"),
                          (robo_css_origin_t)99, &list, &error),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(
                          NULL, 5u, ROBO_CSS_ORIGIN_INLINE, &list, &error),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(
                          "color: red", strlen("color: red"), ROBO_CSS_ORIGIN_INLINE,
                          NULL, &error),
                      ROBO_INVALID_ARGUMENT);
    /* A NULL error record is allowed: the parse still happens. */
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_parse_text(
                          "color: red", strlen("color: red"), ROBO_CSS_ORIGIN_INLINE,
                          &list, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);

    robo_css_declaration_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void test_css_parse_declaration_lifetime(void)
{
    static const char *text = "p { color: red !important; margin: 10px 20px; }";
    int we_enabled = css_allocator_ensure_enabled();
    size_t baseline = robo_mem_debug_live_count();
    int round;

    /* Forty parses of a sheet whose declarations own value trees, each one
     * disposed: a leak of any node type shows up as a rising live count at the
     * end, and a stale range shows up as a crash on the second round. */
    for (round = 0; round < 40; round++) {
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_stylesheet_id_t sheet = css_parse_sheet(text, &error);
        const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, 0u);

        ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(rule), 2u);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration(rule, 0u)->important, 1);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    }

    /* Parsing the same sheet twice is idempotent, and the sheet owns its source
     * copy, so the second parse reads the same bytes the first one did. */
    {
        robo_css_stylesheet_id_t id = ROBO_CSS_STYLESHEET_ID_NONE;
        robo_css_error_t error = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_copy_source(
                              id, (const uint8_t *)text, strlen(text), NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_parse(id, &error, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_parse(id, &error, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(id), 1u);
        ROBO_CHECK_EQ_INT(
            robo_css_style_rule_declaration(css_sheet_style_rule(id, 0u), 0u)->important,
            1);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(id, NULL), ROBO_OK);
    }

    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(we_enabled);
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_parse_declaration(void);

void robo_test_register_css_parse_declaration(void)
{
    robo_test_register("css.parse.declaration.names",
                       test_css_parse_declaration_names);
    robo_test_register("css.parse.declaration.name_case",
                       test_css_parse_declaration_name_case);
    robo_test_register("css.parse.declaration.value_shapes",
                       test_css_parse_declaration_value_shapes);
    robo_test_register("css.parse.declaration.value_trivia",
                       test_css_parse_declaration_value_trivia);
    robo_test_register("css.parse.declaration.important",
                       test_css_parse_declaration_important);
    robo_test_register("css.parse.declaration.semicolons",
                       test_css_parse_declaration_semicolons);
    robo_test_register("css.parse.declaration.custom_properties",
                       test_css_parse_declaration_custom_properties);
    robo_test_register("css.parse.declaration.custom_property_raw_value",
                       test_css_parse_declaration_custom_property_value_is_raw);
    robo_test_register("css.parse.declaration.malformed",
                       test_css_parse_declaration_malformed);
    robo_test_register("css.parse.declaration.malformed_shapes",
                       test_css_parse_declaration_malformed_shapes);
    robo_test_register("css.parse.declaration.duplicates_are_kept",
                       test_css_parse_declaration_duplicates_are_kept);
    robo_test_register("css.parse.declaration.ranges",
                       test_css_parse_declaration_ranges);
    robo_test_register("css.parse.declaration.inline_style",
                       test_css_parse_declaration_inline_style);
    robo_test_register("css.parse.declaration.inline_style_edge_cases",
                       test_css_parse_declaration_inline_style_edge_cases);
    robo_test_register("css.parse.declaration.lifetime",
                       test_css_parse_declaration_lifetime);
}
