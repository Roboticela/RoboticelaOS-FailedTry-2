/*
 * RoboHTML Phase 8.1 foundation suite:
 * module lifecycle, diagnostics, lifecycle state machine.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLTypes.h"
#include "RoboHTML/RoboHTMLVersion.h"
#include "RoboTest.h"

static void html_fixture(void)
{
    /* Foundation must be up for every RoboHTML call. */
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
}

static void test_html_version(void)
{
    robo_i32 major = 0;
    robo_i32 minor = 0;
    robo_i32 patch = 0;

    ROBO_CHECK_STR_EQ(robo_html_version_string(), ROBO_HTML_VERSION_STRING);
    ROBO_CHECK_EQ_INT(ROBO_HTML_VERSION_MAJOR, 0);
    ROBO_CHECK_EQ_INT(ROBO_HTML_VERSION_MINOR, 11);
    ROBO_CHECK_EQ_INT(ROBO_HTML_VERSION_PATCH, 0);
    /* Basic-elements milestone: API not frozen until later Phase 8 goals. */
    ROBO_CHECK_EQ_INT(ROBO_HTML_API_FROZEN, 0);

    ROBO_CHECK_EQ_INT(robo_html_get_version(NULL, &minor, &patch),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_get_version(&major, NULL, &patch),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_get_version(&major, &minor, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_get_version(&major, &minor, &patch), ROBO_OK);
    ROBO_CHECK_EQ_INT(major, 0);
    ROBO_CHECK_EQ_INT(minor, 11);
    ROBO_CHECK_EQ_INT(patch, 0);
}

static void test_html_module_init_shutdown(void)
{
    html_fixture();
    ROBO_CHECK_EQ_INT(robo_html_is_initialized(), 0);

    /* Document APIs refuse to run while the module is down. */
    {
        robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;
        ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_INIT_ERROR);
        ROBO_CHECK_EQ_INT(id, ROBO_HTML_DOCUMENT_ID_NONE);
    }

    ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    ROBO_CHECK(robo_html_is_initialized());
    ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_ALREADY_EXISTS);
    ROBO_CHECK_EQ_INT(robo_html_live_document_count(), 0u);

    /* Idempotent shutdown. */
    robo_html_shutdown();
    ROBO_CHECK_EQ_INT(robo_html_is_initialized(), 0);
    robo_html_shutdown();
    ROBO_CHECK_EQ_INT(robo_html_is_initialized(), 0);

    /* Re-init works after clean shutdown. */
    ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_live_document_count(), 0u);
    robo_html_shutdown();
}

static void test_html_diagnostics(void)
{
    robo_html_diag_t diag;
    robo_html_diag_t missing;

    robo_html_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_OK);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_NONE);

    /* NULL diag record: returns the strict code. */
    ROBO_CHECK_EQ_INT(robo_html_diag_set(NULL, ROBO_ERROR,
                                         ROBO_HTML_DOMAIN_DOCUMENT, "m",
                                         "f", 1),
                      ROBO_INVALID_ARGUMENT);

    robo_html_diag_reset(&missing);
    ROBO_CHECK_EQ_INT(ROBO_HTML_DIAG(&missing, ROBO_HTML_DOMAIN_PARSER,
                                     ROBO_BUSY, "busy"),
                      ROBO_BUSY);
    ROBO_CHECK_EQ_INT(missing.code, ROBO_BUSY);
    ROBO_CHECK_EQ_INT(missing.domain, ROBO_HTML_DOMAIN_PARSER);
    ROBO_CHECK(missing.message != NULL);
    ROBO_CHECK(missing.file != NULL);
    ROBO_CHECK(missing.line > 0);

    /* NULL diag through the non-strict macro: code is authoritative. */
    ROBO_CHECK_EQ_INT(ROBO_HTML_DIAG(NULL, ROBO_HTML_DOMAIN_SOURCE,
                                     ROBO_PARSE_ERROR, "ignored"),
                      ROBO_PARSE_ERROR);

    /* OK code records cleanly. */
    ROBO_CHECK_EQ_INT(ROBO_HTML_DIAG(&missing, ROBO_HTML_DOMAIN_NONE,
                                     ROBO_OK, "ok"),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(missing.code, ROBO_OK);
}

