/*
 * RoboHTML Phase 8.9 DOM mutation suite.
 *
 * What it covers, in the order the task spec lists it: the four structural
 * operations and their positional cases, bulk child removal, parent/child/
 * sibling/first/last integrity after every one of them, ordering and child
 * count, duplicate ownership, self-insertion and ancestor cycles,
 * cross-document refusal, detached-node behaviour, mutation across the
 * construction/finalization boundary, parser-created trees, Unicode and
 * attribute preservation, the mutation hook, the capability flag, the
 * traversal-invalidation rule, and teardown.
 *
 * The fixture builds trees through the element and text models under a fresh
 * document rather than through the parser, because a mutation test wants a
 * shape it stated itself — except for the two tests that deliberately go
 * through the real parser to prove a finalized tree is mutable.
 *
 * Every structural assertion is doubled: the specific one (which child is
 * first, how many children) and a whole-tree audit
 * (robo_html_document_validate_tree). The specific check says what the
 * operation was supposed to do; the audit is what catches a repair that
 * fixed the link the test looked at and left another one wrong — a stale
 * last_child, a child_count that drifted, a prev_sibling that disagrees with
 * the traversal order, or a node that fell out of the tree entirely.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void mu_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void mu_fixture_end(void)
{
    robo_html_shutdown();
}

static robo_html_document_id_t mu_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    /*
     * Brings the module up on demand: the fixture is built at the top of a
     * test (declaration order), so the document the test measures is created
     * before its body's mu_fixture_begin() line would have run. Every other
     * suite puts the call first for the same reason; doing it here means a
     * test cannot get the order wrong.
     */
    mu_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

static robo_html_node_handle_t mu_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

/** Create an element named @p name under @p parent (attached). */
static robo_html_node_handle_t mu_element(robo_html_document_id_t id,
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
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

/** Create a text node with @p content under @p parent (attached). */
static robo_html_node_handle_t mu_text(robo_html_document_id_t id,
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
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

/**
 * The whole-tree audit. Every mutating test ends with this, because the
 * audit reports *what* disagrees rather than only that something does.
 */
static void mu_expect_clean(robo_html_document_id_t id)
{
    robo_html_tree_report_t report;

    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK_MSG(report.ok, "tree invariants broken after mutation");
    ROBO_CHECK_EQ_INT((int)report.orphans, 0);
    ROBO_CHECK_EQ_INT((int)report.bad_links, 0);
    ROBO_CHECK_EQ_INT((int)report.cycles, 0);
    ROBO_CHECK_EQ_INT((int)report.bad_parents, 0);
    ROBO_CHECK_EQ_INT((int)report.bad_counts, 0);
    ROBO_CHECK_EQ_INT((int)report.sibling_mismatches, 0);
}

/** Parse @p text into @p id with the real tree builder (finalizes it). */
static void mu_build(robo_html_document_id_t id, const char *text)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);
}

/** Child count of @p node, or SIZE_MAX when it does not resolve. */
static size_t mu_child_count(const robo_html_node_handle_t *node)
{
    size_t count = SIZE_MAX;

    if (robo_html_node_child_count(node, &count, NULL) != ROBO_OK) {
        return SIZE_MAX;
    }
    return count;
}

static uint32_t mu_version(robo_html_document_id_t id)
{
    uint32_t version = 0u;

    ROBO_CHECK_EQ_INT(robo_html_node_tree_version(id, &version, NULL), ROBO_OK);
    ROBO_CHECK(version != 0u);
    return version;
}

/* ------------------------------------------------------------------ */
/* Append                                                              */
/* ------------------------------------------------------------------ */

