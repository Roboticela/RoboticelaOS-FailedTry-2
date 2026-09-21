/*
 * RoboPaint invalidation suite (Phase 11.11) — paint.invalidation.*
 *
 * The release that answers the question the ten before it raised: given a picture this module
 * already produced, which of its pixels are no longer the pixels the document describes? Every
 * case here is built from **real artifacts** — two documents through the real chain (HTML → CSS
 * → layout → records → display list), or two states of one, or the records of one and a mark
 * naming a box in it — because the thing under test is a *claim about artifacts*, and a
 * hand-built list would prove the walk reads a struct rather than the walk reads a document.
 *
 * The claims the release makes, and where each is pinned:
 *
 *   **a region is sufficient, never exact**            `stale_pixels` paints two frames into two
 *                                                     real surfaces and asserts that **every
 *                                                     pixel that differs** lies inside some
 *                                                     region of the set — the property a partial
 *                                                     repaint is allowed to depend on, checked
 *                                                     against pixels rather than against the
 *                                                     records that produced them;
 *
 *   **and sufficient is enough to repaint**            `partial_repaint_matches_full` is the
 *                                                     capstone: clear the regions of the first
 *                                                     frame, paint the second frame into it, and
 *                                                     compare the whole surface against a fresh
 *                                                     render of the second frame. Identical — or
 *                                                     the invalidation missed something;
 *
 *   **geometry is two rectangles, not one**            `geometry_move` and `geometry_resize` pin
 *                                                     that the old rectangle is in the set as
 *                                                     well as the new, which is the difference
 *                                                     between a repaint and a smear;
 *
 *   **the boundary is read, never invented**           `narrowed` intersects against a boundary
 *                                                     that names both axes; `conservative` keeps
 *                                                     the whole rectangle for an axis with no
 *                                                     extent; `zero_area_and_invalid` pins that
 *                                                     a boundary which excludes everything
 *                                                     produces **no** region rather than an
 *                                                     empty one;
 *
 *   **a no-op is not a repaint**                       `no_op` compares a document with itself
 *                                                     and `unconsumed_properties` a property no
 *                                                     layer consumes: nothing owed, scope NONE;
 *
 *   **the set is a snapshot**                          `mutation_isolation_and_determinism`
 *                                                     releases both artifacts and rebuilds the
 *                                                     transition independently; the set outlives
 *                                                     them and two runs are equal;
 *
 *   **11.1's hook is wired**                           `from_stats` turns `styleOnly` into a
 *                                                     conservative whole-extent region -- and
 *                                                     into *nothing* when the count is zero.
 *
 * Everything is deterministic, and the dump cases assert it by text: the module writes no
 * address, and `determinism` compares two independently built sets **and their dumps**.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on no
 * Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboCSS/RoboCSSOverflow.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboPaint/RoboPaintDisplayListBuilder.h"
#include "RoboPaint/RoboPaintInvalidation.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/*
 * One box, one sheet, two states. The geometry is declared rather than intrinsic (this build has
 * no intrinsic sizing and no UA sheet), so a case can move, resize or recolour the box by
 * swapping the sheet and nothing else.
 */
#define INVAL_DOC \
    "<html><body><div class=\"box\"></div></body></html>"

#define INVAL_BASE \
    "html, body, div, p { display: block } " \
    "html, body { background-color: #ffffff } "

#define INVAL_BOX_BLUE \
    ".box { width: 60px; height: 40px; margin: 20px; background-color: #3366cc }"

/** The page's own colour: what a partial repaint clears an invalidated region to. */
#define INVAL_PAGE GfxRgba8(0xff, 0xff, 0xff, 255)

/** The surface every pixel case renders into. */
#define INVAL_SURFACE_W 200u
#define INVAL_SURFACE_H 160u

static LayoutRect InvalRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    LayoutRect rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    return rect;
}

static PaintInvalidationSet *InvalSet(void)
{
    PaintInvalidationSet *set = NULL;

    RoboCheckEqInt(PaintInvalidationSetCreate(&set, NULL), RoboOk);
    RoboCheck(set != NULL);
    return set;
}

static void InvalDestroy(PaintInvalidationSet *set)
{
    PaintInvalidationSetDestroy(set);
}

/** A boundary that names both axes: the one case this module may narrow against. */
static PaintClip InvalClipBoth(int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    PaintClip clip;

    PaintClipInit(&clip);
    clip.horizontal.mode = RoboCssOverflowHidden;
    clip.horizontal.start = x0;
    clip.horizontal.end = x1;
    clip.vertical.mode = RoboCssOverflowHidden;
    clip.vertical.start = y0;
    clip.vertical.end = y1;
    return clip;
}

/** A boundary that hides one axis and leaves the other free. */
static PaintClip InvalClipXOnly(int32_t x0, int32_t x1)
{
    PaintClip clip;

    PaintClipInit(&clip);
    clip.horizontal.mode = RoboCssOverflowHidden;
    clip.horizontal.start = x0;
    clip.horizontal.end = x1;
    return clip;
}

/** Assert one region's four fields and both flags. */
static void InvalRegionIs(const PaintInvalidationRegion *region,
                            LayoutRect rect,
                            PaintInvalidationReason reason,
                            int narrowed,
                            int full,
                            const char *what)
{
    PaintRectEq(region->rect, rect, what);
    if (region->reason != reason) {
        TestFail(__FILE__, __LINE__, "%s: expected reason %s, got %s", what,
                       PaintInvalidationReasonName(reason),
                       PaintInvalidationReasonName(region->reason));
    }
    if (region->narrowed != narrowed || region->full != full) {
        TestFail(__FILE__, __LINE__, "%s: narrowed=%d full=%d, expected %d/%d", what,
                       region->narrowed, region->full, narrowed, full);
    }
}

static PaintInvalidationRegion InvalAt(const PaintInvalidationSet *set,
                                                 size_t index)
{
    PaintInvalidationRegion region;

    PaintInvalidationRegionInit(&region);
    RoboCheckEqInt(PaintInvalidationSetAt(set, index, &region, NULL), RoboOk);
    return region;
}

/** Non-zero when @p rect contains the point. */
static int InvalRectContains(LayoutRect rect, int32_t x, int32_t y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/** Non-zero when **some** region of @p set contains the point. */
static int InvalSetContains(const PaintInvalidationSet *set, int32_t x, int32_t y)
{
    size_t index;

    for (index = 0u; index < PaintInvalidationSetCount(set); index++) {
        if (InvalRectContains(InvalAt(set, index).rect, x, y)) {
            return 1;
        }
    }
    return 0;
}

/**
 * The **paint-space** border box of @p node, read from its background record.
 *
 * The record carries both coordinate spaces (11.1) and the two are not interchangeable:
 * `PaintBoxesOf` answers in layout's, everything in the paint list is in page space, and a
 * case that compared one against the other would be asserting about two different rectangles.
 * So every geometry expectation in this suite is read out of the artifact, in the space the
 * artifact is in.
 *
 * @return 1 when @p node has a background record and @p out was filled; 0 otherwise.
 */
static int InvalBoxRect(const PaintResult *result,
                          LayoutNodeId node,
                          LayoutRect *out)
{
    size_t index;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            return 0;
        }
        if (operation.kind == RoboPaintOpBackground && operation.node == node) {
            *out = operation.boxes.border;
            return 1;
        }
    }
    return 0;
}

