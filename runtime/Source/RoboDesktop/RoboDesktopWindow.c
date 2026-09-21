/*
 * RoboDesktop shell windows (Phase 11.13) — see RoboDesktopWindow.h.
 *
 * ## The list is an array, and the z-order is its order
 *
 * Eight slots, `windows[0]` furthest back, `windows[count - 1]` in front. A *list* rather than a
 * z-order field per window, and the reason is that the two have to agree: with an order field, "paint in
 * z-order" is a sort whose tie-breaking is a second decision, and "raise" is a renumbering that has to
 * update every window it moved past. With the array, raising is `memmove` one element to the end and the
 * painter is a loop. The cost is that `at(index)` is an index and not an id -- which is exactly what the
 * door says it is.
 *
 * Removing a window shifts the ones above it down, so `focused_index` is repaired on every removal rather
 * than being allowed to dangle: a shell whose focus pointed at a destroyed window would paint no accent
 * border and route a press to nothing, and neither would be a visible failure until someone looked.
 *
 * ## The minimum-size rule, and what it is not
 *
 * `robo_desktop_window_add` **grows** a window below the decoration minimum rather than refusing it. That
 * is the opposite of what this tree's `robo_window_set_size` does one module down (which rejects an
 * out-of-constraint size rather than clamping), and the difference is what the minimum *is*: RoboWindow's
 * constraints are the caller's policy, so silently overriding them would be overriding an intention; the
 * desktop's minimum is a property of decorations the caller does not control, so a request below it has
 * no expressible meaning and the smallest drawable window is the closest thing to what was asked for.
 * Both rules are right for their own subject, and this comment exists so the divergence reads as one.
 *
 * ## Closing is not destroying
 *
 * `request_close` marks a window CLOSING, hides it, and leaves it in the list for the owner to destroy --
 * see `RoboDesktopTypes.h`. The close button produces this and nothing else, which is why a desktop with
 * a close button is still a desktop whose windows' lifetimes belong to whoever made them.
 */

#include "RoboDesktopInternal.h"

#include "RoboBase/RoboLog.h"

/* ------------------------------------------------------------------ */
/* Descriptor vocabulary                                               */
/* ------------------------------------------------------------------ */

void robo_desktop_window_desc_init(robo_desktop_window_desc_t *desc)
{
    if (!desc) {
        return;
    }
    desc->title = NULL;
    /* Negative means "place me" -- the only state a caller that does not know the work area can ask for
     * (see the header). */
    desc->x = -1;
    desc->y = -1;
    desc->width = 560;
    desc->height = 360;
    desc->content = NULL;
    desc->content_face = NULL;
    desc->visible = 1;
}

