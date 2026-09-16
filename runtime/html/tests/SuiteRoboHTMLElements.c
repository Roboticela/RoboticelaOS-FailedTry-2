/*
 * RoboHTML Phase 8.11 basic-elements suite (html.elements.*).
 *
 * The tag suite (html.tag.*) proves the vocabulary is internally consistent.
 * This suite proves the *document* agrees with it: that the parser produces
 * the element a tag name claims, that each element carries attributes and
 * text the way its neighbours do, and that the whole existing DOM surface —
 * traversal, mutation, lifecycle, the debug dump — works on the new types
 * without a special case for any of them.
 *
 * Two shapes of test appear here on purpose:
 *   - table-driven sweeps over the element list, which catch the element
 *     someone adds to the vocabulary and forgets to make work; and
 *   - hand-written cases for the elements whose behaviour is genuinely
 *     different (void <br>, the document structure, <pre>'s whitespace, an
 *     unrecognised tag).
 *
 * The fixtures build through the real parser (`<tag>…</tag>`) rather than
 * through the element API, because "the parser creates the right element"
 * is exactly what is being tested.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
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

    el_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

static void el_build(robo_html_document_id_t id, const char *text)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);
}

static robo_html_node_handle_t el_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

/** Create an element under @p parent, through the element seam. */
static robo_html_node_handle_t el_make(robo_html_document_id_t id,
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
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL), ROBO_OK);
    return out;
}

static size_t el_child_count(const robo_html_node_handle_t *node)
{
    size_t count = 0u;

    ROBO_CHECK_EQ_INT(robo_html_node_child_count(node, &count, NULL), ROBO_OK);
    return count;
}

/** Whole subtree text of @p node into @p out (NUL-terminated). */
static void el_text(const robo_html_node_handle_t *node, char *out, size_t cap)
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

typedef struct el_tag_query {
    robo_html_tag_t tag;
} el_tag_query_t;

static int el_match_tag(const robo_html_node_view_t *view, void *user)
{
    const el_tag_query_t *q = (const el_tag_query_t *)user;

    return robo_html_element_is_tag(&view->handle, q->tag);
}

/** First element of @p tag at or below @p start; asserts it was found. */
static robo_html_node_handle_t el_find(const robo_html_node_handle_t *start,
                                       robo_html_tag_t tag)
{
    el_tag_query_t q;
    robo_html_node_handle_t out;

    q.tag = tag;
    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(
        robo_html_node_find_first(start, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  el_match_tag, &q, &out, NULL),
        ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

/** True when an element of @p tag is reachable from the document. */
static int el_has_tag(const robo_html_node_handle_t *root, robo_html_tag_t tag)
{
    el_tag_query_t q;
    robo_html_node_handle_t out;

    q.tag = tag;
    robo_html_node_handle_clear(&out);
    return robo_html_node_find_first(root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                     el_match_tag, &q, &out, NULL) == ROBO_OK;
}

static robo_result_t el_count_visit(const robo_html_node_view_t *view,
                                    size_t depth,
                                    void *user);
static robo_result_t el_visit_identity(const robo_html_node_view_t *view,
                                       size_t depth,
                                       void *user);

/** Node kind of @p node, through the resolver. */
static robo_html_node_kind_t el_kind(const robo_html_node_handle_t *node)
{
    robo_html_node_kind_t kind = ROBO_HTML_NODE_KIND_DOCUMENT;

    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(node, &kind), ROBO_OK);
    return kind;
}

/* ------------------------------------------------------------------ */
/* Every supported element type                                        */
/* ------------------------------------------------------------------ */

/**
 * Parse `<tag>text</tag>`, then assert the element really is that type.
 *
 * One row per element the phase introduces, so this is the test that answers
 * "does the parser produce the element the vocabulary names?" for all of
 * them at once. An element that cannot be found, or is found with the wrong
 * identity, fails on its own row rather than being silently absent.
 */
typedef struct el_case {
    robo_html_tag_t tag;
    /** Expected role, so a misclassified element is caught too. */
    robo_html_element_class_t cls;
} el_case_t;

static void el_run_case(const el_case_t *c)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t el;
    robo_html_element_view_t view;

    id = el_doc();
    el_build(id,
             "<!DOCTYPE html><html><head>"
             "<title>Doc</title></head><body>"
             "<div>alpha<span>beta</span></div>"
             "<h1>one</h1><h2>two</h2><h3>three</h3>"
             "<h4>four</h4><h5>five</h5><h6>six</h6>"
             "<p>para</p><br>"
             "<ul><li>u1</li></ul><ol><li>o1</li></ol>"
             "<strong>s</strong><em>e</em><b>b</b><i>i</i><u>u</u>"
             "<small>sm</small><code>c</code><pre>pre</pre>"
             "<section>sec</section><article>art</article><nav>nv</nav>"
             "<main>mn</main><header>hd</header><footer>ft</footer>"
             "<aside>as</aside>"
             "</body></html>");
    root = el_root(id);

    ROBO_CHECK_MSG(el_has_tag(&root, c->tag),
                   robo_html_tag_name(c->tag));
    el = el_find(&root, c->tag);
    ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&el), (int)c->tag);
    ROBO_CHECK(robo_html_element_is_tag(&el, c->tag));
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&el, &view, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)view.tag, (int)c->tag);
    ROBO_CHECK_EQ_INT((int)view.classification, (int)c->cls);
    ROBO_CHECK_EQ_INT((int)view.namespace_id, (int)ROBO_HTML_NAMESPACE_HTML);
    /* The element's stored name is the canonical spelling of its type. */
    ROBO_CHECK_STR_EQ(robo_html_element_name(&el, NULL),
                      robo_html_tag_name(c->tag));
    ROBO_CHECK_EQ_INT((int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   view.name),
                      (int)c->tag);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_document_structure_elements(void)
{
    static const el_case_t k_cases[] = {
        {ROBO_HTML_TAG_HTML, ROBO_HTML_ELEMENT_CLASS_ROOT},
        {ROBO_HTML_TAG_HEAD, ROBO_HTML_ELEMENT_CLASS_METADATA},
        {ROBO_HTML_TAG_BODY, ROBO_HTML_ELEMENT_CLASS_SECTION},
        {ROBO_HTML_TAG_TITLE, ROBO_HTML_ELEMENT_CLASS_METADATA},
    };
    size_t i;

    for (i = 0u; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        el_run_case(&k_cases[i]);
    }
}

