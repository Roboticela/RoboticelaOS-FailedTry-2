/*
 * RoboPaint conformance suite (Phase 11.1) — paint.conformance.*
 *
 * The cases that are about the *module* rather than about any one piece of it, and they
 * fall into four kinds:
 *
 *   capabilities   every field of `PaintCapabilities`, read **positionally**
 *                  against a list of names. That is the device RoboLayout's conformance
 *                  suite uses and it is kept for the same reason: `capabilities` is a
 *                  struct, a struct read in declaration order is the one way to assert
 *                  eleven `int` fields without writing eleven accessors, and a field added
 *                  without an assertion in that list is a visible omission rather than a
 *                  silently untested flag. Three of the fields are deliberately 0, and the
 *                  list says which three and why;
 *
 *   the absence    the three-place statement 11.1 makes about drawing -- the constant,
 *                  the door and the capability field -- asserted together, because a
 *                  caller that checked only a version number would conclude this build
 *                  paints;
 *
 *   a document     a document that uses every layer this build paints from, with the
 *                  invariants asked of the whole list: every operation is a record, every
 *                  kind is in the vocabulary, every box's three rectangles nest, and every
 *                  operation's two coordinate spaces agree with the tree. That last one is
 *                  the phase's central claim at the widest scope a suite can ask it;
 *
 *   hostile input  every public door called with NULLs, zero capacities, out-of-range
 *                  indices and undefined enumerators. The contract is uniform -- a
 *                  refusal, a documented default, or a no-op, and never a crash -- and the
 *                  sweep is what makes "uniform" a fact rather than a claim.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends
 * on no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. Capabilities                                                     */
/* ------------------------------------------------------------------ */

/** One capability field: its name, and what this build must report for it. */
typedef struct paintCapabilityRow {
    const char *name;
    int value;
} PaintCapabilityRow;

/*
 * Declaration order, and the order is the assertion: the walk below pairs each name with
 * the field at the same offset, so a field inserted anywhere but the end shows up as every
 * row after it disagreeing -- which is the loudest possible failure and the right one.
 */
static const PaintCapabilityRow paintCapabilityRows[] = {
    { "context", 1 },
    { "operation_model", 1 },
    { "paint_list", 1 },
    { "traversal", 1 },
    { "box_geometry", 1 },
    { "clip_state", 1 },
    { "decoration_ops", 1 },
    /* `drawing` is read from its own constant so the field and its two doors cannot drift.
     * 11.3's `displayLists` and 11.4's `drawing`/`backgroundPainting` joined the 1s, and
     * 11.5's `borderPainting` beside them. */
    { "drawing", RoboPaintDrawingImplemented },
    { "display_lists", 1 },
    /* 11.12's `raster`, the last field in this table to turn and the only one whose flip is about
     * the module **above**: `RoboRender` executes a whole display list into an owned pixel target,
     * and what this module added for it is the bounded walk. The row is still `1` here rather than a
     * reference to `RoboRender`'s table because the two modules' capability sets are asserted
     * separately, by their own suites -- a paint suite that had to build a frame would be a paint
     * suite that needs a renderer to test paint. */
    { "raster", 1 },
    /* 11.11's `paintInvalidation`, the field 11.1 declared and left 0: an owned set of
     * rectangles with a reason, a boundary and a conservative flag each, filled by explicit
     * marks or by comparing two artifacts. As of 11.12 both halves of the pair it belongs to are
     * true: a caller can ask which regions changed *and* re-execute just those. */
    { "paint_invalidation", 1 },
    { "background_painting", 1 },
    { "border_painting", 1 },
    { "text_painting", 1 },
    /* 11.8's `clipping`: a boundary in force is emitted, carried and enforced, which is
     * what the phase's own suite pins pixel by pixel. */
    { "clipping", 1 },
    /* 11.9's `opacity`: a strength in force is accumulated along the path, emitted as a
     * scope, carried on every command and applied to each drawing -- and the phase's own
     * suite pins it at the pixel, including the group-opacity deviation the header states. */
    { "opacity", 1 },
    /* 11.10's `hitTesting`: a point resolves to the node drawn there, in reverse paint order,
     * under the boundary that node's `overflow` puts in force -- and the phase's own suite pins
     * each of the four rules separately, because one overlapping fixture would let three of them
     * be wrong at once. */
    { "hit_testing", 1 },
    /* 11.7's `imagePainting`: a document's `<img>` resource reaches a surface at the rectangle
     * layout gave its element, drawn by the graphics module's own scaled sampler. It sits here
     * rather than beside `backgroundPainting`, where a reader would look for it, because this
     * list is **positional** and a field inserted in the middle renumbers every row after it. */
    { "image_painting", 1 },
    /* 12.16's `glyphRuns`: a text record and the display command it becomes carry the **shaped glyphs** --
     * ids, placements, advances, clusters, source ranges -- plus the token naming the face they came from, so a
     * replay resolves no character to a glyph. `text_painting` above says glyphs reach a surface; this says what
     * travels with a run so that they can, which is the distinction the pair exists for. */
    { "glyph_runs", 1 },
    /* 12.16's `shapedTextRecording`: the shaping happens **once per document**, while it is recorded, and the
     * list a frame executes shapes nothing at all. A build could carry glyphs a replay re-derived (which is
     * what 12.4-12.15's drawing pass did on every frame), so the two rows are separate claims and the phase's
     * own suite pins this one where it is observable: a display list still draws its ink after the record list
     * that produced it is destroyed, and with no face handed to the drawing pass. */
    { "shaped_text_recording", 1 }
};

