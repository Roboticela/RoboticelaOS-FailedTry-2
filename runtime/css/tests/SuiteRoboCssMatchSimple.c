/*
 * RoboCSS simple and compound selector matching (Phase 9.4) — css.select.match.*
 *
 * These tests are written against hosts, not documents, because that is what the
 * matcher is written against: the seam in RoboCSSStyle.h is the whole contract,
 * so every case here is a statement about which element *view* satisfies which
 * selector. The DOM integration suite separately re-runs the same selectors
 * through the real RoboHTML tree, which is what makes these two independent
 * rather than duplicated.
 *
 * The negatives matter as much as the positives. `[a=""]` matching an absent
 * attribute, `.card` matching `cardigan`, and `DIV` not matching `div` are all
 * mistakes that a "does it match something" test would pass and a real stylesheet
 * would not survive.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Type and universal                                                  */
/* ------------------------------------------------------------------ */

static void test_css_match_type(void)
{
    css_host_t host;
    robo_css_element_ref_t div;

    css_host_init(&host);
    div = css_host_add(&host, "div", "app", NULL);
    css_host_add(&host, "span", NULL, NULL);
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "div", div), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "span", div), 0);
    /* HTML element names are ASCII case-insensitive, and the DOM stores them
     * lowercase, so the author's casing must not decide the match. */
    ROBO_CHECK_EQ_INT(css_match(&host, "DIV", div), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "DiV", div), 1);
    /* The element's own spelling is what it is; the selector is what folds. */
    ROBO_CHECK_EQ_INT(css_match(&host, "div", 2u), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "*", div), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "*", 2u), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "div", ROBO_CSS_ELEMENT_REF_NONE), 0);
}

static void test_css_match_type_unicode(void)
{
    css_host_t host;
    robo_css_element_ref_t element;

    /* A custom element name in Arabic script: byte-exact, and not folded — the
     * case fold is ASCII-only by design, and must not touch non-ASCII bytes. */
    css_host_init(&host);
    element = css_host_add(&host, "\xD9\x82\xD8\xB3\xD9\x85", NULL, NULL);
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "\xD9\x82\xD8\xB3\xD9\x85", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "\xD9\x85", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "*", element), 1);
}

/* ------------------------------------------------------------------ */
/* Class                                                              */
/* ------------------------------------------------------------------ */

static void test_css_match_class(void)
{
    css_host_t host;
    robo_css_element_ref_t card;

    css_host_init(&host);
    card = css_host_add(&host, "div", NULL, "card active");
    css_host_add(&host, "div", NULL, "cardigan");
    css_host_add(&host, "div", NULL, "card");
    css_host_add(&host, "div", NULL, "");
    css_host_add(&host, "div", NULL, NULL);
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, ".card", card), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, ".active", card), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, ".card.active", card), 1);
    /* Multiple simple selectors in one compound is "all of them", so a class
     * the element does not have decides the whole compound. */
    ROBO_CHECK_EQ_INT(css_match(&host, ".card.missing", card), 0);
    /* `cardigan` contains `card` as bytes and is not the class `card`. */
    ROBO_CHECK_EQ_INT(css_match(&host, ".card", 2u), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, ".cardigan", 2u), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, ".card", 3u), 1);
    /* Class names are case-sensitive in HTML. */
    ROBO_CHECK_EQ_INT(css_match(&host, ".Card", card), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, ".CARD", card), 0);
    /* An empty class attribute holds no classes, and neither does none. */
    ROBO_CHECK_EQ_INT(css_match(&host, ".card", 4u), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "*", 4u), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, ".card", 5u), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "*", 5u), 1);
}

static void test_css_match_class_escaped_name(void)
{
    css_host_t host;
    robo_css_element_ref_t element;

    /* `.foo\:bar` is the class `foo:bar`, which is a legal class name. The
     * parser resolves the escape, so matching is a byte comparison against the
     * resolved bytes rather than any unescaping. */
    css_host_init(&host);
    element = css_host_add(&host, "div", NULL, "foo:bar");
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, ".foo\\:bar", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, ".foo", element), 0);
}

/* ------------------------------------------------------------------ */
/* Id                                                                 */
/* ------------------------------------------------------------------ */

static void test_css_match_id(void)
{
    css_host_t host;
    robo_css_element_ref_t app;

    css_host_init(&host);
    app = css_host_add(&host, "div", "app", NULL);
    css_host_add(&host, "div", "APP", NULL);
    css_host_add(&host, "div", "", NULL);
    css_host_add(&host, "div", NULL, NULL);
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "#app", app), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "#other", app), 0);
    /* Ids are case-sensitive. */
    ROBO_CHECK_EQ_INT(css_match(&host, "#app", 2u), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "#APP", 2u), 1);
    /* `id=""` is an id that no `#name` selector names, and no id at all is the
     * same answer by a different route. */
    ROBO_CHECK_EQ_INT(css_match(&host, "#app", 3u), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "#app", 4u), 0);
    /* Duplicate ids both answer: the matcher has no uniqueness assumption. */
    ROBO_CHECK_EQ_INT(css_match(&host, "#APP", app), 0);
}

