/*
 * RoboPaint vocabularies (Phase 11.1–11.2).
 *
 * Two closed enumerations and their counters, in the shape every module in this tree
 * uses: an enumerator list with an explicit `NONE`/`COUNT`, an `is_known` predicate, and
 * a `name` door that never returns NULL. The shape is not decoration -- a suite that
 * walks the vocabulary and checks the count against the names is how a phase that adds
 * an enumerator is *made* to add its name, rather than forgetting and shipping a
 * value that prints "unknown".
 *
 * The **drawing** vocabulary is deliberately small -- one background and four border
 * sides -- and the reason is the same one RoboLayout 10.2 gives for not adding a constant
 * for a feature it does not have: a name with no code behind it is a promise, not a
 * vocabulary. So there is no `TEXT` operation and no `IMAGE` operation and no `CLIP`,
 * `SAVE`, `RESTORE` or `OPACITY` in this release, even though the brief that asks for
 * this architecture lists all six as things the representation must *eventually* be able
 * to express. What makes them expressible is the record's shape rather than an enumerator
 * nobody emits: an operation is a tagged record with a node, a depth, three rectangles
 * and a clip, and adding a kind is one enumerator, one name row and one case in the
 * validity rule -- never a redesign of the list. `CONTENT` (11.2) is the one kind added
 * since 11.1, and it is the one the brief's record sequence names that the first release
 * did not emit.
 *
 * The four border kinds are four enumerators rather than one kind plus a side field,
 * and that is a decision about **invalid states** rather than about verbosity: a `side`
 * field would have to carry a "not a border" value for the other three kinds, and a
 * field that means nothing for most of its records is the "shape that reads as an
 * answer" this module's neighbours refuse everywhere -- `RoboLayoutBorder.h` argues the
 * same point about a colour's kind. With a kind per side there is no such value to get
 * wrong, and `robo_paint_op_kind_side` is a total function from the kind to the side it
 * draws on.
 */
#ifndef ROBO_PAINT_TYPES_H
#define ROBO_PAINT_TYPES_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSBox.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What one paint operation is.
 *
 * A closed vocabulary in the order a linear consumer meets it, and the order matters:
 * `BEGIN_BOX`, then the two kinds of decoration in the order CSS paints them
 * (background first, then the border), then `CONTENT` -- the boundary between a box's
 * own records and its descendants' -- then `END_BOX`. A consumer that walked the list
 * and ignored every `kind` would still see each box's operations contiguous and in
 * paint order, which is the property `paint/docs/Operations.md` states as the
 * traversal's contract.
 *
 * The vocabulary splits in two, and the split is the rule a builder obeys rather than a
 * fact about this release. A **structural** record (`BEGIN_BOX`, `CONTENT`, `END_BOX`)
 * draws nothing, carries no colour and is emitted for **every** placed box, so the
 * sequence has a regular skeleton. A **drawing** record (`BACKGROUND`, the four
 * `BORDER_*` sides and, since 11.6, `TEXT`) is emitted **only when it draws**, so the
 * list's length is a statement about the document. `robo_paint_op_kind_draws` and
 * `robo_paint_op_kind_is_structural` are the two predicates, and they partition the
 * vocabulary above `NONE`.
 */
