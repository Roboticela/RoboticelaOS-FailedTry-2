/*
 * RoboCSS Phase 9.1 selector suite:
 * the simple/compound/complex/list structure and its vocabulary.
 *
 * Nothing here matches anything. The assertions are about what a selector
 * *records* — which is the whole contract this phase offers — and about the
 * ownership rules the 9.4 matcher and the 9.6 specificity calculation will be
 * written against.
 */

#include "RoboCssTestSupport.h"

static void test_css_selector_vocabulary(void)
{
    robo_css_simple_selector_kind_t kind;
    robo_css_attribute_match_t match;
    robo_css_attribute_case_t flag;
    robo_css_combinator_t combinator;

    for (kind = ROBO_CSS_SIMPLE_SELECTOR_NONE;
         kind < (robo_css_simple_selector_kind_t)ROBO_CSS_SIMPLE_SELECTOR_KIND_COUNT;
         kind = (robo_css_simple_selector_kind_t)(kind + 1)) {
        ROBO_CHECK(robo_css_simple_selector_kind_is_known(kind));
        ROBO_CHECK(strcmp(robo_css_simple_selector_kind_name(kind), "unknown") != 0);
    }
    ROBO_CHECK(!robo_css_simple_selector_kind_is_known(
        (robo_css_simple_selector_kind_t)77));
    ROBO_CHECK_STR_EQ(robo_css_simple_selector_kind_name(
                          (robo_css_simple_selector_kind_t)77),
                      "unknown");

    /* All seven comparison operators CSS defines, plus "no comparison". */
    for (match = ROBO_CSS_ATTRIBUTE_MATCH_NONE;
         match < (robo_css_attribute_match_t)ROBO_CSS_ATTRIBUTE_MATCH_COUNT;
         match = (robo_css_attribute_match_t)(match + 1)) {
        ROBO_CHECK(robo_css_attribute_match_is_known(match));
        ROBO_CHECK(strcmp(robo_css_attribute_match_name(match), "unknown") != 0);
    }
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_ATTRIBUTE_MATCH_COUNT, 7);
    ROBO_CHECK_STR_EQ(robo_css_attribute_match_name(ROBO_CSS_ATTRIBUTE_MATCH_INCLUDES),
                      "includes");
    ROBO_CHECK_STR_EQ(robo_css_attribute_match_name(ROBO_CSS_ATTRIBUTE_MATCH_DASH),
                      "dash");
    ROBO_CHECK(!robo_css_attribute_match_is_known((robo_css_attribute_match_t)42));

    for (flag = ROBO_CSS_ATTRIBUTE_CASE_DEFAULT;
         flag < (robo_css_attribute_case_t)ROBO_CSS_ATTRIBUTE_CASE_COUNT;
         flag = (robo_css_attribute_case_t)(flag + 1)) {
        ROBO_CHECK(robo_css_attribute_case_is_known(flag));
        ROBO_CHECK(strcmp(robo_css_attribute_case_name(flag), "unknown") != 0);
    }
    ROBO_CHECK_STR_EQ(robo_css_attribute_case_name(
                          ROBO_CSS_ATTRIBUTE_CASE_INSENSITIVE),
                      "insensitive");
    ROBO_CHECK(!robo_css_attribute_case_is_known((robo_css_attribute_case_t)42));

    /* Five combinators plus "none"; the column combinator is included because
     * modern CSS has it and a model that omitted it would need a redesign. */
    for (combinator = ROBO_CSS_COMBINATOR_NONE;
         combinator < (robo_css_combinator_t)ROBO_CSS_COMBINATOR_COUNT;
         combinator = (robo_css_combinator_t)(combinator + 1)) {
        ROBO_CHECK(robo_css_combinator_is_known(combinator));
        ROBO_CHECK(strcmp(robo_css_combinator_name(combinator), "unknown") != 0);
    }
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_COMBINATOR_COUNT, 6);
    ROBO_CHECK_STR_EQ(robo_css_combinator_name(ROBO_CSS_COMBINATOR_NEXT_SIBLING),
                      "next-sibling");
    ROBO_CHECK_STR_EQ(robo_css_combinator_name(ROBO_CSS_COMBINATOR_COLUMN), "column");
    ROBO_CHECK(!robo_css_combinator_is_known((robo_css_combinator_t)42));
}

