/*
 * RoboHTML Phase 8.10 document lifecycle suite.
 *
 * The lifecycle is a state machine on the document, so this suite tests it in
 * that shape: the graph itself (which edges exist, which are refused), then
 * the transitions each entry point actually drives, then what stays true
 * across them.
 *
 * The organising idea is that COMPLETE is a statement about *input* — no
 * further input can arrive — while INTERACTIVE is a statement about the
 * *tree* — parsing finished and the DOM is live. Almost every case below is a
 * check that those two are not confused:
 *
 *   a one-shot build over a complete source   → COMPLETE
 *   a build truncated at the node ceiling     → INTERACTIVE
 *   a source detached mid-load                → INTERACTIVE
 *   an empty source                           → COMPLETE
 *   a reset that abandoned a load             → COMPLETE
 *
 * Two invariants are asserted alongside the transitions, because a lifecycle
 * that changed the tree would be worse than no lifecycle at all: after every
 * transition the whole-tree audit (robo_html_document_validate_tree) is clean,
 * and the root handle is the same root. A state machine that quietly retired
 * nodes while reporting a new state would pass every state assertion above and
 * still be broken.
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
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void lc_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void lc_fixture_end(void)
{
    robo_html_shutdown();
}

/** A fresh document; brings the module up on demand (declaration order). */
static robo_html_document_id_t lc_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    lc_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/** Current lifecycle, through the accessor every caller uses. */
static robo_html_document_lifecycle_t lc_state(robo_html_document_id_t id)
{
    robo_html_document_lifecycle_t life =
        ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED;

    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    return life;
}

/** A one-shot source over @p text. */
static void lc_source(robo_html_source_t *src, const char *text)
{
    robo_html_source_init(src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(src, text, NULL), ROBO_OK);
}

/** Attach a non-empty source directly: CREATED/INTERACTIVE/COMPLETE → LOADING. */
static void lc_attach(robo_html_document_id_t id, const char *text)
{
    robo_html_source_t src;

    lc_source(&src, text);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                      ROBO_OK);
}

/** One-shot build; the source is complete, so this is a whole load. */
static void lc_build(robo_html_document_id_t id,
                     const char *text,
                     robo_html_tree_result_t *out)
{
    robo_html_source_t src;

    lc_source(&src, text);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, out, NULL), ROBO_OK);
}

static robo_html_node_handle_t lc_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

