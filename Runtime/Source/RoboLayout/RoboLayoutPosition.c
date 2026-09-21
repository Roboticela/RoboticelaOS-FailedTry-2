/*
 * RoboLayout positioning (Phase 10.12) — see RoboLayoutPosition.h.
 *
 * One reader over a computed style, one policy over the declarations it read, and one
 * arithmetic over the numbers the policy produced. The file is short because the phase
 * is: the vocabulary it needs already exists (9.11's offset shape, 9.13's mode), so
 * what is left is the four lines that turn two opposing pairs into two numbers.
 *
 * The reader is the one place the shared classification is asked with
 * `allowNegative = 1`, which is the margin reader's parameter rather than the
 * dimension, padding or border reader's -- and it is not a coincidence: an inset and a
 * margin are the same `EDGE_OFFSET` grammar (9.13 says so), and `left: -10px` is legal
 * for exactly the reason `margin-left: -10px` is.
 *
 * The policy is where the phase's one rule lives:
 *
 *   left wins over right, top wins over bottom, and an `auto` side says nothing
 *
 * which is deliberately *less* than CSS's offset resolution (writing mode, direction
 * and containing-block anchoring all decide which of an opposing pair is the one that
 * means something) and deliberately stated at the rule rather than spread across the
 * callers, so that the phase that implements the full rule changes this function and
 * nothing else.
 *
 * No allocations, no state between calls, no cache: every function below is a function
 * of the record it was handed, which is what makes re-layout a matter of calling the
 * pass again.
 */

#include "RoboLayout/RoboLayoutPosition.h"

#include "RoboLayoutInternal.h"

/* ------------------------------------------------------------------ */
/* The inset's kinds                                                   */
/* ------------------------------------------------------------------ */

int LayoutInsetKindIsKnown(LayoutInsetKind kind)
{
    return kind == RoboLayoutInsetAbsent || kind == RoboLayoutInsetResolved ||
           kind == RoboLayoutInsetAuto || kind == RoboLayoutInsetUnresolved;
}

const char *LayoutInsetKindName(LayoutInsetKind kind)
{
    switch (kind) {
    case RoboLayoutInsetAbsent:
        return "absent";
    case RoboLayoutInsetResolved:
        return "resolved";
    case RoboLayoutInsetAuto:
        return "auto";
    case RoboLayoutInsetUnresolved:
        return "unresolved";
    default:
        break;
    }
    return "unknown";
}

/** The one shape every constructor below has: a kind and a length that means
 *  something for exactly one of the four. */
static LayoutInset LayoutInsetMake(LayoutInsetKind kind,
                                             GfxPixelX length)
{
    LayoutInset inset;

    inset.kind = kind;
    inset.length = length;
    return inset;
}

LayoutInset LayoutInsetZero(void)
{
    return LayoutInsetMake(RoboLayoutInsetResolved, 0);
}

LayoutInset LayoutInsetAbsent(void)
{
    return LayoutInsetMake(RoboLayoutInsetAbsent, 0);
}

LayoutInset LayoutInsetResolved(GfxPixelX length)
{
    return LayoutInsetMake(RoboLayoutInsetResolved, length);
}

LayoutInset LayoutInsetAuto(void)
{
    return LayoutInsetMake(RoboLayoutInsetAuto, 0);
}

LayoutInset LayoutInsetUnresolved(void)
{
    return LayoutInsetMake(RoboLayoutInsetUnresolved, 0);
}

int LayoutInsetIsAuto(const LayoutInset *inset)
{
    return inset && inset->kind == RoboLayoutInsetAuto;
}

int LayoutInsetIsResolved(const LayoutInset *inset)
{
    return inset && inset->kind == RoboLayoutInsetResolved;
}

int LayoutInsetIsAbsent(const LayoutInset *inset)
{
    return inset && inset->kind == RoboLayoutInsetAbsent;
}

int LayoutInsetIsUnresolved(const LayoutInset *inset)
{
    return inset && inset->kind == RoboLayoutInsetUnresolved;
}

int LayoutInsetIsValid(const LayoutInset *inset)
{
    if (!inset || !LayoutInsetKindIsKnown(inset->kind)) {
        return 0;
    }
    /*
     * The range check the shared classification already made, restated as a predicate
     * for the reason 10.8 gives for its dimension: a caller may hand this module a
     * record it built itself, and "is this something I may read?" should have an answer
     * that does not depend on how it was built. Only a `RESOLVED` inset has a length, so
     * only one kind is bounded.
     */
    if (inset->kind == RoboLayoutInsetResolved &&
        (inset->length > (GfxPixelX)LAYOUT_STYLE_LENGTH_MAX ||
         inset->length < (GfxPixelX)(-LAYOUT_STYLE_LENGTH_MAX))) {
        return 0;
    }
    return 1;
}

