/*
 * RoboPaint border suite (Phase 11.5) — paint.border.*
 *
 * The second drawing this module does, and the first one with a **shape**. The claims the
 * release makes, each pinned where it can be observed rather than argued:
 *
 *   **the style travelled**   a border side's `border-style` reaches the display command, and
 *                             the case asserts it end to end -- the record's own accessor, the
 *                             command's field -- because the fact that a `dashed` band is not a
 *                             `solid` one is not derivable from a band's rectangle, so dropping
 *                             it one stage early would be a silent loss of the only thing that
 *                             makes a band render-ready;
 *
 *   **the band's geometry**   the four sides of a box are **bands**: the region of the border
 *                             box outside the padding box on that edge. Each case computes its
 *                             expectation from the box's own border box and its declared
 *                             insets rather than from absolutes, so the assertion is about the
 *                             derivation and not about this build's default margin;
 *
 *   **pixels**                every band is filled where the display list said it is, in the
 *                             side's own colour, asserted by reading the surface back -- and
 *                             the corner belongs to the side drawn **last**, which is the one
 *                             place the four-band order is observable as a colour;
 *
 *   **one pass, one order**   the decisive case is a relatively positioned child that spills
 *                             over its parent's border: the child's background is filled
 *                             **after** the parent's bands, because the list's order is the
 *                             traversal's and CSS paints a descendant's background over its
 *                             ancestor's border. Two passes would fill every background in the
 *                             document before every border and get exactly this wrong;
 *
 *   **pattern styles**        `dashed`, `dotted`, `double`, `groove`, `ridge`, `inset` and
 *                             `outset` are drawn as their **band**, in the side's colour, and
 *                             counted (`unshaped`) rather than silently reported as shaped --
 *                             because a border drawn as a solid band when the author asked for
 *                             dots is the kind of quiet wrongness this module counts instead of
 *                             shipping;
 *
 *   **snapshot**              once a display list exists, clearing the record list and laying
 *                             the tree out again does not change a single pixel it paints.
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → records →
 * display list) and then paints it into a real RoboGraphics surface. Hand-built *commands*
 * appear only where the question is about the painter's own contract -- the style rule, the
 * counters, the clip application -- and never in place of a layout result.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on no
 * Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboCSS/RoboCSSBox.h"
#include "RoboGraphics/RoboGraphicsBitmap.h"

#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayDump.h"

#include <string.h>

/* The same base sheet the other paint suites use: these elements are the block containers (so a
 * fixture's boxes stack in flow order), and `<head>` stays the one box nothing styles. */
#define BORDER_BASE_CSS \
    "html, body, div, section, aside, figure, p { display: block }"

/* The near-black a band usually is, and a colour a pixel assertion can name. */
#define BORDER_BLACK robo_gfx_rgba8(0x11, 0x11, 0x11, 255)
#define BORDER_RED robo_gfx_rgba8(0xff, 0x00, 0x00, 255)
#define BORDER_GREEN robo_gfx_rgba8(0x00, 0xff, 0x00, 255)

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/**
 * Compose a stylesheet from @p declaration in @p buffer.
 *
 * The clip and absent-side cases differ in **one declaration** and in nothing else, so the
 * document is built from a part that never changes and a part that is the whole difference --
 * a reader can see exactly what varies. Hand-built rather than with `stdio`, because the
 * runtime's guest target has none and these suites compile for it.
 */
static const char *border_sheet(char *buffer,
                                size_t capacity,
                                const char *declaration)
{
    const char *parts[3];
    size_t length = 0u;
    size_t part;

    parts[0] = BORDER_BASE_CSS " .box { width: 60px; height: 30px; ";
    parts[1] = declaration;
    parts[2] = " }";

    buffer[0] = '\0';
    for (part = 0u; part < 3u; part++) {
        const char *cursor = parts[part];

        while (*cursor != '\0' && length + 1u < capacity) {
            buffer[length++] = *cursor++;
            buffer[length] = '\0';
        }
    }
    return buffer;
}

/** A CPU surface for a decoration pass: owned, RGBA8, zero-filled (so "untouched" is
 *  transparent and an unpainted pixel is distinguishable from a black one). */
static robo_gfx_surface_t *border_surface(uint32_t width,
                                          uint32_t height,
                                          robo_gfx_surface_access_t access)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = access;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surface), ROBO_OK);
    ROBO_CHECK(surface != NULL);
    return surface;
}

/** Build a display list from @p records, asserting the build succeeds. */
static void border_build(const paint_result_t *records,
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
        robo_test_fail(__FILE__, __LINE__, "the display build refused the records: %s",
                       diag.message ? diag.message : "(no message)");
    }
    robo_paint_display_list_builder_destroy(builder);
}

/**
 * Paint @p list into @p target, asserting the pass succeeds.
 *
 * The pass is *not* asserted to fill anything -- several of the cases below are about a document
 * that draws nothing -- but a failure is always a bug in a case, so the diagnostic is reported.
 */
static void border_paint(const robo_paint_display_list_t *list,
                         robo_gfx_surface_t *target,
                         robo_paint_decoration_stats_t *out_stats)
{
    robo_paint_diag_t diag;
    robo_result_t status;

    robo_paint_diag_reset(&diag);
    status = robo_paint_decoration_paint(list, target, out_stats, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the decoration pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** The @p nth command of kind @p kind whose node is @p node, or 0 when there is none. */
static int border_command_of_kind_for(const robo_paint_display_list_t *list,
                                      robo_paint_display_command_kind_t kind,
                                      robo_layout_node_id_t node,
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
        if (command.kind != kind || command.node != node) {
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

/** The index of the first command of @p kind whose node is @p node, or `(size_t)-1`. */
static size_t border_index_of_kind_for(const robo_paint_display_list_t *list,
                                       robo_paint_display_command_kind_t kind,
                                       robo_layout_node_id_t node)
{
    size_t index;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            return (size_t)-1;
        }
        if (command.kind == kind && command.node == node) {
            return index;
        }
    }
    return (size_t)-1;
}

/** The index of the **last** command of @p kind whose node is @p node, or `(size_t)-1`. */
static size_t border_last_index_of_kind_for(const robo_paint_display_list_t *list,
                                            robo_paint_display_command_kind_t kind,
                                            robo_layout_node_id_t node)
{
    size_t index;
    size_t found = (size_t)-1;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            return (size_t)-1;
        }
        if (command.kind == kind && command.node == node) {
            found = index;
        }
    }
    return found;
}

/** Read the pixel at (x, y), failing when the point is outside the surface. */
static robo_gfx_color_rgba8_t border_read(robo_gfx_surface_t *surface, int32_t x, int32_t y)
{
    robo_gfx_color_rgba8_t color;
    int32_t width = (int32_t)robo_gfx_surface_width(surface);
    int32_t height = (int32_t)robo_gfx_surface_height(surface);

    memset(&color, 0, sizeof(color));
    if (x < 0 || y < 0 || x >= width || y >= height) {
        robo_test_fail(__FILE__, __LINE__, "the sample %d,%d is outside the %dx%d surface", (int)x,
                       (int)y, (int)width, (int)height);
        return color;
    }
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, (uint32_t)x, (uint32_t)y, &color),
                      ROBO_OK);
    return color;
}

/** Assert the pixel at (x, y) is @p expected. */
static void border_pixel_is(robo_gfx_surface_t *surface,
                            int32_t x,
                            int32_t y,
                            robo_gfx_color_rgba8_t expected,
                            const char *what)
{
    robo_gfx_color_rgba8_t actual = border_read(surface, x, y);

    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: expected rgba(%u,%u,%u,%u) at %d,%d, got rgba(%u,%u,%u,%u)", what,
                       expected.r, expected.g, expected.b, expected.a, (int)x, (int)y, actual.r,
                       actual.g, actual.b, actual.a);
    }
}

