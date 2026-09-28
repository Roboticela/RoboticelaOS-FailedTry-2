/*
 * RoboLayout fragmentation (Phase 19.7).
 *
 * A **fragmentainer** is a box that splits its contents into a sequence of
 * *fragments*: a column of a multi-column container, a page, a region. Fragmentation
 * is the question of *where* the splits fall, and this module is the policy that
 * answers it -- one boundary at a time, as a function of the two declarations that
 * meet there and the room the container has left.
 *
 * It is a **policy**, not a pass, and the split is the same one 10.12 and 14.0 make
 * for positioning: RoboLayoutAbsolute.h answers "what is this box placed against,
 * how big is it, where does it go" and hands a record back for the pass to apply;
 * this header answers "may the contents break here, must they, and why", and hands a
 * decision back for the pass to act on. Nothing below allocates, caches or walks a
 * tree, so the same question asked twice is the same answer -- which is what makes
 * the rules assertable one at a time rather than through a document's geometry.
 *
 * The vocabulary comes from RoboCSS and is not redefined here
 *
 * `break-before`, `break-after` and `break-inside` are RoboCSS's closed sets
 * (RoboCSSFragment.h), and `orphans`/`widows` are its two inherited line floors. This
 * module **reads** those values and classifies them; it never re-parses a keyword and
 * never keeps a second copy of a vocabulary. That is why the classifications live in
 * RoboCSSFragment.h's table (`forced`, `avoided`, `columnBreak`, `pageBreak`) and this
 * file only decides what they mean *at a container of a given kind*.
 *
 * The four things the policy decides
 *
 *   a **boundary**   the edge between two boxes: the earlier box's `break-after` and
 *                    the later box's `break-before` meet there, and CSS's rule is that
 *                    a **forced** break wins over an `avoid`, wherever the two come
 *                    from. That is why the decision carries both an action and a
 *                    *reason*: "a break happens here because one declaration forced it"
 *                    and "because the container ran out of room" produce the same
 *                    geometry and are entirely different documents.
 *
 *   an **interior**  whether a refusal applies *inside* a box. `break-inside: avoid`
 *                    is one statement, but which container it is about is the
 *                    container's answer -- `avoid-column` says nothing to a page
 *                    container -- so the door asks the container's kind alongside it.
 *                    CSS's `break-inside` has no forced values at all (RoboCSSFragment.h
 *                    states why: an interior boundary has no next fragmentainer for a
 *                    break to begin one), which is why this half of the policy is one
 *                    question rather than three.
 *
 *   a **line floor**  `orphans` and `widows`: the minimum number of lines a break may
 *                    leave at the end of one fragment and carry to the start of the
 *                    next. This is the one place the policy needs a *count* rather than
 *                    a declaration -- the number of lines the box would put on either
 *                    side of the candidate break -- and 10.6's line boxes are what
 *                    supplies it.
 *
 *   the **kind gate**  a break names a container (`page`, `column`, `region`) or is the
 *                    generic `always`. A `column` break means nothing to a page
 *                    container, so a forced break is *honoured* only by the container
 *                    whose kind it names -- and `region` is honoured by **no** container
 *                    this build lays out, which the decision records rather than
 *                    rounding to a column break.
 *
 * What this module is not
 *
 * Not a fragment generator: it produces no geometry, places no box and writes no
 * `LayoutResult`. `LayoutFragment` is a *record of what a pass did* -- an ordinal, a
 * source box and a block range -- and it is the pass that fills one in. Not a
 * line breaker (10.6) and not a column sizer (18.6's vocabulary is a *value*; sizing
 * tracks is 19.4's shape and a column's track arithmetic is named as absent below). Not
 * a page model: this build has no page boxes, no page margins and no page orientation,
 * so a page container exists as a kind for a decision and not as a thing that is laid
 * out.
 *
 * Named as absent rather than approximated
 *
 *   - **multi-column layout itself.** A column container's track count, its gutter and
 *     its `column-fill` balancing are not computed here, so a document that writes
 *     `column-count: 3` gets the *number* (18.6's computed value) and no columns. What
 *     this module supplies is the break policy such a pass would consult.
 *   - **the fragment's own geometry** -- where a fragment's content box begins, how its
 *     margins are truncated at a break, what a box's `block-size` means when it is the
 *     first thing in a fragment. A pass owns those, and none of them is a decision.
 *   - **`break-before`/`break-after` suppression by an ancestor's `break-inside: avoid`**
 *     when the ancestor is *deeper* than the boundary being asked about: the boundary
 *     record a caller hands in carries the box's own `break-inside` and nothing above
 *     it. An ancestor's refusal is applied by the pass at the ancestor's own boundary,
 *     which is where CSS applies it.
 *   - **the legacy `page-break-*` spellings**, which are not registered rows
 *     (RoboCSSFragment.h names that), **regions as a laid-out container**, **`orphans`/
 *     `widows` on a container with no lines** (a box with no inline content has no
 *     floor to apply, and the door says so with `RoboNotFound` rather than answering
 *     "allowed"), and **replaced elements and `aspect-ratio`**, which a fragmentation
 *     pass would treat as unbreakable and this build does not classify.
 *
 * The counters are the record a pass reports its own work through, nested in
 * `LayoutBlockStats` beside the flex, grid, table and absolute ones for the reason those
 * are: the interesting fact is the *mix*, and two documents that broke the same number
 * of times are entirely different if one was forced and the other ran out of room.
 */

