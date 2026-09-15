/*
 * RoboHTML Phase 8.6 attribute-model suite.
 *
 * The attribute model is the runtime's single attribute representation: the
 * tokenizer stages it, the tree builder stores it, an element owns it, and
 * the DOM seam mutates it. This suite covers the type itself, parser
 * integration and stored order, lookup and presence, the mutation verbs,
 * Unicode in names and values, malformed input, the attribute limit, the
 * debug dump, teardown, and the ownership rules that make all of it safe.
 *
 * Note on lookups: the tree builder keeps whitespace between tags as text
 * nodes, so a child index is never an element index — elements are found by
 * name here, never by offset.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void at_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void at_fixture_end(void)
{
    robo_html_shutdown();
}

static robo_html_document_id_t at_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/** Build @p text into @p id with the one-shot API. */
static void at_build(robo_html_document_id_t id, const char *text)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);
}

/** Handle of the first descendant element named @p name (invalid when none). */
static robo_html_node_handle_t at_find(robo_html_document_id_t id,
                                       const char *name)
{
    robo_html_node_handle_t root = robo_html_document_root(id);
    robo_html_node_handle_t found;

    robo_html_node_handle_clear(&found);
    if (robo_html_node_find_element(&root, name, &found, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&found);
    }
    return found;
}

/** Attribute count of the first element named @p name; -1 when absent. */
static int at_count(robo_html_document_id_t id, const char *name)
{
    robo_html_node_handle_t e = at_find(id, name);
    size_t count = 0u;

    if (!robo_html_element_is_element(&e)) {
        return -1;
    }
    if (robo_html_element_attribute_count(&e, &count, NULL) != ROBO_OK) {
        return -1;
    }
    return (int)count;
}

/** Copy attribute @p attr_name of element @p elem_name into @p out.
 *  @return 1 when the attribute is present (the value may be empty). */
static int at_value(robo_html_document_id_t id,
                    const char *elem_name,
                    const char *attr_name,
                    char *out,
                    size_t cap)
{
    robo_html_node_handle_t e = at_find(id, elem_name);
    robo_html_attribute_t attr;
    size_t n;

    out[0] = '\0';
    if (!robo_html_element_is_element(&e)) {
        return 0;
    }
    if (robo_html_element_find_attribute(&e, attr_name, &attr, NULL) !=
        ROBO_OK) {
        return 0;
    }
    n = attr.value_length;
    if (n >= cap) {
        n = cap - 1u;
    }
    if (n > 0u) {
        memcpy(out, attr.value, n);
    }
    out[n] = '\0';
    return 1;
}

