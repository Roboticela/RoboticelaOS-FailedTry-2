/*
 * RoboLayout borders (Phase 10.11) — see RoboLayoutBorder.h.
 *
 * Three readers over the same computed style, and they are three because the three
 * properties have three genuinely different computed shapes:
 *
 *   a **width** is a length *or* one of three keywords (`thin`, `medium`, `thick`),
 *   and the shared classification folds every keyword but `auto` onto UNUSABLE --
 *   correctly, because no other property's keyword names a length. So this reader
 *   asks the classification first (which owns the length rule: px, finite,
 *   non-negative, inside the geometry's range) and consults the keyword vocabulary
 *   only when the classification says "declared, and not a length".
 *
 *   a **style** is a typed enumerator (9.15 kept the vocabulary rather than
 *   resolving it), so this reader is a kind check and a read -- and it is the reader
 *   that decides whether any of the twelve widths is *used*.
 *
 *   a **colour** is a typed record (9.9), contextual when the author wrote
 *   `currentColor`, and carried here exactly as 9.9 left it: only a layer with an
 *   element can resolve one, and this one has a style rather than an element.
 *
 * The one rule this file adds to the module is the fourth clause of the resolve
 * policy:
 *
 *   a border's used width is ZERO unless its style generates a border
 *
 * which is CSS's own rule and the reason `CssBorderStyleGeneratesBorder`
 * exists. Everything else is 10.10's arithmetic composed one layer out -- which is
 * the phase's whole shape, and the reason the two files read as a pair.
 *
 * No allocations, no state between calls, no cache: every function below is a
 * function of the record it was handed, which is what makes re-layout a matter of
 * calling the pass again.
 */

#include "RoboLayout/RoboLayoutBorder.h"

#include "RoboLayoutInternal.h"

/* ------------------------------------------------------------------ */
/* The width's kinds                                                   */
/* ------------------------------------------------------------------ */

int LayoutBorderWidthKindIsKnown(LayoutBorderWidthKind kind)
{
    return kind == RoboLayoutBorderWidthAbsent ||
           kind == RoboLayoutBorderWidthResolved ||
           kind == RoboLayoutBorderWidthUnresolved;
}

const char *LayoutBorderWidthKindName(LayoutBorderWidthKind kind)
{
    switch (kind) {
    case RoboLayoutBorderWidthAbsent:
        return "absent";
    case RoboLayoutBorderWidthResolved:
        return "resolved";
    case RoboLayoutBorderWidthUnresolved:
        return "unresolved";
    default:
        break;
    }
    return "unknown";
}

LayoutBorderWidth LayoutBorderWidthZero(void)
{
    return LayoutBorderWidthResolved(0);
}

LayoutBorderWidth LayoutBorderWidthAbsent(void)
{
    LayoutBorderWidth width;

    width.kind = RoboLayoutBorderWidthAbsent;
    width.length = 0;
    return width;
}

LayoutBorderWidth LayoutBorderWidthResolved(GfxPixelX length)
{
    LayoutBorderWidth width;

    width.kind = RoboLayoutBorderWidthResolved;
    width.length = length;
    return width;
}

LayoutBorderWidth LayoutBorderWidthUnresolved(void)
{
    LayoutBorderWidth width;

    width.kind = RoboLayoutBorderWidthUnresolved;
    width.length = 0;
    return width;
}

int LayoutBorderWidthIsResolved(const LayoutBorderWidth *width)
{
    return width && width->kind == RoboLayoutBorderWidthResolved;
}

int LayoutBorderWidthIsAbsent(const LayoutBorderWidth *width)
{
    return width && width->kind == RoboLayoutBorderWidthAbsent;
}

int LayoutBorderWidthIsUnresolved(const LayoutBorderWidth *width)
{
    return width && width->kind == RoboLayoutBorderWidthUnresolved;
}

