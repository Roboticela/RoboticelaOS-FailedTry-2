/*
 * RoboPaint background suite (Phase 11.4, through the 11.5 decoration pass) —
 * paint.background.*
 *
 * The first place in this module where a colour reaches a **surface**, and the claims the
 * release makes, each pinned where it can be observed rather than argued. The door these
 * cases walk is `robo_paint_decoration_paint`, which 11.5 generalised from the
 * background-only painter of 11.4 to fill a box's border bands in the same pass -- the
 * cases here are about the **background** half of it, and the border suite beside this one
 * covers the other:
 *
 *   **the colour bridge**  one field-for-field copy between two 8-bit sRGB records, with the
 *                          three kinds that are *not* colours a surface can take decided
 *                          rather than flattened -- so `transparent` draws nothing,
 *                          `currentColor` is refused, `none` is "there was no colour" and
 *                          `invalid` is a mistake;
 *
 *   **pixels**             a document's `background-color` lands where the display list said
 *                          its rectangle is, and *only* there -- asserted by reading the
 *                          surface back, which is the strongest form of "the colour reached
 *                          RoboGraphics" available without a screenshot;
 *
 *   **paint order**        the traversal's order is the order of the fills, so a child's
 *                          background covers its parent's where they overlap -- the classic
 *                          proof that the sequence is depth-first and not a set;
 *
 *   **clipping**           the boundary an ancestor's `overflow: hidden` put in force bounds
 *                          a descendant's fill, demonstrated against a control document with
 *                          the same boxes and `visible` overflow;
 *
 *   **the fence**          the three **structural** commands are *skipped and counted*: they
 *                          draw nothing, by definition, which is what `raster == 0` means.
 *                          A border band is *not* skipped -- it is a drawing, and since 11.5
 *                          it is filled by this same pass -- so the case that used to pin
 *                          "a border-only document paints nothing" now pins the opposite,
 *                          which is the fence moving one command kind along;
 *
 *   **snapshot**           once a display list exists, clearing the record list and laying the
 *                          tree out again does not change a single pixel it paints.
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → records →
 * display list) and then paints it into a real RoboGraphics surface. Hand-built *commands*
 * appear only where the question is about the painter's own contract -- the four counters, the
 * clip application -- and never in place of a layout result.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on
 * no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"

#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayDump.h"

#include <string.h>

/* The same base sheet the other paint suites use: these elements are the block containers
 * (so a fixture's boxes stack in flow order), and `<head>` stays the one box nothing styles. */
#define BACKGROUND_BASE_CSS \
    "html, body, div, section, aside, figure, p { display: block }"

/* Two colours a pixel assertion can name, and one that is deliberately not opaque. */
#define BACKGROUND_RED robo_gfx_rgba8(255, 0, 0, 255)
#define BACKGROUND_BLUE robo_gfx_rgba8(0, 0, 255, 255)

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/** A CPU surface for a background pass: owned, RGBA8, zero-filled (so "untouched" is
 *  transparent and an unpainted pixel is distinguishable from a black one). */
