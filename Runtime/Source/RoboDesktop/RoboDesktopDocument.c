/*
 * RoboDesktop application documents (Phase 11.13) — see RoboDesktopDocument.h.
 *
 * One function does the work: `DesktopDocumentBuild` runs the real chain in production order, and the
 * order is the interesting part of this file.
 *
 *   1. the HTML document, and the tree builder — the DOM every later stage asks about;
 *   2. the CSS context, its property table and registry, and the author sheet parsed **with its selectors**,
 *      because a sheet whose selectors were never parsed matches nothing and would silently produce an
 *      unstyled document rather than a failure;
 *   3. the layout bridge, which is what turns DOM elements into computed styles, and `refresh` before the
 *      style source is taken — a source taken from an unrefreshed bridge resolves nothing;
 *   4. the tree, built from the document with that source, then one layout pass with RoboLayout's own
 *      font-backed measurer attached to the caller's face;
 *   5. paint, into a record list, and the display list built from it.
 *
 * Every failure releases **everything built so far**, in reverse, through one function. That is why this file
 * has a `DesktopDocumentRelease` rather than a destroy that also serves the failure path with a flag:
 * the partial state after a stage-4 failure is exactly the state after a stage-4 success minus two objects,
 * and a single release routine that copes with NULLs is the smallest way to be sure of that.
 *
 * The layout tree **borrows** the bridge's computed styles, so the release order is a rule rather than a
 * convention: the tree goes before the bridge, the bridge before the CSS context, and the texts' measurer is
 * detached before the tree's styles stop existing. Nothing here reorders that.
 */

#include "RoboDesktop/RoboDesktopDocument.h"

#include "RoboDesktop/RoboDesktopFontProvider.h"
#include "RoboDesktopInternal.h"

#include "RoboBase/RoboError.h"
#include "RoboBase/RoboLog.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSPropertyRegistry.h"
#include "RoboCSS/RoboCSSSelectorParser.h"
#include "RoboCSS/RoboCSSStyle.h"
#include "RoboCSS/RoboCSSStyleSheet.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboLayout/RoboLayoutBuilder.h"
#include "RoboLayout/RoboLayoutContext.h"
#include "RoboLayout/RoboLayoutDom.h"
#include "RoboLayout/RoboLayoutText.h"
#include "RoboLayout/RoboLayoutTextFont.h"
#include "RoboLayout/RoboLayoutTree.h"
#include "RoboMem/RoboMem.h"
#include "RoboPaint/RoboPaintContext.h"
#include "RoboPaint/RoboPaintDisplayListBuilder.h"
#include "RoboPaint/RoboPaintList.h"

/** The document object: owned state, released in one place and in one order (see the file comment). */
struct DesktopDocument {
    int live;
    HtmlDocumentId html;
    CssStyleContext *styles;
    CssPropertyTable *table;
    CssPropertyRegistry *registry;
    CssStylesheetId sheet;
    /** The generated typography defaults, at runtime origin, or `RoboCssStylesheetIdNone`. */
    CssStylesheetId runtimeSheet;
    /** The face text was measured with: the resolver's body role face, or the caller's own. */
    FontFace *face;
    LayoutDom *dom;
    LayoutStyleSource source;
    LayoutTree *tree;
    LayoutTextFont font;
    /** The per-run family resolver (Phase 12.17), or NULL for a single-face document. */
    DesktopFontProvider *fontProvider;
    PaintList *records;
    PaintDisplayList *display;
    GfxSize viewport;
    DesktopDocumentReport report;
};

void DesktopDocumentConfigInit(DesktopDocumentConfig *config)
{
    if (!config) {
        return;
    }
    config->html = NULL;
    config->css = NULL;
    config->width = 800;
    config->height = 600;
    config->face = NULL;
    config->typography = NULL;
    config->registry = NULL;
}

int DesktopDocumentIsImplemented(void)
{
    return RoboDesktopDocumentsImplemented;
}

