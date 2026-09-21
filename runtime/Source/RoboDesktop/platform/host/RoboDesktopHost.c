/*
 * RoboDesktop's host launcher (Phase 11.13) — see RoboDesktopHost.h.
 *
 * The whole of the checkpoint on a host, in two phases:
 *
 *   offscreen   the scene renders into its own screen target with no window at all, and the frame is
 *               *read*: exact pixels at named places, the shell's own frame record, and — the check that
 *               matters most — the two counts that say whether text reached the picture
 *               (`labels` against `labels_unshaped`). A run with no desktop session still verifies every
 *               pixel, which is what makes this usable from a script.
 *
 *   windowed    the same scene in a real native window: `robo_desktop_create` creates it, the shell's
 *               `present` door drives its client area, and pointer input is delivered through
 *               `robo_window_observe_mouse` so a press travels observe → queue → poll → hit test → action.
 *               The last thing this phase does is press the window's close button, because that is the only
 *               shell action that *destroys* something and the only one a picture cannot prove.
 *
 * What is asserted, and why these things: the panel is the shell's own surface, the work area's corner is
 * the desktop's, the window's title band is drawn, the client area's first pixels are the document's *page*
 * background (a value only the CSS the scene ships can produce), the one accent appears both in the panel
 * (the identity mark) and in the document (the button), and every label was shaped by a real face. Each of
 * those fails if one stage of the pipeline stops doing its job, which is the property a checkpoint is for.
 *
 * Exit codes: 0 verified, 1 a stage or probe failed, 2 a usage error.
 */

#include "RoboDesktop/RoboDesktopHost.h"

#include "RoboBase/RoboBase.h"
#include "RoboCSS/RoboCSS.h"
#include "RoboDesktop/RoboDesktop.h"
#include "RoboDesktop/RoboDesktopDocument.h"
#include "RoboDesktop/RoboDesktopScene.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendHost.h"
#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboLayout/RoboLayout.h"
#include "RoboPaint/RoboPaint.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackend.h"
#include "RoboWindow/RoboWindowBackendHost.h"
#include "RoboWindow/RoboWindowEventLoop.h"
#include "RoboWindow/RoboWindowMouse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The face the checkpoint draws with, as the *build* knows where it is. The fallback is the path inside the
 * runtime tree, so a source tree compiled by hand still finds it. */
#if defined(ROBO_FONT_ASSET_PATH_FROM_BUILD)
#include "robo_font_asset_path.h"
#endif
#ifndef ROBO_FONT_ASSET_PATH
#define ROBO_FONT_ASSET_PATH "desktop/assets/Inter.ttf"
#endif

#define HOST_KEY "RoboDesktop"

#define HOST_FRAME_WAIT_MS 16u
#define HOST_HOLD_DEFAULT_MS 4000u
#define HOST_LATE_WAIT_MS 64u
#define HOST_ACTION_ATTEMPTS 40u

/* The checkpoint's status label. A fixed string rather than a clock, because a launcher that stamped the
 * time into a frame would produce a different picture every run — and this file's whole job is to make one
 * picture checkable. */
#define HOST_STATUS_TEXT "11.13"

typedef struct host_report {
    int checks;
    int failures;
} host_report_t;

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

static robo_gfx_color_rgba8_t host_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    robo_gfx_color_rgba8_t color;

    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

/* RGB-only compare: alpha packing differs between RGBA8 and BGRA8 targets, and every probe here is about
 * *which* colour landed rather than about its storage order. */
