/*
 * RoboInput ownership suite (Phase 12.1) — `input.ownership.*`.
 *
 * The phase brief's §12 asks for one property above all: a queued event must stay meaningful while the world
 * around it changes -- a caller reuses its record, a document is mutated, a window is destroyed, a layout is
 * rebuilt. The way this module gets that is **value semantics**: an event is a struct, a target holds ids and
 * an inert handle rather than a pointer, and the queue copies on the way in and on the way out. That is a
 * claim about the code, and these cases are how the claim is checked rather than asserted:
 *
 *   `snapshot`        the caller mutates *every* field of the event it pushed after the push, and the queued
 *                     copy is unchanged -- which is what "the queue owns its copy" means;
 *
 *   `target_snapshot` the same for the target, which is the record most likely to be shared by accident, and
 *                     the one whose DOM handle is the identity most likely to be mistaken for a reference;
 *
 *   `geometry`        the geometry a conversion needs is handed in per call and never retained: two calls
 *                     with two routes answer differently, which proves the module caches no window table;
 *
 *   `lifetime`        a queue survives the caller's event going out of scope entirely, and the module holds
 *                     nothing after its last object is destroyed.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* The queued copy is the caller's to forget                            */
/* ------------------------------------------------------------------ */

static void TestOwnershipSnapshot(void)
{
    InputQueue *queue = GetRoboInputQueue(4u);
    InputEvent caller = InputButtonEvent(30, 40, RoboMouseButtonLeft, 1, 3u);
    InputEvent expected;
    InputEvent out;
    size_t index;

    RoboCheckEqInt(InputEventSetTimestamp(&caller, 77u), RoboOk);
    RoboCheckEqInt(InputEventSetDevice(&caller,
                                                  &(InputDevice){ 0 }, NULL),
                      RoboOk);
    caller.device = GetRoboInputDevice(2u, RoboInputSourceMouse, "Trackpad");
    expected = caller;

    RoboInputPush(queue, &caller);

    /* Now wreck the caller's record: every field a producer could reuse, in the way a producer reuses them.
     * The queued event must not move. */
    caller.type = RoboInputEventTextInput;
    caller.source = RoboInputSourceVirtual;
    InputKeyboardDataInit(&caller.payload.keyboard);
    caller.payload.pointer.position = MakeGfxPoint(-1, -1);
    caller.payload.pointer.buttons = 0u;
    caller.space = RoboInputSpaceUnknown;
    caller.window = 99u;
    caller.timestampMs = 0u;
    caller.modifiers = RoboKeyModShift;
    InputDeviceInit(&caller.device);
    caller.device.name[0] = 'x';

    RoboCheckEqInt(InputQueueCount(queue), 1u);
    RoboCheckEqInt(InputQueuePeek(queue, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &expected));
    RoboCheckEqInt(out.payload.pointer.position.x, 30);
    RoboCheckEqInt(out.payload.pointer.buttons,
                      InputButtonMask(RoboMouseButtonLeft));
    RoboCheckEqInt(out.timestampMs, 77u);
    RoboCheckStrEq(out.device.name, "Trackpad");
    /* ... and the copy is still a *valid* event: an input event copies like the plain record it is. */
    RoboCheck(InputEventIsValid(&out));

    /* The event the caller pushed had gone out of scope in a real caller, so this is the same check without
     * the wrecking: a fresh event with the same contents compares equal to what came back. */
    {
        InputQueue *second = GetRoboInputQueue(2u);
        InputEvent produced = InputPointerMove(5, 6, 0u, RoboInputSpaceDocument, 3u);

        RoboInputPush(second, &produced);
        produced.payload.pointer.position = MakeGfxPoint(0, 0);
        RoboCheckEqInt(InputQueuePop(second, &out, NULL), RoboOk);
        RoboCheckEqInt(out.payload.pointer.position.x, 5);
        RoboCheckEqInt(out.payload.pointer.position.y, 6);
        InputQueueDestroy(second);
    }

    /* A whole sequence, pushed from one reused buffer, comes back as the sequence -- which is the pattern a
     * backend actually uses and the one a copy-on-push makes safe. */
    {
        InputQueue *sequence = GetRoboInputQueue(8u);
        char line[RoboInputDumpLineMax];

        for (index = 0u; index < 6u; index++) {
            InputEvent step = InputPointerMove((int32_t)index, (int32_t)(index * 2), 0u,
                                                         RoboInputSpaceScreen, 3u);

            RoboInputPush(sequence, &step);
        }
        for (index = 0u; index < 6u; index++) {
            RoboCheckEqInt(InputQueuePop(sequence, &out, NULL), RoboOk);
            RoboCheckEqInt(out.payload.pointer.position.x, (int32_t)index);
            RoboCheckEqInt(out.payload.pointer.position.y, (int32_t)(index * 2));
        }
        /* Drained: the dump is the summary line alone, and it says the queue holds nothing. */
        RoboCheck(InputQueueText(sequence, line, sizeof(line)) > 0u);
        RoboCheck(strstr(line, "count=0") != NULL);
        InputQueueDestroy(sequence);
    }

    InputQueueDestroy(queue);
}

