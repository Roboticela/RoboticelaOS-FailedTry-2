/*
 * RoboDemo HTML suite — visual checkpoint verification.
 *
 * These tests are the reason the checkpoint can be trusted: they drive the
 * real RoboHTML tokenizer and the real RoboGraphics primitives, and assert
 * the resulting picture at pixel level. Everything here is deterministic and
 * headless, so it runs in CI without a desktop and without QEMU.
 *
 * Scope reminder: the demo adapter is throwaway code (see
 * demo/docs/VisualCheckpoint.md). Its tests exist to protect the *real*
 * subsystems it exercises — a regression in the 8.2 tokenizer or in
 * RoboGraphics surface/pixel behavior shows up here as a wrong pixel.
 */

#include "RoboBase/RoboBase.h"
#include "RoboDemo/RoboDemoFont.h"
#include "RoboDemo/RoboDemoHtml.h"
#include "RoboDemo/RoboDemoVersion.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboGraphics/RoboGraphicsScreenshot.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLAnchor.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ROBO_DEMO_ASSET_PATH_FROM_BUILD)
#include "robo_demo_asset_path.h"
#endif

/* Palette mirrors the painter (AGENTS.md light tokens). */
static const robo_gfx_color_rgba8_t T_BG = {245, 245, 247, 255};
static const robo_gfx_color_rgba8_t T_BRAND = {14, 122, 255, 255};
static const robo_gfx_color_rgba8_t T_BRAND_HOVER = {10, 106, 232, 255};
static const robo_gfx_color_rgba8_t T_BRAND_ACTIVE = {7, 87, 199, 255};
static const robo_gfx_color_rgba8_t T_TEXT = {29, 29, 31, 255};
static const robo_gfx_color_rgba8_t T_MUTED = {110, 110, 115, 255};
static const robo_gfx_color_rgba8_t T_SURFACE = {255, 255, 255, 255};
static const robo_gfx_color_rgba8_t T_BORDER = {210, 210, 215, 255};

/* ------------------------------------------------------------------ */
/* Fixtures                                                           */
/* ------------------------------------------------------------------ */

static void demo_fixture(void)
{
    robo_result_t r;

    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_graphics_init(), ROBO_OK);
    r = robo_html_init();
    ROBO_CHECK(r == ROBO_OK || r == ROBO_ALREADY_EXISTS);
}

static robo_gfx_surface_t *make_surface(int32_t w, int32_t h)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = (uint32_t)w;
    desc.height = (uint32_t)h;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    if (robo_gfx_surface_create(&desc, &surface) != ROBO_OK) {
        return NULL;
    }
    return surface;
}

static robo_demo_page_t *page_from(const char *html, robo_result_t *out_result)
{
    robo_demo_page_t *page = NULL;
    robo_result_t r;

    if (!html) {
        html = "";
    }
    r = robo_demo_page_create(html, strlen(html), &page, NULL);
    if (out_result) {
        *out_result = r;
    }
    return (r == ROBO_OK) ? page : NULL;
}

static int rgb_at(const robo_gfx_surface_t *surface,
                  uint32_t x,
                  uint32_t y,
                  uint8_t *r,
                  uint8_t *g,
                  uint8_t *b)
{
    robo_gfx_color_rgba8_t c;

    if (robo_gfx_surface_get_pixel(surface, x, y, &c) != ROBO_OK) {
        return 0;
    }
    *r = c.r;
    *g = c.g;
    *b = c.b;
    return 1;
}

static int pixel_is(const robo_gfx_surface_t *surface,
                    uint32_t x,
                    uint32_t y,
                    robo_gfx_color_rgba8_t expect)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (!rgb_at(surface, x, y, &r, &g, &b)) {
        return 0;
    }
    return r == expect.r && g == expect.g && b == expect.b;
}

/** Non-zero when @p rect contains any pixel different from @p backdrop. */
static int rect_has_ink(const robo_gfx_surface_t *surface,
                        robo_gfx_rect_t rect,
                        robo_gfx_color_rgba8_t backdrop)
{
    int32_t x;
    int32_t y;

    for (y = rect.y; y < rect.y + rect.height; y++) {
        for (x = rect.x; x < rect.x + rect.width; x++) {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            if (x < 0 || y < 0) {
                continue;
            }
            if (!rgb_at(surface, (uint32_t)x, (uint32_t)y, &r, &g, &b)) {
                continue;
            }
            if (r != backdrop.r || g != backdrop.g || b != backdrop.b) {
                return 1;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 1. Module identity                                                  */
/* ------------------------------------------------------------------ */

static void test_demo_version(void)
{
    ROBO_CHECK_STR_EQ(ROBO_DEMO_VERSION_STRING, "0.1.0");
    ROBO_CHECK_EQ_INT(ROBO_DEMO_VERSION_MAJOR, 0);
    ROBO_CHECK_EQ_INT(ROBO_DEMO_VERSION_MINOR, 1);
    ROBO_CHECK_EQ_INT(ROBO_DEMO_VERSION_PATCH, 0);

    ROBO_CHECK_STR_EQ(robo_demo_block_style_name(ROBO_DEMO_BLOCK_HEADING),
                      "heading");
    ROBO_CHECK_STR_EQ(robo_demo_block_style_name(ROBO_DEMO_BLOCK_PARAGRAPH),
                      "paragraph");
    ROBO_CHECK_STR_EQ(robo_demo_block_style_name(ROBO_DEMO_BLOCK_BUTTON),
                      "button");
    ROBO_CHECK_STR_EQ(
        robo_demo_block_style_name(ROBO_DEMO_BLOCK_DOCUMENT_TITLE), "title");
    ROBO_CHECK_STR_EQ(robo_demo_block_style_name((robo_demo_block_style_t)99),
                      "unknown");
}

/* ------------------------------------------------------------------ */
/* 2. Built-in document → page model                                   */
/* ------------------------------------------------------------------ */

static void test_demo_default_source(void)
{
    robo_demo_page_t *page;
    robo_demo_block_style_t style;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    ROBO_CHECK(page != NULL);
    if (!page) {
        return;
    }

    /* <title> becomes the window title, and html/head/body are structural
     * only, so the visible elements become exactly these blocks. */
    ROBO_CHECK_STR_EQ(robo_demo_page_title(page), "RoboticelaOS Runtime");
    /*
     * Six blocks above the form, then the Phase 8.14 form: seven `<label>`
     * captions, five `<input>`s, the `<textarea>` and the `<select>` —
     * fourteen control blocks — and the two `<button>`s (the form's submit
     * and the plain one below it).
     */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(page), 22);
    ROBO_CHECK_EQ_INT(robo_demo_page_field_count(page), 14);
    ROBO_CHECK_EQ_INT(robo_demo_page_supported_element_count(page), 29);
    ROBO_CHECK_EQ_INT(robo_demo_page_ignored_tag_count(page), 0);
    ROBO_CHECK_EQ_INT(robo_demo_page_diagnostic_count(page), 0);
    ROBO_CHECK_EQ_INT(robo_demo_page_has_button(page), 1);
    ROBO_CHECK_EQ_INT(robo_demo_page_link_count(page), 2);

    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 0, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_HEADING);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 0),
                      "RoboticelaOS Runtime");

    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 1, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_PARAGRAPH);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 1),
                      "Independent HTML rendering is working.");

    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 2, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_PARAGRAPH);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 2),
                      "RoboGraphics + RoboWindow + RoboHTML");

    /*
     * The two links (Phase 8.13): a block each, because they are only
     * interesting through their `href`. The `href` bytes are kept verbatim
     * and unresolved, and the paragraph that follows carries the `id` the
     * fragment link names.
     */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 3, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_LINK);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 3), "Settings");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_href(page, 3), "/Settings");

    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 4, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_LINK);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 4), "Overview");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_href(page, 4), "#Overview");

    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 5, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_PARAGRAPH);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 5),
                      "RoboHTML links resolve to real elements.");

    /*
     * The form's caption/control pairs, alternating, exactly as the document
     * spells them. Asserting the *pairs* rather than only the count is what
     * makes this a check on the walk: a caption that swallowed its control,
     * or a control that lost its caption, would still leave fourteen blocks.
     */
    {
        static const char *const k_captions[] = {
            "Display name", "Passphrase", "Build revision", "Platform notes",
            "Release ring", "Notes",          "Country",
        };
        size_t c;

        for (c = 0u; c < 7u; c++) {
            size_t at = 6u + c * 2u;

            ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, at, &style),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_FIELD);
            ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, at),
                              k_captions[c]);
            ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, at + 1u, &style),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_FIELD);
        }
    }

    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 20, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_BUTTON);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 20), "Register");

    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 21, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_BUTTON);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 21), "Demo Button");

    /* Out-of-range access is a clean NOT_FOUND, never garbage. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 22, &style),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 22), "");
    /* A non-link block has no href, and says so rather than inventing one. */
    ROBO_CHECK_STR_EQ(robo_demo_page_block_href(page, 0), "");

    robo_demo_page_destroy(page);
}

static void test_demo_asset_matches_embedded_source(void)
{
#if defined(ROBO_DEMO_ASSET_PATH_FROM_BUILD)
    static const char *path = ROBO_DEMO_ASSET_PATH;
    FILE *f;
    char *file_text;
    long size;
    size_t i;
    size_t trimmed;
    size_t elen;
    char *embed;
    int same;

    f = fopen(path, "rb");
    /* The asset is the editable copy; if it cannot be read the guard is
     * worthless, so this is a failure rather than a skip. */
    ROBO_CHECK_MSG(f != NULL, "demo/assets/Demo.html is unreadable");
    if (!f) {
        return;
    }
    ROBO_CHECK_EQ_INT(fseek(f, 0, SEEK_END), 0);
    size = ftell(f);
    ROBO_CHECK(size > 0);
    if (size <= 0) {
        fclose(f);
        return;
    }
    ROBO_CHECK_EQ_INT(fseek(f, 0, SEEK_SET), 0);
    file_text = (char *)malloc((size_t)size + 1u);
    ROBO_CHECK(file_text != NULL);
    if (!file_text) {
        fclose(f);
        return;
    }
    ROBO_CHECK_EQ_INT((long)fread(file_text, 1u, (size_t)size, f), size);
    fclose(f);
    file_text[size] = '\0';

    /* Normalize CRLF so a Windows checkout does not fail the comparison. */
    {
        size_t out = 0u;
        for (i = 0u; file_text[i] != '\0'; i++) {
            if (file_text[i] == '\r' && file_text[i + 1u] == '\n') {
                continue;
            }
            file_text[out++] = file_text[i];
        }
        file_text[out] = '\0';
        trimmed = out;
    }
    while (trimmed > 0u &&
           (file_text[trimmed - 1u] == '\n' || file_text[trimmed - 1u] == ' ')) {
        file_text[--trimmed] = '\0';
    }

    /* A trailing newline is a formatting detail, not a document difference:
     * trim the embedded copy the same way before comparing. */
    elen = robo_demo_default_html_length();
    embed = (char *)malloc(elen + 1u);
    ROBO_CHECK(embed != NULL);
    if (!embed) {
        free(file_text);
        return;
    }
    memcpy(embed, robo_demo_default_html(), elen + 1u);
    while (elen > 0u && (embed[elen - 1u] == '\n' || embed[elen - 1u] == ' ')) {
        embed[--elen] = '\0';
    }

    same = strcmp(file_text, embed) == 0;
    if (!same) {
        printf("demo asset drift:\n--- file ---\n%s\n--- embedded ---\n%s\n",
               file_text, embed);
    }
    ROBO_CHECK_MSG(same, "demo/assets/Demo.html drifted from the embedded "
                         "document");
    free(embed);
    free(file_text);
#else
    /* Without the build-provided path the guard cannot run; the embedded
     * document is still exercised by every other test in this suite. */
    ROBO_CHECK(robo_demo_default_html_length() > 0u);
#endif
}

