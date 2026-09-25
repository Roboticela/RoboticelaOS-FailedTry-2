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
#include "RoboInput/RoboInputClipboard.h"
#include "RoboInput/RoboInputCommand.h"
#include "RoboInput/RoboInputComposition.h"
#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputCursor.h"
#include "RoboInput/RoboInputDebug.h"
#include "RoboInput/RoboInputDevice.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputDispatch.h"
#include "RoboInput/RoboInputDragDrop.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputFocus.h"
#include "RoboInput/RoboInputGesture.h"
#include "RoboInput/RoboInputGamepad.h"
#include "RoboInput/RoboInputIme.h"
#include "RoboInput/RoboInputKeyboard.h"
#include "RoboInput/RoboInputKeyboardState.h"
#include "RoboInput/RoboInputLayout.h"
#include "RoboInput/RoboInputPen.h"
#include "RoboInput/RoboInputPointer.h"
#include "RoboInput/RoboInputPointerState.h"
#include "RoboInput/RoboInputQueue.h"
#include "RoboInput/RoboInputReplay.h"
#include "RoboInput/RoboInputSession.h"
#include "RoboInput/RoboInputShortcut.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputText.h"
#include "RoboInput/RoboInputTextBridge.h"
#include "RoboInput/RoboInputTouch.h"
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
    /**
     * **Dispatch** (13.29--13.34): the hit test, the path an event travels, the capture/target/bubble walk,
     * and the propagation context a listener writes to.
     *
     * Every dependency on the layer above is one function pointer -- the window, the tree, the listener
     * list -- so a caller with none of them still gets a stated answer rather than a refusal. What it does
     * **not** claim is that anything owns a tree: this module resolves no node by itself.
     */
    int dispatch;
    /**
     * **Focus** (13.25--13.28): one focused target per window scope, so bringing a window back restores
     * what it had, plus the transition record that names both ends in the order a listener must see them
     * (lose, then gain) and the navigation chain a caller declares and this layer walks.
     *
     * Focus is deliberately *not* hover, capture or window activation: four questions, four owners.
     */
    int focus;
    /** Text editing: a caret, a selection, an edit operation. **0** in 12.1. */
    int textEditing;
    /** Composition: dead keys, IME, a preedit string. **0** in 12.1. */
    int textComposition;
    /**
     * The **clipboard** (13.40/13.41): one fixed by-value text buffer, a monotonic serial, and the rule
     * that a copy of an **empty** selection leaves what is already there alone.
     *
     * The text is read through a function pointer (`InputClipboardTextSource`) because RoboText is not this
     * module's to include: a copy carries *characters*, and never a glyph id or a shaped buffer.
     */
    int clipboard;
    /** **Propagation** (13.32): `stopPropagation`, `stopImmediatePropagation` and `preventDefault`, kept
     *  in the dispatch context so a hardware event is never mutated by a listener. */
    int propagation;
    /**
     * **Gestures** (13.23/13.24): the sample a recognizer consumes, the arena that picks one winner per
     * sequence in the caller's priority order, and the heuristic recognizer this build ships for tap,
     * double-tap, long-press, drag, pan, pinch and rotate.
     *
     * They consume normalized pointer/touch streams and never bypass pointer routing.
     */
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

    /**
     * **Pointer state** (13.14/13.15): the live table of pointers a device has reported -- where each one
     * is, which buttons are down, which plane the position is in -- so a control can ask "is the pointer
     * still down over me?" without having watched every event since.
     *
     * It is a *state machine*, not a reader: `deviceReading` is still 0, and a caller tells this layer what
     * a pointer did rather than the layer asking hardware.
     */
    int pointerState;
    /**
     * **Pointer capture** (13.17): which target owns each pointer, so a drag that leaves its bounds still
     * delivers, with the rule that only the current holder may release and a destroyed target or a
     * deactivated window releases by identity.
     */
    int pointerCapture;
    /**
     * **Hover** (13.18): which target each pointer is over, and the leave/enter pair a change produces with
     * one invariant -- a target is never entered twice, and the redundancy is **counted** rather than
     * hidden.
     */
    int pointerHover;
    /**
     * **Click counting** (13.16) derived here rather than taken from the platform: the same pointer, the
     * same button, within a configured time and distance. A platform that reports its own count keeps it,
     * and a zero time window is how a caller says "every press is a single click".
     */
    int clickCounting;
    /**
     * **Wheel normalization** (13.19): line and pixel deltas converted to pixels through the line height
     * the caller supplies, with no opinion of this build's own about how tall a line is.
     */
    int wheelNormalization;

    /**
     * **Touch** (13.20/13.21): contacts keyed by the hardware's own id rather than an array index, a
     * cancellation that keeps the contact until it is acknowledged, and the one conversion into the pointer
     * model so a consumer that understands a mouse understands a tap.
     */
    int touchInput;
    /**
     * **Pen** (13.22): pressure, tilt, rotation, the barrel button and the eraser as *capability-gated*
     * fields, so a device that reports none of them is still a pen.
     *
     * It is the payload half only: `deviceReading` is still 0, and nothing surveys a stylus.
     */
    int penInput;
    /**
     * **Gamepad** (13.53): the button/axis vocabulary with normalized axes, as a payload and a state.
     *
     * `gamepadNavigation` (13.54) stays 0: D-pad-to-focus and A-to-activate are a *mapping* this build has
     * not made, and naming the difference is the point of the two flags.
     */
    int gamepad;
    /**
     * **Gamepad navigation** (13.54): the semantic map from a controller to focus moves and activation.
     * **0** until that mapping exists.
     */
    int gamepadNavigation;
    /**
     * The **cursor** (13.43): a shape per target resolved from the hit test by one owner, with the caller's
     * declarations beating the default table and the "same shape" case counted rather than re-applied.
     *
     * It does not draw one: this module owns no pixels, and the shape is a value the shell turns into an
     * image.
     */
    int cursorManagement;
    /**
     * **Drag and drop** (13.42): a session that owns its source and payload, the negotiation between what
     * the source offers and what the target accepts, the arrival/leave pair, the drop, and `Escape`'s
     * cancellation.
     *
     * It carries no bytes: the payload is an identity plus a preview, and the data behind it belongs to
     * whoever produced it -- the same seam the clipboard draws.
     */
    int dragDrop;
    /**
     * **Input methods** (13.37--13.39): the context an input method talks to -- the caret's rectangle, the
     * surrounding text a purpose permits, the composition doors, and the candidate rectangle anchored to
     * the caret.
     *
     * What it does **not** claim is an implemented input method or a candidate window: this layer owns the
     * interface, the client on the other side is the editor, and a candidate rectangle is a value a caller
     * draws.
     */
    int inputMethods;
    /**
     * The **command layer**'s shortcuts (13.35/13.36): scoped bindings walked innermost-first, where a
     * focused control wins an argument with a global one and "not mine" continues the walk while a real
     * failure stops it.
     */
    int shortcuts;
    /**
     * **Command routing** (13.36): a shortcut resolving to an `InputCommand` and that command reaching a
     * target chain, ending at the RoboText bridge for the editing commands.
     */
    int commandRouting;

    /**
     * **Hotplug** (13.44): connect and disconnect as transitions that release everything the device held --
     * keys, pointers, captures, touches, the repeat, the pending accent -- so no stale state survives it.
     * **0** until that sweep is built.
     */
    int deviceHotplug;
    /** **Event coalescing** (13.47): merging high-frequency moves while never merging a press, a release,
     *  a key or a text event. **0** until it is built. */
    int eventCoalescing;
    /** **Latency metrics** (13.48): the five stamps from hardware to present. **0** until they exist. */
    int inputLatency;
    /** **Replay** (13.49): recording a stream and feeding it back through the same pipeline. **0** until it
     *  exists. */
    int inputReplay;
    /** **Synthetic input** (13.50): trusted injection that still travels the normal routing path. **0** until
     *  it exists. */
    int syntheticInput;
    /** **Injection boundaries** (13.51): who may inject input, by privilege. **0** until they are stated in
     *  code rather than in a document. */
    int injectionBoundaries;
    /** **Accessibility hooks** (13.52): semantic commands an accessibility system invokes without
     *  pretending to be hardware. **0** until they exist. */
    int accessibilityHooks;
    /** **Input reset** (13.78): cancelling every transient state at once -- captures, gestures, the pending
     *  accent, the drag. **0** until the sweep exists. */
    int inputReset;

    /** Pointers one state table tracks (`RoboInputMaxPointers`). */
    uint32_t maxPointers;
    /** Captures one table holds (`RoboInputMaxCaptures`). */
    uint32_t maxCaptures;
    /** Scroll units this build defines (`RoboInputScrollUnitCount`). */
    uint32_t scrollUnitCount;

    /** Cursor shapes this build names (`RoboInputCursorTypeCount`). */
    uint32_t cursorTypeCount;
    /** Per-target cursor declarations one manager holds (`RoboInputMaxCursorOverrides`). */
    uint32_t maxCursorOverrides;
    /** Drag operations this build defines (`RoboInputDragOperationCount`). */
    uint32_t dragOperationCount;
    /** Drag phases this build defines (`RoboInputDragPhaseCount`). */
    uint32_t dragPhaseCount;
    /** Bytes of drag preview a payload carries (`RoboInputDragTextMax`). */
    uint32_t dragPreviewBytes;
    /** Text purposes this build defines (`RoboInputImeTextPurposeCount`). */
    uint32_t imePurposeCount;
    /** Clauses one composition may declare (`RoboInputImeMaxClauses`). */
    uint32_t imeMaxClauses;
    /** Highlights one clipboard entry names (`RoboInputClipboardFormatsMax`). */
    uint32_t clipboardFormatsMax;
    /** Formats this build names, `NONE` included (`RoboInputClipboardFormatCount`). */
    uint32_t clipboardFormatCount;

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
