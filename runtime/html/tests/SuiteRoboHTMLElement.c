/*
 * RoboHTML Phase 8.5 element-model suite:
 * element creation, identity/validity, tag names, namespace and
 * classification, document ownership and cross-document rejection,
 * parent/child/sibling relationships, temporary attribute storage,
 * self-closing and state bits, start/end tag integration, teardown,
 * malformed recovery, Unicode text, the tree dump, and the visual demo
 * document's expected element tree.
 *
 * Note on lookups: the tree builder keeps whitespace between tags as text
 * nodes (as the 8.3 suite documents for the demo document), so a child index
 * is never an element index. Everything here finds elements either by name
 * (robo_html_node_find_element) or through el_element_children, never by a
 * fixed offset into a child list.
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

static void el_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void el_fixture_end(void)
{
    robo_html_shutdown();
}

static robo_html_document_id_t el_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/** Build @p text into @p id with the one-shot API; result optional. */
static void el_build(robo_html_document_id_t id,
                     const char *text,
                     robo_html_tree_result_t *out)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, out, NULL), ROBO_OK);
}

static robo_html_node_handle_t el_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

/** Handle of the first descendant element named @p name (invalid when none). */
static robo_html_node_handle_t el_find(robo_html_document_id_t id,
                                       const char *name)
{
    robo_html_node_handle_t root = el_root(id);
    robo_html_node_handle_t found;

    robo_html_node_handle_clear(&found);
    if (robo_html_node_find_element(&root, name, &found, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&found);
    }
    return found;
}

/** Child @p index (any kind); invalid when out of range. */
static robo_html_node_handle_t el_child(const robo_html_node_handle_t *parent,
                                        size_t index)
{
    robo_html_node_handle_t out;

    robo_html_node_handle_clear(&out);
    if (robo_html_node_child_at(parent, index, &out, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&out);
    }
    return out;
}

/** Element children of @p parent in document order; returns how many. */
static size_t el_element_children(const robo_html_node_handle_t *parent,
                                  robo_html_node_handle_t *out,
                                  size_t cap)
{
    size_t children = 0u;
    size_t found = 0u;
    size_t i;

    if (!parent || !out || cap == 0u) {
        return 0u;
    }
    if (robo_html_node_child_count(parent, &children, NULL) != ROBO_OK) {
        return 0u;
    }
    for (i = 0u; i < children && found < cap; i++) {
        robo_html_node_handle_t child = el_child(parent, i);

        if (!robo_html_element_is_element(&child)) {
            continue;
        }
        out[found++] = child;
    }
    return found;
}

/** Concatenated text of a subtree; "" when the node is unknown. */
static void el_text(robo_html_node_handle_t h, char *buf, size_t cap)
{
    size_t length = 0u;

    buf[0] = '\0';
    if (robo_html_node_subtree_text(&h, buf, cap, &length, NULL) != ROBO_OK) {
        buf[0] = '\0';
    }
}

/** Create an element under @p parent; returns an invalid handle on failure. */
static robo_html_node_handle_t el_create_under(
    robo_html_document_id_t id,
    const robo_html_node_handle_t *parent,
    const char *name,
    robo_result_t *out_code)
{
    robo_html_element_create_info_t info;
    robo_html_node_handle_t out;
    robo_result_t r;

    robo_html_element_create_info_init(&info);
    info.parent = *parent;
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
    info.namespace_id = ROBO_HTML_NAMESPACE_HTML;
    info.local_name = name;

    robo_html_node_handle_clear(&out);
    r = robo_html_element_create(id, &info, &out, NULL);
    if (out_code) {
        *out_code = r;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Namespace + classification                                          */
/* ------------------------------------------------------------------ */

static void test_el_namespace_helpers(void)
{
    el_fixture_begin();

    ROBO_CHECK(robo_html_namespace_is_known(ROBO_HTML_NAMESPACE_NONE));
    ROBO_CHECK(robo_html_namespace_is_known(ROBO_HTML_NAMESPACE_HTML));
    ROBO_CHECK(robo_html_namespace_is_known(ROBO_HTML_NAMESPACE_SVG));
    ROBO_CHECK(robo_html_namespace_is_known(ROBO_HTML_NAMESPACE_MATHML));
    ROBO_CHECK(!robo_html_namespace_is_known((robo_html_namespace_t)42));

    ROBO_CHECK_STR_EQ(robo_html_namespace_name(ROBO_HTML_NAMESPACE_HTML),
                      "html");
    ROBO_CHECK_STR_EQ(robo_html_namespace_name(ROBO_HTML_NAMESPACE_SVG), "svg");
    ROBO_CHECK_STR_EQ(robo_html_namespace_name(ROBO_HTML_NAMESPACE_NONE),
                      "none");
    ROBO_CHECK_STR_EQ(robo_html_namespace_name((robo_html_namespace_t)42),
                      "unknown");

    ROBO_CHECK_STR_EQ(robo_html_namespace_url(ROBO_HTML_NAMESPACE_HTML),
                      "http://www.w3.org/1999/xhtml");
    ROBO_CHECK_STR_EQ(robo_html_namespace_url(ROBO_HTML_NAMESPACE_SVG),
                      "http://www.w3.org/2000/svg");
    ROBO_CHECK_STR_EQ(robo_html_namespace_url(ROBO_HTML_NAMESPACE_MATHML),
                      "http://www.w3.org/1998/Math/MathML");
    ROBO_CHECK_STR_EQ(robo_html_namespace_url(ROBO_HTML_NAMESPACE_NONE), "");
    ROBO_CHECK_STR_EQ(robo_html_namespace_url((robo_html_namespace_t)42), "");

    /* The element domain is part of the shared diagnostic vocabulary. */
    ROBO_CHECK(robo_html_diagnostic_domain_is_known(ROBO_HTML_DOMAIN_ELEMENT));
    ROBO_CHECK_STR_EQ(robo_html_diagnostic_domain_name(ROBO_HTML_DOMAIN_ELEMENT),
                      "element");

    el_fixture_end();
}

static void test_el_classification_helpers(void)
{
    el_fixture_begin();

    ROBO_CHECK(robo_html_element_class_is_known(ROBO_HTML_ELEMENT_CLASS_GENERIC));
    ROBO_CHECK(robo_html_element_class_is_known(ROBO_HTML_ELEMENT_CLASS_CONTROL));
    ROBO_CHECK(!robo_html_element_class_is_known(
        (robo_html_element_class_t)99));

    ROBO_CHECK_STR_EQ(
        robo_html_element_class_name(ROBO_HTML_ELEMENT_CLASS_METADATA),
        "metadata");
    ROBO_CHECK_STR_EQ(
        robo_html_element_class_name(ROBO_HTML_ELEMENT_CLASS_REPLACED),
        "replaced");
    ROBO_CHECK_STR_EQ(
        robo_html_element_class_name((robo_html_element_class_t)99),
        "unknown");

    /* Every element this phase claims to know classifies to its own role. */
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "html"),
        (int)ROBO_HTML_ELEMENT_CLASS_ROOT);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "head"),
        (int)ROBO_HTML_ELEMENT_CLASS_METADATA);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "title"),
        (int)ROBO_HTML_ELEMENT_CLASS_METADATA);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "body"),
        (int)ROBO_HTML_ELEMENT_CLASS_SECTION);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "div"),
        (int)ROBO_HTML_ELEMENT_CLASS_SECTION);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "h1"),
        (int)ROBO_HTML_ELEMENT_CLASS_HEADING);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "h6"),
        (int)ROBO_HTML_ELEMENT_CLASS_HEADING);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "p"),
        (int)ROBO_HTML_ELEMENT_CLASS_PARAGRAPH);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "span"),
        (int)ROBO_HTML_ELEMENT_CLASS_TEXT);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "a"),
        (int)ROBO_HTML_ELEMENT_CLASS_TEXT);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "img"),
        (int)ROBO_HTML_ELEMENT_CLASS_REPLACED);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "button"),
        (int)ROBO_HTML_ELEMENT_CLASS_CONTROL);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "input"),
        (int)ROBO_HTML_ELEMENT_CLASS_CONTROL);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "form"),
        (int)ROBO_HTML_ELEMENT_CLASS_CONTROL);

    /* An unknown name is GENERIC, never an error. */
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "my-widget"),
        (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, NULL),
        (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, ""),
        (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);

    /* The same local name in another namespace is not the HTML element. */
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_SVG, "a"),
        (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_NONE, "html"),
        (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);

    /* Case-insensitive, because callers hold names from several sources. */
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "DIV"),
        (int)ROBO_HTML_ELEMENT_CLASS_SECTION);

    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

