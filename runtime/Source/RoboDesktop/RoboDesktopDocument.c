/*
 * RoboDesktop application documents (Phase 11.13) — see RoboDesktopDocument.h.
 *
 * One function does the work: `robo_desktop_document_build` runs the real chain in production order, and the
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
 * has a `robo_desktop_document_release` rather than a destroy that also serves the failure path with a flag:
 * the partial state after a stage-4 failure is exactly the state after a stage-4 success minus two objects,
 * and a single release routine that copes with NULLs is the smallest way to be sure of that.
 *
 * The layout tree **borrows** the bridge's computed styles, so the release order is a rule rather than a
 * convention: the tree goes before the bridge, the bridge before the CSS context, and the texts' measurer is
 * detached before the tree's styles stop existing. Nothing here reorders that.
 */

#include "RoboDesktop/RoboDesktopDocument.h"

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
struct robo_desktop_document {
    int live;
    robo_html_document_id_t html;
    robo_css_style_context_t *styles;
    robo_css_property_table_t *table;
    robo_css_property_registry_t *registry;
    robo_css_stylesheet_id_t sheet;
    robo_layout_dom_t *dom;
    robo_layout_style_source_t source;
    robo_layout_tree_t *tree;
    robo_layout_text_font_t font;
    robo_paint_list_t *records;
    robo_paint_display_list_t *display;
    robo_gfx_size_t viewport;
    robo_desktop_document_report_t report;
};

void robo_desktop_document_config_init(robo_desktop_document_config_t *config)
{
    if (!config) {
        return;
    }
    config->html = NULL;
    config->css = NULL;
    config->width = 800;
    config->height = 600;
    config->face = NULL;
}

int robo_desktop_document_is_implemented(void)
{
    return ROBO_DESKTOP_DOCUMENTS_IMPLEMENTED;
}

/** Everything, in reverse: the picture, then the tree that borrowed the styles, then the styles. */
static void robo_desktop_document_release(robo_desktop_document_t *document)
{
    if (document->display) {
        robo_paint_display_list_destroy(document->display);
        document->display = NULL;
    }
    if (document->records) {
        robo_paint_list_destroy(document->records);
        document->records = NULL;
    }
    robo_layout_text_font_detach(&document->font);
    if (document->tree) {
        /* The tree goes before the bridge: its boxes hold pointers into the bridge's styles. */
        robo_layout_tree_destroy(document->tree);
        document->tree = NULL;
    }
    if (document->dom) {
        robo_layout_dom_destroy(document->dom);
        document->dom = NULL;
    }
    if (document->styles) {
        robo_css_style_context_destroy(document->styles);
        document->styles = NULL;
    }
    if (document->registry) {
        robo_css_property_registry_destroy(document->registry);
        document->registry = NULL;
    }
    if (document->table) {
        robo_css_property_table_destroy(document->table);
        document->table = NULL;
    }
    if (document->sheet != ROBO_CSS_STYLESHEET_ID_NONE) {
        (void)robo_css_stylesheet_destroy(document->sheet, NULL);
        document->sheet = ROBO_CSS_STYLESHEET_ID_NONE;
    }
    if (robo_html_document_is_live(document->html)) {
        (void)robo_html_document_destroy(document->html, NULL);
    }
    document->html = ROBO_HTML_DOCUMENT_ID_NONE;
    document->live = 0;
}

void robo_desktop_document_destroy(robo_desktop_document_t *document)
{
    if (!document) {
        return;
    }
    robo_desktop_document_release(document);
    robo_free(document);
}

/** One stage's failure: release what exists and report the stage's own message. */
static robo_result_t robo_desktop_document_fail(robo_desktop_document_t *document,
                                                robo_desktop_diag_t *diag,
                                                robo_result_t code,
                                                const char *message)
{
    robo_desktop_document_release(document);
    robo_free(document);
    /* The stage's own message is the useful half of a document failure, so it is logged as well as
     * returned: a caller that dropped the diagnostic still gets the reason in the run's output. */
    robo_log_warn("robo_desktop: document refused (%d): %s", (int)code,
                  message ? message : "the document could not be built");
    return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_DOCUMENT, code,
                             message ? message : "the document could not be built");
}