/* ------------------------------------------------------------------ */
/* 3. Creation contract                                                */
/* ------------------------------------------------------------------ */

static void test_demo_create_invalid_args(void)
{
    robo_demo_page_t *page = NULL;
    robo_html_diag_t diag;
    robo_result_t r;

    demo_fixture();

    ROBO_CHECK_EQ_INT(robo_demo_page_create("x", 1u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_demo_page_create(NULL, 8u, &page, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(page == NULL);

    robo_html_diag_reset(&diag);
    r = robo_demo_page_create(NULL, 8u, &page, &diag);
    ROBO_CHECK_EQ_INT(r, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_SOURCE);

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_source(NULL, &page, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_source(NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Destroy is NULL-safe and repeatable. */
    robo_demo_page_destroy(NULL);
}

static void test_demo_empty_source(void)
{
    robo_demo_page_t *page = NULL;
    robo_html_source_t src;
    demo_fixture();

    /* Empty input is a valid empty page, not an error. */
    ROBO_CHECK_EQ_INT(robo_demo_page_create(NULL, 0u, &page, NULL), ROBO_OK);
    ROBO_CHECK(page != NULL);
    if (page) {
        ROBO_CHECK_STR_EQ(robo_demo_page_title(page), "");
        ROBO_CHECK_EQ_INT(robo_demo_page_block_count(page), 0);
        /* An empty document still has the implied html/head/body elements. */
        ROBO_CHECK_EQ_INT(robo_demo_page_supported_element_count(page), 3);
        ROBO_CHECK_EQ_INT(robo_demo_page_has_button(page), 0);
        robo_demo_page_destroy(page);
    }

    ROBO_CHECK_EQ_INT(robo_html_source_empty(&src, NULL), ROBO_OK);
    page = NULL;
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_source(&src, &page, NULL),
                      ROBO_OK);
    ROBO_CHECK(page != NULL);
    if (page) {
        ROBO_CHECK_EQ_INT(robo_demo_page_block_count(page), 0);
        ROBO_CHECK_EQ_INT(robo_demo_page_has_button(page), 0);
        robo_demo_page_destroy(page);
    }
}

/* ------------------------------------------------------------------ */
/* 4. Tokenizer + tree-builder integration                             */
/* ------------------------------------------------------------------ */

static void test_demo_character_references(void)
{
    robo_demo_page_t *page;

    demo_fixture();
    /* The 8.2 tokenizer decodes references, so the page sees real text. */
    page = page_from("<h1>Tom &amp; Jerry</h1><p>&#65;&#x42;C</p>", NULL);
    ROBO_CHECK(page != NULL);
    if (!page) {
        return;
    }
    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(page), 2);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 0), "Tom & Jerry");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 1), "ABC");
    robo_demo_page_destroy(page);
}

static void test_demo_unsupported_tags(void)
{
    robo_demo_page_t *page;
    robo_demo_block_style_t style;

    demo_fixture();
    page = page_from("<html><body><div class='x'><h1>Title</h1>"
                     "<section><p>Body</p></section></div>"
                     "<span>dropped</span></body></html>",
                     NULL);
    ROBO_CHECK(page != NULL);
    if (!page) {
        return;
    }
    /* Unsupported markup must not crash, must not swallow supported content,
     * and must be accounted for. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(page), 2);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 0), "Title");
    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 0, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_HEADING);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 1), "Body");
    ROBO_CHECK(robo_demo_page_ignored_tag_count(page) >= 2u);
    ROBO_CHECK_STR_EQ(robo_demo_page_title(page), "");
    robo_demo_page_destroy(page);
}

static void test_demo_malformed_input(void)
{
    static const char *cases[] = {
        "<",
        "</",
        "<h1",
        "<h1>unclosed",
        "<p a=",
        "<p a=\"",
        "<!--",
        "<!DOCTYPE",
        "<h1>&notarealentity;</h1>",
        "<h1>&#xZZ;</h1>",
        "<button><h1>overlap</h1></button>",
        "<h1></h1><p></p>",
        "text before any tag",
        "<!-- comment --><h1>ok</h1>",
        "<h1>a</h1><p>b</p><button>c</button>",
        "\0\0junk",
        "<h1>needs a \xff\xfe byte</h1>"
    };
    size_t i;

    demo_fixture();
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_demo_page_t *page = NULL;
        robo_result_t r;

        r = robo_demo_page_create(cases[i], strlen(cases[i]), &page, NULL);
        if (r != ROBO_OK) {
            printf("malformed case %lu unexpectedly failed (%d)\n",
                   (unsigned long)i, (int)r);
        }
        ROBO_CHECK_EQ_INT(r, ROBO_OK);
        ROBO_CHECK_MSG(page != NULL, "malformed input produced no page");
        robo_demo_page_destroy(page);
    }

    /* Truncation robustness: every prefix of the demo document must build. */
    {
        const char *html = robo_demo_default_html();
        size_t total = robo_demo_default_html_length();
        size_t n;
        for (n = 0u; n <= total; n++) {
            robo_demo_page_t *page = NULL;
            robo_result_t r = robo_demo_page_create(html, n, &page, NULL);
            if (r != ROBO_OK) {
                printf("prefix %lu of %lu failed (%d)\n", (unsigned long)n,
                       (unsigned long)total, (int)r);
                ROBO_CHECK_EQ_INT(r, ROBO_OK);
                break;
            }
            robo_demo_page_destroy(page);
        }
    }
}

static void test_demo_text_ownership(void)
{
    robo_demo_page_t *page;
    char *scratch;
    static const char *html = "<h1>Owned Copy</h1>";

    demo_fixture();
    scratch = (char *)malloc(strlen(html) + 1u);
    ROBO_CHECK(scratch != NULL);
    if (!scratch) {
        return;
    }
    strcpy(scratch, html);
    ROBO_CHECK_EQ_INT(robo_demo_page_create(scratch, strlen(scratch), &page,
                                            NULL),
                      ROBO_OK);
    /* The page must own its text: scribbling over (and freeing) the caller's
     * buffer must not change it. */
    memset(scratch, 'X', strlen(scratch));
    free(scratch);
    ROBO_CHECK(page != NULL);
    if (!page) {
        return;
    }
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 0), "Owned Copy");
    ROBO_CHECK_STR_EQ(robo_demo_page_title(page), "");
    robo_demo_page_destroy(page);

    /* Two pages are fully independent. */
    {
        robo_demo_page_t *a = page_from("<h1>Alpha</h1>", NULL);
        robo_demo_page_t *b = page_from("<h1>Beta</h1>", NULL);
        ROBO_CHECK(a != NULL && b != NULL);
        if (a && b) {
            robo_demo_page_destroy(a);
            ROBO_CHECK_STR_EQ(robo_demo_page_block_text(b, 0), "Beta");
            robo_demo_page_destroy(b);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 5. Layout                                                           */
/* ------------------------------------------------------------------ */

static void test_demo_layout_flow(void)
{
    robo_demo_page_t *page;
    robo_gfx_rect_t r0;
    robo_gfx_rect_t r1;
    robo_gfx_rect_t r2;
    robo_gfx_rect_t r3;
    robo_gfx_size_t size;
    robo_gfx_point_t centre;
    robo_gfx_point_t outside;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    ROBO_CHECK(page != NULL);
    if (!page) {
        return;
    }

    size = robo_demo_page_layout_size(page);
    ROBO_CHECK_EQ_INT(size.width, 0);
    ROBO_CHECK_EQ_INT(size.height, 0);

    ROBO_CHECK_EQ_INT(robo_demo_page_layout(page, 900, 600), ROBO_OK);
    size = robo_demo_page_layout_size(page);
    ROBO_CHECK_EQ_INT(size.width, 900);
    ROBO_CHECK_EQ_INT(size.height, 600);

    ROBO_CHECK(robo_demo_page_block_rect(page, 0, &r0));
    ROBO_CHECK(robo_demo_page_block_rect(page, 1, &r1));
    ROBO_CHECK(robo_demo_page_block_rect(page, 2, &r2));
    /* Block 3 is the first link; the button is the last block. */
    ROBO_CHECK(robo_demo_page_block_rect(page, 21, &r3));
    ROBO_CHECK_EQ_INT(robo_demo_page_block_rect(page, 22, &r3), 0);

    /* Deterministic vertical flow: heading, paragraphs, links, then button. */
    ROBO_CHECK(r0.y < r1.y);
    ROBO_CHECK(r1.y < r2.y);
    ROBO_CHECK(r2.y < r3.y);
    ROBO_CHECK_EQ_INT(r0.x, 32);
    ROBO_CHECK_EQ_INT(r3.width >= 160, 1);
    ROBO_CHECK_EQ_INT(r3.height, 44);
    /* Heading type is 3x the 8px cell plus 1px gap per glyph. */
    ROBO_CHECK_EQ_INT(r0.height, 24);
    ROBO_CHECK_EQ_INT(r1.height, 16);
    ROBO_CHECK_EQ_INT(r2.height, 16);

    /*
     * A link occupies the same flow slot as a paragraph — the adapter's link
     * difference is paint and hit-testing, not geometry — and every block in
     * the document column stays ordered.
     */
    {
        size_t i;
        int32_t prev_y = 0;
        int have_prev = 0;

        for (i = 0u; i < robo_demo_page_block_count(page); i++) {
            robo_gfx_rect_t r;

            ROBO_CHECK(robo_demo_page_block_rect(page, i, &r));
            if (have_prev) {
                ROBO_CHECK(prev_y < r.y);
            }
            prev_y = r.y;
            have_prev = 1;
        }
    }

    centre.x = r3.x + r3.width / 2;
    centre.y = r3.y + r3.height / 2;
    outside.x = r3.x - 1;
    outside.y = r3.y - 1;
    ROBO_CHECK_EQ_INT(robo_demo_page_hit_button(page, centre), 1);
    ROBO_CHECK_EQ_INT(robo_demo_page_hit_button(page, outside), 0);
    /* Half-open: the far edge belongs to the next pixel, not the button. */
    {
        robo_gfx_point_t edge;
        edge.x = r3.x + r3.width;
        edge.y = centre.y;
        ROBO_CHECK_EQ_INT(robo_demo_page_hit_button(page, edge), 0);
    }

    robo_demo_page_destroy(page);
}

static void test_demo_layout_resize(void)
{
    robo_demo_page_t *page;
    robo_gfx_rect_t first;
    robo_gfx_rect_t big;
    robo_gfx_rect_t small;
    robo_gfx_size_t size;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    ROBO_CHECK(page != NULL);
    if (!page) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_demo_page_layout(page, 900, 600), ROBO_OK);
    ROBO_CHECK(robo_demo_page_button_rect(page, &first));

    /* Repeating the same size must not move anything (idempotent). */
    ROBO_CHECK_EQ_INT(robo_demo_page_layout(page, 900, 600), ROBO_OK);
    ROBO_CHECK(robo_demo_page_button_rect(page, &big));
    ROBO_CHECK_EQ_INT(big.x, first.x);
    ROBO_CHECK_EQ_INT(big.y, first.y);
    ROBO_CHECK_EQ_INT(big.width, first.width);

    /* A different target reflows and records the new size. Note the rects are
     * identical: the demo does not wrap text yet — wrapping is Phase 10
     * layout work, and this test pins that known limitation. */
    ROBO_CHECK_EQ_INT(robo_demo_page_layout(page, 480, 320), ROBO_OK);
    size = robo_demo_page_layout_size(page);
    ROBO_CHECK_EQ_INT(size.width, 480);
    ROBO_CHECK_EQ_INT(size.height, 320);
    ROBO_CHECK(robo_demo_page_button_rect(page, &small));
    ROBO_CHECK_EQ_INT(small.x, first.x);
    ROBO_CHECK_EQ_INT(small.y, first.y);

    /* Invalid targets are rejected without disturbing the cached layout. */
    ROBO_CHECK_EQ_INT(robo_demo_page_layout(page, 0, 320),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_demo_page_layout(page, 480, -1),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_demo_page_layout(NULL, 10, 10),
                      ROBO_INVALID_ARGUMENT);
    size = robo_demo_page_layout_size(page);
    ROBO_CHECK_EQ_INT(size.width, 480);

    robo_demo_page_destroy(page);
}

/* ------------------------------------------------------------------ */
/* 6. Painting + pixel validation                                      */
/* ------------------------------------------------------------------ */

static void test_demo_paint_probes(void)
{
    robo_demo_page_t *page;
    robo_gfx_surface_t *surface;
    robo_demo_paint_options_t opts;
    robo_demo_paint_stats_t stats;
    robo_gfx_rect_t button;
    robo_gfx_rect_t heading;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    surface = make_surface(900, 800);
    ROBO_CHECK(page != NULL && surface != NULL);
    if (!page || !surface) {
        robo_demo_page_destroy(page);
        robo_gfx_surface_release(surface);
        return;
    }

    robo_demo_paint_options_init(&opts);
    memset(&stats, 0, sizeof(stats));
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, surface, &opts, &stats),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(stats.blocks_painted, 22);
    ROBO_CHECK_EQ_INT((int)stats.rects_painted, 50);
    /*
     * Six runs above the form, seven captions, three controls carrying text
     * (the text and number inputs and the textarea, whose placeholders and
     * value this adapter draws), the select's selected option, and the two
     * buttons. The password input is deliberately absent from that count: it
     * has no placeholder and no value, so it draws its box and nothing else —
     * which is the honest picture of an empty field.
     */
    ROBO_CHECK_EQ_INT(stats.text_runs_painted, 19);
    ROBO_CHECK(stats.rects_painted >= 6u);
    /* One rule under the headline plus one underline per link. */
    ROBO_CHECK_EQ_INT(stats.lines_painted, 3);

    ROBO_CHECK(robo_demo_page_button_rect(page, &button));
    ROBO_CHECK(robo_demo_page_block_rect(page, 0, &heading));

    /* Accent strip spans the full width at the top. */
    ROBO_CHECK(pixel_is(surface, 450, 2, T_BRAND));
    /* Right/bottom margin stays page background: nothing bleeds out. */
    ROBO_CHECK(pixel_is(surface, 896, 796, T_BG));
    /* Button: filled brand, one-pixel darker border. Sampled three pixels in
     * from the left edge: the label is centred, so this point is always fill
     * regardless of button state. */
    ROBO_CHECK(pixel_is(surface, (uint32_t)(button.x + 8),
                        (uint32_t)(button.y + button.height / 2), T_BRAND));
    ROBO_CHECK(pixel_is(surface, (uint32_t)button.x, (uint32_t)button.y,
                        T_BRAND_ACTIVE));
    /* Text actually rasterised in the heading and paragraph rects. */
    ROBO_CHECK(rect_has_ink(surface, heading, T_BG));
    ROBO_CHECK(rect_has_ink(surface, button, T_BRAND));

    robo_gfx_surface_release(surface);
    robo_demo_page_destroy(page);
}

