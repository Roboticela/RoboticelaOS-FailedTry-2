/*
 * RoboHTML Phase 8.7 text-node suite:
 * creation through the model seam, kind identification, content access,
 * set/append mutation, document ownership and cross-document rejection,
 * parser integration (runs, merging, character references), whitespace and
 * Unicode preservation, traversal, teardown, malformed recovery, the tree
 * dump, and the visual demo document's text.
 *
 * Note on child indices: the tree builder keeps whitespace between tags as
 * text nodes (the 8.3 suite documents that), so a child index is a real
 * index into the child list. Tests that care about *which* children exist
 * walk the list and classify by kind rather than assuming a layout.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void tx_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void tx_fixture_end(void)
{
    robo_html_shutdown();
}

static robo_html_document_id_t tx_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/** Build @p text into @p id with the one-shot API; result optional. */
static void tx_build(robo_html_document_id_t id,
                     const char *text,
                     robo_html_tree_result_t *out)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, out, NULL), ROBO_OK);
}

static robo_html_node_handle_t tx_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

/** Handle of the first descendant element named @p name (invalid when none). */
static robo_html_node_handle_t tx_find(robo_html_document_id_t id,
                                       const char *name)
{
    robo_html_node_handle_t root = tx_root(id);
    robo_html_node_handle_t found;

    robo_html_node_handle_clear(&found);
    if (robo_html_node_find_element(&root, name, &found, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&found);
    }
    return found;
}

/** Child @p index (any kind); invalid when out of range. */
static robo_html_node_handle_t tx_child(const robo_html_node_handle_t *parent,
                                        size_t index)
{
    robo_html_node_handle_t out;

    robo_html_node_handle_clear(&out);
    if (robo_html_node_child_at(parent, index, &out, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&out);
    }
    return out;
}

/** Direct child count; 0 when the handle is not live. */
static size_t tx_child_count(const robo_html_node_handle_t *parent)
{
    size_t count = 0u;

    if (robo_html_node_child_count(parent, &count, NULL) != ROBO_OK) {
        return 0u;
    }
    return count;
}

/** Concatenated subtree text into @p buf ("" when the call fails). */
static void tx_text(const robo_html_node_handle_t *h, char *buf, size_t cap)
{
    size_t length = 0u;

    buf[0] = '\0';
    if (robo_html_node_subtree_text(h, buf, cap, &length, NULL) != ROBO_OK) {
        buf[0] = '\0';
    }
}

/** Subtree text of the first descendant element named @p name. */
static void tx_text_of(robo_html_document_id_t id,
                       const char *name,
                       char *buf,
                       size_t cap)
{
    robo_html_node_handle_t h = tx_find(id, name);

    tx_text(&h, buf, cap);
}

/** Child @p index of @p parent as text bytes ("" when it is not text). */
static void tx_child_text(const robo_html_node_handle_t *parent,
                          size_t index,
                          char *buf,
                          size_t cap)
{
    robo_html_node_handle_t child = tx_child(parent, index);
    size_t length = 0u;
    const char *content = robo_html_text_content(&child, &length);

    buf[0] = '\0';
    if (!content || length >= cap) {
        return;
    }
    memcpy(buf, content, length);
    buf[length] = '\0';
}

/** Index of the @p nth text child of @p parent; -1 when there is none. */
static long tx_text_child_index(const robo_html_node_handle_t *parent, size_t n)
{
    size_t children = tx_child_count(parent);
    size_t seen = 0u;
    size_t i;

    for (i = 0u; i < children; i++) {
        robo_html_node_handle_t child = tx_child(parent, i);

        if (!robo_html_text_is_text(&child)) {
            continue;
        }
        if (seen == n) {
            return (long)i;
        }
        seen++;
    }
    return -1;
}

/** Count of direct text children of @p parent. */
static size_t tx_text_child_count(const robo_html_node_handle_t *parent)
{
    size_t children = tx_child_count(parent);
    size_t texts = 0u;
    size_t i;

    for (i = 0u; i < children; i++) {
        robo_html_node_handle_t child = tx_child(parent, i);

        if (robo_html_text_is_text(&child)) {
            texts++;
        }
    }
    return texts;
}

/** Create an element under @p parent; invalid handle on failure. */
static robo_html_node_handle_t tx_create_element(
    robo_html_document_id_t id,
    const robo_html_node_handle_t *parent,
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
    if (robo_html_element_create(id, &info, &out, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&out);
    }
    return out;
}

/** Create a text node under @p parent via the model seam. */
static robo_html_node_handle_t tx_create_text(
    robo_html_document_id_t id,
    const robo_html_node_handle_t *parent,
    const char *content,
    size_t length,
    robo_html_node_origin_t origin,
    robo_result_t *out_code)
{
    robo_html_text_create_info_t info;
    robo_html_node_handle_t out;
    robo_result_t r;

    robo_html_text_create_info_init(&info);
    info.parent = *parent;
    info.origin = origin;
    info.content = content;
    info.length = length;

    robo_html_node_handle_clear(&out);
    r = robo_html_text_create(id, &info, &out, NULL);
    if (out_code) {
        *out_code = r;
    }
    if (r != ROBO_OK) {
        robo_html_node_handle_clear(&out);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

static void test_tx_create_info_init(void)
{
    robo_html_text_create_info_t info;

    memset(&info, 0xA5, sizeof(info));
    robo_html_text_create_info_init(&info);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&info.parent));
    ROBO_CHECK_EQ_INT((int)info.origin, (int)ROBO_HTML_NODE_ORIGIN_NONE);
    ROBO_CHECK(info.content == NULL);
    ROBO_CHECK_EQ_INT((int)info.length, 0);

    /* Safe on NULL. */
    robo_html_text_create_info_init(NULL);
}