int LayoutBorderWidthIsValid(const LayoutBorderWidth *width)
{
    if (!width || !LayoutBorderWidthKindIsKnown(width->kind)) {
        return 0;
    }
    /*
     * The check this vocabulary has and the margin one does not: a border width is
     * never negative. `border-width: -10px` is invalid CSS, so a record holding one is
     * a record this module's own reader never produced -- which makes it worth stating
     * as a predicate rather than trusting, the same argument 10.10 made for padding.
     */
    if (width->kind == RoboLayoutBorderWidthResolved && width->length < 0) {
        return 0;
    }
    return 1;
}

Result LayoutBorderWidthLength(const LayoutBorderWidth *width,
                                              GfxPixelX *out)
{
    if (!width || !out) {
        return RoboInvalidArgument;
    }
    if (width->kind != RoboLayoutBorderWidthResolved) {
        return RoboNotFound;
    }
    *out = width->length;
    return RoboOk;
}

int LayoutBorderWidthEqual(const LayoutBorderWidth *a,
                                   const LayoutBorderWidth *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    /* The length only where it means something: two `ABSENT` sides are equal whatever
     * their stale `length` fields hold, which is the same rule the kind exists for. */
    if (a->kind != RoboLayoutBorderWidthResolved) {
        return 1;
    }
    return a->length == b->length;
}

/* ------------------------------------------------------------------ */
/* One side                                                            */
/* ------------------------------------------------------------------ */

LayoutBorderSide LayoutBorderSideNone(void)
{
    LayoutBorderSide side;

    side.width = LayoutBorderWidthAbsent();
    side.style = RoboCssBorderStyleNone;
    side.color = CssColorNone();
    return side;
}

int LayoutBorderSideIsValid(const LayoutBorderSide *side)
{
    if (!side) {
        return 0;
    }
    if (!LayoutBorderWidthIsValid(&side->width)) {
        return 0;
    }
    if (!CssBorderStyleIsKnown(side->style)) {
        return 0;
    }
    return 1;
}

int LayoutBorderSideGenerates(const LayoutBorderSide *side)
{
    if (!side) {
        return 0;
    }
    if (!CssBorderStyleGeneratesBorder(side->style)) {
        return 0;
    }
    /* A width nobody declared or this build could not use is zero, and a zero-width
     * border draws nothing -- so "generates" is the conjunction of the style and a
     * positive declared width, and the used question is answered once, below. */
    return side->width.kind == RoboLayoutBorderWidthResolved && side->width.length > 0;
}

int LayoutBorderSideEqual(const LayoutBorderSide *a,
                                  const LayoutBorderSide *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->style != b->style) {
        return 0;
    }
    if (!LayoutBorderWidthEqual(&a->width, &b->width)) {
        return 0;
    }
    /*
     * The *whole* colour record, so a `currentColor` and a resolved colour that
     * happened to share channels are different values. 9.9 made the kind part of the
     * record for that reason and this comparison inherits it rather than re-deciding.
     */
    return CssColorEquals(a->color, b->color);
}

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/** The width longhand each side is read from, indexed by `CssBoxSide`. */
static const char *const layoutBorderWidthProperty[RoboCssBoxSideCount] = {
    "border-top-width",
    "border-right-width",
    "border-bottom-width",
    "border-left-width"
};

/** The style longhand each side is read from. */
static const char *const layoutBorderStyleProperty[RoboCssBoxSideCount] = {
    "border-top-style",
    "border-right-style",
    "border-bottom-style",
    "border-left-style"
};

/** The colour longhand each side is read from. */
static const char *const layoutBorderColorProperty[RoboCssBoxSideCount] = {
    "border-top-color",
    "border-right-color",
    "border-bottom-color",
    "border-left-color"
};

void LayoutBordersInit(LayoutBorders *borders)
{
    if (!borders) {
        return;
    }
    borders->top = LayoutBorderSideNone();
    borders->right = LayoutBorderSideNone();
    borders->bottom = LayoutBorderSideNone();
    borders->left = LayoutBorderSideNone();
}

