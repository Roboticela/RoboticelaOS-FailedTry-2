/*
 * RoboInput conformance suite (Phase 12.1) — `input.conformance.*` and `input.model.dump`.
 *
 * Three cases about *promises*, none of them about behaviour:
 *
 *   `vocabulary`     the numbered vocabularies are asserted **positionally**, value by value, because they
 *                    are the numbers a backend writes into a record and a later phase reads back. A type
 *                    inserted in the middle of the enum would silently reinterpret every event already
 *                    produced, so the suite pins each value and the count that goes with it;
 *
 *   `capabilities`   every field of the capability record is asserted by name, the six that are 1 and the
 *                    eleven that are 0. An untested capability flag is a flag that stops being true without
 *                    anybody noticing, and the zeros are the half that matters: a reader of this build is
 *                    told what it does **not** do rather than left to infer it;
 *
 *   `model.dump`     the vocabulary as text, asserted whole -- the summary line, the line count per
 *                    vocabulary, a sample line from each, and byte-for-byte determinism across two calls.
 *                    This is the artifact a developer reads first, so it is the one that has to be right.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestConformanceVocabulary(void)
{
    /* Event types, in the phase brief's order. */
    RoboCheckEqInt(RoboInputEventNone, 0);
    RoboCheckEqInt(RoboInputEventKeyDown, 1);
    RoboCheckEqInt(RoboInputEventKeyUp, 2);
    RoboCheckEqInt(RoboInputEventPointerMove, 3);
    RoboCheckEqInt(RoboInputEventPointerDown, 4);
    RoboCheckEqInt(RoboInputEventPointerUp, 5);
    RoboCheckEqInt(RoboInputEventPointerCancel, 6);
    RoboCheckEqInt(RoboInputEventWheel, 7);
    RoboCheckEqInt(RoboInputEventTextInput, 8);
    RoboCheckEqInt(RoboInputEventTouchBegin, 9);
    RoboCheckEqInt(RoboInputEventTouchMove, 10);
    RoboCheckEqInt(RoboInputEventTouchEnd, 11);
    RoboCheckEqInt(RoboInputEventTouchCancel, 12);
    /* Phase 13.2 appended twenty types. They are *appended* and never inserted (the header's rule), and
     * this case is what makes that rule checkable: a type put in the middle would shift every value below
     * it, and a recording that replayed those numbers would mean something else. */
    RoboCheckEqInt(RoboInputEventDeviceConnected, 13);
    RoboCheckEqInt(RoboInputEventDeviceDisconnected, 14);
    RoboCheckEqInt(RoboInputEventKeyRepeat, 15);
    RoboCheckEqInt(RoboInputEventPointerEnter, 16);
    RoboCheckEqInt(RoboInputEventPointerLeave, 17);
    RoboCheckEqInt(RoboInputEventPenDown, 18);
    RoboCheckEqInt(RoboInputEventPenMove, 19);
    RoboCheckEqInt(RoboInputEventPenUp, 20);
    RoboCheckEqInt(RoboInputEventPenCancel, 21);
    RoboCheckEqInt(RoboInputEventGamepadButtonDown, 22);
    RoboCheckEqInt(RoboInputEventGamepadButtonUp, 23);
    RoboCheckEqInt(RoboInputEventGamepadAxisChanged, 24);
    RoboCheckEqInt(RoboInputEventCompositionStart, 25);
    RoboCheckEqInt(RoboInputEventCompositionUpdate, 26);
    RoboCheckEqInt(RoboInputEventCompositionCommit, 27);
    RoboCheckEqInt(RoboInputEventCompositionCancel, 28);
    RoboCheckEqInt(RoboInputEventFocusIn, 29);
    RoboCheckEqInt(RoboInputEventFocusOut, 30);
    RoboCheckEqInt(RoboInputEventWindowActivate, 31);
    RoboCheckEqInt(RoboInputEventWindowDeactivate, 32);
    RoboCheckEqInt(RoboInputEventTypeCount, 33);

    /* Sources. */
    RoboCheckEqInt(RoboInputSourceUnknown, 0);
    RoboCheckEqInt(RoboInputSourceKeyboard, 1);
    RoboCheckEqInt(RoboInputSourceMouse, 2);
    RoboCheckEqInt(RoboInputSourcePointer, 3);
    RoboCheckEqInt(RoboInputSourceTouch, 4);
    RoboCheckEqInt(RoboInputSourceTablet, 5);
    RoboCheckEqInt(RoboInputSourceGameController, 6);
    RoboCheckEqInt(RoboInputSourceRemote, 7);
    RoboCheckEqInt(RoboInputSourceVirtual, 8);
    RoboCheckEqInt(RoboInputSourceCount, 9);

    /* Coordinate spaces. */
    RoboCheckEqInt(RoboInputSpaceUnknown, 0);
    RoboCheckEqInt(RoboInputSpaceScreen, 1);
    RoboCheckEqInt(RoboInputSpaceWindow, 2);
    RoboCheckEqInt(RoboInputSpaceApplication, 3);
    RoboCheckEqInt(RoboInputSpaceDocument, 4);
    RoboCheckEqInt(RoboInputSpaceCount, 5);

    /* Payload kinds and target kinds: the record-shape vocabularies. */
    RoboCheckEqInt(RoboInputPayloadNone, 0);
    RoboCheckEqInt(RoboInputPayloadKeyboard, 1);
    RoboCheckEqInt(RoboInputPayloadPointer, 2);
    RoboCheckEqInt(RoboInputPayloadText, 3);
    RoboCheckEqInt(RoboInputPayloadDevice, 4);
    RoboCheckEqInt(RoboInputPayloadPen, 5);
    RoboCheckEqInt(RoboInputPayloadGamepad, 6);
    RoboCheckEqInt(RoboInputPayloadComposition, 7);
    RoboCheckEqInt(RoboInputPayloadKindCount, 8);

    RoboCheckEqInt(RoboInputTargetNone, 0);
    RoboCheckEqInt(RoboInputTargetWindow, 1);
    RoboCheckEqInt(RoboInputTargetApplication, 2);
    RoboCheckEqInt(RoboInputTargetDocument, 3);
    RoboCheckEqInt(RoboInputTargetKindCount, 4);

    /* Diagnostic domains: which boundary refused. */
    RoboCheckEqInt(RoboInputDomainNone, 0);
    RoboCheckEqInt(RoboInputDomainEvent, 1);
    RoboCheckEqInt(RoboInputDomainSpace, 2);
    RoboCheckEqInt(RoboInputDomainQueue, 3);
    RoboCheckEqInt(RoboInputDomainTarget, 4);
    RoboCheckEqInt(RoboInputDomainBridge, 5);
    RoboCheckEqInt(RoboInputDomainDump, 6);
    RoboCheckEqInt(RoboInputDomainDevice, 7);
    RoboCheckEqInt(RoboInputDomainLayout, 8);
    RoboCheckEqInt(RoboInputDomainText, 9);
    RoboCheckEqInt(RoboInputDomainCommand, 10);
    RoboCheckEqInt(RoboInputDomainShortcut, 11);
    RoboCheckEqInt(RoboInputDomainPointer, 12);
    RoboCheckEqInt(RoboInputDomainFocus, 13);
    RoboCheckEqInt(RoboInputDomainDispatch, 14);
    RoboCheckEqInt(RoboInputDomainTouch, 15);
    RoboCheckEqInt(RoboInputDomainGesture, 16);
    RoboCheckEqInt(RoboInputDomainClipboard, 17);
    RoboCheckEqInt(RoboInputDomainCursor, 18);
    RoboCheckEqInt(RoboInputDomainDragDrop, 19);
    RoboCheckEqInt(RoboInputDomainIme, 20);
    RoboCheckEqInt(RoboInputDomainSession, 21);
    RoboCheckEqInt(RoboInputDomainReplay, 22);
    RoboCheckEqInt(RoboInputDomainAccessibility, 23);
    RoboCheckEqInt(RoboInputDomainGamepad, 24);
    RoboCheckEqInt(RoboInputDomainCount, 25);

    /* Every value of every vocabulary has a name, and no two types share one. The cleared value of the
     * *source*, *space*, *kind* and *domain* vocabularies is legitimately named "unknown", which is why the
     * check runs from 1 for those and from 0 for the event types. */
    {
        uint32_t index;

        for (index = 0u; index < RoboInputEventTypeCount; index++) {
            const char *name = InputEventTypeName((InputEventType)index);

            RoboCheck(name != NULL);
            RoboCheckStrEq(InputEventTypeName((InputEventType)index), name);
        }
        RoboCheckStrEq(InputEventTypeName(RoboInputEventNone), "None");
        RoboCheckStrEq(InputEventTypeName(RoboInputEventTouchCancel), "TouchCancel");
        RoboCheckStrEq(InputEventTypeName(RoboInputEventDeviceConnected), "DeviceConnected");
        RoboCheckStrEq(InputEventTypeName(RoboInputEventKeyRepeat), "KeyRepeat");
        RoboCheckStrEq(InputEventTypeName(RoboInputEventWindowDeactivate), "WindowDeactivate");
        RoboCheckStrEq(InputEventTypeName((InputEventType)33), "unknown");

        for (index = 1u; index < RoboInputSourceCount; index++) {
            RoboCheck(strcmp(InputSourceName((InputSource)index), "unknown") != 0);
        }
        RoboCheckStrEq(InputSourceName(RoboInputSourceUnknown), "unknown");
        RoboCheckStrEq(InputSourceName(RoboInputSourceGameController), "game-controller");

        for (index = 1u; index < RoboInputSpaceCount; index++) {
            RoboCheck(strcmp(InputSpaceName((InputSpace)index), "unknown") != 0);
        }
        RoboCheckStrEq(InputSpaceName(RoboInputSpaceDocument), "document");

        for (index = 1u; index < RoboInputPayloadKindCount; index++) {
            RoboCheck(strcmp(InputPayloadKindName((InputPayloadKind)index),
                              "unknown") != 0);
        }
        for (index = 1u; index < RoboInputTargetKindCount; index++) {
            RoboCheck(strcmp(InputTargetKindName((InputTargetKind)index), "unknown") !=
                       0);
        }
        for (index = 1u; index < RoboInputDomainCount; index++) {
            RoboCheck(strcmp(InputDomainName((InputDiagnosticDomain)index), "unknown") !=
                       0);
        }
        RoboCheckStrEq(InputDomainName(RoboInputDomainSpace), "space");
    }

    /* "Is it known?" agrees with the numbered range, at both ends and just outside. */
    RoboCheck(InputEventTypeIsKnown(RoboInputEventNone));
    RoboCheck(InputEventTypeIsKnown(RoboInputEventTouchCancel));
    RoboCheck(InputEventTypeIsKnown(RoboInputEventWindowDeactivate));
    RoboCheck(!InputEventTypeIsKnown((InputEventType)33));
    RoboCheck(!InputEventTypeIsDeliverable(RoboInputEventNone));
    RoboCheck(InputEventTypeIsDeliverable(RoboInputEventKeyDown));

    RoboCheck(!InputSourceIsKnown(RoboInputSourceUnknown));
    RoboCheck(!InputSourceIsKnown((InputSource)9));
    RoboCheck(!InputSpaceIsKnown(RoboInputSpaceUnknown));
    RoboCheck(!InputSpaceIsKnown((InputSpace)5));
    RoboCheck(!InputTargetKindIsKnown((InputTargetKind)4));
    RoboCheck(!InputDomainIsKnown((InputDiagnosticDomain)25));
    RoboCheckStrEq(InputDomainName(RoboInputDomainFocus), "focus");
    RoboCheckStrEq(InputDomainName(RoboInputDomainDispatch), "dispatch");
    RoboCheckStrEq(InputDomainName(RoboInputDomainTouch), "touch");
    RoboCheckStrEq(InputDomainName(RoboInputDomainGesture), "gesture");
    RoboCheckStrEq(InputDomainName(RoboInputDomainClipboard), "clipboard");
    RoboCheckStrEq(InputDomainName(RoboInputDomainCursor), "cursor");
    RoboCheckStrEq(InputDomainName(RoboInputDomainDragDrop), "drag-drop");
    RoboCheckStrEq(InputDomainName(RoboInputDomainIme), "ime");
    RoboCheckStrEq(InputDomainName(RoboInputDomainSession), "session");
    RoboCheckStrEq(InputDomainName(RoboInputDomainReplay), "replay");
    RoboCheckStrEq(InputDomainName(RoboInputDomainAccessibility), "accessibility");
    RoboCheckStrEq(InputDomainName(RoboInputDomainGamepad), "gamepad");
    RoboCheckStrEq(InputDomainName(RoboInputDomainLayout), "layout");
    RoboCheckStrEq(InputDomainName(RoboInputDomainText), "text");
    RoboCheckStrEq(InputDomainName(RoboInputDomainCommand), "command");
    RoboCheckStrEq(InputDomainName(RoboInputDomainShortcut), "shortcut");
    RoboCheckStrEq(InputDomainName(RoboInputDomainPointer), "pointer");

    /* Only three planes can be converted through: the application plane is named and never converted. */
    RoboCheck(InputSpaceIsConvertible(RoboInputSpaceScreen));
    RoboCheck(InputSpaceIsConvertible(RoboInputSpaceWindow));
    RoboCheck(InputSpaceIsConvertible(RoboInputSpaceDocument));
    RoboCheck(!InputSpaceIsConvertible(RoboInputSpaceApplication));
    RoboCheck(!InputSpaceIsConvertible(RoboInputSpaceUnknown));
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

