/*
 * RoboInput coordinate spaces (Phase 12.1) — see RoboInputCoordinates.h.
 *
 * Four conversions, one implementation each, and a generic door that forwards rather than reimplements. The
 * arithmetic is deliberately dull: a conversion subtracts one origin or adds it back, in `int64`, and
 * *refuses* when the difference does not fit a coordinate instead of wrapping. A wrapped position is a click
 * somewhere else entirely, which is the one wrong answer an input module must not give quietly.
 *
 * The two named pairs are the whole vocabulary of this file:
 *
 *     screen ⟷ window    the window's frame origin   (`DesktopWindowBounds`)
 *     window ⟷ document  the client area's inset     (`DesktopWindowClientBounds`)
 *
 * Nothing here knows what a window *is*: the geometry arrives per call in a `InputRoute`, is used,
 * and is dropped. That is why this module can sit beside RoboDesktop in the dependency graph rather than
 * under it, and why a caller that has not supplied the geometry it needs gets a named refusal instead of a
 * guess.
 */

#include "RoboInput/RoboInputCoordinates.h"

#include "RoboInputInternal.h"

#include <stdint.h>

int InputSpaceIsKnown(InputSpace space)
{
    return space > RoboInputSpaceUnknown && space < (InputSpace)RoboInputSpaceCount;
}

int InputSpaceIsConvertible(InputSpace space)
{
    return space == RoboInputSpaceScreen || space == RoboInputSpaceWindow ||
           space == RoboInputSpaceDocument;
}

const char *InputSpaceName(InputSpace space)
{
    switch (space) {
    case RoboInputSpaceUnknown:
        return "unknown";
    case RoboInputSpaceScreen:
        return "screen";
    case RoboInputSpaceWindow:
        return "window";
    case RoboInputSpaceApplication:
        return "application";
    case RoboInputSpaceDocument:
        return "document";
    default:
        break;
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* The geometry a conversion needs                                     */
/* ------------------------------------------------------------------ */

void InputRouteInit(InputRoute *route)
{
    if (!route) {
        return;
    }
    route->window = RoboInputRouteWindowNone;
    route->windowBounds = GfxRectEmpty();
    route->hasWindowBounds = 0;
    route->clientBounds = GfxRectEmpty();
    route->hasClientBounds = 0;
}

int InputRouteHasScreenGeometry(const InputRoute *route)
{
    return route != NULL && route->hasWindowBounds != 0;
}

int InputRouteHasDocumentGeometry(const InputRoute *route)
{
    return route != NULL && route->hasClientBounds != 0;
}

/* ------------------------------------------------------------------ */
/* The arithmetic                                                      */
/* ------------------------------------------------------------------ */

/**
 * Translate @p point by an offset, refusing rather than wrapping.
 *
 * `int64` for the addition and a range check on the way out: two coordinates that are each representable can
 * still differ by more than a coordinate can hold, and the only honest answer there is an error.
 */
static Result PointOffset(GfxPoint point,
                                  int32_t dx,
                                  int32_t dy,
                                  GfxPoint *out,
                                  InputDiag *diag)
{
    int64_t x = (int64_t)point.x + (int64_t)dx;
    int64_t y = (int64_t)point.y + (int64_t)dy;

    if (x < (int64_t)INT32_MIN || x > (int64_t)INT32_MAX || y < (int64_t)INT32_MIN ||
        y > (int64_t)INT32_MAX) {
        return RoboInputDiag(diag,
                               RoboInputDomainSpace,
                               RoboError,
                               "coordinate left the representable range");
    }
    out->x = (int32_t)x;
    out->y = (int32_t)y;
    return RoboOk;
}

/**
 * Negate an origin, refusing the one value that has no positive counterpart.
 *
 * `-INT32_MIN` overflows `int32`, and a rectangle whose origin sits there is a rectangle this build cannot
 * invert exactly -- so it is refused rather than converted to a neighbouring coordinate.
 */
static Result OriginNegated(int32_t origin, int32_t *out, InputDiag *diag)
{
    if (origin == INT32_MIN) {
        return RoboInputDiag(diag,
                               RoboInputDomainSpace,
                               RoboError,
                               "window origin cannot be inverted exactly");
    }
    *out = -origin;
    return RoboOk;
}

static Result RequireScreenGeometry(const InputRoute *route, InputDiag *diag)
{
    if (!InputRouteHasScreenGeometry(route)) {
        return RoboInputDiag(diag,
                               RoboInputDomainSpace,
                               RoboNotSupported,
                               "screen conversion needs the window's frame rectangle");
    }
    return RoboOk;
}

static Result RequireDocumentGeometry(const InputRoute *route, InputDiag *diag)
{
    if (!InputRouteHasDocumentGeometry(route)) {
        return RoboInputDiag(diag,
                               RoboInputDomainSpace,
                               RoboNotSupported,
                               "document conversion needs the window's client rectangle");
    }
    return RoboOk;
}

Result InputScreenToWindow(GfxPoint point,
                                          const InputRoute *route,
                                          GfxPoint *out,
                                          InputDiag *diag)
{
    int32_t dx;
    int32_t dy;
    Result r;

    if (!out || !route) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                               "screen_to_window needs an output and a route");
    }
    r = RequireScreenGeometry(route, diag);
    if (r != RoboOk) {
        return r;
    }
    r = OriginNegated(route->windowBounds.x, &dx, diag);
    if (r != RoboOk) {
        return r;
    }
    r = OriginNegated(route->windowBounds.y, &dy, diag);
    if (r != RoboOk) {
        return r;
    }
    return PointOffset(point, dx, dy, out, diag);
}

