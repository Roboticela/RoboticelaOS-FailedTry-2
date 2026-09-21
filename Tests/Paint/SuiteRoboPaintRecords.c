/*
 * RoboPaint records suite (Phase 11.2) — paint.records.*
 *
 * The phase that makes the record model explicit. 11.1 established the architecture --
 * an operation is a value snapshot, the list owns it, the traversal orders it
 * deterministically and the dump reads it back -- and this suite asks the guarantees
 * that architecture *claims* rather than the ones a single document happens to satisfy:
 *
 *   the **sequence** has a regular skeleton, because structural records (`BEGIN_BOX`,
 *   `CONTENT`, `END_BOX`) are unconditional while drawing records are emitted only when
 *   they draw;
 *
 *   the `CONTENT` boundary is the explicit end of a box's own records and the start of
 *   its subtree's, so nesting is recoverable without consulting the tree;
 *
 *   a record **snapshots** what it needs and references nothing mutable -- proved by
 *   mutating the layout state after a list exists and showing the list does not move;
 *
 *   ownership is the list's and copying is by value, so a stored record cannot be
 *   changed through the argument it was built from.
 *
 * Every case builds a real document through the real chain and paints it. The hand-built
 * records appear only where the question is about a *record* rather than about a
 * document -- the append-copies-by-value case -- and never in place of a layout result.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite
 * depends on no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboPaint/RoboPaintDump.h"

#include <string.h>

/* The same base sheet the traversal suite uses: these elements are the block containers,
 * and `<head>` stays the one box nothing styles. */
#define RECORDS_BASE_CSS \
    "html, body, div, section, aside, figure, p { display: block }"

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/** The @p nth operation of @p kind on @p node, or 0 when there is none. */
static int RecordsOpOf(const PaintResult *result,
                         PaintOpKind kind,
                         LayoutNodeId node,
                         PaintOperation *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            return 0;
        }
        if (operation.kind != kind || operation.node != node) {
            continue;
        }
        if (seen == 0u) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
        seen++;
    }
    return 0;
}

/** The index of the first operation of @p kind on @p node, or `(size_t)-1`. */
static size_t RecordsIndexOf(const PaintResult *result,
                               PaintOpKind kind,
                               LayoutNodeId node)
{
    size_t index;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            break;
        }
        if (operation.kind == kind && operation.node == node) {
            return index;
        }
    }
    return (size_t)-1;
}

/** Write the whole list into @p buffer (asserted to fit) and return its length. */
static size_t RecordsDump(const PaintResult *result, char *buffer, size_t capacity)
{
    size_t length = 0u;

    memset(buffer, 0, capacity);
    RoboCheckEqInt(PaintListWrite(result->list, buffer, capacity, &length, NULL),
                      RoboOk);
    return length;
}

/* ------------------------------------------------------------------ */
/* 1. The sequence                                                     */
/* ------------------------------------------------------------------ */

