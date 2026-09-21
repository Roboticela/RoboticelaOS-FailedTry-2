#include "efi.h"
#include "roboboot.h"

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    Session ss;
    int w, h;
    const Platform *plat;

    if (!EfiPlatformInit(ImageHandle, SystemTable)) {
        Bootlog(NULL, "ESP  open failed");
        EfiLog("RoboBoot could not open the ESP");
        return EFI_LOAD_ERROR;
    }
    plat = EfiPlatform();
    SessionInit(&ss, plat);
    ss.forceMenu = EfiFlagMenu();
    if (ss.forceMenu)
        Bootlog(&ss, "Menu  forced");
    EfiFbSize(&w, &h);
    Bootlogf(&ss, "Surface  request  %d x %d", w, h);
    if (EfiFlagSelftest()) {
        w = 640;
        h = 400;
    } else {
        if (w < 640) w = 640;
        if (h < 480) h = 480;
        if (w > 3840) w = 3840;
        if (h > 2160) h = 2160;
    }
    ss.fb.w = w;
    ss.fb.h = h;
    ss.fb.px = (uint32_t *)RoboRbCalloc((size_t)w * (size_t)h * 4u);
    if (!ss.fb.px && (w > 1280 || h > 800)) {
        w = 1280;
        h = 800;
        ss.fb.w = w;
        ss.fb.h = h;
        ss.fb.px = (uint32_t *)RoboRbCalloc((size_t)w * (size_t)h * 4u);
        Bootlogf(&ss, "Surface  fallback  %d x %d", w, h);
    }
    if (!ss.fb.px) {
        Bootlog(&ss, "Surface  alloc failed");
        EfiLog("RoboBoot framebuffer alloc failed");
        return EFI_LOAD_ERROR;
    }
    Bootlogf(&ss, "Surface  %d x %d  %zu bytes", w, h, (size_t)w * (size_t)h * 4u);
    GfxClear(&ss.fb, Token(RbTokenBgPrimary, 255));
    Bootlog(&ss, "Surface  cleared");
    if (!EfiFlagSelftest() && plat->present) {
        plat->present(&ss.fb);
        Bootlog(&ss, "Surface  first present");
    }

    if (EfiFlagSelftest()) {
        int ticks = 0;
        int splashMs = EfiFlagSplashMs();
        ss.forceMenu = 1;
        SessionTick(&ss);
        if (splashMs >= 0) ss.catalog.splashMinMs = splashMs;
        while (ss.running && ticks++ < 2000) {
            if (ss.phase == RbPhasePicker || ss.phase == RbPhaseDone || ss.phase == RbPhaseHandoff ||
                ss.phase == RbPhaseVerbose || ss.phase == RbPhaseKlog)
                break;
            if (plat->SleepMs) plat->SleepMs(8);
            SessionTick(&ss);
        }
        if (ss.catalog.ntargets > 0 && (ss.phase == RbPhasePicker || ss.phase == RbPhaseDone ||
                                        ss.phase == RbPhaseHandoff || ss.splashDone)) {
            Logf("ROBOBOOT_QEMU_OK targets=%d phase=%d", ss.catalog.ntargets, (int)ss.phase);
        } else {
            Logf("ROBOBOOT_QEMU_FAIL targets=%d phase=%d running=%d",
                    ss.catalog.ntargets, (int)ss.phase, ss.running);
        }
        SessionFree(&ss);
        EfiShutdown();
        return EFI_SUCCESS;
    }

    Bootlog(&ss, "Session  loop");
    SessionTick(&ss);
    if (EfiFlagSplashMs() >= 0)
        ss.catalog.splashMinMs = EfiFlagSplashMs();
    SessionBoot(&ss);
    Logf("RoboBoot session complete");
    for (;;) {
        if (gBS && gBS->Stall) gBS->Stall(1000000);
    }
}
