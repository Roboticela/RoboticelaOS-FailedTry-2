/*
 * RoboInput text pipeline (Phase 13.11/13.8) — from a key to a character, and never further.
 *
 * ## The distinction this file exists for
 *
 * The brief states it as a rule and it is the reason the module has two event types rather than one:
 *
 *   a **key event** means *this physical or logical key changed state*. It is what a shortcut, a game, a
 *   focus-navigation layer and a text field all observe, and it says nothing about characters;
 *
 *   a **text event** means *this text should be inserted into whatever is focused*. It is what an editor
 *   observes, and it says nothing about keys.
 *
 * `InputTextPipelineTranslate` is the one door that turns the first into the second, and it is a *pipeline*
 * rather than a line in a keyboard driver because two things sit between them that are not the driver's
 * business: the **layout** (which key means which character, 13.9) and **dead keys** (which character means
 * "wait for the next one", 13.10). What it deliberately does *not* do is insert anything: the `TEXT_INPUT`
 * events it produces are handed to whatever owns the focused editor, which is RoboText's (12.22).
 *
 * ## Why `Translate` can produce **two** events
 *
 * `dead acute` + `q` has no precomposed form, and the honest rendering of what the user typed is the acute's
 * spacing form **followed by** `q` -- two characters, which may be two events. A door that produced one
 * event would have to concatenate them into a fragment the event's bound might refuse, or drop one of the
 * two, and both of those are silently losing input. So the door fills a small array with a stated capacity
 * (`RoboInputTextMaxEvents`) and reports how many it produced, which is 0 for a chord that produces nothing.
 *
 * ## What produces no text
 *
 * Four cases, each a real one a caller must not read as a failure:
 *
 *   a **control chord**: `Ctrl` without `Alt`, or `Meta` (`InputModifiersIsControlChord`). The chord is a
 *   command; a runtime that also inserted a character would fire `Ctrl+C` and paste a `c` into the document;
 *
 *   a **key the layout does not map**: `F1`, an arrow, a modifier, or a level the layout has no entry for
 *   (AltGr on a US table). The key event still routes; it simply means no character;
 *
 *   an **up**: a release produces nothing, ever. It is accepted (rather than refused) so a caller can hand
 *   every key event to one door;
 *
 *   a **dead key alone**: it sets the pending accent and produces nothing *yet*. That is not a failure and
 *   not an error -- it is a key whose meaning is "the next one, modified", and the pending state is
 *   reported by `InputTextPipelinePendingDeadKey` rather than forced into an event that carries no text.
 *
 * ## The pending accent is state, and it is cancelled, never leaked
 *
 * One dead key is pending at a time, and it is cleared by composing, by being pressed twice (which emits its
 * spacing form -- that is how a user types an accent on its own), by an incompatible following key (which
 * emits the spacing form and then the character) and by `InputTextPipelineCancelDeadKey`. The last one is
 * the door focus loss, `Escape` and a device disconnect call, because an accent that survived a context
 * change would modify the first character typed into the *next* field.
 */
#ifndef ROBO_INPUT_TEXT_H
#define ROBO_INPUT_TEXT_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputLayout.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How many events one chord may produce. Two is the maximum, and `dead + incompatible base` is why. */
#define RoboInputTextMaxEvents 2u

/** A dead accent is waiting for the next key. */
typedef uint32_t InputDeadKeyId;

/** No accent is waiting. */
#define RoboInputDeadKeyIdNone ((InputDeadKeyId)0u)

/** The accent a dead key is waiting for, and when it began waiting. */
typedef struct InputDeadKeyState {
    /** The pending accent, or @ref RoboInputDeadKeyIdNone. */
    InputDeadKeyId id;
    /** The timestamp of the key that armed it, so a caller can time it out. Meaningful when @ref id is
     *  set. */
    uint64_t startedMs;
} InputDeadKeyState;

/**
 * Why a chord produced what it did.
 *
 * Read by a diagnostics page and asserted by the suite; nothing above branches on it, because the *events*
 * are the answer a consumer acts on.
 */
typedef enum InputTextOrigin {
    /** Nothing happened that produces text and nothing is pending. */
    RoboInputTextOriginNone = 0,
    /** A key the layout maps produced its character. */
    RoboInputTextOriginKey = 1,
    /** The key is the *same* dead key that was already pending: its spacing form was emitted, which is how
     *  a user types an accent on its own. */
    RoboInputTextOriginDeadKeyRepeated = 2,
    /** A dead key and a following key composed into one precomposed character. */
    RoboInputTextOriginComposed = 3,
    /** A dead key and a following key could **not** compose: the accent's spacing form and then the
     *  character, which is what the user typed. */
    RoboInputTextOriginDeadKeyRejected = 4,
    /** A dead key began waiting. No text, and none lost. */
    RoboInputTextOriginDeadKeyPending = 5,
    /** A following dead key replaced a pending one: the old accent's spacing form was emitted. */
    RoboInputTextOriginDeadKeyReplaced = 6
} InputTextOrigin;

