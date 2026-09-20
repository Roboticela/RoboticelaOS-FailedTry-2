/*
 * RoboLayout invalidation (Phase 10.14).
 *
 * Every phase before this one answered "what is a box's geometry?". This one
 * answers a question that only exists because those answers persist: **which of
 * them is still true?**
 *
 * A pass is a pure function of the tree, the viewport and the borrowed styles
 * (RoboLayoutBlock.h states that, and 10.13 relied on it). What that buys is
 * reproducibility; what it does not buy is a way to say "nothing you depend on
 * changed, so do not compute any of it again". This header is that way.
 *
 * **An invalidation is a claim about a conclusion, not about an input.** The
 * tempting model -- record which declarations changed and re-resolve those -- is
 * wrong at the second box, because a box's geometry is a function of its
 * *ancestors'* conclusions as well as its own declarations: a leaf whose author
 * changed nothing moves when its parent's padding changes. So what is tracked is
 * not "which inputs changed" but "which conclusions cannot be trusted", which is a
 * property of boxes and composes when it propagates.
 *
 * The four states
 *
 *   NONE / CLEAN   nothing about this box is stale.
 *   STYLE          the box's *computed style* changed in a way layout does not
 *                  read: a colour, a background, a border's style where the width
 *                  is unchanged. It is a real state rather than a discarded one
 *                  because paint (11) is the consumer, and because the phase's
 *                  *point* is that such a change must not pull geometry: a build
 *                  that folded it into LAYOUT would recompute the document to
 *                  repaint one box.
 *   LAYOUT         this box's own geometry is stale -- its position, its size, or
 *                  the inset records a later phase reads.
 *   SUBTREE        this box's **descendants'** layout is stale. Kept apart from
 *                  LAYOUT because the two propagate differently: a box whose own
 *                  origin is stable but whose width changed has descendants to
 *                  re-place and no reason to be re-placed itself.
 *   OVERFLOW       this box's **overflow extents** (10.13) are stale while the box
 *                  itself is not. The state exists because 10.13 made overflow a
 *                  second rectangle *beside* the box, so a change can leave the
 *                  box's own geometry correct and its measurement wrong -- which is
 *                  exactly what a `position: relative` offset does to every
 *                  ancestor it does not otherwise touch.
 *
 * The bits compose, and the policy below is stated in terms of which are set.
 *
 * What "dirty" does not mean
 *
 * It does not mean the pass lays out less. This build's pass recomputes the
 * document from the root, exactly as it did before this phase; 10.14's own brief
 * forbids the alternative ("premature optimization", "do not implement complex
 * dependency graphs"), and the two honest halves of the phase are therefore kept
 * apart on purpose:
 *
 *   the **model** is complete -- states, a classifier, a propagation policy, a
 *   dirty root, and doors that answer per box;
 *
 *   the **use** it is put to is one decision (`robo_layout_context_run_incremental`
 *   runs the pass only when the model says something cannot be trusted), plus the
 *   accounting that proves the model agrees with the pass: a pass reports how many
 *   boxes it began with dirty, which the walk's own numbers are then checked
 *   against.
 *
 * The code that turns a dirty *region* into a partial recursion is not here and is
 * not claimed. A caller that needs it one day will find the region in
 * `robo_layout_tree_invalidation_root` and the policy in this file; a caller today
 * gets correctness rather than a faster wrong answer.
 *
 * Style changes are classified, not parsed
 *
 * `robo_layout_style_effect_for_property` answers, for a property **name**, one of
 * three things: layout does not read it at all, it reads it but only through a
 * paint-visible effect, or it feeds geometry. The names are the ones this module
 * actually consumes -- the twelve border longhands, the four margins, the four
 * paddings, `width`/`height` and their clamps, `display`, `position` and the four
 * insets, the two overflow longhands and the shorthand, the font and line
 * properties text measurement reads -- and a name the table does not know is
 * *NONE*, which is the conservative answer for a paint side effect and the honest
 * answer for a property nobody has taught this module about. RoboLayout parses
 * nothing here and reads no rule: it is told a name and says what the *name* costs.
 *
 * Chromium's dirty-bit / layout-root model is temporary technical reference only;
 * nothing here is a translation of it. See layout/README.md and layout/docs/.
 */

