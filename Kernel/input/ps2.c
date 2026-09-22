#include "input/ps2.h"
#include "input/input.h"
#include "drivers/driver.h"
#include "irq/irq.h"
#include "arch/x86_64/io.h"
#include "graphics/gfx.h"
#include "kio.h"

#define PS2_DATA 0x60
#define PS2_CMD  0x64

static int gE0;
static uint8_t gMpkt[3];
static unsigned gMn;
static uint32_t gMods;
static uint32_t gBtns;
static int gKbdOk, gMouseOk;
static int32_t gMx, gMy;
static int gHavePos;

static void MouseBounds(int32_t *maxX, int32_t *maxY)
{
    RoboDisplayInfo info;

    *maxX = 1279;
    *maxY = 799;
    if (DisplayGetInfo(&info) && info.width > 0u && info.height > 0u) {
        *maxX = (int32_t)info.width - 1;
        *maxY = (int32_t)info.height - 1;
    }
    if (*maxX < 0)
        *maxX = 0;
    if (*maxY < 0)
        *maxY = 0;
}

static void MouseAdvance(int dx, int dy)
{
    int32_t maxX, maxY;

    MouseBounds(&maxX, &maxY);
    if (!gHavePos) {
        gMx = maxX / 2;
        gMy = maxY / 2;
        gHavePos = 1;
    }
    gMx += (int32_t)dx;
    gMy += (int32_t)dy;
    if (gMx < 0)
        gMx = 0;
    if (gMy < 0)
        gMy = 0;
    if (gMx > maxX)
        gMx = maxX;
    if (gMy > maxY)
        gMy = maxY;
}

static void WaitIn(void)
{
    unsigned i;
    for (i = 0; i < 100000u; i++) {
        if (!(Inb(PS2_CMD) & 2u))
            return;
    }
}

static void WaitOut(void)
{
    unsigned i;
    for (i = 0; i < 100000u; i++) {
        if (Inb(PS2_CMD) & 1u)
            return;
    }
}

static void Ps2Cmd(uint8_t c)
{
    WaitIn();
    Outb(PS2_CMD, c);
}

static void Ps2Data(uint8_t c)
{
    WaitIn();
    Outb(PS2_DATA, c);
}

static int Ps2Read(uint8_t *v)
{
    unsigned i;
    for (i = 0; i < 100000u; i++) {
        if (Inb(PS2_CMD) & 1u) {
            *v = Inb(PS2_DATA);
            return 1;
        }
    }
    return 0;
}

static uint16_t MapSet1(uint8_t sc, int ext)
{
    if (ext) {
        if (sc == 0x1D)
            return RoboKeyLctrl;
        if (sc == 0x48)
            return 0x52u; /* ArrowUp */
        if (sc == 0x4B)
            return 0x50u; /* ArrowLeft */
        if (sc == 0x4D)
            return 0x4Fu; /* ArrowRight */
        if (sc == 0x50)
            return 0x51u; /* ArrowDown */
        return RoboKeyUnknown;
    }
    switch (sc) {
    case 0x01: return RoboKeyEsc;
    case 0x02: return RoboKey1;
    case 0x03: return RoboKey2;
    case 0x04: return RoboKey3;
    case 0x05: return RoboKey4;
    case 0x06: return RoboKey5;
    case 0x07: return RoboKey6;
    case 0x08: return RoboKey7;
    case 0x09: return RoboKey8;
    case 0x0A: return RoboKey9;
    case 0x0B: return RoboKey0;
    case 0x0C: return RoboKeyMinus;
    case 0x0D: return RoboKeyEqual;
    case 0x0E: return RoboKeyBackspace;
    case 0x0F: return RoboKeyTab;
    case 0x10: return RoboKeyQ;
    case 0x11: return RoboKeyW;
    case 0x12: return RoboKeyE;
    case 0x13: return RoboKeyR;
    case 0x14: return RoboKeyT;
    case 0x15: return RoboKeyY;
    case 0x16: return RoboKeyU;
    case 0x17: return RoboKeyI;
    case 0x18: return RoboKeyO;
    case 0x19: return RoboKeyP;
    case 0x1A: return RoboKeyLbracket;
    case 0x1B: return RoboKeyRbracket;
    case 0x1C: return RoboKeyEnter;
    case 0x1D: return RoboKeyLctrl;
    case 0x1E: return RoboKeyA;
    case 0x1F: return RoboKeyS;
    case 0x20: return RoboKeyD;
    case 0x21: return RoboKeyF;
    case 0x22: return RoboKeyG;
    case 0x23: return RoboKeyH;
    case 0x24: return RoboKeyJ;
    case 0x25: return RoboKeyK;
    case 0x26: return RoboKeyL;
    case 0x27: return RoboKeySemicolon;
    case 0x28: return RoboKeyQuote;
    case 0x29: return RoboKeyGrave;
    case 0x2A: return RoboKeyLshift;
    case 0x2B: return RoboKeyBackslash;
    case 0x2C: return RoboKeyZ;
    case 0x2D: return RoboKeyX;
    case 0x2E: return RoboKeyC;
    case 0x2F: return RoboKeyV;
    case 0x30: return RoboKeyB;
    case 0x31: return RoboKeyN;
    case 0x32: return RoboKeyM;
    case 0x33: return RoboKeyComma;
    case 0x34: return RoboKeyDot;
    case 0x35: return RoboKeySlash;
    case 0x36: return RoboKeyLshift;
    case 0x39: return RoboKeySpace;
    default:
        return RoboKeyUnknown;
    }
}

