/*
 * RoboHTML Phase 8.11 element-tag suite (html.tag.*).
 *
 * The tag vocabulary is the one place that answers "what is this element?".
 * So the tests here are mostly about *integrity of that answer* rather than
 * about any single element: that the table and the enum agree, that every
 * name round-trips, that the role column is what the element model reports,
 * that the flags and the named predicates never disagree, and that an
 * unrecognised element is a supported state rather than a hole.
 *
 * The identity tests are deliberately end-to-end — parse real HTML, then ask
 * a handle what it is — because the point of the layer is that the answer a
 * *consumer* gets is the same one the table defines. A table that is correct
 * in isolation and is not consulted is worth nothing.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static void tg_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void tg_fixture_end(void)
{
    robo_html_shutdown();
}

static robo_html_document_id_t tg_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    return id;
}

static void tg_build(robo_html_document_id_t id, const char *text)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);
}

static robo_html_node_handle_t tg_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

/* ------------------------------------------------------------------ */
/* Predicates                                                          */
/* ------------------------------------------------------------------ */

typedef struct tg_tag_query {
    robo_html_tag_t tag;
} tg_tag_query_t;

/**
 * Visitor: every visited element's stored name and its identity must agree,
 * and every identity must be a defined value. Counts what it checked.
 */
static robo_result_t tg_visit_identity(const robo_html_node_view_t *view,
                                       size_t depth,
                                       void *user)
{
    size_t *visited = (size_t *)user;
    robo_html_tag_t tag = robo_html_element_tag(&view->handle);

    (void)depth;
    ROBO_CHECK(robo_html_tag_is_known(tag));
    ROBO_CHECK_EQ_INT((int)tag,
                      (int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   view->name));
    ROBO_CHECK(robo_html_element_is_tag(&view->handle, tag));
    (*visited)++;
    return ROBO_OK;
}

static int tg_match_tag(const robo_html_node_view_t *view, void *user)
{
    const tg_tag_query_t *q = (const tg_tag_query_t *)user;

    return robo_html_element_is_tag(&view->handle, q->tag);
}

