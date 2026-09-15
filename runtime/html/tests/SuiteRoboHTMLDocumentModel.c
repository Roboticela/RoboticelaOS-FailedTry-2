/*
 * RoboHTML Phase 8.4 document-model suite.
 *
 * Covers the document as the authoritative context for the HTML tree:
 * identity and metadata, root ownership, node ownership and staleness,
 * cross-document mistakes, source/encoding association, construction
 * closure and reuse, whole-tree invariant auditing, failure state,
 * teardown, and the end-to-end tokenizer → tree builder → document path.
 *
 * The tests assert *behaviour*, so every one of them drives real APIs and
 * inspects real state; nothing here merely proves a header compiles.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void dm_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void dm_fixture_end(void)
{
    robo_html_shutdown();
}

static robo_html_document_id_t dm_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/** Build @p text into @p id; result optional. */
static void dm_build(robo_html_document_id_t id,
                     const char *text,
                     robo_html_tree_result_t *out)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, out, NULL), ROBO_OK);
}

/** Handle of the first descendant element named @p name (invalid when none). */
static robo_html_node_handle_t dm_find(robo_html_document_id_t id,
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

/** Non-zero when the first descendant element named @p name is live. */
static int dm_has(robo_html_document_id_t id, const char *name)
{
    robo_html_node_handle_t h = dm_find(id, name);

    return robo_html_node_handle_is_wellformed(&h);
}

/*
 * A document whose shape is exactly known, used wherever counts matter.
 * The input carries no whitespace between tags, so the only text nodes are
 * the two element texts — which keeps the expected counts checkable by hand.
 */
#define DM_COMPACT_DOC                                                       \
    "<!DOCTYPE html><html><head><title>T</title></head>"                     \
    "<body><p>a</p><!--c--></body></html>"

/** Trees are expected to be consistent unless a test says otherwise. */
static void dm_check_tree_clean(robo_html_document_id_t id)
{
    robo_html_tree_report_t rep;

    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &rep, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rep.root_ok, 1);
    ROBO_CHECK_EQ_INT(rep.orphans, 0u);
    ROBO_CHECK_EQ_INT(rep.cycles, 0u);
    ROBO_CHECK_EQ_INT(rep.bad_links, 0u);
    ROBO_CHECK_EQ_INT(rep.bad_parents, 0u);
    ROBO_CHECK_EQ_INT(rep.bad_counts, 0u);
    ROBO_CHECK_EQ_INT(rep.sibling_mismatches, 0u);
    ROBO_CHECK_EQ_INT(rep.ok, 1);
    ROBO_CHECK_EQ_INT(rep.nodes, rep.registered);
}

/* ------------------------------------------------------------------ */
/* 1. Metadata (items 1-5, 16, 27, 28)                                 */
/* ------------------------------------------------------------------ */