/** The `BEGIN_BOX` record of @p node in @p result, when the list has one. */
static int InvalBoxStart(const PaintResult *result,
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

/** Non-zero when some region of @p set has exactly @p rect and @p node. */
static int InvalSetHas(const PaintInvalidationSet *set,
                         LayoutRect rect,
                         LayoutNodeId node)
{
    size_t index;

    for (index = 0u; index < PaintInvalidationSetCount(set); index++) {
        PaintInvalidationRegion region = InvalAt(set, index);

        if (region.node == node && region.rect.x == rect.x && region.rect.y == rect.y &&
            region.rect.width == rect.width && region.rect.height == rect.height) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Two states of one document                                          */
/* ------------------------------------------------------------------ */

typedef struct invalPair {
    PaintFixtureDoc before;
    PaintFixtureDoc after;
    PaintResult beforeResult;
    PaintResult afterResult;
} InvalPair;

/**
 * Build @p html twice: once with @p cssBefore and once with @p cssAfter, laid out and painted
 * through the real chain.
 *
 * Two independent trees rather than a mutation of one, which is what makes a comparison honest:
 * both sides are documents, so a region the walk attributes to a node is a node both trees
 * really have.
 */
static void InvalPairBegin(InvalPair *pair,
                             const char *html,
                             const char *cssBefore,
                             const char *cssAfter)
{
    PaintBeginLaidOut(&pair->before, html, cssBefore);
    PaintOnce(&pair->before, &pair->beforeResult);
    PaintBeginLaidOut(&pair->after, html, cssAfter);
    PaintOnce(&pair->after, &pair->afterResult);
}

static void InvalPairEnd(InvalPair *pair)
{
    PaintResultDispose(&pair->beforeResult);
    PaintResultDispose(&pair->afterResult);
    PaintEnd(&pair->after);
    PaintEnd(&pair->before);
}

/* ------------------------------------------------------------------ */
/* Surfaces                                                            */
/* ------------------------------------------------------------------ */

static GfxSurface *InvalSurface(GfxColorRgba8 page)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = INVAL_SURFACE_W;
    desc.height = INVAL_SURFACE_H;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    RoboCheck(surface != NULL);
    /* Every frame starts as the page, which is also what a partial repaint clears to. */
    RoboCheckEqInt(GfxSurfaceClearRgba8(surface, page), RoboOk);
    return surface;
}

/** Build the display list for @p records and paint it into @p target. */
static void InvalRender(const PaintList *records, GfxSurface *target)
{
    PaintDisplayList *display = NULL;
    PaintDisplayListBuilder *builder = NULL;

    RoboCheckEqInt(PaintDisplayListCreate(&display, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    RoboCheckEqInt(
        PaintDisplayListBuilderBuild(builder, records, display, NULL, NULL), RoboOk);
    /* No face: the documents these cases render have no text, so nothing needs one. */
    RoboCheckEqInt(PaintDecorationPaint(display, target, NULL, NULL, NULL), RoboOk);
    PaintDisplayListBuilderDestroy(builder);
    PaintDisplayListDestroy(display);
}

/** Clear every region of @p set to @p page -- the partial repaint's first half. */
static void InvalClearRegions(const PaintInvalidationSet *set,
                                GfxSurface *target,
                                GfxColorRgba8 page)
{
    GfxRaster *raster = NULL;
    size_t index;

    RoboCheckEqInt(GfxRasterCreate(target, &raster), RoboOk);
    for (index = 0u; index < PaintInvalidationSetCount(set); index++) {
        PaintInvalidationRegion region = InvalAt(set, index);

        RoboCheckEqInt(GfxRasterFillRect(raster, region.rect, page), RoboOk);
    }
    GfxRasterDestroy(raster);
}

static GfxColorRgba8 InvalPixel(GfxSurface *surface, int32_t x, int32_t y)
{
    GfxColorRgba8 color;

    memset(&color, 0, sizeof(color));
    RoboCheckEqInt(GfxSurfaceGetPixel(surface, (uint32_t)x, (uint32_t)y, &color),
                      RoboOk);
    return color;
}

static int InvalSamePixel(GfxColorRgba8 a, GfxColorRgba8 b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/**
 * The count of points where @p a and @p b differ, and the number of those points **outside**
 * every region of @p set.
 *
 * The two numbers are the phase's whole claim: the first is what a full repaint would have to
 * fix, the second is what a partial repaint would leave wrong.
 */
static void InvalCompareFrames(GfxSurface *a,
                                 GfxSurface *b,
                                 const PaintInvalidationSet *set,
                                 size_t *outDiffering,
                                 size_t *outUncovered)
{
    size_t differing = 0u;
    size_t uncovered = 0u;
    int32_t x;
    int32_t y;

    for (y = 0; y < (int32_t)INVAL_SURFACE_H; y++) {
        for (x = 0; x < (int32_t)INVAL_SURFACE_W; x++) {
            if (InvalSamePixel(InvalPixel(a, x, y), InvalPixel(b, x, y))) {
                continue;
            }
            differing++;
            if (!InvalSetContains(set, x, y)) {
                uncovered++;
            }
        }
    }
    *outDiffering = differing;
    *outUncovered = uncovered;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestInvalidationReasonVocabulary(void)
{
    size_t index;
    size_t duplicates = 0u;

    RoboCheckEqInt((int)RoboPaintInvalidationReasonCount, 11);
    for (index = 0u; index < (size_t)RoboPaintInvalidationReasonCount; index++) {
        PaintInvalidationReason reason = (PaintInvalidationReason)index;
        size_t other;

        RoboCheckEqInt(PaintInvalidationReasonIsKnown(reason), 1);
        RoboCheck(strcmp(PaintInvalidationReasonName(reason), "unknown") != 0);
        for (other = 0u; other < index; other++) {
            if (strcmp(PaintInvalidationReasonName(reason),
                       PaintInvalidationReasonName(
                           (PaintInvalidationReason)other)) == 0) {
                duplicates++;
            }
        }
    }
    RoboCheckEqInt((int)duplicates, 0);
    /* `NONE` is the cleared record's value rather than a reason a producer may hand in. */
    RoboCheckEqInt(PaintInvalidationReasonIsKnown(
                          (PaintInvalidationReason)RoboPaintInvalidationReasonCount),
                      0);
    RoboCheck(strcmp(PaintInvalidationReasonName(
                          (PaintInvalidationReason)99),
                      "unknown") == 0);
    /* Every reason the doors below produce is named here, which is what makes the vocabulary a
     * statement about the code rather than a list of hopes. */
    RoboCheck(strcmp(PaintInvalidationReasonName(
                          RoboPaintInvalidationReasonStyle),
                      "style") == 0);
    RoboCheck(strcmp(PaintInvalidationReasonName(
                          RoboPaintInvalidationReasonViewport),
                      "viewport") == 0);
}

static void TestInvalidationClassAndScopeVocabulary(void)
{
    size_t index;

    RoboCheckEqInt((int)RoboPaintInvalidationClassCount, 3);
    RoboCheckEqInt((int)RoboPaintInvalidationScopeCount, 3);
    for (index = 0u; index < (size_t)RoboPaintInvalidationClassCount; index++) {
        RoboCheckEqInt(
            PaintInvalidationClassIsKnown((PaintInvalidationClass)index), 1);
        RoboCheck(strcmp(PaintInvalidationClassName(
                              (PaintInvalidationClass)index),
                          "unknown") != 0);
    }
    for (index = 0u; index < (size_t)RoboPaintInvalidationScopeCount; index++) {
        RoboCheckEqInt(
            PaintInvalidationScopeIsKnown((PaintInvalidationScope)index), 1);
        RoboCheck(strcmp(PaintInvalidationScopeName(
                              (PaintInvalidationScope)index),
                          "unknown") != 0);
    }
    RoboCheck(strcmp(PaintInvalidationClassName(
                          RoboPaintInvalidationClassLayout),
                      "layout") == 0);
    RoboCheck(strcmp(PaintInvalidationScopeName(
                          RoboPaintInvalidationScopePartial),
                      "partial") == 0);
}

static void TestInvalidationClassifyProperty(void)
{
    /*
     * What layout measures is a geometry change whatever it paints: a repaint of a box that moved
     * is only sound once a layout pass has run, so the class says so and the caller owes both.
     */
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("width"),
        (int)RoboPaintInvalidationClassLayout);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("margin-left"),
        (int)RoboPaintInvalidationClassLayout);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("font-size"),
        (int)RoboPaintInvalidationClassLayout);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("display"),
        (int)RoboPaintInvalidationClassLayout);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("overflow"),
        (int)RoboPaintInvalidationClassLayout);

    /* What this module draws from: a repaint of a region, geometry untouched. */
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("background-color"),
        (int)RoboPaintInvalidationClassPaint);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("background"),
        (int)RoboPaintInvalidationClassPaint);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("border-top-color"),
        (int)RoboPaintInvalidationClassPaint);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("color"),
        (int)RoboPaintInvalidationClassPaint);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("opacity"),
        (int)RoboPaintInvalidationClassPaint);

    /* A spelling from a document, not from a table. */
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("BACKGROUND-COLOR"),
        (int)RoboPaintInvalidationClassPaint);

    /* Nothing this build draws: no pixel changes, so nothing is owed. */
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("cursor"),
        (int)RoboPaintInvalidationClassNone);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("not-a-property"),
        (int)RoboPaintInvalidationClassNone);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty(NULL),
        (int)RoboPaintInvalidationClassNone);
    /* `border-style` is layout's, because a style of `none` collapses the used width. */
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("border-style"),
        (int)RoboPaintInvalidationClassLayout);
}

