/*
 * RoboDesktop's checkpoint scene (Phase 11.13) — see RoboDesktopScene.h.
 *
 * Three things live here and nothing else: the checkpoint's own words (the markup, the author sheet, the
 * window's title), the order a scene is opened and released in, and the iteration a launcher drives.
 *
 * The iteration is the part worth reading. Every frame of a driven desktop runs the same four steps, and
 * they are in this order because each depends on the one before it:
 *
 *   pump    the window queue, bounded by the wait the caller named, and take what it holds;
 *   hand    the pointer to the shell — `robo_desktop_observe_pointer` is the whole of this build's input
 *           integration (RoboDesktop.h §16), and a scene must not invent more of one;
 *   follow  the destination: `robo_desktop_sync_size` matches the screen to the window's *physical* client
 *           size, which is how a user's resize reaches the shell;
 *   paint   and present, once, when there is something new to show.
 *
 * A scene keeps no clock and no frame counter: `needs_paint` is set by the things that change a picture
 * (a resize, an event, the caller's own `invalidate`) and cleared by the frame that answers them. So a
 * launcher that nothing happens to paints one frame and then waits, and a launcher under a mouse paints
 * exactly as often as it observes.
 */

#include "RoboDesktop/RoboDesktopScene.h"

#include "RoboMem/RoboMem.h"

#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowDisplay.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindow/RoboWindowEventLoop.h"

/*
 * The checkpoint's words.
 *
 * The markup is what a first application on this system is: a card, a headline, a line of copy and a
 * footnote — enough structure that a missing stage shows up as a wrong *picture* rather than as a
 * plausible one. Every element is one RoboLayout can lay out today (a block box with text inside it), and
 * the sheet names only properties `RoboCSSPropertyRegistry.c` registers, so a document that failed to
 * build here would be a fact about the pipeline and not about this file's vocabulary.
 *
 * The colours are the documented palette (AGENTS.md): the page's `#F5F5F7`, the text's `#1D1D1F`, the
 * secondary `#6E6E73`, the tertiary `#86868B` and the one accent `#0E7AFF` on the button. They are written
 * as hex in *markup*, which is what markup is for; the theme's own copy of the same tokens is what the
 * shell draws its panel with, and the two agreeing is the checkpoint's point.
 */
static const char SCENE_MARKUP[] =
    "<html><body>"
    "<div class='page'>"
    "<div class='headline'>RoboticelaOS</div>"
    "<div class='lede'>This window is a document. The panel above it is the shell.</div>"
    "<div class='detail'>HTML, CSS, layout, paint and one rasteriser, on one screen.</div>"
    "<div class='button'>Continue</div>"
    "</div>"
    "</body></html>";

static const char SCENE_STYLE[] =
    "html, body { display: block; margin: 0; padding: 0;"
    "  background-color: #f5f5f7; color: #1d1d1f }"
    ".page { display: block; padding: 32px }"
    ".headline { display: block; font-size: 28px; line-height: 36px }"
    ".lede { display: block; font-size: 15px; line-height: 24px; color: #6e6e73 }"
    ".detail { display: block; font-size: 12px; line-height: 20px; color: #86868b;"
    "  background-color: #ffffff; padding: 16px }"
    ".button { display: block; font-size: 15px; line-height: 24px; color: #ffffff;"
    "  background-color: #0e7aff; padding: 12px }";

struct robo_desktop_scene {
    robo_desktop_t *desktop;
    robo_desktop_document_t *document;
    robo_desktop_window_id_t window;
    /** Borrowed: the caller's face, kept only so a rebuilt document is built with the same one. */
    robo_font_face_t *face;
    /** Non-zero when the next step must paint a frame. */
    int needs_paint;
    /** Non-zero once the checkpoint is finished: the shell's window is gone or its native window closed. */
    int finished;
};

/* ------------------------------------------------------------------ */
/* Own string helpers                                                  */
/* ------------------------------------------------------------------ */

/*
 * This file is compiled into the guest program, which is freestanding: `string.h` is not available to it
 * and a `strlen` that linked would be a hosted contract this module does not own. Two one-line readers are
 * cheaper than the dependency, and the same reason (and the same shape) as `RoboDesktopText.c`.
 */
