/*
 * RoboPaint image suite (Phase 11.7) — paint.image.*
 *
 * The fourth drawing kind, and the first whose appearance this module does **not** resolve: a
 * background's colour comes from a style, a run's characters from a text node, and an image's
 * pixels from a **resource** the document attached to an element. Every claim the release makes
 * is pinned where it can be observed rather than argued:
 *
 *   **the resource is the document's**       an `<img>` whose `src` was resolved through
 *                                            RoboHTML's own fetch seam paints the pixels that
 *                                            were decoded — asserted at the pixel, against a
 *                                            surface this suite encoded itself;
 *
 *   **the destination is layout's**          the image's rectangle is the **content box**
 *                                            RoboLayout concluded for its element, checked
 *                                            against the tree with the box suites' own helper
 *                                            and then read back off the display command;
 *
 *   **the source is the whole image**        a payload names `(0, 0, width, height)` and says so
 *                                            (`robo_paint_image_is_full_source`); the scaling into
 *                                            the destination is the graphics module's
 *                                            nearest-neighbour sampler, pinned to the pixel at
 *                                            a ratio where the mapping is unmistakable;
 *
 *   **alpha survives**                       a source pixel with `a == 0` leaves the target
 *                                            untouched, one at `a == 255` writes its colour, and
 *                                            one in between blends — measured, not assumed;
 *
 *   **the order is CSS's**                   background, border, image: an image is a replaced
 *                                            element's content, so its record sits inside its own
 *                                            element's group and before that box's `CONTENT`;
 *
 *   **a missing image is a number**          a `src` that names nothing, a resource nothing
 *                                            resolved, bytes that failed to decode and a scheme
 *                                            this runtime refuses all paint **nothing** and are
 *                                            counted (`image_elements` against `images` and
 *                                            `image_skipped`) rather than crashing or drawing a
 *                                            placeholder this runtime does not have;
 *
 *   **the artifacts are snapshots**           mutating the DOM, re-resolving the element and
 *                                            releasing the document's own reference afterwards
 *                                            cannot change a record or a command, and clearing
 *                                            the record list cannot change a pixel the display
 *                                            list draws;
 *
 *   **identical input, identical output**     two passes over one document produce equal lists,
 *                                            equal dumps (with **no address** in them) and equal
 *                                            pixels.
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → records →
 * display list) and, where pixels are the question, paints it into a real RoboGraphics surface.
 * Hand-built records and commands appear only where the question is about the payload's own
 * contract — the validity rules, the constructors, the accessors — and never in place of a layout
 * result.
 *
 * The image bytes are produced by the real encoder (Goal 6.13) and decoded by the real decoder
 * (8.12), because that is what makes the pixel assertions meaningful: the suite knows the surface
 * it encoded, so it can compare the picture that came out of the document against the one that
 * went in. Nothing here is fetched, and no Chromium, Skia or Blink header, library or test
 * framework is involved.
 */

#include "RoboPaintTestSupport.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTag.h"

#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayDump.h"
#include "RoboPaint/RoboPaintDump.h"

#include <string.h>

/*
 * The same base sheet the other paint suites use, with `img` added as a **block** -- because what
 * this build paints is the content box layout produced, and a replaced element with no CSS size
 * is zero-sized until layout does intrinsic sizing (which it does not, and
 * `paint/docs/Images.md` says so).
 */
#define IMAGE_BASE_CSS \
    "html, body, div, section, p, span, img { display: block }"

/* The document every pixel case uses: a box, a picture in it, sized by the sheet. */
#define IMAGE_DOCUMENT \
    "<html><body><div class=\"box\"><img class=\"pic\" src=\"Assets/Logo.png\"></div></body></html>"

#define IMAGE_WHITE robo_gfx_rgba8(0xff, 0xff, 0xff, 255)

/* ------------------------------------------------------------------ */
/* A resource, encoded and decoded for real                            */
/* ------------------------------------------------------------------ */

/**
 * A 4x4 surface with a fixed, non-uniform pattern.
 *
 * Non-uniform on purpose: a sampler that used one pixel for the whole image, or transposed the
 * axes, would pass a solid fixture and fail this one. The values are `x * 60 + 1`,
 * `y * 60 + 2`, `(x + y) * 30 + 3`, so no two pixels are equal and none is a round number.
 */
static robo_gfx_surface_t *image_logo_surface(void)
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
            robo_gfx_color_rgba8_t color;

            color.r = (uint8_t)(x * 60u + 1u);
            color.g = (uint8_t)(y * 60u + 2u);
            color.b = (uint8_t)((x + y) * 30u + 3u);
            color.a = 255u;
            ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(surface, x, y, color), ROBO_OK);
        }
    }
    return surface;
}

/** The colour of @p surface at @p x,@p y, asserted to be readable. */
static robo_gfx_color_rgba8_t image_source_pixel(robo_gfx_surface_t *surface,
                                                 uint32_t x,
                                                 uint32_t y)
{
    robo_gfx_color_rgba8_t color;

    memset(&color, 0, sizeof(color));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, x, y, &color), ROBO_OK);
    return color;
}

static void image_color_eq(robo_gfx_color_rgba8_t actual,
                           robo_gfx_color_rgba8_t expected,
                           const char *what)
{
    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: expected rgba(%u,%u,%u,%u), got rgba(%u,%u,%u,%u)", what,
                       (unsigned)expected.r, (unsigned)expected.g, (unsigned)expected.b,
                       (unsigned)expected.a, (unsigned)actual.r, (unsigned)actual.g,
                       (unsigned)actual.b, (unsigned)actual.a);
    }
}

/**
 * One image document's resources: the surface encoded, the bytes it encoded to, and the asset
 * table those bytes are registered in.
 *
 * The encoded buffer must outlive the table and the table must outlive every resolver, which is
 * the lifetime contract `RoboHTMLImage.h` documents -- so they are bundled here and torn down in
 * reverse order.
 */
typedef struct image_assets {
    robo_gfx_surface_t *surface;
    robo_gfx_encoded_buffer_t encoded;
    robo_html_image_assets_t *assets;
} image_assets_t;

/** Encode @p surface to PNG and register it as `Assets/Logo.png` in a fresh table. */
static void image_assets_begin(image_assets_t *assets, robo_gfx_surface_t *surface)
{
    robo_gfx_encode_options_t options;

    memset(assets, 0, sizeof(*assets));
    assets->surface = surface;
    robo_gfx_encode_options_init(&options);
    options.container = ROBO_GFX_ENCODE_PNG;
    options.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    memset(&assets->encoded, 0, sizeof(assets->encoded));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_encode(surface, &options, &assets->encoded), ROBO_OK);
    ROBO_CHECK(assets->encoded.size > 0u);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_create(NULL, 0u, &assets->assets), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(assets->assets, "Assets/Logo.png",
                                                 assets->encoded.data, assets->encoded.size,
                                                 NULL),
                      ROBO_OK);
}

static void image_assets_end(image_assets_t *assets)
{
    robo_html_image_assets_destroy(assets->assets);
    assets->assets = NULL;
    robo_gfx_encoded_buffer_release(&assets->encoded);
    robo_gfx_surface_release(assets->surface);
    assets->surface = NULL;
}

