/*
 * RoboHTML DOM suite (html.dom.*) — Phase 8.15.
 *
 * The per-phase suites (html.doc.*, html.element.*, html.attr.*, html.text.*,
 * html.traverse.*, html.mutate.*, html.lifecycle.*, html.image.*,
 * html.anchor.*, html.form.*) each pin one phase's surface. This suite pins
 * the things that only exist *between* them — the ones that would still be
 * wrong if every per-phase suite passed:
 *
 *   isolation      two documents that parse to identical bytes stay two
 *                  documents, and no operation can mix them.
 *   one id rule    the fragment lookup, a label's `for` and a control's
 *                  `form` are three consumers of one rule, and they are
 *                  asserted to agree — including where they diverge, which is
 *                  when the first element carrying an id is not the kind of
 *                  element that consumer can use.
 *   live resolution  an id change moves every consumer at once, because
 *                  nothing is cached.
 *   void           void is a *parser* rule, not a DOM rule. The tree builder
 *                  never puts a child under `<input>`; the mutation API
 *                  decides by node *kind* and therefore allows it. Both facts
 *                  are asserted, because a reader will expect one of them to
 *                  be the other.
 *   recovery       a malformed corpus leaves a tree that audits clean and
 *                  re-parses byte-identically.
 *   mutation       attribute set/replace/remove with the dependent state
 *                  checked at every step — including the case that makes the
 *                  declared/current split worth having: an explicit control
 *                  value survives the removal of the attribute it defaulted
 *                  from.
 *   depth, width   structure at 200 levels and 300 siblings: order, depth,
 *                  whole-subtree moves, and teardown.
 *   unicode        thirteen script families through parse, attributes, text
 *                  nodes, mutation, and the id consumers.
 *   churn          repeated document lifetimes balance the debug allocator's
 *                  live count, which is the closest thing to a leak test the
 *                  runtime can express.
 *
 * Three tests are worth reading first, because they are the ones that would
 * fail if the model were quietly wrong:
 *
 *   html.dom.id_resolution_agrees
 *   html.dom.void_child_behaviour
 *   html.dom.attribute_mutation_sequences
 *
 * Every fixture parses real HTML through the real tokenizer and tree builder.
 * Where a test needs to *construct* nodes it uses a hand-built document,
 * because construction is gated on a document that has not been finalized and
 * a parsed document is finalized by definition — that split is the model's,
 * not the suite's convenience.
 */

#include "RoboHTMLDomTestSupport.h"

#include "RoboMem/RoboMemDebug.h"
#include "RoboHTML/RoboHTMLAnchor.h"
#include "RoboHTML/RoboHTMLForm.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLText.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Construction (hand-built documents only)                            */
/* ------------------------------------------------------------------ */

/** Create an element of @p name under @p parent, asserting success. */
static robo_html_node_handle_t dom_create_under(
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

/** Create a text node of @p text under @p parent, asserting success. */
static robo_html_node_handle_t dom_create_text(
    robo_html_document_id_t id, const robo_html_node_handle_t *parent,
    const char *text)
{
    robo_html_text_create_info_t info;
    robo_html_node_handle_t out;

    robo_html_text_create_info_init(&info);
    info.parent = *parent;
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
    info.content = text;
    info.length = strlen(text);
    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(robo_html_text_create(id, &info, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

static size_t dom_child_count(const robo_html_node_handle_t *node)
{
    size_t count = 0u;

    ROBO_CHECK_EQ_INT(robo_html_node_child_count(node, &count, NULL), ROBO_OK);
    return count;
}

static size_t dom_depth_of(const robo_html_node_handle_t *node)
{
    size_t depth = 0u;

    ROBO_CHECK_EQ_INT(robo_html_node_depth(node, &depth, NULL), ROBO_OK);
    return depth;
}

/**
 * Detach @p node from whatever actually lists it.
 *
 * A parsed document's elements hang under implied `html`/`body` elements, not
 * directly under the document node, so a test that removes a child by naming
 * the root is asserting the wrong tree. Asking the node for its parent is the
 * only form of the call that works for both a parsed and a hand-built tree.
 */
static void dom_detach(const robo_html_node_handle_t *node)
{
    robo_html_node_handle_t parent;

    ROBO_CHECK_EQ_INT(robo_html_node_parent(node, &parent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&parent, node, NULL),
                      ROBO_OK);
}

/**
 * Next following sibling of @p node that is a @p tag.
 *
 * Separate in and out handles on purpose: `robo_html_node_next_sibling` may
 * clear its output, so passing one handle as both would invalidate the node
 * being asked about. @return 1 and a filled @p out, or 0 at the end of the
 * sibling list.
 */
static int dom_next_tag_sibling(const robo_html_node_handle_t *node,
                                robo_html_tag_t tag,
                                robo_html_node_handle_t *out)
{
    robo_html_node_handle_t cursor = *node;
    robo_html_node_handle_t next;

    while (robo_html_node_next_sibling(&cursor, &next, NULL) == ROBO_OK) {
        cursor = next;
        if (robo_html_element_is_tag(&cursor, tag)) {
            *out = cursor;
            return 1;
        }
    }
    return 0;
}

/** An attribute to match, for a name/value search. */
typedef struct dom_attr_match {
    const char *name;
    const char *value;
} dom_attr_match_t;

static int dom_predicate_attr(const robo_html_node_view_t *view, void *user)
{
    const dom_attr_match_t *m = (const dom_attr_match_t *)user;
    const char *value = robo_html_element_attribute_value(&view->handle,
                                                          m->name, NULL);

    return value != NULL && strcmp(value, m->value) == 0;
}

/** First element whose attribute @p name equals @p value. */
static robo_html_node_handle_t dom_find_by_attr(
    const robo_html_node_handle_t *start, const char *name, const char *value)
{
    dom_attr_match_t m;
    robo_html_node_handle_t out;

    m.name = name;
    m.value = value;
    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(
        robo_html_node_find_first(start, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  dom_predicate_attr, &m, &out, NULL),
        ROBO_OK);
    return out;
}

/** Read an element's current control value into @p buf. */
static void dom_control_value(const robo_html_node_handle_t *element,
                              char *buf,
                              size_t cap,
                              int *out_declared)
{
    size_t length = 0u;

    ROBO_CHECK_EQ_INT(robo_html_element_control_value(element, buf, cap,
                                                      &length, out_declared,
                                                      NULL),
                      ROBO_OK);
    buf[length < cap ? length : cap - 1u] = '\0';
}

/* ------------------------------------------------------------------ */
/* Walk visitors                                                       */
/* ------------------------------------------------------------------ */

/** Counts elements on a chain and checks each sits one level below the last. */
typedef struct dom_chain_record {
    size_t visited;
    size_t mismatches;
} dom_chain_record_t;

static robo_result_t dom_chain_visitor(const robo_html_node_view_t *view,
                                       size_t depth,
                                       void *user)
{
    dom_chain_record_t *r = (dom_chain_record_t *)user;

    (void)view;
    r->visited++;
    if (depth != r->visited) {
        r->mismatches++;
    }
    return ROBO_OK;
}

/** Records the first document-order index at which some tags appear. */
typedef struct dom_order_record {
    int heading;
    int form;
    int paragraph;
    int index;
} dom_order_record_t;

static robo_result_t dom_order_visitor(const robo_html_node_view_t *view,
                                       size_t depth,
                                       void *user)
{
    dom_order_record_t *r = (dom_order_record_t *)user;

    (void)depth;
    if (robo_html_element_is_tag(&view->handle, ROBO_HTML_TAG_H1) &&
        r->heading < 0) {
        r->heading = r->index;
    }
    if (robo_html_element_is_tag(&view->handle, ROBO_HTML_TAG_FORM) &&
        r->form < 0) {
        r->form = r->index;
    }
    if (robo_html_element_is_tag(&view->handle, ROBO_HTML_TAG_P) &&
        r->paragraph < 0) {
        r->paragraph = r->index;
    }
    r->index++;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* 1. Document isolation                                               */
/* ------------------------------------------------------------------ */

/**
 * Two documents that parsed the same bytes stay two documents.
 *
 * This is the assertion the ownership model exists for. Identical markup means
 * identical node *names* and identical `id` attributes, so a model that
 * resolved anything by name or by id-string across documents would look
 * correct on one document and corrupt the other. Every check therefore has a
 * counterpart: an operation that mixes the documents is refused, and the
 * document that was not addressed is asserted unchanged — by its dump, its
 * node count and its audit.
 */
static void test_dom_document_isolation(void)
{
    static const char k_markup[] =
        "<div id=\"Box\"><p id=\"Text\">One</p>"
        "<input id=\"Field\" value=\"declared\"></div>";
    size_t live_before = robo_html_live_document_count();
    robo_html_document_id_t a = dom_new_document();
    robo_html_document_id_t b = dom_new_document();
    robo_html_node_handle_t root_a;
    robo_html_node_handle_t root_b;
    robo_html_node_handle_t box_a;
    robo_html_node_handle_t box_b;
    robo_html_node_handle_t field_a;
    robo_html_node_handle_t field_b;
    char dump_a[2048];
    char dump_b[2048];
    char dump_after[2048];
    char buf[64];
    int declared = 0;

    dom_parse(a, k_markup);
    dom_parse(b, k_markup);
    root_a = dom_root(a);
    root_b = dom_root(b);

    /* Identical bytes, identical trees — and no shared identity at all. */
    dom_dump(a, dump_a, sizeof(dump_a));
    dom_dump(b, dump_b, sizeof(dump_b));
    ROBO_CHECK_STR_EQ(dump_a, dump_b);
    ROBO_CHECK_EQ_INT((int)robo_html_document_node_count(a),
                      (int)robo_html_document_node_count(b));
    ROBO_CHECK(robo_html_node_handle_document(&root_a) !=
               robo_html_node_handle_document(&root_b));
    ROBO_CHECK(!robo_html_node_handle_equal(&root_a, &root_b));

    box_a = dom_find_id(&root_a, "Box");
    box_b = dom_find_id(&root_b, "Box");
    ROBO_CHECK(!robo_html_node_handle_equal(&box_a, &box_b));
    ROBO_CHECK_EQ_INT(robo_html_document_owns_node(a, &box_a), 1);
    ROBO_CHECK_EQ_INT(robo_html_document_owns_node(b, &box_a), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_owns_node(a, &box_b), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_owns_node(b, &box_b), 1);

    /*
     * A mutation that names one node from each document is refused. The code
     * is the documented one for a foreign child (ROBO_NOT_SUPPORTED), not a
     * not-found: the child resolves fine, it is simply in the wrong document.
     */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&box_a, &box_b, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(
        robo_html_node_insert_before(&box_a, &box_b, &box_a, NULL),
        ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&box_b, &box_a, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(
        robo_html_node_replace_child(&root_b, &box_a, &box_b, NULL),
        ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&box_a, &root_b, NULL),
                      ROBO_NOT_SUPPORTED);

    /* The construction seam refuses a foreign parent before it creates
     * anything, so no node is added to either document. */
    {
        robo_html_element_create_info_t info;
        robo_html_node_handle_t rejected;

        robo_html_element_create_info_init(&info);
        info.parent = box_a; /* a handle of document a ... */
        info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
        info.namespace_id = ROBO_HTML_NAMESPACE_HTML;
        info.local_name = "aside";
        robo_html_node_handle_clear(&rejected);
        /* ... used to create in document b. */
        ROBO_CHECK(robo_html_element_create(b, &info, &rejected, NULL) !=
                   ROBO_OK);
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&rejected));
    }

    /* Neither tree moved: not a child, not a byte of the dump. */
    ROBO_CHECK_EQ_INT((int)dom_child_count(&box_a), 2);
    ROBO_CHECK_EQ_INT((int)dom_child_count(&root_b), 1);
    dom_dump(a, dump_after, sizeof(dump_after));
    ROBO_CHECK_STR_EQ(dump_after, dump_a);
    dom_dump(b, dump_after, sizeof(dump_after));
    ROBO_CHECK_STR_EQ(dump_after, dump_b);
    dom_expect_clean_tree(a);
    dom_expect_clean_tree(b);

    /*
     * Form state is per document. The same `<input value="declared">` exists
     * in both, so pinning an override in one must leave the other reporting
     * its declared value — a leaked value would still pass a single-document
     * test.
     */
    field_a = dom_find_id(&root_a, "Field");
    field_b = dom_find_id(&root_b, "Field");
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&field_a, "override", 8u, NULL),
        ROBO_OK);
    dom_control_value(&field_a, buf, sizeof(buf), &declared);
    ROBO_CHECK_STR_EQ(buf, "override");
    ROBO_CHECK_EQ_INT(declared, 0);
    dom_control_value(&field_b, buf, sizeof(buf), &declared);
    ROBO_CHECK_STR_EQ(buf, "declared");
    ROBO_CHECK_EQ_INT(declared, 1);

    /*
     * Destroying one document leaves the other fully usable: its handles
     * resolve, its audit is clean, and nothing it holds went stale with the
     * other document.
     */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_is_live(a), 0);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_stale(&box_a), 1);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_stale(&field_a), 1);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_stale(&box_b), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_owns_node(a, &box_a), 0);
    dom_expect_clean_tree(b);
    dom_dump(b, dump_after, sizeof(dump_after));
    ROBO_CHECK_STR_EQ(dump_after, dump_b);
    ROBO_CHECK_EQ_INT((int)dom_count_tags(&root_b, ROBO_HTML_TAG_DIV), 1);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_html_live_document_count(), (int)live_before);
}