static size_t scene_length(const char *text)
{
    size_t length = 0u;

    if (!text) {
        return 0u;
    }
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

/* ------------------------------------------------------------------ */
/* The scene's words                                                   */
/* ------------------------------------------------------------------ */

const char *robo_desktop_scene_markup(void)
{
    return SCENE_MARKUP;
}

const char *robo_desktop_scene_style(void)
{
    return SCENE_STYLE;
}

size_t robo_desktop_scene_markup_length(void)
{
    return scene_length(SCENE_MARKUP);
}

void robo_desktop_scene_config_init(robo_desktop_scene_config_t *config)
{
    if (!config) {
        return;
    }
    config->width = ROBO_DESKTOP_SCENE_DEFAULT_WIDTH;
    config->height = ROBO_DESKTOP_SCENE_DEFAULT_HEIGHT;
    config->face = NULL;
    config->create_window = 1;
    config->window_title = NULL;
    config->status_text = NULL;
    config->identity_text = NULL;
    config->applications_text = NULL;
}

void robo_desktop_scene_step_init(robo_desktop_scene_step_t *step)
{
    if (!step) {
        return;
    }
    step->events = 0u;
    step->pointer_actions = 0u;
    step->resized = 0;
    step->rebuilt = 0;
    step->painted = 0;
    step->presented = 0;
    step->window_presented = 0;
    step->close_requested = 0;
    robo_desktop_frame_record_init(&step->frame);
    robo_render_present_record_init(&step->present);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

/**
 * Build the document the scene's window shows, at the client size the *shell* computed.
 *
 * The client rectangle is asked for rather than derived, and that is the same rule the desktop's own suite
 * follows: a document built for a size the shell did not compute would be composited at one size and laid
 * out for another, which is the defect this call exists to make unexpressible.
 *
 * The old document is released **before** the new contents are attached, because a window borrows a display
 * list: attaching the new list while the old document still owned nothing would be fine, but releasing the
 * old one *after* attaching a new list from it is the order that would leave a window pointing at freed
 * memory.
 */
static robo_result_t scene_build_document(robo_desktop_scene_t *scene, robo_desktop_diag_t *diag)
{
    robo_desktop_document_config_t config;
    robo_desktop_document_t *document = NULL;
    robo_gfx_rect_t client;
    robo_result_t result;

    if (robo_desktop_window_client_bounds(scene->desktop, scene->window, &client) != ROBO_OK) {
        return ROBO_NOT_FOUND;
    }
    if (client.width <= 0 || client.height <= 0) {
        /* A window grown to nothing shows nothing; that is not a document that failed to build. */
        return ROBO_OK;
    }
    robo_desktop_document_config_init(&config);
    config.html = SCENE_MARKUP;
    config.css = SCENE_STYLE;
    config.width = client.width;
    config.height = client.height;
    config.face = scene->face;

    result = robo_desktop_document_build(&config, &document, diag);
    if (result != ROBO_OK) {
        return result;
    }
    robo_desktop_document_destroy(scene->document);
    scene->document = document;
    return robo_desktop_window_set_content(scene->desktop, scene->window,
                                          robo_desktop_document_display_list(document),
                                          scene->face, diag);
}

/**
 * The window's outer rectangle: the work area less a margin on every side.
 *
 * Not the whole work area, and not the screen: a window that filled the screen would hide the panel's
 * relationship to the surface behind it, which is one of the three depth levels the theme is arranged
 * around, and a checkpoint that hid the thing it demonstrates would be a checkpoint nobody could read. A
 * screen too small for the margin degrades to the work area itself rather than to a negative rectangle.
 */
static void scene_window_bounds(const robo_desktop_t *desktop, int32_t *out_width, int32_t *out_height)
{
    robo_gfx_rect_t work = robo_desktop_work_area(desktop);
    int32_t width = work.width - ROBO_DESKTOP_SCENE_MARGIN * 2;
    int32_t height = work.height - ROBO_DESKTOP_SCENE_MARGIN * 2;

    if (width < 1) {
        width = work.width;
    }
    if (height < 1) {
        height = work.height;
    }
    *out_width = width;
    *out_height = height;
}

robo_result_t robo_desktop_scene_open(const robo_desktop_scene_config_t *config,
                                      robo_desktop_scene_t **out,
                                      robo_desktop_diag_t *diag)
{
    robo_desktop_config_t shell_config;
    robo_desktop_window_desc_t desc;
    robo_desktop_scene_t *scene;
    int32_t width;
    int32_t height;
    int32_t window_width;
    int32_t window_height;
    robo_result_t result;

    if (!config || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    width = (config->width > 0) ? config->width : ROBO_DESKTOP_SCENE_DEFAULT_WIDTH;
    height = (config->height > 0) ? config->height : ROBO_DESKTOP_SCENE_DEFAULT_HEIGHT;

    scene = (robo_desktop_scene_t *)robo_malloc(sizeof(*scene));
    if (!scene) {
        return ROBO_OUT_OF_MEMORY;
    }
    scene->desktop = NULL;
    scene->document = NULL;
    scene->window = ROBO_DESKTOP_WINDOW_ID_NONE;
    scene->face = config->face;
    scene->needs_paint = 1;
    scene->finished = 0;

    robo_desktop_config_init(&shell_config);
    shell_config.width = width;
    shell_config.height = height;
    shell_config.face = config->face;
    shell_config.create_window = config->create_window ? 1 : 0;
    shell_config.window_title = config->window_title;
    shell_config.status_text = config->status_text;
    if (config->identity_text) {
        shell_config.identity_text = config->identity_text;
    }
    if (config->applications_text) {
        shell_config.applications_text = config->applications_text;
    }

    result = robo_desktop_create(&shell_config, &scene->desktop, diag);
    if (result != ROBO_OK) {
        robo_desktop_scene_close(scene);
        return result;
    }

    scene_window_bounds(scene->desktop, &window_width, &window_height);
    robo_desktop_window_desc_init(&desc);
    desc.title = ROBO_DESKTOP_SCENE_WINDOW_TITLE;
    /* Negative origin: the shell places the window (a third down the work area, centred horizontally),
     * which is what keeps the checkpoint's composition the shell's business rather than this file's. */
    desc.x = -1;
    desc.y = -1;
    desc.width = window_width;
    desc.height = window_height;

    result = robo_desktop_window_add(scene->desktop, &desc, &scene->window, diag);
    if (result != ROBO_OK) {
        robo_desktop_scene_close(scene);
        return result;
    }

    result = scene_build_document(scene, diag);
    if (result != ROBO_OK) {
        robo_desktop_scene_close(scene);
        return result;
    }
    (void)robo_desktop_window_focus(scene->desktop, scene->window);

    *out = scene;
    return ROBO_OK;
}

void robo_desktop_scene_close(robo_desktop_scene_t *scene)
{
    if (!scene) {
        return;
    }
    /* Reverse of the borrowing order: the document's list is borrowed by the window, so the window goes
     * first, then the document, then the shell that owned neither. */
    robo_desktop_document_destroy(scene->document);
    scene->document = NULL;
    robo_desktop_destroy(scene->desktop);
    scene->desktop = NULL;
    robo_free(scene);
}

int robo_desktop_scene_is_live(const robo_desktop_scene_t *scene)
{
    return scene != NULL && scene->desktop != NULL;
}

robo_desktop_t *robo_desktop_scene_desktop(robo_desktop_scene_t *scene)
{
    return scene ? scene->desktop : NULL;
}

robo_desktop_window_id_t robo_desktop_scene_window(const robo_desktop_scene_t *scene)
{
    return scene ? scene->window : ROBO_DESKTOP_WINDOW_ID_NONE;
}

const robo_desktop_document_t *robo_desktop_scene_document(const robo_desktop_scene_t *scene)
{
    return scene ? scene->document : NULL;
}

void robo_desktop_scene_invalidate(robo_desktop_scene_t *scene)
{
    if (scene) {
        scene->needs_paint = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Stepping                                                            */
/* ------------------------------------------------------------------ */

/** Hand one pointer state to the shell, counting it only when the shell acted on it. */
static void scene_observe_pointer(robo_desktop_scene_t *scene,
                                  int32_t x,
                                  int32_t y,
                                  int pressed,
                                  robo_desktop_scene_step_t *step,
                                  robo_desktop_diag_t *diag)
{
    robo_desktop_stats_t before;
    robo_desktop_stats_t after;

    before.pointer_actions = 0u;
    after.pointer_actions = 0u;
    (void)robo_desktop_stats(scene->desktop, &before);
    (void)robo_desktop_observe_pointer(scene->desktop, robo_gfx_point(x, y), pressed, diag);
    (void)robo_desktop_stats(scene->desktop, &after);
    if (after.pointer_actions > before.pointer_actions) {
        step->pointer_actions += after.pointer_actions - before.pointer_actions;
    }
}

/**
 * Take everything the window queue holds and act on it.
 *
 * Only the events this build has something to *do* with are acted on, and a key press is deliberately not
 * one of them: this build has no keyboard routing (`RoboDesktop.h`'s capability table reports
 * `input_delivery == 0`), and a scene that invented a key meaning would be inventing the ABI those phases
 * have to agree on.
 */
static void scene_drain_events(robo_desktop_scene_t *scene,
                               robo_desktop_scene_step_t *step,
                               robo_desktop_diag_t *diag)
{
    robo_window_event_t event;

    while (robo_window_event_poll(&event) == ROBO_OK) {
        step->events++;
        switch (event.type) {
        case ROBO_WINDOW_EVENT_CLOSE_REQUESTED:
        case ROBO_WINDOW_EVENT_CLOSED:
            /* The shell's own window is closing: the checkpoint is over, whatever the app window is doing. */
            scene->finished = 1;
            step->close_requested = 1;
            break;
        case ROBO_WINDOW_EVENT_RESIZED:
        case ROBO_WINDOW_EVENT_SCALE_CHANGED:
            scene->needs_paint = 1;
            break;
        case ROBO_WINDOW_EVENT_MOUSE_MOVE:
            scene_observe_pointer(scene, event.mouse.client_x, event.mouse.client_y, 0, step, diag);
            break;
        case ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN:
            if (event.mouse.button == ROBO_MOUSE_BUTTON_LEFT) {
                scene_observe_pointer(scene, event.mouse.client_x, event.mouse.client_y, 1, step, diag);
            }
            break;
        case ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP:
            if (event.mouse.button == ROBO_MOUSE_BUTTON_LEFT) {
                scene_observe_pointer(scene, event.mouse.client_x, event.mouse.client_y, 0, step, diag);
            }
            break;
        default:
            break;
        }
    }
}

/** The application window's close button, honoured: the shell's window goes and the checkpoint is over. */
static void scene_retire_closed_window(robo_desktop_scene_t *scene, robo_desktop_scene_step_t *step)
{
    robo_desktop_window_state_t state = ROBO_DESKTOP_WINDOW_OPEN;

    if (scene->window == ROBO_DESKTOP_WINDOW_ID_NONE) {
        return;
    }
    if (robo_desktop_window_state(scene->desktop, scene->window, &state) != ROBO_OK ||
        state != ROBO_DESKTOP_WINDOW_CLOSING) {
        return;
    }
    robo_desktop_document_destroy(scene->document);
    scene->document = NULL;
    (void)robo_desktop_window_destroy(scene->desktop, scene->window);
    scene->window = ROBO_DESKTOP_WINDOW_ID_NONE;
    scene->finished = 1;
    scene->needs_paint = 1;
    step->close_requested = 1;
}

robo_result_t robo_desktop_scene_step(robo_desktop_scene_t *scene,
                                      uint32_t wait_ms,
                                      robo_desktop_scene_step_t *out_step,
                                      robo_desktop_diag_t *diag)
{
    robo_desktop_scene_step_t step;
    int resized = 0;
    robo_result_t result;

    if (!scene || !scene->desktop) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_desktop_scene_step_init(&step);

    /*
     * The window queue is drained only when RoboWindow came up. A desktop without it is a desktop this
     * module supports (`RoboDesktop.h` reads RoboWindow rather than requiring it), and a scene that pumped
     * an uninitialised module's loop would be the one place that assumed differently.
     */
    if (robo_window_is_initialized()) {
        (void)robo_window_event_loop_run_once(wait_ms);
        scene_drain_events(scene, &step, diag);
        scene_retire_closed_window(scene, &step);
    }

    if (robo_desktop_attached_window(scene->desktop) != ROBO_WINDOW_ID_NONE) {
        result = robo_desktop_sync_size(scene->desktop, &resized, diag);
        if (result == ROBO_OK && resized) {
            step.resized = 1;
            scene->needs_paint = 1;
            /* A document is laid out for a viewport: a resized client area gets a rebuilt picture, not a
             * stretched one. */
            if (scene->window != ROBO_DESKTOP_WINDOW_ID_NONE &&
                scene_build_document(scene, diag) == ROBO_OK) {
                step.rebuilt = 1;
            }
        }
    }

    if (scene->needs_paint) {
        result = robo_desktop_render(scene->desktop, &step.frame, diag);
        if (result != ROBO_OK) {
            if (out_step) {
                *out_step = step;
            }
            return result;
        }
        scene->needs_paint = 0;
        step.painted = 1;

        if (robo_desktop_attached_window(scene->desktop) != ROBO_WINDOW_ID_NONE) {
            result = robo_desktop_present(scene->desktop, &step.present, &step.window_presented, diag);
            if (result != ROBO_OK) {
                if (out_step) {
                    *out_step = step;
                }
                return result;
            }
            step.presented = 1;
        }
    }

    if (out_step) {
        *out_step = step;
    }
    return ROBO_OK;
}
