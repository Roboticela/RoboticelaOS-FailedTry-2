/*
 * RoboLayout sizing (Phase 10.8) — see RoboLayoutSizing.h.
 *
 * Two readers over one classification, and the classification is the file's centre.
 * `layout_style_read_px` asks a computed style one question -- *what does this
 * declaration hold?* -- and answers with a kind and a raw number. Both readers then
 * shape that answer:
 *
 *   `LayoutStylePxLength` wants only "is it a usable px length, and how
 *   many?" because that is all `font-size` needs (RoboLayoutText.c), and a font
 *   size is not a box dimension: `12.5px` has to stay `12.5`.
 *
 *   `LayoutSizingReadDimension` wants the whole classification, because
 *   `auto` and `50%` resolve the *same way today* and differently tomorrow, and
 *   folding them here is what would make that change a rewrite.
 *
 * One classification and two shapes is the same arrangement 10.3 arrived at for its
 * property reader and 10.7 for its baseline rules: a fact computed once and read
 * twice, so the day the fact changes there is one place to change it.
 *
 * The resolver is deliberately *not* a cache. Everything below reads the record as
 * it stands and writes the answer into it, so resolving twice is legal and the
 * second answer is a function of the second input. That is the whole of the
 * re-layout story this phase promises, and it is a property of the shape rather
 * than a feature that had to be added.
 */

#include "RoboLayout/RoboLayoutSizing.h"

#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSUnit.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/*
 * `LAYOUT_SIZING_LENGTH_MAX` lived here from 10.8 and moved to
 * RoboLayoutInternal.h in 10.9, where it is `LAYOUT_STYLE_LENGTH_MAX`: the margin
 * seam reads the same comparison through this classification, so the bound belongs
 * beside the vocabulary two layers now share rather than inside one of them.
 */

/* ------------------------------------------------------------------ */
/* The shared classification                                           */
/* ------------------------------------------------------------------ */

/*
 * The classification's vocabulary is in RoboLayoutInternal.h, and it moved there in
 * 10.9 for the reason a shared rule always moves: the margin seam reads a *signed*
 * length through the same four checks, so the checks now have two callers' worth of
 * vocabulary and one implementation.
 */

/**
 * Classify what @p style declares for @p property.
 *
 * The four checks 10.3's version of this rule wrote, and they are four for the same
 * reasons:
 *
 *   the entry must exist. `CssComputedStyleComputedByName` returns NULL
 *   for an absent property, and that is `ABSENT` rather than a failure.
 *
 *   a `KEYWORD` is `auto` when it spells `auto` and `UNUSABLE` otherwise: no other
 *   keyword names a length, so `width: inherit` reaching here as a keyword is a value
 *   this build cannot use rather than a value it may reinterpret.
 *
 *   a `LENGTH` in `px`, finite and inside the geometry's range is usable. 9.8's
 *   computed rule normalises an *absolute* unit to px exactly, so `1in` arrives as
 *   `96`; a relative unit is not resolvable here. **Non-negative unless the caller
 *   allows otherwise** -- which is 10.9's one parameter and the only difference
 *   between a box dimension and a margin: `width: -10px` is an invalid declaration
 *   and `margin: -10px` is a technique.
 *
 *   anything else -- a percentage, a bare number, a colour -- is `UNUSABLE`.
 *
 * `unresolved` is honoured where the computed layer sets it: a `var()` or a `calc()`
 * is carried intact and is not a length whatever its kind says.
 *
 * @return 1 when the question could be asked; 0 for a NULL @p style or @p property,
 *         which leaves both outputs untouched. That asymmetry is the contract the
 *         public door documents: a box with no style has made no declarations, and
 *         its caller's initialised record already says so.
 */
