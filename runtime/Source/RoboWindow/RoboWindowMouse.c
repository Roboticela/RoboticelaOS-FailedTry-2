#include "RoboWindow/RoboWindowMouse.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static uint8_t g_pressed[ROBO_MOUSE_BUTTON_COUNT];
static int g_have_pos;
static int32_t g_pos_x;
static int32_t g_pos_y;
static robo_key_modifier_flags_t g_modifiers;
static robo_window_id_t g_capture_id = ROBO_WINDOW_ID_NONE;

/* release_capture is defined below; used by reset helpers. */
robo_result_t robo_window_mouse_release_capture(void);

/* Click-count foundation (no gesture engine). */
static robo_mouse_button_t g_last_click_button = ROBO_MOUSE_BUTTON_NONE;
static uint64_t g_last_click_ms;
static int32_t g_last_click_x;
static int32_t g_last_click_y;
static int g_last_click_count;

void robo_mouse_event_init(robo_mouse_event_t *ev)
{
    if (!ev) {
        return;
    }
    memset(ev, 0, sizeof(*ev));
    ev->type = ROBO_MOUSE_EVENT_NONE;
    ev->button = ROBO_MOUSE_BUTTON_NONE;
    ev->scroll_unit = ROBO_MOUSE_SCROLL_UNIT_LINE;
    ev->window_id = ROBO_WINDOW_ID_NONE;
}

int robo_mouse_button_is_known(robo_mouse_button_t button)
{
    return button >= ROBO_MOUSE_BUTTON_LEFT && button <= ROBO_MOUSE_BUTTON_X2;
}

const char *robo_mouse_button_name(robo_mouse_button_t button)
{
    switch (button) {
    case ROBO_MOUSE_BUTTON_NONE:
        return "none";
    case ROBO_MOUSE_BUTTON_LEFT:
        return "left";
    case ROBO_MOUSE_BUTTON_RIGHT:
        return "right";
    case ROBO_MOUSE_BUTTON_MIDDLE:
        return "middle";
    case ROBO_MOUSE_BUTTON_X1:
        return "x1";
    case ROBO_MOUSE_BUTTON_X2:
        return "x2";
    case ROBO_MOUSE_BUTTON_UNKNOWN:
        return "unknown";
    default:
        return "invalid";
    }
}

const char *robo_mouse_event_type_name(robo_mouse_event_type_t type)
{
    switch (type) {
    case ROBO_MOUSE_EVENT_NONE:
        return "none";
    case ROBO_MOUSE_EVENT_MOVE:
        return "move";
    case ROBO_MOUSE_EVENT_BUTTON_DOWN:
        return "button_down";
    case ROBO_MOUSE_EVENT_BUTTON_UP:
        return "button_up";
    case ROBO_MOUSE_EVENT_WHEEL:
        return "wheel";
    default:
        return "invalid";
    }
}

void robo_mouse_reset_pressed_state(void)
{
    memset(g_pressed, 0, sizeof(g_pressed));
    g_have_pos = 0;
    g_pos_x = 0;
    g_pos_y = 0;
    g_last_click_button = ROBO_MOUSE_BUTTON_NONE;
    g_last_click_ms = 0u;
    g_last_click_x = 0;
    g_last_click_y = 0;
    g_last_click_count = 0;
    if (g_capture_id != ROBO_WINDOW_ID_NONE) {
        (void)robo_window_mouse_release_capture();
    }
    robo_log_debug("robo_mouse: pressed state reset");
}

void robo_mouse_reset(void)
{
    if (g_capture_id != ROBO_WINDOW_ID_NONE && robo_window_is_initialized()) {
        const robo_window_backend_vtbl_t *vtbl = robo_window_backend_vtbl();
        if (vtbl && vtbl->release_mouse_capture) {
            (void)vtbl->release_mouse_capture(robo_window_backend_userdata());
        }
    }
    memset(g_pressed, 0, sizeof(g_pressed));
    g_have_pos = 0;
    g_pos_x = 0;
    g_pos_y = 0;
    g_modifiers = ROBO_KEY_MOD_NONE;
    g_last_click_button = ROBO_MOUSE_BUTTON_NONE;
    g_last_click_ms = 0u;
    g_last_click_x = 0;
    g_last_click_y = 0;
    g_last_click_count = 0;
    g_capture_id = ROBO_WINDOW_ID_NONE;
    robo_log_debug("robo_mouse: full reset");
}

int robo_mouse_is_button_pressed(robo_mouse_button_t button)
{
    if (!robo_mouse_button_is_known(button)) {
        return 0;
    }
    return g_pressed[button] != 0;
}

