/*
 * Shared RoboDesktop test fixtures (Phase 11.13).
 *
 * Every suite in `desktop/tests` includes this. Like the paint and render support headers it is **not** a
 * framework -- the runtime has exactly one of those (`Tests/Framework/RoboTest.h`) and this release adds
 * none -- and it holds the four things a desktop suite would otherwise copy:
 *
 *   bring-up    every layer the shell draws on, through `RoboRenderTestSupport.h` rather than beside it.
 *               A desktop fixture *is* a render fixture plus a shell, and a second copy of "build a
 *               document, style it, lay it out, paint it, list it" would be a second thing to keep in step
 *               with the pipeline this module merely composites;
 *
 *   a face      the render fixture's synthetic SFNT, so a window's text is measured and drawn by a real
 *               face and a desktop case can assert that a title *drew* rather than that it was asked to.
 *               The shell's own chrome face is the same face: one face for chrome and content keeps a
 *               pixel assertion comparable across the two;
 *
 *   a screen    an owned desktop at a named size with the default theme and metrics, plus the two reads a
 *               pixel assertion needs. The pixel read goes through `robo_desktop_frame_surface` and
 *               RoboGraphics' own `robo_gfx_surface_get_pixel` deliberately: this module has no pixel door
 *               of its own, because reading a pixel is what the graphics layer is for;
 *
 *   a document  `desktop_window_content` builds a real application document (markup, an author sheet,
 *               layout, paint, display list) for a window's client area, at the client size the *shell*
 *               computed -- so a case can say "this window shows this markup" in one call without
 *               re-deriving the client rectangle, which is the door the shell itself would use.
 *
 * Every helper asserts rather than returning a status, which is the convention the layout, paint and
 * render suites set: a suite that had to check each step of its own scaffolding would drown its subject in
 * harness.
 */

#ifndef ROBO_DESKTOP_TEST_SUPPORT_H
#define ROBO_DESKTOP_TEST_SUPPORT_H

#include "RoboRenderTestSupport.h"

#include "RoboDesktop/RoboDesktop.h"
#include "RoboDesktop/RoboDesktopDocument.h"
/* The blend itself, for a fixture that has to state an expected pixel for a *translucent* drawing: the
 * panel's falloff rows are a shadow over the surface, and a test that hand-computed the blend would be a
 * second implementation of it. */
#include "RoboGraphics/RoboGraphicsComposite.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Bring-up                                                            */
/* ------------------------------------------------------------------ */

/** Bring every layer a desktop test needs up (once) and leave them up. */
static inline void desktop_fixture(void)
{
    render_fixture();
}

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t desktop_live_blocks(void)
{
    return robo_mem_debug_live_count();
}

/** This build's default theme, by value. */
static inline robo_desktop_theme_t desktop_theme(void)
{
    robo_desktop_theme_t theme;

    robo_desktop_theme_init(&theme);
    return theme;
}

/** This build's default panel metrics, by value. */
static inline robo_desktop_panel_metrics_t desktop_panel_metrics(void)
{
    robo_desktop_panel_metrics_t metrics;

    robo_desktop_panel_metrics_init(&metrics);
    return metrics;
}

/** This build's default window decoration metrics, by value. */
static inline robo_desktop_window_metrics_t desktop_window_metrics(void)
{
    robo_desktop_window_metrics_t metrics;

    robo_desktop_window_metrics_init(&metrics);
    return metrics;
}

/* ------------------------------------------------------------------ */
/* A screen                                                            */
/* ------------------------------------------------------------------ */

/**
 * A desktop at @p width by @p height with no native window.
 *
 * No window on purpose: this build's desktop is presentable without one (`robo_desktop_present` reports
 * `ROBO_NOT_SUPPORTED` and the frames are still real), and a suite that created one would need a display
 * session to check a rectangle. The cases that *are* about presentation take a window explicitly.
 */
static inline robo_desktop_t *desktop_open(int32_t width, int32_t height)
{
    robo_desktop_config_t config;
    robo_desktop_t *desktop = NULL;

    desktop_fixture();
    robo_desktop_config_init(&config);
    config.width = width;
    config.height = height;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_OK);
    ROBO_CHECK(desktop != NULL);
    return desktop;
}

