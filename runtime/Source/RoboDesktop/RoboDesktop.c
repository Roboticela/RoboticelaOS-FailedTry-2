/*
 * RoboDesktop (Phase 11.13) — the shell's lifecycle, its frames, and its presentation.
 *
 * ## The screen target is the framebuffer, and the frame is the unit
 *
 * `desktop->target` is a RoboRender target — owned, RGBA8, zero-filled at creation — and every frame paints
 * *into* it from its first fill. That is why there is no "clear the frame" step anywhere in this file: the
 * first thing `robo_desktop_chrome_paint_shell` does is fill the whole screen with the desktop surface, which
 * is an opaque fill and therefore *is* the clear. A separate clear would be a second full-surface write per
 * frame to reach the same state, and a frame that cleared and then painted a translucent desktop would be
 * relying on the clear's colour rather than on its own.
 *
 * ## Content is rasterised per window, then composited
 *
 * Each window with contents owns a target the size of its client area (`desktop_window_t.target`, created
 * lazily), the display list is executed into it, and the result is composited into the screen at the client
 * rectangle's origin. Three consequences, and all three are why it is done this way rather than compositing
 * the list straight into the screen:
 *
 *   a window's contents are **one picture** — they do not share a clip or an opacity scope with the shell,
 *   so a `push-clip` in a document cannot reach the panel;
 *   a window whose contents fail to rasterise leaves a **window** on screen (its body and decorations are
 *   painted before the composite), not a hole;
 *   and the same content target is what a future compositor would take as a layer, which is the boundary
 *   this build stops at rather than one it crosses.
 *
 * ## Presentation is one call and one wait
 *
 * `robo_desktop_present` is RoboWindow's own frame triple around `robo_render_present`: the window gives its
 * render surface, the frame is composited into it, the window is marked dirty and presented. Nothing waits
 * for a vertical blank, nothing is double-buffered, and nothing is paced — which is stated here because a
 * `present` that was paced would be a frame *scheduler*, and this phase does not have one.
 */

#include "RoboDesktopInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboMem/RoboMem.h"
#include "RoboRender/RoboRenderPresenter.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowGraphics.h"

/* ------------------------------------------------------------------ */
/* The module                                                          */
/* ------------------------------------------------------------------ */

const char *robo_desktop_version_string(void)
{
    return ROBO_DESKTOP_VERSION_STRING;
}

robo_result_t robo_desktop_get_version(robo_i32 *out_major,
                                      robo_i32 *out_minor,
                                      robo_i32 *out_patch)
{
    if (!out_major || !out_minor || !out_patch) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_major = (robo_i32)ROBO_DESKTOP_VERSION_MAJOR;
    *out_minor = (robo_i32)ROBO_DESKTOP_VERSION_MINOR;
    *out_patch = (robo_i32)ROBO_DESKTOP_VERSION_PATCH;
    return ROBO_OK;
}

int robo_desktop_shell_is_implemented(void)
{
    return ROBO_DESKTOP_SHELL_IMPLEMENTED;
}

void robo_desktop_capabilities_init(robo_desktop_capabilities_t *capabilities)
{
    size_t bytes;
    size_t index;

    if (!capabilities) {
        return;
    }
    bytes = sizeof(*capabilities);
    for (index = 0u; index < bytes; index++) {
        ((char *)capabilities)[index] = 0;
    }
}