static void TestRecordsContentSequence(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId bodyBox;
    LayoutNodeId card;
    PaintOperation begin;
    PaintOperation content;
    PaintOperation end;
    size_t index;
    int open = 0;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         RECORDS_BASE_CSS " .card { width: 100px; height: 40px }");
    bodyBox = PaintBoxByTag(&fixture, "body");
    card = PaintBox(&fixture, "card");

    PaintOnce(&fixture, &result);

    /* The skeleton: one structural triple per placed box, and nothing that draws. */
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBeginBox), 3);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpContent), 3);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpEndBox), 3);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBackground), 0);
    RoboCheckEqInt((int)PaintListCount(result.list), 9);

    /* Each box's own records are contiguous: `[BEGIN_BOX .. CONTENT]` then its subtree
     * then `END_BOX`. Walking and checking the nesting as an open/close count is how the
     * claim is asked without naming every index. */
    for (index = 0u; index < PaintListCount(result.list); index++) {
        PaintOperation operation;

        RoboCheckEqInt(PaintListAt(result.list, index, &operation), RoboOk);
        if (operation.kind == RoboPaintOpBeginBox) {
            open++;
        } else if (operation.kind == RoboPaintOpEndBox) {
            open--;
        }
        RoboCheck(open >= 0);
    }
    RoboCheckEqInt(open, 0);

    /* And for the two boxes it is worth naming: the content boundary sits between the
     * opening marker and the closing one, and a parent's comes before its child opens. */
    RoboCheckEqInt(RecordsOpOf(&result, RoboPaintOpBeginBox, bodyBox, &begin), 1);
    RoboCheckEqInt(RecordsOpOf(&result, RoboPaintOpContent, bodyBox, &content), 1);
    RoboCheckEqInt(RecordsOpOf(&result, RoboPaintOpEndBox, bodyBox, &end), 1);
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpBeginBox, bodyBox) <
               RecordsIndexOf(&result, RoboPaintOpContent, bodyBox));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpContent, bodyBox) <
               RecordsIndexOf(&result, RoboPaintOpBeginBox, card));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpContent, bodyBox) <
               RecordsIndexOf(&result, RoboPaintOpEndBox, bodyBox));

    /* The triple carries one depth, so a consumer can group a box's records by it. */
    RoboCheckEqInt((int)begin.depth, (int)content.depth);
    RoboCheckEqInt((int)content.depth, (int)end.depth);
    RoboCheckEqInt((int)bodyBox, (int)content.node);

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestRecordsStructuralUnconditional(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId card;
    PaintOperation content;

    /* A box that declares no background, draws no border and has no children: it still
     * gets its whole skeleton, because the records state a fact about the sequence
     * rather than about the document. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"bare\"></div></body></html>",
                         RECORDS_BASE_CSS " .bare { width: 30px; height: 30px }");
    card = PaintBox(&fixture, "bare");

    PaintOnce(&fixture, &result);

    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBackground), 0);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBorderTop), 0);
    RoboCheckEqInt((int)RecordsOpOf(&result, RoboPaintOpContent, card, &content), 1);
    RoboCheckEqInt(PaintOperationIsStructural(&content), 1);
    RoboCheckEqInt(PaintOperationIsDrawing(&content), 0);
    RoboCheckEqInt((int)PaintOperationColor(&content).kind,
                      (int)RoboCssColorNone);

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. The content boundary and its geometry                            */
/* ------------------------------------------------------------------ */

static void TestRecordsContentGeometry(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId card;
    PaintOperation content;
    PaintBoxes boxes;
    PaintBoxes fromLayout;

    /* A box whose padding and border differ on each axis, so that the content box is
     * neither the border box nor the padding box: the three cannot be confused. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         RECORDS_BASE_CSS
                         " .card { width: 100px; height: 40px;"
                         "   margin: 7px; padding: 3px 5px 7px 11px;"
                         "   border: 2px solid #000000 }");
    card = PaintBox(&fixture, "card");

    PaintOnce(&fixture, &result);

    RoboCheckEqInt(RecordsOpOf(&result, RoboPaintOpContent, card, &content), 1);
    memset(&boxes, 0, sizeof(boxes));
    memset(&fromLayout, 0, sizeof(fromLayout));
    PaintOperationBoxes(&content, &boxes);
    RoboCheckEqInt(PaintBoxesOf(fixture.tree, card, &fromLayout, NULL), RoboOk);

    /* The record carries the box's three rectangles, and the one it *acts on* is the
     * content box -- the region a later clip, scroll or content origin will read. */
    RoboCheckEqInt(PaintBoxesEqual(&boxes, &fromLayout), 1);
    RoboCheckEqInt(boxes.insetsMeasured, 1);
    RoboCheck(boxes.content.x > boxes.padding.x);
    RoboCheck(boxes.content.y > boxes.padding.y);
    RoboCheck(boxes.content.width < boxes.padding.width);
    RoboCheck(boxes.content.height < boxes.padding.height);
    RoboCheck(boxes.padding.x > boxes.border.x);
    RoboCheck(boxes.padding.width < boxes.border.width);

    /* The content boundary and the box's own markers agree about the box, and disagree
     * about which rectangle they are about -- which is what makes the boundary worth
     * emitting rather than inferring. */
    {
        PaintOperation begin;

        RoboCheckEqInt(RecordsOpOf(&result, RoboPaintOpBeginBox, card, &begin), 1);
        RoboCheckEqInt(PaintBoxesEqual(&begin.boxes, &content.boxes), 1);
        RoboCheckEqInt(GfxPointEqual(begin.origin, content.origin), 1);
    }

    PaintLocalBoxesMatchLayout(&fixture, content);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Record fields                                                    */
/* ------------------------------------------------------------------ */

static void TestRecordsFields(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId card;
    size_t index;
    size_t structural = 0u;
    size_t drawing = 0u;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         RECORDS_BASE_CSS
                         " .card { width: 50px; height: 50px;"
                         "   background-color: #3366cc; border: 1px solid #000000 }");
    card = PaintBox(&fixture, "card");

    PaintOnce(&fixture, &result);

    for (index = 0u; index < PaintListCount(result.list); index++) {
        PaintOperation operation;

        RoboCheckEqInt(PaintListAt(result.list, index, &operation), RoboOk);

        /* Kind, identity, depth and geometry: the fields every record carries, each
         * asserted to be a value the module's own rules accept. */
        RoboCheckEqInt(PaintOpKindIsKnown(operation.kind), 1);
        RoboCheck(operation.kind != RoboPaintOpNone);
        RoboCheck(operation.node != RoboLayoutNodeIdNone);
        RoboCheckEqInt(LayoutObjectIsLive(fixture.tree, operation.node), 1);
        RoboCheckEqInt(PaintBoxesIsValid(&operation.boxes), 1);
        RoboCheckEqInt(PaintClipIsValid(&operation.clip), 1);
        RoboCheckEqInt(PaintOperationIsValid(&operation), 1);

        /* Structural and drawing are exact complements over a live record, and the
         * colour rule follows the split. */
        if (PaintOperationIsStructural(&operation)) {
            structural++;
            RoboCheckEqInt(PaintOperationIsDrawing(&operation), 0);
            RoboCheckEqInt((int)PaintOperationColor(&operation).kind,
                              (int)RoboCssColorNone);
        } else {
            drawing++;
            RoboCheckEqInt(PaintOperationIsStructural(&operation), 0);
            RoboCheckEqInt(PaintOperationIsDrawing(&operation), 1);
            RoboCheckEqInt(CssColorIsValid(
                                  PaintOperationColor(&operation)),
                              1);
        }

        PaintLocalBoxesMatchLayout(&fixture, operation);
    }

    /* The card draws a background and four borders; the three boxes contribute three
     * structural triples. So structural = 9 and drawing = 5. */
    RoboCheckEqInt((int)structural, 9);
    RoboCheckEqInt((int)drawing, 5);

    /* A `CONTENT` record is structural and not a box marker -- the distinction the
     * predicates draw, asked once against a real record. */
    {
        PaintOperation content;

        RoboCheckEqInt(RecordsOpOf(&result, RoboPaintOpContent, card, &content), 1);
        RoboCheckEqInt(PaintOperationIsStructural(&content), 1);
        RoboCheckEqInt(PaintOperationIsMarker(&content), 0);
    }

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Ordering                                                         */
/* ------------------------------------------------------------------ */

static void TestRecordsNestedOrder(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId htmlBox;
    LayoutNodeId bodyBox;
    LayoutNodeId outer;
    LayoutNodeId inner;

    /* A four-deep chain: html > body > outer > inner. Each level's content boundary must
     * precede the next level's opening marker, so the nesting is recoverable from the
     * record sequence alone -- which is the property a flat list with depths exists to
     * have. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\">"
                         "<div class=\"inner\"></div></div></body></html>",
                         RECORDS_BASE_CSS
                         " .outer { width: 200px; height: 100px;"
                         "   border: 1px solid #000000 }"
                         " .inner { width: 40px; height: 20px;"
                         "   background-color: #ff0000 }");
    htmlBox = PaintBoxByTag(&fixture, "html");
    bodyBox = PaintBoxByTag(&fixture, "body");
    outer = PaintBox(&fixture, "outer");
    inner = PaintBox(&fixture, "inner");

    PaintOnce(&fixture, &result);

    /* Four boxes' structural triples (12) plus `outer`'s four border sides and `inner`'s
     * background (5): the records that draw, in their declared order. */
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpContent), 4);
    RoboCheckEqInt((int)PaintListCount(result.list), 17);

    /* The chain, in order: every parent's `CONTENT` before the next child's `BEGIN_BOX`. */
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpBeginBox, htmlBox) <
               RecordsIndexOf(&result, RoboPaintOpContent, htmlBox));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpContent, htmlBox) <
               RecordsIndexOf(&result, RoboPaintOpBeginBox, bodyBox));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpContent, bodyBox) <
               RecordsIndexOf(&result, RoboPaintOpBeginBox, outer));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpContent, outer) <
               RecordsIndexOf(&result, RoboPaintOpBeginBox, inner));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpContent, inner) <
               RecordsIndexOf(&result, RoboPaintOpEndBox, inner));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpEndBox, inner) <
               RecordsIndexOf(&result, RoboPaintOpEndBox, outer));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpEndBox, outer) <
               RecordsIndexOf(&result, RoboPaintOpEndBox, bodyBox));

    /* And the depth of each level is its position in the chain, which is what a
     * rasteriser pushes and pops a clip stack by. */
    {
        PaintOperation operation;

        RoboCheckEqInt(RecordsOpOf(&result, RoboPaintOpBeginBox, htmlBox,
                                        &operation),
                          1);
        RoboCheckEqInt((int)operation.depth, 0);
        RoboCheckEqInt(RecordsOpOf(&result, RoboPaintOpBeginBox, bodyBox,
                                        &operation),
                          1);
        RoboCheckEqInt((int)operation.depth, 1);
        RoboCheckEqInt(RecordsOpOf(&result, RoboPaintOpBeginBox, outer,
                                        &operation),
                          1);
        RoboCheckEqInt((int)operation.depth, 2);
        RoboCheckEqInt(RecordsOpOf(&result, RoboPaintOpBeginBox, inner,
                                        &operation),
                          1);
        RoboCheckEqInt((int)operation.depth, 3);
    }

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestRecordsSiblingsOrder(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId first;
    LayoutNodeId second;
    LayoutNodeId third;

    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"a\"></div><div class=\"b\"></div><div class=\"c\"></div>"
                         "</body></html>",
                         RECORDS_BASE_CSS
                         " .a, .b, .c { width: 20px; height: 10px }");
    first = PaintBox(&fixture, "a");
    second = PaintBox(&fixture, "b");
    third = PaintBox(&fixture, "c");

    PaintOnce(&fixture, &result);

    /* Siblings in DOM order, each with its own structural triple, none interleaved. */
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpBeginBox, first) <
               RecordsIndexOf(&result, RoboPaintOpBeginBox, second));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpBeginBox, second) <
               RecordsIndexOf(&result, RoboPaintOpBeginBox, third));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpEndBox, first) <
               RecordsIndexOf(&result, RoboPaintOpBeginBox, second));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpContent, first) <
               RecordsIndexOf(&result, RoboPaintOpEndBox, first));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpContent, second) <
               RecordsIndexOf(&result, RoboPaintOpEndBox, second));
    RoboCheck(RecordsIndexOf(&result, RoboPaintOpContent, third) <
               RecordsIndexOf(&result, RoboPaintOpEndBox, third));
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpContent), 5);
    RoboCheckEqInt((int)PaintListCount(result.list), 15);

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 5. Mutation isolation                                               */
/* ------------------------------------------------------------------ */