/** Create an element attached under @p parent (legal before finalization). */
static robo_html_node_handle_t lc_element(robo_html_document_id_t id,
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

/** Create a text node attached under @p parent. */
static robo_html_node_handle_t lc_text(robo_html_document_id_t id,
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
    return out;
}

static size_t lc_child_count(const robo_html_node_handle_t *node)
{
    size_t count = 0u;

    ROBO_CHECK_EQ_INT(robo_html_node_child_count(node, &count, NULL), ROBO_OK);
    return count;
}

/**
 * The whole-tree audit, run after every transition in this suite. A lifecycle
 * change must never be what breaks the tree.
 */
static void lc_expect_clean(robo_html_document_id_t id)
{
    robo_html_tree_report_t report;

    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK_MSG(report.ok, "tree invariants broken by a lifecycle change");
    ROBO_CHECK_EQ_INT((int)report.orphans, 0);
    ROBO_CHECK_EQ_INT((int)report.bad_links, 0);
    ROBO_CHECK_EQ_INT((int)report.cycles, 0);
    ROBO_CHECK_EQ_INT((int)report.bad_parents, 0);
    ROBO_CHECK_EQ_INT((int)report.bad_counts, 0);
    ROBO_CHECK_EQ_INT((int)report.sibling_mismatches, 0);
}

/** A traversal visitor that counts what it reaches. */
typedef struct lc_count {
    size_t nodes;
    size_t elements;
    size_t texts;
} lc_count_t;

static robo_result_t lc_count_visit(const robo_html_node_view_t *view,
                                    size_t depth,
                                    void *user)
{
    lc_count_t *count = (lc_count_t *)user;

    (void)depth;
    if (!view || !count) {
        return ROBO_INVALID_ARGUMENT;
    }
    count->nodes++;
    if (view->kind == ROBO_HTML_NODE_KIND_ELEMENT) {
        count->elements++;
    } else if (view->kind == ROBO_HTML_NODE_KIND_TEXT) {
        count->texts++;
    }
    return ROBO_OK;
}

/** Walk @p node's descendants, counting. Returns the walk's own result. */
static robo_result_t lc_walk(const robo_html_node_handle_t *node,
                             lc_count_t *count)
{
    memset(count, 0, sizeof(*count));
    return robo_html_node_walk_descendants(node, ROBO_HTML_NODE_FILTER_ALL,
                                           lc_count_visit, count, NULL);
}

/** Handle of the first descendant element named @p name. */
static robo_html_node_handle_t lc_find(const robo_html_node_handle_t *start,
                                       const char *name)
{
    robo_html_node_handle_t found;

    robo_html_node_handle_clear(&found);
    if (robo_html_node_find_element(start, name, &found, NULL) != ROBO_OK) {
        robo_html_node_handle_clear(&found);
    }
    return found;
}

/* ------------------------------------------------------------------ */
/* 1. Initial state and the happy path                                 */
/* ------------------------------------------------------------------ */

static void test_lifecycle_initial_state(void)
{
    robo_html_document_id_t id;
    robo_html_document_metadata_t meta;

    id = lc_doc();

    /* A created document is CREATED: nothing attached, nothing parsed. */
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    ROBO_CHECK(!robo_html_document_is_loading(id));
    ROBO_CHECK(!robo_html_document_is_interactive(id));
    ROBO_CHECK(!robo_html_document_is_complete(id));
    ROBO_CHECK(!robo_html_document_is_settled(id));
    ROBO_CHECK(robo_html_document_is_empty(id));

    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    ROBO_CHECK_EQ_INT(meta.previous_lifecycle,
                      ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED);
    /* Construction is open, so the tree is being built, not settled. */
    ROBO_CHECK_EQ_INT(meta.finalized, 0);
    ROBO_CHECK_EQ_INT(meta.root_ok, 1);
    ROBO_CHECK_EQ_INT(meta.node_count, 1u);
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_valid_path(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root_before;
    robo_html_node_handle_t root_after;

    id = lc_doc();
    root_before = lc_root(id);

    /* CREATED → LOADING: input is attached. */
    lc_attach(id, "<p>x</p>");
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK(robo_html_document_is_loading(id));
    ROBO_CHECK(!robo_html_document_is_settled(id));
    lc_expect_clean(id);

    /* LOADING → INTERACTIVE: parsing finished, the DOM is live. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);
    ROBO_CHECK(robo_html_document_is_interactive(id));
    ROBO_CHECK(robo_html_document_is_settled(id));
    ROBO_CHECK(!robo_html_document_is_complete(id));
    lc_expect_clean(id);

    /* INTERACTIVE → COMPLETE: no further input can arrive. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK(robo_html_document_is_complete(id));
    ROBO_CHECK(robo_html_document_is_settled(id));
    /* Exactly one state, not two: complete is not also interactive. */
    ROBO_CHECK(!robo_html_document_is_interactive(id));

    /* The root survived every step: a lifecycle change is not a rebuild. */
    root_after = lc_root(id);
    ROBO_CHECK(robo_html_node_handle_equal(&root_before, &root_after));
    lc_expect_clean(id);

    /* COMPLETE → DESTROYED, one step at a time. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_document_is_live(id));
    lc_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 2. Which entry point settles where                                  */
/* ------------------------------------------------------------------ */

static void test_lifecycle_one_shot_build_completes(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t result;

    id = lc_doc();

    /*
     * A one-shot build consumes a complete source, so the document is done:
     * the build drives LOADING → INTERACTIVE → COMPLETE and the caller never
     * has to say so.
     */
    lc_build(id, "<html><body><p>Hello</p></body></html>", &result);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK(robo_html_document_is_complete(id));
    ROBO_CHECK_EQ_INT(robo_html_document_is_finalized(id), 1);
    ROBO_CHECK(!robo_html_document_is_empty(id));
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_empty_source_settles_complete(void)
{
    robo_html_document_id_t id;
    robo_html_source_t src;

    id = lc_doc();

    /*
     * Nothing to parse means nothing can be outstanding: an empty source
     * still passes through INTERACTIVE, then settles. The document stays
     * empty and stays owned.
     */
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_empty(&src, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK(robo_html_document_is_empty(id));
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_detach_settles_interactive(void)
{
    robo_html_document_id_t id;

    id = lc_doc();

    /*
     * Detaching abandons the input rather than finishing it, so the document
     * is interactive and explicitly *not* complete.
     */
    lc_attach(id, "<p>abandoned</p>");
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK_EQ_INT(robo_html_document_detach_source(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);
    ROBO_CHECK(robo_html_document_is_interactive(id));
    ROBO_CHECK(!robo_html_document_is_complete(id));
    ROBO_CHECK_STR_EQ(robo_html_document_source_name(id), "");
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_truncated_build_stays_interactive(void)
{
    enum { ELEMENTS = 4300 };
    robo_html_document_id_t id;
    robo_html_source_t src;
    robo_html_tree_result_t result;
    char *html;
    size_t i;
    size_t at = 0u;

    id = lc_doc();

    /*
     * The node ceiling (ROBO_HTML_MAX_NODES_PER_DOCUMENT) is 4096, so this
     * input cannot be consumed in full. The build therefore abandons the
     * source rather than finishing it, and the document stops at INTERACTIVE:
     * claiming COMPLETE would be saying "no further input can arrive" about
     * input that was never read.
     */
    html = (char *)robo_malloc(64u + (size_t)ELEMENTS * 32u);
    ROBO_CHECK(html != NULL);
    if (!html) {
        (void)robo_html_document_destroy(id, NULL);
        lc_fixture_end();
        return;
    }
    for (i = 0u; i < (size_t)ELEMENTS; i++) {
        static const char PIECE[] = "<span>node</span>";

        memcpy(html + at, PIECE, sizeof(PIECE) - 1u);
        at += sizeof(PIECE) - 1u;
    }
    html[at] = '\0';

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_memory(&src, (const uint8_t *)html, at, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, &result, NULL), ROBO_OK);
    ROBO_CHECK_MSG(result.truncated, "expected the node ceiling to truncate");
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);
    ROBO_CHECK(!robo_html_document_is_complete(id));
    ROBO_CHECK(robo_html_document_is_settled(id));
    ROBO_CHECK_EQ_INT(robo_html_document_is_finalized(id), 1);
    lc_expect_clean(id);

    robo_free(html);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_replacement_source_reloads(void)
{
    robo_html_document_id_t id;
    robo_html_tree_result_t result;

    id = lc_doc();
    lc_build(id, "<html><body><p>one</p></body></html>", &result);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);

    /*
     * A settled document accepts a replacement source: COMPLETE → LOADING.
     * Settled is not sealed.
     */
    lc_attach(id, "<p>two</p>");
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);

    /* A second one-shot build reloads and settles again. */
    lc_build(id, "<html><body><p>three</p></body></html>", &result);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_parser_lease_settles_interactive(void)
{
    robo_html_document_id_t id;
    robo_html_parser_t *parser = NULL;
    robo_html_source_t src;

    id = lc_doc();

    ROBO_CHECK_EQ_INT(robo_html_parser_create(id, &parser, NULL), ROBO_OK);
    lc_source(&src, "<div>streamed</div>");
    ROBO_CHECK_EQ_INT(robo_html_parser_attach_source(parser, &src, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);

    /*
     * A parser detaching has not finished the load — the tree is whatever was
     * parsed so far — so the document is interactive.
     */
    ROBO_CHECK_EQ_INT(robo_html_parser_detach(parser, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);
    robo_html_parser_destroy(parser);
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 3. Refused transitions leave the document exactly as it was         */
/* ------------------------------------------------------------------ */

static void test_lifecycle_complete_requires_interactive(void)
{
    robo_html_document_id_t id;

    id = lc_doc();

    /* From CREATED, completion is refused: nothing has been parsed. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);

    /* From LOADING, also refused: parsing has not finished. */
    lc_attach(id, "<p>x</p>");
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);

    /* The intermediate state is the only way through. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_repeated_transitions(void)
{
    robo_html_document_id_t id;

    id = lc_doc();

    /* Interactive is only reachable from LOADING. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);

    lc_attach(id, "<p>x</p>");
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    /* ... and only once. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);

    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL), ROBO_OK);
    /* Completion is a one-way edge, and there is no way back to interactive. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);

    /*
     * Failed attempts really changed nothing: the recorded previous state is
     * still the transition that succeeded.
     */
    {
        robo_html_document_metadata_t meta;

        ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(meta.previous_lifecycle,
                          ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);
    }
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_invalid_transitions_are_inert(void)
{
    robo_html_document_id_t id;
    robo_html_source_t src;

    id = lc_doc();

    /* A failed document is sticky and cannot be talked back into loading. */
    lc_attach(id, "<p>x</p>");
    ROBO_CHECK_EQ_INT(
        robo_html_document_mark_failed(id, ROBO_HTML_DOMAIN_SOURCE, "test",
                                       NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED);
    /* A failed document refuses new input rather than silently discarding. */
    lc_source(&src, "<p>y</p>");
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED);
    /* And a second failure is a no-op, not an error about the first. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_mark_failed(id, ROBO_HTML_DOMAIN_PARSER, "again",
                                       NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED);
    /* Failure is not teardown: the tree is still there to inspect. */
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 4. Observability                                                    */
/* ------------------------------------------------------------------ */

static void test_lifecycle_observability(void)
{
    robo_html_document_id_t id;
    robo_html_document_id_t dead;
    robo_html_document_lifecycle_t life;

    id = lc_doc();

    /* Exactly one predicate is true at a time, in every reachable state. */
    ROBO_CHECK_EQ_INT(robo_html_document_is_loading(id), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_is_interactive(id), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_is_complete(id), 0);

    lc_attach(id, "<p>x</p>");
    ROBO_CHECK_EQ_INT(robo_html_document_is_loading(id), 1);
    ROBO_CHECK_EQ_INT(robo_html_document_is_interactive(id), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_is_complete(id), 0);

    (void)robo_html_document_mark_interactive(id, NULL);
    ROBO_CHECK_EQ_INT(robo_html_document_is_loading(id), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_is_interactive(id), 1);
    ROBO_CHECK_EQ_INT(robo_html_document_is_complete(id), 0);

    (void)robo_html_document_mark_complete(id, NULL);
    ROBO_CHECK_EQ_INT(robo_html_document_is_loading(id), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_is_interactive(id), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_is_complete(id), 1);

    /* A retired id answers "no" to every state question... */
    dead = id;
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(dead, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_is_loading(dead), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_is_interactive(dead), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_is_complete(dead), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_is_settled(dead), 0);
    /* ... and get_lifecycle is how a caller tells that apart from a state the
     * document is simply not in. */
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(dead, &life),
                      ROBO_NOT_FOUND);

    /* An id that was never issued behaves identically: retired ids are not
     * probeable, so this cannot become an oracle for recycled ids. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_is_complete((robo_html_document_id_t)(dead + 1000u)),
        0);
    lc_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 5. Lifecycle × mutation                                             */
/* ------------------------------------------------------------------ */

static void test_lifecycle_mutation_in_every_state(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t first;
    robo_html_node_handle_t second;
    char buf[64];
    size_t length = 0u;

    id = lc_doc();
    root = lc_root(id);

    /* CREATED: construction is open, and the tree is still mutable. */
    first = lc_element(id, &root, "div");
    second = lc_element(id, &root, "span");
    ROBO_CHECK_EQ_INT(lc_child_count(&root), 2u);
    lc_expect_clean(id);

    /* LOADING: input is attached, and mutation keeps working. */
    lc_attach(id, "<p>ignored</p>");
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &second, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_child_count(&root), 1u);
    lc_expect_clean(id);

    /* INTERACTIVE: parsing is over, the DOM is live. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    (void)lc_text(id, &first, "Hello");
    ROBO_CHECK_EQ_INT(lc_child_count(&first), 1u);
    lc_expect_clean(id);

    /* COMPLETE: completion describes input, not permission. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&first, &second, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_child_count(&first), 2u);
    ROBO_CHECK_EQ_INT(
        robo_html_node_subtree_text(&first, buf, sizeof(buf), &length, NULL),
        ROBO_OK);
    ROBO_CHECK(strstr(buf, "Hello") != NULL);
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_mutation_after_destruction(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t child;
    robo_html_node_handle_t made;
    size_t removed = 0u;

    id = lc_doc();
    root = lc_root(id);
    (void)lc_element(id, &root, "p");
    lc_build(id, "<html><body><b>live</b></body></html>", NULL);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    child = lc_find(&root, "body");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&child));

    /*
     * Destroy retires the document *and* every handle into it, so a mutation
     * attempted afterwards is a not-found lookup rather than a write into freed
     * state. The old handles are stale, not dangling.
     */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_stale(&root));
    ROBO_CHECK(robo_html_node_handle_is_stale(&child));
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &child, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_children(&root, &removed, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(removed, 0u);
    /* The document root accessor refuses too, rather than handing back a
     * zeroed handle as if it were a node. */
    made = robo_html_document_root(id);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&made));

    /* Repeated destruction is safe: the second call reports not-found rather
     * than double-freeing. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);
    lc_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 6. Lifecycle × traversal                                            */
/* ------------------------------------------------------------------ */

static void test_lifecycle_traversal_in_every_state(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t child;
    lc_count_t count;

    id = lc_doc();
    root = lc_root(id);

    /* CREATED: the root resolves, and an empty walk is legal, not an error. */
    ROBO_CHECK_EQ_INT(lc_walk(&root, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count.nodes, 0u);

    /* LOADING: same, and still no crash on an unfinished tree. */
    lc_attach(id, "<p>x</p>");
    ROBO_CHECK_EQ_INT(lc_walk(&root, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count.nodes, 0u);

    /* Add real children so the walk has something to reach. */
    child = lc_element(id, &root, "div");
    (void)lc_element(id, &child, "span");
    (void)lc_text(id, &child, "text");

    /* INTERACTIVE: the walk reaches the whole subtree. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_walk(&root, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count.nodes, 3u);
    ROBO_CHECK_EQ_INT(count.elements, 2u);
    ROBO_CHECK_EQ_INT(count.texts, 1u);

    /* COMPLETE: exactly the same result. Completion does not change the tree. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_walk(&root, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count.nodes, 3u);
    ROBO_CHECK_EQ_INT(count.elements, 2u);
    ROBO_CHECK_EQ_INT(count.texts, 1u);
    lc_expect_clean(id);

    /* After destruction the start handle is stale, so the walk refuses rather
     * than walking freed nodes. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_walk(&root, &count), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(count.nodes, 0u);
    lc_fixture_end();
}

static void test_lifecycle_traversal_across_transitions(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t body;
    robo_html_node_handle_t p;
    robo_html_node_handle_t span;
    lc_count_t count;

    id = lc_doc();
    lc_build(id, "<html><body><p>Hello</p></body></html>", NULL);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);

    root = lc_root(id);
    body = lc_find(&root, "body");
    p = lc_find(&root, "p");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&body));
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&p));
    ROBO_CHECK_EQ_INT(lc_walk(&body, &count), ROBO_OK);
    ROBO_CHECK(count.nodes > 0u);

    /*
     * The lifecycle itself is not a structural change, so a handle and a walk
     * taken before a transition are still valid after one on the same tree.
     */
    lc_attach(id, "<p>unused</p>");
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_stale(&body));
    ROBO_CHECK(!robo_html_node_handle_is_stale(&p));
    ROBO_CHECK_EQ_INT(lc_walk(&body, &count), ROBO_OK);
    ROBO_CHECK(count.nodes > 0u);

    /*
     * A *reload* is a structural change: the previous tree is retired, and a
     * handle into it reports stale rather than resolving to whatever now
     * occupies that slot.
     */
    lc_build(id, "<html><body><span>x</span></body></html>", NULL);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK(robo_html_node_handle_is_stale(&body));
    ROBO_CHECK(robo_html_node_handle_is_stale(&p));

    root = lc_root(id);
    span = lc_find(&root, "span");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&span));
    ROBO_CHECK_EQ_INT(lc_walk(&span, &count), ROBO_OK);
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 7. Parser integration and malformed input                           */
/* ------------------------------------------------------------------ */

static void test_lifecycle_parser_integration(void)
{
    robo_html_document_id_t id;
    robo_html_tree_builder_t *builder = NULL;
    robo_html_source_t src;
    robo_html_parser_state_t state = ROBO_HTML_PARSER_STATE_IDLE;
    int i;

    id = lc_doc();

    /* Before any source the document is CREATED, not loading. */
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);

    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &builder, NULL),
                      ROBO_OK);
    lc_source(&src, "<h1>Title</h1>");
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_begin(builder, &src, NULL),
                      ROBO_OK);
    /* Building is loading. */
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK(!robo_html_document_is_settled(id));

    for (i = 0; i < 64; i++) {
        ROBO_CHECK_EQ_INT(robo_html_tree_builder_next(builder, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_tree_builder_get_state(builder, &state),
                          ROBO_OK);
        if (state == ROBO_HTML_PARSER_STATE_COMPLETE) {
            break;
        }
    }
    ROBO_CHECK_EQ_INT(state, ROBO_HTML_PARSER_STATE_COMPLETE);

    /*
     * The parser drove the transition, not the test: a finished build over a
     * complete source settles the document.
     */
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK_EQ_INT(robo_html_document_is_finalized(id), 1);
    lc_expect_clean(id);

    robo_html_tree_builder_destroy(builder);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_malformed_html(void)
{
    static const char *const cases[] = {
        "",
        "<div><p>unclosed<span></div></b>text<<<",
        "<!doctype html><html><body><table><tr><td>cell",
        "&amp&amp;",
        "<a href=\"<script>\">link</a>",
        "<p>&#x1F600;</p>",
        "text only, no markup at all",
        "   \n\t  ",
        "<p><p><p><p><p>deep</p>",
    };
    robo_html_document_id_t id;
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_html_tree_result_t result;

        id = lc_doc();
        lc_build(id, cases[i], &result);

        /*
         * Malformed input is recovered, never fatal: the document reaches a
         * defined settled state and its tree passes the audit, so no parse
         * outcome can produce an impossible lifecycle.
         */
        ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
        ROBO_CHECK(robo_html_document_is_settled(id));
        ROBO_CHECK_EQ_INT(robo_html_document_is_finalized(id), 1);
        lc_expect_clean(id);

        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }
    lc_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 8. Ownership, root integrity, reset                                 */
/* ------------------------------------------------------------------ */

static void test_lifecycle_root_and_ownership_integrity(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root_before;
    robo_html_node_handle_t root_after;
    robo_html_node_handle_t body;
    robo_html_node_handle_t p;
    robo_html_node_handle_t prev;
    robo_html_node_handle_t next;
    size_t count_before;
    size_t count_after;

    id = lc_doc();
    lc_build(id, "<html><body><p>one</p><p>two</p></body></html>", NULL);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);

    root_before = lc_root(id);
    count_before = robo_html_document_node_count(id);
    ROBO_CHECK(robo_html_document_owns_node(id, &root_before));

    /* A source attach alone moves the document to LOADING, then the caller
     * settles it again: the whole cycle, without touching the tree. */
    lc_attach(id, "<html><body><p>replacement</p></body></html>");
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL), ROBO_OK);

    /* The root is still the same node, still owned, still the root. */
    root_after = lc_root(id);
    ROBO_CHECK(robo_html_node_handle_equal(&root_before, &root_after));
    ROBO_CHECK(robo_html_document_owns_node(id, &root_after));
    count_after = robo_html_document_node_count(id);
    ROBO_CHECK_EQ_INT(count_after, count_before);
    lc_expect_clean(id);

    /* Parent and sibling links read correctly after all those transitions. */
    body = lc_find(&root_after, "body");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&body));
    p = lc_find(&body, "p");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&p));
    ROBO_CHECK_EQ_INT(robo_html_node_parent(&p, &prev, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&prev, &body));
    /* The second <p> follows, and the last one has no successor. */
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&p, &next, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&next));
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&next, &prev, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_reset_settles_complete(void)
{
    robo_html_document_id_t id;
    robo_html_document_metadata_t meta;

    id = lc_doc();

    /* Reset on a document that never loaded keeps CREATED: it still has no
     * source and never had one. */
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);

    /* Reset on a loading document abandons the load into the settled resting
     * state — the same state an empty source produces. */
    lc_attach(id, "<p>x</p>");
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK(robo_html_document_is_empty(id));
    lc_expect_clean(id);

    /*
     * Reset on an *interactive* document — a load that was abandoned part way,
     * which is what a streamed document sits in — settles it the same way, and
     * succeeds. Re-requesting INTERACTIVE first would be an edge the machine
     * does not have, and failing here would leave the document cleared but
     * still marked interactive: a half-applied reset.
     */
    lc_attach(id, "<p>interrupted</p>");
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK(robo_html_document_is_empty(id));
    lc_expect_clean(id);

    /* Reset on a settled document leaves the state where it was. */
    lc_build(id, "<p>y</p>", NULL);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK_EQ_INT(meta.is_empty, 1);
    ROBO_CHECK_EQ_INT(meta.node_count, 1u);
    lc_expect_clean(id);

    /* And a reset document still accepts a new source. */
    lc_build(id, "<p>z</p>", NULL);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK(!robo_html_document_is_empty(id));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_hand_built_document_stays_created(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t child;

    id = lc_doc();
    root = lc_root(id);
    child = lc_element(id, &root, "div");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&child));

    /*
     * A document built through the element API and never given a source has no
     * load to settle, so it stays CREATED — and CREATED already permits
     * everything the DOM needs (content, mutation, traversal). There is
     * deliberately no CREATED → INTERACTIVE edge: every state above CREATED is
     * a statement about a load, and inventing a second route to INTERACTIVE
     * would blur what "created" means.
     */
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    lc_expect_clean(id);

    /* Attaching a source is what opens the way out of CREATED. */
    lc_attach(id, "<p>now loaded</p>");
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_teardown_safety(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t deep;
    char name[32];
    char html[512];
    size_t at = 0u;
    int i;

    /*
     * Case 1: teardown from a construction site — a deep hand-built tree that
     * never settled, so teardown has to walk depth the parser did not make.
     */
    id = lc_doc();
    root = lc_root(id);
    deep = root;
    for (i = 0; i < 12; i++) {
        sprintf(name, "d%d", i);
        deep = lc_element(id, &deep, name);
    }
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&deep));
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_stale(&root));
    ROBO_CHECK(robo_html_node_handle_is_stale(&deep));
    ROBO_CHECK(!robo_html_document_is_live(id));
    /* Destroying again is deterministic, never a double free. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);

    /*
     * Case 2: teardown from COMPLETE — a deeply nested parsed tree, so
     * teardown runs after the document has settled and finalized.
     */
    for (i = 0; i < 20; i++) {
        memcpy(html + at, "<div>", 5u);
        at += 5u;
    }
    memcpy(html + at, "deep", 4u);
    at += 4u;
    for (i = 0; i < 20; i++) {
        memcpy(html + at, "</div>", 6u);
        at += 6u;
    }
    html[at] = '\0';

    id = lc_doc();
    lc_build(id, html, NULL);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    root = lc_root(id);
    deep = lc_find(&root, "div");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&deep));
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_stale(&root));
    ROBO_CHECK(robo_html_node_handle_is_stale(&deep));
    ROBO_CHECK(!robo_html_document_is_live(id));
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);
    lc_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 9. Debug output                                                     */