int LayoutStyleClassify(const CssComputedStyle *style,
                               const char *property,
                               int allowNegative,
                               LayoutStyleValueKind *outKind,
                               double *outNumber)
{
    const CssComputedValue *value;

    if (!style || !property) {
        return 0;
    }
    value = CssComputedStyleComputedByName(style,
                                                     CssSliceFromText(property));
    if (!value || value->kind == RoboCssComputedValueNone) {
        *outKind = LAYOUT_STYLE_VALUE_ABSENT;
        *outNumber = 0.0;
        return 1;
    }
    if (value->kind == RoboCssComputedValueKeyword) {
        CssSlice text = CssComputedValueText(value);

        *outKind = CssSliceEqualsAsciiCi(text, "auto") ? LAYOUT_STYLE_VALUE_AUTO
                                                                : LAYOUT_STYLE_VALUE_UNUSABLE;
        *outNumber = 0.0;
        return 1;
    }
    if (value->kind == RoboCssComputedValueLength && value->unit == RoboCssUnitPx &&
        !value->unresolved) {
        /*
         * Deliberately written as out-of-range tests rather than as a pair of
         * comparisons against a negated `number < 0`, so that a NaN -- which compares
         * false against everything -- fails a test rather than slipping through one.
         * The lower bound is 10.9's parameter and nothing else: a margin may be
         * negative, and a *bounded* negative margin is as much a length as a bounded
         * positive one.
         */
        double lowest = allowNegative ? -LAYOUT_STYLE_LENGTH_MAX : 0.0;

        if ((value->number >= lowest) && (value->number <= LAYOUT_STYLE_LENGTH_MAX)) {
            *outKind = LAYOUT_STYLE_VALUE_PX;
            *outNumber = value->number;
            return 1;
        }
    }
    *outKind = LAYOUT_STYLE_VALUE_UNUSABLE;
    *outNumber = 0.0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The dimension                                                       */
/* ------------------------------------------------------------------ */

int LayoutDimensionKindIsKnown(LayoutDimensionKind kind)
{
    return kind == RoboLayoutDimensionAuto || kind == RoboLayoutDimensionDefinite ||
           kind == RoboLayoutDimensionUnresolved;
}

const char *LayoutDimensionKindName(LayoutDimensionKind kind)
{
    switch (kind) {
    case RoboLayoutDimensionAuto:
        return "auto";
    case RoboLayoutDimensionDefinite:
        return "definite";
    case RoboLayoutDimensionUnresolved:
        return "unresolved";
    default:
        break;
    }
    return "unknown";
}

void LayoutDimensionInit(LayoutDimension *dimension)
{
    if (!dimension) {
        return;
    }
    /* The cleared state is `auto` and not a zero length, which is the whole point
     * of the kind: a caller that read a cleared record's `length` would read 0, and
     * 0 is a *size* an author can ask for. */
    dimension->kind = RoboLayoutDimensionAuto;
    dimension->length = 0;
}

LayoutDimension LayoutDimensionAuto(void)
{
    LayoutDimension dimension;

    LayoutDimensionInit(&dimension);
    return dimension;
}

LayoutDimension LayoutDimensionDefinite(GfxPixelX length)
{
    LayoutDimension dimension;

    dimension.kind = RoboLayoutDimensionDefinite;
    dimension.length = length;
    return dimension;
}

LayoutDimension LayoutDimensionUnresolved(void)
{
    LayoutDimension dimension;

    dimension.kind = RoboLayoutDimensionUnresolved;
    dimension.length = 0;
    return dimension;
}

int LayoutDimensionIsAuto(const LayoutDimension *dimension)
{
    return dimension && dimension->kind == RoboLayoutDimensionAuto;
}

int LayoutDimensionIsDefinite(const LayoutDimension *dimension)
{
    return dimension && dimension->kind == RoboLayoutDimensionDefinite;
}

int LayoutDimensionIsUnresolved(const LayoutDimension *dimension)
{
    return dimension && dimension->kind == RoboLayoutDimensionUnresolved;
}

int LayoutDimensionIsValid(const LayoutDimension *dimension)
{
    if (!dimension) {
        return 0;
    }
    if (!LayoutDimensionKindIsKnown(dimension->kind)) {
        return 0;
    }
    /* Only a length can be wrong: a non-negative px length is the whole of what
     * `DEFINITE` means, and the other two kinds carry no number at all. */
    return dimension->kind != RoboLayoutDimensionDefinite || dimension->length >= 0;
}

Result LayoutDimensionLength(const LayoutDimension *dimension,
                                           GfxPixelX *out)
{
    if (!dimension || !out) {
        return RoboInvalidArgument;
    }
    if (dimension->kind != RoboLayoutDimensionDefinite) {
        /* A different answer from a length of zero, and the caller has to be able
         * to tell them apart: `width: 0` is a box an author asked to be empty. */
        return RoboNotFound;
    }
    *out = dimension->length;
    return RoboOk;
}

int LayoutDimensionEqual(const LayoutDimension *a,
                                const LayoutDimension *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    /* The length is compared only where it means something, so two `auto`s built
     * by different paths -- one from `init`, one from `dimension_auto` -- are equal
     * rather than equal-by-luck. */
    if (a->kind != RoboLayoutDimensionDefinite) {
        return 1;
    }
    return a->length == b->length;
}

/* ------------------------------------------------------------------ */
/* The source                                                          */
/* ------------------------------------------------------------------ */

int LayoutDimensionSourceIsKnown(LayoutDimensionSource source)
{
    return source >= RoboLayoutDimensionSourceNone &&
           source <= RoboLayoutDimensionSourceContent;
}

const char *LayoutDimensionSourceName(LayoutDimensionSource source)
{
    switch (source) {
    case RoboLayoutDimensionSourceNone:
        return "none";
    case RoboLayoutDimensionSourceDeclared:
        return "declared";
    case RoboLayoutDimensionSourceAvailable:
        return "available";
    case RoboLayoutDimensionSourceContent:
        return "content";
    default:
        break;
    }
    return "unknown";
}

int LayoutDimensionSourceEqual(LayoutDimensionSource a,
                                       LayoutDimensionSource b)
{
    return a == b;
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void LayoutSizingInit(LayoutSizing *sizing)
{
    if (!sizing) {
        return;
    }
    memset(sizing, 0, sizeof(*sizing));
    /* A cleared record is a *legal* one: both dimensions `auto`, every availability
     * and bound unset, nothing resolved. That is the same choice 10.1 made for the
     * constraints record, and it is what makes `memset` the right implementation --
     * zero is `AUTO` and `SOURCE_NONE` by the enums' own order, so there is no
     * field a reader could find in a state the enum does not define. */
}

void LayoutSizingClear(LayoutSizing *sizing)
{
    LayoutSizingInit(sizing);
}

Result LayoutSizingSetAvailableWidth(LayoutSizing *sizing,
                                                     GfxPixelX extent,
                                                     LayoutDiag *diag)
{
    if (!sizing) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "sizing is NULL");
    }
    if (extent < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "available width is negative");
    }
    sizing->available.width = extent;
    sizing->hasAvailableWidth = 1;
    return RoboOk;
}