robo_result_t robo_desktop_get_capabilities(robo_desktop_capabilities_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_desktop_capabilities_init(out);

    /* What this release is: a shell, its windows, their contents, presentation, records, resize and the
     * pointer targeting that makes the first of those interactive. */
    out->shell = ROBO_DESKTOP_SHELL_IMPLEMENTED;
    out->windows = 1;
    out->window_content = 1;
    out->presentation = 1;
    out->frame_records = 1;
    out->resize = 1;
    out->pointer_targeting = 1;
    out->shell_text = 1;

    /*
     * The absences, and each is a structure rather than a flag somebody forgot to set -- the same
     * discipline `RoboRender.h` keeps one module down. `documents` is 0 because the *shell* does not read
     * HTML: an application document is a separate module (`RoboDesktopDocument.h`), and a capability row
     * here would claim the shell needs one. `compositor`, `tiles` and `damage_tracking` are the three
     * structures a frame would have to become; `frame_scheduling` and `vsync` are the clock and the signal
     * this build's determinism depends on not having; `gpu` is a device and a resource manager;
     * `window_manager`, `input_delivery` and `desktop_environment` are the three products this phase
     * explicitly does not build.
     */
    out->documents = 0;
    out->compositor = 0;
    out->tiles = 0;
    out->damage_tracking = 0;
    out->frame_scheduling = 0;
    out->vsync = 0;
    out->gpu = 0;
    out->window_manager = 0;
    out->input_delivery = 0;
    out->desktop_environment = 0;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Configuration and lifetime                                          */
/* ------------------------------------------------------------------ */

void robo_desktop_config_init(robo_desktop_config_t *config)
{
    if (!config) {
        return;
    }
    config->width = 1280;
    config->height = 800;
    config->window = ROBO_WINDOW_ID_NONE;
    config->create_window = 0;
    config->window_title = NULL;
    config->identity_text = NULL;
    config->applications_text = NULL;
    config->status_text = NULL;
    config->theme = NULL;
    config->panel = NULL;
    config->window_metrics = NULL;
    config->face = NULL;
    config->label_size_px = ROBO_DESKTOP_DEFAULT_LABEL_SIZE_PX;
}

/** The whole struct zeroed, so a shell starts from a state with no stale field in it. */
static void desktop_clear(robo_desktop_t *desktop)
{
    size_t bytes = sizeof(*desktop);
    size_t index;

    for (index = 0u; index < bytes; index++) {
        ((char *)desktop)[index] = 0;
    }
    desktop->focused_index = ROBO_DESKTOP_NO_INDEX;
    desktop->window = ROBO_WINDOW_ID_NONE;
}

robo_result_t robo_desktop_create(const robo_desktop_config_t *config,
                                 robo_desktop_t **out,
                                 robo_desktop_diag_t *diag)
{
    robo_desktop_t *desktop;
    robo_desktop_panel_metrics_t panel;
    robo_desktop_window_metrics_t window_metrics;
    robo_desktop_theme_t theme;

    if (!config || !out) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "a configuration and an out pointer are required");
    }
    if (config->width <= 0 || config->height <= 0 ||
        config->width > (int32_t)ROBO_DESKTOP_MAX_DIMENSION ||
        config->height > (int32_t)ROBO_DESKTOP_MAX_DIMENSION) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "the screen size is not one this build can make");
    }

    /* Metrics and theme first: every refusal below is about something the shell will not draw, and a
     * shell that allocated a target before discovering that cannot present itself is a leak with a
     * message attached. */
    if (config->panel) {
        panel = *config->panel;
    } else {
        robo_desktop_panel_metrics_init(&panel);
    }
    if (!robo_desktop_panel_metrics_is_valid(&panel)) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "the panel metrics are not ones this build can lay out");
    }
    if (config->window_metrics) {
        window_metrics = *config->window_metrics;
    } else {
        robo_desktop_window_metrics_init(&window_metrics);
    }
    if (!robo_desktop_window_metrics_is_valid(&window_metrics)) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "the window metrics are not ones this build can draw");
    }
    if (config->theme) {
        theme = *config->theme;
    } else {
        robo_desktop_theme_init(&theme);
    }
    if (!robo_desktop_theme_is_valid(&theme)) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "the theme is not one this build can paint with");
    }
    {
        const char *identity = config->identity_text ? config->identity_text
                                                    : ROBO_DESKTOP_DEFAULT_IDENTITY;
        const char *applications = config->applications_text ? config->applications_text
                                                            : ROBO_DESKTOP_DEFAULT_APPLICATIONS;
        const char *status_text = config->status_text ? config->status_text : "";

        if (robo_desktop_string_length(identity) + 1u > ROBO_DESKTOP_TITLE_MAX ||
            robo_desktop_string_length(applications) + 1u > ROBO_DESKTOP_TITLE_MAX ||
            robo_desktop_string_length(status_text) + 1u > ROBO_DESKTOP_STATUS_MAX) {
            return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                     "a shell label does not fit");
        }
    }

    desktop = (robo_desktop_t *)robo_realloc(NULL, sizeof(*desktop));
    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_OUT_OF_MEMORY,
                                 "the shell could not be allocated");
    }
    desktop_clear(desktop);
    desktop->theme = theme;
    desktop->panel = panel;
    desktop->window_metrics = window_metrics;
    desktop->label_size_px =
        (config->label_size_px > 0.0f) ? config->label_size_px : ROBO_DESKTOP_DEFAULT_LABEL_SIZE_PX;
    desktop->face = config->face;
    if (robo_desktop_string_copy(desktop->identity, sizeof(desktop->identity),
                                 config->identity_text ? config->identity_text
                                                       : ROBO_DESKTOP_DEFAULT_IDENTITY) != ROBO_OK ||
        robo_desktop_string_copy(desktop->applications, sizeof(desktop->applications),
                                 config->applications_text ? config->applications_text
                                                           : ROBO_DESKTOP_DEFAULT_APPLICATIONS) !=
            ROBO_OK ||
        robo_desktop_string_copy(desktop->status, sizeof(desktop->status),
                                 config->status_text ? config->status_text : "") != ROBO_OK) {
        robo_free(desktop);
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "a shell label does not fit");
    }

    if (robo_desktop_geometry_compute(robo_gfx_size(config->width, config->height), &panel,
                                      &desktop->geometry) != ROBO_OK) {
        robo_free(desktop);
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "the desktop's geometry could not be computed");
    }
    if (robo_render_target_create(&desktop->target, (uint32_t)config->width,
                                  (uint32_t)config->height, NULL) != ROBO_OK ||
        !desktop->target) {
        robo_free(desktop);
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_OUT_OF_MEMORY,
                                 "the screen target could not be allocated");
    }

    if (config->window != ROBO_WINDOW_ID_NONE) {
        /* An attached window is borrowed: this shell presents into it and does not own it. */
        desktop->window = config->window;
        desktop->owns_window = 0;
    } else if (config->create_window) {
        robo_window_config_t window_config;

        robo_window_config_init(&window_config);
        window_config.width = config->width;
        window_config.height = config->height;
        window_config.visible = 1;
        window_config.resizable = 1;
        window_config.title = config->window_title ? config->window_title
                                                  : ROBO_DESKTOP_DEFAULT_TITLE;
        if (robo_window_create(&window_config, &desktop->window) != ROBO_OK) {
            robo_render_target_destroy(desktop->target);
            robo_free(desktop);
            return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_NOT_SUPPORTED,
                                     "no native window could be created for the desktop");
        }
        desktop->owns_window = 1;
        (void)robo_window_show(desktop->window);
    }

    desktop->live = 1;
    desktop->next_id = 0u;
    *out = desktop;
    /*
     * One line per shell, and the facts a reader needs to interpret every later line: how big the screen
     * is, and whether this shell has somewhere to present to at all. The `window=` field is the *state*
     * (`owned`/`attached`/`none`) rather than RoboWindow's id, because that id belongs to the platform
     * rather than to this shell -- a shell window id printed below *is* this module's own and is
     * deterministic, which is why it is safe to print and a native window's is not.
     */
    robo_log_debug("robo_desktop: shell created %dx%d (window=%s, face=%s)",
                   (int)desktop->geometry.screen.width, (int)desktop->geometry.screen.height,
                   desktop->window == ROBO_WINDOW_ID_NONE
                       ? "none"
                       : (desktop->owns_window ? "owned" : "attached"),
                   desktop->face ? "yes" : "none");
    return ROBO_OK;
}

