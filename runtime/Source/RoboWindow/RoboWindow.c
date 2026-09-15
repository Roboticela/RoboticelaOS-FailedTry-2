#include "RoboWindow/RoboWindow.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindowMouse.h"
#include "RoboWindow/RoboWindowSize.h"

#include <string.h>

static int g_initialized;
static uint64_t g_next_id = 1u;
static robo_window_slot_t g_slots[ROBO_WINDOW_MAX_WINDOWS];
/** Depth while applying set_size / set_position / show / hide / focus. */
static int g_geometry_apply_depth;

const char *robo_window_version_string(void)
{
    return ROBO_WINDOW_VERSION_STRING;
}

int robo_window_is_initialized(void)
{
    return g_initialized;
}

robo_result_t robo_window_require_ready(void)
{
    robo_result_t r;

    if (!g_initialized) {
        return ROBO_INIT_ERROR;
    }
    r = robo_window_thread_require_owner();
    return r;
}

static robo_result_t require_ready(void)
{
    return robo_window_require_ready();
}

static size_t title_byte_len(const char *src)
{
    size_t n = 0u;
    if (!src) {
        return 0u;
    }
    while (src[n] != '\0') {
        n++;
    }
    return n;
}

static robo_result_t copy_title_utf8(char *dst, size_t dst_sz, const char *src)
{
    size_t len;
    size_t i;

    if (!dst || dst_sz == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!src) {
        dst[0] = '\0';
        return ROBO_OK;
    }
    len = title_byte_len(src);
    if (!robo_unicode_utf8_validate(src, len)) {
        return ROBO_PARSE_ERROR;
    }
    if (len + 1u > dst_sz) {
        len = dst_sz - 1u;
        /* Truncate only on complete code points: walk back if needed. */
        while (len > 0u && ((unsigned char)src[len] & 0xC0u) == 0x80u) {
            len--;
        }
    }
    for (i = 0u; i < len; i++) {
        dst[i] = src[i];
    }
    dst[len] = '\0';
    return ROBO_OK;
}

robo_window_slot_t *robo_window_slot_find(robo_window_id_t id)
{
    size_t i;

    if (id == ROBO_WINDOW_ID_NONE) {
        return NULL;
    }
    for (i = 0u; i < ROBO_WINDOW_MAX_WINDOWS; i++) {
        if (g_slots[i].in_use && g_slots[i].id == id) {
            return &g_slots[i];
        }
    }
    return NULL;
}

static robo_window_slot_t *slot_find(robo_window_id_t id)
{
    return robo_window_slot_find(id);
}

static robo_window_slot_t *slot_alloc(void)
{
    size_t i;

    for (i = 0u; i < ROBO_WINDOW_MAX_WINDOWS; i++) {
        if (!g_slots[i].in_use) {
            memset(&g_slots[i], 0, sizeof(g_slots[i]));
            g_slots[i].in_use = 1;
            return &g_slots[i];
        }
    }
    return NULL;
}

static void emit_simple(robo_window_event_type_t type, robo_window_id_t id)
{
    robo_window_event_t ev;

    robo_window_event_init(&ev);
    ev.type = type;
    ev.window_id = id;
    (void)robo_window_event_push(&ev);
}

static void emit_with_state(robo_window_event_type_t type,
                            const robo_window_slot_t *slot)
{
    robo_window_event_t ev;

    robo_window_event_init(&ev);
    ev.type = type;
    ev.window_id = slot->id;
    ev.x = slot->state.x;
    ev.y = slot->state.y;
    ev.width = slot->state.width;
    ev.height = slot->state.height;
    ev.scale = slot->state.scale;
    ev.display_id = slot->state.display_id;
    (void)robo_window_event_push(&ev);
}

static int backend_reports_actual(void)
{
    robo_window_backend_caps_t caps;

    if (robo_window_backend_capabilities(&caps) != ROBO_OK) {
        return 0;
    }
    return caps.reports_actual_state != 0;
}

static void sync_state_from_backend(robo_window_slot_t *slot)
{
    const robo_window_backend_vtbl_t *vtbl = robo_window_backend_vtbl();
    int32_t min_w, min_h, max_w, max_h;
    float prev_scale;
    int lock_scale;

    if (!vtbl || !vtbl->query_state || !backend_reports_actual()) {
        return;
    }
    /* Preserve constraints — backends report geometry, not constraint fields. */
    min_w = slot->state.min_width;
    min_h = slot->state.min_height;
    max_w = slot->state.max_width;
    max_h = slot->state.max_height;
    prev_scale = slot->state.scale;
    lock_scale = slot->scale_from_preference;
    (void)vtbl->query_state(robo_window_backend_userdata(), slot->id,
                            &slot->native, &slot->state);
    slot->state.min_width = min_w;
    slot->state.min_height = min_h;
    slot->state.max_width = max_w;
    slot->state.max_height = max_h;
    if (lock_scale) {
        slot->state.scale = prev_scale;
    }
}