/* ------------------------------------------------------------------ */
/* A target is identity, never a reference                              */
/* ------------------------------------------------------------------ */

static void TestOwnershipTargetSnapshot(void)
{
    InputQueue *queue = GetRoboInputQueue(4u);
    InputEvent event = InputPointerMove(120, 84, 0u, RoboInputSpaceDocument, 3u);
    InputEvent expected;
    InputEvent out;
    InputTarget target;
    HtmlNodeHandle handle = { 4u, 17u, 3u };

    RoboCheckEqInt(InputTargetSetDocument(&target, 3u, &handle, 41u,
                                                     RoboInputTargetFlagContent |
                                                     RoboInputTargetFlagText,
                                                     RoboInputSpaceDocument, NULL),
                      RoboOk);
    RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboOk);
    expected = event;
    RoboInputPush(queue, &event);

    /* The caller now reuses both records -- clears the target, scrubs the handle it passed in, edits the
     * event. None of it may reach the queue. */
    InputTargetInit(&target);
    HtmlNodeHandleClear(&handle);
    handle.nodeId = 999u;
    InputEventClearTarget(&event);
    event.payload.pointer.position = MakeGfxPoint(0, 0);

    /* `handle` is a *value* the target copied, so scrubbing the caller's copy changes nothing: there is no
     * pointer between them to dangle. */
    handle.documentId = 88u;

    RoboCheckEqInt(InputQueuePeek(queue, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &expected));
    RoboCheckEqInt(out.hasTarget, 1);
    RoboCheckEqInt(out.target.kind, RoboInputTargetDocument);
    RoboCheckEqInt(out.target.domNode.documentId, 4u);
    RoboCheckEqInt(out.target.domNode.nodeId, 17u);
    RoboCheckEqInt(out.target.domNode.generation, 3u);
    RoboCheckEqInt(out.target.layoutNode, 41u);
    RoboCheck(InputTargetHasFlag(&out.target, RoboInputTargetFlagContent));
    RoboCheck(InputEventIsValid(&out));

    /* A handle that is not well-formed travels too: a consumer with no document identity (a cached
     * document, a synthetic event) still gets the layout node, which is the identity a hit test answered
     * with. */
    {
        InputTarget layoutOnly;
        InputEvent second = InputPointerMove(1, 1, 0u, RoboInputSpaceDocument, 3u);

        RoboCheckEqInt(InputTargetSetDocument(&layoutOnly, 3u, NULL, 7u, 0u,
                                                        RoboInputSpaceDocument, NULL),
                          RoboOk);
        RoboCheckEqInt(InputEventSetTarget(&second, &layoutOnly, NULL), RoboOk);
        RoboCheck(InputEventIsValid(&second));
        RoboCheck(!HtmlNodeHandleIsWellformed(&second.target.domNode));
        RoboCheckEqInt(second.target.layoutNode, 7u);
    }

    InputQueueDestroy(queue);
}

/* ------------------------------------------------------------------ */
/* Geometry is handed in, not held                                     */
/* ------------------------------------------------------------------ */