robo_result_t robo_desktop_document_build(const robo_desktop_document_config_t *config,
                                          robo_desktop_document_t **out,
                                          robo_desktop_diag_t *diag)
{
    robo_desktop_document_t *document;
    robo_layout_build_options_t options;
    robo_layout_build_report_t build_report;
    robo_layout_context_t *context = NULL;
    robo_paint_context_t *paint = NULL;
    robo_paint_display_list_builder_t *builder = NULL;
    robo_paint_diag_t paint_diag;
    robo_layout_diag_t layout_diag;
    robo_html_diag_t html_diag;
    robo_css_error_t css_error;
    robo_result_t status;

    if (!config || !out) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_DOCUMENT, ROBO_INVALID_ARGUMENT,
                                 "a configuration and an out pointer are required");
    }
    *out = NULL;
    if (!config->html) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_DOCUMENT, ROBO_INVALID_ARGUMENT,
                                 "a document needs markup");
    }
    if (config->width <= 0 || config->height <= 0 ||
        config->width > (int32_t)ROBO_DESKTOP_MAX_DIMENSION ||
        config->height > (int32_t)ROBO_DESKTOP_MAX_DIMENSION) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_DOCUMENT, ROBO_INVALID_ARGUMENT,
                                 "the document's viewport is not one this build can lay out");
    }

    document = (robo_desktop_document_t *)robo_realloc(NULL, sizeof(*document));
    if (!document) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_DOCUMENT, ROBO_OUT_OF_MEMORY,
                                 "the document could not be allocated");
    }
    {
        size_t bytes = sizeof(*document);
        size_t index;

        for (index = 0u; index < bytes; index++) {
            ((char *)document)[index] = 0;
        }
    }
    document->html = ROBO_HTML_DOCUMENT_ID_NONE;
    document->sheet = ROBO_CSS_STYLESHEET_ID_NONE;
    document->viewport = robo_gfx_size(config->width, config->height);
    robo_layout_text_font_init(&document->font);

    /* 1. The DOM. */
    robo_html_diag_reset(&html_diag);
    status = robo_html_document_create(&document->html, &html_diag);
    if (status != ROBO_OK) {
        return robo_desktop_document_fail(document, diag, status,
                                         html_diag.message ? html_diag.message
                                                           : "the HTML document could not be created");
    }
    {
        robo_html_source_t source;

        robo_html_source_init(&source);
        status = robo_html_source_string(&source, config->html, &html_diag);
        if (status == ROBO_OK) {
            status = robo_html_build_tree(document->html, &source, NULL, &html_diag);
        }
        if (status != ROBO_OK) {
            return robo_desktop_document_fail(document, diag, status,
                                             html_diag.message
                                                 ? html_diag.message
                                                 : "the markup could not be parsed");
        }
    }

    /* 2. The styles: a context with the standard property registry, and the author sheet. */
    status = robo_css_style_context_create(&document->styles, NULL);
    if (status == ROBO_OK) {
        status = robo_css_property_table_create(&document->table, NULL);
    }
    if (status == ROBO_OK) {
        status = robo_css_property_registry_create_standard(&document->registry, NULL);
    }
    if (status == ROBO_OK) {
        status = robo_css_property_registry_apply_to_table(document->registry, document->table, NULL,
                                                          NULL);
    }
    if (status == ROBO_OK) {
        status = robo_css_style_context_set_property_table(document->styles, document->table, NULL);
    }
    if (status == ROBO_OK) {
        status = robo_css_style_context_set_property_registry(document->styles, document->registry,
                                                             NULL);
    }
    if (status != ROBO_OK) {
        return robo_desktop_document_fail(document, diag, status,
                                          "the style context could not be created");
    }
    if (config->css) {
        robo_css_error_init(&css_error);
        status = robo_css_stylesheet_create(&document->sheet, ROBO_CSS_ORIGIN_AUTHOR, NULL);
        if (status == ROBO_OK) {
            status = robo_css_stylesheet_copy_source(document->sheet,
                                                     (const uint8_t *)config->css,
                                                     robo_desktop_string_length(config->css), NULL);
        }
        if (status == ROBO_OK) {
            status = robo_css_stylesheet_parse(document->sheet, &css_error, NULL);
        }
        if (status == ROBO_OK) {
            size_t index;

            /* A sheet's selectors are parsed separately from its declarations, and a sheet whose
             * selectors were never parsed matches nothing -- so this loop is not tidiness, it is the
             * difference between a styled document and an unstyled one. */
            for (index = 0u; index < robo_css_stylesheet_rule_count(document->sheet) && status == ROBO_OK;
                 index++) {
                robo_css_style_rule_t *rule = (robo_css_style_rule_t *)robo_css_rule_style(
                    robo_css_stylesheet_rule(document->sheet, index));

                if (rule) {
                    status = robo_css_style_rule_parse_selectors(
                        rule, robo_css_stylesheet_source(document->sheet), &css_error, NULL);
                }
            }
        }
        if (status == ROBO_OK) {
            status = robo_css_style_context_add_sheet(document->styles, ROBO_CSS_ORIGIN_AUTHOR,
                                                     document->sheet, NULL);
        }
        if (status != ROBO_OK) {
            return robo_desktop_document_fail(document, diag, status,
                                             css_error.message ? css_error.message
                                                               : "the stylesheet could not be parsed");
        }
    }

    /* 3. The bridge: DOM elements in, computed styles out. */
    robo_layout_diag_reset(&layout_diag);
    status = robo_layout_dom_create(document->styles, document->html, &document->dom, &layout_diag);
    if (status == ROBO_OK) {
        status = robo_layout_dom_refresh(document->dom, &layout_diag);
    }
    if (status == ROBO_OK) {
        status = robo_layout_dom_style_source(document->dom, &document->source, &layout_diag);
    }
    if (status != ROBO_OK) {
        return robo_desktop_document_fail(
            document, diag, status,
            layout_diag.message ? layout_diag.message : "the layout bridge could not be built");
    }

    /* 4. The tree, and one layout pass. */
    status = robo_layout_tree_create(&document->tree, &layout_diag);
    if (status == ROBO_OK) {
        robo_layout_build_options_init(&options);
        options.styles = &document->source;
        status = robo_layout_tree_build_from_document(document->tree, document->html, &options,
                                                     &build_report, &layout_diag);
    }
    if (status == ROBO_OK) {
        status = robo_layout_context_create(&context, &layout_diag);
    }
    if (status == ROBO_OK) {
        robo_layout_size_t viewport;

        viewport.width = document->viewport.width;
        viewport.height = document->viewport.height;
        status = robo_layout_context_set_viewport(context, viewport, &layout_diag);
    }
    if (status == ROBO_OK && config->face) {
        robo_layout_text_metrics_t metrics;

        status = robo_layout_text_font_attach(&document->font, config->face);
        if (status == ROBO_OK) {
            /* RoboLayout's own font-backed measurer, attached to the caller's face: text runs are measured
             * by the real face rather than by a scripted answer, which is the whole difference between a
             * document and a fixture. */
            metrics = robo_layout_text_font_metrics(&document->font);
            status = robo_layout_context_set_text_metrics(context, &metrics);
        }
    }
    if (status == ROBO_OK) {
        status = robo_layout_context_run(context, document->tree, &layout_diag);
    }
    if (context) {
        robo_layout_context_destroy(context);
        context = NULL;
    }
    if (status != ROBO_OK) {
        return robo_desktop_document_fail(
            document, diag, status,
            layout_diag.message ? layout_diag.message : "the document could not be laid out");
    }
    document->report.elements = robo_layout_dom_element_count(document->dom);
    document->report.boxes = build_report.boxes_created;
    document->report.texts = build_report.texts_created;

    /* 5. The records, and the display list built from them. */
    robo_paint_diag_reset(&paint_diag);
    status = robo_paint_list_create(&document->records, &paint_diag);
    if (status == ROBO_OK) {
        status = robo_paint_context_create(&paint, &paint_diag);
    }
    if (status == ROBO_OK) {
        status = robo_paint_context_run(paint, document->tree, document->records, &paint_diag);
    }
    if (paint) {
        robo_paint_context_destroy(paint);
        paint = NULL;
    }
    if (status == ROBO_OK) {
        status = robo_paint_display_list_create(&document->display, &paint_diag);
    }
    if (status == ROBO_OK) {
        status = robo_paint_display_list_builder_create(&builder, &paint_diag);
    }
    if (status == ROBO_OK) {
        status = robo_paint_display_list_builder_build(builder, document->records, document->display,
                                                      NULL, &paint_diag);
    }
    if (builder) {
        robo_paint_display_list_builder_destroy(builder);
        builder = NULL;
    }
    if (status != ROBO_OK) {
        return robo_desktop_document_fail(
            document, diag, status,
            paint_diag.message ? paint_diag.message : "the document could not be painted");
    }
    document->report.records = robo_paint_list_count(document->records);
    document->report.commands = robo_paint_display_list_count(document->display);
    document->live = 1;
    *out = document;
    /* The five stage counts, in pipeline order: when a window is blank, this line says which stage
     * produced nothing without a caller having to build the document again to ask. */
    robo_log_debug("robo_desktop: document built %dx%d (elements=%llu, boxes=%llu, texts=%llu, "
                   "records=%llu, commands=%llu)",
                   (int)document->viewport.width, (int)document->viewport.height,
                   (unsigned long long)document->report.elements,
                   (unsigned long long)document->report.boxes,
                   (unsigned long long)document->report.texts,
                   (unsigned long long)document->report.records,
                   (unsigned long long)document->report.commands);
    return ROBO_OK;
}

int robo_desktop_document_is_live(const robo_desktop_document_t *document)
{
    return (document && document->live) ? 1 : 0;
}

const robo_paint_display_list_t *robo_desktop_document_display_list(
    const robo_desktop_document_t *document)
{
    return document ? document->display : NULL;
}

robo_gfx_size_t robo_desktop_document_viewport(const robo_desktop_document_t *document)
{
    return document ? document->viewport : robo_gfx_size(0, 0);
}

robo_result_t robo_desktop_document_report(const robo_desktop_document_t *document,
                                           robo_desktop_document_report_t *out)
{
    if (!document || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = document->report;
    return ROBO_OK;
}
