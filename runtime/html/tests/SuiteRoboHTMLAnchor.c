/*
 * RoboHTML Phase 8.13 anchor suite (html.anchor.*).
 *
 * What it pins, in the order the phase argues for it: that an `<a>` is a real
 * DOM element with a real attribute view; that a reference resolves to the
 * thing the header documents, including the cases that are *refused*; that a
 * fragment activation completes because it needs no I/O; that everything else
 * either reaches the caller's seam or is refused before it; and that no path
 * anywhere can report a navigation that did not happen.
 *
 * Two tests here are security tests rather than feature tests, and they are
 * the two worth reading first:
 *
 *   html.anchor.activate_refused_schemes
 *       `javascript:`, `file:` and `data:` must never reach the handler —
 *       asserted by a handler that counts its own calls.
 *   html.anchor.activate_no_fake_success
 *       an activation with no handler reports UNSUPPORTED, not a success.
 *
 * The fixture parses real HTML through the real tokenizer and tree builder and
 * names its documents, because the document's own reference is what a relative
 * href resolves against — a test that skipped the name would be testing a
 * resolution the runtime never performs.
 */

#include "RoboBase/RoboBase.h"
#include "RoboMem/RoboMem.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLAnchor.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static void anchor_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static robo_html_document_id_t anchor_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    anchor_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/** Parse @p text into @p id, with no document reference (no base). */
static void anchor_build(robo_html_document_id_t id, const char *text)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);
}

/**
 * Parse @p text into @p id with @p name as the document's own reference.
 *
 * The name is the base every relative href in this suite resolves against, so
 * it is set through the real source API rather than by poking the document —
 * the parser is what records it, and the parser is what a caller would use.
 */
static void anchor_build_named(robo_html_document_id_t id, const char *name,
                               const char *text)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_source_set_name(&src, name, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);
    /* The base really arrived, or every relative expectation below would be
     * measuring a resolution the runtime never had an input for. */
    ROBO_CHECK_STR_EQ(robo_html_document_source_name(id), name);
}

/** The document's lifecycle state, asserted to be observable. */
static robo_html_document_lifecycle_t anchor_lifecycle(
    robo_html_document_id_t id)
{
    robo_html_document_lifecycle_t life =
        ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED;

    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    return life;
}

static robo_html_node_handle_t anchor_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

typedef struct anchor_tag_query {
    robo_html_tag_t tag;
} anchor_tag_query_t;

static int anchor_match_tag(const robo_html_node_view_t *view, void *user)
{
    const anchor_tag_query_t *q = (const anchor_tag_query_t *)user;

    return robo_html_element_is_tag(&view->handle, q->tag);
}