static void test_tx_create_basic(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t div;
    robo_html_node_handle_t text;
    robo_html_text_view_t view;
    robo_html_node_handle_t back;
    size_t length = 0u;
    const char *content;

    tx_fixture_begin();
    id = tx_doc();

    /* A document with no build yet: the root exists, so a parent does too. */
    root = tx_root(id);
    div = tx_create_element(id, &root, "div");
    ROBO_CHECK(robo_html_element_is_element(&div));

    text = tx_create_text(id, &div, "Hello", 5u, ROBO_HTML_NODE_ORIGIN_TEST,
                          NULL);
    ROBO_CHECK(robo_html_text_is_text(&text));
    ROBO_CHECK(!robo_html_element_is_element(&text));

    /* Identity: it is a real, live, resolvable node of this document. */
    ROBO_CHECK(!robo_html_node_handle_is_stale(&text));
    ROBO_CHECK_EQ_INT((int)robo_html_node_handle_document(&text), (int)id);
    ROBO_CHECK(robo_html_node_handle_equal(&text, &text));

    content = robo_html_text_content(&text, &length);
    ROBO_CHECK(content != NULL);
    ROBO_CHECK_EQ_INT((int)length, 5);
    ROBO_CHECK_STR_EQ(content, "Hello");
    ROBO_CHECK_EQ_INT((int)robo_html_text_length(&text), 5);
    ROBO_CHECK_EQ_INT(robo_html_text_is_empty(&text), 0);

    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&text, &view, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&view.handle, &text));
    ROBO_CHECK_EQ_INT((int)view.length, 5);
    ROBO_CHECK_STR_EQ(view.content, "Hello");
    ROBO_CHECK(robo_html_node_handle_equal(&view.parent, &div));
    /* Created by the DOM seam, not the parser. */
    ROBO_CHECK_EQ_INT((int)(view.state & ROBO_HTML_TEXT_STATE_PARSER_CREATED),
                      0);
    ROBO_CHECK((view.state & ROBO_HTML_TEXT_STATE_CONNECTED) != 0u);

    /* It is attached: last child of its parent. */
    ROBO_CHECK_EQ_INT((int)tx_child_count(&div), 1);
    back = tx_child(&div, 0u);
    ROBO_CHECK(robo_html_node_handle_equal(&back, &text));
    ROBO_CHECK_EQ_INT(robo_html_text_parent(&text, &back, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&back, &div));

    ROBO_CHECK_EQ_INT(robo_html_text_parent(&root, &back, NULL),
                      ROBO_NOT_FOUND);

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

