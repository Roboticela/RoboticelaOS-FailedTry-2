/*
 * RoboInput debug suite (Phase 12.1) — `input.debug.*`.
 *
 * The dump is a *door*, not a `printf`: it is asserted whole, line by line, against the exact text this build
 * produces. Two consequences that are the point of the suite:
 *
 *   a field that stops being printed, or moves, fails here rather than being noticed by whoever was reading
 *   the text at the time -- which is why the pointer, key, text and target cases each compare a complete
 *   line instead of searching for a substring;
 *
 *   **determinism** is a case, not a claim. The same sequence dumped twice is byte-identical, dumped from two
 *   different queues is byte-identical, and nothing in the text is a function of an address -- the three
 *   properties a developer relies on when they diff two runs.
 *
 * `line_bound` is the fourth: the public constant `RoboInputDumpLineMax` is a promise about the longest
 * line this build can emit, so the widest event each payload kind can express is built and written through a
 * buffer of exactly that size.
 */

#include "RoboInputTestSupport.h"

#include <limits.h>

/* ------------------------------------------------------------------ */
/* One line, exactly                                                   */
/* ------------------------------------------------------------------ */

static void TestDebugPointerLine(void)
{
    InputEvent event = InputPointerMove(120, 84, 0u, RoboInputSpaceScreen, 3u);
    char line[RoboInputDumpLineMax];
    size_t length = 0u;

    RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
    RoboCheckStrEq(line,
                      "PointerMove source=mouse device=none window=3 display=0 time=0 modifiers=none "
                      "space=screen pointer=0 position=120,84 previous=none delta=0,0 buttons=none "
                      "button=none clicks=0 wheel=0,0 unit=line flags=primary");
    RoboCheckEqInt((int)length, (int)strlen(line));
    /* No newline inside a line: the only newline a writer emits is the one that ends a line of a *queue*. */
    RoboCheck(strchr(line, '\n') == NULL);

    /* A wheel with a device, a modifier, a previous position, a held button and a click count: every field
     * of the payload, in their fixed order. */
    {
        InputPointerData data;
        InputDevice device = GetRoboInputDevice(1u, RoboInputSourceMouse, "Mouse");

        InputPointerDataInit(&data);
        RoboCheckEqInt(InputPointerDataSet(&data, RoboInputPointerIdPrimary,
                                                     MakeGfxPoint(120, 84), RoboMouseButtonNone,
                                                     InputButtonMask(RoboMouseButtonLeft)),
                          RoboOk);
        RoboCheckEqInt(InputPointerDataSetPrevious(&data, MakeGfxPoint(118, 84)), RoboOk);
        RoboCheckEqInt(InputPointerDataSetWheel(&data, 0, 120, RoboMouseScrollUnitLine),
                          RoboOk);
        data.clickCount = 1;

        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventWheel,
                                                      RoboInputSourceMouse, &data,
                                                      RoboInputSpaceScreen, NULL),
                          RoboOk);
        InputEventSetWindow(&event, 3u);
        InputEventSetTimestamp(&event, 1841u);
        InputEventSetModifiers(&event, RoboKeyModCtrl, NULL);
        RoboCheckEqInt(InputEventSetDevice(&event, &device, NULL), RoboOk);

        RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
        RoboCheckStrEq(line,
                          "Wheel source=mouse device=1:Mouse window=3 display=0 time=1841 modifiers=control "
                          "space=screen pointer=0 position=120,84 previous=118,84 delta=2,0 buttons=left "
                          "button=none clicks=1 wheel=0,120 unit=line flags=primary");
    }

    /* A release names its button, and a mask with several buttons reads as the list a developer scans. */
    {
        InputPointerData data;

        InputPointerDataInit(&data);
        RoboCheckEqInt(InputPointerDataSet(&data, RoboInputPointerIdPrimary,
                                                     MakeGfxPoint(-5, 6), RoboMouseButtonX1,
                                                     InputButtonMask(RoboMouseButtonMiddle) |
                                                     InputButtonMask(RoboMouseButtonRight)),
                          RoboOk);
        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerUp,
                                                      RoboInputSourceMouse, &data,
                                                      RoboInputSpaceWindow, NULL),
                          RoboOk);
        RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
        RoboCheckStrEq(line,
                          "PointerUp source=mouse device=none window=0 display=0 time=0 modifiers=none "
                          "space=window pointer=0 position=-5,6 previous=none delta=0,0 "
                          "buttons=right+middle button=x1 clicks=0 wheel=0,0 unit=line flags=primary");
    }
}