static void test_css_selector_simple(void)
{
    robo_css_simple_selector_t selector = ROBO_CSS_CLEARED;
    robo_css_diag_t diag;

    css_fixture();
    robo_css_simple_selector_init(&selector);
    ROBO_CHECK(!robo_css_simple_selector_is_set(&selector));
    ROBO_CHECK_EQ_INT(selector.kind, ROBO_CSS_SIMPLE_SELECTOR_NONE);

    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&selector,
                                                    ROBO_CSS_SIMPLE_SELECTOR_TYPE,
                                                    robo_css_slice_from_text("div"),
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_simple_selector_is_set(&selector));
    ROBO_CHECK_EQ_INT(selector.kind, ROBO_CSS_SIMPLE_SELECTOR_TYPE);
    ROBO_CHECK(robo_css_slice_equals_ascii(selector.name, "div"));

    /* Making a selector again replaces the previous one rather than stacking. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&selector,
                                                    ROBO_CSS_SIMPLE_SELECTOR_CLASS,
                                                    robo_css_slice_from_text("card"),
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(selector.kind, ROBO_CSS_SIMPLE_SELECTOR_CLASS);
    ROBO_CHECK(robo_css_slice_equals_ascii(selector.name, "card"));

    /* `*` carries no name; the kinds that name something require one. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&selector,
                                                    ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL,
                                                    robo_css_slice_empty(), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(selector.kind, ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&selector,
                                                    ROBO_CSS_SIMPLE_SELECTOR_ID,
                                                    robo_css_slice_empty(), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(selector.kind, ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL);

    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&selector,
                                                    ROBO_CSS_SIMPLE_SELECTOR_NONE,
                                                    robo_css_slice_from_text("x"), &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_SELECTOR);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&selector,
                                                    (robo_css_simple_selector_kind_t)77,
                                                    robo_css_slice_from_text("x"), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(NULL,
                                                    ROBO_CSS_SIMPLE_SELECTOR_TYPE,
                                                    robo_css_slice_from_text("x"), NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_simple_selector_dispose(&selector);
    ROBO_CHECK(!robo_css_simple_selector_is_set(&selector));
    robo_css_simple_selector_dispose(NULL);
    ROBO_CHECK(!robo_css_simple_selector_is_set(NULL));
    robo_css_simple_selector_init(NULL);
}

static void test_css_selector_attribute(void)
{
    robo_css_simple_selector_t selector = ROBO_CSS_CLEARED;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&selector,
                                                    ROBO_CSS_SIMPLE_SELECTOR_ATTRIBUTE,
                                                    robo_css_slice_empty(), NULL),
                      ROBO_OK);

    /* Presence: `[disabled]` compares against nothing. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                          &selector, robo_css_slice_from_text("disabled"),
                          ROBO_CSS_ATTRIBUTE_MATCH_NONE, robo_css_slice_empty(),
                          ROBO_CSS_ATTRIBUTE_CASE_DEFAULT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(selector.match, ROBO_CSS_ATTRIBUTE_MATCH_NONE);
    ROBO_CHECK(robo_css_slice_is_absent(selector.attribute_value));

    /* `[type=text i]`: a comparison, its value, and its case flag. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                          &selector, robo_css_slice_from_text("type"),
                          ROBO_CSS_ATTRIBUTE_MATCH_EQUALS,
                          robo_css_slice_from_text("text"),
                          ROBO_CSS_ATTRIBUTE_CASE_INSENSITIVE, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(selector.match, ROBO_CSS_ATTRIBUTE_MATCH_EQUALS);
    ROBO_CHECK(robo_css_slice_equals_ascii(selector.attribute, "type"));
    ROBO_CHECK(robo_css_slice_equals_ascii(selector.attribute_value, "text"));
    ROBO_CHECK_EQ_INT(selector.case_flag, ROBO_CSS_ATTRIBUTE_CASE_INSENSITIVE);
    selector.value_was_string = 1;
    ROBO_CHECK_EQ_INT(selector.value_was_string, 1);

    /* `[a=""]` is not `[a]`: this comparison has a value that is present and
     * empty, and it is recorded as an empty slice — not as an absent one. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                          &selector, robo_css_slice_from_text("a"),
                          ROBO_CSS_ATTRIBUTE_MATCH_EQUALS,
                          robo_css_slice_from_bytes((const uint8_t *)"", 0u),
                          ROBO_CSS_ATTRIBUTE_CASE_DEFAULT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(selector.match, ROBO_CSS_ATTRIBUTE_MATCH_EQUALS);
    ROBO_CHECK(robo_css_slice_is_empty(selector.attribute_value));
    ROBO_CHECK(!robo_css_slice_is_absent(selector.attribute_value));
    ROBO_CHECK_EQ_INT(selector.attribute_value.length, 0u);

    /* Each rejection below must leave the record it already described alone,
     * so the checks that follow each one ask what the selector still says. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                          &selector, robo_css_slice_empty(),
                          ROBO_CSS_ATTRIBUTE_MATCH_NONE, robo_css_slice_empty(),
                          ROBO_CSS_ATTRIBUTE_CASE_DEFAULT, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                          &selector, robo_css_slice_from_text("a"),
                          (robo_css_attribute_match_t)42, robo_css_slice_empty(),
                          ROBO_CSS_ATTRIBUTE_CASE_DEFAULT, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                          &selector, robo_css_slice_from_text("a"),
                          ROBO_CSS_ATTRIBUTE_MATCH_EQUALS,
                          robo_css_slice_from_bytes((const uint8_t *)"", 0u),
                          (robo_css_attribute_case_t)42, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(selector.case_flag, ROBO_CSS_ATTRIBUTE_CASE_DEFAULT);

    /* A comparison with no value at all is refused: absent is not empty, and
     * only the first form may ever appear in a selector with an operator. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                          &selector, robo_css_slice_from_text("a"),
                          ROBO_CSS_ATTRIBUTE_MATCH_PREFIX, robo_css_slice_empty(),
                          ROBO_CSS_ATTRIBUTE_CASE_DEFAULT, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(selector.match, ROBO_CSS_ATTRIBUTE_MATCH_EQUALS);

    /* Presence is the absence of a comparison, so `[a]` carries no value. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                          &selector, robo_css_slice_from_text("a"),
                          ROBO_CSS_ATTRIBUTE_MATCH_NONE, robo_css_slice_empty(),
                          ROBO_CSS_ATTRIBUTE_CASE_DEFAULT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(selector.match, ROBO_CSS_ATTRIBUTE_MATCH_NONE);
    ROBO_CHECK(robo_css_slice_is_absent(selector.attribute_value));

    /* Setting an attribute on a non-attribute selector is refused, and the
     * record it already described is untouched. */
    {
        robo_css_simple_selector_t type_selector = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&type_selector,
                                                        ROBO_CSS_SIMPLE_SELECTOR_TYPE,
                                                        robo_css_slice_from_text("input"),
                                                        NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                              &type_selector, robo_css_slice_from_text("type"),
                              ROBO_CSS_ATTRIBUTE_MATCH_EQUALS,
                              robo_css_slice_from_text("text"),
                              ROBO_CSS_ATTRIBUTE_CASE_DEFAULT, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK(robo_css_slice_is_empty(type_selector.attribute));
        ROBO_CHECK(robo_css_slice_equals_ascii(type_selector.name, "input"));
    }

    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                          NULL, robo_css_slice_from_text("a"),
                          ROBO_CSS_ATTRIBUTE_MATCH_NONE, robo_css_slice_empty(),
                          ROBO_CSS_ATTRIBUTE_CASE_DEFAULT, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_simple_selector_dispose(&selector);
}

