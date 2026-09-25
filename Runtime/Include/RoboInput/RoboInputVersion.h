/*
 * RoboInput version pin — Phase 12.1, the input architecture.
 *
 * RoboInput is the runtime's **own** model of input: the one representation an event has after it has left
 * whatever platform reported it and before anything has decided what to do with it. It exists because the
 * two layers beside it answer different questions:
 *
 *   **RoboWindow** (7.9–7.11) owns the *window system*: a window's own queue, its lifecycle events, and the
 *   mouse and key payloads a backend fills in for a window it owns. Its vocabulary is deliberately narrow --
 *   client and screen coordinates, one key payload, one mouse payload, no touch, no text, no source
 *   identity -- because a window does not need to know where a keystroke came from;
 *
 *   a **future dispatcher** (Phase 12.2 and after) needs the opposite: one event that says which *device*
 *   reported it, which *coordinate space* its positions are in, what the *modifier state* was, and which
 *   window, application or document node it is about. That is what this module is, and it is stated as a
 *   data model first because every later input phase -- dispatch, focus, text editing, selection, the
 *   clipboard, `robo.*` event objects -- is a *consumer* of it.
 *
 * ## What 12.1 is
 *
 * The model and nothing else: an event type vocabulary, per-type payloads, a device/source identity, an
 * explicit coordinate-space tag with the four conversions that are pure arithmetic, a routing target that
 * carries inert identity, a bounded FIFO queue that owns its copies, a deterministic debug dump, and one
 * implemented bridge (RoboWindow's event → this module's event) so that today's backends reach the model
 * without a device layer that does not exist yet.
 *
 * ## What 12.1 is not, and each of these is a 0 in `InputCapabilities`
 *
 * It is **not device reading**: nothing here opens a keyboard, a mouse, a touch digitizer or a game
 * controller, and no platform backend is implemented. The bridge is a *translation*, not a source.
 *
 * It is **not dispatch**: no event is routed, no node is resolved, no listener is called, nothing bubbles
 * and nothing is captured. A target is identity that a dispatcher *fills in*; this module never walks a
 * tree to discover one, which is why it depends on the *types* of the layers above and on none of their
 * behaviour.
 *
 * It is **not focus, text editing, composition or the clipboard**. The keyboard payload carries the text a
 * key produced because a model that cannot record it would have to be widened later; nothing here
 * interprets that text, composes a sequence, maps a layout or edits a caret.
 *
 * It is **not an event loop**, and it holds **no locks and no global state**: a queue is an object a caller
 * owns, and the reason there is no module-wide one is the reason there is no `InputInit` to call.
 */
#ifndef ROBO_INPUT_VERSION_H
#define ROBO_INPUT_VERSION_H

#define RoboInputVersionMajor 0
#define RoboInputVersionMinor 3
#define RoboInputVersionPatch 0
#define RoboInputVersionString "0.3.0"

/** 1 once the Phase 12 model is frozen (not yet). */
#define RoboInputApiFrozen 0

/**
 * 1 since this module's first release: **an event model, a queue, and the coordinate spaces they live in.**
 *
 * Read by `InputGetCapabilities()`'s `eventModel` field and by
 * `InputModelIsImplemented()`, which the conformance suite asserts together -- the device
 * `RoboDesktop`'s two flags established.
 *
 * What it claims: a stable event type vocabulary; per-type payloads that are *the only* payload an event of
 * that type carries; a source and device identity; an explicit coordinate-space tag with named conversions
 * between screen, window and document space; inert target identity (window, application, DOM handle,
 * layout node); a bounded, owned, FIFO queue with deterministic statistics; a deterministic dump; and a
 * bridge from a `WindowEvent` into the model. What it does **not** claim is everything an input
 * *system* has: no device is read, no event is dispatched, no focus is tracked, no text is composed, and
 * there is no event loop -- each of those is a 0 beside this flag in `InputCapabilities`.
 *
 * ## What 13.2 added, and what it still does not claim
 *
 * Phase 13.2 grew the vocabulary from thirteen event types to thirty-three -- hotplug, auto-repeat, hover,
 * pen, gamepad, composition, focus and window activation, each with the payload record it needs -- and built
 * the two halves of the *keyboard* that the model was missing:
 *
 *   the **device table** (`RoboInputDevice.h`): a descriptor per device saying what kind of hardware it is,
 *   what it can report and where it came from, and a fixed table whose disconnect **removes** an entry and
 *   hands it back, so no stale device can be looked up, asked for as a type's primary, or left holding a
 *   key;
 *
 *   the **keyboard state** (`RoboInputKeyboardState.h`): one held table from which the modifiers are
 *   *derived* and the locks are kept, plus an auto-repeat cadence a caller's clock drives one event at a
 *   time. A device disconnect therefore cannot leave a key stuck, because releasing its keys *is* releasing
 *   them and the modifiers that followed from them follow automatically.
 *
 * What it still does **not** claim: nothing reads a device (`deviceReading` and `platformAdapters` are both
 * 0), no event is dispatched and no focus is tracked. Each of those is a 0 in `InputCapabilities`, named so
 * a reader cannot mistake this for a finished input *system*.
 *
 * ## What 13.9/13.10/13.11 added, and what this takes the module to
 *
 * The **keyboard layout and the text pipeline** -- the half of the keyboard that 13.2 left 0:
 *
 *   a **layout** (`RoboInputLayout.h`) is a bounded table of `(key, level) → character` with the four
 *   levels, the AltGr rule (two spellings, and `Ctrl`-without-`Alt` is **not** one of them) and the caps-lock
 *   rule stated once in `InputLayoutLevelFor`. Two tables ship -- `US_ASCII` and `US_INTERNATIONAL` -- and
 *   every other language is data this module can hold and does not ship;
 *
 *   **dead keys** are layout data rather than a text engine: five accents, each with a spacing form, a
 *   combining form and a precomposed-pair table. One accent waits at a time, and arming it produces **no**
 *   event, because there is no character yet;
 *
 *   the **text pipeline** (`RoboInputText.h`) is the one door from a key event to zero, one or two
 *   `TEXT_INPUT` events. That is the brief's mandatory separation made checkable: a key event says a key
 *   changed state, a text event says text should be inserted, and no consumer may read a scan code to decide
 *   what a character is.
 *
 * What it still does **not** claim, and each stays 0: `textEditing` (the text reaches an editor -- 13.12),
 * `textComposition` (an input *method*'s preedit and candidates -- 13.37, which is a different thing from a
 * dead key and is why the layout half did not set it), `deviceReading` and `platformAdapters`.
 */
#define RoboInputModelImplemented 1

/**
 * 0, and it stays 0 for this phase: **no platform input adapter is implemented.**
 *
 * The adapter is the shape §13 of the phase brief names -- native input → a RoboInput event → the queue --
 * and it is deliberately *one* implementation short of existing: the only translation this build performs
 * is `InputEventFromWindowEvent` (`RoboInputBridge.h`), which converts what RoboWindow already
 * delivers rather than reading a device. A build that added a real adapter would flip this flag, and the
 * capability record beside it already names what would have to be true.
 */
#define RoboInputPlatformAdaptersImplemented 0

#endif /* ROBO_INPUT_VERSION_H */