#ifndef ROBO_LAYOUT_FRAGMENT_H
#define ROBO_LAYOUT_FRAGMENT_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSFragment.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* What kind of container this is                                      */
/* ------------------------------------------------------------------ */

/**
 * The kind of fragmentation context a box establishes.
 *
 * `NONE` is 0 and it is not a container: a box that establishes no fragmentation
 * context cannot break its contents, and every door below answers that rather than
 * guessing a kind. The other three name CSS's fragmentation contexts -- a **column**
 * of a multi-column container, a **page**, and a **region** -- and the order they are
 * declared in is the order a forced break is tried in, which is why the generic
 * `always` (a break every container honours) is not one of them.
 */
typedef enum LayoutFragmentainerKind {
    /** Not a fragmentation context: nothing may break here. */
    LayoutFragmentainerKindNone = 0,
    /** A column of a multi-column container (`column-count`/`column-width`, 18.6). */
    LayoutFragmentainerKindColumn = 1,
    /** A page. This build lays out no pages; the kind exists so a page *decision* is
     *  expressible, and `LayoutFragmentainerKindName` says so. */
    LayoutFragmentainerKindPage = 2,
    /** A region. Honoured by no container this build lays out, exactly as
     *  `CssBreakBetweenForcesPage` refuses to claim `region` for a page. */
    LayoutFragmentainerKindRegion = 3
} LayoutFragmentainerKind;

/** Number of defined fragmentainer kinds, `NONE` included. */
#define LayoutFragmentainerKindCount 4u

/** Non-zero when @p kind is a defined enumerator. */
int LayoutFragmentainerKindIsKnown(LayoutFragmentainerKind kind);

/** Human-readable name ("none", "column", "page", "region"); never NULL. */
const char *LayoutFragmentainerKindName(LayoutFragmentainerKind kind);

/** Non-zero when @p kind is a container -- i.e. not `NONE`. */
int LayoutFragmentainerKindEstablishes(LayoutFragmentainerKind kind);

/**
 * The kind of container @p force names, or `NONE` when it names none.
 *
 * The one door that turns a forced break into the container it is about: `page` answers
 * `Page`, `column` and the generic `always` answer `Column` -- because a column is the
 * one container this build lays out, and `always` means "every container", of which
 * there is one -- and `region` answers `NONE`, which is the *stated* answer for a
 * container that does not exist here rather than a rounding to a column. Every `avoid`
 * and `auto` answers `NONE` too: they name no container, which is why the caller must
 * not read this as "the break is impossible".
 */
LayoutFragmentainerKind LayoutFragmentainerKindOfForcedBreak(CssBreakBetween force);

/* ------------------------------------------------------------------ */
/* The decision                                                        */
/* ------------------------------------------------------------------ */

/** What may happen at a boundary. */
typedef enum LayoutBreakAction {
    /** No break: the contents continue in this fragment. */
    LayoutBreakForbidden = 0,
    /** A break may happen here, and the container has room, so it need not. */
    LayoutBreakAllowed = 1,
    /** A break must happen here. */
    LayoutBreakForced = 2
} LayoutBreakAction;

/** Number of defined actions. */
#define LayoutBreakActionCount 3u

int LayoutBreakActionIsKnown(LayoutBreakAction action);
const char *LayoutBreakActionName(LayoutBreakAction action);

/**
 * **Why** a boundary answered what it did.
 *
 * The half of the decision geometry cannot carry, and the reason the record has two
 * fields rather than one: `Forced` and `Full` produce the same next fragment and are
 * different documents -- one an author asked for, one the container imposed -- and a
 * suite that asserted only the action would pass for a build that ignored every
 * declaration and broke whenever it ran out of room.
 */