void robo_desktop_destroy(robo_desktop_t *desktop)
{
    size_t index;

    if (!desktop) {
        return;
    }
    /* The counters go out with the shell, the way every module in this tree reports what it did on the
     * way down (`robo_window_shutdown`, `robo_log_debug` in the CSS tokenizer). Nothing here is an
     * address and nothing here is a clock. */
    robo_log_debug("robo_desktop: shell released (frames=%llu, fills=%llu, content_runs=%llu, "
                   "windows_added=%llu, presentations=%llu)",
                   (unsigned long long)desktop->stats.frames,
                   (unsigned long long)desktop->stats.fill_drawings,
                   (unsigned long long)desktop->stats.content_runs,
                   (unsigned long long)desktop->stats.windows_added,
                   (unsigned long long)desktop->stats.presentations);
    for (index = 0u; index < desktop->window_count; index++) {
        robo_desktop_window_release_target(&desktop->windows[index]);
    }
    desktop->window_count = 0u;
    if (desktop->target) {
        robo_render_target_destroy(desktop->target);
        desktop->target = NULL;
    }
    /* Only a window this shell created is destroyed here; an attached one is the caller's (see the
     * header). */
    if (desktop->owns_window && desktop->window != ROBO_WINDOW_ID_NONE) {
        (void)robo_window_destroy(desktop->window);
    }
    desktop->window = ROBO_WINDOW_ID_NONE;
    desktop->owns_window = 0;
    desktop->live = 0;
    robo_free(desktop);
}

int robo_desktop_is_live(const robo_desktop_t *desktop)
{
    return (desktop && desktop->live) ? 1 : 0;
}

robo_gfx_size_t robo_desktop_screen_size(const robo_desktop_t *desktop)
{
    return desktop ? robo_gfx_size(desktop->geometry.screen.width, desktop->geometry.screen.height)
                   : robo_gfx_size(0, 0);
}

robo_desktop_geometry_t robo_desktop_geometry_of(const robo_desktop_t *desktop)
{
    robo_desktop_geometry_t geometry;

    if (!desktop) {
        robo_desktop_geometry_init(&geometry);
        return geometry;
    }
    return desktop->geometry;
}