/** Everything, in reverse: the picture, then the tree that borrowed the styles, then the styles. */
static void DesktopDocumentRelease(DesktopDocument *document)
{
    if (document->display) {
        PaintDisplayListDestroy(document->display);
        document->display = NULL;
    }
    if (document->records) {
        PaintListDestroy(document->records);
        document->records = NULL;
    }
    LayoutTextFontDetach(&document->font);
    if (document->fontProvider) {
        DesktopFontProviderDestroy(document->fontProvider);
        document->fontProvider = NULL;
    }
    if (document->tree) {
        /* The tree goes before the bridge: its boxes hold pointers into the bridge's styles. */
        LayoutTreeDestroy(document->tree);
        document->tree = NULL;
    }
    if (document->dom) {
        LayoutDomDestroy(document->dom);
        document->dom = NULL;
    }
    if (document->styles) {
        CssStyleContextDestroy(document->styles);
        document->styles = NULL;
    }
    if (document->registry) {
        CssPropertyRegistryDestroy(document->registry);
        document->registry = NULL;
    }
    if (document->table) {
        CssPropertyTableDestroy(document->table);
        document->table = NULL;
    }
    if (document->runtimeSheet != RoboCssStylesheetIdNone) {
        (void)CssStylesheetDestroy(document->runtimeSheet, NULL);
        document->runtimeSheet = RoboCssStylesheetIdNone;
    }
    if (document->sheet != RoboCssStylesheetIdNone) {
        (void)CssStylesheetDestroy(document->sheet, NULL);
        document->sheet = RoboCssStylesheetIdNone;
    }
    if (HtmlDocumentIsLive(document->html)) {
        (void)HtmlDocumentDestroy(document->html, NULL);
    }
    document->html = RoboHtmlDocumentIdNone;
    document->live = 0;
}

void DesktopDocumentDestroy(DesktopDocument *document)
{
    if (!document) {
        return;
    }
    DesktopDocumentRelease(document);
    RoboFree(document);
}

/**
 * Create, parse and register one stylesheet at @p origin.
 *
 * `*slot` receives the sheet's id **as soon as it exists**, so the document's release path owns it from
 * that moment rather than from the moment it parsed: a sheet that failed to parse would otherwise leak,
 * and it would leak on the one path a test that only builds good documents never walks.
 *
 * The selectors are parsed separately from the declarations, for the reason the author sheet's own call
 * states: a sheet whose selectors were never parsed matches nothing, and a sheet that matches nothing is
 * indistinguishable from a document nothing styled.
 */
static Result DesktopDocumentAddSheet(DesktopDocument *document,
                                      CssOrigin origin,
                                      const char *source,
                                      CssStylesheetId *slot,
                                      CssError *error)
{
    CssStylesheetId sheet = RoboCssStylesheetIdNone;
    Result status;

    status = CssStylesheetCreate(&sheet, origin, NULL);
    if (status != RoboOk) {
        return status;
    }
    *slot = sheet;
    if ((status = CssStylesheetCopySource(sheet, (const uint8_t *)source,
                                          DesktopStringLength(source), NULL)) != RoboOk) {
        return status;
    }
    if ((status = CssStylesheetParse(sheet, error, NULL)) != RoboOk) {
        return status;
    }
    {
        size_t index;

        for (index = 0u; index < CssStylesheetRuleCount(sheet) && status == RoboOk; index++) {
            CssStyleRule *rule =
                (CssStyleRule *)CssRuleStyle(CssStylesheetRule(sheet, index));

            if (rule) {
                status = CssStyleRuleParseSelectors(rule, CssStylesheetSource(sheet), error, NULL);
            }
        }
    }
    if (status != RoboOk) {
        return status;
    }
    return CssStyleContextAddSheet(document->styles, origin, sheet, NULL);
}

/** One stage's failure: release what exists and report the stage's own message. */
static Result DesktopDocumentFail(DesktopDocument *document,
                                                DesktopDiag *diag,
                                                Result code,
                                                const char *message)
{
    DesktopDocumentRelease(document);
    RoboFree(document);
    /* The stage's own message is the useful half of a document failure, so it is logged as well as
     * returned: a caller that dropped the diagnostic still gets the reason in the run's output. */
    LogWarn("robo_desktop: document refused (%d): %s", (int)code,
                  message ? message : "the document could not be built");
    return RoboDesktopDiag(diag, RoboDesktopDomainDocument, code,
                             message ? message : "the document could not be built");
}

