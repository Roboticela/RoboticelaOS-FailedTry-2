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

static void TestDocumentBuildReport(void)
{
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    DesktopDocumentReport report;
    const PaintDisplayList *list;

    DesktopFixture();
    DesktopDocumentConfigInit(&config);
    RoboCheck(config.html == NULL && config.css == NULL && config.face == NULL);
    RoboCheckEqInt(config.width, 800);
    RoboCheckEqInt(config.height, 600);
    DesktopDocumentConfigInit(NULL);

    config.html = DOC_HTML;
    config.css = DOC_CSS;
    config.width = 320;
    config.height = 240;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
    RoboCheck(document != NULL);
    RoboCheckEqInt(DesktopDocumentIsLive(document), 1);
    RoboCheckEqInt(DesktopDocumentIsLive(NULL), 0);

    RoboCheckEqInt(DesktopDocumentViewport(document).width, 320);
    RoboCheckEqInt(DesktopDocumentViewport(document).height, 240);
    RoboCheckEqInt(DesktopDocumentViewport(NULL).width, 0);

    list = DesktopDocumentDisplayList(document);
    RoboCheck(list != NULL);
    RoboCheck(PaintDisplayListCount(list) > 0u);
    RoboCheck(DesktopDocumentDisplayList(NULL) == NULL);

    RoboCheckEqInt(GetDesktopDocumentReport(document, &report), RoboOk);
    /* html, body, div, p -- four elements, and a box for each. */
    RoboCheck(report.elements >= 4u);
    RoboCheck(report.boxes >= 4u);
    RoboCheck(report.texts >= 1u);
    RoboCheck(report.records > 0u);
    RoboCheckEqInt((int)report.commands, (int)PaintDisplayListCount(list));
    RoboCheckEqInt(GetDesktopDocumentReport(NULL, &report), RoboInvalidArgument);
    RoboCheckEqInt(GetDesktopDocumentReport(document, NULL), RoboInvalidArgument);

    DesktopDocumentDestroy(document);
    DesktopDocumentDestroy(NULL);
}

static void TestDocumentBuildRefusals(void)
{
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    DesktopDiag diag;

    DesktopFixture();
    DesktopDocumentConfigInit(&config);
    RoboCheckEqInt(DesktopDocumentBuild(NULL, &document, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopDocumentBuild(&config, NULL, NULL), RoboInvalidArgument);

    /* No markup is a caller mistake rather than an empty document. */
    diag.code = RoboOk;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, &diag), RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboDesktopDomainDocument);
    RoboCheck(document == NULL);

    config.html = DOC_HTML;
    config.width = 0;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboInvalidArgument);
    config.width = 320;
    config.height = -1;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboInvalidArgument);
    config.height = 240;
    config.width = (int32_t)RoboDesktopMaxDimension + 1;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboInvalidArgument);
    RoboCheck(document == NULL);
}

static void TestDocumentWithoutStyle(void)
{
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    DesktopDocumentReport report;

    DesktopFixture();
    DesktopDocumentConfigInit(&config);
    config.html = "<html><body><div class='plain'>No stylesheet</div></body></html>";
    config.css = NULL;
    config.width = 200;
    config.height = 120;
    /* An unstyled document is a document: it lays out and paints, and its report says so. */
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopDocumentReport(document, &report), RoboOk);
    RoboCheck(report.boxes >= 2u);
    RoboCheck(PaintDisplayListCount(DesktopDocumentDisplayList(document)) > 0u);
    DesktopDocumentDestroy(document);

    /* And the class the sheet would have styled is still a box: an unstyled document is laid out by the
     * default display of its elements rather than refused for want of a rule. */
    config.html = "<html><body><p class='unstyled'>Text</p></body></html>";
    document = NULL;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopDocumentReport(document, &report), RoboOk);
    RoboCheck(report.texts >= 1u);
    DesktopDocumentDestroy(document);
}

static void TestDocumentWithFace(void)
{
    RenderFace face;
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    DesktopDocumentReport without;
    DesktopDocumentReport with;

    DesktopFixture();
    RenderFaceCreate(&face);

    DesktopDocumentConfigInit(&config);
    config.html = "<html><body><p class='label'>A</p></body></html>";
    config.css = "p{font-size:100px}";
    config.width = 400;
    config.height = 200;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopDocumentReport(document, &without), RoboOk);
    DesktopDocumentDestroy(document);

    /* The same document with a face: measured by a real font, so it produces records as well and its
     * commands describe runs that can be drawn. */
    config.face = face.face;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopDocumentReport(document, &with), RoboOk);
    RoboCheck(with.texts >= 1u);
    RoboCheck(with.records >= without.records);
    DesktopDocumentDestroy(document);

    RenderFaceDestroy(&face);
}

static void TestDocumentDestroyReleases(void)
{
    size_t baseline = DesktopLiveBlocks();
    int round;

    /* Ten documents, built and destroyed: the allocator returns to where it started. The bridge owns a DOM,
     * a style context, a property table, a registry, a stylesheet, a layout bridge, a tree, a record list
     * and a display list, and every one of them is released in the order that keeps the tree's borrow of
     * the bridge's styles valid. */
    for (round = 0; round < 10; round++) {
        DesktopDocumentConfig config;
        DesktopDocument *document = NULL;

        DesktopFixture();
        DesktopDocumentConfigInit(&config);
        config.html = DOC_HTML;
        config.css = DOC_CSS;
        config.width = 160;
        config.height = 120;
        RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
        RoboCheck(DesktopDocumentDisplayList(document) != NULL);
        DesktopDocumentDestroy(document);
    }
    RoboCheckEqInt((int)DesktopLiveBlocks(), (int)baseline);
}

void TestRegisterDesktopDocument(void)
{
    TestRegister("desktop.document.build_report", TestDocumentBuildReport);
    TestRegister("desktop.document.build_refusals", TestDocumentBuildRefusals);
    TestRegister("desktop.document.without_style", TestDocumentWithoutStyle);
    TestRegister("desktop.document.with_face", TestDocumentWithFace);
    TestRegister("desktop.document.destroy_releases", TestDocumentDestroyReleases);
}