#ifndef ROBO_LAYOUT_INVALIDATION_H
#define ROBO_LAYOUT_INVALIDATION_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Forward declaration: a caller reaches the doors below through a tree. */
typedef struct robo_layout_tree robo_layout_tree_t;

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

/**
 * What must be recomputed about one box: a **bitmask**, because the states
 * compose.
 *
 * `CLEAN` is 0 rather than a bit, so "nothing is stale" is the zero a caller gets
 * from a cleared record and from a box in a tree nobody has invalidated -- which
 * is the same convention the result record's flags use, and the reason a
 * freshly-created record needs no setting up.
 *
 * The bits are named for the *conclusion they invalidate* and not for the input
 * that produced it, because two different changes routinely invalidate one
 * conclusion: a declared `width` changing and an ancestor's content width changing
 * both set LAYOUT, and a build that kept a per-property reason on a box would have
 * to pick one of them and lose the other.
 */
typedef enum robo_layout_invalidation_state {
    /** Nothing about this box is stale. */
    ROBO_LAYOUT_INVALIDATION_CLEAN = 0u,
    /** The computed style changed in a way layout does not read. */
    ROBO_LAYOUT_INVALIDATION_STYLE = 1u << 0,
    /** This box's own geometry is stale. */
    ROBO_LAYOUT_INVALIDATION_LAYOUT = 1u << 1,
    /** This box's descendants' layout is stale. */
    ROBO_LAYOUT_INVALIDATION_SUBTREE = 1u << 2,
    /** This box's overflow extents are stale while the box itself is not. */
    ROBO_LAYOUT_INVALIDATION_OVERFLOW = 1u << 3
} robo_layout_invalidation_state_t;

/** The three "something must be recomputed" bits: the states a pass consumes.
 *  STYLE is deliberately absent -- a repaint is not a relayout, and
 *  `robo_layout_tree_needs_layout` asks this question rather than `is_clean` for
 *  exactly that reason. */
#define ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS \
    (ROBO_LAYOUT_INVALIDATION_LAYOUT | ROBO_LAYOUT_INVALIDATION_SUBTREE | \
     ROBO_LAYOUT_INVALIDATION_OVERFLOW)

/**
 * The subset of the above that says a box's **own** geometry is stale: LAYOUT alone.
 *
 * The narrowest question in this header, and the difference between it and
 * `LAYOUT_BITS` is the reason the phase has three bits rather than one:
 *
 *   a box carrying SUBTREE has **descendants** to re-place. It is on the *path* to the
 *   region, and it is deliberately not at its start -- the marking policy never gives an
 *   ancestor LAYOUT, because an ancestor's own origin is computed from its containing
 *   block and does not move when a descendant grows (the doc for
 *   `robo_layout_tree_mark_layout_dirty` says so at length). A root defined over this
 *   macro therefore answers "where did geometry actually change?" rather than "how far up
 *   does the damage reach?", and for a leaf that reflowed it answers the leaf;
 *
 *   a box carrying OVERFLOW alone owes a **measurement** and no placement at all. An
 *   `overflow: hidden` becoming `visible` is exactly that box, and so is every ancestor
 *   of a `position: relative` box that was displaced without its flow space changing.
 *
 * So the three questions have three doors: this macro is what
 * `robo_layout_tree_invalidation_root` selects on, `LAYOUT_BITS` is what
 * `robo_layout_tree_needs_layout` and `robo_layout_tree_invalidation_count` select on,
 * and `ALL` is what `robo_layout_tree_is_clean` selects on.
 */
#define ROBO_LAYOUT_INVALIDATION_OWN_BITS (ROBO_LAYOUT_INVALIDATION_LAYOUT)

/** Every defined state bit. */
#define ROBO_LAYOUT_INVALIDATION_ALL \
    (ROBO_LAYOUT_INVALIDATION_STYLE | ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS)

/** Number of state *values* a caller can be handed by the name door: the four bits
 *  and the empty set. A bitmask's combinations are not enumerated, which is the
 *  whole reason it is a bitmask. */
#define ROBO_LAYOUT_INVALIDATION_STATE_COUNT 5u

/** Non-zero when every set bit of @p state is a defined state bit. */
int robo_layout_invalidation_state_is_known(uint32_t state);

/**
 * Human-readable name of a state bitmask; never NULL.
 *
 * A combination is spelled with `+` between its bits (`"layout+subtree"`), and the
 * empty set is `"clean"`, because the string this is asked for most often is the
 * one a failing assertion prints -- and a suite that had to decode `5` by hand
 * would be a suite nobody reads.
 */