Result LayoutSizingSetAvailableHeight(LayoutSizing *sizing,
                                                      GfxPixelY extent,
                                                      LayoutDiag *diag)
{
    if (!sizing) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "sizing is NULL");
    }
    if (extent < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "available height is negative");
    }
    sizing->available.height = extent;
    sizing->hasAvailableHeight = 1;
    return RoboOk;
}

int LayoutSizingHasAvailableWidth(const LayoutSizing *sizing)
{
    return sizing && sizing->hasAvailableWidth;
}

int LayoutSizingHasAvailableHeight(const LayoutSizing *sizing)
{
    return sizing && sizing->hasAvailableHeight;
}

Result LayoutSizingAvailableWidth(const LayoutSizing *sizing,
                                                 GfxPixelX *out)
{
    if (!sizing || !out) {
        return RoboInvalidArgument;
    }
    if (!sizing->hasAvailableWidth) {
        return RoboNotFound;
    }
    *out = sizing->available.width;
    return RoboOk;
}

Result LayoutSizingAvailableHeight(const LayoutSizing *sizing,
                                                  GfxPixelY *out)
{
    if (!sizing || !out) {
        return RoboInvalidArgument;
    }
    if (!sizing->hasAvailableHeight) {
        return RoboNotFound;
    }
    *out = sizing->available.height;
    return RoboOk;
}

Result LayoutSizingSetWidth(LayoutSizing *sizing,
                                           LayoutDimension dimension,
                                           LayoutDiag *diag)
{
    if (!sizing) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "sizing is NULL");
    }
    if (!LayoutDimensionIsValid(&dimension)) {
        /* Refused rather than clamped, for the reason 10.1 gives for a negative
         * available size: nothing in layout can mean anything by a negative width,
         * and this is the one moment the mistake is cheap to see. */
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "a declared width must be a known kind with a "
                                "non-negative length");
    }
    sizing->width = dimension;
    return RoboOk;
}

