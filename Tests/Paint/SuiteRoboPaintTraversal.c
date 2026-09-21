/*
 * RoboPaint traversal suite (Phase 11.1) — paint.traversal.*
 *
 * The half of the phase's tests that asks the traversal through a **real document**: the
 * real tree builder, the real cascade, the real DOM bridge, a real layout pass, and then a
 * real paint pass. `SuiteRoboPaint.c` asks each piece on its own; this one asks whether
 * the pieces compose, and the difference is not academic -- a validity rule that is right
 * in isolation is still wrong if the traversal feeds it a box four levels deep, and an
 * accumulation that is right for one box is still wrong if the origin it adds is the
 * sibling's.
 *
 * The claims each group of cases pins, in the order they appear:
 *
 *   order and shape   document order, parents before children, an explicit marker pair
 *                     per placed box, the depth that makes the tree recoverable from a
 *                     flat list, and the two cases where a pass produces *nothing* (a
 *                     document whose root generates no box, and a tree whose geometry is
 *                     not final -- which is a refusal rather than a silent empty list);
 *
 *   decorations       emit only when it draws: a declared background, one operation per
 *                     border side whose *used* width is above zero, `currentColor`
 *                     resolved from the element's own `color` and counted when it cannot
 *                     be;
 *
 *   geometry          the two coordinate spaces, held together by the only assertion that
 *                     can hold them: for every operation, the local boxes must be
 *                     field-for-field what `PaintBoxesOf` reads out of the tree,
 *                     and the page-space boxes must be the local ones plus the
 *                     operation's own origin;
 *
 *   clip              the boundary in force, accumulated per axis, with the padding box as
 *                     the edge -- and the axis split, which is the case a single
 *                     rectangle cannot express;
 *
 *   counts            `visited` against `objects`, `boxes` against the marker pairs,
 *                     `ops` against the list, and the pass count across two passes;
 *
 *   determinism      the same tree twice, byte-identical, and the same document built into
 *                     a second tree, which is what rules out a conclusion that depended on
 *                     a pointer or on an allocation.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends
 * on no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A document that gives every layer something to do                   */
/* ------------------------------------------------------------------ */

/*
 * The fixture stylesheet's own base, and the second line is the one a reader is owed an
 * explanation for: an element nothing styles computes to `display: inline`, and an
 * inline-level box in a block formatting context is a box this build gives **no** border
 * and **no** padding (`RoboLayoutPadding.h` argues why a transparent inline box's
 * rectangle is the union of its contents rather than a size to inflate). Every case below
 * is about what a *painted block box* does with its insets -- which side draws, where the
 * band is, what the clip edge is -- so the fixtures say once that these elements are the
 * block containers, rather than leaving each case to re-declare it. The initial value is
 * therefore still exercised, deliberately: `<head>` is a box nothing styles, and it is the
 * unplaced box every count in this file is calibrated against.
 *
 * `* { display: block }` would be shorter and would hide exactly that fact.
 */
#define PAINT_BASE_CSS                                 \
    "html, body, div, section, aside, figure, p { display: block }"

/** The `BEGIN_BOX` operation of @p node, or 0 when it has none. */
static int PaintBeginOf(const PaintResult *result,
                          LayoutNodeId node,
                          PaintOperation *out)
{
    size_t index;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            return 0;
        }
        if (operation.kind == RoboPaintOpBeginBox && operation.node == node) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
    }
    return 0;
}

/** The `END_BOX` operation of @p node, or 0 when it has none. */
static int PaintEndOf(const PaintResult *result,
                        LayoutNodeId node,
                        PaintOperation *out)
{
    size_t index;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            return 0;
        }
        if (operation.kind == RoboPaintOpEndBox && operation.node == node) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
    }
    return 0;
}

/** The first operation of @p kind on @p node, or 0 when it has none. */
static int PaintOpOf(const PaintResult *result,
                       PaintOpKind kind,
                       LayoutNodeId node,
                       PaintOperation *out)
{
    size_t index;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            return 0;
        }
        if (operation.kind == kind && operation.node == node) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
    }
    return 0;
}

/** The index of the first operation of @p kind on @p node, or `(size_t)-1`. */
static size_t PaintIndexOf(const PaintResult *result,
                             PaintOpKind kind,
                             LayoutNodeId node)
{
    size_t index;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            return (size_t)-1;
        }
        if (operation.kind == kind && operation.node == node) {
            return index;
        }
    }
    return (size_t)-1;
}

/**
 * Assert the two coordinate spaces agree with the tree, for **every** operation.
 *
 * The phase's central claim, asked of a whole document rather than one box: every
 * operation's boxes minus its origin must be exactly what RoboLayout wrote, and the
 * page-space boxes must be the local ones plus that origin. A traversal that mixed up a
 * sibling's origin, or carried the wrong containing block down one level, passes every
 * count in this suite and fails here.
 */
