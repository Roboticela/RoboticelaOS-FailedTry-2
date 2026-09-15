/*
 * RoboDemo HTML implementation (visual checkpoint) — see RoboDemoHtml.h.
 *
 * Layout of this file mirrors the three things the adapter does:
 *   1. walk    — traverse the real RoboHTML document tree the 8.3 tree
 *                builder constructed from the 8.2 tokenizer's stream
 *   2. layout  — one deterministic vertical flow
 *   3. paint   — RoboGraphics primitives only
 *
 * Everything here is disposable demo code. It is NOT the DOM and NOT
 * CSS/layout/paint architecture. The tree it walks, however, is the real
 * one: Phases 9/10/11 replace this walk and the constants below, not the
 * document model underneath.
 */

#include "RoboDemo/RoboDemoHtml.h"
#include "RoboDemo/RoboDemoFont.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Default document                                                    */
/* ------------------------------------------------------------------ */

/*
 * Byte-identical to demo/assets/Demo.html. The asset is the editable copy;
 * the demo test asserts the two never drift, so the compiled-in source can
 * serve the guest (which has no filesystem in this path) without silently
 * going stale.
 */
static const char g_demo_source[] =
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
    "</html>\n";

const char *robo_demo_default_html(void)
{
    return g_demo_source;
}

size_t robo_demo_default_html_length(void)
{
    return sizeof(g_demo_source) - 1u;
}

/* ------------------------------------------------------------------ */
/* Palette (AGENTS.md light-mode tokens, resolved to RGBA8)            */
/* ------------------------------------------------------------------ */

static const robo_gfx_color_rgba8_t C_BG = {245, 245, 247, 255};      /* --bg-secondary */
static const robo_gfx_color_rgba8_t C_TEXT = {29, 29, 31, 255};       /* --text-primary */
static const robo_gfx_color_rgba8_t C_MUTED = {110, 110, 115, 255};   /* --text-secondary */
static const robo_gfx_color_rgba8_t C_BRAND = {14, 122, 255, 255};    /* --brand-primary */
static const robo_gfx_color_rgba8_t C_BRAND_HOVER = {10, 106, 232, 255};
static const robo_gfx_color_rgba8_t C_BRAND_ACTIVE = {7, 87, 199, 255};
static const robo_gfx_color_rgba8_t C_BORDER = {210, 210, 215, 255};  /* --border-default */
static const robo_gfx_color_rgba8_t C_ON_BRAND = {255, 255, 255, 255};

/* ------------------------------------------------------------------ */
/* Layout constants (stand-ins for Phase 9/10 computed styles)         */
/* ------------------------------------------------------------------ */

#define DEMO_PAD 32
#define DEMO_ACCENT_BAR_H 6
#define DEMO_HEADING_SCALE 3
#define DEMO_BODY_SCALE 2
#define DEMO_BUTTON_LABEL_SCALE 2
#define DEMO_BUTTON_HEIGHT 44
#define DEMO_BUTTON_PAD_X 22
#define DEMO_BUTTON_MIN_WIDTH 160
#define DEMO_HEADING_GAP 20
#define DEMO_PARAGRAPH_GAP 14
#define DEMO_BUTTON_GAP 16

/** Cap on retained parse diagnostics (bounded demo memory). */
#define DEMO_MAX_DIAGNOSTICS 32u

/* ------------------------------------------------------------------ */
/* Page model                                                          */
/* ------------------------------------------------------------------ */

typedef struct robo_demo_block {
    robo_demo_block_style_t style;
    /** Owned collapsed text (NUL-terminated). */
    char *text;
    size_t length;
    int text_scale;
    robo_gfx_rect_t rect;
} robo_demo_block_t;

struct robo_demo_page {
    char title[128];
    robo_demo_block_t *blocks;
    size_t block_count;
    size_t block_capacity;
    size_t supported_elements;
    size_t ignored_tags;
    robo_html_diag_t diags[DEMO_MAX_DIAGNOSTICS];
    size_t diag_count;
    int32_t layout_width;
    int32_t layout_height;
    int has_button;
    robo_gfx_rect_t button_rect;
};

const char *robo_demo_block_style_name(robo_demo_block_style_t style)
{
    switch (style) {
    case ROBO_DEMO_BLOCK_HEADING:
        return "heading";
    case ROBO_DEMO_BLOCK_PARAGRAPH:
        return "paragraph";
    case ROBO_DEMO_BLOCK_BUTTON:
        return "button";
    case ROBO_DEMO_BLOCK_DOCUMENT_TITLE:
        return "title";
    default:
        return "unknown";
    }
}