static void TestRecordsSnapshotIsolation(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintResult after;
    PaintOperation saved[32];
    size_t savedCount;
    size_t index;
    const PaintOperation *ops;

    /* A block with no declared width fills its containing block, so its geometry follows
     * the viewport -- which makes a second layout pass at a different width an observable
     * mutation rather than a no-op. */
    PaintBegin(&fixture,
                "<html><body><div class=\"fill\"></div></body></html>",
                RECORDS_BASE_CSS " .fill { height: 25px; background-color: #00ff00 }");
    PaintLayout(&fixture, 800, 600);
    PaintOnce(&fixture, &result);

    ops = PaintListOperations(result.list);
    savedCount = PaintListCount(result.list);
    RoboCheck(savedCount > 0u);
    RoboCheck(savedCount <= sizeof(saved) / sizeof(saved[0]));
    for (index = 0u; index < savedCount; index++) {
        saved[index] = ops[index];
    }

    /* Mutate the layout state the records were built from: lay the *same* tree out at a
     * different viewport and paint again into a second list. */
    PaintLayout(&fixture, 400, 600);
    PaintOnce(&fixture, &after);

    /* The first list's records did not move: a record is a snapshot, not a view onto
     * mutable layout state. */
    for (index = 0u; index < savedCount; index++) {
        PaintOperation reread;

        RoboCheckEqInt(PaintListAt(result.list, index, &reread), RoboOk);
        RoboCheckEqInt(PaintOperationEqual(&saved[index], &reread), 1);
    }

    /* And the mutation was real: the second pass produced different geometry, which is
     * what makes the isolation above a statement about records rather than about the
     * mutation never having happened. */
    RoboCheckEqInt(PaintListEqual(result.list, after.list), 0);

    PaintResultDispose(&after);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Ownership                                                        */
/* ------------------------------------------------------------------ */

static void TestRecordsOwnership(void)
{
    PaintOperation operation;
    PaintOperation read;
    PaintList *list = NULL;
    PaintList *copy = NULL;
    size_t capacity;

    PaintFixture();

    /* A hand-built record is the one place a suite fabricates rather than paints, and
     * here it is exactly the point: the question is about the *container's* ownership. */
    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpContent;
    operation.node = (LayoutNodeId)1u;
    operation.depth = 2u;
    operation.boxes.border.x = 5;
    operation.boxes.padding.x = 5;
    operation.boxes.content.x = 5;

    RoboCheckEqInt(PaintListCreate(&list, NULL), RoboOk);
    capacity = PaintListCapacity(list);
    RoboCheckEqInt(PaintListAppend(list, &operation, NULL), RoboOk);
    RoboCheckEqInt((int)PaintListCount(list), 1);

    /* Append copies by value: changing the argument afterwards leaves the stored record
     * exactly as it was. */
    operation.node = (LayoutNodeId)999u;
    operation.boxes.border.x = -1234;
    RoboCheckEqInt(PaintListAt(list, 0u, &read), RoboOk);
    RoboCheckEqInt((int)read.node, 1);
    RoboCheckEqInt(read.boxes.border.x, 5);

    /* A second list is independent: destroying it does not touch the first. */
    RoboCheckEqInt(PaintListCreate(&copy, NULL), RoboOk);
    RoboCheckEqInt(PaintListAppend(copy, &read, NULL), RoboOk);
    PaintListDestroy(copy);
    RoboCheckEqInt((int)PaintListCount(list), 1);

    /* `clear` keeps the storage and drops every record. */
    PaintListClear(list);
    RoboCheckEqInt((int)PaintListCount(list), 0);
    RoboCheckEqInt((int)PaintListCapacity(list), (int)capacity);
    RoboCheck(PaintListOperations(list) == NULL);

    /* And the container refuses a record that is not one, rather than storing it. */
    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpEndBox;
    operation.node = (LayoutNodeId)1u;
    operation.color = CssColorMake(1, 2, 3, 255); /* a structural record with a colour */
    RoboCheckEqInt(PaintListAppend(list, &operation, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)PaintListCount(list), 0);

    /* Empty cleanup and a NULL list are both safe. */
    PaintListDestroy(list);
    PaintListDestroy(NULL);
    PaintListClear(NULL);
}

/* ------------------------------------------------------------------ */
/* 7. Empty and degenerate cases                                       */
/* ------------------------------------------------------------------ */

static void TestRecordsEmptyCases(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintList *empty = NULL;
    char buffer[256];
    size_t length = 0u;

    /* A document whose body is empty still lays out `html` and `body`, so its records
     * are two structural triples and nothing that draws. */
    PaintBeginLaidOut(&fixture, "<html><body></body></html>", RECORDS_BASE_CSS);
    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpContent), 2);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBackground), 0);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBorderTop), 0);
    RoboCheckEqInt((int)PaintListCount(result.list), 6);
    PaintResultDispose(&result);
    PaintEnd(&fixture);

    /* A zero-sized box: its records exist and every geometry is a valid empty rectangle. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"zero\"></div></body></html>",
                         RECORDS_BASE_CSS " .zero { width: 0; height: 0 }");
    PaintOnce(&fixture, &result);
    {
        size_t index;

        for (index = 0u; index < PaintListCount(result.list); index++) {
            PaintOperation operation;

            RoboCheckEqInt(PaintListAt(result.list, index, &operation), RoboOk);
            RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
        }
    }
    PaintResultDispose(&result);
    PaintEnd(&fixture);

    /* An empty list dumps as an empty buffer of length 0 -- a legitimate answer, not a
     * placeholder line. */
    PaintListCreate(&empty, NULL);
    memset(buffer, 0x7f, sizeof(buffer));
    RoboCheckEqInt(PaintListWrite(empty, buffer, sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckEqInt((int)length, 0);
    RoboCheckEqInt((int)buffer[0], 0);
    PaintListDestroy(empty);
}

/* ------------------------------------------------------------------ */
/* 8. Geometry safety                                                  */
/* ------------------------------------------------------------------ */

static void TestRecordsGeometrySafety(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    size_t index;

    /* A box at the geometry type's maximum with a border that must not wrap it, a box
     * with a zero extent, and a nested pair whose offsets accumulate. None may produce a
     * record with a negative extent, an un-nested set of boxes, or an unrepresentable
     * coordinate. */
    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"wide\"></div>"
                         "<div class=\"zero\"></div>"
                         "<div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         RECORDS_BASE_CSS
                         " .wide { width: 2147483647px; height: 10px;"
                         "   border: 5px solid #000000; background-color: #ffffff }"
                         " .zero { width: 0; height: 0;"
                         "   padding: 4px; border: 3px solid #000000 }"
                         " .outer { width: 100px; height: 60px; margin: 9px;"
                         "   padding: 6px; border: 2px solid #000000 }"
                         " .inner { width: 20px; height: 20px; margin: 3px }");

    PaintOnce(&fixture, &result);

    RoboCheck(PaintListCount(result.list) > 0u);
    for (index = 0u; index < PaintListCount(result.list); index++) {
        PaintOperation operation;

        RoboCheckEqInt(PaintListAt(result.list, index, &operation), RoboOk);
        /* The container's rule, asked over an extreme document: every stored record is
         * one, which is three valid nested boxes with non-negative extents. */
        RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
        RoboCheck(operation.boxes.border.width >= 0);
        RoboCheck(operation.boxes.border.height >= 0);
        RoboCheck(operation.boxes.padding.width >= 0);
        RoboCheck(operation.boxes.content.width >= 0);
        RoboCheck(operation.boxes.content.height >= 0);
        /* A border record's band is finite and non-negative on both axes. */
        if (PaintOpKindIsBorder(operation.kind)) {
            LayoutRect band;

            RoboCheckEqInt(PaintOperationSideBand(&operation, &band, NULL),
                              RoboOk);
            RoboCheck(band.width >= 0);
            RoboCheck(band.height >= 0);
        }
        PaintLocalBoxesMatchLayout(&fixture, operation);
    }

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 9. Determinism                                                      */
/* ------------------------------------------------------------------ */

static void TestRecordsDeterminism(void)
{
    PaintFixtureDoc fixture;
    PaintFixtureDoc rebuilt;
    PaintResult first;
    PaintResult second;
    char dumpFirst[4096];
    char dumpSecond[4096];
    size_t lengthFirst;
    size_t lengthSecond;

    const char *html =
        "<html><body>"
        "<div class=\"card\"><section class=\"badge\">New</section></div>"
        "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
        "</body></html>";
    const char *css =
        RECORDS_BASE_CSS
        " .card, .clip { width: 120px; height: 40px;"
        "   border: 1px solid #000000; background-color: #ffffff }"
        " .badge { width: 20px; height: 10px; background-color: #123456 }"
        " .clip { overflow: hidden }"
        " .spill { width: 400px; height: 400px; border: 2px solid #ff0000 }";

    /* The same document, painted twice from one tree... */
    PaintBeginLaidOut(&fixture, html, css);
    PaintOnce(&fixture, &first);
    PaintOnce(&fixture, &second);
    RoboCheckEqInt(PaintListEqual(first.list, second.list), 1);
    lengthFirst = RecordsDump(&first, dumpFirst, sizeof(dumpFirst));
    lengthSecond = RecordsDump(&second, dumpSecond, sizeof(dumpSecond));
    RoboCheckEqInt((int)lengthFirst, (int)lengthSecond);
    RoboCheckEqInt(strcmp(dumpFirst, dumpSecond), 0);
    PaintResultDispose(&second);
    PaintResultDispose(&first);
    PaintEnd(&fixture);

    /* ...and the same document built into a *second* tree, which is the stronger claim:
     * a conclusion that depended on a pointer, an allocation or an iteration order would
     * differ between two independently built trees. */
    PaintBeginLaidOut(&rebuilt, html, css);
    PaintOnce(&rebuilt, &second);
    lengthSecond = RecordsDump(&second, dumpSecond, sizeof(dumpSecond));

    PaintBeginLaidOut(&fixture, html, css);
    PaintOnce(&fixture, &first);
    lengthFirst = RecordsDump(&first, dumpFirst, sizeof(dumpFirst));

    RoboCheckEqInt(PaintListEqual(first.list, second.list), 1);
    RoboCheckEqInt(strcmp(dumpFirst, dumpSecond), 0);

    PaintResultDispose(&second);
    PaintResultDispose(&first);
    PaintEnd(&fixture);
    PaintEnd(&rebuilt);
}

/* ------------------------------------------------------------------ */
/* 10. The dump                                                        */
/* ------------------------------------------------------------------ */

static void TestRecordsDump(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    char buffer[8192];
    size_t length;
    size_t lines = 0u;
    const char *scan;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         RECORDS_BASE_CSS
                         " .card { width: 100px; height: 40px;"
                         "   background-color: #ff0000; border: 1px solid #000000 }");
    PaintOnce(&fixture, &result);

    length = RecordsDump(&result, buffer, sizeof(buffer));
    RoboCheck(length > 0u);

    /* One line per record, and the content boundary is one of them -- naming the
     * rectangle it acts on so a reader can tell it from a marker. */
    scan = buffer;
    while (*scan != '\0') {
        if (*scan == '\n') {
            lines++;
        }
        scan++;
    }
    RoboCheckEqInt((int)lines, (int)PaintListCount(result.list));
    RoboCheck(strstr(buffer, "content") != NULL);
    RoboCheck(strstr(buffer, " content=") != NULL);
    RoboCheck(strstr(buffer, "begin-box") != NULL);
    RoboCheck(strstr(buffer, "background") != NULL);
    RoboCheck(strstr(buffer, "border-top") != NULL);
    RoboCheck(strstr(buffer, "end-box") != NULL);

    /* The dump is deterministic and a refused buffer reports the length it needed. */
    {
        char again[8192];
        size_t againLength;

        againLength = RecordsDump(&result, again, sizeof(again));
        RoboCheckEqInt((int)length, (int)againLength);
        RoboCheckEqInt(strcmp(buffer, again), 0);

        {
            char tiny[8];
            size_t needed = 0u;

            RoboCheckEqInt(PaintListWrite(result.list, tiny, sizeof(tiny),
                                                    &needed, NULL),
                              RoboNotSupported);
            RoboCheckEqInt((int)needed, (int)length);
        }
    }

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 11. Allocation balance                                              */
/* ------------------------------------------------------------------ */

static void TestRecordsAllocatorBalance(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    size_t before;
    size_t after;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         RECORDS_BASE_CSS
                         " .card { width: 10px; height: 10px;"
                         "   background-color: #fff; border: 1px solid #000 }");

    before = PaintLiveBlocks();
    PaintOnce(&fixture, &result);
    /* The list is disposed before the second measurement, because its storage is the
     * module's *only* allocation and the balance is the question: a pass that leaked a
     * per-record block would show here rather than only under a sanitizer. */
    PaintResultDispose(&result);
    after = PaintLiveBlocks();
    RoboCheckEqInt((int)after, (int)before);

    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintRecords(void)
{
    TestRegister("paint.records.content_sequence", TestRecordsContentSequence);
    TestRegister("paint.records.structural_unconditional",
                       TestRecordsStructuralUnconditional);
    TestRegister("paint.records.content_geometry", TestRecordsContentGeometry);
    TestRegister("paint.records.fields", TestRecordsFields);
    TestRegister("paint.records.nested_order", TestRecordsNestedOrder);
    TestRegister("paint.records.siblings_order", TestRecordsSiblingsOrder);
    TestRegister("paint.records.snapshot_isolation", TestRecordsSnapshotIsolation);
    TestRegister("paint.records.ownership", TestRecordsOwnership);
    TestRegister("paint.records.empty_cases", TestRecordsEmptyCases);
    TestRegister("paint.records.geometry_safety", TestRecordsGeometrySafety);
    TestRegister("paint.records.determinism", TestRecordsDeterminism);
    TestRegister("paint.records.dump", TestRecordsDump);
    TestRegister("paint.records.allocator_balance", TestRecordsAllocatorBalance);
}