static void TestConformanceCapabilities(void)
{
    InputCapabilities caps;

    RoboCheckEqInt(InputGetCapabilities(NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputGetCapabilities(&caps), RoboOk);

    /* What exists. */
    RoboCheckEqInt(caps.eventModel, 1);
    RoboCheckEqInt(caps.coordinateSpaces, 1);
    RoboCheckEqInt(caps.targetIdentity, 1);
    RoboCheckEqInt(caps.eventQueue, 1);
    RoboCheckEqInt(caps.debugDump, 1);
    RoboCheckEqInt(caps.windowBridge, 1);
    RoboCheckEqInt(caps.deviceTable, 1);
    RoboCheckEqInt(caps.keyboardState, 1);
    /* 13.9/13.10/13.11: the layout, its dead keys, and the one door from a key to `TEXT_INPUT`. */
    RoboCheckEqInt(caps.keyboardLayout, 1);
    RoboCheckEqInt(caps.deadKeys, 1);
    RoboCheckEqInt(caps.textInput, 1);
    /* 13.12/13.13: a command reaches an editor through the bridge, and the layer still owns no caret. */
    RoboCheckEqInt(caps.textEditing, 1);
    /* 13.14--13.19: the pointer state machine -- state, capture, hover, derived click counts and wheel
     * normalization. It routes nothing, which is why `dispatch` is still 0 below. */
    RoboCheckEqInt(caps.pointerState, 1);
    RoboCheckEqInt(caps.pointerCapture, 1);
    RoboCheckEqInt(caps.pointerHover, 1);
    RoboCheckEqInt(caps.clickCounting, 1);
    RoboCheckEqInt(caps.wheelNormalization, 1);
    /* 13.20/13.21 and 13.22: contacts keyed by the hardware's id, and the pressure/tilt a pen may report
     * (each gated by the device's own capability word rather than assumed present). */
    RoboCheckEqInt(caps.touchInput, 1);
    RoboCheckEqInt(caps.penInput, 1);
    /* 13.23/13.24: the gesture sample, the arena and the shipped recognizer. */
    RoboCheckEqInt(caps.gestures, 1);
    /* 13.25--13.28: one focused target per scope, the transition record and the navigation chain. */
    RoboCheckEqInt(caps.focus, 1);
    /* 13.29--13.36: the dispatcher, the propagation context a listener writes to, and the command layer's
     * scoped shortcuts. */
    RoboCheckEqInt(caps.dispatch, 1);
    RoboCheckEqInt(caps.propagation, 1);
    RoboCheckEqInt(caps.shortcuts, 1);
    RoboCheckEqInt(caps.commandRouting, 1);
    /* 13.40/13.41: the clipboard, whose copy of an *empty* selection leaves what is there alone. */
    RoboCheckEqInt(caps.clipboard, 1);
    /* 13.42 and 13.43: the drag session and the cursor's single owner. */
    RoboCheckEqInt(caps.dragDrop, 1);
    RoboCheckEqInt(caps.cursorManagement, 1);
    /* 13.53: the gamepad vocabulary. Its navigation mapping (13.54) is a *different* flag and still 0. */
    RoboCheckEqInt(caps.gamepad, 1);
    /* 13.37--13.39: the input-method *interface* -- the caret, the surrounding text a purpose permits, the
     * composition doors and the candidate rectangle. */
    RoboCheckEqInt(caps.inputMethods, 1);

    /* What deliberately does not, each one named so that a reader cannot mistake the model for a system. */
    RoboCheckEqInt(caps.deviceReading, 0);
    RoboCheckEqInt(caps.platformAdapters, 0);
    /*
     * A dead key is **not** a composition, and neither is an input-method *interface*: what stays 0 is the
     * pipeline from an input method's preedit into RoboText's composition, which is a caller's wiring rather
     * than this module's code.
     */
    RoboCheckEqInt(caps.textComposition, 0);
    /* D-pad to focus, A to activate: the gamepad vocabulary is here, the *mapping* is not. */
    RoboCheckEqInt(caps.gamepadNavigation, 0);
    RoboCheckEqInt(caps.eventLoop, 0);
    RoboCheckEqInt(caps.multiprocessInput, 0);
    /* 13.44/13.78: the session -- a device's sweep and the transient reset, which is what makes "nothing
     * stale remains" checkable rather than rhetorical. */
    RoboCheckEqInt(caps.deviceHotplug, 1);
    RoboCheckEqInt(caps.inputReset, 1);
    /* 13.49--13.51: recording, replay through the same pipeline, synthetic input, and the privilege
     * comparison behind the boundary. */
    RoboCheckEqInt(caps.inputReplay, 1);
    RoboCheckEqInt(caps.syntheticInput, 1);
    RoboCheckEqInt(caps.injectionBoundaries, 1);
    RoboCheckEqInt(RoboInputInjectionBoundariesImplemented, caps.injectionBoundaries);
    /* Named and not built, each its own row in the phase brief: coalescing and latency metrics, and the
     * accessibility command hooks (13.52). */
    RoboCheckEqInt(caps.eventCoalescing, 0);
    RoboCheckEqInt(caps.inputLatency, 0);
    RoboCheckEqInt(caps.accessibilityHooks, 0);

    RoboCheckEqInt(caps.eventTypeCount, RoboInputEventTypeCount);
    RoboCheckEqInt(caps.sourceCount, RoboInputSourceCount);
    RoboCheckEqInt(caps.spaceCount, RoboInputSpaceCount);
    RoboCheckEqInt(caps.deviceTypeCount, RoboInputDeviceTypeCount);
    RoboCheckEqInt(caps.deviceCapabilityCount, RoboInputDeviceCapabilityCount);
    RoboCheckEqInt(caps.layoutCount, RoboInputLayoutIdCount);
    RoboCheckEqInt(caps.layoutLevelCount, RoboInputLayoutLevelCount);
    RoboCheckEqInt(caps.deadKeyCount, RoboInputDeadKeyCount);
    RoboCheckEqInt(caps.textEventsPerChord, RoboInputTextMaxEvents);
    RoboCheckEqInt(caps.maxPointers, RoboInputMaxPointers);
    RoboCheckEqInt(caps.maxCaptures, RoboInputMaxCaptures);
    RoboCheckEqInt(caps.scrollUnitCount, RoboInputScrollUnitCount);
    RoboCheckEqInt(caps.cursorTypeCount, RoboInputCursorTypeCount);
    RoboCheckEqInt(caps.maxCursorOverrides, RoboInputMaxCursorOverrides);
    RoboCheckEqInt(caps.dragOperationCount, RoboInputDragOperationCount);
    RoboCheckEqInt(caps.dragPhaseCount, RoboInputDragPhaseCount);
    RoboCheckEqInt(caps.dragPreviewBytes, RoboInputDragTextMax);
    RoboCheckEqInt(caps.imePurposeCount, RoboInputImeTextPurposeCount);
    RoboCheckEqInt(caps.imeMaxClauses, RoboInputImeMaxClauses);
    RoboCheckEqInt(caps.clipboardFormatsMax, RoboInputClipboardFormatsMax);
    RoboCheckEqInt(caps.clipboardFormatCount, RoboInputClipboardFormatCount);
    RoboCheckStrEq(caps.version, RoboInputVersionString);
    RoboCheckStrEq(caps.version, InputVersionString());

    /* The flag constants and the door agree: `model_is_implemented` is the same fact as `eventModel`. */
    RoboCheckEqInt(InputModelIsImplemented(), caps.eventModel);
    RoboCheckEqInt(InputPlatformAdaptersAreImplemented(), caps.platformAdapters);
    RoboCheckEqInt(RoboInputApiFrozen, 0);

    /* Version components: numeric and string forms of one version. */
    {
        I32 major = -1;
        I32 minor = -1;
        I32 patch = -1;

        RoboCheckEqInt(InputGetVersion(&major, &minor, &patch), RoboOk);
        RoboCheckEqInt(major, RoboInputVersionMajor);
        RoboCheckEqInt(minor, RoboInputVersionMinor);
        RoboCheckEqInt(patch, RoboInputVersionPatch);
        /* NULL means "do not want that one", which is a legitimate request rather than an error. */
        RoboCheckEqInt(InputGetVersion(NULL, NULL, NULL), RoboOk);
    }
}

/* ------------------------------------------------------------------ */
/* The model dump                                                      */
/* ------------------------------------------------------------------ */

static void TestModelDump(void)
{
    char first[RoboInputModelDumpMax];
    char second[RoboInputModelDumpMax];
    size_t firstLength = 0u;
    size_t secondLength = 0u;
    size_t index;
    int typeLines = 0;
    int sourceLines = 0;
    int spaceLines = 0;
    int summaryLines = 0;

    RoboCheckEqInt(InputWriteModel(NULL, sizeof(first), &firstLength, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputWriteModel(first, 0u, &firstLength, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputWriteModel(first, sizeof(first), NULL, NULL), RoboInvalidArgument);

    RoboCheckEqInt(InputWriteModel(first, sizeof(first), &firstLength, NULL), RoboOk);
    RoboCheck(firstLength > 0u);
    /* The stated constant is the *ceiling*: a model dump that needed more than it would mean a caller who
     * sized a buffer from the header was misled. */
    RoboCheck(firstLength < (size_t)RoboInputModelDumpMax);
    RoboCheckEqInt((int)strlen(first), (int)firstLength);

    RoboCheckEqInt(InputWriteModel(second, sizeof(second), &secondLength, NULL), RoboOk);
    RoboCheckEqInt((int)secondLength, (int)firstLength);
    RoboCheck(strcmp(first, second) == 0);

    /* The summary line, whole, and first. */
    RoboCheck(strstr(first, "input 0.6.0 model=1 adapters=0 types=33 sources=9 spaces=5\n") == first);

    /* One line per vocabulary entry, and no line of one vocabulary masquerading as another. */
    for (index = 0u; index + 1u < firstLength; index++) {
        if (index == 0u || first[index] == '\n') {
            const char *line = first + (index == 0u ? index : index + 1u);

            if (strncmp(line, "type ", 5) == 0) {
                typeLines++;
            } else if (strncmp(line, "source ", 7) == 0) {
                sourceLines++;
            } else if (strncmp(line, "space ", 6) == 0) {
                spaceLines++;
            } else if (strncmp(line, "input ", 6) == 0) {
                summaryLines++;
            }
        }
    }
    RoboCheckEqInt(typeLines, (int)RoboInputEventTypeCount);
    RoboCheckEqInt(sourceLines, (int)RoboInputSourceCount);
    RoboCheckEqInt(spaceLines, (int)RoboInputSpaceCount);
    RoboCheckEqInt(summaryLines, 1);

    /* Sample lines: the shape of one from each vocabulary, with the facts a reader reads them for. */
    RoboCheck(strstr(first, "\ntype KeyDown payload=keyboard position=0 release=0 key=1 touch=0\n") != NULL);
    RoboCheck(strstr(first, "\ntype PointerMove payload=pointer position=1 release=0 key=0 touch=0\n") !=
               NULL);
    RoboCheck(strstr(first, "\ntype TouchCancel payload=pointer position=1 release=1 key=0 touch=1\n") !=
               NULL);
    RoboCheck(strstr(first, "\ntype None payload=none position=0 release=0 key=0 touch=0\n") != NULL);
    RoboCheck(strstr(first, "\nsource mouse positioning=1\n") != NULL);
    RoboCheck(strstr(first, "\nsource keyboard positioning=0\n") != NULL);
    RoboCheck(strstr(first, "\nsource game-controller positioning=1\n") != NULL);
    RoboCheck(strstr(first, "\nspace window convertible=1\n") != NULL);
    RoboCheck(strstr(first, "\nspace application convertible=0\n") != NULL);

    /* A buffer one byte short of the text reports the length it needed, and stores a `?` rather than a
     * prefix that reads as a whole dump. */
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputWriteModel(small, sizeof(small), &needed, NULL), RoboNotSupported);
        RoboCheckEqInt((int)needed, (int)firstLength);
        RoboCheckStrEq(small, "?");
    }
}

void TestRegisterInputConformance(void)
{
    TestRegister("input.conformance.vocabulary", TestConformanceVocabulary);
    TestRegister("input.conformance.capabilities", TestConformanceCapabilities);
    TestRegister("input.model.dump", TestModelDump);
}
