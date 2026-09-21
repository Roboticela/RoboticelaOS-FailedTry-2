/*
 * RoboInput bridge (Phase 12.1) — RoboWindow's events, translated into this model.
 *
 * ## Why this exists and is not the platform adapter
 *
 * §13 of the phase brief wants native input → a RoboInput event → the queue, and §12.1 does not implement
 * the first arrow: nothing in this build reads a keyboard, a mouse or a digitizer, and
 * `ROBO_INPUT_PLATFORM_ADAPTERS_IMPLEMENTED` is 0 to say so.
 *
 * What *does* exist today is RoboWindow: its backends (7.9–7.11) translate the platform's own input into
 * `robo_window_event_t` records with a key payload or a mouse payload, and push them into *its* queue. So
 * the second arrow is implementable now, and implementing it is what makes this phase's model reachable
 * rather than merely well-designed: **one function turns a window event into an input event**, and the
 * desktop's own pointer path can be expressed in the model without a device layer existing.
 *
 * The difference is worth keeping straight: an adapter *owns* a source (it knows which device, it can
 * enumerate them, it may have to open a handle). This is a **translation** -- pure, synchronous, no state,
 * and callable on any window event a caller already has. When Phase 12.2 adds a real adapter it will produce
 * the same events this bridge produces, with a device identity filled in; the model needs no change to
 * accept them, which is the test of whether the model is right.
 *
 * ## The two coordinate planes, and which one an event carries
 *
 * RoboWindow's mouse payload carries **both** planes on purpose: `client_x/y` (its name for this module's
 * *document* plane) and `screen_x/y` (its name for *screen*), with `has_screen_coords` saying whether the
 * second is real -- it is 0 when the backend could not convert. This module's rule is that an event carries
 * **one** plane, so the bridge has to choose, and it chooses the more precise one that is present: with
 * `has_screen_coords` set the event is in **screen** space, otherwise in **document** space.
 *
 * That choice is deliberate and reversible: `prefer_screen_coords` in the options turns it off, and a
 * caller that wants every bridged pointer event in client space can say so. What the bridge will **not** do
 * is invent the plane it lacks: a backend with no screen coordinates produces a document-space event, and
 * a consumer that needs screen space converts with a route it supplies (`RoboInputCoordinates.h`) rather
 * than reading `screen_x/y` and hoping.
 *
 * ## `previous` and the delta ambiguity, stated rather than hidden
 *
 * RoboWindow's `delta_x/y` is "movement since the previous tracked position, 0 on first sample **or after a
 * reset**". So a zero delta means either "the pointer did not move" or "this is the first report the
 * runtime has seen", and the window payload cannot distinguish them.
 *
 * The bridge therefore: copies the delta through unchanged, always; sets `previous = position - delta` and
 * `has_previous = 1` **only when the delta is non-zero**, where the arithmetic is provably right; and
 * leaves `has_previous = 0` when the delta is zero, where it would be guessing. A consumer that needs a
 * drag delta reads `delta_x/y` (always present); a consumer that needs a previous point checks
 * `has_previous` and finds the honest answer.
 *
 * ## What the bridge leaves empty, on purpose
 *
 *   **the text** of a key event: RoboWindow's keyboard payload has no text field at all -- that is exactly
 *   why this model has one -- so a bridged `KeyDown` carries `text=""`. Producing text is a later phase's
 *   work (12.8 / 14), and the keyboard payload's own comment says so;
 *
 *   **the device**, unless the caller supplies one through the options: this layer has no device registry to
 *   name a keyboard with, and inventing an id would be inventing a device. An empty device record is
 *   *valid* (`RoboInputTypes.h`), so a bridged event is a well-formed event with no device identity, which
 *   is the honest record of "something translated this, and nothing told us what device it came from";
 *
 *   **the target**: a window event says which window it is about, and that fills the event's `window` field;
 *   the *target* (a document node, an application) is what a dispatcher fills in after the hit test, which
 *   is the next phase's business.
 */
#ifndef ROBO_INPUT_BRIDGE_H
#define ROBO_INPUT_BRIDGE_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputKeyboard.h"
#include "RoboInput/RoboInputPointer.h"
#include "RoboInput/RoboInputQueue.h"
#include "RoboInput/RoboInputTypes.h"

#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindowMouse.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How the bridge translates. */
typedef struct robo_input_bridge_options {
    /** The source a mouse event becomes; `MOUSE` by default. */
    robo_input_source_t mouse_source;
    /** The source a key event becomes; `KEYBOARD` by default. */
    robo_input_source_t keyboard_source;
    /** Non-zero (the default) to use RoboWindow's screen coordinates when the payload has them. */
    int prefer_screen_coords;
    /** Stamped on translated mouse events; left empty (no id) by default. */
    robo_input_device_t mouse_device;
    /** Stamped on translated key events; left empty (no id) by default. */
    robo_input_device_t keyboard_device;
} robo_input_bridge_options_t;

