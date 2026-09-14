/*
 * Guest probe for the independent Roboticela Runtime.
 *
 * Reports version pins from runtime public headers. Does not depend on
 * Chromium engine trees. Full graphics/font libraries remain host-tested
 * via runtime/Tools/Build.ps1 until freestanding guest linking is ready.
 */

#include "RoboFont/RoboFontVersion.h"
#include "RoboGraphics/RoboGraphicsVersion.h"
#include "RoboI18N/RoboI18NVersion.h"
#include "RoboUnicode/RoboUnicodeVersion.h"
#include "RoboticelaRuntime.h"

#include <stdio.h>

int main(void)
{
    printf("Roboticela Runtime (guest)\n");
    printf("  runtime:   %s\n", ROBOTICELA_RUNTIME_VERSION_STRING);
    printf("  graphics:  %s\n", ROBO_GRAPHICS_VERSION_STRING);
    printf("  font:      %s\n", ROBO_FONT_VERSION_STRING);
    printf("  i18n:      %s\n", ROBO_I18N_VERSION_STRING);
    printf("  unicode:   %s\n", ROBO_UNICODE_VERSION_STRING);
    printf("  kind:      independent (no Chromium dependency)\n");
    return 0;
}
