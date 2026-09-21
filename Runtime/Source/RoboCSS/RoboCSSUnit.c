/*
 * RoboCSS units and numbers (Phase 9.10) — see RoboCSSUnit.h.
 *
 * Four things live here and nothing else:
 *
 *   the set      the closed vocabulary, one table row per unit: its spelling,
 *                its family, its fixed factor to px when it has one, and — for
 *                a viewport unit — which viewport and which dimension it
 *                measures
 *   the context  a plain struct of numbers with a `has_` flag each, and the
 *                setters that are the only way to fill one
 *   resolve      the one place a relative length becomes pixels, as a *question*
 *                with four answers rather than a multiplication
 *   numeric      the four kinds, their constructors and equality, the parse that
 *                reads them out of a value tree, and one canonical spelling
 *
 * The unit table is the whole of the vocabulary, and every accessor is a lookup in
 * it. That is the property worth protecting: there is one row per unit, and adding
 * `lh` or `dvw` is adding a row rather than adding a branch to four functions.
 *
 * Nothing here uses math.h. The runtime is freestanding on the guest side, and
 * every function of it this file could want is one comparison: finiteness, minimum,
 * maximum and "is this whole" are all arithmetic the C language already has.
 */

#include "RoboCSS/RoboCSSUnit.h"

#include "RoboCSSInternal.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The unit table                                                      */
/* ------------------------------------------------------------------ */

/** Which dimension of a viewport a unit measures. NONE for a non-viewport unit. */
typedef enum unitViewportMeasure {
    UNIT_VIEWPORT_NONE = 0,
    /** `vw`, and `vi` in a horizontal writing mode. */
    UNIT_VIEWPORT_WIDTH,
    /** `vh`, and `vb` in a horizontal writing mode. */
    UNIT_VIEWPORT_HEIGHT,
    UNIT_VIEWPORT_MIN,
    UNIT_VIEWPORT_MAX,
    /** `vi`: the inline axis, so width horizontally and height vertically. */
    UNIT_VIEWPORT_INLINE,
    /** `vb`: the block axis, so height horizontally and width vertically. */
    UNIT_VIEWPORT_BLOCK
} UnitViewportMeasure;

/** One unit: everything any accessor needs to answer about it. */
typedef struct unitEntry {
    const char *name;
    CssUnit unit;
    CssUnitClass unitClass;
    /** Factor to px for an absolute unit; 0.0 when the unit has no fixed factor. */
    double toPx;
    /** Which viewport a viewport unit measures, and which of its dimensions. */
    CssViewportFamily family;
    UnitViewportMeasure measure;
} UnitEntry;

/*
 * The closed set.
 *
 * The px factors are CSS's own, and they are exact by definition rather than by
 * measurement: 1in is 96px, 1cm is 96/2.54px, 1mm is 1/10cm, 1Q is 1/40cm, 1pt is
 * 1/72in, 1pc is 12pt. Nothing here consults a display's DPI, which is the point —
 * an absolute length means the same thing everywhere, and only a *used* value (a
 * later phase's) may care about the device.
 *
 * The order is the enumerator order, so `CssUnitName` and friends walk a
 * table whose index order matches the enum and a reader can check one against the
 * other by eye.
 */