static void test_demo_paint_button_states(void)
{
    robo_demo_page_t *page;
    robo_gfx_surface_t *surface;
    robo_demo_paint_options_t opts;
    robo_gfx_rect_t button;
    uint32_t cx;
    uint32_t cy;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    surface = make_surface(900, 800);
    ROBO_CHECK(page != NULL && surface != NULL);
    if (!page || !surface) {
        robo_demo_page_destroy(page);
        robo_gfx_surface_release(surface);
        return;
    }

    robo_demo_paint_options_init(&opts);
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, surface, &opts, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_demo_page_button_rect(page, &button));
    /* Clear of the centred label in every state. */
    cx = (uint32_t)(button.x + 8);
    cy = (uint32_t)(button.y + button.height / 2);
    ROBO_CHECK(pixel_is(surface, cx, cy, T_BRAND));

    opts.button_state = ROBO_DEMO_BUTTON_HOVER;
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, surface, &opts, NULL),
                      ROBO_OK);
    ROBO_CHECK(pixel_is(surface, cx, cy, T_BRAND_HOVER));

    opts.button_state = ROBO_DEMO_BUTTON_PRESSED;
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, surface, &opts, NULL),
                      ROBO_OK);
    ROBO_CHECK(pixel_is(surface, cx, cy, T_BRAND_ACTIVE));

    /* NULL options restore the resting state. */
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, surface, NULL, NULL), ROBO_OK);
    ROBO_CHECK(pixel_is(surface, cx, cy, T_BRAND));

    robo_gfx_surface_release(surface);
    robo_demo_page_destroy(page);
}

static void test_demo_paint_determinism(void)
{
    robo_demo_page_t *page;
    robo_gfx_surface_t *a;
    robo_gfx_surface_t *b;
    size_t stride;
    uint32_t y;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    a = make_surface(320, 240);
    b = make_surface(320, 240);
    ROBO_CHECK(page != NULL && a != NULL && b != NULL);
    if (!page || !a || !b) {
        robo_demo_page_destroy(page);
        robo_gfx_surface_release(a);
        robo_gfx_surface_release(b);
        return;
    }

    /* Dirty one surface first: the painter must fully define its output. */
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(a, robo_gfx_rgb8(1, 2, 3)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, a, NULL, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, b, NULL, NULL), ROBO_OK);

    stride = robo_gfx_surface_stride(a);
    ROBO_CHECK_EQ_INT(stride, robo_gfx_surface_stride(b));
    for (y = 0u; y < 240u; y++) {
        const uint8_t *ra = robo_gfx_surface_pixels_const(a) + (size_t)y * stride;
        const uint8_t *rb = robo_gfx_surface_pixels_const(b) + (size_t)y * stride;
        if (memcmp(ra, rb, stride) != 0) {
            ROBO_CHECK_MSG(0, "demo paint is not deterministic");
            break;
        }
    }

    robo_gfx_surface_release(a);
    robo_gfx_surface_release(b);
    robo_demo_page_destroy(page);
}

static void test_demo_paint_clips_small_target(void)
{
    robo_demo_page_t *page;
    robo_gfx_surface_t *surface;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    surface = make_surface(40, 30);
    ROBO_CHECK(page != NULL && surface != NULL);
    if (!page || !surface) {
        robo_demo_page_destroy(page);
        robo_gfx_surface_release(surface);
        return;
    }

    /* Content is far larger than the target: the painter must clip instead of
     * failing (robo_gfx_surface_fill_rect demands an enclosed rect). */
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, surface, NULL, NULL), ROBO_OK);
    ROBO_CHECK(pixel_is(surface, 20, 2, T_BRAND));
    ROBO_CHECK(pixel_is(surface, 39, 29, T_BG));

    robo_gfx_surface_release(surface);
    robo_demo_page_destroy(page);
}

static void test_demo_paint_invalid_args(void)
{
    robo_demo_page_t *page;
    robo_gfx_surface_t *surface;

    demo_fixture();
    page = page_from("<h1>x</h1>", NULL);
    surface = make_surface(64, 64);
    ROBO_CHECK(page != NULL && surface != NULL);
    if (!page || !surface) {
        robo_demo_page_destroy(page);
        robo_gfx_surface_release(surface);
        return;
    }

    ROBO_CHECK_EQ_INT(robo_demo_page_paint(NULL, surface, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(NULL, NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_release(surface);
    robo_demo_page_destroy(page);
}

/* ------------------------------------------------------------------ */
/* 7. Screenshot / framebuffer export                                  */
/* ------------------------------------------------------------------ */

static void test_demo_export_png(void)
{
    static const uint8_t png_sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A,
                                       0x0A};
    robo_demo_page_t *page;
    robo_gfx_surface_t *surface;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_capture_options_t copts;
    robo_gfx_encode_options_t eopts;
    robo_gfx_encoded_buffer_t buf;
    robo_gfx_color_rgba8_t probe;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    surface = make_surface(640, 480);
    ROBO_CHECK(page != NULL && surface != NULL);
    if (!page || !surface) {
        robo_demo_page_destroy(page);
        robo_gfx_surface_release(surface);
        return;
    }
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, surface, NULL, NULL), ROBO_OK);

    robo_gfx_capture_options_init(&copts);
    copts.use_full_extent = 1;
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_capture_surface(surface, &copts, &shot),
                      ROBO_OK);
    ROBO_CHECK(shot != NULL);
    if (shot) {
        ROBO_CHECK_EQ_INT(robo_gfx_screenshot_width(shot), 640);
        ROBO_CHECK_EQ_INT(robo_gfx_screenshot_height(shot), 480);
        /* The capture is an independent copy: the original can be scribbled
         * over without affecting it. */
        ROBO_CHECK_EQ_INT(robo_gfx_surface_clear(surface, robo_gfx_rgb8(0, 0, 0)),
                          ROBO_OK);
        probe.r = 0;
        probe.g = 0;
        probe.b = 0;
        probe.a = 0;
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_get_pixel(robo_gfx_screenshot_surface(shot), 450, 2,
                                       &probe),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(probe.r, T_BRAND.r);
        ROBO_CHECK_EQ_INT(probe.g, T_BRAND.g);
        ROBO_CHECK_EQ_INT(probe.b, T_BRAND.b);

        robo_gfx_encode_options_init(&eopts);
        eopts.container = ROBO_GFX_ENCODE_PNG;
        eopts.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
        buf.data = NULL;
        buf.size = 0u;
        ROBO_CHECK_EQ_INT(robo_gfx_screenshot_export(shot, &eopts, &buf),
                          ROBO_OK);
        ROBO_CHECK(buf.size > 8u);
        ROBO_CHECK_EQ_INT(memcmp(buf.data, png_sig, sizeof(png_sig)), 0);
        robo_gfx_encoded_buffer_release(&buf);
        robo_gfx_screenshot_release(shot);
    }

    robo_gfx_surface_release(surface);
    robo_demo_page_destroy(page);
}

/* ------------------------------------------------------------------ */
/* 8. Lifetime and failure paths                                       */
/* ------------------------------------------------------------------ */