static void test_css_selector_pseudo_argument(void)
{
    robo_css_simple_selector_t selector = ROBO_CSS_CLEARED;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&selector,
                                                    ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS,
                                                    robo_css_slice_from_text("nth-child"),
                                                    NULL),
                      ROBO_OK);
    /* The argument stays one raw slice: whether it is an An+B expression or a
     * selector list depends on which pseudo-class it is, and splitting it here
     * would be parsing. */
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_argument(
                          &selector, robo_css_slice_from_text("2n+1"), NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_slice_equals_ascii(selector.argument, "2n+1"));
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_argument(&selector,
                                                            robo_css_slice_empty(),
                                                            NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_slice_is_empty(selector.argument));

    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&selector,
                                                    ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT,
                                                    robo_css_slice_from_text("marker"),
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_argument(
                          &selector, robo_css_slice_from_text("x"), NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&selector,
                                                    ROBO_CSS_SIMPLE_SELECTOR_CLASS,
                                                    robo_css_slice_from_text("card"),
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_argument(
                          &selector, robo_css_slice_from_text("x"), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_argument(NULL,
                                                            robo_css_slice_empty(),
                                                            NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_simple_selector_dispose(&selector);
}

static void test_css_selector_compound(void)
{
    robo_css_simple_selector_t simple = ROBO_CSS_CLEARED;
    robo_css_compound_selector_t compound = ROBO_CSS_CLEARED;

    css_fixture();
    robo_css_compound_selector_init(&compound);
    ROBO_CHECK(!robo_css_compound_selector_is_set(&compound));
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(&compound), 0u);
    ROBO_CHECK(robo_css_compound_selector_at(&compound, 0u) == NULL);

    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&simple,
                                                    ROBO_CSS_SIMPLE_SELECTOR_TYPE,
                                                    robo_css_slice_from_text("input"),
                                                    NULL),
                      ROBO_OK);
    simple.range = robo_css_range_span(0u, 5u);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&simple,
                                                    ROBO_CSS_SIMPLE_SELECTOR_ATTRIBUTE,
                                                    robo_css_slice_empty(), NULL),
                      ROBO_OK);
    simple.range = robo_css_range_span(5u, 14u);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_set_attribute(
                          &simple, robo_css_slice_from_text("type"),
                          ROBO_CSS_ATTRIBUTE_MATCH_EQUALS,
                          robo_css_slice_from_text("text"),
                          ROBO_CSS_ATTRIBUTE_CASE_DEFAULT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&simple,
                                                    ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS,
                                                    robo_css_slice_from_text("disabled"),
                                                    NULL),
                      ROBO_OK);
    simple.range = robo_css_range_span(20u, 9u);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple, NULL),
                      ROBO_OK);

    /* Order is preserved as written, and the compound seals a range that spans
     * its members rather than one it guessed. */
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(&compound), 2u);
    ROBO_CHECK(robo_css_compound_selector_is_set(&compound));
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_at(&compound, 0u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_TYPE);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_at(&compound, 1u)->kind,
                      ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS);
    ROBO_CHECK(robo_css_compound_selector_at(&compound, 2u) == NULL);
    robo_css_compound_selector_seal_range(&compound);
    ROBO_CHECK_EQ_INT(compound.range.begin, 0u);
    ROBO_CHECK_EQ_INT(compound.range.end, 29u);

    /* A simple selector is copied in, so the caller's record is still usable. */
    ROBO_CHECK(robo_css_simple_selector_is_set(&simple));
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(&compound), 3u);

    /* An unset simple selector is refused. */
    {
        robo_css_simple_selector_t unset = ROBO_CSS_CLEARED;

        robo_css_simple_selector_init(&unset);
        ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &unset, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(&compound), 3u);
    }
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(NULL, &simple, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_compound_selector_dispose(&compound);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(&compound), 0u);
    robo_css_compound_selector_seal_range(&compound);
    ROBO_CHECK_EQ_INT(compound.range.begin, 0u);
    robo_css_compound_selector_dispose(NULL);
    robo_css_compound_selector_init(NULL);
    ROBO_CHECK(!robo_css_compound_selector_is_set(NULL));
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(NULL), 0u);
    ROBO_CHECK(robo_css_compound_selector_at(NULL, 0u) == NULL);
}