static void apply_constraints_to_state(robo_window_slot_t *slot,
                                       const robo_window_size_constraints_t *c)
{
    slot->state.min_width = c->min_width;
    slot->state.min_height = c->min_height;
    slot->state.max_width = c->max_width;
    slot->state.max_height = c->max_height;
    slot->config_snapshot.min_width = c->min_width;
    slot->config_snapshot.min_height = c->min_height;
    slot->config_snapshot.max_width = c->max_width;
    slot->config_snapshot.max_height = c->max_height;
}

static void constraints_from_slot(const robo_window_slot_t *slot,
                                  robo_window_size_constraints_t *out)
{
    out->min_width = slot->config_snapshot.min_width;
    out->min_height = slot->config_snapshot.min_height;
    out->max_width = slot->config_snapshot.max_width;
    out->max_height = slot->config_snapshot.max_height;
}

/** Nearest valid size inside constraints (deterministic corrective resize). */
static void clamp_size_to_constraints(int32_t *w,
                                      int32_t *h,
                                      const robo_window_size_constraints_t *c)
{
    if (c->min_width > 0 && *w < c->min_width) {
        *w = c->min_width;
    }
    if (c->min_height > 0 && *h < c->min_height) {
        *h = c->min_height;
    }
    if (c->max_width > 0 && *w > c->max_width) {
        *w = c->max_width;
    }
    if (c->max_height > 0 && *h > c->max_height) {
        *h = c->max_height;
    }
}

static void destroy_all_windows(void)
{
    size_t i;
    const robo_window_backend_vtbl_t *vtbl = robo_window_backend_vtbl();
    void *ud = robo_window_backend_userdata();
    size_t n = 0u;

    for (i = 0u; i < ROBO_WINDOW_MAX_WINDOWS; i++) {
        if (!g_slots[i].in_use) {
            continue;
        }
        n++;
        if (vtbl && vtbl->destroy_window) {
            (void)vtbl->destroy_window(ud, g_slots[i].id, &g_slots[i].native);
        }
        robo_window_native_handle_invalidate(&g_slots[i].native);
        g_slots[i].present_surface = NULL;
        robo_window_render_teardown(&g_slots[i]);
        g_slots[i].state.visible = 0;
        g_slots[i].state.focused = 0;
        g_slots[i].state.lifecycle = ROBO_WINDOW_LIFECYCLE_CLOSED;
        g_slots[i].in_use = 0;
    }
    if (n > 0u) {
        robo_log_warn("robo_window: shutdown destroyed %llu live window(s)",
                      (unsigned long long)n);
    }
}

/** CREATED only — mutating ops. CLOSING → INVALID_ARGUMENT; missing → NOT_FOUND. */
robo_result_t robo_window_require_mutable_slot(robo_window_id_t id,
                                               robo_window_slot_t **out)
{
    robo_window_slot_t *slot = slot_find(id);
    if (!slot) {
        return ROBO_NOT_FOUND;
    }
    if (!robo_window_lifecycle_allows_mutation(slot->state.lifecycle)) {
        robo_log_debug("robo_window: mutation denied id=%llu life=%s",
                       (unsigned long long)id,
                       robo_window_lifecycle_name(slot->state.lifecycle));
        return ROBO_INVALID_ARGUMENT;
    }
    *out = slot;
    return ROBO_OK;
}

static robo_result_t require_mutable_slot(robo_window_id_t id,
                                          robo_window_slot_t **out)
{
    return robo_window_require_mutable_slot(id, out);
}

robo_result_t robo_window_init(void)
{
    if (g_initialized) {
        return ROBO_OK;
    }
    memset(g_slots, 0, sizeof(g_slots));
    g_next_id = 1u;
    g_geometry_apply_depth = 0;
    robo_window_event_queue_reset();
    robo_window_event_loop_reset();
    robo_keyboard_reset();
    robo_mouse_reset();
    robo_window_display_setup_placeholders();
    robo_window_thread_reset();
    g_initialized = 1;
    robo_log_info("robo_window: init (version %s)", ROBO_WINDOW_VERSION_STRING);
    return ROBO_OK;
}

