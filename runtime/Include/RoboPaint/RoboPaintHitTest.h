/*
 * RoboPaint hit test (Phase 11.10) — "which node is under this point?".
 *
 * The pipeline this answers about is the one this module already renders:
 *
 *   HTML/DOM → RoboCSS → RoboLayout → RoboPaint → display list → RoboGraphics
 *
 * and the question is the **inverse** of the last three stages. Painting turns a laid-out tree
 * into an ordered sequence of drawings; a hit test takes one point on the drawing plane and
 * walks back down to the node that would be drawn there. Everything it needs has therefore
 * already been concluded -- the geometry by RoboLayout, the boundary in force by 11.8's clip
 * record, the strength in force by 11.9's opacity record -- and this header adds a *reader* of
 * those conclusions rather than a second model of any of them.
 *
 * ## Why this lives in RoboPaint and not in a module of its own
 *
 * The natural reading of the phase is a new top-level subsystem, and it is the wrong one. A hit
 * test is a second consumer of exactly the two records this module owns --
 * `robo_paint_clip_t` (the boundary in force) and `robo_paint_opacity_t` (the strength in
 * force) -- and a module beside RoboPaint would have to either duplicate them or depend on paint
 * for two types and reach past it for the geometry, which is two boundaries where there was one.
 * What it shares with the painter is deeper than the two records: **the walk**. The paint
 * traversal accumulates a page origin, narrows a boundary and multiplies a strength down the
 * tree, and a hit test accumulates the same three things in the same order. Putting the second
 * walk next to the first is what makes "the two agree about where a box is" a fact a suite can
 * assert, rather than a coincidence two modules maintain separately.
 *
 * So this is a **subsystem of RoboPaint with its own installed header, source, suite and family
 * document** -- `RoboPaintHitTest.*`, `paint/tests/SuiteRoboPaintHitTest.c`, `paint/docs/HitTest.md`
 * -- and it adds no dependency edge: RoboPaint already reads a layout tree and already owns the
 * clip and opacity records (DEPENDENCIES.md).
 *
 * ## The coordinate model, stated once
 *
 * Three spaces, and only one of them is new:
 *
 *   **layout coordinates**  what RoboLayout wrote: a box's position is relative to its
 *                           containing block's **content origin**. `RoboPaintOperation.h`
 *                           carries this space on every operation and `local = paint - origin`
 *                           recovers it;
 *
 *   **paint coordinates**   one page space, the accumulated origins of every ancestor added
 *                           in. The root box's paint origin is `(0, 0)`;
 *
 *   **window coordinates**  what a window or input layer reports a pointer in.
 *
 * **Paint coordinates are window coordinates in this build, and that is a statement about the
 * document rather than a convenience.** The traversal begins the root at the origin `(0, 0)`,
 * this build has no scroll offset to subtract, no transform to invert, no zoom and no
 * device-pixel-ratio scale -- so the plane a drawing lands on and the plane an input point
 * arrives in are the same plane, and the query takes its point directly in paint coordinates.
 * The two names are kept apart anyway, because the phase that adds scrolling is the phase that
 * makes them differ and a signature that said "window" here would be a lie the day that lands.
 * A caller with a window-relative point therefore hands it over unchanged *today*, and this
 * header says so rather than leaving it to be rediscovered.
 *
 * ## What decides the answer
 *
 * Four rules, each of which is a decision rather than an implementation detail, and each of
 * which mirrors the painter exactly so that the two cannot disagree about a document:
 *
 *   **last drawn wins.** The painter's order for a box is: the box's own decorations, then its
 *   children left to right, each child's subtree in full. So the node that *covers* a point is
 *   the **last** one drawn there, and the walk visits a box's children in **reverse** order and
 *   tests the box itself **after** all of them. A document-order walk would answer with the
 *   box underneath, which is the classic hit-test bug and is why this is stated first.
 *
 *   **a box's own region is its border box.** The region a box may be hit in is the union of the
 *   three layers it could paint into, and for every kind of box this build has that union is the
 *   **border box**: a background fills it (11.4), a border band is inside it, and a content box
 *   is inside it. Declaring anything narrower would need `pointer-events` machinery this build
 *   does not have, and would be wrong besides: CSS hits a transparent `div` over its whole
 *   border box, because painting nothing is not the same as being absent.
 *
 *   **the boundary clips the contents, never the box.** This is 11.8's arrangement read
 *   backwards, and it is where a naive hit test goes wrong in the direction that is hardest to
 *   see: a child outside a `overflow: hidden` ancestor's boundary must not be hit, while that
 *   **ancestor's own border band** -- which is drawn outside that boundary -- must still be. So
 *   the walk enters a box under the boundary in force and descends into its children under the
 *   boundary *that box's own overflow* puts in force, exactly as the painter emits them.
 *
 *   **opacity does not affect hit testing, and that includes `opacity: 0`.** CSS governs
 *   hit testing with `pointer-events`, not with `opacity`: a fully transparent element still
 *   receives a point, because an author who wanted it not to would have said
 *   `pointer-events: none` or `display: none`. This build has no `pointer-events`, so the rule
 *   is the property's *initial* value -- everything is hit-testable -- and the strength in force
 *   travels in the **result** so that a later phase which does implement it has the number
 *   already rather than having to add it to the walk. `ROBO_PAINT_HIT_FLAG_TRANSPARENT` is how a
 *   caller asks "the node I hit is drawn at nothing", which is a fact it may want and which is
 *   *not* the same as "the point missed".
 *
 * ## What it deliberately does not do
 *
 * No **event dispatch** and no bubbling: the result names one node, and walking up to the
 * nearest element that could listen, deciding whether an event is a `click`, and delivering it
 * are Phase 12's. No **caret or character mapping** inside a text run: a run is one rect and one
 * node here, and "which character is between these two coordinates" is a text-editing question
 * this phase explicitly leaves. No **`visibility`**: the property is registered in
 * `RoboCSSPropertyRegistry.c` and consumed by *nobody* in this runtime -- RoboLayout builds the
 * box and the painter paints it -- so reading it here would make a hit test refuse a point on
 * something that is visibly drawn, which is the one failure this header's fourth rule exists to
 * avoid. The limitation is stated rather than papered over: the phase that teaches layout and
 * paint about `visibility` is the phase that teaches the hit test about it, in one change.
 * No `border-radius`, `clip-path`, masks, filters, transforms, scrolling, zoom or
 * device-pixel-ratio handling, and no alpha-mask or SVG geometry: every one of those needs a
 * shape this build does not have, and testing a *rectangle* against them would be a partial
 * behaviour wearing the name of a full one.
 */