Result LayoutInsetLength(const LayoutInset *inset,
                                       GfxPixelX *out)
{
    if (!inset || !out) {
        return RoboInvalidArgument;
    }
    if (inset->kind != RoboLayoutInsetResolved) {
        return RoboNotFound;
    }
    *out = inset->length;
    return RoboOk;
}

int LayoutInsetEqual(const LayoutInset *a, const LayoutInset *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    /* The length only where it means something: two `ABSENT` sides are equal whatever
     * their stale `length` fields hold, which is the same rule the kind exists for. */
    if (a->kind != RoboLayoutInsetResolved) {
        return 1;
    }
    return a->length == b->length;
}

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/** The longhand each side is read from, indexed by `CssBoxSide`. */
static const char *const layoutInsetProperty[RoboCssBoxSideCount] = {
    "top",
    "right",
    "bottom",
    "left"
};

void LayoutInsetsInit(LayoutInsets *insets)
{
    if (!insets) {
        return;
    }
    insets->top = LayoutInsetAbsent();
    insets->right = LayoutInsetAbsent();
    insets->bottom = LayoutInsetAbsent();
    insets->left = LayoutInsetAbsent();
}

/**
 * The declared inset @p style holds for @p property.
 *
 * The shared classification, with the parameter a **margin** passes and a dimension,
 * a padding and a border do not: an inset is signed. That is the only respect in which
 * this reader differs from the border and padding readers beside it, and it is a
 * property of the CSS grammar rather than a choice -- `top: -10px` is legal for the
 * same reason `margin-top: -10px` is, and neither is a position the author did not ask
 * for.
 *
 * The classification answers `ABSENT`, `PX`, `AUTO` and every unusable length (a
 * percentage, a relative unit, an out-of-range magnitude) in one call, which is why
 * there is no second vocabulary consulted here -- unlike the border reader, whose
 * property has keywords that name lengths.
 */
static LayoutInset LayoutInsetOfProperty(const CssComputedStyle *style,
                                                    const char *property)
{
    LayoutStyleValueKind kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;

    if (!LayoutStyleClassify(style, property, 1, &kind, &number)) {
        /* A NULL style or property cannot happen here -- the caller has one by
         * construction -- but the classification refuses it, and a refusal is an
         * absence rather than a length. */
        return LayoutInsetAbsent();
    }

    switch (kind) {
    case LAYOUT_STYLE_VALUE_PX:
        /* Rounded through 10.8's door, half away from zero -- and the direction matters
         * here where it did not for a padding or a border: a negative inset is legal, so
         * `-1.5px` is -2 rather than 2. */
        return LayoutInsetResolved(LayoutPxRound(number));
    case LAYOUT_STYLE_VALUE_AUTO:
        return LayoutInsetAuto();
    case LAYOUT_STYLE_VALUE_ABSENT:
        return LayoutInsetAbsent();
    case LAYOUT_STYLE_VALUE_UNUSABLE:
    default:
        break;
    }
    return LayoutInsetUnresolved();
}

/** Read one side. */
static LayoutInset LayoutInsetOf(const CssComputedStyle *style,
                                           size_t index)
{
    return LayoutInsetOfProperty(style, layoutInsetProperty[index]);
}

LayoutInsets LayoutInsetsOf(const CssComputedStyle *style)
{
    LayoutInsets insets;

    insets.top = LayoutInsetOf(style, RoboCssBoxSideTop);
    insets.right = LayoutInsetOf(style, RoboCssBoxSideRight);
    insets.bottom = LayoutInsetOf(style, RoboCssBoxSideBottom);
    insets.left = LayoutInsetOf(style, RoboCssBoxSideLeft);
    return insets;
}

Result LayoutInsetsSide(const LayoutInsets *insets,
                                      CssBoxSide side,
                                      LayoutInset *out)
{
    if (!insets || !out) {
        return RoboInvalidArgument;
    }
    if (!CssBoxSideIsKnown(side)) {
        return RoboNotFound;
    }
    switch (side) {
    case RoboCssBoxSideTop:
        *out = insets->top;
        return RoboOk;
    case RoboCssBoxSideRight:
        *out = insets->right;
        return RoboOk;
    case RoboCssBoxSideBottom:
        *out = insets->bottom;
        return RoboOk;
    case RoboCssBoxSideLeft:
    default:
        *out = insets->left;
        return RoboOk;
    }
}

int LayoutInsetsEqual(const LayoutInsets *a, const LayoutInsets *b)
{
    if (!a || !b) {
        return a == b;
    }
    return LayoutInsetEqual(&a->top, &b->top) &&
           LayoutInsetEqual(&a->right, &b->right) &&
           LayoutInsetEqual(&a->bottom, &b->bottom) &&
           LayoutInsetEqual(&a->left, &b->left);
}