/** First element of @p tag at or below @p start; asserts it was found. */
static robo_html_node_handle_t tg_find(const robo_html_node_handle_t *start,
                                       robo_html_tag_t tag)
{
    tg_tag_query_t q;
    robo_html_node_handle_t out;

    q.tag = tag;
    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(
        robo_html_node_find_first(start, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  tg_match_tag, &q, &out, NULL),
        ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

/** Whole subtree text of @p node into @p out (NUL-terminated). */
static void tg_text(const robo_html_node_handle_t *node, char *out, size_t cap)
{
    size_t length = 0u;

    out[0] = '\0';
    if (robo_html_node_subtree_text(node, out, cap, &length, NULL) != ROBO_OK) {
        return;
    }
    if (length >= cap) {
        out[cap - 1u] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* The table is internally consistent                                  */
/* ------------------------------------------------------------------ */

/*
 * Every other assertion in this phase is built on the table being what it
 * claims: dense ids, rows in enum order, names that round-trip, roles that
 * are defined values. If this test is green, a failure elsewhere is about
 * behaviour; if it is red, nothing else here means anything.
 */
static void test_tg_table_is_dense(void)
{
    size_t count = robo_html_tag_count();
    size_t i;
    robo_html_tag_t tag;

    tg_fixture_begin();

    ROBO_CHECK_EQ_INT((int)count, (int)(ROBO_HTML_TAG_COUNT - 1));

    for (i = 0u; i < count; i++) {
        ROBO_CHECK_EQ_INT(robo_html_tag_at(i, &tag), ROBO_OK);
        /* Row order is enum order: the row at index i defines tag i + 1. */
        ROBO_CHECK_EQ_INT((int)tag, (int)(i + 1u));
        ROBO_CHECK(robo_html_tag_is_known(tag));
        ROBO_CHECK(robo_html_tag_name(tag)[0] != '\0');
        ROBO_CHECK(robo_html_element_class_is_known(robo_html_tag_class(tag)));
        /* Name lookup returns the very tag the row defines. */
        ROBO_CHECK_EQ_INT(
            (int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                         robo_html_tag_name(tag)),
            (int)tag);
    }

    /* No name is used twice, and every name is lowercase. */
    for (i = 0u; i < count; i++) {
        robo_html_tag_t a;
        size_t j;

        ROBO_CHECK_EQ_INT(robo_html_tag_at(i, &a), ROBO_OK);
        for (j = 0u; j < count; j++) {
            robo_html_tag_t b;
            const char *name;

            ROBO_CHECK_EQ_INT(robo_html_tag_at(j, &b), ROBO_OK);
            name = robo_html_tag_name(b);
            if (i != j) {
                ROBO_CHECK(strcmp(robo_html_tag_name(a), name) != 0);
            }
        }
        {
            const char *name = robo_html_tag_name(a);
            size_t k;

            for (k = 0u; name[k] != '\0'; k++) {
                ROBO_CHECK(!(name[k] >= 'A' && name[k] <= 'Z'));
            }
        }
    }

    tg_fixture_end();
}

static void test_tg_unknown_and_undefined(void)
{
    tg_fixture_begin();

    /* UNKNOWN is a defined answer with no name, role or flags. */
    ROBO_CHECK(!robo_html_tag_is_known(ROBO_HTML_TAG_UNKNOWN));
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_UNKNOWN), "");
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_UNKNOWN),
                      (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_flags(ROBO_HTML_TAG_UNKNOWN), 0);
    ROBO_CHECK(!robo_html_tag_is_void(ROBO_HTML_TAG_UNKNOWN));
    ROBO_CHECK(!robo_html_tag_is_heading(ROBO_HTML_TAG_UNKNOWN));
    ROBO_CHECK(!robo_html_tag_is_root(ROBO_HTML_TAG_UNKNOWN));

    /* An undefined enumerator behaves exactly as UNKNOWN, never as a tag. */
    ROBO_CHECK(!robo_html_tag_is_known((robo_html_tag_t)(ROBO_HTML_TAG_COUNT)));
    ROBO_CHECK(!robo_html_tag_is_known((robo_html_tag_t)9999));
    ROBO_CHECK(!robo_html_tag_is_known((robo_html_tag_t)-1));
    ROBO_CHECK_STR_EQ(robo_html_tag_name((robo_html_tag_t)9999), "");
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class((robo_html_tag_t)9999),
                      (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);

    /* Out-of-range table access is NOT_FOUND, and clears the output. */
    {
        robo_html_tag_t out = ROBO_HTML_TAG_DIV;

        ROBO_CHECK_EQ_INT(robo_html_tag_at(robo_html_tag_count(), &out),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT((int)out, (int)ROBO_HTML_TAG_UNKNOWN);
    }
    ROBO_CHECK_EQ_INT(robo_html_tag_at(0u, NULL), ROBO_INVALID_ARGUMENT);

    tg_fixture_end();
}

static void test_tg_lookup_by_name(void)
{
    tg_fixture_begin();

    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   "div"),
                      (int)ROBO_HTML_TAG_DIV);
    /* Case-insensitive in both directions: the element model owns the rule. */
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   "DIV"),
                      (int)ROBO_HTML_TAG_DIV);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   "DiV"),
                      (int)ROBO_HTML_TAG_DIV);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   "Strong"),
                      (int)ROBO_HTML_TAG_STRONG);

    /* Unrecognised names are UNKNOWN, never an error. */
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   "my-widget"),
                      (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   "divx"),
                      (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   ""),
                      (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   NULL),
                      (int)ROBO_HTML_TAG_UNKNOWN);

    /*
     * Identity is (namespace, name): the same spelling in another namespace
     * is a different element, and answering otherwise would be a lie.
     */
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_SVG,
                                                   "a"),
                      (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_MATHML,
                                                   "div"),
                      (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_NONE,
                                                   "div"),
                      (int)ROBO_HTML_TAG_UNKNOWN);

    tg_fixture_end();
}

