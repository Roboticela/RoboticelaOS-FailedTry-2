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
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
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
 *
 * Phase 8.13 added the two anchors and the `id` they target: one route link
 * the runtime refuses to navigate (no handler, no networking) and one
 * fragment link it really resolves against this document. A checkpoint that
 * only showed link-coloured text would prove nothing about navigation.
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
    "<a href=\"/Settings\">Settings</a>\n"
    "<a href=\"#Overview\">Overview</a>\n"
    "<p id=\"Overview\">RoboHTML links resolve to real elements.</p>\n"
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
    /**
     * Retained image resource for ROBO_DEMO_BLOCK_IMAGE, NULL otherwise
     * (Phase 8.12). The page holds a reference rather than a borrowed
     * pointer so the picture survives the element's attachment being
     * replaced or the document being destroyed after the page was built —
     * which is what lets a page outlive what it was built from.
     */
    robo_html_image_t *image;
    /**
     * Owned `href` bytes for ROBO_DEMO_BLOCK_LINK, NULL otherwise (Phase
     * 8.13). Owned rather than borrowed because a page can outlive the tree
     * it was built from, and a link that forgot its own target would report
     * "no href" for a link that had one.
     */
    char *href;
    /**
     * The anchor this link block came from, so activation can resolve the
     * block back to a real element. Meaningful only while the page's
     * document is live; a page built from markup owns that document (see
     * robo_demo_page_create_from_source).
     */
    robo_html_node_handle_t anchor;
} robo_demo_block_t;

struct robo_demo_page {
    char title[128];
    robo_demo_block_t *blocks;
    size_t block_count;
    size_t block_capacity;
    /**
     * The document the blocks were read from, and whether this page owns it
     * (Phase 8.13). Built from markup → owned; built from a caller's
     * document → borrowed. See the LIFETIME note in RoboDemoHtml.h.
     */
    robo_html_document_id_t document;
    int owns_document;
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
    case ROBO_DEMO_BLOCK_IMAGE:
        return "image";
    case ROBO_DEMO_BLOCK_LINK:
        return "link";
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
    page->blocks[page->block_count].image = NULL;
    page->blocks[page->block_count].href = NULL;
    robo_html_node_handle_clear(&page->blocks[page->block_count].anchor);
    page->block_count++;
    return ROBO_OK;
}

/**
 * Append a link block (Phase 8.13).
 *
 * Separate from page_add_block for the same reason the image block is: it
 * owns something else. A link block owns its `href` copy *and* remembers the
 * anchor it came from, because activation needs an element and not a string
 * — a fragment target only exists inside a document.
 */
static robo_result_t page_add_link_block(robo_demo_page_t *page,
                                         const char *text,
                                         size_t length,
                                         const char *href,
                                         size_t href_length,
                                         const robo_html_node_handle_t *anchor)
{
    robo_demo_block_t *block;
    char *copy = NULL;
    robo_result_t res = page_add_block(page, ROBO_DEMO_BLOCK_LINK, text, length,
                                       DEMO_BODY_SCALE);

    if (res != ROBO_OK) {
        return res;
    }
    if (href && href_length > 0u) {
        copy = (char *)robo_malloc(href_length + 1u);
        if (!copy) {
            /*
             * The block is already appended, so unwinding it is the only way
             * to keep the page consistent with the failure: a link block
             * whose `href` allocation failed would report "no href" for a
             * link that had one.
             */
            robo_free(page->blocks[page->block_count - 1u].text);
            page->block_count--;
            return ROBO_OUT_OF_MEMORY;
        }
        memcpy(copy, href, href_length);
        copy[href_length] = '\0';
    }
    block = &page->blocks[page->block_count - 1u];
    block->href = copy;
    if (anchor) {
        block->anchor = *anchor;
    }
    return ROBO_OK;
}

/**
 * Append an image block that retains @p image (Phase 8.12).
 *
 * Separate from page_add_block because it owns a different thing: a text
 * block owns bytes it copied, an image block owns a reference to a resource
 * it does not control. Releasing the page must release that one reference,
 * and nothing else.
 */