Result DesktopDocumentBuild(const DesktopDocumentConfig *config,
                                          DesktopDocument **out,
                                          DesktopDiag *diag)
{
    DesktopDocument *document;
    LayoutBuildOptions options;
    LayoutBuildReport buildReport;
    LayoutContext *context = NULL;
    PaintContext *paint = NULL;
    PaintDisplayListBuilder *builder = NULL;
    PaintDiag paintDiag;
    LayoutDiag layoutDiag;
    HtmlDiag htmlDiag;
    CssError cssError;
    Result status;

    if (!config || !out) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboInvalidArgument,
                                 "a configuration and an out pointer are required");
    }
    *out = NULL;
    if (!config->html) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboInvalidArgument,
                                 "a document needs markup");
    }
    if (config->width <= 0 || config->height <= 0 ||
        config->width > (int32_t)RoboDesktopMaxDimension ||
        config->height > (int32_t)RoboDesktopMaxDimension) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboInvalidArgument,
                                 "the document's viewport is not one this build can lay out");
    }

    document = (DesktopDocument *)Realloc(NULL, sizeof(*document));
    if (!document) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboOutOfMemory,
                                 "the document could not be allocated");
    }
    {
        size_t bytes = sizeof(*document);
        size_t index;

        for (index = 0u; index < bytes; index++) {
            ((char *)document)[index] = 0;
        }
    }
    document->html = RoboHtmlDocumentIdNone;
    document->sheet = RoboCssStylesheetIdNone;
    document->runtimeSheet = RoboCssStylesheetIdNone;
    document->viewport = MakeGfxSize(config->width, config->height);
    LayoutTextFontInit(&document->font);
    /*
     * The face the document is measured and drawn with (Phase 12.3). A typography resolver is the
     * authority when it names a face for the body role, and `config->face` is the fallback -- the same
     * precedence the shell's chrome uses, so "who chose the face?" has one answer in both places.
     */
    document->face = config->face;
    if (config->typography) {
        RoboTypographyStyle body;

        if (RoboTypographyResolve(config->typography, RoboTypographyRoleDocumentBody, &body, NULL) ==
                RoboOk &&
            body.face) {
            document->face = body.face;
        }
    }

    /* 1. The DOM. */
    HtmlDiagReset(&htmlDiag);
    status = HtmlDocumentCreate(&document->html, &htmlDiag);
    if (status != RoboOk) {
        return DesktopDocumentFail(document, diag, status,
                                         htmlDiag.message ? htmlDiag.message
                                                           : "the HTML document could not be created");
    }
    {
        HtmlSource source;

        HtmlSourceInit(&source);
        status = HtmlSourceString(&source, config->html, &htmlDiag);
        if (status == RoboOk) {
            status = HtmlBuildTree(document->html, &source, NULL, &htmlDiag);
        }
        if (status != RoboOk) {
            return DesktopDocumentFail(document, diag, status,
                                             htmlDiag.message
                                                 ? htmlDiag.message
                                                 : "the markup could not be parsed");
        }
    }

    /* 2. The styles: a context with the standard property registry, and the author sheet. */
    status = CssStyleContextCreate(&document->styles, NULL);
    if (status == RoboOk) {
        status = CssPropertyTableCreate(&document->table, NULL);
    }
    if (status == RoboOk) {
        status = CssPropertyRegistryCreateStandard(&document->registry, NULL);
    }
    if (status == RoboOk) {
        status = CssPropertyRegistryApplyToTable(document->registry, document->table, NULL,
                                                          NULL);
    }
    if (status == RoboOk) {
        status = CssStyleContextSetPropertyTable(document->styles, document->table, NULL);
    }
    if (status == RoboOk) {
        status = CssStyleContextSetPropertyRegistry(document->styles, document->registry,
                                                             NULL);
    }
    if (status != RoboOk) {
        return DesktopDocumentFail(document, diag, status,
                                          "the style context could not be created");
    }
    /*
     * The generated system defaults first (Phase 12.3), at **runtime** origin: below the author's own
     * sheet in the cascade, so a document that declares its own `h1` still wins, and a document that
     * declares nothing starts from the system's table rather than from the layout engine's initial 16px.
     */
    if (config->typography) {
        char defaults[RoboTypographyDocumentSheetMaxBytes];
        size_t defaultsLength = 0u;

        CssErrorInit(&cssError);
        status = RoboTypographyResolverDocumentStyleSheet(config->typography, defaults,
                                                         sizeof(defaults), &defaultsLength);
        if (status == RoboOk) {
            status = DesktopDocumentAddSheet(document, RoboCssOriginRuntime, defaults,
                                             &document->runtimeSheet, &cssError);
        }
        if (status != RoboOk) {
            return DesktopDocumentFail(
                document, diag, status,
                cssError.message ? cssError.message
                                 : "the system typography defaults were refused by the style context");
        }
    }

    if (config->css) {
        CssErrorInit(&cssError);
        status = DesktopDocumentAddSheet(document, RoboCssOriginAuthor, config->css,
                                         &document->sheet, &cssError);
        if (status != RoboOk) {
            return DesktopDocumentFail(document, diag, status,
                                             cssError.message ? cssError.message
                                                               : "the stylesheet could not be parsed");
        }
    }

    /* 3. The bridge: DOM elements in, computed styles out. */
    LayoutDiagReset(&layoutDiag);
    status = LayoutDomCreate(document->styles, document->html, &document->dom, &layoutDiag);
    if (status == RoboOk) {
        status = LayoutDomRefresh(document->dom, &layoutDiag);
    }
    if (status == RoboOk) {
        status = LayoutDomStyleSource(document->dom, &document->source, &layoutDiag);
    }
    if (status != RoboOk) {
        return DesktopDocumentFail(
            document, diag, status,
            layoutDiag.message ? layoutDiag.message : "the layout bridge could not be built");
    }

    /* 4. The tree, and one layout pass. */
    status = LayoutTreeCreate(&document->tree, &layoutDiag);
    if (status == RoboOk) {
        LayoutBuildOptionsInit(&options);
        options.styles = &document->source;
        status = LayoutTreeBuildFromDocument(document->tree, document->html, &options,
                                                     &buildReport, &layoutDiag);
    }
    if (status == RoboOk) {
        status = LayoutContextCreate(&context, &layoutDiag);
    }
    if (status == RoboOk) {
        LayoutSize viewport;

        viewport.width = document->viewport.width;
        viewport.height = document->viewport.height;
        status = LayoutContextSetViewport(context, viewport, &layoutDiag);
    }
    if (status == RoboOk && document->face) {
        LayoutTextMetrics metrics;

        status = LayoutTextFontAttach(&document->font, document->face);
        if (status == RoboOk && config->registry) {
            /*
             * The per-run family resolver (Phase 12.17). With a registry, each run's CSS `font-family`
             * resolves to its own face; without one the measurer measures every run with the single
             * attached face, which is the pre-12.17 behaviour and still correct for a caller that has one
             * system face and no registry to hand.
             */
            status = DesktopFontProviderCreate(
                config->registry,
                config->typography ? RoboTypographyResolverPack(config->typography) : NULL,
                &document->fontProvider, NULL);
        }
        if (status == RoboOk && document->fontProvider) {
            LayoutFontProvider provider = DesktopFontProviderLayout(document->fontProvider);

            LayoutTextFontSetProvider(&document->font, &provider);
        }
        if (status == RoboOk) {
            /* RoboLayout's own font-backed measurer, attached to the caller's face: text runs are measured
             * by the real face rather than by a scripted answer, which is the whole difference between a
             * document and a fixture. */
            metrics = LayoutTextFontMetrics(&document->font);
            status = LayoutContextSetTextMetrics(context, &metrics);
        }
    }
    if (status == RoboOk) {
        status = LayoutContextRun(context, document->tree, &layoutDiag);
    }
    if (context) {
        LayoutContextDestroy(context);
        context = NULL;
    }
    if (status != RoboOk) {
        return DesktopDocumentFail(
            document, diag, status,
            layoutDiag.message ? layoutDiag.message : "the document could not be laid out");
    }
    document->report.elements = LayoutDomElementCount(document->dom);
    document->report.boxes = buildReport.boxesCreated;
    document->report.texts = buildReport.textsCreated;

    /* 5. The records, and the display list built from them. */
    PaintDiagReset(&paintDiag);
    status = PaintListCreate(&document->records, &paintDiag);
    if (status == RoboOk) {
        status = PaintContextCreate(&paint, &paintDiag);
    }
    if (status == RoboOk) {
        /*
         * The face the pass **shapes** with (12.16): the same one the document was measured against, so the
         * glyphs a run records are the glyphs the line was broken against, and the pass is where they are
         * produced rather than each frame's drawing pass. A document built without a face records its
         * characters and no glyphs, and a drawing pass says `noFont` -- the pre-12.16 shape, which is what a
         * caller that only wants the text's placement can still ask for.
         */
        PaintContextSetTextFace(paint, document->face);
        status = PaintContextRun(paint, document->tree, document->records, &paintDiag);
    }
    if (paint) {
        PaintContextDestroy(paint);
        paint = NULL;
    }
    if (status == RoboOk) {
        status = PaintDisplayListCreate(&document->display, &paintDiag);
    }
    if (status == RoboOk) {
        status = PaintDisplayListBuilderCreate(&builder, &paintDiag);
    }
    if (status == RoboOk) {
        status = PaintDisplayListBuilderBuild(builder, document->records, document->display,
                                                      NULL, &paintDiag);
    }
    if (builder) {
        PaintDisplayListBuilderDestroy(builder);
        builder = NULL;
    }
    if (status != RoboOk) {
        return DesktopDocumentFail(
            document, diag, status,
            paintDiag.message ? paintDiag.message : "the document could not be painted");
    }
    document->report.records = PaintListCount(document->records);
    document->report.commands = PaintDisplayListCount(document->display);
    document->report.fontFamiliesResolved = DesktopFontProviderResolved(document->fontProvider);
    document->report.fontFamiliesUnresolved =
        DesktopFontProviderUnresolved(document->fontProvider);
    document->live = 1;
    *out = document;
    /* The five stage counts, in pipeline order: when a window is blank, this line says which stage
     * produced nothing without a caller having to build the document again to ask. */
    LogDebug("robo_desktop: document built %dx%d (elements=%llu, boxes=%llu, texts=%llu, "
                   "records=%llu, commands=%llu)",
                   (int)document->viewport.width, (int)document->viewport.height,
                   (unsigned long long)document->report.elements,
                   (unsigned long long)document->report.boxes,
                   (unsigned long long)document->report.texts,
                   (unsigned long long)document->report.records,
                   (unsigned long long)document->report.commands);
    return RoboOk;
}

int DesktopDocumentIsLive(const DesktopDocument *document)
{
    return (document && document->live) ? 1 : 0;
}

const PaintDisplayList *DesktopDocumentDisplayList(
    const DesktopDocument *document)
{
    return document ? document->display : NULL;
}

FontFace *DesktopDocumentFace(const DesktopDocument *document)
{
    return document ? document->face : NULL;
}

GfxSize DesktopDocumentViewport(const DesktopDocument *document)
{
    return document ? document->viewport : MakeGfxSize(0, 0);
}

const LayoutTree *DesktopDocumentTree(const DesktopDocument *document)
{
    return document ? document->tree : NULL;
}

Result GetDesktopDocumentReport(const DesktopDocument *document,
                                           DesktopDocumentReport *out)
{
    if (!document || !out) {
        return RoboInvalidArgument;
    }
    *out = document->report;
    return RoboOk;
}
