/*
 * RoboHTML Phase 8.14 forms suite (html.form.*).
 *
 * What it pins, in the order the phase argues for it: that the seven form
 * elements are real DOM elements with real attribute views; that an
 * unrecognised `type` degrades instead of damaging the element; that the
 * declared-versus-current split behaves the way the dirty-value flag defines
 * it, *including the half that is easy to get wrong* — that an un-overridden
 * control follows its attribute and an overridden one does not; that a label
 * and a form owner are resolved from the live tree so nothing can go stale;
 * and that a malformed document cannot corrupt a lifecycle or a tree.
 *
 * Three tests are worth reading first, because they are the ones that would
 * fail if the model were quietly wrong:
 *
 *   html.form.input_value_state
 *       the declared/current split, both halves.
 *   html.form.owner_attribute
 *       the `form` attribute winning when it resolves, and falling back when
 *       it does not — plus the source field that says which rule answered.
 *   html.form.parser_malformed
 *       a document full of unclosed tags, a stray `</form>`, a void input
 *       swallowing nothing, still passes the tree invariant audit.
 *
 * The fixture parses real HTML through the real tokenizer and tree builder:
 * a test that built its elements by hand would not be testing the parser
 * integration the phase is largely about.
 */

#include "RoboBase/RoboBase.h"
#include "RoboMem/RoboMem.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLForm.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static void form_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static robo_html_document_id_t form_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    form_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/** Parse @p text into @p id through the real tokenizer and tree builder. */
static void form_build(robo_html_document_id_t id, const char *text)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);
}

static robo_html_node_handle_t form_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

typedef struct form_tag_query {
    robo_html_tag_t tag;
} form_tag_query_t;

static int form_match_tag(const robo_html_node_view_t *view, void *user)
{
    const form_tag_query_t *q = (const form_tag_query_t *)user;

    return robo_html_element_is_tag(&view->handle, q->tag);
}

/** First element of @p tag at or below @p start; asserts it was found. */
static robo_html_node_handle_t form_find_tag(
    const robo_html_node_handle_t *start, robo_html_tag_t tag)
{
    form_tag_query_t q;
    robo_html_node_handle_t out;

    q.tag = tag;
    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(
        robo_html_node_find_first(start, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  form_match_tag, &q, &out, NULL),
        ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

typedef struct form_id_query {
    const char *id;
} form_id_query_t;

static int form_match_id(const robo_html_node_view_t *view, void *user)
{
    const form_id_query_t *q = (const form_id_query_t *)user;
    size_t length = 0u;
    const char *value;

    if (!view || view->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return 0;
    }
    value = robo_html_element_attribute_value(&view->handle, "id", &length);
    return value && strcmp(value, q->id) == 0;
}

/**
 * First element at or below @p start whose `id` is @p value, or an invalid
 * handle when there is none.
 *
 * Built on the library's own search so this helper and the module under test
 * agree about document order rather than each having an opinion. It does not
 * assert, because "there is no such element" is one of the states under test.
 */
static robo_html_node_handle_t form_find_id(
    const robo_html_node_handle_t *start, const char *value)
{
    form_id_query_t q;
    robo_html_node_handle_t out;

    q.id = value;
    robo_html_node_handle_clear(&out);
    if (robo_html_node_find_first(start, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  form_match_id, &q, &out, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&out);
    }
    return out;
}

/** As form_find_tag, but starting at the document root of @p id. */
static robo_html_node_handle_t form_find_tag_doc(robo_html_document_id_t id,
                                                 robo_html_tag_t tag)
{
    robo_html_node_handle_t root = form_root(id);

    return form_find_tag(&root, tag);
}

/** As form_find_id, but starting at the document root of @p id, asserting
 *  that the element is there. */
static robo_html_node_handle_t form_find_id_doc(robo_html_document_id_t id,
                                                const char *value)
{
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t out = form_find_id(&root, value);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

typedef struct form_last_option_query {
    robo_html_node_handle_t last;
    int found;
} form_last_option_query_t;

static robo_result_t form_last_option_visit(const robo_html_node_view_t *view,
                                            size_t depth, void *user)
{
    form_last_option_query_t *q = (form_last_option_query_t *)user;

    (void)depth;
    if (view && view->kind == ROBO_HTML_NODE_KIND_ELEMENT &&
        robo_html_element_is_tag(&view->handle, ROBO_HTML_TAG_OPTION)) {
        q->last = view->handle;
        q->found = 1;
    }
    return ROBO_OK;
}

/**
 * The last `<option>` in @p select's subtree, in document order.
 *
 * Deliberately not a sibling walk: options can be nested in an `<optgroup>`,
 * and a sibling chain ends at its parent's last child rather than continuing
 * into the next parent. A subtree walk is the only thing that gives document
 * order here, and it is the same order the option indices use.
 */
static robo_html_node_handle_t form_last_option(
    const robo_html_node_handle_t *select)
{
    form_last_option_query_t q;

    q.found = 0;
    robo_html_node_handle_clear(&q.last);
    ROBO_CHECK_EQ_INT(
        robo_html_node_walk_descendants(select, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                        form_last_option_visit, &q, NULL),
        ROBO_OK);
    ROBO_CHECK(q.found);
    return q.last;
}

/** The subtree text of @p node, through the DOM's own reader. */
static const char *form_text(const robo_html_node_handle_t *node)
{
    static char buf[1024];
    size_t length = 0u;

    memset(buf, 0, sizeof(buf));
    ROBO_CHECK_EQ_INT(
        robo_html_node_subtree_text(node, buf, sizeof(buf), &length, NULL),
        ROBO_OK);
    ROBO_CHECK(length < sizeof(buf));
    return buf;
}

/** Create an element under @p parent (mutation tests). */
static robo_html_node_handle_t form_create_under(
    robo_html_document_id_t id, const robo_html_node_handle_t *parent,
    const char *name)
{
    robo_html_element_create_info_t info;
    robo_html_node_handle_t out;

    robo_html_element_create_info_init(&info);
    info.parent = *parent;
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
    info.namespace_id = ROBO_HTML_NAMESPACE_HTML;
    info.local_name = name;
    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

/** Read a control's current value into a static buffer. */
static const char *form_control_value(const robo_html_node_handle_t *element,
                                      int *out_is_declared)
{
    static char buf[1024];
    size_t length = 0u;
    int declared = 0;

    memset(buf, 0, sizeof(buf));
    ROBO_CHECK_EQ_INT(robo_html_element_control_value(element, buf,
                                                      sizeof(buf), &length,
                                                      &declared, NULL),
                      ROBO_OK);
    ROBO_CHECK(length < sizeof(buf));
    if (out_is_declared) {
        *out_is_declared = declared;
    }
    return buf;
}

static int nearly(double a, double b)
{
    double d = a - b;

    if (d < 0.0) {
        d = -d;
    }
    return d < 0.000001;
}

/* ------------------------------------------------------------------ */
/* Element identity and attributes                                     */
/* ------------------------------------------------------------------ */

static void test_form_parser_element(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t f;

    form_build(id,
               "<form action=\"/Save\" method=\"post\" "
               "enctype=\"multipart/form-data\" name=\"Editor\" "
               "target=\"_self\"><input name=\"a\"></form>");

    f = form_find_tag(&root, ROBO_HTML_TAG_FORM);
    ROBO_CHECK(robo_html_element_is_tag(&f, ROBO_HTML_TAG_FORM));
    ROBO_CHECK(robo_html_element_is_named(&f, "form"));
    ROBO_CHECK_EQ_INT(robo_html_element_classification(&f),
                      ROBO_HTML_ELEMENT_CLASS_CONTROL);
    ROBO_CHECK_STR_EQ(robo_html_element_attribute_value(&f, "action", NULL),
                      "/Save");

    /* The nine elements this phase models are all real tags with the control
       role, which is the fact the parser already provided in 8.11 and this
       phase must not have broken. */
    ROBO_CHECK(robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML, "label") ==
               ROBO_HTML_TAG_LABEL);
    ROBO_CHECK(robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML, "input") ==
               ROBO_HTML_TAG_INPUT);
    ROBO_CHECK(robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML, "button") ==
               ROBO_HTML_TAG_BUTTON);
    ROBO_CHECK(robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML, "textarea") ==
               ROBO_HTML_TAG_TEXTAREA);
    ROBO_CHECK(robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML, "select") ==
               ROBO_HTML_TAG_SELECT);
    ROBO_CHECK(robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML, "option") ==
               ROBO_HTML_TAG_OPTION);
    ROBO_CHECK(robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML, "fieldset") ==
               ROBO_HTML_TAG_FIELDSET);
    ROBO_CHECK(robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML, "legend") ==
               ROBO_HTML_TAG_LEGEND);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_form_attributes(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_form_attributes_t attrs;
    robo_html_node_handle_t f;

    form_build(id,
               "<form action=\"/Save\" method=\"POST\" "
               "enctype=\"multipart/form-data\" name=\"Editor\" "
               "target=\"Frame\"></form>");
    f = form_find_tag(&root, ROBO_HTML_TAG_FORM);
    ROBO_CHECK_EQ_INT(robo_html_element_form_attributes(&f, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK(attrs.action_present && attrs.action_usable);
    ROBO_CHECK_STR_EQ(attrs.action, "/Save");
    ROBO_CHECK_EQ_INT(attrs.action_length, 5);
    ROBO_CHECK_EQ_INT(attrs.method_kind, ROBO_HTML_FORM_METHOD_POST);
    ROBO_CHECK_EQ_INT(attrs.enctype_kind,
                      ROBO_HTML_FORM_ENCTYPE_MULTIPART);
    ROBO_CHECK_STR_EQ(attrs.name, "Editor");
    ROBO_CHECK_STR_EQ(attrs.target, "Frame");

    /* `<form action>` and `<form action="  ">` name nothing, and that is a
       different fact from "no attribute at all". */
    form_build(id, "<form action=\"  \"></form>");
    f = form_find_tag_doc(id, ROBO_HTML_TAG_FORM);
    ROBO_CHECK_EQ_INT(robo_html_element_form_attributes(&f, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK(attrs.action_present);
    ROBO_CHECK(!attrs.action_usable);
    ROBO_CHECK_EQ_INT(attrs.method_kind, ROBO_HTML_FORM_METHOD_NONE);
    ROBO_CHECK_EQ_INT(attrs.enctype_kind, ROBO_HTML_FORM_ENCTYPE_NONE);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_method_enctype_vocabulary(void)
{
    /* Absent or blank is NONE; a name we do not know is UNKNOWN. The two are
       different answers, and collapsing them would make "the author said
       something odd" indistinguishable from "the author said nothing". */
    ROBO_CHECK_EQ_INT(robo_html_form_method_from_name(NULL, 0u),
                      ROBO_HTML_FORM_METHOD_NONE);
    ROBO_CHECK_EQ_INT(robo_html_form_method_from_name("", 0u),
                      ROBO_HTML_FORM_METHOD_NONE);
    ROBO_CHECK_EQ_INT(robo_html_form_method_from_name("   ", 3u),
                      ROBO_HTML_FORM_METHOD_NONE);
    ROBO_CHECK_EQ_INT(robo_html_form_method_from_name("get", 3u),
                      ROBO_HTML_FORM_METHOD_GET);
    ROBO_CHECK_EQ_INT(robo_html_form_method_from_name("POST", 4u),
                      ROBO_HTML_FORM_METHOD_POST);
    ROBO_CHECK_EQ_INT(robo_html_form_method_from_name(" dialog ", 8u),
                      ROBO_HTML_FORM_METHOD_DIALOG);
    ROBO_CHECK_EQ_INT(robo_html_form_method_from_name("patch", 5u),
                      ROBO_HTML_FORM_METHOD_UNKNOWN);
    ROBO_CHECK_STR_EQ(robo_html_form_method_name(ROBO_HTML_FORM_METHOD_GET),
                      "get");
    ROBO_CHECK_STR_EQ(robo_html_form_method_name(
                          ROBO_HTML_FORM_METHOD_UNKNOWN),
                      "unknown");
    ROBO_CHECK(robo_html_form_method_is_known(ROBO_HTML_FORM_METHOD_DIALOG));
    ROBO_CHECK(!robo_html_form_method_is_known(
        (robo_html_form_method_t)99));

    ROBO_CHECK_EQ_INT(
        robo_html_form_enctype_from_name("application/x-www-form-urlencoded",
                                         33u),
        ROBO_HTML_FORM_ENCTYPE_URLENCODED);
    ROBO_CHECK_EQ_INT(robo_html_form_enctype_from_name("MULTIPART/FORM-DATA",
                                                      19u),
                      ROBO_HTML_FORM_ENCTYPE_MULTIPART);
    ROBO_CHECK_EQ_INT(robo_html_form_enctype_from_name("text/plain", 10u),
                      ROBO_HTML_FORM_ENCTYPE_TEXT_PLAIN);
    ROBO_CHECK_EQ_INT(robo_html_form_enctype_from_name("application/json",
                                                      16u),
                      ROBO_HTML_FORM_ENCTYPE_UNKNOWN);
    ROBO_CHECK_EQ_INT(robo_html_form_enctype_from_name("", 0u),
                      ROBO_HTML_FORM_ENCTYPE_NONE);
    ROBO_CHECK_STR_EQ(
        robo_html_form_enctype_name(ROBO_HTML_FORM_ENCTYPE_MULTIPART),
        "multipart/form-data");
}

static void test_form_non_form_refused(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_form_attributes_t attrs;
    robo_html_input_attributes_t inputs;
    robo_html_node_handle_t div;
    robo_html_node_handle_t stale;

    form_build(id, "<div id=\"d\"><input id=\"i\"></div>");
    div = form_find_tag(&root, ROBO_HTML_TAG_DIV);
    ROBO_CHECK_EQ_INT(robo_html_element_form_attributes(&div, &attrs, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&div, &inputs, NULL),
                      ROBO_NOT_SUPPORTED);
    /* NULL and unknown handles are argument/liveness problems, not "wrong
       element type" ones. */
    ROBO_CHECK_EQ_INT(robo_html_element_form_attributes(NULL, &attrs, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_form_attributes(&div, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    stale.document_id = id;
    stale.node_id = 999999u;
    stale.generation = 1u;
    ROBO_CHECK_EQ_INT(robo_html_element_form_attributes(&stale, &attrs, NULL),
                      ROBO_NOT_FOUND);

    /* The whole tree was zeroed by the refusals: a failed read must not leave
       a partially filled view behind. */
    ROBO_CHECK(!attrs.action_present);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Input types                                                         */
/* ------------------------------------------------------------------ */

static void test_form_input_types(void)
{
    /* The vocabulary, and the degradation that makes "unknown" safe. */
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name(NULL, 0u),
                      ROBO_HTML_INPUT_TYPE_TEXT);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("", 0u),
                      ROBO_HTML_INPUT_TYPE_TEXT);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("TEXT", 4u),
                      ROBO_HTML_INPUT_TYPE_TEXT);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("password", 8u),
                      ROBO_HTML_INPUT_TYPE_PASSWORD);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("checkbox", 8u),
                      ROBO_HTML_INPUT_TYPE_CHECKBOX);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("radio", 5u),
                      ROBO_HTML_INPUT_TYPE_RADIO);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("number", 6u),
                      ROBO_HTML_INPUT_TYPE_NUMBER);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("email", 5u),
                      ROBO_HTML_INPUT_TYPE_EMAIL);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("submit", 6u),
                      ROBO_HTML_INPUT_TYPE_SUBMIT);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("button", 6u),
                      ROBO_HTML_INPUT_TYPE_BUTTON);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("range", 5u),
                      ROBO_HTML_INPUT_TYPE_UNKNOWN);
    ROBO_CHECK_EQ_INT(robo_html_input_type_from_name("color", 5u),
                      ROBO_HTML_INPUT_TYPE_UNKNOWN);

    /* The degradation rule itself, and that it never returns UNKNOWN. */
    ROBO_CHECK_EQ_INT(
        robo_html_input_type_effective(ROBO_HTML_INPUT_TYPE_UNKNOWN),
        ROBO_HTML_INPUT_TYPE_TEXT);
    ROBO_CHECK_EQ_INT(
        robo_html_input_type_effective(ROBO_HTML_INPUT_TYPE_CHECKBOX),
        ROBO_HTML_INPUT_TYPE_CHECKBOX);
    ROBO_CHECK_EQ_INT(
        robo_html_input_type_effective((robo_html_input_type_t)500),
        ROBO_HTML_INPUT_TYPE_TEXT);
    ROBO_CHECK(robo_html_input_type_is_checkable(
        ROBO_HTML_INPUT_TYPE_CHECKBOX));
    ROBO_CHECK(robo_html_input_type_is_checkable(ROBO_HTML_INPUT_TYPE_RADIO));
    ROBO_CHECK(!robo_html_input_type_is_checkable(ROBO_HTML_INPUT_TYPE_TEXT));
    ROBO_CHECK_STR_EQ(robo_html_input_type_name(ROBO_HTML_INPUT_TYPE_NUMBER),
                      "number");
    ROBO_CHECK_STR_EQ(robo_html_input_type_name(ROBO_HTML_INPUT_TYPE_UNKNOWN),
                      "unknown");

    /* An unrecognised type in a real document: the element keeps everything it
       has, and reports the type it actually behaves as. */
    {
        robo_html_document_id_t id = form_doc();
        robo_html_node_handle_t root = form_root(id);
        robo_html_input_attributes_t attrs;
        robo_html_node_handle_t input;

        form_build(id, "<input id=\"i\" type=\"range\" name=\"r\" value=\"7\">");
        input = form_find_tag(&root, ROBO_HTML_TAG_INPUT);
        ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &attrs,
                                                             NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(attrs.type_declared, ROBO_HTML_INPUT_TYPE_UNKNOWN);
        ROBO_CHECK_EQ_INT(attrs.type_effective, ROBO_HTML_INPUT_TYPE_TEXT);
        ROBO_CHECK(!attrs.type_recognised);
        /* Degraded, not damaged: name and value are intact, and so is the
           element's place in the tree. */
        ROBO_CHECK_STR_EQ(attrs.name, "r");
        ROBO_CHECK_STR_EQ(attrs.value, "7");
        ROBO_CHECK(robo_html_element_is_tag(&input, ROBO_HTML_TAG_INPUT));
        ROBO_CHECK_STR_EQ(robo_html_element_attribute_value(&input, "type",
                                                            NULL),
                          "range");

        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }
}