static void test_mutation_append_first(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t body = mu_element(id, &root, "body");
    robo_html_node_handle_t p = mu_element(id, &body, "p");
    robo_html_node_handle_t first;
    robo_html_node_handle_t last;

    mu_fixture_begin();

    ROBO_CHECK_EQ_INT(mu_child_count(&body), 1);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&body, &first, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&body, &last, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&first, &p));
    ROBO_CHECK(robo_html_node_handle_equal(&last, &p));
    /* The only child has neither sibling. */
    {
        robo_html_node_handle_t sib;

        ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&p, &sib, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&p, &sib, NULL),
                          ROBO_NOT_FOUND);
    }
    /* The parent relationship points back. */
    {
        robo_html_node_handle_t back;

        ROBO_CHECK_EQ_INT(robo_html_node_parent(&p, &back, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&back, &body));
    }
    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_append_multiple_order(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t c = mu_element(id, &root, "c");
    robo_html_node_handle_t first;
    robo_html_node_handle_t last;
    robo_html_node_handle_t mid;
    robo_html_node_handle_t back;

    mu_fixture_begin();

    ROBO_CHECK_EQ_INT(mu_child_count(&root), 3);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&root, &last, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&first, &a));
    ROBO_CHECK(robo_html_node_handle_equal(&last, &c));

    /* a -> b -> c, and the inverse links. */
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&a, &mid, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&mid, &b));
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&b, &mid, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&mid, &c));
    ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&b, &mid, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&mid, &a));
    ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&c, &mid, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&mid, &b));
    ROBO_CHECK_EQ_INT(robo_html_node_parent(&c, &back, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&back, &root));

    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_append_text(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t p = mu_element(id, &root, "p");
    robo_html_node_handle_t text = mu_text(id, &p, "Hello");
    const char *content;
    size_t length = 0u;

    mu_fixture_begin();

    ROBO_CHECK_EQ_INT(mu_child_count(&p), 1);
    ROBO_CHECK(robo_html_text_is_text(&text));
    content = robo_html_text_content(&text, &length);
    ROBO_CHECK(content != NULL);
    ROBO_CHECK_EQ_INT((int)length, 5);
    ROBO_CHECK(strcmp(content, "Hello") == 0);
    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_append_rejects_attached(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_diag_t diag;
    uint32_t before;

    mu_fixture_begin();

    before = mu_version(id);
    robo_html_diag_reset(&diag);

    /* b already belongs to root: a silent move would be the bug. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&a, &b, &diag),
                      ROBO_ALREADY_EXISTS);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_MUTATION);
    /* Nothing moved, including the version — the refusal is provably inert. */
    {
        robo_html_node_handle_t back;

        ROBO_CHECK_EQ_INT(robo_html_node_parent(&b, &back, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&back, &root));
        ROBO_CHECK_EQ_INT(mu_child_count(&root), 2);
        ROBO_CHECK_EQ_INT(mu_child_count(&a), 0);
    }
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);
    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_append_rejects_self(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_diag_t diag;
    uint32_t before;

    mu_fixture_begin();

    before = mu_version(id);
    robo_html_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&a, &a, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);
    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_append_rejects_document_node(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t body = mu_element(id, &root, "body");
    robo_html_diag_t diag;
    uint32_t before;

    mu_fixture_begin();

    before = mu_version(id);
    robo_html_diag_reset(&diag);
    /* A document has exactly one root and nothing here may add another. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&body, &root, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);
    mu_expect_clean(id);
    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Cycles                                                             */
/* ------------------------------------------------------------------ */

static void test_mutation_cycle_ancestor_refused(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &a, "b");
    robo_html_node_handle_t c = mu_element(id, &b, "c");
    robo_html_diag_t diag;
    uint32_t before;
    int contains = -1;

    mu_fixture_begin();

    /*
     * The ancestor must be detached for the cycle rule to be the deciding
     * one: an attached node is refused earlier for having a parent, which
     * would make this test pass for the wrong reason. Detaching a takes its
     * whole subtree with it, so a > b > c still holds.
     */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &a, NULL), ROBO_OK);
    ROBO_CHECK(mu_child_count(&root) == 0);

    before = mu_version(id);
    robo_html_diag_reset(&diag);

    /* Putting a under c would make a reachable from itself. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&c, &a, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_MUTATION);
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);
    /* Nothing moved: a still holds b, and b still holds c. */
    ROBO_CHECK_EQ_INT(mu_child_count(&a), 1);
    ROBO_CHECK_EQ_INT(mu_child_count(&c), 0);

    /* The predicate agrees, and it is the same rule the refusal applied. */
    ROBO_CHECK_EQ_INT(robo_html_node_contains(&a, &c, &contains, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(contains, 1);
    ROBO_CHECK_EQ_INT(robo_html_node_contains(&c, &a, &contains, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(contains, 0);
    /* contains is reflexively true, like DOM's Node.contains. */
    ROBO_CHECK_EQ_INT(robo_html_node_contains(&a, &a, &contains, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(contains, 1);

    /* A node detached from that subtree is no longer inside a. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&b, &c, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_contains(&a, &c, &contains, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(contains, 0);
    /* ...and the operation that was a cycle is now simply legal. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&c, &a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_contains(&a, &c, &contains, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(contains, 0);
    ROBO_CHECK_EQ_INT(robo_html_node_contains(&c, &a, &contains, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(contains, 1);
    mu_expect_clean(id);

    /* Put the tree back the way the fixture started. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&c, &a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&b, &c, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &a, NULL), ROBO_OK);
    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_cycle_insert_before_refused(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &a, "b");
    robo_html_node_handle_t c = mu_element(id, &b, "c");
    robo_html_diag_t diag;
    uint32_t before;

    mu_fixture_begin();

    /* Detach a so the cycle rule, not the one-parent rule, decides. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &a, NULL), ROBO_OK);
    before = mu_version(id);
    robo_html_diag_reset(&diag);
    /*
     * Inserting a before c would place a inside its own descendant b: c is a
     * genuine child of b, so the anchor itself is valid and the refusal can
     * only come from the cycle rule.
     */
    ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&b, &a, &c, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_MUTATION);
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);
    ROBO_CHECK_EQ_INT(mu_child_count(&b), 1);
    mu_expect_clean(id);

    /* Restore, then check the substitution path of the same rule. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &a, NULL), ROBO_OK);
    mu_expect_clean(id);
    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Insert before                                                       */
/* ------------------------------------------------------------------ */

static void test_mutation_insert_before_positions(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t c = mu_element(id, &root, "c");
    robo_html_node_handle_t first;
    robo_html_node_handle_t last;
    robo_html_node_handle_t got;

    mu_fixture_begin();

    /* Detach c and put it before a: c must become the new first child. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &c, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&root, &c, &a, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 3);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&first, &c));
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&root, &last, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&last, &b));
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&c, &got, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &a));
    ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&a, &got, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &c));
    mu_expect_clean(id);

    /* Middle: insert c before b, i.e. between a and b. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &c, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&root, &c, &b, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&first, &a));
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&root, &last, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&last, &b));
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 3);
    mu_expect_clean(id);

    /* Before the last child leaves the last child alone. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &c, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&root, &c, &b, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&root, &last, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&last, &b));
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 3);
    mu_expect_clean(id);

    mu_fixture_end();
}

static void test_mutation_insert_before_rejects_foreign_reference(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t other = mu_element(id, &root, "other");
    robo_html_node_handle_t stray = mu_element(id, &a, "stray");
    robo_html_diag_t diag;
    uint32_t before;

    mu_fixture_begin();

    before = mu_version(id);
    robo_html_diag_reset(&diag);
    /* stray is a child of a, not of b: "insert before" must not guess. */
    ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&b, &other, &stray, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_MUTATION);
    /* A stale handle is a miss, not a foreign reference. */
    {
        robo_html_node_handle_t dead = stray;

        dead.node_id = (robo_html_node_id_t)9999u;
        ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&b, &other, &dead, NULL),
                          ROBO_NOT_FOUND);
    }
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);
    mu_expect_clean(id);
    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Remove                                                             */
/* ------------------------------------------------------------------ */

static void test_mutation_remove_positions(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t c = mu_element(id, &root, "c");
    robo_html_node_handle_t first;
    robo_html_node_handle_t last;

    mu_fixture_begin();

    /* Middle: a and c close the gap. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 2);
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&a, &first, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&first, &c));
    ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&c, &last, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&last, &a));
    ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&b, &first, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&b, &first, NULL),
                      ROBO_NOT_FOUND);
    mu_expect_clean(id);

    /* First: the new first child has no previous sibling. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&first, &c));
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&root, &last, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&last, &c));
    mu_expect_clean(id);

    /* Only child: the parent is left valid and childless. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &c, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 0);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&root, &last, NULL),
                      ROBO_NOT_FOUND);
    mu_expect_clean(id);

    mu_fixture_end();
}

static void test_mutation_remove_rejects_wrong_parent(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t child = mu_element(id, &a, "child");
    robo_html_diag_t diag;
    uint32_t before;

    mu_fixture_begin();

    before = mu_version(id);
    robo_html_diag_reset(&diag);
    /* child belongs to a, so b is not its parent — nothing may be removed. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&b, &child, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_MUTATION);
    ROBO_CHECK_EQ_INT(mu_child_count(&a), 1);
    ROBO_CHECK_EQ_INT(mu_child_count(&b), 0);
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);
    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_remove_keeps_subtree(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t holder = mu_element(id, &root, "holder");
    robo_html_node_handle_t box = mu_element(id, &holder, "div");
    robo_html_node_handle_t inner = mu_element(id, &box, "span");
    robo_html_node_handle_t text = mu_text(id, &inner, "kept");
    const char *content;
    size_t length = 0u;
    int contains = 0;

    mu_fixture_begin();

    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&holder, &box, NULL),
                      ROBO_OK);

    /* Everything inside the detached node is intact and still linked. */
    ROBO_CHECK_EQ_INT(mu_child_count(&box), 1);
    ROBO_CHECK_EQ_INT(mu_child_count(&inner), 1);
    content = robo_html_text_content(&text, &length);
    ROBO_CHECK(content != NULL);
    ROBO_CHECK_EQ_INT((int)length, 4);
    ROBO_CHECK(strcmp(content, "kept") == 0);

    /* The detached subtree is traversable in its own right... */
    ROBO_CHECK_EQ_INT(robo_html_node_contains(&box, &text, &contains, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(contains, 1);
    /* ...and is no longer inside the parent it came from. */
    ROBO_CHECK_EQ_INT(robo_html_node_contains(&holder, &box, &contains, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(contains, 0);
    /* Its depth restarts: a detached root sits at depth 0, like the document. */
    {
        size_t depth = 999u;

        ROBO_CHECK_EQ_INT(robo_html_node_depth(&box, &depth, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)depth, 0);
        ROBO_CHECK_EQ_INT(robo_html_node_depth(&text, &depth, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)depth, 2);
    }

    /* The document still owns it: a detached node is not destroyed. */
    ROBO_CHECK(robo_html_document_owns_node(id, &box));

    /* ...and it can be inserted somewhere else. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &box, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 2);
    mu_expect_clean(id);

    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Replace                                                             */
/* ------------------------------------------------------------------ */

static void test_mutation_replace_positions(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t c = mu_element(id, &root, "c");
    robo_html_node_handle_t x = mu_element(id, &root, "x");
    robo_html_node_handle_t y = mu_element(id, &root, "y");
    robo_html_node_handle_t first;
    robo_html_node_handle_t last;
    robo_html_node_handle_t got;

    mu_fixture_begin();

    /*
     * x and y start attached under root so they exist; they are pulled out
     * before each replacement, because a replacement never steals a node
     * that already has a parent.
     */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &x, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &y, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 3);

    /* First position. */
    ROBO_CHECK_EQ_INT(robo_html_node_replace_child(&root, &x, &a, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 3);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&first, &x));
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&root, &last, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&last, &c));
    /* The old child is detached, not destroyed. */
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&a, &got, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(robo_html_document_owns_node(id, &a));
    mu_expect_clean(id);

    /* Middle position: y takes b's slot, between x and c. */
    ROBO_CHECK_EQ_INT(robo_html_node_replace_child(&root, &y, &b, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&first, &x));
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&x, &got, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &y));
    ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&y, &got, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &c));
    ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&y, &got, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &x));
    mu_expect_clean(id);

    /*
     * Last position. a was detached by the first replacement, so it is the
     * free node here: root is [x, y, c] and becomes [x, y, a].
     */
    ROBO_CHECK_EQ_INT(robo_html_node_replace_child(&root, &a, &c, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&root, &last, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&last, &a));
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&first, &x));
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 3);
    mu_expect_clean(id);

    /* Only child: replacing it leaves a one-child parent. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &y, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &x, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 1);
    ROBO_CHECK_EQ_INT(robo_html_node_replace_child(&root, &b, &a, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 1);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&first, &b));
    ROBO_CHECK_EQ_INT(robo_html_node_last_child(&root, &last, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&last, &b));
    mu_expect_clean(id);

    mu_fixture_end();
}

static void test_mutation_replace_rejects_attached(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t c = mu_element(id, &root, "c");
    robo_html_diag_t diag;
    uint32_t before;

    mu_fixture_begin();

    before = mu_version(id);
    robo_html_diag_reset(&diag);
    /* b is attached, so a must not lose its slot to a refusal. */
    ROBO_CHECK_EQ_INT(robo_html_node_replace_child(&root, &b, &a, &diag),
                      ROBO_ALREADY_EXISTS);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_MUTATION);
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 3);
    {
        robo_html_node_handle_t first;
        robo_html_node_handle_t back;

        ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&first, &a));
        ROBO_CHECK_EQ_INT(robo_html_node_parent(&a, &back, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&back, &root));
    }
    /* Replacing a node with itself is refused too. */
    ROBO_CHECK_EQ_INT(robo_html_node_replace_child(&root, &a, &a, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);

    (void)c;
    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_replace_rejects_cycle(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t outer = mu_element(id, &root, "outer");
    robo_html_node_handle_t slot = mu_element(id, &outer, "slot");
    robo_html_node_handle_t deep = mu_element(id, &slot, "deep");
    uint32_t before;

    mu_fixture_begin();

    /* Detached, so the cycle rule decides rather than the one-parent rule. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &outer, NULL),
                      ROBO_OK);
    before = mu_version(id);
    /* outer cannot take deep's slot: outer is deep's ancestor. */
    ROBO_CHECK_EQ_INT(robo_html_node_replace_child(&slot, &outer, &deep, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);
    ROBO_CHECK_EQ_INT(mu_child_count(&slot), 1);
    {
        robo_html_node_handle_t first;

        ROBO_CHECK_EQ_INT(robo_html_node_first_child(&slot, &first, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&first, &deep));
    }
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &outer, NULL),
                      ROBO_OK);
    mu_expect_clean(id);
    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Bulk clear                                                          */
/* ------------------------------------------------------------------ */

static void test_mutation_remove_children(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t parent = mu_element(id, &root, "div");
    robo_html_node_handle_t a = mu_element(id, &parent, "a");
    robo_html_node_handle_t b = mu_element(id, &parent, "b");
    robo_html_node_handle_t c = mu_text(id, &parent, "text");
    size_t removed = 99u;
    size_t count = 99u;

    mu_fixture_begin();

    ROBO_CHECK_EQ_INT(robo_html_node_remove_children(&parent, &removed, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)removed, 3);
    ROBO_CHECK_EQ_INT(mu_child_count(&parent), 0);
    /* Detached, not destroyed: all three are still live nodes of the doc. */
    ROBO_CHECK(robo_html_document_owns_node(id, &a));
    ROBO_CHECK(robo_html_document_owns_node(id, &b));
    ROBO_CHECK(robo_html_document_owns_node(id, &c));
    /* A second clear is a successful no-op. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_children(&parent, &removed, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)removed, 0);
    ROBO_CHECK_EQ_INT(robo_html_node_child_count(&parent, &count, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 0);
    mu_expect_clean(id);

    /* Re-insertion works, so the clear did not strand anything. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&parent, &c, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&parent), 1);
    mu_expect_clean(id);

    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Cross-document                                                      */
/* ------------------------------------------------------------------ */

static void test_mutation_cross_document_refused(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_document_id_t other = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t other_root = mu_root(other);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t foreign = mu_element(other, &other_root, "foreign");
    robo_html_diag_t diag;
    uint32_t before;

    mu_fixture_begin();

    before = mu_version(id);
    robo_html_diag_reset(&diag);

    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&a, &foreign, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_MUTATION);
    ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&a, &foreign, &foreign,
                                                   NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&a, &foreign, NULL),
                      ROBO_NOT_SUPPORTED);
    /* contains across documents is the same refusal, not a false answer. */
    {
        int contains = -1;

        ROBO_CHECK_EQ_INT(
            robo_html_node_contains(&a, &foreign, &contains, &diag),
            ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(contains, 0);
    }
    /* Neither tree moved, and the foreign document is untouched too. */
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)before);
    ROBO_CHECK_EQ_INT(mu_child_count(&a), 0);
    ROBO_CHECK_EQ_INT(mu_child_count(&other_root), 1);
    mu_expect_clean(id);

    (void)robo_html_document_destroy(other, NULL);
    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Boundary + queries                                                  */