static void test_demo_repeated_lifecycle(void)
{
    robo_gfx_surface_t *surface;
    int i;
    int failed = 0;

    demo_fixture();
    surface = make_surface(200, 150);
    ROBO_CHECK(surface != NULL);
    if (!surface) {
        return;
    }

    /* Repeated create → layout → paint → destroy must not leak, alias, or
     * leave stale state behind. Deliberately alternates documents so a page
     * that forgot to copy its text would show the previous run's strings. */
    for (i = 0; i < 64; i++) {
        robo_demo_page_t *page = page_from(
            (i % 2 == 0) ? "<h1>Even</h1><button>Go</button>"
                         : robo_demo_default_html(),
            NULL);
        if (!page) {
            failed = 1;
            break;
        }
        if (robo_demo_page_paint(page, surface, NULL, NULL) != ROBO_OK) {
            failed = 1;
        }
        if (i % 2 == 0) {
            if (strcmp(robo_demo_page_block_text(page, 0), "Even") != 0) {
                failed = 1;
            }
        } else if (strcmp(robo_demo_page_title(page),
                          "RoboticelaOS Runtime") != 0) {
            failed = 1;
        }
        robo_demo_page_destroy(page);
    }
    ROBO_CHECK_EQ_INT(failed, 0);
    robo_gfx_surface_release(surface);
}

static void test_demo_module_not_initialized(void)
{
    robo_demo_page_t *page = NULL;
    robo_html_diag_t diag;
    robo_result_t r;

    demo_fixture();
    robo_html_shutdown();
    ROBO_CHECK_EQ_INT(robo_html_is_initialized(), 0);

    robo_html_diag_reset(&diag);
    r = robo_demo_page_create(robo_demo_default_html(),
                              robo_demo_default_html_length(), &page, &diag);
    ROBO_CHECK_EQ_INT(r, ROBO_INIT_ERROR);
    ROBO_CHECK(page == NULL);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INIT_ERROR);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_HTML_DOMAIN_DOCUMENT);

    /* Restore for every following test. */
    ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    page = page_from(robo_demo_default_html(), NULL);
    ROBO_CHECK(page != NULL);
    robo_demo_page_destroy(page);
}

static void test_demo_font_helpers(void)
{
    robo_gfx_size_t size;

    demo_fixture();
    ROBO_CHECK_EQ_INT(robo_demo_font_scale_clamp(0), 1);
    ROBO_CHECK_EQ_INT(robo_demo_font_scale_clamp(-4), 1);
    ROBO_CHECK_EQ_INT(robo_demo_font_scale_clamp(3), 3);

    size = robo_demo_font_measure("", 0u, 2);
    ROBO_CHECK_EQ_INT(size.width, 0);
    ROBO_CHECK_EQ_INT(size.height, 0);

    size = robo_demo_font_measure("A", 1u, 1);
    ROBO_CHECK_EQ_INT(size.width, 8);
    ROBO_CHECK_EQ_INT(size.height, 8);

    /* N glyphs advance N*(cell+gap) minus the trailing gap. */
    size = robo_demo_font_measure("ABC", 3u, 1);
    ROBO_CHECK_EQ_INT(size.width, 3 * 9 - 1);
    size = robo_demo_font_measure("ABC", 3u, 2);
    ROBO_CHECK_EQ_INT(size.width, (3 * 9 - 1) * 2);
    ROBO_CHECK_EQ_INT(size.height, 16);

    ROBO_CHECK_EQ_INT(robo_demo_font_measure(NULL, 0u, 1).width, 0);
    ROBO_CHECK_EQ_INT(robo_demo_font_draw(NULL, 0, 0, "A", 1u, 1,
                                          robo_gfx_rgb8(0, 0, 0)),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_demo_fill_clipped(NULL, robo_gfx_rect(0, 0, 1, 1),
                                             robo_gfx_rgb8(0, 0, 0)),
                      ROBO_INVALID_ARGUMENT);
}

/**
 * The demo walk's one attribute-driven rule: `hidden` means "do not render".
 *
 * It is also the proof that the renderer reaches attributes through the real
 * model — `id`/`class` on the demo document are irrelevant to the picture, so
 * a rule with visible consequences is the only way to show the seam works.
 */
static void test_demo_hidden_attribute(void)
{
    static const char *html =
        "<h1>Visible</h1>"
        "<h1 hidden>Hidden Heading</h1>"
        "<p hidden><button>Sneaky</button></p>"
        "<p>Shown Paragraph</p>"
        "<button hidden>Hidden Button</button>";
    robo_demo_page_t *page = NULL;

    demo_fixture();
    ROBO_CHECK_EQ_INT(robo_demo_page_create(html, strlen(html), &page, NULL),
                      ROBO_OK);
    ROBO_CHECK_MSG(page != NULL, "hidden-attribute page did not build");
    if (!page) {
        return;
    }

    /* Two visible blocks only: the hidden heading and paragraph contribute
     * nothing, and neither does the button nested inside the hidden one. */
    ROBO_CHECK_EQ_INT((int)robo_demo_page_block_count(page), 2);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 0u), "Visible");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 1u), "Shown Paragraph");

    /* A hidden button is still a recognised control — it just is not there. */
    ROBO_CHECK_EQ_INT(robo_demo_page_supported_element_count(page) > 0u, 1);
    ROBO_CHECK_EQ_INT(robo_demo_page_has_button(page), 0);

    /* Layout and paint must cope with the missing button. */
    ROBO_CHECK_EQ_INT(robo_demo_page_layout(page, 640, 480), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_demo_page_hit_button(page, robo_gfx_point(0, 0)), 0);

    robo_demo_page_destroy(page);
}

/* ------------------------------------------------------------------ */
/* DOM mutation reaches the picture (Phase 8.9)                        */
/* ------------------------------------------------------------------ */

/** Pixels that differ between two same-sized surfaces. */
static int count_pixel_differences(const robo_gfx_surface_t *a,
                                   const robo_gfx_surface_t *b,
                                   uint32_t width,
                                   uint32_t height)
{
    int diff = 0;
    uint32_t x;
    uint32_t y;

    for (y = 0u; y < height; y++) {
        for (x = 0u; x < width; x++) {
            robo_gfx_color_rgba8_t ca;
            robo_gfx_color_rgba8_t cb;

            if (robo_gfx_surface_get_pixel(a, x, y, &ca) != ROBO_OK ||
                robo_gfx_surface_get_pixel(b, x, y, &cb) != ROBO_OK) {
                continue;
            }
            if (ca.r != cb.r || ca.g != cb.g || ca.b != cb.b ||
                ca.a != cb.a) {
                diff++;
            }
        }
    }
    return diff;
}

/**
 * The mutation-to-picture seam: parse a document, render it, change the real
 * DOM through the real mutation API, render the *same* document again, and
 * show the picture followed.
 *
 * This is what keeps the visual checkpoint honest about being driven by the
 * tree rather than by the source bytes: the adapter is a copy of the tree, so
 * re-running it after the tree changed is the only way it can see a
 * mutation — and no source string is parsed the second time.
 */
static void test_demo_mutation_changes_picture(void)
{
    robo_html_document_id_t doc = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_node_handle_t root;
    robo_html_node_handle_t body;
    robo_html_node_handle_t heading;
    robo_html_node_handle_t para;
    robo_html_source_t src;
    robo_demo_page_t *before = NULL;
    robo_demo_page_t *after = NULL;
    robo_gfx_surface_t *s_before = NULL;
    robo_gfx_surface_t *s_after = NULL;

    demo_fixture();

    ROBO_CHECK_EQ_INT(robo_html_document_create(&doc, NULL), ROBO_OK);
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(
            &src, "<html><body><h1>First</h1><p>Second</p></body></html>",
            NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(doc, &src, NULL, NULL), ROBO_OK);

    root = robo_html_document_root(doc);
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&root, "body", &body, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, 0u, &heading, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_child_at(&body, 1u, &para, NULL),
                      ROBO_OK);

    /* As parsed: the heading comes first. */
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &before, NULL),
                      ROBO_OK);
    ROBO_CHECK(before != NULL);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(before), 2);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(before, 0), "First");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(before, 1), "Second");

    /* The real mutation API moves the paragraph ahead of the heading. */
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&body, &para, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_node_insert_before(&body, &para, &heading, NULL), ROBO_OK);

    /* The same document, re-read: the order changed, the source did not. */
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &after, NULL),
                      ROBO_OK);
    ROBO_CHECK(after != NULL);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(after), 2);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(after, 0), "Second");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(after, 1), "First");
    {
        robo_gfx_rect_t r0;
        robo_gfx_rect_t r1;

        /* Rects are only meaningful once layout has run. */
        ROBO_CHECK_EQ_INT(robo_demo_page_layout(after, 900, 600), ROBO_OK);
        ROBO_CHECK(robo_demo_page_block_rect(after, 0, &r0));
        ROBO_CHECK(robo_demo_page_block_rect(after, 1, &r1));
        ROBO_CHECK(r0.y < r1.y);
    }

    /* And the painted output differs: the mutation is visible, not just
     * modelled. */
    s_before = make_surface(900, 600);
    s_after = make_surface(900, 600);
    ROBO_CHECK(s_before != NULL && s_after != NULL);
    if (s_before && s_after && before && after) {
        int diff;

        ROBO_CHECK_EQ_INT(robo_demo_page_paint(before, s_before, NULL, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_demo_page_paint(after, s_after, NULL, NULL),
                          ROBO_OK);
        diff = count_pixel_differences(s_before, s_after, 900u, 600u);
        ROBO_CHECK_MSG(diff > 100,
                       "a DOM mutation did not change the rendered picture");
    }

    robo_demo_page_destroy(before);
    robo_demo_page_destroy(after);
    robo_gfx_surface_release(s_before);
    robo_gfx_surface_release(s_after);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
}

/**
 * The lifecycle-to-picture seam: a document that moves through its whole
 * lifecycle renders the same picture, and a document that is *reloaded* does
 * not.
 *
 * Phase 8.10 is a state machine over the document, so the thing that could go
 * wrong is subtle and invisible to a state assertion: a transition that
 * touched the tree would still report the right new state while quietly
 * changing (or retiring) nodes. Rendering through the real adapter before and
 * after the transitions is what catches that — the adapter is a copy of the
 * tree, so an identical picture means no node moved.
 *
 * The reload half is what makes the first half meaningful: it shows the test
 * would notice a changed tree.
 */
