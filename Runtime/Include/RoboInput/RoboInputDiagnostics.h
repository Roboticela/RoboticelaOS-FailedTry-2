/*
 * RoboInput diagnostics (Phase 12.1).
 *
 * The record every module in this runtime uses -- a plain `Result` plus an optional caller-owned
 * record naming a domain, a stage and the reporting site -- with this module's own vocabulary.
 *
 * The domains are organised by **which boundary refused**, and that is the useful split here because an
 * input event crosses exactly four of them between the platform and a document:
 *
 *   **EVENT**    the event itself does not describe a thing that could have happened: an unknown type, a
 *                payload its type does not own, an action that disagrees with the type. The caller fixes
 *                the event;
 *   **SPACE**    the coordinates are not in a space this call can use: an unset space on a positioned
 *                event, or a conversion between two spaces whose geometry the caller did not supply. The
 *                caller fixes the *geometries it hands over* -- this module never invents one;
 *   **QUEUE**    the queue refused: a capacity that is not a capacity, a push into a full queue, a pop from
 *                an empty one. The caller fixes its own pacing;
 *   **TARGET**   the routing identity is malformed or incomplete: a document tip with no window, a window
 *                kind with no window id. The caller fixes what it filled in;
 *   **BRIDGE**   a RoboWindow event could not be translated: a type this model has no equivalent for, or a
 *                payload the window layer left unset. The caller learns that the gap is the *bridge's*
 *                rather than pretending the event was modelled;
 *   **DUMP**     the text a developer reads does not fit the buffer it was given;
 *   **DEVICE**   the device table refused, or a device's own description is inconsistent with itself. The
 *                caller fixes what it connected, or its own bookkeeping.
 *
 * A **SPACE** refusal is worth reading twice, because it is the one a caller is most likely to meet: this
 * module refuses to guess. An event whose positions are in an unset space is not "probably in window
 * space"; it is a record whose fields disagree about what it is, and the phase brief's rule -- input events
 * must not silently mix coordinate spaces -- is enforced by refusing rather than by assuming.
 */
#ifndef ROBO_INPUT_DIAGNOSTICS_H
#define ROBO_INPUT_DIAGNOSTICS_H

