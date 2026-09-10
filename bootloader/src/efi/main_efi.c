#include "efi.h"
#include "roboboot.h"

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    rb_session ss;
    int w, h;
    const rb_platform *plat;

    if (!efi_platform_init(ImageHandle, SystemTable)) {
        efi_log("RoboBoot could not open the ESP");
        return EFI_LOAD_ERROR;
    }
    plat = efi_platform();
    rb_session_init(&ss, plat);
    ss.force_menu = efi_flag_menu();
    efi_fb_size(&w, &h);
    if (efi_flag_selftest()) {
        w = 640;
        h = 400;
    } else {
        if (w > 1280) w = 1280;
        if (h > 800) h = 800;
    }
    ss.fb.w = w;
    ss.fb.h = h;
    ss.fb.px = (uint32_t *)rb_calloc((size_t)w * (size_t)h * 4u);
    if (!ss.fb.px) {
        efi_log("RoboBoot framebuffer alloc failed");
        return EFI_LOAD_ERROR;
    }

    if (efi_flag_selftest()) {
        int ticks = 0;
        int splash_ms = efi_flag_splash_ms();
        ss.force_menu = 1;
        rb_session_tick(&ss);
        if (splash_ms >= 0) ss.catalog.splash_min_ms = splash_ms;
        while (ss.running && ticks++ < 2000) {
            if (ss.phase == RB_PHASE_PICKER || ss.phase == RB_PHASE_DONE || ss.phase == RB_PHASE_HANDOFF)
                break;
            if (plat->sleep_ms) plat->sleep_ms(8);
            rb_session_tick(&ss);
        }
        if (ss.catalog.ntargets > 0 && (ss.phase == RB_PHASE_PICKER || ss.phase == RB_PHASE_DONE ||
                                        ss.phase == RB_PHASE_HANDOFF || ss.splash_done)) {
            rb_logf("ROBOBOOT_QEMU_OK targets=%d phase=%d", ss.catalog.ntargets, (int)ss.phase);
        } else {
            rb_logf("ROBOBOOT_QEMU_FAIL targets=%d phase=%d running=%d",
                    ss.catalog.ntargets, (int)ss.phase, ss.running);
        }
        rb_session_free(&ss);
        efi_shutdown();
        return EFI_SUCCESS;
    }

    rb_session_boot(&ss);
    rb_session_free(&ss);
    efi_shutdown();
    return EFI_SUCCESS;
}