/** Number of defined origins. Asserted positionally by `input.text.vocabulary`. */
#define RoboInputTextOriginCount 7u

/** Human-readable origin name; never NULL; `"unknown"` when undefined. */
const char *InputTextOriginName(InputTextOrigin origin);

/**
 * The pipeline: a borrowed layout, one pending accent, and four counters.
 *
 * The **layout is borrowed** and not copied -- a layout is a 10 KB table and a pipeline is a field of an
 * input session, so copying one into the other would be paying for the bound twice. A caller that changes
 * the layout under a live pipeline is changing what the *next* key means, which is exactly what a layout
 * switch is; the pipeline holds no mapping between any two keys, so there is nothing to invalidate.
 */
typedef struct InputTextPipeline {
    /** The layout to read, or NULL for "no layout": every key then produces nothing. */
    const InputLayout *layout;
    /** The accent, if any, that is waiting for the next key. */
    InputDeadKeyState dead;
    /** Chords that produced at least one event. Diagnostics. */
    uint64_t producedTotal;
    /** Chords that produced none. Diagnostics, and the four cases above are why it is not an error count. */
    uint64_t emptyTotal;
    /** Compositions that succeeded, and ones that fell back to two characters. Diagnostics. */
    uint64_t composedTotal;
    uint64_t rejectedTotal;
} InputTextPipeline;

/** Clear @p pipeline: no layout, no pending accent, zeroed counters. Safe on NULL. */
void InputTextPipelineInit(InputTextPipeline *pipeline);

/** Point @p pipeline at @p layout (may be NULL). The layout is **borrowed**; see this header's comment. */
Result InputTextPipelineSetLayout(InputTextPipeline *pipeline,
                                  const InputLayout *layout,
                                  InputDiag *diag);

/**
 * Turn one key event into zero, one or two `TEXT_INPUT` events.
 *
 * @param keyEvent     a `KEY_DOWN`, `KEY_UP` or `KEY_REPEAT`; anything else is refused, because a pipeline
 *                     that accepted a pointer event would have to decide what a click meant to a keyboard.
 * @param outEvents    an array of at least `RoboInputTextMaxEvents`; the capacity is checked, so a caller
 *                     cannot silently receive one of a pair.
 * @param outCapacity  how many events @p outEvents holds; fewer than `RoboInputTextMaxEvents` is refused.
 * @param outCount     receives 0, 1 or 2.
 * @param outOrigins   optional; receives **one** `InputTextOrigin` -- the *chord's*, whether or not it
 *                     produced anything -- so `DEAD_KEY_PENDING` is distinguishable from a chord that meant
 *                     nothing. One entry and not one per event on purpose: the origin is a property of
 *                     what the user pressed, and both events of a rejected accent share it, so a door that
 *                     wrote two would make every caller pass an array to answer a question with one
 *                     answer. It is a *report* rather than an answer: nothing above branches on it, and a
 *                     caller that does not want it passes NULL.
 *
 * The events carry the key event's **device, timestamp, modifiers and window**, and their own `TEXT_INPUT`
 * type with the produced text. They name **no plane** (a text event has no position) and are never targeted:
 * where the text goes is the focused editor's business, which this pipeline does not know.
 *
 * Refused with `RoboInvalidArgument` for a non-key event, a NULL argument or a short array. A refusal
 * produces no events and **changes nothing** -- including not consuming a pending accent.
 */
Result InputTextPipelineTranslate(InputTextPipeline *pipeline,
                                  const InputEvent *keyEvent,
                                  InputEvent *outEvents,
                                  uint32_t outCapacity,
                                  uint32_t *outCount,
                                  InputTextOrigin *outOrigins,
                                  InputDiag *diag);

/** The accent waiting for the next key, or @ref RoboInputDeadKeyIdNone. */
InputDeadKeyId InputTextPipelinePendingDeadKey(const InputTextPipeline *pipeline);

/** The timestamp of the key that armed the pending accent; 0 when none is pending. */
uint64_t InputTextPipelineDeadKeyStartedMs(const InputTextPipeline *pipeline);

/**
 * Drop the pending accent without emitting anything.
 *
 * What focus loss, `Escape`, a device disconnect and a window deactivation call: an accent that outlived
 * the field it was typed into would modify the first character typed into the next one. Returns non-zero
 * when there *was* one, so a caller can report it rather than assume.
 */
int InputTextPipelineCancelDeadKey(InputTextPipeline *pipeline);

/**
 * Room `InputTextPipelineWrite` needs, NUL included. A summary line, and nothing else.
 */
#define RoboInputTextPipelineDumpMax 256u

/**
 * Write @p pipeline as one line:
 *
 *     text layout=us-ascii dead=none produced=4 empty=2 composed=1 rejected=0
 *
 * `"?"` and the needed length when the buffer is too small.
 */
Result InputTextPipelineWrite(const InputTextPipeline *pipeline,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_TEXT_H */