/** Copy attribute @p index of element @p elem_name; "" when out of range. */
static void at_attr_at(robo_html_document_id_t id,
                       const char *elem_name,
                       size_t index,
                       char *name_out,
                       size_t name_cap,
                       char *value_out,
                       size_t value_cap)
{
    robo_html_node_handle_t e = at_find(id, elem_name);
    robo_html_attribute_t attr;
    size_t n;

    name_out[0] = '\0';
    value_out[0] = '\0';
    if (!robo_html_element_is_element(&e)) {
        return;
    }
    if (robo_html_element_attribute_at(&e, index, &attr, NULL) != ROBO_OK) {
        return;
    }
    n = attr.name_length;
    if (n >= name_cap) {
        n = name_cap - 1u;
    }
    if (n > 0u) {
        memcpy(name_out, attr.name, n);
    }
    name_out[n] = '\0';

    n = attr.value_length;
    if (n >= value_cap) {
        n = value_cap - 1u;
    }
    if (n > 0u) {
        memcpy(value_out, attr.value, n);
    }
    value_out[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* The type itself                                                     */
/* ------------------------------------------------------------------ */

static void test_at_type_basics(void)
{
    at_fixture_begin();

    robo_html_attribute_t attr;

    at_fixture_begin();

    robo_html_attribute_init(&attr);
    ROBO_CHECK_STR_EQ(attr.name, "");
    ROBO_CHECK_STR_EQ(attr.value, "");
    ROBO_CHECK_EQ_INT((int)attr.name_length, 0);
    ROBO_CHECK_EQ_INT((int)attr.value_length, 0);
    ROBO_CHECK_EQ_INT((int)attr.namespace_id, (int)ROBO_HTML_NAMESPACE_NONE);

    /* init is the "no attribute" state, and it must tolerate NULL. */
    robo_html_attribute_init(NULL);

    at_fixture_end();
}

static void test_at_type_reads_lengths(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t e;
    robo_html_attribute_t attr;

    at_build(id, "<!DOCTYPE html><html><body><div id=\"main\"></div></body></html>");
    e = at_find(id, "div");
    ROBO_CHECK(robo_html_element_is_element(&e));

    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&e, 0u, &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.name, "id");
    ROBO_CHECK_EQ_INT((int)attr.name_length, 2);
    ROBO_CHECK_STR_EQ(attr.value, "main");
    ROBO_CHECK_EQ_INT((int)attr.value_length, 4);
    /* HTML attributes have no namespace; the field exists so the foreign
     * content phase fills it in rather than changing the type. */
    ROBO_CHECK_EQ_INT((int)attr.namespace_id, (int)ROBO_HTML_NAMESPACE_NONE);

    /* Out of range zeroes the out-struct rather than leaving it stale. */
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&e, 1u, &attr, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_STR_EQ(attr.name, "");
    ROBO_CHECK_STR_EQ(attr.value, "");
    ROBO_CHECK_EQ_INT((int)attr.name_length, 0);

    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&e, 0u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(NULL, 0u, &attr, NULL),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Parser integration                                                  */
/* ------------------------------------------------------------------ */

static void test_at_parse_integration(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    char value[64];
    char name[32];

    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"main\" class=\"container\"></div>"
             "</body></html>");

    ROBO_CHECK_EQ_INT(at_count(id, "div"), 2);
    at_attr_at(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "id");
    ROBO_CHECK_STR_EQ(value, "main");
    at_attr_at(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "class");
    ROBO_CHECK_STR_EQ(value, "container");

    /* An element with no attributes has an empty, working attribute set. */
    ROBO_CHECK_EQ_INT(at_count(id, "body"), 0);
    ROBO_CHECK_EQ_INT(at_count(id, "html"), 0);
    {
        robo_html_node_handle_t body = at_find(id, "body");

        ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&body, "id"), 0);
        ROBO_CHECK(robo_html_element_attribute_value(&body, "id", NULL) == NULL);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

static void test_at_parse_order_is_source_order(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    char name[32];
    char value[32];

    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"main\" class=\"panel\" data-x=\"1\"></div>"
             "</body></html>");

    ROBO_CHECK_EQ_INT(at_count(id, "div"), 3);
    at_attr_at(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "id");
    ROBO_CHECK_STR_EQ(value, "main");
    at_attr_at(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "class");
    ROBO_CHECK_STR_EQ(value, "panel");
    at_attr_at(id, "div", 2u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "data-x");
    ROBO_CHECK_STR_EQ(value, "1");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

static void test_at_presence_versus_empty(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t input;
    robo_html_node_handle_t body;
    robo_html_node_handle_t second;
    const char *value = NULL;
    size_t length = 0u;
    size_t children = 0u;
    size_t i;
    size_t seen = 0u;
    char dump[1024];

    /* `<input disabled>` and `<input disabled="">` must both be present with
     * an empty value, and both distinct from absent. */
    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<input disabled>"
             "<input checked=\"\">"
             "</body></html>");

    input = at_find(id, "input");
    ROBO_CHECK(robo_html_element_is_element(&input));
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&input, "disabled"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&input, "checked"), 0);
    value = robo_html_element_attribute_value(&input, "disabled", &length);
    ROBO_CHECK(value != NULL);
    ROBO_CHECK_STR_EQ(value, "");
    ROBO_CHECK_EQ_INT((int)length, 0);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&input, &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)length, 1);

    /* The second <input> carries the explicitly empty value. */
    body = at_find(id, "body");
    robo_html_node_handle_clear(&second);
    ROBO_CHECK_EQ_INT(robo_html_node_child_count(&body, &children, NULL),
                      ROBO_OK);
    for (i = 0u; i < children; i++) {
        robo_html_node_handle_t child;

        if (robo_html_node_child_at(&body, i, &child, NULL) != ROBO_OK) {
            break;
        }
        if (!robo_html_element_is_named(&child, "input")) {
            continue;
        }
        if (seen == 1u) {
            second = child;
            break;
        }
        seen++;
    }
    ROBO_CHECK(robo_html_element_is_element(&second));
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&second, "checked"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&second, "disabled"), 0);
    value = robo_html_element_attribute_value(&second, "checked", &length);
    ROBO_CHECK(value != NULL);
    ROBO_CHECK_EQ_INT((int)length, 0);

    /* The dump shows attributes, and an empty value is visible as "". */
    ROBO_CHECK_EQ_INT(robo_html_tree_dump_to_buffer(id, dump, sizeof(dump),
                                                    NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK(strstr(dump, "input disabled=\"\"") != NULL);
    ROBO_CHECK(strstr(dump, "input checked=\"\"") != NULL);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

static void test_at_duplicates_preserved(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    char value[32];
    char name[32];

    /* The parser does not fold repeated names and the model does not invent
     * a different answer: both records are stored, in source order, and
     * lookup answers the first. */
    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"one\" id=\"two\"></div>"
             "</body></html>");

    ROBO_CHECK_EQ_INT(at_count(id, "div"), 2);
    at_attr_at(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "id");
    ROBO_CHECK_STR_EQ(value, "one");
    at_attr_at(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "id");
    ROBO_CHECK_STR_EQ(value, "two");
    ROBO_CHECK(at_value(id, "div", "id", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "one");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

static void test_at_lookup(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    robo_html_node_handle_t root;
    robo_html_attribute_t attr;
    size_t length = 0u;

    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"main\" data-value=\"hello\" aria-label=\"label\"></div>"
             "</body></html>");
    div = at_find(id, "div");
    ROBO_CHECK(robo_html_element_is_element(&div));

    /* Names are stored lowercase; lookup is ASCII case-insensitive, so a
     * caller may pass any case and every path agrees. */
    ROBO_CHECK_EQ_INT(robo_html_element_find_attribute(&div, "ID", &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.name, "id");
    ROBO_CHECK_STR_EQ(attr.value, "main");

    ROBO_CHECK_EQ_INT(
        robo_html_element_find_attribute(&div, "DATA-VALUE", &attr, NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.value, "hello");

    ROBO_CHECK_EQ_INT(robo_html_element_find_attribute(&div, "missing", &attr,
                                                       NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_STR_EQ(attr.value, "");

    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "ARIA-LABEL"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "aria-labelledby"),
                      0);

    ROBO_CHECK(robo_html_element_attribute_value(&div, "data-value", &length) !=
               NULL);
    ROBO_CHECK_EQ_INT((int)length, 5);
    ROBO_CHECK(robo_html_element_attribute_value(&div, "nope", NULL) == NULL);

    /* NULL arguments are refused, not obeyed. */
    ROBO_CHECK_EQ_INT(robo_html_element_find_attribute(&div, NULL, &attr, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_find_attribute(NULL, "id", &attr, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, NULL), 0);

    /* A non-element node is not an element with an empty attribute set. */
    root = robo_html_document_root(id);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&root, &length, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&root, "id"), 0);
    ROBO_CHECK(robo_html_element_attribute_value(&root, "id", NULL) == NULL);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Mutation                                                            */
/* ------------------------------------------------------------------ */

static void test_at_mutation_set(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    robo_html_node_handle_t parent;
    char name[32];
    char value[64];
    robo_html_node_id_t node_id;
    uint32_t generation;

    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"one\" class=\"panel\"></div>"
             "</body></html>");
    div = at_find(id, "div");
    node_id = div.node_id;
    generation = div.generation;

    /* Update in place: the record is rewritten, not appended, and it keeps
     * its position. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "id", "main", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 2);
    ROBO_CHECK(at_value(id, "div", "id", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "main");
    at_attr_at(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "id");

    /* A name that is not there is appended, as DOM setAttribute does. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "title", "hi", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 3);
    at_attr_at(id, "div", 2u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "title");
    ROBO_CHECK_STR_EQ(value, "hi");

    /* The name is normalized on the way in, so it is found in any case. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "DATA-K", "v", NULL),
                      ROBO_OK);
    at_attr_at(id, "div", 3u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "data-k");
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "Data-K"), 1);

    /* Setting presence with an empty value is how a boolean attribute is
     * written: NULL means the empty string here, not "remove". */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "disabled", NULL,
                                                      NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "disabled"), 1);
    ROBO_CHECK_STR_EQ(robo_html_element_attribute_value(&div, "disabled", NULL),
                      "");

    /* Rewriting the payload does not touch identity or the element's place
     * in the tree: same node id, same generation, still under <body>. */
    ROBO_CHECK_EQ_INT((int)div.node_id, (int)node_id);
    ROBO_CHECK_EQ_INT((int)div.generation, (int)generation);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&div, "div"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_parent(&div, &parent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&parent, "body"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "id"), 1);
    ROBO_CHECK_EQ_INT(at_count(id, "body"), 0);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

static void test_at_mutation_remove(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    char name[32];
    char value[32];

    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"main\" class=\"panel\" data-x=\"1\"></div>"
             "</body></html>");
    div = at_find(id, "div");

    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&div, "class", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 2);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "class"), 0);
    ROBO_CHECK(robo_html_element_attribute_value(&div, "class", NULL) == NULL);
    /* The rest keeps its relative order. */
    at_attr_at(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "id");
    at_attr_at(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "data-x");

    /* Removing something that is not there is a successful no-op, matching
     * DOM removeAttribute — it is not a presence test. */
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&div, "absent", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 2);

    /* Removal is case-insensitive like every other name operation. */
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&div, "DATA-X", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 1);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

