/*
 * Selection mapping — `text.selection.*` (Phase 12.21).
 *
 * ## The claim this suite is arranged around
 *
 * > Selection is expressed in source-text coordinates; glyph geometry is derived from shaped visual output.
 *
 * Two directions are asked of one map, and every case below is one of them or a property of the pair: a
 * **point** becomes a logical position (`RoboTextSelectionMapHitTest`), and a **position** becomes geometry
 * (`…Caret`, `…Geometry`) — never the other way round, and never in terms of a glyph index. The vocabulary
 * cases (`text.selection.positions`) are pure, and they are here because a `RoboTextPosition` that could be
 * confused with a length is the defect the whole phase is written against.
 *
 * ## Where the numbers in this file come from
 *
 * Every expectation is read off the run being queried — its own glyph advances (`conf`'s `hmtx`), its own
 * clusters, its own `ascent`/`descent` — or off the fixture's **builder** (`face.ids.ligaFfi`, which is how the
 * ligature case names the glyph the font's bytes state without writing an id down). A literal pixel value would
 * be a statement about the fixture's metrics; the phase is about the mapping, and a fixture change must not
 * silently invalidate a case.
 *
 * ## What is deliberately absent
 *
 * Editing, caret navigation policy, IME, clipboard, mouse handling and selection *painting* are later rows. So
 * is a `SelectionMap()` accessor on RoboLayout's line boxes: every case here supplies its own placements, which
 * is exactly the contract — the one thing this module cannot read off a run is **where the caller put it**.
 */

#include "RoboTextConformanceFont.h"
#include "RoboTextConformanceSupport.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

#define SelectionSizePx 16.0f

/* The scalars, spelled in UTF-8 so a reader can see which character each case is about. */
#define SelectionA "A"
#define SelectionAB "AB"
#define SelectionFfi "ffi"
#define SelectionAspace "A "
#define SelectionBaseMark "e\xCC\x81"           /* e + U+0301 */
#define SelectionLatinArabic SelectionAB "\xD8\xA8\xD8\xA8"
#define SelectionGreekOmega "\xCE\xA9"          /* U+03A9 */
#define SelectionCyrillicBe "\xD0\x91"          /* U+0411 */
#define SelectionFallbackText SelectionA " " SelectionGreekOmega " " SelectionCyrillicBe

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Open the European conformance fixture: Latin, Greek, Cyrillic, zero-advance marks and ligatures. */
static void SelectionFaceOpen(ConfFace *face)
{
    ConfFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    ConfSpecEuropean(&spec);
    ConfFaceOpen(face, &spec, SelectionSizePx);
}

/**
 * Place every run of @p list on one line, left to right from (100, 200).
 *
 * The caller's array order **is** the visual order, which is the point of the placement contract: a bidi
 * line's runs are listed the way they are laid out, and a case that wanted another order writes another order.
 */
static size_t SelectionPlaceLine(const RoboTextGlyphRunList *list,
                                 RoboTextSelectionPlacement *out,
                                 size_t capacity,
                                 size_t lineIndex,
                                 float originX,
                                 float originY)
{
    size_t count = RoboTextGlyphRunListCount(list);
    float pen = originX;
    size_t index;

    if (count > capacity) {
        count = capacity;
    }
    for (index = 0u; index < count; index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);

        RoboCheckEqInt((int)RoboTextSelectionPlacementForRun(run, index, lineIndex, pen, originY, &out[index],
                                                             NULL),
                       (int)RoboOk);
        pen += RoboTextGlyphRunAdvance(run);
    }
    return count;
}

/** A map over @p list: one line from (100, 200), @p options or the defaults. */
static RoboTextSelectionMap *SelectionMap(const RoboTextGlyphRunList *list,
                                          const RoboTextSelectionOptions *options)
{
    RoboTextSelectionPlacement placements[8];
    RoboTextSelectionMap *map = NULL;
    size_t count = SelectionPlaceLine(list, placements, 8u, 0u, 100.0f, 200.0f);

    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, placements, count, options, &map, NULL), (int)RoboOk);
    RoboCheckMsg(map != NULL, "the map was refused");
    RoboCheckEqInt((int)(RoboTextSelectionMapPlacementCount(map) == count), 1);
    return map;
}

/** The first run of @p result, asserting there is one. */
static const RoboTextGlyphRun *SelectionRun(const RoboTextShapeResult *result)
{
    const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, 0u);

    RoboCheckMsg(run != NULL, "the sample produced no run");
    return run;
}

/** A position as a bare offset. */
static RoboTextPosition SelectionPosition(RoboTextScalarIndex offset)
{
    return RoboTextPositionMake(offset);
}

/** One glyph's advance along @p run's own inline axis — the number every geometry door adds. */
static float SelectionGlyphInline(const RoboTextGlyphRun *run, const RoboTextGlyph *glyph)
{
    return RoboTextGlyphRunIsVertical(run) ? glyph->advanceY : glyph->advanceX;
}

/** The pen's step within @p cluster, along @p run's inline axis: what its own glyphs add up to. */
static float SelectionClusterInline(const RoboTextGlyphRun *run, const RoboTextGlyphCluster *cluster)
{
    float own = 0.0f;
    RoboTextGlyphIndex index;

    for (index = cluster->glyphStart; index < cluster->glyphEnd; index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, index);

        if (glyph) {
            own += SelectionGlyphInline(run, glyph);
        }
    }
    return own;
}

/* ------------------------------------------------------------------ */
/* §2 — positions, affinities and selections                           */
/* ------------------------------------------------------------------ */