static void test_dm_metadata_fresh(void)
{
    robo_html_document_id_t id;
    robo_html_document_metadata_t meta;
    size_t kinds[ROBO_HTML_NODE_KIND_COUNT];

    dm_fixture_begin();
    id = dm_doc();

    ROBO_CHECK_EQ_INT(
        robo_html_document_metadata(id, &meta, kinds, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.id, id);
    ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    ROBO_CHECK_EQ_INT(meta.finalized, 0);
    ROBO_CHECK_EQ_INT(meta.is_empty, 1);
    ROBO_CHECK_EQ_INT(meta.root_ok, 1);
    ROBO_CHECK_EQ_INT(meta.node_count, 1u); /* the document node itself */
    ROBO_CHECK_EQ_INT(meta.element_count, 0u);
    ROBO_CHECK_EQ_INT(meta.text_count, 0u);
    ROBO_CHECK_EQ_INT(meta.comment_count, 0u);
    ROBO_CHECK_EQ_INT(meta.doctype_count, 0u);
    ROBO_CHECK_EQ_INT(meta.source_kind, ROBO_HTML_SOURCE_KIND_NONE);
    ROBO_CHECK_EQ_INT(meta.source_length, 0u);
    /* Unset labels are empty strings, never NULL, so callers need no guard. */
    ROBO_CHECK(meta.source_name != NULL);
    ROBO_CHECK_STR_EQ(meta.source_name, "");
    ROBO_CHECK(meta.fail_message == NULL);
    ROBO_CHECK_EQ_INT(meta.fail_domain, ROBO_HTML_DOMAIN_NONE);
    ROBO_CHECK_EQ_INT(meta.encoding.declared, ROBO_HTML_ENCODING_UNKNOWN);
    ROBO_CHECK_EQ_INT(meta.encoding.status, ROBO_HTML_ENCODING_STATUS_OK);

    /* Per-kind counts, indexed by node kind. */
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_DOCUMENT], 1u);
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_ELEMENT], 0u);
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_TEXT], 0u);
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_COMMENT], 0u);
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_DOCUMENT_TYPE], 0u);
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_FRAGMENT], 0u);

    dm_check_tree_clean(id);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_metadata_built(void)
{
    robo_html_document_id_t id;
    robo_html_document_metadata_t meta;
    size_t kinds[ROBO_HTML_NODE_KIND_COUNT];
    robo_html_tree_report_t rep;

    dm_fixture_begin();
    id = dm_doc();
    dm_build(id, DM_COMPACT_DOC, NULL);

    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, kinds, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_READY);
    /* The build drove CREATED → LOADING → READY, so the previous state is known. */
    ROBO_CHECK_EQ_INT(meta.previous_lifecycle,
                      ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK_EQ_INT(meta.finalized, 1);
    ROBO_CHECK_EQ_INT(meta.is_empty, 0);
    ROBO_CHECK_EQ_INT(meta.root_ok, 1);

    /* document + doctype + html head title body p + 2 text + 1 comment */
    ROBO_CHECK_EQ_INT(meta.node_count, 10u);
    ROBO_CHECK_EQ_INT(meta.element_count, 5u);  /* html head title body p */
    ROBO_CHECK_EQ_INT(meta.text_count, 2u);     /* "T", "a" */
    ROBO_CHECK_EQ_INT(meta.comment_count, 1u);
    ROBO_CHECK_EQ_INT(meta.doctype_count, 1u);
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_ELEMENT], 5u);
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_TEXT], 2u);
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_COMMENT], 1u);
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_DOCUMENT_TYPE], 1u);
    ROBO_CHECK_EQ_INT(kinds[ROBO_HTML_NODE_KIND_DOCUMENT], 1u);

    /* The metadata counts and the registry-derived count must agree. */
    ROBO_CHECK_EQ_INT(meta.node_count, robo_html_document_node_count(id));

    /*
     * Depth is node depth: root 0, doctype/html 1, head/body 2,
     * title/p/comment 3, and the two text nodes at 4.
     */
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &rep, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rep.max_depth, 4u);
    dm_check_tree_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_metadata_misuse(void)
{
    robo_html_document_id_t id;
    robo_html_document_metadata_t meta;
    robo_html_diag_t diag;

    dm_fixture_begin();
    id = dm_doc();

    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* An unknown id is not found, and the snapshot is left zeroed. */
    robo_html_diag_reset(&diag);
    memset(&meta, 0xAB, sizeof(meta));
    ROBO_CHECK_EQ_INT(
        robo_html_document_metadata(0xDEADu, &meta, NULL, &diag),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_DOCUMENT);
    ROBO_CHECK_EQ_INT(meta.node_count, 0u);
    ROBO_CHECK_EQ_INT(meta.root_ok, 0);

    /* out_kind_count is optional; passing NULL is not an error. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_metadata(id, &meta, NULL, NULL), ROBO_OK);

    /* A destroyed document reports NOT_FOUND, not stale data. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_metadata(id, &meta, NULL, NULL), ROBO_NOT_FOUND);

    dm_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 2. Root (items 7-9, 23)                                             */
/* ------------------------------------------------------------------ */

static void test_dm_root(void)
{
    robo_html_document_id_t a;
    robo_html_document_id_t b;
    robo_html_node_handle_t root_a;
    robo_html_node_handle_t root_b;
    robo_html_node_view_t view;
    robo_html_node_kind_t kind = ROBO_HTML_NODE_KIND_FRAGMENT;

    dm_fixture_begin();
    a = dm_doc();
    b = dm_doc();

    root_a = robo_html_document_root(a);
    root_b = robo_html_document_root(b);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root_a));
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root_b));

    /* The root is a document node with no parent. */
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&root_a, &kind), ROBO_OK);
    ROBO_CHECK_EQ_INT(kind, ROBO_HTML_NODE_KIND_DOCUMENT);
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&root_a, &view, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&view.parent));
    ROBO_CHECK_EQ_INT(view.kind, ROBO_HTML_NODE_KIND_DOCUMENT);

    /* Identity is the document id, so two roots are never equal. */
    ROBO_CHECK(!robo_html_node_handle_equal(&root_a, &root_b));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_document(&root_a), a);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_document(&root_b), b);

    /* Ownership: a root belongs to its own document and to no other. */
    ROBO_CHECK(robo_html_document_owns_node(a, &root_a));
    ROBO_CHECK(!robo_html_document_owns_node(b, &root_a));
    ROBO_CHECK(!robo_html_document_owns_node(a, &root_b));
    ROBO_CHECK(robo_html_document_owns_node(b, &root_b));

    /* Retrieval is stable, and an unknown document has no root at all. */
    {
        robo_html_node_handle_t again = robo_html_document_root(a);
        robo_html_node_handle_t none = robo_html_document_root(0xDEADu);

        ROBO_CHECK(robo_html_node_handle_equal(&root_a, &again));
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&none));
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(a, NULL), ROBO_OK);
    /* A retired root is stale, and the document no longer hands one out. */
    ROBO_CHECK(robo_html_node_handle_is_stale(&root_a));
    ROBO_CHECK(!robo_html_document_owns_node(a, &root_a));
    {
        robo_html_node_handle_t gone = robo_html_document_root(a);

        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&gone));
    }
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(b, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_root_survives_rebuild(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root_before;
    robo_html_node_handle_t root_after;
    robo_html_node_handle_t p_before;

    dm_fixture_begin();
    id = dm_doc();
    dm_build(id, "<p>one</p>", NULL);

    root_before = robo_html_document_root(id);
    p_before = dm_find(id, "p");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&p_before));

    /* Re-parsing replaces the content but not the document's own root. */
    dm_build(id, "<div>two</div>", NULL);
    root_after = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_equal(&root_before, &root_after));
    ROBO_CHECK(robo_html_document_owns_node(id, &root_before));
    ROBO_CHECK(!robo_html_node_handle_is_stale(&root_before));

    /* Every other node from the first tree is retired. */
    ROBO_CHECK(robo_html_node_handle_is_stale(&p_before));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&p_before, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_document_owns_node(id, &p_before));

    /* And the rebuilt tree is itself consistent. */
    ROBO_CHECK(dm_has(id, "div"));
    dm_check_tree_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 3. Identity, ownership and staleness (items 10-14)                   */