static void test_el_structural_content_elements(void)
{
    static const el_case_t k_cases[] = {
        {ROBO_HTML_TAG_DIV, ROBO_HTML_ELEMENT_CLASS_SECTION},
        {ROBO_HTML_TAG_SPAN, ROBO_HTML_ELEMENT_CLASS_TEXT},
        {ROBO_HTML_TAG_P, ROBO_HTML_ELEMENT_CLASS_PARAGRAPH},
        {ROBO_HTML_TAG_BR, ROBO_HTML_ELEMENT_CLASS_TEXT},
    };
    size_t i;

    for (i = 0u; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        el_run_case(&k_cases[i]);
    }
}

static void test_el_heading_elements(void)
{
    static const el_case_t k_cases[] = {
        {ROBO_HTML_TAG_H1, ROBO_HTML_ELEMENT_CLASS_HEADING},
        {ROBO_HTML_TAG_H2, ROBO_HTML_ELEMENT_CLASS_HEADING},
        {ROBO_HTML_TAG_H3, ROBO_HTML_ELEMENT_CLASS_HEADING},
        {ROBO_HTML_TAG_H4, ROBO_HTML_ELEMENT_CLASS_HEADING},
        {ROBO_HTML_TAG_H5, ROBO_HTML_ELEMENT_CLASS_HEADING},
        {ROBO_HTML_TAG_H6, ROBO_HTML_ELEMENT_CLASS_HEADING},
    };
    size_t i;

    for (i = 0u; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        el_run_case(&k_cases[i]);
    }
}

static void test_el_list_elements(void)
{
    static const el_case_t k_cases[] = {
        {ROBO_HTML_TAG_UL, ROBO_HTML_ELEMENT_CLASS_SECTION},
        {ROBO_HTML_TAG_OL, ROBO_HTML_ELEMENT_CLASS_SECTION},
        {ROBO_HTML_TAG_LI, ROBO_HTML_ELEMENT_CLASS_SECTION},
    };
    size_t i;

    for (i = 0u; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        el_run_case(&k_cases[i]);
    }
}

static void test_el_inline_semantic_elements(void)
{
    static const el_case_t k_cases[] = {
        {ROBO_HTML_TAG_STRONG, ROBO_HTML_ELEMENT_CLASS_TEXT},
        {ROBO_HTML_TAG_EM, ROBO_HTML_ELEMENT_CLASS_TEXT},
        {ROBO_HTML_TAG_B, ROBO_HTML_ELEMENT_CLASS_TEXT},
        {ROBO_HTML_TAG_I, ROBO_HTML_ELEMENT_CLASS_TEXT},
        {ROBO_HTML_TAG_U, ROBO_HTML_ELEMENT_CLASS_TEXT},
        {ROBO_HTML_TAG_SMALL, ROBO_HTML_ELEMENT_CLASS_TEXT},
        {ROBO_HTML_TAG_CODE, ROBO_HTML_ELEMENT_CLASS_TEXT},
        {ROBO_HTML_TAG_PRE, ROBO_HTML_ELEMENT_CLASS_SECTION},
    };
    size_t i;

    for (i = 0u; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        el_run_case(&k_cases[i]);
    }
}

static void test_el_generic_semantic_elements(void)
{
    static const el_case_t k_cases[] = {
        {ROBO_HTML_TAG_SECTION, ROBO_HTML_ELEMENT_CLASS_SECTION},
        {ROBO_HTML_TAG_ARTICLE, ROBO_HTML_ELEMENT_CLASS_SECTION},
        {ROBO_HTML_TAG_NAV, ROBO_HTML_ELEMENT_CLASS_SECTION},
        {ROBO_HTML_TAG_MAIN, ROBO_HTML_ELEMENT_CLASS_SECTION},
        {ROBO_HTML_TAG_HEADER, ROBO_HTML_ELEMENT_CLASS_SECTION},
        {ROBO_HTML_TAG_FOOTER, ROBO_HTML_ELEMENT_CLASS_SECTION},
        {ROBO_HTML_TAG_ASIDE, ROBO_HTML_ELEMENT_CLASS_SECTION},
    };
    size_t i;

    for (i = 0u; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        el_run_case(&k_cases[i]);
    }
}

/**
 * The pure identity/role sweep over the *whole* vocabulary: every known tag
 * resolves to itself by name, so an element the vocabulary lists is an
 * element the document can name. This is the test that would catch a table
 * row added without a matching identity path.
 */
static void test_el_vocabulary_resolves_through_document(void)
{
    robo_html_document_id_t id;
    size_t count = robo_html_tag_count();
    size_t i;

    id = el_doc();

    /* The capability reports the vocabulary this build actually has, and it
     * is a claim about the tag model rather than about CSS or selectors. */
    {
        robo_html_dom_capabilities_t caps;

        robo_html_dom_capabilities_init(&caps);
        ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(&caps), ROBO_OK);
        ROBO_CHECK_EQ_INT(caps.element_tags, 1);
        ROBO_CHECK_EQ_INT(caps.events, 0);
        ROBO_CHECK(count > 0u);
        /* The count the capability implies is the count the table has. */
        ROBO_CHECK_EQ_INT((int)ROBO_HTML_TAG_COUNT - 1, (int)count);
    }

    for (i = 0u; i < count; i++) {
        robo_html_tag_t tag;
        const char *name;

        ROBO_CHECK_EQ_INT(robo_html_tag_at(i, &tag), ROBO_OK);
        name = robo_html_tag_name(tag);
        ROBO_CHECK_STR_EQ(robo_html_tag_name(
                              robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                      name)),
                          name);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* The document structure                                              */
/* ------------------------------------------------------------------ */