int robo_mouse_get_position(robo_mouse_position_t *out)
{
    if (!out || !g_have_pos) {
        return 0;
    }
    out->x = g_pos_x;
    out->y = g_pos_y;
    return 1;
}

robo_key_modifier_flags_t robo_mouse_modifiers(void)
{
    return g_modifiers;
}

robo_window_id_t robo_window_mouse_captured_id(void)
{
    if (g_capture_id != ROBO_WINDOW_ID_NONE &&
        !robo_window_id_is_live(g_capture_id)) {
        g_capture_id = ROBO_WINDOW_ID_NONE;
    }
    return g_capture_id;
}

robo_result_t robo_window_mouse_capture(robo_window_id_t id)
{
    robo_result_t r;
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;
    robo_window_native_handle_t native;

    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    r = robo_window_thread_require_owner();
    if (r != ROBO_OK) {
        return r;
    }
    if (!robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }

    g_capture_id = id;

    memset(&caps, 0, sizeof(caps));
    if (robo_window_backend_capabilities(&caps) == ROBO_OK &&
        caps.mouse_capture) {
        vtbl = robo_window_backend_vtbl();
        if (vtbl && vtbl->capture_mouse) {
            memset(&native, 0, sizeof(native));
            if (robo_window_get_native_handle(id, &native) == ROBO_OK) {
                r = vtbl->capture_mouse(robo_window_backend_userdata(), id,
                                        &native);
                if (r != ROBO_OK) {
                    g_capture_id = ROBO_WINDOW_ID_NONE;
                    robo_log_warn("robo_mouse: native capture failed (%d)",
                                  (int)r);
                    return r;
                }
            }
        }
    }

    robo_log_debug("robo_mouse: capture id=%llu", (unsigned long long)id);
    return ROBO_OK;
}

robo_result_t robo_window_mouse_release_capture(void)
{
    robo_result_t r;
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;

    if (!robo_window_is_initialized()) {
        g_capture_id = ROBO_WINDOW_ID_NONE;
        return ROBO_OK;
    }
    r = robo_window_thread_require_owner();
    if (r != ROBO_OK) {
        return r;
    }

    memset(&caps, 0, sizeof(caps));
    if (robo_window_backend_capabilities(&caps) == ROBO_OK &&
        caps.mouse_capture) {
        vtbl = robo_window_backend_vtbl();
        if (vtbl && vtbl->release_mouse_capture) {
            (void)vtbl->release_mouse_capture(robo_window_backend_userdata());
        }
    }

    if (g_capture_id != ROBO_WINDOW_ID_NONE) {
        robo_log_debug("robo_mouse: release capture was id=%llu",
                       (unsigned long long)g_capture_id);
    }
    g_capture_id = ROBO_WINDOW_ID_NONE;
    return ROBO_OK;
}

static int click_slop_ok(int32_t x, int32_t y)
{
    const int32_t slop = 4;
    int32_t dx = x - g_last_click_x;
    int32_t dy = y - g_last_click_y;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    return dx <= slop && dy <= slop;
}

static int synthesize_click_count(robo_mouse_button_t button, int32_t x,
                                  int32_t y, uint64_t timestamp_ms)
{
    const uint64_t window_ms = 500u;

    if (button == ROBO_MOUSE_BUTTON_NONE ||
        button == ROBO_MOUSE_BUTTON_UNKNOWN) {
        return 0;
    }
    if (g_last_click_button == button && click_slop_ok(x, y) &&
        timestamp_ms != 0u && g_last_click_ms != 0u &&
        timestamp_ms >= g_last_click_ms &&
        (timestamp_ms - g_last_click_ms) <= window_ms) {
        g_last_click_count++;
    } else {
        g_last_click_count = 1;
    }
    g_last_click_button = button;
    g_last_click_ms = timestamp_ms;
    g_last_click_x = x;
    g_last_click_y = y;
    return g_last_click_count;
}

static void update_position(robo_mouse_event_t *local)
{
    if (g_have_pos) {
        if (local->delta_x == 0 && local->delta_y == 0) {
            local->delta_x = local->client_x - g_pos_x;
            local->delta_y = local->client_y - g_pos_y;
        }
    } else if (local->delta_x == 0 && local->delta_y == 0) {
        local->delta_x = 0;
        local->delta_y = 0;
    }
    g_pos_x = local->client_x;
    g_pos_y = local->client_y;
    g_have_pos = 1;
}