static void test_demo_lifecycle_advance_keeps_picture(void)
{
    static const char *const DOC =
        "<html><body><h1>Stable</h1><p>Across the lifecycle</p>"
        "</body></html>";
    robo_html_document_id_t doc = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_document_lifecycle_t life = ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED;
    robo_html_source_t src;
    robo_demo_page_t *before = NULL;
    robo_demo_page_t *after = NULL;
    robo_demo_page_t *reloaded = NULL;
    robo_gfx_surface_t *s_before = NULL;
    robo_gfx_surface_t *s_after = NULL;
    robo_gfx_surface_t *s_reloaded = NULL;
    size_t nodes_before = 0u;

    demo_fixture();

    ROBO_CHECK_EQ_INT(robo_html_document_create(&doc, NULL), ROBO_OK);
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, DOC, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(doc, &src, NULL, NULL), ROBO_OK);

    /* A one-shot load consumes a complete source, so it is done. */
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(doc, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK(robo_html_document_is_settled(doc));
    nodes_before = robo_html_document_node_count(doc);

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &before, NULL),
                      ROBO_OK);
    ROBO_CHECK(before != NULL);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(before), 2);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(before, 0), "Stable");

    /*
     * Now walk the document through the rest of its lifecycle: a replacement
     * source is attached (COMPLETE → LOADING), and the caller settles it again
     * (LOADING → INTERACTIVE → COMPLETE). No parse happens in between, so the
     * tree is untouched.
     */
    ROBO_CHECK_EQ_INT(robo_html_document_attach_source(doc, &src, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(doc, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_interactive(doc, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_mark_complete(doc, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(doc, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(doc), nodes_before);

    /* The same document renders the same blocks and the same picture. */
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &after, NULL),
                      ROBO_OK);
    ROBO_CHECK(after != NULL);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(after), 2);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(after, 0), "Stable");

    s_before = make_surface(900, 600);
    s_after = make_surface(900, 600);
    ROBO_CHECK(s_before != NULL && s_after != NULL);
    if (s_before && s_after && before && after) {
        ROBO_CHECK_EQ_INT(robo_demo_page_paint(before, s_before, NULL, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_demo_page_paint(after, s_after, NULL, NULL),
                          ROBO_OK);
        ROBO_CHECK_MSG(count_pixel_differences(s_before, s_after, 900u, 600u) ==
                           0,
                       "a lifecycle transition changed the rendered picture");
    }

    /*
     * A reload does change it, so the check above is not vacuous: the same
     * document, given a source that parses to a different tree, paints a
     * different picture.
     */
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(
            &src, "<html><body><h1>Reloaded</h1><p>Different words here</p>"
                  "</body></html>",
            NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(doc, &src, NULL, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(doc, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &reloaded, NULL),
                      ROBO_OK);
    ROBO_CHECK(reloaded != NULL);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(reloaded), 2);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(reloaded, 0), "Reloaded");

    s_reloaded = make_surface(900, 600);
    ROBO_CHECK(s_reloaded != NULL);
    if (s_reloaded && s_before && reloaded) {
        ROBO_CHECK_EQ_INT(
            robo_demo_page_paint(reloaded, s_reloaded, NULL, NULL), ROBO_OK);
        ROBO_CHECK_MSG(
            count_pixel_differences(s_before, s_reloaded, 900u, 600u) > 100,
            "a reload did not change the rendered picture");
    }

    robo_demo_page_destroy(before);
    robo_demo_page_destroy(after);
    robo_demo_page_destroy(reloaded);
    robo_gfx_surface_release(s_before);
    robo_gfx_surface_release(s_after);
    robo_gfx_surface_release(s_reloaded);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
}

/*
 * The Phase 8.11 visual checkpoint: a document made of the basic element
 * types, rendered through the real DOM.
 *
 * The picture is the assertion, but the *point* is that the picture comes
 * from typed elements — an <h1> is painted as a heading because it IS a
 * heading, a <title> becomes the caption because it IS the title element,
 * and <div>/<span> contribute nothing because the adapter paints no role for
 * them. If a tag resolved to the wrong type, the styles asserted here would
 * come out wrong or the blocks would not appear at all.
 */
static void test_demo_paint_element_kinds(void)
{
    static const char *const k_html =
        "<!DOCTYPE html><html><head><title>Roboticela</title></head><body>"
        "<h1>RoboticelaOS Runtime</h1>"
        "<p>Hello from RoboHTML.</p>"
        "<div>Basic DOM elements work.</div>"
        "<span>Inline content</span>"
        "</body></html>";
    robo_html_document_id_t doc = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_source_t src;
    robo_html_node_handle_t root;
    robo_html_node_handle_t el;
    robo_html_node_handle_t found;
    robo_demo_page_t *page = NULL;
    robo_gfx_surface_t *surface = NULL;
    robo_demo_block_style_t style;
    robo_gfx_rect_t heading_rect;

    demo_fixture();

    ROBO_CHECK_EQ_INT(robo_html_document_create(&doc, NULL), ROBO_OK);
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, k_html, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(doc, &src, NULL, NULL), ROBO_OK);
    root = robo_html_document_root(doc);

    /*
     * The DOM really holds these types before anything is drawn. No call
     * here aliases its input with its output: a resolution clears its output
     * first, so `find_element(&el, …, &el)` would always miss.
     */
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&root, "html", &el, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&el, ROBO_HTML_TAG_HTML));
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&el, "title", &found, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&found, ROBO_HTML_TAG_TITLE));
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&root, "h1", &found, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&found, ROBO_HTML_TAG_H1));
    ROBO_CHECK(robo_html_tag_is_heading(robo_html_element_tag(&found)));
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&root, "p", &found, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&found, ROBO_HTML_TAG_P));
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&root, "div", &found, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&found, ROBO_HTML_TAG_DIV));
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&root, "span", &found, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_html_element_is_tag(&found, ROBO_HTML_TAG_SPAN));
    ROBO_CHECK(robo_html_tag_is_text_level(robo_html_element_tag(&found)));

    /* The adapter reads that tree: title, heading, paragraph are painted. */
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    ROBO_CHECK(page != NULL);
    if (!page) {
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
        return;
    }

    /* The caption came from the real <title>, not from the h1. */
    ROBO_CHECK_STR_EQ(robo_demo_page_title(page), "Roboticela");

    /* Three painted blocks: the heading, the paragraph, and the division. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(page), 3);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 0, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_HEADING);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 1, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_PARAGRAPH);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, 2, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)style, (int)ROBO_DEMO_BLOCK_PARAGRAPH);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 0),
                      "RoboticelaOS Runtime");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 1),
                      "Hello from RoboHTML.");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 2),
                      "Basic DOM elements work.");

    /*
     * The bare <span> is real DOM that the adapter does not paint, because it
     * is inline and this demo has no inline flow. Asserting the count keeps
     * that a stated limit: if a later phase teaches the adapter to paint a
     * bare inline element, this test says so instead of quietly passing.
     */
    ROBO_CHECK_EQ_INT((int)robo_demo_page_ignored_tag_count(page), 1);

    /* And the pixels follow: layout, then a deterministic paint. */
    ROBO_CHECK_EQ_INT(robo_demo_page_layout(page, 900, 600), ROBO_OK);
    surface = make_surface(900, 600);
    ROBO_CHECK(surface != NULL);
    if (surface) {
        robo_gfx_surface_t *again = make_surface(900, 600);

        ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, surface, NULL, NULL),
                          ROBO_OK);
        /* Accent strip at the top, page background at the right margin. */
        ROBO_CHECK(pixel_is(surface, 450, 2, T_BRAND));
        ROBO_CHECK(pixel_is(surface, 899, 599, T_BG));
        /* The heading block is laid out below the caption. */
        ROBO_CHECK(robo_demo_page_block_rect(page, 1, &heading_rect));
        ROBO_CHECK(heading_rect.y > 0);

        /* A second paint of the same page is pixel-identical. */
        ROBO_CHECK(again != NULL);
        if (again) {
            ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, again, NULL, NULL),
                              ROBO_OK);
            ROBO_CHECK_MSG(count_pixel_differences(surface, again, 900u, 600u) ==
                               0,
                           "painting an element document twice differed");
            robo_gfx_surface_release(again);
        }
        robo_gfx_surface_release(surface);
    }

    robo_demo_page_destroy(page);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
}

/*
 * The Phase 8.12 visual regression: a real decoded image, travelling from
 * markup through the DOM to the framebuffer.
 *
 * Every step is the real implementation — the tokenizer and tree builder
 * produce the `<img>`, the asset resolver supplies genuine PNG bytes, the
 * RoboGraphics decoder produces the pixels, and the assertion is made on the
 * painted surface. Nothing here draws a rectangle and calls it an image.
 */
typedef struct demo_image_resolve_ctx {
    const robo_html_image_resolver_t *resolver;
    size_t count;
} demo_image_resolve_ctx_t;

static robo_result_t demo_image_resolve_visitor(
    const robo_html_node_view_t *view, size_t depth, void *user)
{
    demo_image_resolve_ctx_t *ctx = (demo_image_resolve_ctx_t *)user;

    (void)depth;
    if (!robo_html_element_is_tag(&view->handle, ROBO_HTML_TAG_IMG)) {
        return ROBO_OK;
    }
    if (robo_html_element_image_resolve(&view->handle, ctx->resolver, NULL) !=
        ROBO_OK) {
        return ROBO_ERROR;
    }
    ctx->count++;
    return ROBO_OK;
}