robo_gfx_rect_t robo_desktop_work_area(const robo_desktop_t *desktop)
{
    return desktop ? desktop->geometry.work_area : robo_gfx_rect_empty();
}

robo_window_id_t robo_desktop_attached_window(const robo_desktop_t *desktop)
{
    return desktop ? desktop->window : ROBO_WINDOW_ID_NONE;
}

robo_result_t robo_desktop_set_status(const robo_desktop_t *desktop,
                                      const char *utf8_text,
                                      robo_desktop_diag_t *diag)
{
    robo_desktop_t *mutable_desktop = (robo_desktop_t *)desktop;

    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    if (robo_desktop_string_copy(mutable_desktop->status, sizeof(mutable_desktop->status),
                                 utf8_text) != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "the status label does not fit");
    }
    return ROBO_OK;
}

const char *robo_desktop_status(const robo_desktop_t *desktop)
{
    return desktop ? desktop->status : "";
}

/* ------------------------------------------------------------------ */
/* The frame record                                                    */
/* ------------------------------------------------------------------ */

void robo_desktop_frame_record_init(robo_desktop_frame_record_t *record)
{
    size_t bytes;
    size_t index;

    if (!record) {
        return;
    }
    bytes = sizeof(*record);
    for (index = 0u; index < bytes; index++) {
        ((char *)record)[index] = 0;
    }
}

int robo_desktop_frame_record_is_valid(const robo_desktop_frame_record_t *record)
{
    if (!record) {
        return 0;
    }
    if (record->width == 0u || record->height == 0u) {
        return 0;
    }
    if (record->width > (uint32_t)ROBO_DESKTOP_MAX_DIMENSION ||
        record->height > (uint32_t)ROBO_DESKTOP_MAX_DIMENSION) {
        return 0;
    }
    /* The counts a frame's own arithmetic fixes: a window can only have painted if there are windows, and
     * a label can only have been skipped if there was one to skip. A record whose fields disagree about
     * that is a record that has been assembled by hand. */
    if (record->windows > record->windows_total) {
        return 0;
    }
    if (record->content_pixels > (size_t)record->width * (size_t)record->height) {
        return 0;
    }
    if (record->bytes != (size_t)record->width * (size_t)record->height * 4u) {
        return 0;
    }
    return 1;
}

int robo_desktop_frame_record_equal(const robo_desktop_frame_record_t *a,
                                    const robo_desktop_frame_record_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->width == b->width && a->height == b->height && a->fills == b->fills &&
           a->labels == b->labels && a->labels_unshaped == b->labels_unshaped &&
           a->windows == b->windows && a->windows_total == b->windows_total &&
           a->content_runs == b->content_runs && a->content_pixels == b->content_pixels &&
           a->clipped == b->clipped && a->bytes == b->bytes && a->checksum == b->checksum;
}

/* ------------------------------------------------------------------ */
/* Window contents                                                     */
/* ------------------------------------------------------------------ */

/** The window's content target, created or recreated to match @p client. */
static robo_result_t desktop_content_target(desktop_window_t *window,
                                           robo_gfx_rect_t client,
                                           robo_render_diag_t *diag)
{
    if (client.width <= 0 || client.height <= 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (window->target) {
        if ((int32_t)robo_render_target_width(window->target) == client.width &&
            (int32_t)robo_render_target_height(window->target) == client.height) {
            return ROBO_OK;
        }
        robo_render_target_destroy(window->target);
        window->target = NULL;
    }
    return robo_render_target_create(&window->target, (uint32_t)client.width,
                                     (uint32_t)client.height, diag);
}

/**
 * Rasterise a window's contents and composite them into the screen.
 *
 * The failure is *returned* rather than swallowed -- the shell reports that a window's contents could not
 * be drawn (`ROBO_NOT_SUPPORTED`, `WINDOW` domain) while still having painted the window around them --
 * so a caller learns that a document is broken instead of quietly seeing an empty body.
 */
static robo_result_t desktop_paint_window_content(robo_desktop_t *desktop,
                                                 desktop_window_t *window,
                                                 robo_gfx_rect_t client,
                                                 robo_gfx_raster_t *raster,
                                                 robo_desktop_frame_record_t *record,
                                                 robo_render_diag_t *render_diag)
{
    robo_font_face_t *face;
    robo_gfx_surface_t *screen_surface;
    robo_result_t status;

    if (!window->content) {
        return ROBO_OK;
    }
    status = desktop_content_target(window, client, render_diag);
    if (status != ROBO_OK || !window->target) {
        return (status != ROBO_OK) ? status : ROBO_OUT_OF_MEMORY;
    }
    /* The body colour first: a document that draws nothing must leave a *window*, not the desktop showing
     * through the hole where its client area is. */
    (void)robo_render_target_clear(window->target, desktop->theme.window_surface, NULL);

    face = window->content_face ? window->content_face : desktop->face;
    status = robo_render_rasterize(window->content, window->target, face, NULL, render_diag);
    if (status != ROBO_OK) {
        return status;
    }
    record->content_runs++;

    screen_surface = robo_gfx_raster_target(raster);
    if (!screen_surface) {
        return ROBO_ERROR;
    }
    status = robo_gfx_surface_composite_rect(robo_render_target_surface_const(window->target),
                                            robo_render_target_bounds(window->target),
                                            screen_surface,
                                            robo_gfx_point(client.x, client.y), 255u);
    if (status != ROBO_OK) {
        return status;
    }
    record->content_pixels += (size_t)client.width * (size_t)client.height;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Frames                                                              */
/* ------------------------------------------------------------------ */

robo_result_t robo_desktop_begin_frame(robo_desktop_t *desktop, robo_desktop_diag_t *diag)
{
    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    if (desktop->frame_open) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_BUSY,
                                 "a frame is already open");
    }
    desktop->frame_open = 1;
    desktop->painted = 0;
    return ROBO_OK;
}

