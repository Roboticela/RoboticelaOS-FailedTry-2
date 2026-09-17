/*
 * RoboCSS selector parsing suite (Phase 9.4) — css.select.parse.*
 *
 * Every test here asserts the *structure* the parser built: how many selectors,
 * how many compounds, which combinator between them, which kind of simple
 * selector, and the resolved name. Asserting "it parsed" would pass for a parser
 * that produced one universal selector for every input, so nothing here does.
 *
 * The names are the other half. A parsed name is the tokenizer's *value*, not
 * the source bytes, so `.foo\:bar` must read back as `foo:bar` and `#item\31 23`
 * as `item123`. If escapes were left in, every one of those tests would fail —
 * which is the point: escape handling is observable here rather than assumed.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Lists                                                               */
/* ------------------------------------------------------------------ */

static void test_css_select_single_type(void)
{
    robo_css_selector_list_t list;
    const robo_css_simple_selector_t *simple;
    char text[32];

    css_select_parse("p", &list);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 1u);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_is_well_formed(&list), 1);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(
                          robo_css_selector_list_at(&list, 0u)),
                      1u);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 1u);

    simple = css_select_simple(&list, 0u, 0u, 0u);
    ROBO_CHECK_EQ_INT(simple->kind, ROBO_CSS_SIMPLE_SELECTOR_TYPE);
    css_select_text(simple->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "p"), 0);
    ROBO_CHECK_EQ_INT(simple->range.begin, 0u);
    ROBO_CHECK_EQ_INT(simple->range.end, 1u);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_list_order(void)
{
    robo_css_selector_list_t list;
    size_t i;
    static const char *names[] = {"h1", "h2", "h3"};

    css_select_parse("h1, h2, h3", &list);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 3u);
    for (i = 0u; i < 3u; i++) {
        char text[16];

        css_select_text(css_select_simple(&list, i, 0u, 0u)->name, text, sizeof(text));
        ROBO_CHECK_EQ_INT(strcmp(text, names[i]), 0);
    }
    /* The list's range spans the whole thing, commas included: `h1, h2, h3` is
     * ten bytes, and the derived range reaches the end of `h3`, not of the
     * comma before it. */
    ROBO_CHECK_EQ_INT(list.range.begin, 0u);
    ROBO_CHECK_EQ_INT(list.range.end, 10u);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_list_whitespace_around_commas(void)
{
    robo_css_selector_list_t list;

    css_select_parse("  h1 ,\n\t h2  ", &list);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 2u);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 1u);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Type selectors                                                      */
/* ------------------------------------------------------------------ */

static void test_css_select_type_keeps_spelling(void)
{
    robo_css_selector_list_t list;
    const robo_css_simple_selector_t *simple;
    char text[16];

    /* The author's spelling is preserved; folding is the matcher's business, so
     * a diagnostic can quote what was actually written. */
    css_select_parse("DIV", &list);
    simple = css_select_simple(&list, 0u, 0u, 0u);
    ROBO_CHECK_EQ_INT(simple->kind, ROBO_CSS_SIMPLE_SELECTOR_TYPE);
    css_select_text(simple->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "DIV"), 0);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_type_unicode(void)
{
    robo_css_selector_list_t list;
    const robo_css_simple_selector_t *simple;
    char text[32];

    /* "fixture" in Arabic script: a non-ASCII identifier is one ident token and
     * must survive byte for byte. */
    css_select_parse("\xD9\x82\xD8\xB3\xD9\x85", &list);
    simple = css_select_simple(&list, 0u, 0u, 0u);
    ROBO_CHECK_EQ_INT(simple->kind, ROBO_CSS_SIMPLE_SELECTOR_TYPE);
    ROBO_CHECK_EQ_INT(simple->name.length, 6u);
    css_select_text(simple->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "\xD9\x82\xD8\xB3\xD9\x85"), 0);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_type_escaped(void)
{
    robo_css_selector_list_t list;
    char text[16];

    /* `\64 iv` spells `div`: the hex escape and the whitespace that terminates
     * it are both gone from the name. */
    css_select_parse("\\64 iv", &list);
    css_select_text(css_select_simple(&list, 0u, 0u, 0u)->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "div"), 0);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Universal                                                           */
/* ------------------------------------------------------------------ */

static void test_css_select_universal_alone(void)
{
    robo_css_selector_list_t list;

    css_select_parse("*", &list);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL);
    ROBO_CHECK_EQ_INT(robo_css_slice_is_empty(
                          css_select_simple(&list, 0u, 0u, 0u)->name),
                      1);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_universal_in_compound(void)
{
    robo_css_selector_list_t list;

    css_select_parse("*.card", &list);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 2u);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 1u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_CLASS);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_universal_descendant(void)
{
    robo_css_selector_list_t list;
    const robo_css_complex_selector_t *complex;

    css_select_parse("div *", &list);
    complex = robo_css_selector_list_at(&list, 0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(complex), 2u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(complex, 0u),
                      ROBO_CSS_COMBINATOR_DESCENDANT);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 1u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Id and class                                                        */
/* ------------------------------------------------------------------ */

static void test_css_select_id_and_class(void)
{
    robo_css_selector_list_t list;
    char text[32];

    css_select_parse("div#main.card", &list);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 3u);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_TYPE);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 1u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_ID);
    css_select_text(css_select_simple(&list, 0u, 0u, 1u)->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "main"), 0);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 2u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_CLASS);
    css_select_text(css_select_simple(&list, 0u, 0u, 2u)->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "card"), 0);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_class_multiple_in_one_compound(void)
{
    robo_css_selector_list_t list;
    char text[32];

    css_select_parse(".button.primary.active", &list);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 3u);
    css_select_text(css_select_simple(&list, 0u, 0u, 2u)->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "active"), 0);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_class_hyphen_and_underscore(void)
{
    robo_css_selector_list_t list;
    char text[32];

    css_select_parse(".foo-bar_baz", &list);
    css_select_text(css_select_simple(&list, 0u, 0u, 0u)->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "foo-bar_baz"), 0);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_escaped_identifiers(void)
{
    robo_css_selector_list_t list;
    char text[32];

    /* A colon in a class name is spelled with an escape, and the escape is not
     * part of the name. */
    css_select_parse(".foo\\:bar", &list);
    css_select_text(css_select_simple(&list, 0u, 0u, 0u)->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "foo:bar"), 0);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_id_escaped_digits(void)
{
    robo_css_selector_list_t list;
    char text[32];

    /* `#item\31 23` is `item123`: a leading digit needs the escape, and the
     * whitespace that ends the hex escape is consumed. */
    css_select_parse("#item\\31 23", &list);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_ID);
    css_select_text(css_select_simple(&list, 0u, 0u, 0u)->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "item123"), 0);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Attribute selectors                                                 */