void robo_window_shutdown(void)
{
    if (!g_initialized) {
        return;
    }
    (void)robo_window_event_loop_request_stop();
    destroy_all_windows();
    robo_window_backend_clear();
    robo_keyboard_reset();
    robo_mouse_reset();
    robo_window_event_queue_reset();
    robo_window_event_loop_reset();
    robo_window_display_reset_placeholders();
    robo_window_thread_reset();
    g_geometry_apply_depth = 0;
    g_initialized = 0;
    robo_log_info("robo_window: shutdown");
}

robo_result_t robo_window_create(const robo_window_config_t *cfg,
                                 robo_window_id_t *out_id)
{
    robo_result_t r;
    robo_window_slot_t *slot;
    const robo_window_backend_vtbl_t *vtbl;
    void *ud;
    robo_display_info_t disp;
    robo_window_backend_caps_t caps;

    if (!cfg || !out_id) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_id = ROBO_WINDOW_ID_NONE;
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_config_validate(cfg);
    if (r != ROBO_OK) {
        return r;
    }
    if (!robo_window_backend_is_registered()) {
        return ROBO_NOT_SUPPORTED;
    }
    if (robo_window_backend_capabilities(&caps) != ROBO_OK ||
        !caps.create_window) {
        return ROBO_NOT_SUPPORTED;
    }
    vtbl = robo_window_backend_vtbl();
    ud = robo_window_backend_userdata();
    slot = slot_alloc();
    if (!slot) {
        return ROBO_BUSY;
    }

    slot->id = g_next_id++;
    if (slot->id == ROBO_WINDOW_ID_NONE) {
        slot->id = g_next_id++;
    }
    r = copy_title_utf8(slot->title, sizeof(slot->title), cfg->title);
    if (r != ROBO_OK) {
        slot->in_use = 0;
        return r;
    }
    slot->config_snapshot = *cfg;
    slot->config_snapshot.title = slot->title;

    robo_window_state_init(&slot->state);
    r = robo_window_lifecycle_check_transition(ROBO_WINDOW_LIFECYCLE_UNINITIALIZED,
                                               ROBO_WINDOW_LIFECYCLE_CREATED);
    if (r != ROBO_OK) {
        slot->in_use = 0;
        return r;
    }
    slot->state.lifecycle = ROBO_WINDOW_LIFECYCLE_CREATED;
    slot->state.width = cfg->width;
    slot->state.height = cfg->height;
    slot->state.requested_width = cfg->width;
    slot->state.requested_height = cfg->height;
    slot->state.min_width = cfg->min_width;
    slot->state.min_height = cfg->min_height;
    slot->state.max_width = cfg->max_width;
    slot->state.max_height = cfg->max_height;
    slot->state.visible = 0;
    slot->state.focused = 0;
    slot->state.fullscreen = cfg->fullscreen ? 1 : 0;
    slot->scale_from_preference = 0;
    if (cfg->scale_preference != 0.0f &&
        robo_display_scale_validate(cfg->scale_preference) == ROBO_OK) {
        slot->state.scale = cfg->scale_preference;
        slot->scale_from_preference = 1;
    } else {
        slot->state.scale = 0.0f; /* resolve from display below */
    }
    if (cfg->x != ROBO_WINDOW_COORD_UNKNOWN) {
        slot->state.x = cfg->x;
        slot->state.requested_x = cfg->x;
    }
    if (cfg->y != ROBO_WINDOW_COORD_UNKNOWN) {
        slot->state.y = cfg->y;
        slot->state.requested_y = cfg->y;
    }
    if (cfg->display_id != ROBO_DISPLAY_ID_NONE) {
        slot->state.display_id = cfg->display_id;
    } else if (robo_display_primary(&disp) == ROBO_OK) {
        slot->state.display_id = disp.id;
    }
    if (!slot->scale_from_preference &&
        slot->state.display_id != ROBO_DISPLAY_ID_NONE) {
        float ds = 0.0f;
        if (robo_display_get_scale(slot->state.display_id, &ds) == ROBO_OK) {
            slot->state.scale = ds;
        } else {
            slot->state.scale = 1.0f; /* architecture default when unknown */
        }
    } else if (!slot->scale_from_preference) {
        slot->state.scale = 1.0f;
    }

    robo_window_render_init_slot(slot);

    robo_window_native_handle_clear(&slot->native);
    r = vtbl->create_window(ud, slot->id, &slot->config_snapshot, &slot->native);
    if (r != ROBO_OK) {
        robo_log_error("robo_window: backend create_window failed id=%llu (%d)",
                       (unsigned long long)slot->id, (int)r);
        slot->in_use = 0;
        return r;
    }

    sync_state_from_backend(slot);
    emit_with_state(ROBO_WINDOW_EVENT_CREATED, slot);
    robo_log_debug("robo_window: created id=%llu %dx%d backend=%s native=%s",
                   (unsigned long long)slot->id, (int)slot->state.width,
                   (int)slot->state.height, robo_window_backend_name(),
                   robo_window_native_handle_is_valid(&slot->native) ? "yes"
                                                                     : "empty");

    *out_id = slot->id;
    if (cfg->visible) {
        r = robo_window_show(slot->id);
        if (r != ROBO_OK) {
            return r;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_window_show(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (robo_window_backend_capabilities(&caps) != ROBO_OK || !caps.show_hide) {
        return ROBO_NOT_SUPPORTED;
    }
    vtbl = robo_window_backend_vtbl();
    if (!vtbl) {
        return ROBO_NOT_SUPPORTED;
    }
    g_geometry_apply_depth++;
    r = vtbl->show(robo_window_backend_userdata(), id, &slot->native);
    g_geometry_apply_depth--;
    if (r != ROBO_OK) {
        return r;
    }
    if (!slot->state.visible) {
        slot->state.visible = 1;
        sync_state_from_backend(slot);
        emit_simple(ROBO_WINDOW_EVENT_SHOWN, id);
        robo_log_debug("robo_window: shown id=%llu", (unsigned long long)id);
    }
    return ROBO_OK;
}

robo_result_t robo_window_hide(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (robo_window_backend_capabilities(&caps) != ROBO_OK || !caps.show_hide) {
        return ROBO_NOT_SUPPORTED;
    }
    vtbl = robo_window_backend_vtbl();
    if (!vtbl) {
        return ROBO_NOT_SUPPORTED;
    }
    g_geometry_apply_depth++;
    r = vtbl->hide(robo_window_backend_userdata(), id, &slot->native);
    g_geometry_apply_depth--;
    if (r != ROBO_OK) {
        return r;
    }
    if (slot->state.visible) {
        slot->state.visible = 0;
        sync_state_from_backend(slot);
        emit_simple(ROBO_WINDOW_EVENT_HIDDEN, id);
        robo_log_debug("robo_window: hidden id=%llu", (unsigned long long)id);
    }
    return ROBO_OK;
}

robo_result_t robo_window_request_close(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot) {
        return ROBO_NOT_FOUND;
    }
    if (slot->state.lifecycle == ROBO_WINDOW_LIFECYCLE_CLOSING) {
        robo_log_debug("robo_window: close already requested id=%llu",
                       (unsigned long long)id);
        return ROBO_ALREADY_EXISTS;
    }
    r = robo_window_lifecycle_check_transition(slot->state.lifecycle,
                                               ROBO_WINDOW_LIFECYCLE_CLOSING);
    if (r != ROBO_OK) {
        return r;
    }
    slot->state.lifecycle = ROBO_WINDOW_LIFECYCLE_CLOSING;
    emit_simple(ROBO_WINDOW_EVENT_CLOSE_REQUESTED, id);
    robo_log_debug("robo_window: close requested id=%llu",
                   (unsigned long long)id);
    return ROBO_OK;
}

robo_result_t robo_window_observe_close_request(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot) {
        return ROBO_NOT_FOUND;
    }
    if (slot->state.lifecycle == ROBO_WINDOW_LIFECYCLE_CLOSING) {
        /* Native may deliver repeated close messages — idempotent. */
        return ROBO_OK;
    }
    return robo_window_request_close(id);
}

robo_result_t robo_window_destroy(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    const robo_window_backend_vtbl_t *vtbl;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot) {
        return ROBO_NOT_FOUND;
    }
    r = robo_window_lifecycle_check_transition(slot->state.lifecycle,
                                               ROBO_WINDOW_LIFECYCLE_CLOSED);
    if (r != ROBO_OK) {
        return r;
    }
    vtbl = robo_window_backend_vtbl();
    if (vtbl && vtbl->destroy_window) {
        r = vtbl->destroy_window(robo_window_backend_userdata(), id,
                                 &slot->native);
        if (r != ROBO_OK) {
            robo_log_error("robo_window: destroy_window failed id=%llu (%d)",
                           (unsigned long long)id, (int)r);
            return r;
        }
    }
    robo_window_native_handle_invalidate(&slot->native);
    slot->state.lifecycle = ROBO_WINDOW_LIFECYCLE_CLOSED;
    slot->state.visible = 0;
    slot->state.focused = 0;
    slot->present_surface = NULL;
    robo_window_render_teardown(slot);
    /* Drop pending input/lifecycle events so a later window cannot see them. */
    robo_window_event_purge_window(id);
    emit_simple(ROBO_WINDOW_EVENT_CLOSED, id);
    slot->in_use = 0;
    robo_keyboard_reset_pressed_state();
    robo_mouse_reset_pressed_state();
    robo_log_debug("robo_window: destroyed id=%llu", (unsigned long long)id);
    return ROBO_OK;
}