robo_result_t robo_desktop_window_desc_validate(const robo_desktop_window_desc_t *desc)
{
    if (!desc) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (desc->width < 0 || desc->height < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (desc->width > (int32_t)ROBO_DESKTOP_MAX_DIMENSION ||
        desc->height > (int32_t)ROBO_DESKTOP_MAX_DIMENSION) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (desc->title && robo_desktop_string_length(desc->title) + 1u > ROBO_DESKTOP_TITLE_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

int robo_desktop_window_id_is_none(robo_desktop_window_id_t id)
{
    return id == ROBO_DESKTOP_WINDOW_ID_NONE;
}

int robo_desktop_window_id_equal(robo_desktop_window_id_t a, robo_desktop_window_id_t b)
{
    return a == b;
}

/* ------------------------------------------------------------------ */
/* The list                                                            */
/* ------------------------------------------------------------------ */

const desktop_window_t *robo_desktop_window_find_const(const robo_desktop_t *desktop,
                                                      robo_desktop_window_id_t id)
{
    size_t index;

    if (!desktop || robo_desktop_window_id_is_none(id)) {
        return NULL;
    }
    for (index = 0u; index < desktop->window_count; index++) {
        if (desktop->windows[index].id == id) {
            return &desktop->windows[index];
        }
    }
    return NULL;
}

desktop_window_t *robo_desktop_window_find(robo_desktop_t *desktop, robo_desktop_window_id_t id)
{
    return (desktop_window_t *)robo_desktop_window_find_const(desktop, id);
}

size_t robo_desktop_window_index_of(const robo_desktop_t *desktop, robo_desktop_window_id_t id)
{
    size_t index;

    for (index = 0u; index < desktop->window_count; index++) {
        if (desktop->windows[index].id == id) {
            return index;
        }
    }
    return ROBO_DESKTOP_NO_INDEX;
}

/** Focus falls to the front window, or to none. Called after every removal. */
void robo_desktop_window_repair_focus(robo_desktop_t *desktop)
{
    if (desktop->focused_index != ROBO_DESKTOP_NO_INDEX &&
        desktop->focused_index < desktop->window_count) {
        return;
    }
    desktop->focused_index =
        (desktop->window_count == 0u) ? ROBO_DESKTOP_NO_INDEX : (desktop->window_count - 1u);
}

void robo_desktop_window_release_target(desktop_window_t *window)
{
    if (window && window->target) {
        robo_render_target_destroy(window->target);
        window->target = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Public doors                                                        */
/* ------------------------------------------------------------------ */

size_t robo_desktop_window_count(const robo_desktop_t *desktop)
{
    return desktop ? desktop->window_count : 0u;
}

robo_result_t robo_desktop_window_at(const robo_desktop_t *desktop,
                                     size_t index,
                                     robo_desktop_window_id_t *out_id)
{
    if (!desktop || !out_id) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= desktop->window_count) {
        return ROBO_NOT_FOUND;
    }
    *out_id = desktop->windows[index].id;
    return ROBO_OK;
}

int robo_desktop_window_exists(const robo_desktop_t *desktop, robo_desktop_window_id_t id)
{
    return robo_desktop_window_find_const(desktop, id) != NULL;
}

robo_result_t robo_desktop_window_add(robo_desktop_t *desktop,
                                     const robo_desktop_window_desc_t *desc,
                                     robo_desktop_window_id_t *out_id,
                                     robo_desktop_diag_t *diag)
{
    desktop_window_t *window;
    robo_gfx_point_t placed;
    robo_gfx_rect_t frame;
    robo_result_t status;

    if (!desktop || !desc || !out_id) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_INVALID_ARGUMENT,
                                 "a desktop, a description and an out id are required");
    }
    status = robo_desktop_window_desc_validate(desc);
    if (status != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, status,
                                 "the window description is not one this build can create");
    }
    if (desktop->window_count >= ROBO_DESKTOP_MAX_WINDOWS) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_ALREADY_EXISTS,
                                 "the desktop already holds the maximum number of windows");
    }
    /*
     * "Is there a work area to place a window in?" is a question about *room*, not about emptiness: a
     * work area one pixel tall is a rectangle, so `robo_gfx_rect_is_empty` would answer no and the shell
     * would place a window it cannot draw. `ROBO_DESKTOP_MIN_DIMENSION` is the smallest work area this
     * build treats as usable, and it is tested on both axes -- a 400x4 work area is as unplaceable as a
     * 4x400 one. The geometry itself is total and stays total (a thin work area is a rectangle and a
     * caller can read it); this is the door where "drawable" is decided. See `RoboDesktopTypes.h`.
     */
    if (desktop->geometry.work_area.width < ROBO_DESKTOP_MIN_DIMENSION ||
        desktop->geometry.work_area.height < ROBO_DESKTOP_MIN_DIMENSION) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_SHELL, ROBO_INVALID_ARGUMENT,
                                 "there is no work area to place a window in");
    }

    /* The decorations come first: a window smaller than them cannot be drawn at all. */
    frame.width = (desc->width < desktop->window_metrics.min_width)
                      ? desktop->window_metrics.min_width
                      : desc->width;
    frame.height = (desc->height < desktop->window_metrics.min_height)
                       ? desktop->window_metrics.min_height
                       : desc->height;
    if (desc->x < 0 || desc->y < 0) {
        /* "Place me": the deterministic centred-nudged-up placement, then the work-area clamp, so a
         * default-sized window on a small screen is still fully visible. */
        status = robo_desktop_geometry_place(robo_gfx_size(frame.width, frame.height),
                                             &desktop->geometry.work_area, &placed);
        if (status != ROBO_OK) {
            return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, status,
                                     "the window could not be placed in the work area");
        }
        frame.x = placed.x;
        frame.y = placed.y;
    } else {
        frame.x = desc->x;
        frame.y = desc->y;
    }
    if (robo_desktop_geometry_contain(frame, &desktop->geometry.work_area, &frame) != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_ERROR,
                                 "the window's bounds could not be clamped into the work area");
    }

    window = &desktop->windows[desktop->window_count];
    /* Zeroed rather than field-assigned where a field has a meaningful "nothing": the array is reused
     * across additions, and a stale `target` or `content` from a destroyed window would be exactly the
     * cross-window leak this module can least afford. */
    {
        size_t bytes = sizeof(*window);
        size_t index;

        for (index = 0u; index < bytes; index++) {
            ((char *)window)[index] = 0;
        }
    }
    window->id = ++desktop->next_id;
    if (window->id == ROBO_DESKTOP_WINDOW_ID_NONE) {
        /* A wrap would hand out the "none" token, which is not a window: skip it. */
        window->id = ++desktop->next_id;
    }
    window->bounds = frame;
    window->state = ROBO_DESKTOP_WINDOW_OPEN;
    window->visible = desc->visible ? 1 : 0;
    window->content = desc->content;
    window->content_face = desc->content_face;
    window->target = NULL;
    if (desc->title &&
        robo_desktop_string_copy(window->title, sizeof(window->title), desc->title) != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_INVALID_ARGUMENT,
                                 "the window's title does not fit");
    }

    desktop->window_count++;
    desktop->stats.windows_added++;
    /* A new window is focused: it is what the caller just made, and on a desktop with no window manager
     * "the newest window is the one being used" is the only defensible default. */
    desktop->focused_index = desktop->window_count - 1u;
    *out_id = window->id;
    /* The *bounds* rather than the request: a grown or clamped window is the fact a reader needs, and
     * the placement decision is the one thing this door and `RoboDesktop.h` most often disagree about. */
    robo_log_debug("robo_desktop: window added id=%llu bounds=%d,%d %dx%d (count=%llu)",
                   (unsigned long long)window->id, (int)frame.x, (int)frame.y, (int)frame.width,
                   (int)frame.height, (unsigned long long)desktop->window_count);
    return ROBO_OK;
}

