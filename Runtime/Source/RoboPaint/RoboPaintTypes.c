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
typedef struct paintOpRow {
    PaintOpKind kind;
    const char *name;
    /** The side it draws on, or `RoboCssBoxSideCount` for a non-border kind. */
    CssBoxSide side;
} PaintOpRow;

/*
 * `RoboCssBoxSideCount` as the "no side" marker *in this table only*, and it is not
 * a sentinel that leaks: it is a table row's way of saying "this kind is not a border",
 * the public door that reads it refuses rather than answering, and no record in the
 * module carries the value. The alternative -- a `-1` in a signed field -- would be a
 * second spelling of the same idea and one more place for it to be read as a side.
 */
static const PaintOpRow paintOpRows[RoboPaintOpKindCount] = {
    { RoboPaintOpNone, "none", RoboCssBoxSideCount },
    { RoboPaintOpBeginBox, "begin-box", RoboCssBoxSideCount },
    { RoboPaintOpBackground, "background", RoboCssBoxSideCount },
    { RoboPaintOpBorderTop, "border-top", RoboCssBoxSideTop },
    { RoboPaintOpBorderRight, "border-right", RoboCssBoxSideRight },
    { RoboPaintOpBorderBottom, "border-bottom", RoboCssBoxSideBottom },
    { RoboPaintOpBorderLeft, "border-left", RoboCssBoxSideLeft },
    { RoboPaintOpContent, "content", RoboCssBoxSideCount },
    { RoboPaintOpPushClip, "push-clip", RoboCssBoxSideCount },
    { RoboPaintOpText, "text", RoboCssBoxSideCount },
    { RoboPaintOpPopClip, "pop-clip", RoboCssBoxSideCount },
    { RoboPaintOpEndBox, "end-box", RoboCssBoxSideCount },
    /* 11.9's pair, appended in enumerator order. It is the first pair in this table whose
     * *paint* order is not its enumerator order -- an opacity scope opens before a box's
     * decorations and closes before its end marker, where a clip scope opens after the content
     * boundary -- and the vocabulary note in the header states the sequence the traversal
     * actually emits. The table is searched rather than indexed, so the order of these rows is
     * about a reader's eye and nothing else. */
    { RoboPaintOpPushOpacity, "push-opacity", RoboCssBoxSideCount },
    { RoboPaintOpPopOpacity, "pop-opacity", RoboCssBoxSideCount },
    /* 11.7's image record, appended only for the reason the opacity pair was: the rows are
     * searched rather than indexed. Unlike that pair it *is* in paint order -- it is emitted
     * between a box's border records and its `CONTENT` -- so the row here and the sequence a
     * consumer meets agree, and the note `RoboPaintTypes.h` carries about enumerator order no
     * longer being paint order holds for exactly two rows above. */
    { RoboPaintOpImage, "image", RoboCssBoxSideCount }
};

/** The row for @p kind, or NULL when @p kind names none. */
static const PaintOpRow *PaintOpRowOf(PaintOpKind kind)
{
    size_t index;

    for (index = 0u; index < (size_t)RoboPaintOpKindCount; index++) {
        if (paintOpRows[index].kind == kind) {
            return &paintOpRows[index];
        }
    }
    return NULL;
}

int PaintOpKindIsKnown(PaintOpKind kind)
{
    return PaintOpRowOf(kind) != NULL;
}

const char *PaintOpKindName(PaintOpKind kind)
{
    const PaintOpRow *row = PaintOpRowOf(kind);

    return row ? row->name : "unknown";
}

int PaintOpKindIsBorder(PaintOpKind kind)
{
    const PaintOpRow *row = PaintOpRowOf(kind);

    return row && row->side != RoboCssBoxSideCount;
}

Result PaintOpKindSide(PaintOpKind kind, CssBoxSide *out)
{
    const PaintOpRow *row;

    if (!out) {
        return RoboInvalidArgument;
    }
    row = PaintOpRowOf(kind);
    if (!row || row->side == RoboCssBoxSideCount) {
        return RoboInvalidArgument;
    }
    *out = row->side;
    return RoboOk;
}

int PaintOpKindDraws(PaintOpKind kind)
{
    return kind == RoboPaintOpBackground || kind == RoboPaintOpText ||
           kind == RoboPaintOpImage || PaintOpKindIsBorder(kind);
}

int PaintOpKindUsesColor(PaintOpKind kind)
{
    /*
     * Written as an explicit list of the kinds whose look is a colour, rather than as
     * "draws and is not an image", so that the next payload kind is a kind that answers no
     * here instead of a special case somewhere else -- the argument `RoboPaintTypes.h` makes
     * for the predicate's shape.
     */
    return kind == RoboPaintOpBackground || kind == RoboPaintOpText ||
           PaintOpKindIsBorder(kind);
}

int PaintOpKindIsClip(PaintOpKind kind)
{
    return kind == RoboPaintOpPushClip || kind == RoboPaintOpPopClip;
}

int PaintOpKindIsOpacity(PaintOpKind kind)
{
    return kind == RoboPaintOpPushOpacity || kind == RoboPaintOpPopOpacity;
}

int PaintOpKindIsStructural(PaintOpKind kind)
{
    /*
     * Structural is the complement of drawing above `NONE`, and it is written as that
     * complement rather than as a second list of three enumerators on purpose: a list
     * would be a second place to add the next kind, and the day the two disagreed a
     * record would be neither drawing nor structural and the validity rule would have
     * no branch for it. `NONE` is excluded because a cleared record is not a record.
     */
    if (!PaintOpKindIsKnown(kind) || kind == RoboPaintOpNone) {
        return 0;
    }
    return !PaintOpKindDraws(kind);
}

int PaintOpKindIsMarker(PaintOpKind kind)
{
    return kind == RoboPaintOpBeginBox || kind == RoboPaintOpEndBox;
}

/* ------------------------------------------------------------------ */
/* Diagnostic domains                                                  */
/* ------------------------------------------------------------------ */

static const char *const paintDomainNames[RoboPaintDomainCount] = {
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
    "hit-test",
    "invalidation"
};

int PaintDomainIsKnown(PaintDiagnosticDomain domain)
{
    size_t index = (size_t)domain;

    return index < (size_t)RoboPaintDomainCount;
}

const char *PaintDomainName(PaintDiagnosticDomain domain)
{
    if (!PaintDomainIsKnown(domain)) {
        return "unknown";
    }
    return paintDomainNames[(size_t)domain];
}