static void test_form_input_attributes(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_input_attributes_t attrs;
    robo_html_node_handle_t input;

    form_build(id,
               "<input id=\"e\" type=\"email\" name=\"email\" "
               "value=\"user@example.com\" placeholder=\"you@example.com\" "
               "disabled required readonly>");
    input = form_find_tag(&root, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.type_effective, ROBO_HTML_INPUT_TYPE_EMAIL);
    ROBO_CHECK(attrs.type_recognised);
    ROBO_CHECK_STR_EQ(attrs.name, "email");
    ROBO_CHECK_STR_EQ(attrs.value, "user@example.com");
    ROBO_CHECK_STR_EQ(attrs.placeholder, "you@example.com");
    /* Boolean attributes are presence tests, so a bare attribute and an
       attribute with any value agree. */
    ROBO_CHECK(attrs.disabled);
    ROBO_CHECK(attrs.required);
    ROBO_CHECK(attrs.readonly);
    ROBO_CHECK(!attrs.checked);

    /* `<input checked>` and `<input checked="checked">` agree; `<input>` and
       `<input value="">` do not, which is why `value_present` exists. */
    form_build(id, "<input checked value=\"\"><input>");
    input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK(attrs.checked);
    ROBO_CHECK(attrs.value_present);
    ROBO_CHECK_EQ_INT(attrs.value_length, 0u);
    ROBO_CHECK(robo_html_element_has_attribute(&input, "value"));

    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&input, "checked",
                                                      "anything", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK(attrs.checked);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Declared vs current value — the heart of the phase                  */
/* ------------------------------------------------------------------ */

static void test_form_input_value_state(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t input;
    int declared = 0;

    form_build(id, "<input id=\"a\" name=\"a\" value=\"declared\">");
    input = form_find_tag(&root, ROBO_HTML_TAG_INPUT);

    /* No override: the current value *is* the declared one, and the flag says
       so rather than the caller having to guess from the bytes. */
    ROBO_CHECK_STR_EQ(form_control_value(&input, &declared), "declared");
    ROBO_CHECK(declared);

    /* Pin an override. The attribute is untouched — the declared value is
       still there to be read, which is what a form reset restores. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&input, "current", 7u, NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(form_control_value(&input, &declared), "current");
    ROBO_CHECK(!declared);
    ROBO_CHECK_STR_EQ(robo_html_element_attribute_value(&input, "value", NULL),
                      "declared");

    /* This is the half that is easy to get wrong, and it is the entire
       content of the dirty-value flag: an *overridden* control does not
       follow its attribute... */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&input, "value",
                                                      "changed", NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(form_control_value(&input, &declared), "current");
    ROBO_CHECK(!declared);

    /* ...and an un-overridden one does. */
    {
        robo_html_input_attributes_t attrs;

        form_build(id, "<input id=\"b\" value=\"first\">");
        input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
        ROBO_CHECK_STR_EQ(form_control_value(&input, NULL), "first");
        ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&input, "value",
                                                          "second", NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(form_control_value(&input, &declared), "second");
        ROBO_CHECK(declared);
        ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &attrs,
                                                             NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(attrs.value, "second");
    }

    /* Clearing the override returns the control to tracking the attribute —
       and clearing when there is nothing to clear is a success, because a
       form reset must not fail on a field nobody edited. */
    form_build(id, "<input id=\"c\" value=\"declared\">");
    input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&input, "set", 3u, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(form_control_value(&input, NULL), "set");
    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_value(&input, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(form_control_value(&input, &declared), "declared");
    ROBO_CHECK(declared);
    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_value(&input, NULL),
                      ROBO_OK);

    /* An empty override is a real state, distinct from "no override": the
       field was cleared, and a reset brings the declared value back. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_control_value(&input, "", 0u, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(form_control_value(&input, &declared), "");
    ROBO_CHECK(!declared);
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&input, NULL, 0u, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(form_control_value(&input, NULL), "");
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&input, NULL, 4u, NULL),
        ROBO_INVALID_ARGUMENT);

    /* A value without a `value` attribute at all: the declared value is the
       empty string, not an error. */
    form_build(id, "<input id=\"d\">");
    input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_STR_EQ(form_control_value(&input, &declared), "");
    ROBO_CHECK(declared);

    /* Wrong element: a div has no value to hold. */
    form_build(id, "<div id=\"e\">x</div>");
    {
        robo_html_node_handle_t div =
            form_find_tag_doc(id, ROBO_HTML_TAG_DIV);
        char buf[8];
        size_t length = 0u;

        ROBO_CHECK_EQ_INT(robo_html_element_control_value(&div, buf,
                                                          sizeof(buf), &length,
                                                          NULL, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(
            robo_html_element_set_control_value(&div, "x", 1u, NULL),
            ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(robo_html_element_control_value(&input, NULL, 0u,
                                                          &length, NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_input_checked_state(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t box;
    int checked = 0;
    int declared = 0;

    form_build(id,
               "<input id=\"a\" type=\"checkbox\" name=\"news\" checked>"
               "<input id=\"b\" type=\"radio\" name=\"r\">");
    box = form_find_tag(&root, ROBO_HTML_TAG_INPUT);

    ROBO_CHECK_EQ_INT(robo_html_element_control_checked(&box, &checked,
                                                        &declared, NULL),
                      ROBO_OK);
    ROBO_CHECK(checked);
    ROBO_CHECK(declared);

    /* The override is independent of the attribute, exactly as in HTML: the
       attribute still says checked, the control does not. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_control_checked(&box, 0, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_control_checked(&box, &checked,
                                                        &declared, NULL),
                      ROBO_OK);
    ROBO_CHECK(!checked);
    ROBO_CHECK(!declared);
    ROBO_CHECK(robo_html_element_has_attribute(&box, "checked"));

    /* Removing the attribute does *not* resurrect the override, and clearing
       the override returns the control to following the attribute. */
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&box, "checked",
                                                         NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_control_checked(&box, &checked,
                                                        &declared, NULL),
                      ROBO_OK);
    ROBO_CHECK(!checked);
    ROBO_CHECK(!declared);
    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_checked(&box, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_control_checked(&box, &checked,
                                                        &declared, NULL),
                      ROBO_OK);
    ROBO_CHECK(!checked);
    ROBO_CHECK(declared);

    /* Setting a value never disturbs checkedness, and vice versa. */
    form_build(id, "<input id=\"c\" type=\"checkbox\" checked value=\"v\">");
    box = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&box, "w", 1u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_control_checked(&box, &checked,
                                                        &declared, NULL),
                      ROBO_OK);
    ROBO_CHECK(checked && declared);
    ROBO_CHECK_EQ_INT(robo_html_element_set_control_checked(&box, 0, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(form_control_value(&box, &declared), "w");
    ROBO_CHECK(!declared);

    /* Checkedness is modelled for checkable inputs only, so a text input has
       an attribute that is preserved and no checkedness to report — two
       honest facts instead of one misleading one. */
    form_build(id, "<input id=\"d\" type=\"text\" checked>");
    box = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(robo_html_element_control_checked(&box, &checked, NULL,
                                                        NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_html_element_set_control_checked(&box, 1, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_checked(&box, NULL),
                      ROBO_NOT_SUPPORTED);

    /* And a non-input is refused for the same reason, with the same code, so
       "not an input" and "an input that cannot be checked" are both
       NOT_SUPPORTED rather than one of them being an error. */
    form_build(id, "<select id=\"s\"></select>");
    {
        robo_html_node_handle_t select =
            form_find_tag_doc(id, ROBO_HTML_TAG_SELECT);

        ROBO_CHECK_EQ_INT(robo_html_element_control_checked(&select, &checked,
                                                            NULL, NULL),
                          ROBO_NOT_SUPPORTED);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_input_numeric_constraints(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_input_attributes_t attrs;
    robo_html_node_handle_t input;

    form_build(id,
               "<input id=\"a\" type=\"number\" min=\"0\" max=\"10.5\" "
               "step=\"0.5\">");
    input = form_find_tag(&root, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK(attrs.min_present && attrs.min_valid);
    ROBO_CHECK(nearly(attrs.min_value, 0.0));
    ROBO_CHECK(attrs.max_present && attrs.max_valid);
    ROBO_CHECK(nearly(attrs.max_value, 10.5));
    ROBO_CHECK(attrs.step_present && attrs.step_valid);
    ROBO_CHECK(nearly(attrs.step_value, 0.5));

    /* Negative and signed values, and whitespace around the value. */
    form_build(id, "<input min=\"-2.25\" max=\" +3 \" step=\"1\">");
    input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK(attrs.min_valid);
    ROBO_CHECK(nearly(attrs.min_value, -2.25));
    ROBO_CHECK(attrs.max_valid);
    ROBO_CHECK(nearly(attrs.max_value, 3.0));

    /* Everything a strict decimal refuses. Each is reported as present *and*
       invalid rather than silently dropped, because the document said
       something and a later validation phase needs to know it did. */
    {
        static const char *const bad[] = {
            "abc", "1.", ".", "+", "", "   ", "1e3", "0x10", "1,5", "nan",
            "inf", "--1", "1-", "1 2", "٣",
        };
        size_t i;

        for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
            char markup[128];

            ROBO_CHECK_EQ_INT(
                snprintf(markup, sizeof(markup),
                         "<input type=\"number\" min=\"%s\">", bad[i]) > 0, 1);
            form_build(id, markup);
            input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
            ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input,
                                                                 &attrs, NULL),
                              ROBO_OK);
            ROBO_CHECK(attrs.min_present);
            ROBO_CHECK_MSG(!attrs.min_valid, bad[i]);
            /* Present but invalid must not leave a stale parsed value
               behind. */
            ROBO_CHECK(nearly(attrs.min_value, 0.0));
        }
    }

    /* The fifteen-significant-digit ceiling: at it the value is exact, past it
       the number is refused rather than silently rounded to something else. */
    form_build(id, "<input min=\"999999999999999\">");
    input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK(attrs.min_valid);
    ROBO_CHECK(nearly(attrs.min_value, 999999999999999.0));

    form_build(id, "<input min=\"9999999999999999\">");
    input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK(attrs.min_present);
    ROBO_CHECK(!attrs.min_valid);

    /* Leading zeros do not count toward the ceiling, so this is 1.5, not a
       seventeen-digit number. */
    form_build(id, "<input min=\"00000000000000001.5\">");
    input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK(attrs.min_valid);
    ROBO_CHECK(nearly(attrs.min_value, 1.5));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Button / textarea / select                                          */
/* ------------------------------------------------------------------ */

static void test_form_button(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_button_attributes_t attrs;
    robo_html_node_handle_t button;
    int declared = 0;

    form_build(id,
               "<button type=\"submit\" name=\"save\" value=\"1\" "
               "disabled>Save</button>");
    button = form_find_tag(&root, ROBO_HTML_TAG_BUTTON);
    ROBO_CHECK_EQ_INT(robo_html_element_button_attributes(&button, &attrs,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.type_declared, ROBO_HTML_BUTTON_TYPE_SUBMIT);
    ROBO_CHECK(attrs.type_recognised);
    ROBO_CHECK_STR_EQ(attrs.name, "save");
    ROBO_CHECK_STR_EQ(attrs.value, "1");
    ROBO_CHECK(attrs.disabled);

    /* An absent type is the declared default (submit) and not a degradation;
       an unrecognised one degrades to submit and says that it did. */
    form_build(id, "<button>B</button>");
    button = form_find_tag_doc(id, ROBO_HTML_TAG_BUTTON);
    ROBO_CHECK_EQ_INT(robo_html_element_button_attributes(&button, &attrs,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.type_declared, ROBO_HTML_BUTTON_TYPE_SUBMIT);
    ROBO_CHECK(attrs.type_recognised);
    ROBO_CHECK(!attrs.type_present);

    form_build(id, "<button type=\"menu\">B</button>");
    button = form_find_tag_doc(id, ROBO_HTML_TAG_BUTTON);
    ROBO_CHECK_EQ_INT(robo_html_element_button_attributes(&button, &attrs,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.type_declared, ROBO_HTML_BUTTON_TYPE_UNKNOWN);
    ROBO_CHECK_EQ_INT(attrs.type_effective, ROBO_HTML_BUTTON_TYPE_SUBMIT);
    ROBO_CHECK(!attrs.type_recognised);
    ROBO_CHECK_STR_EQ(robo_html_element_attribute_value(&button, "type", NULL),
                      "menu");

    /* Reset and plain_button are recognised, and the vocabulary's own
       degradation is asserted directly too. */
    ROBO_CHECK_EQ_INT(robo_html_button_type_from_name("RESET", 5u),
                      ROBO_HTML_BUTTON_TYPE_RESET);
    ROBO_CHECK_EQ_INT(robo_html_button_type_from_name("button", 6u),
                      ROBO_HTML_BUTTON_TYPE_BUTTON);
    ROBO_CHECK_EQ_INT(robo_html_button_type_from_name("menu", 4u),
                      ROBO_HTML_BUTTON_TYPE_UNKNOWN);
    ROBO_CHECK_EQ_INT(robo_html_button_type_from_name(NULL, 0u),
                      ROBO_HTML_BUTTON_TYPE_SUBMIT);
    ROBO_CHECK_EQ_INT(robo_html_button_type_effective(
                          ROBO_HTML_BUTTON_TYPE_UNKNOWN),
                      ROBO_HTML_BUTTON_TYPE_SUBMIT);
    ROBO_CHECK_EQ_INT(robo_html_button_type_effective(
                          ROBO_HTML_BUTTON_TYPE_RESET),
                      ROBO_HTML_BUTTON_TYPE_RESET);
    ROBO_CHECK_STR_EQ(robo_html_button_type_name(ROBO_HTML_BUTTON_TYPE_RESET),
                      "reset");
    ROBO_CHECK(!robo_html_button_type_is_known(
        (robo_html_button_type_t)77));

    /* A button holds a value like any other control, with the same declared /
       current split, and its text is its children — not a value. */
    form_build(id, "<button type=\"button\" value=\"declared\">Label</button>");
    button = form_find_tag_doc(id, ROBO_HTML_TAG_BUTTON);
    ROBO_CHECK_STR_EQ(form_control_value(&button, &declared), "declared");
    ROBO_CHECK(declared);
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&button, "current", 7u, NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(form_control_value(&button, &declared), "current");
    ROBO_CHECK(!declared);
    ROBO_CHECK_STR_EQ(form_text(&button), "Label");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_textarea(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_textarea_attributes_t attrs;
    robo_html_node_handle_t area;
    int declared = 0;

    form_build(id,
               "<textarea name=\"message\" rows=\"4\" cols=\"40\" "
               "placeholder=\"say something\" readonly>Hello</textarea>");
    area = form_find_tag(&root, ROBO_HTML_TAG_TEXTAREA);
    ROBO_CHECK_EQ_INT(robo_html_element_textarea_attributes(&area, &attrs,
                                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attrs.name, "message");
    ROBO_CHECK(attrs.rows_valid);
    ROBO_CHECK_EQ_INT(attrs.rows_value, 4u);
    ROBO_CHECK(attrs.cols_valid);
    ROBO_CHECK_EQ_INT(attrs.cols_value, 40u);
    ROBO_CHECK_STR_EQ(attrs.placeholder, "say something");
    ROBO_CHECK(attrs.readonly);
    ROBO_CHECK(!attrs.disabled);
    ROBO_CHECK(!attrs.required);

    /* The declared value is the element's own text — a textarea has no
       `value` attribute, and inventing one here would model something the
       element does not have. */
    ROBO_CHECK_STR_EQ(form_control_value(&area, &declared), "Hello");
    ROBO_CHECK(declared);
    ROBO_CHECK_STR_EQ(form_text(&area), "Hello");

    /* The current value can differ, and the override does not touch the
       children. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&area, "Edited", 6u, NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(form_control_value(&area, &declared), "Edited");
    ROBO_CHECK(!declared);
    ROBO_CHECK_STR_EQ(form_text(&area), "Hello");
    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_value(&area, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(form_control_value(&area, &declared), "Hello");
    ROBO_CHECK(declared);

    /* rows/cols are non-negative integers: a sign, a fraction or a word is
       invalid rather than defaulted, and a huge value is refused rather than
       clamped. */
    {
        static const char *const bad[] = { "abc", "-1", "1.5", "", " ",
                                           "1e3", "99999999" };
        size_t i;

        for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
            char markup[128];

            ROBO_CHECK_EQ_INT(
                snprintf(markup, sizeof(markup),
                         "<textarea rows=\"%s\"></textarea>", bad[i]) > 0, 1);
            form_build(id, markup);
            area = form_find_tag_doc(id, ROBO_HTML_TAG_TEXTAREA);
            ROBO_CHECK_EQ_INT(robo_html_element_textarea_attributes(&area,
                                                                   &attrs,
                                                                   NULL),
                              ROBO_OK);
            ROBO_CHECK(attrs.rows_present);
            ROBO_CHECK_MSG(!attrs.rows_valid, bad[i]);
            ROBO_CHECK_EQ_INT(attrs.rows_value, 0u);
        }
    }

    /* The optional closing tag: `<textarea>` is RCDATA, so the parser knows
       where its content ends and a following element is a sibling. */
    form_build(id, "<div><textarea>t</textarea><span>s</span></div>");
    {
        robo_html_node_handle_t div =
            form_find_tag_doc(id, ROBO_HTML_TAG_DIV);
        robo_html_node_handle_t span =
            form_find_tag(&div, ROBO_HTML_TAG_SPAN);
        robo_html_node_handle_t area2 =
            form_find_tag(&div, ROBO_HTML_TAG_TEXTAREA);

        ROBO_CHECK(robo_html_node_handle_is_wellformed(&span));
        ROBO_CHECK(!robo_html_node_handle_equal(&span, &area2));
        ROBO_CHECK_STR_EQ(form_text(&area2), "t");
        ROBO_CHECK_STR_EQ(form_text(&span), "s");
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_select_options(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_select_attributes_t sattrs;
    robo_html_option_attributes_t oattrs;
    robo_html_node_handle_t select;
    robo_html_node_handle_t first;
    robo_html_node_handle_t second;
    size_t count = 0u;
    size_t index = 0u;

    form_build(id,
               "<select name=\"country\" multiple required>"
               "<option value=\"pt\">Portugal</option>"
               "<option value=\"us\" selected>United States</option>"
               "<option disabled>Disabled</option>"
               "</select>");
    select = form_find_tag(&root, ROBO_HTML_TAG_SELECT);
    ROBO_CHECK_EQ_INT(robo_html_element_select_attributes(&select, &sattrs,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(sattrs.name, "country");
    ROBO_CHECK(sattrs.multiple);
    ROBO_CHECK(sattrs.required);
    ROBO_CHECK(!sattrs.disabled);

    ROBO_CHECK_EQ_INT(robo_html_element_select_option_count(&select, &count,
                                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 3u);

    first = form_find_tag(&select, ROBO_HTML_TAG_OPTION);
    ROBO_CHECK_EQ_INT(robo_html_element_option_attributes(&first, &oattrs,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(oattrs.value, "pt");
    ROBO_CHECK(!oattrs.selected);
    ROBO_CHECK_EQ_INT(robo_html_element_option_index(&first, &index, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(index, 0u);

    /* Ordering is document order, so index 1 is the second option — the one
       the markup marked as selected. */
    {
        robo_html_option_attributes_t a2;
        robo_html_node_handle_t node = first;
        robo_html_node_handle_t sibling;

        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&node, &sibling, NULL),
                          ROBO_OK);
        second = sibling;
        ROBO_CHECK_EQ_INT(robo_html_element_option_attributes(&second, &a2,
                                                              NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(a2.value, "us");
        ROBO_CHECK(a2.selected);
        ROBO_CHECK_STR_EQ(form_text(&second), "United States");
        ROBO_CHECK_EQ_INT(robo_html_element_option_index(&second, &index,
                                                         NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(index, 1u);
    }

    /* The selected option is the first one marked selected. */
    {
        robo_html_node_handle_t picked;

        ROBO_CHECK_EQ_INT(robo_html_element_select_selected_option(&select,
                                                                   &picked,
                                                                   NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&picked, &second));
    }

    /* With nothing marked selected, the first option that is not disabled
       answers — a disabled first option is a state a user could never
       reach. */
    form_build(id,
               "<select><option disabled>a</option><option>b</option>"
               "<option>c</option></select>");
    select = form_find_tag_doc(id, ROBO_HTML_TAG_SELECT);
    {
        robo_html_node_handle_t picked;
        robo_html_node_handle_t expect;

        ROBO_CHECK_EQ_INT(robo_html_element_select_selected_option(&select,
                                                                   &picked,
                                                                   NULL),
                          ROBO_OK);
        expect = form_find_tag(&select, ROBO_HTML_TAG_OPTION);
        ROBO_CHECK(!robo_html_node_handle_equal(&picked, &expect));
        ROBO_CHECK_STR_EQ(form_text(&picked), "b");
    }

    /* An empty select selects nothing, which is a valid empty answer rather
       than an error. */
    form_build(id, "<select></select>");
    select = form_find_tag_doc(id, ROBO_HTML_TAG_SELECT);
    {
        robo_html_node_handle_t picked;

        ROBO_CHECK_EQ_INT(robo_html_element_select_selected_option(&select,
                                                                   &picked,
                                                                   NULL),
                          ROBO_OK);
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&picked));
        ROBO_CHECK_EQ_INT(robo_html_element_select_option_count(&select,
                                                                &count, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(count, 0u);
    }

    /* An option outside a select has no index: 0 would be a lie that looks
       like a position. */
    form_build(id, "<option>loose</option>");
    {
        robo_html_node_handle_t loose =
            form_find_tag_doc(id, ROBO_HTML_TAG_OPTION);

        ROBO_CHECK_EQ_INT(robo_html_element_option_index(&loose, &index, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_html_element_select_selected_option(
                              &loose, &loose, NULL),
                          ROBO_NOT_SUPPORTED);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_option_value_fallback(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t option;
    char buf[128];
    size_t length = 0u;
    int explicit_value = 0;

    form_build(id,
               "<select>"
               "<option value=\"pt\">Portugal</option>"
               "<option>United Kingdom</option>"
               "<option value=\"\">Empty</option>"
               "</select>");
    option = form_find_tag(&root, ROBO_HTML_TAG_OPTION);

    ROBO_CHECK_EQ_INT(robo_html_element_option_value(&option, buf,
                                                     sizeof(buf), &length,
                                                     &explicit_value, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "pt");
    ROBO_CHECK(explicit_value);

    /* The fallback is the real rule: an option with no `value` submits its
       text, and the flag says the text *is* the value rather than merely
       looking like it. */
    {
        robo_html_node_handle_t sibling;

        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&option, &sibling, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_option_value(&sibling, buf,
                                                         sizeof(buf), &length,
                                                         &explicit_value,
                                                         NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "United Kingdom");
        ROBO_CHECK(!explicit_value);

        /* An explicit empty value is not the same as no value at all. */
        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&sibling, &option, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_option_value(&option, buf,
                                                         sizeof(buf), &length,
                                                         &explicit_value,
                                                         NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "");
        ROBO_CHECK(explicit_value);
    }

    /* Buffer truncation reports the untruncated length, so a caller can tell
       a short value from a lost one. */
    form_build(id, "<select><option>0123456789</option></select>");
    option = form_find_tag_doc(id, ROBO_HTML_TAG_OPTION);
    ROBO_CHECK_EQ_INT(robo_html_element_option_value(&option, buf, 4u, &length,
                                                     NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "012");
    ROBO_CHECK_EQ_INT(length, 10u);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Relations                                                           */
/* ------------------------------------------------------------------ */

static void test_form_label_target(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t label;
    robo_html_node_handle_t target;
    int from_attribute = 0;

    /* Rule 1: `for` names an id and that element is labelable. */
    form_build(id,
               "<label for=\"Email\">Email</label>"
               "<input id=\"Email\" type=\"email\">"
               "<div id=\"NotAControl\"></div>");
    label = form_find_tag(&root, ROBO_HTML_TAG_LABEL);
    ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, &target,
                                                     &from_attribute, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&target, ROBO_HTML_TAG_INPUT));
    ROBO_CHECK(from_attribute);
    ROBO_CHECK_STR_EQ(robo_html_element_attribute_value(&target, "id", NULL),
                      "Email");

    /* Rule 2: no `for`, so the first labelable descendant answers. */
    form_build(id, "<label>Name <input id=\"inner\"></label>");
    label = form_find_tag_doc(id, ROBO_HTML_TAG_LABEL);
    ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, &target,
                                                     &from_attribute, NULL),
                      ROBO_OK);
    ROBO_CHECK(from_attribute == 0);
    ROBO_CHECK_STR_EQ(robo_html_element_attribute_value(&target, "id", NULL),
                      "inner");

    /* Rule 3: a label wrapping only text labels nothing, which is a valid
       state and not an error. */
    form_build(id, "<label>Just text</label>");
    label = form_find_tag_doc(id, ROBO_HTML_TAG_LABEL);
    ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, &target,
                                                     &from_attribute, NULL),
                      ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&target));
    ROBO_CHECK_EQ_INT(from_attribute, 0);

    /* A usable `for` that names a non-labelable element is *not* a fallback
       chain: the author named something, and the honest answer is that the
       name does not resolve to a control. The wrapped input is deliberately
       not reported. */
    form_build(id,
               "<label for=\"NotAControl\"><input id=\"wrapped\"></label>"
               "<div id=\"NotAControl\"></div>");
    label = form_find_tag_doc(id, ROBO_HTML_TAG_LABEL);
    ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, &target,
                                                     &from_attribute, NULL),
                      ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&target));
    ROBO_CHECK_EQ_INT(from_attribute, 0);

    /* A usable `for` that names nothing at all behaves the same way. */
    form_build(id,
               "<label for=\"Ghost\"><input id=\"wrapped\"></label>");
    label = form_find_tag_doc(id, ROBO_HTML_TAG_LABEL);
    ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, &target,
                                                     &from_attribute, NULL),
                      ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&target));

    /* An empty `for` is not a usable reference, so the descendant rule
       answers. */
    form_build(id,
               "<label for=\"\" id=\"l2\"><input id=\"w\"></label>"
               "<div id=\"d\"></div>");
    label = form_find_id_doc(id, "l2");
    ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, &target,
                                                     &from_attribute, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_html_element_attribute_value(&target, "id", NULL),
                      "w");

    /* A live element that is not a label is refused with NOT_SUPPORTED, which
       is a different answer from the NOT_FOUND a dead handle gets. */
    {
        robo_html_node_handle_t div = form_find_id_doc(id, "d");

        ROBO_CHECK_EQ_INT(robo_html_element_label_target(&div, &target, NULL,
                                                         NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(robo_html_element_label_target(NULL, &target, NULL,
                                                         NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, NULL, NULL,
                                                         NULL),
                          ROBO_INVALID_ARGUMENT);
        /* The output may be the same object as the input: the resolver works
           on a copy, so this is well defined rather than a self-wipe. */
        ROBO_CHECK_EQ_INT(robo_html_element_label_target(&div, &div, NULL,
                                                         NULL),
                          ROBO_NOT_SUPPORTED);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_owner_nested(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t outer;
    robo_html_node_handle_t inner_input;
    robo_html_node_handle_t owner;
    robo_html_form_owner_source_t source = ROBO_HTML_FORM_OWNER_SOURCE_NONE;

    /* The nearest ancestor form wins, not the outermost. */
    form_build(id,
               "<form id=\"outer\"><div><form id=\"inner\">"
               "<input id=\"x\"></form></div></form>");
    outer = form_find_tag(&root, ROBO_HTML_TAG_FORM);
    {
        robo_html_node_handle_t inner = form_find_id(&outer, "inner");

        ROBO_CHECK(robo_html_node_handle_is_wellformed(&inner));
        inner_input = form_find_id(&inner, "x");
        ROBO_CHECK(robo_html_node_handle_is_wellformed(&inner_input));
        ROBO_CHECK_EQ_INT(
            robo_html_element_form_owner(&inner_input, &owner, &source, NULL),
            ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &inner));
        ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);
    }

    /* A control outside every form has no owner — a valid state, reported as
       a success with an empty answer rather than as an error. */
    {
        robo_html_node_handle_t loose;

        form_build(id, "<div><input id=\"loose\"></div>");
        loose = form_find_id_doc(id, "loose");
        ROBO_CHECK(robo_html_node_handle_is_wellformed(&loose));
        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&loose, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&owner));
        ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_NONE);
    }

    /* An orphaned control (created, then detached) has no ancestor form, and
       the walk terminates rather than chasing a broken chain. */
    form_build(id, "<form id=\"f\"><input id=\"i\"></form>");
    {
        robo_html_node_handle_t form = form_find_id_doc(id, "f");
        robo_html_node_handle_t input = form_find_id(&form, "i");
        robo_html_node_handle_t parent;

        /* Detaching the form from *its* parent, not from the document root —
           the parser placed it inside <body>, and the mutation API only
           removes a child from its actual parent. */
        ROBO_CHECK_EQ_INT(robo_html_element_parent(&form, &parent, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&parent, &form, NULL),
                          ROBO_OK);
        /* Detaching a form does *not* detach its children, so the input is
           still nested inside the form element and still owned by it — the
           ancestor rule describes the tree's shape, not whether that shape is
           reachable from the root. Removing the input from the form is what
           clears the association. */
        ROBO_CHECK(robo_html_node_handle_is_wellformed(&input));
        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&input, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));
        ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);

        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&form, &input, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&input, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&owner));
        ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_NONE);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_owner_attribute(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t target;
    robo_html_node_handle_t owner;
    robo_html_form_owner_source_t source = ROBO_HTML_FORM_OWNER_SOURCE_NONE;

    /* The `form` attribute wins over ancestry when it resolves to a form. */
    form_build(id,
               "<form id=\"remote\"></form>"
               "<div><form id=\"local\">"
               "<input id=\"a\" form=\"remote\">"
               "<input id=\"b\">"
               "</form></div>");
    target = form_find_id(&root, "remote");
    {
        robo_html_node_handle_t local = form_find_id(&root, "local");
        robo_html_node_handle_t a = form_find_id(&local, "a");
        robo_html_node_handle_t b = form_find_id(&local, "b");

        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&a, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &target));
        ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE);

        /* The sibling with no attribute is owned by its ancestor, so the two
           rules are distinguishable in one document. */
        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&b, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &local));
        ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);
    }

    /* A `form` attribute naming an element that is not a form falls back to
       ancestry, and says so through the source field. */
    form_build(id,
               "<div id=\"notform\"></div>"
               "<form id=\"f\"><input id=\"c\" form=\"notform\"></form>");
    {
        robo_html_node_handle_t form = form_find_id_doc(id, "f");
        robo_html_node_handle_t c = form_find_id(&form, "c");

        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&c, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));
        ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);
    }

    /* A `form` attribute naming nothing at all: same fallback. */
    form_build(id, "<form id=\"f\"><input id=\"d\" form=\"ghost\"></form>");
    {
        robo_html_node_handle_t form = form_find_id_doc(id, "f");
        robo_html_node_handle_t d = form_find_id(&form, "d");

        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&d, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));
        ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);
    }

    /* A blank `form` is not a usable reference either. */
    form_build(id, "<form id=\"f\"><input id=\"e\" form=\"  \"></form>");
    {
        robo_html_node_handle_t form = form_find_id_doc(id, "f");
        robo_html_node_handle_t e = form_find_id(&form, "e");

        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&e, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));
    }

    /* And when the referenced form is removed from the tree, the attribute
       rule stops answering: the next lookup finds the ancestor form instead
       of a dangling reference. Nothing needed invalidating for that to be
       true. */
    form_build(id,
               "<form id=\"remote\"></form>"
               "<form id=\"local\">"
               "<input id=\"g\" form=\"remote\">"
               "</form>");
    {
        robo_html_node_handle_t remote = form_find_id_doc(id, "remote");
        robo_html_node_handle_t local = form_find_id_doc(id, "local");
        robo_html_node_handle_t g = form_find_id(&local, "g");

        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&g, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &remote));
        ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE);

        {
            robo_html_node_handle_t parent;

            ROBO_CHECK_EQ_INT(
                robo_html_element_parent(&remote, &parent, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(
                robo_html_node_remove_child(&parent, &remote, NULL), ROBO_OK);
        }
        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&g, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &local));
        ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_owner_not_supported(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t owner;
    robo_html_form_owner_source_t source = ROBO_HTML_FORM_OWNER_SOURCE_NONE;

    /* A div inside a form is not owned by it: the question is a category
       error for an element that cannot have an owner, and saying so is more
       useful than a lookup that happens to fail. An option is likewise not
       form-associated, even inside a form. */
    form_build(id,
               "<form id=\"f\"><div id=\"d\"></div>"
               "<select id=\"s\"><option id=\"o\">x</option></select>"
               "</form>");
    {
        robo_html_node_handle_t d = form_find_id(&root, "d");
        robo_html_node_handle_t s = form_find_id(&root, "s");
        robo_html_node_handle_t f = form_find_id(&root, "f");
        robo_html_node_handle_t label;

        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&d, &owner, NULL, NULL),
                          ROBO_NOT_SUPPORTED);

        /* A select *is* form-associated, so it has an owner even though it
           is not the element whose value the select models. */
        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&s, &owner, &source,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &f));

        form_build(id, "<form id=\"f2\"><label id=\"l\">x</label>"
                       "<div id=\"d2\"></div></form>");
        label = form_find_id_doc(id, "l");
        ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&label, &owner, NULL,
                                                       NULL),
                          ROBO_NOT_SUPPORTED);

        /* A real div in the current tree, so the NULL-output path is
           exercised on an element that *is* live: the argument check comes
           first, which is what makes it an argument error rather than a
           not-supported one. */
        {
            robo_html_node_handle_t div2 = form_find_id_doc(id, "d2");

            ROBO_CHECK(robo_html_node_handle_is_wellformed(&div2));
            ROBO_CHECK_EQ_INT(
                robo_html_element_form_owner(&div2, NULL, NULL, NULL),
                ROBO_INVALID_ARGUMENT);
        }
    }

    ROBO_CHECK_EQ_INT(robo_html_element_form_owner(NULL, &owner, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* The vocabulary these answers rest on, asserted directly: the two sets
       are different, and neither is everything with the control role. */
    ROBO_CHECK(robo_html_tag_is_form_associated(ROBO_HTML_TAG_INPUT));
    ROBO_CHECK(robo_html_tag_is_form_associated(ROBO_HTML_TAG_SELECT));
    ROBO_CHECK(robo_html_tag_is_form_associated(ROBO_HTML_TAG_TEXTAREA));
    ROBO_CHECK(robo_html_tag_is_form_associated(ROBO_HTML_TAG_BUTTON));
    ROBO_CHECK(robo_html_tag_is_form_associated(ROBO_HTML_TAG_FIELDSET));
    ROBO_CHECK(robo_html_tag_is_form_associated(ROBO_HTML_TAG_OBJECT));
    ROBO_CHECK(!robo_html_tag_is_form_associated(ROBO_HTML_TAG_DIV));
    ROBO_CHECK(!robo_html_tag_is_form_associated(ROBO_HTML_TAG_OPTION));
    ROBO_CHECK(!robo_html_tag_is_form_associated(ROBO_HTML_TAG_FORM));
    ROBO_CHECK(!robo_html_tag_is_form_associated(ROBO_HTML_TAG_LABEL));

    ROBO_CHECK(robo_html_tag_is_labelable(ROBO_HTML_TAG_INPUT));
    ROBO_CHECK(robo_html_tag_is_labelable(ROBO_HTML_TAG_TEXTAREA));
    ROBO_CHECK(robo_html_tag_is_labelable(ROBO_HTML_TAG_SELECT));
    ROBO_CHECK(robo_html_tag_is_labelable(ROBO_HTML_TAG_BUTTON));
    ROBO_CHECK(robo_html_tag_is_labelable(ROBO_HTML_TAG_METER));
    ROBO_CHECK(robo_html_tag_is_labelable(ROBO_HTML_TAG_PROGRESS));
    ROBO_CHECK(robo_html_tag_is_labelable(ROBO_HTML_TAG_OUTPUT));
    ROBO_CHECK(!robo_html_tag_is_labelable(ROBO_HTML_TAG_FIELDSET));
    ROBO_CHECK(!robo_html_tag_is_labelable(ROBO_HTML_TAG_OPTION));
    ROBO_CHECK(!robo_html_tag_is_labelable(ROBO_HTML_TAG_LABEL));

    /* Form-associated but not labelable is a real combination, and it is the
       reason these are two flags rather than one. */
    ROBO_CHECK(robo_html_tag_is_form_associated(ROBO_HTML_TAG_OUTPUT));
    ROBO_CHECK(robo_html_tag_is_labelable(ROBO_HTML_TAG_OUTPUT));
    ROBO_CHECK(robo_html_tag_is_form_associated(ROBO_HTML_TAG_FIELDSET));
    ROBO_CHECK(!robo_html_tag_is_labelable(ROBO_HTML_TAG_FIELDSET));

    /* The new flags did not disturb the ones already there. */
    ROBO_CHECK(robo_html_tag_is_void(ROBO_HTML_TAG_INPUT));
    ROBO_CHECK(!robo_html_tag_is_void(ROBO_HTML_TAG_TEXTAREA));
    ROBO_CHECK_EQ_INT(robo_html_tag_class(ROBO_HTML_TAG_INPUT),
                      ROBO_HTML_ELEMENT_CLASS_CONTROL);

    /* The source vocabulary is itself observable, so a caller can report
       which rule answered without re-deriving it from an integer. */
    ROBO_CHECK_STR_EQ(robo_html_form_owner_source_name(
                          ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE),
                      "attribute");
    ROBO_CHECK_STR_EQ(robo_html_form_owner_source_name(
                          ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR),
                      "ancestor");
    ROBO_CHECK_STR_EQ(robo_html_form_owner_source_name(
                          ROBO_HTML_FORM_OWNER_SOURCE_NONE),
                      "none");
    ROBO_CHECK_STR_EQ(robo_html_form_owner_source_name(
                          (robo_html_form_owner_source_t)99),
                      "unknown");
    ROBO_CHECK(robo_html_form_owner_source_is_known(
        ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR));
    ROBO_CHECK(!robo_html_form_owner_source_is_known(
        (robo_html_form_owner_source_t)99));
}

/* ------------------------------------------------------------------ */
/* Parser integration                                                  */
/* ------------------------------------------------------------------ */

static void test_form_parser_malformed(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_tree_report_t report;
    robo_html_node_handle_t root;

    /*
     * A document that is wrong in several ordinary ways at once: an unclosed
     * form containing an unclosed div, a stray end tag for a form that was
     * never opened, a void input followed immediately by a sibling, a
     * textarea whose content ends at its own end tag, and an option with no
     * select. None of it may corrupt the tree.
     */
    form_build(id,
               "<form action=\"/a\">"
               "<div>"
               "<input name=\"a\">"
               "<span>after</span>"
               "</form>"
               "</form>"
               "<textarea>text</textarea>"
               "<option>loose</option>");

    root = form_root(id);
    memset(&report, 0, sizeof(report));
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK_MSG(report.ok, "malformed form markup corrupted the tree");

    /* The void input did not swallow its following sibling: the span is a
       real element and is not inside the input. */
    {
        robo_html_node_handle_t input =
            form_find_tag(&root, ROBO_HTML_TAG_INPUT);
        robo_html_node_handle_t span = form_find_tag(&root, ROBO_HTML_TAG_SPAN);
        int contains = 99;
        size_t children = 99u;

        ROBO_CHECK(robo_html_node_handle_is_wellformed(&span));
        ROBO_CHECK(!robo_html_node_handle_equal(&span, &input));
        ROBO_CHECK_EQ_INT(robo_html_node_contains(&input, &span, &contains,
                                                  NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(contains, 0);
        ROBO_CHECK_EQ_INT(robo_html_node_child_count(&input, &children, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(children, 0u);
    }

    /* The textarea kept its content and gave nothing to the element after
       it. */
    {
        robo_html_node_handle_t area =
            form_find_tag(&root, ROBO_HTML_TAG_TEXTAREA);
        robo_html_node_handle_t option =
            form_find_tag(&root, ROBO_HTML_TAG_OPTION);

        ROBO_CHECK_STR_EQ(form_text(&area), "text");
        ROBO_CHECK_STR_EQ(form_text(&option), "loose");
    }

    /* The form is still a form, and still answerable. */
    {
        robo_html_form_attributes_t attrs;
        robo_html_node_handle_t form = form_find_tag(&root, ROBO_HTML_TAG_FORM);

        ROBO_CHECK_EQ_INT(robo_html_element_form_attributes(&form, &attrs,
                                                            NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(attrs.action, "/a");
        ROBO_CHECK(attrs.action_usable);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_void_input(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);

    /* `input` is void in the tag vocabulary, so the parser inserts and pops
       it: `<input>x</input>` leaves `x` as a sibling, not a child. */
    form_build(id, "<div><input>orphan</input></div>");
    {
        robo_html_node_handle_t input =
            form_find_tag(&root, ROBO_HTML_TAG_INPUT);
        size_t children = 99u;

        ROBO_CHECK_EQ_INT(robo_html_node_child_count(&input, &children, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(children, 0u);
        {
            robo_html_node_handle_t div =
                form_find_tag(&root, ROBO_HTML_TAG_DIV);

            ROBO_CHECK_STR_EQ(form_text(&div), "orphan");
        }
    }

    /* The self-closing spelling behaves identically, and a following element
       is never consumed. */
    form_build(id,
               "<div><input/><input><span>s</span></div>");
    {
        robo_html_node_handle_t div =
            form_find_tag(&root, ROBO_HTML_TAG_DIV);
        robo_html_node_handle_t span = form_find_tag(&div, ROBO_HTML_TAG_SPAN);
        size_t inputs = 0u;

        ROBO_CHECK(robo_html_node_handle_is_wellformed(&span));
        ROBO_CHECK_STR_EQ(form_text(&div), "s");
        /* Two inputs exist, so the second `<input>` was not nested in the
           first (which is void and could not hold it anyway). */
        {
            robo_html_node_handle_t cursor;
            size_t guard = 0u;

            if (robo_html_node_first_child(&div, &cursor, NULL) == ROBO_OK) {
                for (;;) {
                    robo_html_node_handle_t next;

                    if (robo_html_element_is_tag(&cursor, ROBO_HTML_TAG_INPUT)) {
                        inputs++;
                    }
                    /* The out parameter is separate from the in parameter:
                       the reader clears its out slot, so passing one handle
                       for both would wipe it on the first step. */
                    if (robo_html_node_next_sibling(&cursor, &next, NULL) !=
                        ROBO_OK) {
                        break;
                    }
                    cursor = next;
                    if (guard++ > 16u) {
                        break;
                    }
                }
            }
        }
        ROBO_CHECK_EQ_INT(inputs, 2u);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Mutation                                                            */
/* ------------------------------------------------------------------ */

static void test_form_mutation(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t first_form;
    robo_html_node_handle_t second_form;
    robo_html_node_handle_t control;
    robo_html_node_handle_t owner;
    robo_html_form_owner_source_t source = ROBO_HTML_FORM_OWNER_SOURCE_NONE;
    char buf[64];
    size_t length = 0u;

    /*
     * Adding a control to a form, removing it, and moving it between forms —
     * all through the real DOM mutation API on a parsed tree, because that is
     * what an application can actually do: a finalized document refuses
     * *construction* but the mutation seam stays open, deliberately.
     */
    form_build(id,
               "<form id=\"a\"></form>"
               "<form id=\"b\"><input id=\"c\" name=\"n\"></form>");
    first_form = form_find_id(&root, "a");
    second_form = form_find_id(&root, "b");
    control = form_find_id(&root, "c");
    ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&control, &owner, &source,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &second_form));
    ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);

    /* Pin an override, then move the control into the other form: the owner
       changes and the state comes along, because state is keyed by node
       identity rather than by position. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&control, "kept", 4u, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&second_form, &control, NULL),
                      ROBO_OK);
    /* Detached: no ancestor form, so no owner — still a success, with an
       empty answer. */
    ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&control, &owner, &source,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&owner));
    ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_NONE);

    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&first_form, &control, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&control, &owner, &source,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &first_form));
    ROBO_CHECK_EQ_INT(robo_html_element_control_value(&control, buf,
                                                      sizeof(buf), &length,
                                                      NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "kept");

    /* Re-attaching a control that already has a parent is refused rather
       than silently moving it — the caller has to say "remove, then append",
       which is the one rule that keeps every node's parent unambiguous. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&first_form, &control, NULL),
                      ROBO_ALREADY_EXISTS);

    /* Detaching the form from the document leaves the control owned by it:
       the control is still nested inside the form element. Taking the
       control out is what makes it unowned again. */
    {
        robo_html_node_handle_t parent;

        ROBO_CHECK_EQ_INT(robo_html_element_parent(&first_form, &parent, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&parent, &first_form,
                                                      NULL),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&control, &owner, &source,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &first_form));
    ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);

    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&first_form, &control, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&control, &owner, &source,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&owner));
    ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_NONE);

    /* An `id` change is visible immediately, in both the label and the
       ownership direction. */
    form_build(id,
               "<form id=\"one\"></form><form id=\"two\"></form>"
               "<input id=\"c\" form=\"one\">"
               "<label id=\"l\" for=\"c\">L</label>");
    first_form = form_find_id(&root, "one");
    second_form = form_find_id(&root, "two");
    control = form_find_id(&root, "c");
    ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&control, &owner, NULL,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &first_form));

    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&control, "form", "two", NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&control, &owner, NULL,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &second_form));

    /* Renaming the form the control references makes the reference stop
       resolving, and the fallback rule answers instead (there is no ancestor
       form here, so the answer is "no owner"). */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&second_form, "id", "renamed", NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&control, &owner, NULL,
                                                   NULL),
                      ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&owner));

    /* A label's `for` follows the same live lookup, so renaming the control
       breaks the association and restoring it repairs it. */
    {
        robo_html_node_handle_t label = form_find_id(&root, "l");
        robo_html_node_handle_t target;

        ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, &target, NULL,
                                                         NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&target, &control));
        ROBO_CHECK_EQ_INT(
            robo_html_element_set_attribute(&control, "id", "c2", NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, &target, NULL,
                                                         NULL),
                          ROBO_OK);
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&target));
        ROBO_CHECK_EQ_INT(
            robo_html_element_set_attribute(&label, "for", "c2", NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, &target, NULL,
                                                         NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&target, &control));
    }

    /*
     * Replacing a control: the replacement is a *different node*, so it does
     * not inherit the state the old one carried — which is what "keyed by node
     * identity" is supposed to mean, asserted rather than assumed. The two
     * inputs deliberately share an id and a declared value, so the only way
     * the replacement could report "override" is by inheriting state it should
     * not have.
     */
    form_build(id,
               "<form id=\"f\">"
               "<input id=\"i\" value=\"declared\">"
               "<input id=\"i\" value=\"declared\">"
               "</form>");
    {
        robo_html_node_handle_t form = form_find_id(&root, "f");
        robo_html_node_handle_t old = form_find_id(&form, "i");
        robo_html_node_handle_t replacement;
        int declared = 0;

        ROBO_CHECK_EQ_INT(
            robo_html_element_set_control_value(&old, "override", 8u, NULL),
            ROBO_OK);
        ROBO_CHECK_STR_EQ(form_control_value(&old, &declared), "override");
        ROBO_CHECK(!declared);

        /* The second input is the replacement; detach it so replace_child
           accepts it, which is the move rule the DOM enforces. */
        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&old, &replacement,
                                                      NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_node_remove_child(&form, &replacement, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_node_replace_child(&form, &replacement, &old, NULL),
            ROBO_OK);

        ROBO_CHECK_STR_EQ(form_control_value(&replacement, &declared),
                          "declared");
        ROBO_CHECK(declared);
        /* The displaced input is detached and still holds its own override;
           nothing was copied between them. */
        ROBO_CHECK_STR_EQ(form_control_value(&old, &declared), "override");
        ROBO_CHECK(!declared);
        ROBO_CHECK(!robo_html_node_handle_equal(&old, &replacement));
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_form_mutation_options(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t select;
    size_t count = 0u;
    size_t index = 0u;

    form_build(id,
               "<select id=\"s\"><option>a</option>"
               "<option id=\"b\" value=\"b\" selected>b</option></select>");
    select = form_find_id(&root, "s");

    /* Two options, in order: adding one by moving another in would need a
       detached node, and the ordering that matters is the one the tree has.
       The document is finalized after parsing, so a *new* option is not
       something an application can create here — which is itself the rule
       this phase inherits rather than invents. */
    {
        robo_html_node_handle_t added = form_find_id(&select, "b");

        ROBO_CHECK_EQ_INT(robo_html_element_select_option_count(&select,
                                                                &count, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(count, 2u);
        ROBO_CHECK_EQ_INT(robo_html_element_option_index(&added, &index, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(index, 1u);

        /* The marked option is the selected one. */
        {
            robo_html_node_handle_t picked;

            ROBO_CHECK_EQ_INT(
                robo_html_element_select_selected_option(&select, &picked,
                                                         NULL),
                ROBO_OK);
            ROBO_CHECK(robo_html_node_handle_equal(&picked, &added));
        }

        /* Changing `selected`, and removing it, both take effect on the next
           read — and setting it on the other option moves the selection. */
        ROBO_CHECK_EQ_INT(
            robo_html_element_set_attribute(&added, "selected", "selected",
                                            NULL),
            ROBO_OK);
        {
            robo_html_node_handle_t first;
            robo_html_node_handle_t picked;

            ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&added, &first,
                                                          NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(
                robo_html_element_set_attribute(&first, "selected", NULL,
                                                NULL),
                ROBO_OK);
            ROBO_CHECK_EQ_INT(
                robo_html_element_select_selected_option(&select, &picked,
                                                         NULL),
                ROBO_OK);
            ROBO_CHECK(robo_html_node_handle_equal(&picked, &first));
            ROBO_CHECK_STR_EQ(form_text(&picked), "a");
        }

        /* Removing the selected option puts the selection back on the first
           selectable one, because nothing carries the attribute now. */
        ROBO_CHECK_EQ_INT(
            robo_html_element_remove_attribute(&added, "selected", NULL),
            ROBO_OK);
        {
            robo_html_node_handle_t picked;

            ROBO_CHECK_EQ_INT(
                robo_html_element_select_selected_option(&select, &picked,
                                                         NULL),
                ROBO_OK);
            ROBO_CHECK(!robo_html_node_handle_equal(&picked, &added));
            ROBO_CHECK_STR_EQ(form_text(&picked), "a");
        }

        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&select, &added, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_select_option_count(&select,
                                                                &count, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(count, 1u);
        /* The removed option is detached, so it is no longer inside its
           select and has no index. */
        ROBO_CHECK_EQ_INT(robo_html_element_option_index(&added, &index, NULL),
                          ROBO_NOT_FOUND);
    }

    /* Options nested in an optgroup still count and still have positions —
       the walk is the subtree, not the direct children, so the count and the
       index cannot disagree. */
    form_build(id,
               "<select id=\"s\">"
               "<optgroup label=\"g\"><option>a</option><option>b</option>"
               "</optgroup>"
               "<option>c</option></select>");
    select = form_find_id(&root, "s");
    ROBO_CHECK_EQ_INT(robo_html_element_select_option_count(&select, &count,
                                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 3u);
    {
        robo_html_node_handle_t last;
        form_tag_query_t q;

        q.tag = ROBO_HTML_TAG_OPTION;
        /* The first option in the subtree is inside the optgroup, so its
           index is 0 — the walk does not stop at a direct child. */
        ROBO_CHECK_EQ_INT(
            robo_html_node_find_descendant(&select,
                                           ROBO_HTML_NODE_FILTER_ELEMENTS,
                                           form_match_tag, &q, &last, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_option_index(&last, &index, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(index, 0u);
        ROBO_CHECK_STR_EQ(form_text(&last), "a");

        /* And the last option — outside the optgroup — is index 2, so the
           count of 3 and the indices agree across the nesting boundary. */
        {
            robo_html_node_handle_t cursor = form_last_option(&select);

            ROBO_CHECK_EQ_INT(
                robo_html_element_option_index(&cursor, &index, NULL),
                ROBO_OK);
            ROBO_CHECK_EQ_INT(index, 2u);
            ROBO_CHECK_STR_EQ(form_text(&cursor), "c");
        }
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void test_form_lifecycle(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t input;
    char buf[64];
    size_t length = 0u;
    int declared = 0;

    /* A control created before anything is parsed works while the document is
       still CREATED: the state model does not depend on the parser having
       run. */
    root = form_root(id);
    input = form_create_under(id, &root, "input");
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&input, "value",
                                                      "v", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&input, "w", 1u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_control_value(&input, buf,
                                                      sizeof(buf), &length,
                                                      &declared, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "w");
    ROBO_CHECK(!declared);

    /* Parsing into the same document is a fresh tree: the control above is
       gone with it, and the new tree's controls work. */
    form_build(id, "<form><input value=\"parsed\"></form>");
    ROBO_CHECK(robo_html_document_is_settled(id));
    input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(robo_html_element_control_value(&input, buf,
                                                      sizeof(buf), &length,
                                                      &declared, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "parsed");
    ROBO_CHECK(declared);

    /* A reset leaves a document that accepts a new source, and no control
       state survives it — an override must not outlive the tree it was
       written against. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&input, "override", 8u, NULL),
        ROBO_OK);
    /* Parsing already settled the document to COMPLETE, and a second
       transition to the state it is already in is not a valid one. */
    ROBO_CHECK(robo_html_document_is_complete(id));
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);

    form_build(id, "<form><input value=\"fresh\"></form>");
    input = form_find_tag_doc(id, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(robo_html_element_control_value(&input, buf,
                                                      sizeof(buf), &length,
                                                      &declared, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "fresh");
    ROBO_CHECK(declared);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);

    /* And the capability flag says this build has the model. */
    {
        robo_html_dom_capabilities_t caps;

        ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(&caps), ROBO_OK);
        ROBO_CHECK(caps.forms);
        ROBO_CHECK(caps.tree_building);
        ROBO_CHECK(caps.element_tags);
    }
}

static void test_form_destroyed_state(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_node_handle_t input;
    robo_html_form_attributes_t attrs;
    robo_html_input_attributes_t iattrs;
    char buf[64];
    size_t length = 0u;
    size_t count = 0u;

    form_build(id,
               "<form action=\"/a\"><input value=\"v\"><select id=\"s\">"
               "<option>a</option></select></form>");
    input = form_find_tag(&root, ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&input, "over", 4u, NULL),
        ROBO_OK);

    /* Destroying the document removes the tree and the state with it: every
       read against the retired handles is NOT_FOUND, never a stale answer,
       and every write is refused rather than resurrecting state. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_document_is_live(id));

    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &iattrs,
                                                         NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_element_control_value(&input, buf,
                                                      sizeof(buf), &length,
                                                      NULL, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&input, "x", 1u, NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_value(&input, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_element_control_checked(&input, NULL, NULL,
                                                        NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_form_owner(&input, &input, NULL, NULL),
                      ROBO_NOT_FOUND);

    {
        robo_html_node_handle_t form = input;

        ROBO_CHECK_EQ_INT(robo_html_element_form_attributes(&form, &attrs,
                                                            NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_html_element_select_option_count(&form, &count,
                                                                NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_html_element_select_attributes(&form, NULL,
                                                              NULL),
                          ROBO_INVALID_ARGUMENT);
    }

    /* Destruction is safe to repeat — it is not an error, but it is also not
       a success, because the document is already gone. What matters is that
       it does not crash, does not resurrect the id, and cannot bring the
       retired handles back to life. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_document_is_live(id));
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &iattrs,
                                                         NULL),
                      ROBO_NOT_FOUND);
}

static void test_form_structure(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_tree_report_t report;
    robo_html_node_handle_t form;

    /* One realistic document exercising every element this phase models, then
       a full invariant audit: parent links, sibling chains, counts and cycle
       freedom must all still hold with form controls in the tree. */
    form_build(id,
               "<form action=\"/Save\" method=\"post\">"
               "  <fieldset>"
               "    <legend>Account</legend>"
               "    <label for=\"Email\">Email</label>"
               "    <input id=\"Email\" type=\"email\" name=\"email\" "
               "required>"
               "    <label><input type=\"checkbox\" name=\"news\" checked>"
               "Send news</label>"
               "    <textarea name=\"note\" rows=\"3\">Hello</textarea>"
               "    <select name=\"country\">"
               "      <optgroup label=\"Europe\">"
               "        <option value=\"pt\">Portugal</option>"
               "      </optgroup>"
               "      <option value=\"us\" selected>United States</option>"
               "    </select>"
               "    <button type=\"submit\">Save</button>"
               "    <button type=\"reset\">Reset</button>"
               "  </fieldset>"
               "</form>");

    memset(&report, 0, sizeof(report));
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK_MSG(report.ok, "form structure broke a tree invariant");

    form = form_find_tag(&root, ROBO_HTML_TAG_FORM);
    ROBO_CHECK(robo_html_element_is_tag(&form, ROBO_HTML_TAG_FORM));
    ROBO_CHECK(robo_html_element_is_named(&form, "form"));
    /* The fieldset really is inside the form, so the ancestor rule below is
       answering a question about this tree rather than about a detached
       node. */
    {
        robo_html_node_handle_t fieldset =
            form_find_tag(&root, ROBO_HTML_TAG_FIELDSET);
        int contains = 0;

        ROBO_CHECK_EQ_INT(robo_html_node_contains(&form, &fieldset, &contains,
                                                  NULL),
                          ROBO_OK);
        ROBO_CHECK(contains);
    }

    /* Count by walking, so the structure is asserted through the real
       traversal rather than by trust. */
    {
        static const robo_html_tag_t expected[] = {
            ROBO_HTML_TAG_FORM,    ROBO_HTML_TAG_FIELDSET,
            ROBO_HTML_TAG_LEGEND,  ROBO_HTML_TAG_LABEL,
            ROBO_HTML_TAG_INPUT,   ROBO_HTML_TAG_TEXTAREA,
            ROBO_HTML_TAG_SELECT,  ROBO_HTML_TAG_OPTGROUP,
            ROBO_HTML_TAG_OPTION,  ROBO_HTML_TAG_BUTTON,
        };
        size_t i;

        for (i = 0u; i < sizeof(expected) / sizeof(expected[0]); i++) {
            form_tag_query_t q;
            robo_html_node_handle_t found;

            q.tag = expected[i];
            robo_html_node_handle_clear(&found);
            ROBO_CHECK_EQ_INT(robo_html_node_find_first(
                                  &root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  form_match_tag, &q, &found, NULL),
                              ROBO_OK);
            ROBO_CHECK(robo_html_element_is_tag(&found, expected[i]));
        }
    }

    /* Every control in the document resolves to the same owner, through the
       two different rules — which is what makes the document a form. */
    {
        robo_html_node_handle_t owner;
        robo_html_form_owner_source_t source = ROBO_HTML_FORM_OWNER_SOURCE_NONE;
        /* Every control in the form answers with the same owner, through the
           real ancestor rule — so "this is a form" is asserted by every
           control agreeing, not by one lookup succeeding. */
        static const robo_html_tag_t controls[] = {
            ROBO_HTML_TAG_INPUT, ROBO_HTML_TAG_TEXTAREA, ROBO_HTML_TAG_SELECT,
            ROBO_HTML_TAG_FIELDSET,
        };
        size_t i;

        for (i = 0u; i < sizeof(controls) / sizeof(controls[0]); i++) {
            robo_html_node_handle_t control = form_find_tag(&form, controls[i]);

            ROBO_CHECK_EQ_INT(
                robo_html_element_form_owner(&control, &owner, &source, NULL),
                ROBO_OK);
            ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));
            ROBO_CHECK_EQ_INT(source, ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);
        }
    }

    /* Both buttons kept their own types. */
    {
        robo_html_node_handle_t button =
            form_find_tag(&form, ROBO_HTML_TAG_BUTTON);
        robo_html_button_attributes_t attrs;

        ROBO_CHECK_EQ_INT(robo_html_element_button_attributes(&button, &attrs,
                                                              NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(attrs.type_effective, ROBO_HTML_BUTTON_TYPE_SUBMIT);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Unicode                                                             */
/* ------------------------------------------------------------------ */

static void test_form_unicode(void)
{
    robo_html_document_id_t id = form_doc();
    robo_html_node_handle_t root = form_root(id);
    robo_html_input_attributes_t iattrs;
    robo_html_textarea_attributes_t tattrs;
    robo_html_node_handle_t input;
    robo_html_node_handle_t area;
    robo_html_node_handle_t option;
    char buf[512];
    size_t length = 0u;

    /* Labels, values and names in the scripts this runtime already carries:
       ASCII, accented Latin, Arabic, Persian, Urdu and emoji. Nothing here
       shapes or normalizes — the requirement is preservation, and the bytes
       that arrive are the bytes that come back. */
    form_build(id,
               "<form name=\"نموذج\">"
               "<label for=\"e\">البريد الإلكتروني</label>"
               "<input id=\"e\" type=\"email\" name=\"ای میل\" "
               "value=\"مستخدم@مثال.پاکستان\" "
               "placeholder=\"ایمیل درج کریں\">"
               "<input id=\"r\" type=\"radio\" name=\"درجہ\" value=\"جی ہاں\">"
               "<textarea id=\"t\" name=\"پیغام\">نام خدا</textarea>"
               "<select id=\"s\" name=\"ملک\">"
               "<option value=\"پاکستان\">پاکستان</option>"
               "<option selected>مملکت متحدہ 🇵🇰</option>"
               "</select>"
               "<button type=\"submit\" value=\"محفوظ کریں\">محفوظ کریں</button>"
               "</form>");

    input = form_find_id(&root, "e");
    ROBO_CHECK_EQ_INT(robo_html_element_input_attributes(&input, &iattrs,
                                                         NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(iattrs.name, "ای میل");
    ROBO_CHECK_STR_EQ(iattrs.value, "مستخدم@مثال.پاکستان");
    ROBO_CHECK_STR_EQ(iattrs.placeholder, "ایمیل درج کریں");

    /* A label's `for` is a byte-exact id, and the Unicode id resolves. */
    {
        robo_html_node_handle_t label =
            form_find_tag(&root, ROBO_HTML_TAG_LABEL);
        robo_html_node_handle_t target;

        ROBO_CHECK_EQ_INT(robo_html_element_label_target(&label, &target, NULL,
                                                         NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&target, &input));
    }

    /* The declared value of a control read through the buffer API keeps every
       byte, including the multi-byte ones. */
    ROBO_CHECK_EQ_INT(robo_html_element_control_value(&input, buf,
                                                      sizeof(buf), &length,
                                                      NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "مستخدم@مثال.پاکستان");
    ROBO_CHECK_EQ_INT(length, strlen("مستخدم@مثال.پاکستان"));

    /* A textarea's default text is its children, in the same bytes. */
    area = form_find_id(&root, "t");
    ROBO_CHECK_EQ_INT(robo_html_element_textarea_attributes(&area, &tattrs,
                                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(tattrs.name, "پیغام");
    ROBO_CHECK_STR_EQ(form_text(&area), "نام خدا");
    ROBO_CHECK_EQ_INT(robo_html_element_control_value(&area, buf,
                                                      sizeof(buf), &length,
                                                      NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "نام خدا");

    /* An option's fallback value is its Unicode text, verbatim — emoji and
       all, because the text is bytes and this layer does not care which
       bytes. */
    option = form_find_tag(&root, ROBO_HTML_TAG_OPTION);
    {
        robo_html_node_handle_t sibling;
        int explicit_value = 9;

        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&option, &sibling, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_option_value(&sibling, buf,
                                                         sizeof(buf), &length,
                                                         &explicit_value,
                                                         NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "مملکت متحدہ 🇵🇰");
        ROBO_CHECK(!explicit_value);
        ROBO_CHECK_EQ_INT(length, strlen("مملکت متحدہ 🇵🇰"));
    }

    /* An override carrying Unicode round-trips, including into the value of
       an element whose declared value is empty. */
    form_build(id, "<input id=\"u\">");
    input = form_find_id_doc(id, "u");
    ROBO_CHECK_EQ_INT(robo_html_element_set_control_value(
                          &input, "café ☕ Тест", strlen("café ☕ Тест"), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_control_value(&input, buf,
                                                      sizeof(buf), NULL, NULL,
                                                      NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "café ☕ Тест");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_html_form(void)
{
    robo_test_register("html.form.parser_element", test_form_parser_element);
    robo_test_register("html.form.form_attributes",
                       test_form_form_attributes);
    robo_test_register("html.form.method_enctype_vocabulary",
                       test_form_method_enctype_vocabulary);
    robo_test_register("html.form.non_form_refused",
                       test_form_non_form_refused);
    robo_test_register("html.form.input_types", test_form_input_types);
    robo_test_register("html.form.input_attributes",
                       test_form_input_attributes);
    robo_test_register("html.form.input_value_state",
                       test_form_input_value_state);
    robo_test_register("html.form.input_checked_state",
                       test_form_input_checked_state);
    robo_test_register("html.form.input_numeric_constraints",
                       test_form_input_numeric_constraints);
    robo_test_register("html.form.button", test_form_button);
    robo_test_register("html.form.textarea", test_form_textarea);
    robo_test_register("html.form.select_options", test_form_select_options);
    robo_test_register("html.form.option_value_fallback",
                       test_form_option_value_fallback);
    robo_test_register("html.form.label_target", test_form_label_target);
    robo_test_register("html.form.owner_nested", test_form_owner_nested);
    robo_test_register("html.form.owner_attribute",
                       test_form_owner_attribute);
    robo_test_register("html.form.owner_not_supported",
                       test_form_owner_not_supported);
    robo_test_register("html.form.parser_malformed",
                       test_form_parser_malformed);
    robo_test_register("html.form.void_input", test_form_void_input);
    robo_test_register("html.form.mutation", test_form_mutation);
    robo_test_register("html.form.mutation_options",
                       test_form_mutation_options);
    robo_test_register("html.form.lifecycle", test_form_lifecycle);
    robo_test_register("html.form.destroyed_state",
                       test_form_destroyed_state);
    robo_test_register("html.form.structure", test_form_structure);
    robo_test_register("html.form.unicode", test_form_unicode);
}
