#include "input/input.h"
#include "time/time.h"
#include "kio.h"

#define ROBO_INPUT_Q 128

static RoboInputEvent g_q[ROBO_INPUT_Q];
static unsigned g_head, g_tail, g_n, g_drop;
static int g_kbd, g_mouse;

int robo_input_init(void)
{
    g_head = g_tail = g_n = g_drop = 0;
    g_kbd = 0;
    g_mouse = 0;
    return 1;
}

int robo_input_push(const RoboInputEvent *ev)
{
    if (!ev)
        return 0;
    if (g_n >= ROBO_INPUT_Q) {
        g_drop++;
        return 0;
    }
    g_q[g_head] = *ev;
    if (!g_q[g_head].timestamp)
        g_q[g_head].timestamp = robo_time_ns();
    g_head = (g_head + 1u) % ROBO_INPUT_Q;
    g_n++;
    return 1;
}

int robo_input_pop(RoboInputEvent *ev)
{
    if (!ev || !g_n)
        return 0;
    *ev = g_q[g_tail];
    g_tail = (g_tail + 1u) % ROBO_INPUT_Q;
    g_n--;
    return 1;
}

unsigned robo_input_overflows(void)
{
    return g_drop;
}

unsigned robo_input_queued(void)
{
    return g_n;
}

void robo_input_set_keyboard_ready(int v)
{
    g_kbd = v ? 1 : 0;
}

void robo_input_set_mouse_ready(int v)
{
    g_mouse = v ? 1 : 0;
}

int robo_input_keyboard_ready(void)
{
    return g_kbd;
}

int robo_input_mouse_ready(void)
{
    return g_mouse;
}

int robo_input_selftest(void)
{
    RoboInputEvent e, g;
    unsigned i;
    int ok = 1;

    while (robo_input_pop(&e))
        ;
    e.type = ROBO_KEY_DOWN;
    e.device = ROBO_INPUT_KEYBOARD;
    e.code = ROBO_KEY_A;
    e.value = 1;
    e.x = e.y = e.dx = e.dy = 0;
    e.modifiers = ROBO_MOD_SHIFT;
    e.buttons = 0;
    e.timestamp = 1;
    if (!robo_input_push(&e))
        ok = 0;
    e.type = ROBO_KEY_UP;
    e.value = 0;
    e.timestamp = 2;
    if (!robo_input_push(&e))
        ok = 0;
    e.type = ROBO_MOUSE_MOVE;
    e.device = ROBO_INPUT_MOUSE;
    e.code = 0;
    e.dx = 3;
    e.dy = -1;
    e.timestamp = 3;
    if (!robo_input_push(&e))
        ok = 0;
    e.type = ROBO_MOUSE_BUTTON;
    e.code = ROBO_MBUTTON_LEFT;
    e.value = 1;
    e.buttons = ROBO_MBUTTON_LEFT;
    e.timestamp = 4;
    if (!robo_input_push(&e))
        ok = 0;
    e.type = ROBO_MOUSE_SCROLL;
    e.dy = 1;
    e.timestamp = 5;
    if (!robo_input_push(&e))
        ok = 0;

    if (!robo_input_pop(&g) || g.type != ROBO_KEY_DOWN || g.code != ROBO_KEY_A)
        ok = 0;
    if (!robo_input_pop(&g) || g.type != ROBO_KEY_UP)
        ok = 0;
    if (!robo_input_pop(&g) || g.type != ROBO_MOUSE_MOVE || g.dx != 3)
        ok = 0;
    if (!robo_input_pop(&g) || g.type != ROBO_MOUSE_BUTTON)
        ok = 0;
    if (!robo_input_pop(&g) || g.type != ROBO_MOUSE_SCROLL)
        ok = 0;
    if (robo_input_pop(&g))
        ok = 0;

    for (i = 0; i < ROBO_INPUT_Q + 4u; i++) {
        e.type = ROBO_KEY_DOWN;
        e.device = ROBO_INPUT_KEYBOARD;
        e.code = ROBO_KEY_SPACE;
        e.timestamp = i + 10;
        robo_input_push(&e);
    }
    if (robo_input_overflows() == 0)
        ok = 0;
    while (robo_input_pop(&e))
        ;
    kprintf("INPUT_TEST  queue %s  overflow %u\n",
            ok ? "ok" : "fail", robo_input_overflows());
    return ok;
}
