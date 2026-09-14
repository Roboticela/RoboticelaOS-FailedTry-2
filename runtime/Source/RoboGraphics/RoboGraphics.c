#include "RoboGraphics/RoboGraphics.h"

#include "RoboBase/RoboError.h"

const char *robo_graphics_version_string(void)
{
    return ROBO_GRAPHICS_VERSION_STRING;
}

robo_result_t robo_graphics_init(void)
{
    return ROBO_OK;
}

void robo_graphics_shutdown(void)
{
}