static void test_tx_create_empty(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t div;
    robo_html_node_handle_t empty;
    size_t length = 123u;
    const char *content;

    tx_fixture_begin();
    id = tx_doc();
    root = tx_root(id);
    div = tx_create_element(id, &root, "div");

    /*
     * The model permits an empty text node (a zero-length run is a legal
     * value); only the parser declines to produce one, because a zero-length
     * character run carries nothing.
     */
    empty = tx_create_text(id, &div, NULL, 0u, ROBO_HTML_NODE_ORIGIN_TEST,
                           NULL);
    ROBO_CHECK(robo_html_text_is_text(&empty));
    content = robo_html_text_content(&empty, &length);
    /* "" with length 0, never NULL: present-and-empty, not absent. */
    ROBO_CHECK(content != NULL);
    ROBO_CHECK_EQ_INT((int)length, 0);
    ROBO_CHECK_STR_EQ(content, "");
    ROBO_CHECK_EQ_INT((int)robo_html_text_length(&empty), 0);
    ROBO_CHECK_EQ_INT(robo_html_text_is_empty(&empty), 1);
    ROBO_CHECK_EQ_INT((int)tx_child_count(&div), 1);

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

static void test_tx_create_validation(void)
{
    robo_html_document_id_t id;
    robo_html_document_id_t other;
    robo_html_node_handle_t root;
    robo_html_node_handle_t div;
    robo_html_node_handle_t out;
    robo_html_node_handle_t stranger;
    robo_html_text_create_info_t info;
    robo_result_t code = ROBO_OK;

    tx_fixture_begin();
    id = tx_doc();
    other = tx_doc();
    root = tx_root(id);
    div = tx_create_element(id, &root, "div");
    robo_html_text_create_info_init(&info);

    /* NULL arguments. */
    ROBO_CHECK_EQ_INT(robo_html_text_create(id, NULL, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_text_create(id, &info, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* NULL content with a non-zero length. */
    info.parent = div;
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
    info.content = NULL;
    info.length = 3u;
    ROBO_CHECK_EQ_INT(robo_html_text_create(id, &info, &out, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Origin NONE and an undefined origin are both refused. */
    info.content = "abc";
    info.origin = ROBO_HTML_NODE_ORIGIN_NONE;
    ROBO_CHECK_EQ_INT(robo_html_text_create(id, &info, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    info.origin = (robo_html_node_origin_t)77;
    ROBO_CHECK_EQ_INT(robo_html_text_create(id, &info, &out, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Ill-formed parent handle. */
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
    robo_html_node_handle_clear(&info.parent);
    ROBO_CHECK_EQ_INT(robo_html_text_create(id, &info, &out, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Unknown document. */
    info.parent = div;
    ROBO_CHECK_EQ_INT(
        robo_html_text_create(ROBO_HTML_DOCUMENT_ID_NONE, &info, &out, NULL),
        ROBO_NOT_FOUND);

    /*
     * A parent that belongs to another document is refused before anything is
     * resolved — the whole reason the handle records its owning document.
     */
    {
        robo_html_node_handle_t other_root = tx_root(other);
        robo_html_node_handle_t other_div =
            tx_create_element(other, &other_root, "div");

        info.parent = other_div;
        ROBO_CHECK_EQ_INT(robo_html_text_create(id, &info, &out, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK(!robo_html_text_is_text(&out) || out.node_id == 0u);
    }

    /* A handle that claims this document but names no live node. */
    stranger.document_id = id;
    stranger.node_id = 9999u;
    stranger.generation = 1u;
    info.parent = stranger;
    ROBO_CHECK_EQ_INT(robo_html_text_create(id, &info, &out, NULL),
                      ROBO_NOT_FOUND);

    /* Every rejection left @p out cleared and created nothing. */
    ROBO_CHECK_EQ_INT((int)tx_child_count(&div), 0);

    code = ROBO_OK;
    info.parent = div;
    {
        robo_html_node_handle_t ok =
            tx_create_text(id, &div, "ok", 2u, ROBO_HTML_NODE_ORIGIN_TEST,
                           &code);

        ROBO_CHECK(robo_html_text_is_text(&ok));
    }
    ROBO_CHECK_EQ_INT((int)code, (int)ROBO_OK);

    (void)robo_html_document_destroy(id, NULL);
    (void)robo_html_document_destroy(other, NULL);
    tx_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Kind identification and the not-text answers                        */
/* ------------------------------------------------------------------ */

static void test_tx_kind_identification(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t paragraph;
    robo_html_node_handle_t text;
    robo_html_node_handle_t comment = {0u, 0u, 0u};
    robo_html_node_handle_t first;
    robo_html_node_view_t view;
    robo_html_node_kind_t kind = ROBO_HTML_NODE_KIND_DOCUMENT;

    tx_fixture_begin();
    id = tx_doc();
    tx_build(id, "<!DOCTYPE html><html><body><p>Hello<!--x--></p></body>"
                 "</html>",
             NULL);

    paragraph = tx_find(id, "p");
    ROBO_CHECK(robo_html_element_is_element(&paragraph));
    ROBO_CHECK(!robo_html_text_is_text(&paragraph));

    /* The <p> has a text child and a comment child. */
    ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 2);
    first = tx_child(&paragraph, 0u);
    ROBO_CHECK(robo_html_text_is_text(&first));
    ROBO_CHECK(!robo_html_element_is_element(&first));
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&first, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(view.kind, ROBO_HTML_NODE_KIND_TEXT);

    comment = tx_child(&paragraph, 1u);
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&comment, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(view.kind, ROBO_HTML_NODE_KIND_COMMENT);
    /* A comment is not text, so the text model refuses it. */
    ROBO_CHECK(!robo_html_text_is_text(&comment));

    /* The document node and the root element are not text either. */
    root = tx_root(id);
    ROBO_CHECK(!robo_html_text_is_text(&root));
    ROBO_CHECK(!robo_html_text_is_text(NULL));

    /* Role via the node handle, not a string comparison. */
    text = first;
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&text, &kind), ROBO_OK);
    ROBO_CHECK_EQ_INT(kind, ROBO_HTML_NODE_KIND_TEXT);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&paragraph, &kind),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(kind, ROBO_HTML_NODE_KIND_ELEMENT);

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

static void test_tx_not_text_answers(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t paragraph;
    robo_html_node_handle_t out;
    robo_html_text_view_t view;
    size_t length = 99u;

    tx_fixture_begin();
    id = tx_doc();
    tx_build(id, "<html><body><p>Hello</p></body></html>", NULL);
    root = tx_root(id);
    paragraph = tx_find(id, "p");

    /* Content is NULL — "not a text node", distinct from "" (empty run). */
    ROBO_CHECK(robo_html_text_content(&paragraph, &length) == NULL);
    ROBO_CHECK_EQ_INT((int)length, 0);
    ROBO_CHECK_EQ_INT((int)robo_html_text_length(&paragraph), 0);
    ROBO_CHECK_EQ_INT(robo_html_text_is_empty(&paragraph), 0);
    ROBO_CHECK(robo_html_text_content(NULL, &length) == NULL);
    ROBO_CHECK(robo_html_text_content(&root, NULL) == NULL);

    /* Structural accessors report the misuse rather than guessing. */
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&paragraph, &view, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&paragraph, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(NULL, &view, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&root, &view, NULL),
                      ROBO_NOT_FOUND);

    /* Mutation on a non-text node is refused, not applied to the parent. */
    ROBO_CHECK_EQ_INT(robo_html_text_set(&paragraph, "x", 1u, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_text_append(&paragraph, "x", 1u, NULL),
                      ROBO_NOT_FOUND);

    /* Ill-formed handles are argument errors, not lookups that missed. */
    robo_html_node_handle_clear(&out);
    /* A well-formed handle that names nothing live is a miss, not misuse. */
    ROBO_CHECK_EQ_INT(robo_html_text_set(&out, "x", 1u, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_text_append(&out, "x", 1u, NULL),
                      ROBO_NOT_FOUND);
    /* A NULL handle is misuse. */
    ROBO_CHECK_EQ_INT(robo_html_text_set(NULL, "x", 1u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_text_append(NULL, "x", 1u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_text_parent(&out, &paragraph, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_text_parent(&paragraph, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(!robo_html_text_is_text(&out));

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Mutation                                                            */
/* ------------------------------------------------------------------ */

static void test_tx_set(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t div;
    robo_html_node_handle_t text;
    robo_html_node_handle_t sibling;
    robo_html_text_view_t before;
    robo_html_text_view_t after;
    size_t length = 0u;
    const char *content;

    tx_fixture_begin();
    id = tx_doc();
    root = tx_root(id);
    div = tx_create_element(id, &root, "div");
    text = tx_create_text(id, &div, "first", 5u, ROBO_HTML_NODE_ORIGIN_TEST,
                          NULL);
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&text, &before, NULL), ROBO_OK);

    /* Replace with something longer. */
    ROBO_CHECK_EQ_INT(robo_html_text_set(&text, "a longer run", 12u, NULL),
                      ROBO_OK);
    content = robo_html_text_content(&text, &length);
    ROBO_CHECK(content != NULL);
    ROBO_CHECK_EQ_INT((int)length, 12);
    ROBO_CHECK_STR_EQ(content, "a longer run");

    /* Replace with something shorter. */
    ROBO_CHECK_EQ_INT(robo_html_text_set(&text, "s", 1u, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_html_text_content(&text, NULL), "s");
    ROBO_CHECK_EQ_INT((int)robo_html_text_length(&text), 1);

    /* Replace with empty: "" and length 0, still a live text node. */
    ROBO_CHECK_EQ_INT(robo_html_text_set(&text, NULL, 0u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_text_is_empty(&text), 1);
    content = robo_html_text_content(&text, &length);
    ROBO_CHECK(content != NULL);
    ROBO_CHECK_EQ_INT((int)length, 0);

    /* Identity, parent, siblings and position are untouched by mutation. */
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&text, &after, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&before.handle, &after.handle));
    ROBO_CHECK(robo_html_node_handle_equal(&after.parent, &div));
    ROBO_CHECK_EQ_INT((int)tx_child_count(&div), 1);
    sibling = tx_child(&div, 0u);
    ROBO_CHECK(robo_html_node_handle_equal(&sibling, &text));

    /* Repeated sets do not accumulate nodes or lose the last value. */
    {
        int i;

        for (i = 0; i < 32; i++) {
            char value[32];

            (void)snprintf(value, sizeof(value), "run-%d", i);
            ROBO_CHECK_EQ_INT(
                robo_html_text_set(&text, value, strlen(value), NULL), ROBO_OK);
        }
        ROBO_CHECK_STR_EQ(robo_html_text_content(&text, NULL), "run-31");
        ROBO_CHECK_EQ_INT((int)tx_child_count(&div), 1);
    }

    /* Argument errors. */
    ROBO_CHECK_EQ_INT(robo_html_text_set(&text, NULL, 4u, NULL),
                      ROBO_INVALID_ARGUMENT);

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

static void test_tx_append(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t div;
    robo_html_node_handle_t text;
    robo_html_node_handle_t after;
    size_t length = 0u;
    const char *content;

    tx_fixture_begin();
    id = tx_doc();
    root = tx_root(id);
    div = tx_create_element(id, &root, "div");
    text = tx_create_text(id, &div, "Hello", 5u, ROBO_HTML_NODE_ORIGIN_TEST,
                          NULL);

    /* In-place growth: the run still ends the arena. */
    ROBO_CHECK_EQ_INT(robo_html_text_append(&text, ", world", 7u, NULL),
                      ROBO_OK);
    content = robo_html_text_content(&text, &length);
    ROBO_CHECK(content != NULL);
    ROBO_CHECK_EQ_INT((int)length, 12);
    ROBO_CHECK_STR_EQ(content, "Hello, world");

    /* A zero-length append is a successful no-op. */
    ROBO_CHECK_EQ_INT(robo_html_text_append(&text, NULL, 0u, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_html_text_content(&text, NULL), "Hello, world");
    ROBO_CHECK_EQ_INT(robo_html_text_append(&text, "ignored", 0u, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_html_text_content(&text, NULL), "Hello, world");

    /* NULL bytes with a non-zero length is an argument error. */
    ROBO_CHECK_EQ_INT(robo_html_text_append(&text, NULL, 3u, NULL),
                      ROBO_INVALID_ARGUMENT);

    /*
     * Slow path: another payload now sits behind the run, so it cannot grow in
     * place and the model has to write the extended run somewhere else. The
     * result must be indistinguishable from the fast path.
     */
    after = tx_create_element(id, &div, "span");
    ROBO_CHECK(robo_html_element_is_element(&after));
    {
        robo_html_node_handle_t span_text =
            tx_create_text(id, &after, "behind", 6u,
                           ROBO_HTML_NODE_ORIGIN_TEST, NULL);

        ROBO_CHECK(robo_html_text_is_text(&span_text));
    }
    ROBO_CHECK_EQ_INT(robo_html_text_append(&text, "!", 1u, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_html_text_content(&text, NULL), "Hello, world!");
    ROBO_CHECK_EQ_INT((int)robo_html_text_length(&text), 13);

    /* The node behind it is untouched. */
    {
        robo_html_node_handle_t span = tx_find(id, "span");
        char buf[32];

        tx_text(&span, buf, sizeof(buf));
        ROBO_CHECK_STR_EQ(buf, "behind");
    }

    /* Appending to an empty node is a plain set-equivalent. */
    {
        robo_html_node_handle_t fresh =
            tx_create_text(id, &div, NULL, 0u, ROBO_HTML_NODE_ORIGIN_TEST,
                           NULL);

        ROBO_CHECK_EQ_INT(robo_html_text_append(&fresh, "born", 4u, NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(robo_html_text_content(&fresh, NULL), "born");
    }

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

static void test_tx_mutation_after_finalization(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t paragraph;
    robo_html_node_handle_t text;
    char buf[64];

    tx_fixture_begin();
    id = tx_doc();
    tx_build(id, "<html><body><p>Hello</p></body></html>", NULL);

    /*
     * 8.4 closed tree *construction*, not the document: character data stays
     * mutable, which is what a later DOM needs.
     */
    paragraph = tx_find(id, "p");
    text = tx_child(&paragraph, 0u);
    ROBO_CHECK(robo_html_text_is_text(&text));
    ROBO_CHECK_EQ_INT(robo_html_text_set(&text, "Goodbye", 7u, NULL), ROBO_OK);
    tx_text(&paragraph, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "Goodbye");

    ROBO_CHECK_EQ_INT(robo_html_text_append(&text, "!", 1u, NULL), ROBO_OK);
    tx_text(&paragraph, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "Goodbye!");

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Parser integration                                                  */
/* ------------------------------------------------------------------ */

static void test_tx_parser_creates_text_nodes(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t paragraph;
    robo_html_node_handle_t bold;
    robo_html_node_handle_t child;
    robo_html_text_view_t view;
    char buf[64];

    tx_fixture_begin();
    id = tx_doc();
    tx_build(id, "<html><body><p>Hello Roboticela</p></body></html>", NULL);

    paragraph = tx_find(id, "p");
    /* Exactly one child, and it is a text node — not an element property. */
    ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 1);
    child = tx_child(&paragraph, 0u);
    ROBO_CHECK(robo_html_text_is_text(&child));
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&child, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(view.state & ROBO_HTML_TEXT_STATE_PARSER_CREATED,
                      ROBO_HTML_TEXT_STATE_PARSER_CREATED);
    ROBO_CHECK_EQ_INT(view.state & ROBO_HTML_TEXT_STATE_CONNECTED,
                      ROBO_HTML_TEXT_STATE_CONNECTED);
    ROBO_CHECK(robo_html_node_handle_equal(&view.parent, &paragraph));
    ROBO_CHECK_STR_EQ(view.content, "Hello Roboticela");
    ROBO_CHECK_EQ_INT((int)view.length, 16);

    /* Text, element, text — three children, in document order. */
    (void)robo_html_document_destroy(id, NULL);

    id = tx_doc();
    tx_build(id, "<html><body><p>Hello <b>Roboticela</b> Runtime</p></body>"
                 "</html>",
             NULL);
    paragraph = tx_find(id, "p");
    ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 3);

    child = tx_child(&paragraph, 0u);
    ROBO_CHECK(robo_html_text_is_text(&child));
    ROBO_CHECK_STR_EQ(robo_html_text_content(&child, NULL), "Hello ");

    bold = tx_child(&paragraph, 1u);
    ROBO_CHECK(robo_html_element_is_element(&bold));
    ROBO_CHECK(robo_html_element_is_named(&bold, "b"));
    ROBO_CHECK_EQ_INT((int)tx_child_count(&bold), 1);
    {
        robo_html_node_handle_t bold_text = tx_child(&bold, 0u);

        ROBO_CHECK(robo_html_text_is_text(&bold_text));
        ROBO_CHECK_STR_EQ(robo_html_text_content(&bold_text, NULL),
                          "Roboticela");
    }

    child = tx_child(&paragraph, 2u);
    ROBO_CHECK(robo_html_text_is_text(&child));
    ROBO_CHECK_STR_EQ(robo_html_text_content(&child, NULL), " Runtime");

    /* The concatenation is the paragraph's text, in order. */
    tx_text(&paragraph, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "Hello Roboticela Runtime");

    /* <div>one<span>two</span>three</div> — same shape, different names. */
    (void)robo_html_document_destroy(id, NULL);

    id = tx_doc();
    tx_build(id, "<html><body><div>one<span>two</span>three</div></body>"
                 "</html>",
             NULL);
    {
        robo_html_node_handle_t div = tx_find(id, "div");
        char first[16];
        char second[16];

        ROBO_CHECK_EQ_INT((int)tx_child_count(&div), 3);
        tx_child_text(&div, 0u, first, sizeof(first));
        ROBO_CHECK_STR_EQ(first, "one");
        tx_child_text(&div, 2u, second, sizeof(second));
        ROBO_CHECK_STR_EQ(second, "three");
        tx_text(&div, buf, sizeof(buf));
        ROBO_CHECK_STR_EQ(buf, "onetwothree");
    }

    /* <div>Hello <strong>Roboticela</strong> Runtime</div> */
    (void)robo_html_document_destroy(id, NULL);

    id = tx_doc();
    tx_build(id, "<html><body><div>Hello <strong>Roboticela</strong> Runtime"
                 "</div></body></html>",
             NULL);
    {
        robo_html_node_handle_t div = tx_find(id, "div");
        robo_html_node_handle_t strong;

        ROBO_CHECK_EQ_INT((int)tx_child_count(&div), 3);
        strong = tx_child(&div, 1u);
        ROBO_CHECK(robo_html_element_is_named(&strong, "strong"));
        tx_text(&div, buf, sizeof(buf));
        ROBO_CHECK_STR_EQ(buf, "Hello Roboticela Runtime");
    }

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

static void test_tx_runs_and_merging(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t paragraph;
    robo_html_node_handle_t div;
    robo_html_tree_result_t result;
    char buf[64];

    tx_fixture_begin();

    /*
     * A run arrives as many character tokens (buffer boundaries, character
     * references, split-up text) and becomes exactly ONE node. The tokenizer's
     * segmentation is not observable in the tree, which is the point.
     */
    id = tx_doc();
    tx_build(id, "<html><body><p>alpha &amp; beta &#65; gamma</p></body>"
                 "</html>",
             &result);
    paragraph = tx_find(id, "p");
    ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 1);
    ROBO_CHECK_EQ_INT((int)tx_text_child_count(&paragraph), 1);
    tx_text(&paragraph, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "alpha & beta A gamma");
    (void)robo_html_document_destroy(id, NULL);

    /* An element breaks the run: text either side stays separate. */
    id = tx_doc();
    tx_build(id, "<html><body><p>before<em>mid</em>after</p></body></html>",
             NULL);
    paragraph = tx_find(id, "p");
    ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 3);
    ROBO_CHECK_EQ_INT((int)tx_text_child_count(&paragraph), 2);
    tx_text(&paragraph, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "beforemidafter");
    (void)robo_html_document_destroy(id, NULL);

    /* A comment breaks the run as well. */
    id = tx_doc();
    tx_build(id, "<html><body><p>one<!--c-->two</p></body></html>", NULL);
    paragraph = tx_find(id, "p");
    ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 3);
    ROBO_CHECK_EQ_INT((int)tx_text_child_count(&paragraph), 2);
    tx_text(&paragraph, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "onetwo");
    (void)robo_html_document_destroy(id, NULL);

    /*
     * Decoded character references join the run they sit in rather than
     * splitting it: the div is Text("A \xA0 B \xA9 C ") + <i>, and only the
     * element breaks the run.
     */
    id = tx_doc();
    tx_build(id, "<html><body><div>A &nbsp; B &copy; C <i>d</i>"
                 "</div></body></html>",
             NULL);
    div = tx_find(id, "div");
    ROBO_CHECK_EQ_INT((int)tx_child_count(&div), 2);
    ROBO_CHECK_EQ_INT((int)tx_text_child_count(&div), 1);
    tx_text(&div, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "A \xC2\xA0 B \xC2\xA9 C d");

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

static void test_tx_whitespace_preserved(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t div;
    robo_html_node_handle_t text;
    robo_html_text_view_t view;
    char buf[128];

    tx_fixture_begin();

    /* Runs of spaces are not collapsed at this layer. */
    id = tx_doc();
    tx_build(id, "<html><body><div>Hello    world</div></body></html>", NULL);
    div = tx_find(id, "div");
    ROBO_CHECK_EQ_INT((int)tx_child_count(&div), 1);
    text = tx_child(&div, 0u);
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&text, &view, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(view.content, "Hello    world");
    ROBO_CHECK_EQ_INT((int)view.length, 14);

    /* Newlines and tabs inside a run survive byte-for-byte. */
    (void)robo_html_document_destroy(id, NULL);
    id = tx_doc();
    tx_build(id, "<html><body><div>a\tb\nc</div></body></html>", NULL);
    div = tx_find(id, "div");
    text = tx_child(&div, 0u);
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&text, &view, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(view.content, "a\tb\nc");
    ROBO_CHECK_EQ_INT((int)view.length, 5);

    /* Leading and trailing whitespace is not trimmed; nothing to trim with. */
    (void)robo_html_document_destroy(id, NULL);
    id = tx_doc();
    tx_build(id, "<html><body><div>  padded  </div></body></html>", NULL);
    div = tx_find(id, "div");
    text = tx_child(&div, 0u);
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&text, &view, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(view.content, "  padded  ");

    /*
     * The whitespace between tags is real text too: <body> holds the block
     * plus the newline runs around it.
     */
    (void)robo_html_document_destroy(id, NULL);
    id = tx_doc();
    tx_build(id, "<html>\n<body>\n  <h1>Title</h1>\n  <p>Body</p>\n</body>\n"
                 "</html>\n",
             NULL);
    {
        robo_html_node_handle_t body = tx_find(id, "body");
        size_t children = tx_child_count(&body);
        size_t i;
        size_t blanks = 0u;

        /* The two tagged runs around the blocks, plus the run after </body>,
         * which HTML re-inserts into the body: four whitespace nodes. */
        ROBO_CHECK_EQ_INT((int)children, 6);
        for (i = 0u; i < children; i++) {
            robo_html_node_handle_t child = tx_child(&body, i);

            if (robo_html_text_is_text(&child)) {
                blanks++;
            }
        }
        ROBO_CHECK_EQ_INT((int)blanks, 4);
        /* Document order concatenation keeps the layout newlines. */
        tx_text(&body, buf, sizeof(buf));
        ROBO_CHECK(strstr(buf, "\n  ") != NULL);
    }

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

static void test_tx_char_references(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t paragraph;
    robo_html_node_handle_t text;
    size_t length = 0u;
    const char *content;

    tx_fixture_begin();
    id = tx_doc();
    tx_build(id, "<html><body><p>&amp;&lt;&gt;&#65;&#x42;</p></body>"
                 "</html>",
             NULL);
    paragraph = tx_find(id, "p");
    ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 1);
    text = tx_child(&paragraph, 0u);
    ROBO_CHECK(robo_html_text_is_text(&text));

    /* Decoding is the tokenizer's job; the node holds the decoded bytes. */
    content = robo_html_text_content(&text, &length);
    ROBO_CHECK(content != NULL);
    ROBO_CHECK_EQ_INT((int)length, 5);
    ROBO_CHECK_STR_EQ(content, "&<>AB");

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

static void test_tx_unicode_preserved(void)
{
    static const char *const k_cases[] = {
        "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7",             /* Arabic  */
        "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C",             /* Persian */
        "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88",                     /* Urdu    */
        "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0"
        "\xA4\xA6\xE0\xA5\x80",                                 /* Hindi   */
        "\xE0\xA6\xAC\xE0\xA6\xBE\xE0\xA6\x82\xE0\xA6\xB2\xE0"
        "\xA6\xBE",                                             /* Bengali */
        "\xE4\xB8\xAD\xE6\x96\x87",                             /* Chinese */
        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",                 /* Japanese*/
        "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4",                 /* Korean  */
        "\xF0\x9F\x98\x80 \xF0\x9F\x9A\x80 \xE2\x9D\xA4\xEF"
        "\xB8\x8F"                                              /* emoji   */
    };
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    size_t i;

    tx_fixture_begin();

    for (i = 0u; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        robo_html_node_handle_t paragraph;
        robo_html_node_handle_t text;
        robo_html_text_view_t view;
        char source[256];

        (void)snprintf(source, sizeof(source),
                       "<html><body><p>%s</p></body></html>", k_cases[i]);
        id = tx_doc();
        tx_build(id, source, NULL);
        paragraph = tx_find(id, "p");

        ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 1);
        text = tx_child(&paragraph, 0u);
        ROBO_CHECK(robo_html_text_is_text(&text));
        ROBO_CHECK_EQ_INT(robo_html_text_view_get(&text, &view, NULL), ROBO_OK);
        /* Byte-exact round trip; no normalization, no re-encoding. */
        ROBO_CHECK_STR_EQ(view.content, k_cases[i]);
        ROBO_CHECK_EQ_INT((int)view.length, (int)strlen(k_cases[i]));

        (void)robo_html_document_destroy(id, NULL);
    }

    /* Mixed scripts plus a character reference in one run. */
    id = tx_doc();
    tx_build(id, "<html><body><p>ASCII \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8"
                 "\xD8\xA7 \xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80"
                 " &mdash; end</p></body></html>",
             NULL);
    {
        robo_html_node_handle_t paragraph = tx_find(id, "p");
        robo_html_node_handle_t text = tx_child(&paragraph, 0u);
        robo_html_text_view_t view;

        ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 1);
        ROBO_CHECK_EQ_INT(robo_html_text_view_get(&text, &view, NULL), ROBO_OK);
        ROBO_CHECK(strstr(view.content, "ASCII ") == view.content);
        ROBO_CHECK(strstr(view.content, "\xE4\xB8\xAD\xE6\x96\x87") != NULL);
        ROBO_CHECK(strstr(view.content, "\xF0\x9F\x98\x80") != NULL);
        ROBO_CHECK(strstr(view.content, "end") != NULL);
    }

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

static void test_tx_long_text(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t paragraph;
    robo_html_node_handle_t text;
    robo_html_text_view_t view;
    static char run[6001];
    char source[8192];
    size_t i;

    tx_fixture_begin();

    for (i = 0u; i < 6000u; i++) {
        run[i] = (char)('a' + (int)(i % 26));
    }
    run[6000] = '\0';

    (void)snprintf(source, sizeof(source),
                   "<html><body><p>%s</p></body></html>", run);
    id = tx_doc();
    tx_build(id, source, NULL);
    paragraph = tx_find(id, "p");
    ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 1);
    text = tx_child(&paragraph, 0u);
    ROBO_CHECK(robo_html_text_is_text(&text));
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&text, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)view.length, 6000);
    /* One node, not one per token, and byte-exact. */
    ROBO_CHECK(memcmp(view.content, run, 6000u) == 0);

    /* Extending a long run keeps it one node. */
    ROBO_CHECK_EQ_INT(robo_html_text_append(&text, "TAIL", 4u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_html_text_length(&text), 6004);
    ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 1);
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&text, &view, NULL), ROBO_OK);
    ROBO_CHECK(memcmp(view.content + 6000, "TAIL", 4u) == 0);

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Traversal                                                           */
/* ------------------------------------------------------------------ */

static void test_tx_traversal(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t body;
    robo_html_node_handle_t first;
    robo_html_node_handle_t last;
    robo_html_node_handle_t middle;
    robo_html_node_handle_t view_child;
    robo_html_node_view_t view;
    size_t children;
    size_t i;
    size_t walked = 0u;

    tx_fixture_begin();

    /*
     * Text and elements interleave as real siblings, so the traversal is
     * exercised on a mixed child list rather than a uniform one.
     */
    id = tx_doc();
    tx_build(id, "<html><body>alpha<span>mid</span>omega</body></html>",
             NULL);
    body = tx_find(id, "body");
    children = tx_child_count(&body);
    ROBO_CHECK_EQ_INT((int)children, 3);

    /* Child iteration reaches every child in document order, by kind. */
    for (i = 0u; i < children; i++) {
        robo_html_node_handle_t child = tx_child(&body, i);

        if (i == 1u) {
            ROBO_CHECK(robo_html_element_is_named(&child, "span"));
        } else {
            ROBO_CHECK(robo_html_text_is_text(&child));
        }
        walked++;
    }
    ROBO_CHECK_EQ_INT((int)walked, 3);
    ROBO_CHECK_EQ_INT((int)tx_text_child_count(&body), 2);

    first = tx_child(&body, 0u);
    middle = tx_child(&body, 1u);
    last = tx_child(&body, 2u);

    /* first_child / last_child through the node view. */
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&body, &view, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&view.first_child, &first));
    ROBO_CHECK(robo_html_node_handle_equal(&view.last_child, &last));
    ROBO_CHECK_EQ_INT((int)view.child_count, 3);

    /* Sibling links are the ones the tree already maintains, text included. */
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&first, &view, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&view.next_sibling, &middle));
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&view.prev_sibling));
    ROBO_CHECK(robo_html_node_handle_equal(&view.parent, &body));

    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&middle, &view, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&view.prev_sibling, &first));
    ROBO_CHECK(robo_html_node_handle_equal(&view.next_sibling, &last));

    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&last, &view, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&view.prev_sibling, &middle));
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&view.next_sibling));

    /* The text model reports the same parent the tree does. */
    {
        robo_html_node_handle_t parent = {0u, 0u, 0u};

        ROBO_CHECK_EQ_INT(robo_html_text_parent(&first, &parent, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&parent, &body));
    }

    /* A text node is a leaf: no children, and index 0 is out of range. */
    ROBO_CHECK_EQ_INT((int)tx_child_count(&first), 0);
    view_child = tx_child(&first, 0u);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&view_child));

    /* Recursive walk over the whole tree sees text and elements. */
    {
        size_t texts = 0u;
        size_t elements = 0u;
        robo_html_node_handle_t stack[64];
        size_t depth = 0u;

        stack[depth++] = tx_root(id);
        while (depth > 0u) {
            robo_html_node_handle_t node = stack[--depth];
            size_t count = tx_child_count(&node);
            size_t k;

            if (robo_html_text_is_text(&node)) {
                texts++;
            } else if (robo_html_element_is_element(&node)) {
                elements++;
            }
            for (k = 0u; k < count && depth < 64u; k++) {
                stack[depth++] = tx_child(&node, k);
            }
        }
        ROBO_CHECK_EQ_INT((int)texts, 3);    /* alpha, mid, omega */
        ROBO_CHECK_EQ_INT((int)elements, 4); /* html head body span */
    }

    (void)robo_html_document_destroy(id, NULL);

    /* Text under text-level elements is reachable one level down. */
    id = tx_doc();
    tx_build(id, "<html><body><p>one</p><p>two</p><p>three</p></body>"
                 "</html>",
             NULL);
    body = tx_find(id, "body");
    ROBO_CHECK_EQ_INT((int)tx_child_count(&body), 3);
    ROBO_CHECK_EQ_INT((int)tx_text_child_count(&body), 0);
    {
        static const char *const k_words[] = {"one", "two", "three"};

        for (i = 0u; i < 3u; i++) {
            robo_html_node_handle_t paragraph = tx_child(&body, i);
            robo_html_node_handle_t text;
            char buf[16];

            ROBO_CHECK(robo_html_element_is_named(&paragraph, "p"));
            ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 1);
            text = tx_child(&paragraph, 0u);
            ROBO_CHECK(robo_html_text_is_text(&text));
            ROBO_CHECK_STR_EQ(robo_html_text_content(&text, NULL),
                              k_words[i]);
            tx_text(&paragraph, buf, sizeof(buf));
            ROBO_CHECK_STR_EQ(buf, k_words[i]);
        }
    }

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Ownership and teardown                                              */
/* ------------------------------------------------------------------ */

static void test_tx_ownership_and_stale_handles(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t saved;

    tx_fixture_begin();
    id = tx_doc();
    tx_build(id, "<html><body><p>Hello</p></body></html>", NULL);
    {
        robo_html_node_handle_t before = tx_find(id, "p");

        saved = tx_child(&before, 0u);
    }
    ROBO_CHECK(robo_html_text_is_text(&saved));

    /*
     * Rebuilding the document's tree retires the old nodes: the generation
     * guard is what makes the saved handle resolve as stale instead of
     * aliasing whatever now occupies that id.
     */
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_stale(&saved));
    ROBO_CHECK(!robo_html_text_is_text(&saved));
    ROBO_CHECK(robo_html_text_content(&saved, NULL) == NULL);
    ROBO_CHECK_EQ_INT(robo_html_text_set(&saved, "x", 1u, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_text_append(&saved, "x", 1u, NULL),
                      ROBO_NOT_FOUND);

    /* The rebuilt tree has its own text nodes, unaffected by the old handle. */
    tx_build(id, "<html><body><p>Second</p></body></html>", NULL);
    {
        robo_html_node_handle_t second = tx_find(id, "p");
        robo_html_node_handle_t fresh = tx_child(&second, 0u);
        char buf[32];

        ROBO_CHECK(robo_html_text_is_text(&fresh));
        ROBO_CHECK(!robo_html_node_handle_equal(&fresh, &saved));
        tx_text(&second, buf, sizeof(buf));
        ROBO_CHECK_STR_EQ(buf, "Second");
    }

    /* Destroying the document retires every handle it issued. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_text_is_text(&saved));
    ROBO_CHECK(robo_html_text_content(&saved, NULL) == NULL);
    ROBO_CHECK_EQ_INT((int)robo_html_text_length(&saved), 0);
    ROBO_CHECK_EQ_INT(robo_html_text_is_empty(&saved), 0);
    ROBO_CHECK_EQ_INT(robo_html_text_view_get(&saved, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    tx_fixture_end();
}

static void test_tx_teardown(void)
{
    robo_html_document_id_t id;
    robo_html_tree_report_t report;

    tx_fixture_begin();
    id = tx_doc();
    tx_build(id, "<!DOCTYPE html><html><head><title>T</title></head>"
                 "<body><h1>H</h1><p>one</p><p>two</p>"
                 "<div>a<span>b</span>c</div></body></html>",
             NULL);

    /* The tree is internally consistent with text nodes in it. */
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK(report.ok);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                      ROBO_NOT_FOUND);

    tx_fixture_end();
}

static void test_tx_large_tree_teardown(void)
{
    robo_html_document_id_t id;
    static char source[16000];
    size_t used = 0u;
    int i;

    tx_fixture_begin();
    id = tx_doc();

    used += (size_t)snprintf(source + used, sizeof(source) - used,
                             "<html><body>");
    for (i = 0; i < 80 && used < sizeof(source) - 64u; i++) {
        used += (size_t)snprintf(source + used, sizeof(source) - used,
                                 "<p>paragraph %d</p>", i);
    }
    (void)snprintf(source + used, sizeof(source) - used, "</body></html>");

    tx_build(id, source, NULL);
    {
        robo_html_node_handle_t body = tx_find(id, "body");
        size_t texts = 0u;
        size_t i;

        ROBO_CHECK_EQ_INT((int)tx_child_count(&body), 80);
        /* Each <p> holds exactly one text run. */
        for (i = 0u; i < 80u; i++) {
            robo_html_node_handle_t paragraph = tx_child(&body, i);
            robo_html_node_handle_t text;

            ROBO_CHECK(robo_html_element_is_named(&paragraph, "p"));
            ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 1);
            text = tx_child(&paragraph, 0u);
            if (robo_html_text_is_text(&text)) {
                texts++;
            }
        }
        ROBO_CHECK_EQ_INT((int)texts, 80);
    }

    /* One teardown releases the registry and the payload arena together. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    tx_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Malformed input                                                     */
/* ------------------------------------------------------------------ */

static void test_tx_malformed_input(void)
{
    static const char *const k_cases[] = {
        "",
        "Hello",
        "   \n\t  ",
        "<p>unclosed",
        "<p>a<div>b</div>",
        "</p>stray close",
        "<html><body><p>&amp;</p>",
        "<html><body><div>   </div></body></html>",
        "<html><body><p></p></body></html>",
        "<p><b>a<i>b</b>c</i></p>",
        "<html><body><p>\xF0\x9F\x98\x80</p></body></html>",
        "<!DOCTYPE html><html><body><br>text</body></html>"
    };
    size_t i;

    tx_fixture_begin();

    /*
     * Every one of these must build without crashing, produce a tree the
     * document validator accepts, and keep whatever text it did have. Full
     * HTML5 error recovery is not this phase's scope.
     */
    for (i = 0u; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        robo_html_document_id_t id = tx_doc();
        robo_html_tree_report_t report;

        tx_build(id, k_cases[i], NULL);
        ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                          ROBO_OK);
        ROBO_CHECK(report.ok);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    /* Text-only input lands in body as a real text node. */
    {
        robo_html_document_id_t id = tx_doc();
        char buf[64];

        tx_build(id, "just text", NULL);
        tx_text_of(id, "body", buf, sizeof(buf));
        ROBO_CHECK_STR_EQ(buf, "just text");
        (void)robo_html_document_destroy(id, NULL);
    }

    /* Whitespace-only input keeps its whitespace and creates no empty node. */
    {
        robo_html_document_id_t id = tx_doc();
        robo_html_node_handle_t body;
        char buf[32];

        tx_build(id, "<html><body>   </body></html>", NULL);
        body = tx_find(id, "body");
        ROBO_CHECK_EQ_INT((int)tx_child_count(&body), 1);
        ROBO_CHECK_EQ_INT((int)tx_text_child_count(&body), 1);
        tx_text(&body, buf, sizeof(buf));
        ROBO_CHECK_STR_EQ(buf, "   ");
        (void)robo_html_document_destroy(id, NULL);
    }

    /* An empty element creates no text child rather than an empty one. */
    {
        robo_html_document_id_t id = tx_doc();
        robo_html_node_handle_t paragraph;

        tx_build(id, "<html><body><p></p></body></html>", NULL);
        paragraph = tx_find(id, "p");
        ROBO_CHECK(robo_html_element_is_element(&paragraph));
        ROBO_CHECK_EQ_INT((int)tx_child_count(&paragraph), 0);
        (void)robo_html_document_destroy(id, NULL);
    }

    tx_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Tree dump                                                           */
/* ------------------------------------------------------------------ */

static robo_result_t tx_collect_sink(const char *text, size_t length,
                                     void *user)
{
    char *buffer = (char *)user;
    size_t used = strlen(buffer);

    memcpy(buffer + used, text, length);
    buffer[used + length] = '\0';
    return ROBO_OK;
}

static void test_tx_tree_dump(void)
{
    robo_html_document_id_t id;
    static char buffer[8192];

    tx_fixture_begin();
    id = tx_doc();
    tx_build(id, "<!DOCTYPE html><html><body><h1>Heading</h1>"
                 "<p>line one\nline two\tend</p></body></html>",
             NULL);

    buffer[0] = '\0';
    ROBO_CHECK_EQ_INT(robo_html_tree_dump(id, tx_collect_sink, buffer, NULL),
                      ROBO_OK);

    /*
     * A text node is labelled and its whitespace is escaped, which is what
     * makes a run legible in a dump (`Text: "line one\nline two\tend"`).
     */
    ROBO_CHECK(strstr(buffer, "Text: \"Heading\"") != NULL);
    ROBO_CHECK(strstr(buffer, "Text: \"line one\\nline two\\tend\"") !=
               NULL);

    (void)robo_html_document_destroy(id, NULL);

    /* An empty text node prints its empty run rather than being elided. */
    id = tx_doc();
    {
        robo_html_node_handle_t root = tx_root(id);
        robo_html_node_handle_t div = tx_create_element(id, &root, "div");
        robo_html_node_handle_t empty =
            tx_create_text(id, &div, NULL, 0u, ROBO_HTML_NODE_ORIGIN_TEST,
                           NULL);

        ROBO_CHECK(robo_html_text_is_text(&empty));
        buffer[0] = '\0';
        ROBO_CHECK_EQ_INT(
            robo_html_tree_dump(id, tx_collect_sink, buffer, NULL), ROBO_OK);
        ROBO_CHECK(strstr(buffer, "Text: \"\"") != NULL);
    }

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Capability + visual demo document                                   */
/* ------------------------------------------------------------------ */

static void test_tx_capability_flag(void)
{
    robo_html_dom_capabilities_t caps;

    robo_html_dom_capabilities_init(&caps);
    ROBO_CHECK_EQ_INT(caps.text_nodes, 0);
    ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(&caps), ROBO_OK);
    /* The text model is available; the node DOM and events still are not. */
    ROBO_CHECK_EQ_INT(caps.text_nodes, 1);
    ROBO_CHECK_EQ_INT(caps.tree_building, 1);
    ROBO_CHECK_EQ_INT(caps.dom_nodes, 0);
    ROBO_CHECK_EQ_INT(caps.events, 0);
}

static void test_tx_demo_document(void)
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
    robo_html_node_handle_t body;
    robo_html_node_handle_t head;
    robo_html_node_handle_t title;
    char text[128];

    tx_fixture_begin();
    id = tx_doc();
    tx_build(id, k_demo, &result);

    /*
     * The demo document's text is carried by real text nodes: 6 whitespace
     * runs around the 4 body blocks, each its own node.
     */
    body = tx_find(id, "body");
    ROBO_CHECK_EQ_INT((int)tx_child_count(&body), 10);
    ROBO_CHECK_EQ_INT((int)tx_text_child_count(&body), 6);

    /* Each block's visible text comes from a text node child, not a field. */
    {
        static const char *const k_names[] = {"h1", "p", "p", "button"};
        size_t i;

        for (i = 0u; i < 4u; i++) {
            robo_html_node_handle_t element = tx_find(id, k_names[i]);

            ROBO_CHECK(robo_html_element_is_element(&element));
            ROBO_CHECK_EQ_INT((int)tx_child_count(&element), 1);
            if (i == 0u) {
                robo_html_node_handle_t child = tx_child(&element, 0u);

                ROBO_CHECK(robo_html_text_is_text(&child));
                ROBO_CHECK_STR_EQ(robo_html_text_content(&child, NULL),
                                  "RoboticelaOS Runtime");
            }
        }
    }

    tx_text_of(id, "h1", text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "RoboticelaOS Runtime");
    tx_text_of(id, "button", text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "Demo Button");

    head = tx_find(id, "head");
    title = tx_find(id, "title");
    ROBO_CHECK(robo_html_element_is_element(&head));
    ROBO_CHECK(robo_html_element_is_element(&title));
    ROBO_CHECK_EQ_INT((int)tx_child_count(&title), 1);
    tx_text_of(id, "title", text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "RoboticelaOS Runtime");

    /* The body's whitespace runs are what separate the blocks. */
    {
        long run = tx_text_child_index(&body, 0u);
        char whitespace[16];

        ROBO_CHECK(run >= 0);
        tx_child_text(&body, (size_t)run, whitespace, sizeof(whitespace));
        ROBO_CHECK_STR_EQ(whitespace, "\n");
    }

    ROBO_CHECK_EQ_INT((int)result.element_count, 8);
    ROBO_CHECK_EQ_INT((int)result.truncated, 0);

    (void)robo_html_document_destroy(id, NULL);
    tx_fixture_end();
}

/* ------------------------------------------------------------------ */

void robo_test_register_html_text(void)
{
    robo_test_register("html.text.create_info_init", test_tx_create_info_init);
    robo_test_register("html.text.create_basic", test_tx_create_basic);
    robo_test_register("html.text.create_empty", test_tx_create_empty);
    robo_test_register("html.text.create_validation", test_tx_create_validation);
    robo_test_register("html.text.kind_identification",
                       test_tx_kind_identification);
    robo_test_register("html.text.not_text_answers", test_tx_not_text_answers);
    robo_test_register("html.text.set", test_tx_set);
    robo_test_register("html.text.append", test_tx_append);
    robo_test_register("html.text.mutation_after_finalization",
                       test_tx_mutation_after_finalization);
    robo_test_register("html.text.parser_integration",
                       test_tx_parser_creates_text_nodes);
    robo_test_register("html.text.runs_and_merging", test_tx_runs_and_merging);
    robo_test_register("html.text.whitespace", test_tx_whitespace_preserved);
    robo_test_register("html.text.char_references", test_tx_char_references);
    robo_test_register("html.text.unicode", test_tx_unicode_preserved);
    robo_test_register("html.text.long_text", test_tx_long_text);
    robo_test_register("html.text.traversal", test_tx_traversal);
    robo_test_register("html.text.ownership", test_tx_ownership_and_stale_handles);
    robo_test_register("html.text.teardown", test_tx_teardown);
    robo_test_register("html.text.large_tree_teardown",
                       test_tx_large_tree_teardown);
    robo_test_register("html.text.malformed", test_tx_malformed_input);
    robo_test_register("html.text.tree_dump", test_tx_tree_dump);
    robo_test_register("html.text.capability_flag", test_tx_capability_flag);
    robo_test_register("html.text.demo_document", test_tx_demo_document);
}
