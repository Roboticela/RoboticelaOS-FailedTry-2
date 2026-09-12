#include "drivers/builtin/fb_generic.h"
#include "drivers/driver.h"
#include "graphics/gfx.h"
#include "kio.h"

static int fb_probe(RoboDevice *d)
{
    (void)d;
    return robo_gfx_ready() ? 0 : -1;
}

static int fb_start(RoboDevice *d)
{
    RoboDisplayInfo info;
    (void)d;
    if (!robo_display_get_info(&info))
        return -1;
    kprintf("[INFO] [DRIVER] UEFI framebuffer initialized\n");
    kprintf("[INFO] [DRIVER] Resolution : %u x %u\n",
            info.width, info.height);
    return 0;
}

static int fb_stop(RoboDevice *d)
{
    (void)d;
    return 0;
}

static const RoboDriverId fb_ids[] = {
    { ROBO_BUS_PLATFORM, 0, ROBO_PLAT_FB, 0, 0, 0, 0, 0,
      ROBO_MATCH_BUS | ROBO_MATCH_DEVICE }
};

static RoboDriver fb_drv = {
    "uefi-framebuffer", "Roboticela", 0x00010000u,
    ROBO_DRIVER_ABI_MAJOR, ROBO_DRIVER_ABI_MINOR,
    ROBO_CAP_DISPLAY,
    ROBO_DRV_CHANNEL_BUILTIN, ROBO_DRV_STABLE, ROBO_DRV_DISCOVERED, 1,
    fb_ids, 1, fb_probe, fb_start, fb_stop, 0
};

int robo_fb_driver_register(void)
{
    return robo_driver_register(&fb_drv);
}
