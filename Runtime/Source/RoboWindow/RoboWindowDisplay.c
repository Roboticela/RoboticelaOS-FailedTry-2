#include "RoboWindow/RoboWindowDisplay.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboDisplay/RoboDisplay.h"

void WindowDisplayResetPlaceholders(void)
{
    DisplayShutdown();
}

void WindowDisplaySetupPlaceholders(void)
{
    Result r;

    if (!DisplayIsInitialized()) {
        r = DisplayInit();
        if (r != RoboOk) {
            LogError("robo_window: display init failed (%d)", (int)r);
            return;
        }
    }
    r = DisplayBackendUsePlaceholder();
    if (r != RoboOk) {
        LogError("robo_window: placeholder display backend failed (%d)",
                       (int)r);
        return;
    }
    r = DisplayRefresh();
    if (r != RoboOk) {
        LogError("robo_window: display refresh failed (%d)", (int)r);
        return;
    }
    LogDebug("robo_window: display via RoboDisplay placeholder");
}