/* ------------------------------------------------------------------ */

static void test_mutation_null_and_unknown_handles(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t zero;
    uint32_t version = 0u;

    mu_fixture_begin();

    robo_html_node_handle_clear(&zero);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(NULL, &a, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &zero, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&zero, &a, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* A handle naming no document is a miss, not a cross-document refusal. */
    {
        robo_html_node_handle_t no_doc = a;

        no_doc.document_id = ROBO_HTML_DOCUMENT_ID_NONE;
        ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &no_doc, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 1);

    ROBO_CHECK_EQ_INT(robo_html_node_tree_version(id, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_tree_version(ROBO_HTML_DOCUMENT_ID_NONE,
                                                  &version, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)version, 0);
    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_version_moves_only_on_success(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t c = mu_element(id, &root, "c");
    uint32_t v0;
    uint32_t v1;

    mu_fixture_begin();

    v0 = mu_version(id);
    /* A no-op removal of an absent name is not a tree change. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_children(&a, NULL, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)v0);

    /* A refusal is not a tree change either. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&a, &b, NULL),
                      ROBO_ALREADY_EXISTS);
    ROBO_CHECK_EQ_INT((int)mu_version(id), (int)v0);

    /* A success is. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &c, NULL), ROBO_OK);
    v1 = mu_version(id);
    ROBO_CHECK(v1 != v0);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&a, &c, NULL), ROBO_OK);
    ROBO_CHECK(mu_version(id) != v1);

    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_capability_reports_itself(void)
{
    robo_html_dom_capabilities_t caps;

    mu_fixture_begin();

    robo_html_dom_capabilities_init(&caps);
    ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.mutation, 1);
    ROBO_CHECK_EQ_INT(caps.traversal, 1);
    ROBO_CHECK_EQ_INT(caps.tree_building, 1);
    /* Still later-phase work, and saying so is the point of the flags. */
    ROBO_CHECK_EQ_INT(caps.dom_nodes, 0);
    ROBO_CHECK_EQ_INT(caps.events, 0);
    ROBO_CHECK(robo_html_mutation_kind_is_known(
        ROBO_HTML_MUTATION_TREE_MUTATED));
    ROBO_CHECK(!robo_html_mutation_kind_is_known(
        (robo_html_mutation_kind_t)9));

    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Mutation hook                                                       */
/* ------------------------------------------------------------------ */

static int g_hook_mutations;
static int g_hook_registrations;

static void mutation_hook_count(const robo_html_mutation_event_t *event,
                                void *userdata)
{
    int *seen = (int *)userdata;

    if (!event || !seen) {
        return;
    }
    if (event->kind == ROBO_HTML_MUTATION_TREE_MUTATED) {
        g_hook_mutations++;
    } else if (event->kind == ROBO_HTML_MUTATION_NODE_REGISTERED) {
        g_hook_registrations++;
    }
    (*seen)++;
}

static void test_mutation_hook_emitted(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    int seen = 0;

    mu_fixture_begin();

    g_hook_mutations = 0;
    g_hook_registrations = 0;
    /*
     * Installed after the tree exists, because registration emits too and
     * this test is about the structural event.
     */
    ROBO_CHECK_EQ_INT(robo_html_document_set_mutation_hook(
                          id, mutation_hook_count, &seen, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(seen, 1);
    ROBO_CHECK_EQ_INT(g_hook_mutations, 1);
    ROBO_CHECK_EQ_INT(g_hook_registrations, 0);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&a, &b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(seen, 2);
    /* A refusal emits nothing: there was no structural change to report. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&a, &b, NULL),
                      ROBO_ALREADY_EXISTS);
    ROBO_CHECK_EQ_INT(seen, 2);
    ROBO_CHECK_EQ_INT(robo_html_document_set_mutation_hook(id, NULL, NULL,
                                                           NULL),
                      ROBO_OK);

    mu_expect_clean(id);
    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Traversal interaction                                               */
/* ------------------------------------------------------------------ */

static void test_mutation_cursor_invalidated(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t c = mu_element(id, &root, "c");
    robo_html_node_handle_t extra = mu_element(id, &root, "extra");
    robo_html_child_cursor_t cursor;
    robo_html_node_handle_t got;
    robo_html_diag_t diag;

    mu_fixture_begin();

    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &extra, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(&cursor, &root, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &a));

    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &b, NULL), ROBO_OK);

    /*
     * The cursor's stored next id is now b, a node that is no longer a child
     * of root. Handing it out would report a child the parent does not have.
     */
    robo_html_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &got, &diag),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_MUTATION);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&got));
    /* The cursor is invalidated, not merely paused. */
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &got, NULL),
                      ROBO_NOT_FOUND);

    /* A fresh cursor on the mutated tree is correct. */
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(&cursor, &root, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &a));
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&got, &c));
    ROBO_CHECK_EQ_INT(robo_html_child_cursor_next(&cursor, &got, NULL),
                      ROBO_NOT_FOUND);

    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_cursor_survives_refusal(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_child_cursor_t cursor;
    robo_html_node_handle_t got;
    size_t seen = 0u;

    mu_fixture_begin();

    ROBO_CHECK_EQ_INT(robo_html_child_cursor_open(&cursor, &root, NULL),
                      ROBO_OK);
    /* A refused mutation changed nothing, so the iteration is still valid. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&a, &b, NULL),
                      ROBO_ALREADY_EXISTS);
    while (robo_html_child_cursor_next(&cursor, &got, NULL) == ROBO_OK) {
        seen++;
    }
    ROBO_CHECK_EQ_INT((int)seen, 2);

    mu_fixture_end();
}

static int g_walk_mutated;

static robo_result_t walk_mutating_visitor(const robo_html_node_view_t *view,
                                           size_t depth,
                                           void *user)
{
    robo_html_node_handle_t *mover = (robo_html_node_handle_t *)user;

    (void)depth;
    if (!g_walk_mutated && view->kind == ROBO_HTML_NODE_KIND_TEXT) {
        g_walk_mutated = 1;
        /* Detach the subtree the walk is standing in. */
        return robo_html_node_remove_child(&view->parent, &view->handle, NULL);
    }
    (void)mover;
    return ROBO_OK;
}

