/*
 * RoboPaint invalidation (Phase 11.11).
 *
 * Eleven phases of RoboPaint answered "what should be drawn?". This one answers the
 * question that only exists because those answers are *artifacts*: **which pixels of the
 * picture a caller already has are no longer the pixels the document describes?**
 *
 * The module is the sixth in a line of accumulation layers, and its shape is deliberately
 * the least clever one available: a **set of rectangles**, each carrying why it is there
 * and the boundary it was drawn under. Nothing here tracks layers, tiles, a damage tree
 * or a frame history, because nothing above it can consume any of those yet -- `raster` is
 * still 0 and the pass is the caller's. What it does claim is the one thing a caller can
 * act on today: a deterministic, conservative list of the regions a repaint must cover,
 * and a scope saying whether that list is a part of the document or the whole of it.
 *
 * ## The one question, and the four ways it is asked
 *
 * A region enters the set through one of four doors, and the difference between them is
 * what the caller knows:
 *
 *   `set_add_rect`         a caller knows a rectangle changed (a live reload, a
 *                          resource that finished decoding, a rectangle of its own);
 *
 *   `set_add_change`       a caller knows a rectangle *was* one thing and *is* another --
 *                          the old and new bounds, unioned, which is the door that makes
 *                          stale pixels impossible to leave behind;
 *
 *   `set_mark_*`           a caller knows *which box* changed but not where it is, so it
 *                          hands over the paint list and a node id and lets this module
 *                          read the geometry out of the record -- `mark_node` for one
 *                          box, `mark_subtree` for a box and its descendants (the shape an
 *                          element's `opacity` needs, since it changes what its whole
 *                          subtree composites to), `mark_node_change` for both artifacts of
 *                          a mutation that is still being applied;
 *
 *   `set_from_*`           a caller knows nothing except that time passed: two paint
 *                          artifacts (record lists, or the display lists a rasteriser
 *                          would consume) are compared positionally and every position
 *                          that differs contributes its regions, and
 *                          `set_from_stats` reads the one number 11.1 left for exactly
 *                          this phase (`PaintStats.styleOnly`).
 *
 * ## What a region is, and what it is not
 *
 * A region is a **value**: a rectangle in paint coordinates (one page space, the space
 * every other rectangle in this module is in), the node it came from where one is known,
 * a reason, the boundary that was in force, and two flags. It carries no pointer, no
 * borrow of a tree and no colour, so a set outlives the layout tree it was produced from
 * -- which is the phase's whole use case, since the invalidation is produced *before* the
 * tree is mutated and consumed *after*.
 *
 * A region is **not** a promise that those pixels changed. It is a promise that a
 * repaint of it is *sufficient*: every door widens rather than narrows when it is not
 * certain (see `set_add_rect`'s rule for an axis with no boundary, and
 * `set_from_stats`), because the two mistakes are not symmetric -- a region that is too
 * large costs a few pixels of work, and a region that is too small leaves pixels of the
 * previous picture on screen forever.
 *
 * ## What this phase does not do
 *
 * It does not rasterise (`raster` stays 0), it does not patch a display list (the list is
 * rebuilt; the set says how little of it a caller *may* re-execute), it does not schedule
 * frames, it does not track damage per compositor layer, and it does not read a clock.
 * `PaintCapabilities.paintInvalidation` is the field this phase flips, and it
 * claims exactly what §`paint/docs/Invalidation.md` argues: regions, reasons and a scope,
 * deterministic, conservative, and safe to act on with the rasteriser this build has.
 */
#ifndef ROBO_PAINT_INVALIDATION_H
#define ROBO_PAINT_INVALIDATION_H

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutTree.h"

#include "RoboPaint/RoboPaintContext.h"
#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintDisplayCommand.h"
#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintList.h"
#include "RoboPaint/RoboPaintOperation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Why a region needs repainting                                       */
/* ------------------------------------------------------------------ */

/**
 * Why a region is in the set: the *reason*, not the mechanism.
 *
 * One reason per region and not one per property, for the reason RoboLayout's reasons
 * give: the answer a human reads in a dump is "what happened?", and a row per property
 * would have to be a string, which would make the record own storage and the module own
 * lifetimes for a fact only a person reads.
 *
 * Every row here has a **producer in this file**, which is the rule `RoboPaintTypes.h`
 * states for an enumerator: a name with no code behind it is a promise, not a vocabulary.
 * The rows that look conspicuous by their absence are argued where the classifier is
 * (`RoboPaintInvalidationReasonStyle` below and
 * `PaintInvalidationClassifyProperty`), and there is one: **there is no
 * `VISIBILITY`**, because no layer in this runtime consumes `visibility`, so a change to
 * it cannot change a pixel -- see `paint/docs/Invalidation.md` §9 and the case
 * `paint.invalidation.visibility_is_not_consumed`.
 */