static void test_el_create_info_init(void)
{
    robo_html_element_create_info_t info;
    robo_html_element_create_info_t dirty;

    el_fixture_begin();

    memset(&dirty, 0xAB, sizeof(dirty));
    robo_html_element_create_info_init(&dirty);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&dirty.parent), 0);
    ROBO_CHECK_EQ_INT((int)dirty.origin, (int)ROBO_HTML_NODE_ORIGIN_NONE);
    ROBO_CHECK_EQ_INT((int)dirty.namespace_id, (int)ROBO_HTML_NAMESPACE_NONE);
    ROBO_CHECK(dirty.local_name == NULL);
    ROBO_CHECK(dirty.attributes == NULL);
    ROBO_CHECK_EQ_INT((int)dirty.attribute_count, 0);
    ROBO_CHECK_EQ_INT(dirty.self_closing, 0);

    /* NULL-safe. */
    robo_html_element_create_info_init(NULL);

    memset(&info, 0, sizeof(info));
    robo_html_element_create_info_init(&info);
    ROBO_CHECK_EQ_INT((int)info.namespace_id, (int)ROBO_HTML_NAMESPACE_NONE);

    el_fixture_end();
}

static void test_el_create_basic(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t div;
    robo_html_node_handle_t span;
    robo_html_node_handle_t parent;
    robo_html_element_view_t view;
    robo_html_node_kind_t kind = ROBO_HTML_NODE_KIND_DOCUMENT;
    robo_html_tree_report_t report;
    size_t count = 0u;

    el_fixture_begin();
    id = el_doc();
    root = el_root(id);

    div = el_create_under(id, &root, "div", NULL);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&div));

    /* Identity: the element IS a node of this document. */
    ROBO_CHECK_EQ_INT((int)robo_html_node_handle_document(&div), (int)id);
    ROBO_CHECK(robo_html_document_owns_node(id, &div));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&div, &kind), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)kind, (int)ROBO_HTML_NODE_KIND_ELEMENT);
    ROBO_CHECK(robo_html_element_is_element(&div));

    /* Shape. */
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&div, &view, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(view.name, "div");
    ROBO_CHECK_EQ_INT((int)view.name_length, 3);
    ROBO_CHECK_EQ_INT((int)view.namespace_id, (int)ROBO_HTML_NAMESPACE_HTML);
    ROBO_CHECK_EQ_INT((int)view.classification,
                      (int)ROBO_HTML_ELEMENT_CLASS_SECTION);
    ROBO_CHECK_EQ_INT((int)view.attribute_count, 0);
    ROBO_CHECK_EQ_INT(view.self_closing, 0);
    /* Created under the document root with the TEST origin: connected, but
     * not parser-created and not self-closing. */
    ROBO_CHECK(view.state & ROBO_HTML_ELEMENT_STATE_CONNECTED);
    ROBO_CHECK(!(view.state & ROBO_HTML_ELEMENT_STATE_PARSER_CREATED));
    ROBO_CHECK(!(view.state & ROBO_HTML_ELEMENT_STATE_SELF_CLOSING));

    /* Relationships: linked, never detached. */
    ROBO_CHECK_EQ_INT(robo_html_element_parent(&div, &parent, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&parent, &root));
    ROBO_CHECK_EQ_INT(robo_html_node_child_count(&root, &count, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 1);
    {
        robo_html_node_handle_t first = el_child(&root, 0u);

        ROBO_CHECK(robo_html_node_handle_equal(&first, &div));
    }

    /* A second element lands after the first: creation order is tree order. */
    span = el_create_under(id, &root, "span", NULL);
    ROBO_CHECK(robo_html_element_is_element(&span));
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&span, "span"), 1);
    {
        size_t elements;
        robo_html_node_handle_t list[4];

        elements = el_element_children(&root, list, 4u);
        ROBO_CHECK_EQ_INT((int)elements, 2);
        ROBO_CHECK(robo_html_node_handle_equal(&list[0], &div));
        ROBO_CHECK(robo_html_node_handle_equal(&list[1], &span));
    }

    /* The tree is internally consistent after direct creation. */
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK(report.ok);
    ROBO_CHECK_EQ_INT((int)report.nodes, 3); /* root + div + span */

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_create_validation(void)
{
    robo_html_document_id_t id;
    robo_html_document_id_t other;
    robo_html_node_handle_t root;
    robo_html_node_handle_t foreign_root;
    robo_html_element_create_info_t info;
    robo_html_node_handle_t out;
    robo_html_diag_t diag;

    el_fixture_begin();
    id = el_doc();
    other = el_doc();
    root = el_root(id);
    foreign_root = el_root(other);

    robo_html_element_create_info_init(&info);
    info.parent = root;
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
    info.namespace_id = ROBO_HTML_NAMESPACE_HTML;
    info.local_name = "div";

    /* NULL arguments. */
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, NULL, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Missing / empty name. */
    info.local_name = NULL;
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_ELEMENT);
    info.local_name = "";
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    info.local_name = "div";

    /* Namespace must be live and not NONE. */
    info.namespace_id = ROBO_HTML_NAMESPACE_NONE;
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_ELEMENT);
    info.namespace_id = (robo_html_namespace_t)99;
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    info.namespace_id = ROBO_HTML_NAMESPACE_HTML;

    /* Attributes claimed without an array, and an unknown origin. */
    info.attribute_count = 1u;
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    info.attribute_count = 0u;
    info.origin = (robo_html_node_origin_t)99;
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;

    /* Parent must be well-formed, same-document and live. */
    {
        robo_html_node_handle_t bad;

        robo_html_node_handle_clear(&bad);
        info.parent = bad;
        ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
    info.parent = foreign_root;
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_ELEMENT);
    /* ... and the cross-document attempt must not attach anything to
     * either document. */
    ROBO_CHECK_EQ_INT((int)robo_html_document_node_count(id), 1);
    ROBO_CHECK_EQ_INT((int)robo_html_document_node_count(other), 1);
    {
        robo_html_node_handle_t ghost;

        ghost.document_id = id;
        ghost.node_id = (robo_html_node_id_t)9999;
        ghost.generation = 1u;
        info.parent = ghost;
        ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT((int)robo_html_document_node_count(id), 1);
    }
    info.parent = root;

    /* Unknown document. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_create(ROBO_HTML_DOCUMENT_ID_NONE, &info, &out, NULL),
        ROBO_NOT_FOUND);

    /* Failure never leaves a partially valid handle behind. */
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&out));

    /* A destroyed document cannot create, and neither can a retired id. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(other, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_create(other, &info, &out, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_create_refused_after_build_and_failure(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_element_create_info_t info;
    robo_html_node_handle_t out;

    el_fixture_begin();
    id = el_doc();
    el_build(id, "<p>done</p>", NULL);
    ROBO_CHECK(robo_html_document_is_finalized(id));

    root = robo_html_document_root(id);
    robo_html_element_create_info_init(&info);
    info.parent = root;
    info.origin = ROBO_HTML_NODE_ORIGIN_DOM;
    info.namespace_id = ROBO_HTML_NAMESPACE_HTML;
    info.local_name = "div";

    /* A completed build closes construction. */
    ROBO_CHECK_EQ_INT((int)robo_html_document_allows_construction(id), 0);
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL),
                      ROBO_ERROR);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&out));

    /* Reopening is explicit. */
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_html_document_allows_construction(id), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_element_is_element(&out));
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&out),
                      (int)ROBO_HTML_ELEMENT_CLASS_SECTION);

    /* A FAILED document refuses creation. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_failed(
                          id, ROBO_HTML_DOMAIN_PARSER, "test", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL),
                      ROBO_ERROR);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Identity, names and state                                           */