robo_result_t robo_window_get_lifecycle(robo_window_id_t id,
                                        robo_window_lifecycle_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    *out = slot->state.lifecycle;
    return ROBO_OK;
}

int robo_window_is_closing(robo_window_id_t id)
{
    robo_window_slot_t *slot = slot_find(id);
    return slot != NULL &&
           slot->state.lifecycle == ROBO_WINDOW_LIFECYCLE_CLOSING;
}

robo_result_t robo_window_get_state(robo_window_id_t id, robo_window_state_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot) {
        return ROBO_NOT_FOUND;
    }
    sync_state_from_backend(slot);
    *out = slot->state;
    return ROBO_OK;
}

robo_result_t robo_window_get_associated_display(robo_window_id_t window_id,
                                                 robo_display_info_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(window_id);
    if (!slot) {
        return ROBO_NOT_FOUND;
    }
    if (slot->state.display_id == ROBO_DISPLAY_ID_NONE) {
        return ROBO_NOT_FOUND;
    }
    return robo_display_get(slot->state.display_id, out);
}

robo_result_t robo_window_set_associated_display(robo_window_id_t window_id,
                                                 robo_display_id_t display_id)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(window_id);
    if (!slot || !robo_window_id_is_live(window_id)) {
        return ROBO_NOT_FOUND;
    }
    if (display_id == ROBO_DISPLAY_ID_NONE) {
        slot->state.display_id = ROBO_DISPLAY_ID_NONE;
        return ROBO_OK;
    }
    if (!robo_display_id_is_known(display_id)) {
        return ROBO_NOT_FOUND;
    }
    slot->state.display_id = display_id;
    if (!slot->scale_from_preference) {
        float ds = 0.0f;
        if (robo_display_get_scale(display_id, &ds) == ROBO_OK) {
            slot->state.scale = ds;
            robo_window_render_mark_stale(slot);
            robo_log_debug("robo_window: scale from display id=%llu scale=%g",
                           (unsigned long long)window_id, (double)ds);
        }
    }
    return ROBO_OK;
}