static void TestSelectionPositions(void)
{
    RoboTextSelection selection;
    RoboTextRange range;
    char line[96];
    char narrow[4];
    size_t need = 0u;

    /* A position is a value in the source text, the end included: the end of the text is where a caret after
     * the last character lives, and refusing it would make that position unexpressible. */
    RoboCheckEqInt((int)RoboTextPositionMake(3u).offset, 3);
    RoboCheck(RoboTextPositionEqual(SelectionPosition(3u), SelectionPosition(3u)) == 1);
    RoboCheck(RoboTextPositionEqual(SelectionPosition(3u), SelectionPosition(4u)) == 0);
    RoboCheckEqInt(RoboTextPositionIsWithin(SelectionPosition(3u), 3u), 1);
    RoboCheckEqInt(RoboTextPositionIsWithin(SelectionPosition(4u), 3u), 0);
    RoboCheckEqInt(RoboTextPositionIsWithin(SelectionPosition(0u), 0u), 1);

    /* Anchor and focus are two facts: a backwards selection keeps its direction and normalizes on demand. */
    selection = RoboTextSelectionMake(SelectionPosition(7u), SelectionPosition(2u));
    RoboCheckEqInt((int)RoboTextSelectionDirectionOf(selection), (int)RoboTextSelectionDirectionBackward);
    RoboCheckEqInt(RoboTextSelectionIsEmpty(selection), 0);
    range = RoboTextSelectionRange(selection);
    RoboCheckEqInt((int)range.start, 2);
    RoboCheckEqInt((int)range.end, 7);
    RoboCheckEqInt((int)RoboTextSelectionDirectionOf(RoboTextSelectionMake(SelectionPosition(2u),
                                                                          SelectionPosition(7u))),
                   (int)RoboTextSelectionDirectionForward);
    {
        RoboTextSelection collapsed = RoboTextSelectionCollapse(selection);

        RoboCheckEqInt((int)collapsed.anchor.offset, 7);
        RoboCheckEqInt((int)collapsed.focus.offset, 7);
        RoboCheckEqInt(RoboTextSelectionIsEmpty(collapsed), 1);
        RoboCheckEqInt((int)RoboTextSelectionDirectionOf(collapsed), (int)RoboTextSelectionDirectionNone);
    }

    /* Clamping stops a position the text no longer has without reordering: an inverted pair stays inverted. */
    {
        RoboTextSelection clamped = RoboTextSelectionClamp(RoboTextSelectionMake(SelectionPosition(9u),
                                                                                SelectionPosition(1u)),
                                                          5u);

        RoboCheckEqInt((int)clamped.anchor.offset, 5);
        RoboCheckEqInt((int)clamped.focus.offset, 1);
        RoboCheckEqInt((int)RoboTextSelectionRange(clamped).start, 1);
        RoboCheckEqInt((int)RoboTextSelectionRange(clamped).end, 5);
    }

    /* Affinity is two values and an involution; the names are the ones the bidi algorithm uses. */
    RoboCheckEqInt((int)RoboTextAffinityOther(RoboTextAffinityUpstream), (int)RoboTextAffinityDownstream);
    RoboCheckEqInt((int)RoboTextAffinityOther(RoboTextAffinityDownstream), (int)RoboTextAffinityUpstream);
    RoboCheckEqInt(RoboTextAffinityIsKnown(RoboTextAffinityUpstream), 1);
    RoboCheckEqInt(RoboTextAffinityIsKnown((RoboTextAffinity)7), 0);
    RoboCheck(strcmp(RoboTextAffinityName(RoboTextAffinityUpstream), "upstream") == 0);
    RoboCheck(strcmp(RoboTextAffinityName(RoboTextAffinityDownstream), "downstream") == 0);
    RoboCheck(strcmp(RoboTextSelectionDirectionName(RoboTextSelectionDirectionForward), "forward") == 0);
    RoboCheckEqInt(RoboTextSelectionDirectionIsKnown((RoboTextSelectionDirection)9), 0);

    /* The geometry vocabulary: a rectangle covers an area or it does not, and a caret knows which way it
     * stands from the mode alone — the caller never measures the segment to find out. */
    {
        RoboTextSelectionRect rect;
        RoboTextSelectionRect other;
        RoboTextCaretGeometry caret;

        memset(&rect, 0, sizeof(rect));
        rect.x = 10.0f;
        rect.y = 20.0f;
        rect.width = 20.0f;
        rect.height = 20.0f;
        other = rect;
        RoboCheckEqInt(RoboTextSelectionRectIsEmpty(&rect), 0);
        ConfExpectFloatClose(RoboTextSelectionRectWidth(&rect), 20.0f, "selection.positions", "a rect's width");
        ConfExpectFloatClose(RoboTextSelectionRectHeight(&rect), 20.0f, "selection.positions", "a rect's height");
        RoboCheckEqInt(RoboTextSelectionRectEqual(&rect, &other), 1);
        other.height = 0.0f;
        RoboCheckEqInt(RoboTextSelectionRectEqual(&rect, &other), 0);
        RoboCheckEqInt(RoboTextSelectionRectIsEmpty(&other), 1);
        ConfExpectFloatClose(RoboTextSelectionRectWidth(NULL), 0.0f, "selection.positions", "the NULL width");

        RoboTextCaretGeometryInit(&caret);
        RoboCheckEqInt(RoboTextCaretGeometryIsValid(&caret), 0);
        ConfExpectFloatClose(RoboTextCaretGeometrySpanX(&caret), 0.0f, "selection.positions", "a cleared span");
        caret.start = RoboTextSelectionPointMake(5.0f, 6.0f);
        caret.end = RoboTextSelectionPointMake(5.0f, 26.0f);
        RoboCheckEqInt(RoboTextCaretGeometryIsHorizontal(&caret), 0);
        ConfExpectFloatClose(RoboTextCaretGeometrySpanY(&caret), 20.0f, "selection.positions", "the caret's span");
        caret.writingMode = RoboTextWritingVerticalRightToLeft;
        /*
         * The door reads the **segment**, not the mode's name: a horizontal line's caret lies across the text
         * and so its span is along y, while the column's caret spans x. Setting the mode alone changes nothing
         * about the segment, which is the point — the caller draws `start` to `end` either way.
         */
        RoboCheckEqInt(RoboTextCaretGeometryIsHorizontal(&caret), 0);
        caret.end = RoboTextSelectionPointMake(25.0f, 6.0f);
        RoboCheckEqInt(RoboTextCaretGeometryIsHorizontal(&caret), 1);
    }

    /* The vocabulary dumps, complete or absent. */
    RoboCheckEqInt((int)RoboTextPositionWrite(SelectionPosition(3u), 8u, line, sizeof(line), &need), (int)RoboOk);
    RoboCheck(strcmp(line, "3") == 0);
    RoboCheckEqInt((int)RoboTextPositionWrite(SelectionPosition(0u), 8u, line, sizeof(line), &need), (int)RoboOk);
    RoboCheck(strcmp(line, "0 (start)") == 0);
    RoboCheckEqInt((int)RoboTextPositionWrite(SelectionPosition(8u), 8u, line, sizeof(line), &need), (int)RoboOk);
    RoboCheck(strcmp(line, "8 (end)") == 0);
    RoboCheckEqInt((int)RoboTextSelectionWrite(RoboTextSelectionMake(SelectionPosition(2u),
                                                                    SelectionPosition(7u)),
                                               line, sizeof(line), &need),
                   (int)RoboOk);
    RoboCheck(strcmp(line, "2..7 forward") == 0);
    /* A line that does not fit is **absent**, not truncated, and reports what a complete one needs. */
    narrow[0] = 'x';
    /* `0 (start)` needs ten bytes with its terminator, so a four-byte buffer cannot hold it. */
    RoboCheckEqInt((int)RoboTextPositionWrite(SelectionPosition(0u), 8u, narrow, sizeof(narrow), &need),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)narrow[0], 0);
    /* The count is the text's own length; the terminator is the caller's, and the next call's byte count. */
    RoboCheckEqInt((int)need, 9);
    RoboCheckEqInt((int)RoboTextPositionWrite(SelectionPosition(3u), 8u, NULL, 0u, &need), (int)RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* §3 — the map, its refusals and its accessors                        */
/* ------------------------------------------------------------------ */

static void TestSelectionMap(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    const RoboTextGlyphRunList *list;
    RoboTextSelectionPlacement placements[2];
    RoboTextSelectionPlacement broken;
    RoboTextSelectionProblem problem = RoboTextSelectionProblemNone;
    RoboTextSelectionOptions options;
    RoboTextSelectionMap *map = NULL;
    RoboTextGlyphRunList *emptyList = NULL;
    size_t count;

    SelectionFaceOpen(&face);
    result = ConfShapeDefault(&face, SelectionAB);
    list = RoboTextShapeResultGlyphRunList(result);
    count = SelectionPlaceLine(list, placements, 2u, 0u, 100.0f, 200.0f);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)RoboTextGlyphRunListCount(list), 1);

    /*
     * A placement's writing mode is **not** its own: the run was shaped in a mode, so a placement that
     * disagreed would project a point onto an axis the glyphs never moved along. Set it wrong here and watch
     * the constructor correct it.
     */
    placements[0].writingMode = RoboTextWritingVerticalLeftToRight;
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, placements, count, NULL, &map, NULL), (int)RoboOk);
    RoboCheck(map != NULL);
    RoboCheckEqInt((int)RoboTextSelectionMapPlacementCount(map), (int)count);
    RoboCheckEqInt((int)RoboTextSelectionMapLineCount(map), 1);
    RoboCheckEqInt((int)RoboTextSelectionMapSourceRange(map).start, 0);
    RoboCheckEqInt((int)RoboTextSelectionMapSourceRange(map).end, 2);
    RoboCheckEqInt((int)RoboTextSelectionMapPlacementAt(map, 0u)->writingMode,
                   (int)RoboTextGlyphRunWritingMode(RoboTextGlyphRunListRun(list, 0u)));
    /* Readable by index and by run, and both name the same record. */
    RoboCheck(RoboTextSelectionMapPlacementAt(map, 0u) == RoboTextSelectionMapPlacementForRun(map, 0u));
    RoboCheck(RoboTextSelectionMapPlacementAt(map, count) == NULL);
    RoboCheck(RoboTextSelectionMapPlacementForRun(map, count) == NULL);
    /* The options come back by value, and a NULL map answers the defaults. */
    options = RoboTextSelectionMapOptions(map);
    ConfExpectFloatClose(options.caretThickness, 1.0f, "selection.map", "the caret thickness");
    RoboCheckEqInt(options.includeTrailingWhitespace, 1);
    RoboCheckEqInt(options.preferNearestLine, 1);
    (void)RoboTextSelectionMapOptions(NULL);
    RoboCheckEqInt((int)RoboTextSelectionMapPlacementCount(NULL), 0);
    RoboCheckEqInt((int)RoboTextSelectionMapLineCount(NULL), 0);
    RoboCheckEqInt((int)RoboTextSelectionMapSourceRange(NULL).end, 0);
    RoboCheckEqInt((int)RoboTextSelectionMapValidate(map, &problem, NULL), (int)RoboOk);
    RoboCheckEqInt((int)problem, (int)RoboTextSelectionProblemNone);
    RoboCheckEqInt(RoboTextSelectionProblemIsKnown(RoboTextSelectionProblemDuplicate), 1);
    RoboCheckEqInt(RoboTextSelectionProblemIsKnown((RoboTextSelectionProblem)42), 0);
    RoboCheck(strcmp(RoboTextSelectionProblemName(RoboTextSelectionProblemDuplicate), "duplicate") == 0);
    RoboCheck(strcmp(RoboTextSelectionProblemName(RoboTextSelectionProblemNone), "none") == 0);
    RoboTextSelectionMapDestroy(map);

    /*
     * The refusals, one each, because every one of them would otherwise be a silent wrong answer: a subset of
     * the list pretended not to be on the page, a run placed twice with no way to choose between two
     * locations, a placement naming no run, and the two NULL arguments.
     */
    map = NULL;
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, placements, 0u, NULL, &map, NULL),
                   (int)RoboInvalidArgument);
    RoboCheck(map == NULL);
    broken = placements[0];
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, &broken, 2u, NULL, &map, NULL),
                   (int)RoboInvalidArgument);
    broken.runIndex = 9u;
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, &broken, 1u, NULL, &map, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(NULL, placements, 1u, NULL, &map, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, NULL, 1u, NULL, &map, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, placements, count, NULL, NULL, NULL),
                   (int)RoboInvalidArgument);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);

    /*
     * An empty list is a **supported** list: a map over it is built, and every query answers "nothing here"
     * rather than failing — the same shape as an empty run set or an empty analysis.
     */
    RoboCheckEqInt((int)RoboTextGlyphRunListCreate(NULL, 0u, &emptyList, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(emptyList, NULL, 0u, NULL, &map, NULL), (int)RoboOk);
    RoboCheck(map != NULL);
    RoboCheckEqInt((int)RoboTextSelectionMapPlacementCount(map), 0);
    RoboCheckEqInt((int)RoboTextSelectionMapSourceRange(map).end, 0);
    RoboTextSelectionMapDestroy(map);
    RoboTextGlyphRunListDestroy(emptyList);
}

