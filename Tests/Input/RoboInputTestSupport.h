/*
 * Shared RoboInput test fixtures (Phase 12.1).
 *
 * Every suite in `input/tests` includes this. Like the paint, render and desktop support headers it is
 * **not** a framework -- the runtime has exactly one of those (`Tests/Framework/RoboTest.h`) and this
 * release adds none -- and it holds the four things an input suite would otherwise copy:
 *
 *   bring-up    RoboBase, so a diagnostic or a log line behaves as it does in the runtime. There is no
 *               `InputInit` to call, and that is this module's design rather than an omission: it owns
 *               no state, so a fixture is a `BaseInit` and nothing else. Nothing here allocates a
 *               module-wide object, which is why two suites cannot disturb each other's input state;
 *
 *   an event    the two kinds of event a case usually wants -- a pointer event at a point, and a key event
 *               -- built through the *public* doors (`InputEventSetPointer`,
 *               `InputKeyboardDataSet`, ...) rather than by filling the struct, so a fixture cannot
 *               construct something the model's own validator would refuse. `input_pointer_event` in
 *               particular is the case's one-liner for "a valid move at this point";
 *
 *   a route     the geometry a conversion needs, for a window at a chosen place with a chosen client inset.
 *               Two doors, because the two pairs of conversions need two different halves: a frame rectangle
 *               for screen ⟷ window, a client rectangle for window ⟷ document;
 *
 *   a face      none. This module has no pixels, no fonts and no surfaces: an input event is a record, and
 *               the whole suite is about records. That absence is worth stating, because the three support
 *               headers beside this one all exist to build a document and this one exists to build nothing.
 *
 * Every helper asserts rather than returning a status, which is the convention the layout, paint, render and
 * desktop suites set: a suite that had to check each step of its own scaffolding would drown its subject in
 * harness.
 */

#ifndef ROBO_INPUT_TEST_SUPPORT_H
#define ROBO_INPUT_TEST_SUPPORT_H

#include "RoboTest.h"

#include "RoboBase/RoboBase.h"
#include "RoboMem/RoboMemDebug.h"

#include "RoboInput/RoboInput.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Bring-up                                                            */
/* ------------------------------------------------------------------ */

