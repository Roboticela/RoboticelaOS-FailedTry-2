/*
 * RoboHTML DOM test support (Phase 8.15).
 *
 * Shared fixtures for the DOM-level suites — the ones that test the model as
 * a *system* (documents, isolation, id resolution, invariants, determinism)
 * rather than one phase's surface in isolation.
 *
 * It is deliberately not a framework: it adds no runner, no registration, no
 * reporter and no assertion vocabulary. Every helper below does one of three
 * things the per-phase suites each re-implemented — begin the fixture, parse
 * a document, and look something up — and everything is `static inline`, so a
 * suite that includes this header pays only for the helpers it calls and no
 * object is emitted for the ones it does not. `-Wall -Wextra -Werror` is on
 * for the host build, which is why `static inline` (rather than plain
 * `static`) matters here.
 *
 * The helpers parse real HTML through the real tokenizer and tree builder.
 * A helper that hand-built nodes would let a suite pass while the parser
 * integration it claims to test was broken.
 */

#ifndef ROBO_HTML_DOM_TEST_SUPPORT_H
#define ROBO_HTML_DOM_TEST_SUPPORT_H

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

/** Bring the foundation up. Idempotent; every suite calls it first. */
static inline void dom_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

/** A new empty document, asserted created. */
static inline robo_html_document_id_t dom_new_document(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    dom_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/**
 * Parse @p text into @p id through the real source → tokenizer → tree
 * builder path, asserting success.
 *
 * Success is asserted rather than tolerated: every input these suites feed it
 * is expected to parse. Malformed *markup* is not a failing build — the tree
 * builder recovers — so a case that means "this recovers" passes through here
 * too, and the recovery itself is asserted by the caller.
 */
static inline void dom_parse(robo_html_document_id_t id, const char *text)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);
}

/** Create a document and parse @p text into it in one step. */
static inline robo_html_document_id_t dom_document_of(const char *text)
{
    robo_html_document_id_t id = dom_new_document();

    dom_parse(id, text);
    return id;
}

/** The document root, asserted well-formed. */
static inline robo_html_node_handle_t dom_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

/** One predicate argument: a tag to match. */
typedef struct dom_tag_query {
    robo_html_tag_t tag;
} dom_tag_query_t;

static inline int dom_predicate_tag(const robo_html_node_view_t *view,
                                    void *user)
{
    const dom_tag_query_t *q = (const dom_tag_query_t *)user;

    return robo_html_element_is_tag(&view->handle, q->tag);
}

/**
 * First element with tag @p tag at or below @p start.
 * @return 1 and a filled @p out when found; 0 (and a cleared @p out) when not.
 */
static inline int dom_try_find_tag(const robo_html_node_handle_t *start,
                                   robo_html_tag_t tag,
                                   robo_html_node_handle_t *out)
{
    dom_tag_query_t q;
    robo_result_t r;

    q.tag = tag;
    robo_html_node_handle_clear(out);
    r = robo_html_node_find_first(start, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  dom_predicate_tag, &q, out, NULL);
    if (r == ROBO_NOT_FOUND) {
        return 0;
    }
    ROBO_CHECK_EQ_INT(r, ROBO_OK);
    return robo_html_node_handle_is_wellformed(out) ? 1 : 0;
}

/** As dom_try_find_tag, but asserts the element exists. */
static inline robo_html_node_handle_t dom_find_tag(
    const robo_html_node_handle_t *start, robo_html_tag_t tag)
{
    robo_html_node_handle_t out;

    ROBO_CHECK(dom_try_find_tag(start, tag, &out));
    return out;
}

/** One predicate argument: an `id` attribute value to match exactly. */
typedef struct dom_id_query {
    const char *id;
    size_t length;
} dom_id_query_t;

/** Exact, case-sensitive comparison against the element's `id` attribute. */
static inline int dom_predicate_id(const robo_html_node_view_t *view,
                                   void *user)
{
    const dom_id_query_t *q = (const dom_id_query_t *)user;
    size_t length = 0u;
    const char *value = robo_html_element_attribute_value(&view->handle, "id",
                                                          &length);

    if (!value || length != q->length) {
        return 0;
    }
    return memcmp(value, q->id, length) == 0;
}

/**
 * First element at or below @p start whose `id` attribute is exactly @p id,
 * in document order — the same rule the runtime's id consumers apply.
 * @return 1 and a filled @p out; 0 when nothing matches.
 */
static inline int dom_try_find_id(const robo_html_node_handle_t *start,
                                  const char *id,
                                  robo_html_node_handle_t *out)
{
    dom_id_query_t q;
    robo_result_t r;

    q.id = id;
    q.length = strlen(id);
    robo_html_node_handle_clear(out);
    r = robo_html_node_find_first(start, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  dom_predicate_id, &q, out, NULL);
    if (r == ROBO_NOT_FOUND) {
        return 0;
    }
    ROBO_CHECK_EQ_INT(r, ROBO_OK);
    return robo_html_node_handle_is_wellformed(out) ? 1 : 0;
}

/** First element with the given `id` attribute, asserted to exist. */
static inline robo_html_node_handle_t dom_find_id(
    const robo_html_node_handle_t *start, const char *id)
{
    robo_html_node_handle_t out;

    ROBO_CHECK(dom_try_find_id(start, id, &out));
    return out;
}