static void test_css_selector_complex(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_complex_selector_t complex = ROBO_CSS_CLEARED;
    robo_css_compound_selector_t compound = ROBO_CSS_CLEARED;
    robo_css_simple_selector_t simple = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "form > input + label");
    robo_css_complex_selector_init(&complex);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(&complex), 0u);
    ROBO_CHECK(!robo_css_complex_selector_is_well_formed(&complex));

    /* Build `form > input + label` the way the parser will: compound,
     * combinator, compound, combinator, compound. */
    {
        static const char *tags[] = {"form", "input", "label"};
        static const robo_css_combinator_t combinators[] = {
            ROBO_CSS_COMBINATOR_CHILD, ROBO_CSS_COMBINATOR_NEXT_SIBLING};
        int i;

        for (i = 0; i < 3; i++) {
            robo_css_compound_selector_init(&compound);
            ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&simple,
                                                            ROBO_CSS_SIMPLE_SELECTOR_TYPE,
                                                            robo_css_slice_from_text(tags[i]),
                                                            NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple,
                                                                NULL),
                              ROBO_OK);
            robo_css_compound_selector_seal_range(&compound);
            /* The compound is moved in: the caller's record is empty afterwards. */
            ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_compound(&complex,
                                                                       &compound,
                                                                       NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(&compound), 0u);
            if (i < 2) {
                ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_combinator(
                                      &complex, combinators[i], NULL),
                                  ROBO_OK);
            }
        }
    }
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(&complex), 3u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_count(&complex), 2u);
    ROBO_CHECK(robo_css_complex_selector_is_well_formed(&complex));
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(&complex, 0u),
                      ROBO_CSS_COMBINATOR_CHILD);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(&complex, 1u),
                      ROBO_CSS_COMBINATOR_NEXT_SIBLING);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_at(&complex, 2u),
                      ROBO_CSS_COMBINATOR_NONE);
    ROBO_CHECK(robo_css_complex_selector_compound_at(&complex, 2u) != NULL);
    ROBO_CHECK(robo_css_complex_selector_compound_at(&complex, 3u) == NULL);

    ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_combinator(
                          &complex, ROBO_CSS_COMBINATOR_NONE, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_combinator(
                          &complex, (robo_css_combinator_t)42, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_combinator(NULL,
                                                                  ROBO_CSS_COMBINATOR_CHILD,
                                                                  NULL),
                      ROBO_INVALID_ARGUMENT);

    /* An empty compound is refused: a selector with no conditions matches
     * everything, which is what `*` is for and must be spelled as such. */
    {
        robo_css_compound_selector_t empty = ROBO_CSS_CLEARED;

        robo_css_compound_selector_init(&empty);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_compound(&complex, &empty,
                                                                    NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(&complex), 3u);
    }
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_compound(NULL, &compound, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_compound(&complex, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* One compound and no combinator is well formed; one compound and one
     * combinator is not, and the check says so rather than being skipped. */
    {
        robo_css_complex_selector_t single = ROBO_CSS_CLEARED;

        robo_css_complex_selector_init(&single);
        robo_css_compound_selector_init(&compound);
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&simple,
                                                        ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL,
                                                        robo_css_slice_empty(), NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_compound(&single, &compound,
                                                                    NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_css_complex_selector_is_well_formed(&single));
        ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_combinator(
                              &single, ROBO_CSS_COMBINATOR_DESCENDANT, NULL),
                          ROBO_OK);
        ROBO_CHECK(!robo_css_complex_selector_is_well_formed(&single));
        robo_css_complex_selector_dispose(&single);
    }

    robo_css_complex_selector_dispose(&complex);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(&complex), 0u);
    robo_css_complex_selector_dispose(NULL);
    robo_css_complex_selector_init(NULL);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_combinator_count(NULL), 0u);
    ROBO_CHECK(robo_css_complex_selector_compound_at(NULL, 0u) == NULL);
    ROBO_CHECK(!robo_css_complex_selector_is_well_formed(NULL));
    robo_css_source_dispose(&source);
}