/** Bring the base layer up (once) and leave it up. */
static inline void InputFixture(void)
{
    Result r = BaseInit();

    if (r != RoboOk && r != RoboAlreadyExists) {
        RoboCheckMsg(0, "robo_base_init failed");
    }
}

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t InputLiveBlocks(void)
{
    return MemDebugLiveCount();
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

/**
 * A route for a window at @p frameX,@p frameY on the screen, @p frameW by @p frameH, whose client area
 * starts @p insetX,@p insetY into the frame and is @p clientW by @p clientH.
 *
 * Both halves are filled in, which is what a *dispatcher* would do; a case that wants to prove a conversion
 * refuses without the geometry it needs clears the half it is testing, which is why the record's two flags
 * are public rather than implied.
 */
static inline InputRoute GetRoboInputRoute(int32_t frameX,
                                             int32_t frameY,
                                             int32_t frameW,
                                             int32_t frameH,
                                             int32_t insetX,
                                             int32_t insetY,
                                             int32_t clientW,
                                             int32_t clientH)
{
    InputRoute route;

    InputRouteInit(&route);
    route.window = 7u;
    route.windowBounds = MakeGfxRect(frameX, frameY, frameW, frameH);
    route.hasWindowBounds = 1;
    route.clientBounds = MakeGfxRect(insetX, insetY, clientW, clientH);
    route.hasClientBounds = 1;
    return route;
}

/** A route with only the frame rectangle: enough for screen ⟷ window, and not for a document conversion. */
static inline InputRoute InputRouteFrameOnly(int32_t x, int32_t y, int32_t w, int32_t h)
{
    InputRoute route;

    InputRouteInit(&route);
    route.window = 7u;
    route.windowBounds = MakeGfxRect(x, y, w, h);
    route.hasWindowBounds = 1;
    return route;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

/** A pointer payload at @p x,@p y with @p button held in a mask of @p buttons. Asserts on refusal. */
static inline InputPointerData GetRoboInputPointerData(int32_t x,
                                                           int32_t y,
                                                           MouseButton button,
                                                           uint32_t buttons)
{
    InputPointerData data;

    InputPointerDataInit(&data);
    if (InputPointerDataSet(&data, RoboInputPointerIdPrimary, MakeGfxPoint(x, y), button,
                                    buttons) != RoboOk) {
        RoboCheckMsg(0, "the fixture's pointer payload was refused");
    }
    return data;
}

/** A key payload. Asserts on refusal. */
static inline InputKeyboardData InputKeyData(PhysicalKey physical,
                                                        LogicalKey logical,
                                                        KeyAction action,
                                                        int repeat,
                                                        const char *text)
{
    InputKeyboardData data;

    InputKeyboardDataInit(&data);
    if (InputKeyboardDataSet(&data, physical, logical, action, repeat, text) != RoboOk) {
        RoboCheckMsg(0, "the fixture's keyboard payload was refused");
    }
    return data;
}

/**
 * A mouse move at @p x,@p y in @p space, with @p buttons down, in window @p window.
 *
 * The one-liner most cases want, and it goes through the public door so the result is an event the model
 * itself would accept: a case that went wrong in a later assertion changed something *after* this call.
 */
static inline InputEvent InputPointerMove(int32_t x,
                                                    int32_t y,
                                                    uint32_t buttons,
                                                    InputSpace space,
                                                    WindowId window)
{
    InputEvent event;
    InputPointerData data = GetRoboInputPointerData(x, y, RoboMouseButtonNone, buttons);

    InputEventInit(&event);
    if (InputEventSetPointer(&event, RoboInputEventPointerMove, RoboInputSourceMouse, &data,
                                     space, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's pointer event was refused");
    }
    InputEventSetWindow(&event, window);
    return event;
}

/** A button press or release of @p button at @p x,@p y. @p pressed chooses the type; the mask follows. */
static inline InputEvent InputButtonEvent(int32_t x,
                                                    int32_t y,
                                                    MouseButton button,
                                                    int pressed,
                                                    WindowId window)
{
    InputEvent event;
    InputPointerData data;
    uint32_t mask = 0u;

    if (pressed) {
        mask = InputButtonMaskApply(mask, button, 1);
    }
    data = GetRoboInputPointerData(x, y, button, mask);
    InputEventInit(&event);
    if (InputEventSetPointer(&event,
                                     pressed ? RoboInputEventPointerDown : RoboInputEventPointerUp,
                                     RoboInputSourceMouse, &data, RoboInputSpaceScreen, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's button event was refused");
    }
    InputEventSetWindow(&event, window);
    return event;
}

/** A key event at @p physical, with the given modifier state. */
static inline InputEvent InputKeyEvent(PhysicalKey physical,
                                                 LogicalKey logical,
                                                 int pressed,
                                                 InputModifiers modifiers)
{
    InputEvent event;
    InputKeyboardData data = InputKeyData(physical, logical,
                                                     pressed ? RoboKeyActionDown : RoboKeyActionUp,
                                                     0, NULL);

    InputEventInit(&event);
    if (InputEventSetKeyboard(&event,
                                      pressed ? RoboInputEventKeyDown : RoboInputEventKeyUp,
                                      RoboInputSourceKeyboard, &data, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's key event was refused");
    }
    InputEventSetModifiers(&event, modifiers, NULL);
    InputEventSetWindow(&event, 3u);
    return event;
}

/**
 * A **descriptor** for a device of @p type, with @p capabilities, asserting on refusal.
 *
 * The descriptor is the table's record (RoboInputDevice.h) and the identity above is what an event carries;
 * a case that wants to connect a device or build a hotplug event needs this one, and its source is left
 * `UNKNOWN` so the type's default is what gets recorded -- which is itself one of the facts a suite asserts.
 */
static inline InputDeviceDescriptor InputFixtureDescriptor(InputDeviceType type,
                                                           InputDeviceId id,
                                                           const char *name,
                                                           uint32_t capabilities)
{
    InputDeviceDescriptor descriptor;

    InputDeviceDescriptorInit(&descriptor);
    if (InputDeviceDescriptorSet(&descriptor, id, type, RoboInputSourceUnknown, name,
                                 capabilities, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's descriptor was refused");
    }
    return descriptor;
}

/** A device identity a case can stamp on an event. Asserts on refusal. */
static inline InputDevice GetRoboInputDevice(InputDeviceId id,
                                               InputSource source,
                                               const char *name)
{
    InputDevice device;

    InputDeviceInit(&device);
    if (InputDeviceSet(&device, id, source, name) != RoboOk) {
        RoboCheckMsg(0, "the fixture's device was refused");
    }
    return device;
}

/* ------------------------------------------------------------------ */
/* Queues                                                              */
/* ------------------------------------------------------------------ */

/** Create a queue through the public door, with @p capacity (0 = default). Asserts on refusal. */
static inline InputQueue *GetRoboInputQueue(uint32_t capacity)
{
    InputQueueConfig config;
    InputQueue *queue = NULL;

    InputQueueConfigInit(&config);
    config.capacity = capacity;
    if (InputQueueCreate(&config, &queue, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's queue was refused");
        return NULL;
    }
    return queue;
}

/** Push through the public door, asserting the push was accepted. */
static inline void RoboInputPush(InputQueue *queue, const InputEvent *event)
{
    if (InputQueuePush(queue, event, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's push was refused");
    }
}

/** Dump a queue into a caller's buffer, asserting the dump fit. Returns the length written. */
static inline size_t InputQueueText(const InputQueue *queue, char *buffer, size_t capacity)
{
    size_t length = 0u;

    if (InputQueueWrite(queue, buffer, capacity, &length, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's queue dump did not fit its buffer");
        return 0u;
    }
    return length;
}

#endif /* ROBO_INPUT_TEST_SUPPORT_H */