static void KbdIrq(unsigned irq, void *ctx)
{
    uint8_t sc;
    int rel, ext;
    uint16_t code;
    RoboInputEvent e;
    (void)irq;
    (void)ctx;
    if (!(Inb(PS2_CMD) & 1u))
        return;
    sc = Inb(PS2_DATA);
    if (sc == 0xE0) {
        gE0 = 1;
        return;
    }
    ext = gE0;
    gE0 = 0;
    rel = (sc & 0x80u) != 0;
    sc = (uint8_t)(sc & 0x7Fu);
    code = MapSet1(sc, ext);
    if (code == RoboKeyLshift)
        gMods = rel ? (gMods & ~RoboModShift) : (gMods | RoboModShift);
    if (code == RoboKeyLctrl)
        gMods = rel ? (gMods & ~RoboModCtrl) : (gMods | RoboModCtrl);
    e.type = rel ? RoboKeyUp : RoboKeyDown;
    e.device = RoboInputKeyboard;
    e.code = code;
    e.value = rel ? 0u : 1u;
    e.x = e.y = e.dx = e.dy = 0;
    e.modifiers = gMods;
    e.buttons = 0;
    e.timestamp = 0;
    RoboInputPush(&e);
}

static void MouseIrq(unsigned irq, void *ctx)
{
    uint8_t b;
    (void)irq;
    (void)ctx;
    if (!(Inb(PS2_CMD) & 1u))
        return;
    b = Inb(PS2_DATA);
    if (gMn == 0 && !(b & 0x08u))
        return;
    gMpkt[gMn++] = b;
    if (gMn < 3u)
        return;
    gMn = 0;
    {
        RoboInputEvent e;
        int dx = (int)(int8_t)gMpkt[1];
        int dy = (int)(int8_t)gMpkt[2];
        uint32_t nb = 0;
        if (gMpkt[0] & 1u)
            nb |= RoboMbuttonLeft;
        if (gMpkt[0] & 2u)
            nb |= RoboMbuttonRight;
        if (gMpkt[0] & 4u)
            nb |= RoboMbuttonMiddle;
        dy = -dy;
        MouseAdvance(dx, dy);
        e.type = RoboMouseMove;
        e.device = RoboInputMouse;
        e.code = 0;
        e.value = 0;
        e.dx = dx;
        e.dy = dy;
        e.x = gMx;
        e.y = gMy;
        e.modifiers = 0;
        e.buttons = nb;
        e.timestamp = 0;
        if (dx || dy)
            RoboInputPush(&e);
        if (nb != gBtns) {
            unsigned bit;
            for (bit = 1; bit <= 4; bit <<= 1) {
                if ((nb & bit) == (gBtns & bit))
                    continue;
                e.type = RoboMouseButton;
                e.code = bit;
                e.value = (nb & bit) ? 1u : 0u;
                e.dx = e.dy = 0;
                e.x = gMx;
                e.y = gMy;
                RoboInputPush(&e);
            }
            gBtns = nb;
        }
    }
}