typedef enum PaintInvalidationReason {
    /** Nothing: a cleared record's value, and the reason a region-less set has. */
    RoboPaintInvalidationReasonNone = 0,
    /**
     * A paint-visible change **somewhere** in the document that the producer cannot place:
     * the shape `set_from_stats` produces when the traversal counted paint-only staleness
     * but not where it was.
     */
    RoboPaintInvalidationReasonStyle = 1,
    /** A box's background changed (or a box gained or lost one). */
    RoboPaintInvalidationReasonBackground = 2,
    /** A border's colour, style or used width changed: a band, its shape or its extent. */
    RoboPaintInvalidationReasonBorder = 3,
    /** A run's characters, colour or font inputs changed: glyphs, and the line they sit on. */
    RoboPaintInvalidationReasonText = 4,
    /** A document's picture changed: a resource resolved, failed, or was replaced. */
    RoboPaintInvalidationReasonImage = 5,
    /** A box moved or resized: the *old* rectangle is as stale as the new one. */
    RoboPaintInvalidationReasonGeometry = 6,
    /** An element's strength changed, which changes what its whole subtree composites to. */
    RoboPaintInvalidationReasonOpacity = 7,
    /** The boundary in force changed: what is drawn there, and what is now cut away. */
    RoboPaintInvalidationReasonClip = 8,
    /** The sequence itself changed: a box was inserted, removed, or replaced in place. */
    RoboPaintInvalidationReasonContent = 9,
    /** The containing block changed (the viewport, or its constraints): the whole extent. */
    RoboPaintInvalidationReasonViewport = 10
} PaintInvalidationReason;

/** Number of defined reasons. */
#define RoboPaintInvalidationReasonCount 11u

/** Non-zero if @p reason is a defined reason. */
int PaintInvalidationReasonIsKnown(PaintInvalidationReason reason);

/** Human-readable reason name; never NULL; "unknown" when undefined. */
const char *PaintInvalidationReasonName(PaintInvalidationReason reason);

/* ------------------------------------------------------------------ */
/* What a change costs                                                 */
/* ------------------------------------------------------------------ */

/**
 * What changing one computed property costs the **picture**.
 *
 * Three outcomes and not two, for the reason `LayoutStyleEffect` gives: "this
 * build draws nothing from it" and "this build has never heard of it" are different
 * statements about a property, and only the first belongs to a name a stylesheet may
 * legitimately write.
 *
 * `PAINT` is the answer that means *a repaint of a region is owed and the geometry is
 * sound*; `LAYOUT` means the picture changed because a coordinate did, so a caller owes a
 * layout pass **and then** a repaint of the region — this module never runs one, and
 * `set_from_stats`'s sibling door for the layout side is the caller's `robo_layout_tree_*`
 * marking, which already exists and is not duplicated here (10.14).
 */
typedef enum PaintInvalidationClass {
    /** This build draws nothing from it: a change repaints nothing. */
    RoboPaintInvalidationClassNone = 0,
    /** A repaint of a region, with the geometry untouched. */
    RoboPaintInvalidationClassPaint = 1,
    /** The geometry moved: layout first, then the region. */
    RoboPaintInvalidationClassLayout = 2
} PaintInvalidationClass;

/** Number of defined classes. */
#define RoboPaintInvalidationClassCount 3u

/** Non-zero if @p klass is a defined class. */
int PaintInvalidationClassIsKnown(PaintInvalidationClass klass);

/** Human-readable class name; never NULL; "unknown" when undefined. */
const char *PaintInvalidationClassName(PaintInvalidationClass klass);