static void TestPaintConformanceCapabilities(void)
{
    PaintCapabilities capabilities;
    int values[sizeof(PaintCapabilities) / sizeof(int)];
    size_t index;

    PaintFixture();
    /*
     * The walk below is positional, so the *counts* are what make it sound: the struct
     * has exactly one `int` per row and no padding, or a row would be asserting the
     * neighbouring field.
     */
    RoboCheckEqInt((int)(sizeof(PaintCapabilities) / sizeof(int)),
                      (int)(sizeof(paintCapabilityRows) / sizeof(paintCapabilityRows[0])));
    RoboCheckEqInt((int)sizeof(PaintCapabilities),
                      (int)(sizeof(paintCapabilityRows) / sizeof(paintCapabilityRows[0])) *
                          (int)sizeof(int));
    RoboCheckEqInt(PaintGetCapabilities(&capabilities), RoboOk);

    memset(values, 0, sizeof(values));
    memcpy(values, &capabilities, sizeof(capabilities));
    for (index = 0u; index < sizeof(paintCapabilityRows) / sizeof(paintCapabilityRows[0]);
         index++) {
        if (values[index] != paintCapabilityRows[index].value) {
            TestFail(__FILE__, __LINE__, "capability %s is %d, expected %d",
                           paintCapabilityRows[index].name, values[index],
                           paintCapabilityRows[index].value);
        }
        /* Every field is a flag, so a value that is neither 0 nor 1 is a capability that
         * cannot be read as one. */
        RoboCheck(values[index] == 0 || values[index] == 1);
    }

    /* `capabilities_init` writes the "nothing" state rather than a build's list, so a
     * caller can tell "I did not ask" from "I asked and this build does nothing". */
    PaintCapabilitiesInit(&capabilities);
    {
        int cleared[sizeof(PaintCapabilities) / sizeof(int)];
        size_t clearedIndex;

        memset(cleared, 0, sizeof(cleared));
        memcpy(cleared, &capabilities, sizeof(capabilities));
        for (clearedIndex = 0u;
             clearedIndex < sizeof(cleared) / sizeof(cleared[0]); clearedIndex++) {
            RoboCheckEqInt(cleared[clearedIndex], 0);
        }
    }
    PaintCapabilitiesInit(NULL); /* safe */
}