robo_result_t robo_desktop_window_set_bounds(robo_desktop_t *desktop,
                                            robo_desktop_window_id_t id,
                                            robo_gfx_rect_t bounds,
                                            robo_desktop_diag_t *diag)
{
    desktop_window_t *window;
    robo_gfx_rect_t clamped;

    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    window = robo_desktop_window_find(desktop, id);
    if (!window) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_NOT_FOUND,
                                 "no such window");
    }
    if (!robo_gfx_rect_is_valid(bounds)) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_INVALID_ARGUMENT,
                                 "the window's bounds are not a rectangle");
    }
    if (bounds.width < desktop->window_metrics.min_width) {
        bounds.width = desktop->window_metrics.min_width;
    }
    if (bounds.height < desktop->window_metrics.min_height) {
        bounds.height = desktop->window_metrics.min_height;
    }
    if (robo_desktop_geometry_contain(bounds, &desktop->geometry.work_area, &clamped) != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_ERROR,
                                 "the window's bounds could not be clamped into the work area");
    }
    if (robo_gfx_rect_equal(window->bounds, clamped)) {
        /* An unchanged size is not a no-op if the *client* area changed -- it cannot have, since the
         * decoration metrics are fixed -- so nothing has to be invalidated either way. */
        return ROBO_OK;
    }
    window->bounds = clamped;
    /* The content target is deliberately **not** recreated here: `robo_desktop_window_content_target`
     * rebuilds it when a frame finds it stale, so resizing eight windows allocates nothing until they are
     * painted (see `RoboDesktopInternal.h`). */
    return ROBO_OK;
}