const char *robo_layout_invalidation_state_name(uint32_t state);

/* ------------------------------------------------------------------ */
/* Why                                                                 */
/* ------------------------------------------------------------------ */

/**
 * Why a box was invalidated: the *reason*, not the state.
 *
 * One reason per box and not one per bit, because the reason answers a different
 * question than the state does -- "what happened?" rather than "what is stale?" --
 * and a caller reading a dirty tree in a debugger wants the first. It is the
 * lightweight debug surface this phase promises (requirement 14) and nothing more:
 * it carries no counts and no history.
 *
 * The set is closed and short on purpose. A reason that named a *property*
 * (`"padding-left changed"`) would have to be a string, which would make the record
 * own storage and the module own lifetimes for a fact that only a human reads.
 */
typedef enum robo_layout_invalidation_reason {
    /** Clean: no reason, which is also the cleared record's value. */
    ROBO_LAYOUT_INVALIDATION_REASON_NONE = 0,
    /** A style change layout does not read (colour, background, a border's style). */
    ROBO_LAYOUT_INVALIDATION_REASON_PAINT = 1,
    /** A style change that feeds the box's own size or insets. */
    ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY = 2,
    /** A change to `position` or an inset: the *displacement* moved and the box did
     *  not. The one reason that sets LAYOUT without SUBTREE on the box itself. */
    ROBO_LAYOUT_INVALIDATION_REASON_POSITION = 3,
    /** A change to the box set or to what a box contains (a rebuild, a rebuild of
     *  the classification, a subtree invalidation). */
    ROBO_LAYOUT_INVALIDATION_REASON_CONTENT = 4,
    /** The containing block changed: the viewport, or the constraints that describe
     *  it. Never set per box by a caller -- it is what `mark_all` records. */
    ROBO_LAYOUT_INVALIDATION_REASON_VIEWPORT = 5,
    /** Inherited from a descendant, a following sibling or an ancestor rather than
     *  caused here. The reason a *reader* distinguishes "this box was told" from
     *  "this box was told it must follow". */
    ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED = 6
} robo_layout_invalidation_reason_t;

/** Number of defined reasons. */
#define ROBO_LAYOUT_INVALIDATION_REASON_COUNT 7u

/** Non-zero if @p reason is a defined reason. */
int robo_layout_invalidation_reason_is_known(robo_layout_invalidation_reason_t reason);

/** Human-readable reason name; never NULL; "unknown" when undefined. */
const char *robo_layout_invalidation_reason_name(robo_layout_invalidation_reason_t reason);

/* ------------------------------------------------------------------ */
/* What a style change costs                                           */
/* ------------------------------------------------------------------ */

/**
 * What a computed property costs when it changes.
 *
 * Three outcomes and not two, because "layout does not read this" and "this does
 * not exist" are different statements and only one of them belongs to a property a
 * stylesheet wrote: a `background-color` change is NONE-and-paint (there is no
 * paint yet, so nothing is invalidated), a `width` change is LAYOUT, and a
 * `cursor` change is NONE because no phase has claimed to read it.
 */
typedef enum robo_layout_style_effect {
    /** Layout reads nothing here; nothing is invalidated. */
    ROBO_LAYOUT_STYLE_EFFECT_NONE = 0,
    /** Paint-visible but not measured: marks STYLE and stops. */
    ROBO_LAYOUT_STYLE_EFFECT_PAINT = 1,
    /** Measured: invalidates geometry. */
    ROBO_LAYOUT_STYLE_EFFECT_LAYOUT = 2
} robo_layout_style_effect_t;

/** Number of defined style effects. */
#define ROBO_LAYOUT_STYLE_EFFECT_COUNT 3u

/** Non-zero if @p effect is a defined effect. */
int robo_layout_style_effect_is_known(robo_layout_style_effect_t effect);

/** Human-readable effect name; never NULL; "unknown" when undefined. */
const char *robo_layout_style_effect_name(robo_layout_style_effect_t effect);