static void TestPaintConformanceDrawingPresent(void)
{
    PaintCapabilities capabilities;
    I32 major = 0;
    I32 minor = 0;
    I32 patch = 0;

    PaintFixture();

    /*
     * The statement 11.4 makes, in one place: the constant is 1, the door answers 1, and
     * the capability field reports it -- and `backgroundPainting`/`borderPainting` beside it
     * say *which* drawings exist, while `raster` (1 since 11.12) says a whole list can be
     * executed -- by `RoboRender`, which is the module this one's walk feeds. A caller deciding
     * whether to look for a surface reads the door; one deciding whether to *make* one reads
     * `drawing`; one deciding whether the whole list renders reads `raster` **and** that module's
     * own table, which is where a rasteriser's harder claims are named as 0.
     */
    RoboCheckEqInt(RoboPaintDrawingImplemented, 1);
    RoboCheckEqInt(PaintDrawingIsImplemented(), 1);
    RoboCheckEqInt(PaintGetCapabilities(&capabilities), RoboOk);
    RoboCheckEqInt(capabilities.drawing, PaintDrawingIsImplemented());
    RoboCheckEqInt(capabilities.backgroundPainting, 1);
    RoboCheckEqInt(capabilities.borderPainting, 1);
    RoboCheckEqInt(capabilities.textPainting, 1);
    RoboCheckEqInt(capabilities.clipping, 1);
    /* The sixth drawing-boundary claim, and the one that is about strength rather than space:
     * an element's `opacity` reaches the raster's own global opacity, per drawing. */
    RoboCheckEqInt(capabilities.opacity, 1);
    /* And the seventh claim, the first here that is not about drawing at all: the module can be
     * asked the **inverse** question, which is what makes the answer's four rules (reverse paint
     * order, the border box as the region, the contents' boundary versus the box's own, and a
     * strength that does not gate) a property of the build rather than of a header. */
    RoboCheckEqInt(capabilities.hitTesting, 1);
    /* And the eighth claim, the fourth that is about a drawing and the second whose payload is a
     * **resource** rather than a number: an image reaches the surface through the graphics layer's
     * own sampler, at the destination layout concluded. */
    RoboCheckEqInt(capabilities.imagePainting, 1);
    /* 12.16's pair, read by name as well as positionally: the shaped run on the record, and the shaping having
     * happened where the document was recorded. */
    RoboCheckEqInt(capabilities.glyphRuns, 1);
    RoboCheckEqInt(capabilities.shapedTextRecording, 1);
    /* And the claim 11.12 turned: a whole display list is executed into pixels by this build --
     * across this module and `RoboRender` together, which is the pair the field is a statement
     * about. */
    RoboCheckEqInt(capabilities.raster, 1);
    /* The drawing claims are independent of it, which is what keeps either side honest: a hit test
     * is not a drawing and a rasteriser is not a drawing kind, so the fields below move with their
     * own phases rather than with `raster`. */
    RoboCheckEqInt(capabilities.backgroundPainting, 1);
    RoboCheckEqInt(capabilities.borderPainting, 1);

    /* The version this build reports is 0.13.0 and its API is not frozen -- two facts a
     * consumer of a release needs, and the second is the one that says a later goal may
     * still add to this surface (12.16 moved text shaping onto the record and changed no
     * meaning in the frozen v1 surface). */
    RoboCheckEqInt(PaintGetVersion(&major, &minor, &patch), RoboOk);
    RoboCheckEqInt((int)major, 0);
    RoboCheckEqInt((int)minor, 13);
    RoboCheckEqInt((int)patch, 0);
    RoboCheckStrEq(PaintVersionString(), RoboPaintVersionString);
    RoboCheckEqInt(RoboPaintApiFrozen, 0);

    /*
     * What the release *does* claim has to be true of the build rather than of a header:
     * the decoration layer really is present, so a list from a document with a background
     * and a border really does contain those operations. The traversal suite proves it in
     * detail; this is the one-line version that keeps the capability field honest.
     */
    {
        PaintFixtureDoc fixture;
        PaintResult result;

        RoboCheckEqInt(capabilities.decorationOps, 1);
        PaintBeginLaidOut(&fixture,
                             "<html><body><div class=\"card\"></div></body></html>",
                             "html, body, div, section, aside, figure, p { display: block }"
                             " .card { width: 10px; height: 10px;"
                             "   background-color: #ff0000; border: 1px solid #000000 }");
        PaintOnce(&fixture, &result);
        RoboCheck(PaintCountOf(&result, RoboPaintOpBackground) > 0u);
        RoboCheck(PaintCountOf(&result, RoboPaintOpBorderTop) > 0u);
        /* And the list is not empty while the drawing is decoration-only: the pass ran, the
         * list carries the operations, and what reaches a surface is the background and the
         * four border bands. */
        RoboCheck(PaintListCount(result.list) > 0u);
        RoboCheckEqInt(PaintDrawingIsImplemented(), 1);
        RoboCheckEqInt(capabilities.backgroundPainting, 1);
        RoboCheckEqInt(capabilities.borderPainting, 1);
        PaintResultDispose(&result);
        PaintEnd(&fixture);
    }
}