int robo_desktop_frame_is_open(const robo_desktop_t *desktop)
{
    return (desktop && desktop->frame_open) ? 1 : 0;
}

robo_result_t robo_desktop_paint(robo_desktop_t *desktop, robo_desktop_diag_t *diag)
{
    robo_desktop_frame_record_t record;
    robo_gfx_raster_t *raster = NULL;
    robo_render_diag_t render_diag;
    const char *content_failure = NULL;
    size_t index;

    robo_render_diag_reset(&render_diag);
    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    if (!desktop->frame_open) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "there is no open frame to paint");
    }
    if (desktop->painted) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_BUSY,
                                 "this frame has already been painted");
    }
    if (!desktop->target) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_NOT_FOUND,
                                 "the desktop has no screen target");
    }
    if (robo_gfx_raster_create(robo_render_target_surface(desktop->target), &raster) != ROBO_OK ||
        !raster) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_ERROR,
                                 "the screen target could not be opened for painting");
    }
    /* The clip is the screen for the whole frame: the raster's own bounds do this anyway, and stating it
     * is what makes "clipped" in the record a statement about a *drawing* rather than about a surface. */
    (void)robo_gfx_raster_reset_clip(raster);

    robo_desktop_frame_record_init(&record);
    record.width = (uint32_t)desktop->geometry.screen.width;
    record.height = (uint32_t)desktop->geometry.screen.height;
    record.windows_total = desktop->window_count;
    record.bytes = (size_t)record.width * (size_t)record.height * 4u;

    (void)robo_desktop_chrome_paint_shell(desktop, raster, &record, diag);

    /* Back to front: the last window painted is the front one, which is the whole of this build's z-order
     * (see `RoboDesktopWindow.c`). */
    for (index = 0u; index < desktop->window_count; index++) {
        desktop_window_t *window = &desktop->windows[index];
        robo_gfx_rect_t client;
        int focused;

        if (!window->visible || window->state != ROBO_DESKTOP_WINDOW_OPEN) {
            continue;
        }
        focused = (desktop->focused_index == index) ? 1 : 0;
        client = robo_desktop_chrome_client_bounds(desktop, window);
        (void)robo_desktop_chrome_paint_window_frame(desktop, window, focused, raster, &record, diag);
        if (desktop_paint_window_content(desktop, window, client, raster, &record, &render_diag) !=
            ROBO_OK) {
            /* The first failure is the one reported, and the painting continues: the window around a body
             * that could not be drawn is still a window, and the panel and every other window are still
             * worth painting. A frame that stopped here would leave a half-drawn screen whose record
             * described less than the shell actually painted. */
            if (!content_failure) {
                content_failure = render_diag.message;
            }
            /* Warned per frame rather than once, because a broken document is a state the caller is in
             * for as long as it holds that content, and a single line would make the *second* failing
             * frame look like a frame that painted fine. */
            robo_log_warn("robo_desktop: window contents could not be drawn (id=%llu): %s",
                          (unsigned long long)window->id,
                          render_diag.message ? render_diag.message : "the run was refused");
        }
        robo_desktop_chrome_paint_close_button(desktop, window, raster, &record);
        robo_desktop_chrome_paint_window_title(desktop, window, raster, &record);
        record.windows++;
    }

    robo_gfx_raster_destroy(raster);
    /* The record a frame produces is kept until `end_frame`, which is where the counters are folded into
     * the shell's own: a caller that never ends its frame gets no statistics, which is the honest reading
     * of "the frame never finished". */
    desktop->frame_record = record;
    desktop->painted = 1;

    if (content_failure) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_NOT_SUPPORTED,
                                 content_failure);
    }
    return ROBO_OK;
}

