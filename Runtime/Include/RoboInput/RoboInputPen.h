/*
 * RoboInput pen payload (Phase 13.22) — a stylus, and the three facts a finger cannot report.
 *
 * ## Why the pointer payload is *inside* this one
 *
 * A pen is a pointer: it has a place, a previous place, buttons (a barrel button, a tip contact) and a
 * selection of which pointer it is. Everything that already works on a pointer -- the plane a coordinate is
 * on, the four conversions, the button mask, the hit test a dispatcher performs -- must work on a pen event
 * without a second implementation, and the cheapest way to guarantee that is for the pen payload to
 * *contain* the pointer payload rather than to repeat its fields.
 *
 * So `InputPenData.pointer` is the first member, and every door that reads a position, a mask or a
 * previous place takes `&data->pointer`. The consequence worth stating: a pen event is a **positioned**
 * event (`InputEventTypeHasPosition`), it names a space exactly like a move, and `InputEventPosition`
 * answers for it -- there is one coordinate model, and a stylus is not an exception to it.
 *
 * The cost of the nesting is that `sizeof(InputPenData)` is the pointer payload plus four words, and the
 * event union therefore carries the largest of its members. That is the trade this module makes everywhere:
 * an event is a value with everything in it, rather than a pointer to something the queue would have to
 * own.
 *
 * ## Capability, not presence
 *
 * A digitizer that reports no pressure, one that reports pressure and no tilt, and one that reports all
 * three are all pens, and the difference is not a defect in any of them. So the fields are always present
 * and their *meaning when absent* is stated once:
 *
 *   `pressure`  0 when the device does not report it, and 0 also means "no contact" -- which is why
 *               `RoboInputPenDataIsValid` does not refuse a zero. A hover sample from a pen that *does*
 *               report pressure is legitimately 0, and refusing it would make hovering impossible;
 *
 *   `tiltX/Y`   both 0 when unreported, and a legitimate pen writing perpendicular to the screen also
 *               reports 0. There is no way to tell those apart from the values alone, and there is
 *               deliberately no flag pretending otherwise: the *descriptor's* `TILT` capability is where a
 *               consumer asks whether the hardware has it (RoboInputDevice.h);
 *
 *   `rotation`  0 when unreported. For an ordinary pen it stays 0; the field exists because a barrel or an
 *               art pen has it and a model that could not carry it would have to be widened later.
 *
 * That split -- "what this device can report" on the descriptor, "what it reported this time" on the
 * payload -- is the same one `RoboInputPointer.h` makes about pressure for a mouse, and it is the reason
 * neither this header nor that one guesses.
 *
 * ## Flags, not values
 *
 * Four facts about a pen that are not numbers: the barrel button is down, the tip is the eraser end, the
 * pen is inverted (held eraser-first), and this sample is a hover (in range, not touching). They are bits
 * because each is a yes-or-no about the sample rather than a magnitude, and because a consumer that wants
 * "a pen is near the surface" asks one question rather than comparing a pressure to zero.
 */
#ifndef ROBO_INPUT_PEN_H
#define ROBO_INPUT_PEN_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputPointer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The barrel button (or the primary side button) is down. */
#define RoboInputPenFlagBarrel ((uint32_t)(1u << 0))
/** The pen is inverted: the eraser end is what is touching. */
#define RoboInputPenFlagEraser ((uint32_t)(1u << 1))
/** The pen is in range but not touching the surface: a hover sample. */
#define RoboInputPenFlagHover ((uint32_t)(1u << 2))
/** The device reports no pressure for this sample, and the zero is not a measurement. */
#define RoboInputPenFlagPressureUnknown ((uint32_t)(1u << 3))

/** Every defined pen flag. */
#define RoboInputPenFlagAll                                                              \
    (RoboInputPenFlagBarrel | RoboInputPenFlagEraser | RoboInputPenFlagHover |            \
     RoboInputPenFlagPressureUnknown)

/** Pressure is in tenths of a percent of full scale: 0 (none) to 1000 (full). */
#define RoboInputPenPressureMax 1000

/** Tilt is in degrees from perpendicular, so the useful range is -90..90 on each axis. */
#define RoboInputPenTiltMin (-90)
#define RoboInputPenTiltMax 90

/** Rotation is in degrees, 0..359. */
#define RoboInputPenRotationMax 359

/** One pen sample, by value: the pointer sample it is, plus what a stylus knows and a finger does not. */
typedef struct InputPenData {
    /** Where this pen is, which buttons the *pointer* side reports, and where it was before. The first
     *  member on purpose: every pointer door takes this by address (see this header's comment). */
    InputPointerData pointer;
    /** Pressure, 0..`RoboInputPenPressureMax`; 0 when the device does not report it. */
    int32_t pressure;
    /** Tilt from perpendicular along x, in degrees; 0 when unreported. */
    int32_t tiltX;
    /** Tilt from perpendicular along y, in degrees; 0 when unreported. */
    int32_t tiltY;
    /** Barrel rotation in degrees, 0..`RoboInputPenRotationMax`; 0 when unreported. */
    int32_t rotation;
    /** `RoboInputPenFlag*` bits. */
    uint32_t flags;
} InputPenData;

/** Clear @p data: the primary pointer where the pointer payload starts, no pressure, no tilt, nothing set.
 *  Safe on NULL. */
void InputPenDataInit(InputPenData *data);

/**
 * Fill @p data from a pointer sample and the pen facts.
 *
 * @param pointer  the position and button state, exactly as a move would carry them
 *                 (`InputPointerDataSet` built it); must be valid.
 * @param pressure 0..`RoboInputPenPressureMax`.
 * @param tiltX    `RoboInputPenTiltMin`..`RoboInputPenTiltMax`.
 * @param tiltY    `RoboInputPenTiltMin`..`RoboInputPenTiltMax`.
 * @param rotation 0..`RoboInputPenRotationMax`.
 * @param flags    `RoboInputPenFlag*` bits.
 *
 * `data` is cleared first and filled only if every argument is accepted, so a refusal leaves an empty sample
 * rather than a mixture -- the rule every setter in this module follows. A pressure or tilt outside its
 * range is **refused** rather than clamped: a clamped measurement is a plausible wrong number, and there is
 * no reading of an out-of-range tilt that is better than being told.
 */
Result InputPenDataSet(InputPenData *data,
                       const InputPointerData *pointer,
                       int32_t pressure,
                       int32_t tiltX,
                       int32_t tiltY,
                       int32_t rotation,
                       uint32_t flags,
                       InputDiag *diag);

/** Non-zero when @p data is a record this build may carry on a pen event. */
int InputPenDataIsValid(const InputPenData *data);

/** Non-zero when @p data reports a hover sample. Safe on NULL (0). */
int InputPenDataIsHover(const InputPenData *data);

/** Non-zero when @p data is the eraser end. Safe on NULL (0). */
int InputPenDataIsEraser(const InputPenData *data);

/** Field-by-field equality. Safe on NULL; two NULLs are equal. */
int InputPenDataEqual(const InputPenData *a, const InputPenData *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_PEN_H */