typedef enum LayoutBreakReason {
    /** Nothing may break here because the box is not a fragmentation context. */
    LayoutBreakReasonNoContext = 0,
    /** Allowed: the container has room and nothing forbade a break. */
    LayoutBreakReasonAuto = 1,
    /** Forced by a `break-before`/`break-after` this container honours. */
    LayoutBreakReasonForcedByDeclaration = 2,
    /** Forced, but **nothing precedes it in this fragment**, so CSS suppresses it: the
     *  content starts the fragment anyway and a break there would be an empty one. */
    LayoutBreakReasonSuppressedAtStart = 3,
    /** Forbidden by an `avoid` a `break-before`/`break-after` declared. */
    LayoutBreakReasonAvoidedByDeclaration = 4,
    /** Forbidden because the box's own `break-inside` refuses to be split and it does
     *  not fit in what is left -- so it moves whole to the next fragment. */
    LayoutBreakReasonAvoidedInside = 5,
    /** Forbidden because the next box does not fit and no declaration said anything:
     *  the container is full, which is the only reason a break ever happens without a
     *  declaration. */
    LayoutBreakReasonNoRoom = 6,
    /** Forbidden because breaking here would leave too few lines before it
     *  (`orphans`). */
    LayoutBreakReasonOrphansFloor = 7,
    /** Forbidden because breaking here would carry too few lines after it
     *  (`widows`). */
    LayoutBreakReasonWidowsFloor = 8
} LayoutBreakReason;

/** Number of defined reasons. */
#define LayoutBreakReasonCount 9u

int LayoutBreakReasonIsKnown(LayoutBreakReason reason);
const char *LayoutBreakReasonName(LayoutBreakReason reason);

/**
 * One boundary, as the two declarations that meet there plus what the container has.
 *
 * A **record rather than a parameter list**, because the four facts are asked together
 * and a caller that forgot one would be answered as if it had said `auto` -- which is
 * the answer that silently drops a declaration. `LayoutFragmentBoundaryInit` writes
 * exactly that: `auto` everywhere, `roomLeft` set and `anythingBefore` clear, which is
 * the state of a box arriving at an empty container.
 */
typedef struct LayoutFragmentBoundary {
    /** The **later** box's `break-before`; `Auto` when it declared none. */
    CssBreakBetween before;
    /** The **earlier** box's `break-after`; `Auto` when it declared none. */
    CssBreakBetween after;
    /** The later box's `break-inside` -- the interior refusal that matters when it does
     *  not fit. */
    CssBreakInside inside;
    /** Non-zero while the fragmentainer still has room for the next box. */
    int roomLeft;
    /** Non-zero when something already sits in this fragment. A boundary with nothing
     *  before it is where CSS *suppresses* a forced break. */
    int anythingBefore;
} LayoutFragmentBoundary;

/**
 * Clear @p boundary to the state of a box arriving at an **empty** container with room:
 * every declaration `auto`, `roomLeft` set, `anythingBefore` clear. Safe on NULL.
 *
 * The default is `roomLeft` **set** and it is deliberate: a boundary that does not say
 * whether there is room would otherwise be answered "no room" by a zeroed record, and a
 * policy whose zero means "full" breaks every document that forgot to say.
 */
void LayoutFragmentBoundaryInit(LayoutFragmentBoundary *boundary);

/** Non-zero when @p boundary is a record this build may read: two known vocabulary
 *  values (or the initial ones), and the two flags 0 or 1. Safe on NULL. */
int LayoutFragmentBoundaryIsValid(const LayoutFragmentBoundary *boundary);

/** Field-by-field equality. Safe on NULL. */
int LayoutFragmentBoundaryEqual(const LayoutFragmentBoundary *a,
                                const LayoutFragmentBoundary *b);

/** The answer: what may happen and why. */
typedef struct LayoutBreakDecision {
    /** What may happen at this boundary. */
    LayoutBreakAction action;
    /** Why it answered that. */
    LayoutBreakReason reason;
    /** The container kind a **forced** break named, or `NONE` when the break was not
     *  forced. `Column` for `column` and `always`, `Page` for `page`, and `NONE` for
     *  `region` -- a forced break no container here honours is still `Forced`, and this
     *  field is what says the container did not choose it. */
    LayoutFragmentainerKind forcedBy;
} LayoutBreakDecision;