/** Assert the pixel at (x, y) holds @p r, @p g, @p b, @p a -- the same door, spelled for a
 *  colour no macro names. */
static void border_pixel_is_rgba(robo_gfx_surface_t *surface,
                                 int32_t x,
                                 int32_t y,
                                 uint8_t r,
                                 uint8_t g,
                                 uint8_t b,
                                 uint8_t a,
                                 const char *what)
{
    border_pixel_is(surface, x, y, robo_gfx_rgba8(r, g, b, a), what);
}

/** The centre of @p rect, asserted to be a non-empty rectangle. */
static void border_centre(robo_layout_rect_t rect, int32_t *out_x, int32_t *out_y)
{
    ROBO_CHECK(rect.width > 0 && rect.height > 0);
    *out_x = rect.x + rect.width / 2;
    *out_y = rect.y + rect.height / 2;
}

/** Assert the pixel at the centre of @p rect is @p expected. */
static void border_centre_is(robo_gfx_surface_t *surface,
                             robo_layout_rect_t rect,
                             robo_gfx_color_rgba8_t expected,
                             const char *what)
{
    int32_t x = 0;
    int32_t y = 0;

    border_centre(rect, &x, &y);
    border_pixel_is(surface, x, y, expected, what);
}

/** Assert every field of two pass records agrees. */
static void border_stats_eq(const robo_paint_decoration_stats_t *a,
                            const robo_paint_decoration_stats_t *b,
                            const char *what)
{
    if (a->commands != b->commands || a->draws != b->draws ||
        a->backgrounds != b->backgrounds || a->borders != b->borders ||
        a->filled != b->filled || a->skipped != b->skipped || a->contextual != b->contextual ||
        a->transparent != b->transparent || a->empty != b->empty || a->unshaped != b->unshaped ||
        a->clipped != b->clipped) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: commands %d/%d draws %d/%d backgrounds %d/%d borders %d/%d "
                       "filled %d/%d skipped %d/%d contextual %d/%d transparent %d/%d "
                       "empty %d/%d unshaped %d/%d clipped %d/%d",
                       what, (int)a->commands, (int)b->commands, (int)a->draws, (int)b->draws,
                       (int)a->backgrounds, (int)b->backgrounds, (int)a->borders, (int)b->borders,
                       (int)a->filled, (int)b->filled, (int)a->skipped, (int)b->skipped,
                       (int)a->contextual, (int)b->contextual, (int)a->transparent,
                       (int)b->transparent, (int)a->empty, (int)b->empty, (int)a->unshaped,
                       (int)b->unshaped, (int)a->clipped, (int)b->clipped);
    }
}

/** A clip whose horizontal axis is bounded and whose vertical axis is not. */
static robo_paint_clip_t border_clip_x(int32_t start, int32_t end)
{
    robo_paint_clip_t clip = robo_paint_clip_none();

    clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    clip.horizontal.start = start;
    clip.horizontal.end = end;
    return clip;
}

/** A hand-built `draw-border`: kind, node, band, clip, colour **and style**, everything else
 *  cleared. The builder is bypassed on purpose -- these cases are about the painter's own
 *  contract, and the list's validity rule is the door that keeps them legal. */
static robo_paint_display_command_t border_make_band(robo_layout_node_id_t node,
                                                     robo_layout_rect_t rect,
                                                     robo_paint_clip_t clip,
                                                     robo_css_color_t color,
                                                     robo_css_border_style_t style)
{
    robo_paint_display_command_t command;

    robo_paint_display_command_init(&command);
    command.kind = ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER;
    command.node = node;
    command.depth = 0u;
    command.rect = rect;
    command.clip = clip;
    command.color = color;
    command.style = style;
    return command;
}

/** A hand-built structural command: a `begin-node` marker that draws nothing. */
static robo_paint_display_command_t border_make_structural(robo_layout_node_id_t node)
{
    robo_paint_display_command_t command;

    robo_paint_display_command_init(&command);
    command.kind = ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE;
    command.node = node;
    command.depth = 0u;
    command.rect = robo_gfx_rect(0, 0, 10, 10);
    return command;
}

/** Append @p command to @p list, asserting it is a record the list accepts. */
static void border_append(robo_paint_display_list_t *list,
                          robo_paint_display_command_t command)
{
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_append(list, &command, NULL), ROBO_OK);
}

/**
 * The four bands a box with uniform insets must produce, computed from its own border box.
 *
 * A reader can check these four lines against CSS's box model without knowing anything about
 * this build, which is the point: a band is the region of the border box outside the padding box
 * on one edge, so with a uniform @p inset the expectation is the border box with one edge of the
 * given thickness.
 */
static void border_uniform_bands(robo_layout_rect_t border_box,
                                 int32_t inset,
                                 robo_layout_rect_t *out_top,
                                 robo_layout_rect_t *out_right,
                                 robo_layout_rect_t *out_bottom,
                                 robo_layout_rect_t *out_left)
{
    out_top->x = border_box.x;
    out_top->y = border_box.y;
    out_top->width = border_box.width;
    out_top->height = inset;

    out_right->x = border_box.x + border_box.width - inset;
    out_right->y = border_box.y;
    out_right->width = inset;
    out_right->height = border_box.height;

    out_bottom->x = border_box.x;
    out_bottom->y = border_box.y + border_box.height - inset;
    out_bottom->width = border_box.width;
    out_bottom->height = inset;

    out_left->x = border_box.x;
    out_left->y = border_box.y;
    out_left->width = inset;
    out_left->height = border_box.height;
}

/** The four band commands of @p node, in order top, right, bottom, left, asserted to exist. */
static void border_four_bands(const robo_paint_display_list_t *list,
                              robo_layout_node_id_t node,
                              robo_paint_display_command_t *out_bands)
{
    size_t side;

    for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
        ROBO_CHECK_EQ_INT(border_command_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER,
                                                     node, side, &out_bands[side]),
                          1);
    }
}

/** The border box of @p node, read out of its opening marker rather than assumed. */
static robo_layout_rect_t border_box_of(const robo_paint_display_list_t *list,
                                        robo_layout_node_id_t node)
{
    robo_paint_display_command_t marker;

    ROBO_CHECK_EQ_INT(border_command_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE, node,
                                                 0u, &marker),
                      1);
    return marker.rect;
}

/* ------------------------------------------------------------------ */
/* 1. The style travels the chain                                      */
/* ------------------------------------------------------------------ */