/**
 * Collapse ASCII whitespace the way HTML text does: leading and trailing
 * runs are dropped, inner runs become a single space. Demo-scope only — no
 * Unicode whitespace, no CSS `white-space` modes.
 */
static void collapse_ws(const char *src, size_t len, char *dst, size_t dst_cap)
{
    size_t i = 0u;
    size_t out = 0u;
    int pending_space = 0;

    if (!dst || dst_cap == 0u) {
        return;
    }
    while (i < len) {
        char c = src[i];
        int is_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                     c == '\f' || c == '\v');
        if (is_ws) {
            if (out > 0u) {
                pending_space = 1;
            }
        } else {
            if (pending_space && out + 1u < dst_cap) {
                dst[out++] = ' ';
            }
            pending_space = 0;
            if (out + 1u >= dst_cap) {
                break;
            }
            dst[out++] = c;
        }
        i++;
    }
    dst[out] = '\0';
}

static robo_result_t page_add_block(robo_demo_page_t *page,
                                    robo_demo_block_style_t style,
                                    const char *text,
                                    size_t length,
                                    int scale)
{
    robo_demo_block_t *grown;
    char *copy;

    if (page->block_count == page->block_capacity) {
        size_t next = (page->block_capacity == 0u) ? 8u : page->block_capacity * 2u;
        if (next <= page->block_capacity) {
            return ROBO_OUT_OF_MEMORY;
        }
        grown = (robo_demo_block_t *)robo_realloc(
            page->blocks, next * sizeof(robo_demo_block_t));
        if (!grown) {
            return ROBO_OUT_OF_MEMORY;
        }
        page->blocks = grown;
        page->block_capacity = next;
    }

    copy = (char *)robo_malloc(length + 1u);
    if (!copy) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (length > 0u) {
        memcpy(copy, text, length);
    }
    copy[length] = '\0';

    page->blocks[page->block_count].style = style;
    page->blocks[page->block_count].text = copy;
    page->blocks[page->block_count].length = length;
    page->blocks[page->block_count].text_scale = scale;
    page->blocks[page->block_count].rect = robo_gfx_rect_empty();
    page->block_count++;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Page from the RoboHTML document tree (Phase 8.3)                    */
/* ------------------------------------------------------------------ */
/*
 * The page model is now built by WALKING the real document tree the 8.3
 * tree builder produced, instead of flattening a token stream:
 *
 *   HTML source
 *      → RoboHTML source boundary (8.1)
 *      → RoboHTML tokenizer (8.2)
 *      → RoboHTML tree builder (8.3)      ← real nodes, real ownership
 *      → document tree
 *      → this walk (supported subset → block list)
 *      → layout → RoboGraphics → RoboWindow
 *
 * The painted subset is unchanged (h1, p, button plus the <title> window
 * caption), so the picture is identical while the structure beneath it
 * became real. Nesting, sibling order, and text now come from the tree, so
 * markup the old flattening had to approximate (overlapping or nested
 * elements) is simply correct. Phases 9/10/11 replace the walk itself.
 */

/** Text copied out of one block (demo-scope, not a real limit). */
#define DEMO_BLOCK_TEXT_MAX 1024u

/** Recursion bound for the tree walk. */
#define DEMO_WALK_DEPTH_MAX 48u

/** Painted style for an element name; 0 when the subset does not paint it. */
static robo_demo_block_style_t style_for_element(const char *name)
{
    if (strcmp(name, "h1") == 0) {
        return ROBO_DEMO_BLOCK_HEADING;
    }
    if (strcmp(name, "p") == 0) {
        return ROBO_DEMO_BLOCK_PARAGRAPH;
    }
    if (strcmp(name, "button") == 0) {
        return ROBO_DEMO_BLOCK_BUTTON;
    }
    if (strcmp(name, "title") == 0) {
        return ROBO_DEMO_BLOCK_DOCUMENT_TITLE;
    }
    return (robo_demo_block_style_t)0;
}

static int scale_for_style(robo_demo_block_style_t style)
{
    switch (style) {
    case ROBO_DEMO_BLOCK_HEADING:
        return DEMO_HEADING_SCALE;
    case ROBO_DEMO_BLOCK_BUTTON:
        return DEMO_BUTTON_LABEL_SCALE;
    case ROBO_DEMO_BLOCK_PARAGRAPH:
    case ROBO_DEMO_BLOCK_DOCUMENT_TITLE:
    default:
        return DEMO_BODY_SCALE;
    }
}

/** Collapsed text of @p node's subtree (text nodes, document order). */
static void block_text_of(const robo_html_node_handle_t *node,
                          char *out,
                          size_t cap)
{
    char raw[DEMO_BLOCK_TEXT_MAX];
    size_t length = 0u;
    size_t usable;

    out[0] = '\0';
    if (robo_html_node_subtree_text(node, raw, sizeof(raw), &length, NULL) !=
        ROBO_OK) {
        return;
    }
    usable = (length < sizeof(raw)) ? length : sizeof(raw) - 1u;
    collapse_ws(raw, usable, out, cap);
}

/**
 * Depth-first walk of the built tree in document order.
 * @p depth bounds the recursion; the node registry already bounds the tree.
 */
static robo_result_t walk_subtree(robo_demo_page_t *page,
                                  const robo_html_node_handle_t *node,
                                  unsigned int depth)
{
    size_t children = 0u;
    size_t i;

    if (depth > (unsigned int)DEMO_WALK_DEPTH_MAX) {
        return ROBO_OK;
    }
    if (robo_html_node_child_count(node, &children, NULL) != ROBO_OK) {
        return ROBO_OK;
    }
    for (i = 0u; i < children; i++) {
        robo_html_node_handle_t child;
        robo_html_node_view_t view;
        robo_demo_block_style_t style;

        if (robo_html_node_child_at(node, i, &child, NULL) != ROBO_OK) {
            break;
        }
        if (robo_html_node_get_view(&child, &view, NULL) != ROBO_OK) {
            continue;
        }
        if (view.kind != ROBO_HTML_NODE_KIND_ELEMENT) {
            continue; /* text/comments/doctypes are painted via their element */
        }
        style = style_for_element(view.name);
        if (style == (robo_demo_block_style_t)0) {
            /* html/head/body are recognised structure; the rest is not in
             * the subset and is counted so tests can assert it. */
            if (strcmp(view.name, "html") == 0 ||
                strcmp(view.name, "head") == 0 ||
                strcmp(view.name, "body") == 0) {
                page->supported_elements++;
            } else {
                page->ignored_tags++;
            }
        } else {
            char text[DEMO_BLOCK_TEXT_MAX];

            page->supported_elements++;
            block_text_of(&child, text, sizeof(text));
            if (text[0] != '\0') {
                if (style == ROBO_DEMO_BLOCK_DOCUMENT_TITLE) {
                    size_t n = strlen(text);

                    if (n >= sizeof(page->title)) {
                        n = sizeof(page->title) - 1u;
                    }
                    memcpy(page->title, text, n);
                    page->title[n] = '\0';
                } else {
                    robo_result_t r;

                    if (style == ROBO_DEMO_BLOCK_BUTTON) {
                        page->has_button = 1;
                    }
                    r = page_add_block(page, style, text, strlen(text),
                                       scale_for_style(style));
                    if (r != ROBO_OK) {
                        return r;
                    }
                }
            }
        }
        /* Unsupported elements still get walked: supported markup nested
         * inside them is painted rather than dropped. */
        {
            robo_result_t sub = walk_subtree(page, &child, depth + 1u);

            if (sub != ROBO_OK) {
                return sub;
            }
        }
    }
    return ROBO_OK;
}

/**
 * Build the page model from @p src through the real 8.3 tree builder.
 * The temporary document lives only for the duration of the call: the page
 * copies the text it keeps, exactly as before.
 */
static robo_result_t build_page_from_tree(robo_demo_page_t *page,
                                          const robo_html_source_t *src)
{
    robo_html_document_id_t doc = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_tree_builder_t *builder = NULL;
    robo_html_node_handle_t root;
    robo_result_t r;
    size_t i;

    r = robo_html_document_create(&doc, NULL);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_html_tree_builder_create(doc, &builder, NULL);
    if (r == ROBO_OK) {
        r = robo_html_tree_builder_build(builder, src, NULL);
    }
    if (r == ROBO_OK) {
        root = robo_html_document_root(doc);
        r = walk_subtree(page, &root, 0u);
    }
    if (r == ROBO_OK) {
        /* Malformed markup is recovered by the builder; keep its report so
         * the demo can show that recovery happened rather than hiding it. */
        page->diag_count = robo_html_tree_builder_diagnostic_count(builder);
        if (page->diag_count > DEMO_MAX_DIAGNOSTICS) {
            page->diag_count = DEMO_MAX_DIAGNOSTICS;
        }
        for (i = 0u; i < page->diag_count; i++) {
            robo_html_diag_t got;

            if (robo_html_tree_builder_get_diagnostic(builder, i, &got) ==
                ROBO_OK) {
                page->diags[i] = got;
            }
        }
    }
    robo_html_tree_builder_destroy(builder);
    (void)robo_html_document_destroy(doc, NULL);
    return r;
}

robo_result_t robo_demo_page_create_from_source(const robo_html_source_t *src,
                                                robo_demo_page_t **out,
                                                robo_html_diag_t *diag)
{
    robo_demo_page_t *page;
    robo_result_t r;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_INVALID_ARGUMENT, "out page is NULL");
    }
    *out = NULL;
    if (!src) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT, "source is NULL");
    }
    if (!robo_html_is_initialized()) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_INIT_ERROR,
                              "RoboHTML module is not initialized");
    }

    page = (robo_demo_page_t *)robo_calloc(1u, sizeof(robo_demo_page_t));
    if (!page) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_OUT_OF_MEMORY, "page allocation failed");
    }
    page->title[0] = '\0';

    r = build_page_from_tree(page, src);
    if (r != ROBO_OK) {
        robo_demo_page_destroy(page);
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, r,
                              "tree build failed while building the demo page");
    }

    if (diag) {
        robo_html_diag_reset(diag);
    }
    *out = page;
    return ROBO_OK;
}

