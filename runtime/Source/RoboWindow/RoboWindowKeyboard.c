#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static uint8_t g_pressed[ROBO_PHYSICAL_KEY_COUNT];
static uint8_t g_logical_pressed[ROBO_LOGICAL_KEY_COUNT];
static robo_key_modifier_flags_t g_modifiers;

void robo_keyboard_event_init(robo_keyboard_event_t *ev)
{
    if (!ev) {
        return;
    }
    memset(ev, 0, sizeof(*ev));
    ev->action = ROBO_KEY_ACTION_NONE;
    ev->physical_key = ROBO_PHYSICAL_KEY_UNKNOWN;
    ev->logical_key = ROBO_LOGICAL_KEY_UNKNOWN;
    ev->window_id = ROBO_WINDOW_ID_NONE;
}

void robo_keyboard_reset_pressed_state(void)
{
    memset(g_pressed, 0, sizeof(g_pressed));
    memset(g_logical_pressed, 0, sizeof(g_logical_pressed));
    /* Keep lock bits; clear momentary modifiers. */
    g_modifiers &= (ROBO_KEY_MOD_CAPS_LOCK | ROBO_KEY_MOD_NUM_LOCK |
                    ROBO_KEY_MOD_SCROLL_LOCK);
    robo_log_debug("robo_keyboard: pressed state reset");
}

void robo_keyboard_reset(void)
{
    memset(g_pressed, 0, sizeof(g_pressed));
    memset(g_logical_pressed, 0, sizeof(g_logical_pressed));
    g_modifiers = ROBO_KEY_MOD_NONE;
    robo_log_debug("robo_keyboard: full reset");
}

int robo_keyboard_is_physical_pressed(robo_physical_key_t physical)
{
    if (!robo_physical_key_is_known(physical)) {
        return 0;
    }
    return g_pressed[physical] != 0;
}

int robo_keyboard_is_logical_pressed(robo_logical_key_t logical)
{
    if (!robo_logical_key_is_known(logical)) {
        return 0;
    }
    return g_logical_pressed[logical] != 0;
}

robo_key_modifier_flags_t robo_keyboard_modifiers(void)
{
    return g_modifiers;
}

int robo_keyboard_modifier_active(robo_key_modifier_flags_t flag)
{
    return (g_modifiers & flag) != 0;
}

static void apply_lock_from_modifiers(robo_key_modifier_flags_t mods)
{
    g_modifiers &= ~(ROBO_KEY_MOD_CAPS_LOCK | ROBO_KEY_MOD_NUM_LOCK |
                     ROBO_KEY_MOD_SCROLL_LOCK);
    g_modifiers |=
        (mods & (ROBO_KEY_MOD_CAPS_LOCK | ROBO_KEY_MOD_NUM_LOCK |
                 ROBO_KEY_MOD_SCROLL_LOCK));
}

static void refresh_momentary_modifiers(void)
{
    robo_key_modifier_flags_t locks =
        g_modifiers & (ROBO_KEY_MOD_CAPS_LOCK | ROBO_KEY_MOD_NUM_LOCK |
                       ROBO_KEY_MOD_SCROLL_LOCK);
    robo_key_modifier_flags_t m = locks;

    if (g_pressed[ROBO_PHYSICAL_KEY_SHIFT_LEFT] ||
        g_pressed[ROBO_PHYSICAL_KEY_SHIFT_RIGHT]) {
        m |= ROBO_KEY_MOD_SHIFT;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_SHIFT_LEFT]) {
        m |= ROBO_KEY_MOD_SHIFT_LEFT;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_SHIFT_RIGHT]) {
        m |= ROBO_KEY_MOD_SHIFT_RIGHT;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_CONTROL_LEFT] ||
        g_pressed[ROBO_PHYSICAL_KEY_CONTROL_RIGHT]) {
        m |= ROBO_KEY_MOD_CTRL;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_CONTROL_LEFT]) {
        m |= ROBO_KEY_MOD_CTRL_LEFT;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_CONTROL_RIGHT]) {
        m |= ROBO_KEY_MOD_CTRL_RIGHT;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_ALT_LEFT] ||
        g_pressed[ROBO_PHYSICAL_KEY_ALT_RIGHT]) {
        m |= ROBO_KEY_MOD_ALT;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_ALT_LEFT]) {
        m |= ROBO_KEY_MOD_ALT_LEFT;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_ALT_RIGHT]) {
        m |= ROBO_KEY_MOD_ALT_RIGHT;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_META_LEFT] ||
        g_pressed[ROBO_PHYSICAL_KEY_META_RIGHT]) {
        m |= ROBO_KEY_MOD_META;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_META_LEFT]) {
        m |= ROBO_KEY_MOD_META_LEFT;
    }
    if (g_pressed[ROBO_PHYSICAL_KEY_META_RIGHT]) {
        m |= ROBO_KEY_MOD_META_RIGHT;
    }
    g_modifiers = m;
}