static void test_border_style_reaches_the_command(void)
{
    static const struct {
        const char *declaration;
        robo_css_border_style_t expected;
    } k_cases[] = {
        { "border: 4px solid #111111", ROBO_CSS_BORDER_STYLE_SOLID },
        { "border: 4px dashed #ff0000", ROBO_CSS_BORDER_STYLE_DASHED },
        { "border: 4px dotted #ff0000", ROBO_CSS_BORDER_STYLE_DOTTED },
        { "border: 4px double #ff0000", ROBO_CSS_BORDER_STYLE_DOUBLE }
    };
    size_t which;

    paint_fixture();

    /*
     * The claim in one loop: for each of the styles this half of CSS is exercised with, the
     * **record** carries it, the **command** carries it, and they agree -- on all four sides, so
     * a bug that read one side twice cannot pass. The record's accessor is asserted too, because
     * the interesting failure this phase prevents is a style that survives the traversal and is
     * then dropped by the builder.
     */
    for (which = 0u; which < sizeof(k_cases) / sizeof(k_cases[0]); which++) {
        paint_fixture_doc_t fixture;
        paint_result_t result;
        robo_paint_display_list_t *list = NULL;
        robo_paint_display_list_stats_t list_stats;
        robo_layout_node_id_t box;
        char sheet[256];
        size_t bands = 0u;
        size_t index;

        paint_begin_laid_out(&fixture,
                             "<html><body><div class=\"box\"></div></body></html>",
                             border_sheet(sheet, sizeof(sheet), k_cases[which].declaration));
        paint_once(&fixture, &result);
        border_build(&result, &list, &list_stats);
        box = paint_box(&fixture, "box");

        /* The record layer: the traversal's own accessor answers the declared style for every
         * border record of this box, or `NONE` for a kind that is not a border. */
        for (index = 0u; index < robo_paint_list_count(result.list); index++) {
            robo_paint_operation_t operation;

            ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &operation), ROBO_OK);
            if (operation.node != box) {
                continue;
            }
            if (robo_paint_op_kind_is_border(operation.kind)) {
                bands++;
                ROBO_CHECK_EQ_INT((int)robo_paint_operation_style(&operation),
                                  (int)k_cases[which].expected);
            } else {
                ROBO_CHECK_EQ_INT((int)robo_paint_operation_style(&operation), 0);
            }
        }
        ROBO_CHECK_EQ_INT((int)bands, 4);

        /* And the command layer, which is where a style that was carried could still be lost. */
        bands = 0u;
        for (index = 0u; index < robo_paint_display_list_count(list); index++) {
            robo_paint_display_command_t command;

            ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
            if (command.node != box) {
                continue;
            }
            if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER) {
                bands++;
                ROBO_CHECK_EQ_INT((int)robo_paint_display_command_style(&command),
                                  (int)k_cases[which].expected);
            } else {
                /* A non-border command carries no style, which is the mirror half of the rule. */
                ROBO_CHECK_EQ_INT((int)robo_paint_display_command_style(&command), 0);
            }
        }
        ROBO_CHECK_EQ_INT((int)bands, 4);
        ROBO_CHECK_EQ_INT((int)list_stats.borders, 4);

        robo_paint_display_list_destroy(list);
        paint_result_dispose(&result);
        paint_end(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* 2. The four bands                                                   */
/* ------------------------------------------------------------------ */

static void test_border_four_bands(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t bands[ROBO_CSS_BOX_SIDE_COUNT];
    robo_paint_display_command_t content;
    robo_layout_rect_t border_box;
    robo_layout_rect_t expected[ROBO_CSS_BOX_SIDE_COUNT];
    robo_layout_node_id_t box;
    size_t side;

    /* `width: 200px; height: 100px` is the **content** box, so the border box is 208x108: the box
     * model is layout's conclusion and this case reads it back rather than assuming it. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BORDER_BASE_CSS
                         " .box { width: 200px; height: 100px; border: 4px solid #111111 }");
    paint_once(&fixture, &result);
    border_build(&result, &list, &list_stats);

    box = paint_box(&fixture, "box");
    ROBO_CHECK_EQ_INT((int)list_stats.borders, 4);
    ROBO_CHECK_EQ_INT((int)list_stats.backgrounds, 0);
    ROBO_CHECK_EQ_INT((int)list_stats.draws, 4);

    border_box = border_box_of(list, box);
    ROBO_CHECK_EQ_INT((int)border_box.width, 208);
    ROBO_CHECK_EQ_INT((int)border_box.height, 108);

    border_four_bands(list, box, bands);
    border_uniform_bands(border_box, 4, &expected[0], &expected[1], &expected[2], &expected[3]);
    for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
        paint_rect_eq(bands[side].rect, expected[side], "a border band");
        ROBO_CHECK_EQ_INT((int)bands[side].style, (int)ROBO_CSS_BORDER_STYLE_SOLID);
    }

    /* And the band is not the content box: the two rectangles a band is the difference of are
     * both still in the list, and they are 4px apart on every edge. */
    ROBO_CHECK_EQ_INT(border_command_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_CONTENT, box, 0u,
                                                 &content),
                      1);
    ROBO_CHECK_EQ_INT((int)content.rect.x, (int)border_box.x + 4);
    ROBO_CHECK_EQ_INT((int)content.rect.y, (int)border_box.y + 4);
    ROBO_CHECK_EQ_INT((int)content.rect.width, (int)border_box.width - 8);
    ROBO_CHECK_EQ_INT((int)content.rect.height, (int)border_box.height - 8);

    /* Every band reaches the surface, in the border's colour. */
    surface = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    border_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.draws, 4);
    ROBO_CHECK_EQ_INT((int)stats.borders, 4);
    ROBO_CHECK_EQ_INT((int)stats.filled, 4);
    ROBO_CHECK_EQ_INT((int)stats.empty, 0);
    ROBO_CHECK_EQ_INT((int)stats.unshaped, 0);
    for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
        border_centre_is(surface, bands[side].rect, BORDER_BLACK, "a filled border band");
    }

    /* One pixel past the left band's inner edge is the content box, where nothing drew. */
    border_pixel_is(surface, border_box.x + 4, border_box.y + border_box.height / 2,
                    robo_gfx_rgba8(0, 0, 0, 0),
                    "the first pixel of the content box, where no background was declared");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Per-side widths                                                  */
/* ------------------------------------------------------------------ */

static void test_border_per_side_widths(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t bands[ROBO_CSS_BOX_SIDE_COUNT];
    robo_layout_rect_t border_box;
    robo_layout_node_id_t box;

    /*
     * The four-value shorthand, which RoboCSS distributes as top, right, bottom, left. The
     * assertion is that the *paint* layer reads the per-side used widths rather than one uniform
     * number: each band's facing extent is its own side's width, and its other extent is the
     * whole border box on that axis -- which is what makes a box with two borders have two
     * full-height bands rather than two half-height ones.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BORDER_BASE_CSS
                         " .box { width: 100px; height: 50px;"
                         "   border-width: 1px 2px 3px 4px; border-style: solid;"
                         "   border-color: #ff0000 }");
    paint_once(&fixture, &result);
    border_build(&result, &list, &list_stats);

    box = paint_box(&fixture, "box");
    ROBO_CHECK_EQ_INT((int)list_stats.borders, 4);
    border_box = border_box_of(list, box);
    /* 100 + 2 + 4 and 50 + 1 + 3. */
    ROBO_CHECK_EQ_INT((int)border_box.width, 106);
    ROBO_CHECK_EQ_INT((int)border_box.height, 54);

    border_four_bands(list, box, bands);
    /* Top: full width, 1px tall. Right: 2px wide, full height. Bottom: full width, 3px tall.
     * Left: 4px wide, full height. */
    paint_rect_is(border_box.x, border_box.y, border_box.width, 1, bands[0].rect, "the top band");
    paint_rect_is(border_box.x + border_box.width - 2, border_box.y, 2, border_box.height,
                  bands[1].rect, "the right band");
    paint_rect_is(border_box.x, border_box.y + border_box.height - 3, border_box.width, 3,
                  bands[2].rect, "the bottom band");
    paint_rect_is(border_box.x, border_box.y, 4, border_box.height, bands[3].rect,
                  "the left band");

    surface = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    border_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.borders, 4);
    ROBO_CHECK_EQ_INT((int)stats.filled, 4);
    ROBO_CHECK_EQ_INT((int)stats.empty, 0);
    border_centre_is(surface, bands[0].rect, BORDER_RED, "the 1px top band");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Sides that are not there                                         */
/* ------------------------------------------------------------------ */