/* ------------------------------------------------------------------ */

static void test_dm_node_identity_and_lookup(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_node_handle_t by_id;
    robo_html_node_handle_t p;
    robo_html_diag_t diag;

    dm_fixture_begin();
    id = dm_doc();
    dm_build(id, "<p>x</p>", NULL);

    /* Root id → handle round-trips to the identical identity. */
    root = robo_html_document_root(id);
    ROBO_CHECK_EQ_INT(
        robo_html_document_node_by_id(id, root.node_id, &by_id, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&root, &by_id));

    p = dm_find(id, "p");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&p));
    ROBO_CHECK_EQ_INT(
        robo_html_document_node_by_id(id, p.node_id, &by_id, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_equal(&p, &by_id));
    ROBO_CHECK(robo_html_document_owns_node(id, &p));

    /* Ids are dense: each live node occupies id == slot + 1. */
    {
        size_t count = robo_html_document_node_count(id);
        size_t i;
        int dense = 1;

        for (i = 1u; i <= count; i++) {
            robo_html_node_handle_t h;

            if (robo_html_document_node_by_id(id, i, &h, NULL) != ROBO_OK ||
                h.node_id != i) {
                dense = 0;
            }
        }
        ROBO_CHECK(dense);
        /* And one past the end is genuinely absent. */
        ROBO_CHECK_EQ_INT(robo_html_document_node_by_id(
                              id, (robo_html_node_id_t)count + 1u, &by_id, NULL),
                          ROBO_NOT_FOUND);
    }

    /* Argument and lookup failures are reported, and never leave a handle. */
    ROBO_CHECK_EQ_INT(robo_html_document_node_by_id(id, root.node_id, NULL,
                                                    NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_document_node_by_id(id, ROBO_HTML_NODE_ID_NONE, &by_id, &diag),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_TREE);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&by_id));

    memset(&by_id, 0xAB, sizeof(by_id));
    ROBO_CHECK_EQ_INT(robo_html_document_node_by_id(id, 9999u, &by_id, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&by_id));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_stale_handles(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t h;
    robo_html_node_handle_t forged;
    robo_html_node_kind_t kind = ROBO_HTML_NODE_KIND_FRAGMENT;

    dm_fixture_begin();
    id = dm_doc();
    dm_build(id, "<h1>H</h1>", NULL);
    h = dm_find(id, "h1");
    ROBO_CHECK(!robo_html_node_handle_is_stale(&h));

    /*
     * A forged generation on a live id must be stale. Staleness is a
     * generation question, not only an id question, so resolve and
     * is_stale have to agree here.
     */
    forged = h;
    forged.generation = h.generation + 1000u;
    ROBO_CHECK(robo_html_node_handle_is_stale(&forged));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&forged, &kind),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_document_owns_node(id, &forged));

    /* A handle whose document is unknown is stale, not merely unresolvable. */
    forged = h;
    forged.document_id = 0xDEADu;
    ROBO_CHECK(robo_html_node_handle_is_stale(&forged));

    /* Retired by a rebuild: is_stale, resolve and ownership all agree. */
    dm_build(id, "<p>P</p>", NULL);
    ROBO_CHECK(robo_html_node_handle_is_stale(&h));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_resolve(&h, &kind), ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_document_owns_node(id, &h));

    /* An ill-formed handle is an argument error, never "stale". */
    {
        robo_html_node_handle_t zero;

        robo_html_node_handle_clear(&zero);
        ROBO_CHECK(!robo_html_node_handle_is_stale(&zero));
        ROBO_CHECK(!robo_html_document_owns_node(id, &zero));
        ROBO_CHECK(!robo_html_node_handle_is_stale(NULL));
        ROBO_CHECK(!robo_html_document_owns_node(id, NULL));
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_cross_document(void)
{
    robo_html_document_id_t a;
    robo_html_document_id_t b;
    robo_html_node_handle_t pa;
    robo_html_node_handle_t pb;

    dm_fixture_begin();
    a = dm_doc();
    b = dm_doc();
    dm_build(a, "<p>a</p>", NULL);
    dm_build(b, "<p>b</p>", NULL);

    pa = dm_find(a, "p");
    pb = dm_find(b, "p");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&pa));
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&pb));

    /* Same shape, same local ids — but ownership never crosses documents. */
    ROBO_CHECK(robo_html_document_owns_node(a, &pa));
    ROBO_CHECK(robo_html_document_owns_node(b, &pb));
    ROBO_CHECK(!robo_html_document_owns_node(b, &pa));
    ROBO_CHECK(!robo_html_document_owns_node(a, &pb));
    ROBO_CHECK_EQ_INT(robo_html_node_handle_document(&pa), a);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_document(&pb), b);

    /* Node ids may coincide while identities stay distinct. */
    if (pa.node_id == pb.node_id) {
        ROBO_CHECK(pa.generation != pb.generation ||
                   pa.document_id != pb.document_id);
    }

    /* A node from a keeps resolving in a even as b is rebuilt. */
    dm_build(b, "<div>other</div>", NULL);
    ROBO_CHECK(robo_html_document_owns_node(a, &pa));
    ROBO_CHECK(!robo_html_node_handle_is_stale(&pa));
    ROBO_CHECK(robo_html_node_handle_is_stale(&pb));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(b, NULL), ROBO_OK);
    dm_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 4. Invariant auditing (items 14, 24)                                */