static void test_at_mutation_set_collapses_duplicates(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    char name[32];
    char value[32];

    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"one\" class=\"panel\" id=\"two\"></div>"
             "</body></html>");
    div = at_find(id, "div");
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 3);

    /* set collapses every match to one, at the first match's position. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "id", "final", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 2);
    at_attr_at(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "id");
    ROBO_CHECK_STR_EQ(value, "final");
    at_attr_at(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "class");

    /* remove drops every match, not just the first. */
    ROBO_CHECK_EQ_INT(robo_html_element_add_attribute(&div, "id", "again", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 3);
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&div, "id", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 1);
    at_attr_at(id, "div", 0u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "class");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

static void test_at_mutation_add_appends(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    char name[32];
    char value[32];

    at_build(id,
             "<!DOCTYPE html><html><body><div id=\"main\"></div></body></html>");
    div = at_find(id, "div");

    /* add is the parser's verb: it appends and never collapses, which is the
     * only way a duplicate name can enter the model. */
    ROBO_CHECK_EQ_INT(robo_html_element_add_attribute(&div, "id", "second", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 2);
    at_attr_at(id, "div", 1u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(value, "second");
    /* Lookup still answers the first record. */
    ROBO_CHECK(at_value(id, "div", "id", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "main");

    ROBO_CHECK_EQ_INT(robo_html_element_add_attribute(&div, "ID", "third", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 3);
    at_attr_at(id, "div", 2u, name, sizeof(name), value, sizeof(value));
    ROBO_CHECK_STR_EQ(name, "id");
    ROBO_CHECK_STR_EQ(value, "third");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

static void test_at_mutation_validation(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    robo_html_node_handle_t root;
    robo_html_node_handle_t ghost;
    char long_name[120];
    size_t i;

    at_build(id,
             "<!DOCTYPE html><html><body><div id=\"main\"></div></body></html>");
    div = at_find(id, "div");

    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(NULL, "id", "x", NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, NULL, "x", NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "", "x", NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "a b", "x", NULL),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "<", "x", NULL),
                      ROBO_PARSE_ERROR);

    for (i = 0u; i < sizeof(long_name) - 1u; i++) {
        long_name[i] = 'a';
    }
    long_name[sizeof(long_name) - 1u] = '\0';
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&div, long_name, "x", NULL),
        ROBO_NOT_SUPPORTED);

    /* A refused mutation leaves the element exactly as it was. */
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "id"), 1);

    /* A node that is not an element cannot carry attributes. */
    root = robo_html_document_root(id);
    ROBO_CHECK_EQ_INT(robo_html_element_add_attribute(&root, "id", "x", NULL),
                      ROBO_NOT_FOUND);

    /* Nor can a stale handle. */
    ghost = div;
    ghost.generation = (uint32_t)(ghost.generation + 7u);
    ROBO_CHECK_EQ_INT(robo_html_element_add_attribute(&ghost, "id", "x", NULL),
                      ROBO_NOT_FOUND);

    /* Nor a handle whose document is gone. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_add_attribute(&div, "id", "x", NULL),
                      ROBO_NOT_FOUND);

    at_fixture_end();
}

static void test_at_mutation_after_finalize(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    char value[32];

    at_build(id,
             "<!DOCTYPE html><html><body><div id=\"main\"></div></body></html>");
    /* A completed build finalizes *tree construction*. Attributes are a
     * different operation — the DOM seam stays open, which is what makes
     * this model usable by the later DOM phase rather than parser-only. */
    ROBO_CHECK_EQ_INT(robo_html_document_is_finalized(id), 1);
    ROBO_CHECK_EQ_INT(robo_html_document_allows_construction(id), 0);
    div = at_find(id, "div");

    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "data-live", "yes",
                                                      NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "data-live"), 1);
    ROBO_CHECK(at_value(id, "div", "data-live", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "yes");
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&div, "data-live",
                                                         NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "data-live"), 0);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