/**
 * What changing @p propertyName costs **this build's picture**.
 *
 * The answer is *derived* from RoboLayout's classifier rather than restated: a property
 * layout measures answers `LAYOUT`, and everything else is asked of the small table of
 * properties this module **draws from** (`paint_invalidation_drawn_property` in the
 * source, which is `color`, the `background` family, the four `border-*-color`s, their
 * shorthand and `opacity`). A property no layer draws from answers `NONE`.
 *
 * That second table is the whole reason this door exists rather than a re-export of
 * layout's. RoboLayout's PAINT row means "drawn, not measured" -- which is a statement
 * about *layout* -- and three of its members are drawn by **nobody** in this runtime:
 * `visibility` (registered, consumed by no layer), `text-decoration` and
 * `background-image`. Answering `PAINT` for those would make a change that produces an
 * *identical picture* request a repaint, which is the one thing a paint-invalidation
 * system must not do casually -- the completion criteria 11.11 is held to say "no-op
 * changes do not unnecessarily require repaint". So they answer `NONE`, and the cases
 * that pin it (`paint.invalidation.unconsumed_properties`) are the place a later goal
 * finds out that consuming one of them means adding a row here.
 *
 * @p propertyName may be NULL and is matched case-insensitively, because a caller
 * reaching this from a stylesheet has a spelling from a document. An unknown or NULL name
 * answers NONE, which is the *sound* answer rather than the lazy one: this module's table
 * names every property it draws from, so a name outside it cannot have changed a pixel.
 */
PaintInvalidationClass PaintInvalidationClassifyProperty(
    const char *propertyName);

/**
 * Why a change to @p propertyName needs repainting.
 *
 * The reason for the rows this module draws from (`background` → `BACKGROUND`, `color` →
 * `TEXT`, a `border-*-color` → `BORDER`, `opacity` → `OPACITY`); **`GEOMETRY`** for every
 * property RoboLayout measures, because the fact a repaint has to cover there is that the box
 * is somewhere else rather than what it is made of; and `NONE` for everything else, including
 * a name neither module has heard of -- a reason is only meaningful beside a region, and
 * nothing here produces one for a property no drawing reads.
 */
PaintInvalidationReason PaintInvalidationReasonForProperty(
    const char *propertyName);

/* ------------------------------------------------------------------ */
/* How much of the document a set covers                               */
/* ------------------------------------------------------------------ */

/**
 * Whether a set asks for nothing, for a part of the document, or for the whole of it.
 *
 * The distinction the phase's brief asks for by name, and the one a caller acts on: NONE
 * means the picture is current and no work is owed, PARTIAL means the regions are a
 * smaller answer than the extent, and FULL means the set contains at least one region
 * produced as a **conservative whole-extent** answer -- the case a caller must not
 * mistake for "one box changed", because a region that is the whole extent is exactly what
 * "the producer could not tell where" looks like.
 */
typedef enum PaintInvalidationScope {
    /** No repaint is owed: the set is empty. */
    RoboPaintInvalidationScopeNone = 0,
    /** A part of the extent is stale; the set's regions are the answer. */
    RoboPaintInvalidationScopePartial = 1,
    /** At least one region is a conservative whole-extent answer. */
    RoboPaintInvalidationScopeFull = 2
} PaintInvalidationScope;

/** Number of defined scopes. */
#define RoboPaintInvalidationScopeCount 3u

/** Non-zero if @p scope is a defined scope. */
int PaintInvalidationScopeIsKnown(PaintInvalidationScope scope);

/** Human-readable scope name; never NULL; "unknown" when undefined. */
const char *PaintInvalidationScopeName(PaintInvalidationScope scope);

/* ------------------------------------------------------------------ */
/* One region                                                          */
/* ------------------------------------------------------------------ */

/**
 * One rectangle that must be repainted, why, and the boundary it was drawn under.
 *
 * A value record owned by nobody, like every other record in this module -- and unlike
 * them it is the **first** one whose whole purpose is to survive a mutation: it is
 * produced from a layout tree that is about to change and consumed after the change, so
 * it carries no reference to that tree, no borrowed style and no live geometry. A
 * `LayoutNodeId` is a number, not a handle, and an id that no longer names a box
 * is exactly the removal case this record exists to describe.
 *
 * `narrowed` and `full` are two facts a consumer must keep apart, which is why neither is
 * inferred from the rectangle:
 *
 *   `narrowed`  the rectangle **is** the intersection with `clip`, so a consumer that has
 *               the same boundary may treat it as exact. When it is clear, the rectangle
 *               is *conservative*: an axis of the boundary has no extent to intersect
 *               with (`overflow-x: visible`), so this module kept the whole rectangle
 *               rather than inventing a bound for the free axis -- the rule
 *               `PaintClipRect` states by refusing to answer.
 *
 *   `full`      the rectangle is a **whole-extent** answer: the producer knew that
 *               *something* became stale and could not say where, so the region is the
 *               document rather than a box. `PaintInvalidationSetScope` reads
 *               these and nothing else.
 */
