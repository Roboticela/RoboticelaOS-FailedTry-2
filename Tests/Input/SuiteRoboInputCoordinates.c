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

static void TestCoordinatesSpaces(void)
{
    RoboCheckEqInt(InputSpaceIsKnown(RoboInputSpaceScreen), 1);
    RoboCheckEqInt(InputSpaceIsKnown(RoboInputSpaceWindow), 1);
    RoboCheckEqInt(InputSpaceIsKnown(RoboInputSpaceApplication), 1);
    RoboCheckEqInt(InputSpaceIsKnown(RoboInputSpaceDocument), 1);
    RoboCheckEqInt(InputSpaceIsKnown(RoboInputSpaceUnknown), 0);
    RoboCheckStrEq(InputSpaceName(RoboInputSpaceScreen), "screen");
    RoboCheckStrEq(InputSpaceName(RoboInputSpaceWindow), "window");
    RoboCheckStrEq(InputSpaceName(RoboInputSpaceApplication), "application");
    RoboCheckStrEq(InputSpaceName(RoboInputSpaceDocument), "document");
    RoboCheckStrEq(InputSpaceName(RoboInputSpaceUnknown), "unknown");

    /* A route remembers exactly what it was given, and answers what it can do with it. */
    {
        InputRoute route;
        InputRoute frameOnly;
        InputRoute clientOnly;

        InputRouteInit(&route);
        RoboCheckEqInt(route.window, RoboInputRouteWindowNone);
        RoboCheckEqInt(route.hasWindowBounds, 0);
        RoboCheckEqInt(route.hasClientBounds, 0);
        RoboCheckEqInt(InputRouteHasScreenGeometry(&route), 0);
        RoboCheckEqInt(InputRouteHasDocumentGeometry(&route), 0);
        RoboCheckEqInt(InputRouteHasScreenGeometry(NULL), 0);
        RoboCheckEqInt(InputRouteHasDocumentGeometry(NULL), 0);
        InputRouteInit(NULL);

        frameOnly = InputRouteFrameOnly(10, 20, 400, 300);
        RoboCheckEqInt(InputRouteHasScreenGeometry(&frameOnly), 1);
        RoboCheckEqInt(InputRouteHasDocumentGeometry(&frameOnly), 0);
        RoboCheckEqInt(frameOnly.window, 7u);

        clientOnly = GetRoboInputRoute(0, 0, 400, 300, 1, 25, 398, 274);
        RoboCheckEqInt(InputRouteHasScreenGeometry(&clientOnly), 1);
        RoboCheckEqInt(InputRouteHasDocumentGeometry(&clientOnly), 1);

        /* A supplied-but-empty rectangle is *not* the same as an unsupplied one: the flag decides, and the
         * zeroes are the geometry of a window at the origin. */
        InputRouteInit(&route);
        route.windowBounds = MakeGfxRect(0, 0, 0, 0);
        route.hasWindowBounds = 1;
        RoboCheckEqInt(InputRouteHasScreenGeometry(&route), 1);
    }
}

/* ------------------------------------------------------------------ */
/* Screen ⟷ window                                                     */
/* ------------------------------------------------------------------ */

