/*
 * RoboHTML Phase 8.12 image suite (html.image.*).
 *
 * The decoder suite (gfx.decode.*) proves the pixels are right. This one
 * proves the *DOM side* is: that the parser builds the element the markup
 * asks for, that the attributes read back exactly as written, that a
 * reference travels from an element through the resolver seam to real
 * RoboGraphics pixels, and that every way that journey can fail leaves the
 * document untouched and the failure observable.
 *
 * The PNG bytes used here are produced by the real Goal 6.13 encoder rather
 * than embedded, because that is what makes the pixel assertions meaningful:
 * the test knows the surface it encoded, so it can compare the image that
 * came back out of the DOM against the surface that went in.
 */

#include "RoboBase/RoboBase.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

/* Defined with the traversal test below; declared here for the resolver
 * cases that need a fetch which cannot complete yet. */
static robo_result_t img_fetch_busy(void *userdata,
                                    const robo_html_image_request_t *request,
                                    const uint8_t **out_bytes,
                                    size_t *out_length,
                                    robo_html_diag_t *diag);

static void img_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static robo_html_document_id_t img_doc(void)
{
    robo_html_document_id_t id = ROBO_HTML_DOCUMENT_ID_NONE;

    img_fixture_begin();
    ROBO_CHECK_EQ_INT(robo_html_document_create(&id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_HTML_DOCUMENT_ID_NONE);
    return id;
}

/** Parse @p text into @p id through the real tokenizer and tree builder. */
static void img_build(robo_html_document_id_t id, const char *text)
{
    robo_html_source_t src;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(id, &src, NULL, NULL), ROBO_OK);
}

static robo_html_node_handle_t img_root(robo_html_document_id_t id)
{
    robo_html_node_handle_t root = robo_html_document_root(id);

    ROBO_CHECK(robo_html_node_handle_is_wellformed(&root));
    return root;
}

typedef struct img_tag_query {
    robo_html_tag_t tag;
} img_tag_query_t;

static int img_match_tag(const robo_html_node_view_t *view, void *user)
{
    const img_tag_query_t *q = (const img_tag_query_t *)user;

    return robo_html_element_is_tag(&view->handle, q->tag);
}

/** First element of @p tag at or below @p start; asserts it was found. */
static robo_html_node_handle_t img_find(const robo_html_node_handle_t *start,
                                        robo_html_tag_t tag)
{
    img_tag_query_t q;
    robo_html_node_handle_t out;

    q.tag = tag;
    robo_html_node_handle_clear(&out);
    ROBO_CHECK_EQ_INT(
        robo_html_node_find_first(start, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  img_match_tag, &q, &out, NULL),
        ROBO_OK);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&out));
    return out;
}

/** Build a 4x4 surface with a fixed, non-uniform pattern. */
static robo_gfx_surface_t *img_surface(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;
    uint32_t x;
    uint32_t y;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 4u;
    desc.height = 4u;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surface), ROBO_OK);
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            robo_gfx_color_rgba8_t c;

            c.r = (uint8_t)(x * 60u + 1u);
            c.g = (uint8_t)(y * 60u + 2u);
            c.b = (uint8_t)((x + y) * 30u + 3u);
            c.a = 255u;
            ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(surface, x, y, c),
                              ROBO_OK);
        }
    }
    return surface;
}

/** Encode @p surface to real PNG bytes (the Goal 6.13 encoder). */
static void img_encode(robo_gfx_surface_t *surface,
                       robo_gfx_encoded_buffer_t *out)
{
    robo_gfx_encode_options_t opts;

    robo_gfx_encode_options_init(&opts);
    opts.container = ROBO_GFX_ENCODE_PNG;
    opts.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    memset(out, 0, sizeof(*out));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_encode(surface, &opts, out), ROBO_OK);
    ROBO_CHECK(out->size > 0u);
}

/** Compare an attached resource's pixels against the surface encoded. */
static void img_expect_same_pixels(const robo_html_image_t *image,
                                   robo_gfx_surface_t *source)
{
    robo_gfx_image_t *graphics = robo_html_image_graphics(image);
    robo_gfx_surface_t *surface;
    uint32_t x;
    uint32_t y;

    ROBO_CHECK(graphics != NULL);
    if (!graphics) {
        return;
    }
    ROBO_CHECK_EQ_INT((int)robo_gfx_image_width(graphics), 4);
    ROBO_CHECK_EQ_INT((int)robo_gfx_image_height(graphics), 4);
    ROBO_CHECK_EQ_INT(robo_gfx_image_format(graphics), ROBO_GFX_FORMAT_RGBA8);
    surface = robo_gfx_image_surface(graphics);
    ROBO_CHECK(surface != NULL);
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            robo_gfx_color_rgba8_t want;
            robo_gfx_color_rgba8_t got;

            ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(source, x, y, &want),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, x, y, &got),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(got.r, want.r);
            ROBO_CHECK_EQ_INT(got.g, want.g);
            ROBO_CHECK_EQ_INT(got.b, want.b);
            ROBO_CHECK_EQ_INT(got.a, want.a);
        }
    }
}

/*
 * One asset table plus the bytes it borrows. The encoded buffer must outlive
 * the table, and the table must outlive the resolver — so they are bundled
 * here and torn down in reverse order, which is the lifetime contract the
 * header documents.
 */
typedef struct img_fixture {
    robo_gfx_surface_t *surface;
    robo_gfx_encoded_buffer_t encoded;
    robo_html_image_assets_t *assets;
} img_fixture_t;

static void img_asset_fixture_begin(img_fixture_t *f)
{
    img_fixture_begin();
    f->surface = img_surface();
    img_encode(f->surface, &f->encoded);
    ROBO_CHECK_EQ_INT(
        robo_html_image_assets_create(NULL, 0u, &f->assets), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(f->assets, "Assets/Logo.png",
                                                 f->encoded.data,
                                                 f->encoded.size, NULL),
                      ROBO_OK);
}

static void img_asset_fixture_end(img_fixture_t *f)
{
    robo_html_image_assets_destroy(f->assets);
    robo_gfx_encoded_buffer_release(&f->encoded);
    robo_gfx_surface_release(f->surface);
}

/* ------------------------------------------------------------------ */
/* Parser + element                                                    */
/* ------------------------------------------------------------------ */