static void TestInvalidationReasonForProperty(void)
{
    RoboCheckEqInt(
        PaintInvalidationReasonForProperty("background-color"),
        (int)RoboPaintInvalidationReasonBackground);
    RoboCheckEqInt(PaintInvalidationReasonForProperty("border-color"),
                      (int)RoboPaintInvalidationReasonBorder);
    RoboCheckEqInt(PaintInvalidationReasonForProperty("color"),
                      (int)RoboPaintInvalidationReasonText);
    RoboCheckEqInt(PaintInvalidationReasonForProperty("opacity"),
                      (int)RoboPaintInvalidationReasonOpacity);
    /* A measured property repaints because a coordinate moved, so the reason names the geometry
     * and the caller reads its own cascade for the declaration. */
    RoboCheckEqInt(PaintInvalidationReasonForProperty("width"),
                      (int)RoboPaintInvalidationReasonGeometry);
    RoboCheckEqInt(PaintInvalidationReasonForProperty("visibility"),
                      (int)RoboPaintInvalidationReasonNone);
    RoboCheckEqInt(PaintInvalidationReasonForProperty("unknown-thing"),
                      (int)RoboPaintInvalidationReasonNone);
}

/**
 * The three properties layout calls PAINT and **nobody draws**.
 *
 * This is the phase's one veto, and it is a veto of a *repaint* rather than of a row in a table:
 * `visibility` is registered in RoboCSS and consumed by no layer in this runtime, no background
 * image is ever painted, and `text-decoration` is drawn by nobody -- so a change to any of them
 * produces an **identical picture**, and a system that asked for a repaint would be asking for
 * work that cannot change a pixel. The document-level half of the case is the proof: two sheets
 * that differ only in `visibility` produce **equal** record lists, so the comparison between them
 * is a comparison between two current pictures.
 */
static void TestInvalidationUnconsumedProperties(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();

    InvalPairBegin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE INVAL_BOX_BLUE " .box { visibility: hidden }");

    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("visibility"),
        (int)RoboPaintInvalidationClassNone);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("text-decoration"),
        (int)RoboPaintInvalidationClassNone);
    RoboCheckEqInt(
        PaintInvalidationClassifyProperty("background-image"),
        (int)RoboPaintInvalidationClassNone);

    /* The picture really is the same one, which is what makes `NONE` the sound answer rather than
     * a decision to look away. */
    RoboCheck(PaintListEqual(pair.beforeResult.list, pair.afterResult.list));

    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 0);
    RoboCheckEqInt(PaintInvalidationSetScope(set),
                      (int)RoboPaintInvalidationScopeNone);

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

/* ------------------------------------------------------------------ */
/* The set                                                             */
/* ------------------------------------------------------------------ */

static void TestInvalidationEmptySet(void)
{
    PaintInvalidationSet *set = InvalSet();
    PaintInvalidationStats stats;
    LayoutRect bounds = InvalRect(0, 0, 0, 0);

    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 0);
    RoboCheck(PaintInvalidationSetCapacity(set) >=
               RoboPaintInvalidationInitialCapacity);
    RoboCheckEqInt(PaintInvalidationSetIsEmpty(set), 1);
    RoboCheckEqInt(PaintInvalidationSetScope(set),
                      (int)RoboPaintInvalidationScopeNone);
    RoboCheckEqInt(PaintInvalidationSetBounds(set, &bounds), RoboNotFound);
    /* Nothing owed, and no regions produced: the record of an empty set is all zeros. */
    memset(&stats, 0xff, sizeof(stats));
    PaintInvalidationSetStats(set, &stats);
    RoboCheckEqInt((int)stats.added, 0);
    RoboCheckEqInt((int)stats.excluded, 0);
    RoboCheckEqInt((int)stats.compared, 0);

    /* The NULL-tolerant half of every read, which is what a caller with no set can rely on. */
    RoboCheckEqInt((int)PaintInvalidationSetCount(NULL), 0);
    RoboCheckEqInt(PaintInvalidationSetIsEmpty(NULL), 1);
    RoboCheckEqInt(PaintInvalidationSetScope(NULL),
                      (int)RoboPaintInvalidationScopeNone);
    RoboCheckEqInt(PaintInvalidationSetBounds(NULL, &bounds), RoboInvalidArgument);

    PaintInvalidationSetClear(set);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 0);
    InvalDestroy(set);
    InvalDestroy(NULL);
}

static void TestInvalidationAddRect(void)
{
    PaintInvalidationSet *set = InvalSet();
    PaintInvalidationStats stats;
    PaintInvalidationRegion region;
    LayoutRect bounds;
    LayoutRect rect = InvalRect(20, 20, 60, 40);

    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, rect, (LayoutNodeId)7,
                          RoboPaintInvalidationReasonBackground, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 1);
    RoboCheckEqInt(PaintInvalidationSetIsEmpty(set), 0);

    region = InvalAt(set, 0u);
    InvalRegionIs(&region, rect, RoboPaintInvalidationReasonBackground, 0, 0, "the region");
    RoboCheckEqInt((int)region.node, 7);
    RoboCheckEqInt(PaintInvalidationRegionIsValid(&region), 1);
    /* No boundary was handed in, so there is none in force and nothing was narrowed. */
    RoboCheckEqInt(PaintClipIsActive(&region.clip), 0);

    RoboCheckEqInt(PaintInvalidationSetBounds(set, &bounds), RoboOk);
    PaintRectEq(bounds, rect, "the bounds of one region are the region");
    RoboCheckEqInt(PaintInvalidationSetScope(set),
                      (int)RoboPaintInvalidationScopePartial);

    PaintInvalidationSetStats(set, &stats);
    RoboCheckEqInt((int)stats.added, 1);
    RoboCheckEqInt((int)stats.conservative, 1);
    RoboCheckEqInt((int)stats.narrowed, 0);
    RoboCheckEqInt((int)stats.full, 0);

    /* A hand-built region with no reason is not a record this build may store. */
    PaintInvalidationRegionInit(&region);
    region.rect = rect;
    RoboCheckEqInt(PaintInvalidationRegionIsValid(&region), 0);
    RoboCheckEqInt(PaintInvalidationSetAddRegion(set, &region, NULL),
                      RoboInvalidArgument);

    InvalDestroy(set);
}

static void TestInvalidationBoundsUnion(void)
{
    PaintInvalidationSet *set = InvalSet();
    LayoutRect bounds;

    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(10, 10, 20, 20), RoboLayoutNodeIdNone,
                          RoboPaintInvalidationReasonBackground, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(100, 5, 10, 10), RoboLayoutNodeIdNone,
                          RoboPaintInvalidationReasonText, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(0, 50, 5, 5), RoboLayoutNodeIdNone,
                          RoboPaintInvalidationReasonImage, NULL, NULL),
                      RoboOk);

    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 3);
    RoboCheckEqInt(PaintInvalidationSetBounds(set, &bounds), RoboOk);
    /* The rectangle containing all three, which is always sufficient and may be too large. */
    PaintRectIs(0, 5, 110, 50, bounds, "the union bounds");

    /* Deterministic iteration: the order added, by value, reason and rectangle. */
    RoboCheckEqInt((int)InvalAt(set, 0u).reason,
                      (int)RoboPaintInvalidationReasonBackground);
    RoboCheckEqInt((int)InvalAt(set, 1u).reason, (int)RoboPaintInvalidationReasonText);
    RoboCheckEqInt((int)InvalAt(set, 2u).reason, (int)RoboPaintInvalidationReasonImage);
    PaintRectIs(10, 10, 20, 20, InvalAt(set, 0u).rect, "the first region");
    PaintRectIs(0, 50, 5, 5, InvalAt(set, 2u).rect, "the third region");

    InvalDestroy(set);
}

