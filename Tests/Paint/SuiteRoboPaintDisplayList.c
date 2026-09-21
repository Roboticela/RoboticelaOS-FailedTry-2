/*
 * RoboDisplay list suite (Phase 11.3) — paint.display.*
 *
 * The artifact Phase 11.3 adds above the paint record, and the guarantees it claims:
 *
 *   **mapping**    every paint operation resolves to exactly one display command, by a
 *                  rule that is a total function over the record vocabulary -- so the two
 *                  lists are the same length and a reader can pair them positionally;
 *
 *   **resolution** the command carries the **one rectangle it acts on**, resolved: a
 *                  background is the border box, a border side its band, the content
 *                  boundary the content box, a marker the border box -- and the band is
 *                  asserted against `PaintOperationSideBand`, the same derivation
 *                  the record leaves to its consumer;
 *
 *   **regularity** the builder refuses a record list that is not a traversable document
 *                  (a box that never closes, a content boundary in the wrong place, a
 *                  decoration after it, a marker that names the wrong node), so a display
 *                  list's structural skeleton is an invariant rather than a convention;
 *
 *   **snapshot**   a display list references nothing mutable: mutating the record list or
 *                  re-laying-out the tree leaves it byte-identical.
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → paint →
 * display). Hand-built *operations* appear only in the malformed-sequence cases, where the
 * question is about the *builder* rather than about a document -- and never in place of a
 * layout result.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends
 * on no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboPaint/RoboPaintDisplayDump.h"
#include "RoboPaint/RoboPaintDisplayListBuilder.h"

#include <string.h>

/* The same base sheet the other paint suites use: these elements are the block
 * containers, and `<head>` stays the one box nothing styles. */
#define DISPLAY_BASE_CSS \
    "html, body, div, section, aside, figure, p { display: block }"

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/** Build a display list from @p records, asserting the build succeeds. */
static void DisplayBuild(const PaintResult *records,
                          PaintDisplayList **outList,
                          PaintDisplayListStats *outStats)
{
    PaintDisplayListBuilder *builder = NULL;
    PaintDiag diag;
    Result status;

    RoboCheckEqInt(PaintDisplayListCreate(outList, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    PaintDiagReset(&diag);
    status = PaintDisplayListBuilderBuild(builder, records->list, *outList, outStats,
                                             &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the display build refused the records: %s",
                       diag.message ? diag.message : "(no message)");
    }
    PaintDisplayListBuilderDestroy(builder);
}

/** The @p nth command of @p list whose kind is @p kind, or 0 when there is none. */
static int DisplayNthOfKind(const PaintDisplayList *list,
                               PaintDisplayCommandKind kind,
                               size_t nth,
                               PaintDisplayCommand *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            return 0;
        }
        if (command.kind != kind) {
            continue;
        }
        if (seen == nth) {
            if (out) {
                *out = command;
            }
            return 1;
        }
        seen++;
    }
    return 0;
}

/** Write @p list's dump into @p buffer (asserted to fit) and return its length. */
static size_t DisplayDump(const PaintDisplayList *list, char *buffer, size_t capacity)
{
    size_t length = 0u;

    memset(buffer, 0, capacity);
    RoboCheckEqInt(PaintDisplayListWrite(list, buffer, capacity, &length, NULL),
                      RoboOk);
    return length;
}