Result LayoutSizingSetHeight(LayoutSizing *sizing,
                                            LayoutDimension dimension,
                                            LayoutDiag *diag)
{
    if (!sizing) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "sizing is NULL");
    }
    if (!LayoutDimensionIsValid(&dimension)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "a declared height must be a known kind with a "
                                "non-negative length");
    }
    sizing->height = dimension;
    return RoboOk;
}

LayoutDimension LayoutSizingWidth(const LayoutSizing *sizing)
{
    /* `auto` for NULL, which is the same answer the cleared record carries: "this
     * box has been said nothing about" and "there is no record" are one fact about a
     * dimension, and inventing a second answer for the second case would be a second
     * thing for a caller to handle. */
    return sizing ? sizing->width : LayoutDimensionAuto();
}

LayoutDimension LayoutSizingHeight(const LayoutSizing *sizing)
{
    return sizing ? sizing->height : LayoutDimensionAuto();
}

Result LayoutSizingSetMinimum(LayoutSizing *sizing,
                                             LayoutSize size,
                                             LayoutDiag *diag)
{
    if (!sizing) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "sizing is NULL");
    }
    if (size.width < 0 || size.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "minimum size is negative");
    }
    sizing->minimum = size;
    sizing->hasMinimum = 1;
    return RoboOk;
}

Result LayoutSizingSetMaximum(LayoutSizing *sizing,
                                             LayoutSize size,
                                             LayoutDiag *diag)
{
    if (!sizing) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "sizing is NULL");
    }
    if (size.width < 0 || size.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "maximum size is negative");
    }
    sizing->maximum = size;
    sizing->hasMaximum = 1;
    return RoboOk;
}

int LayoutSizingHasMinimum(const LayoutSizing *sizing)
{
    return sizing && sizing->hasMinimum;
}

int LayoutSizingHasMaximum(const LayoutSizing *sizing)
{
    return sizing && sizing->hasMaximum;
}

Result LayoutSizingMinimum(const LayoutSizing *sizing,
                                         LayoutSize *out)
{
    if (!sizing || !out) {
        return RoboInvalidArgument;
    }
    if (!sizing->hasMinimum) {
        return RoboNotFound;
    }
    *out = sizing->minimum;
    return RoboOk;
}

Result LayoutSizingMaximum(const LayoutSizing *sizing,
                                         LayoutSize *out)
{
    if (!sizing || !out) {
        return RoboInvalidArgument;
    }
    if (!sizing->hasMaximum) {
        return RoboNotFound;
    }
    *out = sizing->maximum;
    return RoboOk;
}

int LayoutSizingIsValid(const LayoutSizing *sizing)
{
    if (!sizing) {
        return 0;
    }
    if (!LayoutDimensionIsValid(&sizing->width) ||
        !LayoutDimensionIsValid(&sizing->height)) {
        return 0;
    }
    if (sizing->hasAvailableWidth && sizing->available.width < 0) {
        return 0;
    }
    if (sizing->hasAvailableHeight && sizing->available.height < 0) {
        return 0;
    }
    /* One bound is always consistent with itself, and "nothing is known" is a legal
     * state rather than an invalid one. Only a pair can contradict -- the same rule,
     * restated over this record's fields rather than reached through a conversion. */
    if (sizing->hasMinimum && sizing->hasMaximum) {
        if (sizing->minimum.width > sizing->maximum.width) {
            return 0;
        }
        if (sizing->minimum.height > sizing->maximum.height) {
            return 0;
        }
    }
    return 1;
}

Result LayoutSizingSetContent(LayoutSizing *sizing,
                                             LayoutSize size,
                                             LayoutDiag *diag)
{
    if (!sizing) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "sizing is NULL");
    }
    if (size.width < 0 || size.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "contents extent is negative");
    }
    sizing->content = size;
    sizing->hasContent = 1;
    return RoboOk;
}

int LayoutSizingHasContent(const LayoutSizing *sizing)
{
    return sizing && sizing->hasContent;
}