#ifndef ROBO_PAINT_HIT_TEST_H
#define ROBO_PAINT_HIT_TEST_H

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutTree.h"
#include "RoboLayout/RoboLayoutTypes.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintOpacity.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The outcome                                                         */
/* ------------------------------------------------------------------ */

/**
 * What a query concluded.
 *
 * A vocabulary rather than a boolean, and the third and fourth members are why. "Hit" and
 * "missed" are the two answers a caller expects, and separating them is what makes
 * `robo_paint_hit_test_result_is_hit` a real predicate instead of a comparison against a
 * sentinel id. The other two are the states that a boolean cannot express at all and that this
 * module's neighbours both insist on expressing: a query that has not run, and a query that
 * **refused to run** because the tree's geometry is not final.
 *
 * The refusal is not an error and must not be reported as one, which is `RoboPaintContext.h`'s
 * argument restated: nothing went wrong that a caller cannot fix, and the fix -- run a layout
 * pass -- is the same one. A hit test against half-computed geometry is not a *less accurate*
 * answer, it is a **wrong** one: it would report whichever node happened to hold a stale
 * rectangle, and a caller that presented it as a click would be dispatching an event to a node
 * the user is not pointing at. So a stale tree answers `STALE` with **no node**, and the unit
 * test that pins it is the one asserting the difference between "no node was hit" and "no question
 * was asked".
 *
 * `NONE` is 0 so that a cleared result describes the state no query has reached, which is what a
 * caller who zeroed its struct has -- the same "the cleared state is the honest one" rule the
 * paint pass outcome follows.
 */