/* ------------------------------------------------------------------ */
/* Counts and text                                                     */
/* ------------------------------------------------------------------ */

typedef struct dom_count_query {
    robo_html_tag_t tag;
    size_t count;
} dom_count_query_t;

static inline robo_result_t dom_count_visitor(
    const robo_html_node_view_t *view, size_t depth, void *user)
{
    dom_count_query_t *q = (dom_count_query_t *)user;

    (void)depth;
    if (robo_html_element_is_tag(&view->handle, q->tag)) {
        q->count++;
    }
    return ROBO_OK;
}

/**
 * Number of elements of @p tag at or below @p start.
 *
 * Counted with the leaf walker rather than a recursive walk of the caller's
 * own, so the count is produced by the traversal the runtime ships.
 */
static inline size_t dom_count_tags(const robo_html_node_handle_t *start,
                                    robo_html_tag_t tag)
{
    dom_count_query_t q;

    q.tag = tag;
    q.count = 0u;
    ROBO_CHECK_EQ_INT(
        robo_html_node_walk_descendants(start, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                        dom_count_visitor, &q, NULL),
        ROBO_OK);
    return q.count;
}

/**
 * Subtree text of @p node into @p out, asserted to fit.
 *
 * @p out_cap is treated as a hard ceiling: a truncated read would make a
 * Unicode-preservation assertion pass on a prefix, so this fails instead.
 */
static inline void dom_text(const robo_html_node_handle_t *node,
                            char *out,
                            size_t out_cap)
{
    size_t length = 0u;

    ROBO_CHECK(out_cap > 0u);
    ROBO_CHECK_EQ_INT(
        robo_html_node_subtree_text(node, out, out_cap, &length, NULL),
        ROBO_OK);
    ROBO_CHECK_MSG(length < out_cap, "subtree text was truncated");
    out[length] = '\0';
}

/** Borrowed attribute value of @p element, or NULL when absent. */
static inline const char *dom_attr(const robo_html_node_handle_t *element,
                                   const char *name)
{
    return robo_html_element_attribute_value(element, name, NULL);
}

/* ------------------------------------------------------------------ */
/* Tree invariants                                                     */
/* ------------------------------------------------------------------ */

/**
 * Run the whole-tree invariant audit and assert it is clean.
 *
 * The audit is the runtime's own (`robo_html_document_validate_tree`), so
 * this asks the model whether it agrees with itself rather than re-walking
 * the tree here and calling the answer independent. Detached subtrees are
 * legal and do not make the audit unclean; orphans, bad parents, bad counts,
 * cycles and bad links do, and any of them failing this call is a real
 * corruption rather than a test artefact.
 */
static inline void dom_expect_clean_tree(robo_html_document_id_t id)
{
    robo_html_tree_report_t report;

    memset(&report, 0, sizeof(report));
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK_MSG(report.ok, "tree invariant audit reported corruption");
    ROBO_CHECK_EQ_INT(report.orphans, 0u);
    ROBO_CHECK_EQ_INT(report.bad_links, 0u);
    ROBO_CHECK_EQ_INT(report.cycles, 0u);
    ROBO_CHECK_EQ_INT(report.bad_parents, 0u);
    ROBO_CHECK_EQ_INT(report.bad_counts, 0u);
    ROBO_CHECK_EQ_INT(report.sibling_mismatches, 0u);
    ROBO_CHECK(report.root_ok);
}

/** Dump the document to @p out, asserted to fit. */
static inline void dom_dump(robo_html_document_id_t id,
                            char *out,
                            size_t out_cap)
{
    size_t length = 0u;

    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, out, out_cap, &length, NULL),
        ROBO_OK);
    ROBO_CHECK_MSG(length < out_cap, "tree dump was truncated");
    out[length] = '\0';
}

/* ------------------------------------------------------------------ */
/* Unicode samples                                                     */
/* ------------------------------------------------------------------ */

/**
 * One sample per script family the DOM has to carry intact.
 *
 * The bytes are UTF-8 literals rather than escapes, because the point of the
 * matrix is that these exact bytes survive; the name is what a failure
 * message should name.
 */
typedef struct dom_unicode_sample {
    const char *name;
    const char *text;
} dom_unicode_sample_t;

static inline const dom_unicode_sample_t *dom_unicode_samples(size_t *out_count)
{
    static const dom_unicode_sample_t samples[] = {
        {"english", "The quick brown fox"},
        {"french", "Élève à l'école — voilà"},
        {"german", "Grüße aus München"},
        {"arabic", "مرحبا بالعالم"},
        {"persian", "سلام دنیا"},
        {"urdu", "ہیلو دنیا"},
        {"hebrew", "שלום עולם"},
        {"devanagari", "नमस्ते दुनिया"},
        {"bengali", "হ্যালো বিশ্ব"},
        {"chinese", "你好，世界"},
        {"japanese", "こんにちは世界"},
        {"korean", "안녕하세요 세계"},
        {"emoji", "👩🏽‍🚀 family 👨‍👩‍👧‍👦"}
    };

    if (out_count) {
        *out_count = sizeof(samples) / sizeof(samples[0]);
    }
    return samples;
}

#endif /* ROBO_HTML_DOM_TEST_SUPPORT_H */
