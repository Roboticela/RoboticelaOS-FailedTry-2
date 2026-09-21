/*
 * RoboPaint context (Phase 11.1).
 *
 * One paint pass: the tree it read, what it saw, and where a later goal hangs temporary
 * data. The shape is RoboLayout's (`RoboLayoutContext.h`), and for the same reason: a
 * pass *accumulates* -- a page origin and a clip are refined as the walk descends, and
 * the counters are read after it ends -- so passing all of that down a call stack would
 * be passing a struct through every function anyway.
 *
 * Three decisions this header records, and each of them is a boundary rather than a
 * detail.
 *
 * **The pass takes the tree by `const` pointer.** RoboPaint never writes a layout
 * result, a line, a baseline or an overflow record; the whole module reads through
 * RoboLayout's const doors and allocates nothing but its own list. So "paint does not
 * modify layout state" is the signature rather than a rule a reviewer has to check, and
 * a caller cannot accidentally hand a mutable tree to a function that would change it.
 *
 * **A tree whose geometry is not final is refused, not painted.**
 * `robo_layout_tree_needs_layout` answers whether any box carries a layout bit, and when
 * it does the pass stops with `ROBO_PAINT_PASS_STALE` and appends nothing. This is the
 * phase's entire integration with 10.14, and it is a refusal rather than a warning
 * because the two mistakes are not symmetric: a list built from half-computed geometry
 * is a *wrong* list that draws boxes in the wrong places, and a caller that wanted a
 * best-effort preview can get one by running a layout pass first. A pass that painted
 * anyway and reported "some geometry was stale" would be handing back a list nobody
 * could trust and no way to tell which parts of it.
 *
 * **That is where paint invalidation stops, and the boundary is deliberate.** 10.14
 * asks a per-box question -- *is this conclusion still true?* -- and its answer is a
 * state on the box. Paint invalidation asks a different question -- *is this list still
 * current?* -- and this phase's answer is not a mechanism at all: a paint list is **not
 * a cache of conclusions**, it is regenerated from the tree on every pass, so there is
 * nothing for a dirty-rect system to maintain and nothing for a second invalidation
 * vocabulary to mean. What the phase hands the future system is its **input** rather
 * than its machinery: `robo_paint_stats_t.style_only` counts the visited boxes whose
 * only staleness is a paint-visible change, which is exactly the set a Phase 11.11
 * system would repaint. Reading that number is not painting anything, and building the
 * system that consumes it is a later goal's -- which is why this module has no
 * paint-invalidation constant, no dirty rectangle and no per-list currency check.
 *
 * The context owns **nothing borrowed**: the tree and the caller's user data are
 * references, the list is the caller's, and the stats are a record by value. Destroying
 * a context frees the context and nothing else, so it is safe to destroy in any order
 * relative to the tree and the list it was used with.
 */
#ifndef ROBO_PAINT_CONTEXT_H
#define ROBO_PAINT_CONTEXT_H

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutInvalidation.h"
#include "RoboLayout/RoboLayoutTree.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintList.h"
#include "RoboPaint/RoboPaintTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the last paint pass did. */
typedef enum robo_paint_pass_outcome {
    /** No pass has run in this context yet. */
    ROBO_PAINT_PASS_NONE = 0,
    /** The tree was walked and the list was produced. */
    ROBO_PAINT_PASS_PAINTED = 1,
    /** The pass could not complete (a NULL or unusable tree, a link naming nothing). */
    ROBO_PAINT_PASS_FAILED = 2,
    /**
     * The tree's geometry is not final and the pass **refused** rather than painting
     * it: at least one box carries a layout bit
     * (`robo_layout_tree_needs_layout`).
     *
     * A fourth outcome rather than a failure, because a caller can act on it: the
     * cure is to run a layout pass, and a caller that cannot tell "your tree is stale"
     * from "your tree is broken" would go looking for a bug in the document.
     */
    ROBO_PAINT_PASS_STALE = 3
} robo_paint_pass_outcome_t;

/** Number of defined pass outcomes. */
#define ROBO_PAINT_PASS_OUTCOME_COUNT 4u