static void PaintAllBoxesMatchLayout(PaintFixtureDoc *fixture,
                                         const PaintResult *result)
{
    size_t index;

    RoboCheck(PaintListCount(result->list) > 0u);
    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;
        PaintBoxes local;
        PaintBoxes shifted;

        RoboCheckEqInt(PaintListAt(result->list, index, &operation), RoboOk);
        /* Every operation in a list is a record, because the list refuses anything else
         * -- so this is a check of the container's promise as much as of the walk's. */
        RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
        PaintLocalBoxesMatchLayout(fixture, operation);

        memset(&local, 0, sizeof(local));
        memset(&shifted, 0, sizeof(shifted));
        RoboCheckEqInt(PaintOperationLocalBoxes(&operation, &local, NULL), RoboOk);
        RoboCheckEqInt(PaintBoxesTranslate(&local, operation.origin.x,
                                                     operation.origin.y, &shifted, NULL),
                          RoboOk);
        if (!PaintBoxesEqual(&shifted, &operation.boxes)) {
            TestFail(__FILE__, __LINE__,
                           "operation %d on node %d is not its local boxes plus its origin",
                           (int)operation.kind, (int)operation.node);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 1. Order and shape                                                  */
/* ------------------------------------------------------------------ */

static void TestTraversalRefusalsAndEmpty(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintContext *context = NULL;

    /*
     * A **document that builds no boxes at all**: the root element generates none, and
     * nothing inside a `display: none` subtree generates one either -- so there is not
     * even a root for the traversal to start from. A legitimate pass that paints nothing
     * rather than an error: no root box is an answer, not an absence.
     */
    PaintBegin(&fixture, "<html><body><div class=\"card\"></div></body></html>",
                "html { display: none }");
    RoboCheckEqInt((int)LayoutTreeObjectCount(fixture.tree), 0u);
    RoboCheckEqInt((int)LayoutTreeRoot(fixture.tree),
                      (int)RoboLayoutNodeIdNone);
    PaintLayout(&fixture, 800, 600);

    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)PaintListCount(result.list), 0);
    RoboCheckEqInt((int)result.stats.objects, 0);
    RoboCheckEqInt((int)result.stats.visited, 0);
    RoboCheckEqInt((int)result.stats.ops, 0);
    RoboCheckEqInt((int)result.stats.passes, 1);
    PaintResultDispose(&result);
    PaintEnd(&fixture);

    /*
     * A tree whose geometry is not final is *refused*, not painted: a list built from
     * half-computed geometry is a wrong list, and a caller who wanted a preview can run a
     * layout pass first. The refusal is an **outcome** and not a failure -- the pass
     * reached a conclusion, so the code is OK and the diagnosis is the fourth outcome.
     */
    PaintBegin(&fixture, "<html><body><div class=\"card\"></div></body></html>",
                PAINT_BASE_CSS " .card { width: 20px; height: 10px }");
    RoboCheckEqInt(LayoutTreeNeedsLayout(fixture.tree), 1);
    PaintResultInit(&result);
    RoboCheckEqInt(PaintContextCreate(&context, NULL), RoboOk);
    PaintRun(&fixture, context, result.list);
    RoboCheckEqInt((int)PaintContextOutcome(context),
                      (int)RoboPaintPassStale);
    /* A refused pass hands back an **empty** list rather than the previous pass's, which
     * is the only answer a caller can act on. */
    RoboCheckEqInt((int)PaintListCount(result.list), 0);
    PaintContextStats(context, &result.stats);
    RoboCheckEqInt((int)result.stats.ops, 0);
    RoboCheckEqInt((int)result.stats.visited, 0);
    RoboCheckEqInt((int)result.stats.passes, 1);

    /* And the same list, once the geometry *is* final, paints: the refusal was about the
     * tree's state and not about the document. */
    PaintLayout(&fixture, 800, 600);
    PaintRun(&fixture, context, result.list);
    RoboCheckEqInt((int)PaintContextOutcome(context),
                      (int)RoboPaintPassPainted);
    RoboCheck(PaintListCount(result.list) > 0u);
    PaintContextDestroy(context);
    PaintResultDispose(&result);
    PaintEnd(&fixture);

    /* The argument refusals, and the defaults a context that has run nothing answers. */
    PaintBegin(&fixture, "<html><body></body></html>", PAINT_BASE_CSS);
    PaintLayout(&fixture, 800, 600);
    PaintResultInit(&result);
    RoboCheckEqInt(PaintContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(PaintContextRun(NULL, fixture.tree, result.list, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintContextRun(context, NULL, result.list, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintContextRun(context, fixture.tree, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)PaintContextOutcome(context), (int)RoboPaintPassFailed);
    RoboCheckEqInt(PaintContextCreate(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)PaintContextOutcome(NULL), (int)RoboPaintPassNone);
    RoboCheck(PaintContextTree(NULL) == NULL);
    PaintContextStats(NULL, &result.stats);
    RoboCheckEqInt((int)result.stats.objects, 0);
    PaintContextStats(NULL, NULL); /* safe */
    PaintContextDestroy(context);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTraversalSingleBox(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId htmlBox;
    LayoutNodeId bodyBox;
    LayoutNodeId card;
    PaintOperation begin;
    PaintOperation end;
    PaintOperation bodyBegin;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         PAINT_BASE_CSS " .card { width: 100px; height: 40px }");
    htmlBox = PaintBoxByTag(&fixture, "html");
    bodyBox = PaintBoxByTag(&fixture, "body");
    card = PaintBox(&fixture, "card");
    /* The root box is the root *element*'s, which is what makes the tree's root and the
     * document's document element the same thing. */
    RoboCheckEqInt((int)htmlBox, (int)LayoutTreeRoot(fixture.tree));

    PaintOnce(&fixture, &result);

    /* Three placed boxes -- html, body and the card -- and one unplaced `<head>`, which
     * is the parser's own element and the box the layout pass declines. */
    RoboCheckEqInt((int)result.stats.objects, 4);
    RoboCheckEqInt((int)result.stats.visited, 4);
    RoboCheckEqInt((int)result.stats.boxes, 3);
    RoboCheckEqInt((int)result.stats.unplaced, 1);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBeginBox), 3);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpEndBox), 3);
    /* The content boundary is emitted for every placed box, so it is as many records as
     * there are boxes -- and the sequence is three structural triples and nothing that
     * draws, because this document declares no background and no border. */
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpContent), 3);
    RoboCheckEqInt((int)result.stats.ops, (int)PaintListCount(result.list));
    RoboCheckEqInt((int)result.stats.ops, 9);

    /* The root's containing block is the initial containing block, so its origin is the
     * page origin -- and it is the one place an origin is not another box's content. */
    RoboCheckEqInt(PaintBeginOf(&result, htmlBox, &begin), 1);
    RoboCheckEqInt((int)begin.depth, 0);
    RoboCheckEqInt(begin.origin.x, 0);
    RoboCheckEqInt(begin.origin.y, 0);
    RoboCheckEqInt(PaintBeginOf(&result, bodyBox, &bodyBegin), 1);
    RoboCheckEqInt((int)bodyBegin.depth, 1);
    RoboCheckEqInt(PaintBeginOf(&result, card, &begin), 1);
    RoboCheckEqInt((int)begin.depth, 2);

    /* Parents before children and the closing marker after them: the property a
     * rasteriser walks once and keeps a clip stack for. */
    RoboCheck(PaintIndexOf(&result, RoboPaintOpBeginBox, htmlBox) <
               PaintIndexOf(&result, RoboPaintOpBeginBox, bodyBox));
    RoboCheck(PaintIndexOf(&result, RoboPaintOpBeginBox, bodyBox) <
               PaintIndexOf(&result, RoboPaintOpBeginBox, card));
    RoboCheck(PaintIndexOf(&result, RoboPaintOpBeginBox, card) <
               PaintIndexOf(&result, RoboPaintOpEndBox, card));
    RoboCheck(PaintIndexOf(&result, RoboPaintOpEndBox, card) <
               PaintIndexOf(&result, RoboPaintOpEndBox, bodyBox));
    /* The content boundary of a parent precedes its children's opening markers, and a
     * box's own boundary comes after its own opening marker: the shape that makes each
     * box's own records a contiguous group. */
    RoboCheck(PaintIndexOf(&result, RoboPaintOpBeginBox, bodyBox) <
               PaintIndexOf(&result, RoboPaintOpContent, bodyBox));
    RoboCheck(PaintIndexOf(&result, RoboPaintOpContent, bodyBox) <
               PaintIndexOf(&result, RoboPaintOpBeginBox, card));
    RoboCheck(PaintIndexOf(&result, RoboPaintOpContent, card) <
               PaintIndexOf(&result, RoboPaintOpEndBox, card));
    RoboCheck(PaintBeginOf(&result, card, &begin) == 1);
    RoboCheck(PaintEndOf(&result, card, &end) == 1);
    /* The pair closes over the same rectangle: an `END_BOX` that reported different
     * geometry from its `BEGIN_BOX` would be two answers to one question. */
    RoboCheckEqInt(PaintBoxesEqual(&begin.boxes, &end.boxes), 1);

    PaintAllBoxesMatchLayout(&fixture, &result);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTraversalSiblingsInOrder(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId boxes[3];
    size_t index;

    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"one\"></div>"
                         "<div class=\"two\"></div>"
                         "<div class=\"three\"></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .one, .two, .three { width: 50px; height: 10px }");

    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)result.stats.boxes, 5); /* html, body, and three divs */
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBeginBox), 5);

    /* Document order, and nothing else: the three siblings' markers appear in the order
     * the DOM gave them, which is what makes a paint list reproducible from a tree. */
    boxes[0] = PaintBox(&fixture, "one");
    boxes[1] = PaintBox(&fixture, "two");
    boxes[2] = PaintBox(&fixture, "three");
    for (index = 0u; index + 1u < 3u; index++) {
        size_t a = PaintIndexOf(&result, RoboPaintOpBeginBox, boxes[index]);
        size_t b = PaintIndexOf(&result, RoboPaintOpBeginBox, boxes[index + 1u]);

        RoboCheck(a != (size_t)-1);
        RoboCheck(b != (size_t)-1);
        RoboCheck(a < b);
    }

    /* Their origins are the body's content origin -- one containing block, so one origin,
     * and each box's own rectangle is where layout put it relative to that. */
    {
        PaintOperation first;
        PaintOperation second;

        RoboCheckEqInt(PaintBeginOf(&result, boxes[0], &first), 1);
        RoboCheckEqInt(PaintBeginOf(&result, boxes[1], &second), 1);
        RoboCheckEqInt(first.origin.x, second.origin.x);
        RoboCheckEqInt(first.origin.y, second.origin.y);
        RoboCheck(second.boxes.border.y >= first.boxes.border.y + first.boxes.border.height);
    }

    PaintAllBoxesMatchLayout(&fixture, &result);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTraversalEmptyElement(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId empty;
    PaintOperation begin;
    PaintOperation end;

    /* A box with no children and no size is still a placement, so it still gets a marker
     * pair -- and an empty rectangle is a *predicate* rather than a failure. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"empty\"></div></body></html>",
                         PAINT_BASE_CSS " .empty { width: 0; height: 0 }");
    empty = PaintBox(&fixture, "empty");

    PaintOnce(&fixture, &result);
    RoboCheckEqInt(PaintBeginOf(&result, empty, &begin), 1);
    RoboCheckEqInt(PaintEndOf(&result, empty, &end), 1);
    RoboCheckEqInt(begin.boxes.border.width, 0);
    RoboCheckEqInt(begin.boxes.border.height, 0);
    RoboCheckEqInt(PaintBoxesIsValid(&begin.boxes), 1);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. Decorations: emitted only when they draw                         */
/* ------------------------------------------------------------------ */

static void TestTraversalBackground(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId painted;
    LayoutNodeId bare;
    LayoutNodeId clear;
    PaintOperation begin;
    PaintOperation background;

    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"painted\"></div>"
                         "<div class=\"bare\"></div>"
                         "<div class=\"clear\"></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .painted, .bare, .clear { width: 40px; height: 10px }"
                         " .painted { background-color: #ff0000 }"
                         " .clear { background-color: transparent }");
    painted = PaintBox(&fixture, "painted");
    bare = PaintBox(&fixture, "bare");
    clear = PaintBox(&fixture, "clear");

    PaintOnce(&fixture, &result);

    /* One background operation, for the one box that declared one: an undeclared
     * background is `transparent` and a declared transparent one is the same answer, so
     * neither emits an operation that draws nothing. */
    RoboCheckEqInt((int)result.stats.backgrounds, 1);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBackground), 1);
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBackground, painted, &background),
                      1);
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBackground, bare, NULL), 0);
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBackground, clear, NULL), 0);

    /* The colour is the declaration's, and the painting area is the **border box** --
     * CSS's initial `background-clip`. */
    PaintColorEq(background.color, CssColorMake(255, 0, 0, 255),
                   "the background colour");
    RoboCheckEqInt(PaintBeginOf(&result, painted, &begin), 1);
    PaintRectEq(background.boxes.border, begin.boxes.border, "the background's area");
    RoboCheckEqInt((int)background.color.kind, (int)RoboCssColorRgba);

    PaintAllBoxesMatchLayout(&fixture, &result);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTraversalBorderSides(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId solid;
    LayoutNodeId noneStyle;
    LayoutNodeId zeroWidth;
    PaintOperation begin;
    PaintOperation side;
    CssBoxSide boxSide;

    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"solid\"></div>"
                         "<div class=\"nostyle\"></div>"
                         "<div class=\"zero\"></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .solid, .nostyle, .zero { width: 100px; height: 20px }"
                         " .solid { border: 2px solid #0000ff }"
                         " .nostyle { border: 5px none #000000 }"
                         " .zero { border: 0 solid #000000 }");
    solid = PaintBox(&fixture, "solid");
    noneStyle = PaintBox(&fixture, "nostyle");
    zeroWidth = PaintBox(&fixture, "zero");

    PaintOnce(&fixture, &result);

    /* One operation per side whose **used** width is above zero, which is why a
     * `border: 5px none` side -- a declared five that costs no room -- emits nothing and
     * needs no second decision here. */
    RoboCheckEqInt((int)result.stats.borderSides, 4);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBorderTop), 1);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBorderRight), 1);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBorderBottom), 1);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBorderLeft), 1);
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBorderTop, noneStyle, NULL), 0);
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBorderTop, zeroWidth, NULL), 0);

    RoboCheckEqInt(PaintBeginOf(&result, solid, &begin), 1);
    /* The top band spans the border box's full width and stops where the padding box
     * begins, which for a 2px border is the border box's own top edge plus 2. */
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBorderTop, solid, &side), 1);
    RoboCheckEqInt(PaintOperationSide(&side, &boxSide), RoboOk);
    RoboCheckEqInt((int)boxSide, (int)RoboCssBoxSideTop);
    {
        LayoutRect band;

        memset(&band, 0, sizeof(band));
        RoboCheckEqInt(PaintOperationSideBand(&side, &band, NULL), RoboOk);
        RoboCheckEqInt(band.x, begin.boxes.border.x);
        RoboCheckEqInt(band.y, begin.boxes.border.y);
        RoboCheckEqInt(band.width, begin.boxes.border.width);
        RoboCheckEqInt(band.height, 2);
    }
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBorderLeft, solid, &side), 1);
    {
        LayoutRect band;

        memset(&band, 0, sizeof(band));
        RoboCheckEqInt(PaintOperationSideBand(&side, &band, NULL), RoboOk);
        RoboCheckEqInt(band.width, 2);
        RoboCheckEqInt(band.height, begin.boxes.border.height);
    }
    /* Every side carries the declared colour. */
    {
        const PaintOpKind kinds[RoboCssBoxSideCount] = {
            RoboPaintOpBorderTop, RoboPaintOpBorderRight,
            RoboPaintOpBorderBottom, RoboPaintOpBorderLeft
        };
        size_t index;

        for (index = 0u; index < (size_t)RoboCssBoxSideCount; index++) {
            RoboCheckEqInt(PaintOpOf(&result, kinds[index], solid, &side), 1);
            PaintColorEq(side.color, CssColorMake(0, 0, 255, 255),
                           "a border side's colour");
        }
    }

    PaintAllBoxesMatchLayout(&fixture, &result);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTraversalBorderColorInitial(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId initial;
    LayoutNodeId explicitColor;
    PaintOperation side;

    /*
     * An undeclared border colour is CSS's initial `currentColor`, and resolving it is
     * exactly the job of the layer that has an element -- `RoboLayoutBorder.h` says the
     * used record carries a `CURRENT` colour because "only the layer with an element can
     * resolve one", and that layer is this one.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"initial\"></div>"
                         "<div class=\"explicit\"></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .initial, .explicit { width: 60px; height: 10px;"
                         "   border: 1px solid }"
                         " .initial { color: #00ff00 }"
                         " .explicit { color: #00ff00; border-color: #123456 }");
    initial = PaintBox(&fixture, "initial");
    explicitColor = PaintBox(&fixture, "explicit");

    PaintOnce(&fixture, &result);
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBorderTop, initial, &side), 1);
    PaintColorEq(side.color, CssColorMake(0, 255, 0, 255),
                   "an undeclared border colour resolved from `color`");
    RoboCheckEqInt((int)side.color.kind, (int)RoboCssColorRgba);

    /* And a declared colour wins over the element's `color`, which is what makes the
     * resolution above a fallback rather than an override. */
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBorderTop, explicitColor, &side),
                      1);
    PaintColorEq(side.color, CssColorMake(0x12, 0x34, 0x56, 255),
                   "a declared border colour");

    RoboCheckEqInt((int)result.stats.contextualColors, 0);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Geometry and the clip in force                                   */
