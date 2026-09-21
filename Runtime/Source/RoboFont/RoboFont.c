#include "RoboFont/RoboFont.h"

const char *FontVersionString(void)
{
    return RoboFontVersionString;
}

Result FontInit(void)
{
    return RoboOk;
}

void FontShutdown(void)
{
}
