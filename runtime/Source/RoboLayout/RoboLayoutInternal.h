/*
 * RoboLayout internals (Phase 10.1).
 *
 * The one thing two translation units must agree on that the public headers
 * deliberately do not state: what a tree *is*. RoboLayoutTree.c owns the arena and
 * RoboLayoutObject.c reads it, so the struct lives here and nowhere else, and no
 * public header learns its layout.
 *
 * The arena is a single reallocating array of objects rather than a node-per-
 * allocation tree, and the choice is what makes the rest of the module simple:
 *
 *   - ids are indices plus one, so a lookup is arithmetic and a walk is a loop;
 *   - disposing a tree is one `robo_free`, so there is no recursive free to get
 *     wrong on a deep document (RoboHTML's 4096-node ceiling is the bound);
 *   - the links are ids rather than pointers, so a growth does not have to
 *     rewrite them -- which is the property that keeps realloc safe at all.
 *
 * The price is stated in the public header: a borrowed object pointer dies at the
 * next add.
 *
 * The second arena (Phase 10.2) is here for the same reason the first is: the
 * tree owns its formatting contexts, and the *only* producer is the pass -- which
 * means the two translation units that touch the arena are this file's and
 * RoboLayoutContext.c's. The public header exposes the read side; nothing public
 * can create or mutate a context, because a context is a conclusion the pass
 * draws and not a thing a caller may invent. That is why the doors below are
 * internal and why there is no `context_info` descriptor mirroring
 * robo_layout_object_info_t: a descriptor exists so several callers can build one
 * kind of thing, and here there is exactly one caller.
 */

#ifndef ROBO_LAYOUT_INTERNAL_H
#define ROBO_LAYOUT_INTERNAL_H

#include "RoboLayout/RoboLayoutBlock.h"
#include "RoboLayout/RoboLayoutBorder.h"
#include "RoboLayout/RoboLayoutInline.h"
#include "RoboLayout/RoboLayoutLineBox.h"
#include "RoboLayout/RoboLayoutMargin.h"
#include "RoboLayout/RoboLayoutOverflow.h"
#include "RoboLayout/RoboLayoutPadding.h"
#include "RoboLayout/RoboLayoutSizing.h"
#include "RoboLayout/RoboLayoutText.h"
#include "RoboLayout/RoboLayoutTree.h"

/** Objects the arena holds before its first growth. */
#define ROBO_LAYOUT_INITIAL_CAPACITY 8u

/**
 * Contexts the second arena holds before its first growth.
 *
 * Smaller than the object arena on purpose: a context exists only for a box that
 * establishes one, and most documents are mostly transparent boxes. A size that
 * guessed high would be a size the small fixtures pay for and the deep ones do
 * not.
 */
#define ROBO_LAYOUT_INITIAL_CONTEXT_CAPACITY 4u

/**
 * Line boxes and line members the third and fourth arenas hold before their first
 * growth (Phase 10.6).
 *
 * Both small, and for the reason the context arena is: a document with one line has
 * one line and as many members as it placed, so a size that guessed high is a size
 * every small fixture pays for. They are separate arenas because their growth rates
 * are unrelated -- a line has one record and many members -- and a single arena of
 * a union would waste the larger of the two on every entry.
 */
#define ROBO_LAYOUT_INITIAL_LINE_CAPACITY 4u
#define ROBO_LAYOUT_INITIAL_LINE_MEMBER_CAPACITY 8u

/*
 * The walk (Phase 10.3, shared with 10.4)
 *
 * One pass lays both directions out, and it is split across two translation units
 * along the line the two algorithms divide on: the block direction stacks in
 * RoboLayoutBlock.c, the inline direction places along a line in
 * RoboLayoutInline.c, and each calls the other. The coupling is not a convenience
 * -- an inline formatting context's height *is* the block extent of the box that
 * owns it, so the two cannot be separate passes without re-stacking every
 * ancestor afterwards -- and what crosses the boundary is this record, holding the
 * tree, the two stats records and the diagnostic sink, plus the two doors below,
 * one defined per file.
 */

/**
 * One pass's shared state.
 *
 * Borrowed throughout and owned by `robo_layout_block_layout`, which fills it
 * once and hands the same pointer to every level of both recursions. A record on
 * the caller's stack rather than a heap object, because nothing here outlives the
 * pass that made it -- and because a pass that allocated its own scratch would be
 * a pass that could fail for a reason its caller cannot see.
 */