/* ------------------------------------------------------------------ */

static void test_el_identity_and_validity(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t html;
    robo_html_node_handle_t head;
    robo_html_node_handle_t body;
    robo_html_node_handle_t h1;
    robo_html_node_handle_t text;
    robo_html_node_handle_t comment;
    robo_html_node_handle_t doctype;
    robo_html_node_handle_t missing;
    robo_html_node_handle_t fresh;
    robo_html_element_view_t view;
    robo_html_diag_t diag;

    el_fixture_begin();
    id = el_doc();
    el_build(id,
             "<!DOCTYPE html><html><head><title>T</title></head>"
             "<body><h1>Hi</h1><!-- c --></body></html>",
             NULL);

    root = el_root(id);
    html = el_find(id, "html");
    h1 = el_find(id, "h1");
    missing = el_find(id, "video");
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&missing));

    /* The document node is not an element. */
    ROBO_CHECK_EQ_INT(robo_html_element_is_element(&root), 0);
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&root, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&root, &view, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_NODE);

    /* An element resolves, and comparison is case-insensitive. */
    ROBO_CHECK_EQ_INT(robo_html_element_is_element(&h1), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&h1, "h1"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&h1, "H1"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&h1, "p"), 0);

    /* A text child is a node, not an element. */
    text = el_child(&h1, 0u);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&text));
    ROBO_CHECK_EQ_INT(robo_html_element_is_element(&text), 0);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&text, "h1"), 0);
    ROBO_CHECK_STR_EQ(robo_html_element_name(&text, NULL), "");
    ROBO_CHECK_EQ_INT((int)robo_html_element_namespace(&text),
                      (int)ROBO_HTML_NAMESPACE_NONE);
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&text),
                      (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&text, &view, NULL),
                      ROBO_NOT_FOUND);

    /* A comment child is not an element either. */
    head = el_child(&html, 0u);
    body = el_child(&html, 1u);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&head, "head"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&body, "body"), 1);
    comment = el_child(&body, 1u);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&comment));
    ROBO_CHECK_EQ_INT(robo_html_element_is_element(&comment), 0);

    /* Neither is a DOCTYPE node. */
    doctype = el_child(&root, 0u);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&doctype));
    ROBO_CHECK_EQ_INT(robo_html_element_is_element(&doctype), 0);
    {
        robo_html_node_kind_t kind = ROBO_HTML_NODE_KIND_ELEMENT;

        ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&doctype, &kind),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)kind, (int)ROBO_HTML_NODE_KIND_DOCUMENT_TYPE);
    }

    /* Malformed and NULL handles are refused, never dereferenced. */
    {
        robo_html_node_handle_t malformed;
        robo_html_node_handle_t foreign;

        robo_html_node_handle_clear(&malformed);
        ROBO_CHECK_EQ_INT(robo_html_element_is_element(&malformed), 0);
        ROBO_CHECK_EQ_INT(robo_html_element_is_element(NULL), 0);
        ROBO_CHECK_EQ_INT(robo_html_element_is_named(NULL, "h1"), 0);
        ROBO_CHECK_EQ_INT(robo_html_element_is_named(&h1, NULL), 0);
        ROBO_CHECK_EQ_INT(robo_html_element_is_named(&h1, ""), 0);
        ROBO_CHECK_EQ_INT(robo_html_element_view_get(NULL, &view, NULL),
                          ROBO_INVALID_ARGUMENT);

        foreign = h1;
        foreign.document_id += 1000u;
        ROBO_CHECK_EQ_INT(robo_html_element_is_element(&foreign), 0);
        ROBO_CHECK_STR_EQ(robo_html_element_name(&foreign, NULL), "");
    }

    /* A reset retires every element handle from the discarded tree. */
    {
        robo_html_node_handle_t stale_h1 = h1;
        robo_html_node_handle_t stale_html = html;
        size_t length = 7u;

        ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_is_stale(&stale_h1));
        ROBO_CHECK(robo_html_node_handle_is_stale(&stale_html));
        ROBO_CHECK_EQ_INT(robo_html_element_is_element(&stale_h1), 0);
        ROBO_CHECK_EQ_INT(robo_html_element_is_named(&stale_h1, "h1"), 0);
        ROBO_CHECK_STR_EQ(robo_html_element_name(&stale_h1, &length), "");
        ROBO_CHECK_EQ_INT((int)length, 0);
        ROBO_CHECK_EQ_INT((int)robo_html_element_namespace(&stale_h1),
                          (int)ROBO_HTML_NAMESPACE_NONE);
        ROBO_CHECK_EQ_INT(robo_html_element_view_get(&stale_h1, &view, NULL),
                          ROBO_NOT_FOUND);
    }

    /* Rebuilding hands out fresh handles for the same names. */
    el_build(id, "<html><body><h1>Again</h1></body></html>", NULL);
    fresh = el_find(id, "h1");
    ROBO_CHECK(robo_html_element_is_element(&fresh));
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&fresh, "h1"), 1);
    ROBO_CHECK(!robo_html_node_handle_equal(&fresh, &h1));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_tag_names(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t upper;
    robo_html_node_handle_t custom;
    robo_html_node_handle_t long_el;
    char long_name[128];
    char expected[128];
    const char *stored;
    size_t length = 0u;
    size_t i;

    el_fixture_begin();
    id = el_doc();
    root = el_root(id);

    /* Creation lowercases, so the stored-name invariant holds for the DOM
     * seam too, not only for parser input. */
    upper = el_create_under(id, &root, "SeCtIoN", NULL);
    ROBO_CHECK(robo_html_element_is_element(&upper));
    ROBO_CHECK_STR_EQ(robo_html_element_name(&upper, &length), "section");
    ROBO_CHECK_EQ_INT((int)length, 7);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&upper, "SECTION"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&upper, "section"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&upper, "div"), 0);
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&upper),
                      (int)ROBO_HTML_ELEMENT_CLASS_SECTION);

    /* Unknown / custom-looking names are ordinary elements. */
    custom = el_create_under(id, &root, "my-widget-2", NULL);
    ROBO_CHECK(robo_html_element_is_element(&custom));
    ROBO_CHECK_STR_EQ(robo_html_element_name(&custom, NULL), "my-widget-2");
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&custom),
                      (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&custom, "MY-WIDGET-2"), 1);

    /* A name longer than the normalization buffer is stored verbatim: it is
     * never truncated, because shrinking a tag name would change the
     * element's identity. */
    for (i = 0u; i < 100u; i++) {
        long_name[i] = (i % 2u == 0u) ? 'A' : 'b';
        expected[i] = (i % 2u == 0u) ? 'A' : 'b'; /* unchanged */
    }
    long_name[100] = '\0';
    expected[100] = '\0';
    long_el = el_create_under(id, &root, long_name, NULL);
    ROBO_CHECK(robo_html_element_is_element(&long_el));
    stored = robo_html_element_name(&long_el, &length);
    ROBO_CHECK_EQ_INT((int)length, 100);
    ROBO_CHECK_STR_EQ(stored, expected);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&long_el, long_name), 1);

    /* Empty and NULL names are rejected at creation, so no element can ever
     * exist with an empty tag name. */
    {
        robo_result_t code = ROBO_OK;
        robo_html_node_handle_t empty = el_create_under(id, &root, "", &code);

        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&empty));
        ROBO_CHECK_EQ_INT(code, ROBO_INVALID_ARGUMENT);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_state_bits(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t img;
    robo_html_node_handle_t br;
    robo_html_element_view_t view;

    el_fixture_begin();
    id = el_doc();
    el_build(id, "<html><body><img src=test.png/><br><p>x</p></body></html>",
             NULL);
    img = el_find(id, "img");
    br = el_find(id, "br");

    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&img, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(view.self_closing, 1);
    ROBO_CHECK(view.state & ROBO_HTML_ELEMENT_STATE_SELF_CLOSING);
    ROBO_CHECK(view.state & ROBO_HTML_ELEMENT_STATE_PARSER_CREATED);
    ROBO_CHECK(view.state & ROBO_HTML_ELEMENT_STATE_CONNECTED);
    ROBO_CHECK_EQ_INT((int)view.classification,
                      (int)ROBO_HTML_ELEMENT_CLASS_REPLACED);

    /* A void element written without "/" is not self-closing. */
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&br, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(view.self_closing, 0);
    ROBO_CHECK(!(view.state & ROBO_HTML_ELEMENT_STATE_SELF_CLOSING));
    ROBO_CHECK(view.state & ROBO_HTML_ELEMENT_STATE_PARSER_CREATED);
    ROBO_CHECK(view.state & ROBO_HTML_ELEMENT_STATE_CONNECTED);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Relationships                                                       */
/* ------------------------------------------------------------------ */

static void test_el_relationships(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t html;
    robo_html_node_handle_t head;
    robo_html_node_handle_t body;
    robo_html_node_handle_t parent;
    robo_html_node_kind_t kind = ROBO_HTML_NODE_KIND_ELEMENT;
    robo_html_node_view_t body_view;
    size_t count = 0u;

    el_fixture_begin();
    id = el_doc();
    el_build(id, "<html><head><title>T</title></head><body><p>a</p></body></html>",
             NULL);
    root = el_root(id);
    html = el_find(id, "html");
    head = el_find(id, "head");
    body = el_find(id, "body");

    /* The document element's parent is the document node itself. */
    ROBO_CHECK_EQ_INT(robo_html_element_parent(&html, &parent, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&parent, &root));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&parent, &kind), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)kind, (int)ROBO_HTML_NODE_KIND_DOCUMENT);

    ROBO_CHECK_EQ_INT(robo_html_element_parent(&body, &parent, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&parent, &html));

    /* The document node is not an element, so it has no element parent. */
    ROBO_CHECK_EQ_INT(robo_html_element_parent(&root, &parent, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&parent));

    /* Element children of <html> are head then body. */
    {
        robo_html_node_handle_t list[4];
        size_t elements = el_element_children(&html, list, 4u);

        ROBO_CHECK_EQ_INT((int)elements, 2);
        ROBO_CHECK(robo_html_node_handle_equal(&list[0], &head));
        ROBO_CHECK(robo_html_node_handle_equal(&list[1], &body));
    }

    ROBO_CHECK_EQ_INT(robo_html_node_child_count(&html, &count, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 2);

    /* Out of range is a miss, never a zeroed-but-successful handle. */
    robo_html_node_handle_clear(&parent);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&html, 2u, &parent, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&parent));

    /* Sibling links agree with element order. */
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&body, &body_view, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&body_view.prev_sibling, &head));
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&body_view.next_sibling));

    /* Misuse is reported, not undefined. */
    ROBO_CHECK_EQ_INT(robo_html_element_parent(NULL, &parent, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_parent(&html, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_nesting_and_siblings(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t outer;
    robo_html_node_handle_t inner;
    robo_html_node_handle_t p1;
    robo_html_node_handle_t p2;
    robo_html_node_handle_t up;
    char text[64];

    el_fixture_begin();
    id = el_doc();
    el_build(id,
             "<html><body><div class='x'><span><a href='/'>link</a></span></div>"
             "<p>one</p><p>two</p></body></html>",
             NULL);

    outer = el_find(id, "div");
    inner = el_find(id, "span");
    p1 = el_find(id, "p");

    ROBO_CHECK(robo_html_element_is_named(&outer, "div"));
    ROBO_CHECK(robo_html_element_is_named(&inner, "span"));
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&inner),
                      (int)ROBO_HTML_ELEMENT_CLASS_TEXT);
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&p1),
                      (int)ROBO_HTML_ELEMENT_CLASS_PARAGRAPH);

    /* The nested element sits under the right ancestors. */
    ROBO_CHECK_EQ_INT(robo_html_element_parent(&inner, &up, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&up, &outer));
    ROBO_CHECK_EQ_INT(robo_html_element_parent(&outer, &up, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&up, "body"), 1);

    /* Siblings in document order: div, p, p. */
    {
        robo_html_node_handle_t body = el_find(id, "body");
        robo_html_node_handle_t list[8];
        size_t elements = el_element_children(&body, list, 8u);

        ROBO_CHECK_EQ_INT((int)elements, 3);
        ROBO_CHECK(robo_html_node_handle_equal(&list[0], &outer));
        ROBO_CHECK(robo_html_node_handle_equal(&list[1], &p1));
        p2 = list[2];
        ROBO_CHECK(robo_html_element_is_named(&p2, "p"));
        ROBO_CHECK(!robo_html_node_handle_equal(&p1, &p2));
    }

    el_text(inner, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "link");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_ownership_rules(void)
{
    robo_html_document_id_t a;
    robo_html_document_id_t b;
    robo_html_node_handle_t a_root;
    robo_html_node_handle_t b_root;
    robo_html_node_handle_t a_child;
    robo_html_node_handle_t b_child;

    el_fixture_begin();
    a = el_doc();
    b = el_doc();
    a_root = el_root(a);
    b_root = el_root(b);

    a_child = el_create_under(a, &a_root, "div", NULL);
    b_child = el_create_under(b, &b_root, "div", NULL);
    ROBO_CHECK(robo_html_element_is_element(&a_child));
    ROBO_CHECK(robo_html_element_is_element(&b_child));

    /* One document owns its element and not the other's. */
    ROBO_CHECK(robo_html_document_owns_node(a, &a_child));
    ROBO_CHECK(!robo_html_document_owns_node(a, &b_child));
    ROBO_CHECK(robo_html_document_owns_node(b, &b_child));
    ROBO_CHECK(!robo_html_document_owns_node(b, &a_child));

    ROBO_CHECK(!robo_html_node_handle_equal(&a_child, &b_child));
    ROBO_CHECK_EQ_INT((int)robo_html_node_handle_document(&a_child), (int)a);
    ROBO_CHECK_EQ_INT((int)robo_html_node_handle_document(&b_child), (int)b);

    /*
     * A handle is inert identity, not a capability: the document id is what
     * separates the two documents' node 2, and ownership compares the whole
     * triple. A handle whose document id names a different live document is
     * simply not owned here, and a generation that was never issued for this
     * document is not owned either.
     */
    {
        robo_html_node_handle_t retired = a_child;

        retired.generation += 1u;
        ROBO_CHECK(!robo_html_document_owns_node(a, &retired));
        ROBO_CHECK_EQ_INT(robo_html_element_is_element(&retired), 0);
    }

    /* Destroying A retires A's elements and leaves B untouched. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(a, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_stale(&a_child));
    ROBO_CHECK_EQ_INT(robo_html_element_is_element(&a_child), 0);
    ROBO_CHECK(robo_html_element_is_element(&b_child));
    ROBO_CHECK(robo_html_document_owns_node(b, &b_child));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(b, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Attributes (the 8.6 boundary, as shepherded in 8.5)                 */
/* ------------------------------------------------------------------ */

static void test_el_attributes(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t div;
    robo_html_node_handle_t input;
    robo_html_node_handle_t p;
    robo_html_node_handle_t text;
    robo_html_element_view_t view;
    robo_html_attribute_t attr;
    size_t count = 0u;

    el_fixture_begin();
    id = el_doc();
    el_build(id,
             "<html><body><div CLASS='Test' id=\"main\" data-x='1' class='second'>"
             "<p>x</p></div><input disabled value='hello world'></body></html>",
             NULL);
    div = el_find(id, "div");
    input = el_find(id, "input");
    p = el_find(id, "p");
    text = el_child(&p, 0u);
    ROBO_CHECK_EQ_INT(robo_html_element_is_element(&text), 0);

    /* Count + iteration in source order, duplicates preserved. */
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&div, &count, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 4);
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&div, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)view.attribute_count, 4);

    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&div, 0u, &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.name, "class"); /* lowercased by the tokenizer */
    ROBO_CHECK_STR_EQ(attr.value, "Test");
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&div, 1u, &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.name, "id");
    ROBO_CHECK_STR_EQ(attr.value, "main");
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&div, 2u, &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.name, "data-x");
    ROBO_CHECK_STR_EQ(attr.value, "1");
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&div, 3u, &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.value, "second"); /* the duplicate stays one */

    /* Out of range and misuse. */
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&div, 4u, &attr, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&div, 0u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(NULL, 0u, &attr, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(NULL, &count, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&div, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&text, &count, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&text, 0u, &attr, NULL),
                      ROBO_NOT_FOUND);

    /* Lookup by name is case-insensitive; the first match wins. */
    ROBO_CHECK_EQ_INT(robo_html_element_find_attribute(&div, "id", &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.value, "main");
    ROBO_CHECK_EQ_INT(
        robo_html_element_find_attribute(&div, "CLASS", &attr, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.value, "Test");
    ROBO_CHECK_EQ_INT(
        robo_html_element_find_attribute(&div, "missing", &attr, NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_element_find_attribute(&div, NULL, &attr, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Presence is a separate question from value. */
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "id"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "ID"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, "nope"), 0);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&div, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(NULL, "id"), 0);

    /* A boolean-style attribute is present with an empty value. */
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&input, "disabled"), 1);
    ROBO_CHECK_EQ_INT(
        robo_html_element_find_attribute(&input, "disabled", &attr, NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.value, "");
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&input, &count, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 2);
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_at(&input, 1u, &attr, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(attr.name, "value");
    ROBO_CHECK_STR_EQ(attr.value, "hello world");

    /* An element created with no attributes reports none. */
    ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&p, &count, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 0);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Tree builder integration                                            */