static void TestPaintConformanceVocabularyExhaustive(void)
{
    size_t index;

    PaintFixture();

    /*
     * The vocabulary's shape as one statement: as many names as kinds, every kind in the
     * range known and named, and the three predicates agreeing with the enumeration.
     * A phase that added an enumerator without a name row would fail here -- which is the
     * whole point of the table being a table.
     */
    RoboCheckEqInt((int)RoboPaintOpKindCount, 15);
    /* 11.10 appended the hit test's domain, which was the only vocabulary that phase changed:
     * it adds a module of its own beside the traversal rather than an operation kind, because a
     * hit test produces an **answer** and not a drawing. 11.7 then appended its `IMAGE` kind
     * here and its own `imagePainting` capability field, and changed no domain -- an image is a
     * drawing of an existing module rather than a new one. 11.11 appended the invalidation
     * domain, because deciding *which* regions are stale is a module of its own beside the pass
     * that produced the picture. */
    RoboCheckEqInt((int)RoboPaintDomainCount, 13);
    for (index = 0u; index < (size_t)RoboPaintOpKindCount; index++) {
        PaintOpKind kind = (PaintOpKind)index;
        int IsStructural = PaintOpKindIsStructural(kind);
        int isBorder = PaintOpKindIsBorder(kind);
        int isDrawing = PaintOpKindDraws(kind);

        RoboCheckEqInt(PaintOpKindIsKnown(kind), 1);
        RoboCheck(strcmp(PaintOpKindName(kind), "unknown") != 0);
        /* Exactly one of structural/drawing for every kind but `NONE`, which is neither.
         * `CONTENT` is the kind that makes this the right pair to ask: it is structural
         * but not a box marker, so `is_marker + isDrawing` would leave it at 0. */
        if (kind == RoboPaintOpNone) {
            RoboCheckEqInt(IsStructural, 0);
            RoboCheckEqInt(isDrawing, 0);
        } else {
            RoboCheckEqInt(IsStructural + isDrawing, 1);
        }
        /*
         * A drawing kind is exactly the border/background/text/image set, and the marker
         * predicate is the strict subset of structural that excludes the content boundary.
         *
         * The list is spelled out rather than derived, which is the point: a kind added to the
         * vocabulary without a decision about whether it draws fails here. 11.7's `IMAGE` is the
         * fourth entry, and it is the first that draws **without** being a kind whose look is a
         * colour -- which is what `uses_color` below separates from `draws`. */
        RoboCheckEqInt(isDrawing,
                          isBorder || kind == RoboPaintOpBackground ||
                                  kind == RoboPaintOpText || kind == RoboPaintOpImage
                              ? 1
                              : 0);
        /*
         * And the second split, asked here so that a kind that claimed a colour it does not have
         * would fail where the vocabulary is: `uses_color` is a **subset** of `draws` -- the
         * background, the four sides and a text run -- and `IMAGE` is the first drawing kind on
         * the outside of it. A cleared kind uses no colour either, which is why the implication is
         * stated in one direction only.
         */
        if (PaintOpKindUsesColor(kind)) {
            RoboCheckEqInt(isDrawing, 1);
        }
        if (kind == RoboPaintOpImage) {
            RoboCheckEqInt(PaintOpKindUsesColor(kind), 0);
            RoboCheckEqInt(isDrawing, 1);
        }
        if (PaintOpKindIsMarker(kind)) {
            RoboCheckEqInt(IsStructural, 1);
            RoboCheck(kind != RoboPaintOpContent);
        }
        /* A border kind has a side and nothing else does: no kind can be both a border
         * and a marker, and no side is invented for a kind that has none. */
        {
            CssBoxSide side = RoboCssBoxSideTop;
            Result status = PaintOpKindSide(kind, &side);

            RoboCheckEqInt(status, isBorder ? RoboOk : RoboInvalidArgument);
            if (isBorder) {
                RoboCheck(side >= RoboCssBoxSideTop &&
                           side < (CssBoxSide)RoboCssBoxSideCount);
            }
        }
    }
    for (index = 0u; index < (size_t)RoboPaintDomainCount; index++) {
        RoboCheckEqInt(
            PaintDomainIsKnown((PaintDiagnosticDomain)index), 1);
        RoboCheck(strcmp(PaintDomainName((PaintDiagnosticDomain)index),
                          "unknown") != 0);
    }

    /* The three pass outcomes 11.1 defines, and the fourth is the refusal that a caller
     * acts on -- which is why it is an outcome rather than an error. */
    RoboCheckEqInt((int)RoboPaintPassOutcomeCount, 4);
    RoboCheckEqInt(PaintPassOutcomeIsKnown(RoboPaintPassNone), 1);
    RoboCheckEqInt(PaintPassOutcomeIsKnown(RoboPaintPassPainted), 1);
    RoboCheckEqInt(PaintPassOutcomeIsKnown(RoboPaintPassFailed), 1);
    RoboCheckEqInt(PaintPassOutcomeIsKnown(RoboPaintPassStale), 1);
    RoboCheckEqInt(PaintPassOutcomeIsKnown((PaintPassOutcome)99), 0);
    RoboCheckStrEq(PaintPassOutcomeName(RoboPaintPassPainted), "painted");
    RoboCheckStrEq(PaintPassOutcomeName(RoboPaintPassStale), "stale");
    RoboCheckStrEq(PaintPassOutcomeName((PaintPassOutcome)99),
                      "unknown");
}