static robo_result_t page_add_image_block(robo_demo_page_t *page,
                                          robo_html_image_t *image)
{
    robo_result_t res = page_add_block(page, ROBO_DEMO_BLOCK_IMAGE, "", 0u,
                                       DEMO_BODY_SCALE);

    if (res != ROBO_OK) {
        return res;
    }
    robo_html_image_retain(image);
    page->blocks[page->block_count - 1u].image = image;
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
 *      → document tree of elements (8.5)  ← namespace, classification
 *      → this walk (supported subset → block list)
 *      → layout → RoboGraphics → RoboWindow
 *
 * Phase 8.5 changed *how* the walk identifies an element, not what it paints:
 * names go through robo_html_element_is_named and roles come from
 * robo_html_element_classification, so the demo carries no string comparison
 * of its own and the decision the real layout engine makes later is already
 * expressed in element-model terms. The painted subset is otherwise
 * unchanged (headings, paragraphs, button, and the <title> window caption),
 * so the picture stays stable. Phases 9/10/11 replace the walk itself.
 */

/** Text copied out of one block (demo-scope, not a real limit). */
#define DEMO_BLOCK_TEXT_MAX 1024u

/** Recursion bound for the tree walk. */
#define DEMO_WALK_DEPTH_MAX 48u

/**
 * Painted style for one element; 0 when the demo's subset does not paint it.
 *
 * Classification answers the role question (a heading is a heading, whether
 * it is <h1> or <h6>), and the tag test picks out the two elements whose
 * role alone is not enough — <title> (metadata the demo shows as the window
 * caption) and <button> (a control painted as a rectangle). Everything else
 * is deliberately unpainted.
 *
 * The tag test is the Phase 8.11 symbolic form: this adapter names the
 * element type it wants and the runtime owns the spelling, so the two of
 * them cannot drift over a string literal.
 */
/**
 * Does @p element have at least one element child?
 *
 * Asked through the child cursor and the element view, so it does not care
 * how children are stored and it treats a text-only element as childless in
 * the sense this adapter means: "this element holds a run of text, not other
 * elements".
 */
static int has_element_child(const robo_html_node_handle_t *element)
{
    robo_html_child_cursor_t cursor;

    if (robo_html_child_cursor_open(&cursor, element, NULL) != ROBO_OK) {
        return 0;
    }
    for (;;) {
        robo_html_node_handle_t child;
        robo_html_element_view_t view;

        if (robo_html_child_cursor_next(&cursor, &child, NULL) != ROBO_OK) {
            return 0;
        }
        if (robo_html_element_view_get(&child, &view, NULL) == ROBO_OK) {
            return 1;
        }
    }
}

static robo_demo_block_style_t demo_style_for_element(
    const robo_html_node_handle_t *element,
    const robo_html_element_view_t *view)
{
    if (view->namespace_id != ROBO_HTML_NAMESPACE_HTML) {
        return (robo_demo_block_style_t)0;
    }
    switch (view->classification) {
    case ROBO_HTML_ELEMENT_CLASS_HEADING:
        return ROBO_DEMO_BLOCK_HEADING;
    case ROBO_HTML_ELEMENT_CLASS_PARAGRAPH:
        return ROBO_DEMO_BLOCK_PARAGRAPH;
    default:
        break;
    }
    if (robo_html_element_is_tag(element, ROBO_HTML_TAG_BUTTON)) {
        return ROBO_DEMO_BLOCK_BUTTON;
    }
    if (robo_html_element_is_tag(element, ROBO_HTML_TAG_TITLE)) {
        return ROBO_DEMO_BLOCK_DOCUMENT_TITLE;
    }
    /*
     * <a> is a block in this adapter even though it is inline markup, and
     * that is a deliberate limit rather than an oversight: this adapter has
     * no inline flow, so a link nested in a <p>/<div> run is painted as part
     * of that run (text and all, since a block's text is its whole subtree)
     * and only a link that is itself block-level here becomes its own
     * hit-testable block. Painting it any other way would mean inventing an
     * inline layout model in the one module that must not grow one.
     */
    if (robo_html_element_is_tag(element, ROBO_HTML_TAG_A)) {
        return ROBO_DEMO_BLOCK_LINK;
    }
    /*
     * A division that holds text *and nothing else* takes the paragraph
     * representation: this adapter has one way to draw a block of text, and
     * <div> is the element that means "a block of content". Without this row
     * the phase's own checkpoint document would silently drop two of its four
     * body elements.
     *
     * The condition matters and is not a heuristic: a block's text here is its
     * whole subtree, so painting a div that contains other blocks would draw
     * those blocks' text a second time, as one duplicated run. A div that
     * contains elements is therefore transparent — the walk descends into it
     * and paints what is inside — which is exactly how the adapter already
     * treats <section>.
     *
     * <span> deliberately has no row. It is an inline element and this demo
     * has no inline flow, so a span wrapped around text inside a block is
     * painted as part of that block's run while a bare span at body level is
     * counted as unsupported markup. That is the adapter's known limit, and
     * the visual suite asserts it rather than leaving it to be discovered.
     */
    if (robo_html_element_is_tag(element, ROBO_HTML_TAG_DIV) &&
        !has_element_child(element)) {
        return ROBO_DEMO_BLOCK_PARAGRAPH;
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
    case ROBO_DEMO_BLOCK_LINK:
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
 *
 * Children come from the Phase 8.8 child cursor rather than child_at, because
 * child_at resolves child 0 and then follows the sibling links index times —
 * iterating a child list that way is O(n^2) over the list, while the cursor
 * carries the next child's id and is O(n). It also means this loop never
 * learns how children are stored, which is what the traversal API is for.
 */
static robo_result_t walk_subtree(robo_demo_page_t *page,
                                  const robo_html_node_handle_t *node,
                                  unsigned int depth)
{
    robo_html_child_cursor_t cursor;

    if (depth > (unsigned int)DEMO_WALK_DEPTH_MAX) {
        return ROBO_OK;
    }
    if (robo_html_child_cursor_open(&cursor, node, NULL) != ROBO_OK) {
        return ROBO_OK;
    }
    for (;;) {
        robo_html_node_handle_t child;
        robo_html_element_view_t view;
        robo_demo_block_style_t style;

        if (robo_html_child_cursor_next(&cursor, &child, NULL) != ROBO_OK) {
            break;
        }
        /*
         * The element model is the filter as well as the classifier: the
         * call fails for text, comment, doctype and any stale handle, which
         * is exactly what this loop wants to skip (text is painted through
         * the element that contains it).
         */
        if (robo_html_element_view_get(&child, &view, NULL) != ROBO_OK) {
            continue;
        }
        /*
         * <img> is handled before the style table because it is the one
         * element whose block comes from a *resource* rather than from its
         * own text: the page copies the decoded picture, not a string. A
         * recognised-but-unresolved image is counted as unsupported markup
         * and paints nothing, so the adapter never draws a picture that was
         * not decoded.
         */
        if (robo_html_element_is_tag(&child, ROBO_HTML_TAG_IMG)) {
            robo_html_image_t *resolved = NULL;

            if (robo_html_element_image_get(&child, &resolved, NULL) ==
                    ROBO_OK &&
                resolved && robo_html_image_graphics(resolved)) {
                robo_result_t r = page_add_image_block(page, resolved);

                page->supported_elements++;
                if (r != ROBO_OK) {
                    return r;
                }
            } else {
                page->ignored_tags++;
            }
            /* A void element: nothing to descend into. */
            continue;
        }
        style = demo_style_for_element(&child, &view);
        if (style == (robo_demo_block_style_t)0) {
            /* html/head/body are recognised structure; the rest is not in
             * the subset and is counted so tests can assert it. */
            if (robo_html_element_is_named(&child, "html") ||
                robo_html_element_is_named(&child, "head") ||
                robo_html_element_is_named(&child, "body")) {
                page->supported_elements++;
            } else {
                page->ignored_tags++;
            }
        } else {
            page->supported_elements++;
        }
        /*
         * The demo's one piece of attribute-driven behaviour, and the reason
         * it consults the attribute model at all: `hidden` means "do not
         * render", so the element is recognised and counted but paints
         * nothing and its subtree is not entered. Nothing in the shipped
         * demo document carries the attribute, so the picture is unchanged —
         * this is the seam a real style engine replaces, not a second style
         * rule.
         */
        if (robo_html_element_has_attribute(&child, "hidden")) {
            continue;
        }
        if (style != (robo_demo_block_style_t)0) {
            char text[DEMO_BLOCK_TEXT_MAX];

            block_text_of(&child, text, sizeof(text));
            if (text[0] != '\0') {
                if (style == ROBO_DEMO_BLOCK_DOCUMENT_TITLE) {
                    size_t n = strlen(text);

                    if (n >= sizeof(page->title)) {
                        n = sizeof(page->title) - 1u;
                    }
                    memcpy(page->title, text, n);
                    page->title[n] = '\0';
                } else if (style == ROBO_DEMO_BLOCK_LINK) {
                    /*
                     * The one block that keeps an attribute as well as its
                     * text: a link without its target is not a link, and the
                     * bytes are copied here because the attribute view
                     * borrows from the document's payload arena. The anchor
                     * handle is kept so activation can reach the real
                     * element — a fragment target only exists in a document.
                     */
                    robo_html_anchor_attributes_t attrs;
                    robo_result_t r;

                    memset(&attrs, 0, sizeof(attrs));
                    (void)robo_html_element_anchor_attributes(&child, &attrs,
                                                              NULL);
                    r = page_add_link_block(page, text, strlen(text),
                                            attrs.href, attrs.href_length,
                                            &child);
                    if (r != ROBO_OK) {
                        return r;
                    }
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
 * Walk @p document_id's tree into @p page.
 *
 * Split out from the parse path (Phase 8.9) so a document that was built and
 * then *mutated* can still be rendered: the page model is a copy, so it can
 * be rebuilt from the same document as many times as the tree changes, and
 * the picture follows the mutation. Nothing here parses or destroys.
 */
static robo_result_t build_page_from_document(robo_demo_page_t *page,
                                              robo_html_document_id_t document_id)
{
    robo_html_node_handle_t root;
    robo_result_t r;

    if (!robo_html_document_is_live(document_id)) {
        return ROBO_HTML_DIAG(NULL, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "document is not live");
    }
    root = robo_html_document_root(document_id);
    if (!robo_html_node_handle_is_wellformed(&root)) {
        return ROBO_HTML_DIAG(NULL, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_ERROR,
                              "document has no usable root");
    }
    r = walk_subtree(page, &root, 0u);
    if (r != ROBO_OK) {
        return r;
    }
    /*
     * Recorded so activation has a document to resolve against. Ownership is
     * the caller's: this function borrows, and build_page_from_tree — which
     * created the document itself — flips the flag once the page is built.
     */
    page->document = document_id;
    page->owns_document = 0;
    return ROBO_OK;
}

/**
 * Build the page model from @p src through the real 8.3 tree builder.
 *
 * The page copies the text it keeps, but it KEEPS THE DOCUMENT (Phase
 * 8.13): the tree is what a link activation resolves against, and a page
 * built from markup has no other owner to borrow from. Ownership transfers
 * to the page only once the walk succeeds; every failure path still destroys
 * the document here, so a failed create leaves nothing behind.
 */
static robo_result_t build_page_from_tree(robo_demo_page_t *page,
                                          const robo_html_source_t *src)
{
    robo_html_document_id_t doc = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_tree_builder_t *builder = NULL;
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
        r = build_page_from_document(page, doc);
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
    if (r != ROBO_OK) {
        (void)robo_html_document_destroy(doc, NULL);
        return r;
    }
    page->owns_document = 1;
    return ROBO_OK;
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

robo_result_t robo_demo_page_create_from_document(
    robo_html_document_id_t document_id,
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

    r = build_page_from_document(page, document_id);
    if (r != ROBO_OK) {
        robo_demo_page_destroy(page);
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, r,
                              "could not read the document tree");
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
        if (page->blocks[i].href) {
            robo_free(page->blocks[i].href);
        }
        /* Each image block holds one reference; releasing it here is the
         * whole of the page's claim on a decoded picture. */
        if (page->blocks[i].image) {
            robo_html_image_release(page->blocks[i].image);
        }
    }
    if (page->blocks) {
        robo_free(page->blocks);
    }
    /*
     * Only a page built from markup owns its document. A page built from a
     * caller's document must not destroy something it does not own, and a
     * failure is ignored rather than reported: the only ways this can fail
     * are a document the caller already destroyed or a module that was shut
     * down first, and neither is something a destructor can report to
     * anyone. The blocks are the page's own either way, so nothing leaks.
     */
    if (page->owns_document) {
        (void)robo_html_document_destroy(page->document, NULL);
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

robo_gfx_image_t *robo_demo_page_block_image(const robo_demo_page_t *page,
                                             size_t index)
{
    if (!page || index >= page->block_count || !page->blocks[index].image) {
        return NULL;
    }
    return robo_html_image_graphics(page->blocks[index].image);
}

const char *robo_demo_page_block_href(const robo_demo_page_t *page,
                                      size_t index)
{
    if (!page || index >= page->block_count || !page->blocks[index].href) {
        return "";
    }
    return page->blocks[index].href;
}

size_t robo_demo_page_link_count(const robo_demo_page_t *page)
{
    size_t i;
    size_t count = 0u;

    if (!page) {
        return 0u;
    }
    for (i = 0u; i < page->block_count; i++) {
        if (page->blocks[i].style == ROBO_DEMO_BLOCK_LINK) {
            count++;
        }
    }
    return count;
}

size_t robo_demo_page_image_count(const robo_demo_page_t *page)
{
    size_t i;
    size_t count = 0u;

    if (!page) {
        return 0u;
    }
    for (i = 0u; i < page->block_count; i++) {
        if (page->blocks[i].style == ROBO_DEMO_BLOCK_IMAGE) {
            count++;
        }
    }
    return count;
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
        case ROBO_DEMO_BLOCK_LINK:
            /* A link occupies exactly the same flow slot as a paragraph: the
             * adapter's link difference is paint and hit-testing, not
             * geometry. Wrapping and inline flow are Phase 10's, and giving a
             * link its own metric here would bake an invented one in. */
            b->rect = robo_gfx_rect(DEMO_PAD, y, ink.width, ink.height);
            y += ink.height + DEMO_PARAGRAPH_GAP;
            break;
        case ROBO_DEMO_BLOCK_IMAGE: {
            uint32_t iw = 0u;
            uint32_t ih = 0u;

            if (!b->image ||
                !robo_html_image_intrinsic_size(b->image, &iw, &ih)) {
                b->rect = robo_gfx_rect_empty();
                break;
            }
            /* One image pixel is one surface pixel: this adapter has no
             * scaling, and inventing one here would be layout. */
            b->rect = robo_gfx_rect(DEMO_PAD, y, (int32_t)iw, (int32_t)ih);
            y += (int32_t)ih + DEMO_PARAGRAPH_GAP;
            break;
        }
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

int robo_demo_page_hit_link(const robo_demo_page_t *page,
                            robo_gfx_point_t p,
                            size_t *out_index)
{
    size_t i;

    if (!page) {
        return 0;
    }
    for (i = 0u; i < page->block_count; i++) {
        if (page->blocks[i].style != ROBO_DEMO_BLOCK_LINK) {
            continue;
        }
        if (robo_gfx_rect_is_empty(page->blocks[i].rect)) {
            continue;
        }
        if (!robo_gfx_rect_contains_point(page->blocks[i].rect, p)) {
            continue;
        }
        if (out_index) {
            *out_index = i;
        }
        return 1;
    }
    return 0;
}

robo_result_t robo_demo_page_activate_link(
    robo_demo_page_t *page,
    size_t index,
    const robo_html_navigation_handler_t *handler,
    robo_html_navigation_outcome_t *out,
    robo_html_diag_t *diag)
{
    if (!page || index >= page->block_count) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_INVALID_ARGUMENT,
                              "link block index out of range");
    }
    if (page->blocks[index].style != ROBO_DEMO_BLOCK_LINK) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_NOT_SUPPORTED,
                              "block is not a link");
    }
    if (!robo_html_document_is_live(page->document)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "the document this page was built from is gone");
    }
    /*
     * No fake fallback for a page that lost its document: activation of a
     * fragment needs the tree, and a route request needs an anchor handle
     * that still resolves. Re-resolving the stored `href` string here would
     * build a request the module would then have to invent an anchor for,
     * which is exactly the "pretend" this phase refuses to do.
     */
    return robo_html_element_activate(&page->blocks[index].anchor, handler, out,
                                      diag);
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
        case ROBO_DEMO_BLOCK_IMAGE: {
            robo_gfx_image_t *picture =
                b->image ? robo_html_image_graphics(b->image) : NULL;
            robo_gfx_surface_t *source_surface;
            robo_gfx_rect_t visible;

            if (!picture) {
                break; /* nothing decoded behind this block */
            }
            source_surface = robo_gfx_image_surface(picture);
            if (!source_surface) {
                break;
            }
            /*
             * Clip by intersecting, then offset the source rect by however
             * far the visible part starts inside the picture. One copy_rect
             * call does the whole draw, and because the copy is scoped to
             * the intersection there is no path that reads or writes outside
             * either surface.
             */
            if (robo_gfx_rect_clip(rect, robo_gfx_rect(0, 0, (int32_t)w,
                                                       (int32_t)h),
                                   &visible) != ROBO_OK) {
                return ROBO_ERROR;
            }
            if (robo_gfx_rect_is_empty(visible)) {
                break;
            }
            r = robo_gfx_surface_copy_rect(
                source_surface,
                robo_gfx_rect(visible.x - rect.x, visible.y - rect.y,
                              visible.width, visible.height),
                surface, robo_gfx_point(visible.x, visible.y));
            if (r != ROBO_OK) {
                return r;
            }
            stats.rects_painted++;
            break;
        }
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
        case ROBO_DEMO_BLOCK_LINK: {
            /* Brand colour plus a rule under the run. Temporary adapter
             * styling so a link is distinguishable in the picture, not a
             * style engine — the phase says so in as many words. */
            r = robo_demo_font_draw(surface, rect.x, rect.y, b->text,
                                    b->length, b->text_scale, C_BRAND);
            if (r != ROBO_OK) {
                return r;
            }
            stats.text_runs_painted++;
            r = robo_demo_fill_clipped(
                surface, robo_gfx_rect(rect.x, rect.y + rect.height + 1,
                                       rect.width, 1),
                C_BRAND);
            if (r != ROBO_OK) {
                return r;
            }
            stats.lines_painted++;
            break;
        }
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