/* ------------------------------------------------------------------ */

static void test_dm_invariants_clean(void)
{
    robo_html_document_id_t id;
    robo_html_tree_report_t rep;

    dm_fixture_begin();
    id = dm_doc();

    /* An empty document is consistent: one node, the root, nothing else. */
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &rep, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rep.ok, 1);
    ROBO_CHECK_EQ_INT(rep.registered, 1u);
    ROBO_CHECK_EQ_INT(rep.nodes, 1u);
    ROBO_CHECK_EQ_INT(rep.max_depth, 0u);

    dm_build(id, DM_COMPACT_DOC, NULL);
    dm_check_tree_clean(id);
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &rep, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rep.nodes, 10u);
    ROBO_CHECK_EQ_INT(rep.max_depth, 4u); /* the two text nodes sit deepest */

    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_html_diag_t diag;

        robo_html_diag_reset(&diag);
        ROBO_CHECK_EQ_INT(
            robo_html_document_validate_tree(0xDEADu, &rep, &diag),
            ROBO_NOT_FOUND);
        /* The report is zeroed even when the audit could not run. */
        ROBO_CHECK_EQ_INT(rep.ok, 0);
        ROBO_CHECK_EQ_INT(rep.registered, 0u);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_invariants_orphan_detected(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t stray;
    robo_html_tree_report_t rep;

    dm_fixture_begin();
    id = dm_doc();

    /*
     * Registering a node without linking it produces a node the tree cannot
     * reach. That is reachable through the public construction API, which is
     * exactly why the audit must notice it: an unreachable live node is a
     * leak that teardown would still reclaim but rendering would never see.
     */
    ROBO_CHECK_EQ_INT(robo_html_document_register_node(
                          id, ROBO_HTML_NODE_KIND_ELEMENT,
                          ROBO_HTML_NODE_ORIGIN_TEST, &stray, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &rep, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rep.registered, 2u);
    ROBO_CHECK_EQ_INT(rep.nodes, 1u);   /* only the root is reachable */
    ROBO_CHECK_EQ_INT(rep.orphans, 1u);
    ROBO_CHECK_EQ_INT(rep.root_ok, 1);
    ROBO_CHECK_EQ_INT(rep.ok, 0);

    /* Clearing the tree removes both the stray node and the report. */
    dm_build(id, "<p>x</p>", NULL);
    dm_check_tree_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_invariants_deep_chain(void)
{
    robo_html_document_id_t id;
    robo_html_tree_report_t rep;
    char html[4096];
    size_t used = 0u;
    int i;

    dm_fixture_begin();
    id = dm_doc();
    for (i = 0; i < 300; i++) {
        html[used++] = '<';
        html[used++] = 'd';
        html[used++] = 'i';
        html[used++] = 'v';
        html[used++] = '>';
    }
    html[used++] = 'x';
    html[used] = '\0';
    dm_build(id, html, NULL);

    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &rep, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rep.ok, 1);
    /* root + html head body + 300 divs + 1 text */
    ROBO_CHECK_EQ_INT(rep.registered, 305u);
    ROBO_CHECK_EQ_INT(rep.nodes, 305u);
    /* html 1, body 2, 300 nested divs → 302, and the text inside the last. */
    ROBO_CHECK_EQ_INT(rep.max_depth, 303u);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 5. Source and encoding association (items 15-17)                    */
/* ------------------------------------------------------------------ */

static void test_dm_source_association(void)
{
    robo_html_document_id_t id;
    robo_html_document_metadata_t meta;
    robo_html_source_t src;

    dm_fixture_begin();
    id = dm_doc();

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "<p>hello</p>", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_source_set_name(&src, "Applications/Demo.html",
                                               NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.source_kind, ROBO_HTML_SOURCE_KIND_STRING);
    ROBO_CHECK_EQ_INT(meta.source_length, strlen("<p>hello</p>"));
    ROBO_CHECK_STR_EQ(meta.source_name, "Applications/Demo.html");
    ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);

    /* Detaching clears the association rather than leaving it dangling. */
    ROBO_CHECK_EQ_INT(robo_html_document_detach_source(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.source_kind, ROBO_HTML_SOURCE_KIND_NONE);
    ROBO_CHECK_EQ_INT(meta.source_length, 0u);
    ROBO_CHECK_STR_EQ(meta.source_name, "");
    ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_READY);

    /* Detach with nothing attached is a no-op, not an error. */
    ROBO_CHECK_EQ_INT(robo_html_document_detach_source(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(0xDEADu, &src, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_source_bytes_are_not_borrowed_by_the_tree(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t p;
    robo_html_source_t src;
    char buffer[64];
    char text[32];
    size_t length = 0u;

    dm_fixture_begin();
    id = dm_doc();

    strcpy(buffer, "<p>keep me</p>");
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, buffer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);

    /*
     * The tree must own what it renders. Scribbling over the source buffer
     * after the build proves the document copied everything it needs rather
     * than pointing back into caller memory.
     */
    memset(buffer, 'Z', sizeof(buffer));

    p = dm_find(id, "p");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&p));
    ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&p, text, sizeof(text),
                                                  &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(text, "keep me");
    ROBO_CHECK_EQ_INT(length, strlen("keep me"));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_encoding_association(void)
{
    robo_html_document_id_t id;
    robo_html_document_metadata_t meta;

    dm_fixture_begin();
    id = dm_doc();

    /* A declared label reaches the document's encoding state. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_input_encoding_declared(id, "UTF-8", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.encoding.declared, ROBO_HTML_ENCODING_UTF_8);
    ROBO_CHECK_EQ_INT(meta.encoding.actual, ROBO_HTML_ENCODING_UTF_8);
    ROBO_CHECK_EQ_INT(meta.encoding.status, ROBO_HTML_ENCODING_STATUS_OK);

    /* An unsupported-but-valid label is recorded, not fatal. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_input_encoding_declared(id, "Shift_JIS", NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.encoding.declared, ROBO_HTML_ENCODING_SHIFT_JIS);

    /* Encoding state is part of the document, so it survives a build. */
    dm_build(id, "<p>x</p>", NULL);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.encoding.declared, ROBO_HTML_ENCODING_SHIFT_JIS);

    /* Reset drops it along with the tree. */
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.encoding.declared, ROBO_HTML_ENCODING_UNKNOWN);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 6. Finalization and reuse (items 18, 30)                            */
/* ------------------------------------------------------------------ */

static void test_dm_finalize(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t extra;
    robo_html_diag_t diag;
    robo_html_document_metadata_t meta;

    dm_fixture_begin();
    id = dm_doc();

    /* A fresh document accepts construction. */
    ROBO_CHECK(robo_html_document_allows_construction(id));
    ROBO_CHECK(!robo_html_document_is_finalized(id));

    dm_build(id, "<p>x</p>", NULL);

    /* A completed build closes construction. */
    ROBO_CHECK(robo_html_document_is_finalized(id));
    ROBO_CHECK(!robo_html_document_allows_construction(id));
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.finalized, 1);

    robo_html_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_html_document_register_node(
                          id, ROBO_HTML_NODE_KIND_ELEMENT,
                          ROBO_HTML_NODE_ORIGIN_TEST, &extra, &diag),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_TREE);

    /* Finalization is idempotent. */
    ROBO_CHECK_EQ_INT(robo_html_document_finalize(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_finalize(id, NULL), ROBO_OK);

    /* Reading is unaffected: the tree is still fully usable. */
    dm_check_tree_clean(id);
    {
        robo_html_node_handle_t root = robo_html_document_root(id);
        size_t count = 0u;

        ROBO_CHECK(robo_html_document_owns_node(id, &root));
        ROBO_CHECK_EQ_INT(robo_html_node_child_count(&root, &count, NULL),
                          ROBO_OK);
        ROBO_CHECK(count > 0u);
    }

    /* A replacement source reopens the document for parsing. */
    {
        robo_html_source_t src;

        robo_html_source_init(&src);
        ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "<p>y</p>", NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                          ROBO_OK);
        ROBO_CHECK(!robo_html_document_is_finalized(id));
        ROBO_CHECK(robo_html_document_allows_construction(id));
    }
    /* And a build closes it again. */
    dm_build(id, "<p>z</p>", NULL);
    ROBO_CHECK(robo_html_document_is_finalized(id));

    /* Reset reopens it once more. */
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_document_is_finalized(id));
    ROBO_CHECK(robo_html_document_allows_construction(id));

    /* Finalizing an unknown document is NOT_FOUND, not a silent success. */
    ROBO_CHECK_EQ_INT(robo_html_document_finalize(0xDEADu, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_document_is_finalized(0xDEADu), 0);
    ROBO_CHECK_EQ_INT(robo_html_document_allows_construction(0xDEADu), 0);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_reset(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root_before;
    robo_html_node_handle_t p;
    robo_html_node_handle_t root_after;
    robo_html_document_metadata_t meta;

    dm_fixture_begin();
    id = dm_doc();

    /* Reset on a document that never loaded anything keeps it CREATED. */
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id,
                                                       &meta.lifecycle),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    ROBO_CHECK(robo_html_document_is_empty(id));

    dm_build(id, "<p>one</p>", NULL);
    root_before = robo_html_document_root(id);
    p = dm_find(id, "p");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&p));

    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    /* A document that had loaded settles in READY with an empty tree. */
    ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_READY);
    ROBO_CHECK_EQ_INT(meta.is_empty, 1);
    ROBO_CHECK_EQ_INT(meta.node_count, 1u);
    ROBO_CHECK_EQ_INT(meta.source_length, 0u);
    ROBO_CHECK_STR_EQ(meta.source_name, "");
    ROBO_CHECK_EQ_INT(meta.finalized, 0);
    ROBO_CHECK(robo_html_document_allows_construction(id));

    /* Identity survives: the root is the same node, content is not. */
    root_after = robo_html_document_root(id);
    ROBO_CHECK(robo_html_node_handle_equal(&root_before, &root_after));
    ROBO_CHECK(robo_html_document_owns_node(id, &root_after));
    ROBO_CHECK(robo_html_node_handle_is_stale(&p));

    dm_check_tree_clean(id);

    /* The reset document is fully usable for a new parse. */
    dm_build(id, "<div>two</div>", NULL);
    ROBO_CHECK(dm_has(id, "div"));
    dm_check_tree_clean(id);

    ROBO_CHECK_EQ_INT(robo_html_document_reset(0xDEADu, NULL), ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_reset_refused_while_building(void)
{
    robo_html_document_id_t id;
    robo_html_tree_builder_t *builder = NULL;
    robo_html_diag_t diag;

    dm_fixture_begin();
    id = dm_doc();

    /* A live builder holds the document lease and its own tree state. */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &builder, NULL),
                      ROBO_OK);
    ROBO_CHECK(builder != NULL);

    robo_html_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, &diag), ROBO_BUSY);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_DOCUMENT);

    robo_html_tree_builder_destroy(builder);
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 7. Failure and teardown (items 19-22, 30)                           */
/* ------------------------------------------------------------------ */