static void test_tg_name_and_role(void)
{
    tg_fixture_begin();

    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_HTML), "html");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_HEAD), "head");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_BODY), "body");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_TITLE), "title");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_DIV), "div");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_SPAN), "span");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_P), "p");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_BR), "br");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_H1), "h1");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_H6), "h6");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_UL), "ul");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_OL), "ol");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_LI), "li");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_STRONG), "strong");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_EM), "em");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_CODE), "code");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_PRE), "pre");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_SECTION), "section");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_ARTICLE), "article");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_NAV), "nav");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_MAIN), "main");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_HEADER), "header");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_FOOTER), "footer");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_ASIDE), "aside");
    /* Names that need the table to be right about their spelling. */
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_BLOCKQUOTE),
                      "blockquote");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_FIGCAPTION),
                      "figcaption");
    ROBO_CHECK_STR_EQ(robo_html_tag_name(ROBO_HTML_TAG_BASEFONT), "basefont");

    /* Roles, including the two that look alike but are not. */
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_HTML),
                      (int)ROBO_HTML_ELEMENT_CLASS_ROOT);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_HEAD),
                      (int)ROBO_HTML_ELEMENT_CLASS_METADATA);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_TITLE),
                      (int)ROBO_HTML_ELEMENT_CLASS_METADATA);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_BODY),
                      (int)ROBO_HTML_ELEMENT_CLASS_SECTION);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_DIV),
                      (int)ROBO_HTML_ELEMENT_CLASS_SECTION);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_P),
                      (int)ROBO_HTML_ELEMENT_CLASS_PARAGRAPH);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_H3),
                      (int)ROBO_HTML_ELEMENT_CLASS_HEADING);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_SPAN),
                      (int)ROBO_HTML_ELEMENT_CLASS_TEXT);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_BR),
                      (int)ROBO_HTML_ELEMENT_CLASS_TEXT);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_IMG),
                      (int)ROBO_HTML_ELEMENT_CLASS_REPLACED);
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_INPUT),
                      (int)ROBO_HTML_ELEMENT_CLASS_CONTROL);
    /* A recognised tag whose role this runtime does not classify yet. */
    ROBO_CHECK_EQ_INT((int)robo_html_tag_class(ROBO_HTML_TAG_COL),
                      (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);

    tg_fixture_end();
}

/*
 * The element model's classify() and the tag vocabulary are the same answer.
 * This is the assertion that keeps Phase 8.5's role question from drifting
 * away from Phase 8.11's identity answer — the whole reason the old name
 * table was deleted rather than kept alongside.
 */
static void test_tg_classify_agrees_with_vocabulary(void)
{
    size_t count = robo_html_tag_count();
    size_t i;

    tg_fixture_begin();

    for (i = 0u; i < count; i++) {
        robo_html_tag_t tag;
        const char *name;

        ROBO_CHECK_EQ_INT(robo_html_tag_at(i, &tag), ROBO_OK);
        name = robo_html_tag_name(tag);
        ROBO_CHECK_EQ_INT(
            (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, name),
            (int)robo_html_tag_class(tag));
        /* The uppercase spelling must not change the role. */
        {
            char upper[ROBO_HTML_NAME_MAX + 1u];
            size_t k;

            for (k = 0u; name[k] != '\0' && k < ROBO_HTML_NAME_MAX; k++) {
                upper[k] = (name[k] >= 'a' && name[k] <= 'z')
                               ? (char)(name[k] - 'a' + 'A')
                               : name[k];
            }
            upper[k] = '\0';
            ROBO_CHECK_EQ_INT(
                (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML,
                                                upper),
                (int)robo_html_tag_class(tag));
        }
    }

    /* A name outside the vocabulary is GENERIC in both models. */
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_HTML, "my-widget"),
        (int)robo_html_tag_class(ROBO_HTML_TAG_UNKNOWN));
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_classify(ROBO_HTML_NAMESPACE_SVG, "a"),
        (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);

    tg_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Flags and predicates                                                */
/* ------------------------------------------------------------------ */

static void test_tg_void(void)
{
    static const robo_html_tag_t k_void[] = {
        ROBO_HTML_TAG_BR,    ROBO_HTML_TAG_HR,    ROBO_HTML_TAG_IMG,
        ROBO_HTML_TAG_INPUT, ROBO_HTML_TAG_META,  ROBO_HTML_TAG_LINK,
        ROBO_HTML_TAG_WBR,   ROBO_HTML_TAG_AREA,  ROBO_HTML_TAG_BASE,
        ROBO_HTML_TAG_COL,   ROBO_HTML_TAG_EMBED, ROBO_HTML_TAG_FRAME,
        ROBO_HTML_TAG_KEYGEN,
    };
    size_t i;

    tg_fixture_begin();

    for (i = 0u; i < sizeof(k_void) / sizeof(k_void[0]); i++) {
        ROBO_CHECK(robo_html_tag_is_void(k_void[i]));
    }
    /* The containers a document is built from are not void. */
    ROBO_CHECK(!robo_html_tag_is_void(ROBO_HTML_TAG_DIV));
    ROBO_CHECK(!robo_html_tag_is_void(ROBO_HTML_TAG_SPAN));
    ROBO_CHECK(!robo_html_tag_is_void(ROBO_HTML_TAG_P));
    ROBO_CHECK(!robo_html_tag_is_void(ROBO_HTML_TAG_BODY));
    ROBO_CHECK(!robo_html_tag_is_void(ROBO_HTML_TAG_HTML));
    ROBO_CHECK(!robo_html_tag_is_void(ROBO_HTML_TAG_UNKNOWN));
    /* <br> is the one that matters to the parser: void *and* text-level. */
    ROBO_CHECK(robo_html_tag_is_text_level(ROBO_HTML_TAG_BR));

    tg_fixture_end();
}