/* ------------------------------------------------------------------ */
/* 2. A document, and the invariants over the whole list               */
/* ------------------------------------------------------------------ */

static void TestPaintConformanceDocumentList(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    size_t index;

    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\"></section></div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "<p class=\"para\">Text</p>"
                         "</body></html>",
                         "html, body, div, section, aside, figure, p { display: block }"
                         " .card { width: 120px; height: 40px; padding: 4px;"
                         "   border: 1px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px;"
                         "   background-color: #123456 }"
                         " .clip { width: 60px; height: 30px; overflow: hidden }"
                         " .spill { width: 200px; height: 200px;"
                         "   border: 2px solid #ff0000 }");
    PaintOnce(&fixture, &result);

    RoboCheck(PaintListCount(result.list) > 0u);
    for (index = 0u; index < PaintListCount(result.list); index++) {
        PaintOperation operation;
        int kindStructural;

        RoboCheckEqInt(PaintListAt(result.list, index, &operation), RoboOk);

        /* Every operation is a record -- which `PaintListAppend` enforced, so this
         * is the container's promise asked of a whole document rather than of one case. */
        RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
        /* Every kind is in the vocabulary: a list can never carry a value that prints
         * "unknown", because there is no door that would accept one. */
        RoboCheckEqInt(PaintOpKindIsKnown(operation.kind), 1);
        RoboCheck(operation.kind != RoboPaintOpNone);
        /* Every operation names the box a reader verifies it against. */
        RoboCheck(operation.node != RoboLayoutNodeIdNone);
        RoboCheckEqInt(LayoutObjectIsLive(fixture.tree, operation.node), 1);

        /* Structural and drawing agree with the colour rule, in both directions: a
         * structural record carries RoboCSS's own "no colour" and a drawing carries one a
         * rasteriser can use. That is not a tautology -- it is the rule this module's
         * whole validity story rests on, asked once more over a document -- and it is the
         * *structural* predicate rather than the marker one, so a `CONTENT` record is held
         * to the no-colour side rather than asked for a colour it has no use for. */
        kindStructural = PaintOperationIsStructural(&operation);
        if (kindStructural) {
            RoboCheckEqInt((int)PaintOperationColor(&operation).kind,
                              (int)RoboCssColorNone);
        } else {
            RoboCheckEqInt(CssColorIsValid(PaintOperationColor(&operation)),
                              1);
        }

        /* Nested rectangles, and the two coordinate spaces agreeing with the tree -- the
         * widest scope at which the phase's central claim can be asked. */
        RoboCheckEqInt(PaintBoxesIsValid(&operation.boxes), 1);
        PaintLocalBoxesMatchLayout(&fixture, operation);
    }

    /* The marker pairs balance, and the stats agree with the list: the two facts a
     * reader uses to tell a document that painted in full from one half of which was
     * never laid out. */
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBeginBox),
                      (int)PaintCountOf(&result, RoboPaintOpEndBox));
    RoboCheckEqInt((int)result.stats.boxes,
                      (int)PaintCountOf(&result, RoboPaintOpBeginBox));
    /* The content boundary is unconditional, so it is exactly one per placed box. */
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpContent),
                      (int)result.stats.boxes);
    RoboCheckEqInt((int)result.stats.ops, (int)PaintListCount(result.list));
    RoboCheckEqInt((int)result.stats.visited, (int)result.stats.objects);
    RoboCheck((int)result.stats.textRuns >= 1);
    RoboCheck((int)result.stats.clipped >= 1);
    RoboCheck((int)result.stats.backgrounds >= 2);
    RoboCheckEqInt((int)result.stats.passes, 1);

    /* And the dump of that list is a dump: one line per operation, every one of them
     * describing the operation the list holds. A dump that disagreed with the record
     * would be a second implementation of it, so this asks the two agree in count. */
    {
        char buffer[8192];
        size_t length = 0u;
        size_t lines = 0u;
        const char *scan;

        memset(buffer, 0, sizeof(buffer));
        RoboCheckEqInt(PaintListWrite(result.list, buffer, sizeof(buffer),
                                                &length, NULL),
                          RoboOk);
        scan = buffer;
        while (*scan != '\0') {
            if (*scan == '\n') {
                lines++;
            }
            scan++;
        }
        RoboCheckEqInt((int)lines, (int)PaintListCount(result.list));
    }

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Hostile input                                                    */
/* ------------------------------------------------------------------ */