static robo_gfx_surface_t *background_surface(uint32_t width,
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
static void background_build(const paint_result_t *records,
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
 * The pass is *not* asserted to fill anything -- three of the cases below are about a document
 * that draws nothing -- but a failure is always a bug in a case, so the diagnostic is reported.
 */
static void background_paint(const robo_paint_display_list_t *list,
                             robo_gfx_surface_t *target,
                             robo_paint_decoration_stats_t *out_stats)
{
    robo_paint_diag_t diag;
    robo_result_t status;

    robo_paint_diag_reset(&diag);
    status = robo_paint_decoration_paint(list, target, NULL, out_stats, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the background pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** The @p nth command of kind @p kind whose node is @p node, or 0 when there is none. */
static int background_command_of_kind_for(const robo_paint_display_list_t *list,
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

/** The @p nth `draw-background` command whose node is @p node, or 0 when there is none. */
static int background_command_for(const robo_paint_display_list_t *list,
                                  robo_layout_node_id_t node,
                                  size_t nth,
                                  robo_paint_display_command_t *out)
{
    return background_command_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, node, nth,
                                          out);
}

/** The @p nth command of @p list whose kind is @p kind, or 0 when there is none. */
static int background_nth_of_kind(const robo_paint_display_list_t *list,
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

/** Read the pixel at (x, y), failing when the point is outside the surface. */
static robo_gfx_color_rgba8_t background_read(robo_gfx_surface_t *surface, int32_t x, int32_t y)
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
static void background_pixel_is(robo_gfx_surface_t *surface,
                                int32_t x,
                                int32_t y,
                                robo_gfx_color_rgba8_t expected,
                                const char *what)
{
    robo_gfx_color_rgba8_t actual = background_read(surface, x, y);

    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: expected rgba(%u,%u,%u,%u) at %d,%d, got rgba(%u,%u,%u,%u)", what,
                       expected.r, expected.g, expected.b, expected.a, (int)x, (int)y, actual.r,
                       actual.g, actual.b, actual.a);
    }
}

/** The centre of @p rect, asserted to be a non-empty rectangle. */
static void background_centre(robo_layout_rect_t rect, int32_t *out_x, int32_t *out_y)
{
    ROBO_CHECK(rect.width > 0 && rect.height > 0);
    *out_x = rect.x + rect.width / 2;
    *out_y = rect.y + rect.height / 2;
}

/** Assert the pixel at the centre of @p rect is @p expected. */
static void background_centre_is(robo_gfx_surface_t *surface,
                                 robo_layout_rect_t rect,
                                 robo_gfx_color_rgba8_t expected,
                                 const char *what)
{
    int32_t x = 0;
    int32_t y = 0;

    background_centre(rect, &x, &y);
    background_pixel_is(surface, x, y, expected, what);
}

/** Assert every field of two pass records agrees -- all eleven, so a counter added to the
 *  record without a line here is a field two passes could disagree about unnoticed. */
static void background_stats_eq(const robo_paint_decoration_stats_t *a,
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

/** Non-zero when @p clip lets a fill reach the point (x, y). */
static int background_clip_admits(const robo_paint_clip_t *clip, int32_t x, int32_t y)
{
    if (robo_paint_clip_axis_is_active(&clip->horizontal) &&
        (x < clip->horizontal.start || x >= clip->horizontal.end)) {
        return 0;
    }
    if (robo_paint_clip_axis_is_active(&clip->vertical) &&
        (y < clip->vertical.start || y >= clip->vertical.end)) {
        return 0;
    }
    return 1;
}

/**
 * The colour a pass must have left at (x, y): the **last drawing** command -- a background or
 * a border band -- in list order, whose rectangle contains the point and whose clip admits it.
 *
 * It is the expectation a pixel assertion needs when fills overlap, and it is an *oracle*
 * rather than a second painter: it reads only the command's own rectangle, clip and colour, and
 * does no blending or clipping arithmetic -- so it can check the pass without repeating the
 * logic the pass is being checked for. A point no drawable command covers is transparent, which
 * is what an untouched surface holds.
 *
 * Every **drawing** kind rather than backgrounds alone, because 11.5 made a border band a fill
 * this pass performs: an oracle that ignored bands would expect an ancestor's background under
 * a band that is really drawn over it. The clip is consulted exactly as the pass does -- a
 * command whose boundary hides the point contributes nothing -- which is what makes "the
 * clipped pixel is untouched" an assertion rather than an absence.
 */
static robo_gfx_color_rgba8_t background_expected_at(const robo_paint_display_list_t *list,
                                                     int32_t x,
                                                     int32_t y)
{
    robo_gfx_color_rgba8_t color = robo_gfx_rgba8(0, 0, 0, 0);
    size_t index;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            return color;
        }
        if (!robo_paint_display_command_kind_draws(command.kind)) {
            continue;
        }
        if (!robo_paint_color_is_drawable(command.color)) {
            continue;
        }
        if (command.rect.width <= 0 || command.rect.height <= 0) {
            continue;
        }
        if (x < command.rect.x || y < command.rect.y ||
            x >= command.rect.x + command.rect.width ||
            y >= command.rect.y + command.rect.height) {
            continue;
        }
        if (!background_clip_admits(&command.clip, x, y)) {
            continue;
        }
        color = robo_gfx_rgba8(command.color.red, command.color.green, command.color.blue,
                               command.color.alpha);
    }
    return color;
}

/**
 * Assert the surface against the oracle at every **drawing** command's centre.
 *
 * This is the "and the pixels followed the list" claim for a document whose fills overlap: a
 * reader who wants to know which colour should be at a point asks the list, and the pass must
 * agree. Points outside the surface are skipped rather than asserted into it.
 */
static void background_pixels_match(const robo_paint_display_list_t *list,
                                    robo_gfx_surface_t *surface,
                                    const char *what)
{
    size_t index;
    size_t sampled = 0u;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;
        int32_t x;
        int32_t y;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        if (!robo_paint_display_command_kind_draws(command.kind)) {
            continue;
        }
        if (command.rect.width <= 0 || command.rect.height <= 0) {
            continue;
        }
        x = command.rect.x + command.rect.width / 2;
        y = command.rect.y + command.rect.height / 2;
        if (x < 0 || y < 0 || x >= (int32_t)robo_gfx_surface_width(surface) ||
            y >= (int32_t)robo_gfx_surface_height(surface)) {
            continue;
        }
        background_pixel_is(surface, x, y, background_expected_at(list, x, y), what);
        sampled++;
    }
    ROBO_CHECK(sampled > 0u);
}

/** A clip whose horizontal axis is bounded and whose vertical axis is not. */
static robo_paint_clip_t background_clip_x(int32_t start, int32_t end)
{
    robo_paint_clip_t clip = robo_paint_clip_none();

    clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    clip.horizontal.start = start;
    clip.horizontal.end = end;
    return clip;
}

/** A hand-built `draw-background`: kind, node, rectangle, clip and colour, everything else
 *  cleared. The builder is bypassed on purpose -- these cases are about the painter's
 *  contract, and the list's own validity rule is the door that keeps these legal. */
static robo_paint_display_command_t background_make_draw(robo_layout_node_id_t node,
                                                         robo_layout_rect_t rect,
                                                         robo_paint_clip_t clip,
                                                         robo_css_color_t color)
{
    robo_paint_display_command_t command;

    robo_paint_display_command_init(&command);
    command.kind = ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND;
    command.node = node;
    command.depth = 0u;
    command.rect = rect;
    command.clip = clip;
    command.color = color;
    return command;
}

/** A hand-built structural command: a `begin-node` marker that draws nothing. */
static robo_paint_display_command_t background_make_structural(robo_layout_node_id_t node)
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
static void background_append(robo_paint_display_list_t *list,
                              robo_paint_display_command_t command)
{
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_append(list, &command, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* 1. The colour bridge                                                */
/* ------------------------------------------------------------------ */

static void test_background_color_bridge(void)
{
    robo_css_color_t red = robo_css_color_make(255, 0, 0, 255);
    robo_css_color_t translucent = robo_css_color_make(51, 102, 204, 128);
    robo_css_color_t clear_color = robo_css_color_make(0, 0, 0, 0);
    robo_gfx_color_rgba8_t out;

    paint_fixture();

    /* A resolved colour draws, and the conversion is a copy: four channels, no arithmetic and
     * no colour space, so nothing can be lost or invented. */
    ROBO_CHECK_EQ_INT(robo_paint_color_is_drawable(red), 1);
    memset(&out, 0xff, sizeof(out));
    ROBO_CHECK_EQ_INT(robo_paint_color_to_rgba8(red, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.r, 255);
    ROBO_CHECK_EQ_INT((int)out.g, 0);
    ROBO_CHECK_EQ_INT((int)out.b, 0);
    ROBO_CHECK_EQ_INT((int)out.a, 255);

    /* Alpha survives rather than being flattened -- the channel a later compositor needs. */
    ROBO_CHECK_EQ_INT(robo_paint_color_is_drawable(translucent), 1);
    ROBO_CHECK_EQ_INT(robo_paint_color_to_rgba8(translucent, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.r, 51);
    ROBO_CHECK_EQ_INT((int)out.g, 102);
    ROBO_CHECK_EQ_INT((int)out.b, 204);
    ROBO_CHECK_EQ_INT((int)out.a, 128);

    /* Fully transparent: a colour, and one that occupies no pixel, so it neither draws nor
     * fails -- the distinction the predicate exists to state. */
    ROBO_CHECK_EQ_INT(robo_paint_color_is_drawable(clear_color), 0);
    ROBO_CHECK_EQ_INT(robo_paint_color_to_rgba8(clear_color, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.a, 0);

    /* The three kinds that are not colours a surface can take, each with its own answer. */
    ROBO_CHECK_EQ_INT(robo_paint_color_is_drawable(robo_css_color_none()), 0);
    ROBO_CHECK_EQ_INT(robo_paint_color_to_rgba8(robo_css_color_none(), &out), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_paint_color_is_drawable(robo_css_color_current()), 0);
    ROBO_CHECK_EQ_INT(robo_paint_color_to_rgba8(robo_css_color_current(), &out),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_paint_color_is_drawable(robo_css_color_invalid()), 0);
    ROBO_CHECK_EQ_INT(robo_paint_color_to_rgba8(robo_css_color_invalid(), &out),
                      ROBO_INVALID_ARGUMENT);

    /* The door's own argument rule. */
    ROBO_CHECK_EQ_INT(robo_paint_color_to_rgba8(red, NULL), ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* 2. A colour reaches a surface                                       */
/* ------------------------------------------------------------------ */

static void test_background_pixel_fill(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t background;
    robo_layout_node_id_t box;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BACKGROUND_BASE_CSS
                         " .box { width: 120px; height: 60px; background-color: #3366cc }");
    paint_once(&fixture, &result);
    background_build(&result, &list, &list_stats);

    box = paint_box(&fixture, "box");
    ROBO_CHECK_EQ_INT(background_command_for(list, box, 0u, &background), 1);

    surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_paint(list, surface, &stats);

    /* The document asked for one background and it was filled: the rectangle the display list
     * named is the rectangle the colour landed in. */
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 1);
    ROBO_CHECK_EQ_INT((int)stats.filled, 1);
    ROBO_CHECK_EQ_INT((int)stats.clipped, 0);
    background_centre_is(surface, background.rect, robo_gfx_rgba8(0x33, 0x66, 0xcc, 255),
                         "the box's background");

    /* And the fill is bounded by the rectangle: one pixel past its right edge is untouched.
     * The read is skipped when that pixel is off the surface rather than asserted into it. */
    if (background.rect.x + background.rect.width < (int32_t)robo_gfx_surface_width(surface) &&
        background.rect.y + background.rect.height / 2 < (int32_t)robo_gfx_surface_height(surface)) {
        background_pixel_is(surface,
                            background.rect.x + background.rect.width,
                            background.rect.y + background.rect.height / 2,
                            robo_gfx_rgba8(0, 0, 0, 0), "one pixel past the fill");
    } else {
        background_pixel_is(surface, 0, (int32_t)robo_gfx_surface_height(surface) - 1,
                            robo_gfx_rgba8(0, 0, 0, 0), "a pixel no box covers");
    }

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Paint order                                                      */
/* ------------------------------------------------------------------ */

static void test_background_ordering(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t parent;
    robo_paint_display_command_t child;
    robo_layout_node_id_t parent_id;
    robo_layout_node_id_t child_id;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"parent\"><div class=\"child\"></div></div>"
                         "</body></html>",
                         BACKGROUND_BASE_CSS
                         " .parent { width: 100px; height: 100px; background-color: #ff0000 }"
                         " .child { width: 40px; height: 40px; background-color: #0000ff }");
    paint_once(&fixture, &result);
    background_build(&result, &list, &list_stats);

    parent_id = paint_box(&fixture, "parent");
    child_id = paint_box(&fixture, "child");
    ROBO_CHECK_EQ_INT(background_command_for(list, parent_id, 0u, &parent), 1);
    ROBO_CHECK_EQ_INT(background_command_for(list, child_id, 0u, &child), 1);

    /* The order is the tree's: the parent's fill comes first, so it cannot cover the child. */
    {
        size_t parent_index = (size_t)-1;
        size_t child_index = (size_t)-1;
        size_t index;

        for (index = 0u; index < robo_paint_display_list_count(list); index++) {
            robo_paint_display_command_t command;

            ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
            if (command.kind != ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND) {
                continue;
            }
            if (command.node == parent_id) {
                parent_index = index;
            } else if (command.node == child_id) {
                child_index = index;
            }
        }
        ROBO_CHECK(parent_index != (size_t)-1 && child_index != (size_t)-1);
        ROBO_CHECK(parent_index < child_index);
    }

    surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.filled, 2);

    /* Where the two overlap, the later fill wins -- so the child's corner is blue and the
     * parent's far corner, which the child does not reach, is still red. */
    background_centre_is(surface, child.rect, BACKGROUND_BLUE, "the child's background");
    background_pixel_is(surface, parent.rect.x + parent.rect.width - 1,
                        parent.rect.y + parent.rect.height - 1, BACKGROUND_RED,
                        "the parent's far corner");
    background_pixel_is(surface, child.rect.x, child.rect.y, BACKGROUND_BLUE,
                        "the child's top-left corner");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Clipping                                                         */
/* ------------------------------------------------------------------ */

/** What one run of the clipping document produced: the spill's own rectangle and the pass
 *  record, so the two runs can be compared rather than guessed at. */
typedef struct background_clip_probe {
    robo_layout_rect_t spill_rect;
    robo_paint_decoration_stats_t stats;
} background_clip_probe_t;

/**
 * Lay out the clipping document with @p overflow ("hidden" or "visible") and paint it.
 *
 * The two runs differ in **one declaration** and in nothing else -- same markup, same sheet,
 * same order -- so a difference in the pixels is attributable to the overflow mode and not to a
 * second change smuggled in beside it.
 */
static void background_clip_run(int hidden,
                                background_clip_probe_t *out,
                                robo_gfx_surface_t *surface)
{
    static const char *const k_markup =
        "<html><body><aside class=\"clip\"><figure class=\"spill\"></figure>"
        "</aside></body></html>";
    /* The one declaration the two runs differ in, spelled out rather than built: a reader can
     * see the whole difference between the documents and check that it is one property. */
    static const char *const k_hidden_sheet =
        BACKGROUND_BASE_CSS
        " .clip { width: 60px; height: 30px; overflow: hidden; background-color: #00ff00 }"
        " .spill { width: 200px; height: 200px; background-color: #ff0000 }";
    static const char *const k_visible_sheet =
        BACKGROUND_BASE_CSS
        " .clip { width: 60px; height: 30px; overflow: visible; background-color: #00ff00 }"
        " .spill { width: 200px; height: 200px; background-color: #ff0000 }";
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_display_command_t spill;
    robo_layout_node_id_t spill_id;

    paint_begin_laid_out(&fixture, k_markup, hidden ? k_hidden_sheet : k_visible_sheet);
    paint_once(&fixture, &result);
    background_build(&result, &list, &list_stats);

    spill_id = paint_box(&fixture, "spill");
    ROBO_CHECK_EQ_INT(background_command_for(list, spill_id, 0u, &spill), 1);
    out->spill_rect = spill.rect;

    if (hidden) {
        /* The boundary an ancestor's overflow put in force is *on the command*, which is why
         * a rasteriser applies it from the command rather than from a separate operation. */
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&spill.clip), 1);
    } else {
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&spill.clip), 0);
    }

    background_paint(list, surface, &out->stats);
    ROBO_CHECK_EQ_INT((int)out->stats.filled, 2);

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_background_clipping(void)
{
    background_clip_probe_t clipped;
    background_clip_probe_t visible;
    robo_gfx_surface_t *clipped_surface = NULL;
    robo_gfx_surface_t *visible_surface = NULL;

    paint_fixture();

    clipped_surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    visible_surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_clip_run(1, &clipped, clipped_surface);
    background_clip_run(0, &visible, visible_surface);

    /* The same boxes, one property apart: the spill's own rectangle is 200x200 in both. */
    ROBO_CHECK_EQ_INT((int)clipped.spill_rect.width, 200);
    ROBO_CHECK_EQ_INT((int)clipped.spill_rect.height, 200);
    ROBO_CHECK_EQ_INT((int)clipped.spill_rect.x, (int)visible.spill_rect.x);
    ROBO_CHECK_EQ_INT((int)clipped.spill_rect.y, (int)visible.spill_rect.y);

    /* Only the hidden run bounds a fill, and it says so in the record. */
    ROBO_CHECK_EQ_INT((int)clipped.stats.clipped, 1);
    ROBO_CHECK_EQ_INT((int)visible.stats.clipped, 0);

    /* Inside the ancestor, both runs paint the spill's red over the ancestor's green -- the
     * later fill wins. */
    background_pixel_is(clipped_surface, clipped.spill_rect.x + 5, clipped.spill_rect.y + 5,
                        BACKGROUND_RED, "inside the clip, hidden");
    background_pixel_is(visible_surface, visible.spill_rect.x + 5, visible.spill_rect.y + 5,
                        BACKGROUND_RED, "inside the clip, visible");

    /* Far outside the ancestor but well inside the spill's own rectangle: the visible run
     * fills it, and the hidden run leaves it untouched. That difference **is** the clip. */
    background_pixel_is(visible_surface, visible.spill_rect.x + 150, visible.spill_rect.y + 150,
                        BACKGROUND_RED, "the spill outside its ancestor, visible overflow");
    background_pixel_is(clipped_surface, clipped.spill_rect.x + 150, clipped.spill_rect.y + 150,
                        robo_gfx_rgba8(0, 0, 0, 0),
                        "the spill outside its ancestor, hidden overflow");

    /* The mechanism, without a document to infer it from: a hand-built command whose clip
     * bounds one axis. The axis the clip leaves open is the surface's own interval, which is
     * what makes "bounded horizontally, free vertically" expressible at all. */
    {
        robo_paint_display_list_t *list = NULL;
        robo_paint_decoration_stats_t stats;
        /* A surface of its own: the document's fills are on the two above, and an assertion
         * about "untouched" needs somewhere nothing has been painted yet. */
        robo_gfx_surface_t *plain = background_surface(32u, 32u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);

        ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
        /* A rectangle taller than the surface, so that "the open axis was not bounded" is a
         * pixel that can be read rather than a claim about an interval nobody can see. */
        background_append(list,
                          background_make_draw((robo_layout_node_id_t)1u, robo_gfx_rect(0, 0, 10, 32),
                                               background_clip_x(0, 5),
                                               robo_css_color_make(0, 0, 255, 255)));
        background_paint(list, plain, &stats);
        ROBO_CHECK_EQ_INT((int)stats.filled, 1);
        ROBO_CHECK_EQ_INT((int)stats.clipped, 1);
        background_pixel_is(plain, 0, 0, BACKGROUND_BLUE, "inside the bounded axis");
        background_pixel_is(plain, 4, 31, BACKGROUND_BLUE,
                            "the interval's last column, at the bottom of the surface");
        background_pixel_is(plain, 5, 31, robo_gfx_rgba8(0, 0, 0, 0),
                            "one pixel past the interval");
        background_pixel_is(plain, 9, 9, robo_gfx_rgba8(0, 0, 0, 0),
                            "the far side of the rectangle");
        robo_paint_display_list_destroy(list);
        robo_gfx_surface_release(plain);
    }

    robo_gfx_surface_release(clipped_surface);
    robo_gfx_surface_release(visible_surface);
}

/* ------------------------------------------------------------------ */
/* 5. Nothing to paint                                                 */
/* ------------------------------------------------------------------ */

static void test_background_nothing_to_paint(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t content;
    robo_layout_node_id_t box;

    /* An undeclared background is `transparent` (its initial value) and a declared
     * `transparent` is the same colour, so neither emits a background command at all -- the
     * list's own length says so, before the painter is asked anything. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"plain\"></div>"
                         "<div class=\"clear\"></div></body></html>",
                         BACKGROUND_BASE_CSS
                         " .plain { width: 50px; height: 50px }"
                         " .clear { width: 50px; height: 50px; background-color: transparent }");
    paint_once(&fixture, &result);
    background_build(&result, &list, &list_stats);

    ROBO_CHECK_EQ_INT((int)list_stats.backgrounds, 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count_of_kind(
                          list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND),
                      0);

    surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_paint(list, surface, &stats);

    /* A legitimate pass that fills nothing, and every box visited anyway: the structural
     * records are still skipped and counted, which is what makes "the document has no
     * backgrounds" visible rather than silent. */
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)robo_paint_display_list_count(list));
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 0);
    ROBO_CHECK_EQ_INT((int)stats.filled, 0);
    ROBO_CHECK_EQ_INT((int)stats.skipped, (int)stats.commands);
    ROBO_CHECK((int)stats.skipped > 0);

    /* Where the boxes are, the surface is still the transparent it started as. */
    box = paint_box(&fixture, "clear");
    ROBO_CHECK_EQ_INT(background_command_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_CONTENT,
                                                     box, 0u, &content),
                      1);
    background_centre_is(surface, content.rect, robo_gfx_rgba8(0, 0, 0, 0),
                         "a box with no background");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/**
 * A document whose only decoration is a **border**: four bands, no background, and the pass
 * fills all four.
 *
 * This is the case that pins where the fence moved at 11.5. It used to state the opposite --
 * "a border band is resolved and not painted" -- because 11.4's painter consumed exactly one
 * command kind. The pass now consumes every **drawing** kind, so the same document that
 * produced a full display list and an untouched surface produces a full display list and four
 * coloured bands. What has *not* moved is the fence itself, and the two pixels below are where
 * that is visible: the band is coloured, and the box's own middle -- which no command covers,
 * because a border with no `background-color` draws nothing there -- still holds what the
 * surface started with.
 */