static void test_mutation_walk_invalidated(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t body = mu_element(id, &root, "body");
    robo_html_node_handle_t p = mu_element(id, &body, "p");
    robo_html_node_handle_t moving = mu_element(id, &root, "moving");
    robo_html_diag_t diag;
    robo_result_t r;

    mu_fixture_begin();

    (void)mu_text(id, &p, "inside");
    g_walk_mutated = 0;
    robo_html_diag_reset(&diag);

    r = robo_html_node_walk_pre_order(&body, ROBO_HTML_NODE_FILTER_ALL,
                                      walk_mutating_visitor, &moving, &diag);
    ROBO_CHECK_EQ_INT(r, ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_MUTATION);
    ROBO_CHECK_EQ_INT(g_walk_mutated, 1);

    /* The walk stopped; the mutation it performed stands and is coherent. */
    ROBO_CHECK_EQ_INT(mu_child_count(&body), 1);
    ROBO_CHECK_EQ_INT(mu_child_count(&p), 0);

    /* A walk started after the change sees the new shape. */
    {
        robo_html_node_handle_t found;

        ROBO_CHECK_EQ_INT(
            robo_html_node_find_first(&body, ROBO_HTML_NODE_FILTER_TEXT, NULL,
                                      NULL, &found, NULL),
            ROBO_NOT_FOUND);
    }
    mu_expect_clean(id);
    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Parser-created trees                                                */
/* ------------------------------------------------------------------ */

static void test_mutation_parser_tree_reorder(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t body;
    robo_html_node_handle_t heading;
    robo_html_node_handle_t one;
    robo_html_node_handle_t two;
    robo_html_node_handle_t back;
    char text[128];
    size_t length = 0u;

    mu_fixture_begin();

    mu_build(id,
             "<html><body><h1>Title</h1><p>One</p><p>Two</p></body></html>");

    /*
     * The builder finalizes what it built, so this is the case that matters:
     * a closed-for-construction document is still mutable.
     */
    ROBO_CHECK_EQ_INT(robo_html_document_is_finalized(id), 1);
    ROBO_CHECK_EQ_INT(robo_html_document_allows_construction(id), 0);

    root = mu_root(id);
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&root, "body", &body, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&body), 3);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, 0u, &heading, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, 1u, &one, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, 2u, &two, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&body, text, sizeof(text),
                                                  &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(strcmp(text, "TitleOneTwo") == 0);

    /* Move the first paragraph to the end: Title, Two, One. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&body, &one, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&body, &one, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&body), 3);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, 0u, &back, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&back, &heading));
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, 2u, &back, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&back, &one));
    ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&body, text, sizeof(text),
                                                  &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(strcmp(text, "TitleTwoOne") == 0);
    mu_expect_clean(id);

    /* And put it back at the head, ahead of the heading. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&body, &one, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&body, &one, &heading, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&body, text, sizeof(text),
                                                  &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(strcmp(text, "OneTitleTwo") == 0);
    mu_expect_clean(id);

    (void)two;
    mu_fixture_end();
}

static void test_mutation_creation_stays_gated(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t body;
    robo_html_element_create_info_t info;
    robo_html_node_handle_t made;
    robo_html_diag_t diag;

    mu_fixture_begin();

    mu_build(id, "<html><body><p>x</p></body></html>");
    root = mu_root(id);
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&root, "body", &body, NULL),
                      ROBO_OK);

    /*
     * The documented boundary of this phase: the DOM rearranges the nodes a
     * document has; registering a new one stays a construction operation,
     * which finalization closed. Pinned as a test so the seam is a decision
     * rather than an accident.
     */
    robo_html_element_create_info_init(&info);
    info.parent = body;
    info.origin = ROBO_HTML_NODE_ORIGIN_DOM;
    info.namespace_id = ROBO_HTML_NAMESPACE_HTML;
    info.local_name = "p";
    robo_html_node_handle_clear(&made);
    robo_html_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &made, &diag),
                      ROBO_ERROR);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&made));

    /* Rearranging what is already there still works. */
    {
        robo_html_node_handle_t p;

        ROBO_CHECK_EQ_INT(robo_html_node_find_element(&root, "p", &p, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&body, &p, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_append_child(&body, &p, NULL),
                          ROBO_OK);
    }
    mu_expect_clean(id);
    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Preservation                                                        */
/* ------------------------------------------------------------------ */

static void test_mutation_preserves_attributes(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t holder = mu_element(id, &root, "holder");
    robo_html_node_handle_t target;
    robo_html_element_create_info_t info;
    robo_html_attribute_t attrs[2];
    const char *value;
    size_t value_length = 0u;

    mu_fixture_begin();

    memset(attrs, 0, sizeof(attrs));
    attrs[0].name = "id";
    attrs[0].name_length = 2u;
    attrs[0].value = "main";
    attrs[0].value_length = 4u;
    attrs[1].name = "class";
    attrs[1].name_length = 5u;
    attrs[1].value = "panel";
    attrs[1].value_length = 5u;

    robo_html_element_create_info_init(&info);
    info.parent = root;
    info.origin = ROBO_HTML_NODE_ORIGIN_TEST;
    info.namespace_id = ROBO_HTML_NAMESPACE_HTML;
    info.local_name = "div";
    info.attributes = attrs;
    info.attribute_count = 2u;
    robo_html_node_handle_clear(&target);
    ROBO_CHECK_EQ_INT(robo_html_element_create(id, &info, &target, NULL),
                      ROBO_OK);

    /* Move it around, twice, and re-read the attributes each time. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &target, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&holder, &target, NULL),
                      ROBO_OK);
    value = robo_html_element_attribute_value(&target, "id", &value_length);
    ROBO_CHECK(value != NULL);
    ROBO_CHECK_EQ_INT((int)value_length, 4);
    ROBO_CHECK(strcmp(value, "main") == 0);
    value = robo_html_element_attribute_value(&target, "class", &value_length);
    ROBO_CHECK(value != NULL);
    ROBO_CHECK(strcmp(value, "panel") == 0);
    ROBO_CHECK(robo_html_element_has_attribute(&target, "ID"));

    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&holder, &target, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &target, NULL),
                      ROBO_OK);
    {
        robo_html_element_view_t view;

        ROBO_CHECK_EQ_INT(robo_html_element_view_get(&target, &view, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)view.attribute_count, 2);
        ROBO_CHECK_EQ_INT((int)view.namespace_id,
                          (int)ROBO_HTML_NAMESPACE_HTML);
    }
    mu_expect_clean(id);
    mu_fixture_end();
}

static void test_mutation_preserves_unicode_text(void)
{
    static const char *const samples[] = {
        "Hello", "مرحبا", "فارسی", "اردو", "हिन्दी", "বাংলা",
        "中文", "日本語", "한국어", "😀 🚀 ❤️"
    };
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    size_t i;

    mu_fixture_begin();

    for (i = 0u; i < sizeof(samples) / sizeof(samples[0]); i++) {
        robo_html_node_handle_t text = mu_text(id, &a, samples[i]);
        const char *got;
        size_t length = 0u;

        /* Detach and re-attach: the bytes must survive the round trip. */
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&a, &text, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_append_child(&b, &text, NULL),
                          ROBO_OK);
        got = robo_html_text_content(&text, &length);
        ROBO_CHECK(got != NULL);
        ROBO_CHECK_EQ_INT((int)length, (int)strlen(samples[i]));
        ROBO_CHECK_MSG(strcmp(got, samples[i]) == 0,
                       "text bytes changed across a mutation");
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&b, &text, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_append_child(&a, &text, NULL),
                          ROBO_OK);
    }
    mu_expect_clean(id);
    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Long sequences + teardown                                           */
