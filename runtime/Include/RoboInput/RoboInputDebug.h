/*
 * RoboInput debug text (Phase 12.1) — one line per event, and the same line every time.
 *
 * ## The format, exactly
 *
 * The phase brief sketches a multi-line record; this runtime's convention for a debug dump is *one line per
 * object* with `key=value` fields in a fixed order (RoboRender's frame dump, RoboDesktop's shell dump), and
 * that convention wins here for a stated reason: an input sequence is *read as a list*, and a list whose
 * entries are five lines each cannot be compared against a test's expected sequence or scanned for the one
 * event that went wrong. So an event is one line, its fields are always in this order, and every field is
 * always present:
 *
 *     <Type> source=<source> device=<none|id|id:name> window=<id> display=<id> time=<ms>
 *            modifiers=<list> space=<space> <payload fields> [target=<kind> ...]
 *
 * Concretely, the four shapes a reader meets:
 *
 *     KeyDown source=keyboard device=1:Keyboard window=3 display=0 time=0 modifiers=control space=unknown
 *             physical=a logical=a action=down repeat=0 flags=none text="a"
 *     PointerMove source=mouse device=1:Mouse window=3 display=0 time=1841 modifiers=none space=screen
 *             position=120,84 previous=118,84 delta=2,0 buttons=none button=none clicks=0 wheel=0,0 unit=line
 *     Wheel source=mouse device=1:Mouse window=3 display=0 time=1850 modifiers=none space=screen
 *             position=120,84 previous=120,84 delta=0,0 buttons=left button=none clicks=0 wheel=0,120 unit=line
 *     TextInput source=keyboard device=1:Keyboard window=3 display=0 time=1852 modifiers=shift space=unknown
 *             text="A"
 *
 * wrapped here for readability only: **the real dump never wraps and never contains a newline inside an
 * event**. The only newline a writer emits is the one that ends the line.
 *
 * `space=unknown` on a key event is not a bug: a keystroke has no position, so it names no plane, and the
 * validator's rule (RoboInputEvent.h, rule 6) is what makes that visible here instead of hiding it.
 *
 * ## What the text must never contain
 *
 * **No address, no pointer, no clock, no iteration order.** Every field is an id, a name, a number or a
 * unit, all of which are stable across two runs of the same sequence; that is the property
 * `input.debug.determinism` asserts by dumping the same sequence twice and comparing byte for byte. It is
 * also why the dump prints a *device id and name* and never a device pointer, and why a target prints its
 * kind and ids rather than anything resolved.
 *
 * ## Refusal, never truncation
 *
 * A buffer too small gets `ROBO_NOT_SUPPORTED` with a `DUMP` diagnostic and `out_length` set to the length
 * the caller **would have needed**, so a caller can size from the refusal and try again. A truncated line
 * that looked complete is the failure mode this avoids: a reader cannot tell a cut-off field from a short
 * one.
 *
 * ## Line bounds
 *
 * `ROBO_INPUT_DUMP_LINE_MAX` is the longest line this build can emit, device name and modifier list
 * included, with room to spare. It is a stated constant because the test suite sizes a buffer from it and
 * because it is the honest answer to "how much room does the dump need".
 */
#ifndef ROBO_INPUT_DEBUG_H
#define ROBO_INPUT_DEBUG_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputQueue.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Longest single event line this build can produce, NUL included.
 *
 * A bound rather than an estimate, and it is **asserted** rather than asserted-about:
 * `input.debug.line_bound` builds the widest event each payload kind can express -- a 31-character device
 * name, every modifier bit, the most negative coordinates, a full button mask, a document target -- and
 * writes it, so a field added to the dump without this constant being revisited fails the suite instead of
 * quietly returning a refusal on the longest line a caller could actually produce.
 */
#define ROBO_INPUT_DUMP_LINE_MAX 1024u

/**
 * Write @p event as one line (no newline; see this header's format).
 *
 * @param out_length non-NULL; receives the length written (excluding the NUL).
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a zero capacity; ROBO_NOT_SUPPORTED with a
 *         `DUMP` diagnostic when the text does not fit, with @p out_length set to the needed length. A
 *         `NONE`-typed event is written rather than refused -- the dump's job is to show what is there, and
 *         `None ...` is a useful thing to see in a sequence.
 */
robo_result_t robo_input_event_write(const robo_input_event_t *event,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_input_diag_t *diag);

/**
 * Write @p queue as text: one summary line, then one line per queued event in FIFO order, each ending in a
 * newline.
 *
 *     queue capacity=128 count=2 pushed=4 popped=2 dropped=0 cleared=0 high_water=2
 *     KeyDown source=keyboard ...
 *     PointerMove source=mouse ...
 *
 * The summary is the queue's statistics verbatim, which is what makes a dump of a queue also a test of its
 * counters. An **empty queue writes the summary line alone** -- a legitimate result, not a refusal.
 *
 * @return as @ref robo_input_event_write; a NULL @p queue is refused.
 */
robo_result_t robo_input_queue_write(const robo_input_queue_t *queue,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_input_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_DEBUG_H */