/** An empty table: every reference resolves to `NOT_FOUND`, so the resource state is FAILED. */
static void image_assets_empty(image_assets_t *assets)
{
    memset(assets, 0, sizeof(*assets));
    ROBO_CHECK_EQ_INT(robo_html_image_assets_create(NULL, 0u, &assets->assets), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/** The element with class @p name, asserted to be a live element. */
static robo_html_node_handle_t image_element(paint_fixture_doc_t *fixture, const char *name)
{
    robo_html_node_handle_t element = paint_find(fixture, name);

    ROBO_CHECK_EQ_INT((int)robo_html_node_handle_is_wellformed(&element), 1);
    return element;
}

/**
 * Build @p html, resolve the image element with class @p name through @p assets, then lay the
 * document out.
 *
 * The resolve happens **before** the layout pass, which is both the order a real document loads
 * in and the one that keeps the test honest: the resource is attached to the element before
 * anything reads a box, so nothing here can be relying on a paint-time mutation to be visible.
 * Passing NULL @p assets skips the resolve entirely, which is the "a `src` nobody fetched" case.
 */
static void image_begin(paint_fixture_doc_t *fixture,
                        const char *html,
                        const char *css,
                        const char *name,
                        const image_assets_t *assets)
{
    paint_begin(fixture, html, css);
    if (assets != NULL) {
        robo_html_node_handle_t element = image_element(fixture, name);
        robo_html_image_resolver_t resolver = robo_html_image_assets_resolver(assets->assets);

        ROBO_CHECK_EQ_INT(robo_html_element_image_resolve(&element, &resolver, NULL), ROBO_OK);
    }
    paint_layout(fixture, 800, 600);
}

/** A surface the paint pass can write into. */
static robo_gfx_surface_t *image_surface(uint32_t width, uint32_t height)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surface), ROBO_OK);
    ROBO_CHECK(surface != NULL);
    return surface;
}

/** Fill @p surface with one colour, so "nothing was drawn here" is a colour and not a zero. */
static void image_fill(robo_gfx_surface_t *surface, robo_gfx_color_rgba8_t color)
{
    uint32_t x;
    uint32_t y;

    for (y = 0u; y < robo_gfx_surface_height(surface); y++) {
        for (x = 0u; x < robo_gfx_surface_width(surface); x++) {
            ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(surface, x, y, color), ROBO_OK);
        }
    }
}

/** Build a display list from @p records, asserting the build succeeds. */
static void image_build(const paint_result_t *records,
                        robo_paint_display_list_t **out_list,
                        robo_paint_display_list_stats_t *out_stats)
{
    robo_paint_display_list_builder_t *builder = NULL;
    robo_paint_diag_t diag;
    robo_result_t status;

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(out_list, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
    robo_paint_diag_reset(&diag);
    status = robo_paint_display_list_builder_build(builder, records->list, *out_list, out_stats,
                                                   &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the display build refused the records: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
    robo_paint_display_list_builder_destroy(builder);
}

/** Paint @p list into @p target, asserting the pass succeeds. No font is needed for images. */
static void image_paint(const robo_paint_display_list_t *list,
                        robo_gfx_surface_t *target,
                        robo_paint_decoration_stats_t *out_stats)
{
    robo_paint_diag_t diag;
    robo_result_t status;

    robo_paint_diag_reset(&diag);
    status = robo_paint_decoration_paint(list, target, NULL, out_stats, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the decoration pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** The @p nth command of kind @p kind, or 0 when there is none. */
static int image_command_of_kind(const robo_paint_display_list_t *list,
                                 robo_paint_display_command_kind_t kind,
                                 size_t nth,
                                 robo_paint_display_command_t *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            return 0;
        }
        if (command.kind != kind) {
            continue;
        }
        if (seen == nth) {
            if (out) {
                *out = command;
            }
            return 1;
        }
        seen++;
    }
    return 0;
}

/** The first image command, asserted to exist. */
static robo_paint_display_command_t image_require_command(const robo_paint_display_list_t *list)
{
    robo_paint_display_command_t command;

    robo_paint_display_command_init(&command);
    ROBO_CHECK_EQ_INT(image_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE, 0u,
                                            &command),
                      1);
    return command;
}

/** The index of the command that draws @p node's image, or `(size_t)-1`. */
static size_t image_index_of(const robo_paint_display_list_t *list,
                             robo_paint_display_command_kind_t kind,
                             robo_layout_node_id_t node)
{
    size_t index;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            break;
        }
        if (command.kind == kind && command.node == node) {
            return index;
        }
    }
    return (size_t)-1;
}

/** The whole dump of @p list, asserted to fit @p buffer. */
static void image_dump_records(const robo_paint_list_t *list, char *buffer, size_t capacity)
{
    size_t length = 0u;
    robo_result_t status;

    memset(buffer, 0, capacity);
    status = robo_paint_list_write(list, buffer, capacity, &length, NULL);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the record dump did not fit %zu bytes", capacity);
    }
}

static void image_dump_commands(const robo_paint_display_list_t *list,
                                char *buffer,
                                size_t capacity)
{
    size_t length = 0u;
    robo_result_t status;

    memset(buffer, 0, capacity);
    status = robo_paint_display_list_write(list, buffer, capacity, &length, NULL);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the command dump did not fit %zu bytes", capacity);
    }
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

/**
 * A resolved image produces exactly one record, and that record carries the resource's own facts.
 *
 * The identity is compared against the **resource's** own id rather than against a number written
 * here, because that is what makes the comparison a statement about the document: a suite that
 * hard-coded an id would pass for a payload that named a different picture.
 */
static void test_image_record_shape(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    image_assets_t assets;
    robo_paint_operation_t operation;
    robo_html_node_handle_t element;
    robo_html_image_t *resource = NULL;
    robo_gfx_image_t *graphics;
    robo_gfx_surface_t *surface = image_logo_surface();

    image_assets_begin(&assets, surface);
    image_begin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px; background-color: #3366cc }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    element = image_element(&fixture, "pic");
    ROBO_CHECK_EQ_INT(robo_html_element_image_get(&element, &resource, NULL), ROBO_OK);
    ROBO_CHECK(resource != NULL);

    paint_once(&fixture, &result);

    /* The counters: an image element was reached, and it drew. */
    ROBO_CHECK_EQ_INT((int)result.stats.image_elements, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.images, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.image_skipped, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.image_elements,
                      (int)(result.stats.images + result.stats.image_skipped));
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_IMAGE), 1);

    operation = paint_require(&result, ROBO_PAINT_OP_IMAGE);

    /* The payload, read through the record's own door rather than off the field. */
    {
        robo_paint_image_t payload = robo_paint_operation_image(&operation);

        ROBO_CHECK(robo_paint_image_is_cleared(&payload) == 0);
        ROBO_CHECK(robo_paint_image_is_valid(&payload) == 1);
        ROBO_CHECK(robo_paint_image_is_full_source(&payload) == 1);
        graphics = robo_html_image_graphics(resource);
        ROBO_CHECK(graphics != NULL);
        ROBO_CHECK_EQ_INT((int)robo_paint_image_identity(&payload),
                          (int)robo_gfx_image_id(graphics));
        ROBO_CHECK_EQ_INT((int)payload.width, 4);
        ROBO_CHECK_EQ_INT((int)payload.height, 4);
        ROBO_CHECK_EQ_INT((int)payload.format, (int)ROBO_GFX_FORMAT_RGBA8);
        ROBO_CHECK_EQ_INT((int)payload.source.x, 0);
        ROBO_CHECK_EQ_INT((int)payload.source.y, 0);
        ROBO_CHECK_EQ_INT((int)payload.source.width, 4);
        ROBO_CHECK_EQ_INT((int)payload.source.height, 4);
        /* And the accessor's own rule: a kind that is not an image answers "no resource". */
        {
            robo_paint_operation_t content = paint_require(&result, ROBO_PAINT_OP_CONTENT);
            robo_paint_image_t none = robo_paint_operation_image(&content);

            ROBO_CHECK(robo_paint_image_is_cleared(&none) == 1);
        }
    }

    /* No colour and no style: an image's appearance is its resource's pixels. */
    ROBO_CHECK_EQ_INT((int)operation.color.kind, (int)ROBO_CSS_COLOR_NONE);
    ROBO_CHECK_EQ_INT((int)operation.style, (int)ROBO_CSS_BORDER_STYLE_NONE);
    ROBO_CHECK(operation.node == paint_box(&fixture, "pic"));

    /* The boxes are layout's, through the assertion the box suites use. */
    paint_local_boxes_match_layout(&fixture, operation);
    paint_rect_is(0, 0, 64, 48, operation.boxes.content, "the image's content box");

    paint_result_dispose(&result);
    paint_end(&fixture);
    image_assets_end(&assets);
}

