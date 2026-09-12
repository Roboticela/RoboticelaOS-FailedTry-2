#include "drivers/manager.h"
#include "drivers/driver.h"
#include "drivers/package.h"
#include "drivers/signature/sha256.h"
#include "drivers/signature/sha512.h"
#include "drivers/signature/ed25519.h"
#include "drivers/builtin/fb_generic.h"
#include "device/device.h"
#include "pci/pci.h"
#include "arch/x86_64/acpi/acpi.h"
#include "storage/virtio_blk.h"
#include "graphics/gfx.h"
#include "input/input.h"
#include "input/ps2.h"
#include "kio.h"

static unsigned g_pci, g_reg, g_loaded, g_failed, g_signed, g_rejected;
static const RoboBootInfo *g_info;

static int cmd_has(const RoboBootInfo *info, const char *key)
{
    const char *p;
    unsigned i;
    if (!info || !key)
        return 0;
    p = info->cmdline;
    while (*p) {
        for (i = 0; key[i] && p[i] && p[i] == key[i]; i++) { }
        if (!key[i] && (p[i] == 0 || p[i] == ' ' || p[i] == '='))
            return 1;
        p++;
    }
    return 0;
}

static void bind_all(void)
{
    unsigned i;
    for (i = 0; i < robo_device_count(); i++) {
        RoboDevice *d = robo_device_at(i);
        RoboDriver *drv;
        unsigned score = 0;
        if (!d || d->driver)
            continue;
        drv = robo_driver_best(d, &score);
        d->match_score = score;
        if (!drv || score == 0) {
            d->status = ROBO_DEV_UNSUPPORTED;
            kprintf("Device Detected\n");
            kprintf("\n");
            if (d->bus_type == ROBO_BUS_PCI && d->bus_data) {
                RoboPciDevice *p = (RoboPciDevice *)d->bus_data;
                kprintf("PCI\n");
                kprintf("Vendor : %x\n", (unsigned)p->vendor);
                kprintf("Device : %x\n", (unsigned)p->device);
            } else {
                kprintf("Name   : %s\n", d->name);
            }
            kprintf("Driver:\n");
            kprintf("Not Installed\n");
            kprintf("\n");
            kprintf("Status:\n");
            kprintf("Unsupported\n");
            kprintf("\n");
            kprintf("Recommended:\n");
            kprintf("Open Driver Manager\n");
            kprintf("\n");
            continue;
        }
        if (robo_driver_bind(d, drv) != 0) {
            kprintf("[ERROR] [DRIVER] Initialization failed\n");
            kprintf("[ERROR] [DRIVER] Device: %s\n", d->name);
        }
    }
}

static void report(void)
{
    RoboDriverStats st;
    RoboDisplayInfo di;
    unsigned i, unsup = 0;

    robo_driver_stats(&st);
    g_pci = robo_pci_count();
    g_reg = st.registered;
    g_loaded = st.loaded;
    g_failed = st.failed;
    g_signed = st.signed_ok;
    g_rejected = st.rejected;

    for (i = 0; i < robo_device_count(); i++) {
        RoboDevice *d = robo_device_at(i);
        if (d && d->status == ROBO_DEV_UNSUPPORTED)
            unsup++;
    }

    kprintf("========================================\n");
    kprintf("        ROBOTICELA DRIVER SYSTEM\n");
    kprintf("========================================\n");
    kprintf("\n");
    kprintf("PCI\n");
    kprintf("----------------------------------------\n");
    kprintf("Devices Found : %u\n", g_pci);
    kprintf("\n");
    kprintf("Driver Manager\n");
    kprintf("----------------------------------------\n");
    kprintf("Drivers Registered : %u\n", g_reg);
    kprintf("Drivers Loaded     : %u\n", g_loaded);
    kprintf("Drivers Failed     : %u\n", g_failed);
    kprintf("Unsupported        : %u\n", unsup);
    kprintf("\n");
    kprintf("Signature System\n");
    kprintf("----------------------------------------\n");
    kprintf("Trust Root : %s\n", "PUBLIC KEYS ONLY");
    kprintf("Signed Drivers : %u\n", g_signed);
    kprintf("Rejected Drivers : %u\n", g_rejected);
    kprintf("Test Mode : %s\n", robo_driver_test_mode() ? "ON" : "OFF");
    kprintf("\n");
    kprintf("Storage\n");
    kprintf("----------------------------------------\n");
    kprintf("Device : VirtIO Block\n");
    kprintf("Status : %s\n", robo_virtio_blk_bdev() ? "READY" : "NOT READY");
    kprintf("\n");
    kprintf("Graphics\n");
    kprintf("----------------------------------------\n");
    kprintf("Device : UEFI Framebuffer\n");
    kprintf("Status : %s\n", robo_gfx_ready() ? "READY" : "NOT READY");
    if (robo_display_get_info(&di))
        kprintf("Resolution : %u x %u\n", di.width, di.height);
    kprintf("\n");
    kprintf("Input\n");
    kprintf("----------------------------------------\n");
    kprintf("Keyboard : %s\n", robo_input_keyboard_ready() ? "READY" : "NOT READY");
    kprintf("Mouse    : %s\n", robo_input_mouse_ready() ? "READY" : "NOT READY");
    kprintf("\n");
    kprintf("========================================\n");
    kprintf("[ OK ] Driver subsystem initialized\n");
    kprintf("========================================\n");
}

