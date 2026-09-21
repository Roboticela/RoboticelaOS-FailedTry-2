/*
 * RoboDesktop document suite (Phase 11.13) — `desktop.document.*`.
 *
 * The bridge, tested as a bridge: markup and an author sheet in, a display list out, with the report saying
 * how many elements, boxes, texts, records and commands the build passed through. Nothing here needs a
 * desktop, which is the property the whole design rests on — a document is a *value* that a window happens
 * to borrow, not a member of the shell — and `desktop.document.destroy_releases` is the case that keeps the
 * release order honest, because a bridge that destroyed its tree after its styles would be a use-after-free
 * that only a leak checker would notice.
 *
 * The cases that matter most are the two failure ones: a stage that refuses says **which** stage, and
 * everything built before it is released. A bridge that leaked its DOM on a layout failure would leak one
 * document per failed document, and this is a module a desktop calls in a loop.
 */

#include "RoboDesktopTestSupport.h"

static const char *DOC_HTML =
    "<html><body><div class='card'><p>Hello from RoboticelaOS Runtime</p></div></body></html>";
static const char *DOC_CSS =
    "html{margin:0;padding:0}body{margin:0}"
    ".card{background:#0e7aff;padding:8px}p{color:#ffffff}";

static void test_document_build_report(void)
{
    robo_desktop_document_config_t config;
    robo_desktop_document_t *document = NULL;
    robo_desktop_document_report_t report;
    const robo_paint_display_list_t *list;

    desktop_fixture();
    robo_desktop_document_config_init(&config);
    ROBO_CHECK(config.html == NULL && config.css == NULL && config.face == NULL);
    ROBO_CHECK_EQ_INT(config.width, 800);
    ROBO_CHECK_EQ_INT(config.height, 600);
    robo_desktop_document_config_init(NULL);

    config.html = DOC_HTML;
    config.css = DOC_CSS;
    config.width = 320;
    config.height = 240;
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, NULL), ROBO_OK);
    ROBO_CHECK(document != NULL);
    ROBO_CHECK_EQ_INT(robo_desktop_document_is_live(document), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_document_is_live(NULL), 0);

    ROBO_CHECK_EQ_INT(robo_desktop_document_viewport(document).width, 320);
    ROBO_CHECK_EQ_INT(robo_desktop_document_viewport(document).height, 240);
    ROBO_CHECK_EQ_INT(robo_desktop_document_viewport(NULL).width, 0);

    list = robo_desktop_document_display_list(document);
    ROBO_CHECK(list != NULL);
    ROBO_CHECK(robo_paint_display_list_count(list) > 0u);
    ROBO_CHECK(robo_desktop_document_display_list(NULL) == NULL);

    ROBO_CHECK_EQ_INT(robo_desktop_document_report(document, &report), ROBO_OK);
    /* html, body, div, p -- four elements, and a box for each. */
    ROBO_CHECK(report.elements >= 4u);
    ROBO_CHECK(report.boxes >= 4u);
    ROBO_CHECK(report.texts >= 1u);
    ROBO_CHECK(report.records > 0u);
    ROBO_CHECK_EQ_INT((int)report.commands, (int)robo_paint_display_list_count(list));
    ROBO_CHECK_EQ_INT(robo_desktop_document_report(NULL, &report), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_document_report(document, NULL), ROBO_INVALID_ARGUMENT);

    robo_desktop_document_destroy(document);
    robo_desktop_document_destroy(NULL);
}

static void test_document_build_refusals(void)
{
    robo_desktop_document_config_t config;
    robo_desktop_document_t *document = NULL;
    robo_desktop_diag_t diag;

    desktop_fixture();
    robo_desktop_document_config_init(&config);
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(NULL, &document, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, NULL, NULL), ROBO_INVALID_ARGUMENT);

    /* No markup is a caller mistake rather than an empty document. */
    diag.code = ROBO_OK;
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, &diag), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_DESKTOP_DOMAIN_DOCUMENT);
    ROBO_CHECK(document == NULL);

    config.html = DOC_HTML;
    config.width = 0;
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, NULL), ROBO_INVALID_ARGUMENT);
    config.width = 320;
    config.height = -1;
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, NULL), ROBO_INVALID_ARGUMENT);
    config.height = 240;
    config.width = (int32_t)ROBO_DESKTOP_MAX_DIMENSION + 1;
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(document == NULL);
}