static void test_demo_image_reaches_framebuffer(void)
{
    static const uint8_t IMG_R = 200u;
    static const uint8_t IMG_G = 40u;
    static const uint8_t IMG_B = 90u;
    robo_html_document_id_t doc = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_node_handle_t root;
    robo_html_source_t src;
    robo_gfx_surface_desc_t sdesc;
    robo_gfx_surface_t *picture_surface = NULL;
    robo_gfx_surface_t *canvas = NULL;
    robo_gfx_encoded_buffer_t encoded;
    robo_html_image_assets_t *assets = NULL;
    robo_html_image_resolver_t resolver;
    demo_image_resolve_ctx_t ctx;
    robo_demo_page_t *page = NULL;
    robo_gfx_rect_t rect;
    robo_gfx_color_rgba8_t painted;
    uint32_t x;
    uint32_t y;
    size_t image_index = 0u;
    size_t i;

    demo_fixture();

    /* A 6x5 picture whose first pixel is a colour nothing else on the page
     * uses, so "the image was painted" is a pixel fact and not a guess. */
    robo_gfx_surface_desc_init(&sdesc);
    sdesc.width = 6u;
    sdesc.height = 5u;
    sdesc.format = ROBO_GFX_FORMAT_RGBA8;
    sdesc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&sdesc, &picture_surface),
                      ROBO_OK);
    for (y = 0u; y < 5u; y++) {
        for (x = 0u; x < 6u; x++) {
            robo_gfx_color_rgba8_t c;

            c.r = IMG_R;
            c.g = IMG_G;
            c.b = IMG_B;
            c.a = 255u;
            ROBO_CHECK_EQ_INT(
                robo_gfx_surface_set_pixel(picture_surface, x, y, c), ROBO_OK);
        }
    }
    {
        robo_gfx_encode_options_t eopts;

        robo_gfx_encode_options_init(&eopts);
        eopts.container = ROBO_GFX_ENCODE_PNG;
        eopts.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
        memset(&encoded, 0, sizeof(encoded));
        ROBO_CHECK_EQ_INT(
            robo_gfx_surface_encode(picture_surface, &eopts, &encoded),
            ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_create(&doc, NULL), ROBO_OK);
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(
            &src,
            "<html><body><h1>Roboticela</h1>"
            "<img src=\"Assets/Logo.png\" alt=\"Roboticela logo\">"
            "<p>After the image.</p></body></html>",
            NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(doc, &src, NULL, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_html_image_assets_create(NULL, 0u, &assets), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(assets, "Assets/Logo.png",
                                                 encoded.data, encoded.size,
                                                 NULL),
                      ROBO_OK);
    resolver = robo_html_image_assets_resolver(assets);

    root = robo_html_document_root(doc);
    ctx.resolver = &resolver;
    ctx.count = 0u;
    ROBO_CHECK_EQ_INT(
        robo_html_node_walk_descendants(&root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                        demo_image_resolve_visitor, &ctx,
                                        NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ctx.count, 1);
    ROBO_CHECK_EQ_INT((int)robo_html_document_image_count(doc), 1);

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    ROBO_CHECK(page != NULL);
    ROBO_CHECK_EQ_INT((int)robo_demo_page_image_count(page), 1);

    for (i = 0u; i < robo_demo_page_block_count(page); i++) {
        robo_demo_block_style_t style = (robo_demo_block_style_t)0;

        ROBO_CHECK_EQ_INT(robo_demo_page_block_style(page, i, &style), ROBO_OK);
        if (style == ROBO_DEMO_BLOCK_IMAGE) {
            image_index = i;
            break;
        }
    }
    { /* The block really is the decoded picture, at its intrinsic size. */
        robo_gfx_image_t *block_image =
            robo_demo_page_block_image(page, image_index);

        ROBO_CHECK(block_image != NULL);
        if (!block_image) {
            robo_demo_page_destroy(page);
            robo_html_image_assets_destroy(assets);
            robo_gfx_encoded_buffer_release(&encoded);
            robo_gfx_surface_release(picture_surface);
            ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
            return;
        }
        ROBO_CHECK_EQ_INT((int)robo_gfx_image_width(block_image), 6);
        ROBO_CHECK_EQ_INT((int)robo_gfx_image_height(block_image), 5);
        ROBO_CHECK_EQ_INT(robo_gfx_image_format(block_image),
                          ROBO_GFX_FORMAT_RGBA8);
    }

    /* Paint it for real and read the framebuffer back. */
    robo_gfx_surface_desc_init(&sdesc);
    sdesc.width = 420u;
    sdesc.height = 320u;
    sdesc.format = ROBO_GFX_FORMAT_RGBA8;
    sdesc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&sdesc, &canvas), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, canvas, NULL, NULL), ROBO_OK);
    ROBO_CHECK(robo_demo_page_block_rect(page, image_index, &rect));
    ROBO_CHECK(rect.width == 6 && rect.height == 5);

    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_get_pixel(canvas, (uint32_t)rect.x, (uint32_t)rect.y,
                                   &painted),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(painted.r, IMG_R);
    ROBO_CHECK_EQ_INT(painted.g, IMG_G);
    ROBO_CHECK_EQ_INT(painted.b, IMG_B);

    /* And the image occupies exactly its own rect: the page background above
     * it is untouched by the blit. */
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_get_pixel(canvas, (uint32_t)rect.x,
                                   (uint32_t)rect.y - 1, &painted),
        ROBO_OK);
    ROBO_CHECK(!(painted.r == IMG_R && painted.g == IMG_G &&
                 painted.b == IMG_B));

    robo_demo_page_destroy(page);
    robo_html_image_assets_destroy(assets);
    robo_gfx_encoded_buffer_release(&encoded);
    robo_gfx_surface_release(picture_surface);
    robo_gfx_surface_release(canvas);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* The Phase 8.13 link checkpoint                                      */
/* ------------------------------------------------------------------ */
/*
 * The visual checkpoint is only evidence if the picture came from a real
 * anchor in a real document. These tests take the chain the phase asks for —
 * pointer → hit test → anchor → navigation request → outcome — and assert it
 * against the built-in checkpoint document, so a link that stopped resolving
 * (or a runtime that started claiming navigation it cannot perform) fails
 * here rather than looking pretty in a screenshot.
 */

/** Non-zero when @p rect contains a pixel exactly @p expect (RGB). */
static int rect_has_color(const robo_gfx_surface_t *surface,
                          robo_gfx_rect_t rect,
                          robo_gfx_color_rgba8_t expect)
{
    int32_t x;
    int32_t y;

    for (y = rect.y; y < rect.y + rect.height; y++) {
        for (x = rect.x; x < rect.x + rect.width; x++) {
            uint8_t r;
            uint8_t g;
            uint8_t b;

            if (x < 0 || y < 0) {
                continue;
            }
            if (!rgb_at(surface, (uint32_t)x, (uint32_t)y, &r, &g, &b)) {
                continue;
            }
            if (r == expect.r && g == expect.g && b == expect.b) {
                return 1;
            }
        }
    }
    return 0;
}

static void test_demo_link_checkpoint(void)
{
    robo_demo_page_t *page;
    robo_gfx_rect_t link_rect;
    robo_gfx_rect_t heading_rect;
    robo_gfx_point_t p;
    size_t hit = 0u;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    ROBO_CHECK(page != NULL);
    if (!page) {
        return;
    }

    ROBO_CHECK_EQ_INT((int)robo_demo_page_link_count(page), 2);
    ROBO_CHECK_EQ_INT(robo_demo_page_layout(page, 900, 600), ROBO_OK);

    /* The blocks carry the bytes the document wrote, unresolved: resolution
     * is the navigation model's job and doing it here would be a second,
     * worse URL parser. */
    ROBO_CHECK_STR_EQ(robo_demo_page_block_href(page, 3), "/Settings");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_href(page, 4), "#Overview");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_href(page, 0), "");

    /* Pointer → hit test: the centre of a link block resolves to that
     * block, and the heading above it resolves to no link at all. */
    ROBO_CHECK(robo_demo_page_block_rect(page, 3, &link_rect));
    p.x = link_rect.x + link_rect.width / 2;
    p.y = link_rect.y + link_rect.height / 2;
    ROBO_CHECK_EQ_INT(robo_demo_page_hit_link(page, p, &hit), 1);
    ROBO_CHECK_EQ_INT((int)hit, 3);

    ROBO_CHECK(robo_demo_page_block_rect(page, 4, &link_rect));
    p.x = link_rect.x + link_rect.width / 2;
    p.y = link_rect.y + link_rect.height / 2;
    ROBO_CHECK_EQ_INT(robo_demo_page_hit_link(page, p, &hit), 1);
    ROBO_CHECK_EQ_INT((int)hit, 4);

    ROBO_CHECK(robo_demo_page_block_rect(page, 0, &heading_rect));
    p.x = heading_rect.x + 1;
    p.y = heading_rect.y + heading_rect.height / 2;
    ROBO_CHECK_EQ_INT(robo_demo_page_hit_link(page, p, NULL), 0);

    /* Activation of the route link: no handler is installed, so the honest
     * answer is UNSUPPORTED. Nothing here may report navigation. */
    {
        robo_html_navigation_handler_t handler;
        robo_html_navigation_outcome_t outcome;

        robo_html_navigation_handler_init(&handler);
        ROBO_CHECK_EQ_INT(robo_html_navigation_handler_is_usable(&handler), 0);

        memset(&outcome, 0, sizeof(outcome));
        ROBO_CHECK_EQ_INT(
            robo_demo_page_activate_link(page, 3, &handler, &outcome, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT((int)outcome.kind,
                          (int)ROBO_HTML_NAVIGATION_UNSUPPORTED);
        ROBO_CHECK_EQ_INT((int)outcome.target_kind,
                          (int)ROBO_HTML_TARGET_ROUTE);
        ROBO_CHECK_STR_EQ(outcome.resolved, "/Settings");
        ROBO_CHECK_EQ_INT(outcome.handler_called, 0);
        ROBO_CHECK_EQ_INT(outcome.fragment_found, 0);
        robo_html_navigation_outcome_clear(&outcome);
    }

    /* Activation of the fragment link: this is the one navigation the phase
     * really performs, and it names an element in this document. */
    {
        robo_html_navigation_outcome_t outcome;
        robo_html_element_view_t view;

        memset(&outcome, 0, sizeof(outcome));
        ROBO_CHECK_EQ_INT(
            robo_demo_page_activate_link(page, 4, NULL, &outcome, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT((int)outcome.kind,
                          (int)ROBO_HTML_NAVIGATION_FRAGMENT);
        ROBO_CHECK_EQ_INT((int)outcome.target_kind,
                          (int)ROBO_HTML_TARGET_FRAGMENT);
        ROBO_CHECK_STR_EQ(outcome.fragment, "Overview");
        ROBO_CHECK_EQ_INT(outcome.fragment_found, 1);
        /* The named element is a real one in the tree — the paragraph that
         * carries id="Overview" — not a fabricated handle. */
        ROBO_CHECK_EQ_INT(
            robo_html_element_view_get(&outcome.fragment_node, &view, NULL),
            ROBO_OK);
        ROBO_CHECK(robo_html_element_is_tag(&outcome.fragment_node,
                                            ROBO_HTML_TAG_P));
        ROBO_CHECK(robo_html_element_has_attribute(&outcome.fragment_node,
                                                   "id"));
        robo_html_navigation_outcome_clear(&outcome);
    }

    /* Refusals are refusals: an out-of-range index and a non-link block
     * answer differently, and neither is treated as an activation. */
    ROBO_CHECK_EQ_INT(robo_demo_page_activate_link(page, 99u, NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_demo_page_activate_link(page, 0, NULL, NULL, NULL),
                      ROBO_NOT_SUPPORTED);

    robo_demo_page_destroy(page);
}

static void test_demo_link_visual(void)
{
    robo_demo_page_t *page;
    robo_gfx_surface_t *surface;
    robo_gfx_rect_t link_rect;
    robo_gfx_rect_t paragraph_rect;
    robo_gfx_point_t p;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    surface = make_surface(900, 600);
    ROBO_CHECK(page != NULL && surface != NULL);
    if (!page || !surface) {
        robo_demo_page_destroy(page);
        robo_gfx_surface_release(surface);
        return;
    }

    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, surface, NULL, NULL), ROBO_OK);
    ROBO_CHECK(robo_demo_page_block_rect(page, 3, &link_rect));
    ROBO_CHECK(robo_demo_page_block_rect(page, 1, &paragraph_rect));

    /* The link is drawn in the brand colour and the paragraph is not, so the
     * difference is a colour fact and not "something was drawn". */
    ROBO_CHECK(rect_has_color(surface, link_rect, T_BRAND));
    ROBO_CHECK(!rect_has_color(surface, paragraph_rect, T_BRAND));
    ROBO_CHECK(rect_has_ink(surface, paragraph_rect, T_BG));

    /* And the link carries the hairline rule the paragraph does not: the
     * adapter's one visual difference between a link and body copy. */
    ROBO_CHECK(pixel_is(surface, (uint32_t)link_rect.x,
                        (uint32_t)(link_rect.y + link_rect.height + 1),
                        T_BRAND));
    ROBO_CHECK(pixel_is(surface, (uint32_t)paragraph_rect.x,
                        (uint32_t)(paragraph_rect.y + paragraph_rect.height + 1),
                        T_BG));

    /* The picture still tells the pointer where the link is. */
    p.x = link_rect.x + link_rect.width / 2;
    p.y = link_rect.y + link_rect.height / 2;
    ROBO_CHECK_EQ_INT(robo_demo_page_hit_link(page, p, NULL), 1);

    robo_gfx_surface_release(surface);
    robo_demo_page_destroy(page);
}

static void test_demo_link_after_document_destroyed(void)
{
    robo_html_document_id_t doc = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_source_t src;
    robo_demo_page_t *page = NULL;
    robo_gfx_rect_t rect;
    robo_gfx_point_t p;
    robo_html_navigation_outcome_t outcome;
    size_t before;
    size_t hit = 0u;

    demo_fixture();
    before = robo_html_live_document_count();

    ROBO_CHECK_EQ_INT(robo_html_document_create(&doc, NULL), ROBO_OK);
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(
            &src, "<html><body><a href=\"/Settings\">Settings</a></body></html>",
            NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(doc, &src, NULL, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    ROBO_CHECK(page != NULL);
    if (!page) {
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
        return;
    }
    ROBO_CHECK_EQ_INT((int)robo_demo_page_link_count(page), 1);
    ROBO_CHECK_EQ_INT(robo_demo_page_layout(page, 600, 400), ROBO_OK);
    ROBO_CHECK(robo_demo_page_block_rect(page, 0, &rect));
    p.x = rect.x + 1;
    p.y = rect.y + 1;
    ROBO_CHECK_EQ_INT(robo_demo_page_hit_link(page, p, &hit), 1);
    ROBO_CHECK_EQ_INT((int)hit, 0);

    /* A page built from a caller's document borrows it, so destroying the
     * document must leave the page reading fine and activating cleanly. */
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_href(page, 0), "/Settings");
    ROBO_CHECK_EQ_INT(robo_demo_page_hit_link(page, p, &hit), 1);

    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_demo_page_activate_link(page, 0, NULL, &outcome, NULL),
                      ROBO_NOT_FOUND);
    /* Nothing was decided, and the outcome says so by staying zeroed rather
     * than by inventing a kind for navigation that could not happen. */
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_NONE);
    ROBO_CHECK(outcome.target == NULL);
    ROBO_CHECK_EQ_INT(outcome.handler_called, 0);
    robo_html_navigation_outcome_clear(&outcome);

    robo_demo_page_destroy(page);
    /* The page borrowed the document; destroying it must not have destroyed
     * someone else's document twice, and nothing may leak. */
    ROBO_CHECK_EQ_INT(robo_html_live_document_count(), before);
}

static void test_demo_link_mutation(void)
{
    robo_html_document_id_t doc = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_source_t src;
    robo_html_node_handle_t root;
    robo_html_node_handle_t anchor;
    robo_demo_page_t *page = NULL;
    robo_html_navigation_outcome_t outcome;

    demo_fixture();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&doc, NULL), ROBO_OK);
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(
            &src,
            "<html><body><a href=\"/Settings\">Settings</a>"
            "<p id=\"Overview\">Overview</p></body></html>",
            NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(doc, &src, NULL, NULL), ROBO_OK);

    root = robo_html_document_root(doc);
    ROBO_CHECK_EQ_INT(robo_html_node_find_element(&root, "a", &anchor, NULL),
                      ROBO_OK);

    /* Mutating the attribute must reach the page: the href it reports is
     * re-read from the DOM, not remembered from the first build. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&anchor, "href", "#Overview", NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    ROBO_CHECK(page != NULL);
    if (!page) {
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
        return;
    }
    ROBO_CHECK_STR_EQ(robo_demo_page_block_href(page, 0), "#Overview");
    memset(&outcome, 0, sizeof(outcome));
    ROBO_CHECK_EQ_INT(robo_demo_page_activate_link(page, 0, NULL, &outcome, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)outcome.kind, (int)ROBO_HTML_NAVIGATION_FRAGMENT);
    ROBO_CHECK_EQ_INT(outcome.fragment_found, 1);
    robo_html_navigation_outcome_clear(&outcome);
    robo_demo_page_destroy(page);

    /* Removing the href leaves a link-shaped block with no target, and the
     * runtime says NO_TARGET instead of resolving an empty reference. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_remove_attribute(&anchor, "href", NULL), ROBO_OK);
    page = NULL;
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    ROBO_CHECK(page != NULL);
    if (page) {
        robo_html_navigation_outcome_t bare;

        ROBO_CHECK_STR_EQ(robo_demo_page_block_href(page, 0), "");
        memset(&bare, 0, sizeof(bare));
        ROBO_CHECK_EQ_INT(robo_demo_page_activate_link(page, 0, NULL, &bare,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)bare.kind,
                          (int)ROBO_HTML_NAVIGATION_NO_TARGET);
        ROBO_CHECK_EQ_INT((int)bare.target_kind, (int)ROBO_HTML_TARGET_NONE);
        ROBO_CHECK(bare.resolved == NULL);
        robo_html_navigation_outcome_clear(&bare);
        robo_demo_page_destroy(page);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* 8. Form controls (Phase 8.14)                                       */