/* ------------------------------------------------------------------ */
/* §4 — hit testing                                                    */
/* ------------------------------------------------------------------ */

static void TestSelectionHit(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;
    RoboTextGlyphRunList *emptyList = NULL;
    RoboTextSelectionMap *map;
    RoboTextSelectionOptions options;
    RoboTextSelectionHit hit;
    float advanceA;
    float total;

    SelectionFaceOpen(&face);
    result = ConfShapeDefault(&face, SelectionAB);
    run = SelectionRun(result);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 2);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(run), 2);
    advanceA = RoboTextGlyphRunGlyph(run, 0u)->advanceX;
    total = RoboTextGlyphRunAdvance(run);
    RoboCheck(total > advanceA);
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), NULL);

    /*
     * A click in the first glyph's **near half** names the position before it, and one in its far half the
     * position after; the affinity says which side of that position the caret hugs, and the distance is signed
     * along the inline axis — so "before every cluster" and "past the last one" are different numbers.
     */
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(100.0f + advanceA * 0.25f,
                                                                                  205.0f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 0);
    RoboCheckEqInt((int)hit.affinity, (int)RoboTextAffinityDownstream);
    RoboCheckEqInt((int)hit.runIndex, 0);
    RoboCheckEqInt((int)hit.lineIndex, 0);
    RoboCheckEqInt((int)hit.clusterStart, 0);
    RoboCheckEqInt(hit.snapped, 0);
    ConfExpectFloatClose(hit.distance, advanceA * 0.25f, "selection.hit", "the distance past the near edge");

    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(100.0f + advanceA * 0.75f,
                                                                                  205.0f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 1);
    RoboCheckEqInt((int)hit.affinity, (int)RoboTextAffinityUpstream);
    ConfExpectFloatClose(hit.distance, advanceA * -0.25f, "selection.hit", "the signed distance before the edge");

    /* Left of the run is the text's start and right of it the text's end, both with a signed distance. */
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(10.0f, 205.0f), &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 0);
    ConfExpectFloatClose(hit.distance, -90.0f, "selection.hit", "the distance before the run");
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(100.0f + total + 50.0f,
                                                                                  205.0f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 2);
    ConfExpectFloatClose(hit.distance, 50.0f, "selection.hit", "the distance past the run");

    /* A point off every line's band resolves to the nearest line, unless the caller asked for a strict region. */
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(150.0f, 900.0f), &hit, NULL),
                   (int)RoboOk);
    RoboTextSelectionOptionsInit(&options);
    options.preferNearestLine = 0;
    RoboTextSelectionMapDestroy(map);
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), &options);
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(150.0f, 900.0f), &hit, NULL),
                   (int)RoboNotFound);
    RoboTextSelectionMapDestroy(map);
    RoboTextShapeResultDestroy(result);

    /*
     * Trailing whitespace. The option is about **scalars** — a space is a space whatever it measures — and the
     * two answers differ by one position, which is the whole of what the flag means.
     */
    result = ConfShapeDefault(&face, SelectionAspace);
    run = SelectionRun(result);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 2);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(run).end, 2);
    RoboTextSelectionOptionsInit(&options);
    options.includeTrailingWhitespace = 0;
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), &options);
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map,
                                                   RoboTextSelectionPointMake(100.0f +
                                                                              RoboTextGlyphRunAdvance(run) + 30.0f,
                                                                              205.0f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 1);
    RoboTextSelectionMapDestroy(map);
    RoboTextSelectionOptionsInit(&options);
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), &options);
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map,
                                                   RoboTextSelectionPointMake(100.0f +
                                                                              RoboTextGlyphRunAdvance(run) + 30.0f,
                                                                              205.0f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 2);
    RoboTextSelectionMapDestroy(map);
    RoboTextShapeResultDestroy(result);

    /* An empty map has nothing to hit; a NULL map or a NULL out is a misuse rather than a miss. */
    RoboCheckEqInt((int)RoboTextGlyphRunListCreate(NULL, 0u, &emptyList, NULL), (int)RoboOk);
    map = SelectionMap(emptyList, NULL);
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(0.0f, 0.0f), &hit, NULL),
                   (int)RoboNotFound);
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(NULL, RoboTextSelectionPointMake(0.0f, 0.0f), &hit, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(0.0f, 0.0f), NULL, NULL),
                   (int)RoboInvalidArgument);
    RoboTextSelectionHitInit(&hit);
    RoboTextSelectionMapDestroy(map);
    RoboTextGlyphRunListDestroy(emptyList);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §5 — the caret, in both writing modes                               */
/* ------------------------------------------------------------------ */