static void test_dm_failed_state(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t node;
    robo_html_document_metadata_t meta;
    robo_html_diag_t diag;

    dm_fixture_begin();
    id = dm_doc();

    ROBO_CHECK_EQ_INT(robo_html_document_mark_failed(
                          id, ROBO_HTML_DOMAIN_SOURCE, "source unavailable",
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED);
    ROBO_CHECK_EQ_INT(meta.fail_domain, ROBO_HTML_DOMAIN_SOURCE);
    ROBO_CHECK_STR_EQ(meta.fail_message, "source unavailable");

    /* The failure record is sticky: a later failure does not overwrite it. */
    ROBO_CHECK_EQ_INT(robo_html_document_mark_failed(
                          id, ROBO_HTML_DOMAIN_ENCODING, "second", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.fail_domain, ROBO_HTML_DOMAIN_SOURCE);
    ROBO_CHECK_STR_EQ(meta.fail_message, "source unavailable");

    /* A failed document accepts no construction, no finalization, no reset. */
    ROBO_CHECK(!robo_html_document_allows_construction(id));
    ROBO_CHECK_EQ_INT(robo_html_document_finalize(id, NULL), ROBO_ERROR);
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_ERROR);
    robo_html_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_html_document_register_node(
                          id, ROBO_HTML_NODE_KIND_ELEMENT,
                          ROBO_HTML_NODE_ORIGIN_TEST, &node, &diag),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_NODE);

    /* The root is still readable, and teardown still works. */
    {
        robo_html_node_handle_t root = robo_html_document_root(id);

        ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    }
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_document_is_live(id));
    dm_fixture_end();
}