static void fb_debug(const RoboBootInfo *info)
{
    if (!kboot_debug(info))
        return;
    kfb_puts("Driver system\n", 0xA1A1A6u);
    kfb_puts("PCI ", 0x86868Bu);
    kfb_putu(g_pci, 0xFFFFFFFFu);
    kfb_puts("\n", 0xA1A1A6u);
}

int robo_hw_subsystem_init(const RoboBootInfo *info)
{
    unsigned i;
    RoboDevice *d;
    int crypto_ok, reg_ok, pci_ok, gfx_ok, in_ok, st_ok, pkg_ok;

    g_info = info;
    if (cmd_has(info, "driver-test")) {
        robo_driver_set_test_mode(1);
        kprintf("WARNING\n");
        kprintf("You are installing a TEST DRIVER.\n");
        kprintf("This driver is not trusted by the Roboticela production\n");
        kprintf("driver trust chain.\n");
    }

    robo_acpi_init(info);
    robo_pci_init();
    robo_device_init();
    robo_input_init();
    robo_gfx_init(info);

    robo_fb_driver_register();
    robo_virtio_blk_register();
    robo_ps2_kbd_register();
    robo_ps2_mouse_register();

    d = robo_device_add(ROBO_BUS_PLATFORM, "UEFI Framebuffer", "display", 0);
    if (d)
        d->ident = ROBO_PLAT_FB;
    d = robo_device_add(ROBO_BUS_PLATFORM, "PS/2 Keyboard", "keyboard", 0);
    if (d)
        d->ident = ROBO_PLAT_KBD;
    d = robo_device_add(ROBO_BUS_PLATFORM, "PS/2 Mouse", "mouse", 0);
    if (d)
        d->ident = ROBO_PLAT_MOUSE;

    for (i = 0; i < robo_pci_count(); i++) {
        RoboPciDevice *p = robo_pci_at(i);
        if (!p)
            continue;
        d = robo_device_add(ROBO_BUS_PCI, robo_pci_class_name(p->class_code, p->subclass),
                            "pci", p);
        if (d)
            d->ident = ((uint32_t)p->vendor << 16) | p->device;
    }

    bind_all();
    robo_pci_report();

    crypto_ok = robo_sha256_selftest() && robo_sha512_selftest() &&
                robo_ed25519_selftest();
    pkg_ok = robo_rdrv_selftest();
    reg_ok = robo_driver_registry_selftest();
    pci_ok = robo_pci_selftest();
    gfx_ok = robo_gfx_selftest();
    in_ok = robo_input_selftest();
    st_ok = robo_storage_selftest();

    kprintf("PCI_TEST  %s\n", pci_ok ? "ok" : "fail");
    kprintf("CRYPTO_TEST  %s\n", crypto_ok ? "ok" : "fail");
    kprintf("RDRV_TEST  %s\n", pkg_ok ? "ok" : "fail");
    kprintf("REGISTRY_TEST  %s\n", reg_ok ? "ok" : "fail");

    report();
    fb_debug(info);

    if (pci_ok && crypto_ok && pkg_ok && reg_ok && gfx_ok && in_ok && st_ok)
        kprintf("[PASS] DRIVER SYSTEM\n");
    else
        kprintf("[FAIL] DRIVER SYSTEM\n");

    (void)st_ok;
    if (st_ok)
        kprintf("[PASS] STORAGE DRIVER\n");
    else
        kprintf("[FAIL] STORAGE DRIVER\n");
    return 1;
}

void robo_hw_status(unsigned *pci, unsigned *reg, unsigned *loaded,
                    unsigned *failed, unsigned *signed_ok, unsigned *rejected)
{
    if (pci)
        *pci = g_pci;
    if (reg)
        *reg = g_reg;
    if (loaded)
        *loaded = g_loaded;
    if (failed)
        *failed = g_failed;
    if (signed_ok)
        *signed_ok = g_signed;
    if (rejected)
        *rejected = g_rejected;
}

int robo_hw_storage_ready(void)
{
    return robo_virtio_blk_bdev() != 0;
}

int robo_driver_manager_selftest(void)
{
    return robo_driver_registry_selftest();
}
