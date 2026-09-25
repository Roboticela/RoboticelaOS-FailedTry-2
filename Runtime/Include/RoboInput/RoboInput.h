/*
 * RoboInput — the module's front door (Phase 12.1).
 *
 * One event model between whatever reported an input and whatever will decide what it means:
 *
 *     native input → [ platform adapter: Phase 12.2 ] → RoboInput event → RoboInput queue
 *                            ↑                                                      ↓
 *                     RoboWindow event                                   future dispatcher (12.2+)
 *                    (the bridge in this build)                                      ↓
 *                                                                       RoboWindow / application
 *                                                                                  ↓
 *                                                              RoboPaint hit test (11.10) → DOM (later)
 *
 * ## Where the module sits, and what it therefore may not do
 *
 * RoboInput is **below** RoboWindow and RoboPaint in behaviour and beside them in types. It names
 * `WindowId`, `DisplayId`, `HtmlNodeHandle` and `LayoutNodeId` -- four
 * identities -- and calls **none** of those modules: no window is created, no tree is walked, no point is
 * hit-tested, no surface is painted. That is what keeps it a data model: every function here answers a
 * question about a *record*, and the one translation it performs (the bridge) is a pure function of a
 * record it was handed.
 *
 * The consequence is worth stating as a rule for later phases: **a dispatcher will be built on top of this
 * module, not inside it.** Filling in a target, converting a position into the plane a hit test wants, and
 * turning a hit test's answer into a document tip are the dispatcher's work, and this module gives it the
 * pieces -- `InputEventPositionIn`, `InputTargetSetDocument`,
 * `InputRoute` -- without doing any of it.
 *
 * ## No global state, no initialization, no lock
 *
 * There is no `InputInit`: the module owns nothing. A queue is an object a caller creates and
 * destroys, an event is a value, and every conversion takes the geometry it needs as an argument. Two
 * callers in one process cannot interfere with each other, which is why nothing here is synchronized.
 *
 * ## The dependency direction, as a picture
 *
 *     RoboBase (errors, log)          RoboGraphics (points, rects)
 *              \                              /
 *               \                            /
 *                RoboInput ───── RoboMem (the queue's storage only)
 *                    │
 *        RoboWindow (the bridge, one .c file)
 *        RoboHTML / RoboLayout (two identity types, headers only)
 *
 * Nothing above this line depends on the module; the modules beside it do not know it exists.
 */
#ifndef ROBO_INPUT_H
#define ROBO_INPUT_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputBridge.h"
#include "RoboInput/RoboInputComposition.h"
#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputDebug.h"
#include "RoboInput/RoboInputDevice.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputGamepad.h"
#include "RoboInput/RoboInputKeyboard.h"
#include "RoboInput/RoboInputKeyboardState.h"
#include "RoboInput/RoboInputLayout.h"
#include "RoboInput/RoboInputPen.h"
#include "RoboInput/RoboInputPointer.h"
#include "RoboInput/RoboInputQueue.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputText.h"
#include "RoboInput/RoboInputTypes.h"
#include "RoboInput/RoboInputVersion.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The module                                                          */
/* ------------------------------------------------------------------ */

/** Shipped version, e.g. `"0.1.0"`. Static storage; do not free. */
const char *InputVersionString(void);

/** Fill the three version components. NULL means "do not want that one". */
Result InputGetVersion(I32 *outMajor, I32 *outMinor, I32 *outPatch);

/** Non-zero when the event model exists in this build. See `RoboInputModelImplemented`. */
int InputModelIsImplemented(void);

/** Non-zero when a platform input adapter exists in this build. See
 *  `RoboInputPlatformAdaptersImplemented` (0 for 12.1). */
int InputPlatformAdaptersAreImplemented(void);

/**
 * What this build's input module does and does not do.
 *
 * Read positionally or by name, never by count -- the device every module in this tree uses, and asserted
 * field-by-field by `input.conformance.capabilities`, because an untested capability flag is a flag that
 * stops being true without anybody noticing.
 *
 * The first block is what exists; the second is what deliberately does not, and each 0 is here so that a
 * reader of this build's capabilities cannot mistake a model for a system.
 */