static void TestSelectionCaret(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;
    RoboTextSelectionMap *map;
    RoboTextCaretGeometry caret;
    char line[192];
    size_t need = 0u;
    float advanceA;
    float total;
    float ascent;
    float descent;

    SelectionFaceOpen(&face);
    result = ConfShapeDefault(&face, SelectionAB);
    run = SelectionRun(result);
    advanceA = RoboTextGlyphRunGlyph(run, 0u)->advanceX;
    total = RoboTextGlyphRunAdvance(run);
    ascent = RoboTextGlyphRunAscent(run);
    descent = RoboTextGlyphRunDescent(run);
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), NULL);

    /*
     * At the text's start the caret sits on the run's origin, and it is drawn **across** the block axis — so a
     * horizontal line's caret is vertical, which is the opposite of the mode's own name and the reason
     * `RoboTextCaretGeometryIsHorizontal` exists rather than being inferred by a caller.
     */
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(0u), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(RoboTextCaretGeometryIsValid(&caret), 1);
    ConfExpectFloatClose(caret.start.x, 100.0f, "selection.caret", "the caret's x at the text start");
    ConfExpectFloatClose(caret.end.x, 100.0f, "selection.caret", "both ends share the caret's x");
    ConfExpectFloatClose(RoboTextCaretGeometrySpanX(&caret), 0.0f, "selection.caret", "the inline span");
    /* The band is the run's own ascent and descent, so the caret is exactly as tall as its line. */
    ConfExpectFloatClose(caret.start.y, 200.0f - ascent, "selection.caret", "the caret's top");
    ConfExpectFloatClose(caret.end.y, 200.0f + descent, "selection.caret", "the caret's bottom");
    RoboCheckEqInt(RoboTextCaretGeometryIsHorizontal(&caret), 0);
    RoboCheckEqInt((int)caret.writingMode, (int)RoboTextWritingHorizontal);
    ConfExpectFloatClose(caret.thickness, 1.0f, "selection.caret", "the default thickness");

    /* And it steps by the glyphs' own advances, which is the same sum the painter's pen makes. */
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(1u), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    ConfExpectFloatClose(caret.start.x, 100.0f + advanceA, "selection.caret", "the caret's x at position 1");
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(2u), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    ConfExpectFloatClose(caret.start.x, 100.0f + total, "selection.caret",
                         "the caret at the end of the text");

    /*
     * A position this map cannot place is **not** an error: `valid` is 0 with the spans zero, so a caller has
     * one thing to check rather than five. A NULL argument, on the other hand, is a misuse.
     */
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(9u), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(RoboTextCaretGeometryIsValid(&caret), 0);
    ConfExpectFloatClose(RoboTextCaretGeometrySpanX(&caret), 0.0f, "selection.caret", "an unplaceable span");
    ConfExpectFloatClose(RoboTextCaretGeometrySpanY(&caret), 0.0f, "selection.caret", "an unplaceable span");
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(NULL, SelectionPosition(0u), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(0u), RoboTextAffinityDownstream,
                                                 NULL, NULL),
                   (int)RoboInvalidArgument);

    /* The dump, naming the orientation the geometry has rather than the mode the run was shaped in. */
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(0u), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextCaretGeometryWrite(&caret, line, sizeof(line), &need), (int)RoboOk);
    /* A horizontal line's caret is the one that spans y, so the dump says `vertical` about the segment while
     * naming the run's own mode — `horizontal` would describe the line, not this. */
    RoboCheck(strncmp(line, "vertical (100.00, ", 16u) == 0);
    RoboCheck(strstr(line, ") w=1.00 horizontal-tb valid") != NULL);
    RoboCheckEqInt((int)RoboTextCaretGeometryWrite(NULL, line, sizeof(line), &need), (int)RoboInvalidArgument);

    RoboTextSelectionMapDestroy(map);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);

    /* A vertical column's caret is the same call and the other orientation: the mode decides, not a script. */
    {
        ConfFontSpec spec;
        ConfVerticalGlyphIds ids;
        ConfFace verticalFace;
        RoboTextShapeOptions options;
        RoboTextShapeResult *verticalResult;
        float firstAdvance;
        float verticalAscent;
        float verticalDescent;

        memset(&spec, 0, sizeof(spec));
        ConfSpecVertical(&spec, &ids);
        ConfFaceOpen(&verticalFace, &spec, SelectionSizePx);
        RoboTextShapeOptionsInit(&options);
        options.baseDirection = RoboTextDirectionLtr;
        options.writingMode = RoboTextWritingVerticalRightToLeft;
        verticalResult = ConfShape(&verticalFace, SelectionAB, &options);
        run = SelectionRun(verticalResult);
        RoboCheckEqInt((int)RoboTextGlyphRunIsVertical(run), 1);
        firstAdvance = RoboTextGlyphRunGlyph(run, 0u)->advanceY;
        verticalAscent = RoboTextGlyphRunAscent(run);
        verticalDescent = RoboTextGlyphRunDescent(run);
        map = SelectionMap(RoboTextShapeResultGlyphRunList(verticalResult), NULL);
        RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(1u), RoboTextAffinityDownstream,
                                                     &caret, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)caret.writingMode, (int)RoboTextWritingVerticalRightToLeft);
        ConfExpectFloatClose(RoboTextCaretGeometrySpanY(&caret), 0.0f, "selection.caret", "the column's caret");
        RoboCheck(RoboTextCaretGeometrySpanX(&caret) > 0.0f);
        RoboCheckEqInt(RoboTextCaretGeometryIsHorizontal(&caret), 1);
        /* It stands across the column at the pen's step **down** it, and the block axis of `vertical-rl`
         * points at `-x`, which is why the band's two ends are where they are. */
        ConfExpectFloatClose(caret.start.y, 200.0f + firstAdvance, "selection.caret", "the column caret's y");
        ConfExpectFloatClose(caret.start.x, 100.0f - verticalAscent, "selection.caret", "the column caret's x");
        ConfExpectFloatClose(caret.end.x, 100.0f + verticalDescent, "selection.caret", "the column caret's x");
        RoboTextSelectionMapDestroy(map);
        RoboTextShapeResultDestroy(verticalResult);
        ConfFaceClose(&verticalFace);
    }
}

/* ------------------------------------------------------------------ */
/* §6 — a ligature is one place on the page                            */
/* ------------------------------------------------------------------ */

static void TestSelectionLigature(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;
    RoboTextSelectionMap *map;
    RoboTextSelectionHit hit;
    RoboTextCaretGeometry caret;
    RoboTextSelectionGeometry geometry;
    RoboTextSelectionOptions options;
    float ligatureAdvance;

    SelectionFaceOpen(&face);
    result = ConfShapeDefault(&face, SelectionFfi);
    run = SelectionRun(result);
    /* The `liga` rule folded three scalars into the one glyph the fixture's builder assigned. */
    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 0u)->glyphId, (int)face.ids.ligaFfi);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 0u)->ligatureComponents, 3);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(run).end, 3);
    ligatureAdvance = RoboTextGlyphRunAdvance(run);
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), NULL);

    /*
     * A click inside the ligature names one of its two edges — never a place inside it, because the text has no
     * position there. The near half is the position before it, the far half the position after it, and
     * `snapped` says the click was resolved inside a multi-scalar cluster rather than at a boundary.
     */
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map,
                                                   RoboTextSelectionPointMake(100.0f +
                                                                              ligatureAdvance * 0.25f, 205.0f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 0);
    RoboCheckEqInt((int)hit.affinity, (int)RoboTextAffinityDownstream);
    RoboCheckEqInt(hit.snapped, 1);
    RoboCheckEqInt((int)hit.clusterStart, 0);
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map,
                                                   RoboTextSelectionPointMake(100.0f +
                                                                              ligatureAdvance * 0.75f, 205.0f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 3);
    RoboCheckEqInt((int)hit.affinity, (int)RoboTextAffinityUpstream);
    RoboCheckEqInt(hit.snapped, 1);

    /* A caret asked for a position *inside* the ligature snaps to the edge the affinity names. */
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(1u), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    ConfExpectFloatClose(caret.start.x, 100.0f, "selection.ligature", "a downstream caret inside the ligature");
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(1u), RoboTextAffinityUpstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    ConfExpectFloatClose(caret.start.x, 100.0f + ligatureAdvance, "selection.ligature",
                         "an upstream caret inside the ligature");
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(2u), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    ConfExpectFloatClose(caret.start.x, 100.0f, "selection.ligature", "the ligature is never entered");

    /*
     * A selection of *part* of the ligature is the whole ligature: the range is clipped to the cluster rather
     * than split inside it, so selecting `f` and selecting `ffi` are the same rectangle on the page.
     */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(0u),
                                                                               SelectionPosition(1u)),
                                                   &geometry, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)geometry.count, 1);
    ConfExpectFloatClose(RoboTextSelectionRectWidth(&geometry.rects[0]), ligatureAdvance, "selection.ligature",
                         "the partial selection's width");
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(0u),
                                                                               SelectionPosition(3u)),
                                                   &geometry, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)geometry.count, 1);
    ConfExpectFloatClose(RoboTextSelectionRectWidth(&geometry.rects[0]), ligatureAdvance, "selection.ligature",
                         "the whole selection's width");
    RoboCheckEqInt(RoboTextSelectionGeometryIsEmpty(&geometry), 0);

    /* Nothing selected is nothing drawn, and an empty selection is not an error. */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionAt(SelectionPosition(1u)),
                                                    &geometry, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(RoboTextSelectionGeometryIsEmpty(&geometry), 1);
    RoboCheckEqInt((int)geometry.count, 0);
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(NULL, RoboTextSelectionAt(SelectionPosition(0u)),
                                                    &geometry, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionAt(SelectionPosition(0u)),
                                                    NULL, NULL),
                   (int)RoboInvalidArgument);

    /* The thickness a zero-extent selection is widened to is an option, so the map hands it back. */
    RoboTextSelectionOptionsInit(&options);
    options.caretThickness = 3.0f;
    RoboTextSelectionMapDestroy(map);
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), &options);
    ConfExpectFloatClose(RoboTextSelectionMapOptions(map).caretThickness, 3.0f, "selection.ligature",
                         "the option came back");

    RoboTextSelectionMapDestroy(map);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §7 — a zero-advance mark adds no width                              */