static void TestDebugKeyLines(void)
{
    InputEvent event;
    InputKeyboardData key = InputKeyData(RoboPhysicalKeyA, RoboLogicalKeyA,
                                                    RoboKeyActionDown, 0, "a");
    char line[RoboInputDumpLineMax];
    size_t length = 0u;

    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown,
                                                   RoboInputSourceKeyboard, &key, NULL),
                      RoboOk);
    InputEventSetWindow(&event, 3u);
    InputEventSetModifiers(&event, RoboKeyModShift, NULL);
    RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
    /* `space=unknown` is the honest answer for a keystroke: it has no position, so it names no plane. */
    RoboCheckStrEq(line,
                      "KeyDown source=keyboard device=none window=3 display=0 time=0 modifiers=shift "
                      "space=unknown physical=A logical=A action=down repeat=0 flags=none text=\"a\"");

    /* A repeat and a physical identity the platform could not tell: the two flags, in one line. The
     * `MODIFIER` flag is derived from the *physical* key, which is exactly why it is absent here -- a key
     * whose position the platform did not report cannot be *known* to be a modifier, whatever its logical
     * identity says. */
    key = InputKeyData(RoboPhysicalKeyUnknown, RoboLogicalKeyShift, RoboKeyActionDown, 1, NULL);
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown,
                                                   RoboInputSourceKeyboard, &key, NULL),
                      RoboOk);
    RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
    RoboCheckStrEq(line,
                      "KeyDown source=keyboard device=none window=0 display=0 time=0 modifiers=none "
                      "space=unknown physical=unknown logical=Shift action=down repeat=1 "
                      "flags=physical-unknown text=\"\"");

    /* A key up carries no text, and the two keys a case is most likely to mix up read differently. */
    key = InputKeyData(RoboPhysicalKeyNumpad5, RoboLogicalKeyNumpad5, RoboKeyActionUp, 0, NULL);
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyUp,
                                                   RoboInputSourceKeyboard, &key, NULL),
                      RoboOk);
    RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
    RoboCheckStrEq(line,
                      "KeyUp source=keyboard device=none window=0 display=0 time=0 modifiers=none "
                      "space=unknown physical=Numpad5 logical=Numpad5 action=up repeat=0 flags=numpad "
                      "text=\"\"");

    /* Text input is an event with no payload but a string, and a target is the suffix a routed event adds. */
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetText(&event, RoboInputSourceKeyboard, "A", NULL), RoboOk);
    InputEventSetWindow(&event, 3u);
    RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
    RoboCheckStrEq(line,
                      "TextInput source=keyboard device=none window=3 display=0 time=0 modifiers=none "
                      "space=unknown text=\"A\"");
}

static void TestDebugTargetSuffix(void)
{
    InputEvent event;
    InputPointerData data;
    InputTarget target;
    HtmlNodeHandle handle = { 4u, 17u, 3u };
    char line[RoboInputDumpLineMax];
    size_t length = 0u;

    /* A cancel, so that the flag list has two entries and the type's own name says which phase ended. */
    InputPointerDataInit(&data);
    RoboCheckEqInt(InputPointerDataSet(&data, 1u, MakeGfxPoint(40, 50), RoboMouseButtonNone,
                                                 0u),
                      RoboOk);
    data.flags |= RoboInputPointerFlagCancelled;
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventTouchCancel,
                                                  RoboInputSourceTouch, &data,
                                                  RoboInputSpaceDocument, NULL),
                      RoboOk);
    InputEventSetWindow(&event, 3u);
    RoboCheckEqInt(InputTargetSetDocument(&target, 3u, &handle, 41u,
                                                    RoboInputTargetFlagText |
                                                    RoboInputTargetFlagContent,
                                                    RoboInputSpaceDocument, NULL),
                      RoboOk);
    RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboOk);

    RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
    RoboCheckStrEq(line,
                      "TouchCancel source=touch device=none window=3 display=0 time=0 modifiers=none "
                      "space=document pointer=1 position=40,50 previous=none delta=0,0 buttons=none "
                      "button=none clicks=0 wheel=0,0 unit=line flags=primary+cancelled target=document "
                      "target_window=3 target_node=4:17:3 target_layout=41 target_flags=text+content");

    /* A window target prints no node and no flags, because a window target does not carry them. */
    RoboCheckEqInt(InputTargetSetWindow(&target, 3u, RoboInputSpaceDocument, NULL), RoboOk);
    RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboOk);
    RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
    RoboCheck(strstr(line, " target=window target_window=3") != NULL);
    RoboCheck(strstr(line, "target_node") == NULL);
    RoboCheck(strstr(line, "target_flags") == NULL);

    /* An application target prints its application and nothing else. */
    RoboCheckEqInt(InputTargetSetApplication(&target, 7u, RoboInputSpaceDocument, NULL),
                      RoboOk);
    RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboOk);
    RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
    RoboCheck(strstr(line, " target=application target_app=7") != NULL);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