/* ------------------------------------------------------------------ */

/** Parse @p text and return its first simple selector, disposed by the caller. */
static const robo_css_simple_selector_t *select_attribute_of(
    robo_css_selector_list_t *list,
    const char *text)
{
    css_select_parse(text, list);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(list), 1u);
    return css_select_simple(list, 0u, 0u, 0u);
}

static void test_css_select_attribute_presence(void)
{
    robo_css_selector_list_t list;
    const robo_css_simple_selector_t *simple = select_attribute_of(&list, "[disabled]");
    char text[32];

    ROBO_CHECK_EQ_INT(simple->kind, ROBO_CSS_SIMPLE_SELECTOR_ATTRIBUTE);
    css_select_text(simple->attribute, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "disabled"), 0);
    ROBO_CHECK_EQ_INT(simple->match, ROBO_CSS_ATTRIBUTE_MATCH_NONE);
    /* Presence has no value: `[a]` and `[a=""]` must stay different selectors. */
    ROBO_CHECK_EQ_INT(robo_css_slice_is_absent(simple->attribute_value), 1);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_attribute_all_operators(void)
{
    static const struct {
        const char *text;
        robo_css_attribute_match_t expected;
        const char *value;
    } cases[] = {
        {"[type=\"button\"]", ROBO_CSS_ATTRIBUTE_MATCH_EQUALS, "button"},
        {"[type='button']", ROBO_CSS_ATTRIBUTE_MATCH_EQUALS, "button"},
        {"[data-id=abc]", ROBO_CSS_ATTRIBUTE_MATCH_EQUALS, "abc"},
        {"[class~=active]", ROBO_CSS_ATTRIBUTE_MATCH_INCLUDES, "active"},
        {"[lang|=en]", ROBO_CSS_ATTRIBUTE_MATCH_DASH, "en"},
        {"[href^=\"/api\"]", ROBO_CSS_ATTRIBUTE_MATCH_PREFIX, "/api"},
        {"[href$=\".json\"]", ROBO_CSS_ATTRIBUTE_MATCH_SUFFIX, ".json"},
        {"[data-name*=\"test\"]", ROBO_CSS_ATTRIBUTE_MATCH_SUBSTRING, "test"}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple =
            select_attribute_of(&list, cases[i].text);
        char text[32];

        ROBO_CHECK_EQ_INT(simple->match, cases[i].expected);
        css_select_text(simple->attribute_value, text, sizeof(text));
        ROBO_CHECK_EQ_INT(strcmp(text, cases[i].value), 0);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_select_attribute_value_forms(void)
{
    {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple =
            select_attribute_of(&list, "[type=\"text\"]");
        ROBO_CHECK_EQ_INT(simple->value_was_string, 1);
        robo_css_selector_list_dispose(&list);
    }
    {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple =
            select_attribute_of(&list, "[type=text]");
        /* The same comparison, spelled without quotes: the flag records which,
         * the meaning does not change. */
        ROBO_CHECK_EQ_INT(simple->value_was_string, 0);
        ROBO_CHECK_EQ_INT(simple->match, ROBO_CSS_ATTRIBUTE_MATCH_EQUALS);
        robo_css_selector_list_dispose(&list);
    }
    {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple =
            select_attribute_of(&list, "[a=\"\"]");
        /* An empty value is a value: present, empty, and comparing equal to "". */
        ROBO_CHECK_EQ_INT(simple->match, ROBO_CSS_ATTRIBUTE_MATCH_EQUALS);
        ROBO_CHECK_EQ_INT(robo_css_slice_is_empty(simple->attribute_value), 1);
        ROBO_CHECK_EQ_INT(simple->value_was_string, 1);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_select_attribute_case_flags(void)
{
    {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple =
            select_attribute_of(&list, "[type=\"text\" i]");
        ROBO_CHECK_EQ_INT(simple->case_flag, ROBO_CSS_ATTRIBUTE_CASE_INSENSITIVE);
        robo_css_selector_list_dispose(&list);
    }
    {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple =
            select_attribute_of(&list, "[type=\"text\" s]");
        ROBO_CHECK_EQ_INT(simple->case_flag, ROBO_CSS_ATTRIBUTE_CASE_SENSITIVE);
        robo_css_selector_list_dispose(&list);
    }
    {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple =
            select_attribute_of(&list, "[type=\"text\"]");
        ROBO_CHECK_EQ_INT(simple->case_flag, ROBO_CSS_ATTRIBUTE_CASE_DEFAULT);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_select_attribute_whitespace_and_escapes(void)
{
    {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple =
            select_attribute_of(&list, "[ type = \"text\" ]");
        char text[32];

        ROBO_CHECK_EQ_INT(simple->match, ROBO_CSS_ATTRIBUTE_MATCH_EQUALS);
        css_select_text(simple->attribute, text, sizeof(text));
        ROBO_CHECK_EQ_INT(strcmp(text, "type"), 0);
        css_select_text(simple->attribute_value, text, sizeof(text));
        ROBO_CHECK_EQ_INT(strcmp(text, "text"), 0);
        robo_css_selector_list_dispose(&list);
    }
    {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple =
            select_attribute_of(&list, "[data-name=\"a\\ b\"]");
        char text[32];

        /* An escaped space inside a string is content, so the value is `a b`. */
        css_select_text(simple->attribute_value, text, sizeof(text));
        ROBO_CHECK_EQ_INT(strcmp(text, "a b"), 0);
        ROBO_CHECK_EQ_INT(simple->attribute_value.length, 3u);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_select_attribute_in_compound(void)
{
    robo_css_selector_list_t list;

    css_select_parse("input[type=\"text\"].field", &list);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 3u);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_TYPE);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 1u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_ATTRIBUTE);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 2u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_CLASS);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Combinators                                                         */
/* ------------------------------------------------------------------ */

static void test_css_select_combinators_all(void)
{
    static const struct {
        const char *text;
        robo_css_combinator_t expected;
    } cases[] = {
        {"div p", ROBO_CSS_COMBINATOR_DESCENDANT},
        {"div > p", ROBO_CSS_COMBINATOR_CHILD},
        {"div>p", ROBO_CSS_COMBINATOR_CHILD},
        {"h1 + p", ROBO_CSS_COMBINATOR_NEXT_SIBLING},
        {"h1+p", ROBO_CSS_COMBINATOR_NEXT_SIBLING},
        {"h1 ~ p", ROBO_CSS_COMBINATOR_SUBSEQUENT_SIBLING},
        {"h1~p", ROBO_CSS_COMBINATOR_SUBSEQUENT_SIBLING},
        {"col || td", ROBO_CSS_COMBINATOR_COLUMN},
        {"div\t\np", ROBO_CSS_COMBINATOR_DESCENDANT}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_css_selector_list_t list;
        const robo_css_complex_selector_t *complex;

        css_select_parse(cases[i].text, &list);
        complex = robo_css_selector_list_at(&list, 0u);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(complex), 2u);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_count(complex), 1u);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(complex, 0u),
                          cases[i].expected);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_select_combinator_whitespace_forms(void)
{
    static const char *cases[] = {
        "div  >  p", "div >p", "div> p", "div\n>\np"
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_css_selector_list_t list;
        const robo_css_complex_selector_t *complex;

        css_select_parse(cases[i], &list);
        complex = robo_css_selector_list_at(&list, 0u);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(complex), 2u);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(complex, 0u),
                          ROBO_CSS_COMBINATOR_CHILD);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_select_comment_is_not_a_combinator(void)
{
    /* A comment before `>` is not whitespace and does not add a descendant
     * combinator: a comment between `div` and `>p` leaves exactly two compounds
     * joined by a child combinator, not three joined by descendant-then-child. */
    robo_css_selector_list_t list;
    const robo_css_complex_selector_t *complex;

    css_select_parse("div/* note */>p", &list);
    complex = robo_css_selector_list_at(&list, 0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(complex), 2u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_count(complex), 1u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(complex, 0u),
                      ROBO_CSS_COMBINATOR_CHILD);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_complex_selector(void)
{
    robo_css_selector_list_t list;
    const robo_css_complex_selector_t *complex;

    css_select_parse("#app > .panel .title", &list);
    complex = robo_css_selector_list_at(&list, 0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(complex), 3u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_count(complex), 2u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(complex, 0u),
                      ROBO_CSS_COMBINATOR_CHILD);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(complex, 1u),
                      ROBO_CSS_COMBINATOR_DESCENDANT);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 1u);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(
                          robo_css_complex_selector_compound_at(complex, 1u)),
                      1u);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(
                          robo_css_complex_selector_compound_at(complex, 2u)),
                      1u);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_complex_four_compounds(void)
{
    robo_css_selector_list_t list;
    const robo_css_complex_selector_t *complex;

    css_select_parse("article.card > header.title + p em", &list);
    complex = robo_css_selector_list_at(&list, 0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(complex), 4u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(complex, 0u),
                      ROBO_CSS_COMBINATOR_CHILD);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(complex, 1u),
                      ROBO_CSS_COMBINATOR_NEXT_SIBLING);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(complex, 2u),
                      ROBO_CSS_COMBINATOR_DESCENDANT);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Pseudo-classes                                                      */
/* ------------------------------------------------------------------ */

static void test_css_select_structural_pseudo_names(void)
{
    static const char *cases[] = {":root", ":first-child", ":last-child", ":only-child"};
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple;

        css_select_parse(cases[i], &list);
        simple = css_select_simple(&list, 0u, 0u, 0u);
        ROBO_CHECK_EQ_INT(simple->kind, ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS);
        ROBO_CHECK_EQ_INT(simple->name.length, strlen(cases[i]) - 1u);
        ROBO_CHECK_EQ_INT(robo_css_slice_is_absent(simple->argument), 1);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_select_nth_child_forms(void)
{
    static const struct {
        const char *text;
        int a;
        int b;
    } cases[] = {
        {":nth-child(2n+1)", 2, 1},
        {":nth-child(odd)", 2, 1},
        {":nth-child(even)", 2, 0},
        {":nth-child(3)", 0, 3},
        {":nth-child(n)", 1, 0},
        {":nth-child(-n+3)", -1, 3},
        {":nth-child(2n)", 2, 0},
        {":nth-child( 2n + 1 )", 2, 1},
        {":nth-child(-3n-2)", -3, -2},
        {":nth-child(+2n)", 2, 0},
        {":nth-child(n-1)", 1, -1}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple;

        css_select_parse(cases[i].text, &list);
        simple = css_select_simple(&list, 0u, 0u, 0u);
        ROBO_CHECK_EQ_INT(simple->kind, ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS);
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_has_nth(simple), 1);
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_nth_a(simple), cases[i].a);
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_nth_b(simple), cases[i].b);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_select_nth_last_child(void)
{
    robo_css_selector_list_t list;
    const robo_css_simple_selector_t *simple;

    css_select_parse("li:nth-last-child(2)", &list);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 2u);
    simple = css_select_simple(&list, 0u, 0u, 1u);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_is_pseudo_named(simple, "nth-last-child"),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_nth_a(simple), 0);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_nth_b(simple), 2);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_pseudo_name_case_insensitive(void)
{
    robo_css_selector_list_t list;
    const robo_css_simple_selector_t *simple;

    css_select_parse(":FIRST-CHILD", &list);
    simple = css_select_simple(&list, 0u, 0u, 0u);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_is_pseudo_named(simple, "first-child"),
                      1);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_not_parses_its_argument(void)
{
    robo_css_selector_list_t list;
    const robo_css_compound_selector_t *compound;
    const robo_css_simple_selector_t *simple;
    const robo_css_selector_list_t *argument;
    char text[32];

    css_select_parse("p:not(.muted)", &list);
    compound = robo_css_complex_selector_compound_at(
        robo_css_selector_list_at(&list, 0u), 0u);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(compound), 2u);
    simple = robo_css_compound_selector_at(compound, 1u);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_is_pseudo_named(simple, "not"), 1);

    /* The raw argument is kept as written… */
    css_select_text(simple->argument, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, ".muted"), 0);

    /* …and the parsed form is a real selector list owned by the compound. */
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_nested_count(compound), 1u);
    argument = robo_css_compound_selector_argument_of(compound, 1u);
    ROBO_CHECK_EQ_INT(argument != NULL, 1);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(argument), 1u);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_nested_index(simple), 0u);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_not_accepts_a_list_and_a_complex(void)
{
    {
        robo_css_selector_list_t list;
        const robo_css_compound_selector_t *compound;

        css_select_parse(":not(.a, .b)", &list);
        compound = robo_css_complex_selector_compound_at(
            robo_css_selector_list_at(&list, 0u), 0u);
        ROBO_CHECK_EQ_INT(robo_css_selector_list_count(
                              robo_css_compound_selector_argument_of(compound, 0u)),
                          2u);
        robo_css_selector_list_dispose(&list);
    }
    {
        robo_css_selector_list_t list;
        const robo_css_compound_selector_t *compound;
        const robo_css_selector_list_t *argument;

        css_select_parse(":not(div > p)", &list);
        compound = robo_css_complex_selector_compound_at(
            robo_css_selector_list_at(&list, 0u), 0u);
        argument = robo_css_compound_selector_argument_of(compound, 0u);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(
                              robo_css_selector_list_at(argument, 0u)),
                          2u);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(
                              robo_css_selector_list_at(argument, 0u), 0u),
                          ROBO_CSS_COMBINATOR_CHILD);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_select_not_nests(void)
{
    robo_css_selector_list_t list;
    const robo_css_compound_selector_t *outer;
    const robo_css_selector_list_t *level_one;
    const robo_css_compound_selector_t *inner;
    const robo_css_selector_list_t *level_two;

    css_select_parse(":not(:not(.a))", &list);
    outer = robo_css_complex_selector_compound_at(
        robo_css_selector_list_at(&list, 0u), 0u);
    level_one = robo_css_compound_selector_argument_of(outer, 0u);
    ROBO_CHECK_EQ_INT(level_one != NULL, 1);
    inner = robo_css_complex_selector_compound_at(
        robo_css_selector_list_at(level_one, 0u), 0u);
    level_two = robo_css_compound_selector_argument_of(inner, 0u);
    ROBO_CHECK_EQ_INT(level_two != NULL, 1);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(level_two), 1u);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(
                          robo_css_complex_selector_compound_at(
                              robo_css_selector_list_at(level_two, 0u), 0u)),
                      1u);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_pseudo_elements(void)
{
    static const char *cases[] = {"::before", "::after", "::first-line", "::first-letter"};
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_css_selector_list_t list;
        const robo_css_simple_selector_t *simple;
        char text[32];

        css_select_parse(cases[i], &list);
        simple = css_select_simple(&list, 0u, 0u, 0u);
        ROBO_CHECK_EQ_INT(simple->kind, ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT);
        css_select_text(simple->name, text, sizeof(text));
        ROBO_CHECK_EQ_INT(strcmp(text, cases[i] + 2), 0);
        robo_css_selector_list_dispose(&list);
    }
}