static void TestOwnershipGeometry(void)
{
    InputEvent event = InputPointerMove(120, 84, 0u, RoboInputSpaceWindow, 3u);
    GfxPoint out = { 0, 0 };
    InputRoute first = GetRoboInputRoute(10, 20, 400, 300, 1, 25, 398, 274);
    InputRoute second = GetRoboInputRoute(1000, 2000, 400, 300, 1, 25, 398, 274);

    /* The same event, the same conversion, two routes: the answer follows the route that was handed in. A
     * module that had cached a window table would answer the same thing twice. */
    RoboCheckEqInt(InputEventPositionIn(&event, RoboInputSpaceScreen, &first, &out, NULL),
                      RoboOk);
    RoboCheckEqInt(out.x, 130);
    RoboCheckEqInt(out.y, 104);
    RoboCheckEqInt(InputEventPositionIn(&event, RoboInputSpaceScreen, &second, &out, NULL),
                      RoboOk);
    RoboCheckEqInt(out.x, 1120);
    RoboCheckEqInt(out.y, 2084);

    /* The route is a caller's local, and mutating it between calls is legitimate: it is read per call and
     * nothing about the previous call survives. */
    second.windowBounds.x = 5;
    second.windowBounds.y = 6;
    RoboCheckEqInt(InputEventPositionIn(&event, RoboInputSpaceScreen, &second, &out, NULL),
                      RoboOk);
    RoboCheckEqInt(out.x, 125);
    RoboCheckEqInt(out.y, 90);

    /* Clearing the geometry it needs turns the next call into a refusal rather than a cached answer. */
    second.hasWindowBounds = 0;
    RoboCheckEqInt(InputEventPositionIn(&event, RoboInputSpaceScreen, &second, &out, NULL),
                      RoboNotSupported);

    /* ... and the event itself is untouched through all of it. */
    RoboCheckEqInt(event.space, RoboInputSpaceWindow);
    RoboCheckEqInt(event.payload.pointer.position.x, 120);
    RoboCheck(InputEventIsValid(&event));
}

/* ------------------------------------------------------------------ */
/* Nothing outlives the last object                                    */
/* ------------------------------------------------------------------ */

static void TestOwnershipLifetime(void)
{
    size_t baseline = InputLiveBlocks();

    /* Queues holding events with targets and text: the three records with copied storage. Destroying the
     * queue must give back exactly what creating and filling it took, with nothing retained per event. */
    {
        int round;

        for (round = 0; round < 3; round++) {
            InputQueue *queue = GetRoboInputQueue(8u);
            /* A device whose source agrees with the events it is stamped on: a pointer device for a pointer
             * event, because the door refuses the pair disagreeing. */
            InputDevice device = GetRoboInputDevice(1u, RoboInputSourceMouse, "Mouse");
            HtmlNodeHandle handle = { 1u, 2u, 1u };
            int index;

            for (index = 0; index < 8; index++) {
                InputEvent event = InputPointerMove(index, index, 0u, RoboInputSpaceDocument, 3u);
                InputTarget target;

                RoboCheckEqInt(InputTargetSetDocument(&target, 3u, &handle, (LayoutNodeId)index,
                                                                 RoboInputTargetFlagContent,
                                                                 RoboInputSpaceDocument, NULL),
                                  RoboOk);
                RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboOk);
                RoboCheckEqInt(InputEventSetDevice(&event, &device, NULL), RoboOk);
                RoboInputPush(queue, &event);
            }
            RoboCheckEqInt(InputQueueCount(queue), 8u);
            InputQueueDestroy(queue);
            RoboCheckEqInt((int)InputLiveBlocks(), (int)baseline);
        }
    }

    /* An event is a value with no storage of its own: taking one apart costs nothing and frees nothing. */
    {
        InputEvent event = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1,
                                                   RoboInputModifiersNone);
        InputEvent copy;

        RoboCheckEqInt(InputEventCopy(&copy, &event), RoboOk);
        RoboCheck(InputEventEqual(&event, &copy));
        RoboCheckEqInt((int)InputLiveBlocks(), (int)baseline);
    }

    /* A queue can be cleared, destroyed, and a new one made at a different capacity without anything of the
     * old one surviving. */
    {
        InputQueue *small = GetRoboInputQueue(2u);
        InputQueue *large;

        InputQueueDestroy(small);
        large = GetRoboInputQueue(16u);
        RoboCheckEqInt(InputQueueCapacity(large), 16u);
        RoboCheckEqInt(InputQueueCount(large), 0u);
        InputQueueDestroy(large);
        RoboCheckEqInt((int)InputLiveBlocks(), (int)baseline);
    }
}

void TestRegisterInputOwnership(void)
{
    TestRegister("input.ownership.snapshot", TestOwnershipSnapshot);
    TestRegister("input.ownership.target_snapshot", TestOwnershipTargetSnapshot);
    TestRegister("input.ownership.geometry", TestOwnershipGeometry);
    TestRegister("input.ownership.lifetime", TestOwnershipLifetime);
}