/* ------------------------------------------------------------------ */
/* The display command                                                 */
/* ------------------------------------------------------------------ */

/**
 * The record resolves to one `draw-image` command whose rectangle is the element's content box.
 *
 * This is the phase's integration claim in one case: **layout's** box becomes the record's
 * destination and the command's one rectangle, with the same resource named at both stages.
 */
static void test_image_command_and_destination(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    paint_result_t again;
    image_assets_t assets;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_operation_t operation;
    robo_paint_display_command_t command;
    robo_layout_node_id_t box;
    robo_gfx_surface_t *surface = image_logo_surface();

    image_assets_begin(&assets, surface);
    image_begin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px; padding: 5px }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    paint_once(&fixture, &result);

    memset(&stats, 0, sizeof(stats));
    image_build(&result, &list, &stats);

    box = paint_box(&fixture, "pic");
    operation = paint_require(&result, ROBO_PAINT_OP_IMAGE);

    ROBO_CHECK_EQ_INT((int)stats.images, 1);
    ROBO_CHECK_EQ_INT((int)stats.draws, (int)(stats.backgrounds + stats.borders + stats.texts +
                                              stats.images));
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count_of_kind(
                          list, ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE), 1);

    command = image_require_command(list);
    ROBO_CHECK(command.node == box);
    /* The command's one rectangle **is** the record's content box -- the derivation this phase
     * adds and the reason a consumer never has to choose between three rectangles. */
    paint_rect_is(operation.boxes.content.x, operation.boxes.content.y,
                  operation.boxes.content.width, operation.boxes.content.height, command.rect,
                  "the command's destination");
    /* And the payload travelled: the same resource, by identity. */
    {
        robo_paint_image_t payload = robo_paint_display_command_image(&command);
        robo_paint_image_t from_record = robo_paint_operation_image(&operation);

        ROBO_CHECK(robo_paint_image_equal(&payload, &from_record) == 1);
        ROBO_CHECK(robo_paint_image_is_full_source(&payload) == 1);
        ROBO_CHECK_EQ_INT((int)payload.source.width, 4);
        ROBO_CHECK_EQ_INT((int)payload.source.height, 4);
    }
    ROBO_CHECK_EQ_INT((int)robo_paint_display_command_is_drawing(&command), 1);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_command_is_structural(&command), 0);

    /* The same document paints twice: same records, same commands. */
    paint_once(&fixture, &again);
    ROBO_CHECK_EQ_INT(robo_paint_list_equal(result.list, again.list), 1);
    paint_result_dispose(&again);

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    image_assets_end(&assets);
}

/* ------------------------------------------------------------------ */
/* Order                                                               */
/* ------------------------------------------------------------------ */

/**
 * Background, border, image -- and the image inside its own element's group.
 *
 * The two placements are checked separately because they are two different facts: an image record
 * is emitted **after** the box's decorations and **before** its `CONTENT`, and the image **is** a
 * drawing of that box rather than of anything inside it.
 */
static void test_image_order(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    image_assets_t assets;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_layout_node_id_t box;
    robo_layout_node_id_t pic;
    size_t background;
    size_t content;
    size_t image;
    size_t end;
    size_t border;
    size_t pic_begin;
    size_t pic_content;
    size_t pic_end;
    robo_gfx_surface_t *surface = image_logo_surface();

    image_assets_begin(&assets, surface);
    image_begin(&fixture,
                "<html><body><div class=\"box\"><img class=\"pic\" src=\"Assets/Logo.png\">"
                "</div></body></html>",
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px; background-color: #3366cc;"
                " border: 3px solid #111111 }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    image_build(&result, &list, &stats);

    box = paint_box(&fixture, "box");
    pic = paint_box(&fixture, "pic");

    background = image_index_of(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, box);
    border = image_index_of(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER, box);
    content = image_index_of(list, ROBO_PAINT_DISPLAY_CMD_CONTENT, box);
    end = image_index_of(list, ROBO_PAINT_DISPLAY_CMD_END_NODE, box);
    image = image_index_of(list, ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE, pic);
    pic_begin = image_index_of(list, ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE, pic);
    pic_content = image_index_of(list, ROBO_PAINT_DISPLAY_CMD_CONTENT, pic);
    pic_end = image_index_of(list, ROBO_PAINT_DISPLAY_CMD_END_NODE, pic);

    ROBO_CHECK(background != (size_t)-1);
    ROBO_CHECK(border != (size_t)-1);
    ROBO_CHECK(content != (size_t)-1);
    ROBO_CHECK(end != (size_t)-1);
    ROBO_CHECK(image != (size_t)-1);
    ROBO_CHECK(pic_begin != (size_t)-1);
    ROBO_CHECK(pic_content != (size_t)-1);
    ROBO_CHECK(pic_end != (size_t)-1);

    /*
     * CSS's paint order, in two statements that are two different facts. The container's own
     * background and border come first, before anything its contents draw -- and inside the
     * image's **own** element's group the picture is drawn after that box's own records and
     * before its content boundary, because an image is its element's content rather than
     * something inside it. A `draw-text` is placed the other way round (a level below its box),
     * so this pair of inequalities is what distinguishes the two drawing kinds in a stream.
     */
    ROBO_CHECK(background < border);
    ROBO_CHECK(border < image);
    ROBO_CHECK(pic_begin < image);
    ROBO_CHECK(image < pic_content);
    ROBO_CHECK(pic_content < pic_end);
    /* And the image's box is a child of the container, so its whole group is inside the
     * container's own `[content .. end]` window. */
    ROBO_CHECK(content < pic_begin);
    ROBO_CHECK(pic_end < end);

    /* And the image names its **own** element's box, one level below the container. */
    {
        robo_paint_display_command_t command = image_require_command(list);
        robo_paint_display_command_t container;

        ROBO_CHECK(command.node == pic);
        robo_paint_display_command_init(&container);
        /* The first `begin-node` is the root's, and every level of the document adds one: the
         * image's box is a child of the container, which is why the record carries a depth its
         * own element's box owns rather than its parent's. */
        ROBO_CHECK_EQ_INT(image_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE, 0u,
                                                &container),
                          1);
        ROBO_CHECK(command.depth > container.depth);
    }

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    image_assets_end(&assets);
}

/**
 * Two images, two resources, two destinations -- and each command names its own.
 *
 * The second asset is a **different surface** encoded under a second name, so a builder that
 * resolved either reference to the first resource would be caught by the identity comparison
 * rather than by a pixel count.
 */
