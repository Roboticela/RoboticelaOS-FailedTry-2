#include "RoboWindow/RoboWindowThread.h"
#include "RoboWindow/RoboWindow.h"

#include "RoboBase/RoboLog.h"

static robo_window_thread_id_t g_owner = ROBO_WINDOW_THREAD_NONE;
static robo_window_thread_id_t g_current = ROBO_WINDOW_THREAD_NONE;

void robo_window_thread_reset(void)
{
    g_owner = ROBO_WINDOW_THREAD_NONE;
    g_current = ROBO_WINDOW_THREAD_NONE;
}

robo_result_t robo_window_thread_bind_owner(robo_window_thread_id_t tid)
{
    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    g_owner = tid;
    if (tid == ROBO_WINDOW_THREAD_NONE) {
        robo_log_debug("robo_window: owner thread unbound");
    } else {
        robo_log_debug("robo_window: owner thread bound id=%llu",
                       (unsigned long long)tid);
    }
    return ROBO_OK;
}

robo_window_thread_id_t robo_window_thread_owner(void)
{
    return g_owner;
}

robo_result_t robo_window_thread_enter(robo_window_thread_id_t tid)
{
    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    g_current = tid;
    return ROBO_OK;
}

robo_window_thread_id_t robo_window_thread_current(void)
{
    return g_current;
}

robo_result_t robo_window_thread_require_owner(void)
{
    if (g_owner == ROBO_WINDOW_THREAD_NONE) {
        return ROBO_OK;
    }
    if (g_current != g_owner) {
        robo_log_warn("robo_window: wrong thread (owner=%llu current=%llu)",
                      (unsigned long long)g_owner,
                      (unsigned long long)g_current);
        return ROBO_PERMISSION_DENIED;
    }
    return ROBO_OK;
}