robo_result_t robo_window_get_scale(robo_window_id_t id, float *out_scale)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out_scale) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    sync_state_from_backend(slot);
    if (robo_display_scale_is_known(slot->state.scale)) {
        *out_scale = slot->state.scale;
        return ROBO_OK;
    }
    if (slot->state.display_id != ROBO_DISPLAY_ID_NONE) {
        return robo_display_get_scale(slot->state.display_id, out_scale);
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_window_get_dpi(robo_window_id_t id, robo_display_dpi_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    if (slot->state.display_id == ROBO_DISPLAY_ID_NONE) {
        return ROBO_NOT_FOUND;
    }
    return robo_display_get_dpi(slot->state.display_id, out);
}

robo_result_t robo_window_get_size_physical(robo_window_id_t id,
                                            robo_gfx_size_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    float scale;
    int32_t pw;
    int32_t ph;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    sync_state_from_backend(slot);
    r = robo_window_get_scale(id, &scale);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_display_size_logical_to_physical(slot->state.width,
                                              slot->state.height, scale, &pw,
                                              &ph);
    if (r != ROBO_OK) {
        return r;
    }
    out->width = pw;
    out->height = ph;
    return ROBO_OK;
}

robo_result_t robo_window_get_title(robo_window_id_t id, const char **out_title)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out_title) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot) {
        return ROBO_NOT_FOUND;
    }
    *out_title = slot->title;
    return ROBO_OK;
}

robo_result_t robo_window_set_title(robo_window_id_t id, const char *utf8_title)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;
    char tmp[ROBO_WINDOW_TITLE_MAX];

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    r = copy_title_utf8(tmp, sizeof(tmp), utf8_title);
    if (r != ROBO_OK) {
        return r;
    }
    if (robo_window_backend_capabilities(&caps) != ROBO_OK || !caps.set_title) {
        return ROBO_NOT_SUPPORTED;
    }
    vtbl = robo_window_backend_vtbl();
    if (!vtbl || !vtbl->set_title) {
        return ROBO_NOT_SUPPORTED;
    }
    r = vtbl->set_title(robo_window_backend_userdata(), id, &slot->native, tmp);
    if (r != ROBO_OK) {
        return r;
    }
    memcpy(slot->title, tmp, sizeof(slot->title));
    slot->config_snapshot.title = slot->title;
    return ROBO_OK;
}