robo_result_t robo_desktop_window_set_title(robo_desktop_t *desktop,
                                            robo_desktop_window_id_t id,
                                            const char *utf8_title,
                                            robo_desktop_diag_t *diag)
{
    desktop_window_t *window;

    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    window = robo_desktop_window_find(desktop, id);
    if (!window) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_NOT_FOUND,
                                 "no such window");
    }
    if (robo_desktop_string_copy(window->title, sizeof(window->title), utf8_title) != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_INVALID_ARGUMENT,
                                 "the title does not fit");
    }
    return ROBO_OK;
}

const char *robo_desktop_window_title(const robo_desktop_t *desktop, robo_desktop_window_id_t id)
{
    const desktop_window_t *window = robo_desktop_window_find_const(desktop, id);

    return window ? window->title : "";
}

robo_result_t robo_desktop_window_set_content(robo_desktop_t *desktop,
                                              robo_desktop_window_id_t id,
                                              const robo_paint_display_list_t *content,
                                              robo_font_face_t *content_face,
                                              robo_desktop_diag_t *diag)
{
    desktop_window_t *window;

    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    window = robo_desktop_window_find(desktop, id);
    if (!window) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_NOT_FOUND,
                                 "no such window");
    }
    /* Borrowed, not copied: the list belongs to whoever built it, and a copy would be a snapshot the
     * caller could not invalidate without telling the shell. */
    window->content = content;
    window->content_face = content_face;
    return ROBO_OK;
}

const robo_paint_display_list_t *robo_desktop_window_content(const robo_desktop_t *desktop,
                                                            robo_desktop_window_id_t id)
{
    const desktop_window_t *window = robo_desktop_window_find_const(desktop, id);

    return window ? window->content : NULL;
}

robo_result_t robo_desktop_window_set_visible(robo_desktop_t *desktop,
                                              robo_desktop_window_id_t id,
                                              int visible,
                                              robo_desktop_diag_t *diag)
{
    desktop_window_t *window;

    if (!desktop) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_INVALID_ARGUMENT,
                                 "a desktop is required");
    }
    window = robo_desktop_window_find(desktop, id);
    if (!window) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_WINDOW, ROBO_NOT_FOUND,
                                 "no such window");
    }
    window->visible = visible ? 1 : 0;
    return ROBO_OK;
}

int robo_desktop_window_is_visible(const robo_desktop_t *desktop, robo_desktop_window_id_t id)
{
    const desktop_window_t *window = robo_desktop_window_find_const(desktop, id);

    return (window && window->visible) ? 1 : 0;
}

robo_result_t robo_desktop_window_raise(robo_desktop_t *desktop, robo_desktop_window_id_t id)
{
    size_t index;
    desktop_window_t moved;
    size_t cursor;

    if (!desktop) {
        return ROBO_INVALID_ARGUMENT;
    }
    index = robo_desktop_window_index_of(desktop, id);
    if (index == ROBO_DESKTOP_NO_INDEX) {
        return ROBO_NOT_FOUND;
    }
    if (index + 1u != desktop->window_count) {
        moved = desktop->windows[index];
        for (cursor = index; cursor + 1u < desktop->window_count; cursor++) {
            desktop->windows[cursor] = desktop->windows[cursor + 1u];
        }
        desktop->windows[desktop->window_count - 1u] = moved;
    }
    /* Focus follows the raise; see the header of `RoboDesktop.h`'s raise door. */
    desktop->focused_index = desktop->window_count - 1u;
    return ROBO_OK;
}

robo_result_t robo_desktop_window_focus(robo_desktop_t *desktop, robo_desktop_window_id_t id)
{
    size_t index;

    if (!desktop) {
        return ROBO_INVALID_ARGUMENT;
    }
    index = robo_desktop_window_index_of(desktop, id);
    if (index == ROBO_DESKTOP_NO_INDEX) {
        return ROBO_NOT_FOUND;
    }
    desktop->focused_index = index;
    return ROBO_OK;
}