/* ------------------------------------------------------------------ */

static void TestSelectionMarks(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;
    RoboTextSelectionMap *map;
    RoboTextSelectionGeometry geometry;
    RoboTextSelectionHit hit;
    float baseAdvance;
    float total;

    SelectionFaceOpen(&face);
    result = ConfShapeDefault(&face, SelectionBaseMark);
    run = SelectionRun(result);
    /* Base plus mark: two glyphs, and the mark's advance is zero — the fixture states that, not the test. */
    RoboCheck(RoboTextGlyphRunCount(run) >= 2u);
    baseAdvance = RoboTextGlyphRunGlyph(run, 0u)->advanceX;
    total = RoboTextGlyphRunAdvance(run);
    ConfExpectFloatClose(total, baseAdvance, "selection.marks", "the mark adds no width");
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), NULL);

    /*
     * A selection of the base **and** the mark is one place on the page, not a rectangle per zero-advance
     * glyph: the covered clusters coalesce into one segment per run, which is the phase's §12.21.10 stated as a
     * count.
     */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(0u),
                                                                               SelectionPosition(2u)),
                                                   &geometry, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)geometry.count, 1);
    ConfExpectFloatClose(RoboTextSelectionRectWidth(&geometry.rects[0]), baseAdvance, "selection.marks",
                         "the base-and-mark selection's width");

    /* Selecting only the mark is *visible* — the caret's width beside a zero-advance glyph — never invisible. */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(1u),
                                                                               SelectionPosition(2u)),
                                                   &geometry, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)geometry.count, 1);
    RoboCheck(RoboTextSelectionRectWidth(&geometry.rects[0]) > 0.0f);
    RoboCheck(RoboTextSelectionRectWidth(&geometry.rects[0]) <= total + ConfPositionTolerance);

    /* And a click on the mark resolves to a position in the cluster it belongs to, never past the run. */
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(100.0f + baseAdvance * 0.5f,
                                                                                  205.0f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(RoboTextPositionIsWithin(hit.position, RoboTextSelectionMapSourceRange(map).end), 1);

    RoboTextSelectionMapDestroy(map);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §8 — an RTL run, drawn both ways                                    */
/* ------------------------------------------------------------------ */

static void TestSelectionRtl(void)
{
    ConfFontSpec spec;
    ConfVerticalGlyphIds ids;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *rtl;
    const RoboTextGlyphRunList *list;
    RoboTextSelectionPlacement placements[4];
    RoboTextSelectionMap *map = NULL;
    RoboTextCaretGeometry caret;
    RoboTextSelectionHit hit;
    const RoboTextGlyphCluster *firstCluster;
    float latinAdvance;
    float total;
    float firstOwn;
    size_t count;

    memset(&spec, 0, sizeof(spec));
    ConfSpecVertical(&spec, &ids);
    ConfFaceOpen(&face, &spec, SelectionSizePx);
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    result = ConfShape(&face, SelectionLatinArabic, &options);
    /* Latin, then Arabic: two runs, and the second one is classified right-to-left. */
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 2);
    rtl = RoboTextShapeResultGlyphRun(result, 1u);
    RoboCheckEqInt((int)RoboTextGlyphRunDirection(rtl), (int)RoboTextDirectionRtl);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(rtl).start, 2);
    RoboCheckEqInt((int)RoboTextGlyphRunSourceRange(rtl).end, 4);
    list = RoboTextShapeResultGlyphRunList(result);
    count = SelectionPlaceLine(list, placements, 4u, 0u, 100.0f, 200.0f);
    RoboCheckEqInt((int)count, 2);
    latinAdvance = RoboTextGlyphRunAdvance(RoboTextGlyphRunListRun(list, 0u));
    total = RoboTextGlyphRunAdvance(rtl);
    firstCluster = RoboTextGlyphRunCluster(rtl, 0u);
    RoboCheck(firstCluster != NULL);
    firstOwn = SelectionClusterInline(rtl, firstCluster);

    /*
     * Drawn **forwards** — which is what this build's painter does for every run, RTL included — the run's
     * first logical cluster is at the run's own origin, so the caret at its start is one pen-width in.
     */
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, placements, count, NULL, &map, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(2u), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)caret.writingMode, (int)RoboTextWritingHorizontal);
    ConfExpectFloatClose(caret.start.x, 100.0f + latinAdvance, "selection.rtl", "the RTL start, drawn forwards");
    RoboTextSelectionMapDestroy(map);

    /*
     * Drawn **backwards** — the bit a caller sets the day 12.8's visual order reaches the painter — the same
     * logical position is at the run's *other* end and the same click resolves to the logical position at the
     * *other* cluster's edge. This is the case that proves the mapping never assumes glyph order is source
     * order, and `logical source position` is what it returns in both.
     */
    placements[1].flags |= RoboTextSelectionPlacementReversed;
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, placements, count, NULL, &map, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(2u), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    ConfExpectFloatClose(caret.start.x, 100.0f + latinAdvance + (total - firstOwn), "selection.rtl",
                         "the RTL start, drawn backwards");
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map,
                                                   RoboTextSelectionPointMake(100.0f + latinAdvance +
                                                                              total * 0.2f,
                                                                              205.0f),
                                                   &hit, NULL),
                   (int)RoboOk);
    /* The near half of the visually first cluster (the logically **last** one, drawn backwards) is its
     * source **end**: the click is at the run's far edge in the text and near one on the page. */
    RoboCheckEqInt((int)hit.position.offset, 4);
    RoboCheckEqInt((int)hit.affinity, (int)RoboTextAffinityUpstream);
    RoboCheckEqInt(RoboTextPositionIsWithin(hit.position, RoboTextSelectionMapSourceRange(map).end), 1);
    /* And a click before the run's own box still lands in the text rather than past either end. */
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(100.0f, 205.0f), &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(RoboTextPositionIsWithin(hit.position, RoboTextSelectionMapSourceRange(map).end), 1);

    RoboTextSelectionMapDestroy(map);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §9 — a selection across runs, and the two affinities at a boundary  */
/* ------------------------------------------------------------------ */