/* ------------------------------------------------------------------ */

static void test_mutation_long_sequence(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t c = mu_element(id, &root, "c");
    robo_html_node_handle_t d = mu_element(id, &root, "d");
    robo_html_node_handle_t e = mu_element(id, &root, "e");

    mu_fixture_begin();

    /* d and e start attached so they exist; pull them out to re-insert. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &d, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &e, NULL), ROBO_OK);

    /* append A, append B, append C is the fixture: a, b, c.
     * remove B (middle), insert D before C, replace A with E,
     * remove E, remove C, remove D.
     */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 2);
    mu_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_node_insert_before(&root, &d, &c, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 3);
    mu_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_node_replace_child(&root, &e, &a, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 3);
    {
        robo_html_node_handle_t first;

        ROBO_CHECK_EQ_INT(robo_html_node_first_child(&root, &first, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&first, &e));
    }
    mu_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &e, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &c, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &d, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(mu_child_count(&root), 0);
    mu_expect_clean(id);

    /* Everything is still live; the document just does not list it. */
    ROBO_CHECK(robo_html_document_owns_node(id, &a));
    ROBO_CHECK(robo_html_document_owns_node(id, &b));
    ROBO_CHECK(robo_html_document_owns_node(id, &c));
    ROBO_CHECK(robo_html_document_owns_node(id, &d));
    ROBO_CHECK(robo_html_document_owns_node(id, &e));

    mu_fixture_end();
}