static void test_border_absent_sides(void)
{
    static const struct {
        const char *declaration;
        size_t expected_bands;
    } k_cases[] = {
        /* A `none` style and a zero width are *different* declarations and the same conclusion:
         * a side whose used width is zero emits no operation, which is RoboLayout's conclusion
         * and not a decision this layer makes. The two "some sides" rows are the complement --
         * the layer does not require all four. */
        { "border-style: none; border-width: 4px", 0u },
        { "border-style: solid; border-width: 0px", 0u },
        { "border: 4px none #ff0000", 0u },
        { "border: 4px hidden #ff0000", 0u },
        { "border-top: 4px solid #ff0000", 1u },
        { "border: 4px solid #ff0000; border-left-width: 0px", 3u }
    };
    size_t which;

    paint_fixture();

    for (which = 0u; which < sizeof(k_cases) / sizeof(k_cases[0]); which++) {
        paint_fixture_doc_t fixture;
        paint_result_t result;
        robo_paint_display_list_t *list = NULL;
        robo_paint_display_list_stats_t list_stats;
        robo_paint_decoration_stats_t stats;
        robo_gfx_surface_t *surface = NULL;
        char sheet[256];

        paint_begin_laid_out(&fixture,
                             "<html><body><div class=\"box\"></div></body></html>",
                             border_sheet(sheet, sizeof(sheet), k_cases[which].declaration));
        paint_once(&fixture, &result);
        border_build(&result, &list, &list_stats);
        ROBO_CHECK_EQ_INT((int)list_stats.borders, (int)k_cases[which].expected_bands);

        surface = border_surface(64u, 64u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
        border_paint(list, surface, &stats);
        ROBO_CHECK_EQ_INT((int)stats.borders, (int)k_cases[which].expected_bands);
        ROBO_CHECK_EQ_INT((int)stats.filled, (int)k_cases[which].expected_bands);
        ROBO_CHECK_EQ_INT((int)stats.backgrounds, 0);
        ROBO_CHECK_EQ_INT((int)stats.commands, (int)(stats.draws + stats.skipped));

        if (k_cases[which].expected_bands == 0u) {
            /* Nothing was declared to draw, so the surface is what the caller handed over. */
            border_pixel_is(surface, 8, 8, robo_gfx_rgba8(0, 0, 0, 0),
                            "a surface no side drew into");
        }

        robo_gfx_surface_release(surface);
        robo_paint_display_list_destroy(list);
        paint_result_dispose(&result);
        paint_end(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* 5. Colours, and which side owns a corner                            */
/* ------------------------------------------------------------------ */

static void test_border_colors_and_corners(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t bands[ROBO_CSS_BOX_SIDE_COUNT];
    robo_layout_rect_t border_box;
    robo_layout_node_id_t box;

    /* Four colours in the shorthand's top, right, bottom, left order. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BORDER_BASE_CSS
                         " .box { width: 100px; height: 60px; border: 5px solid #000000;"
                         "   border-color: #ff0000 #00ff00 #0000ff #ffff00 }");
    paint_once(&fixture, &result);
    border_build(&result, &list, &list_stats);

    box = paint_box(&fixture, "box");
    border_four_bands(list, box, bands);
    border_box = border_box_of(list, box);

    paint_color_eq(bands[0].color, robo_css_color_make(0xff, 0x00, 0x00, 255), "the top band");
    paint_color_eq(bands[1].color, robo_css_color_make(0x00, 0xff, 0x00, 255), "the right band");
    paint_color_eq(bands[2].color, robo_css_color_make(0x00, 0x00, 0xff, 255), "the bottom band");
    paint_color_eq(bands[3].color, robo_css_color_make(0xff, 0xff, 0x00, 255), "the left band");

    surface = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    border_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.filled, 4);

    /* Each side's own middle is its own colour. */
    border_centre_is(surface, bands[0].rect, BORDER_RED, "the top");
    border_centre_is(surface, bands[1].rect, BORDER_GREEN, "the right");
    border_centre_is(surface, bands[2].rect, robo_gfx_rgba8(0x00, 0x00, 0xff, 255), "the bottom");
    border_centre_is(surface, bands[3].rect, robo_gfx_rgba8(0xff, 0xff, 0x00, 255), "the left");

    /*
     * And the corners, where two bands overlap by design. The four bands are emitted top, right,
     * bottom, left -- the order the sides are numbered -- and each spans its axis's full extent,
     * so the band drawn **last** owns the corner. That is a statement about this build's
     * simplification rather than about CSS (a real renderer mitres the corners), so it is pinned
     * where it is observable: the top-left corner is the left band's yellow (left is drawn after
     * top), the top-right is the right band's green (right after top), the bottom-right is the
     * bottom band's blue (bottom after right) and the bottom-left is yellow again (left after
     * bottom).
     */
    border_pixel_is_rgba(surface, border_box.x, border_box.y, 0xff, 0xff, 0x00, 255,
                         "the top-left corner, where left is drawn after top");
    border_pixel_is_rgba(surface, border_box.x + border_box.width - 1, border_box.y, 0x00, 0xff,
                         0x00, 255, "the top-right corner, where right is drawn after top");
    border_pixel_is_rgba(surface, border_box.x + border_box.width - 1,
                         border_box.y + border_box.height - 1, 0x00, 0x00, 0xff, 255,
                         "the bottom-right corner, where bottom is drawn after right");
    border_pixel_is_rgba(surface, border_box.x, border_box.y + border_box.height - 1, 0xff, 0xff,
                         0x00, 255, "the bottom-left corner, where left is drawn after bottom");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Background and border together                                   */
/* ------------------------------------------------------------------ */

static void test_border_with_background(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t bands[ROBO_CSS_BOX_SIDE_COUNT];
    robo_paint_display_command_t background;
    robo_layout_rect_t border_box;
    robo_layout_node_id_t box;

    /* The pipeline's headline case: 11.4's background and 11.5's border in one box, from one
     * list, through one pass. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BORDER_BASE_CSS
                         " .box { width: 200px; height: 100px; padding: 8px;"
                         "   background-color: #3366cc; border: 4px solid #111111 }");
    paint_once(&fixture, &result);
    border_build(&result, &list, &list_stats);

    box = paint_box(&fixture, "box");
    ROBO_CHECK_EQ_INT((int)list_stats.backgrounds, 1);
    ROBO_CHECK_EQ_INT((int)list_stats.borders, 4);
    ROBO_CHECK_EQ_INT((int)list_stats.draws, 5);

    border_box = border_box_of(list, box);
    ROBO_CHECK_EQ_INT(border_command_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND,
                                                 box, 0u, &background),
                      1);
    border_four_bands(list, box, bands);

    /* The background is the whole border box -- CSS's initial `background-clip` -- and it is
     * emitted **before** the four bands, which is what makes the border visible over it. */
    paint_rect_eq(background.rect, border_box, "a background over the border box");
    {
        size_t background_index = border_index_of_kind_for(list,
                                                           ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND,
                                                           box);
        size_t first_band = border_index_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER,
                                                    box);

        ROBO_CHECK(background_index != (size_t)-1);
        ROBO_CHECK(first_band != (size_t)-1);
        ROBO_CHECK(background_index < first_band);
    }

    surface = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    border_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.draws, 5);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 1);
    ROBO_CHECK_EQ_INT((int)stats.borders, 4);
    ROBO_CHECK_EQ_INT((int)stats.filled, 5);

    /* The ring is the border and inside it the background: two colours, one box, and the
     * boundary between them is exactly the padding box's edge. */
    border_pixel_is(surface, border_box.x + 2, border_box.y + 2, BORDER_BLACK,
                    "inside the top band");
    border_pixel_is(surface, border_box.x + border_box.width / 2, border_box.y + 2, BORDER_BLACK,
                    "inside the top band, at its middle");
    border_pixel_is_rgba(surface, border_box.x + 4, border_box.y + 4, 0x33, 0x66, 0xcc, 255,
                         "the background's first pixel, just inside the border");
    border_pixel_is(surface, 0, (int32_t)robo_gfx_surface_height(surface) - 1,
                    robo_gfx_rgba8(0, 0, 0, 0), "a pixel outside the box entirely");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 7. One pass, one order: a child over its parent's border            */
/* ------------------------------------------------------------------ */

static void test_border_child_over_parent_border(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t parent_bands[ROBO_CSS_BOX_SIDE_COUNT];
    robo_paint_display_command_t child_background;
    robo_layout_rect_t parent_border_box;
    robo_layout_node_id_t parent_id;
    robo_layout_node_id_t child_id;

    /*
     * The decisive case for the phase's one architectural decision. The child is moved by a
     * relative offset until it spills over its parent's border, and the pixel over the border is
     * the child's colour -- because the list's order is the traversal's, so a descendant's
     * background is filled *after* its ancestor's bands. A painter that walked the list twice,
     * once per kind, would fill every background in the document before every border and paint
     * the parent's ring over the child's spill.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"parent\"><div class=\"child\"></div>"
                         "</div></body></html>",
                         BORDER_BASE_CSS
                         " .parent { width: 120px; height: 60px; border: 4px solid #111111 }"
                         " .child { width: 40px; height: 20px; background-color: #00ff00;"
                         "   position: relative; left: -20px; top: -20px }");
    paint_once(&fixture, &result);
    border_build(&result, &list, &list_stats);

    parent_id = paint_box(&fixture, "parent");
    child_id = paint_box(&fixture, "child");
    border_four_bands(list, parent_id, parent_bands);
    parent_border_box = border_box_of(list, parent_id);
    ROBO_CHECK_EQ_INT(border_command_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND,
                                                 child_id, 0u, &child_background),
                      1);

    /* The order, as indices: the parent's four bands, then the child's background -- the
     * traversal's order and the whole reason there is one pass. */
    {
        size_t first_band = border_index_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER,
                                                     parent_id);
        size_t last_band = border_last_index_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER,
                                                         parent_id);
        size_t child_index =
            border_index_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, child_id);

        ROBO_CHECK(first_band != (size_t)-1);
        ROBO_CHECK(last_band != (size_t)-1);
        ROBO_CHECK(child_index != (size_t)-1);
        ROBO_CHECK(last_band < child_index);
    }

    surface = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    border_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.filled, 5);
    ROBO_CHECK_EQ_INT((int)stats.draws, 5);

    /* The child's rectangle is the *moved* one: 20px left and 20px up from where flow put it,
     * which was inside the parent's padding box -- so it now covers the parent's top and left
     * bands. Those pixels are the child's green, not the parent's black. */
    {
        int32_t probe_x = parent_border_box.x + 10;
        int32_t probe_y = parent_border_box.y + 2;

        ROBO_CHECK(probe_x >= child_background.rect.x);
        ROBO_CHECK(probe_x < child_background.rect.x + child_background.rect.width);
        ROBO_CHECK(probe_y >= child_background.rect.y);
        ROBO_CHECK(probe_y < child_background.rect.y + child_background.rect.height);
        border_pixel_is(surface, probe_x, probe_y, BORDER_GREEN,
                        "the child's spill over the parent's top band");
        border_pixel_is(surface, parent_border_box.x + 80, parent_border_box.y + 2, BORDER_BLACK,
                        "the parent's top band where the child does not reach");
    }

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. Clipping                                                         */
/* ------------------------------------------------------------------ */