robo_result_t robo_demo_page_create(const char *html,
                                    size_t length,
                                    robo_demo_page_t **out,
                                    robo_html_diag_t *diag)
{
    robo_html_source_t src;
    robo_result_t r;

    if (!html && length > 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT,
                              "html bytes are NULL with non-zero length");
    }
    if (length == 0u) {
        /* Explicit empty input is valid: an empty page, not an error. */
        (void)robo_html_source_empty(&src, NULL);
        return robo_demo_page_create_from_source(&src, out, diag);
    }
    r = robo_html_source_memory(&src, (const uint8_t *)html, length, NULL);
    if (r != ROBO_OK) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE, r,
                              "could not describe the HTML source");
    }
    return robo_demo_page_create_from_source(&src, out, diag);
}

void robo_demo_page_destroy(robo_demo_page_t *page)
{
    size_t i;

    if (!page) {
        return;
    }
    for (i = 0u; i < page->block_count; i++) {
        if (page->blocks[i].text) {
            robo_free(page->blocks[i].text);
        }
    }
    if (page->blocks) {
        robo_free(page->blocks);
    }
    robo_free(page);
}

const char *robo_demo_page_title(const robo_demo_page_t *page)
{
    return page ? page->title : "";
}

size_t robo_demo_page_block_count(const robo_demo_page_t *page)
{
    return page ? page->block_count : 0u;
}

