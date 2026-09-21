#include "drivers/builtin/fb_generic.h"
#include "drivers/driver.h"
#include "graphics/gfx.h"
#include "kio.h"

static int FbProbe(RoboDevice *d)
{
    (void)d;
    return GfxReady() ? 0 : -1;
}

static int FbStart(RoboDevice *d)
{
    RoboDisplayInfo info;
    (void)d;
    if (!DisplayGetInfo(&info))
        return -1;
    kprintf("[INFO] [DRIVER] UEFI framebuffer initialized\n");
    kprintf("[INFO] [DRIVER] Resolution : %u x %u\n",
            info.width, info.height);
    return 0;
}

static int FbStop(RoboDevice *d)
{
    (void)d;
    return 0;
}

static const RoboDriverId fbIds[] = {
    { RoboBusPlatform, 0, RoboPlatFb, 0, 0, 0, 0, 0,
      RoboMatchBus | RoboMatchDevice }
};

static RoboDriver fbDrv = {
    "uefi-framebuffer", "Roboticela", 0x00010000u,
    RoboDriverAbiMajor, RoboDriverAbiMinor,
    RoboCapDisplay,
    RoboDrvChannelBuiltin, RoboDrvStable, RoboDrvDiscovered, 1,
    fbIds, 1, FbProbe, FbStart, FbStop, 0
};

int FbDriverRegister(void)
{
    return DriverRegister(&fbDrv);
}