/** What one run of the clipping document produced: the spill's top band and the pass record. */
typedef struct border_clip_probe {
    robo_layout_rect_t band;
    robo_paint_decoration_stats_t stats;
} border_clip_probe_t;

/**
 * Lay out the clipping document with @p hidden and paint it.
 *
 * The two runs differ in **one declaration** and in nothing else -- same markup, same sheet,
 * same order -- so a difference in the pixels is attributable to the overflow mode.
 */
static void border_clip_run(int hidden, border_clip_probe_t *out, robo_gfx_surface_t *surface)
{
    static const char *const k_markup =
        "<html><body><aside class=\"clip\"><figure class=\"spill\"></figure>"
        "</aside></body></html>";
    /* The one declaration the two runs differ in, both spelled out in full: a reader can see the
     * whole difference between the documents and check that it is one property. */
    static const char *const k_hidden_sheet =
        BORDER_BASE_CSS
        " .clip { width: 60px; height: 30px; overflow: hidden }"
        " .spill { width: 200px; height: 200px; border: 4px solid #ff0000 }";
    static const char *const k_visible_sheet =
        BORDER_BASE_CSS
        " .clip { width: 60px; height: 30px; overflow: visible }"
        " .spill { width: 200px; height: 200px; border: 4px solid #ff0000 }";
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_display_command_t band;
    robo_layout_node_id_t spill_id;

    paint_begin_laid_out(&fixture, k_markup, hidden ? k_hidden_sheet : k_visible_sheet);
    paint_once(&fixture, &result);
    border_build(&result, &list, &list_stats);

    spill_id = paint_box(&fixture, "spill");
    ROBO_CHECK_EQ_INT(border_command_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER,
                                                 spill_id, 0u, &band),
                      1);
    out->band = band.rect;

    if (hidden) {
        /* The boundary is *on the command*, which is why a rasteriser applies it from the
         * command rather than from a separate operation. */
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&band.clip), 1);
    } else {
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&band.clip), 0);
    }

    border_paint(list, surface, &out->stats);
    ROBO_CHECK_EQ_INT((int)out->stats.borders, 4);
    ROBO_CHECK_EQ_INT((int)out->stats.filled, 4);

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_border_clipping(void)
{
    border_clip_probe_t clipped;
    border_clip_probe_t visible;
    robo_gfx_surface_t *clipped_surface = NULL;
    robo_gfx_surface_t *visible_surface = NULL;

    paint_fixture();

    clipped_surface = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    visible_surface = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    border_clip_run(1, &clipped, clipped_surface);
    border_clip_run(0, &visible, visible_surface);

    /* The same band either way -- the spill declares the same border and the same box. */
    ROBO_CHECK_EQ_INT((int)clipped.band.height, 4);
    ROBO_CHECK_EQ_INT((int)clipped.band.x, (int)visible.band.x);
    ROBO_CHECK_EQ_INT((int)clipped.band.y, (int)visible.band.y);

    /* Only the hidden run bounds a fill, and it says so in the record. */
    ROBO_CHECK((int)clipped.stats.clipped >= 1);
    ROBO_CHECK_EQ_INT((int)visible.stats.clipped, 0);

    /* Inside the ancestor, both runs fill the band red. */
    border_pixel_is(clipped_surface, clipped.band.x + 5, clipped.band.y + 2, BORDER_RED,
                    "inside the clip, hidden");
    border_pixel_is(visible_surface, visible.band.x + 5, visible.band.y + 2, BORDER_RED,
                    "inside the clip, visible");

    /* Far to the right, inside the spill's own band and outside the 60px ancestor: the visible
     * run fills it and the hidden run leaves it untouched. That difference **is** the clip, and
     * it is the same mechanism a background is bounded by -- a band is a fill like any other. */
    border_pixel_is(visible_surface, visible.band.x + 150, visible.band.y + 2, BORDER_RED,
                    "the band outside its ancestor, visible overflow");
    border_pixel_is(clipped_surface, clipped.band.x + 150, clipped.band.y + 2,
                    robo_gfx_rgba8(0, 0, 0, 0),
                    "the band outside its ancestor, hidden overflow");

    /* The mechanism, without a document to infer it from: a hand-built band whose clip bounds one
     * axis. The axis the clip leaves open is the surface's own interval, which is what makes
     * "bounded horizontally, free vertically" expressible at all. */
    {
        robo_paint_display_list_t *list = NULL;
        robo_paint_decoration_stats_t stats;
        robo_gfx_surface_t *plain = border_surface(32u, 32u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);

        ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
        border_append(list,
                      border_make_band((robo_layout_node_id_t)1u, robo_gfx_rect(0, 0, 10, 32),
                                       border_clip_x(0, 5), robo_css_color_make(0, 0, 255, 255),
                                       ROBO_CSS_BORDER_STYLE_SOLID));
        border_paint(list, plain, &stats);
        ROBO_CHECK_EQ_INT((int)stats.filled, 1);
        ROBO_CHECK_EQ_INT((int)stats.clipped, 1);
        border_pixel_is(plain, 0, 0, robo_gfx_rgba8(0, 0, 255, 255), "inside the bounded axis");
        border_pixel_is(plain, 4, 31, robo_gfx_rgba8(0, 0, 255, 255),
                        "the interval's last column, at the bottom of the surface");
        border_pixel_is(plain, 5, 31, robo_gfx_rgba8(0, 0, 0, 0), "one pixel past the interval");
        border_pixel_is(plain, 9, 9, robo_gfx_rgba8(0, 0, 0, 0), "the far side of the band");
        robo_paint_display_list_destroy(list);
        robo_gfx_surface_release(plain);
    }

    robo_gfx_surface_release(clipped_surface);
    robo_gfx_surface_release(visible_surface);
}