robo_result_t robo_demo_page_block_style(const robo_demo_page_t *page,
                                         size_t index,
                                         robo_demo_block_style_t *out)
{
    if (!page || !out || index >= page->block_count) {
        return ROBO_NOT_FOUND;
    }
    *out = page->blocks[index].style;
    return ROBO_OK;
}

const char *robo_demo_page_block_text(const robo_demo_page_t *page,
                                      size_t index)
{
    if (!page || index >= page->block_count) {
        return "";
    }
    return page->blocks[index].text ? page->blocks[index].text : "";
}

size_t robo_demo_page_supported_element_count(const robo_demo_page_t *page)
{
    return page ? page->supported_elements : 0u;
}

size_t robo_demo_page_ignored_tag_count(const robo_demo_page_t *page)
{
    return page ? page->ignored_tags : 0u;
}

size_t robo_demo_page_diagnostic_count(const robo_demo_page_t *page)
{
    return page ? page->diag_count : 0u;
}

robo_result_t robo_demo_page_get_diagnostic(const robo_demo_page_t *page,
                                            size_t index,
                                            robo_html_diag_t *out)
{
    if (!page || !out || index >= page->diag_count) {
        return ROBO_NOT_FOUND;
    }
    *out = page->diags[index];
    return ROBO_OK;
}

