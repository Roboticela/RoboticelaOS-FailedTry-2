/*
 * RoboPaint unit suite (Phase 11.1) — paint.*
 *
 * The half of the phase's tests that asks each piece **on its own**: the two
 * vocabularies and their tables, the diagnostic record, the three-box derivation and the
 * clip record, the operation's validity rule and its two coordinate spaces, the list and
 * its ownership, and the dump's format. The other half — `SuiteRoboPaintTraversal.c` —
 * asks the same pieces through a real document, and the two are deliberately not the
 * same suite because they catch different things: a hand-built record catches an
 * arithmetic or a rule that a document would produce by accident, and a document catches
 * a rule that is right in isolation and wrong when a box is nested four deep.
 *
 * Nothing here fabricates a *layout result*: the hand-built records are the module's own
 * value types (a border box, an inset record, a clip), which are inputs a caller may
 * legitimately construct -- and the layout suite that produces them for real is
 * `layout.tests`' business, not this one's.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite
 * depends on no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Hand-built records, for the arithmetic                              */
/* ------------------------------------------------------------------ */

/** A used border record: the four widths, every side generating a border. */
static LayoutBorderEdges PaintBorderEdges(int32_t top,
                                                     int32_t right,
                                                     int32_t bottom,
                                                     int32_t left)
{
    LayoutBorders declared;
    LayoutBorderEdges edges;

    LayoutBordersInit(&declared);
    declared.top.width = LayoutBorderWidthResolved(top);
    declared.top.style = RoboCssBorderStyleSolid;
    declared.right.width = LayoutBorderWidthResolved(right);
    declared.right.style = RoboCssBorderStyleSolid;
    declared.bottom.width = LayoutBorderWidthResolved(bottom);
    declared.bottom.style = RoboCssBorderStyleSolid;
    declared.left.width = LayoutBorderWidthResolved(left);
    declared.left.style = RoboCssBorderStyleSolid;
    memset(&edges, 0, sizeof(edges));
    RoboCheckEqInt(LayoutBorderResolve(&declared, &edges, NULL), RoboOk);
    RoboCheckEqInt(LayoutBorderEdgesIsValid(&edges), 1);
    return edges;
}

/** A padding record: four used lengths. */
static LayoutPaddingEdges PaintPaddingEdges(int32_t top,
                                                       int32_t right,
                                                       int32_t bottom,
                                                       int32_t left)
{
    LayoutPaddingEdges edges;

    memset(&edges, 0, sizeof(edges));
    edges.top = top;
    edges.right = right;
    edges.bottom = bottom;
    edges.left = left;
    edges.resolved = 4u;
    edges.unresolved = 0u;
    RoboCheckEqInt(LayoutPaddingEdgesIsValid(&edges), 1);
    return edges;
}

/** A valid three-box record for a border box with the given insets. */
static PaintBoxes PaintBoxesMake(int32_t x, int32_t y, int32_t w, int32_t h,
                                           int32_t border, int32_t padding)
{
    LayoutBorderEdges borders = PaintBorderEdges(border, border, border, border);
    LayoutPaddingEdges paddings = PaintPaddingEdges(padding, padding, padding,
                                                               padding);
    LayoutRect box;
    PaintBoxes boxes;

    box.x = x;
    box.y = y;
    box.width = w;
    box.height = h;
    memset(&boxes, 0, sizeof(boxes));
    RoboCheckEqInt(PaintBoxesFromLayers(&box, &borders, &paddings, &boxes, NULL),
                      RoboOk);
    return boxes;
}

/** A `BEGIN_BOX` operation with the given node and boxes: the simplest valid marker. */
static PaintOperation PaintMarkerMake(LayoutNodeId node,
                                               size_t depth,
                                               PaintBoxes boxes)
{
    PaintOperation operation;

    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpBeginBox;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = boxes;
    return operation;
}