static const UnitEntry unitEntries[] = {
    /* absolute */
    { "px",    RoboCssUnitPx,    RoboCssUnitClassAbsolute, 1.0,            RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "cm",    RoboCssUnitCm,    RoboCssUnitClassAbsolute, 96.0 / 2.54,    RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "mm",    RoboCssUnitMm,    RoboCssUnitClassAbsolute, 96.0 / 25.4,    RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "q",     RoboCssUnitQ,     RoboCssUnitClassAbsolute, 96.0 / 101.6,   RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "in",    RoboCssUnitIn,    RoboCssUnitClassAbsolute, 96.0,           RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "pt",    RoboCssUnitPt,    RoboCssUnitClassAbsolute, 96.0 / 72.0,    RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "pc",    RoboCssUnitPc,    RoboCssUnitClassAbsolute, 16.0,           RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    /* font-relative */
    { "em",    RoboCssUnitEm,    RoboCssUnitClassFontRelative, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "rem",   RoboCssUnitRem,   RoboCssUnitClassFontRelative, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "ex",    RoboCssUnitEx,    RoboCssUnitClassFontRelative, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "ch",    RoboCssUnitCh,    RoboCssUnitClassFontRelative, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "lh",    RoboCssUnitLh,    RoboCssUnitClassFontRelative, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    { "rlh",   RoboCssUnitRlh,   RoboCssUnitClassFontRelative, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_NONE },
    /* viewport: the default family is the large viewport */
    { "vw",    RoboCssUnitVw,    RoboCssUnitClassViewport, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_WIDTH },
    { "vh",    RoboCssUnitVh,    RoboCssUnitClassViewport, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_HEIGHT },
    { "vmin",  RoboCssUnitVmin,  RoboCssUnitClassViewport, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_MIN },
    { "vmax",  RoboCssUnitVmax,  RoboCssUnitClassViewport, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_MAX },
    { "vi",    RoboCssUnitVi,    RoboCssUnitClassViewport, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_INLINE },
    { "vb",    RoboCssUnitVb,    RoboCssUnitClassViewport, 0.0, RoboCssViewportDefault, UNIT_VIEWPORT_BLOCK },
    /* viewport: the small family */
    { "svw",   RoboCssUnitSvw,   RoboCssUnitClassViewport, 0.0, RoboCssViewportSmall, UNIT_VIEWPORT_WIDTH },
    { "svh",   RoboCssUnitSvh,   RoboCssUnitClassViewport, 0.0, RoboCssViewportSmall, UNIT_VIEWPORT_HEIGHT },
    { "svmin", RoboCssUnitSvmin, RoboCssUnitClassViewport, 0.0, RoboCssViewportSmall, UNIT_VIEWPORT_MIN },
    { "svmax", RoboCssUnitSvmax, RoboCssUnitClassViewport, 0.0, RoboCssViewportSmall, UNIT_VIEWPORT_MAX },
    /* viewport: the large family */
    { "lvw",   RoboCssUnitLvw,   RoboCssUnitClassViewport, 0.0, RoboCssViewportLarge, UNIT_VIEWPORT_WIDTH },
    { "lvh",   RoboCssUnitLvh,   RoboCssUnitClassViewport, 0.0, RoboCssViewportLarge, UNIT_VIEWPORT_HEIGHT },
    { "lvmin", RoboCssUnitLvmin, RoboCssUnitClassViewport, 0.0, RoboCssViewportLarge, UNIT_VIEWPORT_MIN },
    { "lvmax", RoboCssUnitLvmax, RoboCssUnitClassViewport, 0.0, RoboCssViewportLarge, UNIT_VIEWPORT_MAX },
    /* viewport: the dynamic family */
    { "dvw",   RoboCssUnitDvw,   RoboCssUnitClassViewport, 0.0, RoboCssViewportDynamic, UNIT_VIEWPORT_WIDTH },
    { "dvh",   RoboCssUnitDvh,   RoboCssUnitClassViewport, 0.0, RoboCssViewportDynamic, UNIT_VIEWPORT_HEIGHT },
    { "dvmin", RoboCssUnitDvmin, RoboCssUnitClassViewport, 0.0, RoboCssViewportDynamic, UNIT_VIEWPORT_MIN },
    { "dvmax", RoboCssUnitDvmax, RoboCssUnitClassViewport, 0.0, RoboCssViewportDynamic, UNIT_VIEWPORT_MAX }
};

/** The row for @p unit, or NULL for NONE and for an unknown enumerator. */
static const UnitEntry *UnitLookup(CssUnit unit)
{
    size_t index;

    if (unit == RoboCssUnitNone) {
        return NULL;
    }
    for (index = 0u; index < sizeof(unitEntries) / sizeof(unitEntries[0]); index++) {
        if (unitEntries[index].unit == unit) {
            return &unitEntries[index];
        }
    }
    return NULL;
}

int CssUnitIsKnown(CssUnit unit)
{
    return unit >= RoboCssUnitNone && unit < (CssUnit)RoboCssUnitCount;
}

int CssUnitIsAbsolute(CssUnit unit)
{
    /* Asked of the table, not of `toPx`: that function answers with the value it
     * was given when the unit has no fixed factor, so a relative unit and an
     * unknown one both come back looking convertible. The factor being present is
     * the fact this predicate is about. */
    const UnitEntry *entry = UnitLookup(unit);

    return entry != NULL && entry->toPx != 0.0;
}

CssUnitClass MakeCssUnitClass(CssUnit unit)
{
    const UnitEntry *entry = UnitLookup(unit);

    return entry ? entry->unitClass : RoboCssUnitClassNone;
}

const char *CssUnitClassName(CssUnitClass unitClass)
{
    switch (unitClass) {
    case RoboCssUnitClassAbsolute:
        return "absolute";
    case RoboCssUnitClassFontRelative:
        return "font-relative";
    case RoboCssUnitClassViewport:
        return "viewport";
    default:
        return "none";
    }
}

int CssUnitIsViewport(CssUnit unit)
{
    return MakeCssUnitClass(unit) == RoboCssUnitClassViewport;
}

CssViewportFamily CssUnitViewportFamily(CssUnit unit)
{
    const UnitEntry *entry = UnitLookup(unit);

    return entry ? entry->family : RoboCssViewportDefault;
}

const char *CssUnitName(CssUnit unit)
{
    const UnitEntry *entry = UnitLookup(unit);

    return entry ? entry->name : "";
}

CssUnit CssUnitFromSlice(CssSlice name)
{
    size_t index;

    if (CssSliceIsAbsent(name) || name.length == 0u) {
        return RoboCssUnitNone;
    }
    for (index = 0u; index < sizeof(unitEntries) / sizeof(unitEntries[0]); index++) {
        if (CssBytesEqualAsciiCi(name.bytes, name.length, unitEntries[index].name)) {
            return unitEntries[index].unit;
        }
    }
    return RoboCssUnitNone;
}

CssUnit CssUnitFromText(const char *name)
{
    if (!name) {
        return RoboCssUnitNone;
    }
    return CssUnitFromSlice(CssSliceFromText(name));
}

double CssUnitToPx(double value, CssUnit unit)
{
    const UnitEntry *entry = UnitLookup(unit);

    if (entry && entry->toPx != 0.0) {
        return value * entry->toPx;
    }
    return value;
}

/* ------------------------------------------------------------------ */
/* The resolution context                                              */
/* ------------------------------------------------------------------ */

/*
 * The setters exist rather than callers assigning fields, and the reason is the
 * pairing: every value has a `has_` flag that must be set with it, and a writer
 * that set one without the other would produce a context that resolves lengths
 * against numbers nobody supplied. A setter cannot forget.
 */

void CssUnitContextInit(CssUnitContext *context)
{
    if (context) {
        memset(context, 0, sizeof(*context));
    }
}

void CssUnitContextSetFont(CssUnitContext *context,
                                    double fontSize,
                                    double xHeight,
                                    double zeroWidth,
                                    double lineHeight)
{
    if (!context) {
        return;
    }
    context->hasFontMetrics = 1;
    context->fontSize = fontSize;
    context->xHeight = xHeight;
    context->zeroWidth = zeroWidth;
    context->lineHeight = lineHeight;
}

void CssUnitContextSetRootFont(CssUnitContext *context,
                                         double rootFontSize,
                                         double rootLineHeight)
{
    if (!context) {
        return;
    }
    context->hasRootFontMetrics = 1;
    context->rootFontSize = rootFontSize;
    context->rootLineHeight = rootLineHeight;
}

void CssUnitContextSetViewport(CssUnitContext *context,
                                        double width,
                                        double height)
{
    if (!context) {
        return;
    }
    context->hasViewport = 1;
    context->viewportWidth = width;
    context->viewportHeight = height;
}

void CssUnitContextSetSmallViewport(CssUnitContext *context,
                                              double width,
                                              double height)
{
    if (!context) {
        return;
    }
    context->hasSmallViewport = 1;
    context->smallViewportWidth = width;
    context->smallViewportHeight = height;
}

void CssUnitContextSetLargeViewport(CssUnitContext *context,
                                              double width,
                                              double height)
{
    if (!context) {
        return;
    }
    context->hasLargeViewport = 1;
    context->largeViewportWidth = width;
    context->largeViewportHeight = height;
}

void CssUnitContextSetDynamicViewport(CssUnitContext *context,
                                                double width,
                                                double height)
{
    if (!context) {
        return;
    }
    context->hasDynamicViewport = 1;
    context->dynamicViewportWidth = width;
    context->dynamicViewportHeight = height;
}

/* ------------------------------------------------------------------ */
/* Contextual resolution                                               */
/* ------------------------------------------------------------------ */

int CssLengthResolutionIsKnown(CssLengthResolution resolution)
{
    return resolution >= RoboCssLengthResolved &&
           resolution < (CssLengthResolution)RoboCssLengthResolutionCount;
}

const char *CssLengthResolutionName(CssLengthResolution resolution)
{
    switch (resolution) {
    case RoboCssLengthResolved:
        return "resolved";
    case RoboCssLengthNeedsContext:
        return "needs-context";
    case RoboCssLengthNotALength:
        return "not-a-length";
    case RoboCssLengthInvalid:
        return "invalid";
    default:
        return "unknown";
    }
}

/** @p a and @p b, smaller first. Replaces fmin/fmax, which need math.h. */
static double UnitSmaller(double a, double b)
{
    return a < b ? a : b;
}

static double UnitLarger(double a, double b)
{
    return a > b ? a : b;
}

/**
 * The viewport size @p family measures, as two finite dimensions.
 *
 * The fallback chain is the design (see the header): a family with its own size
 * uses it, and a family without one uses the default, because a window whose
 * chrome never moves has one size and it is all four of them. Only the *default*
 * is required — a caller that set no viewport at all gets NEEDS_CONTEXT, and a
 * caller that set only the default gets a correct answer for all sixteen units.
 *
 * @return 1 and writes both outputs on success; 0 when the context has no viewport
 *         to offer, or when the size it offers is not finite.
 */
static int UnitViewportSize(const CssUnitContext *context,
                              CssViewportFamily family,
                              double *outWidth,
                              double *outHeight,
                              int *outUnusable)
{
    double width = 0.0;
    double height = 0.0;
    int have = 0;

    *outUnusable = 0;
    if (!context) {
        return 0;
    }
    /* The family's own size when it has one, and the default when it does not. An
     * if/else chain rather than a switch, because three of the four arms do the
     * same thing and a switch would say they do three different things. */
    if (family == RoboCssViewportSmall && context->hasSmallViewport) {
        width = context->smallViewportWidth;
        height = context->smallViewportHeight;
        have = 1;
    } else if (family == RoboCssViewportLarge && context->hasLargeViewport) {
        width = context->largeViewportWidth;
        height = context->largeViewportHeight;
        have = 1;
    } else if (family == RoboCssViewportDynamic && context->hasDynamicViewport) {
        width = context->dynamicViewportWidth;
        height = context->dynamicViewportHeight;
        have = 1;
    }
    if (!have) {
        if (!context->hasViewport) {
            return 0;
        }
        width = context->viewportWidth;
        height = context->viewportHeight;
    }
    if (!CssNumberIsFinite(width) || !CssNumberIsFinite(height)) {
        /* An infinite viewport is not a size, and `50vw` of one is not a length.
         * Reported apart from "I do not know", because a caller can fix the
         * first and can only ask for the second. */
        *outUnusable = 1;
        return 0;
    }
    *outWidth = width;
    *outHeight = height;
    return 1;
}

/**
 * The one context number @p unit needs, or why there is not one.
 *
 * A switch over the font-relative units rather than a loop over the table,
 * because which *input* a unit needs is a fact about the unit that the table's
 * columns would have to grow a member to carry — and the member would be read in
 * exactly one place, which is here.
 *
 * @return 1 when @p outValue holds a finite number; 0 otherwise, with
 *         @p outUnusable saying whether the caller supplied a value that is not
 *         one (unusable) or supplied nothing (missing).
 */
static int UnitFontRelativeInput(const CssUnitContext *context,
                                    CssUnit unit,
                                    double *outValue,
                                    int *outUnusable)
{
    double value;

    *outUnusable = 0;
    if (!context) {
        return 0;
    }
    switch (unit) {
    case RoboCssUnitEm:
        if (!context->hasFontMetrics) {
            return 0;
        }
        value = context->fontSize;
        break;
    case RoboCssUnitEx:
        if (!context->hasFontMetrics) {
            return 0;
        }
        value = context->xHeight;
        break;
    case RoboCssUnitCh:
        if (!context->hasFontMetrics) {
            return 0;
        }
        value = context->zeroWidth;
        break;
    case RoboCssUnitLh:
        if (!context->hasFontMetrics) {
            return 0;
        }
        value = context->lineHeight;
        break;
    case RoboCssUnitRem:
        if (!context->hasRootFontMetrics) {
            return 0;
        }
        value = context->rootFontSize;
        break;
    case RoboCssUnitRlh:
        if (!context->hasRootFontMetrics) {
            return 0;
        }
        value = context->rootLineHeight;
        break;
    default:
        return 0;
    }
    if (!CssNumberIsFinite(value)) {
        *outUnusable = 1;
        return 0;
    }
    *outValue = value;
    return 1;
}

CssLengthResolution CssUnitResolvePx(double value,
                                                      CssUnit unit,
                                                      const CssUnitContext *context,
                                                      double *outPx)
{
    const UnitEntry *entry = UnitLookup(unit);
    double resolved;

    if (!entry) {
        return RoboCssLengthNotALength;
    }
    if (!CssNumberIsFinite(value)) {
        return RoboCssLengthInvalid;
    }

    switch (entry->unitClass) {
    case RoboCssUnitClassAbsolute:
        /* No context needed, and none consulted: an absolute length is a
         * definition rather than a measurement. */
        resolved = value * entry->toPx;
        break;
    case RoboCssUnitClassFontRelative: {
        double factor = 0.0;
        int unusable = 0;

        if (!UnitFontRelativeInput(context, unit, &factor, &unusable)) {
            return unusable ? RoboCssLengthInvalid : RoboCssLengthNeedsContext;
        }
        resolved = value * factor;
        break;
    }
    case RoboCssUnitClassViewport: {
        double width = 0.0;
        double height = 0.0;
        double dimension;
        int unusable = 0;

        if (!UnitViewportSize(context, entry->family, &width, &height, &unusable)) {
            return unusable ? RoboCssLengthInvalid : RoboCssLengthNeedsContext;
        }
        switch (entry->measure) {
        case UNIT_VIEWPORT_WIDTH:
            dimension = width;
            break;
        case UNIT_VIEWPORT_HEIGHT:
            dimension = height;
            break;
        case UNIT_VIEWPORT_MIN:
            dimension = UnitSmaller(width, height);
            break;
        case UNIT_VIEWPORT_MAX:
            dimension = UnitLarger(width, height);
            break;
        case UNIT_VIEWPORT_INLINE:
            dimension = context->verticalWriting ? height : width;
            break;
        case UNIT_VIEWPORT_BLOCK:
            dimension = context->verticalWriting ? width : height;
            break;
        default:
            return RoboCssLengthNotALength;
        }
        resolved = value * (dimension / 100.0);
        break;
    }
    default:
        return RoboCssLengthNotALength;
    }

    if (!CssNumberIsFinite(resolved)) {
        /* The multiplication overflowed: `1e308in` is a number and is not a
         * length, and an infinity that reached a layout would be worse than a
         * refusal. */
        return RoboCssLengthInvalid;
    }
    if (outPx) {
        *outPx = resolved;
    }
    return RoboCssLengthResolved;
}

/* ------------------------------------------------------------------ */
/* The typed numeric value                                             */
/* ------------------------------------------------------------------ */

int CssNumericKindIsKnown(CssNumericKind kind)
{
    return kind >= RoboCssNumericNone &&
           kind < (CssNumericKind)RoboCssNumericKindCount;
}

const char *CssNumericKindName(CssNumericKind kind)
{
    switch (kind) {
    case RoboCssNumericNumber:
        return "number";
    case RoboCssNumericInteger:
        return "integer";
    case RoboCssNumericPercentage:
        return "percentage";
    case RoboCssNumericLength:
        return "length";
    default:
        return "none";
    }
}

CssNumeric CssNumericNone(void)
{
    CssNumeric numeric;

    numeric.kind = RoboCssNumericNone;
    numeric.number = 0.0;
    numeric.unit = RoboCssUnitNone;
    return numeric;
}

Result CssNumericMakeNumber(CssNumeric *out,
                                           double number,
                                           int isInteger,
                                           CssDiag *diag)
{
    if (!out) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL");
    }
    if (!CssNumberIsFinite(number)) {
        /* A programmer error rather than a parse outcome: the tokenizer cannot
         * produce one of these without the parser having rejected it, and a value
         * that cannot be written is not a value. */
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "a numeric value must be finite");
    }
    *out = CssNumericNone();
    out->kind = isInteger ? RoboCssNumericInteger : RoboCssNumericNumber;
    out->number = number;
    return RoboOk;
}