/** First element of @p tag at or below @p start; asserts it was found. */
static robo_html_node_handle_t anchor_find(const robo_html_node_handle_t *start,
                                           robo_html_tag_t tag)
{
    anchor_tag_query_t q;
    robo_html_node_handle_t out;

    q.tag = tag;
    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(
        robo_html_node_find_first(start, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  anchor_match_tag, &q, &out, NULL),
        ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

/**
 * The subtree text of @p node, through the DOM's own reader.
 *
 * One static buffer, so a caller compares one node's text at a time — which is
 * exactly what this suite does, and the reason the return is a plain string
 * rather than an out-parameter.
 */
static const char *anchor_text(const robo_html_node_handle_t *node)
{
    static char buf[512];
    size_t length = 0u;

    memset(buf, 0, sizeof(buf));
    ROBO_CHECK_EQ_INT(
        robo_html_node_subtree_text(node, buf, sizeof(buf), &length, NULL),
        ROBO_OK);
    ROBO_CHECK(length < sizeof(buf));
    return buf;
}

/** Number of elements of @p tag in the whole document. */
typedef struct anchor_count_ctx {
    robo_html_tag_t tag;
    size_t count;
} anchor_count_ctx_t;

static robo_result_t anchor_count_visit(const robo_html_node_view_t *view,
                                        size_t depth, void *user)
{
    anchor_count_ctx_t *ctx = (anchor_count_ctx_t *)user;

    (void)depth;
    if (robo_html_element_is_tag(&view->handle, ctx->tag)) {
        ctx->count++;
    }
    return ROBO_OK;
}

static size_t anchor_count(robo_html_document_id_t id, robo_html_tag_t tag)
{
    robo_html_node_handle_t root = anchor_root(id);
    anchor_count_ctx_t ctx;

    ctx.tag = tag;
    ctx.count = 0u;
    ROBO_CHECK_EQ_INT(
        robo_html_node_walk_pre_order(&root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                      anchor_count_visit, &ctx, NULL),
        ROBO_OK);
    return ctx.count;
}

/**
 * Everything one handler was told, and how it answered.
 *
 * The call count is the security assertion in this suite: "the handler was
 * never consulted" is only checkable by asking the handler afterwards.
 */
typedef struct anchor_handler_log {
    int calls;
    char base[128];
    char target_attribute[32];
    robo_html_link_target_context_t target_context;
    char rel[64];
    int download_requested;
    char download_name[64];
    char title[64];
    robo_html_navigation_reason_t reason;
    robo_html_document_id_t document_id;
    char anchor_name[32];
    robo_html_target_kind_t target_kind;
    char resolved[128];
    char scheme[32];
    /* Answer policy. */
    int decline;
} anchor_handler_log_t;

static void anchor_log_reset(anchor_handler_log_t *log)
{
    memset(log, 0, sizeof(*log));
    log->target_context = ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT;
}

static robo_result_t anchor_handler(void *userdata,
                                    const robo_html_navigation_request_t *req,
                                    robo_html_navigation_outcome_kind_t *out_kind,
                                    robo_html_diag_t *diag)
{
    anchor_handler_log_t *log = (anchor_handler_log_t *)userdata;
    const char *s;
    size_t length = 0u;

    (void)diag;
    (void)out_kind;
    log->calls++;
    log->document_id = req->document_id;
    log->target_kind = robo_html_navigation_target_kind(req->target);
    log->target_context = req->target_context;
    log->download_requested = req->download_requested;
    log->reason = req->reason;

    if (req->base) {
        size_t n = req->base_length < sizeof(log->base) - 1u
                       ? req->base_length
                       : sizeof(log->base) - 1u;
        memcpy(log->base, req->base, n);
        log->base[n] = '\0';
    }
    if (req->target_attribute) {
        size_t n = req->target_length < sizeof(log->target_attribute) - 1u
                       ? req->target_length
                       : sizeof(log->target_attribute) - 1u;
        memcpy(log->target_attribute, req->target_attribute, n);
        log->target_attribute[n] = '\0';
    }
    if (req->rel) {
        size_t n = req->rel_length < sizeof(log->rel) - 1u
                       ? req->rel_length
                       : sizeof(log->rel) - 1u;
        memcpy(log->rel, req->rel, n);
        log->rel[n] = '\0';
    }
    if (req->download_name) {
        size_t n = req->download_name_length < sizeof(log->download_name) - 1u
                       ? req->download_name_length
                       : sizeof(log->download_name) - 1u;
        memcpy(log->download_name, req->download_name, n);
        log->download_name[n] = '\0';
    }
    if (req->title) {
        size_t n = req->title_length < sizeof(log->title) - 1u
                       ? req->title_length
                       : sizeof(log->title) - 1u;
        memcpy(log->title, req->title, n);
        log->title[n] = '\0';
    }
    s = robo_html_element_attribute_value(&req->anchor, "data-name", &length);
    if (s) {
        size_t n = length < sizeof(log->anchor_name) - 1u
                       ? length
                       : sizeof(log->anchor_name) - 1u;
        memcpy(log->anchor_name, s, n);
        log->anchor_name[n] = '\0';
    }
    s = robo_html_navigation_target_resolved(req->target, &length);
    if (s) {
        size_t n = length < sizeof(log->resolved) - 1u ? length
                                                       : sizeof(log->resolved) - 1u;
        memcpy(log->resolved, s, n);
        log->resolved[n] = '\0';
    }
    s = robo_html_navigation_target_scheme(req->target, &length);
    if (s) {
        size_t n = length < sizeof(log->scheme) - 1u ? length
                                                     : sizeof(log->scheme) - 1u;
        memcpy(log->scheme, s, n);
        log->scheme[n] = '\0';
    }

    if (log->decline) {
        return ROBO_NOT_SUPPORTED;
    }
    return ROBO_OK;
}

static robo_html_navigation_handler_t anchor_make_handler(
    anchor_handler_log_t *log)
{
    robo_html_navigation_handler_t handler;

    robo_html_navigation_handler_init(&handler);
    handler.navigate = anchor_handler;
    handler.userdata = log;
    return handler;
}

/** Resolve @p element's href and report the fields this suite asserts on. */
typedef struct anchor_resolved {
    robo_html_target_kind_t kind;
    char reference[256];
    char resolved[256];
    char fragment[128];
    char scheme[64];
    int has_reference;
    int has_resolved;
    int has_fragment;
    int has_scheme;
} anchor_resolved_t;

static void anchor_resolve(const robo_html_node_handle_t *element,
                           anchor_resolved_t *out)
{
    robo_html_navigation_target_t *target = NULL;
    const char *s;
    size_t length = 0u;

    memset(out, 0, sizeof(*out));
    ROBO_CHECK_EQ_INT(
        robo_html_element_navigation_target(element, &target, NULL), ROBO_OK);
    ROBO_CHECK(target != NULL);
    if (!target) {
        return;
    }
    out->kind = robo_html_navigation_target_kind(target);
    s = robo_html_navigation_target_reference(target, &length);
    if (s) {
        memcpy(out->reference, s, length);
        out->reference[length] = '\0';
        out->has_reference = 1;
    }
    s = robo_html_navigation_target_resolved(target, &length);
    if (s) {
        memcpy(out->resolved, s, length);
        out->resolved[length] = '\0';
        out->has_resolved = 1;
    }
    s = robo_html_navigation_target_fragment(target, &length);
    if (s) {
        memcpy(out->fragment, s, length);
        out->fragment[length] = '\0';
        out->has_fragment = 1;
    }
    s = robo_html_navigation_target_scheme(target, &length);
    if (s) {
        memcpy(out->scheme, s, length);
        out->scheme[length] = '\0';
        out->has_scheme = 1;
    }
    robo_html_navigation_target_destroy(target);
}

/** Resolve a bare reference against a bare base, with no document involved. */
static void anchor_resolve_raw(const char *href, const char *base,
                               anchor_resolved_t *out)
{
    robo_html_navigation_target_t *target = NULL;
    const char *s;
    size_t length = 0u;

    memset(out, 0, sizeof(*out));
    ROBO_CHECK_EQ_INT(robo_html_navigation_target_create(
                          href, href ? strlen(href) : 0u, base,
                          base ? strlen(base) : 0u, &target, NULL),
                      ROBO_OK);
    ROBO_CHECK(target != NULL);
    if (!target) {
        return;
    }
    out->kind = robo_html_navigation_target_kind(target);
    s = robo_html_navigation_target_reference(target, &length);
    if (s) {
        memcpy(out->reference, s, length);
        out->reference[length] = '\0';
        out->has_reference = 1;
    }
    s = robo_html_navigation_target_resolved(target, &length);
    if (s) {
        memcpy(out->resolved, s, length);
        out->resolved[length] = '\0';
        out->has_resolved = 1;
    }
    s = robo_html_navigation_target_fragment(target, &length);
    if (s) {
        memcpy(out->fragment, s, length);
        out->fragment[length] = '\0';
        out->has_fragment = 1;
    }
    s = robo_html_navigation_target_scheme(target, &length);
    if (s) {
        memcpy(out->scheme, s, length);
        out->scheme[length] = '\0';
        out->has_scheme = 1;
    }
    robo_html_navigation_target_destroy(target);
}

/* ------------------------------------------------------------------ */
/* The element                                                         */
/* ------------------------------------------------------------------ */

static void test_anchor_parser_element(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_element_view_t view;

    anchor_build_named(id, "App/Pages/Index.html",
                       "<div><p>before</p><a href=\"/Settings\" "
                       "title=\"Go\">Settings</a><span>after</span></div>");

    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    /* Identity through the 8.11 vocabulary, not a string comparison. */
    ROBO_CHECK(robo_html_element_is_tag(&a, ROBO_HTML_TAG_A));
    ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&a), (int)ROBO_HTML_TAG_A);
    ROBO_CHECK(robo_html_tag_is_text_level(robo_html_element_tag(&a)));
    ROBO_CHECK_EQ_INT(robo_html_element_view_get(&a, &view, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(view.name, "a");
    ROBO_CHECK_EQ_INT((int)view.classification,
                      (int)ROBO_HTML_ELEMENT_CLASS_TEXT);

    /* Not void: it holds the text, and the span after it stays a sibling. */
    {
        size_t children = 0u;

        ROBO_CHECK(!robo_html_tag_is_void(robo_html_element_tag(&a)));
        ROBO_CHECK_EQ_INT(robo_html_node_child_count(&a, &children, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)children, 1);
    }
    ROBO_CHECK_STR_EQ(anchor_text(&a), "Settings");
    {
        robo_html_node_handle_t parent;
        robo_html_node_handle_t next;

        ROBO_CHECK_EQ_INT(robo_html_element_parent(&a, &parent, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&parent, ROBO_HTML_TAG_DIV));
        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&a, &next, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&next, ROBO_HTML_TAG_SPAN));
    }

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_attributes(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_anchor_attributes_t attrs;

    anchor_build_named(id, "App/Index.html",
                       "<a href=\"/Settings\" target=\"_blank\" "
                       "rel=\"noopener noreferrer\" download=\"Plan.Robo\" "
                       "title=\"Settings page\" data-kind=\"nav\">S</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.href_present, 1);
    ROBO_CHECK_EQ_INT(attrs.href_usable, 1);
    ROBO_CHECK_EQ_INT((int)attrs.href_length, 9);
    ROBO_CHECK(memcmp(attrs.href, "/Settings", 9u) == 0);
    ROBO_CHECK_EQ_INT(attrs.target_present, 1);
    ROBO_CHECK(memcmp(attrs.target, "_blank", 6u) == 0);
    ROBO_CHECK_EQ_INT(attrs.rel_present, 1);
    ROBO_CHECK(memcmp(attrs.rel, "noopener noreferrer", 20u) == 0);
    ROBO_CHECK_EQ_INT(attrs.download_present, 1);
    ROBO_CHECK(memcmp(attrs.download, "Plan.Robo", 9u) == 0);
    ROBO_CHECK_EQ_INT(attrs.title_present, 1);
    ROBO_CHECK(memcmp(attrs.title, "Settings page", 13u) == 0);

    /* An arbitrary attribute is untouched by this phase, and still there. */
    ROBO_CHECK(robo_html_element_has_attribute(&a, "data-kind"));

    /* A bare anchor: nothing present, nothing usable. */
    anchor_build(id, "<a>Bare</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.href_present, 0);
    ROBO_CHECK_EQ_INT(attrs.href_usable, 0);
    ROBO_CHECK(attrs.href == NULL);
    ROBO_CHECK_EQ_INT(attrs.target_present, 0);
    ROBO_CHECK_EQ_INT(attrs.rel_present, 0);
    ROBO_CHECK_EQ_INT(attrs.download_present, 0);

    /*
     * Three situations that look alike and are not: no attribute, an empty
     * value, and a whitespace value. The first two are distinguishable by
     * `present`; the third is what `usable` exists for.
     */
    anchor_build(id, "<a href=\"\">Empty</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.href_present, 1);
    ROBO_CHECK_EQ_INT(attrs.href_usable, 0);
    ROBO_CHECK(attrs.href != NULL && attrs.href_length == 0u);

    anchor_build(id, "<a href=\"   \">Blank</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.href_present, 1);
    ROBO_CHECK_EQ_INT(attrs.href_usable, 0);

    /* A bare `download`: present with a zero-length name, not absent. */
    anchor_build(id, "<a href=\"/x\" download>Save</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.download_present, 1);
    ROBO_CHECK_EQ_INT((int)attrs.download_length, 0);

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_non_anchor_refused(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t p;
    robo_html_node_handle_t text;
    robo_html_anchor_attributes_t attrs;
    robo_html_navigation_target_t *target = NULL;
    robo_html_node_handle_t stale;
    robo_html_diag_t diag;

    anchor_build_named(id, "App/Index.html", "<p>Not a link</p>");
    root = anchor_root(id);
    p = anchor_find(&root, ROBO_HTML_TAG_P);

    /*
     * A live non-anchor is NOT_SUPPORTED; reporting "no href" instead would
     * let a caller mistake "not a link" for "a link with nothing to do".
     */
    robo_html_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&p, &attrs, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_HTML_DOMAIN_ANCHOR);
    ROBO_CHECK_EQ_INT(robo_html_element_navigation_target(&p, &target, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK(target == NULL);

    /* A text node is not an element at all, so it is NOT_FOUND, not
     * NOT_SUPPORTED — the two answers differ and both are asserted. */
    robo_html_node_handle_clear(&text);
    ROBO_CHECK_EQ_INT(robo_html_node_first_child(&p, &text, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&text));
    ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&text, &attrs, NULL),
                      ROBO_NOT_FOUND);

    /* Argument safety. */
    ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(NULL, &attrs, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&p, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_navigation_target(&p, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A handle whose document is gone is NOT_FOUND, never NOT_SUPPORTED:
     * "not a link" is a misleading thing to say about a dead node. */
    stale = p;
    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&stale, &attrs, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(
        robo_html_element_navigation_target(&stale, &target, NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK(target == NULL);
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

static void test_anchor_resolution_matrix(void)
{
    /* Root-relative, relative, `./`, `../`, and the `..` clamp at the root. */
    anchor_resolved_t r;

    anchor_resolve_raw("/Settings", "App/Pages/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.reference, "/Settings");
    ROBO_CHECK_STR_EQ(r.resolved, "/Settings");

    anchor_resolve_raw("Details", "App/Pages/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Pages/Details");

    anchor_resolve_raw("./Details", "App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Pages/Details");

    anchor_resolve_raw("../Details", "App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Details");

    anchor_resolve_raw("../../Details", "App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "Details");

    /* Escaping above the root clamps to the root rather than failing: a URL
     * does the same, and it is not an error worth reporting. */
    anchor_resolve_raw("../../../../Details", "App/Pages/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.resolved, "Details");

    /* A root-relative base keeps its root through `..`. */
    anchor_resolve_raw("/App/Details", "/App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "/App/Details");
    anchor_resolve_raw("../Details", "/App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "/App/Details");
    anchor_resolve_raw("../../Details", "/App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "/Details");
    anchor_resolve_raw("../../../../Details", "/App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "/Details");

    /* A trailing separator is part of the reference's meaning. */
    anchor_resolve_raw("Details/", "App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Pages/Details/");
    anchor_resolve_raw("../", "App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "App/");
    anchor_resolve_raw("/Details/", "/App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "/Details/");

    /* A query rides along verbatim and is not broken out. */
    anchor_resolve_raw("Details?Sort=Name", "App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Pages/Details?Sort=Name");
    /* A bare query keeps the current file, not its directory. */
    anchor_resolve_raw("?Sort=Name", "App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Pages/Index.html?Sort=Name");

    /* Surrounding whitespace is not part of the reference, and the reference
     * is still reported exactly as written. */
    anchor_resolve_raw("  /Settings  ", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.reference, "/Settings");
    ROBO_CHECK_STR_EQ(r.resolved, "/Settings");

    /* Whitespace inside is not trimmed, because it is not at an edge. */
    anchor_resolve_raw("Set tings", "App/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Set tings");

    /* `.` alone is the base's directory. */
    anchor_resolve_raw(".", "App/Pages/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Pages/");
    anchor_resolve_raw(".", "/App/Pages/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "/App/Pages/");
}

static void test_anchor_resolution_without_base(void)
{
    anchor_resolved_t r;

    /* No base at all: a nameless in-memory document is normal, so a relative
     * reference resolves against nothing rather than failing. */
    anchor_resolve_raw("Details", NULL, &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.resolved, "Details");

    anchor_resolve_raw("Details", "", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "Details");

    anchor_resolve_raw("../Details", NULL, &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.resolved, "Details");

    anchor_resolve_raw("/Details", NULL, &r);
    ROBO_CHECK_STR_EQ(r.resolved, "/Details");

    /* A base that is a bare file name has no directory to resolve against. */
    anchor_resolve_raw("Details", "Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "Details");
    anchor_resolve_raw("../Details", "Index.html", &r);
    ROBO_CHECK_STR_EQ(r.resolved, "Details");
}

static void test_anchor_none_and_missing_href(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    anchor_resolved_t r;

    /* Absent, empty and whitespace-only are one kind, and it is a state. */
    anchor_build_named(id, "App/Index.html", "<a>No href</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    anchor_resolve(&a, &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_NONE);
    ROBO_CHECK_EQ_INT(r.has_reference, 0);
    ROBO_CHECK_EQ_INT(r.has_resolved, 0);

    anchor_build(id, "<a href=\"\">Empty</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    anchor_resolve(&a, &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_NONE);

    anchor_build(id, "<a href=\" \t \">Blank</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    anchor_resolve(&a, &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_NONE);

    /* And a bare target create agrees, because both readers share one rule. */
    anchor_resolve_raw("", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_NONE);
    anchor_resolve_raw("   ", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_NONE);
    anchor_resolve_raw(NULL, "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_NONE);

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_invalid_targets(void)
{
    anchor_resolved_t r;

    /* `:rest` — a colon where a scheme should be and no scheme name. */
    anchor_resolve_raw(":Settings", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_INVALID);

    /* A scheme that does not start with a letter. */
    anchor_resolve_raw("1thing:Settings", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_INVALID);

    /* A scheme with a character the grammar does not allow. */
    anchor_resolve_raw("ht^tp:Settings", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_INVALID);

    /*
     * A protocol-relative reference names a network location that only the
     * base's scheme could complete. Guessing a scheme would be wrong and
     * reinterpreting the host as an internal path would be dangerous, so it
     * is refused.
     */
    anchor_resolve_raw("//example.com/Index.html", "/App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_INVALID);

    /* A control byte inside the reference. */
    anchor_resolve_raw("Details\x01", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_INVALID);
    anchor_resolve_raw("Set\x7ftings", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_INVALID);

    /* The same rule through the attribute view's `usable` flag, so the two
     * readers cannot disagree about a reference neither will resolve. */
    {
        robo_html_document_id_t id = anchor_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t a;
        robo_html_anchor_attributes_t attrs;

        anchor_build_named(id, "App/Index.html", "<a href=\"/x\">ok</a>");
        root = anchor_root(id);
        a = anchor_find(&root, ROBO_HTML_TAG_A);
        ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(attrs.href_usable, 1);
        ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    /* An over-long reference is refused at the boundary rather than sized
     * from: a hostile document must not be able to make resolution
     * expensive by claiming a large href. */
    {
        size_t too_long = (size_t)ROBO_HTML_TARGET_MAX_REFERENCE + 1u;
        char *big = (char *)robo_malloc(too_long + 1u);

        ROBO_CHECK(big != NULL);
        if (big) {
            size_t i;

            big[0] = '/';
            for (i = 1u; i < too_long; i++) {
                big[i] = 'a';
            }
            big[too_long] = '\0';
            anchor_resolve_raw(big, "App/Index.html", &r);
            ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_INVALID);
            robo_free(big);
        }
    }

    /* Kind vocabulary. */
    ROBO_CHECK_EQ_INT(robo_html_target_kind_is_known(ROBO_HTML_TARGET_NONE), 1);
    ROBO_CHECK_EQ_INT(robo_html_target_kind_is_known(ROBO_HTML_TARGET_INVALID),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_target_kind_is_known((robo_html_target_kind_t)99),
                      0);
    ROBO_CHECK_STR_EQ(robo_html_target_kind_name(ROBO_HTML_TARGET_ROUTE),
                      "route");
    ROBO_CHECK_STR_EQ(robo_html_target_kind_name(ROBO_HTML_TARGET_FRAGMENT),
                      "fragment");
    ROBO_CHECK_STR_EQ(robo_html_target_kind_name(ROBO_HTML_TARGET_EXTERNAL),
                      "external");
    ROBO_CHECK_STR_EQ(robo_html_target_kind_name(ROBO_HTML_TARGET_INVALID),
                      "invalid");
    ROBO_CHECK_STR_EQ(robo_html_target_kind_name((robo_html_target_kind_t)99),
                      "unknown");
}

static void test_anchor_fragment_targets(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t found;
    anchor_resolved_t r;

    anchor_build_named(
        id, "App/Pages/Index.html",
        "<a href=\"#About\">About</a><a href=\"#\">Top</a>"
        "<a href=\"Details#Step\">Step</a>"
        "<section id=\"About\">About the runtime</section>"
        "<section id=\"Step\">Step</section>");
    root = anchor_root(id);

    {
        robo_html_node_handle_t first = anchor_find(&root, ROBO_HTML_TAG_A);

        anchor_resolve(&first, &r);
        ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_FRAGMENT);
        ROBO_CHECK_EQ_INT(r.has_fragment, 1);
        ROBO_CHECK_STR_EQ(r.fragment, "About");
        ROBO_CHECK_EQ_INT(r.has_resolved, 1);
        ROBO_CHECK_STR_EQ(r.resolved, "App/Pages/Index.html");
        ROBO_CHECK_EQ_INT(robo_html_navigation_target_is_internal(NULL), 0);
    }

    /* The fragment names a real element, in document order. */
    {
        robo_html_node_handle_t first;

        ROBO_CHECK_EQ_INT(
            robo_html_navigation_find_fragment(id, "About", 5u, &found, NULL),
            ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&found, ROBO_HTML_TAG_SECTION));
        ROBO_CHECK_EQ_INT(
            (int)robo_html_element_attribute_value(&found, "id", NULL)[0],
            (int)'A');

        first = anchor_find(&found, ROBO_HTML_TAG_SECTION);
        ROBO_CHECK_EQ_INT(
            (int)robo_html_element_tag(&first), (int)ROBO_HTML_TAG_SECTION);
    }

    /* First match in document order wins, and the comparison is exact. */
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "about", 5u, &found, NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "About ", 6u, &found, NULL),
        ROBO_NOT_FOUND);

    /*
     * An empty fragment names the top of the document, which is not an
     * element — so it resolves to nothing rather than to whatever happens to
     * be first. Returning the root would invent a "top" element.
     */
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_find_fragment(id, "", 0u, &found, NULL),
        ROBO_NOT_FOUND);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&found));

    /* `#` with nothing after it is still a fragment, and still empty. */
    {
        robo_html_node_handle_t first = anchor_find(&root, ROBO_HTML_TAG_A);
        robo_html_node_handle_t second;

        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&first, &second, NULL),
                          ROBO_OK);
        anchor_resolve(&second, &r);
        ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_FRAGMENT);
        ROBO_CHECK_EQ_INT(r.has_fragment, 1);
        ROBO_CHECK_EQ_INT((int)strlen(r.fragment), 0);
    }

    /* A route with a fragment keeps both. */
    {
        robo_html_node_handle_t third;
        robo_html_node_handle_t second;
        robo_html_node_handle_t first = anchor_find(&root, ROBO_HTML_TAG_A);

        ROBO_CHECK_EQ_INT(
            robo_html_node_next_sibling(&first, &second, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_node_next_sibling(&second, &third, NULL), ROBO_OK);
        anchor_resolve(&third, &r);
        ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
        ROBO_CHECK_STR_EQ(r.resolved, "App/Pages/Details");
        ROBO_CHECK_EQ_INT(r.has_fragment, 1);
        ROBO_CHECK_STR_EQ(r.fragment, "Step");
    }

    /* Argument safety on the lookup. */
    ROBO_CHECK_EQ_INT(robo_html_navigation_find_fragment(id, "About", 5u, NULL,
                                                        NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_navigation_find_fragment(
                          ROBO_HTML_DOCUMENT_ID_NONE, "About", 5u, &found, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_external_uris(void)
{
    anchor_resolved_t r;

    anchor_resolve_raw("https://example.com/Path", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_EXTERNAL);
    ROBO_CHECK_STR_EQ(r.scheme, "https");
    /* An absolute URI is carried verbatim: normalizing it is a URL library's
     * job and the reference is already absolute. */
    ROBO_CHECK_STR_EQ(r.resolved, "https://example.com/Path");
    ROBO_CHECK_STR_EQ(r.reference, "https://example.com/Path");

    anchor_resolve_raw("http://example.com", "App/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.scheme, "http");
    anchor_resolve_raw("mailto:someone@example.com", "App/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.scheme, "mailto");
    ROBO_CHECK_STR_EQ(r.resolved, "mailto:someone@example.com");
    anchor_resolve_raw("tel:+10000000000", "App/Index.html", &r);
    ROBO_CHECK_STR_EQ(r.scheme, "tel");

    /* Scheme matching is case-insensitive and the *reported* scheme is
     * lowercase, because that is what the policy table is keyed on. */
    anchor_resolve_raw("HTTPS://Example.com/Path", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_EXTERNAL);
    ROBO_CHECK_STR_EQ(r.scheme, "https");
    ROBO_CHECK_STR_EQ(r.resolved, "HTTPS://Example.com/Path");

    /* A scheme the runtime has never heard of is still classified as an
     * external URI — that is what recognition means — and is not permitted. */
    anchor_resolve_raw("roboticela-notes:Open/Plan", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_EXTERNAL);
    ROBO_CHECK_STR_EQ(r.scheme, "roboticela-notes");
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_scheme_is_known("roboticela-notes", 16u), 0);
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_scheme_is_permitted("roboticela-notes", 16u), 0);

    /* A `+` in a scheme name is legal and stays part of it. */
    anchor_resolve_raw("svn+ssh:host/Path", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_EXTERNAL);
    ROBO_CHECK_STR_EQ(r.scheme, "svn+ssh");

    /* A colon after a separator is a path character, not a scheme. */
    anchor_resolve_raw("Details/Note:x", "App/Index.html", &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Details/Note:x");
}

static void test_anchor_scheme_policy(void)
{
    /*
     * The two columns, and the rows where they disagree. This is the policy
     * table's contract stated as a test rather than left to the reader: the
     * point of `known` next to `permitted` is that `file` and `data` are
     * understood *and refused*, which is a different thing from unknown.
     */
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("http", 4u), 1);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("https", 5u), 1);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("mailto", 6u), 1);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("tel", 3u), 1);

    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_known("file", 4u), 1);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("file", 4u), 0);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_known("data", 4u), 1);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("data", 4u), 0);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_known("javascript", 10u),
                      1);
    ROBO_CHECK_EQ_INT(
        robo_html_navigation_scheme_is_permitted("javascript", 10u), 0);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("vbscript", 8u),
                      0);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("blob", 4u), 0);

    /* Unknown: neither known nor permitted. */
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_known("nonsense", 8u), 0);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("nonsense", 8u),
                      0);

    /* Case-insensitive, and a prefix is not a match. */
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("HTTPS", 5u), 1);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("htt", 3u), 0);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("httpsx", 6u), 0);

    /* Empty and NULL are not schemes. */
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_known("", 0u), 0);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted(NULL, 0u), 0);
    ROBO_CHECK_EQ_INT(robo_html_navigation_scheme_is_permitted("https", 0u), 0);
}

static void test_anchor_target_context(void)
{
    ROBO_CHECK_EQ_INT(
        (int)robo_html_link_target_context_for(NULL, 0u),
        (int)ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_link_target_context_for("", 0u),
        (int)ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_link_target_context_for("_self", 5u),
        (int)ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_link_target_context_for("_SELF", 5u),
        (int)ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_link_target_context_for("_blank", 6u),
        (int)ROBO_HTML_LINK_TARGET_CONTEXT_NEW);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_link_target_context_for("_parent", 7u),
        (int)ROBO_HTML_LINK_TARGET_CONTEXT_PARENT);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_link_target_context_for("_top", 4u),
        (int)ROBO_HTML_LINK_TARGET_CONTEXT_TOP);
    /* A name the runtime does not define is not silently treated as _self. */
    ROBO_CHECK_EQ_INT(
        (int)robo_html_link_target_context_for("_weird", 6u),
        (int)ROBO_HTML_LINK_TARGET_CONTEXT_UNKNOWN);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_link_target_context_for("MainWindow", 10u),
        (int)ROBO_HTML_LINK_TARGET_CONTEXT_NAMED);

    ROBO_CHECK_EQ_INT(robo_html_link_target_context_is_known(
                          ROBO_HTML_LINK_TARGET_CONTEXT_NAMED),
                      1);
    ROBO_CHECK_EQ_INT(
        robo_html_link_target_context_is_known((robo_html_link_target_context_t)99),
        0);
    ROBO_CHECK_STR_EQ(
        robo_html_link_target_context_name(ROBO_HTML_LINK_TARGET_CONTEXT_NEW),
        "new");
    ROBO_CHECK_STR_EQ(
        robo_html_link_target_context_name((robo_html_link_target_context_t)99),
        "unknown");

    /*
     * `_blank` is classified and nothing else happens: RoboticelaOS has
     * applications and windows, not a tab strip, so the value is preserved
     * for a future window layer rather than acted on here.
     */
    {
        robo_html_document_id_t id = anchor_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t a;
        robo_html_anchor_attributes_t attrs;

        anchor_build_named(id, "App/Index.html",
                           "<a href=\"/x\" target=\"_blank\">New</a>");
        root = anchor_root(id);
        a = anchor_find(&root, ROBO_HTML_TAG_A);
        ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            (int)robo_html_link_target_context_for(attrs.target,
                                                   attrs.target_length),
            (int)ROBO_HTML_LINK_TARGET_CONTEXT_NEW);
        ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
    }
}

static void test_anchor_rel_tokens(void)
{
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("noopener noreferrer", 19u,
                                          "noopener"),
        1);
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("noopener noreferrer", 19u,
                                          "noreferrer"),
        1);
    /* The second token is not a substring of the first, and neither is empty:
     * the separator is what makes them two tokens. */
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("noopener noreferrer", 14u,
                                          "noreferrer"),
        0);
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("NOOPENER", 8u, "noopener"), 1);
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("  noopener  ", 12u, "noopener"), 1);
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("\tnoopener\n", 10u, "noopener"), 1);

    /* A substring is not a token, which is the whole reason this is a
     * predicate over tokens rather than a search. */
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("noopeners", 9u, "noopener"), 0);
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("xnoopener", 9u, "noopener"), 0);

    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("external", 8u, "nofollow"), 0);
    ROBO_CHECK_EQ_INT(robo_html_link_rel_contains_token("", 0u, "noopener"), 0);
    ROBO_CHECK_EQ_INT(robo_html_link_rel_contains_token(NULL, 0u, "noopener"),
                      0);
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("noopener", 8u, ""), 0);
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token("noopener", 8u, NULL), 0);
}