typedef struct PaintInvalidationRegion {
    /** The rectangle to repaint, in paint coordinates (one page space). */
    LayoutRect rect;
    /** The box it came from, or `RoboLayoutNodeIdNone` when the region is not one box. */
    LayoutNodeId node;
    /** Why the pixels there are stale. */
    PaintInvalidationReason reason;
    /** The boundary in force where the region was produced. */
    PaintClip clip;
    /** Non-zero when @ref rect is already the intersection with @ref clip. */
    int narrowed;
    /** Non-zero when @ref rect is a conservative whole-extent answer. */
    int full;
} PaintInvalidationRegion;

/** Clear @p region: an empty rectangle, no reason, no boundary. Safe on NULL. */
void PaintInvalidationRegionInit(PaintInvalidationRegion *region);

/**
 * Non-zero when @p region is a record this build may read.
 *
 * Four conditions: a rectangle with a non-negative size (an empty one is fine -- a
 * zero-area region is *produced* and dropped by the set's own rule, and a consumer holding
 * one has a record that means "nothing to repaint here"), a defined reason, a valid
 * boundary, and `narrowed` implying a boundary that is actually active -- a rectangle that
 * claims to have been narrowed by nothing is arithmetic that went wrong somewhere.
 */
int PaintInvalidationRegionIsValid(const PaintInvalidationRegion *region);

/** Field-for-field equality, including both flags. Safe on NULL. */
int PaintInvalidationRegionEqual(const PaintInvalidationRegion *a,
                                         const PaintInvalidationRegion *b);

/* ------------------------------------------------------------------ */
/* What a set costs                                                    */
/* ------------------------------------------------------------------ */

/**
 * What the doors did, for a reader and for a test.
 *
 * Every field is a count of something this module *decided*, and the identities are the
 * reason they are here: `added = narrowed + conservative` says every region was classified
 * exactly once, `compared = matched + differed` says the diff's walk accounted for every
 * position it visited, and `excluded` is the one number that is *not* in the set -- the
 * regions produced and then dropped because the boundary excluded every pixel of them, or
 * because the rectangle had no area. A reader who saw only `added` would have no way to
 * tell "nothing changed" from "everything changed outside the boundary".
 */
typedef struct PaintInvalidationStats {
    /** Regions in the set (`PaintInvalidationSetCount`). */
    size_t added;
    /** Added regions whose rectangle is the intersection with their boundary. */
    size_t narrowed;
    /** Added regions kept whole, because the boundary has an axis with no extent. */
    size_t conservative;
    /** Produced regions dropped: the boundary excluded them, or they had no area. */
    size_t excluded;
    /** Added regions that are conservative whole-extent answers. */
    size_t full;
    /** `set_add_change` calls that marked an old-and-new rectangle as one region. */
    size_t unions;
    /** Regions that covered a box's **subtree** rather than one box (`set_mark_subtree`). */
    size_t spans;
    /** Records or commands a diff walked where both artifacts had an entry. */
    size_t compared;
    /** Of @ref compared, the positions whose artifacts were equal. */
    size_t matched;
    /** Of @ref compared, the positions whose artifacts were not. */
    size_t differed;
    /**
     * Positions only the **after** artifact had (a box or a run that appeared). Counted for
     * every position, so a structural one is in here too though it produced no region.
     */
    size_t inserted;
    /** Positions only the **before** artifact had (a box or a run that went away). */
    size_t removed;
} PaintInvalidationStats;

/** Zero @p stats. Safe on NULL. */
void PaintInvalidationStatsInit(PaintInvalidationStats *stats);

/** Field-for-field equality. Safe on NULL. */
int PaintInvalidationStatsEqual(const PaintInvalidationStats *a,
                                        const PaintInvalidationStats *b);

/* ------------------------------------------------------------------ */
/* The set                                                             */
/* ------------------------------------------------------------------ */

/** The first block a set takes, so `PaintInvalidationSetAt` answers before a mark. */
#define RoboPaintInvalidationInitialCapacity 8u

