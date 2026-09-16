/*
 * RoboHTML Phase 8.1 parser / source / node-identity suite:
 * parser lease, source kinds and lifetime, stale handles.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLParser.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboTest.h"

#include <string.h>

static void html_parser_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void html_parser_fixture_end(void)
{
    robo_html_shutdown();
}

static void test_html_source_kinds(void)
{
    robo_html_source_t src;
    uint8_t buf[8];
    static const uint8_t bytes[] = "hello";

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(src.kind, ROBO_HTML_SOURCE_KIND_NONE);
    ROBO_CHECK(robo_html_source_is_empty(&src));

    /* NULL source is refused for every configuration call. */
    ROBO_CHECK_EQ_INT(robo_html_source_empty(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_source_memory(NULL, bytes, 5u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_source_string(NULL, "x", NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Empty source. */
    ROBO_CHECK_EQ_INT(robo_html_source_empty(&src, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(src.kind, ROBO_HTML_SOURCE_KIND_EMPTY);
    ROBO_CHECK_EQ_INT(robo_html_source_length(&src), 0u);
    ROBO_CHECK(robo_html_source_is_empty(&src));

    /* Memory source: borrow view with explicit length. */
    ROBO_CHECK_EQ_INT(robo_html_source_memory(&src, bytes, 5u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(src.kind, ROBO_HTML_SOURCE_KIND_MEMORY);
    ROBO_CHECK_EQ_INT(robo_html_source_length(&src), 5u);
    ROBO_CHECK(!robo_html_source_is_empty(&src));
    ROBO_CHECK_EQ_INT(robo_html_source_copy_bytes(&src, buf, sizeof(buf)),
                      ROBO_OK);
    ROBO_CHECK(memcmp(buf, "hello", 5) == 0);
    /* Copy that cannot hold the bytes is refused, never truncated. */
    ROBO_CHECK_EQ_INT(robo_html_source_copy_bytes(&src, buf, 3u),
                      ROBO_NOT_SUPPORTED);
    /* NULL bytes with non-zero length is a boundary failure. */
    ROBO_CHECK_EQ_INT(robo_html_source_memory(&src, NULL, 4u, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* NULL bytes with zero length is accepted as empty-equivalent. */
    ROBO_CHECK_EQ_INT(robo_html_source_memory(&src, NULL, 0u, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_source_is_empty(&src));

    /* String source: length derived from the C string. */
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "ab", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(src.kind, ROBO_HTML_SOURCE_KIND_STRING);
    ROBO_CHECK_EQ_INT(robo_html_source_length(&src), 2u);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, NULL, NULL), ROBO_OK);
    ROBO_CHECK(robo_html_source_is_empty(&src));

    /* Encoding label metadata. */
    ROBO_CHECK_EQ_INT(robo_html_source_set_declared_encoding(&src, "UTF-8",
                                                             NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_html_source_declared_encoding(&src), "UTF-8");
    ROBO_CHECK_EQ_INT(robo_html_source_set_declared_encoding(&src, "",
                                                             NULL),
                      ROBO_PARSE_ERROR);
    {
        const char bad[] = { (char)0xFF, '\0' };
        ROBO_CHECK_EQ_INT(robo_html_source_set_declared_encoding(&src, bad,
                                                                 NULL),
                          ROBO_PARSE_ERROR);
    }
    ROBO_CHECK_EQ_INT(robo_html_source_set_declared_encoding(&src, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_html_source_declared_encoding(&src), "");

    /* Name metadata: bounded copy. */
    ROBO_CHECK_EQ_INT(robo_html_source_set_name(&src, "robo://app/main", NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_html_source_name(&src), "robo://app/main");
    {
        char huge[ROBO_HTML_URL_MAX + 8u];
        memset(huge, 'a', sizeof(huge));
        huge[sizeof(huge) - 1u] = '\0';
        ROBO_CHECK_EQ_INT(robo_html_source_set_name(&src, huge, NULL),
                          ROBO_NOT_SUPPORTED);
    }
}

static void test_html_source_lifetime(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_source_t src;
    uint8_t stack_bytes[16];

    html_parser_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);

    /*
     * Lifetime contract: the source borrows stack bytes; RoboHTML copies
     * only metadata (kind/length/name), never the bytes. This test
     * documents the boundary: after attach, RoboHTML makes no further
     * reads of the borrow (no parse in 8.1), so overwriting the borrow
     * afterwards cannot affect the document.
     */
    memcpy(stack_bytes, "OLD", 4);
    ROBO_CHECK_EQ_INT(robo_html_source_memory(&src, stack_bytes, 3u, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_source_set_name(&src, "borrow", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                      ROBO_OK);
    memcpy(stack_bytes, "NEW", 4); /* reuse the borrow: allowed after attach */
    ROBO_CHECK_STR_EQ(robo_html_document_source_name(id), "borrow");
    ROBO_CHECK_EQ_INT(robo_html_document_detach_source(id, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    html_parser_fixture_end();
}

static void test_html_parser_lease(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_document_id_t other = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_parser_t *p = NULL;
    robo_html_parser_t *p2 = NULL;
    robo_html_parser_state_t st;
    robo_html_parser_mode_t mode;
    robo_html_document_lifecycle_t life;
    robo_html_source_t src;

    html_parser_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_create(&other, NULL), ROBO_OK);

    /* Argument / state validation. */
    ROBO_CHECK_EQ_INT(robo_html_parser_create(id, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_parser_create(ROBO_HTML_DOCUMENT_ID_NONE, &p,
                                              NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(p == NULL, 1);

    ROBO_CHECK_EQ_INT(robo_html_parser_create(id, &p, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_parser_document(p), id);
    ROBO_CHECK_EQ_INT(robo_html_parser_get_state(p, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st, ROBO_HTML_PARSER_STATE_IDLE);
    ROBO_CHECK_EQ_INT(robo_html_parser_get_mode(p, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT(mode, ROBO_HTML_PARSER_MODE_NONE);

    /* Exclusive lease: a second parser on the same document is refused. */
    ROBO_CHECK_EQ_INT(robo_html_parser_create(id, &p2, NULL), ROBO_BUSY);
    /* A parser on another document is fine. */
    ROBO_CHECK_EQ_INT(robo_html_parser_create(other, &p2, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_parser_document(p2), other);

    /* Attach drives the document into LOADING and selects the mode. */
    ROBO_CHECK_EQ_INT(robo_html_source_memory(&src, (const uint8_t *)"<p>",
                                              3u, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_parser_attach_source(p, &src, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_parser_get_state(p, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st, ROBO_HTML_PARSER_STATE_FEEDING);
    ROBO_CHECK_EQ_INT(robo_html_parser_get_mode(p, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT(mode, ROBO_HTML_PARSER_MODE_COMPLETE);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);

    /* Re-attach while feeding is refused. */
    ROBO_CHECK_EQ_INT(robo_html_parser_attach_source(p, &src, NULL), ROBO_BUSY);
    /* Direct document attachment while a parser holds the lease is also
     * refused: the document is LOADING (source attached → BUSY). */
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(id, &src, NULL),
                      ROBO_BUSY);

    /*
     * Detach returns the document to INTERACTIVE and clears the lease.
     * Detaching abandons the input rather than finishing it, so the
     * document is interactive and not complete.
     */
    ROBO_CHECK_EQ_INT(robo_html_parser_detach(p, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_parser_get_state(p, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st, ROBO_HTML_PARSER_STATE_DETACHED);
    ROBO_CHECK_EQ_INT(robo_html_parser_document(p),
                      ROBO_HTML_DOCUMENT_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE);
    /* Detach is idempotent. */
    ROBO_CHECK_EQ_INT(robo_html_parser_detach(p, NULL), ROBO_OK);

    /* After detach the lease is free again. */
    {
        robo_html_parser_t *p3 = NULL;
        ROBO_CHECK_EQ_INT(robo_html_parser_create(id, &p3, NULL), ROBO_OK);
        robo_html_parser_destroy(p3);
    }

    /* Destroy-while-leased parser (destroy detaches first). p2 never
     * attached a source, so its document stays CREATED. */
    robo_html_parser_destroy(p2);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(other, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED);
    robo_html_parser_destroy(p);
    robo_html_parser_destroy(NULL); /* safe no-op */

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(other, NULL), ROBO_OK);
    html_parser_fixture_end();
}

static void test_html_parser_document_destroy_lease(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_parser_t *p = NULL;
    robo_html_parser_state_t st;

    html_parser_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_parser_create(id, &p, NULL), ROBO_OK);

    /*
     * Ownership rule: parsers never outlive their document. Document
     * destroy fails the lease; the parser object is still owned by the
     * caller (which must destroy it), but is detached and inert.
     */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_parser_get_state(p, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st, ROBO_HTML_PARSER_STATE_DETACHED);
    ROBO_CHECK_EQ_INT(robo_html_parser_document(p),
                      ROBO_HTML_DOCUMENT_ID_NONE);
    robo_html_parser_destroy(p);
    html_parser_fixture_end();
}

static void test_html_parser_fails_document_destroy(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_parser_t *p = NULL;

    html_parser_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_parser_create(id, &p, NULL), ROBO_OK);
    /* Destroy succeeds and silently releases the lease (see internal
     * contract); parser object remains caller-owned but detached. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_parser_state_is_known(
                          ROBO_HTML_PARSER_STATE_DETACHED),
                      1);
    robo_html_parser_destroy(p);
    html_parser_fixture_end();
}

void robo_test_register_html_parser(void)
{
    robo_test_register("html.source.kinds", test_html_source_kinds);
    robo_test_register("html.source.lifetime", test_html_source_lifetime);
    robo_test_register("html.parser.lease", test_html_parser_lease);
    robo_test_register("html.parser.destroy_lease",
                       test_html_parser_document_destroy_lease);
    robo_test_register("html.parser.detach_on_destroy",
                       test_html_parser_fails_document_destroy);
}
