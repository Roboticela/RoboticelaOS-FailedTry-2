#include "input/ps2.h"
#include "input/input.h"
#include "drivers/driver.h"
#include "irq/irq.h"
#include "arch/x86_64/io.h"
#include "kio.h"

#define PS2_DATA 0x60
#define PS2_CMD  0x64

static int g_e0;
static uint8_t g_mpkt[3];
static unsigned g_mn;
static uint32_t g_mods;
static uint32_t g_btns;
static int g_kbd_ok, g_mouse_ok;

static void wait_in(void)
{
    unsigned i;
    for (i = 0; i < 100000u; i++) {
        if (!(robo_inb(PS2_CMD) & 2u))
            return;
    }
}

static void wait_out(void)
{
    unsigned i;
    for (i = 0; i < 100000u; i++) {
        if (robo_inb(PS2_CMD) & 1u)
            return;
    }
}

static void ps2_cmd(uint8_t c)
{
    wait_in();
    robo_outb(PS2_CMD, c);
}

static void ps2_data(uint8_t c)
{
    wait_in();
    robo_outb(PS2_DATA, c);
}

static int ps2_read(uint8_t *v)
{
    unsigned i;
    for (i = 0; i < 100000u; i++) {
        if (robo_inb(PS2_CMD) & 1u) {
            *v = robo_inb(PS2_DATA);
            return 1;
        }
    }
    return 0;
}

static uint16_t map_set1(uint8_t sc, int ext)
{
    if (ext) {
        if (sc == 0x1D)
            return ROBO_KEY_LCTRL;
        return ROBO_KEY_UNKNOWN;
    }
    switch (sc) {
    case 0x01: return ROBO_KEY_ESC;
    case 0x02: return ROBO_KEY_1;
    case 0x03: return ROBO_KEY_2;
    case 0x04: return ROBO_KEY_3;
    case 0x05: return ROBO_KEY_4;
    case 0x06: return ROBO_KEY_5;
    case 0x07: return ROBO_KEY_6;
    case 0x08: return ROBO_KEY_7;
    case 0x09: return ROBO_KEY_8;
    case 0x0A: return ROBO_KEY_9;
    case 0x0B: return ROBO_KEY_0;
    case 0x0C: return ROBO_KEY_MINUS;
    case 0x0D: return ROBO_KEY_EQUAL;
    case 0x0E: return ROBO_KEY_BACKSPACE;
    case 0x0F: return ROBO_KEY_TAB;
    case 0x10: return ROBO_KEY_Q;
    case 0x11: return ROBO_KEY_W;
    case 0x12: return ROBO_KEY_E;
    case 0x13: return ROBO_KEY_R;
    case 0x14: return ROBO_KEY_T;
    case 0x15: return ROBO_KEY_Y;
    case 0x16: return ROBO_KEY_U;
    case 0x17: return ROBO_KEY_I;
    case 0x18: return ROBO_KEY_O;
    case 0x19: return ROBO_KEY_P;
    case 0x1A: return ROBO_KEY_LBRACKET;
    case 0x1B: return ROBO_KEY_RBRACKET;
    case 0x1C: return ROBO_KEY_ENTER;
    case 0x1D: return ROBO_KEY_LCTRL;
    case 0x1E: return ROBO_KEY_A;
    case 0x1F: return ROBO_KEY_S;
    case 0x20: return ROBO_KEY_D;
    case 0x21: return ROBO_KEY_F;
    case 0x22: return ROBO_KEY_G;
    case 0x23: return ROBO_KEY_H;
    case 0x24: return ROBO_KEY_J;
    case 0x25: return ROBO_KEY_K;
    case 0x26: return ROBO_KEY_L;
    case 0x27: return ROBO_KEY_SEMICOLON;
    case 0x28: return ROBO_KEY_QUOTE;
    case 0x29: return ROBO_KEY_GRAVE;
    case 0x2A: return ROBO_KEY_LSHIFT;
    case 0x2B: return ROBO_KEY_BACKSLASH;
    case 0x2C: return ROBO_KEY_Z;
    case 0x2D: return ROBO_KEY_X;
    case 0x2E: return ROBO_KEY_C;
    case 0x2F: return ROBO_KEY_V;
    case 0x30: return ROBO_KEY_B;
    case 0x31: return ROBO_KEY_N;
    case 0x32: return ROBO_KEY_M;
    case 0x33: return ROBO_KEY_COMMA;
    case 0x34: return ROBO_KEY_DOT;
    case 0x35: return ROBO_KEY_SLASH;
    case 0x36: return ROBO_KEY_LSHIFT;
    case 0x39: return ROBO_KEY_SPACE;
    default:
        return ROBO_KEY_UNKNOWN;
    }
}