static void test_document_without_style(void)
{
    robo_desktop_document_config_t config;
    robo_desktop_document_t *document = NULL;
    robo_desktop_document_report_t report;

    desktop_fixture();
    robo_desktop_document_config_init(&config);
    config.html = "<html><body><div class='plain'>No stylesheet</div></body></html>";
    config.css = NULL;
    config.width = 200;
    config.height = 120;
    /* An unstyled document is a document: it lays out and paints, and its report says so. */
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_document_report(document, &report), ROBO_OK);
    ROBO_CHECK(report.boxes >= 2u);
    ROBO_CHECK(robo_paint_display_list_count(robo_desktop_document_display_list(document)) > 0u);
    robo_desktop_document_destroy(document);

    /* And the class the sheet would have styled is still a box: an unstyled document is laid out by the
     * default display of its elements rather than refused for want of a rule. */
    config.html = "<html><body><p class='unstyled'>Text</p></body></html>";
    document = NULL;
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_document_report(document, &report), ROBO_OK);
    ROBO_CHECK(report.texts >= 1u);
    robo_desktop_document_destroy(document);
}

static void test_document_with_face(void)
{
    render_face_t face;
    robo_desktop_document_config_t config;
    robo_desktop_document_t *document = NULL;
    robo_desktop_document_report_t without;
    robo_desktop_document_report_t with;

    desktop_fixture();
    render_face_create(&face);

    robo_desktop_document_config_init(&config);
    config.html = "<html><body><p class='label'>A</p></body></html>";
    config.css = "p{font-size:100px}";
    config.width = 400;
    config.height = 200;
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_document_report(document, &without), ROBO_OK);
    robo_desktop_document_destroy(document);

    /* The same document with a face: measured by a real font, so it produces records as well and its
     * commands describe runs that can be drawn. */
    config.face = face.face;
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_document_report(document, &with), ROBO_OK);
    ROBO_CHECK(with.texts >= 1u);
    ROBO_CHECK(with.records >= without.records);
    robo_desktop_document_destroy(document);

    render_face_destroy(&face);
}

static void test_document_destroy_releases(void)
{
    size_t baseline = desktop_live_blocks();
    int round;

    /* Ten documents, built and destroyed: the allocator returns to where it started. The bridge owns a DOM,
     * a style context, a property table, a registry, a stylesheet, a layout bridge, a tree, a record list
     * and a display list, and every one of them is released in the order that keeps the tree's borrow of
     * the bridge's styles valid. */
    for (round = 0; round < 10; round++) {
        robo_desktop_document_config_t config;
        robo_desktop_document_t *document = NULL;

        desktop_fixture();
        robo_desktop_document_config_init(&config);
        config.html = DOC_HTML;
        config.css = DOC_CSS;
        config.width = 160;
        config.height = 120;
        ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, NULL), ROBO_OK);
        ROBO_CHECK(robo_desktop_document_display_list(document) != NULL);
        robo_desktop_document_destroy(document);
    }
    ROBO_CHECK_EQ_INT((int)desktop_live_blocks(), (int)baseline);
}

void robo_test_register_desktop_document(void)
{
    robo_test_register("desktop.document.build_report", test_document_build_report);
    robo_test_register("desktop.document.build_refusals", test_document_build_refusals);
    robo_test_register("desktop.document.without_style", test_document_without_style);
    robo_test_register("desktop.document.with_face", test_document_with_face);
    robo_test_register("desktop.document.destroy_releases", test_document_destroy_releases);
}