Result LayoutSizingContent(const LayoutSizing *sizing,
                                         LayoutSize *out)
{
    if (!sizing || !out) {
        return RoboInvalidArgument;
    }
    if (!sizing->hasContent) {
        return RoboNotFound;
    }
    *out = sizing->content;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Resolving                                                           */
/* ------------------------------------------------------------------ */

/**
 * The bounds, as 10.1's record holds them.
 *
 * A conversion rather than a second copy of the arithmetic, and that is the whole
 * point: `LayoutConstraintsClamp` is where "min wins over max" is decided,
 * `RoboLayoutConstraints.h` promised this goal would reach it, and the two records
 * hold their bounds the same way precisely so that reaching it is a copy.
 */
static Result LayoutSizingBounds(const LayoutSizing *sizing,
                                          LayoutConstraints *out,
                                          LayoutDiag *diag)
{
    LayoutConstraintsInit(out);
    if (sizing->hasMinimum) {
        Result status = LayoutConstraintsSetMinimum(out, sizing->minimum, diag);

        if (status != RoboOk) {
            return status;
        }
    }
    if (sizing->hasMaximum) {
        return LayoutConstraintsSetMaximum(out, sizing->maximum, diag);
    }
    return RoboOk;
}

/**
 * Apply the bounds to one dimension's candidate and report whether they moved it.
 *
 * The *other* dimension is carried through the clamp as whatever the record holds,
 * and its result is discarded -- deliberately, because the alternative is a second
 * arithmetic that clamps one number and a header that would then have two places to
 * document. What the caller must not do is read the other field: each resolver only
 * ever writes and only ever flags its own.
 *
 * A pair of bounds that contradict each other is *not* resolved in either's favour:
 * `clamp` reports `RoboNotFound` and this function passes that through, which is
 * what the public header documents.
 */
static Result LayoutSizingClamp(const LayoutSizing *sizing,
                                        LayoutSize candidate,
                                        LayoutSize *out,
                                        LayoutDiag *diag)
{
    LayoutConstraints bounds;
    Result status;

    if (!sizing->hasMinimum && !sizing->hasMaximum) {
        *out = candidate;
        return RoboOk;
    }
    status = LayoutSizingBounds(sizing, &bounds, diag);
    if (status != RoboOk) {
        return status;
    }
    return LayoutConstraintsClamp(&bounds, candidate, out, diag);
}

Result LayoutSizingResolveWidth(LayoutSizing *sizing,
                                               LayoutDiag *diag)
{
    LayoutSize candidate;
    LayoutSize clamped;
    LayoutDimensionSource source;
    GfxPixelX base;
    Result status;

    if (!sizing) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "sizing is NULL");
    }
    if (!LayoutDimensionKindIsKnown(sizing->width.kind)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "the declared width is not a dimension kind");
    }
    if (!LayoutDimensionIsValid(&sizing->width)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "the declared width is negative");
    }

    if (sizing->width.kind == RoboLayoutDimensionDefinite) {
        base = sizing->width.length;
        source = RoboLayoutDimensionSourceDeclared;
    } else {
        /*
         * `AUTO` and `UNRESOLVED` take the same branch, and the header argues why
         * that is the only defensible answer *today*: a percentage needs a containing
         * dimension, and the containing block's content width is the best statement
         * this build can make about one. What keeps the decision honest is that the
         * *dimension* is still `UNRESOLVED` -- the merge happens here, per resolution,
         * rather than at the seam that read the declaration.
         *
         * The available width being unset is a failure and not a zero. A caller that
         * asked for a block's width without saying how wide the containing block is
         * has not given this layer an input it needs, and answering 0 would put a
         * zero-sized box in a document that has no reason to contain one.
         */
        if (!sizing->hasAvailableWidth) {
            return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboNotFound,
                                    "an auto width needs an available width, and none "
                                    "has been recorded");
        }
        base = sizing->available.width;
        source = RoboLayoutDimensionSourceAvailable;
    }

    candidate.width = base;
    candidate.height = sizing->resolved.height;
    status = LayoutSizingClamp(sizing, candidate, &clamped, diag);
    if (status != RoboOk) {
        return status;
    }

    sizing->resolved.width = clamped.width;
    sizing->widthSource = source;
    sizing->widthClamped = clamped.width != base;
    return RoboOk;
}