/* ------------------------------------------------------------------ */
/* 9. Pattern styles are bands, and counted as such                    */
/* ------------------------------------------------------------------ */

static void test_border_pattern_styles(void)
{
    static const struct {
        const char *name;
        robo_css_border_style_t style;
    } k_styles[] = {
        { "dashed", ROBO_CSS_BORDER_STYLE_DASHED },
        { "dotted", ROBO_CSS_BORDER_STYLE_DOTTED },
        { "double", ROBO_CSS_BORDER_STYLE_DOUBLE },
        { "groove", ROBO_CSS_BORDER_STYLE_GROOVE },
        { "ridge", ROBO_CSS_BORDER_STYLE_RIDGE },
        { "inset", ROBO_CSS_BORDER_STYLE_INSET },
        { "outset", ROBO_CSS_BORDER_STYLE_OUTSET }
    };
    size_t which;

    paint_fixture();

    /*
     * The seven pattern styles, each in its own case: the band is **drawn**, in the side's
     * colour, and the simplification is a number rather than a silence. `unshaped` is what keeps
     * `border_painting == 1` from being read as "this build draws every border the way it looks"
     * -- the style is on the command for the rasteriser that will shape it, and counted here for
     * the reader who needs to know this build does not.
     */
    for (which = 0u; which < sizeof(k_styles) / sizeof(k_styles[0]); which++) {
        paint_fixture_doc_t fixture;
        paint_result_t result;
        robo_paint_display_list_t *list = NULL;
        robo_paint_display_list_stats_t list_stats;
        robo_paint_decoration_stats_t stats;
        robo_gfx_surface_t *surface = NULL;
        robo_paint_display_command_t bands[ROBO_CSS_BOX_SIDE_COUNT];
        char declaration[64];
        char sheet[256];
        size_t length = 0u;
        const char *part;
        size_t side;
        robo_layout_node_id_t box;

        declaration[0] = '\0';
        for (part = "border: 4px "; *part != '\0' && length + 1u < sizeof(declaration); part++) {
            declaration[length++] = *part;
            declaration[length] = '\0';
        }
        for (part = k_styles[which].name; *part != '\0' && length + 1u < sizeof(declaration);
             part++) {
            declaration[length++] = *part;
            declaration[length] = '\0';
        }
        for (part = " #ff0000"; *part != '\0' && length + 1u < sizeof(declaration); part++) {
            declaration[length++] = *part;
            declaration[length] = '\0';
        }

        paint_begin_laid_out(&fixture,
                             "<html><body><div class=\"box\"></div></body></html>",
                             border_sheet(sheet, sizeof(sheet), declaration));
        paint_once(&fixture, &result);
        border_build(&result, &list, &list_stats);
        box = paint_box(&fixture, "box");
        border_four_bands(list, box, bands);

        /* The style is on every band command, unmangled: this is the fact that makes a band
         * render-ready rather than merely placed. */
        for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
            ROBO_CHECK_EQ_INT((int)bands[side].style, (int)k_styles[which].style);
        }

        surface = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
        border_paint(list, surface, &stats);
        ROBO_CHECK_EQ_INT((int)stats.borders, 4);
        ROBO_CHECK_EQ_INT((int)stats.filled, 4);
        /* Drawn as the band, and counted as simplified -- both, because both are true. */
        ROBO_CHECK_EQ_INT((int)stats.unshaped, 4);
        border_centre_is(surface, bands[0].rect, BORDER_RED, "a pattern style's band");

        robo_gfx_surface_release(surface);
        robo_paint_display_list_destroy(list);
        paint_result_dispose(&result);
        paint_end(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* 10. The painter's own contract                                      */
/* ------------------------------------------------------------------ */

static void test_border_hand_built_kinds(void)
{
    robo_paint_display_list_t *list = NULL;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t band;

    paint_fixture();

    /* The style rule, from the record layer's side: a `draw-border` with no style is not a
     * command, because a band whose shape is unstated is one its consumer could only guess at. */
    band = border_make_band((robo_layout_node_id_t)1u, robo_gfx_rect(0, 0, 8, 2),
                            robo_paint_clip_none(), robo_css_color_make(0, 255, 0, 255),
                            ROBO_CSS_BORDER_STYLE_NONE);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&band), 0);
    /* `hidden` is a style a *side* may have and one that generates no border, so a command
     * cannot carry it either. */
    band = border_make_band((robo_layout_node_id_t)1u, robo_gfx_rect(0, 0, 8, 2),
                            robo_paint_clip_none(), robo_css_color_make(0, 255, 0, 255),
                            ROBO_CSS_BORDER_STYLE_HIDDEN);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&band), 0);

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
    surface = border_surface(32u, 32u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);

    /* A structural command -- which draws nothing -- then a solid band, a pattern band whose band
     * is filled anyway, a `currentColor` band, a transparent band and a zero-area band. */
    border_append(list, border_make_structural((robo_layout_node_id_t)1u));
    border_append(list, border_make_band((robo_layout_node_id_t)1u, robo_gfx_rect(0, 0, 8, 2),
                                        robo_paint_clip_none(),
                                        robo_css_color_make(0, 255, 0, 255),
                                        ROBO_CSS_BORDER_STYLE_SOLID));
    border_append(list, border_make_band((robo_layout_node_id_t)1u, robo_gfx_rect(0, 2, 8, 2),
                                        robo_paint_clip_none(),
                                        robo_css_color_make(255, 0, 0, 255),
                                        ROBO_CSS_BORDER_STYLE_DASHED));
    border_append(list, border_make_band((robo_layout_node_id_t)1u, robo_gfx_rect(0, 4, 8, 2),
                                        robo_paint_clip_none(), robo_css_color_current(),
                                        ROBO_CSS_BORDER_STYLE_SOLID));
    border_append(list, border_make_band((robo_layout_node_id_t)1u, robo_gfx_rect(0, 6, 8, 2),
                                        robo_paint_clip_none(),
                                        robo_css_color_make(0, 0, 255, 0),
                                        ROBO_CSS_BORDER_STYLE_SOLID));
    border_append(list, border_make_band((robo_layout_node_id_t)1u, robo_gfx_rect(0, 8, 8, 0),
                                        robo_paint_clip_none(),
                                        robo_css_color_make(0, 0, 255, 255),
                                        ROBO_CSS_BORDER_STYLE_SOLID));

    border_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.commands, 6);
    ROBO_CHECK_EQ_INT((int)stats.draws, 5);
    ROBO_CHECK_EQ_INT((int)stats.borders, 5);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 0);
    ROBO_CHECK_EQ_INT((int)stats.skipped, 1);
    ROBO_CHECK_EQ_INT((int)stats.contextual, 1);
    ROBO_CHECK_EQ_INT((int)stats.transparent, 1);
    ROBO_CHECK_EQ_INT((int)stats.empty, 1);
    ROBO_CHECK_EQ_INT((int)stats.unshaped, 1);
    ROBO_CHECK_EQ_INT((int)stats.filled, 2);
    ROBO_CHECK_EQ_INT((int)stats.clipped, 0);
    /* The three identities, on a list this file built by hand. */
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)(stats.draws + stats.skipped));
    ROBO_CHECK_EQ_INT((int)stats.draws, (int)(stats.backgrounds + stats.borders));
    ROBO_CHECK_EQ_INT((int)stats.draws, (int)(stats.filled + stats.contextual +
                                               stats.transparent + stats.empty));

    /* The solid band and the pattern band both reached the surface, in their own colours. */
    border_pixel_is(surface, 3, 0, BORDER_GREEN, "the solid band");
    border_pixel_is(surface, 3, 3, BORDER_RED, "the dashed band, drawn as its band");
    border_pixel_is(surface, 3, 5, robo_gfx_rgba8(0, 0, 0, 0), "the currentColor band");
    border_pixel_is(surface, 3, 7, robo_gfx_rgba8(0, 0, 0, 0), "the transparent band");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
}