static void kbd_irq(unsigned irq, void *ctx)
{
    uint8_t sc;
    int rel, ext;
    uint16_t code;
    RoboInputEvent e;
    (void)irq;
    (void)ctx;
    if (!(robo_inb(PS2_CMD) & 1u))
        return;
    sc = robo_inb(PS2_DATA);
    if (sc == 0xE0) {
        g_e0 = 1;
        return;
    }
    ext = g_e0;
    g_e0 = 0;
    rel = (sc & 0x80u) != 0;
    sc = (uint8_t)(sc & 0x7Fu);
    code = map_set1(sc, ext);
    if (code == ROBO_KEY_LSHIFT)
        g_mods = rel ? (g_mods & ~ROBO_MOD_SHIFT) : (g_mods | ROBO_MOD_SHIFT);
    if (code == ROBO_KEY_LCTRL)
        g_mods = rel ? (g_mods & ~ROBO_MOD_CTRL) : (g_mods | ROBO_MOD_CTRL);
    e.type = rel ? ROBO_KEY_UP : ROBO_KEY_DOWN;
    e.device = ROBO_INPUT_KEYBOARD;
    e.code = code;
    e.value = rel ? 0u : 1u;
    e.x = e.y = e.dx = e.dy = 0;
    e.modifiers = g_mods;
    e.buttons = 0;
    e.timestamp = 0;
    robo_input_push(&e);
}

static void mouse_irq(unsigned irq, void *ctx)
{
    uint8_t b;
    (void)irq;
    (void)ctx;
    if (!(robo_inb(PS2_CMD) & 1u))
        return;
    b = robo_inb(PS2_DATA);
    if (g_mn == 0 && !(b & 0x08u))
        return;
    g_mpkt[g_mn++] = b;
    if (g_mn < 3u)
        return;
    g_mn = 0;
    {
        RoboInputEvent e;
        int dx = (int)(int8_t)g_mpkt[1];
        int dy = (int)(int8_t)g_mpkt[2];
        uint32_t nb = 0;
        if (g_mpkt[0] & 1u)
            nb |= ROBO_MBUTTON_LEFT;
        if (g_mpkt[0] & 2u)
            nb |= ROBO_MBUTTON_RIGHT;
        if (g_mpkt[0] & 4u)
            nb |= ROBO_MBUTTON_MIDDLE;
        dy = -dy;
        e.type = ROBO_MOUSE_MOVE;
        e.device = ROBO_INPUT_MOUSE;
        e.code = 0;
        e.value = 0;
        e.dx = dx;
        e.dy = dy;
        e.x = 0;
        e.y = 0;
        e.modifiers = 0;
        e.buttons = nb;
        e.timestamp = 0;
        if (dx || dy)
            robo_input_push(&e);
        if (nb != g_btns) {
            unsigned bit;
            for (bit = 1; bit <= 4; bit <<= 1) {
                if ((nb & bit) == (g_btns & bit))
                    continue;
                e.type = ROBO_MOUSE_BUTTON;
                e.code = bit;
                e.value = (nb & bit) ? 1u : 0u;
                e.dx = e.dy = 0;
                robo_input_push(&e);
            }
            g_btns = nb;
        }
    }
}