Result InputWindowToScreen(GfxPoint point,
                                          const InputRoute *route,
                                          GfxPoint *out,
                                          InputDiag *diag)
{
    Result r;

    if (!out || !route) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                               "window_to_screen needs an output and a route");
    }
    r = RequireScreenGeometry(route, diag);
    if (r != RoboOk) {
        return r;
    }
    return PointOffset(point, route->windowBounds.x, route->windowBounds.y, out, diag);
}

Result InputWindowToDocument(GfxPoint point,
                                            const InputRoute *route,
                                            GfxPoint *out,
                                            InputDiag *diag)
{
    int32_t dx;
    int32_t dy;
    Result r;

    if (!out || !route) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                               "window_to_document needs an output and a route");
    }
    r = RequireDocumentGeometry(route, diag);
    if (r != RoboOk) {
        return r;
    }
    r = OriginNegated(route->clientBounds.x, &dx, diag);
    if (r != RoboOk) {
        return r;
    }
    r = OriginNegated(route->clientBounds.y, &dy, diag);
    if (r != RoboOk) {
        return r;
    }
    return PointOffset(point, dx, dy, out, diag);
}

Result InputDocumentToWindow(GfxPoint point,
                                            const InputRoute *route,
                                            GfxPoint *out,
                                            InputDiag *diag)
{
    Result r;

    if (!out || !route) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                               "document_to_window needs an output and a route");
    }
    r = RequireDocumentGeometry(route, diag);
    if (r != RoboOk) {
        return r;
    }
    return PointOffset(point, route->clientBounds.x, route->clientBounds.y, out, diag);
}

Result InputConvertPoint(GfxPoint point,
                                       InputSpace from,
                                       InputSpace to,
                                       const InputRoute *route,
                                       GfxPoint *out,
                                       InputDiag *diag)
{
    if (!out || !route) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                               "convert_point needs an output and a route");
    }
    if (from == to) {
        /* "It is already in the plane you want" is a question with an obvious answer, and answering it
         * costs one copy -- but only for a *defined* plane: an unset one is still refused below. */
        if (!InputSpaceIsKnown(from)) {
            return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                                   "convert_point needs a defined space");
        }
        *out = point;
        return RoboOk;
    }
    if (!InputSpaceIsKnown(from) || !InputSpaceIsKnown(to)) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                               "convert_point needs two defined spaces");
    }
    if (from == RoboInputSpaceApplication || to == RoboInputSpaceApplication) {
        return RoboInputDiag(diag,
                               RoboInputDomainSpace,
                               RoboNotSupported,
                               "this build has no application plane to convert through");
    }

    switch (from) {
    case RoboInputSpaceScreen:
        if (to == RoboInputSpaceWindow) {
            return InputScreenToWindow(point, route, out, diag);
        }
        /* SCREEN → DOCUMENT is the composition of the two conversions, each with its own named door: doing it
         * here by adding the two origins would be a third implementation, and the day a transformation
         * enters the chain the composition is what keeps working. */
        {
            GfxPoint windowPoint;
            Result r = InputScreenToWindow(point, route, &windowPoint, diag);

            if (r != RoboOk) {
                return r;
            }
            return InputWindowToDocument(windowPoint, route, out, diag);
        }
    case RoboInputSpaceWindow:
        if (to == RoboInputSpaceScreen) {
            return InputWindowToScreen(point, route, out, diag);
        }
        return InputWindowToDocument(point, route, out, diag);
    case RoboInputSpaceDocument:
        if (to == RoboInputSpaceWindow) {
            return InputDocumentToWindow(point, route, out, diag);
        }
        {
            GfxPoint windowPoint;
            Result r = InputDocumentToWindow(point, route, &windowPoint, diag);

            if (r != RoboOk) {
                return r;
            }
            return InputWindowToScreen(windowPoint, route, out, diag);
        }
    default:
        break;
    }
    return RoboInputDiag(diag, RoboInputDomainSpace, RoboNotSupported,
                           "no conversion exists between those two planes");
}