/** Clear @p decision to `Forbidden` / `NoContext` / `NONE`. Safe on NULL. */
void LayoutBreakDecisionInit(LayoutBreakDecision *decision);

/** Non-zero when @p decision is internally consistent: a known action and reason, and a
 *  `forcedBy` kind only on a `Forced` action with a `ForcedByDeclaration` reason. */
int LayoutBreakDecisionIsValid(const LayoutBreakDecision *decision);

/** Field-by-field equality. Safe on NULL. */
int LayoutBreakDecisionEqual(const LayoutBreakDecision *a,
                             const LayoutBreakDecision *b);

/**
 * Decide one boundary: whether @p boundary's contents break, against a container of
 * @p kind.
 *
 * The whole policy, and it is one function for the reason `LayoutAbsoluteResolveAxis`
 * is: the rule is a precedence between four facts, and writing it twice -- once for a
 * "does it fit" question and once for a "must it break" question -- would be writing the
 * precedence twice. In order:
 *
 *   1. **no context**: `NONE` forbids a break and says so (`NoContext`), whatever was
 *      declared. A box that establishes no fragmentation context cannot break.
 *   2. **nothing before it**: a boundary at the start of a fragment is **suppressed**.
 *      CSS's own rule -- a forced break before the first thing in a fragmentainer would
 *      produce an empty fragment -- and it applies to a forced break and not to an
 *      `avoid`, because an `avoid` at the start of a fragment is trivially satisfied.
 *      Answered `Allowed` / `SuppressedAtStart`.
 *   3. **forced wins over avoided**, from either side of the boundary and this container
 *      included: `page` forces a column container's boundary just as `column` does,
 *      because a forced break the container does not *name* is still a forced break.
 *      Answered `Forced` / `ForcedByDeclaration`, with `forcedBy` naming the container
 *      the declaration named.
 *   4. **avoided**: either declaration forbade a break. Answered `Forbidden` /
 *      `AvoidedByDeclaration`.
 *   5. **the interior**: the box's own `break-inside` refuses to be split **and it does
 *      not fit** -- for a container this refusal is about (see
 *      `LayoutFragmentInsideRefuses`). Answered `Forbidden` / `AvoidedInside`, which is
 *      what moves the whole box to the next fragment.
 *   6. **room**: with room, `Allowed` / `Auto`; without, `Forbidden` / `NoRoom`.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a boundary that is not a
 *         record this build may read.
 */
Result LayoutFragmentBreakDecide(const LayoutFragmentBoundary *boundary,
                                 LayoutFragmentainerKind kind,
                                 LayoutBreakDecision *out,
                                 LayoutDiag *diag);

/**
 * Whether @p inside refuses a split **in a container of @p kind**.
 *
 * `avoid` refuses everywhere; `avoid-column`, `avoid-page` and `avoid-region` refuse in
 * the container they name and say nothing to the others -- which is the whole reason
 * `CssBreakInsideIsAvoided` is one predicate and this is a second door rather than a
 * parameter: "do not split me" and "do not split me *here*" are two questions, and only
 * the container knows the second. An unknown value or `NONE` answers 0.
 */
int LayoutFragmentInsideRefuses(CssBreakInside inside, LayoutFragmentainerKind kind);

/**
 * Decide whether a break may fall **after line @p breakAfterLine** of @p totalLines.
 *
 * The line floors, and the one place the policy needs a count: `orphans` is how many
 * lines must stay in the fragment being left and `widows` how many must travel to the
 * one being entered, so breaking after line *n* of *N* lines is allowed exactly when
 * *n >= orphans* and *N - n >= widows*. Both are refused with a reason of their own,
 * because which floor was breached is the only useful half of the answer.
 *
 * The two degenerate cases are refusals with a reason a caller can act on rather than
 * assertions: a break before the first line (`breakAfterLine < 1`) would empty the
 * fragment, and a break at or after the last line is not a break *inside* the text.
 * Both answer `Forbidden` / `NoRoom`, which is what says "this is not a place a break
 * can fall" instead of "a floor forbade it".
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a negative
 *         @p breakAfterLine, @p orphans or @p widows.
 */
Result LayoutFragmentBreakLines(int totalLines,
                                int breakAfterLine,
                                int orphans,
                                int widows,
                                LayoutBreakDecision *out,
                                LayoutDiag *diag);

/* ------------------------------------------------------------------ */
/* A fragment                                                          */
/* ------------------------------------------------------------------ */