static void test_dm_teardown(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_tree_report_t rep;
    robo_html_document_metadata_t meta;

    dm_fixture_begin();
    id = dm_doc();
    dm_build(id, DM_COMPACT_DOC, NULL);
    root = robo_html_document_root(id);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_live_document_count(), 0u);

    /* Everything that named the document now reports it as gone. */
    ROBO_CHECK(!robo_html_document_is_live(id));
    ROBO_CHECK(robo_html_node_handle_is_stale(&root));
    ROBO_CHECK(!robo_html_document_owns_node(id, &root));
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(id), 0u);
    ROBO_CHECK(!robo_html_document_is_empty(id));
    ROBO_CHECK(!robo_html_document_is_finalized(id));
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &meta.lifecycle),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &rep, NULL),
                      ROBO_NOT_FOUND);

    /* Repeated teardown is reported, never undefined. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_NOT_FOUND);

    dm_fixture_end();
}

static void test_dm_large_tree_cleanup(void)
{
    robo_html_document_id_t id;
    robo_html_document_metadata_t meta;
    robo_html_tree_report_t rep;
    char html[32768];
    size_t used = 0u;
    int i;

    dm_fixture_begin();
    id = dm_doc();
    for (i = 0; i < 1500 && used + 6u < sizeof(html); i++) {
        html[used++] = '<';
        html[used++] = 'd';
        html[used++] = 'i';
        html[used++] = 'v';
        html[used++] = '>';
    }
    html[used++] = 'x';
    html[used] = '\0';
    dm_build(id, html, NULL);

    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.element_count, 1503u); /* html head body + 1500 */
    ROBO_CHECK_EQ_INT(meta.node_count, robo_html_document_node_count(id));
    ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(id, &rep, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(rep.ok, 1);
    ROBO_CHECK_EQ_INT(rep.registered, 1505u);

    /* A deep tree tears down in one step and leaves nothing behind. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_live_document_count(), 0u);

    dm_fixture_end();
}

/* ------------------------------------------------------------------ */
/* 8. Pipeline integration (items 25, 26, 29)                          */
/* ------------------------------------------------------------------ */

static void test_dm_pipeline_integration(void)
{
    robo_html_document_id_t id;
    robo_html_tree_builder_t *builder = NULL;
    robo_html_source_t src;
    robo_html_node_handle_t root;

    dm_fixture_begin();
    id = dm_doc();

    /*
     * Drive the builder step by step, as a caller with an incremental
     * source would: the document must be consistent at each stage, and
     * construction must stay open until the build finishes.
     */
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(&src, DM_COMPACT_DOC, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_create(id, &builder, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_begin(builder, &src, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_document_allows_construction(id));
    ROBO_CHECK(!robo_html_document_is_finalized(id));

    {
        int guard = 0;

        while (guard++ < 200) {
            ROBO_CHECK_EQ_INT(robo_html_tree_builder_next(builder, NULL),
                              ROBO_OK);
            if (robo_html_tree_builder_diagnostic_count(builder) > 0u) {
                break;
            }
            {
                robo_html_parser_state_t state = ROBO_HTML_PARSER_STATE_IDLE;

                ROBO_CHECK_EQ_INT(
                    robo_html_tree_builder_get_state(builder, &state), ROBO_OK);
                if (state == ROBO_HTML_PARSER_STATE_COMPLETE) {
                    break;
                }
            }
        }
    }

    /* The builder consumed EOF, which finishes the build. */
    ROBO_CHECK_EQ_INT(robo_html_tree_builder_finish(builder, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_document_is_finalized(id));

    root = robo_html_document_root(id);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    ROBO_CHECK(robo_html_document_owns_node(id, &root));

    /* Destroying the builder leaves the document's tree intact and owned. */
    robo_html_tree_builder_destroy(builder);
    dm_check_tree_clean(id);
    ROBO_CHECK(dm_has(id, "title"));
    ROBO_CHECK(dm_has(id, "p"));

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_demo_document(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t body;
    robo_html_node_handle_t child;
    char buf[128];

    dm_fixture_begin();
    id = dm_doc();

    /* The visual checkpoint's document, through the document model. */
    dm_build(id,
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
             "</html>\n",
             NULL);

    dm_check_tree_clean(id);
    ROBO_CHECK(robo_html_document_is_finalized(id));

    body = dm_find(id, "body");
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&body));
    {
        size_t count = 0u;

        ROBO_CHECK_EQ_INT(robo_html_node_child_count(&body, &count, NULL),
                          ROBO_OK);
        ROBO_CHECK(count > 0u);
        /* The demo renderer walks the real tree: the first child resolves. */
        ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, 0u, &child, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_document_owns_node(id, &child));
    }

    ROBO_CHECK_EQ_INT(
        robo_html_node_find_child_by_name(&body, "h1", &child, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&child, buf, sizeof(buf),
                                                  NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "RoboticelaOS Runtime");

    ROBO_CHECK_EQ_INT(
        robo_html_node_find_child_by_name(&body, "button", &child, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&child, buf, sizeof(buf),
                                                  NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Demo Button");

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    dm_fixture_end();
}

static void test_dm_unicode_and_malformed(void)
{
    robo_html_document_id_t id;
    robo_html_node_handle_t root;
    robo_html_document_metadata_t meta;
    char buf[128];
    size_t length = 0u;

    dm_fixture_begin();

    /* Unicode content survives anchoring into a document-owned tree. */
    id = dm_doc();
    dm_build(id,
             "<p>R\xc3\xa9sum\xc3\xa9 \xe2\x80\x94 "
             "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x98\x80</p>",
             NULL);
    dm_check_tree_clean(id);
    root = robo_html_document_root(id);
    ROBO_CHECK_EQ_INT(robo_html_node_subtree_text(&root, buf, sizeof(buf),
                                                  &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_document_owns_node(id, &root));
    ROBO_CHECK(strstr(buf, "R\xc3\xa9sum\xc3\xa9") != NULL);
    ROBO_CHECK(strstr(buf, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") != NULL);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);

    /*
     * Malformed markup is recovered, and the result is still a document the
     * model can vouch for: reachable, owned, and consistent.
     */
    id = dm_doc();
    dm_build(id, "<div><p>unclosed<span></div></b>text<<<", NULL);
    dm_check_tree_clean(id);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_READY);
    ROBO_CHECK(meta.node_count > 1u);
    ROBO_CHECK(dm_has(id, "div"));
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);

    /*
     * "Empty" has two distinct meanings and the model keeps them apart.
     * Attaching an empty source leaves the document empty; *parsing* empty
     * input produces the implied html/head/body structure, as a browser
     * does. Either way the result is a consistent, owned tree.
     */
    id = dm_doc();
    ROBO_CHECK(robo_html_document_is_empty(id));
    {
        robo_html_source_t empty;

        robo_html_source_init(&empty);
        ROBO_CHECK_EQ_INT(robo_html_source_string(&empty, "", NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &empty, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(
                              id, &meta.lifecycle),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(meta.lifecycle, ROBO_HTML_DOCUMENT_LIFECYCLE_READY);
        ROBO_CHECK(robo_html_document_is_empty(id));
    }

    dm_build(id, "", NULL);
    dm_check_tree_clean(id);
    ROBO_CHECK_EQ_INT(robo_html_document_metadata(id, &meta, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(meta.node_count, 4u); /* document + html head body */
    ROBO_CHECK_EQ_INT(meta.element_count, 3u);
    ROBO_CHECK_EQ_INT(meta.text_count, 0u);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);

    dm_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_html_document_model(void)
{
    robo_test_register("html.docmodel.metadata_fresh", test_dm_metadata_fresh);
    robo_test_register("html.docmodel.metadata_built", test_dm_metadata_built);
    robo_test_register("html.docmodel.metadata_misuse", test_dm_metadata_misuse);
    robo_test_register("html.docmodel.root", test_dm_root);
    robo_test_register("html.docmodel.root_stable",
                       test_dm_root_survives_rebuild);
    robo_test_register("html.docmodel.node_identity",
                       test_dm_node_identity_and_lookup);
    robo_test_register("html.docmodel.stale_handles", test_dm_stale_handles);
    robo_test_register("html.docmodel.cross_document", test_dm_cross_document);
    robo_test_register("html.docmodel.invariants_clean",
                       test_dm_invariants_clean);
    robo_test_register("html.docmodel.invariants_orphan",
                       test_dm_invariants_orphan_detected);
    robo_test_register("html.docmodel.invariants_deep",
                       test_dm_invariants_deep_chain);
    robo_test_register("html.docmodel.source", test_dm_source_association);
    robo_test_register("html.docmodel.source_ownership",
                       test_dm_source_bytes_are_not_borrowed_by_the_tree);
    robo_test_register("html.docmodel.encoding", test_dm_encoding_association);
    robo_test_register("html.docmodel.finalize", test_dm_finalize);
    robo_test_register("html.docmodel.reset", test_dm_reset);
    robo_test_register("html.docmodel.reset_busy",
                       test_dm_reset_refused_while_building);
    robo_test_register("html.docmodel.failed", test_dm_failed_state);
    robo_test_register("html.docmodel.teardown", test_dm_teardown);
    robo_test_register("html.docmodel.large_cleanup",
                       test_dm_large_tree_cleanup);
    robo_test_register("html.docmodel.pipeline", test_dm_pipeline_integration);
    robo_test_register("html.docmodel.demo_document", test_dm_demo_document);
    robo_test_register("html.docmodel.unicode_malformed",
                       test_dm_unicode_and_malformed);
}