static void test_image_multiple(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    image_assets_t assets;
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *second = NULL;
    robo_gfx_encoded_buffer_t encoded;
    robo_gfx_encode_options_t options;
    robo_paint_operation_t first;
    robo_paint_operation_t other;
    robo_gfx_surface_t *surface = image_logo_surface();

    image_assets_begin(&assets, surface);
    /* A 2x2 surface, encoded under a second name. */
    robo_gfx_surface_desc_init(&desc);
    desc.width = 2u;
    desc.height = 2u;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &second), ROBO_OK);
    image_fill(second, robo_gfx_rgba8(0x10, 0x20, 0x30, 0xff));
    robo_gfx_encode_options_init(&options);
    options.container = ROBO_GFX_ENCODE_PNG;
    options.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    memset(&encoded, 0, sizeof(encoded));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_encode(second, &options, &encoded), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(assets.assets, "Assets/Small.png",
                                                 encoded.data, encoded.size, NULL),
                      ROBO_OK);

    paint_begin(&fixture,
                "<html><body><div class=\"box\">"
                "<img class=\"pic\" src=\"Assets/Logo.png\">"
                "<img class=\"pic2\" src=\"Assets/Small.png\">"
                "</div></body></html>",
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px }"
                " .pic { width: 64px; height: 48px }"
                " .pic2 { width: 8px; height: 8px }");
    {
        robo_html_image_resolver_t resolver = robo_html_image_assets_resolver(assets.assets);
        robo_html_node_handle_t first_element = image_element(&fixture, "pic");
        robo_html_node_handle_t second_element = image_element(&fixture, "pic2");

        ROBO_CHECK_EQ_INT(robo_html_element_image_resolve(&first_element, &resolver, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_element_image_resolve(&second_element, &resolver, NULL),
                          ROBO_OK);
    }
    paint_layout(&fixture, 800, 600);
    paint_once(&fixture, &result);

    ROBO_CHECK_EQ_INT((int)result.stats.image_elements, 2);
    ROBO_CHECK_EQ_INT((int)result.stats.images, 2);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_IMAGE), 2);

    first = paint_require(&result, ROBO_PAINT_OP_IMAGE);
    ROBO_CHECK_EQ_INT(paint_nth_of_kind(&result, ROBO_PAINT_OP_IMAGE, 1u, &other), 1);
    ROBO_CHECK(first.node == paint_box(&fixture, "pic"));
    ROBO_CHECK(other.node == paint_box(&fixture, "pic2"));
    ROBO_CHECK(robo_paint_operation_image(&first).identity !=
               robo_paint_operation_image(&other).identity);
    ROBO_CHECK_EQ_INT((int)robo_paint_operation_image(&first).width, 4);
    ROBO_CHECK_EQ_INT((int)robo_paint_operation_image(&other).width, 2);
    /* Two block-level pictures in flow order: the first at the container's content top, the
     * second immediately below it. The geometry is layout's, and this is the assertion that says
     * the destination really is **each element's own** content box rather than the container's. */
    paint_rect_is(0, 0, 64, 48, first.boxes.content, "the first image's content box");
    paint_rect_is(0, 48, 8, 8, other.boxes.content, "the second image's content box");

    paint_result_dispose(&result);
    paint_end(&fixture);
    robo_gfx_encoded_buffer_release(&encoded);
    robo_gfx_surface_release(second);
    image_assets_end(&assets);
}

/* ------------------------------------------------------------------ */
/* Pixels                                                              */
/* ------------------------------------------------------------------ */

/**
 * The visual smoke test: the document's picture reaches the surface.
 *
 * The destination is read off the **command** rather than written down here, so the case proves
 * the rectangle the pass used is the one the command states -- and the pixels inside it are the
 * source's own, scaled by the graphics module's nearest-neighbour mapping, while everything
 * outside it is still the page's colour.
 */
static void test_image_pixels(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    image_assets_t assets;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_decoration_stats_t decoration;
    robo_paint_display_command_t command;
    robo_gfx_surface_t *surface = image_logo_surface();
    robo_gfx_surface_t *target;
    uint32_t outside = 0u;
    uint32_t inside = 0u;
    uint32_t x;
    uint32_t y;

    image_assets_begin(&assets, surface);
    image_begin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px; background-color: #ffffff }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    image_build(&result, &list, &stats);

    command = image_require_command(list);
    target = image_surface(200u, 200u);
    image_fill(target, IMAGE_WHITE);

    memset(&decoration, 0, sizeof(decoration));
    image_paint(list, target, &decoration);

    ROBO_CHECK_EQ_INT((int)decoration.images, 1);
    ROBO_CHECK_EQ_INT((int)decoration.no_resource, 0);
    ROBO_CHECK_EQ_INT((int)decoration.commands,
                      (int)(decoration.draws + decoration.skipped));
    ROBO_CHECK_EQ_INT((int)decoration.draws,
                      (int)(decoration.backgrounds + decoration.borders + decoration.texts +
                            decoration.images));
    ROBO_CHECK_EQ_INT((int)decoration.draws,
                      (int)(decoration.filled + decoration.contextual + decoration.transparent +
                            decoration.empty + decoration.no_font + decoration.no_resource));
    /* The image counts as a drawing that put pixels down -- the reason `images` is a subset of
     * `filled` rather than a term of the partition. */
    ROBO_CHECK(decoration.images <= decoration.filled);

    /* Every pixel of the destination is the source's own colour, and every pixel outside the
     * page's. */
    for (y = 0u; y < 200u; y++) {
        for (x = 0u; x < 200u; x++) {
            robo_gfx_color_rgba8_t got;
            int in = (int32_t)x >= command.rect.x && (int32_t)x < command.rect.x + command.rect.width &&
                     (int32_t)y >= command.rect.y && (int32_t)y < command.rect.y + command.rect.height;

            ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(target, x, y, &got), ROBO_OK);
            if (in) {
                uint32_t sx = (uint32_t)(((int32_t)x - command.rect.x) * 4) / (uint32_t)command.rect.width;
                uint32_t sy = (uint32_t)(((int32_t)y - command.rect.y) * 4) / (uint32_t)command.rect.height;
                robo_gfx_color_rgba8_t want = image_source_pixel(surface, sx, sy);

                image_color_eq(got, want, "an image pixel");
                inside++;
            } else {
                image_color_eq(got, IMAGE_WHITE, "a pixel outside the image");
                outside++;
            }
        }
    }
    ROBO_CHECK_EQ_INT((int)inside, 64 * 48);
    ROBO_CHECK(outside > 0u);

    robo_gfx_surface_release(target);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    image_assets_end(&assets);
}

/**
 * Scaling: a 4x4 source into a 16x16 destination, one source pixel per four-by-four block.
 *
 * The ratio is chosen so the mapping is unambiguous: block `(bx, by)` of the destination must be
 * the source pixel `(bx, by)`, so a sampler that transposed the axes or rounded the wrong way
 * fails on the first block it reaches.
 */
static void test_image_scaling(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    image_assets_t assets;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t command;
    robo_gfx_surface_t *surface = image_logo_surface();
    robo_gfx_surface_t *target;
    uint32_t bx;
    uint32_t by;

    image_assets_begin(&assets, surface);
    image_begin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 100px; height: 100px }"
                " .pic { width: 16px; height: 16px }",
                "pic", &assets);
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    image_build(&result, &list, &stats);

    command = image_require_command(list);
    paint_rect_is(0, 0, 16, 16, command.rect, "the scaled destination");
    target = image_surface(16u, 16u);
    image_fill(target, IMAGE_WHITE);
    image_paint(list, target, NULL);

    for (by = 0u; by < 4u; by++) {
        for (bx = 0u; bx < 4u; bx++) {
            robo_gfx_color_rgba8_t want = image_source_pixel(surface, bx, by);

            /* Every pixel of the block, because a nearest-neighbour sampler has no interior to
             * interpolate and an off-by-one in the mapping shows up at the block's last column
             * and row. */
            image_color_eq(image_source_pixel(target, bx * 4u, by * 4u), want,
                           "a block's first pixel");
            image_color_eq(image_source_pixel(target, bx * 4u + 3u, by * 4u + 3u), want,
                           "a block's last pixel");
        }
    }

    robo_gfx_surface_release(target);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    image_assets_end(&assets);
}