/**
 * One fragment of one box: what a pass reports it produced.
 *
 * A record with an ordinal and a block range rather than a rectangle, because a
 * fragment's *geometry* is the pass's answer and its *identity* is this module's
 * question -- "which fragment of which container is this, and how much of the box does
 * it carry". `blockOffset` is where in the box's own block axis the fragment begins and
 * `blockExtent` how far it runs, so a box split in three has three records whose
 * extents sum to its block size and whose offsets tile it -- which is an assertion a
 * caller can make and a rectangle could not express without re-deriving the split.
 */
typedef struct LayoutFragment {
    /** The box being fragmented. */
    LayoutNodeId node;
    /** The kind of container this fragment sits in. */
    LayoutFragmentainerKind kind;
    /** Which fragmentainer of the container, 0-based. */
    uint32_t index;
    /** Where in the box's own block axis this fragment begins. */
    GfxPixelY blockOffset;
    /** How far it runs from @ref blockOffset. */
    GfxPixelY blockExtent;
    /** 1 when this is the **last** fragment of @p node, so a caller closes the box
     *  rather than expecting another. */
    int last;
} LayoutFragment;

/** Clear @p fragment to an empty fragment of no container. Safe on NULL. */
void LayoutFragmentInit(LayoutFragment *fragment);

/** Non-zero when @p fragment is a record this build may read: a known kind that
 *  establishes, a non-NULL node, and a non-negative offset and extent. Safe on NULL. */
int LayoutFragmentIsValid(const LayoutFragment *fragment);

/** Field-by-field equality. Safe on NULL. */
int LayoutFragmentEqual(const LayoutFragment *a, const LayoutFragment *b);

/**
 * Write @p fragment as one line into @p buffer.
 *
 * Complete-or-absent, like every dump in this module: @p outLength receives the length a
 * complete line needs whether or not it fits, and @p buffer is left empty when it does
 * not, so a caller can size a buffer from one call rather than printing half a record.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotSupported when the line
 *         does not fit @p capacity.
 */
Result LayoutFragmentWrite(const LayoutFragment *fragment,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           LayoutDiag *diag);

/** Capacity @ref LayoutFragmentWrite needs for any record this build produces. */
#define RoboLayoutFragmentTextMax 160u

/* ------------------------------------------------------------------ */
/* The counters                                                        */
/* ------------------------------------------------------------------ */

/**
 * Counters for the fragmentation work one block pass did.
 *
 * A record by value, owned by nobody, and nested in `LayoutBlockStats` beside the flex,
 * grid, table and absolute ones. Counters rather than one total for the reason every
 * other record here gives: two documents that broke the same number of times are
 * entirely different if one was forced and the other ran out of room, and the refusals
 * are counted *separately from* the breaks because a refusal is the interesting half of
 * this phase -- a build that honoured no declaration at all would report zero forced and
 * zero avoided breaks and look identical to a document that declared nothing.
 */
typedef struct LayoutFragmentCounters {
    /** Fragmentainers the pass established, of any kind. */
    size_t containers;
    /** Containers whose kind is `Column`. Apart from `containers` because a page or a
     *  region is a decision this build can express and not a box it lays out. */
    size_t columnContainers;
    /** Fragment records produced, over every container. */
    size_t fragments;
    /** Boundaries a declaration **forced**. */
    size_t forcedBreaks;
    /** Boundaries a declaration **refused** (`avoid`), counted even when the refusal
     *  was trivially satisfied because nothing preceded the box. */
    size_t avoidedBreaks;
    /** Boundaries that broke because the container ran out of room and nothing said
     *  anything -- the only break no author asked for. */
    size_t automaticBreaks;
    /** Forced boundaries **suppressed** because they were at the start of a fragment.
     *  Counted rather than dropped: a document that wrote `break-before: column` on the
     *  first box of a fragment asked for an empty fragment, and saying no is this
     *  policy's answer rather than an oversight. */
    size_t suppressedAtStart;
    /** Boundaries refused because breaking there would leave too few lines before. */
    size_t orphansRefusals;
    /** Boundaries refused because breaking there would carry too few lines after. */
    size_t widowsRefusals;
    /** Interiors a box's `break-inside` refused, counted against the container the
     *  refusal was about. */
    size_t insideRefusals;
} LayoutFragmentCounters;

/** Clear @p counters to zero. Safe on NULL. */
void LayoutFragmentCountersInit(LayoutFragmentCounters *counters);

/** Field-by-field equality. Safe on NULL. */
int LayoutFragmentCountersEqual(const LayoutFragmentCounters *a,
                                const LayoutFragmentCounters *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_FRAGMENT_H */
