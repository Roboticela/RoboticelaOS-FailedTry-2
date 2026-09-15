#include "RoboWindow/RoboWindowEventLoop.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackend.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static int g_loop_running;
static int g_stop_requested;
static int g_pump_depth;
static int g_wake_pending;

void robo_window_event_loop_reset(void)
{
    g_loop_running = 0;
    g_stop_requested = 0;
    g_pump_depth = 0;
    g_wake_pending = 0;
}

int robo_window_event_loop_is_running(void)
{
    return g_loop_running;
}

int robo_window_event_loop_is_pumping(void)
{
    return g_pump_depth > 0;
}

robo_result_t robo_window_event_loop_start(void)
{
    robo_result_t r;

    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    r = robo_window_thread_require_owner();
    if (r != ROBO_OK) {
        return r;
    }
    g_stop_requested = 0;
    g_loop_running = 1;
    robo_log_debug("robo_window: event loop start");
    return ROBO_OK;
}

robo_result_t robo_window_event_loop_request_stop(void)
{
    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    g_stop_requested = 1;
    g_loop_running = 0;
    g_wake_pending = 1;
    (void)robo_window_event_loop_wakeup();
    robo_log_debug("robo_window: event loop stop requested");
    return ROBO_OK;
}

robo_result_t robo_window_event_loop_wakeup(void)
{
    const robo_window_backend_vtbl_t *vtbl;
    robo_result_t r = ROBO_OK;

    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    g_wake_pending = 1;
    vtbl = robo_window_backend_vtbl();
    if (vtbl && vtbl->wakeup) {
        r = vtbl->wakeup(robo_window_backend_userdata());
        if (r != ROBO_OK && r != ROBO_NOT_SUPPORTED) {
            robo_log_debug("robo_window: backend wakeup failed (%d)", (int)r);
            return r;
        }
    }
    return ROBO_OK;
}

static robo_result_t pump_native(void)
{
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;

    if (robo_window_backend_capabilities(&caps) != ROBO_OK ||
        !caps.process_events) {
        return ROBO_NOT_SUPPORTED;
    }
    vtbl = robo_window_backend_vtbl();
    if (!vtbl || !vtbl->process_events) {
        return ROBO_NOT_SUPPORTED;
    }
    return vtbl->process_events(robo_window_backend_userdata());
}

static robo_result_t wait_native(uint32_t timeout_ms)
{
    const robo_window_backend_vtbl_t *vtbl;
    robo_window_backend_caps_t caps;
    robo_result_t r;

    if (g_wake_pending || g_stop_requested) {
        g_wake_pending = 0;
        return ROBO_OK;
    }
    vtbl = robo_window_backend_vtbl();
    if (vtbl && vtbl->wait_events) {
        if (robo_window_backend_capabilities(&caps) == ROBO_OK &&
            caps.wait_events) {
            r = vtbl->wait_events(robo_window_backend_userdata(), timeout_ms);
            g_wake_pending = 0;
            return r;
        }
    }
    /* Fallback: non-blocking poll only (no Sleep in freestanding core). */
    (void)timeout_ms;
    g_wake_pending = 0;
    return ROBO_OK;
}

robo_result_t robo_window_event_loop_poll(void)
{
    robo_result_t r;

    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    r = robo_window_thread_require_owner();
    if (r != ROBO_OK) {
        return r;
    }
    if (g_pump_depth > 0) {
        robo_log_debug("robo_window: event loop poll reentrant → BUSY");
        return ROBO_BUSY;
    }
    g_pump_depth++;
    r = pump_native();
    g_pump_depth--;
    return r;
}

robo_result_t robo_window_event_loop_run_once(uint32_t timeout_ms)
{
    robo_result_t r;

    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    r = robo_window_thread_require_owner();
    if (r != ROBO_OK) {
        return r;
    }
    if (g_pump_depth > 0) {
        return ROBO_BUSY;
    }
    if (g_stop_requested) {
        return ROBO_OK;
    }
    g_pump_depth++;
    if (timeout_ms != 0u) {
        r = wait_native(timeout_ms);
        if (r != ROBO_OK) {
            g_pump_depth--;
            return r;
        }
    }
    r = pump_native();
    g_pump_depth--;
    return r;
}

robo_result_t robo_window_event_loop_run(void)
{
    robo_result_t r;

    r = robo_window_event_loop_start();
    if (r != ROBO_OK) {
        return r;
    }
    while (g_loop_running && !g_stop_requested) {
        r = robo_window_event_loop_run_once(ROBO_WINDOW_EVENT_WAIT_FOREVER);
        if (r != ROBO_OK) {
            g_loop_running = 0;
            return r;
        }
    }
    g_loop_running = 0;
    return ROBO_OK;
}