static void TestPaintConformanceHostileInputs(void)
{
    PaintBoxes boxes;
    PaintClip clip;
    LayoutRect rect;
    PaintOperation operation;
    char buffer[16];
    size_t length = 0u;

    PaintFixture();

    /*
     * The sweep: every door reached with the argument that must not crash it. The
     * contract is uniform -- a refusal, a documented default, or a no-op -- and the point
     * of asking all of them in one case is that "uniform" is a fact rather than a claim a
     * reader checks door by door.
     */
    PaintBoxesInit(NULL);
    PaintClipInit(NULL);
    PaintOperationInit(NULL);
    RoboCheckEqInt(PaintBoxesIsValid(NULL), 0);
    RoboCheckEqInt(PaintBoxesEqual(NULL, NULL), 1);
    RoboCheckEqInt(PaintClipIsActive(NULL), 0);
    RoboCheckEqInt(PaintClipIsValid(NULL), 0);
    RoboCheckEqInt(PaintClipAxisIsActive(NULL), 0);
    RoboCheckEqInt(PaintClipAxisIsEmpty(NULL), 0);
    RoboCheckEqInt(PaintOperationIsValid(NULL), 0);
    RoboCheckEqInt(PaintOperationIsMarker(NULL), 0);
    RoboCheckEqInt(PaintOperationIsDrawing(NULL), 0);
    RoboCheckEqInt(PaintOperationEqual(NULL, NULL), 1);
    RoboCheckEqInt(PaintOperationSide(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(PaintOperationSideBand(NULL, &rect, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintOperationWrite(NULL, buffer, sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintBoxesOf(NULL, RoboLayoutNodeIdNone, &boxes, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintBoxesTranslate(NULL, 0, 0, &boxes, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintClipIntersect(NULL, NULL, RoboCssOverflowVisible,
                                                RoboCssOverflowVisible, &clip, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintClipRect(NULL, &rect), RoboInvalidArgument);
    RoboCheckEqInt(PaintListAppend(NULL, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(PaintListAt(NULL, 0u, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)PaintListCount(NULL), 0);
    RoboCheckEqInt((int)PaintListCapacity(NULL), 0);
    RoboCheck(PaintListOperations(NULL) == NULL);
    RoboCheckEqInt(PaintListEqual(NULL, NULL), 1);
    RoboCheckEqInt((int)PaintListCountOfKind(NULL, RoboPaintOpNone), 0);
    PaintListClear(NULL);
    PaintListDestroy(NULL);

    /* An id that names no box is a miss rather than a read of whatever occupies the slot
     * -- and a `NONE` id is an argument error rather than a miss, which is the one
     * distinction every read door in this tree draws the same way. */
    {
        PaintFixtureDoc fixture;
        LayoutNodeId missing;

        PaintBeginLaidOut(&fixture, "<html><body></body></html>",
                             "html, body, div, section, aside, figure, p { display: block }");
        missing = (LayoutNodeId)999999u;
        RoboCheckEqInt(PaintBoxesOf(fixture.tree, missing, &boxes, NULL),
                          RoboNotFound);
        RoboCheckEqInt(PaintBoxesOf(fixture.tree, RoboLayoutNodeIdNone, &boxes,
                                              NULL),
                          RoboInvalidArgument);
        PaintEnd(&fixture);
    }

    /* An operation built the wrong way round is refused by the list rather than stored,
     * which is the boundary the whole "every list is a list of records" claim rests on. */
    {
        PaintList *list = NULL;

        PaintOperationInit(&operation);
        RoboCheckEqInt(PaintListCreate(&list, NULL), RoboOk);
        operation.kind = RoboPaintOpEndBox;
        operation.node = (LayoutNodeId)1u;
        operation.color = CssColorMake(1, 1, 1, 255); /* a marker with a colour */
        RoboCheckEqInt(PaintListAppend(list, &operation, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt((int)PaintListCount(list), 0);
        PaintListDestroy(list);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintConformance(void)
{
    TestRegister("paint.conformance.capabilities",
                       TestPaintConformanceCapabilities);
    TestRegister("paint.conformance.drawing_present",
                       TestPaintConformanceDrawingPresent);
    TestRegister("paint.conformance.vocabulary_exhaustive",
                       TestPaintConformanceVocabularyExhaustive);
    TestRegister("paint.conformance.document_list",
                       TestPaintConformanceDocumentList);
    TestRegister("paint.conformance.hostile_inputs",
                       TestPaintConformanceHostileInputs);
}