/** The same, with a chrome face: a desktop that draws its labels. */
static inline robo_desktop_t *desktop_open_with_face(int32_t width, int32_t height,
                                                    robo_font_face_t *face)
{
    robo_desktop_config_t config;
    robo_desktop_t *desktop = NULL;

    desktop_fixture();
    robo_desktop_config_init(&config);
    config.width = width;
    config.height = height;
    config.face = face;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_OK);
    ROBO_CHECK(desktop != NULL);
    return desktop;
}

static inline void desktop_close(robo_desktop_t *desktop)
{
    robo_desktop_destroy(desktop);
}

/** One window, asserted to have been added. */
static inline robo_desktop_window_id_t desktop_add_window(robo_desktop_t *desktop,
                                                          const char *title,
                                                          int32_t x,
                                                          int32_t y,
                                                          int32_t width,
                                                          int32_t height)
{
    robo_desktop_window_desc_t desc;
    robo_desktop_window_id_t id = ROBO_DESKTOP_WINDOW_ID_NONE;

    robo_desktop_window_desc_init(&desc);
    desc.title = title;
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    ROBO_CHECK_EQ_INT(robo_desktop_window_add(desktop, &desc, &id, NULL), ROBO_OK);
    ROBO_CHECK(id != ROBO_DESKTOP_WINDOW_ID_NONE);
    return id;
}

/* ------------------------------------------------------------------ */
/* Pixels                                                              */
/* ------------------------------------------------------------------ */

/** The frame's colour at @p x,@p y, through the graphics layer's own read. */
static inline robo_gfx_color_rgba8_t desktop_pixel(const robo_desktop_t *desktop, uint32_t x, uint32_t y)
{
    robo_gfx_color_rgba8_t color;
    const robo_gfx_surface_t *surface = robo_desktop_frame_surface(desktop);

    memset(&color, 0, sizeof(color));
    ROBO_CHECK(surface != NULL);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, x, y, &color), ROBO_OK);
    return color;
}

static inline int desktop_color_is(robo_gfx_color_rgba8_t a, robo_gfx_color_rgba8_t b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/** Assert the frame's pixel at @p x,@p y is exactly @p expected. */
static inline void desktop_pixel_is(const robo_desktop_t *desktop,
                                    int32_t x,
                                    int32_t y,
                                    robo_gfx_color_rgba8_t expected,
                                    const char *what)
{
    robo_gfx_color_rgba8_t actual = desktop_pixel(desktop, (uint32_t)x, (uint32_t)y);

    if (!desktop_color_is(actual, expected)) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: pixel %d,%d is %u,%u,%u,%u, expected %u,%u,%u,%u", what, (int)x, (int)y,
                       (unsigned)actual.r, (unsigned)actual.g, (unsigned)actual.b,
                       (unsigned)actual.a, (unsigned)expected.r, (unsigned)expected.g,
                       (unsigned)expected.b, (unsigned)expected.a);
    }
}

/** Assert the frame's pixel at @p x,@p y is **not** @p other. */
static inline void desktop_pixel_is_not(const robo_desktop_t *desktop,
                                        int32_t x,
                                        int32_t y,
                                        robo_gfx_color_rgba8_t other,
                                        const char *what)
{
    robo_gfx_color_rgba8_t actual = desktop_pixel(desktop, (uint32_t)x, (uint32_t)y);

    if (desktop_color_is(actual, other)) {
        robo_test_fail(__FILE__, __LINE__, "%s: pixel %d,%d is still %u,%u,%u,%u", what, (int)x,
                       (int)y, (unsigned)actual.r, (unsigned)actual.g, (unsigned)actual.b,
                       (unsigned)actual.a);
    }
}

/** Non-zero when any pixel in the half-open rectangle differs from @p background. */
static inline int desktop_region_has_ink(const robo_desktop_t *desktop,
                                         int32_t x,
                                         int32_t y,
                                         int32_t w,
                                         int32_t h,
                                         robo_gfx_color_rgba8_t background)
{
    robo_gfx_size_t size = robo_desktop_screen_size(desktop);
    int32_t x0 = (x < 0) ? 0 : x;
    int32_t y0 = (y < 0) ? 0 : y;
    int32_t x1 = ((x + w) > size.width) ? size.width : (x + w);
    int32_t y1 = ((y + h) > size.height) ? size.height : (y + h);
    int32_t px;
    int32_t py;

    for (py = y0; py < y1; py++) {
        for (px = x0; px < x1; px++) {
            if (!desktop_color_is(desktop_pixel(desktop, (uint32_t)px, (uint32_t)py), background)) {
                return 1;
            }
        }
    }
    return 0;
}