#include "RoboBase/RoboError.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Which boundary a RoboInput failure came from. */
typedef enum InputDiagnosticDomain {
    /** Nothing: a cleared record's value. */
    RoboInputDomainNone = 0,
    /** The event's own shape: its type, its payload, its action. */
    RoboInputDomainEvent = 1,
    /** The coordinate space of a position, or a conversion between two of them. */
    RoboInputDomainSpace = 2,
    /** The queue: its capacity, its contents, its pacing. */
    RoboInputDomainQueue = 3,
    /** The routing identity: window, application, display, document tip. */
    RoboInputDomainTarget = 4,
    /** The RoboWindow bridge: a window event this model cannot represent. */
    RoboInputDomainBridge = 5,
    /** The dump a developer reads. */
    RoboInputDomainDump = 6,
    /**
     * The device table and the devices in it: a descriptor that says two things at once, a table with no
     * room, a disconnect of an id that is not there, a hotplug cascade that cannot release what it must.
     * (Phase 13.2.) The caller fixes what it connected, or its own bookkeeping.
     */
    RoboInputDomainDevice = 7,

    /* ---- appended per phase, never inserted: a domain is a number a log line carries. ---- */

    /** The keyboard layout, its levels and its dead keys (13.9/13.10). */
    RoboInputDomainLayout = 8,
    /** Turning a key into text, and the pending accent (13.11). */
    RoboInputDomainText = 9,
    /** A command, its arguments, and the editor that executes it (13.12/13.13). */
    RoboInputDomainCommand = 10,
    /** A shortcut: its chord, its scope, and a conflict between two of them (13.35). */
    RoboInputDomainShortcut = 11,
    /**
     * Pointer state, click counting, capture and hover (13.14--13.19): a pointer table with no room, a
     * capture of a target that is not a target, a hover no pointer asked for. The caller fixes its own
     * bookkeeping; the layer kept its promise of never entering a target twice.
     */
    RoboInputDomainPointer = 12,
    /**
     * Focus (13.25--13.28): which target receives keyboard input, which window is active, and the
     * navigation walk. The caller fixes its own tree; the manager kept its promise of never leaving a
     * destroyed target focused.
     */
    RoboInputDomainFocus = 13,
    /**
     * Hit testing, the event path, the dispatcher and propagation (13.29--13.36): a point nobody claimed, a
     * path with no room, a listener asked in the wrong phase. The caller's tree and its listener list are
     * what to look at.
     */
    RoboInputDomainDispatch = 14,
    /**
     * Touch contacts (13.20/13.21): a move for an id nobody saw land, a contact table with no room, a finger
     * that never lifted. The caller fixes what it reported; the layer kept every contact's identity.
     */
    RoboInputDomainTouch = 15,
    /**
     * Gesture recognition and arbitration (13.23/13.24): a recognizer that claimed a sequence nobody offered,
     * an arena that ran out of room, a gesture that outlived its contacts. The caller's recognizers.
     */
    RoboInputDomainGesture = 16,
    /** The clipboard (13.40/13.41): a range that could not be read, text that did not fit, a paste into a
     *  destination that refused. The caller's editor is what to look at. */
    RoboInputDomainClipboard = 17,
    /** The cursor (13.43): a component that tried to drive the hardware cursor itself. The caller's target. */
    RoboInputDomainCursor = 18,
    /** Drag and drop (13.42): a session with no source, a drop on a target that refused the operation, data
     *  that did not fit. The caller's tree and payload. */
    RoboInputDomainDragDrop = 19,
    /** Input methods (13.37--13.39): a context with no client, a candidate position with no caret geometry,
     *  a composition that outlived its editor. The caller's editor is what to look at. */
    RoboInputDomainIme = 20,
    /** The session (13.44--13.48/13.77/13.78): a hotplug cascade that could not release what it must, a
     *  coalescing buffer with no room, a cancelled event nobody received, a latency record out of order. */
    RoboInputDomainSession = 21,
    /** Recording, replay and synthesis (13.49--13.51): a recording that ran out of room, a replay whose
     *  events no longer fit the model, an injection from a trust level that may not inject. */
    RoboInputDomainReplay = 22,
    /** Accessibility hooks (13.52): a semantic command from an accessibility client that could not be
     *  routed. The client's request is what to look at. */
    RoboInputDomainAccessibility = 23,
    /** The gamepad (13.53/13.54): an axis outside its range, a navigation mapping with no direction. The
     *  caller's mapping table. */
    RoboInputDomainGamepad = 24
} InputDiagnosticDomain;

/** Number of defined domains. Asserted positionally by `input.conformance.*`. */
#define RoboInputDomainCount 25u

/** Non-zero if @p domain is a defined domain. */
int InputDomainIsKnown(InputDiagnosticDomain domain);

/** Human-readable domain name; never NULL; `"unknown"` when undefined. */
const char *InputDomainName(InputDiagnosticDomain domain);

/** Caller-owned record. `message` is static and never freed; `file`/`line` name the reporting site. */
typedef struct InputDiag {
    Result code;
    InputDiagnosticDomain domain;
    const char *message; /* static; do not free */
    const char *file;
    int line;
} InputDiag;

/** Clear @p diag to success / empty. Safe on NULL. */
void InputDiagReset(InputDiag *diag);

/**
 * Fill @p diag. NULL @p diag -> RoboInvalidArgument.
 * @p code == RoboOk clears, like reset.
 */
Result InputDiagSet(InputDiag *diag,
                                  Result code,
                                  InputDiagnosticDomain domain,
                                  const char *message,
                                  const char *file,
                                  int line);

/** Non-strict variant used by the macro: always returns @p code, filling a non-NULL record. */
Result InputDiagRecord(InputDiag *diag,
                                     Result code,
                                     InputDiagnosticDomain domain,
                                     const char *message,
                                     const char *file,
                                     int line);

/** Implementation-site wrapper; evaluates to @p code whether or not @p diag is NULL. */
#define RoboInputDiag(diag, dom, code, msg) \
    InputDiagRecord((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_DIAGNOSTICS_H */