/**
 * What changing @p property_name costs this build's layout.
 *
 * The names are the ones the module's own readers ask for, and the table is
 * *written out* rather than derived from RoboCSS's registry on purpose: what is
 * being answered is not "does RoboCSS have a row for this?" but "does **layout**
 * read it?", and only this module can answer that. A generated table would say
 * every registered property costs LAYOUT, which is the wrong direction -- it would
 * pull geometry for a colour -- and would be a promise this module could not keep
 * as 9.x grows.
 *
 * @p property_name may be NULL, and is matched case-insensitively, because a
 * caller reaching this from a stylesheet has a spelling from the document rather
 * than from a table. An unknown or NULL name answers *NONE*: the conservative
 * answer, since a property this module has never read cannot have changed a
 * conclusion it made.
 */
robo_layout_style_effect_t robo_layout_style_effect_for_property(const char *property_name);

/* ------------------------------------------------------------------ */
/* One box's record                                                    */
/* ------------------------------------------------------------------ */

/**
 * What one box owes the next pass.
 *
 * A record by value and owned by nobody, exactly like every other record in this
 * module -- but unlike them it is *stored on the box* (RoboLayoutObject.h) rather
 * than on the tree, and the difference is the phase. A context, a line box and an
 * overflow record are conclusions **of** a pass and die with it; an invalidation is
 * the *input* to the next one, and the only thing in this module that must survive
 * from one pass to the next by design.
 */
typedef struct robo_layout_invalidation {
    /** ROBO_LAYOUT_INVALIDATION_* bits. */
    uint32_t state;
    /** Why, for a reader. Meaningful only while @a state is not CLEAN. */
    robo_layout_invalidation_reason_t reason;
} robo_layout_invalidation_t;

/** Clear @p record: clean, no reason. Safe on NULL. */
void robo_layout_invalidation_init(robo_layout_invalidation_t *record);

/** Non-zero when @p record claims nothing is stale (also true for NULL). */
int robo_layout_invalidation_is_clean(const robo_layout_invalidation_t *record);

/** The state bits (CLEAN for NULL). */
uint32_t robo_layout_invalidation_state(const robo_layout_invalidation_t *record);

/** The reason (NONE for NULL). */
robo_layout_invalidation_reason_t robo_layout_invalidation_reason(
    const robo_layout_invalidation_t *record);

/** Field-by-field equality. Safe on NULL. */
int robo_layout_invalidation_equal(const robo_layout_invalidation_t *a,
                                   const robo_layout_invalidation_t *b);

/* ------------------------------------------------------------------ */
/* What one pass saw                                                   */
/* ------------------------------------------------------------------ */

/**
 * The invalidation half of a pass's report.
 *
 * Counters rather than a single flag, and the pair that matters is
 * (`invalidated`, `style_only`): a pass that began with nothing dirty and a pass
 * that began with a colour change on every box both recompute a document here, and
 * only these two numbers tell a reader which one happened. `root` is the box a
 * partial pass *would* have started from, and `recomputed` is the honest statement
 * of what this build does with it.
 */
typedef struct robo_layout_invalidation_stats {
    /** Boxes carrying a layout bit when the pass began. */
    size_t invalidated;
    /** Boxes whose **only** staleness was STYLE: the changes that did not pull
     *  geometry. */
    size_t style_only;
    /** The shallowest box carrying a layout bit, or ROBO_LAYOUT_NODE_ID_NONE when
     *  nothing was dirty. What a partial pass would start from -- and, today, the
     *  number a suite asserts to prove the *policy* rather than the recursion. */
    robo_layout_node_id_t root;
    /** The dirty root's reason, or NONE. */
    robo_layout_invalidation_reason_t reason;
    /** Non-zero when this pass recomputed geometry. 0 for a pass that found
     *  nothing to do (`robo_layout_context_run_incremental`). */
    int recomputed;
} robo_layout_invalidation_stats_t;

/** Clear @p stats: nothing invalidated, no root, recomputed 0. Safe on NULL. */
void robo_layout_invalidation_stats_init(robo_layout_invalidation_stats_t *stats);

/** Field-by-field equality. Safe on NULL. */
int robo_layout_invalidation_stats_equal(const robo_layout_invalidation_stats_t *a,
                                         const robo_layout_invalidation_stats_t *b);

/* ------------------------------------------------------------------ */
/* Reading the tree                                                    */
/* ------------------------------------------------------------------ */

/**
 * The state of box @p id: 0 (CLEAN) for an id that names no box, which is the same
 * convention the object doors use for a flag read.
 */
uint32_t robo_layout_tree_invalidation_state(const robo_layout_tree_t *tree,
                                             robo_layout_node_id_t id);