/**
 * Alpha: a transparent source pixel writes nothing, an opaque one writes its colour, and a
 * half-transparent one blends.
 *
 * The three source pixels are in **one** image, over one opaque white destination, so the three
 * answers come from one draw and cannot be explained by a pass that skipped the image.
 */
static void test_image_alpha(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    image_assets_t assets;
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *source = NULL;
    robo_gfx_encoded_buffer_t encoded;
    robo_gfx_encode_options_t options;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t command;
    robo_gfx_surface_t *target;
    robo_gfx_color_rgba8_t got;
    robo_gfx_color_rgba8_t opaque = robo_gfx_rgba8(0xff, 0x00, 0x00, 255);
    robo_gfx_color_rgba8_t clear = robo_gfx_rgba8(0x00, 0xff, 0x00, 0);
    robo_gfx_color_rgba8_t half = robo_gfx_rgba8(0x00, 0x00, 0xff, 128);

    /* A 3x1 image: transparent, opaque, half. */
    robo_gfx_surface_desc_init(&desc);
    desc.width = 3u;
    desc.height = 1u;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &source), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(source, 0u, 0u, clear), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(source, 1u, 0u, opaque), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(source, 2u, 0u, half), ROBO_OK);

    image_assets_begin(&assets, source);
    robo_gfx_encode_options_init(&options);
    options.container = ROBO_GFX_ENCODE_PNG;
    options.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    memset(&encoded, 0, sizeof(encoded));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_encode(source, &options, &encoded), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(assets.assets, "Assets/Alpha.png",
                                                 encoded.data, encoded.size, NULL),
                      ROBO_OK);

    image_begin(&fixture,
                "<html><body><div class=\"box\">"
                "<img class=\"pic\" src=\"Assets/Alpha.png\"></div></body></html>",
                IMAGE_BASE_CSS
                " .box { width: 100px; height: 100px }"
                " .pic { width: 3px; height: 1px }",
                "pic", &assets);
    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)result.stats.images, 1);
    memset(&stats, 0, sizeof(stats));
    image_build(&result, &list, &stats);

    command = image_require_command(list);
    paint_rect_is(0, 0, 3, 1, command.rect, "the alpha fixture's destination");
    target = image_surface(3u, 1u);
    image_fill(target, IMAGE_WHITE);
    image_paint(list, target, NULL);

    /* Transparent: the destination is exactly what it was. */
    image_color_eq(image_source_pixel(target, 0u, 0u), IMAGE_WHITE, "a transparent source pixel");
    /* Opaque: the source's own colour, alpha and all. */
    image_color_eq(image_source_pixel(target, 1u, 0u), opaque, "an opaque source pixel");
    /* Half: between the two, and neither endpoint -- which is the whole of what alpha means. */
    got = image_source_pixel(target, 2u, 0u);
    ROBO_CHECK(got.r > 0u && got.r < 255u);
    ROBO_CHECK(got.g > 0u && got.g < 255u);
    ROBO_CHECK(got.b == 255u);

    robo_gfx_surface_release(target);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    robo_gfx_encoded_buffer_release(&encoded);
    image_assets_end(&assets);
}

/**
 * A destination with no area is a counted drawing rather than a failure.
 *
 * `width: 0` is a document a browser paints nothing for and this build must too -- and the two
 * counters that describe it are different facts: the traversal emitted a record (the resource is
 * fine), and the pass counted it as `empty` (there is nowhere to put it). Nothing crashes, and
 * the record and command both exist.
 */
static void test_image_empty_destination(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    image_assets_t assets;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_decoration_stats_t decoration;
    robo_gfx_surface_t *surface = image_logo_surface();
    robo_gfx_surface_t *target;

    image_assets_begin(&assets, surface);
    image_begin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 50px; height: 50px }"
                " .pic { width: 0px; height: 0px }",
                "pic", &assets);
    paint_once(&fixture, &result);

    ROBO_CHECK_EQ_INT((int)result.stats.image_elements, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.images, 1);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_IMAGE), 1);

    memset(&stats, 0, sizeof(stats));
    image_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.images, 1);

    target = image_surface(8u, 8u);
    image_fill(target, IMAGE_WHITE);
    memset(&decoration, 0, sizeof(decoration));
    image_paint(list, target, &decoration);
    ROBO_CHECK_EQ_INT((int)decoration.images, 1);
    ROBO_CHECK_EQ_INT((int)decoration.empty, 1);
    ROBO_CHECK_EQ_INT((int)decoration.filled, 0);
    /* Nothing was drawn: the surface is still the page colour. */
    image_color_eq(image_source_pixel(target, 0u, 0u), IMAGE_WHITE, "an empty destination");

    robo_gfx_surface_release(target);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    image_assets_end(&assets);
}

/* ------------------------------------------------------------------ */
/* Missing images                                                      */
/* ------------------------------------------------------------------ */

/**
 * Every way an image can be absent is a **number**, not a crash and not a placeholder.
 *
 * Four documents, four reasons, one answer each: a `src` attribute nobody wrote, a `src` nothing
 * resolved, a reference the resolver could not find, and a scheme this runtime refuses. Each
 * leaves the element's resource in a state that is legitimate, each produces **no** record, and
 * each is counted -- so a reader can tell a document with no images from one whose images drew
 * nothing.
 */
static void test_image_missing(void)
{
    static const struct {
        const char *what;
        const char *html;
        int resolve;
    } cases[] = {
        { "no source",
          "<html><body><div class=\"box\"><img class=\"pic\" alt=\"nothing\"></div></body></html>",
          0 },
        { "never resolved",
          "<html><body><div class=\"box\"><img class=\"pic\" src=\"Assets/Logo.png\"></div>"
          "</body></html>",
          0 },
        { "not found",
          "<html><body><div class=\"box\"><img class=\"pic\" src=\"Assets/Missing.png\"></div>"
          "</body></html>",
          1 },
        { "refused scheme",
          "<html><body><div class=\"box\"><img class=\"pic\" "
          "src=\"https://example.com/Logo.png\"></div></body></html>",
          1 }
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        paint_fixture_doc_t fixture;
        paint_result_t result;
        image_assets_t assets;
        robo_html_node_handle_t element;

        image_assets_empty(&assets);
        image_begin(&fixture, cases[index].html,
                    IMAGE_BASE_CSS
                    " .box { width: 200px; height: 100px; background-color: #3366cc }"
                    " .pic { width: 64px; height: 48px }",
                    "pic", cases[index].resolve ? &assets : NULL);

        /* The resource state said so before the paint pass ever ran. */
        element = image_element(&fixture, "pic");
        if (strcmp(cases[index].what, "no source") == 0) {
            ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&element),
                              (int)ROBO_HTML_IMAGE_STATE_NONE);
        } else if (cases[index].resolve) {
            robo_html_image_state_t state =
                robo_html_element_image_state(&element);

            ROBO_CHECK(state == (strcmp(cases[index].what, "not found") == 0
                                     ? ROBO_HTML_IMAGE_STATE_FAILED
                                     : ROBO_HTML_IMAGE_STATE_UNSUPPORTED));
        } else {
            ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&element),
                              (int)ROBO_HTML_IMAGE_STATE_PENDING);
        }

        paint_once(&fixture, &result);
        /* Every one of the four reasons is still an **image element**: the traversal reached it
         * and counted it, which is what tells a reader the document asked for a picture. */
        ROBO_CHECK_EQ_INT((int)result.stats.image_elements, 1);
        ROBO_CHECK_EQ_INT((int)result.stats.images, 0);
        ROBO_CHECK_EQ_INT((int)result.stats.image_skipped, 1);
        ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_IMAGE), 0);
        /* The box's own background still drew, so the document was painted rather than skipped. */
        ROBO_CHECK_EQ_INT((int)result.stats.backgrounds, 1);

        paint_result_dispose(&result);
        paint_end(&fixture);
        image_assets_end(&assets);
    }
}