static void test_tg_headings(void)
{
    static const robo_html_tag_t k_headings[] = {
        ROBO_HTML_TAG_H1, ROBO_HTML_TAG_H2, ROBO_HTML_TAG_H3,
        ROBO_HTML_TAG_H4, ROBO_HTML_TAG_H5, ROBO_HTML_TAG_H6,
    };
    size_t i;

    tg_fixture_begin();

    for (i = 0u; i < sizeof(k_headings) / sizeof(k_headings[0]); i++) {
        ROBO_CHECK(robo_html_tag_is_heading(k_headings[i]));
        ROBO_CHECK_EQ_INT((int)robo_html_tag_class(k_headings[i]),
                          (int)ROBO_HTML_ELEMENT_CLASS_HEADING);
    }
    ROBO_CHECK(!robo_html_tag_is_heading(ROBO_HTML_TAG_DIV));
    ROBO_CHECK(!robo_html_tag_is_heading(ROBO_HTML_TAG_HGROUP));
    ROBO_CHECK(!robo_html_tag_is_heading(ROBO_HTML_TAG_HEAD));
    ROBO_CHECK(!robo_html_tag_is_heading(ROBO_HTML_TAG_UNKNOWN));
    /* h7 and h0 are not elements. */
    ROBO_CHECK_EQ_INT(
        (int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML, "h7"),
        (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML, "h0"),
        (int)ROBO_HTML_TAG_UNKNOWN);

    tg_fixture_end();
}

static void test_tg_lists(void)
{
    tg_fixture_begin();

    ROBO_CHECK(robo_html_tag_is_list(ROBO_HTML_TAG_UL));
    ROBO_CHECK(robo_html_tag_is_list(ROBO_HTML_TAG_OL));
    ROBO_CHECK(!robo_html_tag_is_list(ROBO_HTML_TAG_LI));
    ROBO_CHECK(!robo_html_tag_is_list(ROBO_HTML_TAG_MENU));
    ROBO_CHECK(!robo_html_tag_is_list(ROBO_HTML_TAG_DL));

    ROBO_CHECK(robo_html_tag_is_list_item(ROBO_HTML_TAG_LI));
    ROBO_CHECK(!robo_html_tag_is_list_item(ROBO_HTML_TAG_UL));
    ROBO_CHECK(!robo_html_tag_is_list_item(ROBO_HTML_TAG_UNKNOWN));

    tg_fixture_end();
}

static void test_tg_document_structure_and_metadata(void)
{
    tg_fixture_begin();

    ROBO_CHECK(robo_html_tag_is_root(ROBO_HTML_TAG_HTML));
    ROBO_CHECK(!robo_html_tag_is_root(ROBO_HTML_TAG_BODY));
    ROBO_CHECK(!robo_html_tag_is_root(ROBO_HTML_TAG_UNKNOWN));

    ROBO_CHECK(robo_html_tag_is_document_structure(ROBO_HTML_TAG_HTML));
    ROBO_CHECK(robo_html_tag_is_document_structure(ROBO_HTML_TAG_HEAD));
    ROBO_CHECK(robo_html_tag_is_document_structure(ROBO_HTML_TAG_BODY));
    ROBO_CHECK(!robo_html_tag_is_document_structure(ROBO_HTML_TAG_TITLE));
    ROBO_CHECK(!robo_html_tag_is_document_structure(ROBO_HTML_TAG_DIV));

    ROBO_CHECK(robo_html_tag_is_metadata(ROBO_HTML_TAG_HEAD));
    ROBO_CHECK(robo_html_tag_is_metadata(ROBO_HTML_TAG_TITLE));
    ROBO_CHECK(robo_html_tag_is_metadata(ROBO_HTML_TAG_META));
    ROBO_CHECK(robo_html_tag_is_metadata(ROBO_HTML_TAG_LINK));
    ROBO_CHECK(robo_html_tag_is_metadata(ROBO_HTML_TAG_STYLE));
    ROBO_CHECK(robo_html_tag_is_metadata(ROBO_HTML_TAG_SCRIPT));
    ROBO_CHECK(robo_html_tag_is_metadata(ROBO_HTML_TAG_NOSCRIPT));
    ROBO_CHECK(robo_html_tag_is_metadata(ROBO_HTML_TAG_TEMPLATE));
    ROBO_CHECK(robo_html_tag_is_metadata(ROBO_HTML_TAG_BASE));
    ROBO_CHECK(!robo_html_tag_is_metadata(ROBO_HTML_TAG_BODY));
    ROBO_CHECK(!robo_html_tag_is_metadata(ROBO_HTML_TAG_DIV));

    /* <head> is both document structure and metadata; <html> is not metadata. */
    ROBO_CHECK(robo_html_tag_is_metadata(ROBO_HTML_TAG_HEAD) &&
               robo_html_tag_is_document_structure(ROBO_HTML_TAG_HEAD));
    ROBO_CHECK(!robo_html_tag_is_metadata(ROBO_HTML_TAG_HTML));

    tg_fixture_end();
}