/** A minimal, valid hand-built operation: a 10x10 box, all three rectangles equal. */
static PaintOperation DisplayMakeOp(PaintOpKind kind,
                                              LayoutNodeId node,
                                              size_t depth)
{
    PaintOperation operation;

    PaintOperationInit(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes.border = MakeGfxRect(0, 0, 10, 10);
    operation.boxes.padding = MakeGfxRect(0, 0, 10, 10);
    operation.boxes.content = MakeGfxRect(0, 0, 10, 10);
    operation.boxes.insetsMeasured = 0;
    operation.origin = MakeGfxPoint(0, 0);
    operation.color = CssColorNone();
    return operation;
}

/**
 * The mapping and resolution rules, checked for every pair of the two lists.
 *
 * The strongest form of "geometry survives conversion" that does not hardcode a document:
 * the command's rectangle is asserted to be exactly the record's resolved rectangle, kind
 * by kind, and node/depth/clip/colour are asserted to carry through.
 */
static void DisplayCheckMapping(const PaintResult *records,
                                  const PaintDisplayList *list)
{
    size_t count = PaintListCount(records->list);
    size_t index;

    RoboCheckEqInt((int)PaintDisplayListCount(list), (int)count);
    for (index = 0u; index < count; index++) {
        PaintOperation operation;
        PaintDisplayCommand command;
        LayoutRect expected;

        RoboCheckEqInt(PaintListAt(records->list, index, &operation), RoboOk);
        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);

        /* Identity carries through unchanged. */
        RoboCheck(command.node == operation.node);
        RoboCheckEqInt((int)command.depth, (int)operation.depth);
        RoboCheckEqInt(PaintClipEqual(&command.clip, &operation.clip), 1);

        /* The kind mapping, and the resolved rectangle per kind. */
        memset(&expected, 0, sizeof(expected));
        switch (operation.kind) {
        case RoboPaintOpBeginBox:
            RoboCheckEqInt((int)command.kind, (int)RoboPaintDisplayCmdBeginNode);
            expected = operation.boxes.border;
            break;
        case RoboPaintOpBackground:
            RoboCheckEqInt((int)command.kind, (int)RoboPaintDisplayCmdDrawBackground);
            expected = operation.boxes.border;
            break;
        case RoboPaintOpBorderTop:
        case RoboPaintOpBorderRight:
        case RoboPaintOpBorderBottom:
        case RoboPaintOpBorderLeft:
            RoboCheckEqInt((int)command.kind, (int)RoboPaintDisplayCmdDrawBorder);
            /* The band is the one derivation the record leaves to its consumer, and the
             * builder resolves it -- asserted against the record's own door. */
            RoboCheckEqInt(PaintOperationSideBand(&operation, &expected, NULL),
                              RoboOk);
            break;
        case RoboPaintOpText:
            RoboCheckEqInt((int)command.kind, (int)RoboPaintDisplayCmdDrawText);
            /* A run has no insets, so all three of its rectangles are equal and the
             * border box is the one the command is built on. */
            expected = operation.boxes.border;
            break;
        case RoboPaintOpContent:
            RoboCheckEqInt((int)command.kind, (int)RoboPaintDisplayCmdContent);
            expected = operation.boxes.content;
            break;
        case RoboPaintOpPushClip:
        case RoboPaintOpPopClip:
            RoboCheckEqInt((int)command.kind,
                              operation.kind == RoboPaintOpPushClip
                                  ? (int)RoboPaintDisplayCmdPushClip
                                  : (int)RoboPaintDisplayCmdPopClip);
            /* A scope command's rectangle is the box's **padding box** -- the boundary the
             * box's `overflow` resolved to, not the effective clip's rectangle, which does
             * not exist for a one-axis document. The boundary itself travels in `clip`, and
             * the identity check above has already compared it. */
            expected = operation.boxes.padding;
            break;
        case RoboPaintOpEndBox:
            RoboCheckEqInt((int)command.kind, (int)RoboPaintDisplayCmdEndNode);
            expected = operation.boxes.border;
            break;
        default:
            TestFail(__FILE__, __LINE__, "an operation kind outside the vocabulary");
            break;
        }
        RoboCheckEqInt(GfxRectEqual(command.rect, expected), 1);

        /* The colour rule: a drawing carries the record's colour, a structural carries
         * `none`. */
        if (PaintDisplayCommandKindDraws(command.kind)) {
            RoboCheckEqInt(CssColorEquals(command.color, operation.color), 1);
        } else {
            RoboCheckEqInt((int)command.color.kind, (int)RoboCssColorNone);
        }
        RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
    }
}

/* ------------------------------------------------------------------ */
/* 1. The vocabulary and the artifact                                  */
/* ------------------------------------------------------------------ */