static void update_state_from_event(const robo_keyboard_event_t *ev)
{
    apply_lock_from_modifiers(ev->modifiers);

    if (robo_physical_key_is_known(ev->physical_key)) {
        if (ev->action == ROBO_KEY_ACTION_DOWN) {
            g_pressed[ev->physical_key] = 1;
        } else if (ev->action == ROBO_KEY_ACTION_UP) {
            g_pressed[ev->physical_key] = 0;
        }
    }
    if (robo_logical_key_is_known(ev->logical_key)) {
        if (ev->action == ROBO_KEY_ACTION_DOWN) {
            g_logical_pressed[ev->logical_key] = 1;
        } else if (ev->action == ROBO_KEY_ACTION_UP) {
            g_logical_pressed[ev->logical_key] = 0;
        }
    }

    /* Prefer explicit modifier snapshot from backend when provided. */
    if (ev->modifiers != ROBO_KEY_MOD_NONE ||
        (ev->modifiers & (ROBO_KEY_MOD_CAPS_LOCK | ROBO_KEY_MOD_NUM_LOCK |
                          ROBO_KEY_MOD_SCROLL_LOCK)) != 0 ||
        ev->action == ROBO_KEY_ACTION_UP ||
        ev->action == ROBO_KEY_ACTION_DOWN) {
        /* Merge: locks from event; momentary from event if any set, else recompute. */
        if ((ev->modifiers &
             (ROBO_KEY_MOD_SHIFT | ROBO_KEY_MOD_CTRL | ROBO_KEY_MOD_ALT |
              ROBO_KEY_MOD_META | ROBO_KEY_MOD_SHIFT_LEFT |
              ROBO_KEY_MOD_SHIFT_RIGHT | ROBO_KEY_MOD_CTRL_LEFT |
              ROBO_KEY_MOD_CTRL_RIGHT | ROBO_KEY_MOD_ALT_LEFT |
              ROBO_KEY_MOD_ALT_RIGHT | ROBO_KEY_MOD_META_LEFT |
              ROBO_KEY_MOD_META_RIGHT)) != 0) {
            g_modifiers = ev->modifiers;
        } else {
            refresh_momentary_modifiers();
            apply_lock_from_modifiers(ev->modifiers);
        }
    }
}

robo_result_t robo_window_observe_key(robo_window_id_t window_id,
                                      const robo_keyboard_event_t *ev)
{
    robo_result_t r;
    robo_window_id_t target;
    robo_window_event_t we;
    robo_keyboard_event_t local;

    if (!ev) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (ev->action != ROBO_KEY_ACTION_DOWN && ev->action != ROBO_KEY_ACTION_UP) {
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
        target = robo_window_find_focused_id();
    }

    update_state_from_event(&local);

    if (!robo_physical_key_is_known(local.physical_key) &&
        !robo_logical_key_is_known(local.logical_key)) {
        robo_log_debug("robo_keyboard: unmapped key dropped action=%s",
                       robo_key_action_name(local.action));
        /* Still OK — do not crash; state already absorbed locks if any. */
        return ROBO_OK;
    }

    if (target == ROBO_WINDOW_ID_NONE || !robo_window_id_is_live(target)) {
        robo_log_debug("robo_keyboard: no live target for action=%s phys=%s",
                       robo_key_action_name(local.action),
                       robo_physical_key_name(local.physical_key));
        return ROBO_NOT_FOUND;
    }

    robo_window_event_init(&we);
    we.type = (local.action == ROBO_KEY_ACTION_DOWN)
                  ? ROBO_WINDOW_EVENT_KEY_DOWN
                  : ROBO_WINDOW_EVENT_KEY_UP;
    we.window_id = target;
    we.key = local;
    we.key.window_id = target;

    r = robo_window_event_push(&we);
    if (r != ROBO_OK) {
        robo_log_warn("robo_keyboard: queue push failed (%d)", (int)r);
        return r;
    }
    robo_log_debug(
        "robo_keyboard: %s id=%llu phys=%s log=%s mods=0x%x repeat=%d",
        robo_key_action_name(local.action), (unsigned long long)target,
        robo_physical_key_name(local.physical_key),
        robo_logical_key_name(local.logical_key), (unsigned)g_modifiers,
        local.is_repeat);
    return ROBO_OK;
}