static void test_background_border_only_document(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t band;
    robo_paint_display_command_t content;
    robo_layout_node_id_t framed;
    int32_t x = 0;
    int32_t y = 0;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"framed\"></div></body></html>",
                         BACKGROUND_BASE_CSS
                         " .framed { width: 80px; height: 40px; border: 4px solid #00ff00 }");
    paint_once(&fixture, &result);
    background_build(&result, &list, &list_stats);

    ROBO_CHECK_EQ_INT((int)list_stats.borders, 4);
    ROBO_CHECK_EQ_INT((int)list_stats.backgrounds, 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count_of_kind(
                          list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND),
                      0);

    surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_paint(list, surface, &stats);

    /* Four drawings, four fills, no background and no pattern: `solid` is the one style whose
     * band *is* its shape, so nothing here is a simplification being counted. */
    ROBO_CHECK_EQ_INT((int)stats.draws, 4);
    ROBO_CHECK_EQ_INT((int)stats.borders, 4);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 0);
    ROBO_CHECK_EQ_INT((int)stats.filled, 4);
    ROBO_CHECK_EQ_INT((int)stats.unshaped, 0);
    ROBO_CHECK_EQ_INT((int)stats.empty, 0);
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)(stats.draws + stats.skipped));
    /* The document is three placed boxes -- `<html>`, `<body>` and the one `<div>` -- and each
     * contributes its three structural commands (`begin-node`, `content`, `end-node`), which draw
     * nothing by definition. So `skipped` is nine here, and every drawing is a band. */
    ROBO_CHECK_EQ_INT((int)stats.skipped, 9);
    ROBO_CHECK_EQ_INT((int)stats.skipped, (int)(stats.commands - stats.draws));

    /* The first band is the top side: the full width of the border box, four pixels tall. */
    ROBO_CHECK_EQ_INT(background_nth_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER, 0u, &band),
                      1);
    ROBO_CHECK_EQ_INT((int)band.rect.height, 4);
    ROBO_CHECK_EQ_INT((int)band.style, (int)ROBO_CSS_BORDER_STYLE_SOLID);
    background_centre(band.rect, &x, &y);
    background_pixel_is(surface, x, y, robo_gfx_rgba8(0x00, 0xff, 0x00, 255),
                        "the middle of the box's top band");

    /* And the box's own middle, which no command draws into: a border is a ring, so what it
     * encloses is the surface the caller handed over. */
    framed = paint_box(&fixture, "framed");
    ROBO_CHECK_EQ_INT(background_command_of_kind_for(list, ROBO_PAINT_DISPLAY_CMD_CONTENT,
                                                     framed, 0u, &content),
                      1);
    background_centre(content.rect, &x, &y);
    background_pixel_is(surface, x, y, robo_gfx_rgba8(0, 0, 0, 0),
                        "inside the border ring, where no command drew");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_background_zero_area(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_display_command_t background;
    robo_layout_node_id_t box;

    /* A declared, opaque background on a box with no height: the *record* is legitimate ("I
     * would fill this rectangle") and the *fill* is not ("there is no rectangle"), and the two
     * counters are what keep those apart. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"zero\"></div></body></html>",
                         BACKGROUND_BASE_CSS
                         " .zero { width: 30px; height: 0px; background-color: #ff0000 }");
    paint_once(&fixture, &result);
    background_build(&result, &list, &list_stats);

    box = paint_box(&fixture, "zero");
    ROBO_CHECK_EQ_INT(background_command_for(list, box, 0u, &background), 1);
    ROBO_CHECK_EQ_INT((int)background.rect.height, 0);

    surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_paint(list, surface, &stats);

    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 1);
    ROBO_CHECK_EQ_INT((int)stats.empty, 1);
    ROBO_CHECK_EQ_INT((int)stats.filled, 0);
    ROBO_CHECK_EQ_INT((int)stats.clipped, 0);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Positioned rectangles                                            */
/* ------------------------------------------------------------------ */

static void test_background_positions(void)
{
    robo_paint_display_list_stats_t list_stats;

    paint_fixture();

    /* Two documents, one declaration apart. The first is the control; the second moves the box
     * with a relative offset, and the two are compared rather than a coordinate being guessed,
     * so the assertion is about the offset and not about this build's default margin. */
    {
        paint_fixture_doc_t fixture;
        paint_result_t result;
        robo_paint_display_list_t *list = NULL;
        robo_paint_display_command_t background;
        robo_paint_decoration_stats_t stats;
        robo_gfx_surface_t *surface = NULL;
        robo_layout_node_id_t box;
        robo_layout_rect_t base;

        paint_begin_laid_out(&fixture,
                             "<html><body><div class=\"moved\"></div></body></html>",
                             BACKGROUND_BASE_CSS
                             " .moved { width: 50px; height: 20px; background-color: #112233 }");
        paint_once(&fixture, &result);
        background_build(&result, &list, &list_stats);
        box = paint_box(&fixture, "moved");
        ROBO_CHECK_EQ_INT(background_command_for(list, box, 0u, &background), 1);
        base = background.rect;

        surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
        background_paint(list, surface, &stats);
        ROBO_CHECK_EQ_INT((int)stats.filled, 1);
        background_centre_is(surface, base, robo_gfx_rgba8(0x11, 0x22, 0x33, 255),
                             "the unmoved box's background");
        /* The probe the moved document is checked against: inside this rectangle here. */
        background_pixel_is(surface, base.x + 2, base.y + 2, robo_gfx_rgba8(0x11, 0x22, 0x33, 255),
                            "the unmoved box's top-left corner");

        robo_gfx_surface_release(surface);
        robo_paint_display_list_destroy(list);
        paint_result_dispose(&result);
        paint_end(&fixture);

        /* The same box, relatively positioned: the final geometry carries the offset, so the
         * rectangle the display list resolves is the *moved* one. */
        paint_begin_laid_out(&fixture,
                             "<html><body><div class=\"moved\"></div></body></html>",
                             BACKGROUND_BASE_CSS
                             " .moved { width: 50px; height: 20px; background-color: #112233;"
                             "   position: relative; left: 20px; top: 10px }");
        paint_once(&fixture, &result);
        background_build(&result, &list, &list_stats);
        box = paint_box(&fixture, "moved");
        ROBO_CHECK_EQ_INT(background_command_for(list, box, 0u, &background), 1);

        ROBO_CHECK_EQ_INT((int)background.rect.x, (int)base.x + 20);
        ROBO_CHECK_EQ_INT((int)background.rect.y, (int)base.y + 10);
        /* The size is the size: an offset moves a box, it does not grow one. */
        ROBO_CHECK_EQ_INT((int)background.rect.width, (int)base.width);
        ROBO_CHECK_EQ_INT((int)background.rect.height, (int)base.height);

        surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
        background_paint(list, surface, &stats);
        ROBO_CHECK_EQ_INT((int)stats.filled, 1);

        /* The fill followed the box, and the place the box would have been is untouched --
         * read at the control's own top-left corner, which the 20px offset moved the fill
         * away from. */
        background_centre_is(surface, background.rect, robo_gfx_rgba8(0x11, 0x22, 0x33, 255),
                             "the moved box's background");
        background_pixel_is(surface, base.x + 2, base.y + 2, robo_gfx_rgba8(0, 0, 0, 0),
                            "where the box would have been, unmoved");

        robo_gfx_surface_release(surface);
        robo_paint_display_list_destroy(list);
        paint_result_dispose(&result);
        paint_end(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* 7. The counters                                                     */
/* ------------------------------------------------------------------ */

static void test_background_counters(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    size_t index;
    size_t opaque_centres = 0u;

    /* A document that uses the whole vocabulary at once: backgrounds, a border, a nested child
     * and a box with nothing declared. */
    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\"></section></div>"
                         "<div class=\"plain\"></div>"
                         "</body></html>",
                         BACKGROUND_BASE_CSS
                         " .card { width: 120px; height: 60px; padding: 6px;"
                         "   border: 2px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px; background-color: #123456 }"
                         " .plain { width: 20px; height: 20px; border: 1px solid #ff0000 }");
    paint_once(&fixture, &result);
    background_build(&result, &list, &list_stats);

    surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_paint(list, surface, &stats);

    /* Every command was visited, and each was one of the two things the pass can do with it --
     * which is the triple of identities the record's own header states, asserted here so that
     * `skipped` is a statement rather than a remainder nobody checked:
     *
     *   commands = draws + skipped
     *   draws    = backgrounds + borders
     *   draws    = filled + contextual + transparent + empty
     */
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)robo_paint_display_list_count(list));
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)(stats.draws + stats.skipped));
    ROBO_CHECK_EQ_INT((int)stats.draws, (int)(stats.backgrounds + stats.borders));
    ROBO_CHECK_EQ_INT((int)stats.backgrounds,
                      (int)robo_paint_display_list_count_of_kind(
                          list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND));
    ROBO_CHECK_EQ_INT((int)stats.borders,
                      (int)robo_paint_display_list_count_of_kind(
                          list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER));
    ROBO_CHECK_EQ_INT((int)stats.filled + (int)stats.contextual + (int)stats.transparent +
                          (int)stats.empty,
                      (int)stats.draws);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 2);
    /* The card's four sides at 2px and the plain box's four at 1px: eight bands, all of them
     * drawings, which is what 11.5 added to this pass. */
    ROBO_CHECK_EQ_INT((int)stats.borders, 8);
    ROBO_CHECK_EQ_INT((int)stats.filled, 10);
    ROBO_CHECK_EQ_INT((int)stats.contextual, 0);
    ROBO_CHECK_EQ_INT((int)stats.transparent, 0);
    ROBO_CHECK_EQ_INT((int)stats.empty, 0);
    /* Every border here is `solid`, so nothing is being drawn as a simplification. */
    ROBO_CHECK_EQ_INT((int)stats.unshaped, 0);
    ROBO_CHECK_EQ_INT((int)stats.clipped, 0);
    ROBO_CHECK((int)list_stats.borders == 8);
    ROBO_CHECK((int)stats.skipped > 0);

    /* And each of those two fills reached the surface -- asserted against the list's own
     * record of who writes where, rather than by a total that could be right for the wrong
     * reason or a centre that a later fill legitimately covers. */
    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        if (command.kind != ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND) {
            continue;
        }
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&command.clip), 0);
        opaque_centres++;
    }
    ROBO_CHECK_EQ_INT((int)opaque_centres, 2);
    background_pixels_match(list, surface, "a background command's own colour");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. The painter's own contract                                       */