static void test_css_selector_list(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    robo_css_complex_selector_t complex = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "h1, h2, h3");
    robo_css_selector_list_init(&list);
    ROBO_CHECK(!robo_css_selector_list_is_set(&list));
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 0u);
    ROBO_CHECK(robo_css_selector_list_at(&list, 0u) == NULL);

    /* Two entries, second more specific than the first: a selector list is what
     * a comma-separated rule owns, and 9.6 will hang a specificity off each. */
    css_complex_selector_of(&complex, &source, "h1", NULL);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_append(&list, &complex, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(&complex), 0u);
    css_complex_selector_of(&complex, &source, "h2", "title");
    ROBO_CHECK_EQ_INT(robo_css_selector_list_append(&list, &complex, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 2u);
    ROBO_CHECK(robo_css_selector_list_is_set(&list));
    ROBO_CHECK(robo_css_selector_list_is_well_formed(&list));
    ROBO_CHECK_EQ_INT(robo_css_selector_list_at(&list, 0u)->compound_count, 1u);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_at(&list, 1u)->compound_count, 1u);
    ROBO_CHECK_EQ_INT(
        robo_css_compound_selector_count(
            robo_css_complex_selector_compound_at(robo_css_selector_list_at(&list, 0u),
                                                  0u)),
        1u);
    ROBO_CHECK_EQ_INT(
        robo_css_compound_selector_count(
            robo_css_complex_selector_compound_at(robo_css_selector_list_at(&list, 1u),
                                                  0u)),
        2u);
    ROBO_CHECK(robo_css_selector_list_at(&list, 2u) == NULL);

    {
        robo_css_complex_selector_t empty = ROBO_CSS_CLEARED;

        robo_css_complex_selector_init(&empty);
        ROBO_CHECK_EQ_INT(robo_css_selector_list_append(&list, &empty, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 2u);
    }
    ROBO_CHECK_EQ_INT(robo_css_selector_list_append(NULL, &complex, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_append(&list, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(!robo_css_selector_list_is_well_formed(NULL));
    ROBO_CHECK(!robo_css_selector_list_is_set(NULL));
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(NULL), 0u);
    ROBO_CHECK(robo_css_selector_list_at(NULL, 0u) == NULL);

    robo_css_selector_list_dispose(&list);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 0u);
    robo_css_selector_list_dispose(NULL);
    robo_css_selector_list_init(NULL);
    robo_css_source_dispose(&source);
}

static void test_css_selector_lifetime_balances(void)
{
    int we_enabled;
    size_t baseline;

    css_fixture();
    we_enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();
    {
        int i;

        for (i = 0; i < 24; i++) {
            robo_css_selector_list_t list = ROBO_CSS_CLEARED;
            robo_css_complex_selector_t complex = ROBO_CSS_CLEARED;
            robo_css_compound_selector_t compound = ROBO_CSS_CLEARED;
            robo_css_simple_selector_t simple = ROBO_CSS_CLEARED;
            int j;

            robo_css_selector_list_init(&list);
            for (j = 0; j < 3; j++) {
                robo_css_compound_selector_init(&compound);
                ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(
                                      &simple, ROBO_CSS_SIMPLE_SELECTOR_TYPE,
                                      robo_css_slice_from_text("p"), NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple,
                                                                    NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(
                                      &simple, ROBO_CSS_SIMPLE_SELECTOR_CLASS,
                                      robo_css_slice_from_text("note"), NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple,
                                                                    NULL),
                                  ROBO_OK);
                robo_css_complex_selector_init(&complex);
                ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_compound(&complex,
                                                                            &compound,
                                                                            NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_css_selector_list_append(&list, &complex, NULL),
                                  ROBO_OK);
            }
            ROBO_CHECK(robo_css_selector_list_is_well_formed(&list));
            ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&list), 3u);
            robo_css_selector_list_dispose(&list);
            ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        }
    }
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(we_enabled);
}

void robo_test_register_css_selector(void);

void robo_test_register_css_selector(void)
{
    robo_test_register("css.selector.vocabulary", test_css_selector_vocabulary);
    robo_test_register("css.selector.simple", test_css_selector_simple);
    robo_test_register("css.selector.attribute", test_css_selector_attribute);
    robo_test_register("css.selector.pseudo_argument",
                       test_css_selector_pseudo_argument);
    robo_test_register("css.selector.compound", test_css_selector_compound);
    robo_test_register("css.selector.complex", test_css_selector_complex);
    robo_test_register("css.selector.list", test_css_selector_list);
    robo_test_register("css.selector.lifetime_balances",
                       test_css_selector_lifetime_balances);
}