/* ------------------------------------------------------------------ */

static void test_lifecycle_dump_shows_state(void)
{
    robo_html_document_id_t id;
    char dump[4096];
    size_t length = 0u;

    id = lc_doc();

    /* The dump reports the document's own state, not a fixed string. */
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), &length, NULL),
        ROBO_OK);
    ROBO_CHECK_MSG(strstr(dump, "Document lifecycle=created") != NULL, dump);

    lc_attach(id, "<p>x</p>");
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), &length, NULL),
        ROBO_OK);
    ROBO_CHECK_MSG(strstr(dump, "Document lifecycle=loading") != NULL, dump);

    (void)robo_html_document_mark_interactive(id, NULL);
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), &length, NULL),
        ROBO_OK);
    ROBO_CHECK_MSG(strstr(dump, "Document lifecycle=interactive") != NULL,
                   dump);

    (void)robo_html_document_mark_complete(id, NULL);
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), &length, NULL),
        ROBO_OK);
    ROBO_CHECK_MSG(strstr(dump, "Document lifecycle=complete") != NULL, dump);

    /* And a parsed tree puts its elements under that same line. */
    lc_build(id, "<html><body><p>Hello</p></body></html>", NULL);
    ROBO_CHECK_EQ_INT(
        robo_html_tree_dump_to_buffer(id, dump, sizeof(dump), &length, NULL),
        ROBO_OK);
    ROBO_CHECK(strstr(dump, "Document lifecycle=complete") != NULL);
    ROBO_CHECK(strstr(dump, "p") != NULL);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 10. Preservation and diagnostics across transitions                 */