robo_result_t robo_desktop_end_frame(robo_desktop_t *desktop,
                                    robo_desktop_frame_record_t *out_record,
                                    robo_desktop_diag_t *diag)
{
    robo_desktop_frame_record_t record;

    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    if (!desktop->frame_open) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "there is no open frame to end");
    }
    if (desktop->painted) {
        record = desktop->frame_record;
    } else {
        robo_desktop_frame_record_init(&record);
        record.width = (uint32_t)desktop->geometry.screen.width;
        record.height = (uint32_t)desktop->geometry.screen.height;
        record.windows_total = desktop->window_count;
        record.bytes = (size_t)record.width * (size_t)record.height * 4u;
    }
    record.checksum = robo_render_target_checksum(desktop->target);

    desktop->stats.frames++;
    if (record.windows == 0u) {
        desktop->stats.frames_without_windows++;
    }
    desktop->stats.content_runs += record.content_runs;
    desktop->stats.content_pixels += record.content_pixels;
    desktop->stats.labels += record.labels;
    desktop->stats.labels_unshaped += record.labels_unshaped;
    desktop->stats.fill_drawings += record.fills;
    desktop->stats.clipped_drawings += record.clipped;
    desktop->stats.last_frame_checksum = record.checksum;

    desktop->frame_open = 0;
    desktop->painted = 0;
    if (out_record) {
        *out_record = record;
    }
    return ROBO_OK;
}

robo_result_t robo_desktop_render(robo_desktop_t *desktop,
                                 robo_desktop_frame_record_t *out_record,
                                 robo_desktop_diag_t *diag)
{
    robo_result_t status;

    status = robo_desktop_begin_frame(desktop, diag);
    if (status != ROBO_OK) {
        return status;
    }
    status = robo_desktop_paint(desktop, diag);
    if (status != ROBO_OK) {
        /* The frame is ended even when the paint reported a problem, so a caller is never left holding a
         * desktop whose next `begin_frame` reports ROBO_BUSY for a frame nothing will ever close -- and
         * the *record* is still handed over, because a frame with a window whose contents could not be
         * drawn is a frame the caller can still describe. */
        (void)robo_desktop_end_frame(desktop, out_record, NULL);
        return status;
    }
    return robo_desktop_end_frame(desktop, out_record, diag);
}

const robo_gfx_surface_t *robo_desktop_frame_surface(const robo_desktop_t *desktop)
{
    if (!desktop || !desktop->target) {
        return NULL;
    }
    return robo_render_target_surface_const(desktop->target);
}

uint64_t robo_desktop_frame_checksum(const robo_desktop_t *desktop)
{
    if (!desktop || !desktop->target) {
        return 0u;
    }
    return robo_render_target_checksum(desktop->target);
}

const uint8_t *robo_desktop_frame_bytes(const robo_desktop_t *desktop, size_t *out_size)
{
    if (out_size) {
        *out_size = 0u;
    }
    if (!desktop || !desktop->target) {
        return NULL;
    }
    return robo_render_target_bytes(desktop->target, out_size);
}

/* ------------------------------------------------------------------ */
/* Resize                                                              */
/* ------------------------------------------------------------------ */