static void test_css_match_id_unicode(void)
{
    css_host_t host;
    robo_css_element_ref_t element;

    css_host_init(&host);
    element = css_host_add(&host, "div", "\xD8\xB9\xD9\x84\xD8\xA8\xD8\xA9", NULL);
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "#\xD8\xB9\xD9\x84\xD8\xA8\xD8\xA9", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "#\xD8\xB9", element), 0);
}

/* ------------------------------------------------------------------ */
/* Attributes                                                         */
/* ------------------------------------------------------------------ */

static void test_css_match_attribute_existence(void)
{
    css_host_t host;
    robo_css_element_ref_t button;

    css_host_init(&host);
    button = css_host_add(&host, "button", NULL, NULL);
    css_host_attribute(&host, button, "disabled", "");
    css_host_attribute(&host, button, "type", "button");
    css_host_add(&host, "button", NULL, NULL);
    css_host_seal(&host);

    /* A boolean attribute is present with an empty value, which is what presence
     * asks about — nothing is inferred from the value. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[disabled]", button), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[type]", button), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[disabled]", 2u), 0);
    /* Attribute *names* are ASCII case-insensitive in HTML. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[DISABLED]", button), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[Type]", button), 1);
}

static void test_css_match_attribute_operators(void)
{
    css_host_t host;
    robo_css_element_ref_t element;

    css_host_init(&host);
    element = css_host_add(&host, "a", NULL, NULL);
    css_host_attribute(&host, element, "href", "/api/users.json");
    css_host_attribute(&host, element, "lang", "en-GB");
    css_host_attribute(&host, element, "rel", "nofollow external");
    css_host_attribute(&host, element, "data-name", "test");
    css_host_attribute(&host, element, "data-empty", "");
    css_host_seal(&host);

    /* Equality, quoted and unquoted, exactly as parsed. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[href=\"/api/users.json\"]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[lang=en-GB]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[lang=en]", element), 0);
    /* `~=` is a whitespace-separated word, not a substring. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[rel~=external]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[rel~=nofollow]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[rel~=ternal]", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "[rel~=external-]", element), 0);
    /* `|=` is equality or a dash-prefixed continuation. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[lang|=en]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[lang|=en-GB]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[lang|=e]", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "[lang|=en-GBX]", element), 0);
    /* Prefix, suffix and substring. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[href^=\"/api\"]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[href^=\"/API\"]", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "[href$=\".json\"]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[href*=\"users\"]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[href*=\"api/users\"]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[href*=\"api\\/users\"]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[href*=\"users/\"]", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "[data-name*=\"es\"]", element), 1);
    /* A missing attribute fails every operator, presence included. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[missing]", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "[missing=\"\"]", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "[missing^=\"\"]", element), 0);
}

static void test_css_match_attribute_empty_value(void)
{
    css_host_t host;
    robo_css_element_ref_t element;

    /* `[a]` and `[a=""]` are different questions, and an attribute that is
     * present-and-empty answers only the first and the equality. This is the
     * case where collapsing "absent" and "empty" would silently change which
     * rules apply. */
    css_host_init(&host);
    element = css_host_add(&host, "div", NULL, NULL);
    css_host_attribute(&host, element, "data-empty", "");
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "[data-empty]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[data-empty=\"\"]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[data-empty~=\"\"]", element), 0);
    /* An empty operand can match nothing under `|=`, `^=`, `$=` or `*=`,
     * because every string would otherwise trivially contain it. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[data-empty|=\"\"]", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "[data-empty^=\"\"]", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "[data-empty$=\"\"]", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "[data-empty*=\"\"]", element), 0);
}

static void test_css_match_attribute_case_flag(void)
{
    css_host_t host;
    robo_css_element_ref_t element;

    css_host_init(&host);
    element = css_host_add(&host, "input", NULL, NULL);
    css_host_attribute(&host, element, "type", "text");
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "[type=\"text\"]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[type=\"TEXT\"]", element), 0);
    /* `i` folds ASCII case, which is what makes the selector insensitive. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[type=\"TEXT\" i]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[type=\"text\" i]", element), 1);
    /* `s` is the explicit default: sensitive. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[type=\"TEXT\" s]", element), 0);
    /* The flag folds only where the operator compares; `~=` folds too. */
    ROBO_CHECK_EQ_INT(css_match(&host, "[type~=\"Text\" i]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[type^=\"TE\" i]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[type^=\"TE\"]", element), 0);
}