/* ------------------------------------------------------------------ */
/* 2. One id rule, three consumers                                     */
/* ------------------------------------------------------------------ */

/**
 * Three consumers, one rule: the first element in document order whose `id`
 * attribute is exactly the name, with the consuming surface's own kind test
 * applied to *that* element and nowhere else.
 *
 * The divergences are the interesting half. A `<div id="Dup">` before an
 * `<input id="Dup">` means the fragment lookup answers with the `div` (it
 * accepts any element) while a `for="Dup"` answers with nothing — not with
 * the input further down, which is what a "find a labelable element with that
 * id" implementation would wrongly do. Same for a `form="Name"` whose first
 * match is not a `<form>`.
 */
static void test_dom_id_resolution_agrees(void)
{
    static const char k_markup[] =
        "<div id=\"Dup\">first</div>"
        "<input id=\"Dup\">"
        "<form id=\"Shared\"></form>"
        "<div id=\"Shared\"></div>"
        "<label for=\"Shared\">not-a-form-target</label>"
        "<label><input id=\"Wrap\"></label>"
        "<input id=\"Attr\" form=\"Shared\">"
        "<input id=\"Missing\" form=\"Absent\">"
        "<input id=\"DupTarget\" form=\"Dup\">";
    robo_html_document_id_t id = dom_document_of(k_markup);
    robo_html_node_handle_t root = dom_root(id);
    robo_html_node_handle_t found;
    robo_html_node_handle_t label;
    robo_html_node_handle_t owner;
    robo_html_node_handle_t labels[3];
    robo_html_form_owner_source_t source = ROBO_HTML_FORM_OWNER_SOURCE_NONE;
    int from_attribute = -1;
    size_t seen = 0u;

    /* Collect the labels in document order: 0 = for="Shared", 1 = wrapping,
     * 2 = the one after the wrapping label would be next; two are enough to
     * reach the two rules, so collect three and assert the count. */
    label = dom_find_tag(&root, ROBO_HTML_TAG_LABEL);
    labels[seen++] = label;
    while (seen < 3u && dom_next_tag_sibling(&label, ROBO_HTML_TAG_LABEL,
                                             &label)) {
        labels[seen++] = label;
    }
    ROBO_CHECK_EQ_INT((int)seen, 2);

    /* A unique id: both id consumers point at the same element. */
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "Wrap", 4u, &found, NULL),
        ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&found, ROBO_HTML_TAG_INPUT));
    robo_html_node_handle_clear(&owner);
    from_attribute = -1;
    ROBO_CHECK_EQ_INT(
        robo_html_element_label_target(&labels[1], &owner, &from_attribute,
                                       NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(from_attribute, 0); /* found by descent, not by `for` */
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &found));

    /* Duplicate id: the fragment lookup takes the first in document order,
     * whatever kind that is. */
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "Dup", 3u, &found, NULL),
        ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&found, ROBO_HTML_TAG_DIV));

    /* The same name through a label: the first match is not labelable, so the
     * answer is nothing — not the second match, and not the label's own
     * children. */
    {
        robo_html_node_handle_t dup_label = labels[0];

        robo_html_node_handle_clear(&owner);
        from_attribute = -1;
        ROBO_CHECK_EQ_INT(
            robo_html_element_label_target(&dup_label, &owner,
                                           &from_attribute, NULL),
            ROBO_OK);
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&owner));
        /* 0, not 1: the flag says which step *answered*, not which attribute
         * was consulted. A `for` that names a non-labelable element answers
         * nothing, so the honest report is "no target, and no step found
         * one" rather than "the `for` was used". */
        ROBO_CHECK_EQ_INT(from_attribute, 0);
    }

    /* A `form` attribute that resolves wins, and says so. */
    {
        robo_html_node_handle_t control = dom_find_id(&root, "Attr");
        robo_html_node_handle_t shared = dom_find_id(&root, "Shared");

        ROBO_CHECK_EQ_INT(
            robo_html_element_form_owner(&control, &owner, &source, NULL),
            ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &shared));
        ROBO_CHECK_EQ_INT((int)source,
                          (int)ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE);
    }

    /* A `form` attribute naming nothing resolves to no owner, and does not
     * fail: a control outside every form is an ordinary document. */
    {
        robo_html_node_handle_t control = dom_find_id(&root, "Missing");

        robo_html_node_handle_clear(&owner);
        source = ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE;
        ROBO_CHECK_EQ_INT(
            robo_html_element_form_owner(&control, &owner, &source, NULL),
            ROBO_OK);
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&owner));
        ROBO_CHECK_EQ_INT((int)source, (int)ROBO_HTML_FORM_OWNER_SOURCE_NONE);
    }

    /* A `form` attribute whose first match is not a form falls back to
     * ancestry rather than refusing or reaching the second match. */
    {
        robo_html_node_handle_t control = dom_find_id(&root, "DupTarget");

        robo_html_node_handle_clear(&owner);
        source = ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE;
        ROBO_CHECK_EQ_INT(
            robo_html_element_form_owner(&control, &owner, &source, NULL),
            ROBO_OK);
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&owner));
        ROBO_CHECK_EQ_INT((int)source, (int)ROBO_HTML_FORM_OWNER_SOURCE_NONE);
    }

    /* Exact and case-sensitive, and an empty fragment names the top of the
     * document and therefore nothing: all three are refusals, not wildcards. */
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "dup", 3u, &found, NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "", 0u, &found, NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "DupX", 4u, &found, NULL),
        ROBO_NOT_FOUND);

    /* Only elements can answer an id query, so a text node never can. */
    ROBO_CHECK(dom_try_find_id(&root, "Dup", &found));
    ROBO_CHECK(robo_html_element_is_element(&found));

    dom_expect_clean_tree(id);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* 3. An id change moves every consumer                                */