typedef enum robo_paint_hit_test_outcome {
    /** No query has run: a cleared result. */
    ROBO_PAINT_HIT_NONE = 0,
    /** A node was hit; see @ref robo_paint_hit_test_result::node. */
    ROBO_PAINT_HIT_ON_NODE = 1,
    /** The query ran and the point is on no node in the tree. */
    ROBO_PAINT_HIT_MISS = 2,
    /**
     * The query **refused**: the tree carries a layout bit
     * (`robo_layout_tree_needs_layout`), so no box's geometry is final and there is no
     * answer to give.
     */
    ROBO_PAINT_HIT_STALE = 3
} robo_paint_hit_test_outcome_t;

/** Number of defined outcomes. */
#define ROBO_PAINT_HIT_TEST_OUTCOME_COUNT 4u

/** Non-zero if @p outcome is a defined outcome. */
int robo_paint_hit_test_outcome_is_known(robo_paint_hit_test_outcome_t outcome);

/** Human-readable outcome name; never NULL; "unknown" when undefined. */
const char *robo_paint_hit_test_outcome_name(robo_paint_hit_test_outcome_t outcome);

/* ------------------------------------------------------------------ */
/* Flags                                                               */
/* ------------------------------------------------------------------ */

/*
 * Three facts about a hit that the answer's own fields do not state, one bit each.
 *
 * They are **flags rather than fields** for the reason `RoboLayoutResult.h` gives for its own:
 * each is a yes/no about the hit, three of them can hold at once, and a field per bit would make
 * the record three times as wide to say the same thing. And they are **about the hit** rather
 * than about the query, which is why there is no "was the query stale" flag here: that is the
 * outcome, and a state that can be read two ways is the shape this module refuses everywhere.
 */

/** The hit node is a **text run** rather than a box. */
#define ROBO_PAINT_HIT_FLAG_TEXT (1u << 0)
/**
 * The point is inside the hit node's **content box**, not merely its border box.
 *
 * The fact a caller reaching for the thing the node *contains* -- a text editor, a scroll
 * target, a surface a later phase draws into -- needs before it asks anything else, and the one
 * the three rectangles in the result would otherwise make it compute. It is derivable and
 * derivable *exactly*, which is why it is here as a convenience and asserted against the
 * rectangles rather than trusted: `robo_gfx_rect_contains_point(boxes.content, point)` is the
 * same question, and the suite checks the two agree for every case it raises.
 */
#define ROBO_PAINT_HIT_FLAG_CONTENT (1u << 1)
/**
 * The hit node is drawn at **zero strength**: the opacity in force came to exactly `0`.
 *
 * A fact about the *document* and not about the query -- the node is still the answer, because
 * `opacity` does not govern hit testing (see the header comment) -- and it is worth a flag
 * because it is the one case where "the node you hit is invisible" is true, which a caller
 * presenting a focus ring or a hover state very much wants to know. The number itself is in the
 * result; this is the pre-computed question about it, through the opacity module's own predicate
 * rather than a comparison against zero written here.
 */
#define ROBO_PAINT_HIT_FLAG_TRANSPARENT (1u << 2)

/** Every defined hit-test flag. */
#define ROBO_PAINT_HIT_FLAG_ALL \
    (ROBO_PAINT_HIT_FLAG_TEXT | ROBO_PAINT_HIT_FLAG_CONTENT | ROBO_PAINT_HIT_FLAG_TRANSPARENT)

/* ------------------------------------------------------------------ */
/* The answer                                                          */
/* ------------------------------------------------------------------ */