/* ------------------------------------------------------------------ */

static void test_background_hand_built_kinds(void)
{
    robo_paint_display_list_t *list = NULL;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;

    paint_fixture();

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
    surface = background_surface(32u, 32u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);

    /* A structural command -- which draws nothing -- and then a border band, which is the
     * other **drawing** kind this pass fills: it needs its style, because that is the fact a
     * band's band-plus-colour cannot state (11.5). */
    background_append(list, background_make_structural((robo_layout_node_id_t)1u));
    {
        robo_paint_display_command_t band;

        robo_paint_display_command_init(&band);
        band.kind = ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER;
        band.node = (robo_layout_node_id_t)1u;
        band.rect = robo_gfx_rect(0, 0, 8, 2);
        band.color = robo_css_color_make(0, 255, 0, 255);
        band.style = ROBO_CSS_BORDER_STYLE_SOLID;
        background_append(list, band);
    }

    /* The three ways a background draws nothing, then one that does. */
    background_append(list, background_make_draw((robo_layout_node_id_t)1u, robo_gfx_rect(0, 0, 8, 8),
                                                 robo_paint_clip_none(), robo_css_color_current()));
    background_append(list, background_make_draw((robo_layout_node_id_t)1u, robo_gfx_rect(0, 0, 8, 8),
                                                 robo_paint_clip_none(),
                                                 robo_css_color_make(0, 0, 0, 0)));
    background_append(list, background_make_draw((robo_layout_node_id_t)1u, robo_gfx_rect(4, 4, 0, 0),
                                                 robo_paint_clip_none(),
                                                 robo_css_color_make(255, 0, 0, 255)));
    background_append(list, background_make_draw((robo_layout_node_id_t)1u, robo_gfx_rect(2, 2, 4, 4),
                                                 robo_paint_clip_none(),
                                                 robo_css_color_make(0, 0, 255, 255)));

    background_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.commands, 6);
    ROBO_CHECK_EQ_INT((int)stats.draws, 5);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 4);
    ROBO_CHECK_EQ_INT((int)stats.borders, 1);
    ROBO_CHECK_EQ_INT((int)stats.skipped, 1);
    ROBO_CHECK_EQ_INT((int)stats.contextual, 1);
    ROBO_CHECK_EQ_INT((int)stats.transparent, 1);
    ROBO_CHECK_EQ_INT((int)stats.empty, 1);
    ROBO_CHECK_EQ_INT((int)stats.filled, 2);
    ROBO_CHECK_EQ_INT((int)stats.unshaped, 0);
    ROBO_CHECK_EQ_INT((int)stats.clipped, 0);

    /* The band was filled where the command put it, and the drawable background where its own
     * rectangle is -- a photo of the two kinds in list order. */
    background_pixel_is(surface, 3, 0, robo_gfx_rgba8(0, 255, 0, 255),
                        "the hand-built border band");
    background_pixel_is(surface, 3, 3, BACKGROUND_BLUE, "the one drawable background");
    background_pixel_is(surface, 9, 9, robo_gfx_rgba8(0, 0, 0, 0), "past every background");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
}

