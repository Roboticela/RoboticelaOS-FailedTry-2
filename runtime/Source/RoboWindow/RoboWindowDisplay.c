#include "RoboWindow/RoboWindowDisplay.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboDisplay/RoboDisplay.h"

void robo_window_display_reset_placeholders(void)
{
    robo_display_shutdown();
}

void robo_window_display_setup_placeholders(void)
{
    robo_result_t r;

    if (!robo_display_is_initialized()) {
        r = robo_display_init();
        if (r != ROBO_OK) {
            robo_log_error("robo_window: display init failed (%d)", (int)r);
            return;
        }
    }
    r = robo_display_backend_use_placeholder();
    if (r != ROBO_OK) {
        robo_log_error("robo_window: placeholder display backend failed (%d)",
                       (int)r);
        return;
    }
    r = robo_display_refresh();
    if (r != ROBO_OK) {
        robo_log_error("robo_window: display refresh failed (%d)", (int)r);
        return;
    }
    robo_log_debug("robo_window: display via RoboDisplay placeholder");
}