static void TestInvalidationNarrowed(void)
{
    PaintInvalidationSet *set = InvalSet();
    PaintClip boundary = InvalClipBoth(0, 50, 0, 30);
    PaintInvalidationStats stats;
    PaintInvalidationRegion region;

    /* A rectangle that hangs out of a boundary that names both axes: the region is the overlap,
     * which is the one narrowing this module is allowed to perform. */
    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(20, 10, 60, 40), (LayoutNodeId)3,
                          RoboPaintInvalidationReasonGeometry, &boundary, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 1);
    region = InvalAt(set, 0u);
    PaintRectIs(20, 10, 30, 20, region.rect, "the intersection with the boundary");
    RoboCheckEqInt(region.narrowed, 1);
    RoboCheckEqInt(PaintClipIsActive(&region.clip), 1);
    RoboCheckEqInt(PaintInvalidationRegionIsValid(&region), 1);

    /* A rectangle the boundary excludes entirely produces **no** region: no pixel of it was ever
     * drawn, so nothing there can be stale. */
    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(200, 200, 10, 10), (LayoutNodeId)3,
                          RoboPaintInvalidationReasonGeometry, &boundary, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 1);

    PaintInvalidationSetStats(set, &stats);
    RoboCheckEqInt((int)stats.added, 1);
    RoboCheckEqInt((int)stats.narrowed, 1);
    RoboCheckEqInt((int)stats.conservative, 0);
    RoboCheckEqInt((int)stats.excluded, 1);

    InvalDestroy(set);
}

static void TestInvalidationConservativeBoundary(void)
{
    PaintInvalidationSet *set = InvalSet();
    PaintClip boundary = InvalClipXOnly(0, 50);
    PaintInvalidationStats stats;
    PaintInvalidationRegion region;

    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(20, 10, 60, 40), (LayoutNodeId)3,
                          RoboPaintInvalidationReasonGeometry, &boundary, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 1);
    region = InvalAt(set, 0u);
    /* The vertical axis has no extent to intersect with, so the rectangle is kept whole: a
     * region may be too large and may never be too small. */
    PaintRectIs(20, 10, 60, 40, region.rect, "a rectangle kept whole");
    RoboCheckEqInt(region.narrowed, 0);
    RoboCheckEqInt(PaintClipIsActive(&region.clip), 1);
    /* The boundary travels with the region, so a consumer that *can* clip still can. */
    RoboCheckEqInt(
        PaintClipEqual(&region.clip, &boundary), 1);

    PaintInvalidationSetStats(set, &stats);
    RoboCheckEqInt((int)stats.conservative, 1);
    RoboCheckEqInt((int)stats.narrowed, 0);

    InvalDestroy(set);
}

static void TestInvalidationZeroAreaAndInvalid(void)
{
    PaintInvalidationSet *set = InvalSet();
    PaintInvalidationStats stats;
    PaintInvalidationRegion region;
    PaintClip boundary = InvalClipBoth(0, 50, 0, 30);

    PaintInvalidationRegionInit(&region);

    /* A rectangle with no area is a conclusion -- a box with no size changed no pixel -- and it is
     * success with nothing stored. */
    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(10, 10, 0, 40), RoboLayoutNodeIdNone,
                          RoboPaintInvalidationReasonBackground, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintInvalidationSetIsEmpty(set), 1);
    PaintInvalidationSetStats(set, &stats);
    RoboCheckEqInt((int)stats.excluded, 1);
    RoboCheckEqInt((int)stats.added, 0);

    /* A negative size is not a rectangle, a missing reason is not a conclusion, and an id past
     * the end is not a region. */
    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(10, 10, -5, 40), RoboLayoutNodeIdNone,
                          RoboPaintInvalidationReasonBackground, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(10, 10, 5, 40), RoboLayoutNodeIdNone,
                          RoboPaintInvalidationReasonNone, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(10, 10, 5, 40), RoboLayoutNodeIdNone,
                          (PaintInvalidationReason)99, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          NULL, InvalRect(10, 10, 5, 40), RoboLayoutNodeIdNone,
                          RoboPaintInvalidationReasonBackground, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintInvalidationSetAt(set, 0u, &region, NULL), RoboNotFound);
    RoboCheckEqInt(PaintInvalidationSetAt(set, 0u, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(PaintInvalidationSetAddRect(
                          set, InvalRect(10, 60, 5, 40), RoboLayoutNodeIdNone,
                          RoboPaintInvalidationReasonBackground, &boundary, NULL),
                      RoboOk);
    /* The rectangle is below the boundary's vertical extent, so nothing is stored -- the same rule
     * as `narrowed`'s disjoint case, reached from the other direction. */
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 0);

    InvalDestroy(set);
}

/* ------------------------------------------------------------------ */
/* Two states: what a change costs                                     */
/* ------------------------------------------------------------------ */

static void TestInvalidationNoOp(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    PaintInvalidationStats stats;

    /* The same sheet twice: two documents, two trees, one picture. */
    InvalPairBegin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE, INVAL_BASE INVAL_BOX_BLUE);
    RoboCheck(PaintListEqual(pair.beforeResult.list, pair.afterResult.list));

    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 0);
    RoboCheckEqInt(PaintInvalidationSetScope(set),
                      (int)RoboPaintInvalidationScopeNone);

    /* And the walk really did visit the document: "nothing owed" is a conclusion about the
     * records, not a comparison that never happened. */
    PaintInvalidationSetStats(set, &stats);
    RoboCheck(stats.compared > 0u);
    RoboCheckEqInt((int)stats.matched, (int)stats.compared);
    RoboCheckEqInt((int)stats.differed, 0);

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

static void TestInvalidationBackgroundChange(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    PaintInvalidationRegion region;
    PaintBoxes boxes;
    size_t index;

    InvalPairBegin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; background-color: #cc3344 }");
    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 1);
    RoboCheckEqInt(PaintInvalidationSetScope(set),
                      (int)RoboPaintInvalidationScopePartial);

    region = InvalAt(set, 0u);
    RoboCheckEqInt((int)region.reason, (int)RoboPaintInvalidationReasonBackground);
    RoboCheckEqInt((int)region.narrowed, 0);
    RoboCheckEqInt((int)region.full, 0);

    /* The region is the box layout concluded -- read from the record rather than written down. */
    RoboCheck(region.node != RoboLayoutNodeIdNone);
    RoboCheckEqInt(region.node, (int)PaintBox(&pair.before, "box"));
    /* The background acts on the **border box**, which is where the record says it is -- the
     * body's background is a record too, so the lookup names the node rather than taking the
     * first one the list happens to hold. */
    memset(&boxes, 0, sizeof(boxes));
    RoboCheck(InvalBoxRect(&pair.beforeResult, region.node, &boxes.border));
    PaintRectEq(region.rect, boxes.border, "the region is the box's background record box");
    index = 0u;
    (void)index;

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