robo_result_t robo_window_set_size(robo_window_id_t id,
                                   int32_t width,
                                   int32_t height)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;
    robo_window_size_constraints_t cons;
    int32_t prev_req_w;
    int32_t prev_req_h;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    constraints_from_slot(slot, &cons);
    r = robo_window_size_validate_against_constraints(width, height, &cons);
    if (r != ROBO_OK) {
        return r;
    }
    if (robo_window_backend_capabilities(&caps) != ROBO_OK || !caps.resize) {
        return ROBO_NOT_SUPPORTED;
    }
    vtbl = robo_window_backend_vtbl();
    if (!vtbl || !vtbl->set_size) {
        return ROBO_NOT_SUPPORTED;
    }
    prev_req_w = slot->state.requested_width;
    prev_req_h = slot->state.requested_height;
    slot->state.requested_width = width;
    slot->state.requested_height = height;
    g_geometry_apply_depth++;
    r = vtbl->set_size(robo_window_backend_userdata(), id, &slot->native, width,
                       height);
    g_geometry_apply_depth--;
    if (r != ROBO_OK) {
        slot->state.requested_width = prev_req_w;
        slot->state.requested_height = prev_req_h;
        robo_log_debug("robo_window: set_size failed id=%llu (%d); requested unchanged",
                       (unsigned long long)id, (int)r);
        return r;
    }
    if (backend_reports_actual()) {
        sync_state_from_backend(slot);
    } else {
        slot->state.width = width;
        slot->state.height = height;
    }
    emit_with_state(ROBO_WINDOW_EVENT_RESIZED, slot);
    robo_window_render_mark_stale(slot);
    robo_log_debug("robo_window: size id=%llu req=%dx%d actual=%dx%d",
                   (unsigned long long)id, (int)slot->state.requested_width,
                   (int)slot->state.requested_height, (int)slot->state.width,
                   (int)slot->state.height);
    return ROBO_OK;
}

robo_result_t robo_window_request_size(robo_window_id_t id,
                                       int32_t width,
                                       int32_t height)
{
    return robo_window_set_size(id, width, height);
}

robo_result_t robo_window_get_size(robo_window_id_t id, robo_gfx_size_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    sync_state_from_backend(slot);
    out->width = slot->state.width;
    out->height = slot->state.height;
    return ROBO_OK;
}

robo_result_t robo_window_get_requested_size(robo_window_id_t id,
                                             robo_gfx_size_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    out->width = slot->state.requested_width;
    out->height = slot->state.requested_height;
    return ROBO_OK;
}

robo_result_t robo_window_get_size_constraints(
    robo_window_id_t id,
    robo_window_size_constraints_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    constraints_from_slot(slot, out);
    return ROBO_OK;
}

robo_result_t robo_window_set_size_constraints(
    robo_window_id_t id,
    const robo_window_size_constraints_t *constraints)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;
    robo_window_size_constraints_t c;
    int32_t w;
    int32_t h;
    int need_corrective;

    if (!constraints) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    c = *constraints;
    r = robo_window_size_constraints_validate(&c);
    if (r != ROBO_OK) {
        return r;
    }

    vtbl = robo_window_backend_vtbl();
    if (robo_window_backend_capabilities(&caps) == ROBO_OK &&
        caps.size_constraints) {
        if (!vtbl || !vtbl->set_size_constraints) {
            return ROBO_NOT_SUPPORTED;
        }
        r = vtbl->set_size_constraints(robo_window_backend_userdata(), id,
                                       &slot->native, c.min_width, c.min_height,
                                       c.max_width, c.max_height);
        if (r != ROBO_OK) {
            return r;
        }
    } else if (vtbl && vtbl->set_size_constraints) {
        r = vtbl->set_size_constraints(robo_window_backend_userdata(), id,
                                       &slot->native, c.min_width, c.min_height,
                                       c.max_width, c.max_height);
        if (r != ROBO_OK && r != ROBO_NOT_SUPPORTED) {
            return r;
        }
    }

    apply_constraints_to_state(slot, &c);
    robo_log_debug("robo_window: constraints id=%llu min=%dx%d max=%dx%d",
                   (unsigned long long)id, (int)c.min_width, (int)c.min_height,
                   (int)c.max_width, (int)c.max_height);

    sync_state_from_backend(slot);
    w = slot->state.width;
    h = slot->state.height;
    need_corrective = 0;
    if (c.min_width > 0 && w < c.min_width) {
        need_corrective = 1;
    }
    if (c.min_height > 0 && h < c.min_height) {
        need_corrective = 1;
    }
    if (c.max_width > 0 && w > c.max_width) {
        need_corrective = 1;
    }
    if (c.max_height > 0 && h > c.max_height) {
        need_corrective = 1;
    }
    if (need_corrective) {
        clamp_size_to_constraints(&w, &h, &c);
        r = robo_window_set_size(id, w, h);
        if (r != ROBO_OK) {
            return r;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_window_observe_size(robo_window_id_t id,
                                       int32_t width,
                                       int32_t height)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    int changed;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    if (g_geometry_apply_depth > 0) {
        /* Programmatic set_size owns state + RESIZED emit. */
        return ROBO_OK;
    }
    r = robo_window_size_validate(width, height);
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        robo_log_debug("robo_window: observe_size dropped id=%llu (not live)",
                       (unsigned long long)id);
        return ROBO_NOT_FOUND;
    }
    changed = (slot->state.width != width || slot->state.height != height);
    slot->state.width = width;
    slot->state.height = height;
    if (changed) {
        emit_with_state(ROBO_WINDOW_EVENT_RESIZED, slot);
        robo_window_render_mark_stale(slot);
        robo_log_debug("robo_window: observe_size id=%llu actual=%dx%d",
                       (unsigned long long)id, (int)width, (int)height);
    }
    return ROBO_OK;
}