/**
 * What one query concluded, as a **value** owned by nobody.
 *
 * A snapshot rather than a reference, and the reason is the one 11.9's records give: a caller
 * that kept a pointer into the layout tree would be holding a *borrowed* view of a tree the next
 * mutation can move (`RoboLayoutObject.h`), and a click that changed meaning when a style was
 * recomputed is worse than no answer. So every fact the answer depends on is copied in -- the
 * identity, the two coordinate spaces, the geometry and the two states in force -- and a
 * mutation of the tree afterwards cannot rewrite a conclusion that has already been drawn.
 *
 * The layout object id **and** the DOM handle are both carried, and they are not redundant. The
 * id is what every other door in this module and in RoboLayout takes, and it is the one that
 * validates: a stale id is refused rather than answered. The handle is what the layers *above*
 * consume -- Phase 12's dispatch, the DOM bridge, an application asking "which element is this"
 * -- and recovering it from the id means a walk into the tree and a `robo_layout_object_dom_node`
 * call at every use. So both travel, from the one place that already had to ask.
 *
 * Note what a hit on a **text run** reports: the run's own object id and the **text node's**
 * handle, because that is what the object was built from (`RoboLayoutObject.h`). The *element*
 * that owns the run is the parent box's, which the tree answers in one step -- this record does
 * not guess at "the nearest thing that could listen", because deciding that is event dispatch
 * and this phase does not do it.
 */
typedef struct robo_paint_hit_test_result {
    /** What the query concluded (RoboPaintHitTest.h). */
    robo_paint_hit_test_outcome_t outcome;
    /**
     * The layout object that was hit, or `ROBO_LAYOUT_NODE_ID_NONE`.
     *
     * A well-formed id exactly when @ref outcome is `ON_NODE`, which is the identity
     * `robo_paint_hit_test_result_is_valid` enforces: a result claiming a node it does not
     * have, or a node it did not claim, is a record whose fields disagree about what it is.
     */
    robo_layout_node_id_t node;
    /**
     * The DOM node @ref node was built from: an element for a box, a **text node** for a
     * run. An unformed handle for every outcome but `ON_NODE`.
     */
    robo_html_node_handle_t dom_node;
    /** Whether @ref node is a box or a text run; `NONE` when no node was hit. */
    robo_layout_object_kind_t kind;
    /** How deep the hit node sits: 0 for the root. Meaningful only for `ON_NODE`. */
    size_t depth;
    /** The query's point, in **paint** coordinates (== window coordinates here). */
    robo_layout_point_t point;
    /**
     * @ref point in the hit node's **containing block's** space: `point - origin`.
     *
     * The space RoboLayout wrote the node's geometry in, so a caller comparing the point
     * against a layout rectangle uses this one, and the one the phase's coordinate claim is
     * checkable against. Zero for every outcome but `ON_NODE`.
     */
    robo_layout_point_t local;
    /** The hit node's containing-block content origin, in paint coordinates. */
    robo_layout_point_t origin;
    /**
     * The hit node's three rectangles, in **paint** coordinates.
     *
     * A run and a transparent inline box carry one rectangle repeated three times with
     * `insets_measured` clear -- see `robo_paint_boxes_t` -- which is the same record the
     * painter read them from, so the two cannot disagree about where the node is.
     */
    robo_paint_boxes_t boxes;
    /** The boundary in force **on the hit node itself**, as the painter would emit it. */
    robo_paint_clip_t clip;
    /** The strength in force on the hit node: the product along the path that reached it. */
    robo_paint_opacity_t opacity;
    /** `ROBO_PAINT_HIT_FLAG_*` bits. */
    uint32_t flags;
} robo_paint_hit_test_result_t;

/** Clear @p result to the state no query has reached: outcome `NONE`, no node, no geometry.
 *  Safe on NULL. */
void robo_paint_hit_test_result_init(robo_paint_hit_test_result_t *result);

/**
 * Non-zero when @p result is a record this build may hand to a consumer.
 *
 * Three rules, and each refuses a combination that would otherwise travel a stage further than
 * it should:
 *
 *   a **known outcome**;
 *   the **identity agrees with the outcome** -- `ON_NODE` exactly when there is an id, and a
 *   formed DOM handle exactly then too, because a caller that dispatched on a node id and a
 *   caller that dispatched on a handle must not be able to disagree about whether anything was
 *   hit;
 *   the **geometry is a record** when there is one (`robo_paint_boxes_is_valid`), the
 *   **boundary** is one (`robo_paint_clip_is_valid`) and the **strength** is one
 *   (`robo_paint_opacity_is_valid`) -- so a result carrying a rectangle whose layers cross, or a
 *   clip whose interval runs backwards, is refused here rather than read by whichever layer
 *   happens to consume it next.
 *
 * A `MISS` and a `STALE` are legitimate records with no node, so neither is a failure. Safe on
 * NULL (0).
 */