/* ------------------------------------------------------------------ */

static void test_el_start_and_end_tags(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t html;
    robo_html_node_handle_t h1;
    robo_html_node_handle_t title;
    robo_html_node_handle_t up;
    robo_html_element_view_t view;
    robo_html_tree_result_t result;
    char text[64];

    el_fixture_begin();
    id = el_doc();
    el_build(id,
             "<html><head><title>T</title></head><body><h1>Hello</h1></body></html>",
             &result);

    ROBO_CHECK_EQ_INT((int)result.element_count, 5); /* html head title h1 body */
    ROBO_CHECK_EQ_INT((int)result.text_count, 2);    /* T, Hello */
    ROBO_CHECK_EQ_INT((int)result.truncated, 0);

    html = el_find(id, "html");
    h1 = el_find(id, "h1");
    title = el_find(id, "title");

    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&html, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)view.namespace_id, (int)ROBO_HTML_NAMESPACE_HTML);
    ROBO_CHECK_EQ_INT((int)view.classification,
                      (int)ROBO_HTML_ELEMENT_CLASS_ROOT);
    ROBO_CHECK(view.state & ROBO_HTML_ELEMENT_STATE_PARSER_CREATED);
    ROBO_CHECK(view.state & ROBO_HTML_ELEMENT_STATE_CONNECTED);

    ROBO_CHECK(robo_html_element_is_named(&title, "title"));
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&title),
                      (int)ROBO_HTML_ELEMENT_CLASS_METADATA);
    ROBO_CHECK(robo_html_element_is_named(&h1, "h1"));
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&h1),
                      (int)ROBO_HTML_ELEMENT_CLASS_HEADING);

    /* The end tag closed <h1>: its text is a child, and the heading's parent
     * is <body>, so a later sibling would land there and not inside it. */
    el_text(h1, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "Hello");
    ROBO_CHECK_EQ_INT(robo_html_element_parent(&h1, &up, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&up, "body"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_parent(&title, &up, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&up, "head"), 1);

    /* Every element this phase claims to support exists, named and in the
     * HTML namespace. */
    el_build(id,
             "<html><head><title>T</title></head><body><h1>a</h1><p>b</p>"
             "<div>c</div><span>d</span><button>e</button>"
             "<img src=x><a href=y>f</a><form><input></form></body></html>",
             NULL);
    {
        static const char *const k_names[] = {
            "html", "head", "title", "body", "h1",   "p",     "div",
            "span", "button", "img",   "a",    "form",  "input",
        };
        size_t i;

        for (i = 0u; i < sizeof(k_names) / sizeof(k_names[0]); i++) {
            robo_html_node_handle_t h = el_find(id, k_names[i]);

            ROBO_CHECK_MSG(robo_html_element_is_element(&h), k_names[i]);
            ROBO_CHECK_EQ_INT(robo_html_element_is_named(&h, k_names[i]), 1);
            ROBO_CHECK_EQ_INT((int)robo_html_element_namespace(&h),
                              (int)ROBO_HTML_NAMESPACE_HTML);
        }
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_malformed_recovery(void)
{
    static const char *const k_cases[] = {
        "",
        "<",
        "</",
        "<div",
        "<div a=",
        "<div a=\"",
        "<div a='x'/",
        "</>",
        "<h1>x</h2>",
        "<div><p>unclosed",
        "<p>a<p>b",
        "<!-- unterminated",
        "<!DOCTYPE",
        "<div></div></div>",
        "text before html",
    };
    size_t i;

    el_fixture_begin();

    for (i = 0u; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        robo_html_document_id_t id = el_doc();
        robo_html_tree_report_t report;
        robo_html_node_handle_t root;
        size_t children = 0u;
        size_t index;

        el_build(id, k_cases[i], NULL);
        root = el_root(id);
        ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));

        /* Whatever the builder decided, the tree stays internally valid and
         * every element in it is an identifiable HTML element. */
        ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                          ROBO_OK);
        ROBO_CHECK_MSG(report.ok, k_cases[i]);
        ROBO_CHECK_EQ_INT(robo_html_node_child_count(&root, &children, NULL),
                          ROBO_OK);
        for (index = 0u; index < children; index++) {
            robo_html_node_handle_t child = el_child(&root, index);

            if (!robo_html_element_is_element(&child)) {
                continue;
            }
            ROBO_CHECK_MSG(
                robo_html_element_namespace(&child) == ROBO_HTML_NAMESPACE_HTML,
                k_cases[i]);
        }
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    el_fixture_end();
}