robo_result_t robo_window_observe_position(robo_window_id_t id,
                                           int32_t x,
                                           int32_t y)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    int changed;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    if (g_geometry_apply_depth > 0) {
        return ROBO_OK;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        robo_log_debug("robo_window: observe_position dropped id=%llu",
                       (unsigned long long)id);
        return ROBO_NOT_FOUND;
    }
    changed = (slot->state.x != x || slot->state.y != y);
    slot->state.x = x;
    slot->state.y = y;
    if (changed) {
        emit_with_state(ROBO_WINDOW_EVENT_MOVED, slot);
        robo_log_debug("robo_window: observe_position id=%llu (%d,%d)",
                       (unsigned long long)id, (int)x, (int)y);
    }
    return ROBO_OK;
}

robo_result_t robo_window_observe_visibility(robo_window_id_t id, int visible)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    int want = visible ? 1 : 0;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    if (g_geometry_apply_depth > 0) {
        return ROBO_OK;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    if (slot->state.visible == want) {
        return ROBO_OK;
    }
    slot->state.visible = want;
    if (want) {
        emit_simple(ROBO_WINDOW_EVENT_SHOWN, id);
        robo_log_debug("robo_window: observe_visibility shown id=%llu",
                       (unsigned long long)id);
    } else {
        emit_simple(ROBO_WINDOW_EVENT_HIDDEN, id);
        robo_log_debug("robo_window: observe_visibility hidden id=%llu",
                       (unsigned long long)id);
    }
    return ROBO_OK;
}

robo_result_t robo_window_observe_focus(robo_window_id_t id, int focused)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    int want = focused ? 1 : 0;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    if (g_geometry_apply_depth > 0) {
        return ROBO_OK;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    if (slot->state.focused == want) {
        return ROBO_OK;
    }
    slot->state.focused = want;
    if (want) {
        emit_simple(ROBO_WINDOW_EVENT_FOCUS_GAINED, id);
        robo_log_debug("robo_window: observe_focus gained id=%llu",
                       (unsigned long long)id);
    } else {
        emit_simple(ROBO_WINDOW_EVENT_FOCUS_LOST, id);
        robo_keyboard_reset_pressed_state();
        robo_mouse_reset_pressed_state();
        robo_log_debug("robo_window: observe_focus lost id=%llu",
                       (unsigned long long)id);
    }
    return ROBO_OK;
}

robo_result_t robo_window_request_position(robo_window_id_t id,
                                           int32_t x,
                                           int32_t y)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (robo_window_backend_capabilities(&caps) != ROBO_OK || !caps.move) {
        return ROBO_NOT_SUPPORTED;
    }
    vtbl = robo_window_backend_vtbl();
    if (!vtbl || !vtbl->set_position) {
        return ROBO_NOT_SUPPORTED;
    }
    slot->state.requested_x = x;
    slot->state.requested_y = y;
    g_geometry_apply_depth++;
    r = vtbl->set_position(robo_window_backend_userdata(), id, &slot->native, x,
                           y);
    g_geometry_apply_depth--;
    if (r != ROBO_OK) {
        return r;
    }
    if (backend_reports_actual()) {
        sync_state_from_backend(slot);
    } else {
        slot->state.x = x;
        slot->state.y = y;
    }
    emit_with_state(ROBO_WINDOW_EVENT_MOVED, slot);
    return ROBO_OK;
}