static void TestSelectionBidi(void)
{
    ConfFontSpec spec;
    ConfVerticalGlyphIds ids;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;
    const RoboTextGlyphRunList *list;
    RoboTextSelectionPlacement placements[4];
    RoboTextSelectionMap *map = NULL;
    RoboTextSelectionGeometry geometry;
    RoboTextCaretGeometry first;
    RoboTextCaretGeometry second;
    RoboTextScalarIndex boundary;
    size_t count;
    size_t index;
    float pen;

    memset(&spec, 0, sizeof(spec));
    ConfSpecVertical(&spec, &ids);
    ConfFaceOpen(&face, &spec, SelectionSizePx);
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    result = ConfShape(&face, SelectionLatinArabic, &options);
    list = RoboTextShapeResultGlyphRunList(result);
    count = SelectionPlaceLine(list, placements, 4u, 0u, 100.0f, 200.0f);
    RoboCheckEqInt((int)count, (int)RoboTextGlyphRunListCount(list));
    RoboCheck(count >= 2u);
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, placements, count, NULL, &map, NULL), (int)RoboOk);

    /*
     * A logical range that spans the line is **one rectangle per run**, in the order the caller laid them out
     * — never one box around the lot, which is the phase's §12.21.28 stated as a comparison rather than a
     * comment. `bounds` exists beside them as a convenience and is explicitly not the answer.
     */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(0u),
                                                                               SelectionPosition(RoboTextSelectionMapSourceRange(map).end)),
                                                   &geometry, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)geometry.count, (int)count);
    pen = 100.0f;
    for (index = 0u; index < count; index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);

        ConfExpectFloatClose(geometry.rects[index].x, pen, "selection.bidi", "a rectangle at its run's origin");
        ConfExpectFloatClose(RoboTextSelectionRectWidth(&geometry.rects[index]),
                             RoboTextGlyphRunAdvance(run), "selection.bidi", "a rectangle as wide as its run");
        pen += RoboTextGlyphRunAdvance(run);
    }
    ConfExpectFloatClose(geometry.bounds.x, 100.0f, "selection.bidi", "the bounds' left edge");
    ConfExpectFloatClose(geometry.bounds.width, pen - 100.0f, "selection.bidi", "the bounds' width");

    /* A range inside one run produces exactly one rectangle: the logical range decides, not the line. */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(0u),
                                                                               SelectionPosition(1u)),
                                                   &geometry, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)geometry.count, 1);
    ConfExpectFloatClose(geometry.rects[0].x, 100.0f, "selection.bidi", "the first run's rectangle");

    /*
     * The affinity at a run boundary with both runs on one line: there is **one** location and both affinities
     * answer it, which is a fact worth asserting rather than assuming.
     */
    boundary = RoboTextGlyphRunSourceRange(RoboTextGlyphRunListRun(list, 0u)).end;
    RoboCheckEqInt((int)boundary, 2);
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(boundary), RoboTextAffinityUpstream,
                                                 &first, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(boundary), RoboTextAffinityDownstream,
                                                 &second, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(RoboTextCaretGeometryIsValid(&first), 1);
    RoboCheckEqInt(RoboTextCaretGeometryIsValid(&second), 1);
    ConfExpectFloatClose(first.start.x, second.start.x, "selection.bidi", "one line, one location");
    ConfExpectFloatClose(first.start.y, second.start.y, "selection.bidi", "one line, one location");
    RoboTextSelectionMapDestroy(map);

    /* The same text, wrapped: the second run begins at the next line's origin, so there are two locations and
     * the affinity is the only thing that tells them apart. */
    placements[1].lineIndex = 1u;
    placements[1].origin = RoboTextSelectionPointMake(100.0f, 240.0f);
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, placements, count, NULL, &map, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextSelectionMapLineCount(map), 2);
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(boundary), RoboTextAffinityUpstream,
                                                 &first, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(boundary), RoboTextAffinityDownstream,
                                                 &second, NULL),
                   (int)RoboOk);
    RoboCheckMsg(first.start.y != second.start.y, "the two affinities answered the same line");
    ConfExpectFloatClose(second.start.y - first.start.y, 40.0f, "selection.bidi",
                         "the two affinities are one line apart");
    ConfExpectFloatClose(first.start.x, 100.0f + RoboTextGlyphRunAdvance(RoboTextGlyphRunListRun(list, 0u)),
                         "selection.bidi", "the upstream caret at the first run's end");
    ConfExpectFloatClose(second.start.x, 100.0f, "selection.bidi", "the downstream caret at the second's start");
    /* And a wrapped selection has one rectangle per line, in line order. */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(0u),
                                                                               SelectionPosition(RoboTextSelectionMapSourceRange(map).end)),
                                                   &geometry, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)geometry.count, (int)count);
    RoboCheck(geometry.rects[0].y != geometry.rects[1].y);

    RoboTextSelectionMapDestroy(map);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §10 — vertical: the same calls on the other axis                    */
/* ------------------------------------------------------------------ */

static void TestSelectionVertical(void)
{
    ConfFontSpec spec;
    ConfVerticalGlyphIds ids;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;
    RoboTextSelectionMap *map;
    RoboTextSelectionHit hit;
    RoboTextCaretGeometry caret;
    RoboTextSelectionGeometry geometry;
    float firstAdvance;
    float total;

    memset(&spec, 0, sizeof(spec));
    ConfSpecVertical(&spec, &ids);
    ConfFaceOpen(&face, &spec, SelectionSizePx);
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    options.writingMode = RoboTextWritingVerticalRightToLeft;
    result = ConfShape(&face, SelectionAB, &options);
    run = SelectionRun(result);
    RoboCheckEqInt((int)RoboTextGlyphRunIsVertical(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunAdvance(run), 0);
    firstAdvance = RoboTextGlyphRunGlyph(run, 0u)->advanceY;
    total = RoboTextGlyphRunAdvanceY(run);
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), NULL);

    /*
     * The inline axis is `+y`, so a point is chosen by its **y** and by nothing else: the same probe on x is
     * the line's band. A build that had kept an x-based hit test would answer about the wrong axis here rather
     * than fail to compile.
     */
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(100.0f,
                                                                                  200.0f + firstAdvance * 0.25f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 0);
    RoboCheckEqInt((int)hit.affinity, (int)RoboTextAffinityDownstream);
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(100.0f,
                                                                                  200.0f + firstAdvance * 0.75f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 1);
    RoboCheckEqInt((int)hit.affinity, (int)RoboTextAffinityUpstream);
    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(100.0f, 200.0f + total + 40.0f),
                                                   &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)hit.position.offset, 2);

    /* The caret is horizontal and steps down the column by the run's own `advanceY`. */
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(1u), RoboTextAffinityDownstream, &caret,
                                                 NULL),
                   (int)RoboOk);
    RoboCheckEqInt(RoboTextCaretGeometryIsHorizontal(&caret), 1);
    ConfExpectFloatClose(RoboTextCaretGeometrySpanY(&caret), 0.0f, "selection.vertical", "the caret's y span");
    RoboCheck(RoboTextCaretGeometrySpanX(&caret) > 0.0f);
    ConfExpectFloatClose(caret.start.y, 200.0f + firstAdvance, "selection.vertical", "the caret's y");

    /* And a selection is a **column** rectangle: tall, not wide, with nothing assumed about which is which. */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(0u),
                                                                               SelectionPosition(2u)),
                                                   &geometry, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)geometry.count, 1);
    /*
     * The rectangle is *(inline × block)* and each mode puts those on a different axis: down the column the
     * inline extent is the run's own `advanceY`, and across it the block extent is the line's band. The
     * horizontal case below states the same two quantities on the swapped axes — which is the claim, not an
     * inequality that only holds when the letters happen to be wide enough.
     */
    ConfExpectFloatClose(RoboTextSelectionRectHeight(&geometry.rects[0]), total, "selection.vertical",
                         "the selection's inline extent is the run's advanceY");
    ConfExpectFloatClose(RoboTextSelectionRectWidth(&geometry.rects[0]),
                         RoboTextGlyphRunAscent(run) + RoboTextGlyphRunDescent(run), "selection.vertical",
                         "the selection's block extent is the line's band");
    /* The same range horizontally is the other shape, so the pair of cases is the swap and not a coincidence. */
    RoboTextSelectionMapDestroy(map);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);

    {
        ConfFace horizontalFace;
        RoboTextShapeResult *horizontalResult;
        const RoboTextGlyphRun *horizontalRun;

        SelectionFaceOpen(&horizontalFace);
        horizontalResult = ConfShapeDefault(&horizontalFace, SelectionAB);
        horizontalRun = RoboTextGlyphRunListRun(RoboTextShapeResultGlyphRunList(horizontalResult), 0u);
        map = SelectionMap(RoboTextShapeResultGlyphRunList(horizontalResult), NULL);
        RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(0u),
                                                                                   SelectionPosition(2u)),
                                                       &geometry, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)geometry.count, 1);
        ConfExpectFloatClose(RoboTextSelectionRectHeight(&geometry.rects[0]),
                             RoboTextGlyphRunAscent(horizontalRun) + RoboTextGlyphRunDescent(horizontalRun),
                             "selection.vertical", "a horizontal line's block extent is the band");
        ConfExpectFloatClose(RoboTextSelectionRectWidth(&geometry.rects[0]),
                             RoboTextGlyphRunAdvance(horizontalRun), "selection.vertical",
                             "a horizontal line's inline extent is the run's advanceX");
        RoboTextSelectionMapDestroy(map);
        RoboTextShapeResultDestroy(horizontalResult);
        ConfFaceClose(&horizontalFace);
    }
}