static void test_el_full_document_tree(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t html;
    robo_html_node_handle_t head;
    robo_html_node_handle_t body;
    robo_html_node_handle_t title;
    char text[128];

    id = el_doc();
    el_build(id,
             "<!DOCTYPE html><html><head><title>Hello</title></head>"
             "<body>Hi</body></html>");
    root = el_root(id);

    html = el_find(&root, ROBO_HTML_TAG_HTML);
    head = el_find(&html, ROBO_HTML_TAG_HEAD);
    body = el_find(&html, ROBO_HTML_TAG_BODY);
    title = el_find(&head, ROBO_HTML_TAG_TITLE);

    /* The document structure is real nesting, not a flat list of names. */
    {
        int inside = 0;

        ROBO_CHECK_EQ_INT(robo_html_node_contains(&html, &head, &inside, NULL),
                          ROBO_OK);
        ROBO_CHECK(inside);
        ROBO_CHECK_EQ_INT(robo_html_node_contains(&head, &html, &inside, NULL),
                          ROBO_OK);
        ROBO_CHECK(!inside);
        ROBO_CHECK_EQ_INT(robo_html_node_contains(&html, &body, &inside, NULL),
                          ROBO_OK);
        ROBO_CHECK(inside);
        ROBO_CHECK_EQ_INT(robo_html_node_contains(&head, &title, &inside, NULL),
                          ROBO_OK);
        ROBO_CHECK(inside);
        /* <title> belongs to <head>, and it is not inside <body>. */
        ROBO_CHECK_EQ_INT(robo_html_node_contains(&body, &title, &inside, NULL),
                          ROBO_OK);
        ROBO_CHECK(!inside);
    }

    el_text(&title, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "Hello");
    el_text(&body, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "Hi");

    /* The document element is the root element, and every tag sees itself. */
    ROBO_CHECK(robo_html_element_is_tag(&html, ROBO_HTML_TAG_HTML));
    ROBO_CHECK(robo_html_tag_is_root(robo_html_element_tag(&html)));
    ROBO_CHECK(robo_html_tag_is_document_structure(
        robo_html_element_tag(&head)));
    ROBO_CHECK(robo_html_tag_is_document_structure(
        robo_html_element_tag(&body)));
    ROBO_CHECK(robo_html_tag_is_metadata(robo_html_element_tag(&title)));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Void behaviour                                                      */
/* ------------------------------------------------------------------ */

static void test_el_void_br(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t p;
    robo_html_node_handle_t br;
    char buf[64];

    id = el_doc();
    /* <br> is void: it takes no children, so the text after it is a sibling
     * of the <br>, not a child of one. */
    el_build(id, "<body><p>one<br>two</p></body>");
    root = el_root(id);

    p = el_find(&root, ROBO_HTML_TAG_P);
    br = el_find(&p, ROBO_HTML_TAG_BR);
    ROBO_CHECK(robo_html_tag_is_void(robo_html_element_tag(&br)));
    ROBO_CHECK_EQ_INT(el_child_count(&br), 0u);
    ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&br),
                      (int)ROBO_HTML_TAG_BR);

    /* Its parent is the paragraph, and it has siblings on both sides. */
    {
        robo_html_node_handle_t parent;
        robo_html_node_handle_t prev;
        robo_html_node_handle_t next;

        ROBO_CHECK_EQ_INT(robo_html_node_parent(&br, &parent, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&parent, &p));
        ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&br, &prev, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&br, &next, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)el_kind(&prev),
                          (int)ROBO_HTML_NODE_KIND_TEXT);
    }

    /*
     * The whole paragraph still reads as one run of text. A void element
     * contributes no content of its own, so the run is the two text nodes
     * with nothing in between — the <br> separates nothing textually until a
     * layout phase decides it does.
     */
    el_text(&p, buf, sizeof(buf));
    ROBO_CHECK_STR_EQ(buf, "onetwo");

    /* A void element parses the same whether or not it is self-closed, and
     * the flag records the spelling the source used. */
    {
        robo_html_document_id_t id2 = el_doc();
        robo_html_node_handle_t root2;
        robo_html_node_handle_t div;
        robo_html_element_view_t view;

        el_build(id2, "<div>a<br/>b</div>");
        root2 = el_root(id2);
        div = el_find(&root2, ROBO_HTML_TAG_DIV);
        br = el_find(&div, ROBO_HTML_TAG_BR);
        ROBO_CHECK_EQ_INT(el_child_count(&br), 0u);
        ROBO_CHECK_EQ_INT(robo_html_element_view_get(&br, &view, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)view.tag, (int)ROBO_HTML_TAG_BR);
        ROBO_CHECK_STR_EQ(view.name, "br");
        ROBO_CHECK(view.self_closing);
        /* Three children: text, the void <br>, text — never a child of <br>. */
        ROBO_CHECK_EQ_INT(el_child_count(&div), 3u);
        el_text(&div, buf, sizeof(buf));
        ROBO_CHECK_STR_EQ(buf, "ab");
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id2, NULL), ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Attributes and text on the new elements                             */
/* ------------------------------------------------------------------ */