/* ------------------------------------------------------------------ */

static void test_lifecycle_preserves_unicode_and_attributes(void)
{
    static const char *const DOC =
        "<html lang=\"ar\"><body>"
        "<p title=\"\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7\">"
        "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c</p>"
        "<p data-name=\"\xe0\xa4\xb9\xe0\xa4\xbf\xe0\xa4\xa8\xe0\xa5\x8d"
        "\xe0\xa4\xa6\xe0\xa5\x80\">\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"
        "</p>"
        "<p aria-label=\"\xf0\x9f\x98\x80\">\xf0\x9f\x9a\x80</p>"
        "</body></html>";
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t body;
    robo_html_node_handle_t p;
    robo_html_attribute_t attr;
    char text[256];
    size_t text_length = 0u;

    id = lc_doc();
    lc_build(id, DOC, NULL);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);

    root = lc_root(id);
    body = lc_find(&root, "body");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&body));
    p = lc_find(&body, "p");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&p));

    /*
     * Attributes and text are read *after* the lifecycle settled, so this is
     * also a check that completion did not disturb the payload arena.
     */
    robo_html_attribute_init(&attr);
    ROBO_CHECK_EQ_INT(
        robo_html_element_find_attribute(&p, "title", &attr, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(attr.value_length, 10u);
    ROBO_CHECK(strcmp(attr.value, "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7") ==
               0);

    ROBO_CHECK_EQ_INT(
        robo_html_node_subtree_text(&body, text, sizeof(text), &text_length,
                                    NULL),
        ROBO_OK);
    ROBO_CHECK(strstr(text, "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c") != NULL);
    ROBO_CHECK(strstr(text, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") != NULL);
    ROBO_CHECK(strstr(text, "\xf0\x9f\x9a\x80") != NULL);

    /* The same values survive another trip through the whole lifecycle. */
    lc_attach(id, DOC);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);

    root = lc_root(id);
    body = lc_find(&root, "body");
    p = lc_find(&body, "p");
    robo_html_attribute_init(&attr);
    ROBO_CHECK_EQ_INT(
        robo_html_element_find_attribute(&p, "title", &attr, NULL), ROBO_OK);
    ROBO_CHECK(strcmp(attr.value, "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7") ==
               0);
    lc_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_diag_domain(void)
{
    robo_html_document_id_t id;
    robo_html_diag_t diag;
    robo_html_source_t src;

    id = lc_doc();

    /* A refused transition reports itself in the lifecycle domain, so a caller
     * can tell "your state machine says no" from "bad argument". */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(id, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_LIFECYCLE);

    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(id, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_LIFECYCLE);

    /* An unknown document is a document-domain not-found: different question,
     * different answer. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(
                          (robo_html_document_id_t)9999u, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_DOCUMENT);

    /* Attaching while loading is busy, not invalid: the input is real. */
    lc_attach(id, "<p>x</p>");
    lc_source(&src, "<p>y</p>");
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, &diag),
                      ROBO_BUSY);
    ROBO_CHECK_EQ_INT(lc_state(id), ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

static void test_lifecycle_is_not_a_structural_change(void)
{
    robo_html_document_id_t id;
    uint32_t before = 0u;
    uint32_t after = 0u;

    id = lc_doc();
    lc_build(id, "<html><body><p>x</p></body></html>", NULL);

    ROBO_CHECK_EQ_INT(robo_html_node_tree_version(id, &before, NULL), ROBO_OK);
    ROBO_CHECK(before != 0u);

    /*
     * Lifecycle transitions are not structural changes: a document that
     * settles must not invalidate an in-flight traversal, because not one node
     * moved. The tree version is what says so.
     */
    lc_attach(id, "<p>again</p>");
    (void)robo_html_document_mark_interactive(id, NULL);
    (void)robo_html_document_mark_complete(id, NULL);
    ROBO_CHECK_EQ_INT(robo_html_node_tree_version(id, &after, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)(after - before), 0);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    lc_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_html_lifecycle(void)
{
    robo_test_register("html.lifecycle.initial_state",
                       test_lifecycle_initial_state);
    robo_test_register("html.lifecycle.valid_path",
                       test_lifecycle_valid_path);
    robo_test_register("html.lifecycle.one_shot_build_completes",
                       test_lifecycle_one_shot_build_completes);
    robo_test_register("html.lifecycle.empty_source_settles_complete",
                       test_lifecycle_empty_source_settles_complete);
    robo_test_register("html.lifecycle.detach_settles_interactive",
                       test_lifecycle_detach_settles_interactive);
    robo_test_register("html.lifecycle.truncated_build_stays_interactive",
                       test_lifecycle_truncated_build_stays_interactive);
    robo_test_register("html.lifecycle.replacement_source_reloads",
                       test_lifecycle_replacement_source_reloads);
    robo_test_register("html.lifecycle.parser_lease_settles_interactive",
                       test_lifecycle_parser_lease_settles_interactive);
    robo_test_register("html.lifecycle.complete_requires_interactive",
                       test_lifecycle_complete_requires_interactive);
    robo_test_register("html.lifecycle.repeated_transitions",
                       test_lifecycle_repeated_transitions);
    robo_test_register("html.lifecycle.invalid_transitions_are_inert",
                       test_lifecycle_invalid_transitions_are_inert);
    robo_test_register("html.lifecycle.observability",
                       test_lifecycle_observability);
    robo_test_register("html.lifecycle.mutation_in_every_state",
                       test_lifecycle_mutation_in_every_state);
    robo_test_register("html.lifecycle.mutation_after_destruction",
                       test_lifecycle_mutation_after_destruction);
    robo_test_register("html.lifecycle.traversal_in_every_state",
                       test_lifecycle_traversal_in_every_state);
    robo_test_register("html.lifecycle.traversal_across_transitions",
                       test_lifecycle_traversal_across_transitions);
    robo_test_register("html.lifecycle.parser_integration",
                       test_lifecycle_parser_integration);
    robo_test_register("html.lifecycle.malformed_html",
                       test_lifecycle_malformed_html);
    robo_test_register("html.lifecycle.root_and_ownership_integrity",
                       test_lifecycle_root_and_ownership_integrity);
    robo_test_register("html.lifecycle.reset_settles_complete",
                       test_lifecycle_reset_settles_complete);
    robo_test_register("html.lifecycle.hand_built_document_stays_created",
                       test_lifecycle_hand_built_document_stays_created);
    robo_test_register("html.lifecycle.teardown_safety",
                       test_lifecycle_teardown_safety);
    robo_test_register("html.lifecycle.dump_shows_state",
                       test_lifecycle_dump_shows_state);
    robo_test_register("html.lifecycle.preserves_unicode_and_attributes",
                       test_lifecycle_preserves_unicode_and_attributes);
    robo_test_register("html.lifecycle.diag_domain",
                       test_lifecycle_diag_domain);
    robo_test_register("html.lifecycle.is_not_a_structural_change",
                       test_lifecycle_is_not_a_structural_change);
}
