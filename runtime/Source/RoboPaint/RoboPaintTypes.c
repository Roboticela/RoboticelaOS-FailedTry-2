/*
 * RoboPaint vocabularies (Phase 11.1–11.2) — see RoboPaintTypes.h.
 *
 * Two tables and their predicates, and the tables are written as rows rather than as
 * switches for the reason RoboLayout's are: a row per enumerator means the compiler
 * counts the rows against the count, so an enumerator added without a name is a
 * compile-time omission rather than a value that prints "unknown" in a bug report.
 */

#include "RoboPaint/RoboPaintTypes.h"

/* ------------------------------------------------------------------ */
/* Operation kinds                                                     */
/* ------------------------------------------------------------------ */

/** One operation kind: the enumerator, its name, and the facts about it. */
typedef struct paint_op_row {
    robo_paint_op_kind_t kind;
    const char *name;
    /** The side it draws on, or `ROBO_CSS_BOX_SIDE_COUNT` for a non-border kind. */
    robo_css_box_side_t side;
} paint_op_row_t;

/*
 * `ROBO_CSS_BOX_SIDE_COUNT` as the "no side" marker *in this table only*, and it is not
 * a sentinel that leaks: it is a table row's way of saying "this kind is not a border",
 * the public door that reads it refuses rather than answering, and no record in the
 * module carries the value. The alternative -- a `-1` in a signed field -- would be a
 * second spelling of the same idea and one more place for it to be read as a side.
 */
static const paint_op_row_t paint_op_rows[ROBO_PAINT_OP_KIND_COUNT] = {
    { ROBO_PAINT_OP_NONE, "none", ROBO_CSS_BOX_SIDE_COUNT },
    { ROBO_PAINT_OP_BEGIN_BOX, "begin-box", ROBO_CSS_BOX_SIDE_COUNT },
    { ROBO_PAINT_OP_BACKGROUND, "background", ROBO_CSS_BOX_SIDE_COUNT },
    { ROBO_PAINT_OP_BORDER_TOP, "border-top", ROBO_CSS_BOX_SIDE_TOP },
    { ROBO_PAINT_OP_BORDER_RIGHT, "border-right", ROBO_CSS_BOX_SIDE_RIGHT },
    { ROBO_PAINT_OP_BORDER_BOTTOM, "border-bottom", ROBO_CSS_BOX_SIDE_BOTTOM },
    { ROBO_PAINT_OP_BORDER_LEFT, "border-left", ROBO_CSS_BOX_SIDE_LEFT },
    { ROBO_PAINT_OP_CONTENT, "content", ROBO_CSS_BOX_SIDE_COUNT },
    { ROBO_PAINT_OP_PUSH_CLIP, "push-clip", ROBO_CSS_BOX_SIDE_COUNT },
    { ROBO_PAINT_OP_TEXT, "text", ROBO_CSS_BOX_SIDE_COUNT },
    { ROBO_PAINT_OP_POP_CLIP, "pop-clip", ROBO_CSS_BOX_SIDE_COUNT },
    { ROBO_PAINT_OP_END_BOX, "end-box", ROBO_CSS_BOX_SIDE_COUNT },
    /* 11.9's pair, appended in enumerator order. It is the first pair in this table whose
     * *paint* order is not its enumerator order -- an opacity scope opens before a box's
     * decorations and closes before its end marker, where a clip scope opens after the content
     * boundary -- and the vocabulary note in the header states the sequence the traversal
     * actually emits. The table is searched rather than indexed, so the order of these rows is
     * about a reader's eye and nothing else. */
    { ROBO_PAINT_OP_PUSH_OPACITY, "push-opacity", ROBO_CSS_BOX_SIDE_COUNT },
    { ROBO_PAINT_OP_POP_OPACITY, "pop-opacity", ROBO_CSS_BOX_SIDE_COUNT },
    /* 11.7's image record, appended only for the reason the opacity pair was: the rows are
     * searched rather than indexed. Unlike that pair it *is* in paint order -- it is emitted
     * between a box's border records and its `CONTENT` -- so the row here and the sequence a
     * consumer meets agree, and the note `RoboPaintTypes.h` carries about enumerator order no
     * longer being paint order holds for exactly two rows above. */
    { ROBO_PAINT_OP_IMAGE, "image", ROBO_CSS_BOX_SIDE_COUNT }
};