/** Push a fixed sequence of six events -- every payload kind and a target -- into @p queue. */
static void DebugSequence(InputQueue *queue)
{
    InputEvent event;
    InputTarget target;
    HtmlNodeHandle handle = { 1u, 4u, 1u };
    int index;

    for (index = 0; index < 4; index++) {
        event = InputPointerMove(index * 10, index * 20, 0u, RoboInputSpaceScreen, 3u);

        RoboCheckEqInt(InputEventSetTimestamp(&event, (uint64_t)index), RoboOk);
        RoboInputPush(queue, &event);
    }
    event = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone);
    RoboInputPush(queue, &event);
    RoboCheckEqInt(InputEventSetText(&event, RoboInputSourceKeyboard, "x", NULL), RoboOk);
    RoboInputPush(queue, &event);

    event = InputPointerMove(7, 7, 0u, RoboInputSpaceDocument, 3u);
    RoboCheckEqInt(InputTargetSetDocument(&target, 3u, &handle, 12u,
                                                    RoboInputTargetFlagContent,
                                                    RoboInputSpaceDocument, NULL),
                      RoboOk);
    RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboOk);
    RoboInputPush(queue, &event);
}

static void TestDebugDeterminism(void)
{
    InputQueue *first = GetRoboInputQueue(8u);
    InputQueue *second = GetRoboInputQueue(8u);
    char firstText[4096];
    char secondText[4096];
    size_t firstLength;
    size_t secondLength;
    size_t index;

    DebugSequence(first);
    DebugSequence(second);

    firstLength = InputQueueText(first, firstText, sizeof(firstText));
    secondLength = InputQueueText(second, secondText, sizeof(secondText));

    /* Two queues, the same sequence, byte-identical text: nothing in a dump is a function of an address or
     * of which object it came from. */
    RoboCheck(firstLength > 0u);
    RoboCheckEqInt((int)secondLength, (int)firstLength);
    RoboCheckStrEq(firstText, secondText);

    /* ... and dumping one queue twice is byte-identical too, which is the property a developer diffs. */
    RoboCheckEqInt((int)InputQueueText(first, secondText, sizeof(secondText)), (int)firstLength);
    RoboCheckStrEq(firstText, secondText);

    /* The summary line first, then one line per event, each terminated. */
    RoboCheck(strncmp(firstText, "queue capacity=8 count=7", 24) == 0);
    RoboCheckEqInt((int)InputQueueCount(first), 7u);
    {
        int newlines = 0;

        for (index = 0u; index < firstLength; index++) {
            if (firstText[index] == '\n') {
                newlines++;
            }
        }
        RoboCheckEqInt(newlines, 8);
    }
    RoboCheckEqInt(firstText[firstLength - 1u], '\n');

    /* `0x` is the one shape an address would take, and the dump never contains it. */
    RoboCheck(strstr(firstText, "0x") == NULL);

    /* Reading it does not change the queue: a dump is a look. */
    RoboCheckEqInt((int)InputQueueCount(first), 7u);

    InputQueueDestroy(first);
    InputQueueDestroy(second);
}

/* ------------------------------------------------------------------ */
/* The line bound, and refusing rather than truncating                  */
/* ------------------------------------------------------------------ */

