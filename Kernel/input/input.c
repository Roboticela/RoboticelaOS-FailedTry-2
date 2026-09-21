#include "input/input.h"
#include "time/time.h"
#include "kio.h"

#define RoboInputQ 128

static RoboInputEvent gQ[RoboInputQ];
static unsigned gHead, gTail, gN, gDrop;
static int gKbd, gMouse;

int InputInit(void)
{
    gHead = gTail = gN = gDrop = 0;
    gKbd = 0;
    gMouse = 0;
    return 1;
}

int RoboInputPush(const RoboInputEvent *ev)
{
    if (!ev)
        return 0;
    if (gN >= RoboInputQ) {
        gDrop++;
        return 0;
    }
    gQ[gHead] = *ev;
    if (!gQ[gHead].timestamp)
        gQ[gHead].timestamp = TimeNs();
    gHead = (gHead + 1u) % RoboInputQ;
    gN++;
    return 1;
}

int InputPop(RoboInputEvent *ev)
{
    if (!ev || !gN)
        return 0;
    *ev = gQ[gTail];
    gTail = (gTail + 1u) % RoboInputQ;
    gN--;
    return 1;
}

unsigned InputOverflows(void)
{
    return gDrop;
}

unsigned InputQueued(void)
{
    return gN;
}

void InputSetKeyboardReady(int v)
{
    gKbd = v ? 1 : 0;
}

void InputSetMouseReady(int v)
{
    gMouse = v ? 1 : 0;
}

int InputKeyboardReady(void)
{
    return gKbd;
}

int InputMouseReady(void)
{
    return gMouse;
}

int InputSelftest(void)
{
    RoboInputEvent e, g;
    unsigned i;
    int ok = 1;

    while (InputPop(&e))
        ;
    e.type = RoboKeyDown;
    e.device = RoboInputKeyboard;
    e.code = RoboKeyA;
    e.value = 1;
    e.x = e.y = e.dx = e.dy = 0;
    e.modifiers = RoboModShift;
    e.buttons = 0;
    e.timestamp = 1;
    if (!RoboInputPush(&e))
        ok = 0;
    e.type = RoboKeyUp;
    e.value = 0;
    e.timestamp = 2;
    if (!RoboInputPush(&e))
        ok = 0;
    e.type = RoboMouseMove;
    e.device = RoboInputMouse;
    e.code = 0;
    e.dx = 3;
    e.dy = -1;
    e.timestamp = 3;
    if (!RoboInputPush(&e))
        ok = 0;
    e.type = RoboMouseButton;
    e.code = RoboMbuttonLeft;
    e.value = 1;
    e.buttons = RoboMbuttonLeft;
    e.timestamp = 4;
    if (!RoboInputPush(&e))
        ok = 0;
    e.type = RoboMouseScroll;
    e.dy = 1;
    e.timestamp = 5;
    if (!RoboInputPush(&e))
        ok = 0;

    if (!InputPop(&g) || g.type != RoboKeyDown || g.code != RoboKeyA)
        ok = 0;
    if (!InputPop(&g) || g.type != RoboKeyUp)
        ok = 0;
    if (!InputPop(&g) || g.type != RoboMouseMove || g.dx != 3)
        ok = 0;
    if (!InputPop(&g) || g.type != RoboMouseButton)
        ok = 0;
    if (!InputPop(&g) || g.type != RoboMouseScroll)
        ok = 0;
    if (InputPop(&g))
        ok = 0;

    for (i = 0; i < RoboInputQ + 4u; i++) {
        e.type = RoboKeyDown;
        e.device = RoboInputKeyboard;
        e.code = RoboKeySpace;
        e.timestamp = i + 10;
        RoboInputPush(&e);
    }
    if (InputOverflows() == 0)
        ok = 0;
    while (InputPop(&e))
        ;
    kprintf("INPUT_TEST  queue %s  overflow %u\n",
            ok ? "ok" : "fail", InputOverflows());
    return ok;
}