Result CssNumericMakePercentage(CssNumeric *out,
                                               double percentage,
                                               CssDiag *diag)
{
    if (!out) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL");
    }
    if (!CssNumberIsFinite(percentage)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "a numeric value must be finite");
    }
    *out = CssNumericNone();
    out->kind = RoboCssNumericPercentage;
    out->number = percentage;
    return RoboOk;
}

Result CssNumericMakeLength(CssNumeric *out,
                                           double number,
                                           CssUnit unit,
                                           CssDiag *diag)
{
    if (!out) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL");
    }
    if (!CssNumberIsFinite(number)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "a numeric value must be finite");
    }
    if (!CssUnitIsKnown(unit) || unit == RoboCssUnitNone) {
        /* A length with no unit is a number, and a caller that meant that should
         * say so rather than getting a length whose unit is nothing. */
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "a length needs a known, non-empty unit");
    }
    *out = CssNumericNone();
    out->kind = RoboCssNumericLength;
    out->number = number;
    out->unit = unit;
    return RoboOk;
}

int CssNumericIsPresent(CssNumeric numeric)
{
    return numeric.kind != RoboCssNumericNone;
}

int CssNumericIsIntegral(CssNumeric numeric)
{
    if (!CssNumericIsPresent(numeric)) {
        return 0;
    }
    /* The range check comes first, and that order is the whole of numeric safety
     * here: casting a double outside the integer range is undefined behaviour, so
     * a function that asked "is this whole" by casting and then checking would
     * have already invoked the bug it was trying to guard against. */
    if (!(numeric.number <= RoboCssNumericIntegerLimit &&
          numeric.number >= -RoboCssNumericIntegerLimit)) {
        /* Beyond 2^53 a double cannot represent every integer, so "is this whole"
         * has no reliable answer and the honest one is no. */
        return 0;
    }
    return (double)(int64_t)numeric.number == numeric.number;
}