/* ------------------------------------------------------------------ */
/* 11. Determinism                                                     */
/* ------------------------------------------------------------------ */

static void test_border_determinism(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_t *rebuilt = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_display_list_stats_t rebuilt_stats;
    robo_paint_decoration_stats_t first;
    robo_paint_decoration_stats_t second;
    robo_paint_decoration_stats_t again;
    robo_gfx_surface_t *one = NULL;
    robo_gfx_surface_t *two = NULL;
    size_t index;

    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\"></section></div>"
                         "</body></html>",
                         BORDER_BASE_CSS
                         " .card { width: 120px; height: 60px; padding: 6px;"
                         "   border: 2px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px; background-color: #123456;"
                         "   border: 1px dashed #ff0000 }");
    paint_once(&fixture, &result);
    border_build(&result, &list, &list_stats);

    one = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    two = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    border_paint(list, one, &first);
    border_paint(list, two, &second);
    border_stats_eq(&first, &second, "two passes over one list");

    /* Every fill landed in the same place, pixel for pixel -- the sample is each drawing
     * command's own rectangle, so the comparison follows the document rather than a fixed handful
     * of coordinates. */
    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;
        robo_gfx_color_rgba8_t left;
        robo_gfx_color_rgba8_t right;
        int32_t x = 0;
        int32_t y = 0;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        if (!robo_paint_display_command_kind_draws(command.kind)) {
            continue;
        }
        border_centre(command.rect, &x, &y);
        left = border_read(one, x, y);
        right = border_read(two, x, y);
        ROBO_CHECK_EQ_INT((int)left.r, (int)right.r);
        ROBO_CHECK_EQ_INT((int)left.g, (int)right.g);
        ROBO_CHECK_EQ_INT((int)left.b, (int)right.b);
        ROBO_CHECK_EQ_INT((int)left.a, (int)right.a);
    }

    /* And an independently built list for the same document is the *same* list: same order, same
     * geometry, same styles -- which is the strongest form of determinism available, and the one
     * that would catch a field travelling through the builder by accident. */
    border_build(&result, &rebuilt, &rebuilt_stats);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(list, rebuilt), 1);
    ROBO_CHECK_EQ_INT((int)rebuilt_stats.borders, (int)list_stats.borders);

    /* A second pass over the *same* surface leaves it as it was: an opaque fill is idempotent,
     * which is what makes a rebuilt display list safe to repaint in place. */
    border_paint(list, one, &again);
    border_stats_eq(&first, &again, "a repeat pass over one surface");

    robo_gfx_surface_release(one);
    robo_gfx_surface_release(two);
    robo_paint_display_list_destroy(rebuilt);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 12. Snapshot isolation                                              */
/* ------------------------------------------------------------------ */

static void test_border_mutation_isolation(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *before = NULL;
    robo_paint_display_list_t *after = NULL;
    robo_paint_display_list_stats_t before_stats;
    robo_paint_display_list_stats_t after_stats;
    robo_paint_decoration_stats_t first;
    robo_paint_decoration_stats_t second;
    robo_gfx_surface_t *one = NULL;
    robo_gfx_surface_t *two = NULL;
    size_t index;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BORDER_BASE_CSS
                         " .box { height: 40px; border: 6px solid #ff0000 }");
    paint_once(&fixture, &result);
    border_build(&result, &before, &before_stats);

    one = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    border_paint(before, one, &first);

    /*
     * Mutate both sources: clear the record list (the strongest legal change to it) and lay the
     * *same* tree out at a different viewport, which changes every box's geometry and therefore
     * every band. A display list is a snapshot, so the one already built paints exactly as it did
     * -- and the mutation is *proved* observable by building a second display list afterwards and
     * asserting it differs.
     */
    robo_paint_list_clear(result.list);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(result.list), 0);

    paint_layout(&fixture, 400, 600);
    {
        robo_paint_context_t *context = NULL;

        ROBO_CHECK_EQ_INT(robo_paint_context_create(&context, NULL), ROBO_OK);
        paint_run(&fixture, context, result.list);
        robo_paint_context_destroy(context);
    }
    border_build(&result, &after, &after_stats);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(before, after), 0);

    two = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    border_paint(before, two, &second);
    border_stats_eq(&first, &second, "a paint after the sources changed");

    for (index = 0u; index < robo_paint_display_list_count(before); index++) {
        robo_paint_display_command_t command;
        robo_gfx_color_rgba8_t left;
        robo_gfx_color_rgba8_t right;
        int32_t x = 0;
        int32_t y = 0;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(before, index, &command), ROBO_OK);
        if (!robo_paint_display_command_kind_draws(command.kind)) {
            continue;
        }
        border_centre(command.rect, &x, &y);
        left = border_read(one, x, y);
        right = border_read(two, x, y);
        ROBO_CHECK_EQ_INT((int)left.r, (int)right.r);
        ROBO_CHECK_EQ_INT((int)left.g, (int)right.g);
        ROBO_CHECK_EQ_INT((int)left.b, (int)right.b);
        ROBO_CHECK_EQ_INT((int)left.a, (int)right.a);
    }

    robo_gfx_surface_release(one);
    robo_gfx_surface_release(two);
    robo_paint_display_list_destroy(before);
    robo_paint_display_list_destroy(after);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 13. Empty, degenerate and refusing cases                            */
/* ------------------------------------------------------------------ */