static int Ps2ControllerInit(void)
{
    uint8_t v, cfg;
    Ps2Cmd(0xAD);
    Ps2Cmd(0xA7);
    if (Inb(PS2_CMD) & 1u)
        (void)Inb(PS2_DATA);
    Ps2Cmd(0x20);
    if (!Ps2Read(&cfg))
        return 0;
    cfg = (uint8_t)((cfg | 0x03u) & (uint8_t)~0x30u);
    Ps2Cmd(0x60);
    Ps2Data(cfg);
    Ps2Cmd(0xAE);
    Ps2Cmd(0xA8);
    Ps2Data(0xFF);
    if (!Ps2Read(&v))
        return 0;
    return 1;
}

static int KbdProbe(RoboDevice *d)
{
    (void)d;
    return 0;
}

static int KbdStart(RoboDevice *d)
{
    (void)d;
    if (!Ps2ControllerInit())
        return -1;
    if (!IrqRegister(1, KbdIrq, 0))
        return -1;
    gKbdOk = 1;
    InputSetKeyboardReady(1);
    kprintf("[INFO] [DRIVER] PS/2 keyboard initialized\n");
    return 0;
}

static int KbdStop(RoboDevice *d)
{
    (void)d;
    IrqUnregister(1);
    gKbdOk = 0;
    InputSetKeyboardReady(0);
    return 0;
}

static int MouseProbe(RoboDevice *d)
{
    (void)d;
    return 0;
}

static int MouseStart(RoboDevice *d)
{
    uint8_t v;
    (void)d;
    Ps2Cmd(0xA8);
    Ps2Cmd(0xD4);
    Ps2Data(0xF4);
    if (!Ps2Read(&v))
        return -1;
    if (!IrqRegister(12, MouseIrq, 0))
        return -1;
    gMouseOk = 1;
    InputSetMouseReady(1);
    kprintf("[INFO] [DRIVER] PS/2 mouse initialized\n");
    return 0;
}

static int MouseStop(RoboDevice *d)
{
    (void)d;
    IrqUnregister(12);
    gMouseOk = 0;
    InputSetMouseReady(0);
    return 0;
}

static const RoboDriverId kbdIds[] = {
    { RoboBusPlatform, 0, RoboPlatKbd, 0, 0, 0, 0, 0,
      RoboMatchBus | RoboMatchDevice }
};

static const RoboDriverId mouseIds[] = {
    { RoboBusPlatform, 0, RoboPlatMouse, 0, 0, 0, 0, 0,
      RoboMatchBus | RoboMatchDevice }
};

static RoboDriver kbdDrv = {
    "ps2-keyboard", "Roboticela", 0x00010000u,
    RoboDriverAbiMajor, RoboDriverAbiMinor,
    RoboCapIrq | RoboCapInput,
    RoboDrvChannelBuiltin, RoboDrvStable, RoboDrvDiscovered, 1,
    kbdIds, 1, KbdProbe, KbdStart, KbdStop, 0
};

static RoboDriver mouseDrv = {
    "ps2-mouse", "Roboticela", 0x00010000u,
    RoboDriverAbiMajor, RoboDriverAbiMinor,
    RoboCapIrq | RoboCapInput,
    RoboDrvChannelBuiltin, RoboDrvStable, RoboDrvDiscovered, 1,
    mouseIds, 1, MouseProbe, MouseStart, MouseStop, 0
};

int Ps2KbdRegister(void)
{
    return DriverRegister(&kbdDrv);
}

int Ps2MouseRegister(void)
{
    return DriverRegister(&mouseDrv);
}