robo_desktop_window_id_t robo_desktop_window_focused(const robo_desktop_t *desktop)
{
    if (!desktop || desktop->focused_index == ROBO_DESKTOP_NO_INDEX ||
        desktop->focused_index >= desktop->window_count) {
        return ROBO_DESKTOP_WINDOW_ID_NONE;
    }
    return desktop->windows[desktop->focused_index].id;
}

robo_result_t robo_desktop_window_request_close(robo_desktop_t *desktop, robo_desktop_window_id_t id)
{
    desktop_window_t *window;

    if (!desktop) {
        return ROBO_INVALID_ARGUMENT;
    }
    window = robo_desktop_window_find(desktop, id);
    if (!window) {
        return ROBO_NOT_FOUND;
    }
    if (window->state != ROBO_DESKTOP_WINDOW_CLOSING) {
        window->state = ROBO_DESKTOP_WINDOW_CLOSING;
        /* Hidden as well as closing: a window the user asked to close must stop being painted at once,
         * and a state that only the owner reads would leave it on screen until the owner noticed. */
        window->visible = 0;
        desktop->stats.windows_closed++;
        robo_log_debug("robo_desktop: window close requested id=%llu",
                      (unsigned long long)window->id);
    }
    return ROBO_OK;
}

robo_result_t robo_desktop_window_state(const robo_desktop_t *desktop,
                                        robo_desktop_window_id_t id,
                                        robo_desktop_window_state_t *out)
{
    const desktop_window_t *window;

    if (!desktop || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    window = robo_desktop_window_find_const(desktop, id);
    if (!window) {
        return ROBO_NOT_FOUND;
    }
    *out = window->state;
    return ROBO_OK;
}

robo_result_t robo_desktop_window_destroy(robo_desktop_t *desktop, robo_desktop_window_id_t id)
{
    size_t index;
    size_t cursor;

    if (!desktop) {
        return ROBO_INVALID_ARGUMENT;
    }
    index = robo_desktop_window_index_of(desktop, id);
    if (index == ROBO_DESKTOP_NO_INDEX) {
        return ROBO_NOT_FOUND;
    }
    robo_desktop_window_release_target(&desktop->windows[index]);
    for (cursor = index; cursor + 1u < desktop->window_count; cursor++) {
        desktop->windows[cursor] = desktop->windows[cursor + 1u];
    }
    desktop->window_count--;
    /* The vacated slot is cleared so a later `add` cannot inherit a target that has been released. */
    {
        desktop_window_t *last = &desktop->windows[desktop->window_count];
        size_t bytes = sizeof(*last);
        size_t byte;

        for (byte = 0u; byte < bytes; byte++) {
            ((char *)last)[byte] = 0;
        }
    }
    if (desktop->focused_index == index) {
        desktop->focused_index = ROBO_DESKTOP_NO_INDEX;
    } else if (desktop->focused_index != ROBO_DESKTOP_NO_INDEX && desktop->focused_index > index) {
        desktop->focused_index--;
    }
    robo_desktop_window_repair_focus(desktop);
    desktop->stats.windows_destroyed++;
    robo_log_debug("robo_desktop: window destroyed id=%llu (count=%llu, focused=%llu)",
                   (unsigned long long)id, (unsigned long long)desktop->window_count,
                   (unsigned long long)robo_desktop_window_focused(desktop));
    return ROBO_OK;
}

robo_result_t robo_desktop_window_bounds(const robo_desktop_t *desktop,
                                         robo_desktop_window_id_t id,
                                         robo_gfx_rect_t *out)
{
    const desktop_window_t *window;

    if (!desktop || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    window = robo_desktop_window_find_const(desktop, id);
    if (!window) {
        return ROBO_NOT_FOUND;
    }
    *out = window->bounds;
    return ROBO_OK;
}

robo_result_t robo_desktop_window_client_bounds(const robo_desktop_t *desktop,
                                                robo_desktop_window_id_t id,
                                                robo_gfx_rect_t *out)
{
    const desktop_window_t *window;

    if (!desktop || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    window = robo_desktop_window_find_const(desktop, id);
    if (!window) {
        return ROBO_NOT_FOUND;
    }
    *out = robo_desktop_chrome_client_bounds(desktop, window);
    return ROBO_OK;
}