Result LayoutSizingResolveHeight(LayoutSizing *sizing,
                                                LayoutDiag *diag)
{
    LayoutSize candidate;
    LayoutSize clamped;
    LayoutDimensionSource source;
    GfxPixelY base;
    Result status;

    if (!sizing) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "sizing is NULL");
    }
    if (!LayoutDimensionKindIsKnown(sizing->height.kind)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "the declared height is not a dimension kind");
    }
    if (!LayoutDimensionIsValid(&sizing->height)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "the declared height is negative");
    }

    if (sizing->height.kind == RoboLayoutDimensionDefinite) {
        base = sizing->height.length;
        source = RoboLayoutDimensionSourceDeclared;
    } else {
        /*
         * The mirror of the width's branch, and the asymmetry is the interesting
         * part: a block box's height is *never* its containing block's, so `auto`
         * takes the contents' extent rather than any available height. This build
         * indeed never sets an available height, which is why the flag exists -- a
         * later goal that gives percentage heights something to resolve against will
         * set it, and this branch is where that starts.
         *
         * A contents extent of zero is a *known* zero and resolves; no contents
         * extent at all is a failure, because a caller that has not laid the children
         * out yet has asked a question that has no answer yet.
         */
        if (!sizing->hasContent) {
            return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboNotFound,
                                    "an auto height needs the contents' extent, and "
                                    "none has been recorded");
        }
        base = sizing->content.height;
        source = RoboLayoutDimensionSourceContent;
    }

    candidate.width = sizing->resolved.width;
    candidate.height = base;
    status = LayoutSizingClamp(sizing, candidate, &clamped, diag);
    if (status != RoboOk) {
        return status;
    }

    sizing->resolved.height = clamped.height;
    sizing->heightSource = source;
    sizing->heightClamped = clamped.height != base;
    return RoboOk;
}

Result LayoutSizingResolve(LayoutSizing *sizing,
                                         LayoutDiag *diag)
{
    Result status;

    if (!sizing) {
        return RoboLayoutDiag(diag, RoboLayoutDomainSizing, RoboInvalidArgument,
                                "sizing is NULL");
    }
    status = LayoutSizingResolveWidth(sizing, diag);
    if (status != RoboOk) {
        return status;
    }
    /*
     * The width first, deliberately, and not because the order matters to these two
     * numbers -- it does not, which is why the pair exists. It is so that a caller
     * reading the record after a *failed* height finds a width that really was
     * resolved, rather than a half-filled record whose first field is stale. The
     * half that succeeded is not rolled back: the record is a scratchpad the caller
     * is about to discard, and undoing a correct answer to make a failure look tidy
     * would hide which half went wrong.
     */
    return LayoutSizingResolveHeight(sizing, diag);
}

Result LayoutSizingResolved(const LayoutSizing *sizing,
                                          LayoutSize *out)
{
    if (!sizing || !out) {
        return RoboInvalidArgument;
    }
    /*
     * "Resolved" is `widthSource != NONE`, and that is what the source field is
     * *for*: adding a pair of `hasWidth` flags beside it would be two fields that
     * had to agree, and the day they disagreed the record would have two answers to
     * one question.
     */
    if (sizing->widthSource == RoboLayoutDimensionSourceNone ||
        sizing->heightSource == RoboLayoutDimensionSourceNone) {
        return RoboNotFound;
    }
    *out = sizing->resolved;
    return RoboOk;
}

LayoutDimensionSource LayoutSizingWidthSource(
    const LayoutSizing *sizing)
{
    return sizing ? sizing->widthSource : RoboLayoutDimensionSourceNone;
}

LayoutDimensionSource LayoutSizingHeightSource(
    const LayoutSizing *sizing)
{
    return sizing ? sizing->heightSource : RoboLayoutDimensionSourceNone;
}

int LayoutSizingWidthClamped(const LayoutSizing *sizing)
{
    return sizing && sizing->widthClamped;
}

int LayoutSizingHeightClamped(const LayoutSizing *sizing)
{
    return sizing && sizing->heightClamped;
}

