#include "usb/xhci.h"
#include "usb/usb_core.h"
#include "pci/pci.h"
#include "arch/x86_64/mmio.h"
#include "mm/pmm.h"
#include "mm/page.h"
#include "kio.h"

#define XHCI_CAPLENGTH 0x00u
#define XHCI_HCSPARAMS1 0x04u
#define XHCI_HCSPARAMS2 0x08u
#define XHCI_HCCPARAMS1 0x10u

#define XHCI_USBCMD 0x00u
#define XHCI_USBSTS 0x04u
#define XHCI_CRCR   0x18u
#define XHCI_DCBAAP 0x30u
#define XHCI_CONFIG 0x38u

#define XHCI_CMD_RS   (1u << 0)
#define XHCI_CMD_HCRST (1u << 1)
#define XHCI_STS_HCH  (1u << 0)
#define XHCI_STS_CNR  (1u << 11)

#define XHCI_PORTSC_BASE 0x400u
#define XHCI_PORTSC_CCS  (1u << 0)
#define XHCI_PORTSC_PED  (1u << 1)
#define XHCI_PORTSC_PR   (1u << 4)
#define XHCI_PORTSC_PP   (1u << 9)
#define XHCI_PORTSC_CSC  (1u << 17)
#define XHCI_PORTSC_PEC  (1u << 18)
#define XHCI_PORTSC_WRC  (1u << 19)
#define XHCI_PORTSC_OCC  (1u << 20)
#define XHCI_PORTSC_PRC  (1u << 21)

#define XHCI_MAX_PORTS 16u
#define XHCI_MAX_CTRL  2u

struct XhciCtrl {
    RoboPciDevice *pci;
    volatile uint8_t *cap;
    volatile uint8_t *op;
    volatile uint8_t *rt;
    volatile uint8_t *db;
    uint8_t capLength;
    uint8_t maxSlots;
    uint8_t maxPorts;
    uint8_t connected;
    int ready;
};

static struct XhciCtrl gXhci[XHCI_MAX_CTRL];
static unsigned gXhciN;
static unsigned gPorts;
static unsigned gConnected;

static uint32_t XR32(volatile uint8_t *b, uint32_t off)
{
    return MmioR32(b + off);
}

static void XW32(volatile uint8_t *b, uint32_t off, uint32_t v)
{
    MmioW32(b + off, v);
}

static void DelaySpins(unsigned n)
{
    volatile unsigned i;
    for (i = 0; i < n; i++)
        __asm__ volatile ("pause");
}

static void *DmaPage(PhysAddr *pa)
{
    PhysAddr p = PageAlloc();
    uint8_t *v;
    unsigned i;
    if (p == RoboInvalidPhysAddr)
        return 0;
    v = (uint8_t *)(uintptr_t)p;
    for (i = 0; i < RoboPageSize; i++)
        v[i] = 0;
    if (pa)
        *pa = p;
    return v;
}

static int XhciReset(struct XhciCtrl *c)
{
    uint32_t cmd, sts;
    unsigned spins = 0;

    cmd = XR32(c->op, XHCI_USBCMD);
    cmd |= XHCI_CMD_HCRST;
    XW32(c->op, XHCI_USBCMD, cmd);
    while (spins++ < 2000000u) {
        cmd = XR32(c->op, XHCI_USBCMD);
        sts = XR32(c->op, XHCI_USBSTS);
        if (!(cmd & XHCI_CMD_HCRST) && !(sts & XHCI_STS_CNR))
            return 1;
        DelaySpins(100);
    }
    return 0;
}

static int XhciRun(struct XhciCtrl *c)
{
    uint32_t cmd;
    unsigned spins = 0;

    cmd = XR32(c->op, XHCI_USBCMD) | XHCI_CMD_RS;
    XW32(c->op, XHCI_USBCMD, cmd);
    while (spins++ < 1000000u) {
        if (!(XR32(c->op, XHCI_USBSTS) & XHCI_STS_HCH))
            return 1;
        DelaySpins(50);
    }
    return 0;
}

static void ScanPorts(struct XhciCtrl *c)
{
    unsigned i;
    c->connected = 0;
    for (i = 1; i <= c->maxPorts && i <= XHCI_MAX_PORTS; i++) {
        uint32_t sc = XR32(c->op, XHCI_PORTSC_BASE + (i - 1u) * 0x10u);
        if (sc & XHCI_PORTSC_CCS) {
            RoboUsbDevice udev;
            c->connected++;
            gConnected++;
            /* Clear change bits; power port. */
            sc |= XHCI_PORTSC_PP;
            sc |= XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC |
                  XHCI_PORTSC_OCC | XHCI_PORTSC_PRC;
            XW32(c->op, XHCI_PORTSC_BASE + (i - 1u) * 0x10u, sc);
            udev.slot = 0;
            udev.addr = 0;
            udev.speed = RoboUsbSpeedHigh;
            udev.port = (uint8_t)i;
            udev.classCode = 0;
            udev.subclass = 0;
            udev.protocol = 0;
            udev.config = 0;
            udev.vendor = 0;
            udev.product = 0;
            udev.epIn = 0;
            udev.epOut = 0;
            udev.maxPacket = 64;
            udev.hc = c;
            udev.classData = 0;
            udev.ready = 0;
            kinfo("[INFO] [USB] xHCI port %u connected\n", i);
            /*
             * Full TRB enumeration (slot enable, address device, GET_DESCRIPTOR)
             * lands with the transfer ring. Ports are visible so class drivers
             * and diagnostics can see hardware without a vendor USB package.
             */
            (void)udev;
        }
    }
}