static void test_border_empty_cases(void)
{
    robo_paint_display_list_t *list = NULL;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_gfx_surface_t *readonly = NULL;
    robo_paint_diag_t diag;

    paint_fixture();

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
    surface = border_surface(16u, 16u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);

    /* An empty list is a legitimate pass: nothing to do, and every counter nought. */
    memset(&stats, 0xff, sizeof(stats));
    robo_paint_decoration_stats_init(NULL); /* safe */
    border_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.commands, 0);
    ROBO_CHECK_EQ_INT((int)stats.draws, 0);
    ROBO_CHECK_EQ_INT((int)stats.borders, 0);
    ROBO_CHECK_EQ_INT((int)stats.filled, 0);
    ROBO_CHECK_EQ_INT((int)stats.skipped, 0);

    /* A list of structural commands alone: visited, skipped, and nothing drawn. */
    border_append(list, border_make_structural((robo_layout_node_id_t)1u));
    border_append(list, border_make_structural((robo_layout_node_id_t)1u));
    border_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.commands, 2);
    ROBO_CHECK_EQ_INT((int)stats.borders, 0);
    ROBO_CHECK_EQ_INT((int)stats.skipped, 2);
    border_pixel_is(surface, 8, 8, robo_gfx_rgba8(0, 0, 0, 0), "a surface no band touched");

    /* The arguments are required, and a refusal says which. */
    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_paint_decoration_paint(NULL, surface, NULL, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(diag.message != NULL);
    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_paint_decoration_paint(list, NULL, NULL, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(diag.message != NULL);

    /* And a target that cannot take a write is refused with the code the header names, before any
     * fill is attempted -- the graphics layer's own rule, mapped once. */
    readonly = border_surface(16u, 16u, ROBO_GFX_SURFACE_ACCESS_READ);
    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_paint_decoration_paint(list, readonly, NULL, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK(diag.message != NULL);
    robo_gfx_surface_release(readonly);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
}

/* ------------------------------------------------------------------ */
/* 14. End to end                                                      */
/* ------------------------------------------------------------------ */

static void test_border_end_to_end(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t card_bands[ROBO_CSS_BOX_SIDE_COUNT];
    robo_paint_display_command_t content;
    robo_layout_node_id_t card;
    robo_layout_node_id_t inner;
    char dump[8192];
    size_t dump_length = 0u;
    size_t index;
    size_t clipped_drawings = 0u;

    /*
     * The whole pipeline in one case: HTML → CSS → layout → paint records → display list →
     * surface. The document exercises the box model (width/height/margin/padding/border), a
     * nested child, a relative offset, a pattern style and hidden overflow, so the walk it
     * produces has every shape a consumer will meet.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"card\">"
                         "  <div class=\"inner\"></div>"
                         "</div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         BORDER_BASE_CSS
                         " .card { width: 200px; height: 80px; padding: 8px; margin: 4px;"
                         "   border: 3px dashed #111111; background-color: #eeeeee }"
                         " .inner { width: 100px; height: 40px; margin: 6px;"
                         "   background-color: #00ff00; position: relative; left: 5px;"
                         "   border: 2px solid #0000ff }"
                         " .clip { width: 60px; height: 30px; overflow: hidden;"
                         "   border-left: 6px solid #ff0000 }"
                         " .spill { width: 200px; height: 200px; background-color: #ff0000 }");
    paint_once(&fixture, &result);
    border_build(&result, &list, &list_stats);

    card = paint_box(&fixture, "card");
    inner = paint_box(&fixture, "inner");
    border_four_bands(list, card, card_bands);
    ROBO_CHECK_EQ_INT((int)card_bands[0].style, (int)ROBO_CSS_BORDER_STYLE_DASHED);
    ROBO_CHECK_EQ_INT(border_command_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_CONTENT, card, 0u,
                                                 &content),
                      1);

    surface = border_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    border_paint(list, surface, &stats);

    /* The pass's own identities hold for a real document, not just a hand-built one -- and the
     * display list's own counters agree with the pass's, which is the cross-check that keeps "the
     * list says X" and "the pass did X" from drifting. */
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)robo_paint_display_list_count(list));
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)(stats.draws + stats.skipped));
    ROBO_CHECK_EQ_INT((int)stats.draws, (int)(stats.backgrounds + stats.borders));
    ROBO_CHECK_EQ_INT((int)stats.draws, (int)list_stats.draws);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, (int)list_stats.backgrounds);
    ROBO_CHECK_EQ_INT((int)stats.borders, (int)list_stats.borders);
    ROBO_CHECK_EQ_INT((int)stats.filled + (int)stats.contextual + (int)stats.transparent +
                          (int)stats.empty,
                      (int)stats.draws);
    /* The card's four sides, the inner box's four and the clip box's left: nine bands. */
    ROBO_CHECK_EQ_INT((int)stats.borders, 9);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 3);
    ROBO_CHECK_EQ_INT((int)stats.filled, (int)stats.draws);
    /* The card's four dashed sides are the patterns this build draws as bands. */
    ROBO_CHECK_EQ_INT((int)stats.unshaped, 4);

    /* Every command is a record, and the ones under an ancestor's boundary are the ones the pass
     * counted as clipped -- a drawing of either kind, because a band under a hidden ancestor is
     * bounded exactly as a background is. */
    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);
        if (robo_paint_display_command_kind_draws(command.kind) &&
            robo_paint_clip_is_active(&command.clip)) {
            clipped_drawings++;
        }
    }
    ROBO_CHECK_EQ_INT((int)clipped_drawings, (int)stats.clipped);
    ROBO_CHECK((int)stats.clipped >= 1);

    /* The pixels: the card's ring is its dashed border's colour; inside it, past the inner child,
     * the card's own background -- three statements about one box that only a real pass makes. */
    border_pixel_is(surface, card_bands[0].rect.x + 10, card_bands[0].rect.y + 1, BORDER_BLACK,
                    "the card's top band");
    border_pixel_is_rgba(surface, content.rect.x + 40, content.rect.y + 20, 0x00, 0xff, 0x00, 255,
                         "the inner box's background, inside the card's content box");
    border_pixel_is_rgba(surface, content.rect.x + 150, content.rect.y + 70, 0xee, 0xee, 0xee,
                         255, "the card's background, past the inner box");
    /* And the inner box's own border is a ring inside the card: 2px of blue at its left edge. */
    {
        robo_paint_display_command_t inner_bands[ROBO_CSS_BOX_SIDE_COUNT];

        border_four_bands(list, inner, inner_bands);
        border_pixel_is(surface, inner_bands[3].rect.x + 1,
                        inner_bands[3].rect.y + inner_bands[3].rect.height / 2,
                        robo_gfx_rgba8(0x00, 0x00, 0xff, 255), "the inner box's left band");
    }

    /* The developer's door still reads the input the painter consumed -- and a band's style is
     * in the dump, because a command the painter reads has no secrets from the reader. */
    memset(dump, 0, sizeof(dump));
    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(list, dump, sizeof(dump), &dump_length, NULL),
                      ROBO_OK);
    ROBO_CHECK(dump_length > 0u);
    ROBO_CHECK(strstr(dump, "draw-border") != NULL);
    ROBO_CHECK(strstr(dump, "dashed") != NULL);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_border(void)
{
    robo_test_register("paint.border.style_reaches_the_command",
                       test_border_style_reaches_the_command);
    robo_test_register("paint.border.four_bands", test_border_four_bands);
    robo_test_register("paint.border.per_side_widths", test_border_per_side_widths);
    robo_test_register("paint.border.absent_sides", test_border_absent_sides);
    robo_test_register("paint.border.colors_and_corners", test_border_colors_and_corners);
    robo_test_register("paint.border.with_background", test_border_with_background);
    robo_test_register("paint.border.child_over_parent_border",
                       test_border_child_over_parent_border);
    robo_test_register("paint.border.clipping", test_border_clipping);
    robo_test_register("paint.border.pattern_styles", test_border_pattern_styles);
    robo_test_register("paint.border.hand_built_kinds", test_border_hand_built_kinds);
    robo_test_register("paint.border.determinism", test_border_determinism);
    robo_test_register("paint.border.mutation_isolation", test_border_mutation_isolation);
    robo_test_register("paint.border.empty_cases", test_border_empty_cases);
    robo_test_register("paint.border.end_to_end", test_border_end_to_end);
}