typedef struct robo_layout_walk {
    robo_layout_tree_t *tree;
    robo_layout_block_stats_t *block;
    robo_layout_inline_stats_t *inline_flow;
    /**
     * The text direction's counters (10.5), filled by the inline pass where a text
     * participant is measured. The third record for the same reason there are two:
     * the interesting fact is the *mix*, and "how much of this text did anything
     * measure?" is not a question either of the other two can answer.
     */
    robo_layout_text_stats_t *text;
    /**
     * Who measures text, borrowed for the length of the pass (10.5).
     *
     * *Not* on the tree, and the difference matters: the tree is derived state that
     * outlives every pass and is rebuilt from the DOM, while a measurer is an input
     * to one pass -- the same shape the viewport has, which is recorded on the
     * context and handed to the pass rather than stored on the tree. NULL means
     * "nothing can measure", which is the documented degradation and not an error:
     * every run measures to zero and the document lays out exactly as 10.4 left it.
     */
    const robo_layout_text_metrics_t *text_metrics;
    robo_layout_diag_t *diag;
} robo_layout_walk_t;

/**
 * Lay out @p owner's contents in the **block** direction and report the extent
 * they reached. Defined by RoboLayoutBlock.c; called by the inline pass for an
 * atomic inline, whose contents are a formatting context of their own.
 *
 * It also records whether the owner stacked anything, because that is the same
 * fact the block pass records for every box whose contents it stacked.
 */
robo_result_t robo_layout_block_walk_children(robo_layout_walk_t *walk,
                                              robo_layout_node_id_t owner,
                                              int32_t content_width,
                                              robo_gfx_pixel_y_t *out_extent);

/**
 * Lay out @p owner's contents as an **inline** formatting context and report the
 * extent the lines reached. Defined by RoboLayoutInline.c; called by the block
 * pass for every box that establishes one (RoboLayoutInline.h).
 */
robo_result_t robo_layout_inline_walk_contents(robo_layout_walk_t *walk,
                                               robo_layout_node_id_t owner,
                                               int32_t content_width,
                                               robo_gfx_pixel_y_t *out_extent);

/*
 * The one style read every direction makes.
 *
 * 10.3 defined these in RoboLayoutBlock.c, "because that is where it was first
 * needed and where its rules are argued", and predicted that 10.8 would change the
 * rule. It did, and this is where the prediction landed: the four checks now live in
 * RoboLayoutSizing.c as one *classification* that four readers shape -- the dimension
 * reader below (three kinds), `robo_layout_style_px_length` (a px length or nothing),
 * 10.9's margin reader (a **signed** px length or nothing), and 10.10's padding reader
 * (a **non-negative** px length or nothing). `font-size` needs the second because a
 * font size is not a box dimension and `12.5px` has to stay `12.5`, and keeping one
 * classification behind all four is what stops `auto` and `50%` from being folded
 * together at the seam.
 *
 * 10.9 opened the classification's one parameter rather than copying it, and the
 * parameter is the whole of the difference between a box dimension and a margin:
 * `width: -10px` is an invalid declaration and `margin: -10px` is a technique. 10.10
 * is the parameter read the *other* way -- `padding: -10px` is invalid too, so the
 * padding reader passes the same flag a dimension does and differs from the margin
 * reader by nothing else at all. The
 * *rules* -- the entry must exist, the keyword is `auto` or unusable, a LENGTH in px
 * is usable when it is finite and inside the geometry's range, anything else is
 * unusable -- are one rule and stay in one place.
 *
 * Shared rather than copied, for the reason the module shares everything: "a declared
 * value this build can resolve is a computed LENGTH in px, finite and inside the
 * geometry's range" is one rule, and two passes with their own copies of it would be
 * two answers to one question.
 */

/**
 * The largest magnitude a declared length may have and still be usable.
 *
 * A `double` that fits the geometry: `robo_gfx_pixel_x_t` is int32_t, and a declared
 * length of `3e9px` is not a size a box can have. It bounds the *length* rather than
 * the geometry, so it is the seam's number and it belongs beside the seam's other
 * vocabulary: 10.8's classification compares against it, 10.9's margin reader reads
 * the same comparison through that classification, and the availability clamp in
 * RoboLayoutMargin.c floors a containing block into the same range. Named for the
 * seam rather than for sizing since 10.9, because two layers now read it.
 */
#define LAYOUT_STYLE_LENGTH_MAX 2147483647.0