/**
 * A document with no `<img>` at all leaves all three counters at zero.
 *
 * The other half of the pair above, and the reason `image_elements` exists: without it, "the
 * document has no images" and "every image failed" would be the same silence.
 */
static void test_image_none(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\">nothing to see</div></body></html>",
                         IMAGE_BASE_CSS " .box { width: 100px; height: 40px }");
    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)result.stats.image_elements, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.images, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.image_skipped, 0);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_IMAGE), 0);
    ROBO_CHECK(result.stats.boxes > 0u);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The payload's own contract                                          */
/* ------------------------------------------------------------------ */

/**
 * The constructors, the predicates and the validity rules, on hand-built values.
 *
 * The only case here that does not go through a document, because the question is about the
 * **payload's** contract rather than about a traversal: what `robo_paint_image_from_resource`
 * refuses, what a record requires of its payload, and what the accessors answer for a kind that is
 * not an image.
 */
static void test_image_hand_built_rules(void)
{
    robo_gfx_surface_desc_t surface_desc;
    robo_gfx_image_desc_t image_desc;
    robo_gfx_surface_t *surface = NULL;
    robo_gfx_image_t *raster = NULL;
    robo_gfx_image_t *metadata = NULL;
    robo_paint_image_t payload;
    robo_paint_image_t copy;
    robo_paint_image_t other;
    robo_paint_operation_t operation;
    robo_paint_diag_t diag;

    /* A resource with pixels, and one without: an `EMPTY` image is a legitimate RoboGraphics
     * object and an impossible drawing. */
    robo_gfx_surface_desc_init(&surface_desc);
    surface_desc.width = 2u;
    surface_desc.height = 3u;
    surface_desc.format = ROBO_GFX_FORMAT_RGBA8;
    surface_desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&surface_desc, &surface), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_image_create_from_surface(surface, &raster), ROBO_OK);
    robo_gfx_surface_release(surface);
    robo_gfx_image_desc_init(&image_desc);
    image_desc.width = 2u;
    image_desc.height = 3u;
    image_desc.format = ROBO_GFX_FORMAT_RGBA8;
    image_desc.kind = ROBO_GFX_IMAGE_KIND_EMPTY;
    ROBO_CHECK_EQ_INT(robo_gfx_image_create_empty(&image_desc, &metadata), ROBO_OK);

    /* A cleared payload: names nothing, is not valid, and every door answers safely. */
    robo_paint_image_init(&payload);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_cleared(&payload), 1);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_valid(&payload), 0);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_full_source(&payload), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_image_identity(&payload), 0);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_cleared(NULL), 1);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_full_source(NULL), 0);

    /* The constructor's refusals, then its answer. */
    ROBO_CHECK_EQ_INT(robo_paint_image_from_resource(NULL, &payload, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_image_from_resource(raster, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_paint_image_from_resource(metadata, &payload, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_paint_image_from_resource(raster, &payload, &diag), ROBO_OK);

    /* What it filled: the resource's own facts, and the full source. */
    ROBO_CHECK_EQ_INT(robo_paint_image_is_valid(&payload), 1);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_full_source(&payload), 1);
    ROBO_CHECK_EQ_INT((int)payload.width, 2);
    ROBO_CHECK_EQ_INT((int)payload.height, 3);
    ROBO_CHECK_EQ_INT((int)payload.source.width, 2);
    ROBO_CHECK_EQ_INT((int)payload.source.height, 3);

    /* Equality is by identity and by rectangle, and a record's copy is equal to its original. */
    copy = payload;
    ROBO_CHECK_EQ_INT(robo_paint_image_equal(&payload, &copy), 1);
    other = payload;
    other.identity = payload.identity + 1u;
    ROBO_CHECK_EQ_INT(robo_paint_image_equal(&payload, &other), 0);
    other = payload;
    other.source.width = 1;
    ROBO_CHECK_EQ_INT(robo_paint_image_equal(&payload, &other), 0);
    ROBO_CHECK_EQ_INT(robo_paint_image_equal(NULL, NULL), 1);

    /* A crop that runs off the resource is refused, and a full source that no longer matches the
     * dimensions is neither full nor valid. */
    other = payload;
    other.source = robo_gfx_rect(1, 0, 2, 1);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_valid(&other), 0);
    other = payload;
    other.source = robo_gfx_rect(0, 0, 1, 1);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_valid(&other), 1);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_full_source(&other), 0);
    other = payload;
    other.source = robo_gfx_rect(0, 0, 0, 0);
    ROBO_CHECK_EQ_INT(robo_paint_image_is_valid(&other), 0);

    /*
     * An `IMAGE` record requires a payload that draws, no colour and no style. The boxes are set
     * by hand as three nested, non-negative rectangles -- a record with un-nested boxes is refused
     * before its payload is ever read, which would make every assertion below pass for the wrong
     * reason.
     */
    robo_paint_operation_init(&operation);
    operation.kind = ROBO_PAINT_OP_IMAGE;
    operation.node = 1u;
    operation.boxes.border = robo_gfx_rect(0, 0, 10, 10);
    operation.boxes.padding = robo_gfx_rect(1, 1, 8, 8);
    operation.boxes.content = robo_gfx_rect(2, 2, 6, 6);
    operation.boxes.insets_measured = 1;
    ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&operation.boxes), 1);
    operation.origin = robo_gfx_point(0, 0);
    robo_paint_clip_init(&operation.clip);
    robo_paint_opacity_init(&operation.opacity);
    operation.color = robo_css_color_none();
    operation.style = ROBO_CSS_BORDER_STYLE_NONE;
    operation.image = payload;
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);
    {
        robo_paint_image_t read = robo_paint_operation_image(&operation);

        ROBO_CHECK(robo_paint_image_is_cleared(&read) == 0);
        ROBO_CHECK(robo_paint_image_equal(&read, &payload) == 1);
    }

    /* Cleared payload: refused. */
    operation.image = copy;
    robo_paint_image_init(&operation.image);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 0);
    /* A colour on an image record: refused, because its appearance is not a colour. */
    operation.image = payload;
    operation.color = robo_css_color_make(0xffu, 0x00u, 0x00u, 0xffu);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 0);
    /* A style on an image record: refused, for the same reason one field along. */
    operation.color = robo_css_color_none();
    operation.style = ROBO_CSS_BORDER_STYLE_SOLID;
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 0);
    /* A background carrying a payload: refused from the other side. */
    operation.style = ROBO_CSS_BORDER_STYLE_NONE;
    operation.kind = ROBO_PAINT_OP_BACKGROUND;
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 0);
    operation.kind = ROBO_PAINT_OP_IMAGE;
    operation.image = payload;
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);

    /* The vocabularies: an image draws, and it is not a kind whose look is a colour. */
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_draws(ROBO_PAINT_OP_IMAGE), 1);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_uses_color(ROBO_PAINT_OP_IMAGE), 0);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_structural(ROBO_PAINT_OP_IMAGE), 0);
    ROBO_CHECK_EQ_INT(robo_paint_op_kind_is_border(ROBO_PAINT_OP_IMAGE), 0);
    ROBO_CHECK(strcmp(robo_paint_op_kind_name(ROBO_PAINT_OP_IMAGE), "image") == 0);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_kind_draws(
                          ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE),
                      1);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_kind_uses_color(
                          ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE),
                      0);
    ROBO_CHECK(strcmp(robo_paint_display_command_kind_name(ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE),
                      "draw-image") == 0);

    /* And the display command's own rule, from both ends. */
    {
        robo_paint_display_command_t command;

        robo_paint_display_command_init(&command);
        command.kind = ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE;
        command.node = 1u;
        command.rect = robo_gfx_rect(0, 0, 10, 10);
        robo_paint_clip_init(&command.clip);
        robo_paint_opacity_init(&command.opacity);
        ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 0);
        robo_paint_image_t read;

        command.image = payload;
        ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);
        read = robo_paint_display_command_image(&command);
        ROBO_CHECK(robo_paint_image_equal(&read, &payload) == 1);
        command.color = robo_css_color_make(0x00u, 0x00u, 0xffu, 0xffu);
        ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 0);
        /* The accessor's rule for a kind that is not `draw-image`. */
        command.kind = ROBO_PAINT_DISPLAY_CMD_CONTENT;
        command.color = robo_css_color_none();
        read = robo_paint_display_command_image(&command);
        ROBO_CHECK(robo_paint_image_is_cleared(&read) == 1);
        ROBO_CHECK(robo_paint_image_equal(&read, &payload) == 0);
    }

    robo_gfx_image_release(raster);
    robo_gfx_image_release(metadata);
}