/* ------------------------------------------------------------------ */
/* 9. Determinism                                                      */
/* ------------------------------------------------------------------ */

static void test_background_determinism(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
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
                         BACKGROUND_BASE_CSS
                         " .card { width: 120px; height: 60px; padding: 6px;"
                         "   border: 2px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px; background-color: #123456 }");
    paint_once(&fixture, &result);
    background_build(&result, &list, &list_stats);

    one = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    two = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_paint(list, one, &first);
    background_paint(list, two, &second);
    background_stats_eq(&first, &second, "two passes over one list");

    /* Every fill landed in the same place, pixel for pixel -- the sample is each background
     * command's own rectangle, so the comparison follows the document rather than a fixed
     * handful of coordinates. */
    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;
        robo_gfx_color_rgba8_t left;
        robo_gfx_color_rgba8_t right;
        int32_t x = 0;
        int32_t y = 0;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        if (command.kind != ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND) {
            continue;
        }
        background_centre(command.rect, &x, &y);
        left = background_read(one, x, y);
        right = background_read(two, x, y);
        ROBO_CHECK_EQ_INT((int)left.r, (int)right.r);
        ROBO_CHECK_EQ_INT((int)left.g, (int)right.g);
        ROBO_CHECK_EQ_INT((int)left.b, (int)right.b);
        ROBO_CHECK_EQ_INT((int)left.a, (int)right.a);
    }

    /* And a second pass over the *same* surface leaves it as it was: an opaque fill is
     * idempotent, which is what makes a rebuilt display list safe to repaint in place. */
    background_paint(list, one, &again);
    background_stats_eq(&first, &again, "a repeat pass over one surface");
    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        if (command.kind != ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND) {
            continue;
        }
        background_centre_is(one, command.rect,
                             robo_gfx_rgba8(command.color.red, command.color.green,
                                            command.color.blue, command.color.alpha),
                             "a fill after the surface was painted twice");
    }

    robo_gfx_surface_release(one);
    robo_gfx_surface_release(two);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 10. Snapshot isolation                                              */