static int host_color_is(robo_gfx_color_rgba8_t a, robo_gfx_color_rgba8_t b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static void host_probe_true(int condition, const char *what, host_report_t *report)
{
    report->checks++;
    if (condition) {
        printf("probe OK   %s\n", what);
        return;
    }
    report->failures++;
    printf("probe FAIL %s\n", what);
}

static void host_probe_pixel(const robo_gfx_surface_t *surface,
                             int32_t x,
                             int32_t y,
                             robo_gfx_color_rgba8_t expected,
                             const char *what,
                             host_report_t *report)
{
    robo_gfx_color_rgba8_t got;

    report->checks++;
    memset(&got, 0, sizeof(got));
    if (!surface || x < 0 || y < 0 ||
        robo_gfx_surface_get_pixel(surface, (uint32_t)x, (uint32_t)y, &got) != ROBO_OK) {
        report->failures++;
        printf("probe FAIL %s  no pixel at %d,%d\n", what, (int)x, (int)y);
        return;
    }
    if (!host_color_is(got, expected)) {
        report->failures++;
        printf("probe FAIL %s  pixel %d,%d is %u,%u,%u expected %u,%u,%u\n", what, (int)x, (int)y,
               (unsigned)got.r, (unsigned)got.g, (unsigned)got.b, (unsigned)expected.r,
               (unsigned)expected.g, (unsigned)expected.b);
        return;
    }
    printf("probe OK   %s\n", what);
}

/** Non-zero when @p color appears anywhere in @p rect. */
static int host_rect_has_color(const robo_gfx_surface_t *surface,
                               robo_gfx_rect_t rect,
                               robo_gfx_color_rgba8_t color)
{
    int32_t y;

    if (!surface) {
        return 0;
    }
    for (y = rect.y; y < rect.y + rect.height; y++) {
        int32_t x;

        for (x = rect.x; x < rect.x + rect.width; x++) {
            robo_gfx_color_rgba8_t got;

            if (x < 0 || y < 0) {
                continue;
            }
            if (robo_gfx_surface_get_pixel(surface, (uint32_t)x, (uint32_t)y, &got) != ROBO_OK) {
                continue;
            }
            if (host_color_is(got, color)) {
                return 1;
            }
        }
    }
    return 0;
}

static void host_probe_region_has_color(const robo_gfx_surface_t *surface,
                                        robo_gfx_rect_t rect,
                                        robo_gfx_color_rgba8_t color,
                                        const char *what,
                                        host_report_t *report)
{
    report->checks++;
    if (host_rect_has_color(surface, rect, color)) {
        printf("probe OK   %s\n", what);
        return;
    }
    report->failures++;
    printf("probe FAIL %s  no %u,%u,%u inside %d,%d,%d,%d\n", what, (unsigned)color.r,
           (unsigned)color.g, (unsigned)color.b, (int)rect.x, (int)rect.y, (int)rect.width,
           (int)rect.height);
}

/* ------------------------------------------------------------------ */
/* The face                                                            */
/* ------------------------------------------------------------------ */

typedef struct host_face {
    robo_font_source_t *source;
    robo_font_face_t *face;
} host_face_t;

/**
 * Load the checkpoint's face from @p path.
 *
 * A missing or unreadable file is **not** fatal: a desktop with no face draws its geometry and its
 * document's fills and counts the labels it could not shape, which `RoboDesktop.h` treats as a state rather
 * than an error. What is fatal is a file that is there and does not parse — that is a broken asset, and a
 * checkpoint that silently degraded to a textless desktop would report the wrong thing about the pipeline.
 *
 * @return 1 loaded, 0 absent, -1 present but unusable.
 */
static int host_face_load(const char *path, host_face_t *out)
{
    FILE *file;
    long size;
    unsigned char *bytes;
    size_t read_bytes;

    out->source = NULL;
    out->face = NULL;
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    size = ftell(file);
    if (size <= 0l || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    bytes = (unsigned char *)malloc((size_t)size);
    if (!bytes) {
        fclose(file);
        return -1;
    }
    read_bytes = fread(bytes, 1u, (size_t)size, file);
    fclose(file);
    if (read_bytes != (size_t)size) {
        free(bytes);
        return -1;
    }

    /* `create_memory_copy` copies, so the file's bytes stop being this file's business here. */
    if (robo_font_source_create_memory_copy(bytes, read_bytes, &out->source) != ROBO_OK) {
        free(bytes);
        return -1;
    }
    free(bytes);
    if (robo_font_face_create(out->source, 0, &out->face) != ROBO_OK) {
        robo_font_source_release(out->source);
        out->source = NULL;
        return -1;
    }
    return 1;
}

static void host_face_release(host_face_t *face)
{
    if (face->face) {
        robo_font_face_release(face->face);
        face->face = NULL;
    }
    if (face->source) {
        robo_font_source_release(face->source);
        face->source = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* The checkpoint's picture                                            */
/* ------------------------------------------------------------------ */

/** Encode @p surface and write it to @p path. Non-zero on success. */
static int host_write_png(const robo_gfx_surface_t *surface, const char *path)
{
    robo_gfx_encode_options_t options;
    robo_gfx_encoded_buffer_t buffer;
    FILE *file;

    if (!surface || !path) {
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

/**
 * Read the frame back and assert what only the whole pipeline could have put there.
 *
 * Every expectation below is a *documented* value rather than a snapshot: the panel's white, the desktop
 * surface's `#EDEDEF`, the window's title band, the page background `#F5F5F7` that only the scene's CSS
 * produces, and the one accent `#0E7AFF` twice — the shell's identity mark and the document's button.
 */
static void host_probe_scene(robo_desktop_scene_t *scene,
                             const robo_desktop_frame_record_t *frame,
                             host_report_t *report)
{
    robo_desktop_t *desktop = robo_desktop_scene_desktop(scene);
    robo_desktop_window_id_t window = robo_desktop_scene_window(scene);
    const robo_gfx_surface_t *surface = robo_desktop_frame_surface(desktop);
    robo_desktop_geometry_t geometry = robo_desktop_geometry_of(desktop);
    robo_gfx_size_t size = robo_desktop_screen_size(desktop);
    robo_desktop_theme_t theme;
    robo_desktop_window_metrics_t window_metrics;
    robo_gfx_rect_t bounds;
    robo_gfx_rect_t client;
    robo_desktop_document_report_t document_report;

    robo_desktop_theme_init(&theme);
    robo_desktop_window_metrics_init(&window_metrics);
    memset(&bounds, 0, sizeof(bounds));
    memset(&client, 0, sizeof(client));

    host_probe_pixel(surface, size.width - 2, 1, theme.panel_surface, "panel surface", report);
    host_probe_pixel(surface, size.width - 2, size.height - 2, theme.desktop_surface,
                     "desktop surface at the work area's corner", report);
    host_probe_region_has_color(surface, geometry.panel, theme.identity_accent,
                                "the shell's one accent in the panel", report);

    if (window != ROBO_DESKTOP_WINDOW_ID_NONE &&
        robo_desktop_window_bounds(desktop, window, &bounds) == ROBO_OK) {
        host_probe_region_has_color(
            surface,
            robo_gfx_rect(bounds.x, bounds.y, bounds.width,
                          window_metrics.border + window_metrics.title_height),
            theme.window_title_surface, "the window's title bar", report);
    } else {
        host_probe_true(0, "the shell holds a window to probe", report);
    }

    if (window != ROBO_DESKTOP_WINDOW_ID_NONE &&
        robo_desktop_window_client_bounds(desktop, window, &client) == ROBO_OK && client.width > 2 &&
        client.height > 2) {
        /* The page's own background, which is the scene's stylesheet and nothing else. */
        host_probe_pixel(surface, client.x + 2, client.y + 2, host_color(245u, 245u, 247u, 255u),
                         "the document's page background", report);
        host_probe_region_has_color(surface, client, theme.identity_accent,
                                    "the document's own accent in the client area", report);
    } else {
        host_probe_true(0, "the window has a client area to probe", report);
    }

    /* What the frame record says, including the two counts that are the text verdict. */
    host_probe_true(frame->windows >= 1u, "one shell window painted", report);
    host_probe_true(frame->content_runs >= 1u, "the window's document reached the rasteriser", report);
    host_probe_true(frame->labels >= 3u, "the panel's three labels drew", report);
    host_probe_true(frame->labels_unshaped == 0u, "every label was shaped by a real face", report);

    /* And what the document itself reported about the pass it ran. */
    memset(&document_report, 0, sizeof(document_report));
    if (robo_desktop_document_report(robo_desktop_scene_document(scene), &document_report) == ROBO_OK) {
        host_probe_true(document_report.elements >= 4u,
                        "the document's elements reached the layout bridge", report);
        host_probe_true(document_report.texts >= 4u, "the document laid out its text runs", report);
        host_probe_true(document_report.commands >= 1u, "the document's display list is not empty",
                        report);
    } else {
        host_probe_true(0, "the document reported the pass it ran", report);
    }
}

static void host_print_summary(robo_desktop_t *desktop, const robo_desktop_frame_record_t *frame)
{
    robo_desktop_stats_t stats;
    char dump[ROBO_DESKTOP_DUMP_LINE_MAX * 8u];

    memset(&stats, 0, sizeof(stats));
    (void)robo_desktop_stats(desktop, &stats);
    printf("frame: %ux%u fills=%lu labels=%lu unshaped=%lu windows=%lu content_runs=%lu "
           "content_pixels=%lu\n",
           (unsigned)frame->width, (unsigned)frame->height, (unsigned long)frame->fills,
           (unsigned long)frame->labels, (unsigned long)frame->labels_unshaped,
           (unsigned long)frame->windows, (unsigned long)frame->content_runs,
           (unsigned long)frame->content_pixels);
    printf("shell: frames=%lu presentations=%lu resizes=%lu windows_added=%lu closed=%lu "
           "pointer_actions=%lu\n",
           (unsigned long)stats.frames, (unsigned long)stats.presentations, (unsigned long)stats.resizes,
           (unsigned long)stats.windows_added, (unsigned long)stats.windows_closed,
           (unsigned long)stats.pointer_actions);
    memset(dump, 0, sizeof(dump));
    if (robo_desktop_write(desktop, dump, sizeof(dump), NULL, NULL) == ROBO_OK) {
        printf("%s", dump);
    }
}

/* ------------------------------------------------------------------ */
/* Phase 1 — offscreen                                                 */
/* ------------------------------------------------------------------ */

static int host_run_offscreen(robo_font_face_t *face, const char *shot, host_report_t *report)
{
    robo_desktop_scene_config_t config;
    robo_desktop_scene_t *scene = NULL;
    robo_desktop_scene_step_t step;
    robo_desktop_diag_t diag;
    robo_result_t result;

    memset(&diag, 0, sizeof(diag));
    robo_desktop_scene_config_init(&config);
    config.face = face;
    config.create_window = 0;
    config.status_text = HOST_STATUS_TEXT;

    result = robo_desktop_scene_open(&config, &scene, &diag);
    if (result != ROBO_OK) {
        printf("%s: scene_open failed code=%d message=%s\n", HOST_KEY, (int)result,
               diag.message ? diag.message : "(none)");
        return 0;
    }

    result = robo_desktop_scene_step(scene, 0u, &step, &diag);
    if (result != ROBO_OK || !step.painted) {
        printf("%s: the offscreen frame did not paint (code=%d)\n", HOST_KEY, (int)result);
        robo_desktop_scene_close(scene);
        return 0;
    }

    /* A screen-only scene presents nowhere, and says so rather than pretending. */
    host_probe_true(step.presented == 0, "a scene with no window presents nowhere", report);
    host_print_summary(robo_desktop_scene_desktop(scene), &step.frame);
    host_probe_scene(scene, &step.frame, report);

    if (shot) {
        report->checks++;
        if (host_write_png(robo_desktop_frame_surface(robo_desktop_scene_desktop(scene)), shot)) {
            printf("probe OK   wrote %s\n", shot);
        } else {
            report->failures++;
            printf("probe FAIL could not write %s\n", shot);
        }
    }

    robo_desktop_scene_close(scene);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Phase 2 — a real window                                             */
/* ------------------------------------------------------------------ */

/** Push one pointer state through the production queue: observe → queue → poll → hit test → action. */
static void host_inject_pointer(robo_window_id_t window, int32_t x, int32_t y, int pressed)
{
    robo_mouse_event_t event;

    robo_mouse_event_init(&event);
    event.window_id = window;
    event.type = ROBO_MOUSE_EVENT_MOVE;
    event.client_x = x;
    event.client_y = y;
    (void)robo_window_observe_mouse(window, &event);
    event.type = pressed ? ROBO_MOUSE_EVENT_BUTTON_DOWN : ROBO_MOUSE_EVENT_BUTTON_UP;
    event.button = ROBO_MOUSE_BUTTON_LEFT;
    (void)robo_window_observe_mouse(window, &event);
}

/**
 * A point inside the app window's close button, found through the shell's own hit test.
 *
 * Searched for rather than computed: the close button's position is the *shell's* business
 * (`RoboDesktopWindow.h`'s metrics and `RoboDesktopChrome.c`'s layout), and a launcher that repeated that
 * arithmetic would be a second implementation of it — the one that would go stale silently.
 */
static int host_find_close_point(robo_desktop_t *desktop, robo_desktop_window_id_t window,
                                 int32_t *out_x, int32_t *out_y)
{
    robo_gfx_rect_t bounds;
    robo_desktop_window_metrics_t metrics;
    int32_t y;

    if (robo_desktop_window_bounds(desktop, window, &bounds) != ROBO_OK) {
        return 0;
    }
    /* The title bar's vertical middle: the button is centred in it, and the band is
     * `border .. border + title_height` (`RoboDesktopTypes.h`). */
    robo_desktop_window_metrics_init(&metrics);
    y = bounds.y + metrics.border + metrics.title_height / 2;
    {
        int32_t x;

        for (x = bounds.x + bounds.width - 1; x >= bounds.x; x--) {
            robo_desktop_hit_t hit;

            if (robo_desktop_hit_test(desktop, robo_gfx_point(x, y), &hit) != ROBO_OK) {
                return 0;
            }
            if (hit.area == ROBO_DESKTOP_AREA_WINDOW_CLOSE) {
                *out_x = x;
                *out_y = y;
                return 1;
            }
        }
    }
    return 0;
}

static int host_run_windowed(robo_font_face_t *face, unsigned hold_ms, int inject, host_report_t *report)
{
    robo_desktop_scene_config_t config;
    robo_desktop_scene_t *scene = NULL;
    robo_desktop_scene_step_t step;
    robo_desktop_diag_t diag;
    robo_window_id_t native = ROBO_WINDOW_ID_NONE;
    unsigned elapsed = 0u;
    int checked = 0;
    int injected = 0;
    int closed = 0;

    memset(&diag, 0, sizeof(diag));
    if (!robo_window_backend_host_available() || robo_window_backend_use_host() != ROBO_OK) {
        printf("window: the host backend is unavailable — the offscreen result stands\n");
        return 1; /* not a failure: the offscreen phase already verified the picture */
    }

    robo_desktop_scene_config_init(&config);
    config.face = face;
    config.create_window = 1;
    config.status_text = HOST_STATUS_TEXT;
    if (robo_desktop_scene_open(&config, &scene, &diag) != ROBO_OK) {
        printf("%s: the windowed scene could not open (message=%s)\n", HOST_KEY,
               diag.message ? diag.message : "(none)");
        return 1; /* no interactive desktop session: the offscreen result stands */
    }
    native = robo_desktop_attached_window(robo_desktop_scene_desktop(scene));
    printf("window: %s native=%u\n", robo_window_backend_name(), (unsigned)native);

    while (elapsed < hold_ms) {
        if (robo_desktop_scene_step(scene, HOST_FRAME_WAIT_MS, &step, &diag) != ROBO_OK) {
            printf("%s: a scene step failed (message=%s)\n", HOST_KEY,
                   diag.message ? diag.message : "(none)");
            report->failures++;
            robo_desktop_scene_close(scene);
            return 0;
        }
        elapsed += HOST_FRAME_WAIT_MS;

        /* The windowed picture is probed once, on the first frame it presents: every check below is
         * deterministic, so a second frame would assert the same things twice. */
        if (!checked && step.painted) {
            checked = 1;
            host_print_summary(robo_desktop_scene_desktop(scene), &step.frame);
            host_probe_scene(scene, &step.frame, report);
        }

        /* A press on the title bar, once the window exists: the shell's job is to raise and focus it. */
        if (inject && !injected && checked && native != ROBO_WINDOW_ID_NONE) {
            robo_gfx_rect_t bounds;

            injected = 1;
            if (robo_desktop_window_bounds(robo_desktop_scene_desktop(scene),
                                           robo_desktop_scene_window(scene),
                                           &bounds) != ROBO_OK) {
                report->checks++;
                report->failures++;
                printf("probe FAIL the app window has no bounds to press\n");
            } else {
                host_inject_pointer(native, bounds.x + bounds.width / 2, bounds.y + 6, 1);
                host_inject_pointer(native, bounds.x + bounds.width / 2, bounds.y + 6, 0);
            }
        }

        if (step.close_requested) {
            closed = 1;
            break;
        }
    }

    /*
     * Injection is asynchronous — the queue is drained by a later iteration — so the *action* is asserted
     * after the events have been through the loop rather than at the moment they were posted. This is the
     * seam a checkpoint exists to cover: observe → queue → poll → hit test → raise.
     */
    if (inject && injected && !closed) {
        unsigned attempt = 0u;

        while (attempt < HOST_ACTION_ATTEMPTS) {
            attempt++;
            if (robo_desktop_scene_step(scene, HOST_FRAME_WAIT_MS, &step, &diag) != ROBO_OK) {
                break;
            }
            if (step.pointer_actions > 0u || step.close_requested) {
                break;
            }
        }
        {
            robo_desktop_stats_t stats;

            memset(&stats, 0, sizeof(stats));
            (void)robo_desktop_stats(robo_desktop_scene_desktop(scene), &stats);
            host_probe_true(stats.pointer_actions >= 1u,
                            "a press on the title bar reached the shell through the event queue", report);
            host_probe_true(robo_desktop_window_focused(robo_desktop_scene_desktop(scene)) ==
                                robo_desktop_scene_window(scene),
                            "the pressed window is the focused one", report);
        }
    }

    /* The one action a picture cannot prove: the close button. Pressed last, because it retires the window
     * and the checkpoint is then over. */
    if (!closed && robo_desktop_scene_window(scene) != ROBO_DESKTOP_WINDOW_ID_NONE) {
        int32_t close_x = 0;
        int32_t close_y = 0;

        if (native != ROBO_WINDOW_ID_NONE &&
            host_find_close_point(robo_desktop_scene_desktop(scene), robo_desktop_scene_window(scene),
                                  &close_x, &close_y)) {
            unsigned attempt = 0u;

            host_inject_pointer(native, close_x, close_y, 1);
            host_inject_pointer(native, close_x, close_y, 0);
            while (attempt < HOST_ACTION_ATTEMPTS) {
                attempt++;
                if (robo_desktop_scene_step(scene, HOST_LATE_WAIT_MS, &step, &diag) != ROBO_OK) {
                    break;
                }
                if (step.close_requested) {
                    closed = 1;
                    break;
                }
            }
            {
                robo_desktop_stats_t stats;

                memset(&stats, 0, sizeof(stats));
                (void)robo_desktop_stats(robo_desktop_scene_desktop(scene), &stats);
                host_probe_true(stats.windows_closed >= 1u,
                                "the close button closed the window it belongs to", report);
            }
        } else {
            printf("window: no close button found to press\n");
        }
    }

    robo_desktop_scene_close(scene);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

static unsigned host_parse_ms(const char *text, unsigned fallback)
{
    long value;

    if (!text || text[0] == '\0') {
        return fallback;
    }
    value = strtol(text, NULL, 10);
    if (value < 0l) {
        return fallback;
    }
    return (unsigned)value;
}

int robo_desktop_host_main(int argc, char **argv)
{
    host_report_t report;
    host_face_t face;
    unsigned hold_ms;
    const char *shot = NULL;
    int no_window = 0;
    int inject = 1;
    int face_state;
    int i;

    report.checks = 0;
    report.failures = 0;
    hold_ms = host_parse_ms(getenv("ROBO_DESKTOP_HOLD_MS"), HOST_HOLD_DEFAULT_MS);
    shot = getenv("ROBO_DESKTOP_SHOT");
    if (getenv("ROBO_DESKTOP_NO_WINDOW") != NULL) {
        no_window = 1;
    }

    for (i = 1; i < argc; i++) {
        const char *argument = argv[i];

        if (strcmp(argument, "--no-window") == 0) {
            no_window = 1;
        } else if (strcmp(argument, "--no-inject-input") == 0) {
            inject = 0;
        } else if (strcmp(argument, "--hold") == 0 && i + 1 < argc) {
            hold_ms = host_parse_ms(argv[++i], HOST_HOLD_DEFAULT_MS);
        } else if (strcmp(argument, "--shot") == 0 && i + 1 < argc) {
            shot = argv[++i];
        } else {
            printf("desktop: unknown option %s\n", argument);
            printf("usage: %s [--no-window] [--no-inject-input] [--hold ms] [--shot path]\n", argv[0]);
            return 2;
        }
    }

    printf("RoboticelaOS — the desktop shell checkpoint\n");
    printf("document: %lu bytes of markup from RoboDesktopScene\n",
           (unsigned long)robo_desktop_scene_markup_length());

    if (!robo_base_is_initialized() && robo_base_init() != ROBO_OK) {
        printf("desktop: robo_base_init failed\n");
        return 1;
    }
    if (robo_graphics_init() != ROBO_OK || robo_font_init() != ROBO_OK || robo_html_init() != ROBO_OK ||
        robo_css_init() != ROBO_OK || robo_layout_init() != ROBO_OK || robo_paint_init() != ROBO_OK) {
        printf("desktop: the runtime's layers did not come up\n");
        return 1;
    }
    if (robo_display_init() == ROBO_OK) {
        (void)robo_display_backend_use_host();
    }
    if (robo_window_init() != ROBO_OK) {
        printf("desktop: robo_window_init failed\n");
        return 1;
    }

    face_state = host_face_load(ROBO_FONT_ASSET_PATH, &face);
    if (face_state > 0) {
        printf("face: %s\n", ROBO_FONT_ASSET_PATH);
    } else if (face_state == 0) {
        printf("face: %s is not there — the checkpoint will draw no glyphs\n", ROBO_FONT_ASSET_PATH);
    } else {
        printf("desktop: %s did not parse as a font\n", ROBO_FONT_ASSET_PATH);
        return 1;
    }

    if (!host_run_offscreen(face.face, shot, &report)) {
        host_face_release(&face);
        return 1;
    }
    if (!no_window) {
        (void)host_run_windowed(face.face, hold_ms, inject, &report);
    }

    host_face_release(&face);
    robo_window_shutdown();
    robo_display_shutdown();
    robo_paint_shutdown();
    robo_layout_shutdown();
    robo_css_shutdown();
    robo_html_shutdown();
    robo_font_shutdown();
    robo_graphics_shutdown();

    printf("checks: %d passed=%d failed=%d\n", report.checks, report.checks - report.failures,
           report.failures);
    printf("RESULT: %s\n", report.failures == 0 ? "PASS" : "FAIL");
    return report.failures == 0 ? 0 : 1;
}