int CssNumericIntegerValue(CssNumeric numeric, int64_t *outInteger)
{
    if (numeric.kind != RoboCssNumericInteger) {
        /* A NUMBER that happens to be whole is a different question's answer. */
        return 0;
    }
    if (numeric.number > RoboCssNumericIntegerLimit ||
        numeric.number < -RoboCssNumericIntegerLimit) {
        /* Refused rather than cast: the cast would be undefined behaviour, and
         * rounding would turn an out-of-range value into a plausible small one. */
        return 0;
    }
    if (outInteger) {
        *outInteger = (int64_t)numeric.number;
    }
    return 1;
}

int CssNumericEquals(CssNumeric a, CssNumeric b)
{
    if (a.kind != b.kind) {
        /* `0` is not `0%` and `0px` is not `0`: the difference is what property
         * the value belongs to, and a comparison that ignored it is how a length
         * gets used as a proportion. */
        return 0;
    }
    if (a.kind == RoboCssNumericNone) {
        return 1;
    }
    if (a.number != b.number) {
        return 0;
    }
    if (a.kind == RoboCssNumericLength) {
        return a.unit == b.unit;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

int CssNumericParseIsKnown(CssNumericParse outcome)
{
    return outcome >= RoboCssNumericParseInvalid &&
           outcome < (CssNumericParse)RoboCssNumericParseCount;
}

const char *CssNumericParseName(CssNumericParse outcome)
{
    switch (outcome) {
    case RoboCssNumericParseResolved:
        return "resolved";
    case RoboCssNumericParseDeferred:
        return "deferred";
    case RoboCssNumericParseUnknownUnit:
        return "unknown-unit";
    case RoboCssNumericParseNonFinite:
        return "non-finite";
    default:
        return "invalid";
    }
}

/** Non-zero when @p value is the one function @p name, which is deferred. */
static int NumericFunctionIsNamed(const CssValue *value, const char *name)
{
    return CssValueFunctionNamed(value, name);
}

/**
 * Non-zero when @p value is a function whose evaluation this build defers.
 *
 * Five names, and they are one group because they are one thing: a *reference*.
 * `calc()` and its three relatives are arithmetic over lengths that needs a
 * containing block, and `var()` is a custom-property substitution; none of them is
 * a number yet, and each is legal CSS. Treating them as a mistake would be a lie
 * about the author, and evaluating them here would be a lie about the value.
 */
static int NumericFunctionIsDeferred(const CssValue *value)
{
    static const char *const deferred[] = { "calc", "min", "max", "clamp", "var", "env" };
    size_t index;

    for (index = 0u; index < sizeof(deferred) / sizeof(deferred[0]); index++) {
        if (NumericFunctionIsNamed(value, deferred[index])) {
            return 1;
        }
    }
    return 0;
}

CssNumericParse CssNumericFromToken(const CssToken *token,
                                                     CssNumeric *out)
{
    CssNumeric numeric = CssNumericNone();

    if (!token || !out) {
        return RoboCssNumericParseInvalid;
    }
    if (!CssNumberIsFinite(token->number)) {
        /* Legal syntax, unrepresentable magnitude: `1e999` is a number as CSS
         * spells it and an infinity as a double holds it. Reported apart from
         * INVALID, because "your number is too big" is a fixable thing to know. */
        return RoboCssNumericParseNonFinite;
    }
    switch (token->type) {
    case RoboCssTokenNumber:
        if (CssNumericMakeNumber(&numeric, token->number, token->isInteger,
                                         NULL) != RoboOk) {
            return RoboCssNumericParseNonFinite;
        }
        break;
    case RoboCssTokenPercentage:
        if (CssNumericMakePercentage(&numeric, token->number, NULL) != RoboOk) {
            return RoboCssNumericParseNonFinite;
        }
        break;
    case RoboCssTokenDimension: {
        CssUnit unit = CssUnitFromSlice(token->unit);

        if (unit == RoboCssUnitNone) {
            /* `10foo`: the number and the shape are fine and the unit is one this
             * build does not know. */
            return RoboCssNumericParseUnknownUnit;
        }
        if (CssNumericMakeLength(&numeric, token->number, unit, NULL) != RoboOk) {
            return RoboCssNumericParseNonFinite;
        }
        break;
    }
    default:
        /* An identifier, a string, a `%`, a bracket: none of them is a number. */
        return RoboCssNumericParseInvalid;
    }
    *out = numeric;
    return RoboCssNumericParseResolved;
}

CssNumericParse CssNumericFromValue(const CssValue *specified,
                                                     CssNumeric *out)
{
    const CssValue *single;

    if (!out) {
        return RoboCssNumericParseInvalid;
    }
    single = CssValueSingle(specified);
    if (!single) {
        /* Nothing, or more than one significant component: `10 20` is two numbers
         * and therefore not one, and `1.2.3` arrives as three components because
         * the tokenizer is lossless and the grammar is total. */
        return RoboCssNumericParseInvalid;
    }
    if (single->kind == RoboCssValueKindFunction) {
        if (NumericFunctionIsDeferred(single)) {
            return RoboCssNumericParseDeferred;
        }
        /* A function that is not a reference: `rgb(…)`, `url(…)`. Not a number. */
        return RoboCssNumericParseInvalid;
    }
    if (single->kind != RoboCssValueKindToken) {
        /* A block or a nested list is not a numeric value. */
        return RoboCssNumericParseInvalid;
    }
    return CssNumericFromToken(&single->token, out);
}

/* ------------------------------------------------------------------ */
/* Serialization                                                       */
/* ------------------------------------------------------------------ */

/** The buffer the fixed-point rendering is built in before it is copied out. */
#define NUMERIC_DIGITS_MAX 64u

/**
 * @p value rendered canonically into @p out, NUL-terminated.
 *
 * The rules, and each is a decision:
 *
 *   `%.0f` when the value is whole, so `10` has no decimal point;
 *   `%.6f` otherwise, with trailing zeroes and then a bare `.` stripped, so `1.5`
 *   is `1.5` and `0.50` is `0.5`;
 *   `-0` becomes `0`, because it is zero and has no sign;
 *   and a **non-zero value that the fixed-point precision renders as zero** falls
 *   back to a compact exponent form. That last rule is the one that stops this
 *   function lying: `1e-07` rounded to six decimals is `0.000000`, and a value
 *   that is not zero must not print as though it were.
 *
 * Hex-float or `%g`-everything were both available and both rejected: the pinned
 * shape of every number this subsystem has emitted since 9.8 is fixed-point, and a
 * serialization that changes notation with the magnitude is harder to read and no
 * more correct.
 */
static int NumericRender(double value, char *out, size_t capacity)
{
    char digits[NUMERIC_DIGITS_MAX];
    int whole = 0;
    int written;
    size_t length;

    /* The in-range test guards the cast, in that order: a cast of a double outside
     * the integer range is undefined behaviour, and `1e300` is a value this
     * function is asked to render. */
    if (value <= RoboCssNumericIntegerLimit &&
        value >= -RoboCssNumericIntegerLimit &&
        (double)(int64_t)value == value) {
        whole = 1;
    }
    written = snprintf(digits, sizeof(digits), whole ? "%.0f" : "%.6f", value);
    if (written < 0 || (size_t)written >= sizeof(digits)) {
        return 0;
    }
    length = (size_t)written;
    if (!whole) {
        while (length > 0u && digits[length - 1u] == '0') {
            length--;
        }
        if (length > 0u && digits[length - 1u] == '.') {
            length--;
        }
    }
    /*
     * Negative zero is zero, and it is normalized *after* the two branches
     * rather than inside the fractional one. `-0` reaches here with no fraction
     * to strip — `%.0f` renders it whole — so a rule written only for the
     * fractional path would leave the sign on a value that is zero, which is a
     * spelling no reader should have to know means nothing.
     */
    if (length == 0u || (length == 2u && digits[0] == '-' && digits[1] == '0')) {
        digits[0] = '0';
        length = 1u;
    }
    if (!whole) {
        if (length == 1u && digits[0] == '0' && value != 0.0) {
            /* Below the fixed-point precision and not zero: a compact form says
             * what the fixed-point one would have denied. */
            written = snprintf(digits, sizeof(digits), "%.6g", value);
            if (written < 0 || (size_t)written >= sizeof(digits)) {
                return 0;
            }
            length = (size_t)written;
        }
    }
    if (capacity <= length) {
        return 0;
    }
    memcpy(out, digits, length);
    out[length] = '\0';
    return 1;
}

Result CssNumericWrite(CssNumeric numeric,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     CssDiag *diag)
{
    char digits[NUMERIC_DIGITS_MAX];
    size_t used = 0u;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!CssNumericIsPresent(numeric)) {
        /* Nothing to write, and that is a success: a cleared record has no text,
         * exactly as a cleared computed value does. */
        return RoboOk;
    }
    if (!NumericRender(numeric.number, digits, sizeof(digits))) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotSupported,
                             "the number does not fit its canonical form");
    }
    {
        size_t length = strlen(digits);

        if (length >= capacity) {
            *outLength = length;
            return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotSupported,
                                 "the numeric value's text does not fit");
        }
        memcpy(buffer, digits, length);
        used = length;
        buffer[used] = '\0';
    }
    if (numeric.kind == RoboCssNumericPercentage) {
        if (used + 1u >= capacity) {
            buffer[0] = '\0';
            *outLength = used + 1u;
            return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotSupported,
                                 "the numeric value's text does not fit");
        }
        buffer[used++] = '%';
        buffer[used] = '\0';
    } else if (numeric.kind == RoboCssNumericLength) {
        /* The canonical spelling from the table, which is also what proves the
         * unit's case-insensitivity: `10PX` writes as `10px`. */
        const char *unit = CssUnitName(numeric.unit);
        size_t length = strlen(unit);

        if (length == 0u || used + length >= capacity) {
            buffer[0] = '\0';
            *outLength = used + length;
            return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotSupported,
                                 "the numeric value's text does not fit");
        }
        memcpy(buffer + used, unit, length);
        used += length;
        buffer[used] = '\0';
    }
    *outLength = used;
    return RoboOk;
}