static void TestCoordinatesScreenWindow(void)
{
    InputRoute route = GetRoboInputRoute(10, 20, 400, 300, 1, 25, 398, 274);
    GfxPoint windowPoint = { 0, 0 };
    GfxPoint screenPoint = { 0, 0 };
    GfxPoint back = { 0, 0 };
    InputDiag diag;

    RoboCheckEqInt(InputScreenToWindow(MakeGfxPoint(130, 104), &route, &windowPoint, NULL),
                      RoboOk);
    RoboCheckEqInt(windowPoint.x, 120);
    RoboCheckEqInt(windowPoint.y, 84);

    RoboCheckEqInt(InputWindowToScreen(MakeGfxPoint(120, 84), &route, &screenPoint, NULL),
                      RoboOk);
    RoboCheckEqInt(screenPoint.x, 130);
    RoboCheckEqInt(screenPoint.y, 104);

    /* The pair is an exact inverse, in both orders, including for positions outside the frame (a drag that
     * left the window is a legitimate answer this module does not clamp away). */
    RoboCheckEqInt(InputScreenToWindow(screenPoint, &route, &back, NULL), RoboOk);
    RoboCheckEqInt(back.x, 120);
    RoboCheckEqInt(back.y, 84);
    RoboCheckEqInt(InputWindowToScreen(MakeGfxPoint(-40, -70), &route, &screenPoint, NULL),
                      RoboOk);
    RoboCheckEqInt(screenPoint.x, -30);
    RoboCheckEqInt(screenPoint.y, -50);
    RoboCheckEqInt(InputScreenToWindow(screenPoint, &route, &back, NULL), RoboOk);
    RoboCheckEqInt(back.x, -40);
    RoboCheckEqInt(back.y, -70);

    /* A window whose frame sits at a negative origin works; one at the most negative coordinate converts
     * *into* window space only -- the frame's origin cannot be negated exactly, which is the one case where
     * the subtraction has no answer, and it is refused rather than answered approximately. */
    {
        InputRoute far = InputRouteFrameOnly(INT32_MIN, INT32_MIN, 100, 100);

        RoboCheckEqInt(InputScreenToWindow(MakeGfxPoint(5, 5), &far, &windowPoint, &diag),
                          RoboError);
        RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
        RoboCheckEqInt(InputWindowToScreen(MakeGfxPoint(5, 5), &far, &screenPoint, &diag),
                          RoboOk);
        RoboCheckEqInt(screenPoint.x, 5 + INT32_MIN);
    }

    /* Two representable coordinates can differ by more than a coordinate can hold: the direction that would
     * leave the range is refused rather than wrapped, because a wrapped position is a click somewhere else.
     */
    {
        InputRoute wide = InputRouteFrameOnly(INT32_MAX, INT32_MAX, 10, 10);

        RoboCheckEqInt(InputScreenToWindow(MakeGfxPoint(INT32_MIN, INT32_MIN), &wide,
                                                     &windowPoint, &diag),
                          RoboError);
        RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
        RoboCheckEqInt(InputWindowToScreen(MakeGfxPoint(INT32_MAX, 0), &wide, &screenPoint,
                                                     &diag),
                          RoboError);
        RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
        /* ... while a point that still fits converts, at the very edge of the range. */
        RoboCheckEqInt(InputWindowToScreen(MakeGfxPoint(-1, 0), &wide, &screenPoint, NULL),
                          RoboOk);
        RoboCheckEqInt(screenPoint.x, INT32_MAX - 1);
    }

    /* Without the frame rectangle there is nothing to convert with, and the refusal says so in the space
     * domain rather than returning a point that happens to be the input. */
    {
        InputRoute empty;

        InputRouteInit(&empty);
        RoboCheckEqInt(InputScreenToWindow(MakeGfxPoint(1, 2), &empty, &windowPoint, &diag),
                          RoboNotSupported);
        RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
        RoboCheck(diag.message != NULL);
        RoboCheckEqInt(InputWindowToScreen(MakeGfxPoint(1, 2), &empty, &screenPoint, &diag),
                          RoboNotSupported);
        RoboCheckEqInt(InputScreenToWindow(MakeGfxPoint(1, 2), NULL, &windowPoint, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputScreenToWindow(MakeGfxPoint(1, 2), &route, NULL, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputWindowToScreen(MakeGfxPoint(1, 2), NULL, &screenPoint, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* Window ⟷ document                                                   */
/* ------------------------------------------------------------------ */

static void TestCoordinatesWindowDocument(void)
{
    InputRoute route = GetRoboInputRoute(10, 20, 400, 300, 1, 25, 398, 274);
    GfxPoint documentPoint = { 0, 0 };
    GfxPoint windowPoint = { 0, 0 };
    InputDiag diag;

    RoboCheckEqInt(InputWindowToDocument(MakeGfxPoint(120, 84), &route, &documentPoint, NULL),
                      RoboOk);
    RoboCheckEqInt(documentPoint.x, 119);
    RoboCheckEqInt(documentPoint.y, 59);
    RoboCheckEqInt(InputDocumentToWindow(documentPoint, &route, &windowPoint, NULL), RoboOk);
    RoboCheckEqInt(windowPoint.x, 120);
    RoboCheckEqInt(windowPoint.y, 84);

    /* The client inset can be negative (a border drawn outside the client area), and the conversion still
     * inverts exactly. */
    {
        InputRoute outset = GetRoboInputRoute(0, 0, 400, 300, -2, -30, 404, 330);

        RoboCheckEqInt(InputWindowToDocument(MakeGfxPoint(0, 0), &outset, &documentPoint,
                                                       NULL),
                          RoboOk);
        RoboCheckEqInt(documentPoint.x, 2);
        RoboCheckEqInt(documentPoint.y, 30);
        RoboCheckEqInt(InputDocumentToWindow(documentPoint, &outset, &windowPoint, NULL),
                          RoboOk);
        RoboCheckEqInt(windowPoint.x, 0);
        RoboCheckEqInt(windowPoint.y, 0);
    }

    /* Without the client rectangle this pair is refused, and *only* this pair: the frame rectangle is not a
     * substitute, which is the whole reason the route carries two. */
    {
        InputRoute frameOnly = InputRouteFrameOnly(10, 20, 400, 300);

        RoboCheckEqInt(InputWindowToDocument(MakeGfxPoint(1, 2), &frameOnly, &documentPoint,
                                                       &diag),
                          RoboNotSupported);
        RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
        RoboCheckEqInt(InputDocumentToWindow(MakeGfxPoint(1, 2), &frameOnly, &windowPoint,
                                                       &diag),
                          RoboNotSupported);
        RoboCheckEqInt(InputWindowToScreen(MakeGfxPoint(1, 2), &frameOnly, &windowPoint,
                                                     NULL),
                          RoboOk);
        RoboCheckEqInt(InputWindowToDocument(MakeGfxPoint(1, 2), NULL, &documentPoint, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputDocumentToWindow(MakeGfxPoint(1, 2), &route, NULL, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The generic door                                                    */
/* ------------------------------------------------------------------ */

static void TestCoordinatesGeneric(void)
{
    InputRoute route = GetRoboInputRoute(10, 20, 400, 300, 1, 25, 398, 274);
    GfxPoint out = { 0, 0 };
    InputDiag diag;

    /* "It is already in the plane you want" is answered, not refused -- including for a point that no
     * conversion could represent, because none is needed. */
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(INT32_MIN, INT32_MAX),
                                               RoboInputSpaceWindow, RoboInputSpaceWindow, &route,
                                               &out, NULL),
                      RoboOk);
    RoboCheckEqInt(out.x, INT32_MIN);
    RoboCheckEqInt(out.y, INT32_MAX);
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(7, 8), RoboInputSpaceApplication,
                                               RoboInputSpaceApplication, &route, &out, NULL),
                      RoboOk);
    RoboCheckEqInt(out.x, 7);

    /* Screen → document is the composition of the two named conversions, so it has one implementation and
     * the generic door cannot disagree with the two it stands for. */
    {
        GfxPoint viaTwo = { 0, 0 };
        GfxPoint windowMiddle = { 0, 0 };

        RoboCheckEqInt(InputScreenToWindow(MakeGfxPoint(130, 104), &route, &windowMiddle,
                                                     NULL),
                          RoboOk);
        RoboCheckEqInt(InputWindowToDocument(windowMiddle, &route, &viaTwo, NULL), RoboOk);
        RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(130, 104), RoboInputSpaceScreen,
                                                   RoboInputSpaceDocument, &route, &out, NULL),
                          RoboOk);
        RoboCheckEqInt(out.x, viaTwo.x);
        RoboCheckEqInt(out.y, viaTwo.y);
        RoboCheckEqInt(out.x, 119);
        RoboCheckEqInt(out.y, 59);

        /* ... and the other direction is its exact inverse. */
        RoboCheckEqInt(InputConvertPoint(out, RoboInputSpaceDocument, RoboInputSpaceScreen,
                                                   &route, &windowMiddle, NULL),
                          RoboOk);
        RoboCheckEqInt(windowMiddle.x, 130);
        RoboCheckEqInt(windowMiddle.y, 104);
    }

    /* The application plane is named and never converted through: guessing an application's origin is the
     * silent mixing this phase exists to prevent. */
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(1, 2), RoboInputSpaceScreen,
                                               RoboInputSpaceApplication, &route, &out, &diag),
                      RoboNotSupported);
    RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(1, 2), RoboInputSpaceApplication,
                                               RoboInputSpaceWindow, &route, &out, &diag),
                      RoboNotSupported);

    /* Undefined planes are refused, including the "same plane" case: an unset plane is not a plane. */
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(1, 2), RoboInputSpaceUnknown,
                                               RoboInputSpaceWindow, &route, &out, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(1, 2), RoboInputSpaceWindow,
                                               RoboInputSpaceUnknown, &route, &out, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(1, 2), RoboInputSpaceUnknown,
                                               RoboInputSpaceUnknown, &route, &out, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(1, 2), (InputSpace)9,
                                               RoboInputSpaceWindow, &route, &out, &diag),
                      RoboInvalidArgument);

    /* A pair whose geometry is missing is refused rather than answered with a half-conversion. */
    {
        InputRoute frameOnly = InputRouteFrameOnly(10, 20, 400, 300);

        RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(1, 2), RoboInputSpaceScreen,
                                                   RoboInputSpaceDocument, &frameOnly, &out, &diag),
                          RoboNotSupported);
        RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(1, 2), RoboInputSpaceDocument,
                                                   RoboInputSpaceScreen, &frameOnly, &out, &diag),
                          RoboNotSupported);
    }

    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(1, 2), RoboInputSpaceScreen,
                                               RoboInputSpaceWindow, NULL, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(1, 2), RoboInputSpaceScreen,
                                               RoboInputSpaceWindow, &route, NULL, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Mixing planes, and what stops it                                    */