robo_result_t robo_window_request_focus(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (robo_window_backend_capabilities(&caps) != ROBO_OK || !caps.focus) {
        return ROBO_NOT_SUPPORTED;
    }
    vtbl = robo_window_backend_vtbl();
    if (!vtbl || !vtbl->focus) {
        return ROBO_NOT_SUPPORTED;
    }
    g_geometry_apply_depth++;
    r = vtbl->focus(robo_window_backend_userdata(), id, &slot->native);
    g_geometry_apply_depth--;
    if (r != ROBO_OK) {
        return r;
    }
    if (!slot->state.focused) {
        slot->state.focused = 1;
        emit_simple(ROBO_WINDOW_EVENT_FOCUS_GAINED, id);
    }
    sync_state_from_backend(slot);
    return ROBO_OK;
}

robo_result_t robo_window_get_native_handle(robo_window_id_t id,
                                            robo_window_native_handle_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    *out = slot->native;
    return ROBO_OK;
}

int robo_window_id_is_live(robo_window_id_t id)
{
    robo_window_slot_t *slot = slot_find(id);

    return slot != NULL &&
           (slot->state.lifecycle == ROBO_WINDOW_LIFECYCLE_CREATED ||
            slot->state.lifecycle == ROBO_WINDOW_LIFECYCLE_CLOSING);
}

robo_window_id_t robo_window_find_focused_id(void)
{
    size_t i;

    for (i = 0u; i < ROBO_WINDOW_MAX_WINDOWS; i++) {
        if (g_slots[i].in_use && g_slots[i].state.focused &&
            (g_slots[i].state.lifecycle == ROBO_WINDOW_LIFECYCLE_CREATED ||
             g_slots[i].state.lifecycle == ROBO_WINDOW_LIFECYCLE_CLOSING)) {
            return g_slots[i].id;
        }
    }
    return ROBO_WINDOW_ID_NONE;
}

size_t robo_window_live_count(void)
{
    size_t i;
    size_t n = 0u;

    for (i = 0u; i < ROBO_WINDOW_MAX_WINDOWS; i++) {
        if (g_slots[i].in_use) {
            n++;
        }
    }
    return n;
}

robo_result_t robo_window_process_events(void)
{
    return robo_window_event_loop_poll();
}

robo_result_t robo_window_set_present_surface(robo_window_id_t id,
                                              robo_gfx_surface_t *surface)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    slot->present_surface = surface;
    return ROBO_OK;
}

robo_gfx_surface_t *robo_window_get_present_surface(robo_window_id_t id)
{
    robo_window_slot_t *slot = slot_find(id);

    return slot ? slot->present_surface : NULL;
}

robo_result_t robo_window_present(robo_window_id_t id,
                                  robo_gfx_surface_t *surface)
{
    robo_window_slot_t *slot;
    const robo_window_backend_vtbl_t *vtbl;
    robo_gfx_surface_t *target;
    robo_window_backend_caps_t caps;
    robo_result_t r;

    r = require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    target = surface ? surface : slot->present_surface;
    if (!target) {
        target = slot->render_surface;
    }
    if (!target) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_window_backend_capabilities(&caps) != ROBO_OK || !caps.present) {
        return ROBO_NOT_SUPPORTED;
    }
    r = robo_window_present_validate_surface(target, &caps);
    if (r != ROBO_OK) {
        robo_log_debug("robo_window: present validate failed id=%llu (%d)",
                       (unsigned long long)id, (int)r);
        return r;
    }
    vtbl = robo_window_backend_vtbl();
    if (!vtbl || !vtbl->present_surface) {
        return ROBO_NOT_SUPPORTED;
    }
    r = vtbl->present_surface(robo_window_backend_userdata(), id, &slot->native,
                              target);
    if (r == ROBO_OK) {
        robo_log_debug(
            "robo_window: presented id=%llu %ux%u fmt=%d (backend accepted)",
            (unsigned long long)id,
            (unsigned)robo_gfx_surface_width(target),
            (unsigned)robo_gfx_surface_height(target),
            (int)robo_gfx_surface_format(target));
    } else {
        robo_log_debug("robo_window: present backend failed id=%llu (%d)",
                       (unsigned long long)id, (int)r);
    }
    return r;
}