static void test_tg_sectioning_and_regions(void)
{
    tg_fixture_begin();

    /* Sectioning content. */
    ROBO_CHECK(robo_html_tag_is_sectioning(ROBO_HTML_TAG_SECTION));
    ROBO_CHECK(robo_html_tag_is_sectioning(ROBO_HTML_TAG_ARTICLE));
    ROBO_CHECK(robo_html_tag_is_sectioning(ROBO_HTML_TAG_NAV));
    ROBO_CHECK(robo_html_tag_is_sectioning(ROBO_HTML_TAG_ASIDE));
    ROBO_CHECK(!robo_html_tag_is_sectioning(ROBO_HTML_TAG_MAIN));
    ROBO_CHECK(!robo_html_tag_is_sectioning(ROBO_HTML_TAG_HEADER));
    ROBO_CHECK(!robo_html_tag_is_sectioning(ROBO_HTML_TAG_FOOTER));
    ROBO_CHECK(!robo_html_tag_is_sectioning(ROBO_HTML_TAG_DIV));

    /* Page-level regions are a different question with a different answer. */
    ROBO_CHECK(robo_html_tag_is_region(ROBO_HTML_TAG_MAIN));
    ROBO_CHECK(robo_html_tag_is_region(ROBO_HTML_TAG_HEADER));
    ROBO_CHECK(robo_html_tag_is_region(ROBO_HTML_TAG_FOOTER));
    ROBO_CHECK(!robo_html_tag_is_region(ROBO_HTML_TAG_SECTION));
    ROBO_CHECK(!robo_html_tag_is_region(ROBO_HTML_TAG_ARTICLE));
    ROBO_CHECK(!robo_html_tag_is_region(ROBO_HTML_TAG_DIV));
    ROBO_CHECK(!robo_html_tag_is_region(ROBO_HTML_TAG_UNKNOWN));

    tg_fixture_end();
}

static void test_tg_text_level(void)
{
    static const robo_html_tag_t k_inline[] = {
        ROBO_HTML_TAG_SPAN, ROBO_HTML_TAG_STRONG, ROBO_HTML_TAG_EM,
        ROBO_HTML_TAG_B,    ROBO_HTML_TAG_I,      ROBO_HTML_TAG_U,
        ROBO_HTML_TAG_SMALL, ROBO_HTML_TAG_CODE,  ROBO_HTML_TAG_BR,
        ROBO_HTML_TAG_A,    ROBO_HTML_TAG_ABBR,   ROBO_HTML_TAG_WBR,
    };
    size_t i;

    tg_fixture_begin();

    for (i = 0u; i < sizeof(k_inline) / sizeof(k_inline[0]); i++) {
        ROBO_CHECK(robo_html_tag_is_text_level(k_inline[i]));
    }
    /* Block containers are not text-level, even the small ones. */
    ROBO_CHECK(!robo_html_tag_is_text_level(ROBO_HTML_TAG_DIV));
    ROBO_CHECK(!robo_html_tag_is_text_level(ROBO_HTML_TAG_P));
    ROBO_CHECK(!robo_html_tag_is_text_level(ROBO_HTML_TAG_PRE));
    ROBO_CHECK(!robo_html_tag_is_text_level(ROBO_HTML_TAG_UL));
    ROBO_CHECK(!robo_html_tag_is_text_level(ROBO_HTML_TAG_H1));
    ROBO_CHECK(!robo_html_tag_is_text_level(ROBO_HTML_TAG_UNKNOWN));

    tg_fixture_end();
}