/* ------------------------------------------------------------------ */

static void TestTraversalBoxModelGeometry(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId outer;
    LayoutNodeId inner;
    PaintOperation outerBegin;
    PaintOperation innerBegin;
    PaintBoxes fromLayout;

    /* margin -> border -> padding -> content, and the *derived* relationships asserted
     * rather than a pile of magic numbers: the content origin of the outer box is where
     * the inner box's containing block starts, and the inner box's paint coordinates are
     * its layout coordinates plus that origin. */
    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"outer\"><section class=\"inner\"></section></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .outer { width: 200px; height: 100px; margin: 10px 20px;"
                         "   padding: 5px; border: 2px solid #000000 }"
                         " .inner { width: 50px; height: 20px; padding: 3px;"
                         "   border: 1px solid #000000 }");
    outer = PaintBox(&fixture, "outer");
    inner = PaintBox(&fixture, "inner");

    PaintOnce(&fixture, &result);
    RoboCheckEqInt(PaintBeginOf(&result, outer, &outerBegin), 1);
    RoboCheckEqInt(PaintBeginOf(&result, inner, &innerBegin), 1);

    /* The inner box's containing block is the outer's content box, in paint coordinates
     * -- so its origin is that content box's origin, and nothing else. */
    RoboCheckEqInt(innerBegin.origin.x, outerBegin.boxes.content.x);
    RoboCheckEqInt(innerBegin.origin.y, outerBegin.boxes.content.y);

    /* The outer's three rectangles are each inset from the one outside it by exactly the
     * declared insets: 2 of border, then 5 of padding. */
    RoboCheckEqInt(outerBegin.boxes.padding.x - outerBegin.boxes.border.x, 2);
    RoboCheckEqInt(outerBegin.boxes.padding.y - outerBegin.boxes.border.y, 2);
    RoboCheckEqInt(outerBegin.boxes.content.x - outerBegin.boxes.padding.x, 5);
    RoboCheckEqInt(outerBegin.boxes.content.y - outerBegin.boxes.padding.y, 5);
    RoboCheckEqInt(outerBegin.boxes.border.width -
                          outerBegin.boxes.content.width, 14);
    RoboCheckEqInt(outerBegin.boxes.border.height -
                          outerBegin.boxes.content.height, 14);
    RoboCheckEqInt(outerBegin.boxes.insetsMeasured, 1);

    /* The levels are geometrically distinct, which is what makes a border band and a
     * background different regions rather than one region drawn twice. */
    RoboCheckEqInt(PaintBoxesIsValid(&outerBegin.boxes), 1);

    /*
     * And the accumulation really happened: the inner box's *paint* rectangle is its
     * layout rectangle moved by its containing block's content origin, which is a
     * non-zero point here, so the two spaces are distinguishable rather than coincidentally
     * equal. A build that skipped the accumulation would report the inner box at the page
     * origin -- inside its parent's border, which is what makes this a check and not a
     * restatement.
     */
    {
        PaintOperation innerBegin;

        RoboCheckEqInt(PaintBeginOf(&result, inner, &innerBegin), 1);
        memset(&fromLayout, 0, sizeof(fromLayout));
        RoboCheckEqInt(PaintBoxesOf(fixture.tree, inner, &fromLayout, NULL),
                          RoboOk);
        RoboCheckEqInt(PaintBoxesEqual(&fromLayout, &innerBegin.boxes), 0);
        RoboCheckEqInt(innerBegin.origin.x, outerBegin.boxes.content.x);
        RoboCheckEqInt(innerBegin.boxes.border.x,
                          fromLayout.border.x + innerBegin.origin.x);
        RoboCheckEqInt(innerBegin.boxes.border.y,
                          fromLayout.border.y + innerBegin.origin.y);
        RoboCheck((int)innerBegin.origin.x > 0);
        RoboCheck((int)innerBegin.origin.y > 0);
    }

    PaintAllBoxesMatchLayout(&fixture, &result);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTraversalClipVisible(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId host;

    /* Nothing declares an overflow, so no boundary is ever in force and the clip
     * carried by every operation is the cleared one. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"host\"><section class=\"child\">"
                         "</section></div></body></html>",
                         PAINT_BASE_CSS
                         " .host { width: 100px; height: 50px }"
                         " .child { width: 300px; height: 300px }");
    host = PaintBox(&fixture, "host");
    (void)host;

    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)result.stats.clipped, 0);
    {
        size_t index;

        for (index = 0u; index < PaintListCount(result.list); index++) {
            PaintOperation operation;

            RoboCheckEqInt(PaintListAt(result.list, index, &operation), RoboOk);
            RoboCheckEqInt(PaintClipIsActive(&operation.clip), 0);
        }
    }
    PaintAllBoxesMatchLayout(&fixture, &result);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTraversalClipHidden(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId host;
    LayoutNodeId child;
    PaintOperation hostBegin;
    PaintOperation childBegin;
    PaintOperation childBackground;
    PaintClip clip;
    LayoutRect rect;

    /*
     * A box that hides both axes puts a boundary in force for its *contents*, and the
     * edge is the **padding box** -- CSS's clipping edge and the same box
     * `LayoutOverflowEdgesClipRect` reads one layer down.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"host\"><section class=\"child\">"
                         "</section></div></body></html>",
                         PAINT_BASE_CSS
                         " .host { width: 100px; height: 50px; padding: 4px;"
                         "   border: 2px solid #000000; overflow: hidden;"
                         "   background-color: #eeeeee }"
                         " .child { width: 300px; height: 300px;"
                         "   background-color: #ff0000 }");
    host = PaintBox(&fixture, "host");
    child = PaintBox(&fixture, "child");

    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)result.stats.clipped, 1);
    RoboCheckEqInt(PaintBeginOf(&result, host, &hostBegin), 1);
    RoboCheckEqInt(PaintBeginOf(&result, child, &childBegin), 1);

    /* The host's own operations are *outside* its boundary -- a box clips its contents,
     * not itself -- so its background carries no clip. */
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBackground, host, &childBackground),
                      1);
    RoboCheckEqInt(PaintClipIsActive(&childBackground.clip), 0);

    /* The child's do, and the interval is exactly the host's padding box. */
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBackground, child, &childBackground),
                      1);
    clip = childBackground.clip;
    RoboCheckEqInt(PaintClipIsActive(&clip), 1);
    RoboCheckEqInt(clip.horizontal.start, hostBegin.boxes.padding.x);
    RoboCheckEqInt(clip.horizontal.end,
                      hostBegin.boxes.padding.x + hostBegin.boxes.padding.width);
    RoboCheckEqInt(clip.vertical.start, hostBegin.boxes.padding.y);
    RoboCheckEqInt(clip.vertical.end,
                      hostBegin.boxes.padding.y + hostBegin.boxes.padding.height);

    /* Both axes bounded is the one case where a rectangle says everything, and the door
     * that answers a rectangle says so here. */
    memset(&rect, 0, sizeof(rect));
    RoboCheckEqInt(PaintClipRect(&clip, &rect), RoboOk);
    PaintRectEq(rect, hostBegin.boxes.padding, "the clip rectangle");

    /* The child's own operations carry the boundary too, and its begin marker keeps its
     * own geometry -- clipping is what a consumer does with the record, not a change to
     * what layout concluded. */
    RoboCheckEqInt(PaintClipIsActive(&childBegin.clip), 1);
    PaintAllBoxesMatchLayout(&fixture, &result);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTraversalClipAxisSplit(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId child;
    PaintOperation operation;

    /*
     * `overflow-x: hidden` beside `overflow-y: visible` is a legal declaration an author
     * writes and this build resolves, and it means exactly what it says: bounded
     * horizontally, free vertically. A single rectangle cannot express it -- a consumer
     * handing four numbers to a rasteriser would clip the axis the author left open --
     * which is why the record is two intervals.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"host\"><section class=\"child\">"
                         "</section></div></body></html>",
                         PAINT_BASE_CSS
                         " .host { width: 100px; height: 50px;"
                         "   overflow-x: hidden; overflow-y: visible }"
                         " .child { width: 300px; height: 300px;"
                         "   background-color: #00ff00 }");
    child = PaintBox(&fixture, "child");

    PaintOnce(&fixture, &result);
    RoboCheckEqInt(PaintOpOf(&result, RoboPaintOpBackground, child, &operation), 1);
    RoboCheckEqInt(PaintClipAxisIsActive(&operation.clip.horizontal), 1);
    RoboCheckEqInt(PaintClipAxisIsActive(&operation.clip.vertical), 0);
    /* And a rectangle is exactly the wrong answer here, so the door refuses rather than
     * clipping the free axis. */
    {
        LayoutRect rect;

        memset(&rect, 0, sizeof(rect));
        RoboCheckEqInt(PaintClipRect(&operation.clip, &rect), RoboNotFound);
    }
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Text, counts and the pass record                                 */
/* ------------------------------------------------------------------ */

static void TestTraversalTextPainted(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId para;
    PaintOperation text;
    size_t index;
    size_t textCount = 0u;
    size_t boxCount = 0u;

    /*
     * A text run is *visited, counted and painted* -- the 11.6 change stated where it can
     * be seen. The run is a leaf and a **drawing**, so it contributes one operation and no
     * `BEGIN_BOX`/`CONTENT`/`END_BOX` triple: the skeleton is a statement about boxes, and
     * a text node is not one. `textRuns = texts + textSkipped` stays exact, which is
     * what keeps "this document has text" and "this document's text drew nothing" two
     * different numbers rather than one silence.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><p class=\"para\">Alpha beta</p></body></html>",
                         PAINT_BASE_CSS " p { display: block }");
    para = PaintBox(&fixture, "para");

    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)result.stats.textRuns, 1);
    RoboCheckEqInt((int)result.stats.texts, 1);
    RoboCheckEqInt((int)result.stats.textSkipped, 0);
    RoboCheckEqInt((int)result.stats.textRuns,
                      (int)(result.stats.texts + result.stats.textSkipped));
    RoboCheckEqInt(PaintBeginOf(&result, para, NULL), 1);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpText), 1);

    /* Every operation names either a box or the one text object, and the run's node is
     * reached through the tree as a text object rather than a box. */
    for (index = 0u; index < PaintListCount(result.list); index++) {
        PaintOperation operation;
        LayoutObjectKind kind;

        RoboCheckEqInt(PaintListAt(result.list, index, &operation), RoboOk);
        kind = MakeLayoutObjectKind(fixture.tree, operation.node);
        if (kind == RoboLayoutObjectText) {
            textCount++;
            RoboCheckEqInt((int)operation.kind, (int)RoboPaintOpText);
        } else {
            boxCount++;
            RoboCheckEqInt((int)kind, (int)RoboLayoutObjectBox);
            RoboCheck(operation.kind != RoboPaintOpText);
        }
    }
    RoboCheckEqInt((int)textCount, 1);
    RoboCheckEqInt((int)boxCount,
                      (int)PaintCountOf(&result, RoboPaintOpBeginBox) +
                          (int)PaintCountOf(&result, RoboPaintOpContent) +
                          (int)PaintCountOf(&result, RoboPaintOpEndBox));

    /* The run is painted, so the record carries characters and a baseline, and its
     * placement is layout's -- read back through the pool the list owns. */
    text = PaintRequire(&result, RoboPaintOpText);
    {
        PaintTextPool *pool = PaintListTextPool(result.list);
        PaintTextRun run = PaintOperationText(&text);
        uint32_t characters[16];
        size_t got = 0u;

        RoboCheckEqInt(PaintTextRunIsValid(&run), 1);
        RoboCheckEqInt(
            (int)MakeLayoutObjectKind(fixture.tree, PaintOperationNode(&text)),
            (int)RoboLayoutObjectText);
        RoboCheck(pool != NULL);
        RoboCheckEqInt(PaintTextPoolRead(pool, run.span, characters, 16u, &got,
                                                    NULL),
                          RoboOk);
        RoboCheckEqInt((int)got, (int)run.span.count);
        RoboCheck(got == 10u);
        RoboCheck(characters[0] == (uint32_t)'A');
    }

    PaintAllBoxesMatchLayout(&fixture, &result);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTraversalCountsAndDeterminism(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintResult second;
    PaintContext *context = NULL;

    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\">New</section></div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .card, .clip { width: 120px; height: 40px;"
                         "   border: 1px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px;"
                         "   background-color: #123456 }"
                         " .clip { overflow: hidden }"
                         " .spill { width: 400px; height: 400px;"
                         "   border: 2px solid #ff0000 }");

    PaintOnce(&fixture, &result);

    /*
     * The counts have to agree with each other and with the list, which is the pair of
     * facts a reader uses to tell a document that painted in full from one half of which
     * was never laid out.
     */
    RoboCheckEqInt((int)result.stats.visited, (int)result.stats.objects);
    RoboCheckEqInt((int)result.stats.ops, (int)PaintListCount(result.list));
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBeginBox),
                      (int)result.stats.boxes);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpEndBox),
                      (int)result.stats.boxes);
    /* Structural records are unconditional: one content boundary per placed box. */
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpContent),
                      (int)result.stats.boxes);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBackground),
                      (int)result.stats.backgrounds);
    {
        size_t borders = PaintCountOf(&result, RoboPaintOpBorderTop) +
                         PaintCountOf(&result, RoboPaintOpBorderRight) +
                         PaintCountOf(&result, RoboPaintOpBorderBottom) +
                         PaintCountOf(&result, RoboPaintOpBorderLeft);

        RoboCheckEqInt((int)borders, (int)result.stats.borderSides);
    }
    RoboCheckEqInt((int)result.stats.depthMax,
                      (int)LayoutTreeMaxDepth(fixture.tree));
    RoboCheckEqInt((int)result.stats.passes, 1);
    RoboCheckEqInt((int)result.stats.clipped, 1);
    /* Two boxes at the top level plus the two nested ones plus html and body. */
    RoboCheckEqInt((int)result.stats.boxes, 6);
    PaintAllBoxesMatchLayout(&fixture, &result);

    /*
     * Determinism, three ways: the same tree painted again into a *second* list, the same
     * tree painted again after a `clear`, and the same document built into a second tree
     * and painted. A conclusion that depended on a pointer, an allocation or an
     * iteration order would fail at least one of them.
     */
    PaintResultInit(&second);
    RoboCheckEqInt(PaintContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(PaintContextRun(context, fixture.tree, second.list, NULL),
                      RoboOk);
    PaintContextStats(context, &second.stats);
    RoboCheckEqInt(PaintListEqual(result.list, second.list), 1);
    RoboCheckEqInt((int)second.stats.ops, (int)result.stats.ops);
    RoboCheckEqInt((int)second.stats.boxes, (int)result.stats.boxes);
    RoboCheckEqInt((int)second.stats.backgrounds, (int)result.stats.backgrounds);
    RoboCheckEqInt((int)second.stats.borderSides, (int)result.stats.borderSides);
    RoboCheckEqInt((int)second.stats.clipped, (int)result.stats.clipped);
    RoboCheckEqInt((int)second.stats.depthMax, (int)result.stats.depthMax);

    /* One list, reused: `clear` keeps the storage and the next pass recomputes every
     * operation, so nothing from the previous pass survives. */
    RoboCheckEqInt(PaintContextRun(context, fixture.tree, result.list, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintListEqual(result.list, second.list), 1);
    {
        PaintStats stats;

        PaintContextStats(context, &stats);
        RoboCheckEqInt((int)stats.passes, 2);
    }
    PaintContextDestroy(context);
    PaintResultDispose(&second);
    PaintResultDispose(&result);

    /* The same document, a second tree: identical, because a paint list is a conclusion
     * about the document and not about an allocation. */
    {
        PaintFixtureDoc other;
        PaintResult rebuilt;

        PaintBeginLaidOut(&other,
                             "<html><body>"
                             "<div class=\"card\"><section class=\"badge\">New</section>"
                             "</div>"
                             "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                             "</body></html>",
                             PAINT_BASE_CSS
                             " .card, .clip { width: 120px; height: 40px;"
                             "   border: 1px solid #000000; background-color: #ffffff }"
                             " .badge { width: 20px; height: 10px;"
                             "   background-color: #123456 }"
                             " .clip { overflow: hidden }"
                             " .spill { width: 400px; height: 400px;"
                             "   border: 2px solid #ff0000 }");
        PaintOnce(&other, &rebuilt);
        PaintOnce(&fixture, &result);
        RoboCheckEqInt(PaintListEqual(result.list, rebuilt.list), 1);
        PaintResultDispose(&rebuilt);
        PaintResultDispose(&result);
        PaintEnd(&other);
    }
    PaintEnd(&fixture);
}

static void TestTraversalContextLifecycle(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintContext *context = NULL;
    int marker = 0;

    PaintBeginLaidOut(&fixture, "<html><body></body></html>", PAINT_BASE_CSS);
    PaintResultInit(&result);

    RoboCheckEqInt(PaintContextCreate(&context, NULL), RoboOk);
    /* A fresh context has run nothing: no tree, no outcome, and a zeroed record. */
    RoboCheck(PaintContextTree(context) == NULL);
    RoboCheckEqInt((int)PaintContextOutcome(context), (int)RoboPaintPassNone);
    RoboCheck(PaintContextUserData(context) == NULL);

    PaintContextSetUserData(context, &marker);
    RoboCheck(PaintContextUserData(context) == &marker);

    PaintRun(&fixture, context, result.list);
    RoboCheckEqInt((int)PaintContextOutcome(context),
                      (int)RoboPaintPassPainted);
    RoboCheck(PaintContextTree(context) == fixture.tree);

    /* A reset restores the just-created state, pass count included -- "this context has
     * run nothing" rather than "this context has run something else". The user data is
     * the caller's and survives, which is what makes install-once/reset-per-pass work. */
    PaintContextReset(context);
    RoboCheckEqInt((int)PaintContextOutcome(context), (int)RoboPaintPassNone);
    RoboCheck(PaintContextTree(context) == NULL);
    RoboCheck(PaintContextUserData(context) == &marker);
    PaintContextStats(context, &result.stats);
    RoboCheckEqInt((int)result.stats.passes, 0);
    RoboCheckEqInt((int)result.stats.objects, 0);

    PaintContextReset(NULL);      /* safe */
    PaintContextSetUserData(NULL, &marker);
    PaintContextDestroy(NULL);    /* safe */

    PaintContextDestroy(context);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 5. Safety                                                           */
/* ------------------------------------------------------------------ */

static void TestTraversalExtremeGeometry(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId huge;

    /*
     * A dimension at the geometry's own maximum with a border on it: the accumulation
     * saturates rather than wrapping, and no negative extent escapes into a list. This is
     * the case Phase 10.15 found the cost of getting wrong in the layout pass's own
     * cursor, restated where *this* module does its own arithmetic.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"huge\"></div>"
                         "<div class=\"zero\"></div>"
                         "</body></html>",
                         PAINT_BASE_CSS
                         " .huge { width: 2147483647px; height: 2147483647px;"
                         "   border: 5px solid #000000; background-color: #00ff00 }"
                         " .zero { width: 0; height: 0; margin: -10px;"
                         "   background-color: #123456 }");
    huge = PaintBox(&fixture, "huge");

    PaintOnce(&fixture, &result);
    {
        size_t index;

        for (index = 0u; index < PaintListCount(result.list); index++) {
            PaintOperation operation;

            RoboCheckEqInt(PaintListAt(result.list, index, &operation), RoboOk);
            /* The container's promise, which is where "no invalid operation may escape"
             * is actually satisfied: every record in the list is one. */
            RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
            RoboCheck(operation.boxes.border.width >= 0);
            RoboCheck(operation.boxes.border.height >= 0);
            RoboCheck(operation.boxes.padding.width >= 0);
            RoboCheck(operation.boxes.content.width >= 0);
            RoboCheck(operation.boxes.content.height >= 0);
        }
    }
    RoboCheckEqInt(PaintBeginOf(&result, huge, NULL), 1);
    RoboCheckEqInt((int)result.stats.contextualColors, 0);
    PaintAllBoxesMatchLayout(&fixture, &result);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTraversalAllocatorBalance(void)
{
    size_t baseline;
    int round;

    PaintFixture();
    /*
     * A settled baseline first: the bring-up helpers themselves allocate, so the number
     * to return to is the one after the fixture has been built once. The claim is that a
     * *pass* leaks nothing -- the list and the context included.
     */
    {
        PaintFixtureDoc warmup;

        PaintBeginLaidOut(&warmup,
                             "<html><body><div class=\"card\"><section class=\"badge\">"
                             "</section></div></body></html>",
                             PAINT_BASE_CSS
                             " .card { width: 40px; height: 20px;"
                             "   border: 1px solid #000000; background-color: #ffffff }"
                             " .badge { width: 10px; height: 5px; overflow: hidden }");
        PaintEnd(&warmup);
    }
    baseline = PaintLiveBlocks();

    for (round = 0; round < 3; round++) {
        PaintFixtureDoc fixture;
        PaintResult result;

        PaintBeginLaidOut(&fixture,
                             "<html><body><div class=\"card\"><section class=\"badge\">"
                             "</section></div></body></html>",
                             PAINT_BASE_CSS
                             " .card { width: 40px; height: 20px;"
                             "   border: 1px solid #000000; background-color: #ffffff }"
                             " .badge { width: 10px; height: 5px; overflow: hidden }");
        PaintOnce(&fixture, &result);
        RoboCheck(PaintListCount(result.list) > 0u);
        PaintResultDispose(&result);
        PaintEnd(&fixture);
    }

    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintTraversal(void)
{
    TestRegister("paint.traversal.refusals_and_empty",
                       TestTraversalRefusalsAndEmpty);
    TestRegister("paint.traversal.single_box", TestTraversalSingleBox);
    TestRegister("paint.traversal.siblings_in_order", TestTraversalSiblingsInOrder);
    TestRegister("paint.traversal.empty_element", TestTraversalEmptyElement);
    TestRegister("paint.traversal.background", TestTraversalBackground);
    TestRegister("paint.traversal.border_sides", TestTraversalBorderSides);
    TestRegister("paint.traversal.border_color_initial",
                       TestTraversalBorderColorInitial);
    TestRegister("paint.traversal.box_model_geometry",
                       TestTraversalBoxModelGeometry);
    TestRegister("paint.traversal.clip_visible", TestTraversalClipVisible);
    TestRegister("paint.traversal.clip_hidden", TestTraversalClipHidden);
    TestRegister("paint.traversal.clip_axis_split", TestTraversalClipAxisSplit);
    TestRegister("paint.traversal.text_painted", TestTraversalTextPainted);
    TestRegister("paint.traversal.counts_and_determinism",
                       TestTraversalCountsAndDeterminism);
    TestRegister("paint.traversal.context_lifecycle",
                       TestTraversalContextLifecycle);
    TestRegister("paint.traversal.extreme_geometry",
                       TestTraversalExtremeGeometry);
    TestRegister("paint.traversal.allocator_balance",
                       TestTraversalAllocatorBalance);
}