int robo_demo_page_has_button(const robo_demo_page_t *page)
{
    return (page && page->has_button) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

robo_result_t robo_demo_page_layout(robo_demo_page_t *page,
                                    int32_t width,
                                    int32_t height)
{
    int32_t y;
    size_t i;

    if (!page || width <= 0 || height <= 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (page->layout_width == width && page->layout_height == height) {
        return ROBO_OK; /* already laid out for this target */
    }

    page->layout_width = width;
    page->layout_height = height;
    page->button_rect = robo_gfx_rect_empty();
    y = DEMO_PAD;

    for (i = 0u; i < page->block_count; i++) {
        robo_demo_block_t *b = &page->blocks[i];
        robo_gfx_size_t ink = robo_demo_font_measure(b->text, b->length,
                                                     b->text_scale);

        switch (b->style) {
        case ROBO_DEMO_BLOCK_HEADING:
            y += 0;
            b->rect = robo_gfx_rect(DEMO_PAD, y, ink.width, ink.height);
            y += ink.height + DEMO_HEADING_GAP;
            break;
        case ROBO_DEMO_BLOCK_PARAGRAPH:
            b->rect = robo_gfx_rect(DEMO_PAD, y, ink.width, ink.height);
            y += ink.height + DEMO_PARAGRAPH_GAP;
            break;
        case ROBO_DEMO_BLOCK_BUTTON: {
            int32_t bw = ink.width + DEMO_BUTTON_PAD_X * 2;
            y += DEMO_BUTTON_GAP;
            if (bw < DEMO_BUTTON_MIN_WIDTH) {
                bw = DEMO_BUTTON_MIN_WIDTH;
            }
            b->rect = robo_gfx_rect(DEMO_PAD, y, bw, DEMO_BUTTON_HEIGHT);
            page->button_rect = b->rect;
            y += DEMO_BUTTON_HEIGHT;
            break;
        }
        default:
            b->rect = robo_gfx_rect_empty();
            break;
        }
    }
    return ROBO_OK;
}

robo_gfx_size_t robo_demo_page_layout_size(const robo_demo_page_t *page)
{
    robo_gfx_size_t size;
    size.width = page ? page->layout_width : 0;
    size.height = page ? page->layout_height : 0;
    return size;
}

int robo_demo_page_block_rect(const robo_demo_page_t *page,
                              size_t index,
                              robo_gfx_rect_t *out)
{
    if (!page || !out || index >= page->block_count) {
        return 0;
    }
    *out = page->blocks[index].rect;
    return 1;
}

int robo_demo_page_button_rect(const robo_demo_page_t *page,
                               robo_gfx_rect_t *out)
{
    if (!page || !out || !page->has_button) {
        return 0;
    }
    if (robo_gfx_rect_is_empty(page->button_rect)) {
        return 0;
    }
    *out = page->button_rect;
    return 1;
}

int robo_demo_page_hit_button(const robo_demo_page_t *page, robo_gfx_point_t p)
{
    robo_gfx_rect_t r;

    if (!robo_demo_page_button_rect(page, &r)) {
        return 0;
    }
    return robo_gfx_rect_contains_point(r, p);
}

/* ------------------------------------------------------------------ */
/* Paint                                                              */
/* ------------------------------------------------------------------ */

void robo_demo_paint_options_init(robo_demo_paint_options_t *options)
{
    if (options) {
        options->button_state = ROBO_DEMO_BUTTON_NORMAL;
    }
}

/** One-pixel border drawn as four clipped fills (no stroke primitive yet). */
static robo_result_t paint_border(robo_gfx_surface_t *surface,
                                  robo_gfx_rect_t r,
                                  robo_gfx_color_rgba8_t color,
                                  size_t *rects_painted)
{
    robo_result_t res;

    res = robo_demo_fill_clipped(surface, robo_gfx_rect(r.x, r.y, r.width, 1), color);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_demo_fill_clipped(
        surface, robo_gfx_rect(r.x, r.y + r.height - 1, r.width, 1), color);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_demo_fill_clipped(surface, robo_gfx_rect(r.x, r.y, 1, r.height), color);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_demo_fill_clipped(
        surface, robo_gfx_rect(r.x + r.width - 1, r.y, 1, r.height), color);
    if (res != ROBO_OK) {
        return res;
    }
    if (rects_painted) {
        *rects_painted += 4u;
    }
    return ROBO_OK;
}

static robo_gfx_color_rgba8_t button_fill(robo_demo_button_state_t state)
{
    switch (state) {
    case ROBO_DEMO_BUTTON_HOVER:
        return C_BRAND_HOVER;
    case ROBO_DEMO_BUTTON_PRESSED:
        return C_BRAND_ACTIVE;
    default:
        return C_BRAND;
    }
}

robo_result_t robo_demo_page_paint(robo_demo_page_t *page,
                                   robo_gfx_surface_t *surface,
                                   const robo_demo_paint_options_t *options,
                                   robo_demo_paint_stats_t *out_stats)
{
    robo_demo_paint_options_t opts;
    robo_demo_paint_stats_t stats;
    robo_result_t r;
    uint32_t w;
    uint32_t h;
    size_t i;

    if (!page || !surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    w = robo_gfx_surface_width(surface);
    h = robo_gfx_surface_height(surface);
    if (w == 0u || h == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (options) {
        opts = *options;
    } else {
        robo_demo_paint_options_init(&opts);
    }
    stats.blocks_painted = 0u;
    stats.rects_painted = 0u;
    stats.lines_painted = 0u;
    stats.text_runs_painted = 0u;

    r = robo_demo_page_layout(page, (int32_t)w, (int32_t)h);
    if (r != ROBO_OK) {
        return r;
    }

    r = robo_gfx_surface_clear(surface, C_BG);
    if (r != ROBO_OK) {
        return r;
    }

    /* Accent strip: one full-width bar, so a presentation failure in the
     * smallest window is still visibly distinguishable from a blank one. */
    r = robo_demo_fill_clipped(surface,
                               robo_gfx_rect(0, 0, (int32_t)w, DEMO_ACCENT_BAR_H),
                               C_BRAND);
    if (r != ROBO_OK) {
        return r;
    }
    stats.rects_painted++;

    for (i = 0u; i < page->block_count; i++) {
        const robo_demo_block_t *b = &page->blocks[i];
        const robo_gfx_rect_t rect = b->rect;

        if (robo_gfx_rect_is_empty(rect)) {
            continue;
        }
        switch (b->style) {
        case ROBO_DEMO_BLOCK_HEADING:
            r = robo_demo_font_draw(surface, rect.x, rect.y, b->text,
                                    b->length, b->text_scale, C_TEXT);
            if (r != ROBO_OK) {
                return r;
            }
            stats.text_runs_painted++;
            /* Hairline rule under the headline: the page's one divider. */
            r = robo_demo_fill_clipped(
                surface,
                robo_gfx_rect(DEMO_PAD, rect.y + rect.height + 8,
                              (int32_t)w - DEMO_PAD * 2, 1),
                C_BORDER);
            if (r != ROBO_OK) {
                return r;
            }
            stats.lines_painted++;
            break;
        case ROBO_DEMO_BLOCK_PARAGRAPH:
            r = robo_demo_font_draw(surface, rect.x, rect.y, b->text,
                                    b->length, b->text_scale, C_MUTED);
            if (r != ROBO_OK) {
                return r;
            }
            stats.text_runs_painted++;
            break;
        case ROBO_DEMO_BLOCK_BUTTON: {
            robo_gfx_size_t ink =
                robo_demo_font_measure(b->text, b->length, b->text_scale);
            int32_t label_x = rect.x + (rect.width - ink.width) / 2;
            int32_t label_y = rect.y + (rect.height - ink.height) / 2;

            if (opts.button_state == ROBO_DEMO_BUTTON_PRESSED) {
                label_y += 1; /* press feedback moves the label down 1px */
            }
            r = robo_demo_fill_clipped(surface, rect,
                                       button_fill(opts.button_state));
            if (r != ROBO_OK) {
                return r;
            }
            stats.rects_painted++;
            r = paint_border(surface, rect, C_BRAND_ACTIVE, &stats.rects_painted);
            if (r != ROBO_OK) {
                return r;
            }
            r = robo_demo_font_draw(surface, label_x, label_y, b->text,
                                    b->length, b->text_scale, C_ON_BRAND);
            if (r != ROBO_OK) {
                return r;
            }
            stats.text_runs_painted++;
            break;
        }
        default:
            break;
        }
        stats.blocks_painted++;
    }

    if (out_stats) {
        *out_stats = stats;
    }
    return ROBO_OK;
}