/** An owned, growable, deterministic list of invalidation regions. */
typedef struct PaintInvalidationSet PaintInvalidationSet;

/**
 * Create an empty set.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p outSet; RoboOutOfMemory with a
 *         `RoboPaintDomainInvalidation` diagnostic when the set or its first block
 *         could not be taken.
 */
Result PaintInvalidationSetCreate(PaintInvalidationSet **outSet,
                                                PaintDiag *diag);

/** Destroy @p set and the regions it owns. Safe on NULL. */
void PaintInvalidationSetDestroy(PaintInvalidationSet *set);

/** Drop every region and reset the record. Safe on NULL. The allocation is kept. */
void PaintInvalidationSetClear(PaintInvalidationSet *set);

/** Regions in @p set (0 for NULL). */
size_t PaintInvalidationSetCount(const PaintInvalidationSet *set);

/** Capacity, which is at least `RoboPaintInvalidationInitialCapacity` (0 for NULL). */
size_t PaintInvalidationSetCapacity(const PaintInvalidationSet *set);

/** Non-zero when @p set holds no region (also true for NULL: nothing owed). */
int PaintInvalidationSetIsEmpty(const PaintInvalidationSet *set);

/**
 * Region @p index, by value, in the order it was added.
 *
 * The order is the contract a consumer iterates on and the reason this module has no
 * hash table anywhere: regions are appended in the order their producers ran, that order
 * is a function of the document and the artifacts (never of an address, an allocation or
 * a clock), and two runs over the same transition therefore produce equal sets -- which is
 * what `PaintInvalidationSetEqual` is for.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p set or @p outRegion; RoboNotFound
 *         when @p index is past the end.
 */
Result PaintInvalidationSetAt(const PaintInvalidationSet *set,
                                            size_t index,
                                            PaintInvalidationRegion *outRegion,
                                            PaintDiag *diag);

/** Field-for-field equality of two sets: same count and same regions in the same order. */
int PaintInvalidationSetEqual(const PaintInvalidationSet *a,
                                      const PaintInvalidationSet *b);

/** The scope @p set asks for (NONE for NULL or an empty set). */
PaintInvalidationScope PaintInvalidationSetScope(
    const PaintInvalidationSet *set);

/**
 * The rectangle containing every region of @p set.
 *
 * The coarse answer, and the one a caller with a rasteriser that can only be handed one
 * rectangle should use: a union of *all* the regions is always **sufficient** (it can only
 * be too large), while merging the regions into a *smaller* number of tighter rectangles is
 * an optimization this phase deliberately does not attempt -- it needs a region tree, and a
 * subtle merge bug is a stale pixel that no test of counts would catch. A caller that wants
 * the tight list iterates `set_at` and hands each region to the rasteriser.
 *
 * @return RoboOk and @p outRect; RoboInvalidArgument for a NULL argument; RoboNotFound
 *         for an empty set or NULL @p set (nothing to bound).
 */
Result PaintInvalidationSetBounds(const PaintInvalidationSet *set,
                                                LayoutRect *outRect);

/** Copy @p set's record into @p outStats. Safe on NULL @p outStats. */
void PaintInvalidationSetStats(const PaintInvalidationSet *set,
                                      PaintInvalidationStats *outStats);

/**
 * Add @p region, which must be a record this build may read.
 *
 * The one door every other adding door goes through, and the only place the set's rules are
 * written: a region with **no area** is refused by the module's own exclusion rule
 * (`excluded` counts it and nothing is appended), so a caller cannot make a set claim an
 * area that cannot hold a pixel. Refusing to *store* it is not the same as refusing the
 * call: a zero-area invalidation is a conclusion this build reaches all the time -- a box
 * with no size changed nothing -- and it is success with nothing added.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a region
 *         `PaintInvalidationRegionIsValid` rejects; RoboOutOfMemory with a
 *         diagnostic when the set could not grow.
 */
Result PaintInvalidationSetAddRegion(PaintInvalidationSet *set,
                                                    const PaintInvalidationRegion *region,
                                                    PaintDiag *diag);