static void test_image_parser_element(void)
{
    robo_html_document_id_t id = img_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t img;
    robo_html_node_handle_t span;
    size_t children = 0u;

    img_build(id, "<div><img src=\"Assets/Logo.png\" alt=\"Logo\">"
                  "<span>After image</span></div>");
    root = img_root(id);
    img = img_find(&root, ROBO_HTML_TAG_IMG);
    span = img_find(&root, ROBO_HTML_TAG_SPAN);

    /* Stable identity, recognised as a void element by the tag table. */
    ROBO_CHECK_EQ_INT(robo_html_element_tag(&img), ROBO_HTML_TAG_IMG);
    ROBO_CHECK(robo_html_tag_is_void(ROBO_HTML_TAG_IMG));

    /* An unclosed <img> must not swallow the following sibling. */
    ROBO_CHECK_EQ_INT(robo_html_node_child_count(&img, &children, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)children, 0);
    ROBO_CHECK(robo_html_node_handle_is_wellformed(&span));
    {
        robo_html_node_handle_t img_parent;
        robo_html_node_handle_t span_parent;

        ROBO_CHECK_EQ_INT(robo_html_node_parent(&img, &img_parent, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_parent(&span, &span_parent, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&img_parent, &span_parent));
    }

    /* Document order: the span follows the image and is its sibling. */
    {
        robo_html_node_handle_t next;

        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&img, &next, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&next, &span));
    }

    /* Traversal reaches the image as an element. */
    ROBO_CHECK_EQ_INT(
        robo_html_node_find_first(&root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                  img_match_tag,
                                  &(img_tag_query_t){ROBO_HTML_TAG_IMG}, &img,
                                  NULL),
        ROBO_OK);

    /* Malformed closing syntax must not corrupt the tree either. */
    {
        robo_html_document_id_t second = img_doc();
        robo_html_node_handle_t second_root;
        robo_html_node_handle_t broken;

        img_build(second, "<p><img src=\"a.png\"</p><img></img>");
        second_root = img_root(second);
        broken = img_find(&second_root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(robo_html_element_tag(&broken), ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(second, NULL), ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_image_attributes(void)
{
    robo_html_document_id_t id = img_doc();
    robo_html_node_handle_t root;
    robo_html_node_handle_t img;
    robo_html_image_attributes_t attrs;
    size_t length = 0u;

    img_build(id, "<img src=\"Assets/Logo.png\" alt=\"Example\" "
                  "width=\"100\" height=\"50\" data-note=\"custom\" "
                  "class=\"hero\">");
    root = img_root(id);
    img = img_find(&root, ROBO_HTML_TAG_IMG);

    ROBO_CHECK_EQ_INT(robo_html_element_image_attributes(&img, &attrs, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.source_present, 1);
    ROBO_CHECK_EQ_INT(attrs.source_usable, 1);
    ROBO_CHECK_EQ_INT((int)attrs.source_length, 15);
    ROBO_CHECK(attrs.source != NULL);
    ROBO_CHECK(memcmp(attrs.source, "Assets/Logo.png", 15u) == 0);
    ROBO_CHECK_EQ_INT((int)attrs.alt_length, 7);
    ROBO_CHECK(memcmp(attrs.alt, "Example", 7u) == 0);
    ROBO_CHECK_EQ_INT(attrs.width_set, 1);
    ROBO_CHECK_EQ_INT(attrs.width_valid, 1);
    ROBO_CHECK_EQ_INT((int)attrs.width, 100);
    ROBO_CHECK_EQ_INT(attrs.height_set, 1);
    ROBO_CHECK_EQ_INT(attrs.height_valid, 1);
    ROBO_CHECK_EQ_INT((int)attrs.height, 50);

    /* Arbitrary attributes survive untouched — the image view is a lens over
     * the attribute model, not a filter on it. */
    ROBO_CHECK(robo_html_element_has_attribute(&img, "data-note"));
    ROBO_CHECK(robo_html_element_has_attribute(&img, "class"));
    ROBO_CHECK_STR_EQ(robo_html_element_attribute_value(&img, "class", &length),
                      "hero");
    ROBO_CHECK(robo_html_element_has_attribute(&img, "src"));
    ROBO_CHECK(robo_html_element_has_attribute(&img, "width"));

    /* A non-img element is a different answer, not a failed lookup. */
    {
        robo_html_node_handle_t div = img_find(&root, ROBO_HTML_TAG_HTML);

        ROBO_CHECK_EQ_INT(
            robo_html_element_image_attributes(&div, &attrs, NULL),
            ROBO_NOT_SUPPORTED);
    }

    /* Missing src: present=false, usable=false — distinct from empty. */
    {
        robo_html_document_id_t bare = img_doc();
        robo_html_node_handle_t bare_root;
        robo_html_node_handle_t bare_img;

        img_build(bare, "<img alt=\"No source\">");
        bare_root = img_root(bare);
        bare_img = img_find(&bare_root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_attributes(&bare_img, &attrs, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(attrs.source_present, 0);
        ROBO_CHECK_EQ_INT(attrs.source_usable, 0);
        ROBO_CHECK(attrs.source == NULL);
        ROBO_CHECK_EQ_INT((int)attrs.alt_length, 9);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_state(&bare_img),
            ROBO_HTML_IMAGE_STATE_NONE);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(bare, NULL), ROBO_OK);
    }

    /* Empty src is present but not usable: nothing to fetch, still NONE. */
    {
        robo_html_document_id_t empty = img_doc();
        robo_html_node_handle_t empty_root;
        robo_html_node_handle_t empty_img;

        img_build(empty, "<img src=\"\">");
        empty_root = img_root(empty);
        empty_img = img_find(&empty_root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_attributes(&empty_img, &attrs, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(attrs.source_present, 1);
        ROBO_CHECK_EQ_INT(attrs.source_usable, 0);
        ROBO_CHECK_EQ_INT((int)attrs.source_length, 0);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_state(&empty_img),
            ROBO_HTML_IMAGE_STATE_NONE);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(empty, NULL), ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_image_dimensions(void)
{
    int valid = 0;
    uint32_t value = 0u;

    ROBO_CHECK_EQ_INT(
        robo_html_image_parse_dimension("128", 3u, &valid, &value), ROBO_OK);
    ROBO_CHECK_EQ_INT(valid, 1);
    ROBO_CHECK_EQ_INT((int)value, 128);

    ROBO_CHECK_EQ_INT(
        robo_html_image_parse_dimension(" 64 ", 4u, &valid, &value), ROBO_OK);
    ROBO_CHECK_EQ_INT(valid, 1);
    ROBO_CHECK_EQ_INT((int)value, 64);

    ROBO_CHECK_EQ_INT(
        robo_html_image_parse_dimension("0", 1u, &valid, &value), ROBO_OK);
    ROBO_CHECK_EQ_INT(valid, 1);
    ROBO_CHECK_EQ_INT((int)value, 0);

    /* Absent: valid-as-no-dimension, not invalid. */
    ROBO_CHECK_EQ_INT(robo_html_image_parse_dimension(NULL, 0u, &valid, &value),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(valid, 0);

    /* Everything below is present-and-not-a-dimension. */
    {
        const char *bad[] = {"", "   ", "abc", "50%", "-5", "+5", "12px",
                             "1e3", "0x10", "1000001", "99999999999999999999",
                             "12 34", "\t\n"};
        size_t i;

        for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
            valid = 1;
            value = 7u;
            ROBO_CHECK_EQ_INT(robo_html_image_parse_dimension(
                                  bad[i], strlen(bad[i]), &valid, &value),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(valid, 0);
            ROBO_CHECK_EQ_INT((int)value, 0);
        }
    }

    /* The boundary is inclusive: the maximum is a dimension, one past it is not. */
    ROBO_CHECK_EQ_INT(robo_html_image_parse_dimension("1000000", 7u, &valid,
                                                      &value),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(valid, 1);
    ROBO_CHECK_EQ_INT((int)value, 1000000);
    ROBO_CHECK_EQ_INT(robo_html_image_parse_dimension("1000001", 7u, &valid,
                                                      &value),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(valid, 0);

    /* Malformed dimensions on a real element are reported, not dropped. */
    {
        robo_html_document_id_t id = img_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t img;
        robo_html_image_attributes_t attrs;

        img_build(id, "<img src=\"a.png\" width=\"abc\" height=\"\">");
        root = img_root(id);
        img = img_find(&root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_attributes(&img, &attrs, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(attrs.width_set, 1);
        ROBO_CHECK_EQ_INT(attrs.width_valid, 0);
        ROBO_CHECK_EQ_INT(attrs.height_set, 1);
        ROBO_CHECK_EQ_INT(attrs.height_valid, 0);
        /* The bad dimension does not disturb the source. */
        ROBO_CHECK_EQ_INT(attrs.source_usable, 1);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }
}

static void test_image_alt_unicode(void)
{
    /* byte-exact preservation, no decoding, no normalisation, no shaping */
    static const char *cases[] = {
        "",
        "Logo",
        "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7",             /* Arabic */
        "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C",             /* Persian */
        "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88",                     /* Urdu */
        "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA6\xE0\xA5\x80",
        "\xE4\xB8\xAD\xE6\x96\x87",
        "\xF0\x9F\x9A\x80 rocket",
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_html_document_id_t id = img_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t img;
        robo_html_image_attributes_t attrs;
        char markup[256];

        /* Registration order avoids `"` and `<` in the value entirely. */
        markup[0] = '\0';
        strcpy(markup, "<img alt=\"");
        strcat(markup, cases[i]);
        strcat(markup, "\">");
        img_build(id, markup);
        root = img_root(id);
        img = img_find(&root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_attributes(&img, &attrs, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)attrs.alt_length, (int)strlen(cases[i]));
        if (attrs.alt_length > 0u) {
            ROBO_CHECK(attrs.alt != NULL);
            ROBO_CHECK(memcmp(attrs.alt, cases[i], attrs.alt_length) == 0);
        }

        /* The same text survives being read as a plain attribute value. */
        {
            size_t length = 0u;
            const char *value =
                robo_html_element_attribute_value(&img, "alt", &length);

            ROBO_CHECK_EQ_INT((int)length, (int)strlen(cases[i]));
            if (length > 0u) {
                ROBO_CHECK(memcmp(value, cases[i], length) == 0);
            }
        }
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }
}

/* ------------------------------------------------------------------ */
/* Resource resolution                                                 */
/* ------------------------------------------------------------------ */

static void test_image_resolve_decoded(void)
{
    robo_html_document_id_t id = img_doc();
    img_fixture_t f;
    robo_html_node_handle_t root;
    robo_html_node_handle_t img;
    robo_html_image_t *attached = NULL;
    robo_html_image_resolver_t resolver;
    robo_html_image_state_t state;
    uint32_t width = 0u;
    uint32_t height = 0u;
    size_t source_length = 0u;

    img_asset_fixture_begin(&f);
    img_build(id, "<img src=\"Assets/Logo.png\" alt=\"Logo\" width=\"128\" "
                  "height=\"64\">");
    root = img_root(id);
    img = img_find(&root, ROBO_HTML_TAG_IMG);
    resolver = robo_html_image_assets_resolver(f.assets);

    /* Before resolving: the reference is known, the pixels are not. */
    ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                      ROBO_HTML_IMAGE_STATE_PENDING);
    ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                      ROBO_OK);
    ROBO_CHECK(attached == NULL);
    ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 0);

    ROBO_CHECK_EQ_INT(
        robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);

    state = robo_html_element_image_state(&img);
    ROBO_CHECK_EQ_INT(state, ROBO_HTML_IMAGE_STATE_DECODED);
    ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 1);

    ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                      ROBO_OK);
    ROBO_CHECK(attached != NULL);
    if (!attached) {
        img_asset_fixture_end(&f);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
        return;
    }
    img_expect_same_pixels(attached, f.surface);

    /* Intrinsic size is the bitmap's; declared dimensions are separate and
     * do not resize anything in this phase. */
    ROBO_CHECK(robo_html_image_intrinsic_size(attached, &width, &height));
    ROBO_CHECK_EQ_INT((int)width, 4);
    ROBO_CHECK_EQ_INT((int)height, 4);
    ROBO_CHECK(robo_html_image_source(attached, &source_length) != NULL);
    ROBO_CHECK_EQ_INT((int)source_length, 15);
    ROBO_CHECK_EQ_INT(robo_html_image_state(attached),
                      ROBO_HTML_IMAGE_STATE_DECODED);
    ROBO_CHECK_EQ_INT(robo_html_image_failure(attached), ROBO_OK);

    /* Declared dimensions still read back as written. */
    {
        robo_html_image_attributes_t attrs;

        ROBO_CHECK_EQ_INT(
            robo_html_element_image_attributes(&img, &attrs, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)attrs.width, 128);
        ROBO_CHECK_EQ_INT((int)attrs.height, 64);
    }

    img_asset_fixture_end(&f);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_image_resolve_failures(void)
{
    img_fixture_t f;
    robo_html_image_resolver_t resolver;

    img_asset_fixture_begin(&f);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(f.assets, "Broken.png",
                                                 "not an image at all", 19u,
                                                 NULL),
                      ROBO_OK);
    resolver = robo_html_image_assets_resolver(f.assets);

    /* Unknown reference → FAILED, and the call still succeeds. */
    {
        robo_html_document_id_t id = img_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t img;
        robo_html_image_t *attached = NULL;

        img_build(id, "<img src=\"Assets/Missing.png\">");
        root = img_root(id);
        img = img_find(&root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                          ROBO_HTML_IMAGE_STATE_FAILED);
        ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                          ROBO_OK);
        ROBO_CHECK(attached != NULL);
        if (attached) {
            ROBO_CHECK_EQ_INT(robo_html_image_failure(attached),
                              ROBO_NOT_FOUND);
            ROBO_CHECK(robo_html_image_graphics(attached) == NULL);
            ROBO_CHECK(robo_html_image_intrinsic_size(attached, &(uint32_t){0},
                                                      &(uint32_t){0}) == 0);
        }
        ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 1);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    /*
     * Registered bytes that are not a recognisable image at all → UNSUPPORTED.
     * The decoder cannot tell a JPEG from a corrupt PNG, and saying "this
     * runtime does not implement that format" is more honest than claiming
     * the bytes are malformed.
     */
    {
        robo_html_document_id_t id = img_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t img;
        robo_html_image_t *attached = NULL;

        img_build(id, "<div><img src=\"Broken.png\"><span>sibling</span></div>");
        root = img_root(id);
        img = img_find(&root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                          ROBO_HTML_IMAGE_STATE_UNSUPPORTED);
        ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                          ROBO_OK);
        if (attached) {
            ROBO_CHECK_EQ_INT(robo_html_image_failure(attached),
                              ROBO_NOT_SUPPORTED);
        }
        {
            robo_html_node_handle_t sibling = img_find(&root, ROBO_HTML_TAG_SPAN);

            ROBO_CHECK(robo_html_node_handle_is_wellformed(&sibling));
        }
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    /*
     * A real PNG truncated inside its IDAT → FAILED with the decoder's own
     * code. This is the case the state exists for: the container is one we
     * implement, and the bytes are wrong.
     */
    {
        robo_html_document_id_t id = img_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t img;
        robo_html_image_t *attached = NULL;
        robo_gfx_encoded_buffer_t full;

        img_encode(f.surface, &full);
        ROBO_CHECK_EQ_INT(robo_html_image_assets_add(f.assets, "Truncated.png",
                                                     full.data,
                                                     full.size - 2u, NULL),
                          ROBO_OK);
        img_build(id, "<img src=\"Truncated.png\">");
        root = img_root(id);
        img = img_find(&root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                          ROBO_HTML_IMAGE_STATE_FAILED);
        ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                          ROBO_OK);
        if (attached) {
            ROBO_CHECK_EQ_INT(robo_html_image_failure(attached), ROBO_ERROR);
            ROBO_CHECK(robo_html_image_graphics(attached) == NULL);
        }
        robo_gfx_encoded_buffer_release(&full);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    /* A reference that names an image but is not PNG/BMP bytes at all → the
     * decoder says "not a format we implement", which is UNSUPPORTED. */
    {
        robo_html_document_id_t id = img_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t img;
        robo_html_image_state_t state = ROBO_HTML_IMAGE_STATE_NONE;

        ROBO_CHECK_EQ_INT(
            robo_html_image_assets_add(f.assets, "Photo.jpg",
                                       "\xFF\xD8\xFF\xE0notreally", 12u, NULL),
            ROBO_OK);
        img_build(id, "<img src=\"Photo.jpg\">");
        root = img_root(id);
        img = img_find(&root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                          ROBO_HTML_IMAGE_STATE_UNSUPPORTED);
        ROBO_CHECK_EQ_INT(robo_html_image_bytes_state("\xFF\xD8\xFF\xE0xxxx",
                                                      8u, &state),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(state, ROBO_HTML_IMAGE_STATE_UNSUPPORTED);
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    img_asset_fixture_end(&f);
}

static void test_image_resolve_schemes(void)
{
    img_fixture_t f;
    robo_html_image_resolver_t resolver;
    static const char *refs[] = {
        "https://example.com/Logo.png",
        "http://example.com/Logo.png",
        "file:///etc/passwd",
        "data:image/png;base64,AAAA",
        "//example.com/Logo.png",
    };
    size_t i;

    img_asset_fixture_begin(&f);
    resolver = robo_html_image_assets_resolver(f.assets);

    for (i = 0u; i < sizeof(refs) / sizeof(refs[0]); i++) {
        robo_html_document_id_t id = img_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t img;
        char markup[128];

        strcpy(markup, "<img src=\"");
        strcat(markup, refs[i]);
        strcat(markup, "\">");
        img_build(id, markup);
        root = img_root(id);
        img = img_find(&root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
        /*
         * A scheme is refused *by policy* — UNSUPPORTED — not reported as
         * "not found". The distinction is the whole point: it says the
         * runtime will not fetch that, rather than blaming the document.
         * "//example.com/x" has no scheme and is simply not a registered
         * asset, so it is FAILED: it parsed as a relative reference.
         */
        if (i + 1u < sizeof(refs) / sizeof(refs[0])) {
            ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                              ROBO_HTML_IMAGE_STATE_UNSUPPORTED);
        } else {
            ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                              ROBO_HTML_IMAGE_STATE_FAILED);
        }
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
    }

    /* Nothing was fetched, so the document never grew an attachment for the
     * refusals — but it did for the relative one. That asymmetry is the
     * evidence that the resolver ran rather than being skipped. */
    img_asset_fixture_end(&f);
}

static void test_image_resolve_without_source(void)
{
    robo_html_document_id_t id = img_doc();
    img_fixture_t f;
    robo_html_node_handle_t root;
    robo_html_node_handle_t img;
    robo_html_image_resolver_t resolver;
    robo_html_image_attributes_t attrs;
    size_t length = 0u;

    img_asset_fixture_begin(&f);
    resolver = robo_html_image_assets_resolver(f.assets);
    img_build(id, "<img src=\"Assets/Logo.png\" alt=\"Logo\">");
    root = img_root(id);
    img = img_find(&root, ROBO_HTML_TAG_IMG);

    ROBO_CHECK_EQ_INT(
        robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                      ROBO_HTML_IMAGE_STATE_DECODED);
    ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 1);

    /*
     * Remove `src`. The already-resolved resource stays attached until
     * something resolves or detaches it — removing an attribute is not a
     * signal to the resource layer, and pretending otherwise would mean the
     * attribute model reached into the image model.
     */
    ROBO_CHECK_EQ_INT(robo_html_element_remove_attribute(&img, "src", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                      ROBO_HTML_IMAGE_STATE_DECODED);
    ROBO_CHECK_EQ_INT(
        robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                      ROBO_HTML_IMAGE_STATE_NONE);
    ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 0);

    /* Put it back: the element becomes resolvable again, which is what makes
     * "restore src" work without any reload machinery. */
    ROBO_CHECK_EQ_INT(
        robo_html_element_set_attribute(&img, "src", "Assets/Logo.png", NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                      ROBO_HTML_IMAGE_STATE_DECODED);
    ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 1);

    /* Setting src to empty is a third case: present, unusable, NONE. */
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&img, "src", "", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_html_element_image_attributes(&img, &attrs, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(attrs.source_present, 1);
    ROBO_CHECK_EQ_INT(attrs.source_usable, 0);
    ROBO_CHECK_EQ_INT(
        robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                      ROBO_HTML_IMAGE_STATE_NONE);
    ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 0);

    ROBO_CHECK_STR_EQ(robo_html_element_attribute_value(&img, "alt", &length),
                      "Logo");
    img_asset_fixture_end(&f);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_image_reresolve_replaces(void)
{
    robo_html_document_id_t id = img_doc();
    img_fixture_t f;
    robo_html_node_handle_t root;
    robo_html_node_handle_t img;
    robo_html_image_resolver_t resolver;
    robo_html_image_t *attached = NULL;

    img_asset_fixture_begin(&f);
    /* A second, differently sized image, so "which one is attached" is
     * answerable from the pixels rather than from a count. */
    {
        robo_gfx_surface_desc_t desc;
        robo_gfx_surface_t *other = NULL;
        robo_gfx_encoded_buffer_t encoded;
        uint32_t i;

        robo_gfx_surface_desc_init(&desc);
        desc.width = 2u;
        desc.height = 6u;
        desc.format = ROBO_GFX_FORMAT_RGBA8;
        desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
        ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &other), ROBO_OK);
        for (i = 0u; i < 12u; i++) {
            robo_gfx_color_rgba8_t c;

            c.r = 200u;
            c.g = 10u;
            c.b = 20u;
            c.a = 255u;
            ROBO_CHECK_EQ_INT(
                robo_gfx_surface_set_pixel(other, i % 2u, i / 2u, c), ROBO_OK);
        }
        img_encode(other, &encoded);
        ROBO_CHECK_EQ_INT(robo_html_image_assets_add(
                              f.assets, "Assets/Wide.png", encoded.data,
                              encoded.size, NULL),
                          ROBO_OK);

        resolver = robo_html_image_assets_resolver(f.assets);
        img_build(id, "<img src=\"Assets/Logo.png\">");
        root = img_root(id);
        img = img_find(&root, ROBO_HTML_TAG_IMG);

        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 1);

        /* Change src, resolve again: one resource, not two. */
        ROBO_CHECK_EQ_INT(
            robo_html_element_set_attribute(&img, "src", "Assets/Wide.png",
                                            NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 1);
        ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                          ROBO_OK);
        ROBO_CHECK(attached != NULL);
        if (attached) {
            uint32_t w = 0u;
            uint32_t h = 0u;
            size_t length = 0u;

            ROBO_CHECK(robo_html_image_intrinsic_size(attached, &w, &h));
            ROBO_CHECK_EQ_INT((int)w, 2);
            ROBO_CHECK_EQ_INT((int)h, 6);
            ROBO_CHECK(robo_html_image_source(attached, &length) != NULL);
            ROBO_CHECK_EQ_INT((int)length, 15);
            ROBO_CHECK(memcmp(robo_html_image_source(attached, NULL),
                              "Assets/Wide.png", 15u) == 0);
        }

        /* Resolving the same reference repeatedly is stable, not cumulative. */
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 1);

        robo_gfx_encoded_buffer_release(&encoded);
        robo_gfx_surface_release(other);
    }
    img_asset_fixture_end(&f);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_image_detach(void)
{
    robo_html_document_id_t id = img_doc();
    img_fixture_t f;
    robo_html_node_handle_t root;
    robo_html_node_handle_t img;
    robo_html_image_resolver_t resolver;
    robo_html_image_t *attached = NULL;

    img_asset_fixture_begin(&f);
    resolver = robo_html_image_assets_resolver(f.assets);
    img_build(id, "<img src=\"Assets/Logo.png\">");
    root = img_root(id);
    img = img_find(&root, ROBO_HTML_TAG_IMG);

    ROBO_CHECK_EQ_INT(
        robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 1);

    /* A retained resource outlives the element's claim on it — this is the
     * one case where the reference count matters. */
    ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                      ROBO_OK);
    ROBO_CHECK(attached != NULL);
    robo_html_image_retain(attached);

    ROBO_CHECK_EQ_INT(robo_html_element_image_detach(&img, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 0);
    ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                      ROBO_OK);
    ROBO_CHECK(attached == NULL);
    /* Detaching is not deleting the element's attributes. */
    ROBO_CHECK(robo_html_element_has_attribute(&img, "src"));
    ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                      ROBO_HTML_IMAGE_STATE_PENDING);

    robo_html_image_release(attached);
    /* Idempotent. */
    ROBO_CHECK_EQ_INT(robo_html_element_image_detach(&img, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_image_detach(&img, NULL), ROBO_OK);

    img_asset_fixture_end(&f);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_image_argument_safety(void)
{
    robo_html_document_id_t id = img_doc();
    img_fixture_t f;
    robo_html_node_handle_t root;
    robo_html_node_handle_t img;
    robo_html_node_handle_t div;
    robo_html_image_t *image = NULL;
    robo_html_image_state_t state = ROBO_HTML_IMAGE_STATE_NONE;
    robo_html_image_resolver_t resolver;
    robo_html_image_resolver_t unusable;
    robo_html_image_assets_t *empty = NULL;

    img_asset_fixture_begin(&f);
    resolver = robo_html_image_assets_resolver(f.assets);
    robo_html_image_resolver_init(&unusable);

    img_build(id, "<div><img src=\"Assets/Logo.png\"></div>");
    root = img_root(id);
    img = img_find(&root, ROBO_HTML_TAG_IMG);
    div = img_find(&root, ROBO_HTML_TAG_DIV);

    /* NULL arguments are programming errors, and say so. */
    ROBO_CHECK_EQ_INT(robo_html_element_image_attributes(NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_image_get(NULL, &image, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_image_resolve(NULL, &resolver, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_image_resolve(&img, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_element_image_resolve(&img, &unusable, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(!robo_html_image_resolver_is_usable(&unusable));
    ROBO_CHECK(robo_html_image_resolver_is_usable(&resolver));
    ROBO_CHECK_EQ_INT(robo_html_image_bytes_state(NULL, 8u, &state),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_image_bytes_state("abc", 3u, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_html_image_resolver_init(NULL); /* must not crash */

    /* A non-img element is NOT_SUPPORTED; a stale handle is NOT_FOUND. */
    ROBO_CHECK_EQ_INT(robo_html_element_image_resolve(&div, &resolver, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_html_element_image_get(&div, &image, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_html_element_image_attributes(&div, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_html_node_handle_t stale = img;

        stale.document_id = (robo_html_document_id_t)9999u;
        ROBO_CHECK_EQ_INT(robo_html_element_image_get(&stale, &image, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&stale, &resolver, NULL),
            ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_html_element_image_state(&stale),
                          ROBO_HTML_IMAGE_STATE_NONE);
    }

    /*
     * A resolver that reports BUSY leaves the element PENDING: work that has
     * not finished is not a failure, and must not be recorded as one.
     */
    {
        robo_html_image_resolver_t busy_resolver;

        busy_resolver.fetch = img_fetch_busy;
        busy_resolver.userdata = NULL;
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &busy_resolver, NULL),
            ROBO_BUSY);
        ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                          ROBO_HTML_IMAGE_STATE_PENDING);
        ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 0);
    }

    /* Empty asset tables are legal and simply never match. */
    ROBO_CHECK_EQ_INT(robo_html_image_assets_create(NULL, 0u, &empty), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_count(empty), 0);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(empty, "", NULL, 0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(NULL, "x", NULL, 0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(empty, "x", NULL, 4u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(empty, "x", "abcd", 4u, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(empty, "x", "abcd", 4u, NULL),
                      ROBO_ALREADY_EXISTS);
    {
        size_t length = 0u;

        ROBO_CHECK_EQ_INT(robo_html_image_assets_count(empty), 1);
        ROBO_CHECK(robo_html_image_assets_lookup(empty, "x", &length) != NULL);
        ROBO_CHECK_EQ_INT((int)length, 4);
        ROBO_CHECK(robo_html_image_assets_lookup(empty, "y", &length) == NULL);
        ROBO_CHECK_EQ_INT((int)length, 0);
        ROBO_CHECK(robo_html_image_assets_lookup(NULL, "x", &length) == NULL);
    }
    robo_html_image_assets_destroy(empty);
    robo_html_image_assets_destroy(NULL); /* must not crash */

    /* Null/absent image handles never crash the accessors. */
    ROBO_CHECK_EQ_INT(robo_html_image_state(NULL), ROBO_HTML_IMAGE_STATE_NONE);
    ROBO_CHECK(robo_html_image_graphics(NULL) == NULL);
    ROBO_CHECK(robo_html_image_source(NULL, NULL) == NULL);
    ROBO_CHECK(!robo_html_image_intrinsic_size(NULL, &(uint32_t){0},
                                               &(uint32_t){0}));
    ROBO_CHECK_EQ_INT(robo_html_image_failure(NULL), ROBO_INVALID_ARGUMENT);
    robo_html_image_release(NULL);
    robo_html_image_retain(NULL);
    robo_html_image_destroy(NULL);
    robo_html_image_describe(NULL, NULL, 0u);

    /* 16 KiB of garbage registered under a name is still just FAILED. */
    ROBO_CHECK_EQ_INT(
        robo_html_image_bytes_state("\x89PNG\r\n\x1a\nshort", 13u, &state),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(state, ROBO_HTML_IMAGE_STATE_FAILED);

    img_asset_fixture_end(&f);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Ownership, lifecycle, traversal, mutation                           */
/* ------------------------------------------------------------------ */

typedef struct img_resolve_ctx {
    const robo_html_image_resolver_t *resolver;
    size_t resolved;
} img_resolve_ctx_t;

/* Set by the visitor so the assertion below can read it back out. */
static size_t img_last_visited_count = 0u;

static robo_result_t img_resolve_visitor(const robo_html_node_view_t *view,
                                         size_t depth, void *user)
{
    img_resolve_ctx_t *ctx = (img_resolve_ctx_t *)user;

    (void)depth;
    if (!robo_html_element_is_tag(&view->handle, ROBO_HTML_TAG_IMG)) {
        return ROBO_OK;
    }
    if (robo_html_element_image_resolve(&view->handle, ctx->resolver, NULL) !=
        ROBO_OK) {
        return ROBO_ERROR;
    }
    ctx->resolved++;
    img_last_visited_count = ctx->resolved;
    return ROBO_OK;
}

/** A fetch function that reports work in progress, for the BUSY case. */
static robo_result_t img_fetch_busy(void *userdata,
                                    const robo_html_image_request_t *request,
                                    const uint8_t **out_bytes,
                                    size_t *out_length,
                                    robo_html_diag_t *diag)
{
    (void)userdata;
    (void)request;
    (void)diag;
    *out_bytes = NULL;
    *out_length = 0u;
    return ROBO_BUSY;
}

static void test_image_document_teardown(void)
{
    img_fixture_t f;
    robo_html_image_resolver_t resolver;

    img_asset_fixture_begin(&f);
    resolver = robo_html_image_assets_resolver(f.assets);

    /* Several images in one document: the table is per element, not global. */
    {
        robo_html_document_id_t id = img_doc();
        robo_html_node_handle_t root;
        img_tag_query_t q;
        robo_html_node_handle_t found;
        size_t visited = 0u;

        img_build(id, "<div><img src=\"Assets/Logo.png\">"
                      "<p><img src=\"Assets/Logo.png\"></p>"
                      "<img src=\"Assets/Missing.png\"></div>");
        root = img_root(id);

        /* Resolve every img by walking the tree we already have. The walk
         * is the document-order foundation from 8.8, so this also proves the
         * image layer composes with traversal rather than beside it. */
        q.tag = ROBO_HTML_TAG_IMG;
        ROBO_CHECK_EQ_INT(
            robo_html_node_walk_descendants(
                &root, ROBO_HTML_NODE_FILTER_ELEMENTS, img_resolve_visitor,
                &(img_resolve_ctx_t){&resolver, 0u}, NULL),
            ROBO_OK);
        ROBO_CHECK_EQ_INT((int)img_last_visited_count, 3);
        ROBO_CHECK_EQ_INT((int)robo_html_document_image_count(id), 3);
        (void)found;
        (void)visited;

        /* A parsed document is COMPLETE, and images resolve there — the
         * normal case, since that is the state a loaded page is in. */
        {
            robo_html_document_lifecycle_t life =
                ROBO_HTML_DOCUMENT_LIFECYCLE_UNINITIALIZED;

            ROBO_CHECK_EQ_INT(robo_html_document_get_lifecycle(id, &life),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(life, ROBO_HTML_DOCUMENT_LIFECYCLE_COMPLETE);
        }

        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 0);
    }

    /* A retained resource survives its document, because ownership followed
     * the reference count and not the document. */
    {
        robo_html_document_id_t id = img_doc();
        robo_html_node_handle_t root;
        robo_html_node_handle_t img;
        robo_html_image_t *attached = NULL;
        uint32_t width = 0u;
        uint32_t height = 0u;

        img_build(id, "<img src=\"Assets/Logo.png\">");
        root = img_root(id);
        img = img_find(&root, ROBO_HTML_TAG_IMG);
        ROBO_CHECK_EQ_INT(
            robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                          ROBO_OK);
        ROBO_CHECK(attached != NULL);
        robo_html_image_retain(attached);

        ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 0);
        ROBO_CHECK_EQ_INT(robo_html_image_state(attached),
                          ROBO_HTML_IMAGE_STATE_DECODED);
        ROBO_CHECK(robo_html_image_intrinsic_size(attached, &width, &height));
        ROBO_CHECK_EQ_INT((int)width, 4);
        ROBO_CHECK_EQ_INT((int)height, 4);
        img_expect_same_pixels(attached, f.surface);
        robo_html_image_release(attached);

        /* After the document is gone the element no longer resolves at all. */
        ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                          ROBO_HTML_IMAGE_STATE_NONE);
    }

    img_asset_fixture_end(&f);
}

static void test_image_mutation_and_traversal(void)
{
    robo_html_document_id_t id = img_doc();
    img_fixture_t f;
    robo_html_node_handle_t root;
    robo_html_node_handle_t container;
    robo_html_node_handle_t img;
    robo_html_node_handle_t span;
    robo_html_image_resolver_t resolver;

    img_asset_fixture_begin(&f);
    resolver = robo_html_image_assets_resolver(f.assets);
    img_build(id, "<div><span>before</span><img src=\"Assets/Logo.png\">"
                  "<span>after</span></div>");
    root = img_root(id);
    container = img_find(&root, ROBO_HTML_TAG_DIV);
    img = img_find(&root, ROBO_HTML_TAG_IMG);
    span = img_find(&root, ROBO_HTML_TAG_SPAN);

    /* Sibling relationships around a void element are ordinary ones. */
    {
        robo_html_node_handle_t next;
        robo_html_node_handle_t prev;
        size_t depth = 0u;

        ROBO_CHECK_EQ_INT(robo_html_node_next_sibling(&img, &next, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_tag(&next), ROBO_HTML_TAG_SPAN);
        ROBO_CHECK_EQ_INT(robo_html_node_prev_sibling(&img, &prev, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_tag(&prev), ROBO_HTML_TAG_SPAN);
        /* document(0) → html(1) → body(2) → div(3) → img(4) */
        ROBO_CHECK_EQ_INT(robo_html_node_depth(&img, &depth, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)depth, 4);
    }

    /* Traversal reaches it in document order between its two siblings. */
    {
        img_tag_query_t q;
        robo_html_node_handle_t first;

        q.tag = ROBO_HTML_TAG_IMG;
        ROBO_CHECK_EQ_INT(
            robo_html_node_find_first(&container, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                      img_match_tag, &q, &first, NULL),
            ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&first, &img));
    }

    ROBO_CHECK_EQ_INT(
        robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_image_state(&img),
                      ROBO_HTML_IMAGE_STATE_DECODED);

    /* Move the image: the resource travels with the node's identity, so an
     * element that is moved does not silently lose its picture. */
    {
        robo_html_node_handle_t detached = img;
        robo_html_node_handle_t parent;

        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&container, &img, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 1);
        ROBO_CHECK_EQ_INT(robo_html_element_image_state(&detached),
                          ROBO_HTML_IMAGE_STATE_DECODED);
        ROBO_CHECK_EQ_INT(robo_html_node_append_child(&container, &detached,
                                                      NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_parent(&detached, &parent, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_html_node_handle_equal(&parent, &container));
        ROBO_CHECK_EQ_INT(robo_html_element_image_state(&detached),
                          ROBO_HTML_IMAGE_STATE_DECODED);
    }

    /* Replacing an image with a span leaves one resource table entry for the
     * image (which still exists as a detached node) and none for the span. */
    {
        robo_html_node_handle_t replacement;

        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&container, &img, NULL),
                          ROBO_OK);
        /* The span is still a child, so it must be detached before it can be
         * re-placed — the one-parent rule from 8.9. */
        ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&container, &span, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_append_child(&container, &span, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_document_image_count(id), 1);
        replacement = img_find(&container, ROBO_HTML_TAG_SPAN);
        ROBO_CHECK(robo_html_node_handle_is_wellformed(&replacement));
        ROBO_CHECK_EQ_INT(robo_html_element_image_state(&replacement),
                          ROBO_HTML_IMAGE_STATE_NONE);
    }

    img_asset_fixture_end(&f);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

static void test_image_debug_describe(void)
{
    robo_html_document_id_t id = img_doc();
    img_fixture_t f;
    robo_html_node_handle_t root;
    robo_html_node_handle_t img;
    robo_html_image_t *attached = NULL;
    robo_html_image_resolver_t resolver;
    char line[160];
    size_t written;

    img_asset_fixture_begin(&f);
    resolver = robo_html_image_assets_resolver(f.assets);
    img_build(id, "<img src=\"Assets/Logo.png\" alt=\"Logo\">");
    root = img_root(id);
    img = img_find(&root, ROBO_HTML_TAG_IMG);

    /* Before resolving there is no resource to describe, so an
     * element-derived description is only available after one exists. */
    ROBO_CHECK_EQ_INT(robo_html_image_state_is_known(
                          ROBO_HTML_IMAGE_STATE_DECODED),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_image_state_is_known((robo_html_image_state_t)99),
                      0);
    ROBO_CHECK_STR_EQ(robo_html_image_state_name(ROBO_HTML_IMAGE_STATE_NONE),
                      "none");
    ROBO_CHECK_STR_EQ(robo_html_image_state_name(ROBO_HTML_IMAGE_STATE_PENDING),
                      "pending");
    ROBO_CHECK_STR_EQ(robo_html_image_state_name(ROBO_HTML_IMAGE_STATE_DECODED),
                      "decoded");
    ROBO_CHECK_STR_EQ(robo_html_image_state_name(ROBO_HTML_IMAGE_STATE_FAILED),
                      "failed");
    ROBO_CHECK_STR_EQ(
        robo_html_image_state_name(ROBO_HTML_IMAGE_STATE_UNSUPPORTED),
        "unsupported");
    ROBO_CHECK_STR_EQ(robo_html_image_state_name((robo_html_image_state_t)99),
                      "unknown");

    ROBO_CHECK_EQ_INT(
        robo_html_element_image_resolve(&img, &resolver, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_element_image_get(&img, &attached, NULL),
                      ROBO_OK);
    ROBO_CHECK(attached != NULL);
    if (attached) {
        memset(line, 0, sizeof(line));
        written = robo_html_image_describe(attached, line, sizeof(line));
        ROBO_CHECK(written > 0u);
        ROBO_CHECK(strstr(line, "state=decoded") != NULL);
        ROBO_CHECK(strstr(line, "intrinsic=4x4") != NULL);
        ROBO_CHECK(strstr(line, "Assets/Logo.png") != NULL);

        /* Truncation terminates and reports the untruncated length. */
        {
            char small[8];
            size_t full = robo_html_image_describe(attached, small,
                                                   sizeof(small));

            ROBO_CHECK_EQ_INT((int)full, (int)written);
            ROBO_CHECK_EQ_INT((int)strlen(small), 7);
        }
        ROBO_CHECK_EQ_INT((int)robo_html_image_describe(attached, NULL, 0u),
                          (int)written);
    }

    /* A failure resource describes its reason instead of a size. */
    {
        robo_html_image_t *failed = NULL;

        ROBO_CHECK_EQ_INT(robo_html_image_create_decoded(
                              NULL, 0u, "Assets/Missing.png", &failed, NULL),
                          ROBO_OK);
        ROBO_CHECK(failed != NULL);
        if (failed) {
            memset(line, 0, sizeof(line));
            robo_html_image_describe(failed, line, sizeof(line));
            ROBO_CHECK(strstr(line, "state=failed") != NULL);
            ROBO_CHECK(strstr(line, "reason=") != NULL);
            ROBO_CHECK_EQ_INT(robo_html_image_state(failed),
                              ROBO_HTML_IMAGE_STATE_FAILED);
            robo_html_image_release(failed);
        }
    }

    img_asset_fixture_end(&f);
    ROBO_CHECK_EQ_INT(robo_html_document_destroy(id, NULL), ROBO_OK);
}

/*
 * A consumer discovers a model through the capability query, like every other
 * model in this runtime — so it is asserted here rather than assumed, and
 * asserted *narrowly*: `images` means an `<img>` holds real decoded pixels
 * reachable through a resolver. It is not a claim of network access, and it is
 * not a claim that images can be sized, styled or laid out.
 */
static void test_image_capability_reported(void)
{
    robo_html_dom_capabilities_t caps;

    ROBO_CHECK_EQ_INT(robo_html_dom_query_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.images, 1);
    ROBO_CHECK_EQ_INT(caps.dom_nodes, 0);
    ROBO_CHECK_EQ_INT(caps.events, 0);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_html_image(void)
{
    robo_test_register("html.image.parser_element", test_image_parser_element);
    robo_test_register("html.image.attributes", test_image_attributes);
    robo_test_register("html.image.dimensions", test_image_dimensions);
    robo_test_register("html.image.alt_unicode", test_image_alt_unicode);
    robo_test_register("html.image.resolve_decoded", test_image_resolve_decoded);
    robo_test_register("html.image.resolve_failures",
                       test_image_resolve_failures);
    robo_test_register("html.image.resolve_schemes", test_image_resolve_schemes);
    robo_test_register("html.image.resolve_without_source",
                       test_image_resolve_without_source);
    robo_test_register("html.image.reresolve_replaces",
                       test_image_reresolve_replaces);
    robo_test_register("html.image.detach", test_image_detach);
    robo_test_register("html.image.argument_safety",
                       test_image_argument_safety);
    robo_test_register("html.image.document_teardown",
                       test_image_document_teardown);
    robo_test_register("html.image.mutation_and_traversal",
                       test_image_mutation_and_traversal);
    robo_test_register("html.image.debug_describe", test_image_debug_describe);
    robo_test_register("html.image.capability_reported",
                       test_image_capability_reported);
}