/** Assert that something drew inside the region. */
static inline void desktop_region_has_ink_is(const robo_desktop_t *desktop,
                                            int32_t x,
                                            int32_t y,
                                            int32_t w,
                                            int32_t h,
                                            robo_gfx_color_rgba8_t background,
                                            const char *what)
{
    if (!desktop_region_has_ink(desktop, x, y, w, h, background)) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: no pixel in %d,%d,%d,%d differs from the background", what, (int)x,
                       (int)y, (int)w, (int)h);
    }
}

/** Assert that nothing drew inside the region. */
static inline void desktop_region_is_clear(const robo_desktop_t *desktop,
                                          int32_t x,
                                          int32_t y,
                                          int32_t w,
                                          int32_t h,
                                          robo_gfx_color_rgba8_t background,
                                          const char *what)
{
    if (desktop_region_has_ink(desktop, x, y, w, h, background)) {
        robo_test_fail(__FILE__, __LINE__, "%s: a pixel in %d,%d,%d,%d is not the background", what,
                       (int)x, (int)y, (int)w, (int)h);
    }
}

/* ------------------------------------------------------------------ */
/* A window's contents                                                 */
/* ------------------------------------------------------------------ */

/**
 * Build a real application document for @p id's client area and attach it.
 *
 * The client rectangle comes from the *shell* (`robo_desktop_window_client_bounds`), so a document is
 * exactly the size the window will composite it at -- which is the same order the host launcher and the
 * guest build in, one call each. The document object is returned so the caller can release it after the
 * window is destroyed, which is the lifetime `robo_desktop_window_set_content` states.
 */
static inline robo_desktop_document_t *desktop_set_window_content(robo_desktop_t *desktop,
                                                                 robo_desktop_window_id_t id,
                                                                 const char *html,
                                                                 const char *css,
                                                                 robo_font_face_t *face)
{
    robo_desktop_document_config_t config;
    robo_desktop_document_t *document = NULL;
    robo_gfx_rect_t client;

    ROBO_CHECK_EQ_INT(robo_desktop_window_client_bounds(desktop, id, &client), ROBO_OK);
    robo_desktop_document_config_init(&config);
    config.html = html;
    config.css = css;
    config.width = client.width;
    config.height = client.height;
    config.face = face;
    ROBO_CHECK_EQ_INT(robo_desktop_document_build(&config, &document, NULL), ROBO_OK);
    ROBO_CHECK(document != NULL);
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_content(desktop, id,
                                                     robo_desktop_document_display_list(document),
                                                     face, NULL),
                      ROBO_OK);
    return document;
}

/* ------------------------------------------------------------------ */
/* The visual checkpoint                                               */
/* ------------------------------------------------------------------ */

/**
 * Encode the current frame and write it to @p path. Returns non-zero on success.
 *
 * Deliberately **not** an assertion, for the reason the render fixture's own checkpoint gives: a
 * checkpoint is evidence for a person to look at, and a suite whose verdict depended on the filesystem
 * would fail on a machine with a read-only tree while the pixels it verified were perfectly good.
 */
static inline int desktop_write_png(const robo_desktop_t *desktop, const char *path)
{
    robo_gfx_encode_options_t options;
    robo_gfx_encoded_buffer_t buffer;
    const robo_gfx_surface_t *surface = robo_desktop_frame_surface(desktop);
    FILE *file;

    if (!surface) {
        return 0;
    }
    robo_gfx_encode_options_init(&options);
    options.container = ROBO_GFX_ENCODE_PNG;
    options.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    memset(&buffer, 0, sizeof(buffer));
    if (robo_gfx_surface_encode(surface, &options, &buffer) != ROBO_OK) {
        return 0;
    }
    file = fopen(path, "wb");
    if (!file) {
        robo_gfx_encoded_buffer_release(&buffer);
        return 0;
    }
    if (fwrite(buffer.data, 1u, buffer.size, file) != buffer.size) {
        fclose(file);
        robo_gfx_encoded_buffer_release(&buffer);
        return 0;
    }
    fclose(file);
    robo_gfx_encoded_buffer_release(&buffer);
    return 1;
}

/** `Out/Desktop.png` unless the environment names somewhere else. */
static inline const char *desktop_checkpoint_path(void)
{
    const char *override = getenv("ROBO_DESKTOP_PNG");

    return (override && override[0] != '\0') ? override : "Out/Desktop.png";
}

#endif /* ROBO_DESKTOP_TEST_SUPPORT_H */