/**
 * A 64-bit sum clamped to the geometry's range (Phase 10.15).
 *
 * The block pass's cursor is an **accumulation**, and an accumulation is the one kind of
 * arithmetic in this module that a document can make unbounded without any single
 * declaration being unreasonable: two boxes each near the geometry's maximum have a sum
 * larger than any `int32`, and the wrapped result is *negative*. That matters beyond a
 * wrong number, because `robo_layout_sizing_set_content` refuses a negative extent --
 * correctly, since a size is never negative -- so an overflowing cursor used to make the
 * whole pass **fail** on a document that is merely tall.
 *
 * The padding, border and margin layers each state this rule for their own sums (one
 * clamp on the sum, saturated rather than refused, "because there is no document an
 * author can write that some number cannot describe"). This is the same rule for the
 * pass's own arithmetic, in one place, so the pass and the layers cannot disagree about
 * the bound -- and so a saturated extent is still a rectangle a renderer can draw.
 */
static inline int32_t layout_extent_clamp(int64_t value)
{
    if (value > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        return (int32_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    if (value < -(int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        return (int32_t)(-(int64_t)LAYOUT_STYLE_LENGTH_MAX);
    }
    return (int32_t)value;
}

/**
 * What a computed style's declaration holds, before anybody decides what to do with
 * it. RoboLayoutSizing.c owns the classification and its readers; 10.9's margin seam
 * is its second *shape*, which is why the vocabulary is here rather than in that
 * file.
 */
typedef enum layout_style_value_kind {
    /** No entry, or an entry that determines nothing at all. */
    LAYOUT_STYLE_VALUE_ABSENT = 0,
    /** The keyword `auto`. */
    LAYOUT_STYLE_VALUE_AUTO = 1,
    /** A usable px length, in the classification's number output. */
    LAYOUT_STYLE_VALUE_PX = 2,
    /** Declared, and not usable here: another unit, a percentage, or out of range. */
    LAYOUT_STYLE_VALUE_UNUSABLE = 3
} layout_style_value_kind_t;

/**
 * Classify what @p style declares for @p property.
 *
 * The seam's one rule, in one place, with signedness as its one parameter.
 *
 * @param allow_negative a box dimension refuses a negative length; a margin uses one.
 * @return 1 when the question could be asked; 0 for a NULL @p style or @p property,
 *         which leaves both outputs untouched -- the contract that makes "a box with
 *         no style has made no declarations" true rather than a special case.
 */
int robo_layout_style_classify(const robo_css_computed_style_t *style,
                               const char *property,
                               int allow_negative,
                               layout_style_value_kind_t *out_kind,
                               double *out_number);

/**
 * Fill @p sizing from one box (10.8): available width, declared dimensions and
 * bounds. Defined by RoboLayoutBlock.c, called by both directions.
 *
 * The one place a `robo_layout_object_t` is turned into a sizing question, so that a
 * block box and an atomic inline cannot disagree about what "this box declares no
 * width" means. The *containing block* is the caller's to name, because it is the
 * one thing that differs between the two directions.
 */
void robo_layout_object_sizing(robo_layout_sizing_t *sizing,
                               const robo_layout_object_t *object,
                               int32_t available_width);

/*
 * The overflow seam (Phase 10.13)
 *
 * Two doors, and they are here rather than in the public header for the reason the
 * walk is: they are the *pass's* arithmetic, and a caller with a box has the answer
 * already -- the record is on the object, and `robo_layout_object_overflow` reads it
 * back through validation. What a pass needs and a caller does not is the two below:
 * the box's own measurement (which reads its children), and the contribution one child
 * makes to its parent (which reads the child's own record).
 */

/**
 * Measure @p object's overflow against its own box and write it to @p out.
 *
 * The one place a box's four extents are derived, so a block box and an atomic inline
 * cannot disagree about what "my contents reached past my padding box" means. The
 * inputs are the box's own geometry -- the border box a result carries, and the two
 * inset records that turn it into a padding box and locate its contents -- because the
 * two passes have those in hand at the moment they call this and neither of them owns
 * the other's numbers.
 *
 * Every placed child contributes once, through its **region**: its padding box grown by
 * its own extents and clipped back on an axis it hides. Depths below a child are already
 * inside that child's region, which is what makes this function O(children) rather than
 * O(subtree) and is argued at the loop that does it.
 *
 * @return ROBO_OK and @p out; ROBO_INVALID_ARGUMENT for a NULL argument or a negative
 *         box; ROBO_ERROR for a child link that names no box.
 */
robo_result_t robo_layout_overflow_measure(const robo_layout_tree_t *tree,
                                           const robo_layout_object_t *object,
                                           const robo_layout_border_edges_t *borders,
                                           const robo_layout_padding_edges_t *padding,
                                           robo_layout_size_t border_box,
                                           robo_layout_overflow_edges_t *out,
                                           robo_layout_diag_t *diag);

/**
 * The **region** @p object's contents occupy, with its padding box placed at
 * @p padding_origin.
 *
 * The child side of the recursion above: whatever space the caller is working in, this
 * is the rectangle the object occupies there, and folding a rectangle into a record is
 * `robo_layout_overflow_edges_include`'s job rather than this one's. An object with no
 * computed result -- one a pass declined -- has no region; its padding box is empty and
 * its extents are zero, which is the honest answer rather than a failure, because the
 * object was already counted for the reason it has no geometry.
 */
robo_layout_rect_t robo_layout_overflow_object_region(const robo_layout_object_t *object,
                                                     robo_layout_point_t padding_origin);

/** The declared px length @p style holds for @p property, unrounded, or 0 when
 *  there is none this build can resolve. The reader `font-size` goes through. */
int robo_layout_style_px_length(const robo_css_computed_style_t *style,
                                const char *property,
                                double *out_value);

/** Round a resolved length to the integer pixel space (half away from zero). */
robo_gfx_pixel_x_t robo_layout_px_round(double value);

/*
 * The line-box arenas (Phase 10.6)
 *
 * Four arenas now live on the tree, in two pairs, and the pairs exist for the same
 * reason the context one does: a line box is a *conclusion a pass draws*, so the
 * tree owns it, the pass is its only writer, and the public header exposes the read
 * side. A caller cannot create a line, which is what makes `state` a fact rather than
 * a promise.
 *
 * The **member** records are the interesting half. A line's members are the objects
 * it placed, in layout order, and they are a **chain** rather than a contiguous range
 * in one shared vector -- because the placements of one line are *interleaved* with
 * the placements of another. Laying out an atomic inline recurses into the block pass
 * for its contents (10.3's door), and those contents build lines and members of their
 * own before the atomic inline itself is placed on its parent's line. So the arena
 * holds the members of several lines in an order no range can describe, each record
 * names the next member of *its* line, and the line names the head. Reading is
 * therefore O(members) rather than O(1), which the public door states.
 */

/** Identity of one member record in the member arena. Internal: a member is not a
 *  thing a caller names, it is a thing a line contains. */
typedef uint32_t layout_line_member_id_t;

#define LAYOUT_LINE_MEMBER_ID_NONE ((layout_line_member_id_t)0u)

/** One object placed on one line, and the next member of the same line. */
typedef struct layout_line_member {
    robo_layout_node_id_t node;
    layout_line_member_id_t next;
} layout_line_member_t;

/**
 * One line box as stored: the public record plus the one fact the public record does
 * not carry.
 *
 * `first_member` is internal because it names an *internal* id, and a caller with no
 * way to resolve one would be handed a handle to nothing. What a caller gets instead
 * is the count (in the record) and `robo_layout_line_member(tree, line, index, ...)`,
 * which walks the chain and hands back the *object* id -- the thing a caller can
 * actually use.
 */
typedef struct layout_line_slot {
    robo_layout_line_box_t box;
    layout_line_member_id_t first_member;
} layout_line_slot_t;

struct robo_layout_tree {
    robo_layout_object_t *objects;
    /** Live objects; also the number of ids handed out. */
    size_t count;
    /** Slots allocated; always >= count. */
    size_t capacity;
    /** The next id to hand out; ids are 1-based and dense. */
    robo_layout_node_id_t next_id;

    /*
     * The formatting-context arena (Phase 10.2). Lazily allocated -- a tree that
     * is built and never classified owns no contexts and costs no allocation --
     * and *released* rather than reused when the classification is invalidated,
     * because a stale participant count is exactly the kind of half-truth this
     * module refuses to keep.
     */
    robo_layout_formatting_context_t *contexts;
    /** Live contexts; also the number of ids handed out. */
    size_t context_count;
    /** Slots allocated; always >= context_count. */
    size_t context_capacity;
    /** Non-zero once a pass has classified this tree. */
    int classified;

    /*
     * The invalidation count (Phase 10.14).
     *
     * A *count* and not a set, because every question this module asks about
     * dirtiness is either per box (answered from the box's own record) or a total
     * (answered here). There is deliberately no stored "dirty root": the shallowest
     * dirty box is a function of the tree and is computed when it is asked for
     * (`robo_layout_tree_invalidation_root`), which is O(n) once per pass instead of a
     * field that five propagation steps have to keep in agreement.
     *
     * `objects` are zeroed wholesale by a clear, and a clear sets this to 0 with
     * them. An add increments it, and nothing else can make it smaller except a mark
     * that clears a box and a pass that consumes the layout bits.
     */
    size_t invalidated;

    /*
     * The line-box arena (Phase 10.6). Lazily allocated like the context one, and
     * released -- with the member arena -- by the next pass and by anything that
     * releases the classification, because a line box describes a box set and a pass
     * and neither outlives the other.
     *
     * Member ids follow the same discipline as every other id here -- dense,
     * 1-based, made at the moment of the append and never reused -- but there is no
     * `next_member_id` counter, because `member_count + 1` *is* the next id and a
     * second field that had to agree with the first would be a second answer to it.
     */
    layout_line_slot_t *lines;
    /** Live line boxes; also the number of ids handed out. */
    size_t line_count;
    /** Slots allocated; always >= line_count. */
    size_t line_capacity;
    layout_line_member_t *members;
    /** Live member records; also the number of ids handed out. */
    size_t member_count;
    /** Slots allocated; always >= member_count. */
    size_t member_capacity;
};

/**
 * Mark one box dirty without propagating (10.14).
 *
 * The door a **build** uses, and it is deliberately not one of the four public
 * marking policies: a box that has just been created owes a recomputation, and it has
 * no descendants and no following flow yet to tell about it -- so the full policy
 * would be a walk over links that cannot exist. It also keeps `add` O(1) in the depth
 * of the tree, which matters because every document build is a sequence of adds.
 *
 * Bumps the tree's invalidated count, so the door is the only thing a build has to
 * remember about the phase.
 */
void robo_layout_invalidation_mark_new(robo_layout_tree_t *tree,
                                       robo_layout_object_t *object);

/**
 * The object as stored, or NULL when @p id names none.
 *
 * Deliberately not public: it is the accessor that skips the id-to-index
 * conversion's validation, and every public door in RoboLayoutObject.h does that
 * validation itself so that a caller cannot reach a box by an id it was never
 * given.
 */
robo_layout_object_t *robo_layout_object_slot(robo_layout_tree_t *tree,
                                              robo_layout_node_id_t id);

/** Same, for the read-only doors. */
const robo_layout_object_t *robo_layout_object_slot_const(const robo_layout_tree_t *tree,
                                                          robo_layout_node_id_t id);

/** The array itself, for the walk a pass does. NULL when empty. */
const robo_layout_object_t *robo_layout_object_array(const robo_layout_tree_t *tree);

/** Resolve @p id, filling @p out_index. 0 when the id names no box. */
int robo_layout_object_index(const robo_layout_tree_t *tree,
                             robo_layout_node_id_t id,
                             size_t *out_index);

/*
 * The context arena, internal because the pass is its only writer.
 */

/** Release every context and clear the classified flag. Independent of the box
 *  arena: the boxes are untouched, because invalidating a classification is not
 *  the same act as clearing a tree. */
void robo_layout_tree_contexts_reset(robo_layout_tree_t *tree);

/**
 * Append one context, growing the arena if it must.
 *
 * The parent is *not* validated here, because the caller is the pass and the pass
 * establishes contexts in creation order -- a parent is therefore always already
 * present, and a second walk to prove it would be work that cannot fail. Owners,
 * on the other hand, are validated: an owner id that names no box would leave a
 * context nothing can reach.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments, a NONE owner, an
 *         undefined kind or a NONE parent for a non-ROOT kind; ROBO_NOT_FOUND for
 *         an owner that names no box; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_layout_tree_context_add(robo_layout_tree_t *tree,
                                           robo_layout_fc_kind_t kind,
                                           robo_layout_node_id_t owner,
                                           robo_layout_fc_id_t parent,
                                           uint32_t depth,
                                           robo_layout_fc_id_t *out_id,
                                           robo_layout_diag_t *diag);

/** Count one more participant in @p id. A NONE or unknown id is ignored: the
 *  pass calls this once per box and a bug there must not corrupt a count. */
void robo_layout_tree_context_add_participant(robo_layout_tree_t *tree,
                                              robo_layout_fc_id_t id);

/** Mark the tree's classification complete. */
void robo_layout_tree_mark_classified(robo_layout_tree_t *tree);

/** Mutable context slot, or NULL when @p id names none. Internal for the same
 *  reason the object slot accessor is: it skips the validation the public doors
 *  perform, so it must not be reachable from outside the module. */
robo_layout_formatting_context_t *robo_layout_fc_slot(robo_layout_tree_t *tree,
                                                      robo_layout_fc_id_t id);

/** Same, for the read-only doors -- the pair mirrors the object arena's, so that
 *  a const tree is never read through a cast-away. */
const robo_layout_formatting_context_t *robo_layout_fc_slot_const(
    const robo_layout_tree_t *tree,
    robo_layout_fc_id_t id);

/*
 * The line-box arena (Phase 10.6), internal for the same reason the context arena
 * is: the inline pass is its only writer, and there is exactly one caller of each
 * door below, so a public descriptor would exist to be used once.
 */

/**
 * Release every line box and every member record, and clear each object's `line`
 * id.
 *
 * Called by the next pass, before it places anything, and by anything that releases
 * the classification -- so a second pass *replaces* the first one's lines rather than
 * appending to them, and a box set that changed cannot leave a line describing a box
 * that is gone. Independent of the object arena: the boxes and their results are
 * untouched, because releasing a pass's conclusions is not the same act as clearing a
 * tree.
 *
 * Object ids handed out in a *previous* pass therefore remain valid (they name boxes),
 * while line ids do not: a line id is valid until the next pass, which the public read
 * doors state.
 */
void robo_layout_tree_lines_reset(robo_layout_tree_t *tree);

/**
 * Append @p node to the member chain @p head .. @p tail and count it in @p count.
 *
 * One door rather than "allocate a record" plus "link it", because the two are one
 * act: a record that was allocated and not linked would be a member no line names, and
 * a link into an unallocated id would be a chain that ends in nothing. The pass holds
 * the head, the tail and the count on its stack for the length of one open line --
 * construction state, which is exactly where RoboLayoutLineBox.h says it lives -- and
 * hands them over on every placement.
 *
 * `*head` is NONE for an empty chain and is filled on the first append, which is why
 * the pass cannot tell "no members yet" from "the first append failed" by looking at
 * the head: it looks at the return code, and that is the distinction.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a NONE node;
 *         ROBO_OUT_OF_MEMORY when the arena could not grow -- which the pass must
 *         turn into a failure rather than a line, because a placement the line could
 *         not *record* would leave a line whose width and member count disagreed.
 */
robo_result_t robo_layout_line_members_append(robo_layout_tree_t *tree,
                                             robo_layout_node_id_t node,
                                             layout_line_member_id_t *head,
                                             layout_line_member_id_t *tail,
                                             size_t *count,
                                             robo_layout_diag_t *diag);

/**
 * Append one finalized line box and return its id.
 *
 * The caller supplies the whole record, because the pass holds the construction state
 * and this is the single moment it becomes a record. `context` must name a live
 * context: a line that names no context is a line nothing can attribute, and the pass
 * reads the id off the box that established the context rather than inventing one.
 *
 * `baseline` is checked rather than trusted, because it is the one field of the record
 * the arena can validate *absolutely*: 10.7 requires every stored line to have a usable
 * baseline (`RoboLayoutLineBox.h`), so a NONE kind or a negative term is refused here
 * instead of being stored and read thirty lines later by a renderer. `size.height` is
 * deliberately **not** checked against the baseline's extent -- that identity is the
 * pass's arithmetic rather than an invariant of the type, and a door that enforced it
 * would hide the bug it is a symptom of.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p tree or @p out_id, a NONE
 *         owner, an undefined state or a NONE context, or an unusable baseline;
 *         ROBO_NOT_FOUND for a context that names none; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_layout_line_add(robo_layout_tree_t *tree,
                                   robo_layout_fc_id_t context,
                                   robo_layout_node_id_t owner,
                                   size_t index,
                                   robo_layout_point_t origin,
                                   robo_layout_size_t size,
                                   robo_layout_baseline_metrics_t baseline,
                                   int32_t available_width,
                                   layout_line_member_id_t first_member,
                                   size_t members,
                                   robo_layout_line_id_t *out_id,
                                   robo_layout_diag_t *diag);

/** The stored line, or NULL when @p id names none. */
const layout_line_slot_t *robo_layout_line_slot_const(const robo_layout_tree_t *tree,
                                                      robo_layout_line_id_t id);

/** Member record @p id, or NULL when it names none. */
const layout_line_member_t *robo_layout_line_member_slot_const(
    const robo_layout_tree_t *tree,
    layout_line_member_id_t id);

#endif /* ROBO_LAYOUT_INTERNAL_H */