/** Fill @p options with the defaults this header describes. Safe on NULL. */
void robo_input_bridge_options_init(robo_input_bridge_options_t *options);

/**
 * Non-zero when a window event of @p type has an equivalent in this model.
 *
 * True for the six input types (`KEY_DOWN`, `KEY_UP`, the three mouse button/move types and `MOUSE_WHEEL`)
 * and false for the fifteen lifecycle types -- a window being created, shown, moved, focused or closed is
 * the *window system's* business and is deliberately not an input event here (there is no input Focus event
 * in the vocabulary: focus belongs to a later phase, and the brief's §22 lists it as out of scope).
 */
int robo_input_bridge_can_translate(robo_window_event_type_t type);

/**
 * Translate @p window_event into @p out.
 *
 * @return ROBO_OK with a valid event in @p out;
 *         ROBO_INVALID_ARGUMENT for NULL arguments or an undefined/`NONE` window event type;
 *         ROBO_NOT_SUPPORTED with a `BRIDGE` diagnostic for a lifecycle window event (**nothing is written
 *         to @p out**, so a caller looping over a window queue cannot mistake a skipped event for a
 *         translated one);
 *         ROBO_NOT_SUPPORTED for a button event whose button is `UNKNOWN`, and for a `MOUSE_WHEEL` whose
 *         scroll unit is not a unit this build defines.
 *
 * The event's `window`, `display` and timestamp come from the payload (`robo_mouse_event_t.window_id` is
 * used when the window event's own id is `ROBO_WINDOW_ID_NONE`); its modifiers come from the payload, which
 * is what the window layer tracks; and its `space` is chosen as this header describes.
 */
robo_result_t robo_input_event_from_window_event(const robo_window_event_t *window_event,
                                                 robo_input_event_t *out,
                                                 robo_input_diag_t *diag);

/** As @ref robo_input_event_from_window_event, with the translation spelled out. A NULL @p options means
 *  the defaults. */
robo_result_t robo_input_event_from_window_event_with_options(
    const robo_input_bridge_options_t *options,
    const robo_window_event_t *window_event,
    robo_input_event_t *out,
    robo_input_diag_t *diag);

/**
 * Translate and enqueue in one call: the loop a consumer draining RoboWindow wants.
 *
 * @return ROBO_OK when an event was translated **and** pushed; the translation's own refusal (see above)
 *         when the window event has no equivalent, in which case nothing is pushed and @p *translated (when
 *         supplied) is set to 0; and the queue's refusal (`ROBO_BUSY` for a full queue) when it has one.
 * @param out_translated may be NULL; when supplied it is set to 1 only on a successful push.
 */
robo_result_t robo_input_queue_push_window_event(robo_input_queue_t *queue,
                                                 const robo_input_bridge_options_t *options,
                                                 const robo_window_event_t *window_event,
                                                 int *out_translated,
                                                 robo_input_diag_t *diag);

/**
 * Translate a bare keyboard payload.
 *
 * The action is carried across as the payload states it; the two key identities are copied; `is_repeat` is
 * normalized to 0/1 and forced to 0 for an up; `scan_code` becomes 0 (RoboWindow's payload carries none,
 * and a scan code is not an identity this model uses); and the **text is left empty**, for the reason in
 * this header's comment.
 *
 * Refused for NULL arguments or an action that is not down/up.
 */
robo_result_t robo_input_keyboard_data_from_window_keyboard(const robo_keyboard_event_t *source,
                                                            robo_input_keyboard_data_t *out,
                                                            robo_input_diag_t *diag);

/**
 * Translate a bare mouse payload.
 *
 * @param prefer_screen_coords chooses the plane exactly as @ref robo_input_event_from_window_event does.
 * @param out_space receives the plane the payload was written in, so the caller can put it on the event.
 *
 * The button mask is built from the runtime's **tracked** button state
 * (`robo_mouse_is_button_pressed`, 7.11) rather than from the event alone, because a move event does not
 * carry one and a mask of "nothing is down" on every move would be a lie a consumer could not detect. The
 * event's own button is then applied on top: a `BUTTON_DOWN` reports its bit, and a `BUTTON_UP` clears it,
 * so the mask is coherent with the event even if the window layer's state changed afterwards.
 *
 * Refused for NULL arguments, an unknown/`NONE` button on a button event, and an unknown scroll unit.
 */
robo_result_t robo_input_pointer_data_from_window_mouse(const robo_mouse_event_t *source,
                                                        int prefer_screen_coords,
                                                        robo_input_pointer_data_t *out,
                                                        robo_input_space_t *out_space,
                                                        robo_input_diag_t *diag);

/** The plane a mouse payload would be translated into, for a caller that wants to ask before it translates.
 *  `UNKNOWN` for a NULL payload. */
robo_input_space_t robo_input_bridge_space_for_window_mouse(const robo_mouse_event_t *source,
                                                            int prefer_screen_coords);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_BRIDGE_H */