static void test_tg_predicates_match_the_mask(void)
{
    size_t count = robo_html_tag_count();
    size_t i;

    tg_fixture_begin();

    for (i = 0u; i < count; i++) {
        robo_html_tag_t tag;
        unsigned int flags;

        ROBO_CHECK_EQ_INT(robo_html_tag_at(i, &tag), ROBO_OK);
        flags = robo_html_tag_flags(tag);
        ROBO_CHECK(((flags & ROBO_HTML_TAG_FLAG_VOID) != 0u) ==
                   robo_html_tag_is_void(tag));
        ROBO_CHECK(((flags & ROBO_HTML_TAG_FLAG_ROOT) != 0u) ==
                   robo_html_tag_is_root(tag));
        ROBO_CHECK(((flags & ROBO_HTML_TAG_FLAG_DOCUMENT_STRUCTURE) != 0u) ==
                   robo_html_tag_is_document_structure(tag));
        ROBO_CHECK(((flags & ROBO_HTML_TAG_FLAG_METADATA) != 0u) ==
                   robo_html_tag_is_metadata(tag));
        ROBO_CHECK(((flags & ROBO_HTML_TAG_FLAG_SECTIONING) != 0u) ==
                   robo_html_tag_is_sectioning(tag));
        ROBO_CHECK(((flags & ROBO_HTML_TAG_FLAG_REGION) != 0u) ==
                   robo_html_tag_is_region(tag));
        ROBO_CHECK(((flags & ROBO_HTML_TAG_FLAG_HEADING) != 0u) ==
                   robo_html_tag_is_heading(tag));
        ROBO_CHECK(((flags & ROBO_HTML_TAG_FLAG_LIST) != 0u) ==
                   robo_html_tag_is_list(tag));
        ROBO_CHECK(((flags & ROBO_HTML_TAG_FLAG_LIST_ITEM) != 0u) ==
                   robo_html_tag_is_list_item(tag));
        ROBO_CHECK(((flags & ROBO_HTML_TAG_FLAG_TEXT_LEVEL) != 0u) ==
                   robo_html_tag_is_text_level(tag));
        /* Only one root exists, and it is <html>. */
        if (robo_html_tag_is_root(tag)) {
            ROBO_CHECK_EQ_INT((int)tag, (int)ROBO_HTML_TAG_HTML);
        }
    }

    tg_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Identity through the parser                                         */
/* ------------------------------------------------------------------ */

static void test_tg_parser_identity(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t el;
    size_t visited = 0u;

    tg_fixture_begin();
    id = tg_doc();
    tg_build(id, "<!DOCTYPE html><html><head><title>T</title></head>"
                 "<body><div id=\"main\"><h1>Hi</h1><p>Text</p>"
                 "<span>x</span><br><ul><li>one</li></ul></div></body></html>");
    root = tg_root(id);

    /* The document element. */
    el = tg_find(&root, ROBO_HTML_TAG_HTML);
    ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&el),
                      (int)ROBO_HTML_TAG_HTML);
    ROBO_CHECK(robo_html_element_is_tag(&el, ROBO_HTML_TAG_HTML));
    ROBO_CHECK(robo_html_tag_is_root(robo_html_element_tag(&el)));
    /* The name is still the name, and it is lowercase. */
    ROBO_CHECK_STR_EQ(robo_html_element_name(&el, NULL), "html");

    /* A nested element is found by identity, not by path. */
    el = tg_find(&el, ROBO_HTML_TAG_LI);
    ROBO_CHECK(robo_html_element_is_tag(&el, ROBO_HTML_TAG_LI));
    ROBO_CHECK(robo_html_tag_is_list_item(robo_html_element_tag(&el)));

    /* Referring to the wrong tag is false, not a near miss. */
    ROBO_CHECK(!robo_html_element_is_tag(&el, ROBO_HTML_TAG_UL));
    ROBO_CHECK(!robo_html_element_is_tag(&el, ROBO_HTML_TAG_DIV));

    /*
     * Every identity in this document agrees with the name it stores — the
     * check that makes "identity is derived from (namespace, name)" a fact
     * rather than an assumption. A visitor is the right shape because it
     * hands over a fresh view per node, so nothing is ever asked to be both
     * the input and the output of a resolution.
     */
    ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                          &root, ROBO_HTML_NODE_FILTER_ELEMENTS, tg_visit_identity,
                          &visited, NULL),
                      ROBO_OK);
    ROBO_CHECK(visited >= 8u);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    tg_fixture_end();
}