static void TestInvalidationBorderChange(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    size_t index;

    InvalPairBegin(
        &pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE " .box { border: 4px solid #111111 }",
        INVAL_BASE INVAL_BOX_BLUE " .box { border: 4px solid #eeeeee }");
    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    RoboCheck(PaintInvalidationSetCount(set) >= 1u);
    for (index = 0u; index < PaintInvalidationSetCount(set); index++) {
        RoboCheckEqInt((int)InvalAt(set, index).reason,
                          (int)RoboPaintInvalidationReasonBorder);
    }

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

static void TestInvalidationTextChange(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    size_t index;
    int texts = 0;

    InvalPairBegin(&pair,
                     "<html><body><div class=\"box\"><p>Hello</p></div></body></html>",
                     INVAL_BASE INVAL_BOX_BLUE " .box p { color: #ff0000 }",
                     INVAL_BASE INVAL_BOX_BLUE " .box p { color: #00ff00 }");
    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    RoboCheck(PaintInvalidationSetCount(set) >= 1u);
    for (index = 0u; index < PaintInvalidationSetCount(set); index++) {
        if (InvalAt(set, index).reason == RoboPaintInvalidationReasonText) {
            texts++;
        }
    }
    /* The run's own record is the one that changed, and its reason is the text. */
    RoboCheckEqInt(texts, 1);

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

static void TestInvalidationOpacityChange(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    PaintInvalidationSet *subtree = InvalSet();
    LayoutNodeId outer;
    LayoutNodeId inner;
    LayoutRect innerRect;
    LayoutRect outerRect;
    size_t index;
    int opacityRegions = 0;

    InvalPairBegin(&pair,
                     "<html><body><div class=\"box\"><p class=\"child\">Child</p></div></body></html>",
                     INVAL_BASE INVAL_BOX_BLUE " .box { opacity: 1 } "
                     ".box .child { width: 30px; height: 12px; background-color: #22aa55 }",
                     INVAL_BASE INVAL_BOX_BLUE " .box { opacity: 0.4 } "
                     ".box .child { width: 30px; height: 12px; background-color: #22aa55 }");

    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    for (index = 0u; index < PaintInvalidationSetCount(set); index++) {
        if (InvalAt(set, index).reason == RoboPaintInvalidationReasonOpacity) {
            opacityRegions++;
        }
    }
    RoboCheck(opacityRegions >= 1);

    /*
     * And the door an element's strength needs, read on its own: a subtree mark covers the
     * **descendant's** rectangle, which is the whole difference between invalidating the box and
     * invalidating what the box composites to.
     */
    outer = PaintBox(&pair.before, "box");
    inner = PaintBox(&pair.before, "child");
    /* The descendant's rectangle **in the artifact**, not in layout's space: the region is a paint
     * rectangle, and the two spaces are not interchangeable (11.1). */
    memset(&innerRect, 0, sizeof(innerRect));
    RoboCheckEqInt(InvalBoxRect(&pair.beforeResult, inner, &innerRect), 1);
    RoboCheckEqInt(InvalBoxRect(&pair.beforeResult, outer, &outerRect), 1);
    RoboCheck(innerRect.x >= outerRect.x && innerRect.y >= outerRect.y);
    RoboCheckEqInt(PaintInvalidationSetMarkSubtree(
                          subtree, pair.beforeResult.list, outer,
                          RoboPaintInvalidationReasonOpacity, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(subtree), 1);
    {
        PaintInvalidationRegion region = InvalAt(subtree, 0u);
        PaintInvalidationStats stats;

        RoboCheck(InvalRectContains(region.rect, innerRect.x, innerRect.y));
        RoboCheck(InvalRectContains(region.rect, innerRect.x + 1, innerRect.y + 1));
        /* And the whole of it, not just its top-left corner. */
        RoboCheck(InvalRectContains(region.rect, innerRect.x + innerRect.width - 1,
                                       innerRect.y + innerRect.height - 1));
        RoboCheckEqInt((int)region.reason, (int)RoboPaintInvalidationReasonOpacity);
        PaintInvalidationSetStats(subtree, &stats);
        RoboCheckEqInt((int)stats.spans, 1);
    }

    InvalDestroy(subtree);
    InvalDestroy(set);
    InvalPairEnd(&pair);
}

/**
 * The old-and-new rectangle, as one region.
 *
 * The door a caller uses when it knows **both** rectangles itself: a resource that finished
 * decoding somewhere else, a box a caller moved without a layout pass, a region a theme swap
 * restores. The two rectangles are unioned in 64 bits and saturated, and the boundary narrows
 * the **union** rather than either side -- a union narrowed by one of the two would be an answer
 * describing half of what changed.
 */
static void TestInvalidationAddChangeUnion(void)
{
    PaintInvalidationSet *set = InvalSet();
    PaintInvalidationStats stats;
    PaintInvalidationRegion region;
    PaintClip boundary = InvalClipBoth(0, 200, 0, 200);

    RoboCheckEqInt(PaintInvalidationSetAddChange(set, InvalRect(20, 20, 100, 50),
                                                             InvalRect(40, 30, 100, 50),
                                                             (LayoutNodeId)5,
                                                             RoboPaintInvalidationReasonGeometry,
                                                             &boundary, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 1);
    region = InvalAt(set, 0u);
    /* The union of the two, which is exactly the brief's `A ∪ B`. */
    PaintRectIs(20, 20, 120, 60, region.rect, "the union of the old and new rectangles");
    RoboCheckEqInt(region.narrowed, 1);
    RoboCheckEqInt((int)region.reason, (int)RoboPaintInvalidationReasonGeometry);
    RoboCheckEqInt(PaintClipEqual(&region.clip, &boundary), 1);
    PaintInvalidationSetStats(set, &stats);
    RoboCheckEqInt((int)stats.unions, 1);
    RoboCheckEqInt((int)stats.added, 1);

    /* The two rectangles are snapshots: the values handed in are copied, so the region cannot
     * follow anything a caller does to its own records afterwards. */
    RoboCheckEqInt(PaintInvalidationRegionIsValid(&region), 1);

    /* A union outside its own boundary is dropped by the same rule as any other rectangle. */
    PaintInvalidationSetClear(set);
    RoboCheckEqInt(PaintInvalidationSetAddChange(
                          set, InvalRect(400, 400, 10, 10), InvalRect(420, 420, 10, 10),
                          RoboLayoutNodeIdNone, RoboPaintInvalidationReasonImage,
                          &boundary, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintInvalidationSetIsEmpty(set), 1);

    InvalDestroy(set);
}

static void TestInvalidationGeometryMove(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    PaintBoxes was;
    PaintBoxes now;
    PaintInvalidationRegion first;
    PaintInvalidationRegion second;
    LayoutRect bounds;
    LayoutRect oldRect;
    LayoutRect newRect;
    int foundOld = 0;
    int foundNew = 0;

    InvalPairBegin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE INVAL_BOX_BLUE " .box { position: relative; left: 40px }");

    memset(&was, 0, sizeof(was));
    memset(&now, 0, sizeof(now));
    RoboCheckEqInt(PaintBoxesOf(pair.before.tree, PaintBox(&pair.before, "box"),
                                          &was, NULL),
                      RoboOk);
    RoboCheckEqInt(
        PaintBoxesOf(pair.after.tree, PaintBox(&pair.after, "box"), &now, NULL), RoboOk);
    RoboCheck(was.border.x != now.border.x);

    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 2);
    first = InvalAt(set, 0u);
    second = InvalAt(set, 1u);
    /* Both rectangles: the one the box left, and the one it is in. */
    if (first.node == PaintBox(&pair.before, "box")) {
        foundOld = 1;
    }
    if (second.node == PaintBox(&pair.after, "box")) {
        foundNew = 1;
    }
    RoboCheckEqInt(foundOld, 1);
    RoboCheckEqInt(foundNew, 1);
    RoboCheckEqInt((int)first.reason, (int)RoboPaintInvalidationReasonGeometry);
    RoboCheckEqInt((int)second.reason, (int)RoboPaintInvalidationReasonGeometry);
    RoboCheck(first.rect.x < second.rect.x);

    /* And each region carries that box's own painted rectangle, not merely its number: the brief's
     * "old bounds ∪ new bounds" is read out of the two artifacts here rather than restated by
     * hand, so a walk that widened, dropped or misplaced either rectangle would fail this. */
    RoboCheck(InvalBoxRect(&pair.beforeResult, PaintBox(&pair.before, "box"), &oldRect));
    RoboCheck(InvalBoxRect(&pair.afterResult, PaintBox(&pair.after, "box"), &newRect));
    RoboCheck(InvalSetHas(set, oldRect, PaintBox(&pair.before, "box")));
    RoboCheck(InvalSetHas(set, newRect, PaintBox(&pair.after, "box")));
    RoboCheck(oldRect.x != newRect.x);
    /* A displacement changes no coordinate the box's own records carry except the position, so the
     * rectangles are the same size and 40px apart -- the shape the old-and-new rule exists for. */
    RoboCheckEqInt((int)first.rect.width, (int)second.rect.width);
    RoboCheckEqInt((int)first.rect.height, (int)second.rect.height);

    RoboCheckEqInt(PaintInvalidationSetBounds(set, &bounds), RoboOk);
    RoboCheck(InvalRectContains(bounds, first.rect.x, first.rect.y));
    RoboCheck(InvalRectContains(bounds, second.rect.x, second.rect.y));

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

static void TestInvalidationGeometryResize(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();

    InvalPairBegin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 120px; height: 40px; margin: 20px; background-color: #3366cc }");
    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    RoboCheck(PaintInvalidationSetCount(set) >= 2u);
    /* The wide rectangle is covered: a background that grew leaves stale pixels in the part the
     * smaller one did not reach. */
    {
        LayoutRect bounds;

        RoboCheckEqInt(PaintInvalidationSetBounds(set, &bounds), RoboOk);
        RoboCheck(bounds.width >= 120);
    }

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

/**
 * A box that appears and a box that goes away.
 *
 * The transition is `display: none` on an element that is **last** in the document: the records
 * the change adds are at the tail of the list, so the positions both artifacts share still pair
 * up and the difference this case is about is not buried under the shift a change in the middle
 * would cause. The same pair answers both directions -- the second call hands the artifacts over
 * swapped -- because an insertion and a removal are one difference read from either end.
 *
 * The regions are `CONTENT` and not `STYLE`: what changed for a reader is that the sequence of
 * drawings changed, which is the reason the vocabulary keeps for exactly this.
 */
static void TestInvalidationInsertionRemoval(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    PaintInvalidationStats stats;
    size_t index;
    int contentRegions = 0;

    InvalPairBegin(
        &pair, "<html><body><div class=\"box\"></div><div class=\"extra\"></div></body></html>",
        INVAL_BASE INVAL_BOX_BLUE
            ".extra { display: none; width: 24px; height: 18px; margin: 20px; "
            "background-color: #22aa55 }",
        INVAL_BASE INVAL_BOX_BLUE
            ".extra { width: 24px; height: 18px; margin: 20px; background-color: #22aa55 }");

    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    PaintInvalidationSetStats(set, &stats);
    RoboCheck(stats.compared > 0u);
    /* The positions only the after list has, and -- the part that is a claim rather than a count
     * -- a region for the one of them that draws. */
    RoboCheck(stats.inserted > 0u);
    RoboCheckEqInt((int)stats.removed, 0);
    for (index = 0u; index < PaintInvalidationSetCount(set); index++) {
        if (InvalAt(set, index).reason == RoboPaintInvalidationReasonContent) {
            contentRegions++;
        }
    }
    RoboCheck(contentRegions >= 1);

    /* The same difference from the other end: the element is now the one that went away. */
    PaintInvalidationSetClear(set);
    RoboCheckEqInt(PaintInvalidationSetFromLists(
                          set, pair.afterResult.list, pair.beforeResult.list, NULL),
                      RoboOk);
    PaintInvalidationSetStats(set, &stats);
    RoboCheck(stats.removed > 0u);
    RoboCheckEqInt((int)stats.inserted, 0);
    contentRegions = 0;
    for (index = 0u; index < PaintInvalidationSetCount(set); index++) {
        if (InvalAt(set, index).reason == RoboPaintInvalidationReasonContent) {
            contentRegions++;
        }
    }
    RoboCheck(contentRegions >= 1);

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

/**
 * The boundary moving is its own reason.
 *
 * A clipping ancestor displaced by 40px moves the interval it puts in force, so the records inside
 * it differ in their **boundary** as well as their rectangle. The reason is reported as `clip`
 * rather than `geometry` on purpose: what changed for the reader is *what is drawn there*, and a
 * reason that said "it moved" would send them looking for a layout change in a document that has
 * one.
 */
static void TestInvalidationClipChange(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    PaintInvalidationStats stats;
    size_t index;
    int clipRegions = 0;

    InvalPairBegin(
        &pair, "<html><body><div class=\"clip\"><div class=\"wide\"></div></div></body></html>",
        INVAL_BASE
        ".clip { width: 40px; height: 30px; margin: 10px; overflow: hidden } "
        ".wide { width: 200px; height: 20px; background-color: #3366cc }",
        INVAL_BASE
        ".clip { width: 40px; height: 30px; margin: 10px; overflow: hidden; "
        "position: relative; left: 40px } "
        ".wide { width: 200px; height: 200px; background-color: #3366cc }");

    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    RoboCheck(PaintInvalidationSetCount(set) >= 1u);
    PaintInvalidationSetStats(set, &stats);
    for (index = 0u; index < PaintInvalidationSetCount(set); index++) {
        PaintInvalidationRegion region = InvalAt(set, index);

        if (region.reason == RoboPaintInvalidationReasonClip) {
            clipRegions++;
            /* The boundary travels with the region, and a clipped region says so. */
            RoboCheckEqInt(PaintClipIsActive(&region.clip), 1);
        }
    }
    RoboCheck(clipRegions >= 1);
    RoboCheck((int)stats.narrowed >= 1);

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

/* ------------------------------------------------------------------ */
/* Two states: pixels                                                  */
/* ------------------------------------------------------------------ */

/**
 * Every pixel that changed lies inside a region -- the claim a partial repaint may rely on.
 *
 * The transition is a colour change **and** a displacement at once, so the difference between the
 * frames is neither a single rectangle nor a rectangle pair, and the regions are the records' own
 * boxes rather than the ink: the case is deliberately harder than the shape of the answer.
 */
static void TestInvalidationStalePixels(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    GfxSurface *frameA = InvalSurface(INVAL_PAGE);
    GfxSurface *frameB = InvalSurface(INVAL_PAGE);
    size_t differing = 0u;
    size_t uncovered = 0u;

    InvalPairBegin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; "
                     "background-color: #cc3344; position: relative; left: 30px }");
    InvalRender(pair.beforeResult.list, frameA);
    InvalRender(pair.afterResult.list, frameB);

    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    InvalCompareFrames(frameA, frameB, set, &differing, &uncovered);

    /* The two frames really are different -- otherwise "nothing outside a region changed" would be
     * a fact about an empty picture. */
    RoboCheck(differing > 0u);
    RoboCheckEqInt((int)uncovered, 0);
    RoboCheckEqInt((int)PaintInvalidationSetScope(set),
                      (int)RoboPaintInvalidationScopePartial);

    GfxSurfaceRelease(frameB);
    GfxSurfaceRelease(frameA);
    InvalDestroy(set);
    InvalPairEnd(&pair);
}

/**
 * The capstone: clearing the regions and painting the new frame reproduces a full repaint.
 *
 * `frameA` becomes the partial repaint (its regions cleared to the page, then the after list
 * painted into it) and `frame_c` is a fresh render of the after list. Byte-for-byte equal, or the
 * set was missing a pixel. It is the strongest statement this build can make about invalidation
 * with a rasteriser that has no notion of damage, and it is why the module is allowed to claim
 * *sufficiency* rather than exactness.
 */
static void TestInvalidationPartialRepaintMatchesFull(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    GfxSurface *partial = InvalSurface(INVAL_PAGE);
    GfxSurface *full = InvalSurface(INVAL_PAGE);
    size_t differing = 0u;
    size_t uncovered = 0u;

    InvalPairBegin(
        &pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
        INVAL_BASE
        ".box { width: 90px; height: 55px; margin: 25px; "
        "background-color: #228844; position: relative; top: 35px; left: 40px }");

    /* The first frame, painted in full -- that is what a real caller would already have on screen. */
    InvalRender(pair.beforeResult.list, partial);
    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);

    /* The partial repaint: clear what the set says is stale, then paint the new frame. */
    InvalClearRegions(set, partial, INVAL_PAGE);
    InvalRender(pair.afterResult.list, partial);

    /* The reference: a full render of the same frame from the same base colour. */
    InvalRender(pair.afterResult.list, full);

    InvalCompareFrames(partial, full, set, &differing, &uncovered);
    RoboCheckEqInt((int)differing, 0);

    GfxSurfaceRelease(full);
    GfxSurfaceRelease(partial);
    InvalDestroy(set);
    InvalPairEnd(&pair);
}

/* ------------------------------------------------------------------ */
/* Marks                                                               */
/* ------------------------------------------------------------------ */

static void TestInvalidationMarkOperationAndNode(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    LayoutNodeId box;
    PaintOperation marker;
    PaintInvalidationRegion fromOperation;
    PaintInvalidationRegion fromList;

    InvalPairBegin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE, INVAL_BASE INVAL_BOX_BLUE);
    box = PaintBox(&pair.before, "box");
    /* The box's **own** begin record rather than the list's first one of that kind: what the case
     * is about is this node's record, and a marker picked by position would go on passing if the
     * walk started emitting some other box first. */
    RoboCheck(InvalBoxStart(&pair.beforeResult, box, &marker));

    RoboCheckEqInt(PaintInvalidationSetMarkOperation(
                          set, &marker, RoboPaintInvalidationReasonBackground, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 1);
    fromOperation = InvalAt(set, 0u);
    RoboCheckEqInt((int)fromOperation.node, (int)box);
    PaintRectEq(fromOperation.rect, marker.boxes.border, "the record's border box");

    /* The list door reads the same geometry out of the same artifact. */
    PaintInvalidationSetClear(set);
    RoboCheckEqInt(PaintInvalidationSetMarkNode(
                          set, pair.beforeResult.list, box,
                          RoboPaintInvalidationReasonBackground, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 1);
    fromList = InvalAt(set, 0u);
    RoboCheck(PaintInvalidationRegionEqual(&fromOperation, &fromList));

    /* A box this artifact does not have is a caller's mistake rather than a region. */
    RoboCheckEqInt(PaintInvalidationSetMarkNode(
                          set, pair.beforeResult.list, (LayoutNodeId)999999u,
                          RoboPaintInvalidationReasonBackground, NULL),
                      RoboNotFound);
    RoboCheckEqInt(PaintInvalidationSetMarkNode(
                          set, pair.beforeResult.list, RoboLayoutNodeIdNone,
                          RoboPaintInvalidationReasonBackground, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintInvalidationSetMarkOperation(
                          set, NULL, RoboPaintInvalidationReasonBackground, NULL),
                      RoboInvalidArgument);

    /* Both artifacts at once, which is the moment in the middle of a mutation. */
    RoboCheckEqInt(PaintInvalidationSetMarkNodeChange(
                          set, pair.beforeResult.list, pair.afterResult.list, box,
                          RoboPaintInvalidationReasonGeometry, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintInvalidationSetMarkNodeChange(
                          set, pair.beforeResult.list, pair.afterResult.list,
                          (LayoutNodeId)999999u,
                          RoboPaintInvalidationReasonGeometry, NULL),
                      RoboNotFound);

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

static void TestInvalidationFullAndFromStats(void)
{
    PaintInvalidationSet *set = InvalSet();
    PaintStats stats;
    PaintInvalidationRegion region;
    LayoutRect extent = InvalRect(0, 0, 800, 600);

    RoboCheckEqInt(PaintInvalidationSetMarkFull(
                          set, extent, RoboPaintInvalidationReasonViewport, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 1);
    region = InvalAt(set, 0u);
    InvalRegionIs(&region, extent, RoboPaintInvalidationReasonViewport, 0, 1,
                    "a whole-extent region");
    RoboCheckEqInt(PaintInvalidationSetScope(set),
                      (int)RoboPaintInvalidationScopeFull);

    /* 11.1's hook, read the way the phase said it would be: a count with no rectangle. */
    PaintInvalidationSetClear(set);
    memset(&stats, 0, sizeof(stats));
    RoboCheckEqInt(PaintInvalidationSetFromStats(set, &stats, extent, NULL), RoboOk);
    RoboCheckEqInt(PaintInvalidationSetIsEmpty(set), 1);

    stats.styleOnly = 4u;
    RoboCheckEqInt(PaintInvalidationSetFromStats(set, &stats, extent, NULL), RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 1);
    region = InvalAt(set, 0u);
    InvalRegionIs(&region, extent, RoboPaintInvalidationReasonStyle, 0, 1,
                    "the conservative answer");
    RoboCheckEqInt(PaintInvalidationSetScope(set),
                      (int)RoboPaintInvalidationScopeFull);
    RoboCheckEqInt(PaintInvalidationSetFromStats(NULL, &stats, extent, NULL),
                      RoboInvalidArgument);

    InvalDestroy(set);
}

/* ------------------------------------------------------------------ */
/* Display lists                                                       */
/* ------------------------------------------------------------------ */

static void TestInvalidationDisplayLists(void)
{
    InvalPair pair;
    PaintInvalidationSet *fromRecords = InvalSet();
    PaintInvalidationSet *fromCommands = InvalSet();
    PaintDisplayList *beforeDisplay = NULL;
    PaintDisplayList *afterDisplay = NULL;
    PaintDisplayListBuilder *builder = NULL;
    PaintInvalidationRegion region;
    size_t index;
    int backgrounds = 0;

    InvalPairBegin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; background-color: #cc3344 }");

    RoboCheckEqInt(PaintDisplayListCreate(&beforeDisplay, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListCreate(&afterDisplay, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderBuild(builder, pair.beforeResult.list,
                                                            beforeDisplay, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderBuild(builder, pair.afterResult.list,
                                                            afterDisplay, NULL, NULL),
                      RoboOk);
    PaintDisplayListBuilderDestroy(builder);

    RoboCheckEqInt(PaintInvalidationSetFromDisplayLists(
                          fromCommands, beforeDisplay, afterDisplay, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(fromCommands), 1);
    region = InvalAt(fromCommands, 0u);
    RoboCheckEqInt((int)region.reason, (int)RoboPaintInvalidationReasonBackground);
    for (index = 0u; index < PaintDisplayListCount(beforeDisplay); index++) {
        PaintDisplayCommand command;

        RoboCheckEqInt(PaintDisplayListAt(beforeDisplay, index, &command),
                          RoboOk);
        /* The **changed box's** background command, found by node rather than by position: the
         * page's own backgrounds are `draw-background` commands too, and the first of them is
         * neither the box nor the one the region is about. */
        if (command.kind == RoboPaintDisplayCmdDrawBackground &&
            PaintDisplayCommandNode(&command) == region.node) {
            /* A command's rectangle is the resolved one, which is what a rasteriser reads. */
            PaintRectEq(region.rect, command.rect, "the draw-background rect");
            backgrounds++;
            break;
        }
    }
    RoboCheckEqInt(backgrounds, 1);

    /* The two artifact kinds agree about a background change: the same rectangle, the same reason. */
    RoboCheckEqInt(PaintInvalidationSetFromLists(
                          fromRecords, pair.beforeResult.list, pair.afterResult.list, NULL),
                      RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(fromRecords), 1);
    {
        PaintInvalidationRegion fromRecordsRegion = InvalAt(fromRecords, 0u);

        RoboCheck(PaintInvalidationRegionEqual(&region, &fromRecordsRegion));
    }

    PaintDisplayListDestroy(afterDisplay);
    PaintDisplayListDestroy(beforeDisplay);
    InvalDestroy(fromCommands);
    InvalDestroy(fromRecords);
    InvalPairEnd(&pair);
}

/* ------------------------------------------------------------------ */
/* Determinism, isolation and the dump                                 */
/* ------------------------------------------------------------------ */

/**
 * The set is a snapshot of a **decision**, not a view of an artifact.
 *
 * The set is built from one pair of trees, then the transition is rebuilt from scratch in two
 * **other** trees and compared: equal regions, in the same order, with the same reasons -- which is
 * a statement no address, no allocation order and no clock can satisfy by accident. Then the
 * original pair's record lists are destroyed and the set is read again, unchanged: a set that
 * borrowed an artifact would be reading freed memory by then.
 */
static void TestInvalidationDeterminismAndIsolation(void)
{
    InvalPair first;
    InvalPair second;
    PaintInvalidationSet *set = InvalSet();
    PaintInvalidationSet *again = InvalSet();
    PaintInvalidationRegion saved[8];
    size_t savedCount;
    size_t index;
    char firstDump[4096];
    char secondDump[4096];
    size_t firstLength = 0u;
    size_t secondLength = 0u;

    InvalPairBegin(&first, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; "
                     "background-color: #cc3344; position: relative; left: 25px }");
    InvalPairBegin(&second, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; "
                     "background-color: #cc3344; position: relative; left: 25px }");

    RoboCheckEqInt(PaintInvalidationSetFromLists(
                          set, first.beforeResult.list, first.afterResult.list, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintInvalidationSetFromLists(
                          again, second.beforeResult.list, second.afterResult.list, NULL),
                      RoboOk);
    RoboCheck(PaintInvalidationSetCount(set) > 0u);
    RoboCheck(PaintInvalidationSetEqual(set, again));
    RoboCheck(!PaintInvalidationSetEqual(set, NULL));
    RoboCheck(PaintInvalidationSetEqual(NULL, NULL));

    RoboCheckEqInt(PaintInvalidationSetWrite(set, firstDump, sizeof(firstDump),
                                                        &firstLength, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintInvalidationSetWrite(again, secondDump, sizeof(secondDump),
                                                        &secondLength, NULL),
                      RoboOk);
    RoboCheckEqInt((int)firstLength, (int)secondLength);
    RoboCheck(strcmp(firstDump, secondDump) == 0);
    /* No address, no allocation and no clock reaches the dump. */
    RoboCheck(strstr(firstDump, "0x") == NULL);

    savedCount = PaintInvalidationSetCount(set);
    RoboCheck(savedCount <= sizeof(saved) / sizeof(saved[0]));
    for (index = 0u; index < savedCount; index++) {
        saved[index] = InvalAt(set, index);
    }

    /* The artifacts go away. */
    PaintResultDispose(&first.beforeResult);
    PaintResultDispose(&first.afterResult);
    PaintEnd(&first.after);
    PaintEnd(&first.before);

    /* And the decision is still there, field for field. */
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), (int)savedCount);
    for (index = 0u; index < savedCount; index++) {
        PaintInvalidationRegion region = InvalAt(set, index);

        RoboCheck(PaintInvalidationRegionEqual(&region, &saved[index]));
        RoboCheckEqInt(PaintInvalidationRegionIsValid(&region), 1);
    }

    InvalDestroy(again);
    InvalDestroy(set);
    InvalPairEnd(&second);
}

static void TestInvalidationDump(void)
{
    InvalPair pair;
    PaintInvalidationSet *set = InvalSet();
    char buffer[4096];
    char small[16];
    size_t length = 0u;
    size_t needed = 0u;

    InvalPairBegin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; "
                     "background-color: #cc3344; position: relative; left: 25px }");
    RoboCheckEqInt(PaintInvalidationSetFromLists(set, pair.beforeResult.list,
                                                             pair.afterResult.list, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintInvalidationSetWrite(set, buffer, sizeof(buffer), &length,
                                                        NULL),
                      RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(buffer, "reason=geometry") != NULL);
    RoboCheck(strstr(buffer, "rect=") != NULL);
    RoboCheck(strstr(buffer, "narrowed=") != NULL);
    RoboCheck(strstr(buffer, "full=") != NULL);
    RoboCheck(strstr(buffer, "scope=partial") != NULL);
    RoboCheck(strstr(buffer, "regions=2") != NULL);
    RoboCheck(strstr(buffer, "excluded=0") != NULL);
    RoboCheck(strstr(buffer, "0x") == NULL);

    /* The `_write` contract: a refusal hands back the length it would have needed, so a caller can
     * size its buffer from the refusal rather than from an estimate. */
    RoboCheckEqInt(PaintInvalidationSetWrite(set, small, sizeof(small), &needed, NULL),
                      RoboNotSupported);
    RoboCheckEqInt((int)needed, (int)length);
    RoboCheckEqInt(PaintInvalidationSetWrite(set, NULL, 0u, &needed, NULL),
                      RoboInvalidArgument);

    /* An empty set dumps as a summary line and nothing else. */
    PaintInvalidationSetClear(set);
    RoboCheckEqInt(PaintInvalidationSetWrite(set, buffer, sizeof(buffer), &length,
                                                        NULL),
                      RoboOk);
    RoboCheck(strstr(buffer, "scope=none") != NULL);
    RoboCheck(strstr(buffer, "regions=0") != NULL);
    RoboCheck(strstr(buffer, "reason=") == NULL);

    InvalDestroy(set);
    InvalPairEnd(&pair);
}

/**
 * The set owns its regions and gives them back.
 *
 * Run **twice** and compare the two ends, which is the rule 11.7's finding recorded: a baseline
 * measured before the first rig and after the second teardown says as much about what the harness
 * had already allocated as about this module, while comparing the two cycles measures the module's
 * own ownership.
 */
static void TestInvalidationLifetime(void)
{
    InvalPair pair;
    size_t first;
    size_t second;
    int cycle;

    InvalPairBegin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; "
                     "background-color: #cc3344; position: relative; left: 25px }");

    first = PaintLiveBlocks();
    for (cycle = 0; cycle < 2; cycle++) {
        PaintInvalidationSet *set = InvalSet();
        PaintInvalidationRegion region;
        PaintInvalidationStats stats;

        RoboCheckEqInt(PaintInvalidationSetFromLists(
                              set, pair.beforeResult.list, pair.afterResult.list, NULL),
                          RoboOk);
        /* Growth: more regions than the first block, so the array really is re-taken. */
        region = InvalAt(set, 0u);
        RoboCheckEqInt(PaintInvalidationRegionIsValid(&region), 1);
        PaintInvalidationSetStats(set, &stats);
        RoboCheck(stats.added > 0u);

        /* Clearing keeps the allocation and resets the record. */
        PaintInvalidationSetClear(set);
        PaintInvalidationSetStats(set, &stats);
        RoboCheckEqInt((int)stats.added, 0);
        RoboCheck(PaintInvalidationSetCapacity(set) >=
                   RoboPaintInvalidationInitialCapacity);

        InvalDestroy(set);
    }
    second = PaintLiveBlocks();
    RoboCheckEqInt((int)second, (int)first);

    InvalPairEnd(&pair);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintInvalidation(void)
{
    TestRegister("paint.invalidation.reason_vocabulary",
                       TestInvalidationReasonVocabulary);
    TestRegister("paint.invalidation.class_and_scope_vocabulary",
                       TestInvalidationClassAndScopeVocabulary);
    TestRegister("paint.invalidation.classify_property",
                       TestInvalidationClassifyProperty);
    TestRegister("paint.invalidation.reason_for_property",
                       TestInvalidationReasonForProperty);
    TestRegister("paint.invalidation.unconsumed_properties",
                       TestInvalidationUnconsumedProperties);
    TestRegister("paint.invalidation.empty_set", TestInvalidationEmptySet);
    TestRegister("paint.invalidation.add_rect", TestInvalidationAddRect);
    TestRegister("paint.invalidation.bounds_union", TestInvalidationBoundsUnion);
    TestRegister("paint.invalidation.narrowed", TestInvalidationNarrowed);
    TestRegister("paint.invalidation.conservative",
                       TestInvalidationConservativeBoundary);
    TestRegister("paint.invalidation.zero_area_and_invalid",
                       TestInvalidationZeroAreaAndInvalid);
    TestRegister("paint.invalidation.no_op", TestInvalidationNoOp);
    TestRegister("paint.invalidation.add_change_union",
                       TestInvalidationAddChangeUnion);
    TestRegister("paint.invalidation.background_change",
                       TestInvalidationBackgroundChange);
    TestRegister("paint.invalidation.border_change", TestInvalidationBorderChange);
    TestRegister("paint.invalidation.text_change", TestInvalidationTextChange);
    TestRegister("paint.invalidation.opacity_change", TestInvalidationOpacityChange);
    TestRegister("paint.invalidation.geometry_move", TestInvalidationGeometryMove);
    TestRegister("paint.invalidation.geometry_resize", TestInvalidationGeometryResize);
    TestRegister("paint.invalidation.insertion", TestInvalidationInsertionRemoval);
    TestRegister("paint.invalidation.clip_change", TestInvalidationClipChange);
    TestRegister("paint.invalidation.stale_pixels", TestInvalidationStalePixels);
    TestRegister("paint.invalidation.partial_repaint_matches_full",
                       TestInvalidationPartialRepaintMatchesFull);
    TestRegister("paint.invalidation.mark_operation_and_node",
                       TestInvalidationMarkOperationAndNode);
    TestRegister("paint.invalidation.full_and_from_stats",
                       TestInvalidationFullAndFromStats);
    TestRegister("paint.invalidation.display_lists", TestInvalidationDisplayLists);
    TestRegister("paint.invalidation.determinism_and_isolation",
                       TestInvalidationDeterminismAndIsolation);
    TestRegister("paint.invalidation.dump", TestInvalidationDump);
    TestRegister("paint.invalidation.lifetime", TestInvalidationLifetime);
}
