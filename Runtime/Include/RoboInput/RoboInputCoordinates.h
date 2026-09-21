/*
 * RoboInput coordinate spaces (Phase 12.1) — which plane a position is on, and who converts it.
 *
 * ## The four spaces, and why each is named
 *
 *   **screen**       the display's own plane: origin at its top-left, `+x` right, `+y` down, in the display's
 *                    logical units. What a platform reports a global pointer position in, and what
 *                    `DesktopHitTest` answers about (a shell's geometry is screen geometry);
 *
 *   **window**       the plane of one window's **frame**: origin at the frame's top-left, which is where a
 *                    window system hands a backend a click. RoboWindow's mouse payload calls the frame's
 *                    client corner `clientX/y` and the display plane `screenX/y` (7.8 / 7.11); those two
 *                    are exactly the pair this module calls *document* and *screen*, and the reason this
 *                    header exists is so that the pair has names that do not depend on the layer using them;
 *
 *   **document**     the plane a document is laid out and painted on. `RoboPaintHitTest.h` states once, at
 *                    length, that *paint coordinates are window coordinates in this build* -- there is no
 *                    scroll offset, no transform, no zoom and no scale to invert yet -- and that the two
 *                    names are kept apart because the phase that adds scrolling is the phase that makes
 *                    them differ. That statement is not restated here; it is *consumed*: this module calls
 *                    the plane **document** space so that the day paint and document coordinates part
 *                    company the name on an event does not have to change;
 *
 *   **application**  a plane whose origin the *application* chose (an embedded surface, a hosted view, a
 *                    future `robo.*` coordinate system). Nothing in this build produces one, and nothing
 *                    here converts to or from it: it exists so that a producer which genuinely has such a
 *                    plane can say so instead of lying with one of the other three. A conversion that would
 *                    need an application's own origin is refused with a diagnostic rather than guessed.
 *
 * ## Who owns a conversion
 *
 * **The caller, and this module only does arithmetic.** A conversion needs geometry -- where a window's
 * frame is on the screen, where a window's client area starts inside its frame -- and *no module in this
 * runtime stores that except the shell that owns the windows*. So the geometry travels **in**, per call, as
 * `InputRoute`, and is never retained: this module holds no window table, no display table and no
 * cache, which is what keeps it below RoboWindow and RoboDesktop in the dependency graph rather than beside
 * them. A caller with the geometry gets a conversion; a caller without it gets a refusal that names the
 * geometry it should have supplied, which is the diagnostic-shaped version of "this module will not guess".
 *
 * The named doors below therefore come in pairs, one per space boundary, and each says in its name which
 * two spaces it crosses -- the phase brief's rule that an event must not silently mix spaces is enforced by
 * there being no door whose direction a reader has to infer.
 *
 * ## What a conversion does not do
 *
 * It does **not** clip, hit-test or decide whether the point is inside anything: `GfxRectContainsPoint`
 * answers that, and a caller that wants it asks. It does not round, scale or account for a device pixel
 * ratio: every space here is in logical units, exactly as RoboWindow's payloads state for their own
 * coordinates. And it does not clamp: a window-space point outside the client area is a legitimate answer
 * (a drag that left the window), and clamping it here would hide that from every layer above.
 */
#ifndef ROBO_INPUT_COORDINATES_H
#define ROBO_INPUT_COORDINATES_H

#include "RoboBase/RoboError.h"

#include "RoboGraphics/RoboGraphicsGeometry.h"

#include "RoboInput/RoboInputDiagnostics.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Which plane a position is on.
 *
 * Every positioned event carries exactly one of these, and it describes **all** of that event's positions
 * (`position`, `previous`, and the point a target was concluded at). `UNKNOWN` is 0 so that a cleared event
 * claims no space rather than claiming the screen; a positioned event whose space is `UNKNOWN` is refused
 * by `InputEventIsValid`, which is how "no silent mixing" is enforced for the commonest mistake:
 * forgetting to say.
 */
typedef enum InputSpace {
    /** Nothing: a cleared record's value. Never a valid space for a position. */
    RoboInputSpaceUnknown = 0,
    /** The display's plane: a global pointer position. */
    RoboInputSpaceScreen = 1,
    /** One window's frame plane: what a window system hands a backend. */
    RoboInputSpaceWindow = 2,
    /** A plane whose origin the application chose. Named, never converted here. */
    RoboInputSpaceApplication = 3,
    /** The plane a document is laid out and painted on. */
    RoboInputSpaceDocument = 4
} InputSpace;

/** Number of defined spaces. */
#define RoboInputSpaceCount 5u

/** Non-zero if @p space is a defined space. */
int InputSpaceIsKnown(InputSpace space);

/** Non-zero when @p space is one this build can convert into and out of (not `UNKNOWN`, not
 *  `APPLICATION`). */
int InputSpaceIsConvertible(InputSpace space);