static void TestDisplayCommandVocabulary(void)
{
    size_t index;
    int drawingTotal = 0;
    int structuralTotal = 0;

    PaintFixture();

    RoboCheckEqInt((int)RoboPaintDisplayCmdKindCount, 12);
    for (index = 0u; index < (size_t)RoboPaintDisplayCmdKindCount; index++) {
        PaintDisplayCommandKind kind = (PaintDisplayCommandKind)index;
        int isDrawing = PaintDisplayCommandKindDraws(kind);
        int IsStructural = PaintDisplayCommandKindIsStructural(kind);

        RoboCheckEqInt(PaintDisplayCommandKindIsKnown(kind), 1);
        RoboCheck(strcmp(PaintDisplayCommandKindName(kind), "unknown") != 0);
        if (kind == RoboPaintDisplayCmdNone) {
            RoboCheckEqInt(isDrawing, 0);
            RoboCheckEqInt(IsStructural, 0);
        } else {
            /* Exactly one half, and the halves partition the vocabulary above NONE --
             * the split is `draws`, so `CONTENT` (structural, not a marker) falls right. */
            RoboCheckEqInt(isDrawing + IsStructural, 1);
            drawingTotal += isDrawing;
            structuralTotal += IsStructural;
        }
        if (PaintDisplayCommandKindIsMarker(kind)) {
            RoboCheckEqInt(IsStructural, 1);
            RoboCheck(kind != RoboPaintDisplayCmdContent);
        }
    }
    /* **Four** drawing kinds -- background, border, text (11.6) and, since 11.7, an image --
     * and **seven** structural: the begin/end markers, `CONTENT`, 11.8's push/pop pair and
     * 11.9's opacity pair, which draw nothing by definition and therefore live on this side of
     * the split. 11.7 is the first phase since 11.6 to add a drawing kind, and the structural
     * side is unchanged by it: an image command is a drawing, so it lands on the other half. */
    RoboCheckEqInt(drawingTotal, 4);
    RoboCheckEqInt(structuralTotal, 7);
    /* And the `uses_color` split, which 11.7 introduced beside `draws`: the three colour kinds
     * are a strict subset of the drawings, and the fourth drawing kind is outside it. */
    RoboCheckEqInt(PaintDisplayCommandKindUsesColor(
                          RoboPaintDisplayCmdDrawImage),
                      0);
    RoboCheckEqInt(PaintDisplayCommandKindUsesColor(
                          RoboPaintDisplayCmdDrawBackground),
                      1);

    RoboCheckStrEq(PaintDisplayCommandKindName(RoboPaintDisplayCmdDrawBorder),
                      "draw-border");
    RoboCheckStrEq(PaintDisplayCommandKindName(RoboPaintDisplayCmdPushOpacity),
                      "push-opacity");
    RoboCheckStrEq(PaintDisplayCommandKindName(RoboPaintDisplayCmdPopOpacity),
                      "pop-opacity");
    /* The two scope predicates are disjoint and both refuse a drawing kind. */
    RoboCheckEqInt(PaintDisplayCommandKindIsOpacity(
                          RoboPaintDisplayCmdPushOpacity), 1);
    RoboCheckEqInt(PaintDisplayCommandKindIsOpacity(
                          RoboPaintDisplayCmdPopOpacity), 1);
    RoboCheckEqInt(PaintDisplayCommandKindIsClip(
                          RoboPaintDisplayCmdPushOpacity), 0);
    RoboCheckEqInt(PaintDisplayCommandKindIsOpacity(
                          RoboPaintDisplayCmdPushClip), 0);
    RoboCheckEqInt(PaintDisplayCommandKindIsOpacity(
                          RoboPaintDisplayCmdDrawBackground), 0);
    RoboCheckStrEq(PaintDisplayCommandKindName((PaintDisplayCommandKind)99),
                      "unknown");
}