/* ------------------------------------------------------------------ */

static void test_background_mutation_isolation(void)
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
                         BACKGROUND_BASE_CSS
                         " .box { height: 40px; background-color: #ff0000 }");
    paint_once(&fixture, &result);
    background_build(&result, &before, &before_stats);

    one = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_paint(before, one, &first);

    /*
     * Mutate both sources: clear the record list (the strongest legal change to it) and lay the
     * *same* tree out at a different viewport, which changes every box's geometry. A display
     * list is a snapshot, so the one already built paints exactly as it did -- and the mutation
     * is *proved* observable by building a second display list afterwards and asserting it
     * differs.
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
    background_build(&result, &after, &after_stats);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(before, after), 0);

    two = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_paint(before, two, &second);
    background_stats_eq(&first, &second, "a paint after the sources changed");

    for (index = 0u; index < robo_paint_display_list_count(before); index++) {
        robo_paint_display_command_t command;
        robo_gfx_color_rgba8_t left;
        robo_gfx_color_rgba8_t right;
        int32_t x = 0;
        int32_t y = 0;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(before, index, &command), ROBO_OK);
        if (command.kind != ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND) {
            continue;
        }
        background_centre(command.rect, &x, &y);
        left = background_read(one, x, y);
        right = background_read(two, x, y);
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
/* 11. Empty, degenerate and refusing cases                            */
/* ------------------------------------------------------------------ */

