#include "RoboDisplay/RoboDisplayScale.h"
#include "RoboWindow/RoboWindowConfig.h"
#include "RoboWindow/RoboWindowSize.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <string.h>

void WindowConfigInit(WindowConfig *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->width = 800;
    cfg->height = 600;
    cfg->x = RoboWindowCoordUnknown;
    cfg->y = RoboWindowCoordUnknown;
    cfg->visible = 1;
    cfg->resizable = 1;
    cfg->fullscreen = 0;
    cfg->title = NULL;
    cfg->displayId = RoboDisplayIdNone;
    cfg->scalePreference = 0.0f;
}

Result WindowConfigValidate(const WindowConfig *cfg)
{
    WindowSizeConstraints c;
    Result r;

    if (!cfg) {
        return RoboInvalidArgument;
    }
    /* 0 = unspecified; otherwise must be a known valid scale. */
    if (cfg->scalePreference != 0.0f &&
        DisplayScaleValidate(cfg->scalePreference) != RoboOk) {
        return RoboInvalidArgument;
    }
    c.minWidth = cfg->minWidth;
    c.minHeight = cfg->minHeight;
    c.maxWidth = cfg->maxWidth;
    c.maxHeight = cfg->maxHeight;
    r = WindowSizeConstraintsValidate(&c);
    if (r != RoboOk) {
        return r;
    }
    return WindowSizeValidateAgainstConstraints(cfg->width, cfg->height,
                                                         &c);
}