/**
 * The declared width @p style holds for @p property.
 *
 * The classification first, then the keyword vocabulary, and the order is the point:
 * the classification *owns* the length rule -- px, finite, non-negative, inside the
 * geometry's range -- so this reader does not restate it, and it answers `ABSENT`,
 * `PX` and every unusable length (a percentage, a relative unit, a negative, an
 * out-of-range magnitude) before anything below runs.
 *
 * What it cannot answer is `thin`, `medium` and `thick`. The shared classification
 * folds every keyword but `auto` onto UNUSABLE, and that is right for the property it
 * was written for: no *box dimension* takes a keyword that names a length. A border
 * width does, so the vocabulary is consulted here, in the one reader that has a use
 * for it -- and only on the branch the classification already called unusable, which
 * is why a `KEYWORD` read costs nothing on the common path.
 *
 * `auto` reaches the keyword branch and falls through to `UNRESOLVED`, deliberately:
 * CSS's grammar has no `border-width: auto`, so a style carrying one has broken its
 * own grammar and the honest report is "declared, and not a value this build can use".
 */
static LayoutBorderWidth LayoutBorderWidthOfProperty(
    const CssComputedStyle *style,
    const char *property)
{
    LayoutStyleValueKind kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;
    const CssComputedValue *value;
    CssSlice text;

    if (!LayoutStyleClassify(style, property, 0, &kind, &number)) {
        /* A NULL style or property cannot happen here -- the caller has one by
         * construction -- but the classification refuses it, and a refusal is an
         * absence rather than a length. */
        return LayoutBorderWidthAbsent();
    }

    switch (kind) {
    case LAYOUT_STYLE_VALUE_PX:
        /* Rounded through 10.8's door, half away from zero. A border width is never
         * negative by the time it gets here, so the direction is the only thing that
         * matters: `border-width: 1.5px` is 2, and `2.5px` is 3. */
        return LayoutBorderWidthResolved(LayoutPxRound(number));
    case LAYOUT_STYLE_VALUE_ABSENT:
        return LayoutBorderWidthAbsent();
    case LAYOUT_STYLE_VALUE_AUTO:
    case LAYOUT_STYLE_VALUE_UNUSABLE:
    default:
        break;
    }

    value = CssComputedStyleComputedByName(style, CssSliceFromText(property));
    if (value && value->kind == RoboCssComputedValueKeyword) {
        text = CssComputedValueText(value);
        if (CssSliceEqualsAsciiCi(text, "thin")) {
            return LayoutBorderWidthResolved(RoboLayoutBorderThinPx);
        }
        if (CssSliceEqualsAsciiCi(text, "medium")) {
            return LayoutBorderWidthResolved(RoboLayoutBorderMediumPx);
        }
        if (CssSliceEqualsAsciiCi(text, "thick")) {
            return LayoutBorderWidthResolved(RoboLayoutBorderThickPx);
        }
    }
    return LayoutBorderWidthUnresolved();
}

/**
 * The declared style @p style holds for @p property.
 *
 * A kind check and a read, and the kind check is not optional: 9.15's accessor answers
 * `none` for any other kind, and `none` is both a real style and the property's
 * initial value, so reading it without the check would report "this border does not
 * draw" where the truth is "this is not a border style at all". An absent longhand
 * answers `none` too, and there it is the initial value rather than a confusion.
 */
static CssBorderStyle LayoutBorderStyleOfProperty(
    const CssComputedStyle *style,
    const char *property)
{
    const CssComputedValue *value;

    if (!style || !property) {
        return RoboCssBorderStyleNone;
    }
    value = CssComputedStyleComputedByName(style, CssSliceFromText(property));
    if (value && value->kind == RoboCssComputedValueBorderStyle) {
        CssBorderStyle declared = CssComputedValueBorderStyle(value);

        if (CssBorderStyleIsKnown(declared)) {
            return declared;
        }
    }
    return RoboCssBorderStyleNone;
}

/**
 * The declared colour @p style holds for @p property.
 *
 * Carried exactly as 9.9 and 9.8 left it, `currentColor` included: this layer has a
 * style rather than an element, so a contextual colour stays contextual and the layer
 * that *does* have an element resolves it. Copying a channel set here would be
 * inventing a second colour system, which is the one thing the phase's brief forbids
 * outright.
 *
 * An absent longhand answers a `NONE` colour, which is "nobody declared one" rather
 * than `transparent`: CSS's initial border colour is `currentColor`, and reporting
 * `transparent` would tell a paint pass the author had asked for an invisible border.
 */