static void update_state_from_event(robo_mouse_event_t *local)
{
    g_modifiers = local->modifiers;
    update_position(local);

    if (local->type == ROBO_MOUSE_EVENT_BUTTON_DOWN &&
        robo_mouse_button_is_known(local->button)) {
        g_pressed[local->button] = 1;
        if (local->click_count <= 0) {
            local->click_count = synthesize_click_count(
                local->button, local->client_x, local->client_y,
                local->timestamp_ms);
        } else {
            g_last_click_button = local->button;
            g_last_click_ms = local->timestamp_ms;
            g_last_click_x = local->client_x;
            g_last_click_y = local->client_y;
            g_last_click_count = local->click_count;
        }
    } else if (local->type == ROBO_MOUSE_EVENT_BUTTON_UP &&
               robo_mouse_button_is_known(local->button)) {
        g_pressed[local->button] = 0;
    }
}

static robo_window_event_type_t map_window_event_type(
    robo_mouse_event_type_t t)
{
    switch (t) {
    case ROBO_MOUSE_EVENT_MOVE:
        return ROBO_WINDOW_EVENT_MOUSE_MOVE;
    case ROBO_MOUSE_EVENT_BUTTON_DOWN:
        return ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN;
    case ROBO_MOUSE_EVENT_BUTTON_UP:
        return ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP;
    case ROBO_MOUSE_EVENT_WHEEL:
        return ROBO_WINDOW_EVENT_MOUSE_WHEEL;
    default:
        return ROBO_WINDOW_EVENT_NONE;
    }
}

robo_result_t robo_window_observe_mouse(robo_window_id_t window_id,
                                        const robo_mouse_event_t *ev)
{
    robo_result_t r;
    robo_window_id_t target;
    robo_window_event_t we;
    robo_mouse_event_t local;
    robo_window_event_type_t wt;

    if (!ev) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (ev->type != ROBO_MOUSE_EVENT_MOVE &&
        ev->type != ROBO_MOUSE_EVENT_BUTTON_DOWN &&
        ev->type != ROBO_MOUSE_EVENT_BUTTON_UP &&
        ev->type != ROBO_MOUSE_EVENT_WHEEL) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    r = robo_window_thread_require_owner();
    if (r != ROBO_OK) {
        return r;
    }

    local = *ev;
    target = window_id;
    if (target == ROBO_WINDOW_ID_NONE) {
        if (g_capture_id != ROBO_WINDOW_ID_NONE &&
            robo_window_id_is_live(g_capture_id)) {
            target = g_capture_id;
        } else {
            target = robo_window_find_focused_id();
        }
    }

    if (local.type == ROBO_MOUSE_EVENT_BUTTON_DOWN ||
        local.type == ROBO_MOUSE_EVENT_BUTTON_UP) {
        if (local.button == ROBO_MOUSE_BUTTON_NONE ||
            local.button == ROBO_MOUSE_BUTTON_UNKNOWN ||
            !robo_mouse_button_is_known(local.button)) {
            robo_log_debug("robo_mouse: unmapped button dropped type=%s",
                           robo_mouse_event_type_name(local.type));
            g_modifiers = local.modifiers;
            return ROBO_OK;
        }
    }

    if (local.type == ROBO_MOUSE_EVENT_WHEEL) {
        if (local.scroll_x == 0 && local.scroll_y == 0) {
            robo_log_debug("robo_mouse: zero wheel delta ignored");
            g_modifiers = local.modifiers;
            update_position(&local);
            return ROBO_OK;
        }
    }

    update_state_from_event(&local);

    if (target == ROBO_WINDOW_ID_NONE || !robo_window_id_is_live(target)) {
        robo_log_debug("robo_mouse: no live target type=%s",
                       robo_mouse_event_type_name(local.type));
        return ROBO_NOT_FOUND;
    }

    wt = map_window_event_type(local.type);
    if (wt == ROBO_WINDOW_EVENT_NONE) {
        return ROBO_INVALID_ARGUMENT;
    }

    robo_window_event_init(&we);
    we.type = wt;
    we.window_id = target;
    we.x = local.client_x;
    we.y = local.client_y;
    we.mouse = local;
    we.mouse.window_id = target;

    r = robo_window_event_push(&we);
    if (r != ROBO_OK) {
        robo_log_warn("robo_mouse: queue push failed (%d)", (int)r);
        return r;
    }
    robo_log_debug(
        "robo_mouse: %s id=%llu btn=%s client=(%d,%d) delta=(%d,%d) "
        "scroll=(%d,%d) clicks=%d mods=0x%x",
        robo_mouse_event_type_name(local.type), (unsigned long long)target,
        robo_mouse_button_name(local.button), (int)local.client_x,
        (int)local.client_y, (int)local.delta_x, (int)local.delta_y,
        (int)local.scroll_x, (int)local.scroll_y, local.click_count,
        (unsigned)local.modifiers);
    return ROBO_OK;
}