/**
 * The reason box @p id was invalidated, or NONE for an id that names no box or a
 * box that is clean.
 */
robo_layout_invalidation_reason_t robo_layout_tree_invalidation_reason(
    const robo_layout_tree_t *tree,
    robo_layout_node_id_t id);

/** Non-zero when **no** box in @p tree carries any bit -- the strong question, and
 *  the one a caller asks before dropping a tree's work entirely (also true for
 *  NULL and for an empty tree). */
int robo_layout_tree_is_clean(const robo_layout_tree_t *tree);

/**
 * Non-zero when some box carries a **layout** bit.
 *
 * The question `robo_layout_context_run_incremental` asks, and the reason it is not
 * spelled `!is_clean`: a document with only paint-visible changes is not clean and
 * does not need geometry. The two doors exist because the two questions exist.
 */
int robo_layout_tree_needs_layout(const robo_layout_tree_t *tree);

/** Boxes carrying a layout bit (0 for NULL). */
size_t robo_layout_tree_invalidation_count(const robo_layout_tree_t *tree);

/**
 * Boxes whose **only** staleness is STYLE: the changes that were recorded and did
 * not pull geometry (0 for NULL).
 *
 * The companion of the count above, and the pair a pass reports, because the two
 * together are the phase's evidence: a document with four paint-only changes has four
 * boxes no pass will touch, and one with a single `width` change has one box that has
 * dragged its ancestors and its following flow along with it.
 */
size_t robo_layout_invalidation_style_only_count(const robo_layout_tree_t *tree);

/**
 * The **lower common ancestor** of the boxes whose own geometry is stale
 * (`ROBO_LAYOUT_INVALIDATION_OWN_BITS`), or ROBO_LAYOUT_NODE_ID_NONE.
 *
 * The region a partial pass would have to start from: it is the one box whose subtree
 * contains every stale box, so it is the smallest thing a relayout could begin at and
 * still reach everything that needs placing. Note what is **not** in the question,
 * because the two exclusions are the phase's shape:
 *
 *   an ancestor carrying SUBTREE is *on the path* and not the answer. Every ancestor of
 *   a reflowed leaf is marked, and if any of them counted the answer would always be the
 *   root box -- a number that says nothing about where geometry changed;
 *
 *   a box carrying OVERFLOW alone is a measurement. So a displaced box's root is the box
 *   itself rather than the document, and an `overflow` mode change has **no root at
 *   all**, which is the honest answer: nothing needs placing again.
 *
 * The *common ancestor* and not the *shallowest stale box*, and the difference is not
 * pedantry: invalidating one box marks the flow after it too, the flow after an ancestor
 * is a sibling that sits **higher** in the tree than the box that moved, and a
 * shallowest-alone rule could therefore name a box whose subtree does not contain the
 * change at all. The walk is deterministic by construction -- it starts from the first
 * stale box in creation order, which is document order, and the answer is the deepest
 * ancestor-or-self that contains every one of them, so which box the walk started from
 * cannot change the result (requirement 12).
 *
 * NONE therefore means "no box needs placing again", which is a different statement from
 * `robo_layout_tree_needs_layout` answering 0 and from `robo_layout_tree_is_clean`
 * answering 1: a pass can be needed to re-measure with nothing to re-place, and a tree
 * can be clean while a repaint is still owed.
 *
 * O(n) in the tree, deliberately: maintaining a root incrementally would mean
 * recomputing it on every propagation step, and the number is read once per pass
 * and by suites. Premature optimization is the thing this phase is forbidden from.
 */
robo_layout_node_id_t robo_layout_tree_invalidation_root(const robo_layout_tree_t *tree);

/* ------------------------------------------------------------------ */
/* Marking                                                             */
/* ------------------------------------------------------------------ */

/**
 * Clear **every** bit on every box, the STYLE bit included.
 *
 * The full reset, and the door a caller that has consumed a tree's paint signal
 * reaches for. A *layout* pass uses only the narrower clearing below, because a
 * pass has no business dropping a repaint request it cannot honour.
 *
 * Safe on NULL. Idempotent -- "make sure nothing is dirty" is a legitimate thing to
 * want twice.
 */
robo_result_t robo_layout_tree_mark_clean(robo_layout_tree_t *tree);