/* ------------------------------------------------------------------ */

/**
 * The checkpoint's controls report the state the form model holds.
 *
 * This is the assertion that keeps the picture honest: every value below is
 * a fact the Phase 8.14 model derived from the document, so a block that
 * invented a value, defaulted a checked box, or mistook a placeholder for a
 * value would be caught here rather than in a screenshot nobody diffs.
 *
 * The document is the shipped checkpoint, so the indices are the ones the
 * picture has — see test_demo_default_source, which pins the block order.
 */
static void test_demo_field_checkpoint(void)
{
    robo_demo_page_t *page;
    robo_demo_field_info_t info;
    size_t i;
    static const size_t k_caption_at[7] = {6u, 8u, 10u, 12u, 14u, 16u, 18u};
    static const char *const k_caption[7] = {
        "Display name", "Passphrase",   "Build revision", "Platform notes",
        "Release ring", "Notes",        "Country"};
    static const robo_html_input_type_t k_caption_type[7] = {
        ROBO_HTML_INPUT_TYPE_TEXT,     ROBO_HTML_INPUT_TYPE_TEXT,
        ROBO_HTML_INPUT_TYPE_TEXT,     ROBO_HTML_INPUT_TYPE_TEXT,
        ROBO_HTML_INPUT_TYPE_TEXT,     ROBO_HTML_INPUT_TYPE_TEXT,
        ROBO_HTML_INPUT_TYPE_TEXT};

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    ROBO_CHECK(page != NULL);
    if (!page) {
        return;
    }
    ROBO_CHECK_EQ_INT((int)robo_demo_page_field_count(page), 14);

    /* A caption is a control block too, and it carries no state at all: a
     * `<label>` has no value and no checkedness to report. */
    for (i = 0u; i < 7u; i++) {
        ROBO_CHECK_EQ_INT(
            robo_demo_page_block_field(page, k_caption_at[i], &info), 1);
        ROBO_CHECK_EQ_INT((int)info.tag, (int)ROBO_HTML_TAG_LABEL);
        ROBO_CHECK_EQ_INT((int)info.type, (int)k_caption_type[i]);
        ROBO_CHECK_STR_EQ(info.value, "");
        ROBO_CHECK_EQ_INT(info.checked, 0);
        ROBO_CHECK_EQ_INT(info.option_count, 0u);
        ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, k_caption_at[i]),
                          k_caption[i]);
    }

    /* `<input type="text">` with no `value`: nothing is declared, so nothing
     * is reported, and the placeholder is what the picture shows. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 7, &info), 1);
    ROBO_CHECK_EQ_INT((int)info.tag, (int)ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT((int)info.type, (int)ROBO_HTML_INPUT_TYPE_TEXT);
    ROBO_CHECK_STR_EQ(info.value, "");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 7), "Your name");

    /* A password field carries no placeholder and no value, so it has no
     * text at all — a box and nothing else, which is the honest picture. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 9, &info), 1);
    ROBO_CHECK_EQ_INT((int)info.type, (int)ROBO_HTML_INPUT_TYPE_PASSWORD);
    ROBO_CHECK_STR_EQ(info.value, "");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 9), "");

    /* A number input's declared value is its current value, because nothing
     * has overridden it. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 11, &info), 1);
    ROBO_CHECK_EQ_INT((int)info.type, (int)ROBO_HTML_INPUT_TYPE_NUMBER);
    ROBO_CHECK_STR_EQ(info.value, "8");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 11), "8");

    /* The checkbox is checked; a checkbox has no text of its own. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 13, &info), 1);
    ROBO_CHECK_EQ_INT((int)info.type, (int)ROBO_HTML_INPUT_TYPE_CHECKBOX);
    ROBO_CHECK_EQ_INT(info.checked, 1);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 13), "");

    /* The radio is checked, and its value is its `value` attribute. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 15, &info), 1);
    ROBO_CHECK_EQ_INT((int)info.type, (int)ROBO_HTML_INPUT_TYPE_RADIO);
    ROBO_CHECK_EQ_INT(info.checked, 1);
    ROBO_CHECK_STR_EQ(info.value, "Stable");

    /* The textarea has no children, so its only text is its placeholder. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 17, &info), 1);
    ROBO_CHECK_EQ_INT((int)info.tag, (int)ROBO_HTML_TAG_TEXTAREA);
    ROBO_CHECK_STR_EQ(info.value, "");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 17), "Anything else");

    /* The select reports the option it is on — `selected`, not the first —
     * with both the option's value and its text, and its option count. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 19, &info), 1);
    ROBO_CHECK_EQ_INT((int)info.tag, (int)ROBO_HTML_TAG_SELECT);
    ROBO_CHECK_EQ_INT((int)info.option_count, 2);
    ROBO_CHECK_STR_EQ(info.value, "us");
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 19), "United States");

    /* Refusals: a heading is not a control, a button is not a control block
     * (it keeps its own style), an out-of-range index is not one, and NULL
     * out is an argument error rather than a write. */
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 0, &info), 0);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 20, &info), 0);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 21, &info), 0);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 22, &info), 0);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 7, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(NULL, 7, &info), 0);

    robo_demo_page_destroy(page);
}

/**
 * A control reaches the picture as a shape, and the shape differs by state.
 *
 * Asserted on pixels, because "a control was drawn" is not a claim anyone can
 * check later: a caption draws no box, a field draws a white bordered one, a
 * checked checkbox draws a filled mark, and a textarea is taller than a
 * single-line control. Each of those is a distinct pixel fact here.
 */
