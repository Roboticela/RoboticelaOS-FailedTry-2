/*
 * Roboticela Runtime — guest entry (Goal 1.4 RoboDebug).
 */

#include "RoboBase/RoboBase.h"
#include "RoboBase/RoboDebug.h"
#include "RoboBase/RoboLog.h"
#include "RoboticelaRuntime.h"

int main(void)
{
    Result st;

    st = BaseInit();
    RoboAssert(st == RoboOk);

    LogInfo("Roboticela Runtime %s", ROBOTICELA_RUNTIME_VERSION_STRING);
    LogInfo("RoboBase %s (debug=%d)", BaseVersion(),
                  (int)RoboDebugEnabled());
    LogDebug("Goal 1.4 RoboDebug active");

    st = BaseShutdown();
    RoboAssert(st == RoboOk);
    return 0;
}
