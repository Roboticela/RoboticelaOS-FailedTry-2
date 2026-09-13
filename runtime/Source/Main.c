/*
 * Roboticela Runtime — guest entry (Goal 1.4 RoboDebug).
 */

#include "RoboBase/RoboBase.h"
#include "RoboBase/RoboDebug.h"
#include "RoboBase/RoboLog.h"
#include "RoboticelaRuntime.h"

int main(void)
{
    robo_result_t st;

    st = robo_base_init();
    ROBO_ASSERT(st == ROBO_OK);

    robo_log_info("Roboticela Runtime %s", ROBOTICELA_RUNTIME_VERSION_STRING);
    robo_log_info("RoboBase %s (debug=%d)", robo_base_version(),
                  (int)robo_debug_enabled());
    robo_log_debug("Goal 1.4 RoboDebug active");

    st = robo_base_shutdown();
    ROBO_ASSERT(st == ROBO_OK);
    return 0;
}