static void test_mutation_detached_subtree_walk(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t holder = mu_element(id, &root, "holder");
    robo_html_node_handle_t div = mu_element(id, &holder, "div");
    robo_html_node_handle_t span = mu_element(id, &div, "span");
    robo_html_node_handle_t text = mu_text(id, &span, "inner");
    char buffer[64];
    size_t length = 0u;

    mu_fixture_begin();

    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&holder, &div, NULL),
                      ROBO_OK);

    /* The detached subtree is a tree: a walk over it reaches the text. */
    ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&div, buffer, sizeof(buffer),
                                                  &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(strcmp(buffer, "inner") == 0);
    /* And it is no longer part of the document's reachable tree. */
    {
        char whole[64];
        size_t whole_length = 0u;

        ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&root, whole,
                                                      sizeof(whole),
                                                      &whole_length, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)whole_length, 0);
    }
    /* The audit is clean whether or not a detached subtree exists: audit
     * counts only what the root reaches, and detached nodes are not orphans
     * of the tree, they are simply not in it. */
    mu_expect_clean(id);

    (void)text;
    (void)span;
    mu_fixture_end();
}

static void test_mutation_teardown_after_mutation(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &a, "b");
    robo_html_node_handle_t text = mu_text(id, &b, "x");

    mu_fixture_begin();

    /* a/b move around, b keeps and then loses its text, and the sequence
     * ends in a state the audit can check (a detached subtree is legal). */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&a, &b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&b, &text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&b, &text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&b, &a, NULL), ROBO_OK);
    mu_expect_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    /* Every handle from the destroyed document is stale; nothing dangles. */
    ROBO_CHECK(robo_html_node_handle_is_stale(&root));
    ROBO_CHECK(robo_html_node_handle_is_stale(&a));
    ROBO_CHECK(robo_html_node_handle_is_stale(&b));
    ROBO_CHECK(robo_html_node_handle_is_stale(&text));
    /* And the operations refuse rather than touch freed state. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &a, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &a, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_children(&root, NULL, NULL),
                      ROBO_NOT_FOUND);
}

/**
 * The audit's two unreachable-node verdicts, side by side: a detached node
 * is reported and legal; a node that claims a parent while nothing lists it
 * is an orphan and is not.
 */
