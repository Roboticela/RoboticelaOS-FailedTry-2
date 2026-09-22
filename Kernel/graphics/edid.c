#include "graphics/edid.h"
#include "graphics/gfx.h"
#include "kio.h"

static RoboEdidInfo gEdid;
static int gReady;

static void NameCopy(char *dst, unsigned dstLen, const char *src)
{
    unsigned i = 0;
    if (!dst || !dstLen)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1u < dstLen) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int ParseEdidBlock(const uint8_t *edid, unsigned len, RoboEdidInfo *out)
{
    uint16_t mfg;
    unsigned i;

    if (!edid || !out || len < 128u)
        return 0;
    if (edid[0] != 0x00 || edid[1] != 0xFF || edid[2] != 0xFF ||
        edid[3] != 0xFF || edid[4] != 0xFF || edid[5] != 0xFF ||
        edid[6] != 0xFF || edid[7] != 0x00)
        return 0;
    mfg = (uint16_t)((edid[8] << 8) | edid[9]);
    out->manufacturer[0] = (char)('A' + ((mfg >> 10) & 0x1F) - 1);
    out->manufacturer[1] = (char)('A' + ((mfg >> 5) & 0x1F) - 1);
    out->manufacturer[2] = (char)('A' + (mfg & 0x1F) - 1);
    out->manufacturer[3] = 0;
    /* Detailed timing descriptor 0 at offset 54. */
    {
        uint16_t pixClock = (uint16_t)(edid[54] | (edid[55] << 8));
        uint32_t hActive = (uint32_t)edid[56] | (((uint32_t)(edid[58] >> 4) & 0xFu) << 8);
        uint32_t vActive = (uint32_t)edid[59] | (((uint32_t)(edid[61] >> 4) & 0xFu) << 8);
        if (pixClock && hActive && vActive) {
            out->width = hActive;
            out->height = vActive;
            out->refreshHz = 60;
        }
    }
    out->name[0] = 0;
    for (i = 0; i < 4u; i++) {
        const uint8_t *d = edid + 54u + i * 18u;
        if (d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0xFC) {
            unsigned n = 0;
            while (n < 13u && d[5 + n] >= 0x20 && d[5 + n] < 0x7Fu) {
                out->name[n] = (char)d[5 + n];
                n++;
            }
            out->name[n] = 0;
            break;
        }
    }
    if (!out->name[0])
        NameCopy(out->name, RoboEdidNameMax, "Generic Display");
    out->present = 1;
    return 1;
}

int EdidInit(const RoboBootInfo *info)
{
    RoboDisplayInfo di;

    gReady = 0;
    gEdid.present = 0;
    gEdid.width = 0;
    gEdid.height = 0;
    gEdid.refreshHz = 60;
    gEdid.name[0] = 0;
    gEdid.manufacturer[0] = 0;

    /* Bootinfo does not yet carry a raw EDID blob; synthesize from GOP. */
    (void)info;
    if (DisplayGetInfo(&di)) {
        gEdid.present = 1;
        gEdid.width = di.width;
        gEdid.height = di.height;
        gEdid.refreshHz = 60;
        NameCopy(gEdid.name, RoboEdidNameMax, "UEFI Framebuffer");
        gEdid.manufacturer[0] = 'R';
        gEdid.manufacturer[1] = 'B';
        gEdid.manufacturer[2] = 'O';
        gEdid.manufacturer[3] = 0;
        gReady = 1;
        kinfo("[INFO] [DISPLAY] EDID preferred %ux%u (%s)\n",
                (unsigned)gEdid.width, (unsigned)gEdid.height, gEdid.name);
        return 1;
    }
    (void)ParseEdidBlock;
    return 0;
}

int EdidReady(void)
{
    return gReady;
}

int EdidGet(RoboEdidInfo *out)
{
    if (!out || !gReady)
        return 0;
    *out = gEdid;
    return 1;
}

int EdidPreferredMode(uint32_t *width, uint32_t *height)
{
    if (!gReady || !gEdid.present)
        return 0;
    if (width)
        *width = gEdid.width;
    if (height)
        *height = gEdid.height;
    return 1;
}