static void test_css_select_pseudo_element_differs_from_pseudo_class(void)
{
    robo_css_selector_list_t list;

    /* The colon count is the rule, with one deliberate exception: the four
     * pseudo-elements CSS2 spelled with a single colon are still pseudo-elements
     * in that spelling — CSS Selectors 3 says so, every engine accepts it (Blink
     * has a dedicated check for exactly `before`, `after`, `first-letter` and
     * `first-line`), and reading `:before` as a pseudo-*class* would make
     * generated content into a condition nothing can satisfy. */
    css_select_parse("::before", &list);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT);
    robo_css_selector_list_dispose(&list);

    css_select_parse(":before", &list);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT);
    robo_css_selector_list_dispose(&list);

    /* The exception is a closed list, and it is case-insensitive like every
     * other pseudo name. */
    css_select_parse(":FIRST-LETTER", &list);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT);
    robo_css_selector_list_dispose(&list);

    css_select_parse(":hover", &list);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS);
    robo_css_selector_list_dispose(&list);

    css_select_parse(":first-child", &list);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS);
    robo_css_selector_list_dispose(&list);

    /* `:before` takes no argument, so a functional spelling of the same name
     * stays a pseudo-class rather than becoming generated content. */
    css_select_parse(":before(x)", &list);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_pseudo_element_in_compound(void)
{
    robo_css_selector_list_t list;

    css_select_parse("p.intro::first-line", &list);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 3u);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 2u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_pseudo_class_case(void)
{
    robo_css_selector_list_t list;
    const robo_css_compound_selector_t *compound;

    /* `:NOT(...)` is the same pseudo-class, and its argument is still parsed. */
    css_select_parse(":NOT(.a)", &list);
    compound = robo_css_complex_selector_compound_at(
        robo_css_selector_list_at(&list, 0u), 0u);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_nested_count(compound), 1u);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* An+B                                                                */