static void test_mutation_audit_distinguishes_detached(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &a, "b");
    robo_html_node_handle_t text = mu_text(id, &b, "inner");
    robo_html_tree_report_t rep;

    mu_fixture_begin();

    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &rep, NULL),
                      ROBO_OK);
    /* One detached root (a) is counted; b and the text are marked as its
     * subtree, not miscounted as orphans of their own. */
    ROBO_CHECK_EQ_INT((int)rep.detached, 1);
    ROBO_CHECK_EQ_INT((int)rep.orphans, 0);
    ROBO_CHECK_EQ_INT((int)rep.nodes, 1); /* only the root is reachable */
    ROBO_CHECK_EQ_INT((int)rep.registered, 4);
    ROBO_CHECK_EQ_INT(rep.ok, 1);

    /* Re-attaching makes the document whole again. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&root, &a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &rep, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)rep.detached, 0);
    ROBO_CHECK_EQ_INT((int)rep.nodes, 4);
    ROBO_CHECK_EQ_INT(rep.ok, 1);
    mu_expect_clean(id);

    (void)text;
    mu_fixture_end();
}

static void test_mutation_reset_invalidates_handles(void)
{
    robo_html_document_id_t id = mu_doc();
    robo_html_node_handle_t root = mu_root(id);
    robo_html_node_handle_t a = mu_element(id, &root, "a");
    robo_html_node_handle_t b = mu_element(id, &root, "b");
    robo_html_node_handle_t root_after;

    mu_fixture_begin();

    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&root, &b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);

    /* The tree is gone, so the old handles miss and the root carries on. */
    ROBO_CHECK_EQ_INT(robo_html_node_append_child(&a, &b, NULL),
                      ROBO_NOT_FOUND);
    root_after = mu_root(id);
    ROBO_CHECK_EQ_INT(mu_child_count(&root_after), 0);
    mu_expect_clean(id);

    mu_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_html_mutation(void);

