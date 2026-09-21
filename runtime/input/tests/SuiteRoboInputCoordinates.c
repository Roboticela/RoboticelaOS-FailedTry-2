/*
 * RoboInput coordinate suite (Phase 12.1) — `input.coordinates.*`.
 *
 * This is the suite the phase brief's §9 asks for: the four planes named, the four conversions asserted in
 * both directions, and the refusals -- a conversion whose geometry the caller did not supply, a plane this
 * build cannot convert through, and a position that would leave the representable range on the way.
 *
 * The last case, `mixing`, is the one that answers "prevention of accidental space mixing where practical".
 * There is no way to make a *number* carry its plane in C, so what the module does instead is: (1) refuse to
 * treat an unset plane as any plane at all, (2) require an event with a position to name one, and (3) make
 * every conversion name both of its planes, so a reader can see which two are being crossed. The case
 * asserts the first two directly and the third by showing that the *same* point converts to two different
 * answers depending on the plane it is declared in -- which is exactly the mistake a silent mix would make.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* The planes                                                          */
/* ------------------------------------------------------------------ */

static void test_coordinates_spaces(void)
{
    ROBO_CHECK_EQ_INT(robo_input_space_is_known(ROBO_INPUT_SPACE_SCREEN), 1);
    ROBO_CHECK_EQ_INT(robo_input_space_is_known(ROBO_INPUT_SPACE_WINDOW), 1);
    ROBO_CHECK_EQ_INT(robo_input_space_is_known(ROBO_INPUT_SPACE_APPLICATION), 1);
    ROBO_CHECK_EQ_INT(robo_input_space_is_known(ROBO_INPUT_SPACE_DOCUMENT), 1);
    ROBO_CHECK_EQ_INT(robo_input_space_is_known(ROBO_INPUT_SPACE_UNKNOWN), 0);
    ROBO_CHECK_STR_EQ(robo_input_space_name(ROBO_INPUT_SPACE_SCREEN), "screen");
    ROBO_CHECK_STR_EQ(robo_input_space_name(ROBO_INPUT_SPACE_WINDOW), "window");
    ROBO_CHECK_STR_EQ(robo_input_space_name(ROBO_INPUT_SPACE_APPLICATION), "application");
    ROBO_CHECK_STR_EQ(robo_input_space_name(ROBO_INPUT_SPACE_DOCUMENT), "document");
    ROBO_CHECK_STR_EQ(robo_input_space_name(ROBO_INPUT_SPACE_UNKNOWN), "unknown");

    /* A route remembers exactly what it was given, and answers what it can do with it. */
    {
        robo_input_route_t route;
        robo_input_route_t frame_only;
        robo_input_route_t client_only;

        robo_input_route_init(&route);
        ROBO_CHECK_EQ_INT(route.window, ROBO_INPUT_ROUTE_WINDOW_NONE);
        ROBO_CHECK_EQ_INT(route.has_window_bounds, 0);
        ROBO_CHECK_EQ_INT(route.has_client_bounds, 0);
        ROBO_CHECK_EQ_INT(robo_input_route_has_screen_geometry(&route), 0);
        ROBO_CHECK_EQ_INT(robo_input_route_has_document_geometry(&route), 0);
        ROBO_CHECK_EQ_INT(robo_input_route_has_screen_geometry(NULL), 0);
        ROBO_CHECK_EQ_INT(robo_input_route_has_document_geometry(NULL), 0);
        robo_input_route_init(NULL);

        frame_only = input_route_frame_only(10, 20, 400, 300);
        ROBO_CHECK_EQ_INT(robo_input_route_has_screen_geometry(&frame_only), 1);
        ROBO_CHECK_EQ_INT(robo_input_route_has_document_geometry(&frame_only), 0);
        ROBO_CHECK_EQ_INT(frame_only.window, 7u);

        client_only = input_route(0, 0, 400, 300, 1, 25, 398, 274);
        ROBO_CHECK_EQ_INT(robo_input_route_has_screen_geometry(&client_only), 1);
        ROBO_CHECK_EQ_INT(robo_input_route_has_document_geometry(&client_only), 1);

        /* A supplied-but-empty rectangle is *not* the same as an unsupplied one: the flag decides, and the
         * zeroes are the geometry of a window at the origin. */
        robo_input_route_init(&route);
        route.window_bounds = robo_gfx_rect(0, 0, 0, 0);
        route.has_window_bounds = 1;
        ROBO_CHECK_EQ_INT(robo_input_route_has_screen_geometry(&route), 1);
    }
}

/* ------------------------------------------------------------------ */
/* Screen ⟷ window                                                     */
/* ------------------------------------------------------------------ */