static void test_at_mutation_many(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    char name[16];
    char value[16];
    int i;
    int refused_at = -1;

    at_build(id, "<!DOCTYPE html><html><body><div></div></body></html>");
    div = at_find(id, "div");
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 0);

    /* An element's attribute table is bounded, and the bound must be
     * enforced by refusing the write rather than by corrupting the element. */
    for (i = 0; i < 100; i++) {
        robo_result_t r;

        (void)snprintf(name, sizeof(name), "a%d", i);
        (void)snprintf(value, sizeof(value), "v%d", i);
        r = robo_html_element_add_attribute(&div, name, value, NULL);
        if (r != ROBO_OK) {
            ROBO_CHECK_EQ_INT(r, ROBO_NOT_SUPPORTED);
            refused_at = i;
            break;
        }
    }
    ROBO_CHECK(refused_at > 0);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), refused_at);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), refused_at);

    /* The refused name really is absent, and an *update* still works at the
     * limit: reaching the cap must not make the element read-only. */
    (void)snprintf(name, sizeof(name), "a%d", refused_at);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, name), 0);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "a0", "updated",
                                                      NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), refused_at);
    ROBO_CHECK(at_value(id, "div", "a0", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "updated");
    /* A remove at the limit frees a slot for the next add. */
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&div, "a1", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), refused_at - 1);
    ROBO_CHECK_EQ_INT(robo_html_element_add_attribute(&div, name, "late", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), refused_at);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Unicode                                                             */
