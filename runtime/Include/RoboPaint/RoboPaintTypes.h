/*
 * RoboPaint vocabularies (Phase 11.1).
 *
 * Two closed enumerations and their counters, in the shape every module in this tree
 * uses: an enumerator list with an explicit `NONE`/`COUNT`, an `is_known` predicate, and
 * a `name` door that never returns NULL. The shape is not decoration -- a suite that
 * walks the vocabulary and checks the count against the names is how a phase that adds
 * an enumerator is *made* to add its name, rather than forgetting and shipping a
 * value that prints "unknown".
 *
 * The operation vocabulary is deliberately **seven wide**, and the reason is the same
 * one RoboLayout 10.2 gives for not adding a constant for a feature it does not have: a
 * name with no code behind it is a promise, not a vocabulary. So there is no `TEXT`
 * operation and no `IMAGE` operation and no `CLIP`, `SAVE`, `RESTORE` or `OPACITY` in
 * this release, even though the brief that asks for this architecture lists all six as
 * things the representation must *eventually* be able to express. What makes them
 * expressible is the record's shape rather than an enumerator nobody emits: an operation
 * is a tagged record with a node, a depth, three rectangles and a clip, and adding a
 * kind is one enumerator, one name row and one case in the validity rule -- never a
 * redesign of the list.
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
 * (background first, then the border), then `END_BOX`. A consumer that walked the list
 * and ignored every `kind` would still see each box's operations contiguous and in
 * paint order, which is the property `paint/docs/Operations.md` states as the
 * traversal's contract.
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
    /** Leave a box: the marker that closes the operations of one placed box. */
    ROBO_PAINT_OP_END_BOX = 7
} robo_paint_op_kind_t;

/** Number of defined operation kinds. */
#define ROBO_PAINT_OP_KIND_COUNT 8u

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

/** Non-zero when @p kind is a marker rather than a drawing: `BEGIN_BOX`/`END_BOX`. */
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
    ROBO_PAINT_DOMAIN_DUMP = 8
} robo_paint_diagnostic_domain_t;

/** Number of defined diagnostic domains. */
#define ROBO_PAINT_DOMAIN_COUNT 9u

/** Non-zero if @p domain is a defined domain. */
int robo_paint_domain_is_known(robo_paint_diagnostic_domain_t domain);

/** Human-readable domain name; never NULL; "unknown" when undefined. */
const char *robo_paint_domain_name(robo_paint_diagnostic_domain_t domain);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_TYPES_H */