int robo_paint_hit_test_result_is_valid(const robo_paint_hit_test_result_t *result);

/** Non-zero when a node was hit. The predicate a caller asks rather than comparing ids. Safe on
 *  NULL (0). */
int robo_paint_hit_test_result_is_hit(const robo_paint_hit_test_result_t *result);

/** Non-zero when @p result carries @p flag. Safe on NULL (0). */
int robo_paint_hit_test_result_has_flag(const robo_paint_hit_test_result_t *result, uint32_t flag);

/** The outcome (`NONE` for NULL). */
robo_paint_hit_test_outcome_t robo_paint_hit_test_result_outcome(
    const robo_paint_hit_test_result_t *result);

/** The hit node's id (`ROBO_LAYOUT_NODE_ID_NONE` for NULL). */
robo_layout_node_id_t robo_paint_hit_test_result_node(
    const robo_paint_hit_test_result_t *result);

/** The hit node's kind (`NONE` for NULL). */
robo_layout_object_kind_t robo_paint_hit_test_result_kind(
    const robo_paint_hit_test_result_t *result);

/** The hit node's depth (0 for NULL). */
size_t robo_paint_hit_test_result_depth(const robo_paint_hit_test_result_t *result);

/** The query's point, by value. The origin for NULL. */
void robo_paint_hit_test_result_point(const robo_paint_hit_test_result_t *result,
                                     robo_layout_point_t *out);

/** The point in the hit node's own space, by value. The origin for NULL. */
void robo_paint_hit_test_result_local(const robo_paint_hit_test_result_t *result,
                                     robo_layout_point_t *out);

/** The hit node's three rectangles, by value. Empty boxes for NULL. */
void robo_paint_hit_test_result_boxes(const robo_paint_hit_test_result_t *result,
                                     robo_paint_boxes_t *out);

/** The boundary in force on the hit node, by value. A cleared clip for NULL. */
void robo_paint_hit_test_result_clip(const robo_paint_hit_test_result_t *result,
                                    robo_paint_clip_t *out);

/** The strength in force on the hit node, by value. The fully opaque record for NULL -- so a
 *  caller that reads it without asking draws the answer at full strength, which is what a
 *  document that declared no opacity would give. */
void robo_paint_hit_test_result_opacity(const robo_paint_hit_test_result_t *result,
                                       robo_paint_opacity_t *out);

/** The DOM node of the hit (`ROBO_HTML_NODE_HANDLE_INVALID`-shaped handle for NULL). */
robo_html_node_handle_t robo_paint_hit_test_result_dom_node(
    const robo_paint_hit_test_result_t *result);

/**
 * Field-by-field equality of every field.
 *
 * Two results are equal when they answer the same thing about the same document, which is what
 * the determinism case asserts: the same tree and the same point must produce equal results, and
 * a comparison that ignored a field would make a build that *changed* that field pass. Safe on
 * NULL; two NULLs are equal.
 */
int robo_paint_hit_test_result_equal(const robo_paint_hit_test_result_t *a,
                                    const robo_paint_hit_test_result_t *b);

/* ------------------------------------------------------------------ */
/* What the query saw                                                  */
/* ------------------------------------------------------------------ */