static void test_html_lifecycle_machine(void)
{
    /* Enum coverage. */
    ROBO_CHECK(robo_html_document_lifecycle_is_known(
        ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED));
    ROBO_CHECK(robo_html_document_lifecycle_is_known(
        ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED));
    ROBO_CHECK(robo_html_document_lifecycle_is_known(
        ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE));
    ROBO_CHECK(robo_html_document_lifecycle_is_known(
        ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE));
    ROBO_CHECK(!robo_html_document_lifecycle_is_known(
        (robo_html_document_lifecycle_t)99));

    /* Allowed edges. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED,
            ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED,
            ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING,
            ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE),
        ROBO_OK);
    /* The one-way step that makes COMPLETE mean "nothing more can arrive". */
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE,
            ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE),
        ROBO_OK);
    /* A settled document can take a replacement source. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE,
            ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE,
            ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED,
            ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED,
            ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING,
            ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED),
        ROBO_OK);
    /* Idempotent destroyed stay. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED,
            ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED),
        ROBO_OK);

    /* Forbidden edges. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED,
            ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE,
            ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED),
        ROBO_INVALID_ARGUMENT);
    /*
     * The two rules that give the phase its meaning: a document cannot be
     * finished loading before it has finished parsing, and a settled
     * document cannot go back to being merely interactive.
     */
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED,
            ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING,
            ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE,
            ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE),
        ROBO_INVALID_ARGUMENT);
    /* Stay-edges are refused everywhere but DESTROYED. */
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE,
            ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE,
            ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED,
            ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE),
        ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED,
            ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED),
        ROBO_INVALID_ARGUMENT); /* must pass through DESTROYING */
    ROBO_CHECK_EQ_INT(
        robo_html_document_lifecycle_check_transition(
            (robo_html_document_lifecycle_t)77,
            ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED),
        ROBO_INVALID_ARGUMENT);

    /* Names and predicates. */
    ROBO_CHECK_STR_EQ(robo_html_document_lifecycle_name(
                          ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING),
                      "loading");
    ROBO_CHECK_STR_EQ(robo_html_document_lifecycle_name(
                          ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE),
                      "interactive");
    ROBO_CHECK_STR_EQ(robo_html_document_lifecycle_name(
                          ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE),
                      "complete");
    ROBO_CHECK_STR_EQ(robo_html_document_lifecycle_name(
                          (robo_html_document_lifecycle_t)55),
                      "unknown");
    ROBO_CHECK(robo_html_document_lifecycle_allows_content(
        ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED));
    ROBO_CHECK(robo_html_document_lifecycle_allows_content(
        ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING));
    ROBO_CHECK(robo_html_document_lifecycle_allows_content(
        ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE));
    /* Completion describes input, not permission: the DOM stays open. */
    ROBO_CHECK(robo_html_document_lifecycle_allows_content(
        ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE));
    ROBO_CHECK(!robo_html_document_lifecycle_allows_content(
        ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED));
    ROBO_CHECK(!robo_html_document_lifecycle_allows_content(
        ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING));
    ROBO_CHECK(robo_html_document_lifecycle_allows_source_attach(
        ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED));
    ROBO_CHECK(robo_html_document_lifecycle_allows_source_attach(
        ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE));
    ROBO_CHECK(robo_html_document_lifecycle_allows_source_attach(
        ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE));
    ROBO_CHECK(!robo_html_document_lifecycle_allows_source_attach(
        ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING));
    ROBO_CHECK(!robo_html_document_lifecycle_allows_source_attach(
        ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED));

    /* Settled / terminal / destroyed classifiers. */
    ROBO_CHECK(robo_html_document_lifecycle_is_settled(
        ROBO_HTML_DOCUMENT_LIFECYCLE_INTERACTIVE));
    ROBO_CHECK(robo_html_document_lifecycle_is_settled(
        ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE));
    ROBO_CHECK(!robo_html_document_lifecycle_is_settled(
        ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING));
    ROBO_CHECK(robo_html_document_lifecycle_is_terminal(
        ROBO_HTML_DOCUMENT_LIFECYCLE_FAILED));
    ROBO_CHECK(robo_html_document_lifecycle_is_terminal(
        ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED));
    ROBO_CHECK(!robo_html_document_lifecycle_is_terminal(
        ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE));
    ROBO_CHECK(robo_html_document_lifecycle_is_destroyed(
        ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYED));
    ROBO_CHECK(!robo_html_document_lifecycle_is_destroyed(
        ROBO_HTML_DOCUMENT_LIFECYCLE_DESTROYING));
}

static void test_html_type_helpers(void)
{
    ROBO_CHECK(robo_html_node_kind_is_known(ROBO_HTML_NODE_KIND_DOCUMENT));
    ROBO_CHECK(robo_html_node_kind_is_known(ROBO_HTML_NODE_KIND_FRAGMENT));
    ROBO_CHECK(!robo_html_node_kind_is_known((robo_html_node_kind_t)42));
    ROBO_CHECK_STR_EQ(robo_html_node_kind_name(ROBO_HTML_NODE_KIND_TEXT),
                      "text");
    ROBO_CHECK_STR_EQ(robo_html_node_kind_name((robo_html_node_kind_t)42),
                      "unknown");

    ROBO_CHECK(robo_html_diagnostic_domain_is_known(ROBO_HTML_DOMAIN_PARSER));
    ROBO_CHECK(!robo_html_diagnostic_domain_is_known(
        (robo_html_diagnostic_domain_t)77));
    ROBO_CHECK_STR_EQ(
        robo_html_diagnostic_domain_name(ROBO_HTML_DOMAIN_ENCODING),
        "encoding");
    ROBO_CHECK_STR_EQ(
        robo_html_diagnostic_domain_name((robo_html_diagnostic_domain_t)77),
        "unknown");
}

void robo_test_register_html(void)
{
    robo_test_register("html.version", test_html_version);
    robo_test_register("html.module.init_shutdown",
                       test_html_module_init_shutdown);
    robo_test_register("html.module.diagnostics", test_html_diagnostics);
    robo_test_register("html.lifecycle.machine", test_html_lifecycle_machine);
    robo_test_register("html.types.helpers", test_html_type_helpers);
}