static CssColor LayoutBorderColorOfProperty(
    const CssComputedStyle *style,
    const char *property)
{
    const CssComputedValue *value;

    if (!style || !property) {
        return CssColorNone();
    }
    value = CssComputedStyleComputedByName(style, CssSliceFromText(property));
    if (value && value->kind == RoboCssComputedValueColor) {
        return CssComputedValueColor(value);
    }
    return CssColorNone();
}

/** Read one side's three declarations. */
static LayoutBorderSide LayoutBorderSideOf(const CssComputedStyle *style,
                                                      size_t index)
{
    LayoutBorderSide side;

    side.width = LayoutBorderWidthOfProperty(style, layoutBorderWidthProperty[index]);
    side.style = LayoutBorderStyleOfProperty(style, layoutBorderStyleProperty[index]);
    side.color = LayoutBorderColorOfProperty(style, layoutBorderColorProperty[index]);
    return side;
}

LayoutBorders LayoutBordersOf(const CssComputedStyle *style)
{
    LayoutBorders borders;

    borders.top = LayoutBorderSideOf(style, RoboCssBoxSideTop);
    borders.right = LayoutBorderSideOf(style, RoboCssBoxSideRight);
    borders.bottom = LayoutBorderSideOf(style, RoboCssBoxSideBottom);
    borders.left = LayoutBorderSideOf(style, RoboCssBoxSideLeft);
    return borders;
}

Result LayoutBordersSide(const LayoutBorders *borders,
                                       CssBoxSide side,
                                       LayoutBorderSide *out)
{
    if (!borders || !out) {
        return RoboInvalidArgument;
    }
    if (!CssBoxSideIsKnown(side)) {
        return RoboNotFound;
    }
    switch (side) {
    case RoboCssBoxSideTop:
        *out = borders->top;
        return RoboOk;
    case RoboCssBoxSideRight:
        *out = borders->right;
        return RoboOk;
    case RoboCssBoxSideBottom:
        *out = borders->bottom;
        return RoboOk;
    case RoboCssBoxSideLeft:
    default:
        *out = borders->left;
        return RoboOk;
    }
}

int LayoutBordersEqual(const LayoutBorders *a,
                              const LayoutBorders *b)
{
    if (!a || !b) {
        return a == b;
    }
    return LayoutBorderSideEqual(&a->top, &b->top) &&
           LayoutBorderSideEqual(&a->right, &b->right) &&
           LayoutBorderSideEqual(&a->bottom, &b->bottom) &&
           LayoutBorderSideEqual(&a->left, &b->left);
}

int LayoutBordersIsValid(const LayoutBorders *borders)
{
    if (!borders) {
        return 0;
    }
    return LayoutBorderSideIsValid(&borders->top) &&
           LayoutBorderSideIsValid(&borders->right) &&
           LayoutBorderSideIsValid(&borders->bottom) &&
           LayoutBorderSideIsValid(&borders->left);
}

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

void LayoutBorderEdgesInit(LayoutBorderEdges *edges)
{
    if (!edges) {
        return;
    }
    edges->top.width = 0;
    edges->top.style = RoboCssBorderStyleNone;
    edges->top.color = CssColorNone();
    edges->right = edges->top;
    edges->bottom = edges->top;
    edges->left = edges->top;
    edges->widthsResolved = 0u;
    edges->widthsUnresolved = 0u;
    edges->bordersDrawn = 0u;
}