/* ------------------------------------------------------------------ */
/* Activation                                                          */
/* ------------------------------------------------------------------ */

static void test_anchor_activate_no_target(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    anchor_handler_log_t log;
    robo_html_navigation_handler_t handler;
    robo_html_navigation_outcome_t outcome;

    anchor_build_named(id, "App/Index.html", "<a>Nothing here</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);
    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind,
                      (int)ROBO_HTML_NAVIGATION_NO_TARGET);
    ROBO_CHECK_EQ_INT((int)outcome.target_kind, (int)ROBO_HTML_TARGET_NONE);
    /* Nothing to route means nothing was asked. */
    ROBO_CHECK_EQ_INT(log.calls, 0);
    ROBO_CHECK_EQ_INT(outcome.handler_called, 0);
    ROBO_CHECK_EQ_INT((int)outcome.handler_result, (int)ROBO_OK);
    robo_html_navigation_outcome_clear(&outcome);

    /* A non-anchor is a caller bug, not an outcome, and says so. */
    anchor_build(id, "<p>Text</p>");
    root = anchor_root(id);
    {
        robo_html_node_handle_t p = anchor_find(&root, ROBO_HTML_TAG_P);

        ROBO_CHECK_EQ_INT(robo_html_element_activate(&p, &handler, &outcome,
                                                     NULL),
                          ROBO_NOT_SUPPORTED);
    }
    /* A text node is not even an element. */
    {
        robo_html_node_handle_t p = anchor_find(&root, ROBO_HTML_TAG_P);
        robo_html_node_handle_t text;

        robo_html_node_handle_clear(&text);
        ROBO_CHECK_EQ_INT(robo_html_node_first_child(&p, &text, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_activate(&text, &handler, &outcome,
                                                     NULL),
                          ROBO_NOT_FOUND);
    }
    ROBO_CHECK_EQ_INT(robo_html_element_activate(NULL, &handler, &outcome,
                                                 NULL),
                      ROBO_INVALID_ARGUMENT);
    /* An outcome is safe to clear on every failure path above. */
    robo_html_navigation_outcome_clear(&outcome);
    robo_html_navigation_outcome_clear(NULL);
    memset(&outcome, 0, sizeof(outcome));
    robo_html_navigation_outcome_clear(&outcome);

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_activate_route(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    anchor_handler_log_t log;
    robo_html_navigation_handler_t handler;
    robo_html_navigation_outcome_t outcome;

    anchor_build_named(id, "App/Pages/Index.html",
                       "<a href=\"../Settings\" rel=\"external\" "
                       "title=\"Settings\">S</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    /* With a router installed, the route is handed over and accepted. */
    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);
    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_ROUTE);
    ROBO_CHECK_EQ_INT(log.calls, 1);
    ROBO_CHECK_EQ_INT(outcome.handler_called, 1);
    /* The request carried the real document, the resolved route, the base,
     * the reason, and the attributes verbatim. */
    ROBO_CHECK_EQ_INT((int)log.document_id, (int)id);
    ROBO_CHECK_STR_EQ(log.base, "App/Pages/Index.html");
    ROBO_CHECK_STR_EQ(log.resolved, "App/Settings");
    ROBO_CHECK_STR_EQ(log.rel, "external");
    ROBO_CHECK_STR_EQ(log.title, "Settings");
    ROBO_CHECK_EQ_INT((int)log.reason,
                      (int)ROBO_HTML_NAVIGATION_REASON_ACTIVATION);
    ROBO_CHECK_EQ_INT((int)log.target_context,
                      (int)ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT);
    ROBO_CHECK_EQ_INT(log.download_requested, 0);
    /* The outcome reports the same resolution, owned by the outcome. */
    ROBO_CHECK(outcome.target != NULL);
    ROBO_CHECK_EQ_INT(outcome.resolved != NULL, 1);
    ROBO_CHECK(memcmp(outcome.resolved, "App/Settings", 12u) == 0);
    ROBO_CHECK_EQ_INT(outcome.fragment_found, 0);
    robo_html_navigation_outcome_clear(&outcome);
    ROBO_CHECK(outcome.target == NULL);

    /*
     * The outcome's reported strings survive the call and are readable after
     * the outcome was cleared only by the caller — which is the ownership
     * contract, and the reason the outcome owns the target.
     */
    anchor_log_reset(&log);
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)strlen(outcome.resolved), 12);
    robo_html_navigation_outcome_clear(&outcome);

    /* A router that declines is UNSUPPORTED, and its reason is recorded. */
    anchor_log_reset(&log);
    log.decline = 1;
    handler = anchor_make_handler(&log);
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind,
                      (int)ROBO_HTML_NAVIGATION_UNSUPPORTED);
    ROBO_CHECK_EQ_INT(log.calls, 1);
    ROBO_CHECK_EQ_INT(outcome.handler_called, 1);
    ROBO_CHECK_EQ_INT((int)outcome.handler_result, (int)ROBO_NOT_SUPPORTED);
    robo_html_navigation_outcome_clear(&outcome);

    /* `out` is optional: activating for the side effect alone still calls the
     * router and still does not crash. */
    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(log.calls, 1);

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_activate_no_fake_success(void)
{
    /*
     * The single most important assertion in the phase: with no handler
     * installed there is no way for an internal route to be reported as
     * anything but UNSUPPORTED. A vocabulary without a `NAVIGATED` kind is
     * what makes that structural rather than a matter of discipline, and this
     * test is what holds the vocabulary to it.
     */
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_navigation_outcome_t outcome;
    robo_html_navigation_handler_t empty;

    anchor_build_named(id, "App/Index.html", "<a href=\"/Settings\">S</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    robo_html_navigation_handler_init(&empty);
    ROBO_CHECK_EQ_INT(robo_html_navigation_handler_is_usable(&empty), 0);
    ROBO_CHECK_EQ_INT(robo_html_navigation_handler_is_usable(NULL), 0);

    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, NULL, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind,
                      (int)ROBO_HTML_NAVIGATION_UNSUPPORTED);
    ROBO_CHECK_EQ_INT(outcome.handler_called, 0);
    ROBO_CHECK_EQ_INT((int)outcome.handler_result, (int)ROBO_OK);
    robo_html_navigation_outcome_clear(&outcome);

    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &empty, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind,
                      (int)ROBO_HTML_NAVIGATION_UNSUPPORTED);
    ROBO_CHECK_EQ_INT(outcome.handler_called, 0);
    robo_html_navigation_outcome_clear(&outcome);

    /* And no outcome kind is called "navigated", in name or meaning. */
    ROBO_CHECK_EQ_INT(robo_html_navigation_outcome_kind_is_known(
                          ROBO_HTML_NAVIGATION_ROUTE),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_navigation_outcome_kind_is_known(
                          ROBO_HTML_NAVIGATION_UNSUPPORTED),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_navigation_outcome_kind_is_known(
                          (robo_html_navigation_outcome_kind_t)99),
                      0);
    ROBO_CHECK_STR_EQ(
        robo_html_navigation_outcome_kind_name(ROBO_HTML_NAVIGATION_UNSUPPORTED),
        "unsupported");
    ROBO_CHECK_STR_EQ(robo_html_navigation_outcome_kind_name(
                          (robo_html_navigation_outcome_kind_t)99),
                      "unknown");

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_activate_fragment(void)
{
    /*
     * A fragment is the one navigation this phase can complete, so it is
     * answered locally — which is also why it works with no router at all.
     */
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_navigation_outcome_t outcome;
    anchor_handler_log_t log;
    robo_html_navigation_handler_t handler;

    anchor_build_named(id, "App/Index.html",
                       "<a href=\"#About\">About</a>"
                       "<a href=\"#Missing\">Gone</a>"
                       "<section id=\"About\">About</section>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);
    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_FRAGMENT);
    ROBO_CHECK_EQ_INT((int)outcome.target_kind,
                      (int)ROBO_HTML_TARGET_FRAGMENT);
    ROBO_CHECK_EQ_INT(outcome.fragment_found, 1);
    ROBO_CHECK_EQ_INT(
        (int)robo_html_element_tag(&outcome.fragment_node),
        (int)ROBO_HTML_TAG_SECTION);
    ROBO_CHECK_STR_EQ(outcome.fragment, "About");
    /* Delegating a fragment would be asking somebody to do a tree walk we
     * already did, so the router is deliberately not consulted. */
    ROBO_CHECK_EQ_INT(log.calls, 0);
    ROBO_CHECK_EQ_INT(outcome.handler_called, 0);
    robo_html_navigation_outcome_clear(&outcome);

    /* A fragment that names nothing is still reported, with its target. */
    {
        robo_html_node_handle_t second;

        ROBO_CHECK_EQ_INT(
            robo_html_node_next_sibling(&a, &second, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_element_activate(&second, &handler, &outcome, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT((int)outcome.kind,
                          (int)ROBO_HTML_NAVIGATION_FRAGMENT_MISSING);
        ROBO_CHECK_EQ_INT(outcome.fragment_found, 0);
        ROBO_CHECK_STR_EQ(outcome.fragment, "Missing");
        ROBO_CHECK_EQ_INT(log.calls, 0);
        robo_html_navigation_outcome_clear(&outcome);
    }

    /* With no router at all, a fragment still completes: it needs no I/O. */
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, NULL, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_FRAGMENT);
    robo_html_navigation_outcome_clear(&outcome);

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_activate_external(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_navigation_outcome_t outcome;
    anchor_handler_log_t log;
    robo_html_navigation_handler_t handler;

    anchor_build_named(id, "App/Index.html",
                       "<a href=\"https://example.com/Path\">Web</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);
    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_EXTERNAL);
    ROBO_CHECK_EQ_INT(log.calls, 1);
    ROBO_CHECK_STR_EQ(log.scheme, "https");
    ROBO_CHECK_STR_EQ(log.resolved, "https://example.com/Path");
    robo_html_navigation_outcome_clear(&outcome);

    /* Permitted but with no handler: UNSUPPORTED, because this runtime has
     * no network stack and saying otherwise would be claiming a fetch. */
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, NULL, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind,
                      (int)ROBO_HTML_NAVIGATION_UNSUPPORTED);
    ROBO_CHECK_EQ_INT((int)outcome.target_kind,
                      (int)ROBO_HTML_TARGET_EXTERNAL);
    ROBO_CHECK_EQ_INT(outcome.handler_called, 0);
    robo_html_navigation_outcome_clear(&outcome);

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_activate_refused_schemes(void)
{
    /*
     * The security test. A handler is installed and counts its own calls, so
     * "this never reached a handler" is asserted rather than assumed — which
     * is the only way to know that the refusal happens *before* the seam
     * instead of inside it.
     */
    static const char *const k_refused[] = {
        "<a href=\"javascript:App.Run()\">A</a>",
        "<a href=\"JavaScript:App.Run()\">B</a>",
        "<a href=\"file:/System/Secret\">C</a>",
        "<a href=\"data:text/html,<b>x</b>\">D</a>",
        "<a href=\"vbscript:MsgBox\">E</a>",
        "<a href=\"blob:abc\">F</a>",
        "<a href=\"about:blank\">G</a>",
        "<a href=\"unknown-scheme:Thing\">H</a>"
    };
    size_t i;

    for (i = 0u; i < sizeof(k_refused) / sizeof(k_refused[0]); i++) {
        robo_html_document_id_t id = anchor_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t a;
        robo_html_navigation_outcome_t outcome;
        anchor_handler_log_t log;
        robo_html_navigation_handler_t handler;

        anchor_build_named(id, "App/Index.html", k_refused[i]);
        root = anchor_root(id);
        a = anchor_find(&root, ROBO_HTML_TAG_A);

        anchor_log_reset(&log);
        handler = anchor_make_handler(&log);
        memset(&outcome, 0, sizeof(outcome));
        ROBO_CHECK_EQ_INT(
            robo_html_element_activate(&a, &handler, &outcome, NULL), ROBO_OK);

        ROBO_CHECK_EQ_INT((int)outcome.kind,
                          (int)ROBO_HTML_NAVIGATION_UNSUPPORTED);
        ROBO_CHECK_EQ_INT((int)outcome.target_kind,
                          (int)ROBO_HTML_TARGET_EXTERNAL);
        /* The handler was never consulted. */
        ROBO_CHECK_EQ_INT(log.calls, 0);
        ROBO_CHECK_EQ_INT(outcome.handler_called, 0);
        /* And the refusal still reports what it refused. */
        ROBO_CHECK(outcome.scheme != NULL);
        ROBO_CHECK_EQ_INT(
            robo_html_navigation_scheme_is_permitted(outcome.scheme,
                                                    outcome.scheme_length),
            0);
        robo_html_navigation_outcome_clear(&outcome);
        ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
    }
}

static void test_anchor_activate_download(void)
{
    /*
     * `download` asks to save rather than navigate. Saving needs a filesystem
     * capability this runtime does not have, so the intent is reported and
     * the handler is *not* asked — handing a router a save it cannot perform
     * would be worse than telling the truth.
     */
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_navigation_outcome_t outcome;
    anchor_handler_log_t log;
    robo_html_navigation_handler_t handler;

    anchor_build_named(id, "App/Index.html",
                       "<a href=\"/Files/Plan.Robo\" download=\"Plan.robo\">"
                       "Get</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);
    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind,
                      (int)ROBO_HTML_NAVIGATION_DOWNLOAD_PENDING);
    ROBO_CHECK_EQ_INT((int)outcome.target_kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_EQ_INT(log.calls, 0);
    ROBO_CHECK_EQ_INT(outcome.handler_called, 0);
    /* The target is still reported, so a caller can see what would be saved. */
    ROBO_CHECK_STR_EQ(outcome.resolved, "/Files/Plan.Robo");
    robo_html_navigation_outcome_clear(&outcome);

    /* An external target with `download` is the same answer. */
    anchor_build(id, "<a href=\"https://example.com/Plan\" download>Get</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind,
                      (int)ROBO_HTML_NAVIGATION_DOWNLOAD_PENDING);
    ROBO_CHECK_EQ_INT((int)outcome.target_kind, (int)ROBO_HTML_TARGET_EXTERNAL);
    ROBO_CHECK_EQ_INT(log.calls, 0);
    robo_html_navigation_outcome_clear(&outcome);

    /* But a refused scheme stays refused even with `download`: the policy
     * check comes first, so `download` cannot be used to reach `file:`. */
    anchor_build(id, "<a href=\"file:/System/Secret\" download>Get</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind,
                      (int)ROBO_HTML_NAVIGATION_UNSUPPORTED);
    ROBO_CHECK_EQ_INT(log.calls, 0);
    robo_html_navigation_outcome_clear(&outcome);

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_activate_rejected(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_navigation_outcome_t outcome;
    anchor_handler_log_t log;
    robo_html_navigation_handler_t handler;

    anchor_build_named(id, "App/Index.html",
                       "<a href=\"//example.com/Path\">Host</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);
    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_REJECTED);
    /* The reason travels with the outcome: REJECTED plus INVALID is the
     * pair a caller needs to explain what happened. */
    ROBO_CHECK_EQ_INT((int)outcome.target_kind, (int)ROBO_HTML_TARGET_INVALID);
    ROBO_CHECK_EQ_INT(log.calls, 0);
    ROBO_CHECK_EQ_INT(outcome.handler_called, 0);
    robo_html_navigation_outcome_clear(&outcome);

    /* An empty reference is not a rejection: it is simply no target. */
    anchor_build(id, "<a href=\"  \">Empty</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_NO_TARGET);
    robo_html_navigation_outcome_clear(&outcome);

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_request_contents(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_navigation_outcome_t outcome;
    anchor_handler_log_t log;
    robo_html_navigation_handler_t handler;

    /* Every field the request carries, filled at once. The anchor is
     * identified inside the handler through its own attributes, which is what
     * proves the request's handle is the real node rather than a copy. */
    anchor_build_named(
        id, "App/Pages/Index.html",
        "<a href=\"../Settings\" target=\"MainWindow\" "
        "rel=\"noopener external\" title=\"Open settings\" "
        "data-name=\"Settings\">Settings</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);
    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_ROUTE);
    ROBO_CHECK_EQ_INT(log.calls, 1);
    ROBO_CHECK_EQ_INT((int)log.document_id, (int)id);
    ROBO_CHECK_STR_EQ(log.anchor_name, "Settings");
    ROBO_CHECK_STR_EQ(log.base, "App/Pages/Index.html");
    ROBO_CHECK_STR_EQ(log.resolved, "App/Settings");
    ROBO_CHECK_STR_EQ(log.target_attribute, "MainWindow");
    ROBO_CHECK_EQ_INT((int)log.target_context,
                      (int)ROBO_HTML_LINK_TARGET_CONTEXT_NAMED);
    ROBO_CHECK_STR_EQ(log.rel, "noopener external");
    ROBO_CHECK_STR_EQ(log.title, "Open settings");
    ROBO_CHECK_EQ_INT(log.download_requested, 0);
    ROBO_CHECK_STR_EQ(log.download_name, "");
    ROBO_CHECK_EQ_INT((int)log.target_kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_EQ_INT((int)log.reason,
                      (int)ROBO_HTML_NAVIGATION_REASON_ACTIVATION);

    /* `rel` is preserved and queryable, and this phase invents no consequences
     * for it: the request still says what the document asked for. */
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token(log.rel, strlen(log.rel), "noopener"),
        1);
    ROBO_CHECK_EQ_INT(
        robo_html_link_rel_contains_token(log.rel, strlen(log.rel), "nofollow"),
        0);
    robo_html_navigation_outcome_clear(&outcome);

    /* A bare `download` reports a present-but-empty name. */
    anchor_build(id, "<a href=\"/x\" download>Save</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    anchor_log_reset(&log);
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(log.calls, 0); /* download never reaches the handler */
    robo_html_navigation_outcome_clear(&outcome);

    /* Reason vocabulary. */
    ROBO_CHECK_EQ_INT(robo_html_navigation_reason_is_known(
                          ROBO_HTML_NAVIGATION_REASON_ACTIVATION),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_navigation_reason_is_known(
                          (robo_html_navigation_reason_t)99),
                      0);
    ROBO_CHECK_STR_EQ(
        robo_html_navigation_reason_name(ROBO_HTML_NAVIGATION_REASON_ACTIVATION),
        "activation");
    ROBO_CHECK_STR_EQ(robo_html_navigation_reason_name(
                          (robo_html_navigation_reason_t)99),
                      "unknown");

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Structure, mutation, lifecycle                                      */
/* ------------------------------------------------------------------ */

static void test_anchor_structure(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t first;
    robo_html_node_handle_t second;
    robo_html_node_handle_t third;

    /* Adjacent anchors, a nested span, an anchor wrapping an image, and an
     * anchor that must not swallow what follows it. */
    anchor_build_named(
        id, "App/Index.html",
        "<p><a href=\"/A\">A</a><a href=\"/B\">B</a></p>"
        "<a href=\"/C\"><span>C</span></a>"
        "<a href=\"/D\"><img src=\"Assets/Home.png\" alt=\"Home\"></a>"
        "<p>After the links</p>");

    root = anchor_root(id);
    /*
     * Four anchors, and they are not all siblings: the first two are children
     * of the paragraph, the last two are siblings *of* it. Which is which is
     * the structure question, so it is asserted rather than assumed.
     */
    ROBO_CHECK_EQ_INT(anchor_count(id, ROBO_HTML_TAG_A), 4);
    {
        robo_html_node_handle_t p = anchor_find(&root, ROBO_HTML_TAG_P);

        robo_html_node_handle_clear(&first);
        ROBO_CHECK_EQ_INT(robo_html_node_first_child(&p, &first, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&first, ROBO_HTML_TAG_A));
        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&first, &second, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&second, ROBO_HTML_TAG_A));
        ROBO_CHECK_EQ_INT((int)robo_html_element_tag(&second),
                          (int)ROBO_HTML_TAG_A);
        /* The first anchor's own text is its, not the second's. */
        ROBO_CHECK_STR_EQ(anchor_text(&first), "A");
        ROBO_CHECK_STR_EQ(anchor_text(&second), "B");
        /* The second anchor closes the paragraph, and the next anchor belongs
         * to the body — so the third is the paragraph's next sibling, not the
         * second anchor's. */
        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&second, &third, NULL),
                          ROBO_NOT_FOUND);
        robo_html_node_handle_clear(&third);
        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&p, &third, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&third, ROBO_HTML_TAG_A));
    }

    /* An anchor holding a span: the span is a real child, not a sibling, and
     * the anchor's own subtree text is the span's text. */
    {
        robo_html_node_handle_t span;
        size_t children = 0u;

        ROBO_CHECK(robo_html_element_is_tag(&third, ROBO_HTML_TAG_A));
        ROBO_CHECK_EQ_INT(robo_html_node_child_count(&third, &children, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)children, 1);
        robo_html_node_handle_clear(&span);
        ROBO_CHECK_EQ_INT(robo_html_node_first_child(&third, &span, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&span, ROBO_HTML_TAG_SPAN));
        ROBO_CHECK_STR_EQ(anchor_text(&third), "C");
    }

    /* An anchor wrapping an image: the image is the child, and the anchor
     * resolves normally. */
    {
        robo_html_node_handle_t img_anchor;
        robo_html_node_handle_t img;
        size_t children = 0u;
        anchor_resolved_t r;

        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&third, &img_anchor,
                                                      NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&img_anchor, ROBO_HTML_TAG_A));
        ROBO_CHECK_EQ_INT(
            robo_html_node_child_count(&img_anchor, &children, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)children, 1);
        robo_html_node_handle_clear(&img);
        ROBO_CHECK_EQ_INT(robo_html_node_first_child(&img_anchor, &img, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&img, ROBO_HTML_TAG_IMG));
        anchor_resolve(&img_anchor, &r);
        ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
        ROBO_CHECK_STR_EQ(r.resolved, "/D");
    }

    /* The paragraph after the anchors is a sibling of the anchors, not inside
     * the last one — an anchor that swallowed following content would show up
     * exactly here. */
    {
        robo_html_node_handle_t p = anchor_find(&root, ROBO_HTML_TAG_P);
        robo_html_node_handle_t last = p;
        robo_html_node_handle_t after;

        /* Walk the body's children to the final paragraph. */
        while (robo_html_node_handle_is_wellformed(&last) &&
               robo_html_node_next_sibling(&last, &after, NULL) == ROBO_OK) {
            last = after;
        }
        ROBO_CHECK(robo_html_element_is_tag(&last, ROBO_HTML_TAG_P));
        ROBO_CHECK_STR_EQ(anchor_text(&last), "After the links");
    }

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_mutation(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    anchor_resolved_t r;

    anchor_build_named(id, "App/Index.html",
                       "<div><a href=\"/One\">One</a><a href=\"/Two\">Two</a>"
                       "</div>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);

    /* Mutating href is honoured by the next resolution — there is no cached
     * target to go stale, because resolution is a pure function of the
     * attributes at the moment it is asked. */
    anchor_resolve(&a, &r);
    ROBO_CHECK_STR_EQ(r.resolved, "/One");
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&a, "href", "/Changed",
                                                     NULL),
                      ROBO_OK);
    anchor_resolve(&a, &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.resolved, "/Changed");

    /* Removing href makes it inert, and restoring it brings it back. */
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&a, "href", NULL),
                      ROBO_OK);
    {
        robo_html_anchor_attributes_t attrs;

        ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(attrs.href_present, 0);
    }
    anchor_resolve(&a, &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_NONE);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&a, "href", "/Restored",
                                                     NULL),
                      ROBO_OK);
    anchor_resolve(&a, &r);
    ROBO_CHECK_STR_EQ(r.resolved, "/Restored");

    /* target / rel / download mutate like any attribute and the view follows. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&a, "target", "_blank",
                                                     NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&a, "rel",
                                                     "noopener", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&a, "download", "X.robo",
                                                     NULL),
                      ROBO_OK);
    {
        robo_html_anchor_attributes_t attrs;

        ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            (int)robo_html_link_target_context_for(attrs.target,
                                                   attrs.target_length),
            (int)ROBO_HTML_LINK_TARGET_CONTEXT_NEW);
        ROBO_CHECK_EQ_INT(
            robo_html_link_rel_contains_token(attrs.rel, attrs.rel_length,
                                              "noopener"),
            1);
        ROBO_CHECK_EQ_INT(attrs.download_present, 1);
    }
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&a, "download", NULL),
                      ROBO_OK);
    {
        robo_html_anchor_attributes_t attrs;

        ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(attrs.download_present, 0);
    }

    /* Structural mutation: an anchor can be detached, moved and replaced, and
     * stays a valid anchor with a working resolution throughout. */
    {
        robo_html_node_handle_t second;
        robo_html_node_handle_t parent;
        robo_html_node_handle_t sibling;

        ROBO_CHECK_EQ_INT(
            robo_html_node_next_sibling(&a, &second, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_parent(&second, &parent, NULL),
                          ROBO_OK);
        /* Remove the second, put it back before the first. */
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&parent, &second, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_parent(&a, &parent, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_node_insert_before(&parent, &second, &a, NULL), ROBO_OK);
        anchor_resolve(&second, &r);
        ROBO_CHECK_STR_EQ(r.resolved, "/Two");
        /* The move really happened: the two anchors are still adjacent, in the
         * other order. */
        robo_html_node_handle_clear(&sibling);
        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&second, &sibling, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&sibling, &a));

        /*
         * Replacement takes a *detached* replacement, so the order is
         * detach-then-replace: removing `a` frees the slot, and putting `a`
         * back into it takes `second` out. An attached replacement would be
         * refused, and that refusal is asserted below because it is what keeps
         * a half-applied replacement from losing a child.
         */
        ROBO_CHECK_EQ_INT(robo_html_node_replace_child(&parent, &a, &second,
                                                       NULL),
                          ROBO_ALREADY_EXISTS);
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&parent, &a, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_replace_child(&parent, &a, &second,
                                                       NULL),
                          ROBO_OK);
        /* `second` is now detached and still resolvable: detachment is a legal
         * state, not corruption. */
        anchor_resolve(&second, &r);
        ROBO_CHECK_STR_EQ(r.resolved, "/Two");
        ROBO_CHECK_EQ_INT(robo_html_node_parent(&second, &parent, NULL),
                          ROBO_NOT_FOUND);
        /* ... and `a` took the slot `second` was in. */
        ROBO_CHECK_EQ_INT(robo_html_node_parent(&a, &parent, NULL), ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&parent, ROBO_HTML_TAG_DIV));
        anchor_resolve(&a, &r);
        ROBO_CHECK_STR_EQ(r.resolved, "/Restored");
    }

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_lifecycle(void)
{
    /*
     * Resolution and activation answer the same way in every *live* lifecycle
     * state, because a link is a property of the tree and the tree does not
     * change when the document's input state does. What changes is only what a
     * destroyed document can answer: nothing, cleanly.
     *
     * The states are reached the way the 8.10 machine allows rather than by
     * forcing them: CREATED is a fresh document, LOADING is an attached source,
     * INTERACTIVE is a build the node ceiling truncated, COMPLETE is a finished
     * parse. Anchors exist in the last two, which is where a link can be
     * resolved and activated for real.
     */
    enum { MANY_ANCHORS = 2300 };
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_navigation_outcome_t outcome;
    anchor_resolved_t r;
    anchor_handler_log_t log;
    robo_html_navigation_handler_t handler;
    robo_html_source_t src;
    robo_html_tree_result_t result;
    char *html;
    size_t i;
    size_t at = 0u;

    anchor_log_reset(&log);
    handler = anchor_make_handler(&log);

    /* --- CREATED: nothing attached, no tree --------------------------------- */
    ROBO_CHECK_EQ_INT((int)anchor_lifecycle(id),
                      (int)ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    ROBO_CHECK(robo_html_document_allows_construction(id));
    /* With no source there is no base, so a relative reference resolves to
     * itself rather than being refused: a nameless in-memory document is a
     * normal document. */
    anchor_resolve_raw("../Settings", robo_html_document_source_name(id), &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.resolved, "Settings");
    /* Nothing is parsed, so no fragment can name an element yet. */
    {
        robo_html_node_handle_t found;

        ROBO_CHECK_EQ_INT(
            robo_html_navigation_find_fragment(id, "S", 1u, &found, NULL),
            ROBO_NOT_FOUND);
    }

    /* --- LOADING: input attached, base recorded, tree not built yet --------- */
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(&src,
                                "<a href=\"../Settings\">S</a>"
                                "<section id=\"S\">S</section>",
                                NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_source_set_name(&src, "App/Pages/Index.html", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)anchor_lifecycle(id),
                      (int)ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK(robo_html_document_is_loading(id));
    /* The document's own reference arrives with the source, so a href resolves
     * against a real base while the input is still being consumed. */
    ROBO_CHECK_STR_EQ(robo_html_document_source_name(id), "App/Pages/Index.html");
    anchor_resolve_raw("../Settings", robo_html_document_source_name(id), &r);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Settings");

    /* --- COMPLETE: a finished parse, with a real anchor in the tree --------- */
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, &result, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)anchor_lifecycle(id),
                      (int)ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    anchor_resolve(&a, &r);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Settings");
    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_ROUTE);
    ROBO_CHECK_EQ_INT(log.calls, 1);
    robo_html_navigation_outcome_clear(&outcome);

    /* --- INTERACTIVE: a truncated build, anchors live and activatable ------- */
    html = (char *)robo_malloc(64u + (size_t)MANY_ANCHORS * 32u);
    ROBO_CHECK(html != NULL);
    if (!html) {
        (void)robo_html_document_destroy(id, NULL);
        return;
    }
    for (i = 0u; i < (size_t)MANY_ANCHORS; i++) {
        static const char PIECE[] = "<a href=\"/A\">x</a>";

        memcpy(html + at, PIECE, sizeof(PIECE) - 1u);
        at += sizeof(PIECE) - 1u;
    }
    html[at] = '\0';
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(&src, html, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_source_set_name(&src, "App/Index.html", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, &result, NULL), ROBO_OK);
    ROBO_CHECK_MSG(result.truncated, "expected the node ceiling to truncate");
    ROBO_CHECK_EQ_INT((int)anchor_lifecycle(id),
                      (int)ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);
    ROBO_CHECK(robo_html_document_is_interactive(id));
    ROBO_CHECK(!robo_html_document_is_complete(id));
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    anchor_resolve(&a, &r);
    ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
    ROBO_CHECK_STR_EQ(r.resolved, "/A");
    anchor_log_reset(&log);
    ROBO_CHECK_EQ_INT(robo_html_element_activate(&a, &handler, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_ROUTE);
    ROBO_CHECK_EQ_INT(log.calls, 1);
    robo_html_navigation_outcome_clear(&outcome);
    robo_free(html);

    /*
     * Reset drops the tree without changing the identity, and a document that
     * has loaded something settles in COMPLETE with an empty tree — there is
     * no edge back to CREATED, so a reset document has no anchors to resolve
     * until it is fed again.
     */
    ROBO_CHECK_EQ_INT(robo_html_document_reset(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)anchor_lifecycle(id),
                      (int)ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK_EQ_INT(anchor_count(id, ROBO_HTML_TAG_A), 0);
    anchor_build_named(id, "App/Pages/Index.html",
                       "<a href=\"../Settings\">S</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    anchor_resolve(&a, &r);
    ROBO_CHECK_STR_EQ(r.resolved, "App/Settings");

    /* --- DESTROYED: everything answers nothing, and answers it cleanly ------ */
    {
        robo_html_node_handle_t stale = a;
        robo_html_node_handle_t found;
        robo_html_navigation_target_t *dead_target = (void *)0;

        anchor_log_reset(&log);
        ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
        /*
         * Resolution of a handle whose document is gone is NOT_FOUND rather
         * than an empty target: there is no document to resolve against, and
         * reporting "no href" would confuse a dead handle with a live bare
         * anchor. The caller's out-parameter is left NULL either way.
         */
        ROBO_CHECK_EQ_INT(
            robo_html_element_navigation_target(&stale, &dead_target, NULL),
            ROBO_NOT_FOUND);
        ROBO_CHECK(dead_target == NULL);
        ROBO_CHECK(robo_html_node_handle_is_stale(&stale));
        /* Activation finds no document, so it is NOT_FOUND and the router is
         * never handed a request for a tree that no longer exists. */
        memset(&outcome, 0, sizeof(outcome));
        ROBO_CHECK_EQ_INT(
            robo_html_element_activate(&stale, &handler, &outcome, NULL),
            ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(log.calls, 0);
        robo_html_navigation_outcome_clear(&outcome);
        ROBO_CHECK_EQ_INT(
            robo_html_navigation_find_fragment(id, "S", 1u, &found, NULL),
            ROBO_NOT_FOUND);
        /* A reference still parses without the document: resolution is a pure
         * function of its arguments, which is why activation is the only part
         * that has to care about document lifetime. */
        anchor_resolve_raw("/A", NULL, &r);
        ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
        ROBO_CHECK_STR_EQ(r.resolved, "/A");
    }
}

static void test_anchor_unicode_text(void)
{
    /*
     * Link text and hrefs are bytes and this phase never re-encodes either.
     * The text is checked through the DOM's own subtree-text reader, so what
     * is verified is that the anchor really holds the bytes, not that a
     * comparison function agreed with itself.
     */
    static const struct {
        const char *html;
        const char *text;
    } cases[] = {
        {"<a href=\"/A\">Settings</a>", "Settings"},
        {"<a href=\"/B\">\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7</a>", "مرحبا"},
        {"<a href=\"/C\">\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C</a>",
         "فارسی"},
        {"<a href=\"/D\">\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88</a>", "اردو"},
        {"<a href=\"/E\">\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D"
         "\xE0\xA4\xA6\xE0\xA5\x80</a>",
         "हिन्दी"},
        {"<a href=\"/F\">\xE0\xA6\xAC\xE0\xA6\xBE\xE0\xA6\x82\xE0\xA6\xB2"
         "\xE0\xA6\xBE</a>",
         "বাংলা"},
        {"<a href=\"/G\">\xE4\xB8\xAD\xE6\x96\x87</a>", "中文"},
        {"<a href=\"/H\">\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E</a>", "日本語"},
        {"<a href=\"/I\">\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4</a>", "한국어"},
        {"<a href=\"/J\">\xF0\x9F\x98\x80 \xF0\x9F\x9A\x80 \xE2\x9D\xA4\xEF"
         "\xB8\x8F</a>",
         "😀 🚀 ❤️"}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_html_document_id_t id = anchor_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t a;

        anchor_build_named(id, "App/Index.html", cases[i].html);
        root = anchor_root(id);
        a = anchor_find(&root, ROBO_HTML_TAG_A);
        ROBO_CHECK_STR_EQ(anchor_text(&a), cases[i].text);
        ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    /* A non-ASCII href survives resolution byte for byte, because nothing in
     * this phase percent-encodes or re-encodes a reference. */
    {
        robo_html_document_id_t id = anchor_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t a;
        anchor_resolved_t r;

        anchor_build_named(id, "App/Index.html",
                           "<a href=\"/Files/\xE4\xB8\xAD\xE6\x96\x87\">Open</a>");
        root = anchor_root(id);
        a = anchor_find(&root, ROBO_HTML_TAG_A);
        anchor_resolve(&a, &r);
        ROBO_CHECK_EQ_INT((int)r.kind, (int)ROBO_HTML_TARGET_ROUTE);
        ROBO_CHECK_EQ_INT((int)strlen(r.resolved), 13);
        ROBO_CHECK(memcmp(r.resolved, "/Files/\xE4\xB8\xAD\xE6\x96\x87", 13u) ==
                   0);
        ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    /* And a Unicode `title`/`rel`/`download` reads back verbatim. */
    {
        robo_html_document_id_t id = anchor_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t a;
        robo_html_anchor_attributes_t attrs;

        anchor_build_named(id, "App/Index.html",
                           "<a href=\"/A\" title=\"\xE4\xB8\xAD\xE6\x96\x87\" "
                           "download=\"\xD9\x85\xD9\x84\xD9\x81.txt\">T</a>");
        root = anchor_root(id);
        a = anchor_find(&root, ROBO_HTML_TAG_A);
        ROBO_CHECK_EQ_INT(robo_html_element_anchor_attributes(&a, &attrs, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)attrs.title_length, 6);
        ROBO_CHECK(memcmp(attrs.title, "\xE4\xB8\xAD\xE6\x96\x87", 6u) == 0);
        ROBO_CHECK_EQ_INT((int)attrs.download_length, 10);
        ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
    }
}

static void test_anchor_debug_describe(void)
{
    robo_html_document_id_t id = anchor_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t a;
    robo_html_navigation_target_t *target = NULL;
    char line[256];
    size_t written;
    size_t target_written;

    anchor_build_named(id, "App/Pages/Index.html",
                       "<a href=\"../Settings\" target=\"_blank\" "
                       "rel=\"noopener\">Settings</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    ROBO_CHECK_EQ_INT(robo_html_element_navigation_target(&a, &target, NULL),
                      ROBO_OK);

    memset(line, 0, sizeof(line));
    target_written =
        robo_html_navigation_target_describe(target, line, sizeof(line));
    ROBO_CHECK(target_written > 0u);
    ROBO_CHECK(strstr(line, "kind=route") != NULL);
    ROBO_CHECK(strstr(line, "../Settings") != NULL);
    ROBO_CHECK(strstr(line, "resolved=\"App/Settings\"") != NULL);

    memset(line, 0, sizeof(line));
    written = robo_html_element_anchor_describe(&a, target, line, sizeof(line));
    ROBO_CHECK(written > 0u);
    ROBO_CHECK(strstr(line, "a href=\"../Settings\"") != NULL);
    ROBO_CHECK(strstr(line, "resolved=\"App/Settings\"") != NULL);
    ROBO_CHECK(strstr(line, "target=new") != NULL);
    ROBO_CHECK(strstr(line, "rel=\"noopener\"") != NULL);
    ROBO_CHECK(strstr(line, "state=activatable") != NULL);

    /*
     * Truncation always terminates and reports what it actually wrote — the
     * same contract as the 8.12 image describe, so a caller that needs the
     * whole line can size a buffer from a first call and know the answer was
     * short rather than complete.
     */
    {
        char small[8];

        memset(small, 0, sizeof(small));
        ROBO_CHECK_EQ_INT(
            (int)robo_html_navigation_target_describe(target, small,
                                                      sizeof(small)),
            7);
        ROBO_CHECK_EQ_INT((int)strlen(small), 7);
        /* The line really is longer than the buffer, so this exercised
         * truncation rather than a line that happened to fit. */
        ROBO_CHECK(target_written > 7u);
    }
    ROBO_CHECK_EQ_INT((int)robo_html_navigation_target_describe(target, NULL, 0u),
                      0);
    ROBO_CHECK_EQ_INT((int)robo_html_navigation_target_describe(NULL, line,
                                                               sizeof(line)) >
                          0,
                      1);

    robo_html_navigation_target_destroy(target);
    target = NULL;

    /* Without a target the anchor line still reports the attributes, and
     * never invents a resolution the caller did not perform. */
    memset(line, 0, sizeof(line));
    ROBO_CHECK(robo_html_element_anchor_describe(&a, NULL, line, sizeof(line)) >
               0u);
    ROBO_CHECK(strstr(line, "resolved=") == NULL);
    ROBO_CHECK(strstr(line, "href=\"../Settings\"") != NULL);

    /* A refused scheme is visible as such, which is what makes the debug line
     * useful for the security cases. */
    anchor_build(id, "<a href=\"javascript:App.Run()\">Run</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    ROBO_CHECK_EQ_INT(robo_html_element_navigation_target(&a, &target, NULL),
                      ROBO_OK);
    memset(line, 0, sizeof(line));
    ROBO_CHECK(robo_html_element_anchor_describe(&a, target, line,
                                                 sizeof(line)) > 0u);
    ROBO_CHECK(strstr(line, "state=refused") != NULL);
    memset(line, 0, sizeof(line));
    ROBO_CHECK(robo_html_navigation_target_describe(target, line,
                                                    sizeof(line)) > 0u);
    ROBO_CHECK(strstr(line, "permitted=no") != NULL);
    robo_html_navigation_target_destroy(target);

    /* A live non-anchor is not described as one. */
    anchor_build(id, "<p>Text</p>");
    root = anchor_root(id);
    {
        robo_html_node_handle_t p = anchor_find(&root, ROBO_HTML_TAG_P);

        memset(line, 0, sizeof(line));
        ROBO_CHECK(robo_html_element_anchor_describe(&p, NULL, line,
                                                     sizeof(line)) > 0u);
        ROBO_CHECK(strstr(line, "not an anchor") != NULL);
    }

    /* A detached-but-live anchor still describes (it is an element), and an
     * over-long href reports `state=rejected` without a resolution. */
    anchor_build(id, "<a href=\"/A\">A</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    ROBO_CHECK_EQ_INT(robo_html_element_navigation_target(&a, &target, NULL),
                      ROBO_OK);
    memset(line, 0, sizeof(line));
    ROBO_CHECK(robo_html_element_anchor_describe(&a, target, line,
                                                 sizeof(line)) > 0u);
    ROBO_CHECK(strstr(line, "state=activatable") != NULL);
    robo_html_navigation_target_destroy(target);

    /* An anchor with no href is inert, not activatable. */
    anchor_build(id, "<a>Bare</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    memset(line, 0, sizeof(line));
    ROBO_CHECK(robo_html_element_anchor_describe(&a, NULL, line,
                                                 sizeof(line)) > 0u);
    ROBO_CHECK(strstr(line, "state=inert") != NULL);

    /* And a whitespace href is `empty`, distinct from both. */
    anchor_build(id, "<a href=\"   \">Blank</a>");
    root = anchor_root(id);
    a = anchor_find(&root, ROBO_HTML_TAG_A);
    memset(line, 0, sizeof(line));
    ROBO_CHECK(robo_html_element_anchor_describe(&a, NULL, line,
                                                 sizeof(line)) > 0u);
    ROBO_CHECK(strstr(line, "state=empty") != NULL);

    ROBO_CHECK_EQ_INT((int)robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_anchor_capability_reported(void)
{
    robo_html_dom_capabilities_t caps;

    ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.links, 1);
    /* Still narrow: no DOM node API and no events, because this phase added
     * neither. */
    ROBO_CHECK_EQ_INT(caps.dom_nodes, 0);
    ROBO_CHECK_EQ_INT(caps.events, 0);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_html_anchor(void)
{
    robo_test_register("html.anchor.parser_element", test_anchor_parser_element);
    robo_test_register("html.anchor.attributes", test_anchor_attributes);
    robo_test_register("html.anchor.non_anchor_refused",
                       test_anchor_non_anchor_refused);
    robo_test_register("html.anchor.resolution_matrix",
                       test_anchor_resolution_matrix);
    robo_test_register("html.anchor.resolution_without_base",
                       test_anchor_resolution_without_base);
    robo_test_register("html.anchor.none_and_missing_href",
                       test_anchor_none_and_missing_href);
    robo_test_register("html.anchor.invalid_targets",
                       test_anchor_invalid_targets);
    robo_test_register("html.anchor.fragment_targets",
                       test_anchor_fragment_targets);
    robo_test_register("html.anchor.external_uris", test_anchor_external_uris);
    robo_test_register("html.anchor.scheme_policy", test_anchor_scheme_policy);
    robo_test_register("html.anchor.target_context", test_anchor_target_context);
    robo_test_register("html.anchor.rel_tokens", test_anchor_rel_tokens);
    robo_test_register("html.anchor.activate_no_target",
                       test_anchor_activate_no_target);
    robo_test_register("html.anchor.activate_route",
                       test_anchor_activate_route);
    robo_test_register("html.anchor.activate_no_fake_success",
                       test_anchor_activate_no_fake_success);
    robo_test_register("html.anchor.activate_fragment",
                       test_anchor_activate_fragment);
    robo_test_register("html.anchor.activate_external",
                       test_anchor_activate_external);
    robo_test_register("html.anchor.activate_refused_schemes",
                       test_anchor_activate_refused_schemes);
    robo_test_register("html.anchor.activate_download",
                       test_anchor_activate_download);
    robo_test_register("html.anchor.activate_rejected",
                       test_anchor_activate_rejected);
    robo_test_register("html.anchor.request_contents",
                       test_anchor_request_contents);
    robo_test_register("html.anchor.structure", test_anchor_structure);
    robo_test_register("html.anchor.mutation", test_anchor_mutation);
    robo_test_register("html.anchor.lifecycle", test_anchor_lifecycle);
    robo_test_register("html.anchor.unicode_text", test_anchor_unicode_text);
    robo_test_register("html.anchor.debug_describe",
                       test_anchor_debug_describe);
    robo_test_register("html.anchor.capability_reported",
                       test_anchor_capability_reported);
}