static int ps2_controller_init(void)
{
    uint8_t v, cfg;
    ps2_cmd(0xAD);
    ps2_cmd(0xA7);
    if (robo_inb(PS2_CMD) & 1u)
        (void)robo_inb(PS2_DATA);
    ps2_cmd(0x20);
    if (!ps2_read(&cfg))
        return 0;
    cfg = (uint8_t)((cfg | 0x03u) & (uint8_t)~0x30u);
    ps2_cmd(0x60);
    ps2_data(cfg);
    ps2_cmd(0xAE);
    ps2_cmd(0xA8);
    ps2_data(0xFF);
    if (!ps2_read(&v))
        return 0;
    return 1;
}

static int kbd_probe(RoboDevice *d)
{
    (void)d;
    return 0;
}

static int kbd_start(RoboDevice *d)
{
    (void)d;
    if (!ps2_controller_init())
        return -1;
    if (!robo_irq_register(1, kbd_irq, 0))
        return -1;
    g_kbd_ok = 1;
    robo_input_set_keyboard_ready(1);
    kprintf("[INFO] [DRIVER] PS/2 keyboard initialized\n");
    return 0;
}

static int kbd_stop(RoboDevice *d)
{
    (void)d;
    robo_irq_unregister(1);
    g_kbd_ok = 0;
    robo_input_set_keyboard_ready(0);
    return 0;
}

static int mouse_probe(RoboDevice *d)
{
    (void)d;
    return 0;
}

static int mouse_start(RoboDevice *d)
{
    uint8_t v;
    (void)d;
    ps2_cmd(0xA8);
    ps2_cmd(0xD4);
    ps2_data(0xF4);
    if (!ps2_read(&v))
        return -1;
    if (!robo_irq_register(12, mouse_irq, 0))
        return -1;
    g_mouse_ok = 1;
    robo_input_set_mouse_ready(1);
    kprintf("[INFO] [DRIVER] PS/2 mouse initialized\n");
    return 0;
}

static int mouse_stop(RoboDevice *d)
{
    (void)d;
    robo_irq_unregister(12);
    g_mouse_ok = 0;
    robo_input_set_mouse_ready(0);
    return 0;
}

static const RoboDriverId kbd_ids[] = {
    { ROBO_BUS_PLATFORM, 0, ROBO_PLAT_KBD, 0, 0, 0, 0, 0,
      ROBO_MATCH_BUS | ROBO_MATCH_DEVICE }
};

static const RoboDriverId mouse_ids[] = {
    { ROBO_BUS_PLATFORM, 0, ROBO_PLAT_MOUSE, 0, 0, 0, 0, 0,
      ROBO_MATCH_BUS | ROBO_MATCH_DEVICE }
};

static RoboDriver kbd_drv = {
    "ps2-keyboard", "Roboticela", 0x00010000u,
    ROBO_DRIVER_ABI_MAJOR, ROBO_DRIVER_ABI_MINOR,
    ROBO_CAP_IRQ | ROBO_CAP_INPUT,
    ROBO_DRV_CHANNEL_BUILTIN, ROBO_DRV_STABLE, ROBO_DRV_DISCOVERED, 1,
    kbd_ids, 1, kbd_probe, kbd_start, kbd_stop, 0
};

static RoboDriver mouse_drv = {
    "ps2-mouse", "Roboticela", 0x00010000u,
    ROBO_DRIVER_ABI_MAJOR, ROBO_DRIVER_ABI_MINOR,
    ROBO_CAP_IRQ | ROBO_CAP_INPUT,
    ROBO_DRV_CHANNEL_BUILTIN, ROBO_DRV_STABLE, ROBO_DRV_DISCOVERED, 1,
    mouse_ids, 1, mouse_probe, mouse_start, mouse_stop, 0
};

int robo_ps2_kbd_register(void)
{
    return robo_driver_register(&kbd_drv);
}

int robo_ps2_mouse_register(void)
{
    return robo_driver_register(&mouse_drv);
}