/** Non-zero if @p outcome is a defined outcome. */
int robo_paint_pass_outcome_is_known(robo_paint_pass_outcome_t outcome);

/** Human-readable outcome name; never NULL; "unknown" when undefined. */
const char *robo_paint_pass_outcome_name(robo_paint_pass_outcome_t outcome);

/**
 * What one pass saw.
 *
 * A record by value, owned by nobody, and its fields are chosen the way RoboLayout's
 * are: each one answers a question the others cannot, and the interesting facts are the
 * *pairs* rather than the totals. `boxes` against `unplaced` is how a reader learns
 * whether a document painted in full or whether half of it was never laid out;
 * `backgrounds` and `border_sides` against `boxes` is how a reader learns how much of
 * the document draws anything at all; and `style_only` is the one number that belongs
 * to a *later* phase (see the header comment) and lives here because this is where it
 * can be counted.
 */
typedef struct robo_paint_stats {
    /** Objects in the tree. */
    size_t objects;
    /** Objects the traversal reached. */
    size_t visited;
    /** Boxes painted: one `BEGIN_BOX`/`END_BOX` pair each. */
    size_t boxes;
    /**
     * Objects the traversal reached that are **not placements**: nothing computed their
     * geometry, so there is nothing to paint. A box inside a box the pass never placed,
     * an inline-level box in a block-direction context -- RoboLayout's own counters say
     * why each one was left, and this is the painter's count of the consequences.
     */
    size_t unplaced;
    /**
     * Text runs the traversal reached.
     *
     * Since 11.6 a run **is painted** -- a `TEXT` operation carries its characters, its
     * baseline and its font inputs -- so this counter is the first half of a pair rather
     * than a statement about an omission, and the two halves are an identity by
     * construction: `text_runs = texts + text_skipped`. A reader learns from it that the
     * document has text at all, which is the fact every other counter here is silent
     * about.
     */
    size_t text_runs;
    /** `TEXT` operations appended: runs whose characters, colour and placement all said
     *  "draw this". */
    size_t texts;
    /**
     * Runs the traversal reached that produced **no** operation.
     *
     * Every reason is a statement about the document rather than a failure, which is why
     * the pass still succeeds: nothing placed the run (its own geometry is not final), its
     * parent element declares no usable `color`, its text is empty, or its range could not
     * be read. It is the same rule every drawing kind follows -- an operation is emitted
     * only when it draws (`RoboPaintTypes.h`) -- counted here so that the difference
     * between "the document has no text" and "the document's text drew nothing" is
     * visible, since those are very different bugs to go looking for.
     */
    size_t text_skipped;
    /**
     * Placed boxes whose element is an `<img>` -- the traversal reaching an image *element*,
     * whether or not anything is painted for it (11.7).
     *
     * The `text_runs` of this phase, and counted for the same reason: it is the number a
     * reader needs to tell "this document has no images" from "this document's images drew
     * nothing", which are a missing feature and a bug respectively. It counts a **placed**
     * box, so an `<img>` nothing placed is in `unplaced` and is not also counted here -- the
     * two counters stay disjoint the way `unplaced` and `boxes` are.
     */
    size_t image_elements;
    /**
     * `IMAGE` operations appended: images whose resource is decoded, whose payload this build
     * may draw and whose source rectangle is inside the resource.
     *
     * The second half of `image_elements = images + image_skipped`, and the two halves are an
     * identity by construction -- the same pair `text_runs`/`texts` form one level up.
     */
    size_t images;
    /**
     * Image elements the traversal reached that produced **no** operation (11.7).
     *
     * Every reason is a statement about the document rather than a failure, which is why the
     * pass still succeeds: no `src` to fetch, a resource that was never resolved, a decode that
     * failed or was refused as unsupported, a resource with no pixel surface or a zero
     * dimension, or a resolution outside `ROBO_PAINT_IMAGE_MAX_DIMENSION`. That list is long on
     * purpose and *is* the phase's missing-image policy: this runtime has no broken-image
     * presentation, so nothing is drawn and the count says how many drawings that cost.
     *
     * The geometry is deliberately *not* among the reasons: a box with no area still produces
     * an operation, and the drawing pass is where a destination with no area is counted
     * (`robo_paint_decoration_stats_t.empty`). One is a fact about the document's resources and
     * the other about its geometry, and keeping them in different layers is what lets a reader
     * of either number know which they are looking at.
     */
    size_t image_skipped;
    /** Operations appended. */
    size_t ops;
    /** Background operations: boxes whose declared background draws. */
    size_t backgrounds;
    /** Border operations: sides whose used width is above zero and whose colour
     *  draws. */
    size_t border_sides;
    /** Boxes carrying no computed style: no declaration could be read from them, so no
     *  decoration was found. A fact about the build, not a failure. */
    size_t without_style;
    /** Boxes whose overflow put a **boundary** in force for their contents. */
    size_t clipped;
    /**
     * Clip **scopes** the traversal opened: one per box whose overflow actually *changed* the
     * boundary in force for its contents (11.8).
     *
     * Beside `clipped` rather than folded into it, because the two answer different questions
     * and a document can differ on exactly the case between them. `clipped` counts boxes whose
     * used modes say "this axis hides" -- a fact about a box's own `overflow` -- while this
     * counts the ones where that boundary, intersected with everything already in force, came
     * out **different**: a hidden box inside an already-hidden ancestor whose boundary is
     * already inside its own padding box bounds nothing new, and a scope that established
     * nothing is a command a consumer could only skip, so none is emitted. The gap between the
     * two numbers is therefore a real property of the document rather than a rounding, and
     * `clipped - clip_scopes` is the count of scopes the traversal *declined* to push.
     */
    size_t clip_scopes;
    /**
     * Placed boxes whose declared `opacity` is **not** fully opaque (11.9) -- a fact about a
     * box's own style, counted where the declaration is read.
     *
     * The `clipped` half of a second pair, and it pairs with `opacity_scopes` the same way:
     * a document can declare a strength that changes nothing, and the interesting fact is
     * usually which of the two numbers is larger. A declaration of `1` counts nowhere, because
     * `1` is what an element that says nothing already has -- and the reason this is *not*
     * "boxes with an `opacity` declaration" is that `opacity: 1` and no declaration produce the
     * same picture, so a counter that distinguished them would be counting syntax.
     */
    size_t opacity_declared;
    /**
     * Opacity **scopes** the traversal opened: one per box whose declaration actually
     * *changed* the strength in force for itself and its subtree (11.9).
     *
     * The gap against `opacity_declared` is a real property of the document rather than a
     * rounding, and a document reaches it the ordinary way: a `0.5` box inside a `0` ancestor
     * is already drawn at nothing, so its product does not change the strength in force and no
     * scope is emitted -- a scope that established nothing is a command a consumer could only
     * skip.
     *
     * Read against `boxes` it is how a reader learns whether a faint document is faint because
     * of one heavy ancestor or because every box declared its own faintness, which are two very
     * different documents to be looking at.
     */
    size_t opacity_scopes;
    /**
     * Colours that could not be resolved: `currentColor` whose box had no usable
     * `color` either.
     *
     * Counted because the alternative is a silent one -- an operation carrying a
     * contextual colour is a legitimate conclusion, but a document where *every* colour
     * is contextual is a document whose styles were not computed, and a reader who saw
     * only zeros in the other counters would not know why nothing drew.
     */
    size_t contextual_colors;
    /**
     * Visited boxes whose only staleness is a **paint-visible** change (10.14's `STYLE`
     * bit with no layout bit).
     *
     * The input a paint-invalidation system will read, and the whole of this phase's
     * footprint on that question -- see the header comment. It is not a count of
     * anything this pass did; it is a count of what a *later* pass would owe.
     */
    size_t style_only;
    /** Deepest box seen. */
    uint32_t depth_max;
    /** Passes this context has completed, including this one. */
    size_t passes;
} robo_paint_stats_t;