static void test_coordinates_screen_window(void)
{
    robo_input_route_t route = input_route(10, 20, 400, 300, 1, 25, 398, 274);
    robo_gfx_point_t window_point = { 0, 0 };
    robo_gfx_point_t screen_point = { 0, 0 };
    robo_gfx_point_t back = { 0, 0 };
    robo_input_diag_t diag;

    ROBO_CHECK_EQ_INT(robo_input_screen_to_window(robo_gfx_point(130, 104), &route, &window_point, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(window_point.x, 120);
    ROBO_CHECK_EQ_INT(window_point.y, 84);

    ROBO_CHECK_EQ_INT(robo_input_window_to_screen(robo_gfx_point(120, 84), &route, &screen_point, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(screen_point.x, 130);
    ROBO_CHECK_EQ_INT(screen_point.y, 104);

    /* The pair is an exact inverse, in both orders, including for positions outside the frame (a drag that
     * left the window is a legitimate answer this module does not clamp away). */
    ROBO_CHECK_EQ_INT(robo_input_screen_to_window(screen_point, &route, &back, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(back.x, 120);
    ROBO_CHECK_EQ_INT(back.y, 84);
    ROBO_CHECK_EQ_INT(robo_input_window_to_screen(robo_gfx_point(-40, -70), &route, &screen_point, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(screen_point.x, -30);
    ROBO_CHECK_EQ_INT(screen_point.y, -50);
    ROBO_CHECK_EQ_INT(robo_input_screen_to_window(screen_point, &route, &back, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(back.x, -40);
    ROBO_CHECK_EQ_INT(back.y, -70);

    /* A window whose frame sits at a negative origin works; one at the most negative coordinate converts
     * *into* window space only -- the frame's origin cannot be negated exactly, which is the one case where
     * the subtraction has no answer, and it is refused rather than answered approximately. */
    {
        robo_input_route_t far = input_route_frame_only(INT32_MIN, INT32_MIN, 100, 100);

        ROBO_CHECK_EQ_INT(robo_input_screen_to_window(robo_gfx_point(5, 5), &far, &window_point, &diag),
                          ROBO_ERROR);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
        ROBO_CHECK_EQ_INT(robo_input_window_to_screen(robo_gfx_point(5, 5), &far, &screen_point, &diag),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(screen_point.x, 5 + INT32_MIN);
    }

    /* Two representable coordinates can differ by more than a coordinate can hold: the direction that would
     * leave the range is refused rather than wrapped, because a wrapped position is a click somewhere else.
     */
    {
        robo_input_route_t wide = input_route_frame_only(INT32_MAX, INT32_MAX, 10, 10);

        ROBO_CHECK_EQ_INT(robo_input_screen_to_window(robo_gfx_point(INT32_MIN, INT32_MIN), &wide,
                                                     &window_point, &diag),
                          ROBO_ERROR);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
        ROBO_CHECK_EQ_INT(robo_input_window_to_screen(robo_gfx_point(INT32_MAX, 0), &wide, &screen_point,
                                                     &diag),
                          ROBO_ERROR);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
        /* ... while a point that still fits converts, at the very edge of the range. */
        ROBO_CHECK_EQ_INT(robo_input_window_to_screen(robo_gfx_point(-1, 0), &wide, &screen_point, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(screen_point.x, INT32_MAX - 1);
    }

    /* Without the frame rectangle there is nothing to convert with, and the refusal says so in the space
     * domain rather than returning a point that happens to be the input. */
    {
        robo_input_route_t empty;

        robo_input_route_init(&empty);
        ROBO_CHECK_EQ_INT(robo_input_screen_to_window(robo_gfx_point(1, 2), &empty, &window_point, &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
        ROBO_CHECK(diag.message != NULL);
        ROBO_CHECK_EQ_INT(robo_input_window_to_screen(robo_gfx_point(1, 2), &empty, &screen_point, &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(robo_input_screen_to_window(robo_gfx_point(1, 2), NULL, &window_point, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_screen_to_window(robo_gfx_point(1, 2), &route, NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_window_to_screen(robo_gfx_point(1, 2), NULL, &screen_point, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
}

/* ------------------------------------------------------------------ */
/* Window ⟷ document                                                   */
/* ------------------------------------------------------------------ */

static void test_coordinates_window_document(void)
{
    robo_input_route_t route = input_route(10, 20, 400, 300, 1, 25, 398, 274);
    robo_gfx_point_t document_point = { 0, 0 };
    robo_gfx_point_t window_point = { 0, 0 };
    robo_input_diag_t diag;

    ROBO_CHECK_EQ_INT(robo_input_window_to_document(robo_gfx_point(120, 84), &route, &document_point, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(document_point.x, 119);
    ROBO_CHECK_EQ_INT(document_point.y, 59);
    ROBO_CHECK_EQ_INT(robo_input_document_to_window(document_point, &route, &window_point, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(window_point.x, 120);
    ROBO_CHECK_EQ_INT(window_point.y, 84);

    /* The client inset can be negative (a border drawn outside the client area), and the conversion still
     * inverts exactly. */
    {
        robo_input_route_t outset = input_route(0, 0, 400, 300, -2, -30, 404, 330);

        ROBO_CHECK_EQ_INT(robo_input_window_to_document(robo_gfx_point(0, 0), &outset, &document_point,
                                                       NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(document_point.x, 2);
        ROBO_CHECK_EQ_INT(document_point.y, 30);
        ROBO_CHECK_EQ_INT(robo_input_document_to_window(document_point, &outset, &window_point, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(window_point.x, 0);
        ROBO_CHECK_EQ_INT(window_point.y, 0);
    }

    /* Without the client rectangle this pair is refused, and *only* this pair: the frame rectangle is not a
     * substitute, which is the whole reason the route carries two. */
    {
        robo_input_route_t frame_only = input_route_frame_only(10, 20, 400, 300);

        ROBO_CHECK_EQ_INT(robo_input_window_to_document(robo_gfx_point(1, 2), &frame_only, &document_point,
                                                       &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
        ROBO_CHECK_EQ_INT(robo_input_document_to_window(robo_gfx_point(1, 2), &frame_only, &window_point,
                                                       &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(robo_input_window_to_screen(robo_gfx_point(1, 2), &frame_only, &window_point,
                                                     NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_window_to_document(robo_gfx_point(1, 2), NULL, &document_point, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_document_to_window(robo_gfx_point(1, 2), &route, NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
}

/* ------------------------------------------------------------------ */
/* The generic door                                                    */
/* ------------------------------------------------------------------ */

static void test_coordinates_generic(void)
{
    robo_input_route_t route = input_route(10, 20, 400, 300, 1, 25, 398, 274);
    robo_gfx_point_t out = { 0, 0 };
    robo_input_diag_t diag;

    /* "It is already in the plane you want" is answered, not refused -- including for a point that no
     * conversion could represent, because none is needed. */
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(INT32_MIN, INT32_MAX),
                                               ROBO_INPUT_SPACE_WINDOW, ROBO_INPUT_SPACE_WINDOW, &route,
                                               &out, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, INT32_MIN);
    ROBO_CHECK_EQ_INT(out.y, INT32_MAX);
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(7, 8), ROBO_INPUT_SPACE_APPLICATION,
                                               ROBO_INPUT_SPACE_APPLICATION, &route, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 7);

    /* Screen → document is the composition of the two named conversions, so it has one implementation and
     * the generic door cannot disagree with the two it stands for. */
    {
        robo_gfx_point_t via_two = { 0, 0 };
        robo_gfx_point_t window_middle = { 0, 0 };

        ROBO_CHECK_EQ_INT(robo_input_screen_to_window(robo_gfx_point(130, 104), &route, &window_middle,
                                                     NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_window_to_document(window_middle, &route, &via_two, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(130, 104), ROBO_INPUT_SPACE_SCREEN,
                                                   ROBO_INPUT_SPACE_DOCUMENT, &route, &out, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(out.x, via_two.x);
        ROBO_CHECK_EQ_INT(out.y, via_two.y);
        ROBO_CHECK_EQ_INT(out.x, 119);
        ROBO_CHECK_EQ_INT(out.y, 59);

        /* ... and the other direction is its exact inverse. */
        ROBO_CHECK_EQ_INT(robo_input_convert_point(out, ROBO_INPUT_SPACE_DOCUMENT, ROBO_INPUT_SPACE_SCREEN,
                                                   &route, &window_middle, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(window_middle.x, 130);
        ROBO_CHECK_EQ_INT(window_middle.y, 104);
    }

    /* The application plane is named and never converted through: guessing an application's origin is the
     * silent mixing this phase exists to prevent. */
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(1, 2), ROBO_INPUT_SPACE_SCREEN,
                                               ROBO_INPUT_SPACE_APPLICATION, &route, &out, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(1, 2), ROBO_INPUT_SPACE_APPLICATION,
                                               ROBO_INPUT_SPACE_WINDOW, &route, &out, &diag),
                      ROBO_NOT_SUPPORTED);

    /* Undefined planes are refused, including the "same plane" case: an unset plane is not a plane. */
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(1, 2), ROBO_INPUT_SPACE_UNKNOWN,
                                               ROBO_INPUT_SPACE_WINDOW, &route, &out, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(1, 2), ROBO_INPUT_SPACE_WINDOW,
                                               ROBO_INPUT_SPACE_UNKNOWN, &route, &out, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(1, 2), ROBO_INPUT_SPACE_UNKNOWN,
                                               ROBO_INPUT_SPACE_UNKNOWN, &route, &out, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(1, 2), (robo_input_space_t)9,
                                               ROBO_INPUT_SPACE_WINDOW, &route, &out, &diag),
                      ROBO_INVALID_ARGUMENT);

    /* A pair whose geometry is missing is refused rather than answered with a half-conversion. */
    {
        robo_input_route_t frame_only = input_route_frame_only(10, 20, 400, 300);

        ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(1, 2), ROBO_INPUT_SPACE_SCREEN,
                                                   ROBO_INPUT_SPACE_DOCUMENT, &frame_only, &out, &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(1, 2), ROBO_INPUT_SPACE_DOCUMENT,
                                                   ROBO_INPUT_SPACE_SCREEN, &frame_only, &out, &diag),
                          ROBO_NOT_SUPPORTED);
    }

    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(1, 2), ROBO_INPUT_SPACE_SCREEN,
                                               ROBO_INPUT_SPACE_WINDOW, NULL, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(1, 2), ROBO_INPUT_SPACE_SCREEN,
                                               ROBO_INPUT_SPACE_WINDOW, &route, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* Mixing planes, and what stops it                                    */
/* ------------------------------------------------------------------ */

static void test_coordinates_mixing(void)
{
    robo_input_route_t route = input_route(10, 20, 400, 300, 1, 25, 398, 274);
    robo_gfx_point_t out = { 0, 0 };

    /* The same three numbers mean two different places depending on the plane they are declared in, and the
     * conversion is what makes the difference visible: reading a window-space point as if it were a screen
     * point is off by the frame's origin, and the *only* way to get an answer here is to say which plane the
     * input was in. */
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(120, 84), ROBO_INPUT_SPACE_WINDOW,
                                               ROBO_INPUT_SPACE_SCREEN, &route, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 130);
    ROBO_CHECK_EQ_INT(out.y, 104);
    ROBO_CHECK_EQ_INT(robo_input_convert_point(robo_gfx_point(120, 84), ROBO_INPUT_SPACE_SCREEN,
                                               ROBO_INPUT_SPACE_SCREEN, &route, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 120);
    ROBO_CHECK_EQ_INT(out.y, 84);

    /* An event that carries a position must name a plane, and one that carries none must not: the two
     * refusals are what make "which plane is this?" an answerable question at every point in the runtime
     * rather than a convention somebody has to remember. */
    {
        robo_input_event_t event = input_pointer_move(120, 84, 0u, ROBO_INPUT_SPACE_WINDOW, 3u);
        robo_input_event_t unnamed = event;

        unnamed.space = ROBO_INPUT_SPACE_UNKNOWN;
        ROBO_CHECK(robo_input_event_is_valid(&event));
        ROBO_CHECK(!robo_input_event_is_valid(&unnamed));

        /* Converting it does not change it: an event stays in the plane it arrived in, and the plane it was
         * asked for is an answer, not an edit. */
        ROBO_CHECK_EQ_INT(robo_input_event_position_in(&event, ROBO_INPUT_SPACE_DOCUMENT, &route, &out,
                                                      NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(out.x, 119);
        ROBO_CHECK_EQ_INT(event.space, ROBO_INPUT_SPACE_WINDOW);
        ROBO_CHECK_EQ_INT(event.payload.pointer.position.x, 120);
    }

    /* Asking for a plane the route cannot supply is refused rather than answered in the input's own plane,
     * which is the failure a silent mix would produce: a coordinate that looks converted and is not. */
    {
        robo_input_route_t frame_only = input_route_frame_only(10, 20, 400, 300);
        robo_input_event_t event = input_pointer_move(120, 84, 0u, ROBO_INPUT_SPACE_WINDOW, 3u);
        robo_gfx_point_t untouched = { 999, 999 };
        robo_input_diag_t diag;

        ROBO_CHECK_EQ_INT(robo_input_event_position_in(&event, ROBO_INPUT_SPACE_DOCUMENT, &frame_only,
                                                      &untouched, &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
        ROBO_CHECK_EQ_INT(untouched.x, 999);
        ROBO_CHECK_EQ_INT(untouched.y, 999);
    }
}

void robo_test_register_input_coordinates(void)
{
    robo_test_register("input.coordinates.spaces", test_coordinates_spaces);
    robo_test_register("input.coordinates.screen_window", test_coordinates_screen_window);
    robo_test_register("input.coordinates.window_document", test_coordinates_window_document);
    robo_test_register("input.coordinates.generic", test_coordinates_generic);
    robo_test_register("input.coordinates.mixing", test_coordinates_mixing);
}