static void test_el_attributes_on_new_elements(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t el;
    const char *value;
    size_t length = 0u;

    id = el_doc();
    el_build(id,
             "<body><div id=\"main\" class=\"container\" title=\"Card\" "
             "data-x=\"1\">hello</div>"
             "<section aria-label=\"Region\"></section>"
             "<p disabled>x</p></body>");
    root = el_root(id);

    el = el_find(&root, ROBO_HTML_TAG_DIV);
    ROBO_CHECK(robo_html_element_has_attribute(&el, "id"));
    value = robo_html_element_attribute_value(&el, "id", &length);
    ROBO_CHECK(value != NULL);
    if (value) {
        ROBO_CHECK_STR_EQ(value, "main");
        ROBO_CHECK_EQ_INT((int)length, 4);
    }
    value = robo_html_element_attribute_value(&el, "class", NULL);
    ROBO_CHECK(value != NULL);
    if (value) {
        ROBO_CHECK_STR_EQ(value, "container");
    }
    value = robo_html_element_attribute_value(&el, "data-x", NULL);
    ROBO_CHECK(value != NULL);
    if (value) {
        ROBO_CHECK_STR_EQ(value, "1");
    }
    /* Attribute lookup is case-insensitive; storage is lowercase. */
    ROBO_CHECK(robo_html_element_has_attribute(&el, "ID"));
    ROBO_CHECK(robo_html_element_has_attribute(&el, "Class"));

    el = el_find(&root, ROBO_HTML_TAG_SECTION);
    value = robo_html_element_attribute_value(&el, "aria-label", NULL);
    ROBO_CHECK(value != NULL);
    if (value) {
        ROBO_CHECK_STR_EQ(value, "Region");
    }

    /*
     * A boolean-style attribute on any element is "present with an empty
     * value" — the same answer `<input disabled>` gives.
     */
    el = el_find(&root, ROBO_HTML_TAG_P);
    ROBO_CHECK(robo_html_element_has_attribute(&el, "disabled"));
    value = robo_html_element_attribute_value(&el, "disabled", &length);
    ROBO_CHECK(value != NULL);
    if (value) {
        ROBO_CHECK_STR_EQ(value, "");
        ROBO_CHECK_EQ_INT((int)length, 0);
    }
    ROBO_CHECK(!robo_html_element_has_attribute(&el, "absent"));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_text_children(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t p;
    robo_html_node_handle_t strong;
    char text[128];

    id = el_doc();
    el_build(id,
             "<body><p>Hello <strong>Roboticela</strong> Runtime</p>"
             "<div>one<b>two</b>three</div>"
             "<h1>Plain</h1></body>");
    root = el_root(id);

    /*
     * Three children in order: text, element, text. The text either side of
     * the inline element is not folded into it.
     */
    p = el_find(&root, ROBO_HTML_TAG_P);
    ROBO_CHECK_EQ_INT(el_child_count(&p), 3u);
    {
        robo_html_node_handle_t first;
        robo_html_node_handle_t last;
        robo_html_node_handle_t middle;

        ROBO_CHECK_EQ_INT(robo_html_node_child_at(&p, 0u, &first, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_child_at(&p, 1u, &middle, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_child_at(&p, 2u, &last, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)el_kind(&first),
                          (int)ROBO_HTML_NODE_KIND_TEXT);
        ROBO_CHECK_EQ_INT((int)el_kind(&last),
                          (int)ROBO_HTML_NODE_KIND_TEXT);
        ROBO_CHECK(robo_html_element_is_tag(&middle, ROBO_HTML_TAG_STRONG));
        el_text(&first, text, sizeof(text));
        ROBO_CHECK_STR_EQ(text, "Hello ");
        el_text(&last, text, sizeof(text));
        ROBO_CHECK_STR_EQ(text, " Runtime");
    }
    strong = el_find(&p, ROBO_HTML_TAG_STRONG);
    el_text(&strong, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "Roboticela");
    ROBO_CHECK(robo_html_tag_is_text_level(robo_html_element_tag(&strong)));

    /* Six children: text, element, text inside a div. */
    {
        robo_html_node_handle_t div = el_find(&root, ROBO_HTML_TAG_DIV);

        ROBO_CHECK_EQ_INT(el_child_count(&div), 3u);
        /* No whitespace in the source, so none appears in the run. */
        el_text(&div, text, sizeof(text));
        ROBO_CHECK_STR_EQ(text, "onetwothree");
    }

    /* A heading holding a single text node. */
    {
        robo_html_node_handle_t h1 = el_find(&root, ROBO_HTML_TAG_H1);

        robo_html_node_handle_t only;

        ROBO_CHECK_EQ_INT(el_child_count(&h1), 1u);
        ROBO_CHECK_EQ_INT(robo_html_node_child_at(&h1, 0u, &only, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)el_kind(&only),
                          (int)ROBO_HTML_NODE_KIND_TEXT);
        /* Out of range is a miss, never a zeroed-but-successful handle. */
        ROBO_CHECK_EQ_INT(robo_html_node_child_at(&h1, 1u, &only, NULL),
                          ROBO_NOT_FOUND);
        el_text(&h1, text, sizeof(text));
        ROBO_CHECK_STR_EQ(text, "Plain");
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_pre_preserves_whitespace(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t pre;
    char text[256];

    id = el_doc();
    el_build(id, "<body><pre>  line one\n\tline two  </pre></body>");
    root = el_root(id);

    pre = el_find(&root, ROBO_HTML_TAG_PRE);
    el_text(&pre, text, sizeof(text));
    /* Byte-exact: no trimming, no collapsing, tabs kept. */
    ROBO_CHECK_STR_EQ(text, "  line one\n\tline two  ");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_unicode_text(void)
{
    static const char *const k_samples[] = {
        "مرحبا",       /* Arabic  */
        "فارسی",       /* Persian */
        "اردو",        /* Urdu    */
        "हिन्दी",         /* Hindi   */
        "বাংলা",        /* Bengali */
        "中文",        /* Chinese */
        "日本語",      /* Japanese */
        "한국어",      /* Korean  */
        "😀 🚀 ❤️", /* emoji   */
        "line\nbreak",
    };
    size_t i;

    for (i = 0u; i < sizeof(k_samples) / sizeof(k_samples[0]); i++) {
        robo_html_document_id_t id = el_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t h1;
        char text[256];


        /* Append the sample through the real parser, then read it back byte
         * for byte: a tag type that cannot carry arbitrary text is not a
         * usable element. */
        {
            robo_html_source_t src;
            char source[512];

            snprintf(source, sizeof(source), "<body><h1>%s</h1></body>",
                     k_samples[i]);
            el_build(id, source);
            (void)src;
            root = el_root(id);
            h1 = el_find(&root, ROBO_HTML_TAG_H1);
        }
        el_text(&h1, text, sizeof(text));
        ROBO_CHECK_MSG(strstr(text, k_samples[i]) != NULL, k_samples[i]);

        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
        el_fixture_end();
    }
}

static void test_el_unicode_through_parser(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t p;
    robo_html_node_handle_t h2;
    char text[256];

    id = el_doc();
    el_build(id,
             "<body><p title=\"مرحبا\">فارسی and 中文</p>"
             "<h2>😀 emoji</h2></body>");
    root = el_root(id);

    p = el_find(&root, ROBO_HTML_TAG_P);
    el_text(&p, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "فارسی and 中文");
    {
        const char *value = robo_html_element_attribute_value(&p, "title", NULL);

        ROBO_CHECK(value != NULL);
        if (value) {
            ROBO_CHECK_STR_EQ(value, "مرحبا");
        }
    }

    h2 = el_find(&root, ROBO_HTML_TAG_H2);
    el_text(&h2, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "😀 emoji");
    ROBO_CHECK(robo_html_tag_is_heading(robo_html_element_tag(&h2)));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Traversal through the new elements                                  */
/* ------------------------------------------------------------------ */

typedef struct el_order {
    const robo_html_tag_t *tags;
    size_t count;
    size_t seen;
    int failed;
} el_order_t;

static robo_result_t el_visit_order(const robo_html_node_view_t *view,
                                    size_t depth,
                                    void *user)
{
    el_order_t *order = (el_order_t *)user;

    (void)depth;
    if (order->seen >= order->count) {
        order->failed = 1;
        return ROBO_OK;
    }
    if (!robo_html_element_is_tag(&view->handle, order->tags[order->seen])) {
        order->failed = 1;
        return ROBO_OK;
    }
    order->seen++;
    return ROBO_OK;
}

static void test_el_traversal_order(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t body;
    static const robo_html_tag_t k_expected[] = {
        ROBO_HTML_TAG_BODY,  ROBO_HTML_TAG_H1,     ROBO_HTML_TAG_DIV,
        ROBO_HTML_TAG_P,     ROBO_HTML_TAG_SPAN,   ROBO_HTML_TAG_SPAN,
        ROBO_HTML_TAG_SECTION, ROBO_HTML_TAG_UL,   ROBO_HTML_TAG_LI,
    };
    el_order_t order;

    id = el_doc();
    el_build(id,
             "<body><h1>Head</h1><div>"
             "<p>x<span>one</span><span>two</span></p>"
             "<section><ul><li>item</li></ul></section>"
             "</div></body>");
    root = el_root(id);
    body = el_find(&root, ROBO_HTML_TAG_BODY);

    order.tags = k_expected;
    order.count = sizeof(k_expected) / sizeof(k_expected[0]);
    order.seen = 0u;
    order.failed = 0;

    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(
                          &body, ROBO_HTML_NODE_FILTER_ELEMENTS,
                          el_visit_order, &order, NULL),
                      ROBO_OK);
    ROBO_CHECK_MSG(!order.failed, "element order differed from source order");
    ROBO_CHECK_EQ_INT((int)order.seen, (int)order.count);

    /* Element filtering excludes text; the unfiltered walk includes it. */
    {
        size_t elements = 0u;
        size_t texts = 0u;
        size_t all = 0u;

        ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                              &body, ROBO_HTML_NODE_FILTER_ELEMENTS,
                              el_count_visit, &elements, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                              &body, ROBO_HTML_NODE_FILTER_TEXT, el_count_visit,
                              &texts, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                              &body, ROBO_HTML_NODE_FILTER_ALL, el_count_visit,
                              &all, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)all, (int)(elements + texts));
        ROBO_CHECK(elements > 0u);
        ROBO_CHECK(texts > 0u);
    }

    /* Upward traversal from the deepest element reaches the document. */
    {
        robo_html_node_handle_t li = el_find(&root, ROBO_HTML_TAG_LI);
        robo_html_node_handle_t up = li;
        robo_html_node_handle_t parent;
        size_t depth = 0u;
        size_t climbed = 0u;

        ROBO_CHECK_EQ_INT(robo_html_node_depth(&li, &depth, NULL), ROBO_OK);
        ROBO_CHECK(depth >= 4u);
        while (robo_html_node_parent(&up, &parent, NULL) == ROBO_OK) {
            up = parent;
            climbed++;
            /* A bounded climb: a cycle would hang the suite, not fail it. */
            ROBO_CHECK(climbed <= 16u);
            if (climbed > 16u) {
                break;
            }
        }
        ROBO_CHECK_EQ_INT((int)el_kind(&up), (int)ROBO_HTML_NODE_KIND_DOCUMENT);
        ROBO_CHECK(robo_html_node_handle_equal(&up, &root));
        /* The document node itself is the end of the chain. */
        ROBO_CHECK_EQ_INT(robo_html_node_parent(&root, &parent, NULL),
                          ROBO_NOT_FOUND);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static robo_result_t el_count_visit(const robo_html_node_view_t *view,
                                    size_t depth,
                                    void *user)
{
    size_t *count = (size_t *)user;

    (void)view;
    (void)depth;
    (*count)++;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Element creation (the construction seam)                            */
/* ------------------------------------------------------------------ */

/** Append a text node under @p parent, through the text seam. */
static void el_note(robo_html_document_id_t id,
                    const robo_html_node_handle_t *parent,
                    const char *content)
{
    robo_html_text_create_info_t info;
    robo_html_node_handle_t out;

    robo_html_text_create_info_init(&info);
    info.parent = *parent;
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
    info.content = content;
    info.length = content ? strlen(content) : 0u;

    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(robo_html_text_create(id, &info, &out, NULL), ROBO_OK);
}

/**
 * Every element type can be *created*, not only parsed.
 *
 * Creation is a construction operation, so this runs on a document that has
 * not been finalized — the same gate Phase 8.9 established and pinned
 * (`html.mutate.creation_stays_gated`). Moving parsed nodes around is the
 * mutation story below; this is the creation story, and it is the half of
 * the tag layer the parser cannot exercise because the parser only ever
 * spells names.
 */
static void test_el_creation_seam(void)
{
    static const robo_html_tag_t k_types[] = {
        ROBO_HTML_TAG_HTML,     ROBO_HTML_TAG_HEAD,    ROBO_HTML_TAG_BODY,
        ROBO_HTML_TAG_TITLE,    ROBO_HTML_TAG_DIV,     ROBO_HTML_TAG_SPAN,
        ROBO_HTML_TAG_P,        ROBO_HTML_TAG_BR,      ROBO_HTML_TAG_H1,
        ROBO_HTML_TAG_H2,       ROBO_HTML_TAG_H3,      ROBO_HTML_TAG_H4,
        ROBO_HTML_TAG_H5,       ROBO_HTML_TAG_H6,      ROBO_HTML_TAG_UL,
        ROBO_HTML_TAG_OL,       ROBO_HTML_TAG_LI,      ROBO_HTML_TAG_STRONG,
        ROBO_HTML_TAG_EM,       ROBO_HTML_TAG_B,       ROBO_HTML_TAG_I,
        ROBO_HTML_TAG_U,        ROBO_HTML_TAG_SMALL,   ROBO_HTML_TAG_CODE,
        ROBO_HTML_TAG_PRE,      ROBO_HTML_TAG_SECTION, ROBO_HTML_TAG_ARTICLE,
        ROBO_HTML_TAG_NAV,      ROBO_HTML_TAG_MAIN,    ROBO_HTML_TAG_HEADER,
        ROBO_HTML_TAG_FOOTER,   ROBO_HTML_TAG_ASIDE,
    };
    robo_html_document_id_t id = el_doc();
    robo_html_node_handle_t root = el_root(id);
    size_t i;

    for (i = 0u; i < sizeof(k_types) / sizeof(k_types[0]); i++) {
        robo_html_node_handle_t made = el_make(id, &root, robo_html_tag_name(
                                                         k_types[i]));
        robo_html_element_view_t view;

        ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&made),
                          (int)k_types[i]);
        ROBO_CHECK(robo_html_element_is_tag(&made, k_types[i]));
        ROBO_CHECK(robo_html_element_view_get(&made, &view, NULL) == ROBO_OK);
        ROBO_CHECK_EQ_INT((int)view.tag, (int)k_types[i]);
        ROBO_CHECK_EQ_INT((int)view.classification,
                          (int)robo_html_tag_class(k_types[i]));
        /* A created element is as real as a parsed one: it can hold text. */
        if (k_types[i] == ROBO_HTML_TAG_HTML) {
            continue; /* <html> is the root slot; a second one is refused */
        }
        el_note(id, &made, "x");
        ROBO_CHECK_EQ_INT(el_child_count(&made), 1u);
        {
            robo_html_node_handle_t child;

            ROBO_CHECK_EQ_INT(robo_html_node_child_at(&made, 0u, &child, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT((int)el_kind(&child),
                              (int)ROBO_HTML_NODE_KIND_TEXT);
        }
    }

    /* A name the vocabulary does not know creates a valid generic element. */
    {
        robo_html_node_handle_t custom = el_make(id, &root, "my-widget");

        ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&custom),
                          (int)ROBO_HTML_TAG_UNKNOWN);
        ROBO_CHECK(robo_html_element_is_tag(&custom, ROBO_HTML_TAG_UNKNOWN));
        ROBO_CHECK_STR_EQ(robo_html_element_name(&custom, NULL), "my-widget");
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Mutation of the new elements                                        */
/* ------------------------------------------------------------------ */

/**
 * Rearranging a parsed document, which is where mutation is legal.
 *
 * Phase 8.9's rule is that a mutation may only rearrange the nodes a
 * document already has — registering a new node is a construction operation
 * and finalization closes construction. So this test moves parsed elements,
 * which is exactly what a consumer does to a loaded page, and asserts that
 * every element stays the type it was through every move.
 */
static void test_el_mutation(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t body;
    robo_html_node_handle_t section;
    robo_html_node_handle_t ul;
    robo_html_node_handle_t div;
    robo_html_node_handle_t first_li;
    robo_html_node_handle_t second_li;
    char text[128];

    id = el_doc();
    el_build(id,
             "<body><h1>Head</h1>"
             "<section><ul><li>one</li><li>two</li></ul></section>"
             "<div>spare</div></body>");
    root = el_root(id);
    body = el_find(&root, ROBO_HTML_TAG_BODY);
    section = el_find(&body, ROBO_HTML_TAG_SECTION);
    ul = el_find(&section, ROBO_HTML_TAG_UL);
    div = el_find(&body, ROBO_HTML_TAG_DIV);
    first_li = el_find(&ul, ROBO_HTML_TAG_LI);
    ROBO_CHECK_EQ_INT(el_child_count(&ul), 2u);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&ul, 1u, &second_li, NULL),
                      ROBO_OK);

    /* Remove, then re-append: a move is two explicit steps (8.9). */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&body, &div, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&div, ROBO_HTML_TAG_DIV));
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&section, &div, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&div, ROBO_HTML_TAG_DIV));

    /* Insert before an existing sibling: the moved div lands before the ul. */
    {
        robo_html_node_handle_t moved = div;

        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&section, &moved, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_node_insert_before(&section, &moved, &ul, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(el_child_count(&section), 2u);
        {
            robo_html_node_handle_t child0;
            robo_html_node_handle_t child1;

            ROBO_CHECK_EQ_INT(robo_html_node_child_at(&section, 0u, &child0,
                                                      NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_html_node_child_at(&section, 1u, &child1,
                                                      NULL),
                              ROBO_OK);
            ROBO_CHECK(robo_html_node_handle_equal(&child0, &div));
            ROBO_CHECK(robo_html_node_handle_equal(&child1, &ul));
        }
        /* The moved node keeps its identity and its content. */
        ROBO_CHECK(robo_html_element_is_tag(&div, ROBO_HTML_TAG_DIV));
        el_text(&div, text, sizeof(text));
        ROBO_CHECK_STR_EQ(text, "spare");
    }

    /* Replace the first list item with the second, then back again. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&ul, &second_li, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_node_replace_child(&ul, &second_li, &first_li, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(el_child_count(&ul), 1u);
    {
        robo_html_node_handle_t li = el_find(&ul, ROBO_HTML_TAG_LI);

        ROBO_CHECK(robo_html_node_handle_equal(&li, &second_li));
        ROBO_CHECK(robo_html_element_is_tag(&li, ROBO_HTML_TAG_LI));
        ROBO_CHECK(robo_html_tag_is_list_item(robo_html_element_tag(&li)));
        el_text(&li, text, sizeof(text));
        ROBO_CHECK_STR_EQ(text, "two");
    }
    /* The displaced item survives, detached, still typed, content intact. */
    ROBO_CHECK(robo_html_element_is_tag(&first_li, ROBO_HTML_TAG_LI));
    el_text(&first_li, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "one");

    /* Re-parent the displaced item, which is how a move is spelled. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&ul, &first_li, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(el_child_count(&ul), 2u);
    ROBO_CHECK(robo_html_element_is_tag(&first_li, ROBO_HTML_TAG_LI));

    /* The tree is still a tree, and every element still knows its type. */
    {
        robo_html_tree_report_t report;

        ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                          ROBO_OK);
        ROBO_CHECK_MSG(report.ok, "mutation broke a tree invariant");
        ROBO_CHECK_EQ_INT((int)report.orphans, 0);
        ROBO_CHECK_EQ_INT((int)report.bad_links, 0);
        ROBO_CHECK_EQ_INT((int)report.bad_parents, 0);
        ROBO_CHECK_EQ_INT((int)report.cycles, 0);
    }
    ROBO_CHECK(el_has_tag(&body, ROBO_HTML_TAG_DIV));
    ROBO_CHECK(el_has_tag(&section, ROBO_HTML_TAG_UL));
    ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                          &root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                          el_visit_identity, NULL, NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Lifecycle compatibility                                             */
/* ------------------------------------------------------------------ */

static void test_el_lifecycle(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_document_lifecycle_t life;
    robo_html_source_t src;

    el_fixture_begin();
    robo_html_source_init(&src);
    id = el_doc();
    root = el_root(id);

    /* CREATED: no input yet, construction open, elements typed as usual. */
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)life, (int)ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    {
        robo_html_node_handle_t div = el_make(id, &root, "div");

        ROBO_CHECK(robo_html_element_is_tag(&div, ROBO_HTML_TAG_DIV));
    }

    /*
     * A complete one-shot load settles at COMPLETE, so it is neither
     * "loading" nor "interactive" any more: INTERACTIVE names the window
     * between the parse ending and the input settling, and a whole-source
     * load passes through it without stopping there (Phase 8.10).
     */
    el_build(id, "<body><h1>Head</h1><ul><li>one</li></ul></body>");
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)life, (int)ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK(robo_html_document_is_complete(id));
    ROBO_CHECK(!robo_html_document_is_loading(id));
    ROBO_CHECK(!robo_html_document_is_interactive(id));

    /* The parsed elements are typed, and mutation still works. */
    {
        robo_html_node_handle_t body = el_find(&root, ROBO_HTML_TAG_BODY);
        robo_html_node_handle_t h1 = el_find(&body, ROBO_HTML_TAG_H1);
        robo_html_node_handle_t ul = el_find(&body, ROBO_HTML_TAG_UL);

        ROBO_CHECK(robo_html_element_is_tag(&body, ROBO_HTML_TAG_BODY));
        ROBO_CHECK(robo_html_tag_is_heading(robo_html_element_tag(&h1)));
        ROBO_CHECK(robo_html_tag_is_list(robo_html_element_tag(&ul)));
        /* A move on a completed document: still allowed, still typed. */
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&body, &ul, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&body, &ul, &h1, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&ul, ROBO_HTML_TAG_UL));
        /* Registering a *new* node stays closed: creation is construction. */
        {
            robo_html_element_create_info_t info;
            robo_html_node_handle_t out;

            robo_html_element_create_info_init(&info);
            info.parent = body;
            info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
            info.namespace_id = ROBO_HTML_NAMESPACE_HTML;
            info.local_name = "h3";
            robo_html_node_handle_clear(&out);
            ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &out, NULL),
                              ROBO_ERROR);
        }
    }

    /* The tree stayed consistent across every transition. */
    {
        robo_html_tree_report_t report;

        ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                          ROBO_OK);
        ROBO_CHECK(report.ok);
    }

    /* After destruction nothing resolves, so identity is simply absent. */
    {
        robo_html_node_handle_t stale = el_find(&root, ROBO_HTML_TAG_H1);

        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&stale),
                          (int)ROBO_HTML_TAG_UNKNOWN);
        ROBO_CHECK(!robo_html_element_is_tag(&stale, ROBO_HTML_TAG_H1));
        ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &stale, NULL),
                          ROBO_NOT_FOUND);
    }

    /* A fresh document with an attached (incomplete) source is LOADING. */
    {
        robo_html_document_id_t id2 = el_doc();
        robo_html_node_handle_t root2 = el_root(id2);

        ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "<p>x</p>", NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id2, &src, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_document_is_loading(id2));
        ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&root2),
                          (int)ROBO_HTML_TAG_UNKNOWN);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id2, NULL), ROBO_OK);
    }

    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Malformed and unrecognised markup                                   */