typedef struct InputCapabilities {
    /** The event model: types, payloads, sources, devices, modifiers (12.1). */
    int eventModel;
    /** Coordinate spaces and the four conversations between them, plus the generic door (12.1). */
    int coordinateSpaces;
    /** The routing target: window, application, display, document tip (12.1). */
    int targetIdentity;
    /** The owned FIFO queue with its statistics (12.1). */
    int eventQueue;
    /** The deterministic one-line-per-event dump and the model dump (12.1). */
    int debugDump;
    /** The RoboWindow bridge: `WindowEvent` → an input event (12.1). */
    int windowBridge;

    /** Reading a keyboard, a mouse, a touch digitizer or a game controller. **0** in 12.1. */
    int deviceReading;
    /** A platform adapter that owns a source: enumeration, handles, lifecycle. **0** in 12.1. */
    int platformAdapters;
    /** Routing an event to a target, and telling anyone about it. **0** in 12.1. */
    int dispatch;
    /** Focus: which window, application or node receives keyboard input. **0** in 12.1. */
    int focus;
    /** Text editing: a caret, a selection, an edit operation. **0** in 12.1. */
    int textEditing;
    /** Composition: dead keys, IME, a preedit string. **0** in 12.1. */
    int textComposition;
    /** The clipboard. **0** in 12.1. */
    int clipboard;
    /** Propagation: capture, bubble, a listener list. **0** in 12.1. */
    int propagation;
    /** Gestures: tap, drag, pinch, long-press recognition. **0** in 12.1. */
    int gestures;
    /** An event loop, timers, microtasks. **0** in 12.1. */
    int eventLoop;
    /** Input across processes (IPC). **0** in 12.1. */
    int multiprocessInput;

    /** The device table: descriptors, capabilities and hotplug (13.2). */
    int deviceTable;
    /**
     * Keyboard **state**: which keys are held, the modifiers derived from them, the lock bits, and
     * auto-repeat driven by a caller's clock (13.2).
     *
     * What it claims is that a keyboard's state is *derived* rather than accumulated -- so a device
     * disconnect cannot leave a key or a modifier stuck -- and that a repeat is generated here rather than
     * by each control, one event per call, from a deadline the state pushes forward. It does **not** claim
     * that anything *reads* a keyboard: `deviceReading` is still 0.
     */
    int keyboardState;
    /**
     * The keyboard **layout** (13.9): a bounded table of `(key, level) → character`, the four levels, the
     * AltGr rule and the caps-lock rule, and the two tables this build ships (`US_ASCII`,
     * `US_INTERNATIONAL`).
     *
     * What it does **not** claim is a layout for any language whose table is not in this build: a layout is
     * data this module can hold and does not ship (13.9), and a caller supplies one through
     * `InputLayoutSetMapping`.
     */
    int keyboardLayout;
    /**
     * **Dead keys** as composition state (13.10): five accents with a spacing form, a combining form and a
     * precomposed-pair table, one accent pending at a time, and the four ways it is resolved -- compose,
     * repeat, replace and reject.
     *
     * It is *not* `textComposition` (below, still 0): dead keys are a property of a keyboard layout and need
     * no input method, while composition is an external method's preedit and its candidate window.
     */
    int deadKeys;
    /**
     * The **text pipeline** (13.11): the one door that turns a key event into zero, one or two `TEXT_INPUT`
     * events through the layout and the pending accent.
     *
     * What it claims is the *separation* the phase brief makes mandatory: a key event means "this key changed
     * state" and a text event means "this text should be inserted", and no consumer may read a scan code or
     * insert a character on `KeyDown`. What it does **not** claim is that the text reaches an editor --
     * `textEditing` is still 0, and 13.12/13.13 are the command layer that will carry it there.
     */
    int textInput;

    /** The layout ids this build ships (`RoboInputLayoutIdCount`). */
    uint32_t layoutCount;
    /** Layout levels this build defines (`RoboInputLayoutLevelCount`). */
    uint32_t layoutLevelCount;
    /** Dead keys this build defines, `NONE` included (`RoboInputDeadKeyCount`). */
    uint32_t deadKeyCount;
    /** Events one chord may produce (`RoboInputTextMaxEvents`). */
    uint32_t textEventsPerChord;

    /** The device-kind vocabulary's size (`RoboInputDeviceTypeCount`). */
    uint32_t deviceTypeCount;
    /** Device capability bits this build defines (`RoboInputDeviceCapabilityCount`). */
    uint32_t deviceCapabilityCount;

    /** Event types in the vocabulary (`RoboInputEventTypeCount`). */
    uint32_t eventTypeCount;
    /** Sources in the vocabulary (`RoboInputSourceCount`). */
    uint32_t sourceCount;
    /** Coordinate spaces in the vocabulary (`RoboInputSpaceCount`). */
    uint32_t spaceCount;

    /** The shipped version string. */
    const char *version;
} InputCapabilities;

/** Fill @p out. RoboInvalidArgument for NULL; otherwise RoboOk, every field set. */
Result InputGetCapabilities(InputCapabilities *out);

/**
 * Room `InputWriteModel` needs for the whole vocabulary, NUL included.
 *
 * One summary line plus one per event type, one per source and one per space: a count that only this module
 * knows, stated as a constant so a caller can size a buffer once instead of learning the length from a
 * refusal. `input.model.dump` asserts the dump fits and that the constant is the length's ceiling.
 */
#define RoboInputModelDumpMax 4096u

/**
 * Write what this module's model *is*: its vocabulary, in a fixed order, as text.
 *
 * The door a developer reads first and a test can assert whole -- the shape:
 *
 *     input 0.1.0 model=1 adapters=0 types=13 sources=9 spaces=5
 *     type KeyDown payload=keyboard position=0 release=0 key=1 touch=0
 *     source keyboard positioning=0
 *     space screen convertible=1
 *
 * One line per fact, each ending in a newline, nothing that varies between two runs of the same build. It
 * is not a capabilities dump (`InputGetCapabilities`) and not an event dump
 * (`InputEventWrite`): it answers "what can this model express?" rather than "what is this build
 * doing?" or "what happened?".
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a zero capacity; RoboNotSupported with a
 *         `DUMP` diagnostic when the text does not fit, with @p outLength set to the needed length.
 */
Result InputWriteModel(char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_H */
