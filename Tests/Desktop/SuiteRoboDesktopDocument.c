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
#include "RoboTypographyTestSupport.h"

#include "RoboDesktop/RoboDesktopScene.h"

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

/*
 * The per-run family resolver (Phase 12.17).
 *
 * Two paragraphs, one naming the shipped face and one naming a family the pack does not have followed by a
 * generic: the report says one family *name* missed and the pack's generic (and the shipped name) resolved,
 * which is the whole of "a document's `font-family` reaches the font database" in two numbers. Without a
 * registry the provider is not built and nothing is consulted -- the single-face behaviour, stated.
 */
static void TestDocumentFontFamilies(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    RoboTypographyStyle body;
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    DesktopDocumentReport report;

    RoboCheckEqInt(RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDocumentBody, &body,
                                         NULL),
                   RoboOk);
    RoboCheck(body.face != NULL);

    DesktopFixture();
    DesktopDocumentConfigInit(&config);
    config.html = "<html><body><p class='a'>one</p><p class='b'>two</p></body></html>";
    config.css =
        "html,body,p{display:block}"
        "p.a{font-family:\"Inter\"}"
        "p.b{font-family:\"Not Shipped\",sans-serif}";
    config.width = 320;
    config.height = 200;
    config.face = body.face;
    config.typography = fixture.resolver;
    config.registry = fixture.registry;

    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopDocumentReport(document, &report), RoboOk);
    /* `Inter` and the generic both resolve; the unknown name does not. Two paragraphs, three
     * entries walked: the exact counts rather than `>= 1`, so a selection that stopped after the
     * first entry (or walked the list twice) would show here. */
    RoboCheckEqInt((int)report.fontFamiliesResolved, 2);
    RoboCheckEqInt((int)report.fontFamiliesUnresolved, 1);
    DesktopDocumentDestroy(document);

    /* No registry: no provider, no lookups, and the document is still built and drawn with one face. */
    config.registry = NULL;
    document = NULL;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopDocumentReport(document, &report), RoboOk);
    RoboCheckEqInt((int)report.fontFamiliesResolved, 0);
    RoboCheckEqInt((int)report.fontFamiliesUnresolved, 0);
    RoboCheck(PaintDisplayListCount(DesktopDocumentDisplayList(document)) > 0u);
    DesktopDocumentDestroy(document);

    TypographyFixtureClose(&fixture);
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

/*
 * The compiled page, as a document.
 *
 * This is the artifact test the phase asks for rather than a test of the *source*: the markup and the
 * sheet come from `Generated/DesktopPage.h`, which is what the guest builds its desktop from, so a
 * markup change that reaches the image without reaching a test would fail here first.
 */
static void TestDocumentShippedPage(void)
{
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    DesktopDocumentReport report;
    DesktopDiag docDiag;
    const PaintDisplayList *list;

    DesktopFixture();
    DesktopDocumentConfigInit(&config);
    config.html = DesktopSceneMarkup();
    config.css = DesktopSceneStyle();
    config.width = 1024;
    config.height = 768;

    /*
     * The page builds — and this is not a formality. The shell's own page is a *fixed* window layer
     * holding *absolutely positioned* window slots, and the classification pass refused it outright
     * until Phase 14.0 learned that an out-of-flow block container establishes a block context of its
     * own (`layout.absolute.block_child_of_positioned` is the unit case). A page that reaches the image
     * without reaching this case is a page nobody laid out.
     */
    docDiag.code = RoboOk;
    docDiag.message = NULL;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, &docDiag), RoboOk);
    RoboCheckMsg(document != NULL, docDiag.message ? docDiag.message : "no diagnostic");
    RoboCheckEqInt(GetDesktopDocumentReport(document, &report), RoboOk);
    RoboCheckEqInt(DesktopDocumentViewport(document).width, 1024);
    RoboCheckEqInt(DesktopDocumentViewport(document).height, 768);

    /*
     * The picture is the whole page rather than a fragment of it, which is what the numbers are for:
     * every element the markup names becomes a box, the page's text is laid out, and the paint pass
     * produces an operation for each placed box. The floors are deliberately far below the counts this
     * build produces (~85 boxes, ~300 commands) so that adding a component does not break the case,
     * while a regression to "the root box and nothing else" — the shape the classification defect left
     * behind, three commands for eighty-five boxes — fails loudly.
     */
    RoboCheck(report.elements >= 60u);
    RoboCheckEqInt((int)report.boxes, (int)report.elements);
    RoboCheck(report.texts >= 10u);
    RoboCheck(report.records >= 100u);
    list = DesktopDocumentDisplayList(document);
    RoboCheck(list != NULL);
    RoboCheck(PaintDisplayListCount(list) >= 100u);
    RoboCheckEqInt((int)report.commands, (int)PaintDisplayListCount(list));

    DesktopDocumentDestroy(document);
}

void TestRegisterDesktopDocument(void)
{
    TestRegister("desktop.document.shipped_page", TestDocumentShippedPage);
    TestRegister("desktop.document.build_report", TestDocumentBuildReport);
    TestRegister("desktop.document.build_refusals", TestDocumentBuildRefusals);
    TestRegister("desktop.document.without_style", TestDocumentWithoutStyle);
    TestRegister("desktop.document.with_face", TestDocumentWithFace);
    TestRegister("desktop.document.font_families", TestDocumentFontFamilies);
    TestRegister("desktop.document.destroy_releases", TestDocumentDestroyReleases);
}
