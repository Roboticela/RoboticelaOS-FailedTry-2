/*
 * RoboGraphics Goal 6.8 — path foundation (geometry / command model).
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <limits.h>
#include <stdint.h>

static void ExpectCmd(const GfxPathCommand *c,
                       GfxPathVerb verb,
                       int32_t x,
                       int32_t y)
{
    RoboCheckEqInt(c->verb, verb);
    if (verb == RoboGfxPathMoveTo || verb == RoboGfxPathLineTo) {
        RoboCheckEqInt(c->point.x, x);
        RoboCheckEqInt(c->point.y, y);
    }
}

static void TestPathEmpty(void)
{
    GfxPath *path = NULL;
    GfxRect b;
    GfxPathIter it;
    GfxPathCommand cmd;

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    RoboCheck(path != NULL);
    RoboCheck(GfxPathIsEmpty(path));
    RoboCheckEqInt((int)GfxPathCommandCount(path), 0);
    RoboCheck(!GfxPathHasCurrentPoint(path));
    RoboCheck(!GfxPathHasOpenContour(path));
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboOk);
    RoboCheck(GfxRectIsEmpty(b));

    GfxPathIterBegin(path, &it);
    RoboCheck(!GfxPathIterNext(&it, &cmd));

    GfxPathRelease(path);
}

static void TestPathMoveLineClose(void)
{
    GfxPath *path = NULL;
    GfxPathCommand c;
    GfxPoint cur;

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(1, 2)),
                      RoboOk);
    RoboCheck(GfxPathHasOpenContour(path));
    RoboCheckEqInt(GfxPathCurrentPoint(path, &cur), RoboOk);
    RoboCheckEqInt(cur.x, 1);
    RoboCheckEqInt(cur.y, 2);

    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(5, 2)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(5, 8)),
                      RoboOk);
    RoboCheckEqInt(GfxPathClose(path), RoboOk);
    RoboCheck(!GfxPathHasOpenContour(path));
    RoboCheckEqInt(GfxPathCurrentPoint(path, &cur), RoboOk);
    RoboCheckEqInt(cur.x, 1);
    RoboCheckEqInt(cur.y, 2);

    RoboCheckEqInt((int)GfxPathCommandCount(path), 4);
    RoboCheckEqInt(GfxPathCommandAt(path, 0u, &c), RoboOk);
    ExpectCmd(&c, RoboGfxPathMoveTo, 1, 2);
    RoboCheckEqInt(GfxPathCommandAt(path, 1u, &c), RoboOk);
    ExpectCmd(&c, RoboGfxPathLineTo, 5, 2);
    RoboCheckEqInt(GfxPathCommandAt(path, 2u, &c), RoboOk);
    ExpectCmd(&c, RoboGfxPathLineTo, 5, 8);
    RoboCheckEqInt(GfxPathCommandAt(path, 3u, &c), RoboOk);
    RoboCheckEqInt(c.verb, RoboGfxPathClose);

    GfxPathRelease(path);
}

static void TestPathIterationOrder(void)
{
    GfxPath *path = NULL;
    GfxPathIter it;
    GfxPathCommand c;
    int n = 0;

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(0, 0)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(10, 0)),
                      RoboOk);
    RoboCheckEqInt(GfxPathClose(path), RoboOk);
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(20, 20)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(30, 40)),
                      RoboOk);

    GfxPathIterBegin(path, &it);
    RoboCheck(GfxPathIterNext(&it, &c));
    ExpectCmd(&c, RoboGfxPathMoveTo, 0, 0);
    n++;
    RoboCheck(GfxPathIterNext(&it, &c));
    ExpectCmd(&c, RoboGfxPathLineTo, 10, 0);
    n++;
    RoboCheck(GfxPathIterNext(&it, &c));
    RoboCheckEqInt(c.verb, RoboGfxPathClose);
    n++;
    RoboCheck(GfxPathIterNext(&it, &c));
    ExpectCmd(&c, RoboGfxPathMoveTo, 20, 20);
    n++;
    RoboCheck(GfxPathIterNext(&it, &c));
    ExpectCmd(&c, RoboGfxPathLineTo, 30, 40);
    n++;
    RoboCheck(!GfxPathIterNext(&it, &c));
    RoboCheckEqInt(n, 5);

    GfxPathRelease(path);
}

static void TestPathMultipleSubpaths(void)
{
    GfxPath *path = NULL;

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    /* Open subpath */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(0, 0)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(1, 0)),
                      RoboOk);
    /* New MoveTo without Close — open contour ends without Close verb */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(10, 10)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(11, 10)),
                      RoboOk);
    RoboCheckEqInt(GfxPathClose(path), RoboOk);

    RoboCheckEqInt((int)GfxPathCommandCount(path), 5);
    RoboCheck(GfxPathHasCurrentPoint(path));
    RoboCheck(!GfxPathHasOpenContour(path));

    GfxPathRelease(path);
}

