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
#define RoboInputVersionMinor 1
#define RoboInputVersionPatch 0
#define RoboInputVersionString "0.1.0"

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
