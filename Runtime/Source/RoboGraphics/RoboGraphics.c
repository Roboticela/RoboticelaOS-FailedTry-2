#include "RoboGraphics/RoboGraphics.h"

#include "RoboBase/RoboError.h"

const char *GraphicsVersionString(void)
{
    return RoboGraphicsVersionString;
}

Result GraphicsInit(void)
{
    return RoboOk;
}

void GraphicsShutdown(void)
{
}