/** Human-readable space name; never NULL; `"unknown"` when undefined. */
const char *InputSpaceName(InputSpace space);

/**
 * The geometry a conversion needs, handed in by the caller for one call and never retained.
 *
 * Two rectangles and the window they belong to, with an explicit "did I supply this" flag each -- because
 * "not supplied" and "supplied as zero, zero, zero, zero" are different answers and only one of them is a
 * legitimate refusal. `windowBounds` is the **frame** rectangle in screen space, which is what
 * `DesktopWindowBounds` answers; `clientBounds` is the client area's rectangle in window space,
 * which is what `DesktopWindowClientBounds` answers. Between them they carry every offset the four
 * conversions below need, and nothing else.
 */
typedef struct RoboInputRoute {
    /** The window the geometry belongs to, or @ref RoboInputRouteWindowNone. */
    uint64_t window;
    /** The window's frame rectangle, in **screen** space. Read when @ref hasWindowBounds. */
    GfxRect windowBounds;
    /** Non-zero when @ref windowBounds was supplied. */
    int hasWindowBounds;
    /** The window's client rectangle, in **window** space. Read when @ref hasClientBounds. */
    GfxRect clientBounds;
    /** Non-zero when @ref clientBounds was supplied. */
    int hasClientBounds;
} InputRoute;

/** No window: a route with nothing supplied. */
#define RoboInputRouteWindowNone ((uint64_t)0u)

/** Clear @p route: no window, no geometry. Safe on NULL. */
void InputRouteInit(InputRoute *route);

/** Non-zero when @p route supplies the geometry screen ⟷ window needs (`windowBounds`). */
int InputRouteHasScreenGeometry(const InputRoute *route);

/** Non-zero when @p route supplies the geometry window ⟷ document needs (`clientBounds`). */
int InputRouteHasDocumentGeometry(const InputRoute *route);

/**
 * Screen → window: subtract the frame's origin.
 *
 * @param point  the position, in screen space.
 * @param route  geometry; @ref InputRouteHasScreenGeometry must hold.
 * @param out    receives the position in window space.
 * @return RoboOk; RoboInvalidArgument for a NULL @p out or @p route; RoboNotSupported with a `SPACE`
 *         diagnostic when @p route carries no frame rectangle; RoboError when the arithmetic leaves the
 *         coordinate range (a window far enough from the origin that the difference cannot be represented),
 *         which is refused rather than wrapped because a wrapped position is a click somewhere else.
 */
Result InputScreenToWindow(GfxPoint point,
                                          const InputRoute *route,
                                          GfxPoint *out,
                                          InputDiag *diag);

/** Window → screen: add the frame's origin. The exact inverse of @ref InputScreenToWindow. */
Result InputWindowToScreen(GfxPoint point,
                                          const InputRoute *route,
                                          GfxPoint *out,
                                          InputDiag *diag);

/**
 * Window → document: subtract where the client area starts inside the frame.
 *
 * The conversion a dispatcher performs before a hit test, and the one that carries the frame's borders and
 * title band out of a position: `clientBounds.x/y` is that inset, stated by the shell that computed it
 * (`DesktopWindowClientBounds`). It is the arithmetic `RoboWindowMouse.h` describes when it says
 * its `clientX/y` are relative to the client area rather than the frame.
 */
Result InputWindowToDocument(GfxPoint point,
                                            const InputRoute *route,
                                            GfxPoint *out,
                                            InputDiag *diag);

/** Document → window: add the client area's origin. The exact inverse. */
Result InputDocumentToWindow(GfxPoint point,
                                            const InputRoute *route,
                                            GfxPoint *out,
                                            InputDiag *diag);

/**
 * The generic door: convert @p point from @p from to @p to.
 *
 * A **dispatch table, not a shortcut**: it forwards to the four doors above, so there is one implementation
 * of each conversion in this build and the named doors cannot disagree with the generic one. What it adds
 * is the ability to ask at runtime -- a translator that knows only the space an event arrived in and the
 * space it wants needs *one* call, and a caller that switches on the pair would be writing the table a
 * second time.
 *
 * Refused with a `SPACE` diagnostic when: either space is undefined; either is `APPLICATION`
 * (`RoboNotSupported`: this build has no application plane to convert through, and guessing one would be
 * the silent mixing the phase forbids); the pair is a no-op only when @p from equals @p to, which *is*
 * allowed and copies the point through, because "the position is already in the space you want" is a
 * legitimate question with an obvious answer.
 *
 * @param windowBounds the frame rectangle, needed when `SCREEN` is one side of the pair.
 * @param clientBounds the client rectangle, needed when `DOCUMENT` is one side of the pair.
 */
Result InputConvertPoint(GfxPoint point,
                                       InputSpace from,
                                       InputSpace to,
                                       const InputRoute *route,
                                       GfxPoint *out,
                                       InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_COORDINATES_H */