/* ------------------------------------------------------------------ */
/* Ownership and isolation                                             */
/* ------------------------------------------------------------------ */

/**
 * One whole ownership cycle: build a picture, paint it, let the document go, destroy the records,
 * paint again, tear everything down. Returns the live block count **after** the teardown.
 *
 * The cycle exists as a function so the case below can run it twice and compare the two ends: a
 * single "back to the baseline" assertion would be measuring the harness as much as the module, and
 * what the phase actually owes is that repeating a cycle does not grow the heap. Everything the
 * module owns -- the encoded bytes, the decoded resource, the asset table, the record list, the
 * display list, the surfaces -- is created and released inside one call, so a missing release shows
 * up as a difference between two identical cycles.
 */
static size_t image_lifetime_cycle(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    image_assets_t assets;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t command;
    robo_html_node_handle_t element;
    robo_gfx_surface_t *surface = image_logo_surface();
    robo_gfx_surface_t *target;
    robo_gfx_color_rgba8_t expected;

    image_assets_begin(&assets, surface);
    image_begin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    image_build(&result, &list, &stats);
    command = image_require_command(list);

    target = image_surface(64u, 48u);
    image_fill(target, IMAGE_WHITE);
    image_paint(list, target, NULL);
    expected = image_source_pixel(target, 0u, 0u);
    {
        robo_gfx_color_rgba8_t want = image_source_pixel(surface, 0u, 0u);

        image_color_eq(expected, want, "the first paint");
    }

    /*
     * The document lets its reference go -- and the two lists keep theirs. This is the whole of
     * the phase's ownership rule in three lines: `detach` releases what the element (and so the
     * DOM) owed, and what is still drawing afterwards is the reference the containers took.
     */
    element = image_element(&fixture, "pic");
    ROBO_CHECK_EQ_INT(robo_html_element_image_detach(&element, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_html_element_image_state(&element),
                      (int)ROBO_HTML_IMAGE_STATE_PENDING);

    /* The record list is destroyed: the display list is not a view of it. */
    paint_result_dispose(&result);
    {
        robo_gfx_surface_t *again = image_surface(64u, 48u);

        image_fill(again, IMAGE_WHITE);
        image_paint(list, again, NULL);
        image_color_eq(image_source_pixel(again, 0u, 0u), expected,
                       "the same pixel after the DOM and the records were done with it");
        robo_gfx_surface_release(again);
    }
    ROBO_CHECK_EQ_INT((int)robo_paint_display_command_is_valid(&command), 1);

    robo_gfx_surface_release(target);
    robo_paint_display_list_destroy(list);
    paint_end(&fixture);
    image_assets_end(&assets);
    return paint_live_blocks();
}

/**
 * The resources a record list and a display list hold outlive the document that named them, and
 * repeating the whole cycle does not grow the heap.
 *
 * Two claims in one case because they are one question asked from two sides: the first cycle
 * proves the references are real (the picture is still drawn after the DOM released it and the
 * records were destroyed), and the comparison of the two ends proves they are **given back** -- a
 * container that retained without releasing would pass the first and fail the second.
 */
static void test_image_resource_lifetime(void)
{
    size_t first;
    size_t second;

    /* Bring the modules up before the first cycle, so neither count includes a one-off
     * initialisation that only the first cycle would pay. */
    paint_fixture();
    first = image_lifetime_cycle();
    second = image_lifetime_cycle();
    ROBO_CHECK_EQ_INT((int)second, (int)first);
}

/**
 * Mutating the document cannot change a record or a command.
 *
 * After the pass, the element's `src` is rewritten and the picture re-resolved to a **different**
 * image, and the payload the record and the command carry is still the first one -- compared by
 * identity, which is the answer a suite can check without knowing an address.
 */
static void test_image_mutation_isolation(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    image_assets_t assets;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_operation_t operation;
    robo_paint_display_command_t command;
    robo_html_node_handle_t element;
    robo_gfx_image_id_t before;
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *other_surface = NULL;
    robo_gfx_encoded_buffer_t encoded;
    robo_gfx_encode_options_t options;
    robo_gfx_surface_t *surface = image_logo_surface();

    image_assets_begin(&assets, surface);
    /* A second asset with **different dimensions**, so a re-resolve is unmistakable. */
    robo_gfx_surface_desc_init(&desc);
    desc.width = 8u;
    desc.height = 8u;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &other_surface), ROBO_OK);
    image_fill(other_surface, robo_gfx_rgba8(0x44, 0x55, 0x66, 0xff));
    robo_gfx_encode_options_init(&options);
    options.container = ROBO_GFX_ENCODE_PNG;
    options.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    memset(&encoded, 0, sizeof(encoded));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_encode(other_surface, &options, &encoded), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(assets.assets, "Assets/Other.png",
                                                 encoded.data, encoded.size, NULL),
                      ROBO_OK);

    image_begin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    image_build(&result, &list, &stats);
    command = image_require_command(list);
    operation = paint_require(&result, ROBO_PAINT_OP_IMAGE);
    {
        robo_paint_image_t payload = robo_paint_operation_image(&operation);

        before = robo_paint_image_identity(&payload);
    }

    /* Mutate the document and re-resolve it to a different picture. */
    element = image_element(&fixture, "pic");
    ROBO_CHECK_EQ_INT(robo_html_element_set_attribute(&element, "src", "Assets/Other.png", NULL),
                      ROBO_OK);
    {
        robo_html_image_resolver_t resolver = robo_html_image_assets_resolver(assets.assets);

        ROBO_CHECK_EQ_INT(robo_html_element_image_resolve(&element, &resolver, NULL), ROBO_OK);
    }
    {
        robo_html_image_t *now = NULL;
        uint32_t width = 0u;
        uint32_t height = 0u;

        ROBO_CHECK_EQ_INT(robo_html_element_image_get(&element, &now, NULL), ROBO_OK);
        ROBO_CHECK(now != NULL);
        if (now) {
            /* The element really did change picture: 8x8, and a different resource. */
            ROBO_CHECK_EQ_INT(robo_html_image_intrinsic_size(now, &width, &height), 1);
            ROBO_CHECK_EQ_INT((int)width, 8);
            ROBO_CHECK_EQ_INT((int)height, 8);
            ROBO_CHECK(robo_gfx_image_id(robo_html_image_graphics(now)) != before);
        }
    }

    /* The record and the command still name the first picture, and still say 4x4. */
    {
        robo_paint_image_t from_record = robo_paint_operation_image(&operation);
        robo_paint_image_t from_command = robo_paint_display_command_image(&command);

        ROBO_CHECK(robo_paint_image_identity(&from_record) == before);
        ROBO_CHECK(robo_paint_image_identity(&from_command) == before);
        ROBO_CHECK_EQ_INT((int)from_command.width, 4);
        ROBO_CHECK_EQ_INT((int)from_command.height, 4);
    }

    /* And a list rebuilt from the same records is equal to the one built before the mutation. */
    {
        robo_paint_display_list_t *again = NULL;
        robo_paint_display_list_stats_t again_stats;

        memset(&again_stats, 0, sizeof(again_stats));
        image_build(&result, &again, &again_stats);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(list, again), 1);
        robo_paint_display_list_destroy(again);
    }

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    robo_gfx_encoded_buffer_release(&encoded);
    robo_gfx_surface_release(other_surface);
    image_assets_end(&assets);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