int LayoutSizingEqual(const LayoutSizing *a,
                             const LayoutSizing *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->hasAvailableWidth != b->hasAvailableWidth ||
        a->hasAvailableHeight != b->hasAvailableHeight ||
        a->hasMinimum != b->hasMinimum || a->hasMaximum != b->hasMaximum ||
        a->hasContent != b->hasContent || a->widthClamped != b->widthClamped ||
        a->heightClamped != b->heightClamped) {
        return 0;
    }
    if (!LayoutDimensionEqual(&a->width, &b->width) ||
        !LayoutDimensionEqual(&a->height, &b->height)) {
        return 0;
    }
    if (a->widthSource != b->widthSource || a->heightSource != b->heightSource) {
        return 0;
    }
    /* Only the fields that are set are compared: an unset field's bytes are whatever
     * the caller's stack held, and comparing them would make two equal records
     * unequal -- the same rule 10.1's constraints equality follows. */
    if (a->hasAvailableWidth && !GfxSizeEqual(a->available, b->available)) {
        return 0;
    }
    if (a->hasMinimum && !GfxSizeEqual(a->minimum, b->minimum)) {
        return 0;
    }
    if (a->hasMaximum && !GfxSizeEqual(a->maximum, b->maximum)) {
        return 0;
    }
    if (a->hasContent && !GfxSizeEqual(a->content, b->content)) {
        return 0;
    }
    /* The resolved pair is compared only when both dimensions have been resolved,
     * because until then `resolved` holds one field that means something and one that
     * does not. */
    if (a->widthSource != RoboLayoutDimensionSourceNone &&
        a->heightSource != RoboLayoutDimensionSourceNone) {
        return GfxSizeEqual(a->resolved, b->resolved);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The CSS seam                                                        */
/* ------------------------------------------------------------------ */

int LayoutSizingReadDimension(const CssComputedStyle *style,
                                      const char *property,
                                      LayoutDimension *out)
{
    LayoutStyleValueKind kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;

    if (!out) {
        return 0;
    }
    if (!LayoutStyleClassify(style, property, 0, &kind, &number)) {
        return 0;
    }

    switch (kind) {
    case LAYOUT_STYLE_VALUE_PX:
        /* Rounded here and nowhere else, and rounded at all rather than truncated:
         * `width: 10.5px` has to become one of two integers, and a build that
         * truncated would make every fractional declared width come out a pixel short
         * of what the author wrote. */
        *out = LayoutDimensionDefinite(LayoutPxRound(number));
        return 1;
    case LAYOUT_STYLE_VALUE_AUTO:
    case LAYOUT_STYLE_VALUE_ABSENT:
        *out = LayoutDimensionAuto();
        return 1;
    case LAYOUT_STYLE_VALUE_UNUSABLE:
    default:
        /* A percentage, a relative unit, or a keyword that names no length. Kept as
         * its own kind rather than folded into `auto`: it is a statement about the
         * document, and the resolution rule for it is the one that will change. */
        *out = LayoutDimensionUnresolved();
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* The two readers 10.3 wrote, now sharing the classification above     */
/* ------------------------------------------------------------------ */

GfxPixelX LayoutPxRound(double value)
{
    /*
     * Half away from zero, which this door's comment has claimed since 10.8 and which
     * was only true for non-negative values until 10.9 -- because until 10.9 nothing
     * signed reached it. A margin does: `margin: -10.5px` has to become -11 rather
     * than -10, and a build that rounded a negative margin *towards* zero would make
     * every fractional negative margin a pixel small in the one direction an author
     * would notice (the box would not reach as far as it was told to).
     */
    return value < 0.0 ? (GfxPixelX)(value - 0.5) : (GfxPixelX)(value + 0.5);
}

int LayoutStylePxLength(const CssComputedStyle *style,
                                const char *property,
                                double *outValue)
{
    LayoutStyleValueKind kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;

    if (!outValue) {
        return 0;
    }
    if (!LayoutStyleClassify(style, property, 0, &kind, &number)) {
        return 0;
    }
    if (kind != LAYOUT_STYLE_VALUE_PX) {
        /* `auto`, an absent entry, a percentage and a relative unit are all "no
         * usable px length" to this reader, which is exactly what 10.3's version
         * answered. The *classification* of them is above, for the reader that can
         * tell them apart. */
        return 0;
    }
    *outValue = number;
    return 1;
}
