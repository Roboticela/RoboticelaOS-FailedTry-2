#include "arch/x86_64/acpi/acpi.h"
#include "arch/x86_64/mmio.h"
#include "kio.h"

static uint64_t gRsdp;
static RoboMcfgAlloc gMcfg[4];
static int gNmcfg;
static int gReady;

static RoboFadtInfo gFadt;
static RoboMadtInfo gMadt;
static int gHaveFadt;
static int gHaveMadt;

static uint8_t r8(const uint8_t *p)
{
    return p[0];
}

static uint32_t r32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t r64(const uint8_t *p)
{
    return (uint64_t)r32(p) | ((uint64_t)r32(p + 4) << 32);
}

static int checksum(const uint8_t *p, unsigned n)
{
    unsigned i;
    uint8_t s = 0;
    for (i = 0; i < n; i++)
        s = (uint8_t)(s + p[i]);
    return s == 0;
}

static int sig4(const uint8_t *p, const char *s)
{
    return p[0] == (uint8_t)s[0] && p[1] == (uint8_t)s[1] &&
           p[2] == (uint8_t)s[2] && p[3] == (uint8_t)s[3];
}

static const uint8_t *MapTable(uint64_t pa, uint32_t min)
{
    uint8_t *h;
    uint32_t len;
    h = (uint8_t *)MmioMap((PhysAddr)pa, min);
    if (!h)
        return 0;
    len = r32(h + 4);
    if (len < min)
        return 0;
    if (len > min) {
        h = (uint8_t *)MmioMap((PhysAddr)pa, len);
        if (!h)
            return 0;
    }
    if (!checksum(h, len))
        return 0;
    return h;
}

static void ParseMcfg(const uint8_t *t)
{
    uint32_t len = r32((uint8_t *)t + 4);
    uint32_t off = 44;
    while (off + 16 <= len && gNmcfg < 4) {
        gMcfg[gNmcfg].base = r64(t + off);
        gMcfg[gNmcfg].segment = (uint16_t)(t[off + 8] | (t[off + 9] << 8));
        gMcfg[gNmcfg].startBus = t[off + 10];
        gMcfg[gNmcfg].endBus = t[off + 11];
        gNmcfg++;
        off += 16;
    }
}

static void ParseFadt(const uint8_t *t)
{
    uint32_t len = r32((uint8_t *)t + 4);
    gFadt.sciInterrupt = 0;
    gFadt.smiCommand = 0;
    gFadt.pm1aEvent = 0;
    gFadt.pm1aControl = 0;
    gFadt.resetReg = 0;
    gFadt.resetValue = 0;
    gFadt.flags = 0;
    if (len >= 116) {
        gFadt.sciInterrupt = (uint16_t)(t[46] | (t[47] << 8));
        gFadt.smiCommand = r32(t + 48);
        gFadt.pm1aEvent = r32(t + 56);
        gFadt.pm1aControl = r32(t + 64);
        gFadt.flags = r32(t + 112);
    }
    if (len >= 129) {
        /* Reset register GAS at offset 116: address at +8 within GAS (8 bytes). */
        gFadt.resetReg = r64(t + 116 + 8);
        gFadt.resetValue = t[128];
    }
    gHaveFadt = 1;
    kinfo("[INFO] [ACPI] FADT  SCI %u  reset %llx\n",
            (unsigned)gFadt.sciInterrupt,
            (unsigned long long)gFadt.resetReg);
}

static void ParseMadt(const uint8_t *t)
{
    uint32_t len = r32((uint8_t *)t + 4);
    uint32_t off = 44;

    gMadt.localApicAddress = r32(t + 36);
    gMadt.flags = r32(t + 40);
    gMadt.cpuCount = 0;
    gMadt.ioApicCount = 0;
    while (off + 2 <= len) {
        uint8_t type = t[off];
        uint8_t entLen = t[off + 1];
        if (entLen < 2 || off + entLen > len)
            break;
        if (type == 0 && entLen >= 8) {
            /* Processor Local APIC */
            if (!(t[off + 4] & 1u)) {
                /* disabled */
            } else if (gMadt.cpuCount < 255u)
                gMadt.cpuCount++;
        } else if (type == 1 && entLen >= 12) {
            if (gMadt.ioApicCount < 255u)
                gMadt.ioApicCount++;
        } else if (type == 9 && entLen >= 16) {
            /* x2APIC */
            if (gMadt.cpuCount < 255u)
                gMadt.cpuCount++;
        }
        off += entLen;
    }
    gHaveMadt = 1;
    kinfo("[INFO] [ACPI] MADT  CPUs %u  I/O APICs %u  LAPIC %llx\n",
            (unsigned)gMadt.cpuCount, (unsigned)gMadt.ioApicCount,
            (unsigned long long)gMadt.localApicAddress);
}

int AcpiInit(const RoboBootInfo *info)
{
    const uint8_t *rsdp, *xsdt;
    uint64_t xsdtPa;
    uint32_t len, i, n;

    gRsdp = 0;
    gNmcfg = 0;
    gHaveFadt = 0;
    gHaveMadt = 0;
    gReady = 1;
    if (!info || !info->rsdpAddress)
        return 1;
    rsdp = (const uint8_t *)MmioMap((PhysAddr)info->rsdpAddress, 36);
    if (!rsdp)
        return 1;
    if (rsdp[0] != 'R' || rsdp[1] != 'S' || rsdp[2] != 'D' || rsdp[3] != ' ' ||
        rsdp[4] != 'P' || rsdp[5] != 'T' || rsdp[6] != 'R' || rsdp[7] != ' ')
        return 1;
    if (!checksum(rsdp, 20))
        return 1;
    gRsdp = info->rsdpAddress;
    if (r8(rsdp + 15) < 2 || !checksum(rsdp, 36))
        return 1;
    xsdtPa = r64(rsdp + 24);
    xsdt = MapTable(xsdtPa, 36);
    if (!xsdt || !sig4(xsdt, "XSDT"))
        return 1;
    len = r32((uint8_t *)xsdt + 4);
    if (len < 36)
        return 1;
    n = (len - 36) / 8;
    for (i = 0; i < n; i++) {
        uint64_t pa = r64(xsdt + 36 + i * 8);
        const uint8_t *t = MapTable(pa, 36);
        if (!t)
            continue;
        if (sig4(t, "MCFG"))
            ParseMcfg(t);
        else if (sig4(t, "FACP"))
            ParseFadt(t);
        else if (sig4(t, "APIC"))
            ParseMadt(t);
    }
    return 1;
}

uint64_t AcpiRsdp(void)
{
    return gRsdp;
}

int AcpiMcfgCount(void)
{
    return gNmcfg;
}

int AcpiMcfgAt(unsigned i, RoboMcfgAlloc *out)
{
    if (!out || (int)i >= gNmcfg)
        return 0;
    *out = gMcfg[i];
    return 1;
}

int AcpiFadtGet(RoboFadtInfo *out)
{
    if (!out || !gHaveFadt)
        return 0;
    *out = gFadt;
    return 1;
}

int AcpiMadtGet(RoboMadtInfo *out)
{
    if (!out || !gHaveMadt)
        return 0;
    *out = gMadt;
    return 1;
}