/** An opaque, caller-owned paint pass. */
typedef struct robo_paint_context robo_paint_context_t;

/** Create an empty context: no tree, no pass.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL output; ROBO_OUT_OF_MEMORY. */
robo_result_t robo_paint_context_create(robo_paint_context_t **out_context,
                                        robo_paint_diag_t *diag);

/** Destroy @p context. The tree, the list and the caller's user data are not touched.
 *  Safe on NULL. */
void robo_paint_context_destroy(robo_paint_context_t *context);

/** Return @p context to its just-created state: counters zeroed, outcome NONE, last
 *  tree forgotten. The pass count goes to 0 with it -- "this context has run nothing"
 *  is the state it restores, not "this context has run something else". Safe on NULL. */
void robo_paint_context_reset(robo_paint_context_t *context);

/**
 * Install the caller's temporary pass data.
 *
 * The extension point a later goal uses instead of growing this struct -- the same door
 * RoboLayout's context has, for the same reason: a field per future feature would make
 * this header depend on every goal that follows it. RoboPaint never dereferences it, so
 * what it points at is entirely the caller's business.
 */
void robo_paint_context_set_user_data(robo_paint_context_t *context, void *user_data);

/** The installed user data, or NULL. */
void *robo_paint_context_user_data(const robo_paint_context_t *context);