robo_result_t robo_desktop_resize(robo_desktop_t *desktop,
                                 int32_t width,
                                 int32_t height,
                                 robo_desktop_diag_t *diag)
{
    robo_render_target_t *target = NULL;
    robo_desktop_geometry_t geometry;
    size_t index;
    int32_t old_width;
    int32_t old_height;

    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    /* Captured before anything is replaced: a resize log that read the geometry after the assignment
     * would report the new size twice and say nothing about what changed. */
    old_width = desktop->geometry.screen.width;
    old_height = desktop->geometry.screen.height;
    if (width <= 0 || height <= 0 || width > (int32_t)ROBO_DESKTOP_MAX_DIMENSION ||
        height > (int32_t)ROBO_DESKTOP_MAX_DIMENSION) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "the requested screen size is not one this build can make");
    }
    if (desktop->geometry.screen.width == width && desktop->geometry.screen.height == height) {
        /* Nothing to do, and deliberately nothing done: a caller polling a window's size must not force
         * a reallocation every iteration. */
        return ROBO_OK;
    }
    if (robo_desktop_geometry_compute(robo_gfx_size(width, height), &desktop->panel, &geometry) !=
        ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "the desktop's geometry could not be recomputed");
    }
    /* The new target first: if it cannot be allocated, the shell keeps the screen it had rather than
     * becoming a desktop with no pixels. */
    if (robo_render_target_create(&target, (uint32_t)width, (uint32_t)height, NULL) != ROBO_OK ||
        !target) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_OUT_OF_MEMORY,
                                 "the new screen target could not be allocated");
    }

    robo_render_target_destroy(desktop->target);
    desktop->target = target;
    desktop->geometry = geometry;
    /* A frame in flight is abandoned: its record described a screen that no longer exists. */
    desktop->frame_open = 0;
    desktop->painted = 0;

    /*
     * Every window is re-clamped into the new work area and its content target released -- the *client*
     * size follows the frame size, so a window that survived a resize has a stale content target, and
     * letting the next paint recreate it is cheaper than recreating eight here (see the internal header).
     */
    for (index = 0u; index < desktop->window_count; index++) {
        desktop_window_t *window = &desktop->windows[index];
        robo_gfx_rect_t clamped;

        robo_desktop_window_release_target(window);
        if (robo_desktop_geometry_contain(window->bounds, &geometry.work_area, &clamped) == ROBO_OK) {
            window->bounds = clamped;
        }
    }
    desktop->stats.resizes++;
    robo_log_debug("robo_desktop: resized %dx%d -> %dx%d (windows_reclamped=%llu)", (int)old_width,
                   (int)old_height, (int)width, (int)height,
                   (unsigned long long)desktop->window_count);
    return ROBO_OK;
}

robo_result_t robo_desktop_sync_size(robo_desktop_t *desktop,
                                    int *out_resized,
                                    robo_desktop_diag_t *diag)
{
    robo_gfx_size_t physical;
    robo_result_t status;

    if (out_resized) {
        *out_resized = 0;
    }
    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    if (desktop->window == ROBO_WINDOW_ID_NONE) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SURFACE, ROBO_NOT_SUPPORTED,
                                 "the desktop has no native window to follow");
    }
    if (robo_window_get_size_physical(desktop->window, &physical) != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SURFACE, ROBO_ERROR,
                                 "the window's physical size could not be read");
    }
    if (physical.width == desktop->geometry.screen.width &&
        physical.height == desktop->geometry.screen.height) {
        return ROBO_OK;
    }
    status = robo_desktop_resize(desktop, physical.width, physical.height, diag);
    if (status != ROBO_OK) {
        return status;
    }
    if (out_resized) {
        *out_resized = 1;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Presentation                                                        */
/* ------------------------------------------------------------------ */

robo_result_t robo_desktop_present_to_surface(const robo_desktop_t *desktop,
                                              robo_gfx_surface_t *dest,
                                              int32_t x,
                                              int32_t y,
                                              robo_render_present_record_t *out_record,
                                              robo_desktop_diag_t *diag)
{
    robo_render_diag_t render_diag;
    robo_result_t status;

    if (!desktop || !dest) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SURFACE, ROBO_INVALID_ARGUMENT,
                                 "a desktop and a destination surface are required");
    }
    if (!desktop->target) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SURFACE, ROBO_NOT_FOUND,
                                 "the desktop has no screen target");
    }
    robo_render_diag_reset(&render_diag);
    status = robo_render_present(desktop->target, dest, x, y, out_record, &render_diag);
    if (status != ROBO_OK) {
        /* The renderer's message travels: it names the stage that failed, and a desktop-level message
         * would only repeat "presentation failed" in place of the reason. */
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SURFACE, status,
                                 render_diag.message ? render_diag.message
                                                     : "the frame could not be presented");
    }
    return ROBO_OK;
}