/* ------------------------------------------------------------------ */

/**
 * Resolution is live, not cached: the same consumers are asked before and
 * after an `id` is renamed, a `for` is retargeted, a form's id is renamed, and
 * an id is removed. A model that cached any of them would answer the first
 * question correctly and every later one from stale state.
 */
static void test_dom_id_change_propagates(void)
{
    static const char k_markup[] =
        "<form id=\"F\"><label for=\"C\">Caption</label></form>"
        "<input id=\"C\" form=\"F\">";
    robo_html_document_id_t id = dom_document_of(k_markup);
    robo_html_node_handle_t root = dom_root(id);
    robo_html_node_handle_t label = dom_find_tag(&root, ROBO_HTML_TAG_LABEL);
    robo_html_node_handle_t form = dom_find_tag(&root, ROBO_HTML_TAG_FORM);
    robo_html_node_handle_t input = dom_find_id(&root, "C");
    robo_html_node_handle_t found;
    robo_html_node_handle_t owner;
    robo_html_form_owner_source_t source = ROBO_HTML_FORM_OWNER_SOURCE_NONE;
    int from_attribute = -1;

    /* Baseline: all three agree on the input. */
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "C", 1u, &found, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&found, &input));
    ROBO_CHECK_EQ_INT(
        robo_html_element_label_target(&label, &found, &from_attribute, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(from_attribute, 1);
    ROBO_CHECK(robo_html_node_handle_equal(&found, &input));
    ROBO_CHECK_EQ_INT(
        robo_html_element_form_owner(&input, &owner, &source, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));
    ROBO_CHECK_EQ_INT((int)source,
                      (int)ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE);

    /* Rename the control: the fragment follows the new name immediately and
     * forgets the old one, while the label — whose `for` still says "C" —
     * now names nothing. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&input, "id", "D", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "C", 1u, &found, NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "D", 1u, &found, NULL),
        ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&found, &input));
    robo_html_node_handle_clear(&found);
    from_attribute = -1;
    ROBO_CHECK_EQ_INT(
        robo_html_element_label_target(&label, &found, &from_attribute, NULL),
        ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&found));
    ROBO_CHECK_EQ_INT(from_attribute, 0);

    /* Retarget the label: the same control is named again. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&label, "for", "D", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_element_label_target(&label, &found, &from_attribute, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(from_attribute, 1);
    ROBO_CHECK(robo_html_node_handle_equal(&found, &input));

    /* Rename the form: the `form` attribute now names nothing and the input
     * is outside every form, so the owner is gone and the source records
     * which rule answered nothing. Renaming it back restores the association,
     * because nothing was cached in the first place. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&form, "id", "G", NULL),
                      ROBO_OK);
    robo_html_node_handle_clear(&owner);
    source = ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE;
    ROBO_CHECK_EQ_INT(
        robo_html_element_form_owner(&input, &owner, &source, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&owner));
    ROBO_CHECK_EQ_INT((int)source, (int)ROBO_HTML_FORM_OWNER_SOURCE_NONE);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&form, "id", "F", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_element_form_owner(&input, &owner, &source, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));
    ROBO_CHECK_EQ_INT((int)source,
                      (int)ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE);

    /* Remove the control's id: the id consumers lose it, and the form owner —
     * which does not depend on the control's own id — does not. */
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&input, "id", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "D", 1u, &found, NULL),
        ROBO_NOT_FOUND);
    robo_html_node_handle_clear(&found);
    ROBO_CHECK_EQ_INT(
        robo_html_element_label_target(&label, &found, &from_attribute, NULL),
        ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&found));
    ROBO_CHECK_EQ_INT(
        robo_html_element_form_owner(&input, &owner, &source, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));

    /* A control that moves into the form keeps the same answer by a different
     * rule — the source field is the difference a caller can act on. While the
     * `form` attribute still resolves it wins; drop the attribute and the
     * ancestry rule takes over, with the same owner. */
    dom_detach(&input);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&form, &input, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_element_form_owner(&input, &owner, &source, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));
    ROBO_CHECK_EQ_INT((int)source,
                      (int)ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE);
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&input, "form",
                                                         NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_element_form_owner(&input, &owner, &source, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));
    ROBO_CHECK_EQ_INT((int)source, (int)ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);

    dom_expect_clean_tree(id);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* 4. Void elements: a parser rule, not a DOM rule                     */
/* ------------------------------------------------------------------ */

/**
 * The tree builder never gives a void element a child; the mutation API
 * decides by node *kind* rather than by tag, so an explicit append under
 * `<input>` succeeds. Both are asserted here, because the reader will expect
 * one of them to be the other, and a suite that tested only the parser half
 * would leave the DOM rule undocumented.
 */
static void test_dom_void_child_behaviour(void)
{
    static const char k_markup[] =
        "<div id=\"Host\">"
        "<input id=\"V\">"
        "<img id=\"I\" src=\"Assets/Photo.png\">"
        "tail"
        "<img id=\"Bare\">"
        "</div>";
    robo_html_document_id_t id = dom_document_of(k_markup);
    robo_html_node_handle_t root = dom_root(id);
    robo_html_node_handle_t host = dom_find_id(&root, "Host");
    robo_html_node_handle_t input = dom_find_id(&root, "V");
    robo_html_node_handle_t image = dom_find_id(&root, "I");
    robo_html_node_handle_t bare = dom_find_id(&root, "Bare");
    robo_html_node_handle_t text;
    size_t removed = 0u;

    /* The parser's side: a void element is never left open, so the markup
     * that follows it belongs to the enclosing element. */
    ROBO_CHECK_EQ_INT((int)dom_child_count(&input), 0);
    ROBO_CHECK_EQ_INT((int)dom_child_count(&image), 0);
    ROBO_CHECK_EQ_INT((int)dom_child_count(&host), 4);
    ROBO_CHECK_EQ_INT((int)dom_count_tags(&host, ROBO_HTML_TAG_IMG), 2);
    ROBO_CHECK_EQ_INT(robo_html_node_find_first(
                          &host, ROBO_HTML_NODE_FILTER_TEXT, NULL, NULL,
                          &text, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_text_is_text(&text));
    ROBO_CHECK_EQ_INT(robo_html_text_is_empty(&text), 0);

    /* A text node is a leaf by kind — the one kind the mutation API refuses
     * to make a parent, and the refusal is not-supported rather than a silent
     * append. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&text, &image, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)dom_child_count(&text), 0);

    /* The DOM's side: `<input>` is an element node, and elements hold
     * children, so moving a detached element under it is allowed — and the
     * tree stays auditable. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&host, &bare, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)dom_child_count(&host), 3);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&input, &bare, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)dom_child_count(&input), 1);
    ROBO_CHECK(robo_html_node_handle_equal(&bare, &bare));
    dom_expect_clean_tree(id);

    /* Detaching it again is a legal state, not a leak. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&input, &bare, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)dom_child_count(&input), 0);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&host, &bare, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)dom_child_count(&host), 4);
    dom_expect_clean_tree(id);

    /* Removing every child of a childless element succeeds and removes none. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_children(&input, &removed, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)removed, 0);

    /* The image state is derived from the element, so it follows the
     * attribute through the DOM: usable `src` is pending, none is NONE. */
    ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&image),
                      (int)ROBO_HTML_IMAGE_STATE_PENDING);
    ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&bare),
                      (int)ROBO_HTML_IMAGE_STATE_NONE);
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&bare, "src", "x.png", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&bare),
                      (int)ROBO_HTML_IMAGE_STATE_PENDING);
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&bare, "src", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&bare),
                      (int)ROBO_HTML_IMAGE_STATE_NONE);
    ROBO_CHECK_EQ_INT((int)robo_html_document_image_count(id), 0);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* 5. Recovery leaves a tree that audits clean                         */