/**
 * Paint @p tree into @p list.
 *
 * Walks the tree in document order -- parents before children, children left to right --
 * and appends, for every box nothing has to be asked about:
 *
 *   a `BEGIN_BOX` marker carrying the box's three rectangles in paint coordinates, the
 *   accumulated origin, the clip in force and the box's depth;
 *   a `BACKGROUND` operation when the box's background declaration draws;
 *   one `BORDER_*` operation per side whose used width is above zero and whose colour
 *   draws;
 *   a `CONTENT` record marking where the box's own painting ends and its contents
 *   begin, emitted for **every** placed box whether or not it drew anything and whether
 *   or not it has children;
 *   a `TEXT` operation for every run among those children whose colour, characters and
 *   placement say "draw this" (11.6), carrying the span that names its characters in
 *   `list`'s own pool -- and **no** structural triple around it, because a run is not a
 *   box;
 *   the box's children, in order, under the boundary this box's own overflow put in
 *   force;
 *   and an `END_BOX` marker closing the box.
 *
 * @p list is **cleared first**, so a caller may reuse one list across passes and never
 * sees a previous document's operations. It is a parameter rather than a member of the
 * context because the caller owns it (`RoboPaintList.h`): a context that owned a list
 * would have to answer questions about its lifetime that the caller can answer better.
 *
 * **The pass takes @p tree by const pointer and writes nothing into it.** Every read
 * goes through RoboLayout's read doors, and the only allocation the pass performs is the
 * list's own growth.
 *
 * A tree that needs layout is refused with `ROBO_PAINT_PASS_STALE` and @p list is left
 * empty (see the header comment). An empty tree is a legitimate pass that paints
 * nothing -- a document whose root element is `display: none` builds no boxes -- and
 * reports zero objects rather than an error.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL context, tree or list. A failure
 *         inside the walk -- a child link that names no box, a derived box record that
 *         is not one -- fails the pass with `ROBO_PAINT_PASS_FAILED` and the diagnostic
 *         carrying the reason, because a pass that painted half a document and reported
 *         success would be handing back a list nobody asked for.
 */
robo_result_t robo_paint_context_run(robo_paint_context_t *context,
                                    const robo_layout_tree_t *tree,
                                    robo_paint_list_t *list,
                                    robo_paint_diag_t *diag);

/** What the last pass saw, by value. Zeros for a NULL context. */
void robo_paint_context_stats(const robo_paint_context_t *context, robo_paint_stats_t *out);

/** The last pass's outcome; ROBO_PAINT_PASS_NONE for a NULL context. */
robo_paint_pass_outcome_t robo_paint_context_outcome(const robo_paint_context_t *context);

/** Borrowed tree of the last pass, or NULL when none has run. */
const robo_layout_tree_t *robo_paint_context_tree(const robo_paint_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_CONTEXT_H */
