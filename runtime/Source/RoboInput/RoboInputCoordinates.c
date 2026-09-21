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
 *     screen ⟷ window    the window's frame origin   (`robo_desktop_window_bounds`)
 *     window ⟷ document  the client area's inset     (`robo_desktop_window_client_bounds`)
 *
 * Nothing here knows what a window *is*: the geometry arrives per call in a `robo_input_route_t`, is used,
 * and is dropped. That is why this module can sit beside RoboDesktop in the dependency graph rather than
 * under it, and why a caller that has not supplied the geometry it needs gets a named refusal instead of a
 * guess.
 */

#include "RoboInput/RoboInputCoordinates.h"

#include "RoboInputInternal.h"

#include <stdint.h>

int robo_input_space_is_known(robo_input_space_t space)
{
    return space > ROBO_INPUT_SPACE_UNKNOWN && space < (robo_input_space_t)ROBO_INPUT_SPACE_COUNT;
}

int robo_input_space_is_convertible(robo_input_space_t space)
{
    return space == ROBO_INPUT_SPACE_SCREEN || space == ROBO_INPUT_SPACE_WINDOW ||
           space == ROBO_INPUT_SPACE_DOCUMENT;
}

const char *robo_input_space_name(robo_input_space_t space)
{
    switch (space) {
    case ROBO_INPUT_SPACE_UNKNOWN:
        return "unknown";
    case ROBO_INPUT_SPACE_SCREEN:
        return "screen";
    case ROBO_INPUT_SPACE_WINDOW:
        return "window";
    case ROBO_INPUT_SPACE_APPLICATION:
        return "application";
    case ROBO_INPUT_SPACE_DOCUMENT:
        return "document";
    default:
        break;
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* The geometry a conversion needs                                     */
/* ------------------------------------------------------------------ */

void robo_input_route_init(robo_input_route_t *route)
{
    if (!route) {
        return;
    }
    route->window = ROBO_INPUT_ROUTE_WINDOW_NONE;
    route->window_bounds = robo_gfx_rect_empty();
    route->has_window_bounds = 0;
    route->client_bounds = robo_gfx_rect_empty();
    route->has_client_bounds = 0;
}

int robo_input_route_has_screen_geometry(const robo_input_route_t *route)
{
    return route != NULL && route->has_window_bounds != 0;
}

int robo_input_route_has_document_geometry(const robo_input_route_t *route)
{
    return route != NULL && route->has_client_bounds != 0;
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
static robo_result_t point_offset(robo_gfx_point_t point,
                                  int32_t dx,
                                  int32_t dy,
                                  robo_gfx_point_t *out,
                                  robo_input_diag_t *diag)
{
    int64_t x = (int64_t)point.x + (int64_t)dx;
    int64_t y = (int64_t)point.y + (int64_t)dy;

    if (x < (int64_t)INT32_MIN || x > (int64_t)INT32_MAX || y < (int64_t)INT32_MIN ||
        y > (int64_t)INT32_MAX) {
        return ROBO_INPUT_DIAG(diag,
                               ROBO_INPUT_DOMAIN_SPACE,
                               ROBO_ERROR,
                               "coordinate left the representable range");
    }
    out->x = (int32_t)x;
    out->y = (int32_t)y;
    return ROBO_OK;
}

/**
 * Negate an origin, refusing the one value that has no positive counterpart.
 *
 * `-INT32_MIN` overflows `int32`, and a rectangle whose origin sits there is a rectangle this build cannot
 * invert exactly -- so it is refused rather than converted to a neighbouring coordinate.
 */
static robo_result_t origin_negated(int32_t origin, int32_t *out, robo_input_diag_t *diag)
{
    if (origin == INT32_MIN) {
        return ROBO_INPUT_DIAG(diag,
                               ROBO_INPUT_DOMAIN_SPACE,
                               ROBO_ERROR,
                               "window origin cannot be inverted exactly");
    }
    *out = -origin;
    return ROBO_OK;
}

static robo_result_t require_screen_geometry(const robo_input_route_t *route, robo_input_diag_t *diag)
{
    if (!robo_input_route_has_screen_geometry(route)) {
        return ROBO_INPUT_DIAG(diag,
                               ROBO_INPUT_DOMAIN_SPACE,
                               ROBO_NOT_SUPPORTED,
                               "screen conversion needs the window's frame rectangle");
    }
    return ROBO_OK;
}

static robo_result_t require_document_geometry(const robo_input_route_t *route, robo_input_diag_t *diag)
{
    if (!robo_input_route_has_document_geometry(route)) {
        return ROBO_INPUT_DIAG(diag,
                               ROBO_INPUT_DOMAIN_SPACE,
                               ROBO_NOT_SUPPORTED,
                               "document conversion needs the window's client rectangle");
    }
    return ROBO_OK;
}

robo_result_t robo_input_screen_to_window(robo_gfx_point_t point,
                                          const robo_input_route_t *route,
                                          robo_gfx_point_t *out,
                                          robo_input_diag_t *diag)
{
    int32_t dx;
    int32_t dy;
    robo_result_t r;

    if (!out || !route) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_INVALID_ARGUMENT,
                               "screen_to_window needs an output and a route");
    }
    r = require_screen_geometry(route, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = origin_negated(route->window_bounds.x, &dx, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = origin_negated(route->window_bounds.y, &dy, diag);
    if (r != ROBO_OK) {
        return r;
    }
    return point_offset(point, dx, dy, out, diag);
}

robo_result_t robo_input_window_to_screen(robo_gfx_point_t point,
                                          const robo_input_route_t *route,
                                          robo_gfx_point_t *out,
                                          robo_input_diag_t *diag)
{
    robo_result_t r;

    if (!out || !route) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_INVALID_ARGUMENT,
                               "window_to_screen needs an output and a route");
    }
    r = require_screen_geometry(route, diag);
    if (r != ROBO_OK) {
        return r;
    }
    return point_offset(point, route->window_bounds.x, route->window_bounds.y, out, diag);
}

robo_result_t robo_input_window_to_document(robo_gfx_point_t point,
                                            const robo_input_route_t *route,
                                            robo_gfx_point_t *out,
                                            robo_input_diag_t *diag)
{
    int32_t dx;
    int32_t dy;
    robo_result_t r;

    if (!out || !route) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_INVALID_ARGUMENT,
                               "window_to_document needs an output and a route");
    }
    r = require_document_geometry(route, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = origin_negated(route->client_bounds.x, &dx, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = origin_negated(route->client_bounds.y, &dy, diag);
    if (r != ROBO_OK) {
        return r;
    }
    return point_offset(point, dx, dy, out, diag);
}

robo_result_t robo_input_document_to_window(robo_gfx_point_t point,
                                            const robo_input_route_t *route,
                                            robo_gfx_point_t *out,
                                            robo_input_diag_t *diag)
{
    robo_result_t r;

    if (!out || !route) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_INVALID_ARGUMENT,
                               "document_to_window needs an output and a route");
    }
    r = require_document_geometry(route, diag);
    if (r != ROBO_OK) {
        return r;
    }
    return point_offset(point, route->client_bounds.x, route->client_bounds.y, out, diag);
}