/* ------------------------------------------------------------------ */

typedef struct dom_malformed_case {
    const char *name;
    const char *html;
} dom_malformed_case_t;

/**
 * The corpus: unclosed elements, misnested ones, a void element in the middle
 * of a form, a select with two options and no end tag, duplicate attributes,
 * an attribute that runs to EOF, stray end tags, a character reference, an
 * unsupported table structure, angle noise, and nothing at all.
 *
 * These are not "does it crash" inputs. Each is asked three questions: did the
 * build report success, is the resulting tree internally consistent (the
 * runtime's own whole-tree audit), and does re-parsing the same bytes produce
 * the same tree. A recovery that produced a *different* tree on the second
 * parse would be worse than a crash, because nothing would notice.
 */
static const dom_malformed_case_t k_malformed[] = {
    {"unclosed-div", "<div><span>"},
    {"paragraph-then-div", "<p><div>broken"},
    {"form-void-textarea", "<form><input><textarea>text"},
    {"select-options", "<select><option>One<option>Two"},
    {"duplicate-id", "<div id=\"a\" id=\"b\"></div>"},
    {"stray-end-tags", "</div></p>text"},
    {"table", "<table><tr><td>x</td></tr></table>"},
    {"heading-nesting", "<h1>One<h2>Two"},
    {"unclosed-anchor", "<a href=\"#x\">unclosed"},
    {"void-run", "<input><input><input>"},
    {"charrefs", "<p>&notarefsemicolon; &amp; &#x41;</p>"},
    {"misnested-inline", "<b><i>unclosed</b></i>"},
    {"unterminated-attr", "<div title=\"unterminated>"},
    {"angle-noise", "<<>>"},
    {"empty", ""}
};

static void test_dom_invariants_after_recovery(void)
{
    size_t i;

    for (i = 0u; i < sizeof(k_malformed) / sizeof(k_malformed[0]); i++) {
        const dom_malformed_case_t *c = &k_malformed[i];
        robo_html_document_id_t id = dom_new_document();
        robo_html_document_id_t again;
        char first[8192];
        char second[8192];

        dom_parse(id, c->html);
        dom_expect_clean_tree(id);
        dom_dump(id, first, sizeof(first));

        /* The same bytes in a second document: the same tree, byte for byte.
         * The failure message names the input, so a corpus regression is
         * identifiable from the log alone. */
        again = dom_document_of(c->html);
        dom_dump(again, second, sizeof(second));
        ROBO_CHECK_MSG(strcmp(first, second) == 0, c->name);
        ROBO_CHECK_EQ_INT((int)robo_html_document_node_count(id),
                          (int)robo_html_document_node_count(again));
        dom_expect_clean_tree(again);

        ROBO_CHECK_EQ_INT(robo_html_document_destroy(again, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    /* Two cases where the *content* is the claim, not only the shape: the
     * duplicate id is preserved as two records, and three consecutive void
     * inputs are three siblings rather than a nesting. */
    {
        robo_html_document_id_t id = dom_document_of("<div id=\"a\" id=\"b\">");
        robo_html_node_handle_t root = dom_root(id);
        robo_html_node_handle_t div = dom_find_tag(&root, ROBO_HTML_TAG_DIV);
        size_t count = 0u;

        ROBO_CHECK_EQ_INT(robo_html_element_attribute_count(&div, &count, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)count, 2);
        ROBO_CHECK_STR_EQ(dom_attr(&div, "id"), "a");
        dom_expect_clean_tree(id);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }
    {
        robo_html_document_id_t id = dom_document_of("<input><input><input>");
        robo_html_node_handle_t root = dom_root(id);
        robo_html_node_handle_t first;

        ROBO_CHECK_EQ_INT((int)dom_count_tags(&root, ROBO_HTML_TAG_INPUT), 3);
        first = dom_find_tag(&root, ROBO_HTML_TAG_INPUT);
        ROBO_CHECK_EQ_INT((int)dom_child_count(&first), 0);
        dom_expect_clean_tree(id);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }
}

/* ------------------------------------------------------------------ */
/* 6. Rebuilds are deterministic                                       */
/* ------------------------------------------------------------------ */

/**
 * The same source twice, a mutation, a reset, and the same source again: the
 * dump after the rebuild must equal the original byte for byte, not merely
 * "look similar". A document that leaked state out of a previous tree — an id
 * counter, an arena offset, a stale child link — shows up here and almost
 * nowhere else.
 */
static void test_dom_rebuild_is_deterministic(void)
{
    static const char k_markup[] =
        "<h1 id=\"Title\">Heading</h1><p>Body</p><ul><li>One</li>"
        "<li>Two</li></ul>";
    robo_html_document_id_t id = dom_document_of(k_markup);
    robo_html_node_handle_t root = dom_root(id);
    robo_html_node_handle_t list;
    robo_html_node_handle_t item;
    robo_html_element_view_t view;
    char first[4096];
    char second[4096];
    char third[4096];
    char mutated[4096];
    size_t nodes_first;

    dom_dump(id, first, sizeof(first));
    nodes_first = robo_html_document_node_count(id);

    /* Re-parse the identical source on the same document. */
    dom_parse(id, k_markup);
    dom_dump(id, second, sizeof(second));
    ROBO_CHECK_STR_EQ(second, first);
    ROBO_CHECK_EQ_INT((int)robo_html_document_node_count(id),
                      (int)nodes_first);
    dom_expect_clean_tree(id);

    /* A structural change must be visible in the dump, or the determinism
     * above would be vacuous. */
    root = dom_root(id);
    list = dom_find_tag(&root, ROBO_HTML_TAG_UL);
    item = dom_find_tag(&list, ROBO_HTML_TAG_LI);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&list, &item, NULL),
                      ROBO_OK);
    dom_dump(id, mutated, sizeof(mutated));
    ROBO_CHECK(strcmp(mutated, first) != 0);
    dom_expect_clean_tree(id);

    /* Reset and rebuild: the original tree again. */
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_is_empty(id), 1);
    dom_parse(id, k_markup);
    dom_dump(id, third, sizeof(third));
    ROBO_CHECK_STR_EQ(third, first);
    ROBO_CHECK_EQ_INT((int)robo_html_document_node_count(id),
                      (int)nodes_first);
    dom_expect_clean_tree(id);

    /* Handles from before the reset are stale — the generation guard is what
     * makes the old tree and the new one distinguishable. */
    memset(&view, 0, sizeof(view));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_stale(&item), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&item, &view, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* 7. Attribute mutation sequences and the state that depends on them   */
/* ------------------------------------------------------------------ */

/**
 * The sequence — set, read, replace, remove, verify absent — applied to every
 * family whose attributes are load-bearing, with the *dependent* state checked
 * at each step rather than only the attribute.
 *
 * The control-value block is the one worth reading: an explicit value survives
 * the removal of the `value` attribute it defaulted from, and disappears when
 * the override is cleared. A model with one value field would answer the first
 * half correctly and the second half wrongly, and this is the only place in
 * the DOM suites where that difference is visible against a live mutation of
 * the attribute itself.
 */
static void test_dom_attribute_mutation_sequences(void)
{
    static const char k_markup[] =
        "<div id=\"Box\" class=\"old\" title=\"t\">"
        "<a id=\"Link\" href=\"/Route\">go</a>"
        "<img id=\"Photo\" src=\"Assets/Photo.png\" alt=\"بديل\">"
        /* `type="checkbox"` is load-bearing: checkedness is modelled only for
         * an input whose effective type is checkable, so an untyped input's
         * `checked` attribute is preserved with nothing to report. */
        "<input id=\"Field\" type=\"checkbox\" value=\"declared\" checked>"
        "<select id=\"Pick\"><option id=\"Opt1\" value=\"one\">One</option>"
        "<option id=\"Opt2\" value=\"two\" selected>Two</option></select>"
        "</div>";
    robo_html_document_id_t id = dom_document_of(k_markup);
    robo_html_node_handle_t root = dom_root(id);
    robo_html_node_handle_t box = dom_find_id(&root, "Box");
    robo_html_node_handle_t link = dom_find_id(&root, "Link");
    robo_html_node_handle_t photo = dom_find_id(&root, "Photo");
    robo_html_node_handle_t field = dom_find_id(&root, "Field");
    robo_html_node_handle_t pick = dom_find_id(&root, "Pick");
    robo_html_node_handle_t option_one = dom_find_id(&root, "Opt1");
    robo_html_node_handle_t option_two = dom_find_id(&root, "Opt2");
    char buf[128];
    int declared = 0;
    int checked = 0;
    size_t children_before;

    /* A plain attribute: set, read, replace, remove, absent. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&box, "data-role", "panel", NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&box, "data-role"), 1);
    ROBO_CHECK_STR_EQ(dom_attr(&box, "data-role"), "panel");
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&box, "data-role", "card", NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(dom_attr(&box, "data-role"), "card");
    ROBO_CHECK_EQ_INT(
        robo_html_element_remove_attribute(&box, "data-role", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_has_attribute(&box, "data-role"), 0);
    ROBO_CHECK(dom_attr(&box, "data-role") == NULL);

    /* Attribute writes leave the children alone. */
    children_before = dom_child_count(&box);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&box, "class", "new",
                                                      NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(dom_attr(&box, "class"), "new");
    ROBO_CHECK_EQ_INT((int)dom_child_count(&box), (int)children_before);

    /* Unicode attribute values survive set/read byte for byte. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&box, "title",
                                                      "Ελληνικά — 日本語",
                                                      NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(dom_attr(&box, "title"), "Ελληνικά — 日本語");

    /* The anchor model reads the attribute that just changed, and reports
     * removal as absent rather than as an empty reference. */
    {
        robo_html_anchor_attributes_t attrs;

        memset(&attrs, 0, sizeof(attrs));
        ROBO_CHECK_EQ_INT(
            robo_html_element_anchor_attributes(&link, &attrs, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(attrs.href_present, 1);
        ROBO_CHECK_EQ_INT(attrs.href_usable, 1);
        ROBO_CHECK_EQ_INT(
            robo_html_element_set_attribute(&link, "href", "#Region", NULL),
            ROBO_OK);
        memset(&attrs, 0, sizeof(attrs));
        ROBO_CHECK_EQ_INT(
            robo_html_element_anchor_attributes(&link, &attrs, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)attrs.href_length, 7);
        ROBO_CHECK(memcmp(attrs.href, "#Region", 7u) == 0);
        ROBO_CHECK_EQ_INT(
            robo_html_element_remove_attribute(&link, "href", NULL), ROBO_OK);
        memset(&attrs, 0, sizeof(attrs));
        ROBO_CHECK_EQ_INT(
            robo_html_element_anchor_attributes(&link, &attrs, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(attrs.href_present, 0);
        ROBO_CHECK_EQ_INT(attrs.href_usable, 0);
        ROBO_CHECK(attrs.href == NULL);
    }

    /* The image state is derived, so it follows the attribute rather than
     * being stored: present → pending, removed → none, restored → pending. */
    ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&photo),
                      (int)ROBO_HTML_IMAGE_STATE_PENDING);
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&photo, "src", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&photo),
                      (int)ROBO_HTML_IMAGE_STATE_NONE);
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&photo, "src", "Assets/Other.png",
                                        NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&photo),
                      (int)ROBO_HTML_IMAGE_STATE_PENDING);
    ROBO_CHECK_STR_EQ(dom_attr(&photo, "alt"), "بديل");

    /* The declared value follows the attribute; the override does not. */
    dom_control_value(&field, buf, sizeof(buf), &declared);
    ROBO_CHECK_STR_EQ(buf, "declared");
    ROBO_CHECK_EQ_INT(declared, 1);
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&field, "value", "edited", NULL),
        ROBO_OK);
    dom_control_value(&field, buf, sizeof(buf), &declared);
    ROBO_CHECK_STR_EQ(buf, "edited");
    ROBO_CHECK_EQ_INT(declared, 1);

    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&field, "override", 8u, NULL),
        ROBO_OK);
    dom_control_value(&field, buf, sizeof(buf), &declared);
    ROBO_CHECK_STR_EQ(buf, "override");
    ROBO_CHECK_EQ_INT(declared, 0);

    ROBO_CHECK_EQ_INT(
        robo_html_element_remove_attribute(&field, "value", NULL), ROBO_OK);
    /* The override survives the attribute it defaulted from. */
    dom_control_value(&field, buf, sizeof(buf), &declared);
    ROBO_CHECK_STR_EQ(buf, "override");
    ROBO_CHECK_EQ_INT(declared, 0);

    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_value(&field, NULL),
                      ROBO_OK);
    /* With no override and no attribute the value is empty — and it is the
     * declared one, because nothing overrides it. */
    dom_control_value(&field, buf, sizeof(buf), &declared);
    ROBO_CHECK_STR_EQ(buf, "");
    ROBO_CHECK_EQ_INT(declared, 1);

    /* Checkedness is its own sequence, and its own attribute. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_control_checked(&field, &checked, &declared, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(checked, 1);
    ROBO_CHECK_EQ_INT(declared, 1);
    ROBO_CHECK_EQ_INT(robo_html_element_set_control_checked(&field, 0, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_element_control_checked(&field, &checked, &declared, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(checked, 0);
    ROBO_CHECK_EQ_INT(declared, 0);
    ROBO_CHECK_EQ_INT(
        robo_html_element_remove_attribute(&field, "checked", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_element_control_checked(&field, &checked, &declared, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(checked, 0);
    ROBO_CHECK_EQ_INT(declared, 0);
    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_checked(&field, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_element_control_checked(&field, &checked, &declared, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(checked, 0); /* the attribute is gone */
    ROBO_CHECK_EQ_INT(declared, 1);

    /* Dropping `selected` from the second option moves the select back to the
     * first — the deterministic rule, exercised through the attribute. */
    {
        robo_html_node_handle_t selected;
        int explicit_value = 0;
        size_t length = 0u;

        ROBO_CHECK_EQ_INT(
            robo_html_element_select_selected_option(&pick, &selected, NULL),
            ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&selected, &option_two));
        ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&option_two,
                                                             "selected", NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_element_select_selected_option(&pick, &selected, NULL),
            ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&selected, &option_one));
        ROBO_CHECK_EQ_INT(robo_html_element_option_value(
                              &selected, buf, sizeof(buf), &length,
                              &explicit_value, NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "one");
        ROBO_CHECK_EQ_INT(explicit_value, 1);
    }

    dom_expect_clean_tree(id);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* 8. Depth                                                            */