int LayoutInsetsIsValid(const LayoutInsets *insets)
{
    if (!insets) {
        return 0;
    }
    return LayoutInsetIsValid(&insets->top) &&
           LayoutInsetIsValid(&insets->right) &&
           LayoutInsetIsValid(&insets->bottom) &&
           LayoutInsetIsValid(&insets->left);
}

/* ------------------------------------------------------------------ */
/* The declared position                                               */
/* ------------------------------------------------------------------ */

void LayoutPositionInit(LayoutPosition *position)
{
    if (!position) {
        return;
    }
    position->mode = RoboCssPositioningStatic;
    LayoutInsetsInit(&position->insets);
}

LayoutPosition LayoutPositionOf(const CssComputedStyle *style)
{
    LayoutPosition position;

    LayoutPositionInit(&position);
    if (!style) {
        return position;
    }

    /*
     * The mode first, and the kind check is not optional: 9.13's accessor answers
     * `static` for any other kind, and `static` is both a real value and the property's
     * initial value, so reading it without the check would report "this box is not
     * positioned" where the truth might be "this is not a position property at all".
     * An absent longhand answers `static` too, and there it *is* the initial value
     * rather than a confusion.
     */
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(style, CssSliceFromText("position"));

        if (value && value->kind == RoboCssComputedValuePosition) {
            CssPositioning declared = CssComputedValuePosition(value);

            if (CssPositioningIsKnown(declared)) {
                position.mode = declared;
            }
        }
    }
    position.insets = LayoutInsetsOf(style);
    return position;
}

int LayoutPositionIsRelative(const LayoutPosition *position)
{
    return position && position->mode == RoboCssPositioningRelative;
}

int LayoutPositionIsValid(const LayoutPosition *position)
{
    if (!position) {
        return 0;
    }
    if (!CssPositioningIsKnown(position->mode)) {
        return 0;
    }
    return LayoutInsetsIsValid(&position->insets);
}

int LayoutPositionEqual(const LayoutPosition *a,
                               const LayoutPosition *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->mode == b->mode && LayoutInsetsEqual(&a->insets, &b->insets);
}

/* ------------------------------------------------------------------ */
/* The used offset                                                     */
/* ------------------------------------------------------------------ */

void LayoutPositionOffsetInit(LayoutPositionOffset *offset)
{
    if (!offset) {
        return;
    }
    offset->dx = 0;
    offset->dy = 0;
    offset->insetsResolved = 0u;
    offset->insetsAuto = 0u;
    offset->insetsUnresolved = 0u;
    offset->insetsDeclined = 0u;
}

int LayoutPositionOffsetIsValid(const LayoutPositionOffset *offset)
{
    if (!offset) {
        return 0;
    }
    /*
     * The four counters cannot outrun four sides, and the sum is the check rather than
     * each field: the record's whole claim is that it accounts for the declarations it
     * was given, and a hand-built record that counted five sides across four kinds would
     * be one no resolve produced.
     */
    if (offset->insetsResolved + offset->insetsAuto + offset->insetsUnresolved +
            offset->insetsDeclined >
        (size_t)RoboCssBoxSideCount) {
        return 0;
    }
    return 1;
}

int LayoutPositionOffsetEqual(const LayoutPositionOffset *a,
                                      const LayoutPositionOffset *b)
{
    if (!a || !b) {
        return a == b;
    }
    /* The displacement first, then the counters -- and the counters are compared
     * because two resolutions that moved a box the same way while disagreeing about how
     * many declarations they declined are two answers to one document. */
    return a->dx == b->dx && a->dy == b->dy &&
           a->insetsResolved == b->insetsResolved &&
           a->insetsAuto == b->insetsAuto &&
           a->insetsUnresolved == b->insetsUnresolved &&
           a->insetsDeclined == b->insetsDeclined;
}

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

/**
 * Count one declared side into @p offset, and report whether it was a usable length.
 *
 * Split out so the four sides are counted by one switch rather than four, and so that
 * the reason a side did or did not contribute is written once. `*outLength` is filled
 * only for a `RESOLVED` side, which is what lets the caller tell "the author wrote
 * `0px`" from "the author wrote nothing" without a second look at the kind.
 *
 * @return 1 when the side is a usable length the caller may use for its axis.
 */
static int LayoutPositionCountInset(const LayoutInset *inset,
                                       LayoutPositionOffset *offset,
                                       GfxPixelX *outLength)
{
    switch (inset->kind) {
    case RoboLayoutInsetResolved:
        offset->insetsResolved++;
        *outLength = inset->length;
        return 1;
    case RoboLayoutInsetAuto:
        offset->insetsAuto++;
        return 0;
    case RoboLayoutInsetUnresolved:
        offset->insetsUnresolved++;
        return 0;
    case RoboLayoutInsetAbsent:
    default:
        /* Nobody declared this side: CSS's initial value applies, and it is counted
         * nowhere -- the distinction 10.9 added a kind for. */
        return 0;
    }
}