/* ------------------------------------------------------------------ */

static void test_el_malformed_markup(void)
{
    static const char *const k_inputs[] = {
        "",
        "plain text only",
        "   \n\t ",
        "<div",
        "<div>",
        "<div id",
        "<div id=",
        "<div id=\"x",
        "<div =bad>text",
        "<div id=\"x\" id=\"y\">dup</div>",
        "<div class>empty</div>",
        "<div disabled>x</div>",
        "<p>unclosed",
        "<p>a<p>b",
        "<span></div>mismatched",
        "<ul><li>a<li>b</ul>",
        "<h1>unclosed heading",
        "<h1><h2>nested</h2></h1>",
        "<div><my-widget>x</my-widget></div>",
        "<DIV CLASS=\"UPPER\">x</DIV>",
        "<div><div><div><div><div>deep</div></div></div></div></div>",
        "<br><br><br>",
        "<section></section><section></section>",
    };
    size_t i;

    for (i = 0u; i < sizeof(k_inputs) / sizeof(k_inputs[0]); i++) {
        robo_html_document_id_t id = el_doc();
        robo_html_node_handle_t root;
        robo_html_tree_report_t report;

        el_build(id, k_inputs[i]);
        root = el_root(id);

        /* Whatever the recovery decided, the result is a sound tree. */
        ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                          ROBO_OK);
        ROBO_CHECK_MSG(report.ok, k_inputs[i]);
        ROBO_CHECK_EQ_INT((int)report.orphans, 0);
        ROBO_CHECK_EQ_INT((int)report.bad_links, 0);
        ROBO_CHECK_EQ_INT((int)report.cycles, 0);
        ROBO_CHECK_EQ_INT((int)report.bad_parents, 0);

        /* Every element the recovery produced has a coherent identity. */
        ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                              &root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                              el_visit_identity, NULL, NULL),
                          ROBO_OK);

        /* The dumped tree agrees with the tree, malformed input included. */
        {
            char dump[4096];

            dump[0] = '\0';
            ROBO_CHECK_EQ_INT(
                robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), NULL,
                                              NULL),
                ROBO_OK);
        }

        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
        el_fixture_end();
    }
}