static void TestDebugLineBound(void)
{
    char line[RoboInputDumpLineMax];
    size_t length = 0u;

    /* The widest pointer event this build can express: the most negative coordinates, a full button mask,
     * every flag, the longest unit name, the longest space name, the longest device name, every modifier
     * bit, and a document target whose ids are at their maximum. */
    {
        InputPointerData data;
        InputEvent event;
        InputTarget target;
        InputDevice device;
        char name[RoboInputDeviceNameMax];
        size_t index;

        for (index = 0u; index + 1u < RoboInputDeviceNameMax; index++) {
            name[index] = 'q';
        }
        name[RoboInputDeviceNameMax - 1u] = '\0';
        device = GetRoboInputDevice((InputDeviceId)-1, RoboInputSourceMouse, name);

        InputPointerDataInit(&data);
        RoboCheckEqInt(InputPointerDataSet(&data, (InputPointerId)-1,
                                                     MakeGfxPoint(INT32_MIN, INT32_MIN),
                                                     RoboMouseButtonRight,
                                                     (uint32_t)-1),
                          RoboInvalidArgument);
        {
            uint32_t all = 0u;
            int button;

            for (button = (int)RoboMouseButtonLeft; button <= (int)RoboMouseButtonX2; button++) {
                all = InputButtonMaskApply(all, (MouseButton)button, 1);
            }
            /* No button on the event itself: a cancel is not *about* one button, and its own button would
             * have to be absent from the mask it holds. */
            RoboCheckEqInt(InputPointerDataSet(&data, (InputPointerId)-1,
                                                         MakeGfxPoint(INT32_MIN, INT32_MIN),
                                                         RoboMouseButtonNone, all),
                              RoboOk);
        }
        /* A previous position at the same most-negative point: the delta is zero, which is the widest form
         * of the field that stays representable. (A previous position at the opposite end of the range would
         * be refused, because the delta between the two does not fit -- see `input.pointer.payload`.) */
        RoboCheckEqInt(InputPointerDataSetPrevious(&data,
                                                              MakeGfxPoint(INT32_MIN, INT32_MIN)),
                          RoboOk);
        RoboCheckEqInt(InputPointerDataSetWheel(&data, INT32_MIN, INT32_MIN,
                                                           RoboMouseScrollUnitPixel),
                          RoboOk);
        /* A click count cannot be negative -- the payload's own rule -- so the widest line carries the
         * largest count there is. */
        data.clickCount = INT32_MAX;
        data.flags = RoboInputPointerFlagAll;

        /* A cancel, because that is the type whose payload may carry all three flags at once: a press may not
         * be cancelled, so the widest *valid* line is the cancel's. */
        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerCancel,
                                                      RoboInputSourceMouse, &data,
                                                      RoboInputSpaceApplication, NULL),
                          RoboOk);
        InputEventSetWindow(&event, (WindowId)-1);
        InputEventSetDisplay(&event, (DisplayId)-1);
        InputEventSetTimestamp(&event, (uint64_t)-1);
        RoboCheckEqInt(InputEventSetModifiers(&event, RoboInputModifierAll, NULL), RoboOk);
        RoboCheckEqInt(InputEventSetDevice(&event, &device, NULL), RoboOk);
        {
            HtmlNodeHandle handle = { (HtmlDocumentId)-1, (HtmlNodeId)-1,
                                               (uint32_t)-1 };

            RoboCheckEqInt(InputTargetSetDocument(&target, (WindowId)-1, &handle,
                                                            (LayoutNodeId)-1, RoboInputTargetFlagAll,
                                                            RoboInputSpaceApplication, NULL),
                              RoboOk);
            RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboOk);
        }
        RoboCheck(InputEventIsValid(&event));

        /* Written through a buffer of exactly the stated bound: the promise holds for the widest line. */
        RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
        RoboCheck(length < (size_t)RoboInputDumpLineMax);
        RoboCheckEqInt((int)length, (int)strlen(line));
        /* The bound is comfortable rather than exact, and it is *stated* rather than described: three
         * quarters of it is already a line no honest event reaches. */
        RoboCheck(length < (size_t)(RoboInputDumpLineMax - 100u));

        /* A buffer one byte short of the line refuses, reports what it needed, and leaves a `?` rather than a
         * prefix that reads as a whole event. */
        {
            char small[RoboInputDumpLineMax];
            size_t needed = 0u;

            RoboCheckEqInt(InputEventWrite(&event, small, (size_t)length, &needed, NULL),
                              RoboNotSupported);
            RoboCheckEqInt((int)needed, (int)length);
            RoboCheckStrEq(small, "?");
            RoboCheckEqInt(InputEventWrite(&event, small, 1u, &needed, NULL), RoboNotSupported);
            RoboCheckStrEq(small, "?");
        }
    }

    /* A key event and a text event at their widest, for the same promise. */
    {
        InputEvent event;
        InputKeyboardData key;
        char text[RoboInputTextMax];
        size_t index;

        for (index = 0u; index + 1u < RoboInputTextMax; index++) {
            text[index] = 'w';
        }
        text[RoboInputTextMax - 1u] = '\0';

        key = InputKeyData(RoboPhysicalKeyUnknown, RoboLogicalKeyUnknown, RoboKeyActionDown, 1,
                             text);
        key.flags = RoboInputKeyFlagAll;
        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown,
                                                       RoboInputSourceRemote, &key, NULL),
                          RoboOk);
        RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
        RoboCheck(length < (size_t)RoboInputDumpLineMax);

        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetText(&event, RoboInputSourceRemote, text, NULL), RoboOk);
        RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), &length, NULL), RoboOk);
        RoboCheck(length < (size_t)RoboInputDumpLineMax);
    }

    /* Arguments: NULL everywhere is a refusal, and a refused dump leaves the length at what it needed. */
    {
        InputEvent event = InputPointerMove(1, 2, 0u, RoboInputSpaceScreen, 3u);
        size_t needed = 0u;

        RoboCheckEqInt(InputEventWrite(NULL, line, sizeof(line), &length, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputEventWrite(&event, NULL, sizeof(line), &length, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputEventWrite(&event, line, 0u, &length, NULL), RoboInvalidArgument);
        RoboCheckEqInt(InputEventWrite(&event, line, sizeof(line), NULL, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputQueueWrite(NULL, line, sizeof(line), &needed, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The queue dump                                                      */
/* ------------------------------------------------------------------ */

static void TestDebugQueueDump(void)
{
    InputQueue *queue = GetRoboInputQueue(4u);
    char text[4096];
    size_t length = 0u;
    InputEvent first = InputPointerMove(1, 2, 0u, RoboInputSpaceScreen, 3u);
    InputEvent second = InputKeyEvent(RoboPhysicalKeyB, RoboLogicalKeyB, 1,
                                                RoboInputModifiersNone);

    /* An empty queue writes its summary alone: a legitimate dump, not a refusal. */
    RoboCheckEqInt(InputQueueWrite(queue, text, sizeof(text), &length, NULL), RoboOk);
    RoboCheck(strncmp(text, "queue capacity=4 count=0", 23) == 0);
    RoboCheck(strstr(text, "pushed=0 popped=0 dropped=0 cleared=0 high_water=0") != NULL);
    RoboCheckEqInt((int)length, (int)strlen(text));

    RoboInputPush(queue, &first);
    RoboInputPush(queue, &second);
    {
        InputEvent out;

        RoboCheckEqInt(InputQueuePop(queue, &out, NULL), RoboOk);
    }
    RoboCheckEqInt(InputQueuePush(queue, &first, NULL), RoboOk);

    RoboCheckEqInt(InputQueueWrite(queue, text, sizeof(text), &length, NULL), RoboOk);
    RoboCheck(strncmp(text, "queue capacity=4 count=2 pushed=3 popped=1", 37) == 0);
    RoboCheck(strstr(text, "dropped=0 cleared=0 high_water=2") != NULL);
    /* One line per queued event, in FIFO order: the key event that is still queued, then the pointer event
     * pushed back on the end. */
    {
        const char *firstLine = strchr(text, '\n');

        RoboCheck(firstLine != NULL);
        RoboCheck(strncmp(firstLine + 1, "KeyDown ", 8) == 0);
        RoboCheck(strstr(firstLine + 1, "\nPointerMove source=mouse") != NULL);
        /* Nothing after the last event line: the text ends with the newline that terminated it. */
        RoboCheckEqInt(text[length - 1u], '\n');
    }

    /* A buffer too small for the whole text refuses and reports what the *whole* text needs, not what the
     * first line needs. */
    {
        char small[64];
        size_t needed = 0u;

        RoboCheckEqInt(InputQueueWrite(queue, small, sizeof(small), &needed, NULL),
                          RoboNotSupported);
        RoboCheckEqInt((int)needed, (int)length);
        RoboCheckStrEq(small, "?");
    }

    InputQueueDestroy(queue);
}

void TestRegisterInputDebug(void)
{
    TestRegister("input.debug.pointer_line", TestDebugPointerLine);
    TestRegister("input.debug.key_lines", TestDebugKeyLines);
    TestRegister("input.debug.target_suffix", TestDebugTargetSuffix);
    TestRegister("input.debug.determinism", TestDebugDeterminism);
    TestRegister("input.debug.line_bound", TestDebugLineBound);
    TestRegister("input.debug.queue_dump", TestDebugQueueDump);
}