/* ------------------------------------------------------------------ */

static void test_css_an_plus_b_membership(void)
{
    static const struct {
        int a;
        int b;
        int index;
        int expected;
    } cases[] = {
        {2, 1, 1, 1}, {2, 1, 3, 1}, {2, 1, 5, 1},
        {2, 1, 2, 0}, {2, 1, 0, 0}, {2, 1, -1, 0},
        {0, 3, 3, 1}, {0, 3, 4, 0},
        {1, 0, 1, 1}, {1, 0, 7, 1},
        {-1, 3, 3, 1}, {-1, 3, 1, 1}, {-1, 3, 0, 0}, {-1, 3, 4, 0},
        {-2, 4, 4, 1}, {-2, 4, 2, 1}, {-2, 4, 3, 0}, {-2, 4, 6, 0},
        {2, 0, 2, 1}, {2, 0, 4, 1}, {2, 0, 0, 0}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_css_an_plus_b_t form;

        form.a = cases[i].a;
        form.b = cases[i].b;
        ROBO_CHECK_EQ_INT(robo_css_an_plus_b_matches(&form, cases[i].index),
                          cases[i].expected);
    }
    ROBO_CHECK_EQ_INT(robo_css_an_plus_b_matches(NULL, 1), 0);
}

static void test_css_an_plus_b_parse_table(void)
{
    static const struct {
        const char *text;
        int a;
        int b;
    } good[] = {
        {"odd", 2, 1}, {"even", 2, 0}, {"ODD", 2, 1},
        {"3", 0, 3}, {"-1", 0, -1}, {"+2", 0, 2},
        {"n", 1, 0}, {"N", 1, 0}, {"-n", -1, 0}, {"+n", 1, 0},
        {"2n", 2, 0}, {"-3n", -3, 0},
        {"2n+1", 2, 1}, {"2n + 1", 2, 1}, {"2n +1", 2, 1},
        {"2n-1", 2, -1}, {"2n - 1", 2, -1},
        {"n-1", 1, -1}, {"-n+3", -1, 3}, {" 2n+1 ", 2, 1}, {"\n2n+1\t", 2, 1},
        /* The maximum, and the value that is exactly one past it. Eleven digits
         * is where a naive accumulator overflows a 32-bit `long` — which is a
         * signed-overflow bug rather than the "too many digits" answer — so
         * both ends of the bound are pinned here. */
        {"1000000000", 0, 1000000000}, {"2n+1000000000", 2, 1000000000},
        {"-1000000000", 0, -1000000000}
    };
    static const char *bad[] = {
        "", " ", "2n+", "2 n", "n*n", "oddn", "+", "-", "2n+1+1", "abc", "1.5",
        "2n +", "evenn",
        /* One past the bound, a run long enough to overflow any accumulator,
         * and a bound overflow hiding in the offset. */
        "1000000001", "99999999999", "2n+1000000001", "10000000000"
    };
    size_t i;

    for (i = 0u; i < sizeof(good) / sizeof(good[0]); i++) {
        robo_css_an_plus_b_t form;

        form.a = 99;
        form.b = 99;
        ROBO_CHECK_EQ_INT(robo_css_an_plus_b_parse(good[i].text,
                                                  strlen(good[i].text), &form, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(form.a, good[i].a);
        ROBO_CHECK_EQ_INT(form.b, good[i].b);
    }
    for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        robo_css_an_plus_b_t form;

        ROBO_CHECK_EQ_INT(robo_css_an_plus_b_parse(bad[i], strlen(bad[i]), &form,
                                                  NULL),
                          ROBO_PARSE_ERROR);
    }
    ROBO_CHECK_EQ_INT(robo_css_an_plus_b_parse("2", 1u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* Names and the arena                                                 */
/* ------------------------------------------------------------------ */

static void test_css_select_simple_ranges_cover_their_sigils(void)
{
    /* `.a#b[x]:root` — a diagnostic that underlined only `a` of `.a` would
     * point at the wrong column, so every simple selector's range must cover the
     * text that introduced it: the dot, the hash, the brackets, the colons. The
     * spans here are the whole point of the test; the byte offsets are counted
     * from the literal, not from what the parser happens to report. */
    robo_css_selector_list_t list;
    static const struct {
        size_t index;
        size_t begin;
        size_t end;
    } spans[] = {
        {0u, 0u, 2u},   /* .a     */
        {1u, 2u, 4u},   /* #b     */
        {2u, 4u, 7u},   /* [x]    */
        {3u, 7u, 12u}   /* :root  */
    };
    size_t i;

    css_select_parse(".a#b[x]:root", &list);
    ROBO_CHECK_EQ_INT(css_select_first_compound_count(&list), 4u);
    for (i = 0u; i < sizeof(spans) / sizeof(spans[0]); i++) {
        const robo_css_simple_selector_t *simple =
            css_select_simple(&list, 0u, 0u, spans[i].index);

        ROBO_CHECK_EQ_INT(simple->range.begin, spans[i].begin);
        ROBO_CHECK_EQ_INT(simple->range.end, spans[i].end);
    }
    /* And the compound inherits its extent from them, so a compound selector
     * begins where its first simple selector does. */
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_at(
                          robo_css_selector_list_at(&list, 0u), 0u)
                          ->range.begin,
                      0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_at(
                          robo_css_selector_list_at(&list, 0u), 0u)
                          ->range.end,
                      12u);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_pseudo_element_range(void)
{
    /* A pseudo-element's two colons are part of it. */
    robo_css_selector_list_t list;
    const robo_css_simple_selector_t *simple;

    css_select_parse("p::before", &list);
    simple = css_select_simple(&list, 0u, 0u, 1u);
    ROBO_CHECK_EQ_INT(simple->kind, ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT);
    ROBO_CHECK_EQ_INT(simple->range.begin, 1u);
    ROBO_CHECK_EQ_INT(simple->range.end, 9u);
    /* The compound spans both simples: from `p` to the end of `::before`. */
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_at(
                          robo_css_selector_list_at(&list, 0u), 0u)
                          ->range.begin,
                      0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_at(
                          robo_css_selector_list_at(&list, 0u), 0u)
                          ->range.end,
                      9u);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_name_arena(void)
{
    robo_css_selector_list_t list;
    char text[64];

    /* Every name in the list lives in the list, so the bytes are readable for
     * as long as the list is — and they are the *resolved* bytes. */
    css_select_parse(".alpha#beta, .gamma\\:delta", &list);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_names(&list) != NULL, 1);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_names_length(&list) > 0u, 1);

    css_select_text(css_select_simple(&list, 1u, 0u, 0u)->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "gamma:delta"), 0);
    robo_css_selector_list_dispose(&list);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_names(&list) == NULL, 1);
}