/**
 * Add "the rectangle @p rect needs repainting, because @p reason", under @p boundary.
 *
 * The explicit door: a caller that knows a rectangle changed (a resource finished
 * decoding, a theme was swapped, a window region was exposed) says so here, and the box it
 * belongs to, when it knows one.
 *
 * **The boundary is read, never invented.** When @p boundary is non-NULL and names both
 * axes, the region is its intersection with @p rect and `narrowed` is set -- a change
 * inside an `overflow: hidden` ancestor cannot have touched a pixel outside it, and
 * `PaintClipRect` is the door that says so. When either axis has no extent to
 * intersect with, the rectangle is kept **whole** and `conservative` counts it: inventing a
 * bound for a free axis would exclude pixels this build does draw, which is the one failure
 * a repaint list cannot recover from. A boundary the module cannot read is treated the same
 * way, for the same reason.
 *
 * A rectangle, or an intersection, with no area is success with nothing added
 * (`excluded`), and a NULL @p boundary means "no boundary in force".
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p set, a NONE reason, an undefined
 *         reason, or a rectangle with a negative size; RoboOutOfMemory with a diagnostic.
 */
Result PaintInvalidationSetAddRect(PaintInvalidationSet *set,
                                                  LayoutRect rect,
                                                  LayoutNodeId node,
                                                  PaintInvalidationReason reason,
                                                  const PaintClip *boundary,
                                                  PaintDiag *diag);

/**
 * Add "this rectangle **was** @p before and **is** @p after".
 *
 * The stale-pixel door, and the shape the brief asks for by name: a box that moved leaves
 * the old rectangle holding the previous picture, so invalidating the union of the two is
 * the only answer that both repaints where the box is and clears where it was. One region --
 * the union, so a consumer with a single rectangle still gets the whole answer -- with
 * `unions` counting it.
 *
 * The union is taken in 64 bits and saturated, like every other accumulation in this
 * module: two bounded coordinates have an unbounded difference, and a wrapped union would be
 * a rectangle somewhere else on the page rather than an obvious failure.
 *
 * @return RoboOk; RoboInvalidArgument as `set_add_rect`; RoboOutOfMemory with a
 *         diagnostic.
 */
Result PaintInvalidationSetAddChange(PaintInvalidationSet *set,
                                                    LayoutRect before,
                                                    LayoutRect after,
                                                    LayoutNodeId node,
                                                    PaintInvalidationReason reason,
                                                    const PaintClip *boundary,
                                                    PaintDiag *diag);

/* ------------------------------------------------------------------ */
/* Marking what a paint artifact says                                     */
/* ------------------------------------------------------------------ */

/**
 * Mark the box @p operation was emitted for: its **border box**, in paint coordinates.
 *
 * The record carries both coordinate spaces and the accumulated origin (11.1), so the
 * rectangle layout wrote is already the page-space rectangle a region needs -- nothing here
 * re-walks the tree or re-derives an origin, which is what makes this door cheap enough to
 * call per box. The box's own boundary comes from the record's `clip`, so a box inside a
 * clipped ancestor narrows the same way an explicit `set_add_rect` does.
 *
 * The border box is a **superset** of what a record draws -- a border side's band, a
 * background's fill and a run's glyphs are all inside it -- which is the right direction for
 * a mark that knows the box and not the drawing.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments, an operation that is not a
 *         record, or an undefined reason; RoboOutOfMemory with a diagnostic.
 */
Result PaintInvalidationSetMarkOperation(PaintInvalidationSet *set,
                                                        const PaintOperation *operation,
                                                        PaintInvalidationReason reason,
                                                        PaintDiag *diag);

/**
 * Mark box @p node in @p list: every record that names it.
 *
 * The door a caller with a paint list and a node id uses, and the reason it takes the list
 * rather than the tree is the same reason `set_mark_operation` exists: the geometry is in the
 * record, and a caller that re-derived it from layout would be building a second paint pass
 * to ask a question the first one already answered.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments, a NONE id, or an undefined
 *         reason; RoboNotFound with a diagnostic when no record in @p list names @p node;
 *         RoboOutOfMemory.
 */
Result PaintInvalidationSetMarkNode(PaintInvalidationSet *set,
                                                   const PaintList *list,
                                                   LayoutNodeId node,
                                                   PaintInvalidationReason reason,
                                                   PaintDiag *diag);