Result LayoutPositionResolve(const LayoutPosition *position,
                                           LayoutPositionOffset *out,
                                           LayoutDiag *diag)
{
    LayoutPositionOffset offset;
    size_t index;
    const LayoutInset *declared[RoboCssBoxSideCount];

    if (!position || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainPosition, RoboInvalidArgument,
                                "resolving a position needs a record and an out");
    }

    LayoutPositionOffsetInit(&offset);
    declared[RoboCssBoxSideTop] = &position->insets.top;
    declared[RoboCssBoxSideRight] = &position->insets.right;
    declared[RoboCssBoxSideBottom] = &position->insets.bottom;
    declared[RoboCssBoxSideLeft] = &position->insets.left;

    /*
     * Rule 1: a mode that is not `relative` produces no displacement and *declines*
     * every declared side. `static` reaches this branch because CSS does not apply an
     * offset to one, and `absolute`, `fixed` and `sticky` reach it because placing a box
     * against a containing block or a scrollport is 18.5's. The counter is the same on
     * both paths and the mode is what tells a reader which of the two documents this is.
     */
    if (!LayoutPositionIsRelative(position)) {
        for (index = 0u; index < RoboCssBoxSideCount; index++) {
            if (declared[index]->kind != RoboLayoutInsetAbsent) {
                offset.insetsDeclined++;
            }
        }
        *out = offset;
        return RoboOk;
    }

    /*
     * Rule 2 and 3: the two axes, each by the precedence rule -- the "start" side first,
     * and the "end" side only when the start one said nothing. `left` winning over
     * `right` and `top` over `bottom` is one sentence of behaviour rather than CSS's
     * direction-aware rule, and it is stated at `RoboLayoutPosition.h` so the phase that
     * replaces it changes this function and nothing else.
     *
     * The counters are accumulated for *every* side before either axis is decided, so a
     * `left` and a `right` that both resolve are both counted even though only `left`
     * contributes -- the record's counters are about declarations that reached this
     * layer, and precedence is a separate fact about which of two the displacement uses.
     */
    {
        GfxPixelX left = 0;
        GfxPixelX right = 0;
        GfxPixelX top = 0;
        GfxPixelX bottom = 0;
        int haveLeft = LayoutPositionCountInset(declared[RoboCssBoxSideLeft],
                                                    &offset, &left);
        int haveRight = LayoutPositionCountInset(declared[RoboCssBoxSideRight],
                                                     &offset, &right);
        int haveTop = LayoutPositionCountInset(declared[RoboCssBoxSideTop],
                                                   &offset, &top);
        int haveBottom = LayoutPositionCountInset(declared[RoboCssBoxSideBottom],
                                                      &offset, &bottom);

        if (haveLeft) {
            offset.dx = left;
        } else if (haveRight) {
            /* `right` is a distance from the *right* edge, so it displaces leftward --
             * the sign flip the header names as the one place a reader can get this
             * wrong. */
            offset.dx = (GfxPixelX)(-(int64_t)right);
        }
        if (haveTop) {
            offset.dy = top;
        } else if (haveBottom) {
            offset.dy = (GfxPixelY)(-(int64_t)bottom);
        }
    }

    *out = offset;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The two coordinate spaces                                           */
/* ------------------------------------------------------------------ */

/** @p value saturated into the geometry's range, in 64 bits. */
static int64_t LayoutPositionClamp(int64_t value)
{
    if (value > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        return (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    if (value < -(int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        return -(int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    return value;
}

Result LayoutPositionApply(LayoutPoint flow,
                                         const LayoutPositionOffset *offset,
                                         LayoutPoint *out,
                                         LayoutDiag *diag)
{
    if (!offset || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainPosition, RoboInvalidArgument,
                                "applying a position needs an offset and an out");
    }
    if (!LayoutPositionOffsetIsValid(offset)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainPosition, RoboError,
                                "an offset record no position can be derived from");
    }

    /*
     * In 64 bits and clamped, for the reason the two inset layers sum a size that way:
     * the flow position and the displacement are each bounded by the seam's maximum and
     * their sum is not, so a box already at the extreme displaced by an extreme inset
     * must saturate rather than wrap into a coordinate on the other side of the screen.
     * Clamped rather than refused, because there is no document an author can write that
     * some coordinate cannot describe.
     */
    out->x = (GfxPixelX)LayoutPositionClamp((int64_t)flow.x + (int64_t)offset->dx);
    out->y = (GfxPixelY)LayoutPositionClamp((int64_t)flow.y + (int64_t)offset->dy);
    return RoboOk;
}