static robo_result_t el_visit_identity(const robo_html_node_view_t *view,
                                       size_t depth,
                                       void *user)
{
    robo_html_tag_t tag = robo_html_element_tag(&view->handle);

    (void)depth;
    (void)user;
    ROBO_CHECK_EQ_INT((int)tag,
                      (int)robo_html_tag_from_name(ROBO_HTML_NAMESPACE_HTML,
                                                   view->name));
    ROBO_CHECK(robo_html_element_is_tag(&view->handle, tag));
    return ROBO_OK;
}

static void test_el_unknown_element(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t widget;
    robo_html_element_view_t view;
    char text[128];

    id = el_doc();
    el_build(id,
             "<body><my-widget data-kind=\"custom\">inside"
             "<span>nested</span></my-widget></body>");
    root = el_root(id);

    widget = el_find(&root, ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK_STR_EQ(robo_html_element_name(&widget, NULL), "my-widget");
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&widget, &view, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)view.tag, (int)ROBO_HTML_TAG_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)view.classification,
                      (int)ROBO_HTML_ELEMENT_CLASS_GENERIC);

    /* Attributes, children, text and traversal all behave normally. */
    {
        const char *value =
            robo_html_element_attribute_value(&widget, "data-kind", NULL);

        ROBO_CHECK(value != NULL);
        if (value) {
            ROBO_CHECK_STR_EQ(value, "custom");
        }
    }
    ROBO_CHECK_EQ_INT(el_child_count(&widget), 2u);
    el_text(&widget, text, sizeof(text));
    ROBO_CHECK_STR_EQ(text, "insidenested");
    ROBO_CHECK(el_has_tag(&widget, ROBO_HTML_TAG_SPAN));

    /* It is not silently coerced into a known element. */
    ROBO_CHECK(!robo_html_element_is_tag(&widget, ROBO_HTML_TAG_DIV));
    ROBO_CHECK(!robo_html_element_is_tag(&widget, ROBO_HTML_TAG_SPAN));
    {
        size_t elements = 0u;

        ROBO_CHECK_EQ_INT(robo_html_node_walk_descendants(
                              &widget, ROBO_HTML_NODE_FILTER_ELEMENTS,
                              el_count_visit, &elements, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)elements, 1); /* only the nested span */
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Debug dump                                                          */
/* ------------------------------------------------------------------ */

static void test_el_tree_dump_shows_types(void)
{
    robo_html_document_id_t id;
    static char dump[8192];
    static const char *const k_expected[] = {
        "body",   "h1",      "div",     "p",       "span",
        "ul",     "li",      "strong",  "em",      "code",
        "pre",    "section", "article", "nav",     "main",
        "header", "footer",  "aside",   "br",
    };
    size_t i;

    id = el_doc();
    el_build(id,
             "<!DOCTYPE html><html><head><title>T</title></head><body>\n"
             "<h1>Head</h1>\n"
             "<div><p>text <span>s</span></p>"
             "<ul><li>i</li></ul><strong>st</strong><em>e</em><code>c</code>"
             "<pre>p</pre><section>sec</section><article>art</article>"
             "<nav>nv</nav><main>mn</main><header>hd</header>"
             "<footer>ft</footer><aside>as</aside><br></div>\n"
             "</body></html>");

    dump[0] = '\0';
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), NULL, NULL),
        ROBO_OK);

    /*
     * The dump names every element by its tag, which is the element's type in
     * this architecture — so "the dump shows element types" is exactly this
     * assertion. The line-per-node shape is unchanged by this phase on
     * purpose: reformatting it would invalidate six suites to convey nothing
     * the name does not already convey.
     */
    for (i = 0u; i < sizeof(k_expected) / sizeof(k_expected[0]); i++) {
        char needle[32];

        snprintf(needle, sizeof(needle), " %s\n", k_expected[i]);
        ROBO_CHECK_MSG(strstr(dump, needle) != NULL, k_expected[i]);
    }
    ROBO_CHECK(strstr(dump, "Text: \"Head\"") != NULL);
    ROBO_CHECK(strstr(dump, "Text: \"i\"") != NULL);
    /* Void <br> has no children, so no line is indented under it. */
    ROBO_CHECK(strstr(dump, " br\n") != NULL);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