static void TestPathCloseBehavior(void)
{
    GfxPath *path = NULL;
    GfxPathCommand c;

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);

    /* Close with no open contour: no-op */
    RoboCheckEqInt(GfxPathClose(path), RoboOk);
    RoboCheck(GfxPathIsEmpty(path));

    /* Close after MoveTo alone */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(3, 4)),
                      RoboOk);
    RoboCheckEqInt(GfxPathClose(path), RoboOk);
    RoboCheckEqInt((int)GfxPathCommandCount(path), 2);
    RoboCheckEqInt(GfxPathCommandAt(path, 1u, &c), RoboOk);
    RoboCheckEqInt(c.verb, RoboGfxPathClose);

    /* Repeated close: no-op */
    RoboCheckEqInt(GfxPathClose(path), RoboOk);
    RoboCheckEqInt((int)GfxPathCommandCount(path), 2);

    /* LineTo after Close is invalid until MoveTo */
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(9, 9)),
                      RoboInvalidArgument);

    /* New subpath after Close */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(7, 8)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(9, 8)),
                      RoboOk);
    RoboCheckEqInt((int)GfxPathCommandCount(path), 4);

    GfxPathRelease(path);
}

static void TestPathLineWithoutMove(void)
{
    GfxPath *path = NULL;

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(1, 1)),
                      RoboInvalidArgument);
    RoboCheck(GfxPathIsEmpty(path));
    GfxPathRelease(path);
}

static void TestPathBounds(void)
{
    GfxPath *path = NULL;
    GfxRect b;

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);

    /* Empty */
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboOk);
    RoboCheck(GfxRectIsEmpty(b));

    /* Single point (MoveTo only) → 1×1 covering that pixel */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(5, 7)),
                      RoboOk);
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboOk);
    RoboCheckEqInt(b.x, 5);
    RoboCheckEqInt(b.y, 7);
    RoboCheckEqInt(b.width, 1);
    RoboCheckEqInt(b.height, 1);

    GfxPathReset(path);

    /* Horizontal */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(2, 4)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(8, 4)),
                      RoboOk);
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboOk);
    RoboCheckEqInt(b.x, 2);
    RoboCheckEqInt(b.y, 4);
    RoboCheckEqInt(b.width, 7);
    RoboCheckEqInt(b.height, 1);

    GfxPathReset(path);

    /* Vertical */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(3, 1)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(3, 6)),
                      RoboOk);
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboOk);
    RoboCheckEqInt(b.x, 3);
    RoboCheckEqInt(b.y, 1);
    RoboCheckEqInt(b.width, 1);
    RoboCheckEqInt(b.height, 6);

    GfxPathReset(path);

    /* Diagonal + Close (Close adds no new extrema) */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(0, 0)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(4, 3)),
                      RoboOk);
    RoboCheckEqInt(GfxPathClose(path), RoboOk);
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboOk);
    RoboCheckEqInt(b.x, 0);
    RoboCheckEqInt(b.y, 0);
    RoboCheckEqInt(b.width, 5);
    RoboCheckEqInt(b.height, 4);

    GfxPathReset(path);

    /* Negatives + multi-subpath */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(-10, -5)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(-2, 0)),
                      RoboOk);
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(1, 8)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(3, 2)),
                      RoboOk);
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboOk);
    RoboCheckEqInt(b.x, -10);
    RoboCheckEqInt(b.y, -5);
    RoboCheckEqInt(b.width, 14);  /* [-10, 3] → 14 */
    RoboCheckEqInt(b.height, 14); /* [-5, 8] → 14 */

    GfxPathRelease(path);
}

static void TestPathDegenerate(void)
{
    GfxPath *path = NULL;
    GfxRect b;

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);

    /* Zero-length segment */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(4, 4)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(4, 4)),
                      RoboOk);
    RoboCheckEqInt((int)GfxPathCommandCount(path), 2);
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboOk);
    RoboCheckEqInt(b.width, 1);
    RoboCheckEqInt(b.height, 1);

    /* Repeated MoveTo */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(4, 4)),
                      RoboOk);
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(9, 9)),
                      RoboOk);
    RoboCheckEqInt((int)GfxPathCommandCount(path), 4);

    GfxPathRelease(path);
}