/** A drawing operation with the given kind and colour. */
static PaintOperation PaintDrawingMake(PaintOpKind kind,
                                                LayoutNodeId node,
                                                PaintBoxes boxes,
                                                CssColor color)
{
    PaintOperation operation;

    PaintOperationInit(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = 0u;
    operation.boxes = boxes;
    operation.color = color;
    /* The style rule's own shape (11.5): a hand-built drawing carries the style its kind
     * requires -- `solid` for a border side, because a side's band is its shape, and `none`
     * for a background, which has no side to style. A record is refused otherwise, which is
     * the assertion `TestPaintOperationRecord` makes on both halves. */
    operation.style = PaintOpKindIsBorder(kind) ? RoboCssBorderStyleSolid
                                                         : RoboCssBorderStyleNone;
    return operation;
}

/* ------------------------------------------------------------------ */
/* 1. The module                                                       */
/* ------------------------------------------------------------------ */

static void TestPaintModuleLifecycle(void)
{
    I32 major = -1;
    I32 minor = -1;
    I32 patch = -1;
    PaintCapabilities capabilities;

    PaintFixture();

    RoboCheckStrEq(PaintVersionString(), "0.12.0");
    RoboCheckEqInt(PaintGetVersion(&major, &minor, &patch), RoboOk);
    RoboCheckEqInt((int)major, 0);
    /* 11.12 is the release that turned the **last** field of the capability table, and it turned it
     * with one door rather than with a rasteriser of this module's own: the decoration pass gained a
     * bound (`PaintDecorationPaintBounded`) and the unbounded door became that door with no
     * rectangle. No meaning in the frozen v1 surface changed. */
    RoboCheckEqInt((int)minor, 12);
    RoboCheckEqInt((int)patch, 0);
    /* The three-way agreement the version header promises: the constant, the door and
     * the capability field are one statement -- and since 11.4 (backgrounds), 11.5
     * (border bands), 11.6 (text runs) and 11.8 (clip scopes) all three say a drawing
     * reaches a surface. */
    RoboCheckEqInt(PaintDrawingIsImplemented(), 1);
    RoboCheckEqInt(RoboPaintDrawingImplemented, 1);
    RoboCheckEqInt(RoboPaintApiFrozen, 0);
    RoboCheckEqInt(PaintGetCapabilities(&capabilities), RoboOk);
    RoboCheckEqInt(capabilities.drawing, RoboPaintDrawingImplemented);

    RoboCheckEqInt(PaintGetVersion(NULL, &minor, &patch), RoboInvalidArgument);
    RoboCheckEqInt(PaintGetVersion(&major, NULL, &patch), RoboInvalidArgument);
    RoboCheckEqInt(PaintGetVersion(&major, &minor, NULL), RoboInvalidArgument);
    RoboCheckEqInt(PaintGetCapabilities(NULL), RoboInvalidArgument);

    /* The lifecycle: init is idempotent-except-that-it-says-so, and shutdown is safe
     * twice. The module owns no state, which is why neither can fail for an allocation
     * reason -- and why a caller should not have to know that. */
    RoboCheckEqInt(PaintIsInitialized(), 1);
    RoboCheckEqInt(PaintInit(), RoboAlreadyExists);
    PaintShutdown();
    RoboCheckEqInt(PaintIsInitialized(), 0);
    PaintShutdown();
    RoboCheckEqInt(PaintIsInitialized(), 0);
    RoboCheckEqInt(PaintInit(), RoboOk);
    RoboCheckEqInt(PaintIsInitialized(), 1);
}

static void TestPaintKindVocabulary(void)
{
    static const char *const expected[RoboPaintOpKindCount] = {
        "none", "begin-box", "background", "border-top", "border-right", "border-bottom",
        "border-left", "content", "push-clip", "text", "pop-clip", "end-box",
        "push-opacity", "pop-opacity", "image"
    };
    size_t index;

    PaintFixture();

    RoboCheckEqInt((int)RoboPaintOpKindCount, 15);
    /* The zero of the vocabulary is `NONE`, so a cleared record is not an operation --
     * the same decision RoboLayout's border width kinds make one module down. */
    RoboCheckEqInt((int)RoboPaintOpNone, 0);
    RoboCheckEqInt((int)RoboPaintOpBeginBox, 1);
    RoboCheckEqInt((int)RoboPaintOpBackground, 2);
    /* `CONTENT` sits between the last decoration and the closing marker, because the
     * enumerators are in the order a linear consumer meets them (RoboPaintTypes.h). */
    RoboCheckEqInt((int)RoboPaintOpContent, 7);
    /* 11.8's scope pair brackets the contents of a box that clips, and 11.6's `TEXT`
     * sits **between** them because that is where a linear consumer meets a text run
     * inside a scope: `content`, `push-clip`, the run, `pop-clip`, `end-box`. The
     * numbering is the meeting order, not a taxonomy. */
    RoboCheckEqInt((int)RoboPaintOpPushClip, 8);
    RoboCheckEqInt((int)RoboPaintOpText, 9);
    RoboCheckEqInt((int)RoboPaintOpPopClip, 10);
    RoboCheckEqInt((int)RoboPaintOpEndBox, 11);
    /*
     * 11.9's pair is **appended** rather than inserted, and this is the assertion that says so:
     * every enumerator up to and including `END_BOX` kept the value it had, because a stored
     * enumerator is a contract (RoboPaintTypes.h argues it). The cost is that the numbering is
     * no longer the order a consumer meets the kinds in -- an opacity scope opens *before* a
     * box's decorations -- and the benefit is that no record already carrying a kind silently
     * means something else.
     */
    RoboCheckEqInt((int)RoboPaintOpPushOpacity, 12);
    RoboCheckEqInt((int)RoboPaintOpPopOpacity, 13);
    /*
     * And 11.7's `IMAGE` is appended for the same reason, with the one difference worth reading:
     * its **paint** order *is* the order a consumer meets it in -- between a box's border records
     * and its `CONTENT`, because an image is a replaced element's content -- so the row table in
     * `RoboPaintTypes.c` and the sequence agree for this kind and disagree only for the two
     * opacity rows above it.
     */
    RoboCheckEqInt((int)RoboPaintOpImage, 14);
    /* It draws (the fourth drawing kind) and it is **not** a kind whose look is a colour, which is
     * the distinction 11.7 added to this vocabulary: a payload rather than a fill. */
    RoboCheckEqInt(PaintOpKindDraws(RoboPaintOpImage), 1);
    RoboCheckEqInt(PaintOpKindIsStructural(RoboPaintOpImage), 0);
    RoboCheckEqInt(PaintOpKindUsesColor(RoboPaintOpImage), 0);
    RoboCheckEqInt(PaintOpKindIsOpacity(RoboPaintOpImage), 0);
    RoboCheckEqInt(PaintOpKindIsClip(RoboPaintOpImage), 0);
    /* Both are **structural**: a scope opens and closes and draws nothing. That is what keeps
     * the vocabulary's own partition -- `draws` plus `IsStructural` above `NONE` -- true, and
     * it is why the validity rule needs no third branch for them. */
    RoboCheckEqInt(PaintOpKindDraws(RoboPaintOpPushOpacity), 0);
    RoboCheckEqInt(PaintOpKindIsStructural(RoboPaintOpPushOpacity), 1);
    RoboCheckEqInt(PaintOpKindDraws(RoboPaintOpPopOpacity), 0);
    RoboCheckEqInt(PaintOpKindIsStructural(RoboPaintOpPopOpacity), 1);
    /* And the two predicates that name the pairs are disjoint: a kind is a clip scope or an
     * opacity scope or neither, never both. */
    RoboCheckEqInt(PaintOpKindIsOpacity(RoboPaintOpPushOpacity), 1);
    RoboCheckEqInt(PaintOpKindIsOpacity(RoboPaintOpPopOpacity), 1);
    RoboCheckEqInt(PaintOpKindIsClip(RoboPaintOpPushOpacity), 0);
    RoboCheckEqInt(PaintOpKindIsOpacity(RoboPaintOpPushClip), 0);
    RoboCheckEqInt(PaintOpKindIsOpacity(RoboPaintOpBackground), 0);
    RoboCheckEqInt(PaintOpKindIsOpacity(RoboPaintOpNone), 0);

    for (index = 0u; index < (size_t)RoboPaintOpKindCount; index++) {
        PaintOpKind kind = (PaintOpKind)index;

        RoboCheckEqInt(PaintOpKindIsKnown(kind), 1);
        RoboCheckStrEq(PaintOpKindName(kind), expected[index]);
        RoboCheck(PaintOpKindName(kind) != NULL);
        /* The structural/drawing split, asserted where the vocabulary is: every kind
         * above `NONE` either puts a pixel down or is structural, and never both. */
        if (kind == RoboPaintOpNone) {
            RoboCheckEqInt(PaintOpKindDraws(kind), 0);
            RoboCheckEqInt(PaintOpKindIsStructural(kind), 0);
        } else {
            RoboCheckEqInt(PaintOpKindDraws(kind) +
                                  PaintOpKindIsStructural(kind),
                              1);
        }
    }

    /* Every kind prints its own name: a table with a duplicated row would pass the walk
     * above and be wrong, and this is the cheap check that catches it. */
    for (index = 0u; index < (size_t)RoboPaintOpKindCount; index++) {
        size_t other;

        for (other = index + 1u; other < (size_t)RoboPaintOpKindCount; other++) {
            RoboCheck(strcmp(PaintOpKindName((PaintOpKind)index),
                              PaintOpKindName((PaintOpKind)other)) != 0);
        }
    }

    RoboCheckEqInt(PaintOpKindIsMarker(RoboPaintOpBeginBox), 1);
    RoboCheckEqInt(PaintOpKindIsMarker(RoboPaintOpEndBox), 1);
    RoboCheckEqInt(PaintOpKindIsMarker(RoboPaintOpBackground), 0);
    RoboCheckEqInt(PaintOpKindIsMarker(RoboPaintOpBorderTop), 0);

    RoboCheckEqInt(PaintOpKindIsBorder(RoboPaintOpBorderTop), 1);
    RoboCheckEqInt(PaintOpKindIsBorder(RoboPaintOpBorderRight), 1);
    RoboCheckEqInt(PaintOpKindIsBorder(RoboPaintOpBorderBottom), 1);
    RoboCheckEqInt(PaintOpKindIsBorder(RoboPaintOpBorderLeft), 1);
    RoboCheckEqInt(PaintOpKindIsBorder(RoboPaintOpBackground), 0);
    RoboCheckEqInt(PaintOpKindIsBorder(RoboPaintOpBeginBox), 0);

    /* The side is *derived* from the kind, so no operation can carry a side that
     * disagrees with what it draws -- and every other kind is a refusal rather than a
     * sentinel, which is what removing the `side` field bought. */
    {
        CssBoxSide side = RoboCssBoxSideLeft;
        PaintOpKind borderKinds[RoboCssBoxSideCount] = {
            RoboPaintOpBorderTop, RoboPaintOpBorderRight,
            RoboPaintOpBorderBottom, RoboPaintOpBorderLeft
        };

        for (index = 0u; index < (size_t)RoboCssBoxSideCount; index++) {
            RoboCheckEqInt(PaintOpKindSide(borderKinds[index], &side), RoboOk);
            RoboCheckEqInt((int)side, (int)index);
        }
        RoboCheckEqInt(PaintOpKindSide(RoboPaintOpBackground, &side),
                          RoboInvalidArgument);
        RoboCheckEqInt(PaintOpKindSide(RoboPaintOpBeginBox, &side),
                          RoboInvalidArgument);
        RoboCheckEqInt(PaintOpKindSide(RoboPaintOpBorderTop, NULL),
                          RoboInvalidArgument);
    }

    /* An undefined kind is not known, has no name, and is neither a marker nor a border
     * -- the three answers a caller gets instead of a crash. */
    RoboCheckEqInt(PaintOpKindIsKnown((PaintOpKind)99), 0);
    RoboCheckStrEq(PaintOpKindName((PaintOpKind)99), "unknown");
    RoboCheckEqInt(PaintOpKindIsMarker((PaintOpKind)99), 0);
    RoboCheckEqInt(PaintOpKindIsBorder((PaintOpKind)99), 0);
    RoboCheckEqInt(PaintOpKindIsKnown((PaintOpKind)-1), 0);
}

static void TestPaintDomainVocabulary(void)
{
    static const char *const expected[RoboPaintDomainCount] = {
        "none", "module", "context", "operation", "list", "geometry", "traversal", "clip",
        "dump", "display-list", "opacity", "hit-test", "invalidation"
    };
    size_t index;

    PaintFixture();

    RoboCheckEqInt((int)RoboPaintDomainCount, 13);
    RoboCheckEqInt((int)RoboPaintDomainNone, 0);
    RoboCheckEqInt((int)RoboPaintDomainGeometry, 5);
    RoboCheckEqInt((int)RoboPaintDomainDump, 8);
    RoboCheckEqInt((int)RoboPaintDomainDisplayList, 9);
    /* 11.9's domain, appended beside the display list's for the same reason: the module's own
     * failures are reported by the module that owns the failing thing. */
    RoboCheckEqInt((int)RoboPaintDomainOpacity, 10);
    /* And 11.10's, appended after it: the hit test's walk is not the paint pass's walk even
     * where the two report the same shape of failure. */
    RoboCheckEqInt((int)RoboPaintDomainHitTest, 11);
    /* 11.11's, and the last: a set that cannot be grown is a failure of the invalidation module
     * rather than of the pass whose list it was reading. */
    RoboCheckEqInt((int)RoboPaintDomainInvalidation, 12);
    for (index = 0u; index < (size_t)RoboPaintDomainCount; index++) {
        RoboCheckEqInt(PaintDomainIsKnown((PaintDiagnosticDomain)index), 1);
        RoboCheckStrEq(PaintDomainName((PaintDiagnosticDomain)index),
                          expected[index]);
    }
    RoboCheckEqInt(PaintDomainIsKnown((PaintDiagnosticDomain)99), 0);
    RoboCheckStrEq(PaintDomainName((PaintDiagnosticDomain)99), "unknown");
}

static void TestPaintDiagnosticsRecord(void)
{
    PaintDiag diag;

    PaintFixture();

    PaintDiagReset(&diag);
    RoboCheckEqInt(diag.code, RoboOk);
    RoboCheckEqInt((int)diag.domain, (int)RoboPaintDomainNone);
    RoboCheck(diag.message == NULL);
    RoboCheck(diag.file == NULL);
    RoboCheckEqInt(diag.line, 0);

    /* A success clears rather than stores, so a caller may reuse one record across a
     * sequence of calls and read only the last failure. */
    RoboCheckEqInt(PaintDiagSet(&diag, RoboOk, RoboPaintDomainList, "ignored",
                                          "f.c", 1),
                      RoboOk);
    RoboCheckEqInt(diag.code, RoboOk);
    RoboCheck(diag.message == NULL);

    RoboCheckEqInt(PaintDiagSet(&diag, RoboOutOfMemory, RoboPaintDomainList,
                                          "could not grow", "f.c", 42),
                      RoboOutOfMemory);
    RoboCheckEqInt(diag.code, RoboOutOfMemory);
    RoboCheckEqInt((int)diag.domain, (int)RoboPaintDomainList);
    RoboCheckStrEq(diag.message, "could not grow");
    RoboCheckStrEq(diag.file, "f.c");
    RoboCheckEqInt(diag.line, 42);

    /* An undefined domain is normalised to NONE rather than stored, so a reader never
     * sees a domain that has no name. */
    RoboCheckEqInt(PaintDiagSet(&diag, RoboError,
                                          (PaintDiagnosticDomain)77, "bad", "f.c", 3),
                      RoboError);
    RoboCheckEqInt((int)diag.domain, (int)RoboPaintDomainNone);

    /* The macro form is what the module's own call sites use: it always yields the code
     * and tolerates a NULL sink, because diagnostics are optional and the result is
     * authoritative. */
    RoboCheckEqInt(RoboPaintDiag(NULL, RoboPaintDomainContext, RoboNotFound, "x"),
                      RoboNotFound);
    RoboCheckEqInt(RoboPaintDiag(&diag, RoboPaintDomainContext, RoboNotFound, "x"),
                      RoboNotFound);
    RoboCheckEqInt(diag.code, RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboPaintDomainContext);

    RoboCheckEqInt(PaintDiagSet(NULL, RoboError, RoboPaintDomainNone, "x",
                                          "f.c", 1),
                      RoboInvalidArgument);
    PaintDiagReset(NULL); /* safe */
}

/* ------------------------------------------------------------------ */
/* 2. The three boxes                                                  */
/* ------------------------------------------------------------------ */

static void TestPaintBoxesRecord(void)
{
    PaintBoxes boxes;
    PaintBoxes other;

    PaintFixture();

    PaintBoxesInit(&boxes);
    RoboCheckEqInt(boxes.insetsMeasured, 0);
    RoboCheckEqInt(boxes.border.width, 0);
    RoboCheckEqInt(boxes.padding.width, 0);
    RoboCheckEqInt(boxes.content.width, 0);
    /* A cleared record is a *record*: three equal empty rectangles with no insets. */
    RoboCheckEqInt(PaintBoxesIsValid(&boxes), 1);
    RoboCheckEqInt(PaintBoxesEqual(&boxes, &boxes), 1);

    other = PaintBoxesMake(10, 20, 100, 50, 2, 5);
    RoboCheckEqInt(PaintBoxesEqual(&boxes, &other), 0);
    RoboCheckEqInt(PaintBoxesIsValid(&other), 1);

    /* A record whose layers cross is not a record: the three are nested by
     * construction, so a padding box outside its border box is arithmetic that went
     * wrong one level down. */
    {
        PaintBoxes crossed = other;

        crossed.padding.x = 0;
        crossed.padding.width = 1000;
        RoboCheckEqInt(PaintBoxesIsValid(&crossed), 0);
    }
    /* And a record whose flag disagrees with its geometry is not one either: a box whose
     * insets were not measured has one rectangle wearing three names. */
    {
        PaintBoxes unmeasured = other;

        unmeasured.insetsMeasured = 0;
        RoboCheckEqInt(PaintBoxesIsValid(&unmeasured), 0);
    }
    RoboCheckEqInt(PaintBoxesIsValid(NULL), 0);
    RoboCheckEqInt(PaintBoxesEqual(NULL, NULL), 1);
    RoboCheckEqInt(PaintBoxesEqual(&other, NULL), 0);
    PaintBoxesInit(NULL); /* safe */
}

static void TestPaintBoxesFromLayers(void)
{
    LayoutBorderEdges borders = PaintBorderEdges(2, 2, 2, 2);
    LayoutPaddingEdges paddings = PaintPaddingEdges(5, 5, 5, 5);
    LayoutPaddingEdges nonePadding = PaintPaddingEdges(0, 0, 0, 0);
    LayoutRect box;
    PaintBoxes boxes;

    PaintFixture();
    box.x = 10;
    box.y = 20;
    box.width = 100;
    box.height = 50;
    memset(&boxes, 0, sizeof(boxes));

    RoboCheckEqInt(PaintBoxesFromLayers(&box, &borders, &paddings, &boxes, NULL),
                      RoboOk);
    /* border -> padding -> content, geometrically distinct and each inset from the one
     * outside it. These are the numbers a paint pass draws bands between, so an error
     * here is a border in the wrong place rather than a missing one. */
    PaintRectIs(10, 20, 100, 50, boxes.border, "the border box");
    PaintRectIs(12, 22, 96, 46, boxes.padding, "the padding box");
    PaintRectIs(17, 27, 86, 36, boxes.content, "the content box");
    RoboCheckEqInt(boxes.insetsMeasured, 1);
    RoboCheckEqInt(PaintBoxesIsValid(&boxes), 1);

    /* No insets: three rectangles that agree, and the flag *still* set -- the author's
     * declarations were read and came to nothing, which is a different statement from
     * "this box has no layers to measure". */
    {
        LayoutBorderEdges noneBorders = PaintBorderEdges(0, 0, 0, 0);

        RoboCheckEqInt(PaintBoxesFromLayers(&box, &noneBorders, &nonePadding,
                                                       &boxes, NULL),
                          RoboOk);
        PaintRectIs(10, 20, 100, 50, boxes.padding, "an unchanged padding box");
        PaintRectIs(10, 20, 100, 50, boxes.content, "an unchanged content box");
        RoboCheckEqInt(boxes.insetsMeasured, 1);
    }

    /* A box at the edge of the plane: the origins saturate and no extent goes negative,
     * which is the rule Phase 10.15 found the cost of getting wrong. */
    {
        LayoutRect saturated;
        LayoutBorderEdges wide = PaintBorderEdges(5, 5, 5, 5);

        saturated.x = INT32_MAX;
        saturated.y = INT32_MAX;
        saturated.width = INT32_MAX;
        saturated.height = INT32_MAX;
        RoboCheckEqInt(PaintBoxesFromLayers(&saturated, &wide, &nonePadding,
                                                       &boxes, NULL),
                          RoboOk);
        RoboCheck(boxes.padding.x >= 0);
        RoboCheck(boxes.padding.width >= 0);
        RoboCheck(boxes.content.width >= 0);
        RoboCheck(boxes.content.height >= 0);
        RoboCheckEqInt(PaintBoxesIsValid(&boxes), 1);
    }

    /* A border wider than the box leaves no room for the padding box, and a negative
     * width is not a width: the floor is applied to the result. */
    {
        LayoutRect thin;
        LayoutBorderEdges thick = PaintBorderEdges(60, 60, 60, 60);

        thin.x = 0;
        thin.y = 0;
        thin.width = 100;
        thin.height = 50;
        RoboCheckEqInt(PaintBoxesFromLayers(&thin, &thick, &nonePadding, &boxes,
                                                       NULL),
                          RoboOk);
        RoboCheckEqInt(boxes.padding.width, 0);
        RoboCheckEqInt(boxes.padding.height, 0);
        RoboCheckEqInt(PaintBoxesIsValid(&boxes), 1);
    }

    /* The refusals: NULL arguments, a negative-size border box, and an inset record that
     * is not one (a hand-built record with counters that disagree with four sides). */
    RoboCheckEqInt(PaintBoxesFromLayers(NULL, &borders, &paddings, &boxes, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintBoxesFromLayers(&box, NULL, &paddings, &boxes, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintBoxesFromLayers(&box, &borders, NULL, &boxes, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintBoxesFromLayers(&box, &borders, &paddings, NULL, NULL),
                      RoboInvalidArgument);
    {
        LayoutRect negative = box;

        negative.width = -1;
        RoboCheckEqInt(PaintBoxesFromLayers(&negative, &borders, &paddings,
                                                       &boxes, NULL),
                          RoboInvalidArgument);
    }
    {
        LayoutPaddingEdges broken = paddings;

        broken.resolved = 9u; /* counters that disagree with two axes */
        RoboCheck(PaintBoxesFromLayers(&box, &borders, &broken, &boxes, NULL) !=
                   RoboOk);
    }
}

static void TestPaintBoxesTranslate(void)
{
    PaintBoxes boxes = PaintBoxesMake(10, 20, 100, 50, 2, 5);
    PaintBoxes moved;

    PaintFixture();
    memset(&moved, 0, sizeof(moved));

    RoboCheckEqInt(PaintBoxesTranslate(&boxes, 5, 7, &moved, NULL), RoboOk);
    PaintRectIs(15, 27, 100, 50, moved.border, "the moved border box");
    PaintRectIs(17, 29, 96, 46, moved.padding, "the moved padding box");
    PaintRectIs(22, 34, 86, 36, moved.content, "the moved content box");
    RoboCheckEqInt(moved.insetsMeasured, 1);

    /* A displacement that would wrap saturates instead: a coordinate at the edge of the
     * plane is a wrong answer that is still drawable, where a wrapped one is a box at
     * the wrong end of the page. */
    RoboCheckEqInt(PaintBoxesTranslate(&boxes, INT32_MAX, INT32_MIN, &moved,
                                                 NULL),
                      RoboOk);
    RoboCheckEqInt(moved.border.width, boxes.border.width);
    RoboCheckEqInt(moved.border.height, boxes.border.height);
    RoboCheck(moved.border.x >= boxes.border.x);

    RoboCheckEqInt(PaintBoxesTranslate(NULL, 1, 1, &moved, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintBoxesTranslate(&boxes, 1, 1, NULL, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* 3. The clip in force                                                */
/* ------------------------------------------------------------------ */

static void TestPaintClipRecord(void)
{
    PaintClip clip;
    PaintClip none = PaintClipNone();

    PaintFixture();

    PaintClipInit(&clip);
    RoboCheckEqInt((int)clip.horizontal.mode, (int)RoboCssOverflowVisible);
    RoboCheckEqInt((int)clip.vertical.mode, (int)RoboCssOverflowVisible);
    /* A cleared clip is CSS's initial value on both axes, so a document that never
     * declared an overflow carries it unchanged from the root down. */
    RoboCheckEqInt(PaintClipAxisIsActive(&clip.horizontal), 0);
    RoboCheckEqInt(PaintClipAxisIsActive(&clip.vertical), 0);
    RoboCheckEqInt(PaintClipIsActive(&clip), 0);
    RoboCheckEqInt(PaintClipIsValid(&clip), 1);
    RoboCheckEqInt(PaintClipEqual(&clip, &none), 1);

    /* "Has a boundary" and "this mode clips" are one question, borrowed from RoboCSS
     * rather than re-decided here. */
    clip.horizontal.mode = RoboCssOverflowHidden;
    RoboCheckEqInt(PaintClipAxisIsActive(&clip.horizontal), 1);
    RoboCheckEqInt(PaintClipIsActive(&clip), 1);

    /* An interval that runs backwards on an axis that hides is not a record. */
    {
        PaintClip backwards = clip;

        backwards.horizontal.start = 100;
        backwards.horizontal.end = 0;
        RoboCheckEqInt(PaintClipIsValid(&backwards), 0);
        /* The same numbers on a *visible* axis mean nothing and are therefore fine. */
        backwards.horizontal.mode = RoboCssOverflowVisible;
        RoboCheckEqInt(PaintClipIsValid(&backwards), 1);
    }
    {
        PaintClip unknown;

        PaintClipInit(&unknown);
        unknown.vertical.mode = (CssOverflow)99;
        RoboCheckEqInt(PaintClipIsValid(&unknown), 0);
    }

    /* An active axis with no extent is empty, and it is *success* rather than a
     * failure: a box a clipping ancestor has already excluded draws nothing. */
    clip.horizontal.start = 10;
    clip.horizontal.end = 10;
    RoboCheckEqInt(PaintClipAxisIsEmpty(&clip.horizontal), 1);
    clip.horizontal.end = 20;
    RoboCheckEqInt(PaintClipAxisIsEmpty(&clip.horizontal), 0);

    RoboCheckEqInt(PaintClipAxisIsActive(NULL), 0);
    RoboCheckEqInt(PaintClipIsActive(NULL), 0);
    RoboCheckEqInt(PaintClipIsValid(NULL), 0);
    RoboCheckEqInt(PaintClipEqual(NULL, NULL), 1);
    RoboCheckEqInt(PaintClipEqual(&clip, NULL), 0);
    RoboCheckEqInt(PaintClipAxisIsEmpty(NULL), 0);
    PaintClipInit(NULL); /* safe */
}

static void TestPaintClipRectDoor(void)
{
    PaintClip clip = PaintClipNone();
    LayoutRect rect;

    PaintFixture();
    memset(&rect, 0, sizeof(rect));

    /* One rectangle says everything only when *both* axes are bounded, and the door
     * refuses the rest rather than answering a `visible` axis with a number that means
     * "free" as if it meant "bounded". */
    RoboCheckEqInt(PaintClipRect(&clip, &rect), RoboNotFound);
    clip.horizontal.mode = RoboCssOverflowHidden;
    clip.horizontal.start = 1;
    clip.horizontal.end = 11;
    RoboCheckEqInt(PaintClipRect(&clip, &rect), RoboNotFound);
    clip.vertical.mode = RoboCssOverflowHidden;
    clip.vertical.start = 2;
    clip.vertical.end = 12;
    RoboCheckEqInt(PaintClipRect(&clip, &rect), RoboOk);
    PaintRectIs(1, 2, 10, 10, rect, "the clip rectangle");
    RoboCheckEqInt(PaintClipRect(NULL, &rect), RoboInvalidArgument);
    RoboCheckEqInt(PaintClipRect(&clip, NULL), RoboInvalidArgument);
}

static void TestPaintClipIntersect(void)
{
    PaintClip clip = PaintClipNone();
    PaintClip out;
    LayoutRect boundary;

    PaintFixture();
    boundary.x = 10;
    boundary.y = 20;
    boundary.width = 100;
    boundary.height = 50;

    /* A boundary that hides neither axis leaves the clip in force untouched -- the case
     * every box in an ordinary document produces on every descent, and the reason the
     * traversal can call this unconditionally. */
    RoboCheckEqInt(PaintClipIntersect(&clip, &boundary, RoboCssOverflowVisible,
                                                RoboCssOverflowVisible, &out, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintClipEqual(&out, &clip), 1);

    /* Hidden on both axes: the interval is the boundary's, as a half-open range. */
    RoboCheckEqInt(PaintClipIntersect(&clip, &boundary, RoboCssOverflowHidden,
                                                RoboCssOverflowHidden, &out, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintClipIsActive(&out), 1);
    RoboCheckEqInt(out.horizontal.start, 10);
    RoboCheckEqInt(out.horizontal.end, 110);
    RoboCheckEqInt(out.vertical.start, 20);
    RoboCheckEqInt(out.vertical.end, 70);

    /* The axis split, which is the whole reason the record is two intervals: a boundary
     * hidden horizontally and visible vertically bounds *only* x, and nothing about
     * `y` moves. */
    {
        PaintClip split = PaintClipNone();

        RoboCheckEqInt(PaintClipIntersect(&split, &boundary,
                                                    RoboCssOverflowHidden,
                                                    RoboCssOverflowVisible, &out, NULL),
                          RoboOk);
        RoboCheckEqInt(PaintClipAxisIsActive(&out.horizontal), 1);
        RoboCheckEqInt(PaintClipAxisIsActive(&out.vertical), 0);
        RoboCheckEqInt(out.horizontal.end, 110);
    }

    /* A nested boundary is an intersection on the axes that hide: the larger start and
     * the smaller end, per axis. */
    {
        PaintClip outer;
        LayoutRect inner;

        PaintClipInit(&outer);
        outer.horizontal.mode = RoboCssOverflowHidden;
        outer.horizontal.start = 0;
        outer.horizontal.end = 60;
        inner.x = 30;
        inner.y = 0;
        inner.width = 100;
        inner.height = 10;
        RoboCheckEqInt(PaintClipIntersect(&outer, &inner, RoboCssOverflowHidden,
                                                    RoboCssOverflowVisible, &out, NULL),
                          RoboOk);
        RoboCheckEqInt(out.horizontal.start, 30);
        RoboCheckEqInt(out.horizontal.end, 60);
        /* An interval that comes out empty is success with an empty interval. */
        {
            LayoutRect far;

            far.x = 200;
            far.y = 0;
            far.width = 10;
            far.height = 10;
            RoboCheckEqInt(PaintClipIntersect(&outer, &far,
                                                        RoboCssOverflowHidden,
                                                        RoboCssOverflowVisible, &out, NULL),
                              RoboOk);
            RoboCheckEqInt(PaintClipAxisIsEmpty(&out.horizontal), 1);
        }
    }

    /* The refusals. */
    RoboCheckEqInt(PaintClipIntersect(NULL, &boundary, RoboCssOverflowVisible,
                                                RoboCssOverflowVisible, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintClipIntersect(&clip, NULL, RoboCssOverflowVisible,
                                                RoboCssOverflowVisible, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintClipIntersect(&clip, &boundary, RoboCssOverflowVisible,
                                                RoboCssOverflowVisible, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintClipIntersect(&clip, &boundary,
                                                (CssOverflow)99,
                                                RoboCssOverflowVisible, &out, NULL),
                      RoboInvalidArgument);
    {
        LayoutRect negative = boundary;

        negative.height = -1;
        RoboCheckEqInt(PaintClipIntersect(&clip, &negative,
                                                    RoboCssOverflowHidden,
                                                    RoboCssOverflowHidden, &out, NULL),
                          RoboInvalidArgument);
    }
    {
        PaintClip broken = PaintClipNone();

        broken.horizontal.mode = RoboCssOverflowHidden;
        broken.horizontal.start = 10;
        broken.horizontal.end = 5;
        RoboCheckEqInt(PaintClipIntersect(&broken, &boundary,
                                                    RoboCssOverflowHidden,
                                                    RoboCssOverflowHidden, &out, NULL),
                          RoboError);
    }
}

/* ------------------------------------------------------------------ */
/* 4. The operation                                                    */
/* ------------------------------------------------------------------ */

static void TestPaintOperationRecord(void)
{
    PaintBoxes boxes = PaintBoxesMake(0, 0, 100, 40, 2, 5);
    PaintOperation operation;
    CssColor red = CssColorMake(255, 0, 0, 255);

    PaintFixture();

    PaintOperationInit(&operation);
    RoboCheckEqInt((int)operation.kind, (int)RoboPaintOpNone);
    RoboCheckEqInt((int)operation.node, (int)RoboLayoutNodeIdNone);
    /* A colour a rasteriser *cannot* use, which is what makes a cleared record a stated
     * absence rather than a black that might be drawn. */
    RoboCheckEqInt((int)operation.color.kind, (int)RoboCssColorNone);
    RoboCheckEqInt(PaintClipIsActive(&operation.clip), 0);
    RoboCheckEqInt(PaintOperationIsValid(&operation), 0);

    /* A marker: no colour, a live node, valid boxes. */
    operation = PaintMarkerMake(3, 1u, boxes);
    RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
    RoboCheckEqInt(PaintOperationIsMarker(&operation), 1);
    RoboCheckEqInt(PaintOperationIsDrawing(&operation), 0);

    /* Both halves of the colour rule, in both directions. */
    {
        PaintOperation wrong = operation;

        wrong.color = red;
        RoboCheckEqInt(PaintOperationIsValid(&wrong), 0);
    }
    operation = PaintDrawingMake(RoboPaintOpBackground, 3, boxes, red);
    RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
    RoboCheckEqInt(PaintOperationIsMarker(&operation), 0);
    RoboCheckEqInt(PaintOperationIsDrawing(&operation), 1);
    {
        PaintOperation wrong = operation;

        wrong.color = CssColorNone();
        /* A fill with no colour draws nothing, and an operation that draws nothing is
         * not a statement about the document. */
        RoboCheckEqInt(PaintOperationIsValid(&wrong), 0);
    }
    {
        PaintOperation contextual = operation;

        contextual.color = CssColorCurrent();
        RoboCheckEqInt(PaintOperationIsValid(&contextual), 1);
    }
    {
        PaintOperation invalid = operation;

        invalid.color = CssColorInvalid();
        RoboCheckEqInt(PaintOperationIsValid(&invalid), 0);
    }

    /*
     * And the style rule, one field along from the colour's (11.5). It is asserted from both
     * sides because it has two halves that fail differently: a border record must say **which**
     * of the ten styles shapes its band, since that fact is not derivable from the band's
     * rectangle, and a background record must not carry a style, because it has no side to style.
     */
    {
        PaintOperation border = PaintDrawingMake(RoboPaintOpBorderTop, 3, boxes,
                                                          red);

        RoboCheckEqInt((int)PaintOperationStyle(&border),
                          (int)RoboCssBorderStyleSolid);
        RoboCheckEqInt(PaintOperationIsValid(&border), 1);
        border.style = RoboCssBorderStyleNone;
        RoboCheckEqInt(PaintOperationIsValid(&border), 0);
        border.style = RoboCssBorderStyleHidden;
        RoboCheckEqInt(PaintOperationIsValid(&border), 0);
        border.style = RoboCssBorderStyleDashed;
        RoboCheckEqInt(PaintOperationIsValid(&border), 1);
    }
    {
        PaintOperation wrong = operation;

        wrong.style = RoboCssBorderStyleSolid;
        RoboCheckEqInt(PaintOperationIsValid(&wrong), 0);
    }

    /* Every other refusal: a cleared kind, an unknown kind, no node, boxes that are not
     * a record, a clip that is not one. */
    {
        PaintOperation wrong = operation;

        wrong.kind = RoboPaintOpNone;
        RoboCheckEqInt(PaintOperationIsValid(&wrong), 0);
        wrong.kind = (PaintOpKind)99;
        RoboCheckEqInt(PaintOperationIsValid(&wrong), 0);
        wrong = operation;
        wrong.node = RoboLayoutNodeIdNone;
        RoboCheckEqInt(PaintOperationIsValid(&wrong), 0);
        wrong = operation;
        wrong.boxes.padding.width = 0;
        wrong.boxes.content.x = 500;
        RoboCheckEqInt(PaintOperationIsValid(&wrong), 0);
        wrong = operation;
        wrong.clip.horizontal.mode = RoboCssOverflowHidden;
        wrong.clip.horizontal.start = 10;
        wrong.clip.horizontal.end = 1;
        RoboCheckEqInt(PaintOperationIsValid(&wrong), 0);
    }
    RoboCheckEqInt(PaintOperationIsValid(NULL), 0);
    PaintOperationInit(NULL); /* safe */
}

static void TestPaintOperationAccessors(void)
{
    PaintBoxes boxes = PaintBoxesMake(10, 20, 100, 40, 2, 5);
    PaintOperation a = PaintDrawingMake(RoboPaintOpBorderTop, 7, boxes,
                                                 CssColorMake(0, 0, 255, 255));
    PaintOperation b;
    PaintBoxes read;
    LayoutPoint origin;
    PaintClip clip;
    CssBoxSide side;

    PaintFixture();
    b = a;
    a.origin.x = 10;
    a.origin.y = 20;
    b.origin.x = 10;
    b.origin.y = 20;

    RoboCheckEqInt(PaintOperationEqual(&a, &b), 1);
    RoboCheckEqInt(PaintOperationEqual(&a, &a), 1);
    RoboCheckEqInt(PaintOperationEqual(NULL, NULL), 1);
    RoboCheckEqInt(PaintOperationEqual(&a, NULL), 0);

    /* Field by field: each one on its own, because a comparison that ignored a field
     * would make two different conclusions look equal. */
    {
        PaintOperation c = a;

        c.kind = RoboPaintOpBorderRight;
        RoboCheckEqInt(PaintOperationEqual(&a, &c), 0);
        c = a;
        c.node = 8;
        RoboCheckEqInt(PaintOperationEqual(&a, &c), 0);
        c = a;
        c.depth = 2u;
        RoboCheckEqInt(PaintOperationEqual(&a, &c), 0);
        c = a;
        c.origin.x = 11;
        RoboCheckEqInt(PaintOperationEqual(&a, &c), 0);
        c = a;
        c.color = CssColorMake(0, 0, 255, 254);
        RoboCheckEqInt(PaintOperationEqual(&a, &c), 0);
        c = a;
        c.clip.vertical.mode = RoboCssOverflowHidden;
        c.clip.vertical.start = 0;
        c.clip.vertical.end = 10;
        RoboCheckEqInt(PaintOperationEqual(&a, &c), 0);
    }

    RoboCheckEqInt((int)PaintOperationKind(&a), (int)RoboPaintOpBorderTop);
    RoboCheckEqInt((int)PaintOperationNode(&a), 7);
    RoboCheckEqInt((int)PaintOperationDepth(&a), 0);
    RoboCheckEqInt(PaintOperationSide(&a, &side), RoboOk);
    RoboCheckEqInt((int)side, (int)RoboCssBoxSideTop);
    RoboCheckEqInt(PaintOperationSide(&a, NULL), RoboInvalidArgument);
    {
        PaintOperation background = PaintDrawingMake(
            RoboPaintOpBackground, 7, boxes, CssColorMake(0, 0, 0, 255));

        RoboCheckEqInt(PaintOperationSide(&background, &side),
                          RoboInvalidArgument);
    }

    PaintOperationBoxes(&a, &read);
    RoboCheckEqInt(PaintBoxesEqual(&read, &boxes), 1);
    PaintOperationOrigin(&a, &origin);
    RoboCheckEqInt(origin.x, 10);
    RoboCheckEqInt(origin.y, 20);
    PaintOperationClip(&a, &clip);
    RoboCheckEqInt(PaintClipIsActive(&clip), 0);
    PaintColorEq(PaintOperationColor(&a), CssColorMake(0, 0, 255, 255),
                   "the operation's colour");

    /* The two coordinate spaces: `local = boxes - origin`, and it is exactly what
     * layout wrote -- which is the assertion that makes carrying both halves safe rather
     * than a second opinion. */
    {
        PaintBoxes local;

        memset(&local, 0, sizeof(local));
        RoboCheckEqInt(PaintOperationLocalBoxes(&a, &local, NULL), RoboOk);
        PaintRectIs(0, 0, 100, 40, local.border, "the local border box");
        PaintRectIs(2, 2, 96, 36, local.padding, "the local padding box");
        PaintRectIs(7, 7, 86, 26, local.content, "the local content box");
        RoboCheckEqInt(local.insetsMeasured, 1);
    }
    RoboCheckEqInt(PaintOperationLocalBoxes(NULL, &read, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintOperationLocalBoxes(&a, NULL, NULL),
                      RoboInvalidArgument);
    {
        PaintOperation broken = a;

        broken.boxes.padding.x = 900; /* the layers now cross */
        RoboCheckEqInt(PaintOperationLocalBoxes(&broken, &read, NULL), RoboError);
    }

    /* Every accessor on NULL answers the documented value rather than crashing. */
    RoboCheckEqInt((int)PaintOperationKind(NULL), (int)RoboPaintOpNone);
    RoboCheckEqInt((int)PaintOperationNode(NULL), (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt((int)PaintOperationDepth(NULL), 0);
    RoboCheckEqInt(PaintOperationIsMarker(NULL), 0);
    RoboCheckEqInt(PaintOperationIsDrawing(NULL), 0);
    RoboCheckEqInt((int)PaintOperationColor(NULL).kind, (int)RoboCssColorNone);
    PaintOperationBoxes(NULL, &read);
    RoboCheckEqInt(read.insetsMeasured, 0);
    PaintOperationOrigin(NULL, &origin);
    RoboCheckEqInt(origin.x, 0);
    PaintOperationClip(NULL, &clip);
    RoboCheckEqInt(PaintClipIsActive(&clip), 0);
    PaintOperationBoxes(&a, NULL); /* safe */
    PaintOperationOrigin(&a, NULL);
    PaintOperationClip(&a, NULL);
}

static void TestPaintOperationSideBand(void)
{
    /* A box 100 x 40 with four different borders and 5px of padding all round: the four
     * bands are what a rasteriser will fill, and each one is derived from the two
     * rectangles the record already carries rather than stored a third time. */
    LayoutBorderEdges borders = PaintBorderEdges(5, 3, 7, 2);
    LayoutPaddingEdges paddings = PaintPaddingEdges(5, 5, 5, 5);
    LayoutRect box;
    PaintBoxes boxes;
    LayoutRect band;

    PaintFixture();
    box.x = 0;
    box.y = 0;
    box.width = 100;
    box.height = 40;
    memset(&boxes, 0, sizeof(boxes));
    RoboCheckEqInt(PaintBoxesFromLayers(&box, &borders, &paddings, &boxes, NULL),
                      RoboOk);
    /* padding box: x 2, y 5, w 95, h 28. */
    PaintRectIs(2, 5, 95, 28, boxes.padding, "the padded box of the band fixture");

    {
        PaintOperation top = PaintDrawingMake(
            RoboPaintOpBorderTop, 1, boxes, CssColorMake(0, 0, 0, 255));

        memset(&band, 0, sizeof(band));
        RoboCheckEqInt(PaintOperationSideBand(&top, &band, NULL), RoboOk);
        /* The top band spans the box's full width and stops where the padding begins. */
        PaintRectIs(0, 0, 100, 5, band, "the top band");
    }
    {
        PaintOperation right = PaintDrawingMake(
            RoboPaintOpBorderRight, 1, boxes, CssColorMake(0, 0, 0, 255));

        RoboCheckEqInt(PaintOperationSideBand(&right, &band, NULL), RoboOk);
        /* 2 + 95 = 97, and the band is 3 wide (100 - 97). */
        PaintRectIs(97, 0, 3, 40, band, "the right band");
    }
    {
        PaintOperation bottom = PaintDrawingMake(
            RoboPaintOpBorderBottom, 1, boxes, CssColorMake(0, 0, 0, 255));

        RoboCheckEqInt(PaintOperationSideBand(&bottom, &band, NULL), RoboOk);
        /* 5 + 28 = 33, and the band is 7 tall (40 - 33). */
        PaintRectIs(0, 33, 100, 7, band, "the bottom band");
    }
    {
        PaintOperation left = PaintDrawingMake(
            RoboPaintOpBorderLeft, 1, boxes, CssColorMake(0, 0, 0, 255));

        RoboCheckEqInt(PaintOperationSideBand(&left, &band, NULL), RoboOk);
        PaintRectIs(0, 0, 2, 40, band, "the left band");
    }

    /* A non-border operation has no band, and the refusal is the answer rather than a
     * rectangle that happens to be zero. */
    {
        PaintOperation background = PaintDrawingMake(
            RoboPaintOpBackground, 1, boxes, CssColorMake(0, 0, 0, 255));

        RoboCheckEqInt(PaintOperationSideBand(&background, &band, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(PaintOperationSideBand(NULL, &band, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(PaintOperationSideBand(&background, NULL, NULL),
                          RoboInvalidArgument);
    }

    /*
     * A box whose border already exceeds it: the padding box is squeezed to nothing **at
     * the box's own edge** rather than pushed outside it, so the three layers still nest
     * -- which is what keeps every operation derived from such a box a record instead of
     * failing a whole pass on a document that is merely extreme.
     */
    {
        LayoutBorderEdges thick = PaintBorderEdges(60, 60, 60, 60);
        LayoutPaddingEdges none = PaintPaddingEdges(0, 0, 0, 0);
        PaintBoxes squeezed;
        PaintOperation top;

        memset(&squeezed, 0, sizeof(squeezed));
        RoboCheckEqInt(PaintBoxesFromLayers(&box, &thick, &none, &squeezed, NULL),
                          RoboOk);
        RoboCheckEqInt(squeezed.padding.width, 0);
        RoboCheckEqInt(squeezed.padding.height, 0);
        RoboCheckEqInt(squeezed.content.width, 0);
        RoboCheckEqInt(PaintBoxesIsValid(&squeezed), 1);

        top = PaintDrawingMake(RoboPaintOpBorderTop, 1, squeezed,
                                 CssColorMake(0, 0, 0, 255));
        RoboCheckEqInt(PaintOperationSideBand(&top, &band, NULL), RoboOk);
        /* The band spans the box's whole width, and its height is however much of the box
         * the padding box's origin leaves -- which for an emptied layer is all of it. */
        RoboCheckEqInt(band.width, squeezed.border.width);
        RoboCheckEqInt(band.y, squeezed.border.y);
        RoboCheck(band.height <= squeezed.border.height);
        RoboCheckEqInt(band.height, squeezed.padding.y - squeezed.border.y);
    }
}

static void TestPaintOperationWrite(void)
{
    PaintBoxes boxes = PaintBoxesMake(0, 0, 100, 40, 2, 5);
    PaintOperation background = PaintDrawingMake(
        RoboPaintOpBackground, 3, boxes, CssColorMake(255, 0, 0, 255));
    PaintOperation marker = PaintMarkerMake(3, 1u, boxes);
    char buffer[256];
    size_t length = 0u;

    PaintFixture();
    memset(buffer, 0, sizeof(buffer));

    RoboCheckEqInt(PaintOperationWrite(&background, buffer, sizeof(buffer),
                                                 &length, NULL),
                      RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(buffer, "background") != NULL);
    RoboCheck(strstr(buffer, "node=3") != NULL);
    RoboCheck(strstr(buffer, "depth=0") != NULL);
    RoboCheck(strstr(buffer, "clip=visible/visible") != NULL);
    /* A background acts on the border box, and the line says which rectangle it is
     * rather than leaving a reader to remember. */
    RoboCheck(strstr(buffer, "rect=0,0,100,40") != NULL);
    RoboCheck(strstr(buffer, "color=") != NULL);
    RoboCheckEqInt((int)length, (int)strlen(buffer));

    RoboCheckEqInt(PaintOperationWrite(&marker, buffer, sizeof(buffer), &length,
                                                 NULL),
                      RoboOk);
    RoboCheck(strstr(buffer, "begin-box") != NULL);
    RoboCheck(strstr(buffer, "border=0,0,100,40") != NULL);
    /* A marker has no colour to print, because it has none to draw with. */
    RoboCheck(strstr(buffer, "color=") == NULL);

    {
        PaintOperation border = PaintDrawingMake(
            RoboPaintOpBorderTop, 3, boxes, CssColorMake(0, 0, 0, 255));

        RoboCheckEqInt(PaintOperationWrite(&border, buffer, sizeof(buffer),
                                                     &length, NULL),
                          RoboOk);
        RoboCheck(strstr(buffer, "border-top") != NULL);
        /* A border prints its band, which is the rectangle it fills. */
        RoboCheck(strstr(buffer, "band=0,0,100,2") != NULL);
    }

    /* A buffer too small is refused **with the length it would have needed**, so a
     * caller can size one from the refusal rather than from an estimate. */
    {
        char tiny[8];
        size_t needed = 0u;

        RoboCheckEqInt(PaintOperationWrite(&background, tiny, sizeof(tiny),
                                                     &needed, NULL),
                          RoboNotSupported);
        RoboCheck(needed > sizeof(tiny));
        {
            char exact[512];

            RoboCheck(needed < sizeof(exact));
            RoboCheckEqInt(PaintOperationWrite(&background, exact, needed + 1u,
                                                         &length, NULL),
                              RoboOk);
            RoboCheckEqInt((int)length, (int)needed);
        }
    }

    RoboCheckEqInt(PaintOperationWrite(NULL, buffer, sizeof(buffer), &length,
                                                 NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintOperationWrite(&background, NULL, sizeof(buffer), &length,
                                                 NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintOperationWrite(&background, buffer, sizeof(buffer), NULL,
                                                 NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintOperationWrite(&background, buffer, 0u, &length, NULL),
                      RoboInvalidArgument);
    {
        PaintOperation cleared;

        PaintOperationInit(&cleared);
        RoboCheckEqInt(PaintOperationWrite(&cleared, buffer, sizeof(buffer),
                                                     &length, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* 5. The list                                                         */
/* ------------------------------------------------------------------ */

static void TestPaintListLifecycle(void)
{
    PaintList *list = NULL;
    PaintBoxes boxes = PaintBoxesMake(0, 0, 100, 40, 2, 5);
    PaintOperation marker = PaintMarkerMake(1, 0u, boxes);
    PaintOperation background = PaintDrawingMake(
        RoboPaintOpBackground, 1, boxes, CssColorMake(1, 2, 3, 255));
    PaintOperation read;

    PaintFixture();

    RoboCheckEqInt(PaintListCreate(&list, NULL), RoboOk);
    /* The first block is taken at creation, so a caller that sizes a list before a pass
     * does not have to branch. */
    RoboCheckEqInt((int)PaintListCapacity(list), (int)RoboPaintInitialCapacity);
    RoboCheckEqInt((int)PaintListCount(list), 0);
    RoboCheck(PaintListOperations(list) == NULL);
    RoboCheckEqInt(PaintListCreate(NULL, NULL), RoboInvalidArgument);

    RoboCheckEqInt(PaintListAppend(list, &marker, NULL), RoboOk);
    RoboCheckEqInt(PaintListAppend(list, &background, NULL), RoboOk);
    RoboCheckEqInt((int)PaintListCount(list), 2);
    RoboCheck(PaintListOperations(list) != NULL);

    RoboCheckEqInt(PaintListAt(list, 0u, &read), RoboOk);
    RoboCheckEqInt(PaintOperationEqual(&read, &marker), 1);
    RoboCheckEqInt(PaintListAt(list, 1u, &read), RoboOk);
    RoboCheckEqInt(PaintOperationEqual(&read, &background), 1);
    RoboCheckEqInt(PaintListAt(list, 2u, &read), RoboNotFound);
    RoboCheckEqInt(PaintListAt(list, 0u, NULL), RoboInvalidArgument);
    RoboCheckEqInt(PaintListAt(NULL, 0u, &read), RoboInvalidArgument);

    RoboCheckEqInt((int)PaintListCountOfKind(list, RoboPaintOpBackground), 1);
    RoboCheckEqInt((int)PaintListCountOfKind(list, RoboPaintOpBeginBox), 1);
    RoboCheckEqInt((int)PaintListCountOfKind(list, RoboPaintOpEndBox), 0);
    RoboCheckEqInt((int)PaintListCountOfKind(NULL, RoboPaintOpEndBox), 0);

    /* The append's validation is the boundary that keeps an invalid operation out of a
     * list, so a refused append leaves the list untouched. */
    {
        PaintOperation cleared;
        size_t before = PaintListCount(list);

        PaintOperationInit(&cleared);
        RoboCheckEqInt(PaintListAppend(list, &cleared, NULL), RoboInvalidArgument);
        RoboCheckEqInt((int)PaintListCount(list), (int)before);
        RoboCheckEqInt(PaintListAppend(list, NULL, NULL), RoboInvalidArgument);
        RoboCheckEqInt(PaintListAppend(NULL, &marker, NULL), RoboInvalidArgument);
    }

    /* `clear` drops the operations and keeps the storage -- the concession this phase
     * makes to a per-frame renderer, and not an incremental mechanism. */
    {
        size_t capacityBefore = PaintListCapacity(list);

        PaintListClear(list);
        RoboCheckEqInt((int)PaintListCount(list), 0);
        RoboCheckEqInt((int)PaintListCapacity(list), (int)capacityBefore);
        RoboCheckEqInt(PaintListAt(list, 0u, &read), RoboNotFound);
    }

    /* `reserve` below the capacity is a no-op rather than a shrink, so a caller may size
     * a list before every pass without asking what it already holds. */
    RoboCheckEqInt(PaintListReserve(list, 4u, NULL), RoboOk);
    RoboCheckEqInt((int)PaintListCapacity(list), (int)RoboPaintInitialCapacity);
    RoboCheckEqInt(PaintListReserve(list, 100u, NULL), RoboOk);
    RoboCheck(PaintListCapacity(list) >= 100u);
    RoboCheckEqInt(PaintListReserve(NULL, 4u, NULL), RoboInvalidArgument);

    /* Growth: a hundred operations appended one at a time, every one readable and a
     * record, which is what makes the doubling path worth testing at all. */
    {
        size_t index;

        for (index = 0u; index < 100u; index++) {
            PaintOperation operation = marker;

            operation.depth = index;
            RoboCheckEqInt(PaintListAppend(list, &operation, NULL), RoboOk);
        }
        RoboCheckEqInt((int)PaintListCount(list), 100);
        for (index = 0u; index < 100u; index++) {
            RoboCheckEqInt(PaintListAt(list, index, &read), RoboOk);
            RoboCheckEqInt((int)read.depth, (int)index);
            RoboCheckEqInt(PaintOperationIsValid(&read), 1);
        }
    }

    PaintListDestroy(list);
    PaintListDestroy(NULL); /* safe */
    PaintListClear(NULL);
    RoboCheckEqInt((int)PaintListCount(NULL), 0);
    RoboCheckEqInt((int)PaintListCapacity(NULL), 0);
    RoboCheck(PaintListOperations(NULL) == NULL);
}

static void TestPaintListEquality(void)
{
    PaintBoxes boxes = PaintBoxesMake(0, 0, 100, 40, 2, 5);
    PaintOperation aOp = PaintMarkerMake(1, 0u, boxes);
    PaintOperation bOp = PaintDrawingMake(
        RoboPaintOpBackground, 1, boxes, CssColorMake(9, 9, 9, 255));
    PaintList *a = NULL;
    PaintList *b = NULL;
    PaintList *c = NULL;

    PaintFixture();
    RoboCheckEqInt(PaintListCreate(&a, NULL), RoboOk);
    RoboCheckEqInt(PaintListCreate(&b, NULL), RoboOk);
    RoboCheckEqInt(PaintListCreate(&c, NULL), RoboOk);

    /* Two empty lists are equal, and so are two NULLs -- which is the same statement as
     * "a list holds its own operations". */
    RoboCheckEqInt(PaintListEqual(a, b), 1);
    RoboCheckEqInt(PaintListEqual(NULL, NULL), 1);
    RoboCheckEqInt(PaintListEqual(a, NULL), 0);

    RoboCheckEqInt(PaintListAppend(a, &aOp, NULL), RoboOk);
    RoboCheckEqInt(PaintListAppend(a, &bOp, NULL), RoboOk);
    RoboCheckEqInt(PaintListEqual(a, b), 0);
    RoboCheckEqInt(PaintListAppend(b, &aOp, NULL), RoboOk);
    RoboCheckEqInt(PaintListAppend(b, &bOp, NULL), RoboOk);
    RoboCheckEqInt(PaintListEqual(a, b), 1);

    /* A difference in the *middle*: a comparison that stopped at the counts would say
     * these were equal, and this is the cheap check that catches it. */
    RoboCheckEqInt(PaintListAppend(c, &aOp, NULL), RoboOk);
    {
        PaintOperation different = bOp;

        different.depth = 3u;
        RoboCheckEqInt(PaintListAppend(c, &different, NULL), RoboOk);
    }
    RoboCheckEqInt(PaintListAppend(c, &bOp, NULL), RoboOk);
    RoboCheckEqInt(PaintListEqual(a, c), 0);
    /* And swapping the two operations of one list makes it unequal to itself's order. */
    {
        PaintList *d = NULL;

        RoboCheckEqInt(PaintListCreate(&d, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(d, &bOp, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(d, &aOp, NULL), RoboOk);
        RoboCheckEqInt(PaintListEqual(a, d), 0);
        PaintListDestroy(d);
    }

    PaintListDestroy(a);
    PaintListDestroy(b);
    PaintListDestroy(c);
}

/* ------------------------------------------------------------------ */
/* 6. The dump                                                         */
/* ------------------------------------------------------------------ */

/** Count the newlines in @p text, which is one per line. */
static size_t PaintCountLines(const char *text)
{
    size_t count = 0u;

    while (*text != '\0') {
        if (*text == '\n') {
            count++;
        }
        text++;
    }
    return count;
}

static void TestPaintDumpFormat(void)
{
    PaintBoxes boxes = PaintBoxesMake(0, 0, 100, 40, 2, 5);
    PaintOperation begin = PaintMarkerMake(1, 0u, boxes);
    PaintOperation background = PaintDrawingMake(
        RoboPaintOpBackground, 1, boxes, CssColorMake(255, 0, 0, 255));
    PaintOperation border = PaintDrawingMake(
        RoboPaintOpBorderTop, 1, boxes, CssColorMake(0, 0, 0, 255));
    PaintOperation end = PaintMarkerMake(1, 0u, boxes);
    PaintList *list = NULL;
    char buffer[2048];
    size_t length = 0u;

    PaintFixture();
    memset(buffer, 0, sizeof(buffer));
    begin.kind = RoboPaintOpBeginBox;
    end.kind = RoboPaintOpEndBox;

    RoboCheckEqInt(PaintListCreate(&list, NULL), RoboOk);
    RoboCheckEqInt(PaintListAppend(list, &begin, NULL), RoboOk);
    RoboCheckEqInt(PaintListAppend(list, &background, NULL), RoboOk);
    RoboCheckEqInt(PaintListAppend(list, &border, NULL), RoboOk);
    RoboCheckEqInt(PaintListAppend(list, &end, NULL), RoboOk);

    RoboCheckEqInt(PaintListWrite(list, buffer, sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckEqInt((int)length, (int)strlen(buffer));
    RoboCheckEqInt((int)PaintCountLines(buffer), 4);
    /* The ordinal is zero-padded so that the lines sort the way the list does, and every
     * line is newline-terminated including the last. */
    RoboCheck(strncmp(buffer, "#0000 begin-box", 8) == 0);
    RoboCheck(strstr(buffer, "#0001 ") != NULL);
    RoboCheck(strstr(buffer, "#0002 border-top") != NULL);
    RoboCheck(strstr(buffer, "#0003 end-box") != NULL);
    RoboCheck(buffer[length - 1u] == '\n');

    /* Indentation is the operation's own depth, not a nested print of the tree: the
     * flat sequence is what a rasteriser would see, and a dump that re-derived the
     * structure from the tree would hide exactly the misplacement worth looking for. */
    PaintListDestroy(list);
    list = NULL;

    {
        PaintOperation deep = PaintMarkerMake(1, 2u, boxes);

        RoboCheckEqInt(PaintListCreate(&list, NULL), RoboOk);
        RoboCheckEqInt(PaintListAppend(list, &deep, NULL), RoboOk);
        RoboCheckEqInt(PaintListWrite(list, buffer, sizeof(buffer), &length, NULL),
                          RoboOk);
        /* One space after the ordinal, then two per level of depth: `#0000` + 5 spaces. */
        RoboCheck(strncmp(buffer, "#0000     begin-box", 19) == 0);
        PaintListDestroy(list);
        list = NULL;
    }

    /* An empty list writes an empty dump rather than a placeholder line: a document that
     * draws nothing has no operations to describe. */
    RoboCheckEqInt(PaintListCreate(&list, NULL), RoboOk);
    buffer[0] = 'x';
    RoboCheckEqInt(PaintListWrite(list, buffer, sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckEqInt((int)length, 0);
    RoboCheck(buffer[0] == '\0');
    RoboCheckEqInt(PaintListWrite(NULL, buffer, sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckEqInt((int)length, 0);
    PaintListDestroy(list);
    list = NULL;

    /* The refusal carries the length the dump would have needed, which is what lets a
     * caller size a buffer from the refusal instead of from an estimate. */
    {
        PaintList *big = NULL;
        char tiny[16];
        size_t needed = 0u;
        size_t index;

        RoboCheckEqInt(PaintListCreate(&big, NULL), RoboOk);
        for (index = 0u; index < 12u; index++) {
            RoboCheckEqInt(PaintListAppend(big, &background, NULL), RoboOk);
        }
        RoboCheckEqInt(PaintListWrite(big, tiny, sizeof(tiny), &needed, NULL),
                          RoboNotSupported);
        RoboCheck(needed > sizeof(tiny));
        {
            char exact[4096];

            RoboCheck(needed < sizeof(exact));
            RoboCheckEqInt(PaintListWrite(big, exact, needed + 1u, &length, NULL),
                              RoboOk);
            RoboCheckEqInt((int)length, (int)needed);
            RoboCheckEqInt((int)PaintCountLines(exact), 12);
        }
        PaintListDestroy(big);
    }

    RoboCheckEqInt(PaintListWrite(NULL, NULL, 1u, &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintListWrite(NULL, buffer, sizeof(buffer), NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintListWrite(NULL, buffer, 0u, &length, NULL),
                      RoboInvalidArgument);
}

static void TestPaintDumpLineBound(void)
{
    /*
     * `RoboPaintDumpLineMax` is a *bound*, not a guess: the widest line a dumped
     * operation can need is a border side with every field at its widest printed form,
     * and a suite that asserted the bound would be asserting nothing useful. So this
     * measures the widest one this build can produce and states the bound is wide
     * enough -- which is the check that catches a future field being added to the line
     * without the bound growing with it.
     */
    LayoutBorderEdges borders = PaintBorderEdges(5, 5, 5, 5);
    LayoutPaddingEdges paddings = PaintPaddingEdges(5, 5, 5, 5);
    LayoutRect box;
    PaintBoxes boxes;
    PaintOperation border;
    char line[RoboPaintDumpLineMax + 1u];
    size_t length = 0u;

    PaintFixture();
    box.x = INT32_MIN;
    box.y = INT32_MIN;
    box.width = INT32_MAX;
    box.height = INT32_MAX;
    memset(&boxes, 0, sizeof(boxes));
    RoboCheckEqInt(PaintBoxesFromLayers(&box, &borders, &paddings, &boxes, NULL),
                      RoboOk);
    border = PaintDrawingMake(RoboPaintOpBorderBottom, (LayoutNodeId)999999u,
                                boxes, CssColorMake(255, 255, 255, 255));
    border.depth = 99u;
    border.clip.horizontal.mode = RoboCssOverflowHidden;
    border.clip.horizontal.start = INT32_MIN;
    border.clip.horizontal.end = INT32_MAX;
    border.clip.vertical.mode = RoboCssOverflowHidden;
    border.clip.vertical.start = INT32_MIN;
    border.clip.vertical.end = INT32_MAX;

    memset(line, 0, sizeof(line));
    RoboCheckEqInt(PaintOperationWrite(&border, line, sizeof(line), &length, NULL),
                      RoboOk);
    RoboCheck(length < (size_t)RoboPaintDumpLineMax);
    RoboCheck(length > 0u);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaint(void)
{
    TestRegister("paint.module.lifecycle", TestPaintModuleLifecycle);
    TestRegister("paint.types.kind_vocabulary", TestPaintKindVocabulary);
    TestRegister("paint.types.domain_vocabulary", TestPaintDomainVocabulary);
    TestRegister("paint.diagnostics.record", TestPaintDiagnosticsRecord);
    TestRegister("paint.geometry.boxes_record", TestPaintBoxesRecord);
    TestRegister("paint.geometry.boxes_from_layers", TestPaintBoxesFromLayers);
    TestRegister("paint.geometry.boxes_translate", TestPaintBoxesTranslate);
    TestRegister("paint.geometry.clip_record", TestPaintClipRecord);
    TestRegister("paint.geometry.clip_rect", TestPaintClipRectDoor);
    TestRegister("paint.geometry.clip_intersect", TestPaintClipIntersect);
    TestRegister("paint.operation.record", TestPaintOperationRecord);
    TestRegister("paint.operation.accessors", TestPaintOperationAccessors);
    TestRegister("paint.operation.side_band", TestPaintOperationSideBand);
    TestRegister("paint.operation.write", TestPaintOperationWrite);
    TestRegister("paint.list.lifecycle", TestPaintListLifecycle);
    TestRegister("paint.list.equality", TestPaintListEquality);
    TestRegister("paint.dump.format", TestPaintDumpFormat);
    TestRegister("paint.dump.line_bound", TestPaintDumpLineBound);
}