/**
 * Mark every box dirty with @p reason, for a change that redraws the whole
 * document's containing block.
 *
 * The one door whose reason a caller normally passes `VIEWPORT` for, and the reason
 * it takes one at all is that a caller reaching it is describing an *input* rather
 * than a box: a viewport change invalidates the root's containing block and
 * therefore every box beneath it, which is a claim about the whole tree and not
 * about any of its parts.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p tree or an undefined
 *         @p reason.
 */
robo_result_t robo_layout_tree_mark_all(robo_layout_tree_t *tree,
                                        robo_layout_invalidation_reason_t reason,
                                        robo_layout_diag_t *diag);

/**
 * Record that a pass has recomputed geometry: the **layout** bits are satisfied.
 *
 * The narrower clearing a layout pass performs, and the difference from
 * `mark_clean` is one bit. A pass recomputes every conclusion this module reaches, so
 * it consumes LAYOUT, SUBTREE and OVERFLOW; it does not repaint, so it must not
 * consume STYLE -- a box left holding only STYLE is **not clean** and **does not need
 * layout**, which is precisely the pair of answers requirement 13 is about.
 *
 * Callers outside the pass exist for the same reason: a caller that has recomputed one
 * subtree by hand, or a paint phase that has been told its repaint is done, is saying
 * the same thing about the same bits.
 *
 * Safe on NULL and idempotent.
 */
robo_result_t robo_layout_tree_mark_recomputed(robo_layout_tree_t *tree);

/**
 * Record a computed-style change on box @p id, applying @p effect.
 *
 * The phase's style/layout boundary in one call, and the four cases it has:
 *
 *   NONE    nothing is marked. A property this build does not read cannot have
 *           changed a conclusion it made -- and marking would be the conservative
 *           answer that costs a whole document's geometry.
 *   PAINT   STYLE on @a id and nowhere else. No ancestor, no sibling: a colour is
 *           not inherited by a *box's* geometry and does not move anything.
 *   LAYOUT  the full geometry policy (below).
 *
 * A LAYOUT change on @a id is the `mark_layout_dirty` policy, and a caller that
 * already knows the reason passes it there rather than here.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p tree, a NONE id, an undefined
 *         @p effect, or an id that names no box in @p tree (ROBO_NOT_FOUND).
 */
robo_result_t robo_layout_tree_mark_style(robo_layout_tree_t *tree,
                                          robo_layout_node_id_t id,
                                          robo_layout_style_effect_t effect,
                                          robo_layout_diag_t *diag);

/**
 * Record a computed-style change on box @p id by **property name**, classifying it
 * first.
 *
 * The convenience of the pair above: a caller holding a stylesheet's spelling --
 * `"width"`, `"padding-left"`, `"background-color"` -- hands it here and gets the
 * policy without a second call. The classification is
 * `robo_layout_style_effect_for_property`, and the marking is
 * `robo_layout_tree_mark_style`, so the two doors cannot disagree.
 *
 * @return the same codes as `robo_layout_tree_mark_style`.
 */
robo_result_t robo_layout_tree_mark_style_property(robo_layout_tree_t *tree,
                                                   robo_layout_node_id_t id,
                                                   const char *property_name,
                                                   robo_layout_diag_t *diag);

/**
 * Record that box @p id's **own geometry** changed: a size, a margin, a padding, a
 * border, or anything else in the box model (requirement 8).
 *
 * The policy, and it is the phase's one propagation rule. Given @a id:
 *
 *   1. @a id itself gets LAYOUT + SUBTREE + OVERFLOW. All three, because a box
 *      whose own measured geometry moved owes a full re-derivation: its placement,
 *      everything it contains, and the rectangle its contents reached.
 *
 *   2. every **ancestor** gets SUBTREE + OVERFLOW. Not LAYOUT: an ancestor's own
 *      origin is computed from *its* containing block and does not move because a
 *      descendant grew. Its *extent* may, and its overflow almost certainly does --
 *      which is why OVERFLOW is on the ancestor and why this is the bit 10.13's
 *      second rectangle made necessary.
 *
 *   3. the **following flow** after @a id gets LAYOUT + SUBTREE + OVERFLOW: the
 *      following siblings of @a id and, walking up, the following siblings of every
 *      ancestor. This is the sibling rule (requirement 5), and it is transitive
 *      rather than one-deep for a reason a suite can state: a later sibling moves
 *      because an earlier one grew, and the sibling *after it* moves because the one
 *      between them did. A rule that marked only the immediate next sibling would be
 *      right for a two-box document and wrong for the third box of every other one.
 *
 * The alternative -- mark the document -- is what this avoids, and the difference is
 * observable: invalidating a leaf at the end of a long body leaves the boxes before
 * it clean, which is what `layout.invalidation.propagation` asserts.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p tree, a NONE id or an
 *         undefined @p reason; ROBO_NOT_FOUND for an id that names no box.
 */