/**
 * What one walk examined, and why it stopped where it did.
 *
 * The paint pass has a stats record, and this is the same device for the same reason: a query
 * that answered "no node" is either a document with nothing there or a build that gave up too
 * early, and the two are told apart by **counters**, not by an argument. Every counter below is
 * a reason the walk did or did not go somewhere, and the identities between them are the ones
 * the suite asserts:
 *
 *   `boxes + texts + unplaced == objects`   every object the walk examined is either a placed
 *                                           box, a placed run, or something nothing placed;
 *
 *   `hits == (the outcome is ON_NODE)`      exactly one node can be the answer, because the walk
 *                                           returns the moment one is found;
 *
 *   `rejected_region + rejected_clip >= 1`  whenever the answer is a MISS -- because reaching
 *                                           the end without a hit means *something* excluded
 *                                           the point, and a build that missed without excluding
 *                                           anything has lost track of its own walk.
 *
 * `objects` counts what the walk **examined**, and that is deliberately not
 * `robo_layout_tree_object_count`: a subtree hanging under a box that nothing placed is
 * unreachable by construction -- no geometry, no origin, no coordinate to test in -- so the walk
 * counts that box as `unplaced` and does not descend, exactly as the painter does when it emits
 * nothing below it. Reading the two numbers against each other is therefore how a reader learns
 * how much of a document the point could even have been over.
 */
typedef struct robo_paint_hit_test_stats {
    /** Objects the walk examined. */
    size_t objects;
    /** Placed boxes examined. */
    size_t boxes;
    /** Placed text runs examined. */
    size_t texts;
    /** Objects examined that nothing placed: no geometry, so nothing to test. */
    size_t unplaced;
    /**
     * Subtrees the point did not fall inside.
     *
     * The walk's first and cheapest rejection, and the one that makes a query proportional to the
     * boxes the point is near rather than to the document: a box's **region** -- its border box
     * grown by the extents its contents reached past it (10.13) -- contains every node beneath
     * it, so a point outside it cannot be inside any of them and the whole subtree is skipped.
     */
    size_t rejected_region;
    /** Subtrees the **boundary in force** excluded: the point is outside a clip the contents are
     *  drawn under. */
    size_t rejected_clip;
    /** 1 when a node was hit, else 0. */
    size_t hits;
    /** Deepest object examined. */
    uint32_t depth_max;
} robo_paint_hit_test_stats_t;

/** Zero @p stats. Safe on NULL. */
void robo_paint_hit_test_stats_init(robo_paint_hit_test_stats_t *stats);

/* ------------------------------------------------------------------ */
/* The query                                                           */
/* ------------------------------------------------------------------ */

/**
 * Find the node under @p point.
 *
 * Walks @p tree once, in **reverse paint order**, and fills @p out with the last node drawn at
 * @p point -- the four rules the header comment states, each of which mirrors the painter:
 * children before their parent, last child before first, a box's own region after its subtree,
 * and a box's contents under the boundary its own `overflow` puts in force while the box itself
 * is under the boundary it was entered with.
 *
 * **The tree is read, never written**, and that is the signature: every door on the left-hand
 * side takes it by `const` pointer, and the only allocation the walk performs is none at all --
 * the query is a pure function of the tree and the point, which is what makes it safe to run
 * before a drawing exists and cheap to run many times for one.
 *
 * @p out_stats is **optional** and may be NULL: a caller that only wants the answer should not
 * have to declare a record to get one. When supplied it is zeroed first and then filled, so a
 * caller reusing one struct across queries never sees a previous query's numbers.
 *
 * @return `ROBO_OK` for every outcome that is an **answer** -- `ON_NODE`, `MISS` and `STALE`
 *         alike, since a refusal is a legitimate conclusion rather than a failure. A nonzero code
 *         means the query could not be *asked*: `ROBO_INVALID_ARGUMENT` for a NULL tree or a NULL
 *         result, and `ROBO_ERROR` with a diagnostic when the walk met a child link naming no
 *         object or a derived box record that is not one -- the two shapes of broken tree, which
 *         are reported rather than skipped for the reason `RoboPaintContext.h` gives: half an
 *         answer to "what did the user click" is not an answer.
 */
robo_result_t robo_paint_hit_test(const robo_layout_tree_t *tree,
                                 robo_layout_point_t point,
                                 robo_paint_hit_test_result_t *out,
                                 robo_paint_hit_test_stats_t *out_stats,
                                 robo_paint_diag_t *diag);

