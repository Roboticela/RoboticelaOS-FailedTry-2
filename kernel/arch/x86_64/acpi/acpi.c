#include "arch/x86_64/acpi/acpi.h"
#include "arch/x86_64/mmio.h"
#include "kio.h"

static uint64_t g_rsdp;
static RoboMcfgAlloc g_mcfg[4];
static int g_nmcfg;
static int g_ready;

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

static const uint8_t *map_table(uint64_t pa, uint32_t min)
{
    uint8_t *h;
    uint32_t len;
    h = (uint8_t *)robo_mmio_map((phys_addr_t)pa, min);
    if (!h)
        return 0;
    len = r32(h + 4);
    if (len < min)
        return 0;
    if (len > min) {
        h = (uint8_t *)robo_mmio_map((phys_addr_t)pa, len);
        if (!h)
            return 0;
    }
    if (!checksum(h, len))
        return 0;
    return h;
}

static void parse_mcfg(const uint8_t *t)
{
    uint32_t len = r32((uint8_t *)t + 4);
    uint32_t off = 44;
    while (off + 16 <= len && g_nmcfg < 4) {
        g_mcfg[g_nmcfg].base = r64(t + off);
        g_mcfg[g_nmcfg].segment = (uint16_t)(t[off + 8] | (t[off + 9] << 8));
        g_mcfg[g_nmcfg].start_bus = t[off + 10];
        g_mcfg[g_nmcfg].end_bus = t[off + 11];
        g_nmcfg++;
        off += 16;
    }
}

int robo_acpi_init(const RoboBootInfo *info)
{
    const uint8_t *rsdp, *xsdt;
    uint64_t xsdt_pa;
    uint32_t len, i, n;

    g_rsdp = 0;
    g_nmcfg = 0;
    g_ready = 1;
    if (!info || !info->rsdp_address)
        return 1;
    rsdp = (const uint8_t *)robo_mmio_map((phys_addr_t)info->rsdp_address, 36);
    if (!rsdp)
        return 1;
    if (rsdp[0] != 'R' || rsdp[1] != 'S' || rsdp[2] != 'D' || rsdp[3] != ' ' ||
        rsdp[4] != 'P' || rsdp[5] != 'T' || rsdp[6] != 'R' || rsdp[7] != ' ')
        return 1;
    if (!checksum(rsdp, 20))
        return 1;
    g_rsdp = info->rsdp_address;
    if (r8(rsdp + 15) < 2 || !checksum(rsdp, 36))
        return 1;
    xsdt_pa = r64(rsdp + 24);
    xsdt = map_table(xsdt_pa, 36);
    if (!xsdt || !sig4(xsdt, "XSDT"))
        return 1;
    len = r32((uint8_t *)xsdt + 4);
    if (len < 36)
        return 1;
    n = (len - 36) / 8;
    for (i = 0; i < n; i++) {
        uint64_t pa = r64(xsdt + 36 + i * 8);
        const uint8_t *t = map_table(pa, 36);
        if (t && sig4(t, "MCFG"))
            parse_mcfg(t);
    }
    (void)kprintf;
    return 1;
}

uint64_t robo_acpi_rsdp(void)
{
    return g_rsdp;
}

int robo_acpi_mcfg_count(void)
{
    return g_nmcfg;
}

int robo_acpi_mcfg_at(unsigned i, RoboMcfgAlloc *out)
{
    if (!out || (int)i >= g_nmcfg)
        return 0;
    *out = g_mcfg[i];
    return 1;
}
