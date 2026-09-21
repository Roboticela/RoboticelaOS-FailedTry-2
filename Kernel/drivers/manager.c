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
#include "drivers/virtio_net.h"
#include "net/core/network.h"
#include "graphics/gfx.h"
#include "input/input.h"
#include "input/ps2.h"
#include "kio.h"

static unsigned gPci, gReg, gLoaded, gFailed, gSigned, gRejected;
static const RoboBootInfo *gInfo;

static int CmdHas(const RoboBootInfo *info, const char *key)
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

static void BindAll(void)
{
    unsigned i;
    for (i = 0; i < DeviceCount(); i++) {
        RoboDevice *d = DeviceAt(i);
        RoboDriver *drv;
        unsigned score = 0;
        if (!d || d->driver)
            continue;
        drv = DriverBest(d, &score);
        d->matchScore = score;
        if (!drv || score == 0) {
            d->status = RoboDevUnsupported;
            kprintf("Device Detected\n");
            kprintf("\n");
            if (d->busType == RoboBusPci && d->busData) {
                RoboPciDevice *p = (RoboPciDevice *)d->busData;
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
        if (DriverBind(d, drv) != 0) {
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

    DriverStats(&st);
    gPci = PciCount();
    gReg = st.registered;
    gLoaded = st.loaded;
    gFailed = st.failed;
    gSigned = st.signedOk;
    gRejected = st.rejected;

    for (i = 0; i < DeviceCount(); i++) {
        RoboDevice *d = DeviceAt(i);
        if (d && d->status == RoboDevUnsupported)
            unsup++;
    }

    kprintf("========================================\n");
    kprintf("        ROBOTICELA DRIVER SYSTEM\n");
    kprintf("========================================\n");
    kprintf("\n");
    kprintf("PCI\n");
    kprintf("----------------------------------------\n");
    kprintf("Devices Found : %u\n", gPci);
    kprintf("\n");
    kprintf("Driver Manager\n");
    kprintf("----------------------------------------\n");
    kprintf("Drivers Registered : %u\n", gReg);
    kprintf("Drivers Loaded     : %u\n", gLoaded);
    kprintf("Drivers Failed     : %u\n", gFailed);
    kprintf("Unsupported        : %u\n", unsup);
    kprintf("\n");
    kprintf("Signature System\n");
    kprintf("----------------------------------------\n");
    kprintf("Trust Root : %s\n", "PUBLIC KEYS ONLY");
    kprintf("Signed Drivers : %u\n", gSigned);
    kprintf("Rejected Drivers : %u\n", gRejected);
    kprintf("Test Mode : %s\n", DriverTestMode() ? "ON" : "OFF");
    kprintf("\n");
    kprintf("Storage\n");
    kprintf("----------------------------------------\n");
    kprintf("Device : VirtIO Block\n");
    kprintf("Status : %s\n", VirtioBlkBdev() ? "READY" : "NOT READY");
    kprintf("\n");
    kprintf("Network\n");
    kprintf("----------------------------------------\n");
    kprintf("Device : VirtIO Net\n");
    kprintf("Status : %s\n", VirtioNetReady() ? "READY" : "NOT READY");
    kprintf("\n");
    kprintf("Graphics\n");
    kprintf("----------------------------------------\n");
    kprintf("Device : UEFI Framebuffer\n");
    kprintf("Status : %s\n", GfxReady() ? "READY" : "NOT READY");
    if (DisplayGetInfo(&di))
        kprintf("Resolution : %u x %u\n", di.width, di.height);
    kprintf("\n");
    kprintf("Input\n");
    kprintf("----------------------------------------\n");
    kprintf("Keyboard : %s\n", InputKeyboardReady() ? "READY" : "NOT READY");
    kprintf("Mouse    : %s\n", InputMouseReady() ? "READY" : "NOT READY");
    kprintf("\n");
    kprintf("========================================\n");
    kprintf("[ OK ] Driver subsystem initialized\n");
    kprintf("========================================\n");
}

static void FbDebug(const RoboBootInfo *info)
{
    if (!KbootDebug(info))
        return;
    KfbPuts("Driver system\n", 0xA1A1A6u);
    KfbPuts("PCI ", 0x86868Bu);
    KfbPutu(gPci, 0xFFFFFFFFu);
    KfbPuts("\n", 0xA1A1A6u);
}

int HwSubsystemInit(const RoboBootInfo *info)
{
    unsigned i;
    RoboDevice *d;

    gInfo = info;
    if (CmdHas(info, "driver-test")) {
        DriverSetTestMode(1);
        kprintf("WARNING\n");
        kprintf("You are installing a TEST DRIVER.\n");
        kprintf("This driver is not trusted by the Roboticela production\n");
        kprintf("driver trust chain.\n");
    }

    AcpiInit(info);
    PciInit();
    DeviceInit();
    InputInit();
    GfxInit(info);

    NetInit();
    FbDriverRegister();
    VirtioBlkRegister();
    VirtioNetRegister();
    Ps2KbdRegister();
    Ps2MouseRegister();

    d = DeviceAdd(RoboBusPlatform, "UEFI Framebuffer", "display", 0);
    if (d)
        d->ident = RoboPlatFb;
    d = DeviceAdd(RoboBusPlatform, "PS/2 Keyboard", "keyboard", 0);
    if (d)
        d->ident = RoboPlatKbd;
    d = DeviceAdd(RoboBusPlatform, "PS/2 Mouse", "mouse", 0);
    if (d)
        d->ident = RoboPlatMouse;

    for (i = 0; i < PciCount(); i++) {
        RoboPciDevice *p = PciAt(i);
        if (!p)
            continue;
        d = DeviceAdd(RoboBusPci, PciClassName(p->classCode, p->subclass),
                            "pci", p);
        if (d)
            d->ident = ((uint32_t)p->vendor << 16) | p->device;
    }

    BindAll();
    NetBringup();
    PciReport();

    if (KbootSelftest(info)) {
        int cryptoOk, regOk, pciOk, gfxOk, inOk, stOk, pkgOk;

        cryptoOk = Sha256Selftest() && Sha512Selftest() &&
                    Ed25519Selftest();
        pkgOk = RdrvSelftest();
        regOk = DriverRegistrySelftest();
        pciOk = PciSelftest();
        gfxOk = GfxSelftest();
        inOk = InputSelftest();
        stOk = StorageSelftest();

        kprintf("PCI_TEST  %s\n", pciOk ? "ok" : "fail");
        kprintf("CRYPTO_TEST  %s\n", cryptoOk ? "ok" : "fail");
        kprintf("RDRV_TEST  %s\n", pkgOk ? "ok" : "fail");
        kprintf("REGISTRY_TEST  %s\n", regOk ? "ok" : "fail");

        report();
        FbDebug(info);

        if (pciOk && cryptoOk && pkgOk && regOk && gfxOk && inOk && stOk)
            kprintf("[PASS] DRIVER SYSTEM\n");
        else
            kprintf("[FAIL] DRIVER SYSTEM\n");

        if (stOk)
            kprintf("[PASS] STORAGE DRIVER\n");
        else
            kprintf("[FAIL] STORAGE DRIVER\n");
    } else {
        report();
        FbDebug(info);
    }
    return 1;
}

void HwStatus(unsigned *pci, unsigned *reg, unsigned *loaded,
                    unsigned *failed, unsigned *signedOk, unsigned *rejected)
{
    if (pci)
        *pci = gPci;
    if (reg)
        *reg = gReg;
    if (loaded)
        *loaded = gLoaded;
    if (failed)
        *failed = gFailed;
    if (signedOk)
        *signedOk = gSigned;
    if (rejected)
        *rejected = gRejected;
}

int HwStorageReady(void)
{
    return VirtioBlkBdev() != 0;
}

int DriverManagerSelftest(void)
{
    return DriverRegistrySelftest();
}