static void test_demo_field_visual(void)
{
    robo_demo_page_t *page;
    robo_gfx_surface_t *surface;
    robo_gfx_rect_t caption;
    robo_gfx_rect_t input;
    robo_gfx_rect_t password;
    robo_gfx_rect_t checkbox;
    robo_gfx_rect_t textarea;
    robo_gfx_rect_t select_box;

    demo_fixture();
    page = page_from(robo_demo_default_html(), NULL);
    surface = make_surface(900, 800);
    ROBO_CHECK(page != NULL && surface != NULL);
    if (!page || !surface) {
        robo_demo_page_destroy(page);
        robo_gfx_surface_release(surface);
        return;
    }

    ROBO_CHECK_EQ_INT(robo_demo_page_paint(page, surface, NULL, NULL), ROBO_OK);
    ROBO_CHECK(robo_demo_page_block_rect(page, 6, &caption));
    ROBO_CHECK(robo_demo_page_block_rect(page, 7, &input));

    /* A caption draws text and no box; the field below it draws both. */
    ROBO_CHECK(rect_has_ink(surface, caption, T_BG));
    ROBO_CHECK(pixel_is(surface, (uint32_t)caption.x, (uint32_t)caption.y,
                        T_BG));
    ROBO_CHECK(pixel_is(surface, (uint32_t)input.x, (uint32_t)input.y,
                        T_BORDER));
    ROBO_CHECK(pixel_is(surface, (uint32_t)(input.x + input.width / 2),
                        (uint32_t)(input.y + input.height / 2), T_SURFACE));

    /* The placeholder is drawn in the muted colour, so a reader of the
     * picture can tell "no value" from "this is the value". */
    ROBO_CHECK(rect_has_color(surface, input, T_MUTED));

    /* The password field has neither a placeholder nor a value: it contains
     * no ink of any text colour inside its box. */
    ROBO_CHECK(robo_demo_page_block_rect(page, 9, &password));
    ROBO_CHECK(!rect_has_color(surface, password, T_MUTED));
    ROBO_CHECK(!rect_has_color(surface, password, T_TEXT));
    ROBO_CHECK(!rect_has_color(surface, password, T_BRAND));

    /* The number field holds a value and draws it in the primary text
     * colour — the opposite of the placeholder case above. */
    ROBO_CHECK(robo_demo_page_block_rect(page, 11, &input));
    ROBO_CHECK(rect_has_color(surface, input, T_TEXT));

    /* A checked checkbox draws the brand-coloured mark inside its box. */
    ROBO_CHECK(robo_demo_page_block_rect(page, 13, &checkbox));
    ROBO_CHECK(rect_has_color(surface, checkbox, T_BRAND));

    /* A textarea is a field, but a taller one. */
    ROBO_CHECK(robo_demo_page_block_rect(page, 17, &textarea));
    ROBO_CHECK(robo_demo_page_block_rect(page, 9, &password));
    ROBO_CHECK(textarea.height > password.height);

    /* The select draws a chevron at its right edge that a text field does
     * not, so the two controls are distinguishable in the picture. */
    ROBO_CHECK(robo_demo_page_block_rect(page, 19, &select_box));
    ROBO_CHECK(pixel_is(surface,
                        (uint32_t)(select_box.x + select_box.width - 15),
                        (uint32_t)(select_box.y + select_box.height / 2 - 1),
                        T_MUTED));
    ROBO_CHECK(pixel_is(surface,
                        (uint32_t)(input.x + input.width - 15),
                        (uint32_t)(input.y + input.height / 2 - 1), T_SURFACE));

    robo_gfx_surface_release(surface);
    robo_demo_page_destroy(page);
}

/**
 * A control's state is re-read from the DOM, not remembered from the build.
 *
 * The page is rebuilt from the same document after each mutation, which is
 * exactly the flow a caller uses, and the reported value follows the model
 * every time — including the two facts that look contradictory until you
 * separate them: changing the `value` attribute changes a control's current
 * value while nothing has overridden it, and does *not* once it has.
 */
static void test_demo_field_mutation(void)
{
    robo_html_document_id_t doc = ROBO_HTML_DOCUMENT_ID_NONE;
    robo_html_source_t src;
    robo_html_node_handle_t root;
    robo_html_node_handle_t text_input;
    robo_html_node_handle_t check;
    robo_html_node_handle_t area;
    robo_demo_page_t *page = NULL;
    robo_demo_field_info_t info;

    demo_fixture();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&doc, NULL), ROBO_OK);
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(
            &src,
            "<html><body><form action=\"/Save\">"
            "<input id=\"Name\" type=\"text\" name=\"n\" value=\"Declared\">"
            "<input id=\"News\" type=\"checkbox\" name=\"news\" checked>"
            "<textarea id=\"Note\" name=\"note\">Hello</textarea>"
            "</form></body></html>",
            NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(doc, &src, NULL, NULL), ROBO_OK);
    root = robo_html_document_root(doc);
    ROBO_CHECK_EQ_INT(
        robo_html_node_find_element(&root, "input", &text_input, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_node_find_element(&root, "textarea", &area, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_node_next_sibling(&text_input, &check, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    ROBO_CHECK(page != NULL);
    if (!page) {
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
        return;
    }
    /* Three controls and no captions: the form and body are structure. */
    ROBO_CHECK_EQ_INT((int)robo_demo_page_field_count(page), 3);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 0, &info), 1);
    ROBO_CHECK_STR_EQ(info.value, "Declared");
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 1, &info), 1);
    ROBO_CHECK_EQ_INT(info.checked, 1);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 2, &info), 1);
    /* A textarea's declared value is its child text, not an attribute. */
    ROBO_CHECK_STR_EQ(info.value, "Hello");
    robo_demo_page_destroy(page);
    page = NULL;

    /* An explicit current value: the page reports it, and the `value`
     * attribute still says what the author wrote. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&text_input, "Edited", 6u, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    if (page) {
        ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 0, &info), 1);
        ROBO_CHECK_STR_EQ(info.value, "Edited");
        ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 0), "Edited");
        robo_demo_page_destroy(page);
        page = NULL;
    }

    /* Dropping the override returns the control to its declared value. */
    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_value(&text_input, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    if (page) {
        ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 0, &info), 1);
        ROBO_CHECK_STR_EQ(info.value, "Declared");
        robo_demo_page_destroy(page);
        page = NULL;
    }

    /* With no override in effect the current value tracks the attribute, so
     * rewriting the attribute rewrites what the picture shows. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&text_input, "value", "Rewritten", NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    if (page) {
        ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 0, &info), 1);
        ROBO_CHECK_STR_EQ(info.value, "Rewritten");
        robo_demo_page_destroy(page);
        page = NULL;
    }

    /* Checkedness is independent of the value, and moves on its own. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_control_checked(&check, 0, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    if (page) {
        ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 1, &info), 1);
        ROBO_CHECK_EQ_INT(info.checked, 0);
        /* Setting checkedness did not disturb the value beside it. */
        ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 0, &info), 1);
        ROBO_CHECK_STR_EQ(info.value, "Rewritten");
        robo_demo_page_destroy(page);
        page = NULL;
    }
    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_checked(&check, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    if (page) {
        ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 1, &info), 1);
        ROBO_CHECK_EQ_INT(info.checked, 1);
        robo_demo_page_destroy(page);
        page = NULL;
    }

    /* A textarea's content is its default value, so editing the children —
     * which here means an explicit override — and dropping it both show. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_control_value(&area, "Edited note", 11u, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    if (page) {
        ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 2, &info), 1);
        ROBO_CHECK_STR_EQ(info.value, "Edited note");
        ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 2), "Edited note");
        robo_demo_page_destroy(page);
        page = NULL;
    }
    ROBO_CHECK_EQ_INT(robo_html_element_clear_control_value(&area, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(doc, &page, NULL),
                      ROBO_OK);
    if (page) {
        ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 2, &info), 1);
        ROBO_CHECK_STR_EQ(info.value, "Hello");
        robo_demo_page_destroy(page);
        page = NULL;
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(doc, NULL), ROBO_OK);
}

/**
 * An input type this runtime does not model degrades to `text`.
 *
 * The point is that the degradation is reported as the *effective* type, not
 * the declared one: the block says `text`, because that is what the control
 * behaves as and therefore what the picture draws. Reporting `unknown` here
 * would put a fact in the picture that the runtime does not act on.
 */
static void test_demo_field_unknown_type(void)
{
    robo_demo_page_t *page;
    robo_demo_field_info_t info;

    demo_fixture();

    page = page_from("<html><body><form action=\"/Save\">"
                     "<label for=\"When\">When</label>"
                     "<input id=\"When\" type=\"date\" placeholder=\"Pick\">"
                     "</form></body></html>",
                     NULL);
    ROBO_CHECK(page != NULL);
    if (!page) {
        return;
    }
    ROBO_CHECK_EQ_INT((int)robo_demo_page_field_count(page), 2);
    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 0, &info), 1);
    ROBO_CHECK_EQ_INT((int)info.tag, (int)ROBO_HTML_TAG_LABEL);
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 0), "When");

    ROBO_CHECK_EQ_INT(robo_demo_page_block_field(page, 1, &info), 1);
    ROBO_CHECK_EQ_INT((int)info.tag, (int)ROBO_HTML_TAG_INPUT);
    ROBO_CHECK_EQ_INT((int)info.type, (int)ROBO_HTML_INPUT_TYPE_TEXT);
    /* The element kept its identity and its attributes; only the behaviour
     * degraded, which is what "degrades safely" means. */
    ROBO_CHECK_STR_EQ(robo_demo_page_block_text(page, 1), "Pick");
    ROBO_CHECK_EQ_INT(info.checked, 0);
    ROBO_CHECK_STR_EQ(info.value, "");

    robo_demo_page_destroy(page);
}

void robo_test_register_demo_html(void)
{
    robo_test_register("demo.version", test_demo_version);
    robo_test_register("demo.source.default", test_demo_default_source);
    robo_test_register("demo.source.asset_match",
                       test_demo_asset_matches_embedded_source);
    robo_test_register("demo.source.invalid_args",
                       test_demo_create_invalid_args);
    robo_test_register("demo.source.empty", test_demo_empty_source);
    robo_test_register("demo.tree.character_refs",
                       test_demo_character_references);
    robo_test_register("demo.tree.unsupported_tags",
                       test_demo_unsupported_tags);
    robo_test_register("demo.tree.malformed", test_demo_malformed_input);
    robo_test_register("demo.tree.attribute_hidden",
                       test_demo_hidden_attribute);
    robo_test_register("demo.ownership.text", test_demo_text_ownership);
    robo_test_register("demo.layout.flow", test_demo_layout_flow);
    robo_test_register("demo.layout.resize", test_demo_layout_resize);
    robo_test_register("demo.paint.probes", test_demo_paint_probes);
    robo_test_register("demo.paint.button_states",
                       test_demo_paint_button_states);
    robo_test_register("demo.paint.determinism",
                       test_demo_paint_determinism);
    robo_test_register("demo.paint.clip_small",
                       test_demo_paint_clips_small_target);
    robo_test_register("demo.paint.invalid_args",
                       test_demo_paint_invalid_args);
    robo_test_register("demo.export.png", test_demo_export_png);
    robo_test_register("demo.lifetime.repeated",
                       test_demo_repeated_lifecycle);
    robo_test_register("demo.mutation.changes_picture",
                       test_demo_mutation_changes_picture);
    robo_test_register("demo.paint.element_kinds",
                       test_demo_paint_element_kinds);
    robo_test_register("demo.lifecycle.advance_keeps_picture",
                       test_demo_lifecycle_advance_keeps_picture);
    robo_test_register("demo.image.reaches_framebuffer",
                       test_demo_image_reaches_framebuffer);
    robo_test_register("demo.lifetime.uninitialized",
                       test_demo_module_not_initialized);
    robo_test_register("demo.font.helpers", test_demo_font_helpers);
    robo_test_register("demo.link.checkpoint", test_demo_link_checkpoint);
    robo_test_register("demo.link.visual", test_demo_link_visual);
    robo_test_register("demo.link.document_destroyed",
                       test_demo_link_after_document_destroyed);
    robo_test_register("demo.link.mutation", test_demo_link_mutation);
    robo_test_register("demo.field.checkpoint", test_demo_field_checkpoint);
    robo_test_register("demo.field.visual", test_demo_field_visual);
    robo_test_register("demo.field.mutation", test_demo_field_mutation);
    robo_test_register("demo.field.unknown_type",
                       test_demo_field_unknown_type);
}