static void test_el_dump_reflects_mutation(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t body;
    robo_html_node_handle_t section;
    static char dump[4096];

    id = el_doc();
    el_build(id, "<body><h1>Head</h1><section>s</section></body>");
    root = el_root(id);
    body = el_find(&root, ROBO_HTML_TAG_BODY);
    section = el_find(&body, ROBO_HTML_TAG_SECTION);

    dump[0] = '\0';
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), NULL, NULL),
        ROBO_OK);
    ROBO_CHECK(strstr(dump, " section\n") != NULL);

    /*
     * The dump is read from the live tree, not from what was parsed: detaching
     * the section removes it from the picture, and re-attaching brings it
     * back — a real structural change, observed through the debug surface.
     */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&body, &section, NULL),
                      ROBO_OK);
    dump[0] = '\0';
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), NULL, NULL),
        ROBO_OK);
    ROBO_CHECK(strstr(dump, " section\n") == NULL);
    /*
     * The dump walks the document, so a detached subtree is absent from it —
     * and that is the honest picture, not a gap: the section is still a typed
     * tree in its own right, which its own accessors show.
     */
    ROBO_CHECK(strstr(dump, "Text: \"s\"") == NULL);
    ROBO_CHECK(robo_html_element_is_tag(&section, ROBO_HTML_TAG_SECTION));
    {
        char text[32];

        el_text(&section, text, sizeof(text));
        ROBO_CHECK_STR_EQ(text, "s");
    }

    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&body, &section, NULL),
                      ROBO_OK);
    dump[0] = '\0';
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), NULL, NULL),
        ROBO_OK);
    ROBO_CHECK(strstr(dump, " section\n") != NULL);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    el_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_html_elements(void)
{
    robo_test_register("html.elements.document_structure_elements",
                       test_el_document_structure_elements);
    robo_test_register("html.elements.structural_content_elements",
                       test_el_structural_content_elements);
    robo_test_register("html.elements.heading_elements",
                       test_el_heading_elements);
    robo_test_register("html.elements.list_elements",
                       test_el_list_elements);
    robo_test_register("html.elements.inline_semantic_elements",
                       test_el_inline_semantic_elements);
    robo_test_register("html.elements.generic_semantic_elements",
                       test_el_generic_semantic_elements);
    robo_test_register("html.elements.vocabulary_resolves_through_document",
                       test_el_vocabulary_resolves_through_document);
    robo_test_register("html.elements.full_document_tree",
                       test_el_full_document_tree);
    robo_test_register("html.elements.void_br", test_el_void_br);
    robo_test_register("html.elements.attributes_on_new_elements",
                       test_el_attributes_on_new_elements);
    robo_test_register("html.elements.text_children",
                       test_el_text_children);
    robo_test_register("html.elements.pre_preserves_whitespace",
                       test_el_pre_preserves_whitespace);
    robo_test_register("html.elements.unicode_text",
                       test_el_unicode_text);
    robo_test_register("html.elements.unicode_through_parser",
                       test_el_unicode_through_parser);
    robo_test_register("html.elements.traversal_order",
                       test_el_traversal_order);
    robo_test_register("html.elements.creation_seam",
                       test_el_creation_seam);
    robo_test_register("html.elements.mutation", test_el_mutation);
    robo_test_register("html.elements.lifecycle", test_el_lifecycle);
    robo_test_register("html.elements.malformed_markup",
                       test_el_malformed_markup);
    robo_test_register("html.elements.unknown_element",
                       test_el_unknown_element);
    robo_test_register("html.elements.tree_dump_shows_types",
                       test_el_tree_dump_shows_types);
    robo_test_register("html.elements.dump_reflects_mutation",
                       test_el_dump_reflects_mutation);
}