/**
 * One document, two passes: equal records, equal commands, equal dumps, equal pixels.
 *
 * The display list is built from **each** pass's records and compared, which is the stronger
 * statement: a builder that read anything mutable would agree with itself here but not with the
 * other list. The dump is compared too, and asserted to contain no address -- the one thing in
 * this module that would differ between two runs of one document.
 */
static void test_image_determinism(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t first;
    paint_result_t second;
    image_assets_t assets;
    robo_paint_display_list_t *list_a = NULL;
    robo_paint_display_list_t *list_b = NULL;
    robo_paint_display_list_stats_t stats_a;
    robo_paint_display_list_stats_t stats_b;
    robo_gfx_surface_t *surface = image_logo_surface();
    char dump_a[4096];
    char dump_b[4096];
    char commands_a[4096];
    char commands_b[4096];

    image_assets_begin(&assets, surface);
    image_begin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px; background-color: #ffffff }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    paint_once(&fixture, &first);
    paint_once(&fixture, &second);

    ROBO_CHECK_EQ_INT(robo_paint_list_equal(first.list, second.list), 1);
    ROBO_CHECK_EQ_INT((int)first.stats.images, (int)second.stats.images);
    image_dump_records(first.list, dump_a, sizeof(dump_a));
    image_dump_records(second.list, dump_b, sizeof(dump_b));
    ROBO_CHECK_STR_EQ(dump_a, dump_b);
    ROBO_CHECK(strstr(dump_a, "image") != NULL);
    /* **No address**: a dump with a pointer in it would differ between two runs of one
     * document, which is the property the whole module is built to have. */
    ROBO_CHECK(strstr(dump_a, "0x") == NULL);

    memset(&stats_a, 0, sizeof(stats_a));
    memset(&stats_b, 0, sizeof(stats_b));
    image_build(&first, &list_a, &stats_a);
    image_build(&second, &list_b, &stats_b);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(list_a, list_b), 1);
    ROBO_CHECK_EQ_INT((int)stats_a.images, (int)stats_b.images);
    image_dump_commands(list_a, commands_a, sizeof(commands_a));
    image_dump_commands(list_b, commands_b, sizeof(commands_b));
    ROBO_CHECK_STR_EQ(commands_a, commands_b);
    ROBO_CHECK(strstr(commands_a, "draw-image") != NULL);
    ROBO_CHECK(strstr(commands_a, "0x") == NULL);

    /* And the pixels, which is the claim made concrete. */
    {
        robo_gfx_surface_t *target_a = image_surface(80u, 80u);
        robo_gfx_surface_t *target_b = image_surface(80u, 80u);
        uint32_t x;
        uint32_t y;

        image_fill(target_a, IMAGE_WHITE);
        image_fill(target_b, IMAGE_WHITE);
        image_paint(list_a, target_a, NULL);
        image_paint(list_b, target_b, NULL);
        for (y = 0u; y < 80u; y++) {
            for (x = 0u; x < 80u; x++) {
                robo_gfx_color_rgba8_t one;
                robo_gfx_color_rgba8_t two;

                ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(target_a, x, y, &one), ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(target_b, x, y, &two), ROBO_OK);
                image_color_eq(two, one, "a pixel from the second pass");
            }
        }
        robo_gfx_surface_release(target_a);
        robo_gfx_surface_release(target_b);
    }

    robo_paint_display_list_destroy(list_a);
    robo_paint_display_list_destroy(list_b);
    paint_result_dispose(&first);
    paint_result_dispose(&second);
    paint_end(&fixture);
    image_assets_end(&assets);
}

/* ------------------------------------------------------------------ */
/* The dumps                                                           */
/* ------------------------------------------------------------------ */

/**
 * The dumps describe the image, and describe it without an address.
 *
 * The record's line names the resource, the source rectangle and the resource's own size; the
 * command's names the same resource and the destination it resolved. Both are read here as text,
 * because that is what a person diffs -- and a dump that printed a pointer would be a diff that
 * changed for no reason.
 */
static void test_image_dump(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    image_assets_t assets;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_operation_t operation;
    char records[4096];
    char commands[4096];
    char line[512];
    size_t length = 0u;
    robo_gfx_surface_t *surface = image_logo_surface();

    image_assets_begin(&assets, surface);
    image_begin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    image_build(&result, &list, &stats);
    operation = paint_require(&result, ROBO_PAINT_OP_IMAGE);

    image_dump_records(result.list, records, sizeof(records));
    ROBO_CHECK(strstr(records, "image") != NULL);
    ROBO_CHECK(strstr(records, "src=0,0,4x4") != NULL);
    ROBO_CHECK(strstr(records, "source_size=4x4") != NULL);
    ROBO_CHECK(strstr(records, "resource=") != NULL);
    ROBO_CHECK(strstr(records, "0x") == NULL);

    image_dump_commands(list, commands, sizeof(commands));
    ROBO_CHECK(strstr(commands, "draw-image") != NULL);
    ROBO_CHECK(strstr(commands, "resource=") != NULL);
    /* The destination is the command's `rect`, printed the same way for every kind. */
    ROBO_CHECK(strstr(commands, "rect=0,0,64,48") != NULL);

    /* One record's own line, through its writer. */
    {
        robo_paint_diag_t diag;

        robo_paint_diag_reset(&diag);
        ROBO_CHECK_EQ_INT(robo_paint_operation_write(&operation, line, sizeof(line), &length,
                                                     &diag),
                          ROBO_OK);
        ROBO_CHECK(strstr(line, "image") != NULL);
        ROBO_CHECK(strstr(line, "src=") != NULL);
    }

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    image_assets_end(&assets);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_image(void)
{
    robo_test_register("paint.image.record_shape", test_image_record_shape);
    robo_test_register("paint.image.command_and_destination", test_image_command_and_destination);
    robo_test_register("paint.image.order", test_image_order);
    robo_test_register("paint.image.multiple", test_image_multiple);
    robo_test_register("paint.image.pixels", test_image_pixels);
    robo_test_register("paint.image.scaling", test_image_scaling);
    robo_test_register("paint.image.alpha", test_image_alpha);
    robo_test_register("paint.image.empty_destination", test_image_empty_destination);
    robo_test_register("paint.image.missing", test_image_missing);
    robo_test_register("paint.image.none", test_image_none);
    robo_test_register("paint.image.hand_built_rules", test_image_hand_built_rules);
    robo_test_register("paint.image.resource_lifetime", test_image_resource_lifetime);
    robo_test_register("paint.image.mutation_isolation", test_image_mutation_isolation);
    robo_test_register("paint.image.determinism", test_image_determinism);
    robo_test_register("paint.image.dump", test_image_dump);
}
