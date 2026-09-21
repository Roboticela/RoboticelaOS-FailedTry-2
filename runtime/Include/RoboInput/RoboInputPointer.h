/*
 * RoboInput pointer payload (Phase 12.1) — a pointer's place, its buttons, and its wheel.
 *
 * One payload for every *positioning* event, and the wheel is a *record inside it* rather than a payload of
 * its own. That is the decision this header is mostly about, and it has a cost worth stating: a wheel event
 * carries a position it may not need. What it buys is that **every positioned event in this model has the
 * same shape** -- a pointer, a place, a previous place, a button mask -- so a consumer that routes, hover
 * tests or hit tests does not switch on the type before it can read a coordinate. A separate wheel payload
 * would have to repeat all of those fields the day a trackpad reports a wheel at a point (which is every
 * trackpad), and until then would be the one positioned event with nowhere to say where it happened.
 *
 * ## Button state, in both directions
 *
 * `button` is the button this event is *about* (`ROBO_MOUSE_BUTTON_LEFT` on a press, `NONE` on a move), and
 * `buttons` is the **mask of buttons now down**, which is the field that makes an event self-describing: a
 * move with the left button held says so without the consumer having to remember the press. The mask uses
 * RoboWindow's button identities (`robo_mouse_button_t`, 7.11) with bit `n-1` for button `n`, and the two
 * helpers below are the only place that mapping is written.
 *
 * The mask is **normalized**: a mask bit for `NONE` or `UNKNOWN` can never be set, because
 * `robo_input_pointer_data_is_valid` refuses it. A caller who set bit 5 for "unknown" would otherwise get
 * an event that claims a button no screen has.
 *
 * ## Previous position, and why it is a field
 *
 * A move's `previous` is the pointer's position *before* this event, so that a consumer computing a drag
 * delta does not have to keep its own table -- and `has_previous` exists because a first sample has none,
 * which is not the same as one at `(0, 0)`. `delta_x`/`delta_y` are carried beside it because a platform
 * sometimes reports the delta and not the history (a raw device), and a consumer that had to derive one from
 * the other would silently mix the two sources. Where a platform reports neither, both stay zero and
 * `has_previous` is clear, which is the honest record.
 *
 * ## Coordinates
 *
 * Every position in this payload -- `position`, `previous` -- is in **one** space, the one on the event
 * (`robo_input_event_t.space`). It is not repeated in the payload on purpose: two places to say which plane
 * a coordinate is on is one place too many, and `RoboInputCoordinates.h` is where the plane is named and
 * converted.
 *
 * ## What is absent
 *
 * No hover/enter/leave (dispatch's business), no capture (RoboWindow owns soft capture today, 7.11, and a
 * capture model for the runtime is Phase 12.x), no caret, no gesture recognition, no pressure for a mouse, no
 * double-click *timing* (only the count the platform supplies, which is `click_count == 0` when it supplies
 * none), and no inertia or momentum. There is no `pointer_events`-style target policy: the hit test's own
 * rule (paint 11.10) is what decides which node a point is over.
 */
#ifndef ROBO_INPUT_POINTER_H
#define ROBO_INPUT_POINTER_H

#include "RoboBase/RoboError.h"

#include "RoboGraphics/RoboGraphicsGeometry.h"

#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTypes.h"

#include "RoboWindow/RoboWindowMouse.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The pointer left the window/system: the platform will report no more moves for it. */
#define ROBO_INPUT_POINTER_FLAG_CANCELLED ((uint32_t)(1u << 0))
/** This pointer is a **primary** pointer: the one a click without a pointer id means. */
#define ROBO_INPUT_POINTER_FLAG_PRIMARY ((uint32_t)(1u << 1))
/** The coordinates are **synthetic**: produced by a test or a script rather than reported by a device. */
#define ROBO_INPUT_POINTER_FLAG_SYNTHETIC ((uint32_t)(1u << 2))

/** Every defined pointer flag. */
#define ROBO_INPUT_POINTER_FLAG_ALL \
    (ROBO_INPUT_POINTER_FLAG_CANCELLED | ROBO_INPUT_POINTER_FLAG_PRIMARY | \
     ROBO_INPUT_POINTER_FLAG_SYNTHETIC)

/**
 * A wheel's deltas.
 *
 * `unit` is RoboWindow's (`ROBO_MOUSE_SCROLL_UNIT_LINE` / `_PIXEL`, 7.11), and reusing it is deliberate: a
 * high-resolution wheel reports fractions of a notch and a caller that treated those as pixels would scroll a
 * page by a hundred times too much. The sign convention is the one that header states -- positive `delta_y`
 * is "away from the user", which is scrolling down a document and up a value.
 */
typedef struct robo_input_wheel_data {
    /** Horizontal delta; sign as @ref unit. */
    int32_t delta_x;
    /** Vertical delta; sign as @ref unit. */
    int32_t delta_y;
    /** The unit the deltas are in. */
    robo_mouse_scroll_unit_t unit;
} robo_input_wheel_data_t;

/** Clear @p wheel. Safe on NULL. */
void robo_input_wheel_data_init(robo_input_wheel_data_t *wheel);

/** Non-zero when @p unit is a unit this build defines. */
int robo_input_scroll_unit_is_known(robo_mouse_scroll_unit_t unit);

/** Human-readable unit name (`"line"` / `"pixel"`); never NULL; `"unknown"` when undefined. */
const char *robo_input_scroll_unit_name(robo_mouse_scroll_unit_t unit);