robo_result_t robo_input_convert_point(robo_gfx_point_t point,
                                       robo_input_space_t from,
                                       robo_input_space_t to,
                                       const robo_input_route_t *route,
                                       robo_gfx_point_t *out,
                                       robo_input_diag_t *diag)
{
    if (!out || !route) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_INVALID_ARGUMENT,
                               "convert_point needs an output and a route");
    }
    if (from == to) {
        /* "It is already in the plane you want" is a question with an obvious answer, and answering it
         * costs one copy -- but only for a *defined* plane: an unset one is still refused below. */
        if (!robo_input_space_is_known(from)) {
            return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_INVALID_ARGUMENT,
                                   "convert_point needs a defined space");
        }
        *out = point;
        return ROBO_OK;
    }
    if (!robo_input_space_is_known(from) || !robo_input_space_is_known(to)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_INVALID_ARGUMENT,
                               "convert_point needs two defined spaces");
    }
    if (from == ROBO_INPUT_SPACE_APPLICATION || to == ROBO_INPUT_SPACE_APPLICATION) {
        return ROBO_INPUT_DIAG(diag,
                               ROBO_INPUT_DOMAIN_SPACE,
                               ROBO_NOT_SUPPORTED,
                               "this build has no application plane to convert through");
    }

    switch (from) {
    case ROBO_INPUT_SPACE_SCREEN:
        if (to == ROBO_INPUT_SPACE_WINDOW) {
            return robo_input_screen_to_window(point, route, out, diag);
        }
        /* SCREEN → DOCUMENT is the composition of the two conversions, each with its own named door: doing it
         * here by adding the two origins would be a third implementation, and the day a transformation
         * enters the chain the composition is what keeps working. */
        {
            robo_gfx_point_t window_point;
            robo_result_t r = robo_input_screen_to_window(point, route, &window_point, diag);

            if (r != ROBO_OK) {
                return r;
            }
            return robo_input_window_to_document(window_point, route, out, diag);
        }
    case ROBO_INPUT_SPACE_WINDOW:
        if (to == ROBO_INPUT_SPACE_SCREEN) {
            return robo_input_window_to_screen(point, route, out, diag);
        }
        return robo_input_window_to_document(point, route, out, diag);
    case ROBO_INPUT_SPACE_DOCUMENT:
        if (to == ROBO_INPUT_SPACE_WINDOW) {
            return robo_input_document_to_window(point, route, out, diag);
        }
        {
            robo_gfx_point_t window_point;
            robo_result_t r = robo_input_document_to_window(point, route, &window_point, diag);

            if (r != ROBO_OK) {
                return r;
            }
            return robo_input_window_to_screen(window_point, route, out, diag);
        }
    default:
        break;
    }
    return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_NOT_SUPPORTED,
                           "no conversion exists between those two planes");
}
