/*
 * The cursor (Phase 13.43).
 *
 * ## One writer
 *
 * Every platform's cursor goes wrong the same way: each control sets it as its own pointer *enters* and none
 * clears it on the way out, so the shape left over depends on the path a user's mouse took. The fix is the one
 * this file states: **a caller declares what shape belongs to a target, and one manager decides the shape by
 * hit test, once per pointer move.** Nobody sets the cursor directly.
 *
 * ## The decision table
 *
 * `InputCursorForTarget` is that table, and it is a *default* rather than a law: a caller that wants a
 * different shape for a node registers it against the target and the registration wins. The default reads the
 * target's own flags -- a content target is a document node, and the document node's text flag decides
 * `text` -- so the shape follows what the target *is* rather than what it contains.
 *
 * ## What it does not do
 *
 * It does not draw a cursor. This module owns no pixels; the shape is a value the shell turns into an image,
 * and a caller that wanted a custom image would add a variant here rather than a drawing call.
 *
 * See `Documentation/Runtime/Input/Cursor.md`.
 */

#ifndef ROBO_INPUT_CURSOR_H
#define ROBO_INPUT_CURSOR_H

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How many per-target overrides one manager holds. */
#define RoboInputMaxCursorOverrides 16u

/** The shapes this build names. */
typedef enum InputCursorType {
    /** The arrow. **Not** `NONE`: a cleared record's value is a shape a shell can draw. */
    RoboInputCursorDefault = 0,
    /** A hand: the target is activatable. */
    RoboInputCursorPointer = 1,
    /** An I-beam: the target is text. */
    RoboInputCursorText = 2,
    /** A crosshair: a drawing or measuring surface. */
    RoboInputCursorCrosshair = 3,
    /** Four-way arrows: something movable. */
    RoboInputCursorMove = 4,
    /** A horizontal split: a horizontal edge drag. */
    RoboInputCursorResizeHorizontal = 5,
    /** A vertical split. */
    RoboInputCursorResizeVertical = 6,
    /** A diagonal split. */
    RoboInputCursorResizeDiagonal = 7,
    /** A slashed circle: the drop is refused. */
    RoboInputCursorNotAllowed = 8,
    /** A busy indicator. */
    RoboInputCursorWait = 9,
    /** A busy indicator with the arrow still usable. */
    RoboInputCursorProgress = 10,
    /** **No shape at all**: the cursor is hidden. Distinct from the default. */
    RoboInputCursorNone = 11,
    /**
     * A closed hand: something is being *carried* -- a window by its title bar, a drag in flight
     * (Phase 13.75).
     *
     * Distinct from @ref RoboInputCursorMove, which says a thing *can* be moved; this says one *is* moving,
     * and the shell declares it as an override for the duration of the gesture. The default table never
     * answers it, because no *target* is being grabbed -- a gesture is.
     */
    RoboInputCursorGrabbing = 12
} InputCursorType;

/** Number of defined shapes. Asserted positionally by `input.cursor.*`. */
#define RoboInputCursorTypeCount 13u

/** Non-zero if @p type is a defined shape. */
int InputCursorTypeIsKnown(InputCursorType type);

/** Human-readable shape name; never NULL; `"unknown"` when undefined. */
const char *InputCursorTypeName(InputCursorType type);

/** Non-zero when @p type means "hide the cursor" rather than "draw this". */
int InputCursorTypeIsHidden(InputCursorType type);

/**
 * The default shape for @p target: the table, in one function.
 *
 * `DOCUMENT` with the text flag is `TEXT`; `DOCUMENT` without it is `DEFAULT`; a `WINDOW` is `DEFAULT`
 * (a window's own chrome is the shell's business, and it registers an override where it cares); an
 * `APPLICATION` is `DEFAULT`; nothing at all is `DEFAULT`, because a cursor cannot be "about nothing" -- it is
 * always somewhere on a screen.
 */
InputCursorType InputCursorForTarget(const InputTarget *target);

/** One target's declared shape. */
typedef struct InputCursorOverride {
    /** Which target. */
    InputTarget target;
    /** What shape it asks for. */
    InputCursorType type;
    /** Non-zero when this slot is live. */
    int active;
} InputCursorOverride;

/**
 * The manager: what shape is in force, and why.
 *
 * `accepted` counts updates that actually changed the shape. That number is the one a caller watches for a
 * loop: a run that accepts once per pointer move is a run that changed the cursor on every move.
 */
typedef struct InputCursorManager {
    /** The overrides, in registration order. |
     *  A later registration for the same target replaces the earlier one. */
    InputCursorOverride overrides[RoboInputMaxCursorOverrides];
    /** Live overrides. */
    uint32_t count;
    /** The shape in force. */
    InputCursorType current;
    /** The target that produced @ref current; meaningless when @ref hasTarget is 0. */
    InputTarget source;
    /** Non-zero when @ref source is a live target. */
    int hasTarget;
    /** How many times a hit test changed the shape. */
    uint32_t accepted;
    /** Hit tests that resolved to the same shape as the last one. */
    uint32_t unchanged;
    /** Refusals: an invalid target, a table with no room. */
    uint32_t refused;
} InputCursorManager;

/** Clear @p manager to "the default arrow, from nothing". Safe on NULL. */
void InputCursorManagerInit(InputCursorManager *manager);

/**
 * Declare @p target's shape.
 *
 * A target that already has one has it **replaced** rather than added: two shapes for one target would make
 * the answer depend on the registration order, which is a rule nobody could read.
 */
Result InputCursorManagerSetOverride(InputCursorManager *manager,
                                     const InputTarget *target,
                                     InputCursorType type,
                                     InputDiag *diag);

/** Drop @p target's declaration. Returns non-zero when one was live. */
int InputCursorManagerRemoveOverride(InputCursorManager *manager, const InputTarget *target);

/**
 * Decide the shape for the target a pointer move resolved to.
 *
 * @p target may be NULL for "the pointer is over nothing in this window", which resolves to `DEFAULT` -- a
 * blank area of a shell is still part of the shell. Returns the shape **in force**, which is
 * @ref InputCursorManager::current; @p acceptedThisTime (may be NULL) receives whether it changed.
 */
InputCursorType InputCursorManagerUpdate(InputCursorManager *manager,
                                         const InputTarget *target,
                                         int *acceptedThisTime);

/** The shape in force, and the target that produced it (NULL when none). */
InputCursorType InputCursorManagerCurrent(const InputCursorManager *manager);

/** The target that produced the current shape, or NULL. */
const InputTarget *InputCursorManagerSource(const InputCursorManager *manager);

/** Forget every override; the shape in force stays. Returns how many overrides went. */
uint32_t InputCursorManagerClearOverrides(InputCursorManager *manager);

/** One complete line for @p manager, or @ref RoboNotSupported when it does not fit. */
Result InputCursorManagerWrite(const InputCursorManager *manager,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_CURSOR_H */