static void test_tg_uppercase_source_keeps_identity(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_element_view_t view;
    robo_html_node_handle_t div;
    const char *value;

    tg_fixture_begin();
    id = tg_doc();
    /* HTML tag names are case-insensitive, and stored lowercase. */
    tg_build(id, "<DIV CLASS=\"Panel\">Hi</DIV>");
    root = tg_root(id);

    div = tg_find(&root, ROBO_HTML_TAG_DIV);
    ROBO_CHECK_STR_EQ(robo_html_element_name(&div, NULL), "div");
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&div, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)view.tag, (int)ROBO_HTML_TAG_DIV);
    ROBO_CHECK_STR_EQ(view.name, "div");
    ROBO_CHECK_EQ_INT((int)view.classification,
                      (int)ROBO_HTML_ELEMENT_CLASS_SECTION);
    /* Attribute names are stored lowercase too; the value is untouched. */
    ROBO_CHECK(robo_html_element_has_attribute(&div, "class"));
    value = robo_html_element_attribute_value(&div, "class", NULL);
    ROBO_CHECK(value != NULL);
    if (value) {
        ROBO_CHECK_STR_EQ(value, "Panel");
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    tg_fixture_end();
}

static void test_tg_custom_element_has_no_identity(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t custom;
    robo_html_element_view_t view;
    char text[64];

    tg_fixture_begin();
    id = tg_doc();
    tg_build(id, "<body><my-widget data-x=\"1\">hello</my-widget></body>");
    root = tg_root(id);

    custom = tg_find(&root, ROBO_HTML_TAG_UNKNOWN);
    /* The name survives, because an unknown type is not a lost name. */
    ROBO_CHECK_STR_EQ(robo_html_element_name(&custom, NULL), "my-widget");
    ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&custom),
                      (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&custom, &view, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)view.tag, (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)view.classification,
                      (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);

    /* It is a real element: attributes, children and text all work. */
    {
        const char *value =
            robo_html_element_attribute_value(&custom, "data-x", NULL);

        ROBO_CHECK(value != NULL);
        if (value) {
            ROBO_CHECK_STR_EQ(value, "1");
        }
    }
    tg_text(&custom, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "hello");

    /*
     * UNKNOWN is answerable — "this element has no recognised identity" —
     * and it is the absence of identity, not a shared one: asking whether it
     * is a div is false, and asking whether it is unknown is true.
     */
    ROBO_CHECK(robo_html_element_is_tag(&custom, ROBO_HTML_TAG_UNKNOWN));
    ROBO_CHECK(!robo_html_element_is_tag(&custom, ROBO_HTML_TAG_DIV));
    ROBO_CHECK(!robo_html_element_is_tag(&custom, ROBO_HTML_TAG_BODY));

    /* A body element in the same document is not "unknown". */
    {
        robo_html_node_handle_t body = tg_find(&root, ROBO_HTML_TAG_BODY);

        ROBO_CHECK(!robo_html_element_is_tag(&body, ROBO_HTML_TAG_UNKNOWN));
        ROBO_CHECK(robo_html_element_is_tag(&body, ROBO_HTML_TAG_BODY));
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    tg_fixture_end();
}