/**
 * Characters one dumped hit-test line can need, excluding the NUL.
 *
 * A bound rather than a guess, derived from the widest line the format below can produce:
 *
 *   `node=` plus ten digits                                              15
 *   ` depth=` plus ten digits                                            17
 *   ` outcome=` plus the longest reason, `outside-clip`                  22
 *   ` kind=text`                                                         10
 *   two rectangles, `x,y,w,h` at eleven characters each plus labels      64
 *   the two spaces that separate the groups                               2
 *                                                                      ----
 *                                                                        130
 *
 * with a line's worth of headroom, so a caller sizing a buffer from this gets a dump it does not
 * have to retry. The suite measures the widest line this build actually produces and asserts it
 * is inside the bound rather than trusting this paragraph, which is the same discipline
 * `RoboPaintDump.h` uses for its own.
 */
#define ROBO_PAINT_HIT_TEST_LINE_MAX 192u

/**
 * Write the whole of one query into @p buffer, as the trace a developer reads.
 *
 * The first line is the **answer** -- the point, the outcome, and the node when there is one --
 * and every line after it is one object the walk examined, in the order it examined them, each
 * naming why it did or did not produce that answer:
 *
 *   hit-test point=120,80 outcome=node node=2 depth=1
 *   node=1 depth=0 box  outcome=enter    border=0,0,800,600 bound=0,0,800,620
 *   node=2 depth=1 box  outcome=enter    border=0,0,200,100 bound=0,0,200,100
 *   node=7 depth=2 text outcome=enter    border=1,5,80,16   bound=1,5,80,16
 *   hit node=7 depth=2 point=10,10
 *   node=3 depth=1 box  outcome=region   border=400,0,200,100 bound=400,0,200,100
 *
 * The reason column is padded to its widest member, `unplaced`, so the rectangles line up down
 * the trace, and the vocabulary is exactly the walk's five decision points:
 *
 *   `enter`     examined -- inside its subtree's bound and inside the boundary in force, so the
 *               walk descended into it (a run is a leaf, so for a run this means it was tested);
 *   `unplaced`  nothing placed it, so there was no rectangle to test and nothing below it was
 *               reachable (the counters count it once, not once per descendant);
 *   `region`    the point was outside the subtree's **bound** -- the border box grown by what
 *               its contents reached past it -- so nothing beneath it could be hit either;
 *   `clip`      the boundary in force excluded the point;
 *   `hit`       this node is **the answer**, on its own line after the node's, carrying the point
 *               again so a reader can see the conversion.
 *
 * There is no `miss` reason, and its absence is deliberate: a box that was entered and produced
 * no hit writes `enter` and then its children are written, so the line that should have been the
 * `hit` one is found by looking for it rather than by looking for a sixth reason -- and a reason
 * for "tested and not the answer" would be on nearly every line of nearly every trace, which is
 * the definition of noise in a diagnostic.
 *
 * The dump runs the query **again** rather than reading a recorded trace, so it is a statement
 * about the build and not about a previous call -- and because the walk is a pure function of the
 * tree and the point, running it twice is safe and produces the same sequence, which is the fact
 * `paint.hit-test.dump` pins by dumping a tree twice.
 *
 * It reports the trace even when the query **refused**: a stale tree writes one line naming the
 * refusal, which is more useful to a reader than an empty buffer.
 *
 * @param tree       the tree to query.
 * @param point      the point, in paint coordinates.
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param out_length non-NULL; receives the length written, excluding the NUL.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument; ROBO_NOT_SUPPORTED when the dump
 *         does not fit, in which case @p out_length receives the length it would have needed --
 *         the contract every `_write` door in this tree uses, so a caller can size a buffer from
 *         the refusal rather than from an estimate.
 */
robo_result_t robo_paint_hit_test_write(const robo_layout_tree_t *tree,
                                       robo_layout_point_t point,
                                       char *buffer,
                                       size_t capacity,
                                       size_t *out_length,
                                       robo_paint_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_HIT_TEST_H */