static void test_css_match_attribute_unicode_value(void)
{
    css_host_t host;
    robo_css_element_ref_t element;

    css_host_init(&host);
    element = css_host_add(&host, "div", NULL, NULL);
    css_host_attribute(&host, element, "data-name", "\xD8\xA7\xD8\xB3\xD9\x85");
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "[data-name=\"\xD8\xA7\xD8\xB3\xD9\x85\"]",
                                element),
                      1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[data-name$=\"\xD9\x85\"]", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "[data-name|=\"\xD8\xA7\"]", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "[data-name=\"\xD8\xA7\"]", element), 0);
}

/* ------------------------------------------------------------------ */
/* Compounds                                                          */
/* ------------------------------------------------------------------ */

static void test_css_match_compound_requires_every_simple(void)
{
    css_host_t host;
    robo_css_element_ref_t element;

    css_host_init(&host);
    element = css_host_add(&host, "input", "email", "field required");
    css_host_attribute(&host, element, "type", "text");
    css_host_seal(&host);

    ROBO_CHECK_EQ_INT(css_match(&host, "input", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "input#email", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "input#email.field", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "input.field.required", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "input[type=\"text\"].field#email", element), 1);
    /* One failing simple fails the compound, wherever it sits in the order. */
    ROBO_CHECK_EQ_INT(css_match(&host, "input#email.missing", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "span#email.field", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "input#other.field", element), 0);
    ROBO_CHECK_EQ_INT(css_match(&host, "input[type=\"radio\"].field", element), 0);
    /* The universal adds nothing to a compound and removes nothing. */
    ROBO_CHECK_EQ_INT(css_match(&host, "*.field", element), 1);
    ROBO_CHECK_EQ_INT(css_match(&host, "*#email.field", element), 1);
    /* A universal after a type selector is malformed rather than redundant, so
     * it never reaches the matcher: the parser refuses it. */
    (void)css_select_parse_rejected("input*.field");
}

/* ------------------------------------------------------------------ */
/* Errors and argument handling                                       */
/* ------------------------------------------------------------------ */

static void test_css_match_api_arguments(void)
{
    css_host_t host;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    robo_css_selector_match_t result;
    robo_css_element_host_t unusable;

    css_host_init(&host);
    css_host_add(&host, "div", NULL, NULL);
    css_host_seal(&host);
    css_select_parse("div", &list);

    /* No output, no host, or a host with no view at all is a caller bug, and is
     * distinguished from "did not match" — which is what -1 means everywhere. */
    ROBO_CHECK_EQ_INT(robo_css_selector_list_matches(&list, &host.host, 1u, NULL, NULL), -1);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_matches(&list, NULL, 1u, &result, NULL), -1);
    robo_css_element_host_init(&unusable);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_matches(&list, &unusable, 1u, &result, NULL), -1);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_matches(NULL, &host.host, 1u, &result, NULL), -1);
    /* A null element is not an error: it is "no element", and no element matches
     * nothing. */
    ROBO_CHECK_EQ_INT(robo_css_selector_list_matches(&list, &host.host,
                                                     ROBO_CSS_ELEMENT_REF_NONE, &result, NULL),
                      0);
    ROBO_CHECK_EQ_INT(result.matched, 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 0);
    /* An element the host cannot describe is a candidate that matches nothing,
     * not a failure. */
    ROBO_CHECK_EQ_INT(robo_css_selector_list_matches(&list, &host.host, 99u, &result, NULL), 0);

    robo_css_selector_list_dispose(&list);
}

void robo_test_register_css_select_match_simple(void);

void robo_test_register_css_select_match_simple(void)
{
    robo_test_register("css.select.match.type", test_css_match_type);
    robo_test_register("css.select.match.type_unicode", test_css_match_type_unicode);
    robo_test_register("css.select.match.class", test_css_match_class);
    robo_test_register("css.select.match.class_escaped_name",
                       test_css_match_class_escaped_name);
    robo_test_register("css.select.match.id", test_css_match_id);
    robo_test_register("css.select.match.id_unicode", test_css_match_id_unicode);
    robo_test_register("css.select.match.attribute_existence",
                       test_css_match_attribute_existence);
    robo_test_register("css.select.match.attribute_operators",
                       test_css_match_attribute_operators);
    robo_test_register("css.select.match.attribute_empty_value",
                       test_css_match_attribute_empty_value);
    robo_test_register("css.select.match.attribute_case_flag",
                       test_css_match_attribute_case_flag);
    robo_test_register("css.select.match.attribute_unicode_value",
                       test_css_match_attribute_unicode_value);
    robo_test_register("css.select.match.compound_requires_every_simple",
                       test_css_match_compound_requires_every_simple);
    robo_test_register("css.select.match.api_arguments", test_css_match_api_arguments);
}