static void test_tg_non_element_handles_have_no_identity(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t text;
    robo_html_node_handle_t stale;

    tg_fixture_begin();
    id = tg_doc();
    tg_build(id, "<p>Hello</p>");
    root = tg_root(id);

    ROBO_CHECK_EQ_INT(robo_html_node_find_first(
                          &root, ROBO_HTML_NODE_FILTER_TEXT, NULL, NULL, &text,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&text));

    /*
     * A text node has no tag, and — the point of the test — it does not
     * satisfy UNKNOWN either. "No recognised identity" is a statement about
     * an element, so it cannot be true of something that is not one.
     */
    ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&text),
                      (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK(!robo_html_element_is_tag(&text, ROBO_HTML_TAG_UNKNOWN));
    ROBO_CHECK(!robo_html_element_is_tag(&text, ROBO_HTML_TAG_P));

    /* The document node is not an element either. */
    ROBO_CHECK(!robo_html_element_is_tag(&root, ROBO_HTML_TAG_UNKNOWN));
    ROBO_CHECK(!robo_html_element_is_tag(&root, ROBO_HTML_TAG_HTML));
    ROBO_CHECK(!robo_html_element_is_tag(&root, ROBO_HTML_TAG_BODY));

    /* NULL and a stale handle: no crash, no tag. */
    ROBO_CHECK_EQ_INT((int)robo_html_element_tag(NULL),
                      (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK(!robo_html_element_is_tag(NULL, ROBO_HTML_TAG_DIV));
    ROBO_CHECK(!robo_html_element_is_tag(NULL, ROBO_HTML_TAG_UNKNOWN));

    stale = tg_find(&root, ROBO_HTML_TAG_P);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    /* The document is gone, so the handle resolves to nothing. */
    ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&stale),
                      (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK(!robo_html_element_is_tag(&stale, ROBO_HTML_TAG_P));
    ROBO_CHECK(!robo_html_element_is_tag(&stale, ROBO_HTML_TAG_UNKNOWN));

    tg_fixture_end();
}

static void test_tg_undefined_tag_query_is_not_unknown(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t custom;
    robo_html_node_handle_t div;

    tg_fixture_begin();
    id = tg_doc();
    tg_build(id, "<div><my-widget>w</my-widget></div>");
    root = tg_root(id);
    div = tg_find(&root, ROBO_HTML_TAG_DIV);
    custom = tg_find(&root, ROBO_HTML_TAG_UNKNOWN);

    /*
     * A bogus enumerator is a bug in the caller, so it must not behave like
     * "no identity" — otherwise a mistyped tag would silently match every
     * custom element in the document.
     */
    ROBO_CHECK(!robo_html_element_is_tag(&div, (robo_html_tag_t)9999));
    ROBO_CHECK(!robo_html_element_is_tag(&custom, (robo_html_tag_t)9999));
    ROBO_CHECK(!robo_html_element_is_tag(&div,
                                         (robo_html_tag_t)ROBO_HTML_TAG_COUNT));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    tg_fixture_end();
}

static void test_tg_identity_survives_mutation(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t div;
    robo_html_node_handle_t heading;
    robo_html_node_handle_t moved;

    tg_fixture_begin();
    id = tg_doc();
    tg_build(id, "<body><div><h1>Hi</h1></div></body>");
    root = tg_root(id);
    div = tg_find(&root, ROBO_HTML_TAG_DIV);
    heading = tg_find(&root, ROBO_HTML_TAG_H1);

    /* Detach the heading, then put it back under a new parent: a moved
     * element keeps its identity, because identity is its name — and the
     * kind of element it is does not depend on where it sits. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&div, &heading, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&heading),
                      (int)ROBO_HTML_TAG_H1);
    ROBO_CHECK(robo_html_element_is_tag(&heading, ROBO_HTML_TAG_H1));
    ROBO_CHECK(robo_html_tag_is_heading(robo_html_element_tag(&heading)));

    {
        robo_html_node_handle_t body = tg_find(&root, ROBO_HTML_TAG_BODY);

        ROBO_CHECK_EQ_INT(robo_html_node_append_child(&body, &heading, NULL),
                          ROBO_OK);
        moved = tg_find(&body, ROBO_HTML_TAG_H1);
        ROBO_CHECK(robo_html_node_handle_equal(&moved, &heading));
    }
    ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&heading),
                      (int)ROBO_HTML_TAG_H1);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    tg_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_html_tag(void)
{
    robo_test_register("html.tag.table_is_dense", test_tg_table_is_dense);
    robo_test_register("html.tag.unknown_and_undefined",
                       test_tg_unknown_and_undefined);
    robo_test_register("html.tag.lookup_by_name", test_tg_lookup_by_name);
    robo_test_register("html.tag.name_and_role", test_tg_name_and_role);
    robo_test_register("html.tag.classify_agrees_with_vocabulary",
                       test_tg_classify_agrees_with_vocabulary);
    robo_test_register("html.tag.void", test_tg_void);
    robo_test_register("html.tag.headings", test_tg_headings);
    robo_test_register("html.tag.lists", test_tg_lists);
    robo_test_register("html.tag.document_structure_and_metadata",
                       test_tg_document_structure_and_metadata);
    robo_test_register("html.tag.sectioning_and_regions",
                       test_tg_sectioning_and_regions);
    robo_test_register("html.tag.text_level", test_tg_text_level);
    robo_test_register("html.tag.predicates_match_the_mask",
                       test_tg_predicates_match_the_mask);
    robo_test_register("html.tag.parser_identity", test_tg_parser_identity);
    robo_test_register("html.tag.uppercase_source_keeps_identity",
                       test_tg_uppercase_source_keeps_identity);
    robo_test_register("html.tag.custom_element_has_no_identity",
                       test_tg_custom_element_has_no_identity);
    robo_test_register("html.tag.non_element_handles_have_no_identity",
                       test_tg_non_element_handles_have_no_identity);
    robo_test_register("html.tag.undefined_tag_query_is_not_unknown",
                       test_tg_undefined_tag_query_is_not_unknown);
    robo_test_register("html.tag.identity_survives_mutation",
                       test_tg_identity_survives_mutation);
}