static void TestPathNumeric(void)
{
    GfxPath *path = NULL;
    GfxRect b;

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);

    RoboCheckEqInt(
        GfxPathMoveTo(path, MakeGfxPoint(INT32_MIN, INT32_MIN)),
        RoboOk);
    RoboCheckEqInt(
        GfxPathLineTo(path, MakeGfxPoint(INT32_MIN + 1, INT32_MIN)),
        RoboOk);
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboOk);
    RoboCheckEqInt(b.x, INT32_MIN);
    RoboCheckEqInt(b.y, INT32_MIN);
    RoboCheckEqInt(b.width, 2);
    RoboCheckEqInt(b.height, 1);

    GfxPathReset(path);
    RoboCheckEqInt(
        GfxPathMoveTo(path, MakeGfxPoint(INT32_MAX, 0)), RoboOk);
    /* Exclusive right = INT32_MAX+1 cannot fit int32 */
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboInvalidArgument);

    GfxPathReset(path);
    RoboCheckEqInt(
        GfxPathMoveTo(path, MakeGfxPoint(INT32_MAX - 1, 0)),
        RoboOk);
    RoboCheckEqInt(GfxPathBounds(path, &b), RoboOk);
    RoboCheckEqInt(b.x, INT32_MAX - 1);
    RoboCheckEqInt(b.width, 1);

    GfxPathRelease(path);
}

static void TestPathLifecycle(void)
{
    GfxPath *path = NULL;
    GfxPathId id;

    RoboCheckEqInt(GfxPathCreate(NULL), RoboInvalidArgument);
    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    id = GetGfxPathId(path);
    RoboCheck(id != RoboGfxPathIdNone);

    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(1, 1)),
                      RoboOk);
    RoboCheckEqInt(GfxPathLineTo(path, MakeGfxPoint(2, 2)),
                      RoboOk);
    RoboCheck(!GfxPathIsEmpty(path));

    GfxPathReset(path);
    RoboCheck(GfxPathIsEmpty(path));
    RoboCheck(GetGfxPathId(path) == id); /* id stable */

    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(0, 0)),
                      RoboOk);
    GfxPathClear(path);
    RoboCheck(GfxPathIsEmpty(path));
    RoboCheckEqInt((int)GfxPathCommandCount(path), 0);

    /* Reuse after clear */
    RoboCheckEqInt(GfxPathMoveTo(path, MakeGfxPoint(8, 9)),
                      RoboOk);
    RoboCheckEqInt((int)GfxPathCommandCount(path), 1);

    GfxPathRetain(path);
    GfxPathRelease(path);
    GfxPathRelease(path);
}

static void TestPathErrors(void)
{
    GfxPath *path = NULL;
    GfxPathCommand c;
    GfxRect b;
    GfxPoint p;

    RoboCheckEqInt(GfxPathMoveTo(NULL, MakeGfxPoint(0, 0)),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxPathLineTo(NULL, MakeGfxPoint(0, 0)),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxPathClose(NULL), RoboInvalidArgument);
    RoboCheckEqInt(GfxPathBounds(NULL, &b), RoboInvalidArgument);

    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    RoboCheckEqInt(GfxPathBounds(path, NULL), RoboInvalidArgument);
    RoboCheckEqInt(GfxPathCommandAt(path, 0u, &c),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxPathCurrentPoint(path, &p),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxPathCommandAt(path, 0u, NULL),
                      RoboInvalidArgument);

    GfxPathRelease(path);
    GfxPathRelease(NULL);
    GfxPathRetain(NULL);
    GfxPathReset(NULL);
    GfxPathClear(NULL);
}

void TestRegisterGraphicsPath(void)
{
    TestRegister("graphics.path.empty", TestPathEmpty);
    TestRegister("graphics.path.move_line_close",
                       TestPathMoveLineClose);
    TestRegister("graphics.path.iteration", TestPathIterationOrder);
    TestRegister("graphics.path.subpaths", TestPathMultipleSubpaths);
    TestRegister("graphics.path.close", TestPathCloseBehavior);
    TestRegister("graphics.path.line_no_move",
                       TestPathLineWithoutMove);
    TestRegister("graphics.path.bounds", TestPathBounds);
    TestRegister("graphics.path.degenerate", TestPathDegenerate);
    TestRegister("graphics.path.numeric", TestPathNumeric);
    TestRegister("graphics.path.lifecycle", TestPathLifecycle);
    TestRegister("graphics.path.errors", TestPathErrors);
}