static void test_background_empty_cases(void)
{
    robo_paint_display_list_t *list = NULL;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    robo_gfx_surface_t *readonly = NULL;
    robo_paint_diag_t diag;

    paint_fixture();

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
    surface = background_surface(16u, 16u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);

    /* An empty list is a legitimate pass: nothing to do, and every counter nought. */
    memset(&stats, 0xff, sizeof(stats));
    robo_paint_decoration_stats_init(NULL); /* safe */
    background_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.commands, 0);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 0);
    ROBO_CHECK_EQ_INT((int)stats.filled, 0);
    ROBO_CHECK_EQ_INT((int)stats.skipped, 0);

    /* A list of structural commands alone: visited, skipped, and nothing drawn. */
    background_append(list, background_make_structural((robo_layout_node_id_t)1u));
    background_append(list, background_make_structural((robo_layout_node_id_t)1u));
    background_paint(list, surface, &stats);
    ROBO_CHECK_EQ_INT((int)stats.commands, 2);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, 0);
    ROBO_CHECK_EQ_INT((int)stats.skipped, 2);
    background_pixel_is(surface, 8, 8, robo_gfx_rgba8(0, 0, 0, 0),
                        "a surface no background touched");

    /* The arguments are required, and a refusal says which. */
    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_paint_decoration_paint(NULL, surface, NULL, NULL, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(diag.message != NULL);
    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_paint_decoration_paint(list, NULL, NULL, NULL, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(diag.message != NULL);

    /* And a target that cannot take a write is refused with the code the header names, before
     * any fill is attempted -- the graphics layer's own rule, mapped once. */
    readonly = background_surface(16u, 16u, ROBO_GFX_SURFACE_ACCESS_READ);
    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_paint_decoration_paint(list, readonly, NULL, NULL, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK(diag.message != NULL);
    robo_gfx_surface_release(readonly);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
}

/* ------------------------------------------------------------------ */
/* 12. End to end                                                      */
/* ------------------------------------------------------------------ */

static void test_background_end_to_end(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t list_stats;
    robo_paint_decoration_stats_t stats;
    robo_gfx_surface_t *surface = NULL;
    char dump[8192];
    size_t dump_length = 0u;
    size_t index;
    size_t clipped_commands = 0u;

    /*
     * The whole pipeline in one case: HTML → CSS → layout → paint records → display list →
     * surfaces. The document exercises the box model (width/height/margin/padding/border), a
     * nested child, a relative offset and both overflow modes, so the walk it produces has
     * every shape a consumer will meet.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"outer\">"
                         "  <div class=\"inner\"></div>"
                         "</div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         BACKGROUND_BASE_CSS
                         " .outer { width: 200px; height: 80px; padding: 8px; margin: 4px;"
                         "   border: 2px solid #000000; background-color: #eeeeee }"
                         " .inner { width: 100px; height: 40px; margin: 6px;"
                         "   background-color: #00ff00; position: relative; left: 5px }"
                         " .clip { width: 60px; height: 30px; overflow: hidden;"
                         "   background-color: #00ff00 }"
                         " .spill { width: 200px; height: 200px; background-color: #ff0000 }");
    paint_once(&fixture, &result);
    background_build(&result, &list, &list_stats);

    surface = background_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    background_paint(list, surface, &stats);

    /* The pass's own identities hold for a real document, not just a hand-built one. */
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)robo_paint_display_list_count(list));
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)(stats.draws + stats.skipped));
    ROBO_CHECK_EQ_INT((int)stats.draws, (int)(stats.backgrounds + stats.borders));
    ROBO_CHECK_EQ_INT((int)stats.backgrounds,
                      (int)robo_paint_display_list_count_of_kind(
                          list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND));
    ROBO_CHECK_EQ_INT((int)stats.borders,
                      (int)robo_paint_display_list_count_of_kind(
                          list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER));
    ROBO_CHECK_EQ_INT((int)stats.filled + (int)stats.contextual + (int)stats.transparent +
                          (int)stats.empty,
                      (int)stats.draws);
    /* Four backgrounds, the outer box's four bands, and every one of them opaque and of a
     * non-empty rectangle -- so every drawing reaches the surface. */
    ROBO_CHECK((int)stats.backgrounds >= 2);
    ROBO_CHECK((int)stats.borders >= 4);
    ROBO_CHECK_EQ_INT((int)stats.filled, (int)stats.draws);
    ROBO_CHECK((int)list_stats.borders >= 4);

    /* Every command is a record, and the ones under an ancestor's boundary are the ones the
     * pass counted as clipped -- a drawing of either kind, because a band under a hidden
     * ancestor is bounded exactly as a background is. */
    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);
        if (robo_paint_display_command_kind_draws(command.kind) &&
            robo_paint_clip_is_active(&command.clip)) {
            clipped_commands++;
        }
    }
    ROBO_CHECK_EQ_INT((int)clipped_commands, (int)stats.clipped);
    ROBO_CHECK((int)stats.clipped >= 1);

    /* The pixels follow the list -- including where a descendant's fill covers an ancestor's,
     * and where the clip correctly stops one short of the descendant's own rectangle. */
    background_pixels_match(list, surface, "a background in a mixed document");

    /* The developer's door still reads the input the painter consumed. */
    memset(dump, 0, sizeof(dump));
    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(list, dump, sizeof(dump), &dump_length, NULL),
                      ROBO_OK);
    ROBO_CHECK(dump_length > 0u);
    ROBO_CHECK(strstr(dump, "draw-background") != NULL);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_background(void)
{
    robo_test_register("paint.background.color_bridge", test_background_color_bridge);
    robo_test_register("paint.background.pixel_fill", test_background_pixel_fill);
    robo_test_register("paint.background.ordering", test_background_ordering);
    robo_test_register("paint.background.clipping", test_background_clipping);
    robo_test_register("paint.background.nothing_to_paint", test_background_nothing_to_paint);
    robo_test_register("paint.background.border_only_document",
                       test_background_border_only_document);
    robo_test_register("paint.background.zero_area", test_background_zero_area);
    robo_test_register("paint.background.positions", test_background_positions);
    robo_test_register("paint.background.counters", test_background_counters);
    robo_test_register("paint.background.hand_built_kinds", test_background_hand_built_kinds);
    robo_test_register("paint.background.determinism", test_background_determinism);
    robo_test_register("paint.background.mutation_isolation",
                       test_background_mutation_isolation);
    robo_test_register("paint.background.empty_cases", test_background_empty_cases);
    robo_test_register("paint.background.end_to_end", test_background_end_to_end);
}