robo_result_t robo_layout_tree_mark_layout_dirty(robo_layout_tree_t *tree,
                                                 robo_layout_node_id_t id,
                                                 robo_layout_invalidation_reason_t reason,
                                                 robo_layout_diag_t *diag);

/**
 * Record that box @p id's **contents** changed: a rebuild, a structural edit, a
 * classification release, a font-metric change (requirement 6).
 *
 * The same policy as `mark_layout_dirty` with one difference worth naming: every
 * **descendant** is marked as well as the box, because a subtree invalidation is
 * the one mark whose meaning *is* "everything under here". The ancestors and the
 * following flow are treated exactly as above -- a subtree that changed size is a
 * following-flow change, and a subtree whose contents changed may have changed what
 * its ancestors measured.
 *
 * @return the same codes as `robo_layout_tree_mark_layout_dirty`.
 */
robo_result_t robo_layout_tree_mark_subtree_dirty(robo_layout_tree_t *tree,
                                                  robo_layout_node_id_t id,
                                                  robo_layout_invalidation_reason_t reason,
                                                  robo_layout_diag_t *diag);

/**
 * Record that box @p id's **displacement** changed: `position` or an inset
 * (requirement 9).
 *
 * The narrowest of the three policies, and the reason it exists is the sentence the
 * brief is built on -- relative positioning "must not unnecessarily recompute
 * unrelated normal-flow sizing when only the final offset changed". So:
 *
 *   @a id gets LAYOUT + SUBTREE + OVERFLOW: where the box is drawn, where its
 *   contents therefore are, and the rectangle it reaches;
 *
 *   every **ancestor** gets OVERFLOW only. Not SUBTREE, because the box's *flow*
 *   contribution is unchanged -- it still occupies the space it always did, which is
 *   10.12's whole contract -- and not LAYOUT, because nothing about an ancestor
 *   moved. The ancestor's *measurement* did, because an offset can push a box out of
 *   its padding box;
 *
 *   the **following flow** is left **clean**. This is the mark's distinguishing
 *   feature and the one a suite can see: a sibling's placement is computed from the
 *   flow position, which did not move, so a following sibling that was correct before
 *   a `left: 10px` is correct after it.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p tree or a NONE id;
 *         ROBO_NOT_FOUND for an id that names no box. The reason is fixed to
 *         POSITION -- the mark *is* the reason, which is why this door takes none.
 */
robo_result_t robo_layout_tree_mark_position_dirty(robo_layout_tree_t *tree,
                                                   robo_layout_node_id_t id,
                                                   robo_layout_diag_t *diag);

/**
 * Record that box @p id's **overflow modes** changed: `overflow`, `overflow-x` or
 * `overflow-y` (requirement 7).
 *
 * A mode is not a size: which axis has a boundary changes what a *reader* of the
 * overflow record concludes and changes nothing about any box's geometry -- 10.13
 * says so at length. So:
 *
 *   @a id gets OVERFLOW only. Not LAYOUT (its geometry is correct) and not SUBTREE
 *   (its descendants' geometry is correct).
 *
 *   every **ancestor** gets OVERFLOW, because a box that stops clipping contributes
 *   a different region to its parent's measurement even though nothing moved;
 *
 *   the **following flow** is left clean, for the reason `mark_position_dirty`
 *   gives: nothing in the flow changed.
 *
 * This is the door that makes the OVERFLOW bit worth having as a bit of its own, and
 * a suite can assert it in one comparison: after an overflow-mode change the dirty
 * set is the box and its ancestors, and no box carries LAYOUT.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p tree or a NONE id;
 *         ROBO_NOT_FOUND for an id that names no box.
 */
robo_result_t robo_layout_tree_mark_overflow_dirty(robo_layout_tree_t *tree,
                                                   robo_layout_node_id_t id,
                                                   robo_layout_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_INVALIDATION_H */