/**
 * Mark box @p node **and its descendants** in @p list, as one region covering their union.
 *
 * The door an element's `opacity` needs, and the reason it exists rather than a loop around
 * `mark_node`: a strength is applied to everything a subtitle composites to, so a change to
 * it invalidates a *region that contains the subtree*, not the element's own rectangle --
 * invalidating only the box would leave every descendant drawn at the old strength on screen.
 *
 * The region is the union of the records the subtree produced, which is **sufficient and may
 * be larger than the subtree's exact silhouette** (overlapping children, and gaps inside the
 * box, are included): this phase's rule everywhere is that a region may be too large and may
 * never be too small.
 *
 * @return RoboOk; RoboInvalidArgument as `set_mark_node`; RoboNotFound when no record
 *         names @p node; RoboOutOfMemory.
 */
Result PaintInvalidationSetMarkSubtree(PaintInvalidationSet *set,
                                                      const PaintList *list,
                                                      LayoutNodeId node,
                                                      PaintInvalidationReason reason,
                                                      PaintDiag *diag);

/**
 * Mark box @p node in **both** artifacts of a mutation: what it was, and what it is.
 *
 * The door for the moment in the middle of a change, when the before and after lists are
 * both in hand and the question is "what must be repainted once this is applied". Each side
 * contributes its own region under its own boundary, so a box that moved **and** whose
 * ancestor's boundary changed produces two regions rather than one rectangle narrowed by a
 * boundary that only describes one of them.
 *
 * Either side may be NULL, but not both, and a side in which no record names @p node is
 * simply the removal or the insertion case -- which is why an absent node is not a failure
 * here the way it is in `set_mark_node`.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p set, both lists NULL, a NONE id, or an
 *         undefined reason; RoboNotFound when **neither** side names @p node;
 *         RoboOutOfMemory.
 */
Result PaintInvalidationSetMarkNodeChange(PaintInvalidationSet *set,
                                                          const PaintList *before,
                                                          const PaintList *after,
                                                          LayoutNodeId node,
                                                          PaintInvalidationReason reason,
                                                          PaintDiag *diag);

/**
 * Mark the **whole extent** @p extent: the conservative answer.
 *
 * The door for "something changed and this build cannot bound where". It adds one region
 * covering @p extent with `full` set, so `PaintInvalidationSetScope` reports FULL and
 * a caller can tell a conservative answer from a precise one -- which is the entire reason
 * the flag exists rather than a scope derived from a reason string.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p set, an @p extent with a negative size,
 *         or an undefined @p reason; RoboOutOfMemory.
 */
Result PaintInvalidationSetMarkFull(PaintInvalidationSet *set,
                                                   LayoutRect extent,
                                                   PaintInvalidationReason reason,
                                                   PaintDiag *diag);

/* ------------------------------------------------------------------ */
/* Comparing two artifacts                                             */
/* ------------------------------------------------------------------ */

/**
 * Fill @p set with the regions in which two **record lists** disagree.
 *
 * The door for a caller that has no idea what changed: build the list, mutate the document,
 * lay it out again, build the list again, and ask this. The walk is **positional** and the
 * lists are both sequences of a regular document (11.1's structural convention), so a
 * position where both have a record is compared with `PaintOperationEqual` and a
 * position where only one does is an insertion or a removal.
 *
 * What a difference *means* is read from the records rather than guessed:
 *
 *   a different **node** at the position is `CONTENT` -- the sequence itself changed;
 *   a different **clip** is `CLIP`; a different **opacity** is `OPACITY`;
 *   different **boxes** are `GEOMETRY`;
 *   otherwise the reason is the record's own kind (`BACKGROUND`, `BORDER`, `TEXT`, `IMAGE`),
 *   and `STYLE` for a drawing record that differs in some other way.
 *
 * Only records that **paint** produce regions. A structural record -- a box marker, the
 * content boundary, a clip or opacity scope -- draws nothing, and a region out of its box
 * would be a rectangle with nothing stale in it; the scopes are not lost by the rule, because
 * every record inside a scope carries that scope's boundary and strength in its own fields, so
 * a clip that moved and an element that faded both reach the set through the drawings they
 * bound. The structural positions are still **counted** (`compared`, `matched`, `differed`,
 * `inserted`, `removed` describe the walk); they just contribute no rectangle.
 *
 * Each side contributes **its own** rectangle under **its own** boundary, so the old
 * rectangle is narrowed by the boundary it was drawn under and the new one by the boundary it
 * will be drawn under -- the two are not interchangeable, and a single union narrowed by one
 * of them would be wrong in exactly the case clipping exists for. `matched`/`differed`
 * account for every position the walk visited.
 *
 * A NULL list is treated as **empty** rather than as an error: "there was no previous
 * artifact" is a real call (the first frame), and the answer is that every position of the
 * other list is an insertion.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p set; RoboOutOfMemory with a
 *         diagnostic when the set could not grow.
 */