robo_result_t robo_desktop_present(robo_desktop_t *desktop,
                                  robo_render_present_record_t *out_record,
                                  int *out_presented,
                                  robo_desktop_diag_t *diag)
{
    robo_gfx_surface_t *surface = NULL;
    robo_result_t status;

    if (out_presented) {
        *out_presented = 0;
    }
    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SURFACE, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    if (desktop->window == ROBO_WINDOW_ID_NONE) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SURFACE, ROBO_NOT_SUPPORTED,
                                 "the desktop has no native window to present into");
    }
    if (robo_window_begin_frame(desktop->window, &surface) != ROBO_OK || !surface) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SURFACE, ROBO_NOT_SUPPORTED,
                                 "the window could not give a render surface");
    }
    status = robo_desktop_present_to_surface(desktop, surface, 0, 0, out_record, diag);
    if (status != ROBO_OK) {
        /* The window's frame is closed even on a failed copy, so the next iteration can begin another one
         * rather than being refused as nested. */
        (void)robo_window_end_frame(desktop->window);
        return status;
    }
    if (robo_window_end_frame(desktop->window) != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SURFACE, ROBO_ERROR,
                                 "the window's frame could not be ended");
    }
    if (robo_window_present_frame(desktop->window) != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SURFACE, ROBO_ERROR,
                                 "the window could not present the frame");
    }
    desktop->stats.presentations++;
    if (out_presented) {
        *out_presented = 1;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Hit testing and the pointer                                         */
/* ------------------------------------------------------------------ */

void robo_desktop_hit_init(robo_desktop_hit_t *hit)
{
    if (!hit) {
        return;
    }
    hit->area = ROBO_DESKTOP_AREA_NONE;
    hit->window = ROBO_DESKTOP_WINDOW_ID_NONE;
    hit->point = robo_gfx_point(0, 0);
    hit->inside = 0;
}

robo_result_t robo_desktop_hit_test(const robo_desktop_t *desktop,
                                    robo_gfx_point_t point,
                                    robo_desktop_hit_t *out)
{
    size_t index;

    if (!desktop || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_desktop_hit_init(out);
    out->point = point;
    out->inside = robo_gfx_rect_contains_point(desktop->geometry.screen, point) ? 1 : 0;

    /* Front to back: a point over two windows belongs to the front one, which is the answer a *user*
     * would give. */
    for (index = desktop->window_count; index > 0u; index--) {
        const desktop_window_t *window = &desktop->windows[index - 1u];
        robo_gfx_rect_t close_bounds;
        robo_gfx_rect_t title_bounds;

        if (!window->visible || window->state != ROBO_DESKTOP_WINDOW_OPEN) {
            continue;
        }
        if (!robo_gfx_rect_contains_point(window->bounds, point)) {
            continue;
        }
        out->window = window->id;
        close_bounds = robo_desktop_chrome_close_bounds(desktop, window);
        title_bounds = robo_desktop_chrome_title_bounds(desktop, window);
        if (robo_gfx_rect_contains_point(close_bounds, point)) {
            out->area = ROBO_DESKTOP_AREA_WINDOW_CLOSE;
        } else if (robo_gfx_rect_contains_point(title_bounds, point)) {
            out->area = ROBO_DESKTOP_AREA_WINDOW_TITLE;
        } else {
            out->area = ROBO_DESKTOP_AREA_WINDOW_CONTENT;
        }
        return ROBO_OK;
    }

    if (robo_gfx_rect_contains_point(desktop->geometry.panel, point)) {
        out->area = ROBO_DESKTOP_AREA_PANEL;
    } else if (out->inside) {
        out->area = ROBO_DESKTOP_AREA_BACKGROUND;
    }
    return ROBO_OK;
}

robo_result_t robo_desktop_observe_pointer(robo_desktop_t *desktop,
                                           robo_gfx_point_t point,
                                           int pressed,
                                           robo_desktop_diag_t *diag)
{
    robo_desktop_hit_t hit;
    robo_result_t status;

    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    status = robo_desktop_hit_test(desktop, point, &hit);
    if (status != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, status,
                                 "the pointer's position could not be resolved");
    }

    desktop->pointer.point = point;
    desktop->pointer.inside = hit.inside;
    desktop->pointer.area = hit.area;
    desktop->pointer.window = hit.window;
    desktop->pointer.pressed = pressed ? 1 : 0;

    if (!pressed) {
        desktop->stats.pointer_moves++;
        return ROBO_OK;
    }

    /*
     * A press, and the whole of this build's input: two actions, both about *windows*. A press that hit
     * nothing is a legitimate press and is a success -- a shell that reported an error for a click on the
     * desktop surface would be reporting an error for the most ordinary thing a user does.
     */
    switch (hit.area) {
    case ROBO_DESKTOP_AREA_WINDOW_CLOSE:
        if (robo_desktop_window_request_close(desktop, hit.window) == ROBO_OK) {
            desktop->stats.pointer_actions++;
        }
        break;
    case ROBO_DESKTOP_AREA_WINDOW_TITLE:
    case ROBO_DESKTOP_AREA_WINDOW_CONTENT:
        if (robo_desktop_window_raise(desktop, hit.window) == ROBO_OK) {
            desktop->stats.pointer_actions++;
        }
        break;
    default:
        /* The panel and the desktop surface: a shell with a launcher would act here. This build has no
         * launcher, and inventing an action for a click on an empty panel would be inventing a feature. */
        break;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

robo_result_t robo_desktop_stats(const robo_desktop_t *desktop, robo_desktop_stats_t *out)
{
    if (!desktop || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = desktop->stats;
    return ROBO_OK;
}

robo_result_t robo_desktop_pointer(const robo_desktop_t *desktop, robo_desktop_pointer_t *out)
{
    if (!desktop || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = desktop->pointer;
    return ROBO_OK;
}