static void test_el_unicode_text(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t p;
    char text[128];

    el_fixture_begin();
    id = el_doc();
    /* UTF-8 passes through unchanged: deciding what an element is must not
     * mangle what it contains. */
    el_build(id, "<html><body><p>Héllo — 日本語 🚀</p></body></html>", NULL);
    p = el_find(id, "p");
    ROBO_CHECK(robo_html_element_is_named(&p, "p"));
    el_text(p, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "Héllo — 日本語 🚀");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Teardown                                                            */
/* ------------------------------------------------------------------ */

static void test_el_teardown(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t h1;
    robo_html_node_handle_t body;
    robo_html_element_view_t view;

    el_fixture_begin();
    id = el_doc();
    el_build(id, "<html><body><h1>Hi</h1></body></html>", NULL);
    h1 = el_find(id, "h1");
    body = el_find(id, "body");
    ROBO_CHECK(robo_html_element_is_element(&h1));

    /* A reset drops the whole element tree and retires every handle. */
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_stale(&h1));
    ROBO_CHECK(robo_html_node_handle_is_stale(&body));
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&h1, &view, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)robo_html_document_node_count(id), 1);

    /* The document survives the reset and can build again. */
    el_build(id, "<html><body><p>second life</p></body></html>", NULL);
    {
        robo_html_node_handle_t p = el_find(id, "p");
        char text[32];

        ROBO_CHECK(robo_html_element_is_named(&p, "p"));
        el_text(p, text, sizeof(text));
        ROBO_CHECK_STR_EQ(text, "second life");
        /* The builder implies the structural elements, so a document that
         * names only html/body/p still has a head: document, html, head,
         * body, p and the text node. */
        ROBO_CHECK_EQ_INT((int)robo_html_document_node_count(id), 6);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    /* Destroy is terminal and repeatable. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_element_is_element(&h1), 0);

    el_fixture_end();
}

static void test_el_large_tree_teardown(void)
{
    robo_html_document_id_t id;
    char source[8192];
    size_t used = 0u;
    size_t i;
    robo_html_tree_result_t result;
    robo_html_node_handle_t deepest;
    robo_html_tree_report_t report;

    el_fixture_begin();
    id = el_doc();

    /* A deep chain. Ownership is what makes teardown one operation rather
     * than a walk, so building many elements must not change how cleanly
     * they go away. */
    used += (size_t)snprintf(source + used, sizeof(source) - used,
                             "<html><body>");
    for (i = 0u; i < 200u; i++) {
        used += (size_t)snprintf(source + used, sizeof(source) - used,
                                 "<div>");
    }
    used += (size_t)snprintf(source + used, sizeof(source) - used,
                             "<p>deep</p>");
    for (i = 0u; i < 200u; i++) {
        used += (size_t)snprintf(source + used, sizeof(source) - used,
                                 "</div>");
    }
    used += (size_t)snprintf(source + used, sizeof(source) - used,
                             "</body></html>");
    el_build(id, source, &result);

    ROBO_CHECK(result.element_count >= 200u);
    deepest = el_find(id, "p");
    ROBO_CHECK(robo_html_element_is_named(&deepest, "p"));
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK(report.ok);
    ROBO_CHECK(report.max_depth >= 200u);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Debug output + the demo document                                    */
/* ------------------------------------------------------------------ */

static void test_el_tree_dump(void)
{
    robo_html_document_id_t id;
    char dump[1024];
    size_t length = 0u;

    el_fixture_begin();
    id = el_doc();
    el_build(id,
             "<!DOCTYPE html><html><head><title>T</title></head>"
             "<body><h1>Hi</h1><button>Go</button></body></html>",
             NULL);
    dump[0] = '\0';
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), &length, NULL),
        ROBO_OK);
    ROBO_CHECK(length > 0u);

    /* The dump identifies elements by name, so it is usable for debugging
     * element-model problems without a viewer. */
    ROBO_CHECK(strstr(dump, "Document") != NULL);
    ROBO_CHECK(strstr(dump, "doctype html") != NULL);
    ROBO_CHECK(strstr(dump, "head") != NULL);
    ROBO_CHECK(strstr(dump, "title") != NULL);
    ROBO_CHECK(strstr(dump, "body") != NULL);
    ROBO_CHECK(strstr(dump, "h1") != NULL);
    ROBO_CHECK(strstr(dump, "button") != NULL);
    ROBO_CHECK(strstr(dump, "Text: \"Hi\"") != NULL);
    /* HTML names are printed unprefixed. */
    ROBO_CHECK(strstr(dump, "html:") == NULL);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_demo_document_tree(void)
{
    static const char *const k_demo =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<title>RoboticelaOS Runtime</title>\n"
        "</head>\n"
        "<body>\n"
        "<h1>RoboticelaOS Runtime</h1>\n"
        "<p>Independent HTML rendering is working.</p>\n"
        "<p>RoboGraphics + RoboWindow + RoboHTML</p>\n"
        "<button>Demo Button</button>\n"
        "</body>\n"
        "</html>\n";
    robo_html_document_id_t id;
    robo_html_tree_result_t result;
    robo_html_tree_report_t report;
    robo_html_node_handle_t html;
    robo_html_node_handle_t head;
    robo_html_node_handle_t body;
    robo_html_node_handle_t list[8];
    size_t elements;
    char text[128];

    el_fixture_begin();
    id = el_doc();
    el_build(id, k_demo, &result);

    /* Document → html → head/title + body/h1/p/p/button. Element counts are
     * independent of the whitespace text nodes the builder keeps. */
    ROBO_CHECK_EQ_INT((int)result.element_count, 8);
    ROBO_CHECK_EQ_INT((int)result.doctype_count, 1);
    ROBO_CHECK_EQ_INT((int)result.comment_count, 0);
    ROBO_CHECK_EQ_INT((int)result.attribute_count, 0);
    ROBO_CHECK_EQ_INT((int)result.truncated, 0);

    html = el_find(id, "html");
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&html),
                      (int)ROBO_HTML_ELEMENT_CLASS_ROOT);

    head = el_find(id, "head");
    body = el_find(id, "body");
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&head),
                      (int)ROBO_HTML_ELEMENT_CLASS_METADATA);
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&body),
                      (int)ROBO_HTML_ELEMENT_CLASS_SECTION);

    /* <head> holds exactly <title>. */
    elements = el_element_children(&head, list, 8u);
    ROBO_CHECK_EQ_INT((int)elements, 1);
    ROBO_CHECK(robo_html_element_is_named(&list[0], "title"));
    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&list[0]),
                      (int)ROBO_HTML_ELEMENT_CLASS_METADATA);
    el_text(list[0], text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "RoboticelaOS Runtime");

    /* <body> holds exactly h1, p, p, button — in that order. */
    elements = el_element_children(&body, list, 8u);
    ROBO_CHECK_EQ_INT((int)elements, 4);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&list[0], "h1"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&list[1], "p"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&list[2], "p"), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_is_named(&list[3], "button"), 1);

    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&list[0]),
                      (int)ROBO_HTML_ELEMENT_CLASS_HEADING);
    el_text(list[0], text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "RoboticelaOS Runtime");

    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&list[1]),
                      (int)ROBO_HTML_ELEMENT_CLASS_PARAGRAPH);
    el_text(list[1], text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "Independent HTML rendering is working.");

    el_text(list[2], text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "RoboGraphics + RoboWindow + RoboHTML");

    ROBO_CHECK_EQ_INT((int)robo_html_element_classification(&list[3]),
                      (int)ROBO_HTML_ELEMENT_CLASS_CONTROL);
    el_text(list[3], text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "Demo Button");

    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK(report.ok);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */

void robo_test_register_html_element(void)
{
    robo_test_register("html.element.namespace_helpers",
                       test_el_namespace_helpers);
    robo_test_register("html.element.classification_helpers",
                       test_el_classification_helpers);
    robo_test_register("html.element.create_info_init",
                       test_el_create_info_init);
    robo_test_register("html.element.create_basic", test_el_create_basic);
    robo_test_register("html.element.create_validation",
                       test_el_create_validation);
    robo_test_register("html.element.create_refused",
                       test_el_create_refused_after_build_and_failure);
    robo_test_register("html.element.identity", test_el_identity_and_validity);
    robo_test_register("html.element.tag_names", test_el_tag_names);
    robo_test_register("html.element.state", test_el_state_bits);
    robo_test_register("html.element.relationships", test_el_relationships);
    robo_test_register("html.element.nesting", test_el_nesting_and_siblings);
    robo_test_register("html.element.ownership", test_el_ownership_rules);
    robo_test_register("html.element.attributes", test_el_attributes);
    robo_test_register("html.element.start_end_tags",
                       test_el_start_and_end_tags);
    robo_test_register("html.element.malformed", test_el_malformed_recovery);
    robo_test_register("html.element.unicode_text", test_el_unicode_text);
    robo_test_register("html.element.teardown", test_el_teardown);
    robo_test_register("html.element.large_tree_teardown",
                       test_el_large_tree_teardown);
    robo_test_register("html.element.tree_dump", test_el_tree_dump);
    robo_test_register("html.element.demo_document", test_el_demo_document_tree);
}