/* ------------------------------------------------------------------ */
/* §11 — a font boundary is not a logical boundary                     */
/* ------------------------------------------------------------------ */

/** The pair, opened together: the primary, the chain, and a selector over both. */
typedef struct SelectionPair {
    ConfFontSpec primarySpec;
    ConfFontSpec secondarySpec;
    ConfFace primary;
    ConfFace secondary;
    RoboTextFontCandidate candidate;
    RoboTextFontFallback chain;
    RoboTextFontSelector *selector;
} SelectionPair;

/** Build and open both faces and a selector with the primary's context and the secondary in the chain. */
static void SelectionPairOpen(SelectionPair *pair)
{
    memset(pair, 0, sizeof(*pair));
    ConfSpecFallbackPrimary(&pair->primarySpec);
    ConfSpecFallbackSecondary(&pair->secondarySpec);
    ConfFaceOpen(&pair->primary, &pair->primarySpec, SelectionSizePx);
    ConfFaceOpen(&pair->secondary, &pair->secondarySpec, SelectionSizePx);
    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&pair->selector, NULL), (int)RoboOk);
    RoboCheck(pair->selector != NULL);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(pair->selector, &pair->primary.font, NULL), (int)RoboOk);
    pair->candidate.family = "Conformance Secondary";
    pair->candidate.face = pair->secondary.face;
    pair->chain.entries = &pair->candidate;
    pair->chain.count = 1u;
    RoboCheckEqInt((int)RoboTextFontSelectorSetFallback(pair->selector, &pair->chain, NULL), (int)RoboOk);
}

/** Release the pair. The selector first: it borrows both faces. */
static void SelectionPairClose(SelectionPair *pair)
{
    RoboTextFontSelectorDestroy(pair->selector);
    pair->selector = NULL;
    ConfFaceClose(&pair->secondary);
    ConfFaceClose(&pair->primary);
}

/**
 * `A Ω Б`, shaped run by run through 12.5's own bridge.
 *
 * Not `RoboTextShape`, which takes **one** face: a fallback boundary is a run boundary, and the runs must come
 * out of two faces for this case to mean anything. `RoboTextAnalysis` selects a face per script run; the
 * request it emits is the one the pipeline consumes, which is the route a layout's font request takes.
 */
static void TestSelectionFallback(void)
{
    SelectionPair pair;
    RoboTextView view = ConfView(SelectionFallbackText);
    RoboTextShapeOptions options;
    RoboTextAnalysis *analysis = NULL;
    RoboTextFontStyle style;
    RoboTextLanguage language;
    RoboTextGlyphRun **runs;
    RoboTextGlyphRunList *list = NULL;
    RoboTextSelectionPlacement placements[4];
    RoboTextSelectionMap *map = NULL;
    RoboTextSelectionGeometry geometry;
    RoboTextCaretGeometry caret;
    RoboTextSelectionHit hit;
    RoboTextScalarIndex boundary;
    size_t count = 0u;
    size_t analysisRuns;
    size_t index;
    float pen = 100.0f;
    float probe;

    SelectionPairOpen(&pair);
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    RoboTextFontStyleInit(&style);
    style.sizePx = SelectionSizePx;
    memset(&language, 0, sizeof(language));
    /*
     * The run array is **allocated**: `RoboTextGlyphRunListCreate` adopts both the runs and the array it was
     * handed — `Destroy` frees each run and then the array — so a stack array here would be freed by the list
     * and a caller's buffer would be the thing a later allocation tripped over.
     */
    runs = (RoboTextGlyphRun **)RoboCalloc(4u, sizeof(*runs));
    RoboCheckMsg(runs != NULL, "the run array could not be allocated");
    RoboCheckEqInt((int)RoboTextAnalysisCreate(view, pair.selector, &options, &analysis, NULL), (int)RoboOk);
    analysisRuns = RoboTextAnalysisRunCount(analysis);
    RoboCheck(analysisRuns >= 2u);
    for (index = 0u; index < analysisRuns && count < 4u; index++) {
        const RoboTextAnalysisRun *analysisRun = RoboTextAnalysisRunAt(analysis, index);
        RoboTextShapeRequest request;
        RoboTextRun textRun;
        RoboTextGlyphRun *glyphs = NULL;

        RoboCheck(analysisRun != NULL);
        RoboTextShapeRequestInit(&request);
        RoboCheckEqInt((int)RoboTextShapeRequestFromRun(analysisRun, view, &style, &language,
                                                        RoboTextDirectionLtr, &options, &request),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextShapeRequestToRun(&request, &textRun), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, &textRun, &glyphs, NULL), (int)RoboOk);
        RoboCheck(glyphs != NULL);
        runs[count] = glyphs;
        count++;
    }
    RoboCheck(count >= 2u);
    /*
     * The two runs came out of **different faces**, which is what makes this a fallback case rather than a
     * two-run case: the boundary the fonts put in is invisible to a caller, and the next assertion is that the
     * map keeps it that way.
     */
    RoboCheck(RoboTextGlyphRunFont(runs[0]) != NULL && RoboTextGlyphRunFont(runs[1]) != NULL);
    RoboCheckMsg(RoboTextGlyphRunFont(runs[0])->face != RoboTextGlyphRunFont(runs[1])->face,
                 "the two runs were not answered by two faces");

    RoboCheckEqInt((int)RoboTextGlyphRunListCreate(runs, count, &list, NULL), (int)RoboOk);
    for (index = 0u; index < count; index++) {
        RoboCheckEqInt((int)RoboTextSelectionPlacementForRun(RoboTextGlyphRunListRun(list, index), index, 0u, pen,
                                                             200.0f, &placements[index], NULL),
                       (int)RoboOk);
        pen += RoboTextGlyphRunAdvance(RoboTextGlyphRunListRun(list, index));
    }
    RoboCheckEqInt((int)RoboTextSelectionMapCreate(list, placements, count, NULL, &map, NULL), (int)RoboOk);

    /* Every run is one rectangle and the positions stay logical; the boundary is only a run boundary. */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(0u),
                                                                               SelectionPosition(RoboTextSelectionMapSourceRange(map).end)),
                                                   &geometry, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)geometry.count, (int)count);
    boundary = RoboTextGlyphRunSourceRange(RoboTextGlyphRunListRun(list, 0u)).end;
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(boundary), RoboTextAffinityUpstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    ConfExpectFloatClose(caret.start.x, 100.0f + RoboTextGlyphRunAdvance(RoboTextGlyphRunListRun(list, 0u)),
                         "selection.fallback", "the upstream caret at the font boundary");
    RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(boundary), RoboTextAffinityDownstream,
                                                 &caret, NULL),
                   (int)RoboOk);
    ConfExpectFloatClose(caret.start.x, 100.0f + RoboTextGlyphRunAdvance(RoboTextGlyphRunListRun(list, 0u)),
                         "selection.fallback", "the downstream caret at the font boundary");

    /* Every position the map answers is a position in the text, whichever face drew it. */
    for (probe = 0.0f; probe < 400.0f; probe += 3.0f) {
        if (RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(100.0f + probe, 205.0f), &hit,
                                        NULL) == RoboOk) {
            RoboCheckEqInt(RoboTextPositionIsWithin(hit.position, RoboTextSelectionMapSourceRange(map).end), 1);
        }
    }

    RoboTextSelectionMapDestroy(map);
    RoboTextGlyphRunListDestroy(list);
    RoboTextAnalysisDestroy(analysis);
    SelectionPairClose(&pair);
}

/* ------------------------------------------------------------------ */
/* §12 — the round trip                                                */
/* ------------------------------------------------------------------ */

