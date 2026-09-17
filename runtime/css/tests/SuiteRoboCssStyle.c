/*
 * RoboCSS Phase 9.1 style-boundary suite:
 * the element view, the style context, and the computed-style container.
 *
 * The two resolution entry points are tested as boundaries: they refuse, they
 * change nothing, and they say why. What they will do in 9.4, 9.5 and 9.8 is
 * not asserted here, and could not be.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSStyle.h"

/* ------------------------------------------------------------------ */
/* Element view                                                        */
/* ------------------------------------------------------------------ */

static void test_css_element_view(void)
{
    robo_css_element_view_t view = ROBO_CSS_CLEARED;

    css_fixture();
    robo_css_element_view_init(&view);
    ROBO_CHECK(!robo_css_element_view_is_set(&view));
    ROBO_CHECK_EQ_INT(view.ref, ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(robo_css_slice_is_empty(view.tag_name));
    ROBO_CHECK(view.classes == NULL);
    ROBO_CHECK_EQ_INT(view.class_count, 0u);
    ROBO_CHECK(view.attributes == NULL);
    ROBO_CHECK_EQ_INT(view.attribute_count, 0u);

    ROBO_CHECK_EQ_INT(robo_css_element_view_set_element(&view, 42u,
                                                        robo_css_slice_from_text("div"),
                                                        NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_element_view_is_set(&view));
    ROBO_CHECK_EQ_INT(view.ref, 42u);
    ROBO_CHECK(robo_css_slice_equals_ascii(view.tag_name, "div"));

    /* The ref is the element's identity: 0 means "no element" and is refused. */
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_element(&view, ROBO_CSS_ELEMENT_REF_NONE,
                                                        robo_css_slice_from_text("div"),
                                                        NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_element(&view, 42u,
                                                        robo_css_slice_empty(), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(view.ref, 42u);

    ROBO_CHECK_EQ_INT(robo_css_element_view_set_id(&view,
                                                   robo_css_slice_from_text("main"), NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_element_view_id_equals(&view, robo_css_slice_from_text("main")));
    ROBO_CHECK(!robo_css_element_view_id_equals(&view,
                                               robo_css_slice_from_text("Main")));

    /* An empty id is a real id (`id=""`), distinct from having none: the view
     * records what the DOM said. */
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_id(&view, robo_css_slice_empty(), NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_element_view_id_equals(&view, robo_css_slice_empty()));
    ROBO_CHECK(!robo_css_element_view_id_equals(&view, robo_css_slice_from_text("main")));

    ROBO_CHECK_EQ_INT(robo_css_element_view_set_element(&view, 42u,
                                                        robo_css_slice_from_text("div"),
                                                        NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_element(NULL, 42u,
                                                        robo_css_slice_from_text("div"),
                                                        NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(!robo_css_element_view_is_set(NULL));
    ROBO_CHECK(!robo_css_element_view_id_equals(NULL, robo_css_slice_from_text("x")));
    ROBO_CHECK(!robo_css_element_view_has_class(NULL, robo_css_slice_from_text("x")));
    ROBO_CHECK(!robo_css_element_view_find_attribute(NULL, robo_css_slice_from_text("x"),
                                                     NULL));
    robo_css_element_view_init(NULL);
}

static void test_css_element_view_classes_and_attributes(void)
{
    robo_css_element_view_t view = ROBO_CSS_CLEARED;
    robo_css_slice_t classes[4];
    int truncated = 0;
    robo_css_attribute_t attributes[3];
    robo_css_slice_t value;

    css_fixture();
    robo_css_element_view_init(&view);
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_element(&view, 7u,
                                                        robo_css_slice_from_text("input"),
                                                        NULL),
                      ROBO_OK);

    /* The class attribute is text; splitting it is the one helper this phase
     * owns for it, and the host may equally hand over a list it already has. */
    ROBO_CHECK_EQ_INT((int)robo_css_split_ascii_whitespace(
                          robo_css_slice_from_text("  card  wide\tselected "), classes,
                          4u, &truncated),
                      3);
    ROBO_CHECK_EQ_INT(truncated, 0);
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_classes(&view, classes, 3u, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_element_view_has_class(&view, robo_css_slice_from_text("card")));
    ROBO_CHECK(robo_css_element_view_has_class(&view, robo_css_slice_from_text("wide")));
    ROBO_CHECK(robo_css_element_view_has_class(&view,
                                               robo_css_slice_from_text("selected")));
    /* Class matching is byte-exact, unlike an attribute name. */
    ROBO_CHECK(!robo_css_element_view_has_class(&view, robo_css_slice_from_text("Card")));
    ROBO_CHECK(!robo_css_element_view_has_class(&view, robo_css_slice_from_text("car")));
    ROBO_CHECK(!robo_css_element_view_has_class(&view, robo_css_slice_empty()));

    attributes[0].name = robo_css_slice_from_text("type");
    attributes[0].value = robo_css_slice_from_text("text");
    attributes[1].name = robo_css_slice_from_text("disabled");
    attributes[1].value = robo_css_slice_empty();
    attributes[2].name = robo_css_slice_from_text("placeholder");
    attributes[2].value = robo_css_slice_from_text("\xD8\xA7\xD8\xB3\xD9\x85");
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_attributes(&view, attributes, 3u, NULL),
                      ROBO_OK);

    /* Finding an attribute is ASCII case-insensitive and yields its value,
     * which may be empty and is therefore distinguished by the return value. */
    ROBO_CHECK(robo_css_element_view_find_attribute(&view,
                                                    robo_css_slice_from_text("type"),
                                                    &value));
    ROBO_CHECK(robo_css_slice_equals_ascii(value, "text"));
    ROBO_CHECK(robo_css_element_view_find_attribute(&view,
                                                    robo_css_slice_from_text("TYPE"),
                                                    &value));
    ROBO_CHECK(robo_css_slice_equals_ascii(value, "text"));
    ROBO_CHECK(robo_css_element_view_find_attribute(&view,
                                                    robo_css_slice_from_text("DISABLED"),
                                                    &value));
    ROBO_CHECK(robo_css_slice_is_empty(value));
    ROBO_CHECK(!robo_css_element_view_find_attribute(&view,
                                                     robo_css_slice_from_text("nope"),
                                                     &value));
    ROBO_CHECK(!robo_css_element_view_find_attribute(&view, robo_css_slice_empty(),
                                                     &value));
    /* A prefix and a longer name are different attributes. */
    ROBO_CHECK(!robo_css_element_view_find_attribute(&view,
                                                     robo_css_slice_from_text("typ"),
                                                     &value));
    ROBO_CHECK(!robo_css_element_view_find_attribute(
        &view, robo_css_slice_from_text("type-long"), &value));
    /* Unicode values are returned verbatim. */
    ROBO_CHECK(robo_css_element_view_find_attribute(
        &view, robo_css_slice_from_text("placeholder"), &value));
    /* Three Arabic letters, six UTF-8 bytes, returned untouched. */
    ROBO_CHECK_EQ_INT(value.length, 6u);
    ROBO_CHECK_EQ_INT(value.bytes[0], 0xD8u);
    ROBO_CHECK_EQ_INT(value.bytes[1], 0xA7u);

    /* A NULL array with a non-zero count is a caller mistake; an empty view is
     * described by passing NULL with 0. */
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_classes(&view, NULL, 3u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_attributes(&view, NULL, 1u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_classes(&view, NULL, 0u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(view.class_count, 0u);
    ROBO_CHECK(!robo_css_element_view_has_class(&view, robo_css_slice_from_text("card")));
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_classes(&view, classes, 3u, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_classes(NULL, classes, 3u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_element_view_set_attributes(NULL, attributes, 1u, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* Style context                                                       */
/* ------------------------------------------------------------------ */

static void test_css_style_context_sheets(void)
{
    robo_css_style_context_t *context = NULL;
    robo_css_stylesheet_id_t author_a = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_stylesheet_id_t author_b = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_stylesheet_id_t runtime = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_diag_t diag;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_style_context_create(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_style_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK(context != NULL);
    ROBO_CHECK_EQ_INT(robo_css_style_context_total_sheet_count(context), 0u);
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet_count(context, ROBO_CSS_ORIGIN_AUTHOR),
                      0u);
    ROBO_CHECK(!robo_css_style_context_has_runtime_sheet(context));
    ROBO_CHECK(robo_css_style_context_property_table(context) == NULL);
    ROBO_CHECK(robo_css_style_context_element_lookup(context) == NULL);
    ROBO_CHECK(robo_css_style_context_inline_style(context) == NULL);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&author_a, ROBO_CSS_ORIGIN_AUTHOR,
                                                 NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&author_b, ROBO_CSS_ORIGIN_AUTHOR,
                                                 NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&runtime, ROBO_CSS_ORIGIN_RUNTIME,
                                                 NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(context, ROBO_CSS_ORIGIN_AUTHOR,
                                                       author_a, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(context, ROBO_CSS_ORIGIN_AUTHOR,
                                                       author_b, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(context, ROBO_CSS_ORIGIN_RUNTIME,
                                                       runtime, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet_count(context, ROBO_CSS_ORIGIN_AUTHOR),
                      2u);
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet_count(context, ROBO_CSS_ORIGIN_RUNTIME),
                      1u);
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet_count(context, ROBO_CSS_ORIGIN_USER),
                      0u);
    ROBO_CHECK_EQ_INT(robo_css_style_context_total_sheet_count(context), 3u);
    /* The order a sheet was added in is the order it is read back, which is the
     * order a cascade will need. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet(context, ROBO_CSS_ORIGIN_AUTHOR, 0u),
                      author_a);
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet(context, ROBO_CSS_ORIGIN_AUTHOR, 1u),
                      author_b);
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet(context, ROBO_CSS_ORIGIN_AUTHOR, 2u),
                      ROBO_CSS_STYLESHEET_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet(context, ROBO_CSS_ORIGIN_USER, 0u),
                      ROBO_CSS_STYLESHEET_ID_NONE);

    /* The runtime origin is the default-stylesheet boundary: this phase ships
     * no default, and a caller can tell that from the context. */
    ROBO_CHECK(robo_css_style_context_has_runtime_sheet(context));

    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(context, ROBO_CSS_ORIGIN_INLINE,
                                                       author_a, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(context, ROBO_CSS_ORIGIN_NONE,
                                                       author_a, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(context, ROBO_CSS_ORIGIN_AUTHOR,
                                                       ROBO_CSS_STYLESHEET_ID_NONE, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* A dead id is refused where it is added, not later during a resolution. */
    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(context, ROBO_CSS_ORIGIN_AUTHOR,
                                                       (robo_css_stylesheet_id_t)9999u,
                                                       &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_STYLE);
    ROBO_CHECK_EQ_INT(robo_css_style_context_total_sheet_count(context), 3u);
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(NULL, ROBO_CSS_ORIGIN_AUTHOR,
                                                       author_a, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_style_context_clear_origin(context, ROBO_CSS_ORIGIN_AUTHOR);
    ROBO_CHECK_EQ_INT(robo_css_style_context_total_sheet_count(context), 1u);
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet_count(context, ROBO_CSS_ORIGIN_AUTHOR),
                      0u);
    robo_css_style_context_clear(context);
    ROBO_CHECK_EQ_INT(robo_css_style_context_total_sheet_count(context), 0u);
    ROBO_CHECK(!robo_css_style_context_has_runtime_sheet(context));
    robo_css_style_context_clear_origin(context, ROBO_CSS_ORIGIN_INLINE);
    robo_css_style_context_clear(context);

    /* Destroying the context does not destroy the sheets it borrowed: they are
     * still live, and still the caller's. */
    robo_css_style_context_destroy(context);
    ROBO_CHECK(robo_css_stylesheet_is_live(author_a));
    ROBO_CHECK(robo_css_stylesheet_is_live(runtime));
    ROBO_CHECK_EQ_INT(robo_css_style_context_total_sheet_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet_count(NULL, ROBO_CSS_ORIGIN_AUTHOR),
                      0u);
    ROBO_CHECK_EQ_INT(robo_css_style_context_sheet(NULL, ROBO_CSS_ORIGIN_AUTHOR, 0u),
                      ROBO_CSS_STYLESHEET_ID_NONE);
    ROBO_CHECK(!robo_css_style_context_has_runtime_sheet(NULL));
    ROBO_CHECK(robo_css_style_context_property_table(NULL) == NULL);
    robo_css_style_context_destroy(NULL);
    robo_css_style_context_clear(NULL);
    robo_css_style_context_clear_origin(NULL, ROBO_CSS_ORIGIN_AUTHOR);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(author_a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(author_b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(runtime, NULL), ROBO_OK);
}

static int css_test_lookup(void *userdata, robo_css_element_ref_t element,
                           robo_css_element_view_t *out_view)
{
    int *calls = (int *)userdata;

    *calls += 1;
    if (element != 5u) {
        return ROBO_NOT_FOUND;
    }
    robo_css_element_view_init(out_view);
    return robo_css_element_view_set_element(out_view, element,
                                             robo_css_slice_from_text("div"), NULL);
}

static int css_test_inline(void *userdata, robo_css_element_ref_t element,
                           robo_css_slice_t *out_text)
{
    int *calls = (int *)userdata;

    *calls += 1;
    if (element != 5u) {
        return ROBO_NOT_FOUND;
    }
    *out_text = robo_css_slice_from_text("color: red");
    return ROBO_OK;
}

static void test_css_style_context_borrows(void)
{
    robo_css_style_context_t *context = NULL;
    robo_css_property_table_t *table = NULL;
    int lookup_calls = 0;
    int inline_calls = 0;
    robo_css_element_view_t view = ROBO_CSS_CLEARED;
    robo_css_slice_t text;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_create(&context, NULL), ROBO_OK);

    /* The table is borrowed: the context holds a reference, not a copy, and
     * NULL is a legal way to say "no identities available". */
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_property_table(context, table, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_style_context_property_table(context) == table);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_property_table(context, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_style_context_property_table(context) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_property_table(NULL, table, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Both callbacks are borrowed with their userdata, and both can be cleared. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_element_lookup(context, css_test_lookup,
                                                                &lookup_calls, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_style_context_element_lookup(context) == css_test_lookup);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_inline_style(context, css_test_inline,
                                                              &inline_calls, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_style_context_inline_style(context) == css_test_inline);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_element_lookup(NULL, css_test_lookup,
                                                                NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_inline_style(NULL, css_test_inline, NULL,
                                                              NULL),
                      ROBO_INVALID_ARGUMENT);

    /* The seam works in isolation: the host answers a view, and answers the
     * inline text. RoboCSS calls neither yet — that is 9.4's and 9.8's job — so
     * this test drives them the way those phases will, and asserts the contract
     * a host has to satisfy. */
    robo_css_element_view_init(&view);
    ROBO_CHECK_EQ_INT(css_test_lookup(&lookup_calls, 5u, &view), ROBO_OK);
    ROBO_CHECK(robo_css_element_view_is_set(&view));
    ROBO_CHECK(robo_css_slice_equals_ascii(view.tag_name, "div"));
    /* Installing a callback is not calling it: the count is one, from this
     * test's own call. */
    ROBO_CHECK_EQ_INT(lookup_calls, 1);
    /* An element the host cannot answer is reported missing, which is how a
     * matcher learns to stop rather than to guess. */
    ROBO_CHECK_EQ_INT(css_test_lookup(&lookup_calls, 6u, &view), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(lookup_calls, 2);

    text = robo_css_slice_empty();
    ROBO_CHECK_EQ_INT(css_test_inline(&inline_calls, 5u, &text), ROBO_OK);
    ROBO_CHECK(robo_css_slice_equals_ascii(text, "color: red"));
    ROBO_CHECK_EQ_INT(css_test_inline(&inline_calls, 6u, &text), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(inline_calls, 2);

    ROBO_CHECK_EQ_INT(robo_css_style_context_set_element_lookup(context, NULL, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_style_context_element_lookup(context) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_inline_style(context, NULL, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_style_context_inline_style(context) == NULL);

    robo_css_style_context_destroy(context);
    ROBO_CHECK_EQ_INT(robo_css_property_table_count(table), 0u);
    robo_css_property_table_destroy(table);
}

/* ------------------------------------------------------------------ */
/* Resolution boundaries                                               */
/* ------------------------------------------------------------------ */

/*
 * The collection door, exercised where it is *not* about CSS at all: an empty
 * context declares nothing for any element, and that is a complete answer rather
 * than a missing feature. What a context with sheets in it does is the cascade
 * suites' business (css.cascade.*); what belongs here is the door's own shape —
 * what it refuses, what it fills, and what it leaves behind for a caller that
 * ignores the result code.
 */
static void test_css_style_match_declarations_empty_context(void)
{
    robo_css_style_context_t *context = NULL;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_diag_t diag;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_style_context_create(&context, NULL), ROBO_OK);

    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_style_context_match_declarations(context, 5u, &cascade,
                                                                &error, &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 0u);
    /* No sheets, so nothing was even looked at — and an empty answer over clean
     * input is complete, which is the distinction the result has to keep. */
    ROBO_CHECK_EQ_INT(cascade.unsupported, 0);
    ROBO_CHECK_EQ_INT(robo_css_cascade_is_complete(&cascade), 1);
    ROBO_CHECK_EQ_INT(cascade.element, 5u);
    ROBO_CHECK_EQ_INT(robo_css_cascade_at(&cascade, 0u) == NULL, 1);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 0);
    /* Nothing failed, so there is nothing to diagnose. */
    ROBO_CHECK_EQ_INT(diag.code, ROBO_OK);
    robo_css_cascade_dispose(&cascade);

    /* An element is not optional: nothing applies to no element, and an empty
     * result would be indistinguishable from a real "nothing matched". */
    ROBO_CHECK_EQ_INT(robo_css_style_context_match_declarations(
                          context, ROBO_CSS_ELEMENT_REF_NONE, &cascade, &error, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 0u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_STYLE);
    robo_css_cascade_dispose(&cascade);

    ROBO_CHECK_EQ_INT(robo_css_style_context_match_declarations(context, 5u, NULL,
                                                                &error, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_style_context_match_declarations(NULL, 5u, &cascade,
                                                                &error, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_css_cascade_dispose(&cascade);
    /* The error record is optional. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_match_declarations(context, 5u, &cascade,
                                                                NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 0u);
    robo_css_cascade_dispose(&cascade);

    robo_css_style_context_destroy(context);
}

/**
 * The computation door's argument contract.
 *
 * 9.8 is the phase that made this door real, so the assertions here are about a
 * door that answers rather than about one that refuses. An element of NONE is
 * still refused — "no element" is not an element — and a context with no host, no
 * table and no registry is still *accepted*, because a document with no
 * stylesheets is a document and not an error: its cascade is empty, nothing
 * inherits, and the answer is an empty style rather than a failed call.
 */
static void test_css_style_compute_arguments(void)
{
    robo_css_style_context_t *context = NULL;
    robo_css_computed_style_t *style = NULL;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_diag_t diag;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_style_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    robo_css_diag_reset(&diag);
    /* No host, no property table, no registry: an empty context still computes
     * a style, and the style is empty because nothing was there to make it
     * otherwise. An incomplete input is not an incomplete call. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(context, 5u, style, &error,
                                                           &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 0u);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_is_truncated(style), 0);

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(context, 5u, NULL, &error,
                                                           NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(NULL, 5u, style, &error,
                                                           NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(
                          context, ROBO_CSS_ELEMENT_REF_NONE, style, &error, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
    /* The error record is optional, as every other door in this file says. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(context, 5u, style, NULL,
                                                           NULL),
                      ROBO_OK);

    robo_css_computed_style_destroy(style);
    robo_css_style_context_destroy(context);
}

/* ------------------------------------------------------------------ */
/* Computed container                                                  */
/* ------------------------------------------------------------------ */

static void test_css_computed_container(void)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_property_table_t *table = NULL;
    robo_css_property_id_t color = ROBO_CSS_PROPERTY_ID_NONE;
    robo_css_property_id_t padding = ROBO_CSS_PROPERTY_ID_NONE;
    robo_css_value_t value = ROBO_CSS_CLEARED;
    robo_css_property_id_t property = ROBO_CSS_PROPERTY_ID_NONE;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 0u);

    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&table, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "color", 0u, &color, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_register(table, "padding", 0u, &padding,
                                                       NULL),
                      ROBO_OK);

    ROBO_CHECK(!robo_css_computed_style_has(style, color));
    ROBO_CHECK(robo_css_computed_style_get(style, color) == NULL);

    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set(style, color, &value, NULL), ROBO_OK);
    ROBO_CHECK(robo_css_value_is_empty(&value)); /* moved in */
    ROBO_CHECK(robo_css_computed_style_has(style, color));
    ROBO_CHECK(robo_css_computed_style_get(style, color) != NULL);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 1u);

    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set(style, padding, &value, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 2u);

    /* Iteration order is insertion order, and replacing a property keeps its
     * position rather than moving it to the end: order must not depend on how
     * often a property was written. */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_property_at(style, 0u, &property, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(property, color);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_property_at(style, 1u, &property, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(property, padding);

    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set(style, color, &value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 2u);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_property_at(style, 0u, &property, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(property, color);

    ROBO_CHECK(robo_css_computed_style_value_at(style, 0u) != NULL);
    ROBO_CHECK(robo_css_computed_style_value_at(style, 2u) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_property_at(style, 2u, &property, NULL),
                      ROBO_NOT_FOUND);

    /* Removing keeps the rest in order too. */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_remove(style, color, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 1u);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_property_at(style, 0u, &property, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(property, padding);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_remove(style, color, NULL), ROBO_NOT_FOUND);

    /* A property that was never assigned has no value at all, and "no property"
     * is refused rather than stored. */
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                  &value, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(value.kind, ROBO_CSS_VALUE_KIND_LIST);
    robo_css_value_dispose(&value);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set(style, color, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set(NULL, color, &value, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_property_at(style, 0u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_property_at(NULL, 0u, &property, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_computed_style_clear(style);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 0u);
    ROBO_CHECK(!robo_css_computed_style_has(style, padding));
    /* Clearing leaves a usable container. */
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set(style, padding, &value, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 1u);

    ROBO_CHECK_EQ_INT(robo_css_computed_style_count(NULL), 0u);
    ROBO_CHECK(robo_css_computed_style_get(NULL, color) == NULL);
    ROBO_CHECK(!robo_css_computed_style_has(NULL, color));
    ROBO_CHECK(robo_css_computed_style_value_at(NULL, 0u) == NULL);
    robo_css_computed_style_clear(NULL);
    robo_css_computed_style_destroy(NULL);

    robo_css_computed_style_destroy(style);
    robo_css_property_table_destroy(table);
}

static void test_css_computed_lifetime_balances(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    int we_enabled;
    size_t baseline;

    css_fixture();
    css_source_of_string(&source, "4px");
    we_enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();
    {
        int i;

        for (i = 0; i < 24; i++) {
            robo_css_computed_style_t *style = NULL;
            robo_css_value_t function = ROBO_CSS_CLEARED;
            robo_css_value_t child = ROBO_CSS_CLEARED;
            int j;

            ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
            for (j = 1; j <= 6; j++) {
                ROBO_CHECK_EQ_INT(robo_css_value_make_function(
                                      &function, robo_css_slice_from_text("f"),
                                      robo_css_range_at(0u), NULL),
                                  ROBO_OK);
                css_value_token_at(&child, &source, "4px", NULL);
                ROBO_CHECK_EQ_INT(robo_css_value_append(&function, &child, NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_css_computed_style_set(
                                      style, (robo_css_property_id_t)j, &function, NULL),
                                  ROBO_OK);
            }
            ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 6u);
            /* Replacing every property must not accumulate values. */
            for (j = 1; j <= 6; j++) {
                css_value_token_at(&child, &source, "4px", NULL);
                ROBO_CHECK_EQ_INT(robo_css_computed_style_set(
                                      style, (robo_css_property_id_t)j, &child, NULL),
                                  ROBO_OK);
            }
            ROBO_CHECK_EQ_INT(robo_css_computed_style_count(style), 6u);
            robo_css_computed_style_destroy(style);
            ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        }
    }
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(we_enabled);
    robo_css_source_dispose(&source);
}

void robo_test_register_css_style(void);

void robo_test_register_css_style(void)
{
    robo_test_register("css.style.element_view", test_css_element_view);
    robo_test_register("css.style.element_view_classes_and_attributes",
                       test_css_element_view_classes_and_attributes);
    robo_test_register("css.style.context_sheets", test_css_style_context_sheets);
    robo_test_register("css.style.context_borrows", test_css_style_context_borrows);
    robo_test_register("css.style.match_declarations_empty_context",
                       test_css_style_match_declarations_empty_context);
    robo_test_register("css.style.compute_arguments",
                       test_css_style_compute_arguments);
    robo_test_register("css.computed.container", test_css_computed_container);
    robo_test_register("css.computed.lifetime_balances",
                       test_css_computed_lifetime_balances);
}