static void test_css_select_nested_list_owns_its_names(void)
{
    robo_css_selector_list_t list;
    const robo_css_selector_list_t *argument;
    const robo_css_compound_selector_t *compound;
    char text[32];

    /* The nested list is a complete list with its own arena, so its names are
     * readable after the parent's arena has been filled further. */
    css_select_parse(":not(.muted.strong)", &list);
    compound = robo_css_complex_selector_compound_at(
        robo_css_selector_list_at(&list, 0u), 0u);
    argument = robo_css_compound_selector_argument_of(compound, 0u);
    ROBO_CHECK_EQ_INT(argument != NULL, 1);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_names(argument) != NULL, 1);
    css_select_text(css_select_simple(argument, 0u, 0u, 1u)->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "strong"), 0);
    robo_css_selector_list_dispose(&list);
}

static void test_css_select_name_capacity_covers_escapes(void)
{
    robo_css_selector_list_t list;
    char text[32];

    /* A `\0` escape expands: two source bytes become the three bytes of U+FFFD.
     * The pre-reserved arena has to cover that, or interning would have to grow
     * and every name already recorded would move. */
    css_select_parse(".a\\0\\0\\0", &list);
    ROBO_CHECK_EQ_INT(css_select_simple(&list, 0u, 0u, 0u)->name.length, 10u);
    css_select_text(css_select_simple(&list, 0u, 0u, 0u)->name, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, "a\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"), 0);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Rule integration                                                    */