static void TestSelectionRoundTrip(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;
    RoboTextSelectionMap *map;
    RoboTextCaretGeometry caret;
    RoboTextSelectionHit hit;
    RoboTextSelectionGeometry first;
    RoboTextSelectionGeometry second;
    RoboTextSelection selection = RoboTextSelectionMake(SelectionPosition(1u), SelectionPosition(5u));
    size_t cluster;
    size_t clusters;
    size_t index;

    SelectionFaceOpen(&face);
    result = ConfShapeDefault(&face, "ABAV office");
    run = SelectionRun(result);
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), NULL);
    clusters = RoboTextGlyphRunClusterCount(run);
    RoboCheck(clusters > 0u);
    RoboCheck(RoboTextGlyphRunAdvance(run) > 0.0f);

    /*
     * Every **cluster boundary** survives the trip: position → caret → hit → position. The walk is over
     * clusters rather than over scalars because the boundaries are the only positions a caret may occupy, so a
     * round trip over an offset inside a ligature would be asking for something the text cannot express.
     */
    for (cluster = 0u; cluster < clusters; cluster++) {
        const RoboTextGlyphCluster *entry = RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)cluster);
        RoboTextSelectionPoint where;

        RoboCheck(entry != NULL);
        RoboCheckEqInt((int)RoboTextSelectionMapCaret(map, SelectionPosition(entry->source.start),
                                                      RoboTextAffinityDownstream, &caret, NULL),
                       (int)RoboOk);
        RoboCheckEqInt(RoboTextCaretGeometryIsValid(&caret), 1);
        where = RoboTextSelectionPointMake((caret.start.x + caret.end.x) * 0.5f,
                                           (caret.start.y + caret.end.y) * 0.5f);
        RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, where, &hit, NULL), (int)RoboOk);
        /* The answer is a position in the text — never a glyph index, never past the end. */
        RoboCheckEqInt(RoboTextPositionIsWithin(hit.position, RoboTextSelectionMapSourceRange(map).end), 1);
        RoboCheckEqInt((int)(hit.runIndex == 0u), 1);
    }

    /* The same selection twice is the same rectangles, bit for bit. */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, selection, &first, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, selection, &second, NULL), (int)RoboOk);
    RoboCheckEqInt((int)first.count, (int)second.count);
    RoboCheck(first.count > 0u);
    for (index = 0u; index < first.count; index++) {
        RoboCheckEqInt(RoboTextSelectionRectEqual(&first.rects[index], &second.rects[index]), 1);
    }
    /* And a backwards drag produces the same rectangles as a forwards one, because the range is normalized. */
    RoboCheckEqInt((int)RoboTextSelectionMapGeometry(map, RoboTextSelectionMake(SelectionPosition(5u),
                                                                               SelectionPosition(1u)),
                                                   &second, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)second.count, (int)first.count);
    for (index = 0u; index < first.count; index++) {
        RoboCheckEqInt(RoboTextSelectionRectEqual(&first.rects[index], &second.rects[index]), 1);
    }

    RoboTextSelectionMapDestroy(map);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §13 — the dump                                                      */
/* ------------------------------------------------------------------ */

static void TestSelectionDiagnostics(void)
{
    ConfFace face;
    RoboTextShapeResult *result;
    RoboTextSelectionMap *map;
    RoboTextSelectionHit hit;
    char block[4096];
    size_t need = 0u;

    SelectionFaceOpen(&face);
    result = ConfShapeDefault(&face, SelectionFfi);
    map = SelectionMap(RoboTextShapeResultGlyphRunList(result), NULL);

    RoboCheckEqInt((int)RoboTextSelectionMapWrite(map, block, sizeof(block), &need), (int)RoboOk);
    RoboCheck(strncmp(block, "selection map: 1 runs, 1 lines, 3 scalars", 41u) == 0);
    RoboCheck(strstr(block, "\n  run 0 line 0 src 0..3") != NULL);
    RoboCheck(strstr(block, "forward origin (100.00, 200.00)") != NULL);
    /* The one thing the dump exists for: a reader can see `source → glyphs` per cluster, which is the mapping
     * the whole phase is about. */
    RoboCheck(strstr(block, "cluster 0..3 glyphs 0..1") != NULL);
    /* Complete or absent: a buffer too small leaves it empty and reports what a complete one would need. */
    {
        size_t small = 0u;

        block[0] = 'x';
        RoboCheckEqInt((int)RoboTextSelectionMapWrite(map, block, 8u, &small), (int)RoboNotSupported);
        RoboCheckEqInt((int)block[0], 0);
        RoboCheck((int)(small > 8u));
    }
    RoboCheckEqInt((int)RoboTextSelectionMapWrite(NULL, block, sizeof(block), &need), (int)RoboInvalidArgument);

    RoboCheckEqInt((int)RoboTextSelectionMapHitTest(map, RoboTextSelectionPointMake(100.0f, 205.0f), &hit, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextSelectionHitWrite(&hit, block, sizeof(block), &need), (int)RoboOk);
    RoboCheck(strncmp(block, "position 0 downstream (run 0, line 0, cluster 0, snapped", 52u) == 0);
    RoboCheckEqInt((int)RoboTextSelectionHitWrite(NULL, block, sizeof(block), &need), (int)RoboInvalidArgument);

    RoboTextSelectionMapDestroy(map);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §14 — the fuzz entry                                                */
/* ------------------------------------------------------------------ */

static void TestSelectionFuzz(void)
{
    static const uint8_t junk[64] = {
        0x41u, 0x42u, 0x00u, 0xFFu, 0x80u, 0xE0u, 0x30u, 0x0Au, 0xE4u, 0xB8u, 0x80u, 0xD8u, 0xA8u, 0xCCu, 0x81u,
        0x07u, 0x10u, 0x1Fu, 0x00u, 0x41u, 0x41u, 0x56u, 0x2Eu, 0x41u, 0x56u, 0x41u, 0x00u, 0x20u, 0x20u, 0x20u,
        0x20u, 0x78u, 0x00u, 0x7Fu, 0xFEu, 0x80u, 0xBFu, 0x3Fu, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u,
        0x08u, 0x09u, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u, 0x18u,
        0x19u, 0x1Au, 0x1Bu, 0x1Cu
    };
    uint8_t pattern[32];
    size_t index;

    /* The contract, stated where a reader looks for it: NULL with a size is refused, and so is two NULLs. */
    RoboCheckEqInt((int)RoboTextSelectionFuzz(NULL, 8u), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextSelectionFuzz(junk, sizeof(junk)), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextSelectionFuzz(NULL, 0u), (int)RoboOk);

    /* Every prefix, so a byte count that lands mid-scalar is exercised too. */
    for (index = 0u; index <= sizeof(junk); index++) {
        RoboCheckEqInt((int)RoboTextSelectionFuzz(junk, index), (int)RoboOk);
    }
    /* And a deterministic sweep of configurations, because the first bytes steer the options and the mode. */
    for (index = 0u; index < sizeof(pattern); index++) {
        pattern[index] = (uint8_t)(index * 7u + 3u);
    }
    for (index = 0u; index < 8u; index++) {
        RoboCheckEqInt((int)RoboTextSelectionFuzz(pattern + index, sizeof(pattern) - index), (int)RoboOk);
    }
}

void TestRegisterTextSelection(void)
{
    TestRegister("text.selection.positions", TestSelectionPositions);
    TestRegister("text.selection.map", TestSelectionMap);
    TestRegister("text.selection.hit", TestSelectionHit);
    TestRegister("text.selection.caret", TestSelectionCaret);
    TestRegister("text.selection.ligature", TestSelectionLigature);
    TestRegister("text.selection.marks", TestSelectionMarks);
    TestRegister("text.selection.rtl", TestSelectionRtl);
    TestRegister("text.selection.bidi", TestSelectionBidi);
    TestRegister("text.selection.vertical", TestSelectionVertical);
    TestRegister("text.selection.fallback", TestSelectionFallback);
    TestRegister("text.selection.roundtrip", TestSelectionRoundTrip);
    TestRegister("text.selection.diagnostics", TestSelectionDiagnostics);
    TestRegister("text.selection.fuzz", TestSelectionFuzz);
}