/* ------------------------------------------------------------------ */

static void test_at_unicode_values(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    char value[128];

    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<div title=\"\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7\" "
             "data-name=\"\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c\" "
             "aria-label=\"\xd8\xa7\xd8\xb1\xd8\xaf\xd9\x88\" "
             "data-text=\"\xf0\x9f\x98\x80\" "
             "data-cjk=\"\xe4\xb8\xad\xe6\x96\x87\" "
             "data-jp=\"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\"></div>"
             "</body></html>");

    ROBO_CHECK_EQ_INT(at_count(id, "div"), 6);

    /* Arabic "marhaba": byte-for-byte, with no normalization applied. */
    ROBO_CHECK(at_value(id, "div", "title", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7");
    ROBO_CHECK(at_value(id, "div", "data-name", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c");
    ROBO_CHECK(at_value(id, "div", "aria-label", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "\xd8\xa7\xd8\xb1\xd8\xaf\xd9\x88");
    /* A 4-byte emoji survives the round trip whole. */
    ROBO_CHECK(at_value(id, "div", "data-text", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "\xf0\x9f\x98\x80");
    ROBO_CHECK(at_value(id, "div", "data-cjk", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "\xe4\xb8\xad\xe6\x96\x87");
    ROBO_CHECK(at_value(id, "div", "data-jp", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");

    /* Bytes written through the DOM seam ride the same encoder. */
    div = at_find(id, "div");
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&div, "data-extra",
                                        "\xf0\x9f\x8e\x89", NULL),
        ROBO_OK);
    ROBO_CHECK(at_value(id, "div", "data-extra", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "\xf0\x9f\x8e\x89");
    /* The length is bytes, not characters — a 4-byte emoji is 4. */
    {
        size_t length = 0u;

        (void)robo_html_element_attribute_value(&div, "data-extra", &length);
        ROBO_CHECK_EQ_INT((int)length, 4);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

static void test_at_unicode_names(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    robo_html_attribute_t attr;
    size_t count = 0u;

    /* A non-ASCII attribute name: the tokenizer keeps the UTF-8 bytes and
     * the model stores and finds them unchanged. Bytes outside A-Z are never
     * case-folded, so the comparison stays byte-exact. */
    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<div \xd0\xb4\xd0\xb0\xd1\x82\xd0\xb0=\"x\"></div>"
             "</body></html>");

    div = at_find(id, "div");
    ROBO_CHECK(robo_html_element_is_element(&div));
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&div, &count, NULL),
                      ROBO_OK);
    if (count == 1u) {
        ROBO_CHECK_EQ_INT(robo_html_element_find_attribute(
                              &div, "\xd0\xb4\xd0\xb0\xd1\x82\xd0\xb0", &attr,
                              NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(attr.name, "\xd0\xb4\xd0\xb0\xd1\x82\xd0\xb0");
        ROBO_CHECK_STR_EQ(attr.value, "x");
    } else {
        /* If the tokenizer chose to drop it, it must be gone cleanly rather
         * than stored half-formed. */
        ROBO_CHECK_EQ_INT((int)count, 0);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Malformed input                                                     */
/* ------------------------------------------------------------------ */

/** One hostile-attribute source plus what the parser should make of it. */
typedef struct malformed_case {
    const char *source;
    /**
     * 1 when a <div> element must exist afterwards. 0 only for the input
     * whose quoted value is never closed: the tokenizer consumes the rest of
     * the document as that value and no start tag is ever emitted, which is
     * the 8.2 tokenizer's documented behaviour, not an attribute-model
     * concern. Everything else about that case must still hold.
     */
    int expects_div;
} malformed_case_t;

static void test_at_malformed_input(void)
{
    static const malformed_case_t cases[] = {
        {"<!DOCTYPE html><html><body><div></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id=\"\"></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id=\"x\"></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id=\"x\" id=\"y\"></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div class></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div disabled></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div =bad></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id = 'single'></div></body></html>", 1},
        {"<!DOCTYPE html><html><body><div a==b c= d= e></div></body></html>",
         1},
        {"<!DOCTYPE html><html><body><div/></body></html>", 1},
        {"<!DOCTYPE html><html><body><div id=\"unterminated></body></html>", 0}
    };
    size_t i;

    at_fixture_begin();

    /* Every one of these must build, and every div that *was* created must
     * keep an attribute table that is internally consistent: a hostile or
     * broken attribute must never desynchronise the payload walk. */
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_html_document_id_t id = at_doc();
        robo_html_node_handle_t div;
        robo_html_element_view_t view;
        robo_html_attribute_t attr;
        char dump[2048];
        size_t count = 0u;
        size_t j;

        at_build(id, cases[i].source);
        div = at_find(id, "div");
        ROBO_CHECK_EQ_INT(robo_html_element_is_element(&div),
                          cases[i].expects_div);
        if (cases[i].expects_div) {
            ROBO_CHECK_EQ_INT(robo_html_element_view_get(&div, &view, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(
                robo_html_element_attribute_count(&div, &count, NULL),
                ROBO_OK);
            ROBO_CHECK_EQ_INT((int)view.attribute_count, (int)count);
            for (j = 0u; j < count; j++) {
                ROBO_CHECK_EQ_INT(
                    robo_html_element_attribute_at(&div, j, &attr, NULL),
                    ROBO_OK);
                ROBO_CHECK(attr.name != NULL);
                ROBO_CHECK(attr.value != NULL);
                ROBO_CHECK_EQ_INT((int)attr.name_length,
                                  (int)strlen(attr.name));
                ROBO_CHECK_EQ_INT((int)attr.value_length,
                                  (int)strlen(attr.value));
            }
            ROBO_CHECK_EQ_INT(
                robo_html_element_attribute_at(&div, count, &attr, NULL),
                ROBO_NOT_FOUND);
        }
        /* The dump walks every stored attribute, so a malformed table would
         * surface here as garbage rather than as a clean tree. */
        ROBO_CHECK_EQ_INT(
            robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), NULL, NULL),
            ROBO_OK);
        ROBO_CHECK(strstr(dump, "Document") != NULL);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    at_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Debug output                                                        */
/* ------------------------------------------------------------------ */

static void test_at_tree_dump_shows_attributes(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    char dump[1024];
    size_t length = 0u;

    at_build(id,
             "<!DOCTYPE html><html><body>"
             "<div id=\"main\" class=\"container\"></div>"
             "<img src=\"a.png\">"
             "</body></html>");

    ROBO_CHECK_EQ_INT(robo_html_tree_dump_to_buffer(id, dump, sizeof(dump),
                                                    &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(length > 0u);
    ROBO_CHECK(strstr(dump, "div id=\"main\" class=\"container\"") != NULL);
    /* The pre-existing shape is unchanged: name, quote, value, optional
     * self-closing marker. The model now feeds it, it did not replace it. */
    ROBO_CHECK(strstr(dump, "img src=\"a.png\"") != NULL);

    /* A mutation shows up in the dump, because the dump reads the model. */
    div = at_find(id, "div");
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "class", "updated",
                                                      NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_dump_to_buffer(id, dump, sizeof(dump),
                                                    NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK(strstr(dump, "class=\"updated\"") != NULL);
    ROBO_CHECK(strstr(dump, "class=\"container\"") == NULL);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Ownership / lifetime                                                */
/* ------------------------------------------------------------------ */

static void test_at_ownership(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_document_id_t other;
    robo_html_node_handle_t div;
    robo_html_node_handle_t other_div;
    robo_html_attribute_t attr;

    at_build(id,
             "<!DOCTYPE html><html><body><div id=\"main\"></div></body></html>");
    div = at_find(id, "div");

    /* Attributes live in the owning document's payload arena, so two
     * documents' elements are separate: the same name resolves inside its
     * own document only. */
    other = at_doc();
    at_build(other,
             "<!DOCTYPE html><html><body><div id=\"elsewhere\"></div></body>"
             "</html>");
    other_div = at_find(other, "div");

    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "data-owner", "a",
                                                      NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&other_div, "data-owner"),
                      0);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&other_div, "data-owner",
                                                      "b", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_find_attribute(&div, "data-owner", &attr,
                                                       NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.value, "a");
    ROBO_CHECK_EQ_INT(robo_html_element_find_attribute(&other_div, "data-owner",
                                                       &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.value, "b");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(other, NULL), ROBO_OK);
    /* The other document is gone: its handles are inert and the first
     * document is untouched, so there is no shared state to dangle. */
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&other_div, "data-owner"),
                      0);
    ROBO_CHECK_EQ_INT(robo_html_element_add_attribute(&other_div, "x", "y",
                                                      NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "data-owner"), 1);

    /* Document teardown frees every attribute with the arena it lives in. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "id"), 0);
    ROBO_CHECK(robo_html_element_attribute_value(&div, "id", NULL) == NULL);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "id", "x", NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&div, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    at_fixture_end();
}

static void test_at_teardown_with_many_mutations(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    char name[16];
    char value[16];
    int i;

    at_build(id, "<!DOCTYPE html><html><body><div></div></body></html>");
    div = at_find(id, "div");

    /* Rewriting the payload many times must neither corrupt the element nor
     * outlive the document: the arena is freed as a unit at teardown. */
    for (i = 0; i < 40; i++) {
        (void)snprintf(name, sizeof(name), "k%d", i);
        (void)snprintf(value, sizeof(value), "v%d", i);
        ROBO_CHECK_EQ_INT(robo_html_element_add_attribute(&div, name, value,
                                                          NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, name, value,
                                                          NULL),
                          ROBO_OK);
        if (i % 3 == 0) {
            ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&div, name,
                                                                 NULL),
                              ROBO_OK);
        }
    }
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&div, "div"), 1);
    ROBO_CHECK_EQ_INT((int)at_count(id, "div"), 26);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

static void test_at_rebuild_clears_attributes(void)
{
    at_fixture_begin();

    robo_html_document_id_t id = at_doc();
    robo_html_node_handle_t div;
    robo_html_node_handle_t again;
    char value[32];

    at_build(id,
             "<!DOCTYPE html><html><body><div id=\"first\" class=\"x\"></div>"
             "</body></html>");
    div = at_find(id, "div");
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 2);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&div, "extra", "y", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 3);

    /* Re-parsing is explicit (8.4) and replaces the tree: the new element
     * carries only what the new source said. */
    at_build(id,
             "<!DOCTYPE html><html><body><div id=\"second\"></div></body>"
             "</html>");
    again = at_find(id, "div");
    ROBO_CHECK(robo_html_element_is_element(&again));
    ROBO_CHECK_EQ_INT(at_count(id, "div"), 1);
    ROBO_CHECK(at_value(id, "div", "id", value, sizeof(value)));
    ROBO_CHECK_STR_EQ(value, "second");
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&again, "extra"), 0);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&again, "class"), 0);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    at_fixture_end();
}

/* ------------------------------------------------------------------ */

void robo_test_register_html_attribute(void)
{
    robo_test_register("html.attr.type_basics", test_at_type_basics);
    robo_test_register("html.attr.type_reads_lengths",
                       test_at_type_reads_lengths);
    robo_test_register("html.attr.parse_integration",
                       test_at_parse_integration);
    robo_test_register("html.attr.parse_order_is_source_order",
                       test_at_parse_order_is_source_order);
    robo_test_register("html.attr.presence_versus_empty",
                       test_at_presence_versus_empty);
    robo_test_register("html.attr.duplicates_preserved",
                       test_at_duplicates_preserved);
    robo_test_register("html.attr.lookup", test_at_lookup);
    robo_test_register("html.attr.mutation_set", test_at_mutation_set);
    robo_test_register("html.attr.mutation_remove", test_at_mutation_remove);
    robo_test_register("html.attr.mutation_set_collapses_duplicates",
                       test_at_mutation_set_collapses_duplicates);
    robo_test_register("html.attr.mutation_add_appends",
                       test_at_mutation_add_appends);
    robo_test_register("html.attr.mutation_validation",
                       test_at_mutation_validation);
    robo_test_register("html.attr.mutation_after_finalize",
                       test_at_mutation_after_finalize);
    robo_test_register("html.attr.mutation_many", test_at_mutation_many);
    robo_test_register("html.attr.unicode_values", test_at_unicode_values);
    robo_test_register("html.attr.unicode_names", test_at_unicode_names);
    robo_test_register("html.attr.malformed_input", test_at_malformed_input);
    robo_test_register("html.attr.tree_dump_shows_attributes",
                       test_at_tree_dump_shows_attributes);
    robo_test_register("html.attr.ownership", test_at_ownership);
    robo_test_register("html.attr.teardown_with_many_mutations",
                       test_at_teardown_with_many_mutations);
    robo_test_register("html.attr.rebuild_clears_attributes",
                       test_at_rebuild_clears_attributes);
}