int LayoutBorderEdgesIsValid(const LayoutBorderEdges *edges)
{
    const LayoutBorderUsedSide *sides[RoboCssBoxSideCount];
    size_t index;

    if (!edges) {
        return 0;
    }
    sides[RoboCssBoxSideTop] = &edges->top;
    sides[RoboCssBoxSideRight] = &edges->right;
    sides[RoboCssBoxSideBottom] = &edges->bottom;
    sides[RoboCssBoxSideLeft] = &edges->left;

    for (index = 0u; index < RoboCssBoxSideCount; index++) {
        if (sides[index]->width < 0) {
            return 0;
        }
        if (!CssBorderStyleIsKnown(sides[index]->style)) {
            return 0;
        }
        /*
         * The invariant the fourth resolve rule *is*: a used width above zero implies
         * the style draws. A record that violated it would be a border paint would
         * draw as nothing while geometry charged it room -- which is the exact failure
         * the counter below exists to make visible, and checking it here means a
         * hand-built record cannot produce one.
         */
        if (sides[index]->width > 0 &&
            !CssBorderStyleGeneratesBorder(sides[index]->style)) {
            return 0;
        }
    }

    if (edges->widthsResolved + edges->widthsUnresolved > RoboCssBoxSideCount) {
        return 0;
    }
    if (edges->bordersDrawn > edges->widthsResolved) {
        return 0;
    }
    return 1;
}

int LayoutBorderEdgesEqual(const LayoutBorderEdges *a,
                                   const LayoutBorderEdges *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->top.width != b->top.width || a->right.width != b->right.width ||
        a->bottom.width != b->bottom.width || a->left.width != b->left.width) {
        return 0;
    }
    if (a->top.style != b->top.style || a->right.style != b->right.style ||
        a->bottom.style != b->bottom.style || a->left.style != b->left.style) {
        return 0;
    }
    if (!CssColorEquals(a->top.color, b->top.color) ||
        !CssColorEquals(a->right.color, b->right.color) ||
        !CssColorEquals(a->bottom.color, b->bottom.color) ||
        !CssColorEquals(a->left.color, b->left.color)) {
        return 0;
    }
    return a->widthsResolved == b->widthsResolved &&
           a->widthsUnresolved == b->widthsUnresolved &&
           a->bordersDrawn == b->bordersDrawn;
}

Result LayoutBorderEdgesWidth(const LayoutBorderEdges *edges,
                                             CssBoxSide side,
                                             GfxPixelX *out)
{
    if (!edges || !out) {
        return RoboInvalidArgument;
    }
    switch (side) {
    case RoboCssBoxSideTop:
        *out = edges->top.width;
        return RoboOk;
    case RoboCssBoxSideRight:
        *out = edges->right.width;
        return RoboOk;
    case RoboCssBoxSideBottom:
        *out = edges->bottom.width;
        return RoboOk;
    case RoboCssBoxSideLeft:
        *out = edges->left.width;
        return RoboOk;
    default:
        break;
    }
    return RoboInvalidArgument;
}

/* ------------------------------------------------------------------ */
/* The two derived numbers                                             */
/* ------------------------------------------------------------------ */

/**
 * The one place a border record is refused, so both doors refuse it identically.
 *
 * A record with a negative side or counters that disagree with four sides is not one
 * this module's resolver can produce, so one arriving here is a caller bug -- and the
 * moment to see it is before a size is derived from it, which is why the refusal is a
 * diagnostic rather than a floor.
 */
static Result LayoutBorderEdgesRequire(const LayoutBorderEdges *edges,
                                                LayoutDiag *diag)
{
    if (!LayoutBorderEdgesIsValid(edges)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBorder, RoboError,
                                "a border record no size can be derived from");
    }
    return RoboOk;
}

Result LayoutBorderEdgesPaddingBoxWidth(const LayoutBorderEdges *edges,
                                                        int32_t containingWidth,
                                                        int32_t *out,
                                                        LayoutDiag *diag)
{
    int64_t available;
    Result status;

    if (!edges || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBorder, RoboInvalidArgument,
                                "reading a padding box width needs a record and an out");
    }
    status = LayoutBorderEdgesRequire(edges, diag);
    if (status != RoboOk) {
        return status;
    }

    available = (int64_t)containingWidth - (int64_t)edges->left.width -
                (int64_t)edges->right.width;
    /*
     * Floored, and floored *here* rather than at each term, which is the shape both
     * earlier layers have and for the same reason: one clamp, in one place, on the one
     * number a size arithmetic can act on. A containing block narrower than the box's
     * own borders leaves no room for a padding box, and "no room" is zero rather than a
     * negative width -- a negative available width would make every `auto` dimension in
     * the subtree depend on which of two impossible numbers a subtraction produced.
     */
    if (available < 0) {
        available = 0;
    }
    if (available > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        available = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    *out = (int32_t)available;
    return RoboOk;
}