typedef enum robo_paint_op_kind {
    /** Nothing: a cleared record. Never appended to a list. */
    ROBO_PAINT_OP_NONE = 0,
    /**
     * Enter a box: the marker that begins the operations of one placed box.
     *
     * It draws nothing and carries no colour. It exists because a flat list still has
     * to say **where a box's operations begin and end**: a rasteriser that will one day
     * push a clip, save a state or blend a group needs a boundary to hang that on, and
     * the depth this operation carries is what makes the tree's shape recoverable from
     * the sequence.
     */
    ROBO_PAINT_OP_BEGIN_BOX = 1,
    /**
     * Fill the box's background.
     *
     * The painting area is the **border box**, which is CSS's own initial
     * `background-clip` -- the background extends under the border rather than stopping
     * at the padding edge. `background-clip` and `background-origin` are not read in
     * this release and are a later goal's; what this operation claims is the initial
     * value, not a simplification of it.
     */
    ROBO_PAINT_OP_BACKGROUND = 2,
    /** Draw the top border: the band between the border box and the padding box. */
    ROBO_PAINT_OP_BORDER_TOP = 3,
    /** Draw the right border. */
    ROBO_PAINT_OP_BORDER_RIGHT = 4,
    /** Draw the bottom border. */
    ROBO_PAINT_OP_BORDER_BOTTOM = 5,
    /** Draw the left border. */
    ROBO_PAINT_OP_BORDER_LEFT = 6,
    /**
     * A box's own painting is over and its contents begin.
     *
     * The record that makes the boundary between a box's own records and its
     * descendants' **explicit**, and it is emitted for every placed box even when the
     * box declares no background, draws no border and has no children -- because the
     * thing it states is a fact about the sequence rather than about the document.
     * Without it a consumer would have to infer where one box's own records end by
     * looking at the *next* child's depth, and a box with no children would have no
     * boundary at all; with it every box's own painting is the contiguous group
     * `[BEGIN_BOX .. CONTENT]` and its subtree is `(CONTENT .. END_BOX)`, readable
     * without consulting the tree.
     *
     * It carries the **content box** as the rectangle it acts on -- the region a later
     * clip, scroll or content-origin record will need -- and draws nothing, so it is a
     * structural record and its colour is `none` (see the vocabulary note above).
     */
    ROBO_PAINT_OP_CONTENT = 7,
    /**
     * Draw one text run's glyphs (11.6).
     *
     * The first drawing this module has that is **not a rectangle filled with a colour**,
     * and the only record whose payload is variable-length data. A text node generates a
     * layout object of kind `TEXT` (`RoboLayoutTypes.h`) rather than a box, so this record
     * is emitted where that object sits **in the sequence** -- as a child of the box whose
     * element owns the text node, after that box's `CONTENT` and in document order among
     * its siblings -- and a text object gets **no** `BEGIN_BOX`/`CONTENT`/`END_BOX` triple,
     * because it is not a box and the skeleton is a statement about boxes. That there is
     * no `TEXT_BEGIN`/`TEXT_END` pair around it is the point: a run has nothing to open,
     * nothing to clip for and no children, so a pair would be a boundary a consumer could
     * only skip.
     *
     * It is a **drawing** kind, so it carries a colour a rasteriser can use -- the run's
     * inherited `color` -- and it is held to the same "emit only when it draws" rule every
     * other drawing kind is: a run whose colour is transparent, whose text is empty, or
     * whose range could not be read produces no operation rather than an invisible one.
     *
     * The characters live in the list's **text pool** and this record carries the
     * **span** that names them (`RoboPaintText.h` argues why characters rather than glyph
     * ids, and why a span rather than a pointer). The font inputs and the baseline travel
     * beside it in the `text` member.
     */
    ROBO_PAINT_OP_TEXT = 8,
    /** Leave a box: the marker that closes the operations of one placed box. */
    ROBO_PAINT_OP_END_BOX = 9
} robo_paint_op_kind_t;

/** Number of defined operation kinds. */
#define ROBO_PAINT_OP_KIND_COUNT 10u

/** Non-zero if @p kind is a defined operation kind. */
int robo_paint_op_kind_is_known(robo_paint_op_kind_t kind);

/** Human-readable kind name; never NULL; "unknown" when undefined. */
const char *robo_paint_op_kind_name(robo_paint_op_kind_t kind);

/** Non-zero when @p kind is one of the four border kinds. */
int robo_paint_op_kind_is_border(robo_paint_op_kind_t kind);