/** One positioning event's payload, by value. */
typedef struct robo_input_pointer_data {
    /** Which pointer this is: `ROBO_INPUT_POINTER_ID_PRIMARY` for a mouse. */
    robo_input_pointer_id_t pointer;
    /** The button this event is about, or `NONE` for a move. */
    robo_mouse_button_t button;
    /** Mask of buttons now down: bit `n - 1` for button `n`. Never carries `NONE` or `UNKNOWN`. */
    uint32_t buttons;
    /** Where the pointer is, in the event's space. */
    robo_gfx_point_t position;
    /** Where it was before, in the event's space. Read when @ref has_previous. */
    robo_gfx_point_t previous;
    /** Non-zero when @ref previous is a position this event actually knows. */
    int has_previous;
    /** Movement since @ref previous, when the platform reports it directly; else 0. */
    int32_t delta_x;
    int32_t delta_y;
    /** Clicks this event completes (1 for a single click), or 0 when the platform does not say. */
    int click_count;
    /** Wheel deltas; all zero for anything but a wheel event. */
    robo_input_wheel_data_t wheel;
    /** `ROBO_INPUT_POINTER_FLAG_*` bits. */
    uint32_t flags;
} robo_input_pointer_data_t;

/** Clear @p data to "the primary pointer, at the origin, with nothing down". Safe on NULL. */
void robo_input_pointer_data_init(robo_input_pointer_data_t *data);

/**
 * Fill @p data with a position and a button state.
 *
 * The common case, and the only door that sets `position` and `buttons` together, so that a caller cannot
 * put a press in the mask without saying where it happened.
 *
 * @param pointer   the pointer id; `PRIMARY` for a mouse.
 * @param position  where it is, in the space the event will carry.
 * @param button    the button this event is about, or `NONE`.
 * @param buttons   the mask of buttons **now down**, which must *include* @p button for a press and must not
 *                  for a release; `robo_input_pointer_data_is_valid` is what refuses the two disagreeing, and
 *                  this door is where a caller sets them so that the pair is written in one place.
 * @return ROBO_INVALID_ARGUMENT for NULL @p data, an unknown @p button, or a mask with a bit this build does
 *         not define.
 */
robo_result_t robo_input_pointer_data_set(robo_input_pointer_data_t *data,
                                          robo_input_pointer_id_t pointer,
                                          robo_gfx_point_t position,
                                          robo_mouse_button_t button,
                                          uint32_t buttons);

/**
 * Record @p previous as where the pointer was, and derive @p delta_x/@p delta_y from the two positions.
 *
 * A previous position equal to the current one is legitimate (a stationary report) and yields a zero delta.
 * Refused with `ROBO_INVALID_ARGUMENT` for NULL, and with `ROBO_ERROR` -- **having written nothing** --
 * when the two positions differ by more than a delta can hold: two representable coordinates can, and a
 * wrapped delta is a movement in the wrong direction rather than a smaller one.
 */
robo_result_t robo_input_pointer_data_set_previous(robo_input_pointer_data_t *data,
                                                   robo_gfx_point_t previous);

/** Set the wheel record and its unit. Refused for a NULL @p data or an unknown @p unit. */
robo_result_t robo_input_pointer_data_set_wheel(robo_input_pointer_data_t *data,
                                                int32_t delta_x,
                                                int32_t delta_y,
                                                robo_mouse_scroll_unit_t unit);

/**
 * Non-zero when @p data is a payload this build may carry on a pointer, wheel or touch event.
 *
 * The rules: the button is known (or `NONE`, which is the honest value for a move); the mask carries only
 * bits that stand for a real button, so no bit for `NONE` or `UNKNOWN`; the wheel's unit is one this build
 * defines; `click_count` is non-negative; `has_previous` is 0 or 1; and the flags are defined bits. It does
 * **not** require a space -- the space lives on the event, and the event's validator is what refuses a
 * positioned event whose space is unset -- and it does not judge the deltas, because a wheel that scrolled
 * nowhere is a real thing a high-resolution device reports. Whether the payload *suits the event type it is
 * on* (a move with no button, a release whose button is no longer down, a touch contact with a button held)
 * is `robo_input_event_is_valid`'s business, in `RoboInputEvent.h`, one layer up.
 */
int robo_input_pointer_data_is_valid(const robo_input_pointer_data_t *data);

/** The mask bit for @p button, or 0 for `NONE` / `UNKNOWN` / an undefined button. */
uint32_t robo_input_button_mask(robo_mouse_button_t button);

/** Non-zero when @p mask has @p button down. 0 for `NONE` / `UNKNOWN`. */
int robo_input_button_mask_has(uint32_t mask, robo_mouse_button_t button);

/** @p mask with @p button set or cleared. Returns @p mask unchanged for `NONE` / `UNKNOWN`. */
uint32_t robo_input_button_mask_apply(uint32_t mask, robo_mouse_button_t button, int pressed);

/**
 * Write @p mask as the compact list a debug line reads: `none`, `left`, `left+right`.
 *
 * Lower case and unseparated by anything but `+`, matching the button names it is built from (RoboWindow's)
 * and the rest of this module's dumps -- so a reader scanning a line sees `buttons=left+right` beside
 * `button=none` rather than two conventions for the same vocabulary. An empty mask is `none`, which is the
 * same word every other empty list in a dump uses.
 *
 * `"?"` and the needed length when the buffer is too small, as every `_write` door here does.
 */
robo_result_t robo_input_button_mask_write(uint32_t mask, char *buffer, size_t capacity, size_t *out_length);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_POINTER_H */