/** The row for @p kind, or NULL when @p kind names none. */
static const paint_op_row_t *paint_op_row_of(robo_paint_op_kind_t kind)
{
    size_t index;

    for (index = 0u; index < (size_t)ROBO_PAINT_OP_KIND_COUNT; index++) {
        if (paint_op_rows[index].kind == kind) {
            return &paint_op_rows[index];
        }
    }
    return NULL;
}

int robo_paint_op_kind_is_known(robo_paint_op_kind_t kind)
{
    return paint_op_row_of(kind) != NULL;
}

const char *robo_paint_op_kind_name(robo_paint_op_kind_t kind)
{
    const paint_op_row_t *row = paint_op_row_of(kind);

    return row ? row->name : "unknown";
}

int robo_paint_op_kind_is_border(robo_paint_op_kind_t kind)
{
    const paint_op_row_t *row = paint_op_row_of(kind);

    return row && row->side != ROBO_CSS_BOX_SIDE_COUNT;
}

robo_result_t robo_paint_op_kind_side(robo_paint_op_kind_t kind, robo_css_box_side_t *out)
{
    const paint_op_row_t *row;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    row = paint_op_row_of(kind);
    if (!row || row->side == ROBO_CSS_BOX_SIDE_COUNT) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = row->side;
    return ROBO_OK;
}

int robo_paint_op_kind_draws(robo_paint_op_kind_t kind)
{
    return kind == ROBO_PAINT_OP_BACKGROUND || kind == ROBO_PAINT_OP_TEXT ||
           kind == ROBO_PAINT_OP_IMAGE || robo_paint_op_kind_is_border(kind);
}

int robo_paint_op_kind_uses_color(robo_paint_op_kind_t kind)
{
    /*
     * Written as an explicit list of the kinds whose look is a colour, rather than as
     * "draws and is not an image", so that the next payload kind is a kind that answers no
     * here instead of a special case somewhere else -- the argument `RoboPaintTypes.h` makes
     * for the predicate's shape.
     */
    return kind == ROBO_PAINT_OP_BACKGROUND || kind == ROBO_PAINT_OP_TEXT ||
           robo_paint_op_kind_is_border(kind);
}

int robo_paint_op_kind_is_clip(robo_paint_op_kind_t kind)
{
    return kind == ROBO_PAINT_OP_PUSH_CLIP || kind == ROBO_PAINT_OP_POP_CLIP;
}

int robo_paint_op_kind_is_opacity(robo_paint_op_kind_t kind)
{
    return kind == ROBO_PAINT_OP_PUSH_OPACITY || kind == ROBO_PAINT_OP_POP_OPACITY;
}

int robo_paint_op_kind_is_structural(robo_paint_op_kind_t kind)
{
    /*
     * Structural is the complement of drawing above `NONE`, and it is written as that
     * complement rather than as a second list of three enumerators on purpose: a list
     * would be a second place to add the next kind, and the day the two disagreed a
     * record would be neither drawing nor structural and the validity rule would have
     * no branch for it. `NONE` is excluded because a cleared record is not a record.
     */
    if (!robo_paint_op_kind_is_known(kind) || kind == ROBO_PAINT_OP_NONE) {
        return 0;
    }
    return !robo_paint_op_kind_draws(kind);
}

int robo_paint_op_kind_is_marker(robo_paint_op_kind_t kind)
{
    return kind == ROBO_PAINT_OP_BEGIN_BOX || kind == ROBO_PAINT_OP_END_BOX;
}

/* ------------------------------------------------------------------ */
/* Diagnostic domains                                                  */
/* ------------------------------------------------------------------ */

static const char *const paint_domain_names[ROBO_PAINT_DOMAIN_COUNT] = {
    "none",
    "module",
    "context",
    "operation",
    "list",
    "geometry",
    "traversal",
    "clip",
    "dump",
    "display-list",
    "opacity",
    "hit-test"
};

int robo_paint_domain_is_known(robo_paint_diagnostic_domain_t domain)
{
    size_t index = (size_t)domain;

    return index < (size_t)ROBO_PAINT_DOMAIN_COUNT;
}

const char *robo_paint_domain_name(robo_paint_diagnostic_domain_t domain)
{
    if (!robo_paint_domain_is_known(domain)) {
        return "unknown";
    }
    return paint_domain_names[(size_t)domain];
}