/* ------------------------------------------------------------------ */

#define DOM_DEEP_LEVELS 200u

/**
 * A chain 200 levels deep, built through the construction seam so the
 * measurements are the tree's rather than the parser's.
 *
 * Detaching a node in the middle is the interesting half: the whole subtree
 * below it travels with it, which is asserted by the depth of the deepest node
 * before and after re-attachment — a number that changes by exactly the number
 * of levels the subtree moved. The pre-order walk is asked to confirm the
 * shape independently: on a pure chain the k-th element visited sits at depth
 * k, and nothing else does.
 */
static void test_dom_deep_structure_stress(void)
{
    robo_html_document_id_t id = dom_new_document();
    robo_html_node_handle_t root = dom_root(id);
    robo_html_node_handle_t node = root;
    robo_html_node_handle_t parent_of_middle = root;
    robo_html_node_handle_t middle;
    robo_html_node_handle_t leaf;
    dom_chain_record_t chain;
    size_t i;

    for (i = 0u; i < DOM_DEEP_LEVELS; i++) {
        robo_html_node_handle_t parent = node;

        if (i + 1u == DOM_DEEP_LEVELS / 2u) {
            parent_of_middle = node;
        }
        node = dom_create_under(id, &parent, "div");
        if (i + 1u == DOM_DEEP_LEVELS / 2u) {
            middle = node;
        }
    }
    leaf = node;

    ROBO_CHECK_EQ_INT((int)dom_depth_of(&leaf), (int)DOM_DEEP_LEVELS);
    ROBO_CHECK_EQ_INT((int)dom_depth_of(&middle),
                      (int)(DOM_DEEP_LEVELS / 2u));
    ROBO_CHECK_EQ_INT((int)dom_count_tags(&root, ROBO_HTML_TAG_DIV),
                      (int)DOM_DEEP_LEVELS);
    chain.visited = 0u;
    chain.mismatches = 0u;
    ROBO_CHECK_EQ_INT(
        robo_html_node_walk_descendants(&root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                        dom_chain_visitor, &chain, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)chain.visited, (int)DOM_DEEP_LEVELS);
    ROBO_CHECK_EQ_INT((int)chain.mismatches, 0);
    dom_expect_clean_tree(id);

    /* Detach the middle: a hundred levels leave the root path and become one
     * detached subtree, which the audit reports as legal. */
    ROBO_CHECK_EQ_INT(
        robo_html_node_remove_child(&parent_of_middle, &middle, NULL),
        ROBO_OK);
    {
        robo_html_tree_report_t report;

        memset(&report, 0, sizeof(report));
        ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                          ROBO_OK);
        ROBO_CHECK(report.ok);
        ROBO_CHECK_EQ_INT((int)report.detached, 1);
    }

    /* Re-attach it at the top: the deepest node is now one level below the
     * root plus the hundred levels the subtree brought with it. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &middle, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)dom_depth_of(&middle), 1);
    ROBO_CHECK_EQ_INT((int)dom_depth_of(&leaf),
                      1 + (int)(DOM_DEEP_LEVELS - DOM_DEEP_LEVELS / 2u));
    ROBO_CHECK_EQ_INT((int)dom_count_tags(&root, ROBO_HTML_TAG_DIV),
                      (int)DOM_DEEP_LEVELS);
    dom_expect_clean_tree(id);

    /* Depth survives a document that re-parses deeper markup too. */
    {
        robo_html_document_id_t parsed = dom_new_document();
        char markup[2048];
        size_t at = 0u;
        size_t levels = 100u;
        robo_html_node_handle_t proot;
        robo_html_node_handle_t deepest;

        for (i = 0u; i < levels; i++) {
            at += (size_t)snprintf(markup + at, sizeof(markup) - at, "<div>");
        }
        for (i = 0u; i < levels; i++) {
            at += (size_t)snprintf(markup + at, sizeof(markup) - at, "</div>");
        }
        dom_parse(parsed, markup);
        proot = dom_root(parsed);
        deepest = dom_find_tag(&proot, ROBO_HTML_TAG_DIV);
        {
            size_t depth = 1u;
            robo_html_node_handle_t child;

            while (robo_html_node_first_child(&deepest, &child, NULL) ==
                   ROBO_OK) {
                if (!robo_html_element_is_tag(&child, ROBO_HTML_TAG_DIV)) {
                    break;
                }
                deepest = child;
                depth++;
            }
            ROBO_CHECK_EQ_INT((int)depth, (int)levels);
        }
        ROBO_CHECK_EQ_INT((int)dom_count_tags(&proot, ROBO_HTML_TAG_DIV),
                          (int)levels);
        dom_expect_clean_tree(parsed);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(parsed, NULL), ROBO_OK);
    }

    /* Teardown leaves every handle stale. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_stale(&leaf), 1);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_stale(&middle), 1);
}

/* ------------------------------------------------------------------ */
/* 9. Width                                                            */
/* ------------------------------------------------------------------ */