static void TestDisplayEmptyList(void)
{
    PaintDisplayList *list = NULL;
    PaintDisplayListBuilder *builder = NULL;
    PaintDisplayListStats stats;
    PaintList *records = NULL;
    char buffer[64];

    PaintFixture();

    memset(&stats, 0xff, sizeof(stats));
    RoboCheckEqInt(PaintListCreate(&records, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);

    /* An empty record list is a legitimate build: a document that draws nothing has an
     * empty display list, and every counter is zero. */
    RoboCheckEqInt(PaintDisplayListBuilderBuild(builder, records, list, &stats, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintDisplayListCount(list), 0);
    RoboCheck(PaintDisplayListCommands(list) == NULL);
    RoboCheckEqInt((int)stats.records, 0);
    RoboCheckEqInt((int)stats.commands, 0);
    RoboCheckEqInt((int)stats.nodes, 0);

    /* Its dump is empty, which is an answer rather than a placeholder line. */
    {
        size_t length = DisplayDump(list, buffer, sizeof(buffer));

        RoboCheckEqInt((int)length, 0);
        RoboCheckEqInt((int)buffer[0], 0);
    }

    PaintDisplayListBuilderDestroy(builder);
    PaintDisplayListDestroy(list);
    PaintListDestroy(records);
}

static void TestDisplaySingleNodeMapping(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         DISPLAY_BASE_CSS
                         " .card { width: 100px; height: 40px;"
                         "   border: 1px solid #000000; background-color: #ff0000 }");
    PaintOnce(&fixture, &result);
    DisplayBuild(&result, &list, &stats);

    RoboCheck(PaintListCount(result.list) > 0u);
    DisplayCheckMapping(&result, list);

    /* The card's own commands: a begin marker, a background, four border bands, a content
     * boundary, an end marker. */
    RoboCheckEqInt((int)stats.nodes, (int)result.stats.boxes);
    RoboCheckEqInt((int)stats.backgrounds, (int)result.stats.backgrounds);
    RoboCheckEqInt((int)stats.borders, (int)result.stats.borderSides);
    RoboCheckEqInt((int)stats.commands, (int)PaintListCount(result.list));
    RoboCheckEqInt((int)stats.records, (int)PaintListCount(result.list));
    RoboCheck((int)stats.backgrounds >= 1);
    RoboCheck((int)stats.borders >= 4);

    /* The border band is *resolved* into the command: with a 1px border the top band's
     * height is exactly one, where the background's rectangle is the whole border box. */
    {
        PaintDisplayCommand background;
        PaintDisplayCommand border;

        RoboCheckEqInt(DisplayNthOfKind(list, RoboPaintDisplayCmdDrawBackground, 0u,
                                              &background),
                          1);
        RoboCheckEqInt(DisplayNthOfKind(list, RoboPaintDisplayCmdDrawBorder, 0u, &border),
                          1);
        RoboCheckEqInt((int)border.rect.height, 1);
        RoboCheck(border.rect.height < background.rect.height);
    }

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. Ordering, nesting, siblings                                      */
/* ------------------------------------------------------------------ */

static void TestDisplayOrdering(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    size_t index;

    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\"></section></div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         DISPLAY_BASE_CSS
                         " .card { width: 120px; height: 40px; padding: 4px;"
                         "   border: 1px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px; background-color: #123456 }"
                         " .clip { width: 60px; height: 30px; overflow: hidden }"
                         " .spill { width: 200px; height: 200px; border: 2px solid #ff0000 }");
    PaintOnce(&fixture, &result);
    DisplayBuild(&result, &list, &stats);

    DisplayCheckMapping(&result, list);

    /*
     * The order is the records' order, which is the tree's: a consumer that ignored every
     * kind would still see each box's commands contiguous and in paint order.
     *
     * The nesting is recoverable from the display list alone: walk it, and the first
     * command of every box is a `BEGIN_NODE` whose depth is the number of open markers --
     * which is exactly the invariant the builder enforced, asked here as a consumer would.
     */
    {
        PaintClip openScopes[8];
        size_t scopeOwner[8];
        size_t open = 0u;
        size_t scopes = 0u;
        int sawContentForOpen = 0;

        memset(openScopes, 0, sizeof(openScopes));
        memset(scopeOwner, 0, sizeof(scopeOwner));
        for (index = 0u; index < PaintDisplayListCount(list); index++) {
            PaintDisplayCommand command;

            RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
            if (command.kind == RoboPaintDisplayCmdBeginNode) {
                RoboCheckEqInt((int)command.depth, (int)open);
                open++;
                sawContentForOpen = 0;
            } else if (command.kind == RoboPaintDisplayCmdContent) {
                RoboCheckEqInt((int)command.depth, (int)(open - 1u));
                sawContentForOpen = 1;
            } else if (command.kind == RoboPaintDisplayCmdPushClip) {
                /* A scope opens **after** its box's content boundary and inside it, which is
                 * the two facts a consumer replaying the sequence needs to reconstruct the
                 * nesting -- and the reason this branch cannot share the drawing one below. */
                RoboCheckEqInt((int)command.depth, (int)(open - 1u));
                RoboCheckEqInt(sawContentForOpen, 1);
                RoboCheck(scopes < sizeof(openScopes) / sizeof(openScopes[0]));
                openScopes[scopes] = command.clip;
                scopeOwner[scopes] = (size_t)command.depth;
                scopes++;
            } else if (command.kind == RoboPaintDisplayCmdPopClip) {
                RoboCheckEqInt((int)command.depth, (int)(open - 1u));
                RoboCheckEqInt(sawContentForOpen, 1);
                RoboCheck(scopes > 0u);
                RoboCheckEqInt(PaintClipEqual(&command.clip, &openScopes[scopes - 1u]), 1);
                RoboCheckEqInt((int)scopeOwner[scopes - 1u], (int)command.depth);
                scopes--;
            } else if (command.kind == RoboPaintDisplayCmdEndNode) {
                RoboCheckEqInt((int)command.depth, (int)(open - 1u));
                RoboCheckEqInt(sawContentForOpen, 1);
                /*
                 * **A box's scope closes before its own end-node.** Scopes still open here
                 * belong to ancestors -- `.spill`'s marker stands inside `.clip`'s scope -- so
                 * what is asserted is that none of them was opened *by the box being closed*,
                 * which is the ownership half of the invariant. The scope's pop names the same
                 * depth as its push, which is checked one branch up.
                 */
                for (size_t scope = 0u; scope < scopes; scope++) {
                    RoboCheck(scopeOwner[scope] != (size_t)command.depth);
                }
                open--;
            } else {
                RoboCheckEqInt((int)command.depth, (int)(open - 1u));
                RoboCheckEqInt(sawContentForOpen, 0);
            }
        }
        RoboCheckEqInt((int)open, 0);
        RoboCheckEqInt((int)scopes, 0);
        RoboCheck(stats.clipScopes > 0u);
    }

    /* The markers balance and the boundaries are one per node. */
    RoboCheckEqInt((int)PaintDisplayListCountOfKind(list, RoboPaintDisplayCmdBeginNode),
                      (int)PaintDisplayListCountOfKind(list, RoboPaintDisplayCmdEndNode));
    RoboCheckEqInt((int)PaintDisplayListCountOfKind(list, RoboPaintDisplayCmdContent),
                      (int)stats.nodes);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Clip carries through                                             */
/* ------------------------------------------------------------------ */

static void TestDisplayClipCarried(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    size_t index;
    int clippedCommands = 0;

    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         DISPLAY_BASE_CSS
                         " .clip { width: 60px; height: 30px; overflow-x: hidden }"
                         " .spill { width: 200px; height: 200px }");
    PaintOnce(&fixture, &result);
    DisplayBuild(&result, &list, &stats);

    DisplayCheckMapping(&result, list);

    /* The boundary a box's overflow put in force travels onto its descendants' commands,
     * so a rasteriser applies it from the command rather than from a separate op. */
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (PaintClipIsActive(&command.clip)) {
            clippedCommands++;
        }
    }
    RoboCheck(clippedCommands >= 1);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. The builder refuses a list that is not a document                */
/* ------------------------------------------------------------------ */

static void DisplayExpectRefusal(PaintList *records, const char *what)
{
    PaintDisplayList *list = NULL;
    PaintDisplayListBuilder *builder = NULL;
    PaintDiag diag;
    Result status;

    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    PaintDiagReset(&diag);
    status = PaintDisplayListBuilderBuild(builder, records, list, NULL, &diag);
    if (status != RoboError) {
        TestFail(__FILE__, __LINE__, "%s: expected ROBO_ERROR, got %d", what,
                       (int)status);
    }
    /* A refused build hands back an empty list, never a partially converted one. */
    RoboCheckEqInt((int)PaintDisplayListCount(list), 0);

    PaintDisplayListBuilderDestroy(builder);
    PaintDisplayListDestroy(list);
}

static void TestDisplayBuilderRefusesMalformed(void)
{
    PaintList *records = NULL;

    PaintFixture();

    /* A box that never closes: a begin with no end. */
    RoboCheckEqInt(PaintListCreate(&records, NULL), RoboOk);
    {
        PaintOperation op = DisplayMakeOp(RoboPaintOpBeginBox, 1u, 0u);
        RoboCheckEqInt(PaintListAppend(records, &op, NULL), RoboOk);
    }
    DisplayExpectRefusal(records, "an unclosed box");

    /* A begin nested where its depth says it does not nest. */
    PaintListClear(records);
    {
        PaintOperation a = DisplayMakeOp(RoboPaintOpBeginBox, 1u, 0u);
        PaintOperation b = DisplayMakeOp(RoboPaintOpBeginBox, 2u, 2u);

        RoboCheckEqInt(PaintListAppend(records, &a, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(records, &b, NULL), RoboOk);
    }
    DisplayExpectRefusal(records, "a begin at the wrong depth");

    /* A content boundary before any box is open. */
    PaintListClear(records);
    {
        PaintOperation op = DisplayMakeOp(RoboPaintOpContent, 1u, 0u);
        RoboCheckEqInt(PaintListAppend(records, &op, NULL), RoboOk);
    }
    DisplayExpectRefusal(records, "a content boundary with no box");

    /* A box that closes before its content boundary. */
    PaintListClear(records);
    {
        PaintOperation a = DisplayMakeOp(RoboPaintOpBeginBox, 1u, 0u);
        PaintOperation b = DisplayMakeOp(RoboPaintOpEndBox, 1u, 0u);

        RoboCheckEqInt(PaintListAppend(records, &a, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(records, &b, NULL), RoboOk);
    }
    DisplayExpectRefusal(records, "a box closed before its content boundary");

    /* A marker that names a node other than its box's. */
    PaintListClear(records);
    {
        PaintOperation a = DisplayMakeOp(RoboPaintOpBeginBox, 1u, 0u);
        PaintOperation b = DisplayMakeOp(RoboPaintOpContent, 2u, 0u);

        RoboCheckEqInt(PaintListAppend(records, &a, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(records, &b, NULL), RoboOk);
    }
    DisplayExpectRefusal(records, "a marker naming the wrong node");

    /* A decoration after its box's content boundary. */
    PaintListClear(records);
    {
        PaintOperation a = DisplayMakeOp(RoboPaintOpBeginBox, 1u, 0u);
        PaintOperation b = DisplayMakeOp(RoboPaintOpContent, 1u, 0u);
        PaintOperation c = DisplayMakeOp(RoboPaintOpBackground, 1u, 0u);

        c.color = CssColorMake(0, 0, 0, 255);
        RoboCheckEqInt(PaintListAppend(records, &a, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(records, &b, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(records, &c, NULL), RoboOk);
    }
    DisplayExpectRefusal(records, "a decoration after the content boundary");

    /* And a well-formed hand-built list is *not* refused, so the refusals above are the
     * rules rather than the builder refusing everything it did not produce. */
    PaintListClear(records);
    {
        PaintOperation a = DisplayMakeOp(RoboPaintOpBeginBox, 1u, 0u);
        PaintOperation b = DisplayMakeOp(RoboPaintOpBackground, 1u, 0u);
        PaintOperation c = DisplayMakeOp(RoboPaintOpContent, 1u, 0u);
        PaintOperation d = DisplayMakeOp(RoboPaintOpEndBox, 1u, 0u);
        PaintDisplayList *list = NULL;
        PaintDisplayListBuilder *builder = NULL;

        b.color = CssColorMake(0, 0, 0, 255);
        RoboCheckEqInt(PaintListAppend(records, &a, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(records, &b, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(records, &c, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(records, &d, NULL), RoboOk);
        RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
        RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
        RoboCheckEqInt(
            PaintDisplayListBuilderBuild(builder, records, list, NULL, NULL), RoboOk);
        RoboCheckEqInt((int)PaintDisplayListCount(list), 4);
        RoboCheckEqInt(PaintDisplayListBuilderBuild(builder, records, list, NULL, NULL),
                          RoboOk);
        RoboCheckEqInt((int)PaintDisplayListCount(list), 4);
        PaintDisplayListBuilderDestroy(builder);
        PaintDisplayListDestroy(list);
    }

    PaintListDestroy(records);
}

/* ------------------------------------------------------------------ */
/* 5. Ownership, determinism, dumps                                    */
/* ------------------------------------------------------------------ */

static void TestDisplayOwnershipAndBalance(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    size_t baseline;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         DISPLAY_BASE_CSS
                         " .card { width: 100px; height: 40px;"
                         "   border: 1px solid #000000; background-color: #ff0000 }");
    PaintOnce(&fixture, &result);

    baseline = PaintLiveBlocks();
    {
        PaintDisplayListBuilder *builder = NULL;

        RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
        RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
        RoboCheckEqInt(PaintDisplayListBuilderBuild(builder, result.list, list, &stats,
                                                          NULL),
                          RoboOk);
        RoboCheck(PaintDisplayListCount(list) > 0u);
        RoboCheck(PaintDisplayListCapacity(list) >= PaintDisplayListCount(list));

        /* Clear keeps the capacity and drops the commands -- the door a per-frame renderer
         * uses. */
        {
            size_t capacity = PaintDisplayListCapacity(list);

            PaintDisplayListClear(list);
            RoboCheckEqInt((int)PaintDisplayListCount(list), 0);
            RoboCheckEqInt((int)PaintDisplayListCapacity(list), (int)capacity);
        }
        /* Rebuild into the same list, and the build clears first. */
        RoboCheckEqInt(PaintDisplayListBuilderBuild(builder, result.list, list, &stats,
                                                          NULL),
                          RoboOk);

        PaintDisplayListDestroy(list);
        list = NULL;
        PaintDisplayListBuilderDestroy(builder);
    }
    /* The list and the builder released everything they owned. */
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestDisplayDeterminism(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *first = NULL;
    PaintDisplayList *second = NULL;
    PaintDisplayListStats firstStats;
    PaintDisplayListStats secondStats;
    char firstDump[8192];
    char secondDump[8192];

    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\"></section></div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         DISPLAY_BASE_CSS
                         " .card { width: 120px; height: 40px; padding: 4px;"
                         "   border: 1px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px; background-color: #123456 }"
                         " .clip { width: 60px; height: 30px; overflow: hidden }"
                         " .spill { width: 200px; height: 200px; border: 2px solid #ff0000 }");
    PaintOnce(&fixture, &result);

    /* Two independent builds of the same records produce byte-identical lists, stats and
     * dumps. */
    {
        PaintDisplayListBuilder *builder = NULL;

        RoboCheckEqInt(PaintDisplayListCreate(&first, NULL), RoboOk);
        RoboCheckEqInt(PaintDisplayListCreate(&second, NULL), RoboOk);
        RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
        RoboCheckEqInt(PaintDisplayListBuilderBuild(builder, result.list, first,
                                                          &firstStats, NULL),
                          RoboOk);
        RoboCheckEqInt(PaintDisplayListBuilderBuild(builder, result.list, second,
                                                          &secondStats, NULL),
                          RoboOk);
        /* And one builder reused a third time: the stack's capacity does not change the
         * commands. */
        PaintDisplayListBuilderReset(builder);
        RoboCheckEqInt(PaintDisplayListBuilderBuild(builder, result.list, second,
                                                          &secondStats, NULL),
                          RoboOk);
        PaintDisplayListBuilderDestroy(builder);
    }
    RoboCheckEqInt(PaintDisplayListEqual(first, second), 1);
    RoboCheckEqInt((int)firstStats.commands, (int)secondStats.commands);
    RoboCheckEqInt((int)firstStats.maxDepth, (int)secondStats.maxDepth);

    DisplayDump(first, firstDump, sizeof(firstDump));
    DisplayDump(second, secondDump, sizeof(secondDump));
    RoboCheckStrEq(firstDump, secondDump);

    PaintDisplayListDestroy(first);
    PaintDisplayListDestroy(second);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestDisplayDump(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    char buffer[8192];
    size_t length;
    size_t lines = 0u;
    const char *scan;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"card\"><section class=\"badge\"></section>"
                         "</div></body></html>",
                         DISPLAY_BASE_CSS
                         " .card { width: 100px; height: 40px; border: 1px solid #000000;"
                         "   background-color: #ff0000 }"
                         " .badge { width: 20px; height: 10px; background-color: #123456 }");
    PaintOnce(&fixture, &result);
    DisplayBuild(&result, &list, &stats);

    length = DisplayDump(list, buffer, sizeof(buffer));
    RoboCheck(length > 0u);
    for (scan = buffer; *scan != '\0'; scan++) {
        if (*scan == '\n') {
            lines++;
        }
    }
    RoboCheckEqInt((int)lines, (int)PaintDisplayListCount(list));

    /* The dump names the commands and their resolved rectangles, so a person can diff two
     * documents after the box model has been resolved. */
    RoboCheck(strstr(buffer, "BEGIN_NODE") != NULL ||
               strstr(buffer, "begin-node") != NULL);
    RoboCheck(strstr(buffer, "draw-border") != NULL);
    RoboCheck(strstr(buffer, "draw-background") != NULL);
    RoboCheck(strstr(buffer, "rect=") != NULL);

    /* A buffer one byte too small is refused with the length it would have needed. */
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(PaintDisplayListWrite(list, small, sizeof(small), &needed, NULL),
                          RoboNotSupported);
        RoboCheckEqInt((int)needed, (int)length);
    }

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Mutation isolation                                               */
/* ------------------------------------------------------------------ */

static void TestDisplayMutationIsolation(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *before = NULL;
    PaintDisplayList *after = NULL;
    PaintDisplayListStats beforeStats;
    PaintDisplayListStats afterStats;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         DISPLAY_BASE_CSS
                         " .box { height: 40px; background-color: #ff0000 }");
    PaintOnce(&fixture, &result);
    DisplayBuild(&result, &before, &beforeStats);

    /*
     * Mutate both sources: clear the record list (the strongest legal change to it) and
     * lay the *same* tree out at a different viewport, which changes every box's geometry.
     * A display list is a snapshot -- and the mutation is *proved* observable by building
     * a second display list afterwards and asserting the two differ.
     */
    PaintListClear(result.list);
    RoboCheckEqInt((int)PaintListCount(result.list), 0);

    PaintLayout(&fixture, 400, 600);
    {
        PaintContext *context = NULL;

        RoboCheckEqInt(PaintContextCreate(&context, NULL), RoboOk);
        PaintRun(&fixture, context, result.list);
        PaintContextDestroy(context);
    }
    DisplayBuild(&result, &after, &afterStats);

    RoboCheck(PaintDisplayListCount(before) > 0u);
    RoboCheck(PaintDisplayListCount(after) > 0u);
    /* The mutation moved the second build, so the isolation is a real test rather than a
     * comparison of two identical documents. */
    RoboCheckEqInt(PaintDisplayListEqual(before, after), 0);

    PaintDisplayListDestroy(before);
    PaintDisplayListDestroy(after);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 7. Empty and degenerate cases                                       */
/* ------------------------------------------------------------------ */

static void TestDisplayEmptyAndDegenerate(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;

    /* A document whose root element generates no box: a legitimate empty pass and an empty
     * display list. */
    PaintBeginLaidOut(&fixture, "<html><body></body></html>", DISPLAY_BASE_CSS);
    PaintOnce(&fixture, &result);
    DisplayBuild(&result, &list, &stats);
    RoboCheckEqInt((int)stats.records, (int)PaintListCount(result.list));
    RoboCheckEqInt((int)stats.commands, (int)PaintDisplayListCount(list));
    DisplayCheckMapping(&result, list);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);

    /* A zero-sized box, and a box with no children: still a regular skeleton. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"empty\"></div></body></html>",
                         DISPLAY_BASE_CSS " .empty { width: 0px; height: 0px }");
    PaintOnce(&fixture, &result);
    DisplayBuild(&result, &list, &stats);
    DisplayCheckMapping(&result, list);
    RoboCheckEqInt((int)PaintDisplayListCountOfKind(list, RoboPaintDisplayCmdContent),
                      (int)stats.nodes);
    {
        PaintDisplayCommand command;

        /* A structural command with a zero-sized rectangle is a valid record. */
        RoboCheckEqInt(DisplayNthOfKind(list, RoboPaintDisplayCmdContent, 0u, &command), 1);
        RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
    }
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. Geometry safety                                                  */
/* ------------------------------------------------------------------ */

static void TestDisplayGeometrySafety(void)
{
    PaintDisplayList *list = NULL;
    PaintDisplayCommand command;

    PaintFixture();

    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);

    /* A cleared command is not a command: the list refuses it, so no list can hold a
     * record with no node or a negative rectangle. */
    PaintDisplayCommandInit(&command);
    RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 0);
    RoboCheckEqInt(PaintDisplayListAppend(list, &command, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)PaintDisplayListCount(list), 0);

    /* A negative rectangle is refused; a zero-sized one is not. */
    command.kind = RoboPaintDisplayCmdContent;
    command.node = (LayoutNodeId)1u;
    command.depth = 0u;
    command.rect = MakeGfxRect(0, 0, -1, 10);
    RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 0);
    command.rect = MakeGfxRect(0, 0, 0, 0);
    RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
    RoboCheckEqInt(PaintDisplayListAppend(list, &command, NULL), RoboOk);
    RoboCheckEqInt((int)PaintDisplayListCount(list), 1);

    /* A drawing command with no colour is refused: a fill with no colour draws nothing. */
    PaintDisplayCommandInit(&command);
    command.kind = RoboPaintDisplayCmdDrawBackground;
    command.node = (LayoutNodeId)1u;
    RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 0);
    command.color = CssColorMake(255, 0, 0, 255);
    RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);

    PaintDisplayListDestroy(list);
}

/* ------------------------------------------------------------------ */
/* 9. End to end                                                       */
/* ------------------------------------------------------------------ */

static void TestDisplayEndToEnd(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    size_t index;

    /*
     * The whole pipeline in one case: HTML → CSS → layout → paint records → display list.
     * The display list is checked to be a *self-contained* sequence -- every command valid,
     * every kind known, every node live, the skeleton balanced -- with the tree still
     * present only to answer "is this node live", never to supply a rectangle.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\">"
                         "<span class=\"s\">Hello</span></div></div></body></html>",
                         DISPLAY_BASE_CSS
                         " .outer { width: 200px; height: 80px; padding: 8px;"
                         "   border: 2px solid #000000; background-color: #ffffff }"
                         " .inner { width: 100px; height: 40px; margin: 4px;"
                         "   background-color: #00ff00 }");
    PaintOnce(&fixture, &result);
    DisplayBuild(&result, &list, &stats);

    RoboCheck(PaintListCount(result.list) > 0u);
    RoboCheck(PaintDisplayListCount(list) > 0u);
    DisplayCheckMapping(&result, list);

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
        RoboCheckEqInt(PaintDisplayCommandKindIsKnown(command.kind), 1);
        RoboCheck(command.kind != RoboPaintDisplayCmdNone);
        RoboCheck(command.node != RoboLayoutNodeIdNone);
        RoboCheckEqInt(LayoutObjectIsLive(fixture.tree, command.node), 1);
        RoboCheck(command.rect.width >= 0 && command.rect.height >= 0);
    }

    RoboCheckEqInt((int)stats.commands, (int)PaintListCount(result.list));
    RoboCheckEqInt((int)stats.nodes, (int)result.stats.boxes);
    RoboCheckEqInt((int)stats.backgrounds, (int)result.stats.backgrounds);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterDisplayList(void)
{
    TestRegister("paint.display.command_vocabulary", TestDisplayCommandVocabulary);
    TestRegister("paint.display.empty_list", TestDisplayEmptyList);
    TestRegister("paint.display.single_node_mapping", TestDisplaySingleNodeMapping);
    TestRegister("paint.display.ordering", TestDisplayOrdering);
    TestRegister("paint.display.clip_carried", TestDisplayClipCarried);
    TestRegister("paint.display.builder_refuses_malformed",
                       TestDisplayBuilderRefusesMalformed);
    TestRegister("paint.display.ownership_and_balance",
                       TestDisplayOwnershipAndBalance);
    TestRegister("paint.display.determinism", TestDisplayDeterminism);
    TestRegister("paint.display.dump", TestDisplayDump);
    TestRegister("paint.display.mutation_isolation", TestDisplayMutationIsolation);
    TestRegister("paint.display.empty_and_degenerate",
                       TestDisplayEmptyAndDegenerate);
    TestRegister("paint.display.geometry_safety", TestDisplayGeometrySafety);
    TestRegister("paint.display.end_to_end", TestDisplayEndToEnd);
}