Result PaintInvalidationSetFromLists(PaintInvalidationSet *set,
                                                    const PaintList *before,
                                                    const PaintList *after,
                                                    PaintDiag *diag);

/**
 * The same comparison one stage further along, over **display lists**.
 *
 * The artifact a rasteriser actually consumes (§`paint/docs/DisplayList.md`), so this is the
 * door a caller that has already built both display lists should use: each command carries
 * the **one resolved rectangle** it acts on, which is a *tighter* answer than the record
 * path's border box (a border band is a band, not the box it sits in), and the reasons are
 * read from the command kinds.
 *
 * The trade is the one 11.3 made: a display command has collapsed the box model and the
 * record's history into a rectangle, so a difference here can no longer be attributed to a
 * property -- the reasons are therefore coarser (`BACKGROUND`, `BORDER`, `TEXT`, `IMAGE`,
 * `CLIP`, `OPACITY`, `CONTENT`, `GEOMETRY`), and a caller that wants to know *which
 * declaration* moved a box should compare the record lists instead.
 *
 * Only commands that **draw** produce regions, by the same rule the record walk follows.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p set; RoboOutOfMemory with a
 *         diagnostic.
 */
Result PaintInvalidationSetFromDisplayLists(
    PaintInvalidationSet *set,
    const PaintDisplayList *before,
    const PaintDisplayList *after,
    PaintDiag *diag);

/**
 * Read a paint pass's **`styleOnly`** count and answer conservatively.
 *
 * The integration 11.1 promised: `PaintStats.styleOnly` counts the visited boxes
 * whose only staleness is a paint-visible change, which is *exactly* the set a
 * paint-invalidation system must repaint -- but only as a **count**, with no node and no
 * rectangle attached. So this door is deliberately the coarse one:
 *
 *   `styleOnly == 0`  nothing is owed. The set is left as it is and nothing is added,
 *                      which is the answer a frame with no paint-visible change deserves;
 *   `styleOnly > 0`   **one** region covering @p extent, `full`, with reason `STYLE`.
 *
 * `extent` is the caller's, because this module does not own a viewport: it is whatever the
 * caller considers the drawable area (the surface, the window, the page), and passing a
 * rectangle that is too small is the one mistake here that can leave stale pixels -- which is
 * why the door documents it rather than defaulting it.
 *
 * A caller that also knows *which* box changed should use `set_mark_node` (with the pre-pass
 * list in hand) and get a region instead of the extent.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p set or a NULL @p stats; RoboOutOfMemory
 *         with a diagnostic.
 */
Result PaintInvalidationSetFromStats(PaintInvalidationSet *set,
                                                    const PaintStats *stats,
                                                    LayoutRect extent,
                                                    PaintDiag *diag);

/* ------------------------------------------------------------------ */
/* The developer's view                                                */
/* ------------------------------------------------------------------ */

/**
 * Write the whole of @p set into @p buffer, one line per region, plus a summary line.
 *
 * The dump answers the four questions a person debugging a repaint asks, in the order they
 * ask them: which region, why, was it narrowed, and is it the whole extent --
 *
 *   `0 reason=opacity rect=20,20,200,100 node=7 clip=visible/visible narrowed=no full=no`
 *   `reason=geometry rect=20,20,120,60 node=3 clip=hidden/visible narrowed=yes full=no`
 *   `scope=full regions=3 narrowed=1 conservative=2 excluded=0 full=1`
 *
 * The rectangles are printed **without** the `old`/`new` pair of the brief's example, and
 * that is a decision rather than an omission: the pair does not survive into a region by
 * design -- `set_add_change` stores their union and `set_mark_node_change` stores two
 * regions -- so a dump that printed both fields would be describing a record this module does
 * not have.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboNotSupported when the
 *         dump does not fit, in which case @p outLength receives the length it would have
 *         needed -- so a caller can size a buffer from the refusal rather than an estimate,
 *         the contract every `_write` door in this tree uses.
 */
Result PaintInvalidationSetWrite(const PaintInvalidationSet *set,
                                               char *buffer,
                                               size_t capacity,
                                               size_t *outLength,
                                               PaintDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_INVALIDATION_H */