#define DOM_WIDE_SIBLINGS 300u

/**
 * 300 siblings: creation order is document order, a removal in the middle
 * closes the gap without reordering anything else, and an insert lands exactly
 * where it was asked to. The first, middle and last children are compared by
 * identity rather than by count, because a count-only assertion would pass on
 * a tree whose order had been reversed.
 */
static void test_dom_wide_structure_stress(void)
{
    robo_html_document_id_t id = dom_new_document();
    robo_html_node_handle_t root = dom_root(id);
    robo_html_node_handle_t container = dom_create_under(id, &root, "div");
    robo_html_node_handle_t spans[DOM_WIDE_SIBLINGS];
    robo_html_node_handle_t got;
    size_t i;
    size_t removed_total = 0u;

    for (i = 0u; i < DOM_WIDE_SIBLINGS; i++) {
        spans[i] = dom_create_under(id, &container, "span");
    }
    ROBO_CHECK_EQ_INT((int)dom_child_count(&container),
                      (int)DOM_WIDE_SIBLINGS);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&container, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &spans[0]));
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&container, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &spans[DOM_WIDE_SIBLINGS - 1u]));
    ROBO_CHECK_EQ_INT(
        robo_html_node_child_at(&container, DOM_WIDE_SIBLINGS / 2u, &got, NULL),
        ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got,
                                           &spans[DOM_WIDE_SIBLINGS / 2u]));
    dom_expect_clean_tree(id);

    /* Remove every other one. The remaining order must be the original
     * subsequence, which is checked by identity at three points. */
    for (i = 1u; i < DOM_WIDE_SIBLINGS; i += 2u) {
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&container, &spans[i],
                                                      NULL),
                          ROBO_OK);
        removed_total++;
    }
    ROBO_CHECK_EQ_INT((int)removed_total, (int)(DOM_WIDE_SIBLINGS / 2u));
    ROBO_CHECK_EQ_INT((int)dom_child_count(&container),
                      (int)(DOM_WIDE_SIBLINGS / 2u));
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&container, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &spans[0]));
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&container, 1u, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &spans[2]));
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&container, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(
        &got, &spans[DOM_WIDE_SIBLINGS - 2u]));
    dom_expect_clean_tree(id);

    /* Put one back, in the position it came from. */
    ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&container, &spans[1],
                                                   &spans[2], NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)dom_child_count(&container),
                      (int)(DOM_WIDE_SIBLINGS / 2u) + 1);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&container, 1u, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &spans[1]));
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&container, 2u, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &spans[2]));
    dom_expect_clean_tree(id);

    /* Removing the whole child list at once is the bulk path, and it reports
     * how many it detached. */
    ROBO_CHECK_EQ_INT(
        robo_html_node_remove_children(&container, &removed_total, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)removed_total,
                      (int)(DOM_WIDE_SIBLINGS / 2u) + 1);
    ROBO_CHECK_EQ_INT((int)dom_child_count(&container), 0);
    dom_expect_clean_tree(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* 10. Unicode matrix                                                  */
/* ------------------------------------------------------------------ */

/**
 * Thirteen script families through the whole pipeline: parse → text →
 * attribute → programmatic text node → mutation. Every comparison is
 * byte-exact; a length check accompanies each one, because a truncation and a
 * substitution can both leave a "contains the sample" assertion satisfied.
 */
static void test_dom_unicode_matrix(void)
{
    const dom_unicode_sample_t *samples;
    size_t count = 0u;
    size_t i;

    samples = dom_unicode_samples(&count);
    ROBO_CHECK(count >= 13u);

    for (i = 0u; i < count; i++) {
        char markup[1024];
        const char *text = samples[i].text;
        size_t text_length = strlen(text);
        robo_html_document_id_t id;
        robo_html_document_id_t built;
        robo_html_node_handle_t root;
        robo_html_node_handle_t holder;
        robo_html_node_handle_t node;
        robo_html_node_handle_t section;
        robo_html_node_handle_t text_node;
        char got[512];
        size_t content_length = 0u;

        ROBO_CHECK(snprintf(markup, sizeof(markup),
                            "<section id=\"Holder\"></section>"
                            "<div id=\"Node\" title=\"%s\">%s</div>",
                            text, text) > 0);

        id = dom_document_of(markup);
        root = dom_root(id);
        node = dom_find_id(&root, "Node");
        holder = dom_find_id(&root, "Holder");

        dom_text(&node, got, sizeof(got));
        ROBO_CHECK_MSG(strcmp(got, text) == 0, samples[i].name);
        ROBO_CHECK_STR_EQ(dom_attr(&node, "title"), text);
        ROBO_CHECK_EQ_INT((int)strlen(dom_attr(&node, "title")),
                          (int)text_length);

        /* Move the parsed element: mutation must carry the bytes. Detached
         * from its *real* parent, not from the document root — a parsed
         * element hangs under the implied `<body>`, so naming the root would
         * be a not-found rather than a move. */
        dom_detach(&node);
        ROBO_CHECK_EQ_INT(robo_html_node_append_child(&holder, &node, NULL),
                          ROBO_OK);
        dom_text(&node, got, sizeof(got));
        ROBO_CHECK_MSG(strcmp(got, text) == 0, samples[i].name);
        dom_expect_clean_tree(id);

        /* A programmatic text node on a hand-built document carries the same
         * bytes — the construction seam copies, it does not normalize. */
        built = dom_new_document();
        {
            robo_html_node_handle_t built_root = dom_root(built);

            section = dom_create_under(built, &built_root, "section");
            text_node = dom_create_text(built, &section, text);
            ROBO_CHECK_EQ_INT((int)robo_html_text_length(&text_node),
                              (int)text_length);
            ROBO_CHECK_STR_EQ(robo_html_text_content(&text_node,
                                                     &content_length),
                              text);
            ROBO_CHECK_EQ_INT((int)content_length, (int)text_length);
            ROBO_CHECK_EQ_INT(robo_html_text_is_empty(&text_node), 0);
            dom_text(&section, got, sizeof(got));
            ROBO_CHECK_MSG(strcmp(got, text) == 0, samples[i].name);
            dom_expect_clean_tree(built);
        }

        ROBO_CHECK_EQ_INT(robo_html_document_destroy(built, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    /* An empty text node is the one case where "no bytes" is the content, and
     * it is a legal value rather than an error. */
    {
        robo_html_document_id_t built = dom_new_document();
        robo_html_node_handle_t built_root = dom_root(built);
        robo_html_node_handle_t section = dom_create_under(built, &built_root,
                                                           "p");
        robo_html_node_handle_t empty = dom_create_text(built, &section, "");

        ROBO_CHECK_EQ_INT(robo_html_text_is_empty(&empty), 1);
        ROBO_CHECK_EQ_INT((int)robo_html_text_length(&empty), 0);
        ROBO_CHECK_STR_EQ(robo_html_text_content(&empty, NULL), "");
        dom_expect_clean_tree(built);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(built, NULL), ROBO_OK);
    }
}

/**
 * The same Unicode ids through the three consumers. A model that compared ids
 * byte-wise is the only kind that can pass this; one that folded, truncated or
 * ASCII-filtered the id would answer the ASCII case and fail here.
 */
static void test_dom_unicode_id_consumers(void)
{
    static const char k_arabic[] = "مرحبا";
    static const char k_devanagari[] = "नमस्ते";
    static const char k_korean[] = "안녕하세요";
    char markup[512];
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t found;
    robo_html_node_handle_t owner;

    ROBO_CHECK(snprintf(markup, sizeof(markup),
                        "<div id=\"%s\">x</div>"
                        "<label for=\"%s\">L</label><input id=\"%s\">"
                        "<form id=\"%s\"></form><input form=\"%s\">",
                        k_arabic, k_devanagari, k_devanagari, k_korean,
                        k_korean) > 0);
    id = dom_document_of(markup);
    root = dom_root(id);

    /* A fragment naming a Unicode id resolves to it... */
    ROBO_CHECK_EQ_INT(robo_html_navigation_find_fragment(
                          id, k_arabic, strlen(k_arabic), &found, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&found, ROBO_HTML_TAG_DIV));

    /* ...and a name that differs in one byte does not. */
    {
        char near_miss[64];

        ROBO_CHECK(snprintf(near_miss, sizeof(near_miss), "%sX", k_arabic) > 0);
        ROBO_CHECK_EQ_INT(robo_html_navigation_find_fragment(
                              id, near_miss, strlen(near_miss), &found, NULL),
                          ROBO_NOT_FOUND);
    }

    /* A label resolves a Devanagari `for`, and the wrapping rule is the same
     * comparison. */
    {
        robo_html_node_handle_t label = dom_find_tag(&root,
                                                     ROBO_HTML_TAG_LABEL);
        int from_attribute = -1;

        ROBO_CHECK_EQ_INT(
            robo_html_element_label_target(&label, &found, &from_attribute,
                                           NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(from_attribute, 1);
        ROBO_CHECK(robo_html_element_is_tag(&found, ROBO_HTML_TAG_INPUT));
    }

    /* A control resolves a Korean `form` attribute. */
    {
        robo_html_node_handle_t form = dom_find_tag(&root, ROBO_HTML_TAG_FORM);
        robo_html_form_owner_source_t source = ROBO_HTML_FORM_OWNER_SOURCE_NONE;
        robo_html_node_handle_t control;

        /* The Korean id names *two* elements: the form itself, and through
         * `form=` the input. An id lookup answers with the first in document
         * order — the form — so the control is found by the attribute that
         * distinguishes it. */
        control = dom_find_by_attr(&root, "form", k_korean);
        ROBO_CHECK(robo_html_element_is_tag(&control, ROBO_HTML_TAG_INPUT));
        ROBO_CHECK_EQ_INT(
            robo_html_element_form_owner(&control, &owner, &source, NULL),
            ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&owner, &form));
        ROBO_CHECK_EQ_INT((int)source,
                          (int)ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE);
    }

    dom_expect_clean_tree(id);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* 11. Lifecycle churn                                                 */
/* ------------------------------------------------------------------ */

#define DOM_CHURN_CYCLES 24u

/**
 * Repeated whole-document lifetimes, with the debug allocator's live count
 * checked before and after.
 *
 * One warm-up cycle runs before the baseline is taken: the first document a
 * process creates may leave the module's own tables sized for it, and
 * counting that as a leak would make the test fail for the wrong reason. After
 * the warm-up, the live count must return to exactly where it started — for a
 * test process this is the closest thing to a leak check that exists, and it
 * catches a document whose arena, control table or attachment table is not
 * released on the destroy path.
 *
 * The observable half of the test runs in every configuration: the live
 * document count returns to its start, and handles from a destroyed document
 * are stale rather than silently reusable.
 */
static void test_dom_lifecycle_churn(void)
{
    static const char k_markup[] =
        "<div id=\"Box\"><a href=\"#Target\">go</a>"
        "<img src=\"Assets/Photo.png\" alt=\"a\">"
        "<form id=\"F2\"><label for=\"C2\">L</label>"
        "<input id=\"C2\" type=\"checkbox\" value=\"v\" checked>"
        "<select id=\"S2\"><option value=\"o\">O</option></select>"
        "</form></div>";
    size_t docs_before = robo_html_live_document_count();
    int debug_was_enabled = robo_mem_debug_is_enabled();
    size_t baseline = 0u;
    size_t i;

    if (!debug_was_enabled) {
        robo_mem_debug_enable();
    }

    /* Warm-up: one full lifetime before the baseline is measured. */
    {
        robo_html_document_id_t warm = dom_document_of(k_markup);
        robo_html_node_handle_t warm_root = dom_root(warm);
        robo_html_node_handle_t field = dom_find_id(&warm_root, "C2");

        ROBO_CHECK_EQ_INT(
            robo_html_element_set_control_value(&field, "warm", 4u, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(warm, NULL), ROBO_OK);
    }

    baseline = robo_mem_debug_live_count();

    for (i = 0u; i < DOM_CHURN_CYCLES; i++) {
        robo_html_document_id_t id = dom_document_of(k_markup);
        robo_html_node_handle_t root = dom_root(id);
        robo_html_node_handle_t box = dom_find_id(&root, "Box");
        robo_html_node_handle_t anchor = dom_find_tag(&root, ROBO_HTML_TAG_A);
        robo_html_node_handle_t field = dom_find_id(&root, "C2");
        robo_html_node_handle_t option;
        char buf[32];
        int declared = 0;

        /* Touch every subsystem that owns per-document state: control state
         * (a heap copy), image attachment bookkeeping, option ordering, and a
         * structural mutation. */
        ROBO_CHECK_EQ_INT(
            robo_html_element_set_control_value(&field, "cycled", 6u, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_set_control_checked(&field, 0,
                                                                NULL),
                          ROBO_OK);
        robo_html_element_control_value(&field, buf, sizeof(buf), NULL,
                                        &declared, NULL);
        ROBO_CHECK_EQ_INT(
            robo_html_navigation_find_fragment(id, "C2", 2u, &option, NULL),
            ROBO_OK);
        (void)anchor;
        ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&box, "data-i", "1",
                                                          NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&box, "data-i",
                                                             NULL),
                          ROBO_OK);

        dom_expect_clean_tree(id);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_handle_is_stale(&box), 1);
        ROBO_CHECK_EQ_INT(robo_html_node_handle_is_stale(&field), 1);
        /* A destroyed document is not merely empty: it is gone. */
        ROBO_CHECK_EQ_INT(robo_html_document_is_live(id), 0);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);
    }

    ROBO_CHECK_EQ_INT((int)robo_html_live_document_count(), (int)docs_before);

#if ROBO_MEM_DEBUG
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
#endif

    if (!debug_was_enabled) {
        robo_mem_debug_disable();
    }
}

/* ------------------------------------------------------------------ */
/* 12. Feature integration                                             */
/* ------------------------------------------------------------------ */

/**
 * One document that uses every subsystem the runtime has, checked as a whole:
 * the capability flags, the counts, all four element-side models, traversal
 * order, a mutation that changes that order, and teardown.
 *
 * `events` is asserted to be 0 on purpose. The flags are a promise about what
 * this build can do, and a flag that claimed the DOM-events phase before it
 * exists would be the one lie in an otherwise verifiable set.
 */
static void test_dom_feature_integration(void)
{
    static const char k_markup[] =
        "<!DOCTYPE html>\n"
        "<html>\n<head><title>Integration</title></head>\n"
        "<body>\n"
        "<h1 id=\"Title\">Heading</h1>\n"
        "<p class=\"Lead\">Body <strong>bold</strong> text</p>\n"
        "<a id=\"Link\" href=\"#Title\">to title</a>\n"
        "<img id=\"Photo\" src=\"Assets/Photo.png\" alt=\"Photo\">\n"
        "<form id=\"Editor\" action=\"/Save\" method=\"post\">\n"
        "<label for=\"Name\">Name</label>\n"
        "<input id=\"Name\" type=\"text\" name=\"name\" value=\"Ada\">\n"
        "<input id=\"Agree\" type=\"checkbox\" name=\"agree\" checked>\n"
        "<textarea id=\"Notes\" name=\"notes\">Hello</textarea>\n"
        "<select id=\"Ring\" name=\"ring\">"
        "<option value=\"a\">A</option>"
        "<option value=\"b\" selected>B</option></select>\n"
        "<button type=\"submit\">Save</button>\n"
        "</form>\n"
        "<p>امتحان</p>\n"
        "</body></html>";
    robo_html_document_id_t id = dom_document_of(k_markup);
    robo_html_node_handle_t root = dom_root(id);
    robo_html_dom_capabilities_t caps;
    dom_order_record_t order;
    char buf[64];
    int declared = 0;
    int checked = 0;

    /* The build's promises, all at once. */
    memset(&caps, 0, sizeof(caps));
    ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.tree_building, 1);
    /* `dom_nodes` stays 0 by design: it is the flag for a *complete* node DOM
     * surface, and this build has one model per aspect (attributes, text,
     * traversal, mutation) rather than the whole. Asserting 0 keeps the flag
     * honest rather than aspirational. */
    ROBO_CHECK_EQ_INT(caps.dom_nodes, 0);
    ROBO_CHECK_EQ_INT(caps.attributes, 1);
    ROBO_CHECK_EQ_INT(caps.text_nodes, 1);
    ROBO_CHECK_EQ_INT(caps.traversal, 1);
    ROBO_CHECK_EQ_INT(caps.mutation, 1);
    ROBO_CHECK_EQ_INT(caps.element_tags, 1);
    ROBO_CHECK_EQ_INT(caps.images, 1);
    ROBO_CHECK_EQ_INT(caps.links, 1);
    ROBO_CHECK_EQ_INT(caps.forms, 1);
    ROBO_CHECK_EQ_INT(caps.events, 0); /* not this phase, and not claimed */

    /* The tree is what the source said. */
    ROBO_CHECK_EQ_INT((int)dom_count_tags(&root, ROBO_HTML_TAG_H1), 1);
    ROBO_CHECK_EQ_INT((int)dom_count_tags(&root, ROBO_HTML_TAG_P), 2);
    ROBO_CHECK_EQ_INT((int)dom_count_tags(&root, ROBO_HTML_TAG_FORM), 1);
    ROBO_CHECK_EQ_INT((int)dom_count_tags(&root, ROBO_HTML_TAG_INPUT), 2);
    ROBO_CHECK_EQ_INT((int)dom_count_tags(&root, ROBO_HTML_TAG_LABEL), 1);
    ROBO_CHECK(robo_html_document_node_count(id) > 20u);

    /* Every subsystem answers about this document. */
    {
        robo_html_node_handle_t heading = dom_find_id(&root, "Title");
        robo_html_node_handle_t found;
        robo_html_node_handle_t label = dom_find_tag(&root,
                                                     ROBO_HTML_TAG_LABEL);
        robo_html_node_handle_t name = dom_find_id(&root, "Name");
        robo_html_node_handle_t agree = dom_find_id(&root, "Agree");
        robo_html_node_handle_t ring = dom_find_id(&root, "Ring");
        robo_html_node_handle_t selected;
        robo_html_node_handle_t owner;
        robo_html_node_handle_t photo = dom_find_id(&root, "Photo");
        robo_html_node_handle_t link = dom_find_id(&root, "Link");
        robo_html_anchor_attributes_t anchor;
        robo_html_form_owner_source_t source = ROBO_HTML_FORM_OWNER_SOURCE_NONE;
        int from_attribute = -1;
        char text[256];

        /* links: a fragment naming a real element in this document. */
        ROBO_CHECK_EQ_INT(
            robo_html_navigation_find_fragment(id, "Title", 5u, &found, NULL),
            ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&found, &heading));
        memset(&anchor, 0, sizeof(anchor));
        ROBO_CHECK_EQ_INT(
            robo_html_element_anchor_attributes(&link, &anchor, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(anchor.href_usable, 1);
        ROBO_CHECK(memcmp(anchor.href, "#Title", 6u) == 0);

        /* images: a declared but unresolved resource is PENDING, not an
         * error, and nothing was fetched. */
        ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&photo),
                          (int)ROBO_HTML_IMAGE_STATE_PENDING);

        /* forms: label → control, control → form, declared state. */
        ROBO_CHECK_EQ_INT(
            robo_html_element_label_target(&label, &found, &from_attribute,
                                           NULL),
            ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&found, &name));
        ROBO_CHECK_EQ_INT(
            robo_html_element_form_owner(&name, &owner, &source, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT((int)source,
                          (int)ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR);
        ROBO_CHECK(robo_html_element_is_tag(&owner, ROBO_HTML_TAG_FORM));
        dom_control_value(&name, buf, sizeof(buf), &declared);
        ROBO_CHECK_STR_EQ(buf, "Ada");
        ROBO_CHECK_EQ_INT(declared, 1);
        ROBO_CHECK_EQ_INT(
            robo_html_element_control_checked(&agree, &checked, &declared,
                                              NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(checked, 1);
        ROBO_CHECK_EQ_INT(
            robo_html_element_select_selected_option(&ring, &selected, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_html_element_option_value(
                              &selected, buf, sizeof(buf), NULL, NULL, NULL),
                          (int)ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "b");

        /* Attribute-driven lookup: the same descendant walk that id lookup
         * uses, with a different predicate, reaches the same nodes a reader
         * would — the submit button by `type` and the chosen option by
         * `value` (which pins the *attribute*, not the selection logic that
         * was just asserted above). */
        {
            robo_html_node_handle_t submit = dom_find_by_attr(&root, "type",
                                                              "submit");
            robo_html_node_handle_t option_b = dom_find_by_attr(&root, "value",
                                                                "b");

            size_t attributes = 0u;

            ROBO_CHECK(robo_html_element_is_tag(&submit, ROBO_HTML_TAG_BUTTON));
            ROBO_CHECK(robo_html_node_handle_equal(&option_b, &selected));
            ROBO_CHECK_EQ_INT(
                robo_html_element_attribute_count(&submit, &attributes, NULL),
                ROBO_OK);
            ROBO_CHECK_EQ_INT((int)attributes, 1);
        }

        /* the text model: the last paragraph is not Latin. */
        {
            robo_html_node_handle_t paragraphs[2];
            size_t seen = 0u;
            robo_html_node_handle_t walk = dom_find_tag(&root,
                                                        ROBO_HTML_TAG_P);

            paragraphs[seen++] = walk;
            while (seen < 2u && dom_next_tag_sibling(&walk, ROBO_HTML_TAG_P,
                                                     &walk)) {
                paragraphs[seen++] = walk;
            }
            ROBO_CHECK_EQ_INT((int)seen, 2);
            dom_text(&paragraphs[1], text, sizeof(text));
            ROBO_CHECK_STR_EQ(text, "امتحان");
        }
    }

    /* Document order, observed rather than assumed: the heading precedes the
     * form, and the first paragraph precedes it too. */
    memset(&order, 0, sizeof(order));
    order.heading = -1;
    order.form = -1;
    order.paragraph = -1;
    ROBO_CHECK_EQ_INT(
        robo_html_node_walk_descendants(&root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                        dom_order_visitor, &order, NULL),
        ROBO_OK);
    ROBO_CHECK(order.heading >= 0);
    ROBO_CHECK(order.form > order.heading);
    ROBO_CHECK(order.paragraph > order.heading);
    ROBO_CHECK(order.paragraph < order.form);

    /* A mutation changes that order, and the dump says so. */
    {
        robo_html_node_handle_t form = dom_find_tag(&root,
                                                    ROBO_HTML_TAG_FORM);
        robo_html_node_handle_t heading = dom_find_id(&root, "Title");
        robo_html_node_handle_t body = dom_find_tag(&root, ROBO_HTML_TAG_BODY);
        char before[4096];
        char after[4096];

        dom_dump(id, before, sizeof(before));
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&body, &form, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_node_insert_before(&body, &form, &heading, NULL),
            ROBO_OK);
        dom_dump(id, after, sizeof(after));
        ROBO_CHECK(strcmp(before, after) != 0);

        memset(&order, 0, sizeof(order));
        order.heading = -1;
        order.form = -1;
        order.paragraph = -1;
        ROBO_CHECK_EQ_INT(
            robo_html_node_walk_descendants(&root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                            dom_order_visitor, &order, NULL),
            ROBO_OK);
        ROBO_CHECK(order.form < order.heading);
        dom_expect_clean_tree(id);
    }

    /* Teardown: every handle goes stale, and destroying twice is a not-found
     * rather than a second free. */
    {
        robo_html_node_handle_t heading = dom_find_id(&root, "Title");

        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_handle_is_stale(&heading), 1);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_html_dom(void)
{
    robo_test_register("html.dom.document_isolation",
                       test_dom_document_isolation);
    robo_test_register("html.dom.id_resolution_agrees",
                       test_dom_id_resolution_agrees);
    robo_test_register("html.dom.id_change_propagates",
                       test_dom_id_change_propagates);
    robo_test_register("html.dom.void_child_behaviour",
                       test_dom_void_child_behaviour);
    robo_test_register("html.dom.invariants_after_recovery",
                       test_dom_invariants_after_recovery);
    robo_test_register("html.dom.rebuild_is_deterministic",
                       test_dom_rebuild_is_deterministic);
    robo_test_register("html.dom.attribute_mutation_sequences",
                       test_dom_attribute_mutation_sequences);
    robo_test_register("html.dom.deep_structure_stress",
                       test_dom_deep_structure_stress);
    robo_test_register("html.dom.wide_structure_stress",
                       test_dom_wide_structure_stress);
    robo_test_register("html.dom.unicode_matrix", test_dom_unicode_matrix);
    robo_test_register("html.dom.unicode_id_consumers",
                       test_dom_unicode_id_consumers);
    robo_test_register("html.dom.lifecycle_churn", test_dom_lifecycle_churn);
    robo_test_register("html.dom.feature_integration",
                       test_dom_feature_integration);
}