/* ------------------------------------------------------------------ */

static void TestCoordinatesMixing(void)
{
    InputRoute route = GetRoboInputRoute(10, 20, 400, 300, 1, 25, 398, 274);
    GfxPoint out = { 0, 0 };

    /* The same three numbers mean two different places depending on the plane they are declared in, and the
     * conversion is what makes the difference visible: reading a window-space point as if it were a screen
     * point is off by the frame's origin, and the *only* way to get an answer here is to say which plane the
     * input was in. */
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(120, 84), RoboInputSpaceWindow,
                                               RoboInputSpaceScreen, &route, &out, NULL),
                      RoboOk);
    RoboCheckEqInt(out.x, 130);
    RoboCheckEqInt(out.y, 104);
    RoboCheckEqInt(InputConvertPoint(MakeGfxPoint(120, 84), RoboInputSpaceScreen,
                                               RoboInputSpaceScreen, &route, &out, NULL),
                      RoboOk);
    RoboCheckEqInt(out.x, 120);
    RoboCheckEqInt(out.y, 84);

    /* An event that carries a position must name a plane, and one that carries none must not: the two
     * refusals are what make "which plane is this?" an answerable question at every point in the runtime
     * rather than a convention somebody has to remember. */
    {
        InputEvent event = InputPointerMove(120, 84, 0u, RoboInputSpaceWindow, 3u);
        InputEvent unnamed = event;

        unnamed.space = RoboInputSpaceUnknown;
        RoboCheck(InputEventIsValid(&event));
        RoboCheck(!InputEventIsValid(&unnamed));

        /* Converting it does not change it: an event stays in the plane it arrived in, and the plane it was
         * asked for is an answer, not an edit. */
        RoboCheckEqInt(InputEventPositionIn(&event, RoboInputSpaceDocument, &route, &out,
                                                      NULL),
                          RoboOk);
        RoboCheckEqInt(out.x, 119);
        RoboCheckEqInt(event.space, RoboInputSpaceWindow);
        RoboCheckEqInt(event.payload.pointer.position.x, 120);
    }

    /* Asking for a plane the route cannot supply is refused rather than answered in the input's own plane,
     * which is the failure a silent mix would produce: a coordinate that looks converted and is not. */
    {
        InputRoute frameOnly = InputRouteFrameOnly(10, 20, 400, 300);
        InputEvent event = InputPointerMove(120, 84, 0u, RoboInputSpaceWindow, 3u);
        GfxPoint untouched = { 999, 999 };
        InputDiag diag;

        RoboCheckEqInt(InputEventPositionIn(&event, RoboInputSpaceDocument, &frameOnly,
                                                      &untouched, &diag),
                          RoboNotSupported);
        RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
        RoboCheckEqInt(untouched.x, 999);
        RoboCheckEqInt(untouched.y, 999);
    }
}

void TestRegisterInputCoordinates(void)
{
    TestRegister("input.coordinates.spaces", TestCoordinatesSpaces);
    TestRegister("input.coordinates.screen_window", TestCoordinatesScreenWindow);
    TestRegister("input.coordinates.window_document", TestCoordinatesWindowDocument);
    TestRegister("input.coordinates.generic", TestCoordinatesGeneric);
    TestRegister("input.coordinates.mixing", TestCoordinatesMixing);
}