Result LayoutBorderEdgesBorderBox(const LayoutBorderEdges *edges,
                                                  LayoutSize paddingBox,
                                                  LayoutSize *out,
                                                  LayoutDiag *diag)
{
    int64_t width;
    int64_t height;
    Result status;

    if (!edges || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBorder, RoboInvalidArgument,
                                "deriving a border box needs a record and an out");
    }
    if (paddingBox.width < 0 || paddingBox.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBorder, RoboInvalidArgument,
                                "a padding box is never negative");
    }
    status = LayoutBorderEdgesRequire(edges, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * In 64 bits and clamped, exactly as 10.10's door is and for the same reason one
     * layer in: the terms are each bounded by the seam's maximum and their sum is not,
     * so a `width: 2147483647px` box with a `border: 5px solid` must saturate rather
     * than wrap into a negative rectangle. The clamps compose because both saturate at
     * the same bound: a padding box that already saturated keeps the border it cannot
     * express rather than losing it.
     */
    width = (int64_t)paddingBox.width + (int64_t)edges->left.width + (int64_t)edges->right.width;
    height = (int64_t)paddingBox.height + (int64_t)edges->top.width + (int64_t)edges->bottom.width;

    if (width > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        width = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    if (height > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        height = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }

    out->width = (GfxPixelX)width;
    out->height = (GfxPixelY)height;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

Result LayoutBorderResolve(const LayoutBorders *borders,
                                         LayoutBorderEdges *out,
                                         LayoutDiag *diag)
{
    const LayoutBorderSide *declared[RoboCssBoxSideCount];
    LayoutBorderUsedSide *used[RoboCssBoxSideCount];
    LayoutBorderEdges edges;
    size_t index;

    if (!borders || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBorder, RoboInvalidArgument,
                                "resolving borders needs a record and an out");
    }

    LayoutBorderEdgesInit(&edges);
    declared[RoboCssBoxSideTop] = &borders->top;
    declared[RoboCssBoxSideRight] = &borders->right;
    declared[RoboCssBoxSideBottom] = &borders->bottom;
    declared[RoboCssBoxSideLeft] = &borders->left;
    used[RoboCssBoxSideTop] = &edges.top;
    used[RoboCssBoxSideRight] = &edges.right;
    used[RoboCssBoxSideBottom] = &edges.bottom;
    used[RoboCssBoxSideLeft] = &edges.left;

    for (index = 0u; index < RoboCssBoxSideCount; index++) {
        const LayoutBorderSide *side = declared[index];

        /*
         * The style and the colour travel with the side whatever its width comes to, so
         * a paint pass reads one record and never re-reads a declaration to find out
         * whether it was one this build could use.
         */
        used[index]->style = side->style;
        used[index]->color = side->color;

        switch (side->width.kind) {
        case RoboLayoutBorderWidthAbsent:
            /* Nobody declared this side: CSS's initial value is `medium`, this build
             * uses zero, and it is counted nowhere -- the distinction 10.9 added a
             * kind for. */
            used[index]->width = 0;
            break;
        case RoboLayoutBorderWidthResolved:
            edges.widthsResolved++;
            /*
             * And the phase's one new rule: a width is *used* only when the style
             * draws. `border: 5px none` is a declared five and a used zero, and a
             * build that used the declaration would charge a box ten pixels of width
             * for a border nothing paints -- which is the most plausible way to get
             * this phase wrong, and the reason the declaration and the used value are
             * different records.
             */
            if (CssBorderStyleGeneratesBorder(side->style)) {
                used[index]->width = side->width.length;
            } else {
                used[index]->width = 0;
            }
            if (used[index]->width > 0) {
                edges.bordersDrawn++;
            }
            break;
        case RoboLayoutBorderWidthUnresolved:
        default:
            /* Declared, and not a value this build can use. Zero in geometry, counted
             * so that a document whose borders depend on a `calc()` is visible as one. */
            edges.widthsUnresolved++;
            used[index]->width = 0;
            break;
        }
    }

    *out = edges;
    return RoboOk;
}