void robo_test_register_html_mutation(void)
{
    robo_test_register("html.mutate.append_first",
                       test_mutation_append_first);
    robo_test_register("html.mutate.append_multiple_order",
                       test_mutation_append_multiple_order);
    robo_test_register("html.mutate.append_text",
                       test_mutation_append_text);
    robo_test_register("html.mutate.append_rejects_attached",
                       test_mutation_append_rejects_attached);
    robo_test_register("html.mutate.append_rejects_self",
                       test_mutation_append_rejects_self);
    robo_test_register("html.mutate.append_rejects_document_node",
                       test_mutation_append_rejects_document_node);
    robo_test_register("html.mutate.cycle_ancestor_refused",
                       test_mutation_cycle_ancestor_refused);
    robo_test_register("html.mutate.cycle_insert_before_refused",
                       test_mutation_cycle_insert_before_refused);
    robo_test_register("html.mutate.insert_before_positions",
                       test_mutation_insert_before_positions);
    robo_test_register("html.mutate.insert_before_rejects_foreign_reference",
                       test_mutation_insert_before_rejects_foreign_reference);
    robo_test_register("html.mutate.remove_positions",
                       test_mutation_remove_positions);
    robo_test_register("html.mutate.remove_rejects_wrong_parent",
                       test_mutation_remove_rejects_wrong_parent);
    robo_test_register("html.mutate.remove_keeps_subtree",
                       test_mutation_remove_keeps_subtree);
    robo_test_register("html.mutate.replace_positions",
                       test_mutation_replace_positions);
    robo_test_register("html.mutate.replace_rejects_attached",
                       test_mutation_replace_rejects_attached);
    robo_test_register("html.mutate.replace_rejects_cycle",
                       test_mutation_replace_rejects_cycle);
    robo_test_register("html.mutate.remove_children",
                       test_mutation_remove_children);
    robo_test_register("html.mutate.cross_document_refused",
                       test_mutation_cross_document_refused);
    robo_test_register("html.mutate.null_and_unknown_handles",
                       test_mutation_null_and_unknown_handles);
    robo_test_register("html.mutate.version_moves_only_on_success",
                       test_mutation_version_moves_only_on_success);
    robo_test_register("html.mutate.capability_reports_itself",
                       test_mutation_capability_reports_itself);
    robo_test_register("html.mutate.hook_emitted",
                       test_mutation_hook_emitted);
    robo_test_register("html.mutate.cursor_invalidated",
                       test_mutation_cursor_invalidated);
    robo_test_register("html.mutate.cursor_survives_refusal",
                       test_mutation_cursor_survives_refusal);
    robo_test_register("html.mutate.walk_invalidated",
                       test_mutation_walk_invalidated);
    robo_test_register("html.mutate.parser_tree_reorder",
                       test_mutation_parser_tree_reorder);
    robo_test_register("html.mutate.creation_stays_gated",
                       test_mutation_creation_stays_gated);
    robo_test_register("html.mutate.preserves_attributes",
                       test_mutation_preserves_attributes);
    robo_test_register("html.mutate.preserves_unicode_text",
                       test_mutation_preserves_unicode_text);
    robo_test_register("html.mutate.long_sequence",
                       test_mutation_long_sequence);
    robo_test_register("html.mutate.detached_subtree_walk",
                       test_mutation_detached_subtree_walk);
    robo_test_register("html.mutate.teardown_after_mutation",
                       test_mutation_teardown_after_mutation);
    robo_test_register("html.mutate.audit_distinguishes_detached",
                       test_mutation_audit_distinguishes_detached);
    robo_test_register("html.mutate.reset_invalidates_handles",
                       test_mutation_reset_invalidates_handles);
}