static int XhciInitCtrl(struct XhciCtrl *c, RoboPciDevice *pci)
{
    uint32_t hcs1, hccp;
    uint8_t rtsoff, dboff;
    PhysAddr dcbaaPa;
    uint64_t *dcbaa;

    if (!(pci->bar[0].flags & RoboPciBarMem) || !pci->bar[0].addr)
        return 0;
    PciEnableMmio(pci);
    PciEnableBusmaster(pci);
    c->pci = pci;
    c->cap = (volatile uint8_t *)MmioMap((PhysAddr)pci->bar[0].addr,
                                             pci->bar[0].size);
    if (!c->cap)
        return 0;
    c->capLength = (uint8_t)(XR32(c->cap, XHCI_CAPLENGTH) & 0xFFu);
    c->op = c->cap + c->capLength;
    hcs1 = XR32(c->cap, XHCI_HCSPARAMS1);
    c->maxSlots = (uint8_t)(hcs1 & 0xFFu);
    c->maxPorts = (uint8_t)((hcs1 >> 24) & 0xFFu);
    if (!c->maxPorts)
        c->maxPorts = 1;
    hccp = XR32(c->cap, XHCI_HCCPARAMS1);
    rtsoff = (uint8_t)((XR32(c->cap, 0x18u) >> 0) & 0xFFu); /* RTSOFF low byte via CAP+0x18 */
    /* CAP + RTSOFF / DBOFF are at offsets 0x18 and 0x14 of capability regs. */
    {
        uint32_t rtsoff32 = XR32(c->cap, 0x18u) & ~0x1Fu;
        uint32_t dboff32 = XR32(c->cap, 0x14u) & ~0x3u;
        c->rt = c->cap + rtsoff32;
        c->db = c->cap + dboff32;
    }
    (void)hccp;
    (void)rtsoff;
    (void)dboff;

    if (!XhciReset(c))
        return 0;
    XW32(c->op, XHCI_CONFIG, c->maxSlots);
    dcbaa = (uint64_t *)DmaPage(&dcbaaPa);
    if (!dcbaa)
        return 0;
    MmioW64(c->op + XHCI_DCBAAP, dcbaaPa);
    /* Command ring: one page, cycle bit set, stop bit clear. */
    {
        PhysAddr crPa;
        uint64_t *cr = (uint64_t *)DmaPage(&crPa);
        if (!cr)
            return 0;
        cr[0] = 1ull; /* cycle */
        MmioW64(c->op + XHCI_CRCR, crPa | 1ull);
    }
    if (!XhciRun(c))
        return 0;
    gPorts += c->maxPorts;
    ScanPorts(c);
    c->ready = 1;
    kinfo("[INFO] [DRIVER] xHCI slots %u ports %u connected %u\n",
            (unsigned)c->maxSlots, (unsigned)c->maxPorts,
            (unsigned)c->connected);
    return 1;
}

static int XhciProbe(RoboDevice *dev)
{
    RoboPciDevice *pci;
    if (!dev || !dev->busData)
        return -1;
    pci = (RoboPciDevice *)dev->busData;
    if (pci->classCode != 0x0C || pci->subclass != 0x03)
        return -1;
    if (pci->progif != 0x30)
        return -1;
    return 0;
}

static int XhciStart(RoboDevice *dev)
{
    struct XhciCtrl *c;
    if (gXhciN >= XHCI_MAX_CTRL)
        return -1;
    c = &gXhci[gXhciN];
    if (!XhciInitCtrl(c, (RoboPciDevice *)dev->busData))
        return -1;
    gXhciN++;
    return 0;
}

static int XhciStop(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static int XhciRemove(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static const RoboDriverId xhciIds[] = {
    { RoboBusPci, 0, 0, 0, 0, 0x0C, 0x03, 0x30,
      RoboMatchBus | RoboMatchClass }
};

static RoboDriver xhciDrv = {
    "xhci",
    "Roboticela",
    0x00010000u,
    RoboDriverAbiMajor,
    RoboDriverAbiMinor,
    RoboCapPciConfig | RoboCapMmio | RoboCapDma | RoboCapUsb | RoboCapIrq,
    RoboDrvChannelBuiltin,
    RoboDrvStable,
    RoboDrvDiscovered,
    1,
    xhciIds,
    1,
    XhciProbe,
    XhciStart,
    XhciStop,
    XhciRemove
};

int XhciRegister(void)
{
    return DriverRegister(&xhciDrv);
}

unsigned XhciPortCount(void)
{
    return gPorts;
}

unsigned XhciConnectedPorts(void)
{
    return gConnected;
}