/* ------------------------------------------------------------------ */

static void test_css_select_rule_prelude_becomes_selectors(void)
{
    robo_css_error_t error;
    robo_css_stylesheet_id_t sheet;
    const robo_css_style_rule_t *rule;
    const robo_css_source_t *source;

    sheet = css_parse_sheet("p, .a > b { color: red; }", &error);
    rule = css_sheet_style_rule(sheet, 0u);
    ROBO_CHECK_EQ_INT(rule != NULL, 1);

    /* 9.3 left it empty on purpose; this is what fills it. */
    ROBO_CHECK_EQ_INT(robo_css_style_rule_selector_count(rule), 0u);
    source = robo_css_stylesheet_source(sheet);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors((robo_css_style_rule_t *)rule,
                                                         source, &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 0);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_selector_count(rule), 2u);

    {
        const robo_css_selector_list_t *selectors = robo_css_style_rule_selectors(rule);

        char text[16];

        ROBO_CHECK_EQ_INT(robo_css_selector_list_count(selectors), 2u);
        ROBO_CHECK_EQ_INT(css_select_simple(selectors, 0u, 0u, 0u)->kind,
                          ROBO_CSS_SIMPLE_SELECTOR_TYPE);
        css_select_text(css_select_simple(selectors, 0u, 0u, 0u)->name, text,
                        sizeof(text));
        ROBO_CHECK_EQ_INT(strcmp(text, "p"), 0);
        /* The prelude's own range is the source's coordinate system, so the
         * selector's range starts where the prelude does. */
        ROBO_CHECK_EQ_INT(robo_css_selector_list_at(selectors, 0u)->range.begin,
                          rule->prelude_range.begin);
        ROBO_CHECK_EQ_INT(robo_css_selector_list_at(selectors, 1u)->range.begin,
                          3u);
    }
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_select_rule_reparse_is_idempotent(void)
{
    robo_css_error_t error;
    robo_css_stylesheet_id_t sheet;
    const robo_css_style_rule_t *rule;
    const robo_css_source_t *source;

    sheet = css_parse_sheet("a.b, c d { color: red; }", &error);
    rule = css_sheet_style_rule(sheet, 0u);
    source = robo_css_stylesheet_source(sheet);

    ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors((robo_css_style_rule_t *)rule,
                                                         source, &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors((robo_css_style_rule_t *)rule,
                                                         source, &error, NULL),
                      ROBO_OK);
    /* Parsing twice replaces rather than accumulates — and does not leak the
     * list it replaced. */
    ROBO_CHECK_EQ_INT(robo_css_style_rule_selector_count(rule), 2u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_select_rule_with_bad_prelude_keeps_the_rule(void)
{
    robo_css_error_t error;
    robo_css_stylesheet_id_t sheet;
    const robo_css_style_rule_t *rule;
    const robo_css_source_t *source;

    sheet = css_parse_sheet("p > { color: red; }", &error);
    rule = css_sheet_style_rule(sheet, 0u);
    ROBO_CHECK_EQ_INT(rule != NULL, 1);
    source = robo_css_stylesheet_source(sheet);

    robo_css_error_init(&error);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors((robo_css_style_rule_t *)rule,
                                                         source, &error, NULL),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 1);
    /* The rule survives with an empty selector list and its declarations: a
     * prelude nobody can read does not delete the rule. */
    ROBO_CHECK_EQ_INT(robo_css_style_rule_selector_count(rule), 0u);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_declaration_count(rule), 1u);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_select_rule_scale(void)
{
    robo_css_error_t error;
    robo_css_stylesheet_id_t sheet;
    const robo_css_source_t *source;
    size_t index;

    /* Every rule of a many-rule sheet, including at-rules and malformed ones,
     * which is where a range that is off by a byte shows up. */
    sheet = css_parse_sheet(
        "h1 { color: red; }\n"
        ".button { padding: 10px; }\n"
        "#title { font-size: 24px; }\n"
        "input[type=\"text\"] { width: 200px; }\n"
        "body { background: white; }\n",
        &error);
    source = robo_css_stylesheet_source(sheet);
    for (index = 0u; index < robo_css_stylesheet_rule_count(sheet); index++) {
        const robo_css_style_rule_t *rule = css_sheet_style_rule(sheet, index);

        if (!rule) {
            continue;
        }
        robo_css_error_init(&error);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors(
                              (robo_css_style_rule_t *)rule, source, &error, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_selector_count(rule), 1u);
    }
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
}

static void test_css_select_parse_range_bounds(void)
{
    robo_css_source_t source;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    robo_css_error_t error;

    css_source_of_string(&source, "div p");
    robo_css_error_init(&error);
    /* A range past the end is refused, and the output stays a valid empty list
     * rather than an uninitialised one. */
    ROBO_CHECK_EQ_INT(robo_css_selector_list_parse_range(
                          &list, &source, robo_css_range_span(0u, 64u), &error, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 1);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 0u);
    robo_css_selector_list_dispose(&list);

    robo_css_error_init(&error);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_parse_range(
                          &list, &source, robo_css_range_span(4u, 1u), &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 1u);
    /* Positions are source positions, not text positions. */
    ROBO_CHECK_EQ_INT(robo_css_selector_list_at(&list, 0u)->range.begin, 4u);
    robo_css_selector_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void test_css_select_lifetime_repeat(void)
{
    int we_enabled = css_allocator_ensure_enabled();
    size_t baseline = robo_mem_debug_live_count();
    size_t i;

    for (i = 0u; i < 200u; i++) {
        robo_css_selector_list_t list;

        css_select_parse("div.a#b > ul li:nth-child(2n+1):not(.x, .y)", &list);
        ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 1u);
        robo_css_selector_list_dispose(&list);
    }
    ROBO_CHECK_EQ_INT(robo_mem_debug_live_count(), baseline);
    css_allocator_restore(we_enabled);
}

static void test_css_select_lifetime_failed_parse_releases_everything(void)
{
    int we_enabled = css_allocator_ensure_enabled();
    size_t baseline = robo_mem_debug_live_count();
    size_t i;

    /* A rejected selector must leave nothing behind: failures happen in the
     * middle of a partially built tree, which is where leaks live. */
    for (i = 0u; i < 200u; i++) {
        robo_css_selector_list_t list = ROBO_CSS_CLEARED;
        robo_css_error_t error;

        robo_css_error_init(&error);
        ROBO_CHECK_EQ_INT(robo_css_selector_list_parse_text(
                              &list, "div > ul li:nth-child(", &error, NULL),
                          ROBO_PARSE_ERROR);
        robo_css_selector_list_dispose(&list);
    }
    ROBO_CHECK_EQ_INT(robo_mem_debug_live_count(), baseline);
    css_allocator_restore(we_enabled);
}

static void test_css_select_api_arguments(void)
{
    robo_css_selector_list_t list;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_selector_list_parse_text(NULL, "p", NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_parse_text(&list, NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_parse_slice(&list,
                                                        robo_css_slice_empty(), NULL,
                                                        NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_parse_range(&list, NULL,
                                                        robo_css_range_at(0u), NULL,
                                                        NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors(NULL, NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
}

void robo_test_register_css_select_parse(void);

void robo_test_register_css_select_parse(void)
{
    robo_test_register("css.select.parse.single_type", test_css_select_single_type);
    robo_test_register("css.select.parse.list_order", test_css_select_list_order);
    robo_test_register("css.select.parse.list_whitespace_around_commas",
                       test_css_select_list_whitespace_around_commas);
    robo_test_register("css.select.parse.type_keeps_spelling",
                       test_css_select_type_keeps_spelling);
    robo_test_register("css.select.parse.type_unicode", test_css_select_type_unicode);
    robo_test_register("css.select.parse.type_escaped", test_css_select_type_escaped);
    robo_test_register("css.select.parse.universal_alone",
                       test_css_select_universal_alone);
    robo_test_register("css.select.parse.universal_in_compound",
                       test_css_select_universal_in_compound);
    robo_test_register("css.select.parse.universal_descendant",
                       test_css_select_universal_descendant);
    robo_test_register("css.select.parse.id_and_class", test_css_select_id_and_class);
    robo_test_register("css.select.parse.class_multiple_in_one_compound",
                       test_css_select_class_multiple_in_one_compound);
    robo_test_register("css.select.parse.class_hyphen_and_underscore",
                       test_css_select_class_hyphen_and_underscore);
    robo_test_register("css.select.parse.escaped_identifiers",
                       test_css_select_escaped_identifiers);
    robo_test_register("css.select.parse.id_escaped_digits",
                       test_css_select_id_escaped_digits);
    robo_test_register("css.select.parse.attribute_presence",
                       test_css_select_attribute_presence);
    robo_test_register("css.select.parse.attribute_all_operators",
                       test_css_select_attribute_all_operators);
    robo_test_register("css.select.parse.attribute_value_forms",
                       test_css_select_attribute_value_forms);
    robo_test_register("css.select.parse.attribute_case_flags",
                       test_css_select_attribute_case_flags);
    robo_test_register("css.select.parse.attribute_whitespace_and_escapes",
                       test_css_select_attribute_whitespace_and_escapes);
    robo_test_register("css.select.parse.attribute_in_compound",
                       test_css_select_attribute_in_compound);
    robo_test_register("css.select.parse.combinators_all",
                       test_css_select_combinators_all);
    robo_test_register("css.select.parse.combinator_whitespace_forms",
                       test_css_select_combinator_whitespace_forms);
    robo_test_register("css.select.parse.comment_is_not_a_combinator",
                       test_css_select_comment_is_not_a_combinator);
    robo_test_register("css.select.parse.complex_selector",
                       test_css_select_complex_selector);
    robo_test_register("css.select.parse.complex_four_compounds",
                       test_css_select_complex_four_compounds);
    robo_test_register("css.select.parse.structural_pseudo_names",
                       test_css_select_structural_pseudo_names);
    robo_test_register("css.select.parse.nth_child_forms",
                       test_css_select_nth_child_forms);
    robo_test_register("css.select.parse.nth_last_child",
                       test_css_select_nth_last_child);
    robo_test_register("css.select.parse.pseudo_name_case_insensitive",
                       test_css_select_pseudo_name_case_insensitive);
    robo_test_register("css.select.parse.not_parses_its_argument",
                       test_css_select_not_parses_its_argument);
    robo_test_register("css.select.parse.not_accepts_a_list_and_a_complex",
                       test_css_select_not_accepts_a_list_and_a_complex);
    robo_test_register("css.select.parse.not_nests", test_css_select_not_nests);
    robo_test_register("css.select.parse.pseudo_elements",
                       test_css_select_pseudo_elements);
    robo_test_register("css.select.parse.pseudo_element_differs_from_pseudo_class",
                       test_css_select_pseudo_element_differs_from_pseudo_class);
    robo_test_register("css.select.parse.pseudo_element_in_compound",
                       test_css_select_pseudo_element_in_compound);
    robo_test_register("css.select.parse.pseudo_class_case",
                       test_css_select_pseudo_class_case);
    robo_test_register("css.select.parse.an_plus_b_membership",
                       test_css_an_plus_b_membership);
    robo_test_register("css.select.parse.an_plus_b_parse_table",
                       test_css_an_plus_b_parse_table);
    robo_test_register("css.select.parse.simple_ranges_cover_their_sigils",
                       test_css_select_simple_ranges_cover_their_sigils);
    robo_test_register("css.select.parse.pseudo_element_range",
                       test_css_select_pseudo_element_range);
    robo_test_register("css.select.parse.name_arena", test_css_select_name_arena);
    robo_test_register("css.select.parse.nested_list_owns_its_names",
                       test_css_select_nested_list_owns_its_names);
    robo_test_register("css.select.parse.name_capacity_covers_escapes",
                       test_css_select_name_capacity_covers_escapes);
    robo_test_register("css.select.parse.rule_prelude_becomes_selectors",
                       test_css_select_rule_prelude_becomes_selectors);
    robo_test_register("css.select.parse.rule_reparse_is_idempotent",
                       test_css_select_rule_reparse_is_idempotent);
    robo_test_register("css.select.parse.rule_with_bad_prelude_keeps_the_rule",
                       test_css_select_rule_with_bad_prelude_keeps_the_rule);
    robo_test_register("css.select.parse.rule_scale", test_css_select_rule_scale);
    robo_test_register("css.select.parse.parse_range_bounds",
                       test_css_select_parse_range_bounds);
    robo_test_register("css.select.parse.lifetime_repeat",
                       test_css_select_lifetime_repeat);
    robo_test_register("css.select.parse.lifetime_failed_parse_releases_everything",
                       test_css_select_lifetime_failed_parse_releases_everything);
    robo_test_register("css.select.parse.api_arguments", test_css_select_api_arguments);
}
