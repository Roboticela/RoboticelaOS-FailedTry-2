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
 * `robo_window_id_t`, `robo_display_id_t`, `robo_html_node_handle_t` and `robo_layout_node_id_t` -- four
 * identities -- and calls **none** of those modules: no window is created, no tree is walked, no point is
 * hit-tested, no surface is painted. That is what keeps it a data model: every function here answers a
 * question about a *record*, and the one translation it performs (the bridge) is a pure function of a
 * record it was handed.
 *
 * The consequence is worth stating as a rule for later phases: **a dispatcher will be built on top of this
 * module, not inside it.** Filling in a target, converting a position into the plane a hit test wants, and
 * turning a hit test's answer into a document tip are the dispatcher's work, and this module gives it the
 * pieces -- `robo_input_event_position_in`, `robo_input_target_set_document`,
 * `robo_input_route_t` -- without doing any of it.
 *
 * ## No global state, no initialization, no lock
 *
 * There is no `robo_input_init`: the module owns nothing. A queue is an object a caller creates and
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
#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputDebug.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputKeyboard.h"
#include "RoboInput/RoboInputPointer.h"
#include "RoboInput/RoboInputQueue.h"
#include "RoboInput/RoboInputTarget.h"
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
const char *robo_input_version_string(void);

/** Fill the three version components. NULL means "do not want that one". */
robo_result_t robo_input_get_version(robo_i32 *out_major, robo_i32 *out_minor, robo_i32 *out_patch);

/** Non-zero when the event model exists in this build. See `ROBO_INPUT_MODEL_IMPLEMENTED`. */
int robo_input_model_is_implemented(void);

/** Non-zero when a platform input adapter exists in this build. See
 *  `ROBO_INPUT_PLATFORM_ADAPTERS_IMPLEMENTED` (0 for 12.1). */
int robo_input_platform_adapters_are_implemented(void);

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
typedef struct robo_input_capabilities {
    /** The event model: types, payloads, sources, devices, modifiers (12.1). */
    int event_model;
    /** Coordinate spaces and the four conversations between them, plus the generic door (12.1). */
    int coordinate_spaces;
    /** The routing target: window, application, display, document tip (12.1). */
    int target_identity;
    /** The owned FIFO queue with its statistics (12.1). */
    int event_queue;
    /** The deterministic one-line-per-event dump and the model dump (12.1). */
    int debug_dump;
    /** The RoboWindow bridge: `robo_window_event_t` → an input event (12.1). */
    int window_bridge;

    /** Reading a keyboard, a mouse, a touch digitizer or a game controller. **0** in 12.1. */
    int device_reading;
    /** A platform adapter that owns a source: enumeration, handles, lifecycle. **0** in 12.1. */
    int platform_adapters;
    /** Routing an event to a target, and telling anyone about it. **0** in 12.1. */
    int dispatch;
    /** Focus: which window, application or node receives keyboard input. **0** in 12.1. */
    int focus;
    /** Text editing: a caret, a selection, an edit operation. **0** in 12.1. */
    int text_editing;
    /** Composition: dead keys, IME, a preedit string. **0** in 12.1. */
    int text_composition;
    /** The clipboard. **0** in 12.1. */
    int clipboard;
    /** Propagation: capture, bubble, a listener list. **0** in 12.1. */
    int propagation;
    /** Gestures: tap, drag, pinch, long-press recognition. **0** in 12.1. */
    int gestures;
    /** An event loop, timers, microtasks. **0** in 12.1. */
    int event_loop;
    /** Input across processes (IPC). **0** in 12.1. */
    int multiprocess_input;

    /** Event types in the vocabulary (`ROBO_INPUT_EVENT_TYPE_COUNT`). */
    uint32_t event_type_count;
    /** Sources in the vocabulary (`ROBO_INPUT_SOURCE_COUNT`). */
    uint32_t source_count;
    /** Coordinate spaces in the vocabulary (`ROBO_INPUT_SPACE_COUNT`). */
    uint32_t space_count;

    /** The shipped version string. */
    const char *version;
} robo_input_capabilities_t;

/** Fill @p out. ROBO_INVALID_ARGUMENT for NULL; otherwise ROBO_OK, every field set. */
robo_result_t robo_input_get_capabilities(robo_input_capabilities_t *out);

/**
 * Room `robo_input_write_model` needs for the whole vocabulary, NUL included.
 *
 * One summary line plus one per event type, one per source and one per space: a count that only this module
 * knows, stated as a constant so a caller can size a buffer once instead of learning the length from a
 * refusal. `input.model.dump` asserts the dump fits and that the constant is the length's ceiling.
 */
#define ROBO_INPUT_MODEL_DUMP_MAX 2048u

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
 * is not a capabilities dump (`robo_input_get_capabilities`) and not an event dump
 * (`robo_input_event_write`): it answers "what can this model express?" rather than "what is this build
 * doing?" or "what happened?".
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a zero capacity; ROBO_NOT_SUPPORTED with a
 *         `DUMP` diagnostic when the text does not fit, with @p out_length set to the needed length.
 */
robo_result_t robo_input_write_model(char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_input_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_H */