/**
 * The side @p kind draws on.
 *
 * A total function over the four border kinds and a **refusal** for every other kind,
 * which is what removes the sentinel a `side` field would have needed. A caller that
 * wants "does this operation draw a side, and which?" asks this once:
 *
 * @return ROBO_OK and the side; ROBO_INVALID_ARGUMENT for NULL @p out or an @p kind
 *         that is not a border kind (@p out is untouched).
 */
robo_result_t robo_paint_op_kind_side(robo_paint_op_kind_t kind, robo_css_box_side_t *out);

/**
 * Non-zero when @p kind **puts a pixel down**: `BACKGROUND`, one of the four `BORDER_*`
 * sides, or `TEXT`. It is the predicate a builder asks before emitting a decoration, and
 * the split the validity rule uses -- a kind that draws must carry a colour a rasteriser
 * can use, and a kind that does not must carry `none`.
 */
int robo_paint_op_kind_draws(robo_paint_op_kind_t kind);

/**
 * Non-zero when @p kind is **structural**: a record that draws nothing and is emitted
 * for every placed box, namely `BEGIN_BOX`, `CONTENT` and `END_BOX`. It is the exact
 * complement of @ref robo_paint_op_kind_draws above `NONE`.
 */
int robo_paint_op_kind_is_structural(robo_paint_op_kind_t kind);

/**
 * Non-zero when @p kind is a **box marker** rather than a drawing or the content
 * boundary: `BEGIN_BOX`/`END_BOX`. Narrower than
 * @ref robo_paint_op_kind_is_structural, which also covers `CONTENT`.
 */
int robo_paint_op_kind_is_marker(robo_paint_op_kind_t kind);

/*
 * Diagnostic domains. The same discipline the modules below this one use: a call
 * returns a plain robo_result_t and an optional caller-owned record refines the failure
 * with a domain and a message. There is no global mutable error state, and a paint
 * failure is never reported as a layout failure -- "the tree was not laid out" is
 * RoboLayout's answer and "the list could not grow" is this module's.
 */
typedef enum robo_paint_diagnostic_domain {
    ROBO_PAINT_DOMAIN_NONE = 0,
    /** Module lifecycle: not initialized, already initialized. */
    ROBO_PAINT_DOMAIN_MODULE = 1,
    /** The paint context: creation, the tree it ran on, its pass record. */
    ROBO_PAINT_DOMAIN_CONTEXT = 2,
    /** One operation: its kind, its fields, its validity. */
    ROBO_PAINT_DOMAIN_OPERATION = 3,
    /** The list: growth, capacity, the append. */
    ROBO_PAINT_DOMAIN_LIST = 4,
    /** Reading a box's geometry out of a layout result. */
    ROBO_PAINT_DOMAIN_GEOMETRY = 5,
    /** The traversal: a link that names nothing, a tree that was not laid out. */
    ROBO_PAINT_DOMAIN_TRAVERSAL = 6,
    /** The clip in force: the intersection, the mode accumulation. */
    ROBO_PAINT_DOMAIN_CLIP = 7,
    /** The text dump a developer reads. */
    ROBO_PAINT_DOMAIN_DUMP = 8,
    /**
     * The display list and its builder (11.3): the list's growth, a record that resolves
     * to no command, and a record list that is not a traversable document.
     *
     * Its own domain rather than `LIST` or `OPERATION`, because a display list that could
     * not be built is a failure of a *different stage* than a paint list that could not
     * grow -- and a reader sent to the wrong stage is a reader sent to the wrong phase.
     */
    ROBO_PAINT_DOMAIN_DISPLAY_LIST = 9
} robo_paint_diagnostic_domain_t;

/** Number of defined diagnostic domains. */
#define ROBO_PAINT_DOMAIN_COUNT 10u

/** Non-zero if @p domain is a defined domain. */
int robo_paint_domain_is_known(robo_paint_diagnostic_domain_t domain);

/** Human-readable domain name; never NULL; "unknown" when undefined. */
const char *robo_paint_domain_name(robo_paint_diagnostic_domain_t domain);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_TYPES_H */
