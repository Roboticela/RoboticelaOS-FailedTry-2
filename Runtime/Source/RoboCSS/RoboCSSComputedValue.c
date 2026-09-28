/*
 * RoboCSS computed values (Phase 9.8) — see RoboCSSComputedValue.h.
 *
 * Two things live here and nothing else:
 *
 *   compute    the one dispatch from "what shape is this property" to a typed
 *              value, which is the whole of this phase's normalisation
 *   preserve   the token-tree readers and writer a PRESERVED value needs, and
 *              the numeric writer, which are the parts nothing else owns
 *
 * It used to be four. 9.9 took the colour model out to RoboCSSColor.c and 9.10 took
 * the unit vocabulary and the numeric parse out to RoboCSSUnit.c, and in both cases
 * the reason was the same: this file was their only reader until a second one
 * arrived, and a subsystem with two readers needs a name and a header rather than a
 * second copy. This layer now owns the *dispatch* — which shape is this property,
 * and what does a value of that shape become — and delegates the shapes it does not
 * own to the modules that do.
 *
 * Two habits are worth naming because they are what keeps the file honest.
 *
 * First, every path that reads a token reads its *value* rather than its `text`:
 * `R\47 B` is `rgb`, `\72 ed` is `red`, and a hex digit cannot be an escape
 * because escapes were already resolved by the time this file looks. Using the
 * raw bytes would make an escape spelling a different colour, which is exactly
 * the bug CSS's escape rule exists to prevent.
 *
 * Second, nothing here resolves a relative length or a percentage. `2em` is
 * carried as 2 em and `50%` as 50 percent because the information needed to go
 * further — a parent font size, a containing block — does not exist at this
 * stage. A converter that guessed would produce a number that looked computed
 * and was wrong.
 */

#include "RoboCSS/RoboCSSComputedValue.h"

#include "RoboCSS/RoboCSSOpacity.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Units — moved to RoboCSSUnit.c in 9.10                               */
/* ------------------------------------------------------------------ */

/*
 * 9.8 kept the unit table and its six accessors here, because this file was their
 * only reader. 9.10 gave them a module (RoboCSSUnit.h/.c, reached through this
 * header's include of it), and this file now *consumes* that vocabulary instead of
 * defining it: `CssUnitFromSlice` turns a dimension's unit into an
 * enumerator exactly as before, and the difference is that it is the one place
 * that does so rather than the first of two.
 *
 * What changed for this layer, and it is one thing: `ComputedComputeLength` no
 * longer opens a token and decides what it is. It asks `CssNumericFromValue`
 * — the module's parse door — and translates the answer. The five outcomes are not
 * five new branches here; four of them are "this is not a length, and the reason
 * is the diagnostic".
 */

/* ------------------------------------------------------------------ */
/* Reading a component value                                          */
/* ------------------------------------------------------------------ */

/*
 * Four readers used to be defined here — trivia, "the one significant
 * component", "is this function named X", and "a token's decoded keyword text".
 * Phase 9.9's colour subsystem needs all four (an `rgb()` call's arguments are
 * component values like any other), so they moved to RoboCSSInternal.h rather
 * than being written twice. What is left here is the one wrapper the length and
 * keyword normalisers want.
 */

/** The token of a single non-trivia component, or NULL. */
static const CssToken *ComputedSingleToken(const CssValue *value)
{
    const CssValue *single = CssValueSingle(value);

    if (!single || single->kind != RoboCssValueKindToken) {
        return NULL;
    }
    if (single->token.type == RoboCssTokenWhitespace ||
        single->token.type == RoboCssTokenComment) {
        return NULL;
    }
    return &single->token;
}

/**
 * Non-zero when @p value is a function this build recognises but cannot compute
 * — `calc()` and the maths family, `var()` and the environment family.
 *
 * They are *recognised* rather than invalid, and that is the point: `calc(100%
 * - 20px)` is a perfectly good computed value that only a layout pass can
 * reduce, and calling it invalid would delete a declaration the author wrote.
 * Carried PRESERVED with the unresolved flag set, it survives to the phase that
 * has the context, which is the correct behaviour for every one of these.
 */
static int ComputedFunctionIsUnresolved(const CssValue *value)
{
    static const char *const names[] = { "calc", "min", "max", "clamp", "var", "env" };
    size_t index;

    if (!value || value->kind != RoboCssValueKindFunction) {
        return 0;
    }
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        if (CssBytesEqualAsciiCi(value->name.bytes, value->name.length,
                                          names[index])) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

void CssComputedValueInit(CssComputedValue *value)
{
    if (!value) {
        return;
    }
    memset(value, 0, sizeof(*value));
    value->kind = RoboCssComputedValueNone;
    value->unit = RoboCssUnitNone;
    /*
     * The opacity's initial value, written explicitly because zeroing the struct does not
     * produce it. Every other property's initial value happens to *be* zero -- `visible` is
     * 0, `static` is 0, an absent colour is a zeroed record -- so a cleared record holds the
     * right thing by accident of the enumerator order. Opacity's initial value is **one**, so
     * an accident would hold "fully transparent", and this is the one place where the
     * difference between those two answers is a blank page (RoboCSSOpacity.h).
     */
    value->opacity = CssOpacityInitial();
    /*
     * The three flexbox enumerators are written explicitly for the same reason the
     * opacity above is, and the answer happens to be the same: each vocabulary's zero
     * *is* its property's initial value (`row`, `nowrap`, `normal`), so a cleared record
     * is correct by accident of the enumerator order. Writing them down costs three
     * lines and removes the accident -- a later row inserted ahead of one of them would
     * otherwise move the default silently, and the failure would be a container that
     * laid its items out as a row or a wrap when nothing said so.
     */
    value->flexDirection = RoboCssFlexDirectionRow;
    value->flexWrap = RoboCssFlexWrapNoWrap;
    value->flexAlign = RoboCssFlexAlignNormal;
    /* The grid flow's initial is `row`, and written for the reason the three above are:
     * each vocabulary's zero happens to be its property's initial, so a cleared record
     * is right by accident of the enumerator order, and removing the accident costs a
     * line. A grid container that laid its auto-items out by column because a row was
     * inserted ahead of the default is the failure this prevents (RoboCSSGrid.h). */
    value->gridAutoFlow = RoboCssGridAutoFlowRow;
    CssValueInit(&value->preserved);
}

void CssComputedValueDispose(CssComputedValue *value)
{
    if (!value) {
        return;
    }
    RoboFree(value->text);
    CssValueDispose(&value->preserved);
    CssComputedValueInit(value);
}

Result CssComputedValueMove(CssComputedValue *value,
                                          CssComputedValue *outValue,
                                          CssDiag *diag)
{
    if (!value || !outValue || value == outValue) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "a computed value cannot move into itself");
    }
    CssComputedValueDispose(outValue);
    *outValue = *value;
    CssComputedValueInit(value);
    return RoboOk;
}

Result CssComputedValueCopy(CssComputedValue *outValue,
                                          const CssComputedValue *value,
                                          CssDiag *diag)
{
    Result result;

    if (!outValue || !value || outValue == value) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "a computed value cannot copy into itself");
    }
    CssComputedValueDispose(outValue);
    *outValue = *value;
    outValue->text = NULL;
    outValue->textLength = 0u;
    CssValueInit(&outValue->preserved);

    if (value->text) {
        outValue->text = (char *)Malloc(value->textLength + 1u);
        if (!outValue->text) {
            CssComputedValueInit(outValue);
            return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                                 "could not copy the computed value's text");
        }
        memcpy(outValue->text, value->text, value->textLength);
        outValue->text[value->textLength] = '\0';
        /* The length goes back only once the bytes it describes exist: a value
         * whose text pointer is set but whose length is still zero reads as
         * empty, which is how a copied keyword (`red`) would arrive at a child
         * as no keyword at all. */
        outValue->textLength = value->textLength;
    }
    if (value->preserved.kind != RoboCssValueKindNone) {
        result = CssValueCopy(&outValue->preserved, &value->preserved, diag);
        if (result != RoboOk) {
            RoboFree(outValue->text);
            CssComputedValueInit(outValue);
            return result;
        }
    }
    return RoboOk;
}

int CssComputedValueIsEmpty(const CssComputedValue *value)
{
    return !value || value->kind == RoboCssComputedValueNone;
}

/* ------------------------------------------------------------------ */
/* Kinds                                                              */
/* ------------------------------------------------------------------ */

int CssComputedValueKindIsKnown(CssComputedValueKind kind)
{
    return kind >= RoboCssComputedValueNone &&
           kind < (CssComputedValueKind)RoboCssComputedValueKindCount;
}

const char *CssComputedValueKindName(CssComputedValueKind kind)
{
    switch (kind) {
    case RoboCssComputedValueNone:
        return "none";
    case RoboCssComputedValueKeyword:
        return "keyword";
    case RoboCssComputedValueNumber:
        return "number";
    case RoboCssComputedValuePercentage:
        return "percentage";
    case RoboCssComputedValueLength:
        return "length";
    case RoboCssComputedValueColor:
        return "colour";
    case RoboCssComputedValueFamilyList:
        return "family-list";
    case RoboCssComputedValuePreserved:
        return "preserved";
    case RoboCssComputedValueInvalid:
        return "invalid";
    case RoboCssComputedValueDisplay:
        return "display";
    case RoboCssComputedValuePosition:
        return "position";
    case RoboCssComputedValueBackgroundPosition:
        return "background-position";
    case RoboCssComputedValueBackgroundSize:
        return "background-size";
    case RoboCssComputedValueBorderStyle:
        return "border-style";
    case RoboCssComputedValueOverflow:
        return "overflow";
    case RoboCssComputedValueOpacity:
        return "opacity";
    case RoboCssComputedValueFlexDirection:
        return "flex-direction";
    case RoboCssComputedValueFlexWrap:
        return "flex-wrap";
    case RoboCssComputedValueFlexAlign:
        return "flex-align";
    case RoboCssComputedValueGridAutoFlow:
        return "grid-auto-flow";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Construction                                                       */
/* ------------------------------------------------------------------ */

/** Own a copy of @p bytes in @p value->text, or fail without changing it. */
static int ComputedAdoptText(CssComputedValue *value,
                               const uint8_t *bytes,
                               size_t length)
{
    char *copy = (char *)Malloc(length + 1u);

    if (!copy) {
        return 0;
    }
    if (length > 0u) {
        memcpy(copy, bytes, length);
    }
    copy[length] = '\0';
    RoboFree(value->text);
    value->text = copy;
    value->textLength = length;
    return 1;
}

Result CssComputedValueMakeKeyword(CssComputedValue *value,
                                                  CssSlice keyword,
                                                  CssDiag *diag)
{
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "value is NULL");
    }
    CssComputedValueDispose(value);
    if (!ComputedAdoptText(value, keyword.bytes, keyword.length)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                             "could not copy the keyword");
    }
    value->kind = RoboCssComputedValueKeyword;
    return RoboOk;
}

Result CssComputedValueMakeNumber(CssComputedValue *value,
                                                 double number,
                                                 int isInteger,
                                                 CssDiag *diag)
{
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "value is NULL");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueNumber;
    value->number = number;
    value->isInteger = isInteger ? 1 : 0;
    return RoboOk;
}

Result CssComputedValueMakePercentage(CssComputedValue *value,
                                                     double percentage,
                                                     int isInteger,
                                                     CssDiag *diag)
{
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "value is NULL");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValuePercentage;
    value->number = percentage;
    value->isInteger = isInteger ? 1 : 0;
    return RoboOk;
}

Result CssComputedValueMakeLength(CssComputedValue *value,
                                                 double number,
                                                 CssUnit unit,
                                                 int isInteger,
                                                 CssDiag *diag)
{
    if (!value || !CssUnitIsKnown(unit) || unit == RoboCssUnitNone) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "a length needs a known, non-empty unit");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueLength;
    value->number = number;
    value->unit = unit;
    value->isInteger = isInteger ? 1 : 0;
    return RoboOk;
}

Result CssComputedValueMakeColor(CssComputedValue *value,
                                                CssColor color,
                                                CssDiag *diag)
{
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "value is NULL");
    }
    if (!CssColorIsValid(color)) {
        /* A COLOR value that is not a colour (NONE or INVALID) is a bug in the
         * caller, not a value: the honest way to say "there is no colour here"
         * is a computed value that is not a COLOR at all. `currentColor` is
         * accepted, because it *is* a colour and its whole point is that it is
         * not resolved yet. */
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "a colour value needs a colour, or currentColor");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueColor;
    value->color = color;
    return RoboOk;
}

Result CssComputedValueMakeFamilyList(CssComputedValue *value,
                                                       CssSlice families,
                                                       CssDiag *diag)
{
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "value is NULL");
    }
    CssComputedValueDispose(value);
    if (!ComputedAdoptText(value, families.bytes, families.length)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                             "could not copy the family list");
    }
    value->kind = RoboCssComputedValueFamilyList;
    return RoboOk;
}

Result CssComputedValueMakeDisplay(CssComputedValue *value,
                                                   CssDisplay display,
                                                   CssDiag *diag)
{
    if (!value || !CssDisplayIsKnown(display)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or not a display value");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueDisplay;
    value->display = display;
    return RoboOk;
}

Result CssComputedValueMakeOverflow(CssComputedValue *value,
                                                    CssOverflow overflow,
                                                    CssDiag *diag)
{
    if (!value || !CssOverflowIsKnown(overflow)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or not an overflow value");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueOverflow;
    value->overflow = overflow;
    return RoboOk;
}

Result CssComputedValueMakeOpacity(CssComputedValue *value,
                                                   double opacity,
                                                   CssDiag *diag)
{
    if (!value || !CssOpacityIsValid(opacity)) {
        /* Out of range is refused rather than clamped -- see the header: the clamp is the
         * *compute* layer's rule for a declaration an author wrote, and a caller that
         * reached this door with `2` skipped it. */
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or not an opacity value");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueOpacity;
    value->opacity = opacity;
    return RoboOk;
}

Result CssComputedValueMakeFlexDirection(CssComputedValue *value,
                                                       CssFlexDirection direction,
                                                       CssDiag *diag)
{
    if (!value || !CssFlexDirectionIsKnown(direction)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or not a flex-direction value");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueFlexDirection;
    value->flexDirection = direction;
    return RoboOk;
}

Result CssComputedValueMakeFlexWrap(CssComputedValue *value,
                                                  CssFlexWrap wrap,
                                                  CssDiag *diag)
{
    if (!value || !CssFlexWrapIsKnown(wrap)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or not a flex-wrap value");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueFlexWrap;
    value->flexWrap = wrap;
    return RoboOk;
}

Result CssComputedValueMakeFlexAlign(CssComputedValue *value,
                                                   CssFlexAlign align,
                                                   CssDiag *diag)
{
    if (!value || !CssFlexAlignIsKnown(align)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or not an alignment value");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueFlexAlign;
    value->flexAlign = align;
    return RoboOk;
}

Result CssComputedValueMakeGridAutoFlow(CssComputedValue *value,
                                                     CssGridAutoFlow flow,
                                                     CssDiag *diag)
{
    if (!value || !CssGridAutoFlowIsKnown(flow)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or not a grid-auto-flow value");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueGridAutoFlow;
    value->gridAutoFlow = flow;
    return RoboOk;
}

Result CssComputedValueMakeBorderStyle(CssComputedValue *value,
                                                        CssBorderStyle style,
                                                        CssDiag *diag)
{
    if (!value || !CssBorderStyleIsKnown(style)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or not a border style");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueBorderStyle;
    value->borderStyle = style;
    return RoboOk;
}

Result CssComputedValueMakePosition(CssComputedValue *value,
                                                    CssPositioning position,
                                                    CssDiag *diag)
{
    if (!value || !CssPositioningIsKnown(position)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or not a position value");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValuePosition;
    value->position = position;
    return RoboOk;
}

Result CssComputedValueMakeBackgroundPosition(
    CssComputedValue *value,
    CssBackgroundPosition position,
    CssDiag *diag)
{
    /* Both components, because a half-filled pair is a state the parsing layer
     * holds and not one a *computed value* may: a consumer reading the pair would
     * otherwise have to know which half the parser had defaulted. */
    if (!value || !CssBackgroundPositionIsPresent(position)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or the position is incomplete");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueBackgroundPosition;
    value->backgroundPosition = position;
    return RoboOk;
}

Result CssComputedValueMakeBackgroundSize(
    CssComputedValue *value,
    CssBackgroundSize size,
    CssDiag *diag)
{
    if (!value || !CssBackgroundSizeIsPresent(size)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output is NULL or not a background size");
    }
    CssComputedValueDispose(value);
    value->kind = RoboCssComputedValueBackgroundSize;
    value->backgroundSize = size;
    return RoboOk;
}

Result CssComputedValueMakePreserved(CssComputedValue *value,
                                                     const CssValue *specified,
                                                     CssDiag *diag)
{
    if (!value || !specified) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "value or specified value is NULL");
    }
    CssComputedValueDispose(value);
    if (specified->kind == RoboCssValueKindNone) {
        /* Nothing to preserve. NONE is the honest answer: "there was no value"
         * is not "here is a value that means nothing". */
        return RoboOk;
    }
    value->kind = RoboCssComputedValuePreserved;
    {
        Result result = CssValueCopy(&value->preserved, specified, diag);

        if (result != RoboOk) {
            CssComputedValueInit(value);
            return result;
        }
    }
    return RoboOk;
}

Result CssComputedValueMakeInvalid(CssComputedValue *value,
                                                   const char *reason,
                                                   CssDiag *diag)
{
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "value is NULL");
    }
    CssComputedValueDispose(value);
    if (!ComputedAdoptText(value, (const uint8_t *)(reason ? reason : ""),
                             reason ? strlen(reason) : 0u)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                             "could not copy the reason");
    }
    value->kind = RoboCssComputedValueInvalid;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Reading                                                            */
/* ------------------------------------------------------------------ */

CssSlice CssComputedValueText(const CssComputedValue *value)
{
    if (!value || !value->text) {
        return CssSliceEmpty();
    }
    return CssSliceFromBytes((const uint8_t *)value->text, value->textLength);
}

const CssValue *CssComputedValuePreserved(
    const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValuePreserved) {
        return NULL;
    }
    return &value->preserved;
}

CssColor CssComputedValueColor(const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueColor) {
        return CssColorMake(0u, 0u, 0u, 0u);
    }
    return value->color;
}

CssDisplay CssComputedValueDisplay(const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueDisplay) {
        return RoboCssDisplayNone;
    }
    return value->display;
}

CssPositioning CssComputedValuePosition(const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValuePosition) {
        return RoboCssPositioningStatic;
    }
    return value->position;
}

CssOverflow CssComputedValueOverflow(const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueOverflow) {
        return RoboCssOverflowVisible;
    }
    return value->overflow;
}

CssBorderStyle CssComputedValueBorderStyle(
    const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueBorderStyle) {
        return RoboCssBorderStyleNone;
    }
    return value->borderStyle;
}

double CssComputedValueOpacity(const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueOpacity) {
        /* The initial value, which is the direction that matters: "not an opacity" must
         * render the way "no opacity declared" does, and that way is *visible*. */
        return CssOpacityInitial();
    }
    return value->opacity;
}

CssFlexDirection CssComputedValueFlexDirection(
    const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueFlexDirection) {
        return RoboCssFlexDirectionRow;
    }
    return value->flexDirection;
}

CssFlexWrap CssComputedValueFlexWrap(const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueFlexWrap) {
        return RoboCssFlexWrapNoWrap;
    }
    return value->flexWrap;
}

CssFlexAlign CssComputedValueFlexAlign(const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueFlexAlign) {
        return RoboCssFlexAlignNormal;
    }
    return value->flexAlign;
}

CssGridAutoFlow CssComputedValueGridAutoFlow(const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueGridAutoFlow) {
        return RoboCssGridAutoFlowRow;
    }
    return value->gridAutoFlow;
}

CssBackgroundPosition CssComputedValueBackgroundPosition(
    const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueBackgroundPosition) {
        return CssBackgroundPositionNone();
    }
    return value->backgroundPosition;
}

CssBackgroundSize CssComputedValueBackgroundSize(
    const CssComputedValue *value)
{
    if (!value || value->kind != RoboCssComputedValueBackgroundSize) {
        return CssBackgroundSizeNone();
    }
    return value->backgroundSize;
}

/* ------------------------------------------------------------------ */
/* Equality                                                           */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when two tokens' *values* are the same bytes.
 *
 * The raw text is the fast path and the correct one whenever neither token
 * contains an escape, which is the overwhelming majority: an escape always
 * changes the value's length, so equal text length and value length proves
 * there is none. When there is one, both values are decoded and compared,
 * because `\72 ed` and `red` are the same token and a comparison that said
 * otherwise would make two identical styles unequal.
 */
static int ComputedTokenValueEquals(const CssToken *a,
                                       const CssToken *b)
{
    uint8_t scratchA[128];
    uint8_t scratchB[128];
    uint8_t *bytesA = scratchA;
    uint8_t *bytesB = scratchB;
    size_t lengthA = CssTokenValueLength(a);
    size_t lengthB = CssTokenValueLength(b);
    int equal;

    if (lengthA != lengthB) {
        return 0;
    }
    if (a->text.length == lengthA && b->text.length == lengthB) {
        return CssSliceEquals(a->text, b->text);
    }
    if (lengthA > sizeof(scratchA)) {
        bytesA = (uint8_t *)Malloc(lengthA);
        bytesB = (uint8_t *)Malloc(lengthB);
        if (!bytesA || !bytesB) {
            RoboFree(bytesA == scratchA ? NULL : bytesA);
            RoboFree(bytesB == scratchB ? NULL : bytesB);
            return 0;
        }
    }
    equal = CssTokenValueCopy(a, bytesA, lengthA, &lengthA, NULL) == RoboOk &&
            CssTokenValueCopy(b, bytesB, lengthB, &lengthB, NULL) == RoboOk &&
            lengthA == lengthB &&
            (lengthA == 0u || memcmp(bytesA, bytesB, lengthA) == 0);
    if (bytesA != scratchA) {
        RoboFree(bytesA);
        RoboFree(bytesB);
    }
    return equal;
}

/** Structural equality of two component-value trees. */
static int ComputedTreesEqual(const CssValue *a, const CssValue *b)
{
    size_t index;

    if (a->kind != b->kind) {
        return 0;
    }
    switch (a->kind) {
    case RoboCssValueKindNone:
        return 1;
    case RoboCssValueKindToken:
        /* The resolved value, not the raw text: `\72 ed` and `red` are the same
         * token, and two values that mean the same thing must compare equal or
         * a test cannot say what it expects. */
        return a->token.type == b->token.type &&
               ComputedTokenValueEquals(&a->token, &b->token);
    case RoboCssValueKindFunction:
        if (a->children.count != b->children.count ||
            !CssSliceEqualsCi(a->name, b->name)) {
            return 0;
        }
        break;
    case RoboCssValueKindBlock:
        if (a->children.count != b->children.count || a->open != b->open ||
            a->close != b->close) {
            return 0;
        }
        break;
    default:
        if (a->children.count != b->children.count) {
            return 0;
        }
        break;
    }
    for (index = 0u; index < a->children.count; index++) {
        if (!ComputedTreesEqual(&a->children.items[index], &b->children.items[index])) {
            return 0;
        }
    }
    return 1;
}

int CssComputedValueEquals(const CssComputedValue *a,
                                   const CssComputedValue *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind || a->unresolved != b->unresolved) {
        return 0;
    }
    switch (a->kind) {
    case RoboCssComputedValueNone:
        return 1;
    case RoboCssComputedValueKeyword:
        return CssSliceEquals(CssComputedValueText(a),
                                     CssComputedValueText(b));
    case RoboCssComputedValueFamilyList:
    case RoboCssComputedValueInvalid:
        return CssSliceEquals(CssComputedValueText(a),
                                     CssComputedValueText(b));
    case RoboCssComputedValueNumber:
        return a->isInteger == b->isInteger && a->number == b->number;
    case RoboCssComputedValuePercentage:
        return a->isInteger == b->isInteger && a->number == b->number;
    case RoboCssComputedValueLength:
        return a->unit == b->unit && a->isInteger == b->isInteger &&
               a->number == b->number;
    case RoboCssComputedValueColor:
        return CssColorEquals(a->color, b->color);
    case RoboCssComputedValueDisplay:
        /* Two enumerators, so equality is the enumerator: `inline-block` and
         * `inline` are different displays however similar they read. */
        return a->display == b->display;
    case RoboCssComputedValuePosition:
        /* The same, and here it is the difference that matters rather than the
         * similarity: `relative` and `sticky` share both of their classifications
         * and are still two different positions, which is exactly why the
         * comparison is the enumerator and not the columns. */
        return a->position == b->position;
    case RoboCssComputedValueBackgroundPosition:
        /* Component-wise, and kind-aware at the component level: `left top` and
         * `0% 0%` are different positions however equal they look once something
         * resolves them, and the pair's own module is where that is decided. */
        return CssBackgroundPositionEquals(a->backgroundPosition,
                                                   b->backgroundPosition);
    case RoboCssComputedValueBackgroundSize:
        return CssBackgroundSizeEquals(a->backgroundSize, b->backgroundSize);
    case RoboCssComputedValueBorderStyle:
        /* The enumerator, for the same reason a display's and a position's are:
         * `hidden` and `none` are different styles that both draw nothing, and a
         * comparison on "draws nothing" would make them one value. */
        return a->borderStyle == b->borderStyle;
    case RoboCssComputedValueOverflow:
        /* The enumerator, for the same reason the four above are: `visible` and
         * `hidden` are two different answers about one axis, and a comparison on
         * "does this clip?" would make them one value. */
        return a->overflow == b->overflow;
    case RoboCssComputedValueFlexDirection:
        /* The enumerator, for the reason a display's and a position's are:
         * `row-reverse` and `row` share the main axis and are still two different
         * directions, so a comparison on the classification would make them one. */
        return a->flexDirection == b->flexDirection;
    case RoboCssComputedValueFlexWrap:
        return a->flexWrap == b->flexWrap;
    case RoboCssComputedValueFlexAlign:
        return a->flexAlign == b->flexAlign;
    case RoboCssComputedValueGridAutoFlow:
        /* The enumerator, for the reason a display's and a position's are: `row dense`
         * and `row` share an axis and are still two different flows, so a comparison on
         * the axis classification would make them one. */
        return a->gridAutoFlow == b->gridAutoFlow;
    case RoboCssComputedValueOpacity:
        /* The number, exactly -- and the exactness is the point rather than a
         * shortcut. Two opacities that differ by one part in a million are two
         * different values with two different pictures, and a comparison with a
         * tolerance would report a determinism failure as a pass. This is also the
         * one comparison here that a *test* relies on in that direction: the suite
         * that proves `0.5 * 0.5` is `0.25` reads the answer back through this
         * door, so any rounding anywhere between the two would show up as an
         * inequality rather than as a slightly wrong picture. */
        return a->opacity == b->opacity;
    default:
        return ComputedTreesEqual(&a->preserved, &b->preserved);
    }
}

/* ------------------------------------------------------------------ */
/* Rendering                                                          */
/* ------------------------------------------------------------------ */

/** The character a block's opening token type stands for. */
static char ComputedBracketChar(CssTokenType type)
{
    switch (type) {
    case RoboCssTokenLeftSquare:
        return '[';
    case RoboCssTokenLeftCurly:
        return '{';
    default:
        return '(';
    }
}

static char ComputedBracketClose(CssTokenType type)
{
    switch (type) {
    case RoboCssTokenLeftSquare:
        return ']';
    case RoboCssTokenLeftCurly:
        return '}';
    default:
        return ')';
    }
}

/**
 * Write @p value's *number* into the buffer, trimming meaningless zeroes.
 *
 * Integers render without a decimal point because a value written as `10` is a
 * different value from `10.0` in CSS only where the property says so, and the
 * `isInteger` flag already records that. A non-integer renders with at most six
 * decimals — enough that every length and colour channel this build produces
 * round-trips, and few enough that `0.1 + 0.2` does not print seventeen digits.
 */
/**
 * Append @p numeric's canonical text to the buffer at @p used.
 *
 * The spelling is the unit module's (RoboCSSUnit.h), and that is the point: a
 * number formatted one way here and another way there is the same class of defect
 * as two colour parsers, and `10px`, `10%` and `10` have to agree about how ten is
 * written. The module owns that rule; this layer owns deciding *which* numeric a
 * computed value is.
 */
static int ComputedAppendNumeric(const CssNumeric *numeric,
                                   char *buffer, size_t capacity, size_t *used)
{
    char text[RoboCssNumericTextMax];
    size_t length = 0u;

    if (CssNumericWrite(*numeric, text, sizeof(text), &length, NULL) != RoboOk) {
        return 0;
    }
    if (capacity <= *used + length) {
        return 0;
    }
    memcpy(buffer + *used, text, length);
    *used += length;
    buffer[*used] = '\0';
    return 1;
}

/**
 * Write the numeric a NUMBER, PERCENTAGE or LENGTH computed value carries.
 *
 * The computed value's own `isInteger` is deliberately *not* handed over: the
 * module formats by value, so `10`, `10.0` and `1e1` all write as `10` and the
 * field stays what it has always been — the tokenizer's record that the author
 * wrote a whole number, which is the `<integer>` question rather than a formatting
 * hint.
 */
static int ComputedWriteNumericKind(const CssComputedValue *value,
                                       char *buffer, size_t capacity, size_t *used)
{
    CssNumeric numeric = CssNumericNone();
    Result rc;

    switch (value->kind) {
    case RoboCssComputedValuePercentage:
        rc = CssNumericMakePercentage(&numeric, value->number, NULL);
        break;
    case RoboCssComputedValueLength:
        rc = CssNumericMakeLength(&numeric, value->number, value->unit, NULL);
        break;
    default:
        rc = CssNumericMakeNumber(&numeric, value->number, value->isInteger,
                                          NULL);
        break;
    }
    if (rc != RoboOk) {
        return 0;
    }
    return ComputedAppendNumeric(&numeric, buffer, capacity, used);
}

static int ComputedWriteInto(const CssValue *value, char *buffer,
                               size_t capacity, size_t *used);

static int ComputedWriteChildren(const CssValue *value, char *buffer,
                                   size_t capacity, size_t *used)
{
    size_t index;

    for (index = 0u; index < value->children.count; index++) {
        if (!ComputedWriteInto(&value->children.items[index], buffer, capacity,
                                 used)) {
            return 0;
        }
    }
    return 1;
}

static int ComputedAppendChar(char *buffer, size_t capacity, size_t *used, char byte)
{
    if (capacity <= *used + 1u) {
        return 0;
    }
    buffer[*used] = byte;
    (*used)++;
    buffer[*used] = '\0';
    return 1;
}

static int ComputedWriteTokenText(const CssValue *value, char *buffer,
                                     size_t capacity, size_t *used);

static int ComputedWriteInto(const CssValue *value, char *buffer,
                               size_t capacity, size_t *used)
{
    switch (value->kind) {
    case RoboCssValueKindToken:
        /* The raw text: this rendering is a diagnostic, and for a preserved
         * tree the source spelling is what a developer wants to see. */
        return ComputedWriteTokenText(value, buffer, capacity, used);
    case RoboCssValueKindFunction:
        if (value->name.length > capacity - *used - 1u) {
            return 0;
        }
        if (value->name.length > 0u) {
            memcpy(buffer + *used, value->name.bytes, value->name.length);
            *used += value->name.length;
            buffer[*used] = '\0';
        }
        return ComputedAppendChar(buffer, capacity, used, '(') &&
               ComputedWriteChildren(value, buffer, capacity, used) &&
               ComputedAppendChar(buffer, capacity, used, ')');
    case RoboCssValueKindBlock:
        return ComputedAppendChar(buffer, capacity, used,
                                    ComputedBracketChar(value->open)) &&
               ComputedWriteChildren(value, buffer, capacity, used) &&
               ComputedAppendChar(buffer, capacity, used,
                                    ComputedBracketClose(value->open));
    case RoboCssValueKindList:
        return ComputedWriteChildren(value, buffer, capacity, used);
    default:
        return 1;
    }
}

/**
 * One token's raw text, appended to the buffer.
 *
 * The raw text rather than the decoded value, because this rendering exists so
 * that a developer can see the tokens a PRESERVED value carries and the source
 * spelling is exactly what they wrote.
 */
static int ComputedWriteTokenText(const CssValue *value, char *buffer,
                                     size_t capacity, size_t *used)
{
    if (value->token.text.length > capacity - *used - 1u) {
        return 0;
    }
    if (value->token.text.length > 0u) {
        memcpy(buffer + *used, value->token.text.bytes, value->token.text.length);
        *used += value->token.text.length;
        buffer[*used] = '\0';
    }
    return 1;
}

Result CssComputedValueWrite(const CssComputedValue *value,
                                            char *buffer,
                                            size_t capacity,
                                            size_t *outLength,
                                            CssDiag *diag)
{
    size_t used = 0u;
    int ok = 1;

    if (!value || !buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "value, buffer and out_length are required");
    }
    buffer[0] = '\0';

    switch (value->kind) {
    case RoboCssComputedValueNone:
        break;
    case RoboCssComputedValueKeyword:
    case RoboCssComputedValueFamilyList:
    case RoboCssComputedValueInvalid:
        if (value->textLength >= capacity) {
            *outLength = value->textLength;
            return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotSupported,
                                 "the computed value's text does not fit");
        }
        if (value->textLength > 0u) {
            memcpy(buffer, value->text, value->textLength);
        }
        buffer[value->textLength] = '\0';
        used = value->textLength;
        break;
    case RoboCssComputedValueNumber:
    case RoboCssComputedValuePercentage:
    case RoboCssComputedValueLength:
        /* One writer for the three numeric kinds, because the difference between
         * them is a suffix the module already knows how to add. */
        ok = ComputedWriteNumericKind(value, buffer, capacity, &used);
        break;
    case RoboCssComputedValueColor:
        /* One spelling per colour, and it is the colour subsystem's: this layer
         * formats lengths, numbers and token trees, and a colour is none of
         * those. `currentColor` therefore prints as `currentColor` because that
         * is what the colour module says it prints as. */
        ok = CssColorWrite(value->color, buffer, capacity, &used, NULL) == RoboOk;
        break;
    case RoboCssComputedValueDisplay:
        /* The keyword is the display module's, for the same reason a colour's
         * spelling is the colour module's: this layer formats lengths, numbers
         * and token trees, and a display value is none of those. `inline-block`
         * prints with its hyphen because that is what CSS writes. */
        ok = CssDisplayWrite(value->display, buffer, capacity, &used, NULL) ==
             RoboOk;
        break;
    case RoboCssComputedValuePosition:
        /* And the same for a position value, whose keyword is the positioning
         * module's: `static`, `relative`, `absolute`, `fixed`, `sticky`, spelled
         * as CSS spells them. */
        ok = CssPositioningWrite(value->position, buffer, capacity, &used, NULL) ==
             RoboOk;
        break;
    case RoboCssComputedValueFlexDirection:
        /* The flex module's spelling, for the reason a display's, a position's and an
         * overflow's are the other modules': `row-reverse` prints with its hyphen
         * because that is what CSS writes, and this layer formats lengths, numbers and
         * token trees rather than property vocabularies. */
        ok = CssFlexDirectionWrite(value->flexDirection, buffer, capacity, &used,
                                            NULL) == RoboOk;
        break;
    case RoboCssComputedValueFlexWrap:
        ok = CssFlexWrapWrite(value->flexWrap, buffer, capacity, &used,
                                       NULL) == RoboOk;
        break;
    case RoboCssComputedValueFlexAlign:
        ok = CssFlexAlignWrite(value->flexAlign, buffer, capacity, &used,
                                        NULL) == RoboOk;
        break;
    case RoboCssComputedValueGridAutoFlow:
        /* The grid module's spelling, for the reason a display's and a flex direction's
         * are the other modules': `row dense` prints with its space because that is what
         * CSS writes, and this layer formats lengths, numbers and token trees rather
         * than property vocabularies. */
        ok = CssGridAutoFlowWrite(value->gridAutoFlow, buffer, capacity, &used,
                                            NULL) == RoboOk;
        break;
    case RoboCssComputedValueBackgroundPosition:
        /* The background module's spelling, and it is the *pair's* rather than the
         * declaration's: `center` prints `center center`, so two equal positions
         * cannot print differently because one was written in the short form. */
        ok = CssBackgroundPositionWrite(value->backgroundPosition, buffer,
                                                capacity, &used, NULL) == RoboOk;
        break;
    case RoboCssComputedValueBackgroundSize:
        /* The same, and here the folding is visible in the other direction: a pair
         * of two `auto`s is the value `auto`, and prints as it. */
        ok = CssBackgroundSizeWrite(value->backgroundSize, buffer, capacity,
                                            &used, NULL) == RoboOk;
        break;
    case RoboCssComputedValueOverflow:
        /* The overflow module's spelling, for the reason a display's, a position's
         * and a border style's are the other modules': `visible` and `hidden` are
         * spelled the way CSS spells them, and this layer formats lengths, numbers
         * and token trees rather than property vocabularies. */
        ok = CssOverflowWrite(value->overflow, buffer, capacity, &used, NULL) ==
             RoboOk;
        break;
    case RoboCssComputedValueBorderStyle:
        /* The border module's spelling, so a style this build adds later prints
         * its own keyword rather than falling to a default here. */
        ok = CssBorderStyleWrite(value->borderStyle, buffer, capacity, &used,
                                        NULL) == RoboOk;
        break;
    case RoboCssComputedValueOpacity:
        /* The opacity module's spelling, for the reason a display's, a position's, a
         * border style's and an overflow's are the other modules': this layer formats
         * lengths, numbers and token trees, and an opacity is one of those three
         * *shapes* but not one of its rules -- its range, its percentage conversion and
         * its formatter are the property's own. */
        ok = CssOpacityWrite(value->opacity, buffer, capacity, &used, NULL) ==
             RoboOk;
        break;
    case RoboCssComputedValuePreserved:
        if (value->preserved.kind == RoboCssValueKindToken) {
            ok = ComputedWriteTokenText(&value->preserved, buffer, capacity, &used);
        } else {
            ok = ComputedWriteInto(&value->preserved, buffer, capacity, &used);
        }
        break;
    default:
        ok = 0;
        break;
    }

    if (!ok) {
        buffer[0] = '\0';
        *outLength = used;
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotSupported,
                             "the computed value's text does not fit");
    }
    *outLength = used;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Computing                                                          */
/* ------------------------------------------------------------------ */

/** Make @p out INVALID and record why, always succeeding at the report. */
static Result ComputedReject(CssComputedValue *out,
                                     CssPropertyComputation computation,
                                     const char *reason,
                                     CssDiag *diag)
{
    char message[128];

    (void)snprintf(message, sizeof(message), "%s: %s",
                   CssPropertyComputationName(computation), reason);
    RoboCssDiag(diag, RoboCssDomainComputed, RoboNotSupported, message);
    return CssComputedValueMakeInvalid(out, reason, NULL);
}

/** Preserve @p specified, marking it unresolved for @p out. */
static Result ComputedKeep(CssComputedValue *out,
                                   const CssValue *specified,
                                   int unresolved,
                                   CssDiag *diag)
{
    Result result = CssComputedValueMakePreserved(out, specified, diag);

    if (result == RoboOk) {
        out->unresolved = unresolved ? 1 : 0;
    }
    return result;
}

/**
 * The reason an INVALID colour carries, naming the function it was written as.
 *
 * `rgb()` and `hsl()` fail for the same set of reasons — a channel out of range,
 * the wrong separator, the wrong number of arguments — and an author who wrote
 * `hsl(0, 100%)` is better served by a message that says which call was wrong.
 * The name is quoted from the value itself rather than reconstructed from the
 * property, so a message cannot disagree with the declaration it describes.
 */
static void ComputedColorReason(const CssValue *specified, char *buffer,
                                  size_t capacity)
{
    const CssValue *single = CssValueSingle(specified);
    size_t length;

    if (!single || single->kind != RoboCssValueKindFunction ||
        CssSliceIsAbsent(single->name) || single->name.length == 0u) {
        (void)snprintf(buffer, capacity, "a colour function written wrong");
        return;
    }
    length = single->name.length;
    if (length > 32u) {
        length = 32u;
    }
    (void)snprintf(buffer, capacity, "%.*s() arguments are out of range or malformed",
                   (int)length, (const char *)single->name.bytes);
}

/**
 * Compute a colour, by handing it to the colour subsystem.
 *
 * The whole of this layer's colour knowledge, and it is deliberately almost
 * nothing: `CssColorFromValue` (RoboCSSColor.h) owns every spelling — the
 * named set, `transparent`, all four hex lengths, `rgb()`/`rgba()` and
 * `hsl()`/`hsla()` — and this function's only job is to turn its answer into the
 * right computed value. There is one colour parser in this subsystem and it is
 * that one; a second copy here is how two spellings of `rgb()` come to disagree.
 *
 * The five outcomes, and why each maps where it does:
 *
 *   RESOLVED    the channels, as a COLOR value
 *   CURRENT     a COLOR value whose colour is the CURRENT kind, flagged
 *               unresolved. It is *typed* rather than carried as tokens, and it
 *               stays unresolved because the only context that can resolve it —
 *               the element's own `color`, and its parent's — is one the computed
 *               style has and this function does not (see RoboCSSComputed.c).
 *   DEFERRED    a colour function this build does not represent, carried with
 *               its tokens and flagged unresolved. The author wrote legal CSS;
 *               refusing it would be a lie, and approximating it would be worse.
 *   MALFORMED   a colour function written wrong, reported INVALID with *why*.
 *   INVALID     not a colour at all.
 *
 * One case is settled before any of that, because the colour parser is the wrong
 * authority for it: a **reference** — `var()`, `env()`, `calc()` — is not a
 * colour *yet*, so `color: var(--brand)` is carried with its tokens and flagged
 * unresolved rather than reported invalid. The colour module would call it
 * non-colour syntax, and it would be right about the syntax and wrong about the
 * author, who wrote a legal reference to a colour. Custom properties are still
 * never *interpreted* here (see RoboCSSValue.h): nothing substitutes `--brand`,
 * and nothing reads it as a colour until a later phase resolves the reference.
 */
static Result ComputedComputeColor(CssComputedValue *out,
                                            const CssValue *specified,
                                            CssDiag *diag)
{
    CssColor color = CssColorNone();
    CssColorParse outcome = CssColorFromValue(specified, &color);

    if (outcome == RoboCssColorParseInvalid) {
        const CssValue *single = CssValueSingle(specified);

        if (single && single->kind == RoboCssValueKindFunction &&
            ComputedFunctionIsUnresolved(single)) {
            return ComputedKeep(out, specified, 1, diag);
        }
    }

    switch (outcome) {
    case RoboCssColorParseResolved:
    case RoboCssColorParseCurrent:
        if (outcome == RoboCssColorParseCurrent) {
            /* The one thing this layer can say about `currentColor`: the value
             * is a colour, and it is not resolved. */
            Result rc = CssComputedValueMakeColor(out, color, diag);

            if (rc == RoboOk) {
                out->unresolved = 1;
            }
            return rc;
        }
        return CssComputedValueMakeColor(out, color, diag);
    case RoboCssColorParseDeferred:
        /* Recognised and deliberately not computed: the colour subsystem owns
         * these spaces, and a wrong colour is worse than a deferred one. */
        return ComputedKeep(out, specified, 1, diag);
    case RoboCssColorParseMalformed: {
        char reason[128];

        ComputedColorReason(specified, reason, sizeof(reason));
        return ComputedReject(out, RoboCssPropertyComputationColor, reason, diag);
    }
    case RoboCssColorParseUnknownName:
        return ComputedReject(out, RoboCssPropertyComputationColor,
                               "unknown colour name", diag);
    default:
        return ComputedReject(out, RoboCssPropertyComputationColor,
                               "not a colour", diag);
    }
}

/*
 * The identifiers each length family may be written with.
 *
 * Four lists, and the reason there are four rather than one union is that this
 * is the line where a value *shape* meets a property's *grammar*. `medium` is a
 * `font-size`; it is also a `border-width` and means a different thing there.
 * `auto` is a width, a height and a margin, and is neither a padding nor a
 * border. A single union would make every one of those computable everywhere,
 * which is exactly how `padding: auto` comes to look like a value.
 *
 * The first list is unchanged from 9.8 and is a union on purpose: the properties
 * it serves (`width`, `height`, `font-size`, `letter-spacing`, `text-indent`,
 * `word-spacing`) genuinely share it, and no box-model family needed from it is
 * narrower than the whole. The three 9.11 lists are exact, because there the
 * difference *is* the property family.
 *
 * What none of them will do is call every identifier a length. `width: red` and
 * `margin-top: min-content` are mistakes an author wants told about, and carrying
 * one as a computed keyword would hand a later phase a value it can only fail on.
 */
static const char *const computedLengthKeywords[] = {
    "auto",     "none",       "normal",   "medium",   "min-content",
    "max-content", "fit-content", "smaller", "larger",   "xx-small",
    "x-small",  "small",      "large",    "x-large",  "xx-large",
    "xxx-large"
};

/**
 * A length, a percentage, or `auto` — an edge whose own size decides, which is
 * what `margin-*` and the four inset properties `top`/`right`/`bottom`/`left`
 * have in common.
 *
 * The sentence below names neither family, and that is deliberate rather than
 * vague: the same list serves both, so a sentence naming one would be *wrong* for
 * the other — `top: normal` is not a margin, and telling its author about margins
 * would send them looking in the wrong place. What the diagnostic does say is
 * which property it was, because `ComputedReject` prefixes the shape's name.
 */
static const char *const computedBoxOffsetKeywords[] = { "auto" };

/** `border-*-width`: a length, or one of the three width keywords. */
static const char *const computedBoxLineWidthKeywords[] = {
    "thin", "medium", "thick"
};

/*
 * `flex-basis`: `<length-percentage>`, or one of the four keywords its grammar
 * allows.
 *
 * `content` is the one this list has and no other does: it means "the item's own
 * content size", which is not a length and not `auto`, and reading it as a keyword is
 * the whole of what this build can honestly say about it -- the size it stands for is
 * a layout question. `min-content` and `max-content` are the intrinsic sizes and are
 * likewise carried rather than resolved; `auto` is the property's initial value and
 * means "the item's width/height", which is not zero and not a length.
 */
static const char *const computedBoxFlexBasisKeywords[] = {
    "auto", "content", "min-content", "max-content"
};

/**
 * A grid gutter's one keyword (18.4): `normal`, or a length or a percentage.
 *
 * `normal` is not zero — for a grid container it is the user agent's ordinary gutter,
 * which is a different statement from a gutter of no pixels — so it is carried as the
 * keyword it is rather than resolved to a length, exactly as `medium` is for a border
 * width. A gutter this build had turned into `0px` would make every grid container an
 * author wrote lay out with no gaps at all.
 */
static const char *const computedBoxGridGapKeywords[] = { "normal" };

/**
 * `column-width`'s one keyword (18.6): `auto`, or a length.
 *
 * `auto` means "let the count decide", which is not zero and not a length, so it is
 * carried as the keyword it is — exactly as `normal` is for a grid gutter.
 */
static const char *const computedColumnWidthKeywords[] = { "auto" };

/**
 * One length family: which shape it is, what it may be written as, and what to
 * say when an identifier is not in that vocabulary.
 *
 * The reason is carried *here* rather than composed at the reject site because
 * the vocabulary and the sentence that explains it are the same fact stated twice,
 * and the four families need four different sentences: an author who wrote
 * `margin-top: normal` and one who wrote `padding: auto` are owed different
 * explanations, and both are owed one that names the family rather than the
 * layer.
 */
typedef struct computedLengthGrammar {
    CssPropertyComputation computation;
    const char *const *keywords;
    size_t keywordCount;
    const char *keywordReason;
} ComputedLengthGrammar;

static const ComputedLengthGrammar computedLengthGrammars[] = {
    { RoboCssPropertyComputationLength, computedLengthKeywords,
      sizeof(computedLengthKeywords) / sizeof(computedLengthKeywords[0]),
      "not a length keyword" },
    { RoboCssPropertyComputationEdgeOffset, computedBoxOffsetKeywords,
      sizeof(computedBoxOffsetKeywords) / sizeof(computedBoxOffsetKeywords[0]),
      "not a length, a percentage or auto" },
    /* An empty vocabulary is the value, not an omission: a padding has nothing
     * to distribute, so there is no keyword it accepts and every identifier is a
     * mistake — which is what makes `padding: auto` INVALID rather than a
     * keyword a later phase would have to interpret. */
    { RoboCssPropertyComputationEdgeInset, NULL, 0u,
      "not a padding length" },
    { RoboCssPropertyComputationLineWidth, computedBoxLineWidthKeywords,
      sizeof(computedBoxLineWidthKeywords) /
          sizeof(computedBoxLineWidthKeywords[0]),
      "not a border width keyword" },
    /* The flex basis (18.3) is a length family like the four above rather than a shape
     * of its own: its value *is* a length, a percentage or one of four keywords, so it
     * belongs to the same body -- which is what keeps `flex-basis: 1in` and
     * `width: 1in` from being normalised differently. What is its own is the keyword
     * list, and that is exactly the parameter this table grew for. */
    { RoboCssPropertyComputationFlexBasis, computedBoxFlexBasisKeywords,
      sizeof(computedBoxFlexBasisKeywords) /
          sizeof(computedBoxFlexBasisKeywords[0]),
      "not a length, a percentage, auto or content" },
    /* The grid gutters (18.4) join the same body for the same reason: a gutter's value
     * *is* a length, a percentage or the keyword `normal`, so it is the one length
     * grammar with its own keyword list rather than a shape of its own. */
    { RoboCssPropertyComputationGridGap, computedBoxGridGapKeywords,
      sizeof(computedBoxGridGapKeywords) /
          sizeof(computedBoxGridGapKeywords[0]),
      "not a length, a percentage or normal" },
    /* `column-width` (18.6) joins the length body with the keyword `auto`, for the same
     * reason the grid gutter did: its value *is* a length, and what is its own is the
     * keyword list. */
    { RoboCssPropertyComputationColumnWidth, computedColumnWidthKeywords,
      sizeof(computedColumnWidthKeywords) /
          sizeof(computedColumnWidthKeywords[0]),
      "not a length or auto" }
};

/** The grammar for @p computation, or NULL when it is not a length family. */
static const ComputedLengthGrammar *ComputedLengthGrammarFor(
    CssPropertyComputation computation)
{
    size_t index;

    for (index = 0u;
         index < sizeof(computedLengthGrammars) / sizeof(computedLengthGrammars[0]);
         index++) {
        if (computedLengthGrammars[index].computation == computation) {
            return &computedLengthGrammars[index];
        }
    }
    return NULL;
}

/** Non-zero when @p keyword (already lowercased) is in @p grammar's vocabulary. */
static int ComputedLengthKeywordIsKnown(const ComputedLengthGrammar *grammar,
                                            const char *keyword,
                                            size_t length)
{
    size_t index;

    if (!grammar) {
        return 0;
    }
    for (index = 0u; index < grammar->keywordCount; index++) {
        if (CssBytesEqualAsciiCi((const uint8_t *)keyword, length,
                                          grammar->keywords[index])) {
            return 1;
        }
    }
    return 0;
}

/**
 * Compute a length-family value: dimension, percentage, zero, or keyword.
 *
 * The numeric shapes come from the module (RoboCSSUnit.h) and the keyword shape is
 * this layer's, and the split is the phase boundary: a unit's meaning belongs to
 * the unit vocabulary, and a property's accepted keyword list belongs to the
 * property. So this function asks the module what the value *is* and then decides
 * whether that is something a length-shaped property may hold.
 *
 * The four non-RESOLVED outcomes map to four different diagnostics, and they stay
 * four because a developer reading "unknown length unit", "a bare number is not a
 * length" or "not a finite number" learns three different things about the
 * declaration they wrote.
 *
 * The keyword is asked first, and that ordering is load-bearing — see the comment
 * inside.
 */
static Result ComputedComputeLengthGrammar(
    const ComputedLengthGrammar *grammar,
    CssComputedValue *out,
    const CssValue *specified,
    CssDiag *diag)
{
    CssPropertyComputation computation = grammar->computation;
    const CssToken *token = ComputedSingleToken(specified);
    CssNumeric numeric = CssNumericNone();

    /*
     * A keyword first, and it has to be first.
     *
     * `auto` is one IDENT *token*, so a branch guarded by "there is no single
     * token" would never reach it — the identifier would fall through to the
     * numeric parse, which is right to call an identifier not a number, and the
     * developer would be told "not a length" when the property's grammar simply
     * does not accept the keyword they wrote. Asking the property's keyword list
     * before asking the numeric vocabulary keeps those two diagnostics apart.
     */
    if (token && token->type == RoboCssTokenIdent) {
        char keyword[64];
        size_t length = 0u;

        if (!CssTokenKeywordText(token, keyword, sizeof(keyword), &length) ||
            !ComputedLengthKeywordIsKnown(grammar, keyword, length)) {
            return ComputedReject(out, computation, grammar->keywordReason, diag);
        }
        return CssComputedValueMakeKeyword(
            out, CssSliceFromBytes((const uint8_t *)keyword, length), diag);
    }

    switch (CssNumericFromValue(specified, &numeric)) {
    case RoboCssNumericParseResolved:
        break;
    case RoboCssNumericParseDeferred:
        return ComputedKeep(out, specified, 1, diag);
    case RoboCssNumericParseUnknownUnit:
        return ComputedReject(out, computation, "unknown length unit", diag);
    case RoboCssNumericParseNonFinite:
        return ComputedReject(out, computation, "not a finite number", diag);
    default:
        /* INVALID. With a token in hand the value *is* a component and simply is
         * not a length — a string, a block, a function like `rgb()`. Without one,
         * the declaration was not one component at all, which is a different thing
         * to tell a developer and the reason `ComputedSingleToken` is asked
         * again here rather than the outcome being trusted alone. */
        return ComputedReject(out, computation,
                               token ? "not a length"
                                     : "not a single length component",
                               diag);
    }

    switch (numeric.kind) {
    case RoboCssNumericLength:
        if (CssUnitIsAbsolute(numeric.unit)) {
            /* `1in` and `96px` are the same length, so a computed value has one
             * of them. Converting here is exact, not an approximation — and the
             * unit's *family* is the module's answer, not a fact restated here. */
            return CssComputedValueMakeLength(
                out, CssUnitToPx(numeric.number, numeric.unit), RoboCssUnitPx,
                token->isInteger, diag);
        }
        /* A relative or viewport unit keeps its unit and its number: resolving it
         * needs a font or a viewport, and neither exists at this stage. */
        return CssComputedValueMakeLength(out, numeric.number, numeric.unit,
                                                   token->isInteger, diag);
    case RoboCssNumericPercentage:
        /* Stays a percentage: only layout knows what it is a percentage *of*. */
        return CssComputedValueMakePercentage(out, numeric.number,
                                                       token->isInteger, diag);
    case RoboCssNumericNumber:
    case RoboCssNumericInteger:
        if (numeric.number == 0.0) {
            /* CSS's one exception: a bare `0` is a length, because there is no
             * unit that changes what zero means. It is the *only* number a
             * length-shaped property accepts. */
            return CssComputedValueMakeLength(out, 0.0, RoboCssUnitPx, 1,
                                                       diag);
        }
        return ComputedReject(out, computation,
                               "a bare number is not a length", diag);
    default:
        return ComputedReject(out, computation, "not a length", diag);
    }
}

/**
 * Compute a length-family value for the LENGTH shape (9.8's, unchanged).
 *
 * Thin on purpose: 9.11 gave the family a grammar parameter so that the box
 * model's three narrower vocabularies could share this body rather than restate
 * it, and this is the one caller that wants the whole union.
 */
static Result ComputedComputeLength(CssComputedValue *out,
                                             const CssValue *specified,
                                             CssDiag *diag)
{
    return ComputedComputeLengthGrammar(
        ComputedLengthGrammarFor(RoboCssPropertyComputationLength), out,
        specified, diag);
}

/** Compute a single-identifier keyword. */
static Result ComputedComputeKeyword(CssComputedValue *out,
                                              const CssValue *specified,
                                              CssDiag *diag)
{
    const CssToken *token = ComputedSingleToken(specified);
    const CssValue *single = CssValueSingle(specified);
    char keyword[64];
    size_t length = 0u;

    if (!token) {
        if (single && ComputedFunctionIsUnresolved(single)) {
            return ComputedKeep(out, specified, 1, diag);
        }
        return ComputedReject(out, RoboCssPropertyComputationKeyword,
                               "not a single identifier", diag);
    }
    if (token->type != RoboCssTokenIdent) {
        return ComputedReject(out, RoboCssPropertyComputationKeyword,
                               "not an identifier", diag);
    }
    if (!CssTokenKeywordText(token, keyword, sizeof(keyword), &length)) {
        return ComputedReject(out, RoboCssPropertyComputationKeyword,
                               "not an identifier", diag);
    }
    return CssComputedValueMakeKeyword(
        out, CssSliceFromBytes((const uint8_t *)keyword, length), diag);
}

/*
 * The closed vocabularies.
 *
 * A shape whose whole content is "one identifier, and it must be one of these"
 * has two readers per vocabulary — a predicate that says whether the identifier
 * is one of them, and a constructor that turns it into the value — and nothing
 * else. So a vocabulary is a row: the shape, those two doors, and the two
 * sentences its refusals need. 9.12 wrote `display`'s body out by hand because it
 * was the only one; 9.13 adds `position` and the body became a table, which is
 * the same move the box model's three length families made one phase earlier.
 *
 * The row carries three of the four refusal sentences and not the fourth,
 * deliberately. "Not an identifier" is the *same* mistake for every vocabulary —
 * `display: 20px` and `position: 20px` differ in nothing an author did — and the
 * diagnostic already names the shape (`display: not an identifier`), so a row
 * field for it would be a place to write a sentence that is wrong for one of its
 * users. The two that *are* per-vocabulary stay per-vocabulary: what a display's
 * identifier is allowed to be, and what a position's is.
 */
typedef int (*computedVocabularyKnownFn)(CssSlice keyword);

typedef Result (*computedVocabularyMakeFn)(CssComputedValue *out,
                                                     CssSlice keyword,
                                                     CssDiag *diag);

typedef struct computedVocabularyGrammar {
    CssPropertyComputation computation;
    /** Non-zero when @p keyword spells one of this vocabulary, case-insensitively. */
    computedVocabularyKnownFn IsKnown;
    /** The value @p keyword names; only called once @ref IsKnown said yes. */
    computedVocabularyMakeFn make;
    /** What to say when the declaration is not one identifier at all. */
    const char *notSingleReason;
    /** What to say when the identifier is not in this vocabulary. */
    const char *unknownReason;
} ComputedVocabularyGrammar;

static int ComputedVocabularyDisplayKnown(CssSlice keyword)
{
    return CssDisplaySliceIsKnown(keyword);
}

static Result ComputedVocabularyDisplayMake(CssComputedValue *out,
                                                      CssSlice keyword,
                                                      CssDiag *diag)
{
    return CssComputedValueMakeDisplay(out,
                                                CssDisplayFromSlice(keyword),
                                                diag);
}

static int ComputedVocabularyPositionKnown(CssSlice keyword)
{
    return CssPositioningSliceIsKnown(keyword);
}

static Result ComputedVocabularyPositionMake(CssComputedValue *out,
                                                       CssSlice keyword,
                                                       CssDiag *diag)
{
    return CssComputedValueMakePosition(out,
                                                 CssPositioningFromSlice(keyword),
                                                 diag);
}

static int ComputedVocabularyBackgroundRepeatKnown(CssSlice keyword)
{
    return CssBackgroundRepeatSliceIsKnown(keyword);
}

static int ComputedVocabularyBackgroundAttachmentKnown(CssSlice keyword)
{
    return CssBackgroundAttachmentSliceIsKnown(keyword);
}

static int ComputedVocabularyBorderStyleKnown(CssSlice keyword)
{
    return CssBorderStyleSliceIsKnown(keyword);
}

static int ComputedVocabularyOverflowKnown(CssSlice keyword)
{
    return CssOverflowSliceIsKnown(keyword);
}

static Result ComputedVocabularyOverflowMake(CssComputedValue *out,
                                                       CssSlice keyword,
                                                       CssDiag *diag)
{
    return CssComputedValueMakeOverflow(
        out, CssOverflowFromSlice(keyword), diag);
}

static Result ComputedVocabularyBorderStyleMake(CssComputedValue *out,
                                                           CssSlice keyword,
                                                           CssDiag *diag)
{
    return CssComputedValueMakeBorderStyle(
        out, CssBorderStyleFromSlice(keyword), diag);
}

static int ComputedVocabularyFlexDirectionKnown(CssSlice keyword)
{
    return CssFlexDirectionSliceIsKnown(keyword);
}

static Result ComputedVocabularyFlexDirectionMake(CssComputedValue *out,
                                                              CssSlice keyword,
                                                              CssDiag *diag)
{
    return CssComputedValueMakeFlexDirection(
        out, CssFlexDirectionFromSlice(keyword), diag);
}

static int ComputedVocabularyFlexWrapKnown(CssSlice keyword)
{
    return CssFlexWrapSliceIsKnown(keyword);
}

static Result ComputedVocabularyFlexWrapMake(CssComputedValue *out,
                                                         CssSlice keyword,
                                                         CssDiag *diag)
{
    return CssComputedValueMakeFlexWrap(
        out, CssFlexWrapFromSlice(keyword), diag);
}

/*
 * The four alignment properties share one vocabulary and one constructor; only the
 * *predicate* differs, because only the predicate knows which of the four is being
 * asked. `space-between` is a value for `justify-content` and not for `align-items`,
 * and that difference has to be a refusal here — the shape is the last place that
 * knows the property, and a value the property cannot take is a mistake rather than a
 * typed value a later reader would have to second-guess.
 */
static int ComputedVocabularyAlignJustifyKnown(CssSlice keyword)
{
    return CssFlexAlignSliceIsKnown(keyword) &&
           CssFlexAlignAllowedFor(CssFlexAlignFromSlice(keyword),
                                  RoboCssFlexAlignRoleJustify);
}

static int ComputedVocabularyAlignItemsKnown(CssSlice keyword)
{
    return CssFlexAlignSliceIsKnown(keyword) &&
           CssFlexAlignAllowedFor(CssFlexAlignFromSlice(keyword),
                                  RoboCssFlexAlignRoleItems);
}

static int ComputedVocabularyAlignSelfKnown(CssSlice keyword)
{
    return CssFlexAlignSliceIsKnown(keyword) &&
           CssFlexAlignAllowedFor(CssFlexAlignFromSlice(keyword),
                                  RoboCssFlexAlignRoleSelf);
}

static int ComputedVocabularyAlignContentKnown(CssSlice keyword)
{
    return CssFlexAlignSliceIsKnown(keyword) &&
           CssFlexAlignAllowedFor(CssFlexAlignFromSlice(keyword),
                                  RoboCssFlexAlignRoleContent);
}

static Result ComputedVocabularyFlexAlignMake(CssComputedValue *out,
                                                          CssSlice keyword,
                                                          CssDiag *diag)
{
    return CssComputedValueMakeFlexAlign(
        out, CssFlexAlignFromSlice(keyword), diag);
}

/* The table vocabularies (18.5): five more predicates, and the same make body.
 * Each value here is a keyword and nothing else, so a typed enumerator would be a
 * second copy of a table for a reader this phase does not yet have -- the table
 * layout pass (19.6) is what will read them. What the closed set buys is the
 * refusal, exactly as it does for `background-repeat`. */
static int ComputedVocabularyTableLayoutKnown(CssSlice keyword)
{
    return CssTableLayoutSliceIsKnown(keyword);
}

static int ComputedVocabularyBorderCollapseKnown(CssSlice keyword)
{
    return CssBorderCollapseSliceIsKnown(keyword);
}

static int ComputedVocabularyCaptionSideKnown(CssSlice keyword)
{
    return CssCaptionSideSliceIsKnown(keyword);
}

static int ComputedVocabularyEmptyCellsKnown(CssSlice keyword)
{
    return CssEmptyCellsSliceIsKnown(keyword);
}

static int ComputedVocabularyVisibilityKnown(CssSlice keyword)
{
    return CssVisibilitySliceIsKnown(keyword);
}

/* The multi-column vocabularies (18.6): two more predicates and the same make body. */
static int ComputedVocabularyColumnSpanKnown(CssSlice keyword)
{
    return CssColumnSpanSliceIsKnown(keyword);
}

static int ComputedVocabularyColumnFillKnown(CssSlice keyword)
{
    return CssColumnFillSliceIsKnown(keyword);
}

/* The fragmentation vocabularies (19.7): two predicates for three properties, and
 * the sharing is the interesting half -- `break-before` and `break-after` hold one
 * vocabulary, so they share one predicate and one row body, while `break-inside`
 * holds a smaller set of its own and gets its own. A build that gave all three the
 * `break-between` predicate would accept `break-inside: column`, which is not a
 * value: a break *inside* a box has no next fragmentainer to begin. */
static int ComputedVocabularyBreakBetweenKnown(CssSlice keyword)
{
    return CssBreakBetweenSliceIsKnown(keyword);
}

static int ComputedVocabularyBreakInsideKnown(CssSlice keyword)
{
    return CssBreakInsideSliceIsKnown(keyword);
}

/* The writing-mode vocabularies (18.13): three more predicates and the same make body.
 * `direction` is the one whose shape is *upgraded* rather than added -- it used to be
 * the KEYWORD shape -- so its predicate is what makes `direction: banana` a refusal
 * where it used to be a value. */
static int ComputedVocabularyDirectionKnown(CssSlice keyword)
{
    return CssDirectionSliceIsKnown(keyword);
}

static int ComputedVocabularyWritingModeKnown(CssSlice keyword)
{
    return CssWritingModeSliceIsKnown(keyword);
}

static int ComputedVocabularyTextOrientationKnown(CssSlice keyword)
{
    return CssTextOrientationSliceIsKnown(keyword);
}

/* The transform vocabularies (18.7): two more predicates and the same make body. The
 * transform *list* and its origin are carried declarations, so the function-name set
 * is not a computed shape here -- it is the vocabulary a later pass checks a list's
 * members against. */
static int ComputedVocabularyTransformStyleKnown(CssSlice keyword)
{
    return CssTransformStyleSliceIsKnown(keyword);
}

static int ComputedVocabularyBackfaceVisibilityKnown(CssSlice keyword)
{
    return CssBackfaceVisibilitySliceIsKnown(keyword);
}

/* The transition vocabulary (18.8): one predicate and the same make body. The
 * timing-function keywords are the *keyword* half of a grammar this phase carries,
 * so `transition-behavior` is the only shape read through the closed set. */
static int ComputedVocabularyTransitionBehaviorKnown(CssSlice keyword)
{
    return CssTransitionBehaviorSliceIsKnown(keyword);
}

/* The animation vocabularies (18.9): four more predicates and the same make body. */
static int ComputedVocabularyAnimationDirectionKnown(CssSlice keyword)
{
    return CssAnimationDirectionSliceIsKnown(keyword);
}

static int ComputedVocabularyAnimationFillModeKnown(CssSlice keyword)
{
    return CssAnimationFillModeSliceIsKnown(keyword);
}

static int ComputedVocabularyAnimationPlayStateKnown(CssSlice keyword)
{
    return CssAnimationPlayStateSliceIsKnown(keyword);
}

static int ComputedVocabularyAnimationCompositionKnown(CssSlice keyword)
{
    return CssAnimationCompositionSliceIsKnown(keyword);
}

/* The container-query vocabulary (18.11): one predicate and the same make body. */
static int ComputedVocabularyContainerTypeKnown(CssSlice keyword)
{
    return CssContainerTypeSliceIsKnown(keyword);
}

/*
 * The font vocabularies (18.14): six predicates and the same make body.
 *
 * Six rows because six properties have a grammar that *is* their closed set. The
 * other half of the family — `font-stretch`'s percentage, `font-variant-numeric`'s
 * space-separated axes, `font-feature-settings`' tag/value list — is carried
 * PRESERVED, and the closed sets inside those declarations live in RoboCSSFont.h
 * where a consumer can reach them without this table having to pretend the whole
 * grammar is closed. That division is 18.7's: `transform` is carried and the
 * function-name set is typed.
 */
static int ComputedVocabularyFontStyleKnown(CssSlice keyword)
{
    return CssFontStyleSliceIsKnown(keyword);
}

static int ComputedVocabularyFontKerningKnown(CssSlice keyword)
{
    return CssFontKerningSliceIsKnown(keyword);
}

static int ComputedVocabularyFontOpticalSizingKnown(CssSlice keyword)
{
    return CssFontOpticalSizingSliceIsKnown(keyword);
}

static int ComputedVocabularyFontSynthesisKnown(CssSlice keyword)
{
    return CssFontSynthesisSliceIsKnown(keyword);
}

static int ComputedVocabularyFontVariantCapsKnown(CssSlice keyword)
{
    return CssFontVariantCapsSliceIsKnown(keyword);
}

static int ComputedVocabularyFontVariantPositionKnown(CssSlice keyword)
{
    return CssFontVariantPositionSliceIsKnown(keyword);
}

/*
 * The text vocabularies (18.15): ten predicates and the same make body.
 *
 * Ten rows because ten properties have a grammar that *is* their closed set. The
 * rest of the family is carried PRESERVED, and the closed sets inside those
 * declarations live in RoboCSSText.h where a consumer can reach them without this
 * table pretending the whole grammar is closed: `text-decoration-line` is a *set*,
 * `text-overflow` may be a string, `text-decoration-thickness` may be a length, and
 * `text-underline-position` is a set of two different questions. That division is
 * 18.7's, and 18.14 made the same split for the font family.
 */
static int ComputedVocabularyTextAlignKnown(CssSlice keyword)
{
    return CssTextAlignSliceIsKnown(keyword);
}

static int ComputedVocabularyTextAlignLastKnown(CssSlice keyword)
{
    return CssTextAlignLastSliceIsKnown(keyword);
}

static int ComputedVocabularyTextJustifyKnown(CssSlice keyword)
{
    return CssTextJustifySliceIsKnown(keyword);
}

static int ComputedVocabularyTextTransformKnown(CssSlice keyword)
{
    return CssTextTransformSliceIsKnown(keyword);
}

static int ComputedVocabularyWhiteSpaceKnown(CssSlice keyword)
{
    return CssWhiteSpaceSliceIsKnown(keyword);
}

static int ComputedVocabularyWordBreakKnown(CssSlice keyword)
{
    return CssWordBreakSliceIsKnown(keyword);
}

static int ComputedVocabularyOverflowWrapKnown(CssSlice keyword)
{
    return CssOverflowWrapSliceIsKnown(keyword);
}

static int ComputedVocabularyLineBreakKnown(CssSlice keyword)
{
    return CssLineBreakSliceIsKnown(keyword);
}

static int ComputedVocabularyHyphensKnown(CssSlice keyword)
{
    return CssHyphensSliceIsKnown(keyword);
}

static int ComputedVocabularyTextDecorationStyleKnown(CssSlice keyword)
{
    return CssTextDecorationStyleSliceIsKnown(keyword);
}

/*
 * The image vocabularies (18.16): two predicates and the same make body.
 *
 * Two rows because two properties have a grammar that *is* their closed set. The rest
 * of the family is carried PRESERVED, and the closed sets inside those declarations
 * live in RoboCSSImage.h where a consumer can reach them without this table
 * pretending the whole grammar is closed: `image-orientation` is
 * `from-image | none | <angle>`, `image-resolution` is a `||` of two optional parts,
 * and `object-position` is the position pair the background module owns. That
 * division is 18.7's, and 18.15 made the same split for the text family.
 */
static int ComputedVocabularyImageRenderingKnown(CssSlice keyword)
{
    return CssImageRenderingSliceIsKnown(keyword);
}

static int ComputedVocabularyObjectFitKnown(CssSlice keyword)
{
    return CssObjectFitSliceIsKnown(keyword);
}

/*
 * The 18.17 additions: the box vocabulary `background-clip` and `background-origin`
 * share, and `background-blend-mode`'s sixteen modes.
 */
static int ComputedVocabularyBackgroundBoxKnown(CssSlice keyword)
{
    return CssBackgroundBoxSliceIsKnown(keyword);
}

static int ComputedVocabularyBlendModeKnown(CssSlice keyword)
{
    return CssBlendModeSliceIsKnown(keyword);
}

/* 18.18's one closed set: the border-image repeat vocabulary. */
static int ComputedVocabularyBorderImageRepeatKnown(CssSlice keyword)
{
    return CssBorderImageRepeatSliceIsKnown(keyword);
}

/**
 * A closed vocabulary whose value *is* its keyword, carried as one.
 *
 * `background-repeat` and `background-attachment` have no representation beyond
 * the word: unlike a display or a position there is nothing to *do* with
 * `no-repeat` at this stage, so a typed enumerator here would be a second copy of
 * a table for no reader. What the closed vocabulary buys is the refusal —
 * `background-repeat: banana` is a mistake rather than a keyword carrying the text
 * `banana` — and what arrives is already the canonical spelling, because the
 * token's decoded text is lower-cased and `IsKnown` has just confirmed it is one
 * of the rows.
 */
static Result ComputedVocabularyKeywordMake(CssComputedValue *out,
                                                      CssSlice keyword,
                                                      CssDiag *diag)
{
    return CssComputedValueMakeKeyword(out, keyword, diag);
}

static const ComputedVocabularyGrammar computedVocabularyGrammars[] = {
    { RoboCssPropertyComputationDisplay, ComputedVocabularyDisplayKnown,
      ComputedVocabularyDisplayMake, "not a single display keyword",
      "not a display value" },
    { RoboCssPropertyComputationPosition, ComputedVocabularyPositionKnown,
      ComputedVocabularyPositionMake, "not a single position keyword",
      "not a position value" },
    { RoboCssPropertyComputationBackgroundRepeat,
      ComputedVocabularyBackgroundRepeatKnown, ComputedVocabularyKeywordMake,
      "not a single background-repeat keyword", "not a background repeat value" },
    { RoboCssPropertyComputationBackgroundAttachment,
      ComputedVocabularyBackgroundAttachmentKnown, ComputedVocabularyKeywordMake,
      "not a single background-attachment keyword",
      "not a background attachment value" },
    { RoboCssPropertyComputationBorderStyle, ComputedVocabularyBorderStyleKnown,
      ComputedVocabularyBorderStyleMake, "not a single border style keyword",
      "not a border style value" },
    { RoboCssPropertyComputationOverflow, ComputedVocabularyOverflowKnown,
      ComputedVocabularyOverflowMake, "not a single overflow keyword",
      "not an overflow value" },
    /* The flexbox vocabularies (18.3) are four more rows in the same table. The
     * alignment family is six rows rather than two because each of the four
     * properties gets its own predicate: the vocabulary is one, but which values
     * belong to which property is the property's answer. */
    { RoboCssPropertyComputationFlexDirection, ComputedVocabularyFlexDirectionKnown,
      ComputedVocabularyFlexDirectionMake, "not a single flex-direction keyword",
      "not a flex-direction value" },
    { RoboCssPropertyComputationFlexWrap, ComputedVocabularyFlexWrapKnown,
      ComputedVocabularyFlexWrapMake, "not a single flex-wrap keyword",
      "not a flex-wrap value" },
    { RoboCssPropertyComputationJustifyContent, ComputedVocabularyAlignJustifyKnown,
      ComputedVocabularyFlexAlignMake, "not a single justify-content keyword",
      "not a justify-content value" },
    { RoboCssPropertyComputationAlignItems, ComputedVocabularyAlignItemsKnown,
      ComputedVocabularyFlexAlignMake, "not a single align-items keyword",
      "not an align-items value" },
    { RoboCssPropertyComputationAlignSelf, ComputedVocabularyAlignSelfKnown,
      ComputedVocabularyFlexAlignMake, "not a single align-self keyword",
      "not an align-self value" },
    { RoboCssPropertyComputationAlignContent, ComputedVocabularyAlignContentKnown,
      ComputedVocabularyFlexAlignMake, "not a single align-content keyword",
      "not an align-content value" },
    /* The table vocabularies (18.5) are five more rows in the same table, and their
     * make door is the one that carries the keyword: unlike a display or a position
     * there is nothing to *do* with `collapse` at this stage, so a typed enumerator
     * would be a second copy of a table for a reader that arrives at 19.6. */
    { RoboCssPropertyComputationTableLayout, ComputedVocabularyTableLayoutKnown,
      ComputedVocabularyKeywordMake, "not a single table-layout keyword",
      "not a table-layout value" },
    { RoboCssPropertyComputationBorderCollapse,
      ComputedVocabularyBorderCollapseKnown, ComputedVocabularyKeywordMake,
      "not a single border-collapse keyword", "not a border-collapse value" },
    { RoboCssPropertyComputationCaptionSide, ComputedVocabularyCaptionSideKnown,
      ComputedVocabularyKeywordMake, "not a single caption-side keyword",
      "not a caption-side value" },
    { RoboCssPropertyComputationEmptyCells, ComputedVocabularyEmptyCellsKnown,
      ComputedVocabularyKeywordMake, "not a single empty-cells keyword",
      "not an empty-cells value" },
    { RoboCssPropertyComputationVisibility, ComputedVocabularyVisibilityKnown,
      ComputedVocabularyKeywordMake, "not a single visibility keyword",
      "not a visibility value" },
    /* The multi-column vocabularies (18.6) are two more rows in the same table. */
    { RoboCssPropertyComputationColumnSpan, ComputedVocabularyColumnSpanKnown,
      ComputedVocabularyKeywordMake, "not a single column-span keyword",
      "not a column-span value" },
    { RoboCssPropertyComputationColumnFill, ComputedVocabularyColumnFillKnown,
      ComputedVocabularyKeywordMake, "not a single column-fill keyword",
      "not a column-fill value" },
    /* The fragmentation vocabularies (19.7) are three rows over two vocabularies: the
     * `break-between` set answers both edges of a box and the `break-inside` set answers
     * the interior, so the pair of rows that share a predicate are the pair that share a
     * meaning. */
    { RoboCssPropertyComputationBreakBefore, ComputedVocabularyBreakBetweenKnown,
      ComputedVocabularyKeywordMake, "not a single break-before keyword",
      "not a break-before value" },
    { RoboCssPropertyComputationBreakAfter, ComputedVocabularyBreakBetweenKnown,
      ComputedVocabularyKeywordMake, "not a single break-after keyword",
      "not a break-after value" },
    { RoboCssPropertyComputationBreakInside, ComputedVocabularyBreakInsideKnown,
      ComputedVocabularyKeywordMake, "not a single break-inside keyword",
      "not a break-inside value" },
    /* The writing-mode vocabularies (18.13) are three more rows in the same table. */
    { RoboCssPropertyComputationDirection, ComputedVocabularyDirectionKnown,
      ComputedVocabularyKeywordMake, "not a single direction keyword",
      "not a direction value" },
    { RoboCssPropertyComputationWritingMode, ComputedVocabularyWritingModeKnown,
      ComputedVocabularyKeywordMake, "not a single writing-mode keyword",
      "not a writing-mode value" },
    { RoboCssPropertyComputationTextOrientation,
      ComputedVocabularyTextOrientationKnown, ComputedVocabularyKeywordMake,
      "not a single text-orientation keyword", "not a text-orientation value" },
    /* The transform vocabularies (18.7) are two more rows in the same table. */
    { RoboCssPropertyComputationTransformStyle,
      ComputedVocabularyTransformStyleKnown, ComputedVocabularyKeywordMake,
      "not a single transform-style keyword", "not a transform-style value" },
    { RoboCssPropertyComputationBackfaceVisibility,
      ComputedVocabularyBackfaceVisibilityKnown, ComputedVocabularyKeywordMake,
      "not a single backface-visibility keyword",
      "not a backface-visibility value" },
    /* The transition vocabulary (18.8) is one more row in the same table. */
    { RoboCssPropertyComputationTransitionBehavior,
      ComputedVocabularyTransitionBehaviorKnown, ComputedVocabularyKeywordMake,
      "not a single transition-behavior keyword",
      "not a transition-behavior value" },
    /* The animation vocabularies (18.9) are four more rows in the same table. */
    { RoboCssPropertyComputationAnimationDirection,
      ComputedVocabularyAnimationDirectionKnown, ComputedVocabularyKeywordMake,
      "not a single animation-direction keyword",
      "not an animation-direction value" },
    { RoboCssPropertyComputationAnimationFillMode,
      ComputedVocabularyAnimationFillModeKnown, ComputedVocabularyKeywordMake,
      "not a single animation-fill-mode keyword",
      "not an animation-fill-mode value" },
    { RoboCssPropertyComputationAnimationPlayState,
      ComputedVocabularyAnimationPlayStateKnown, ComputedVocabularyKeywordMake,
      "not a single animation-play-state keyword",
      "not an animation-play-state value" },
    { RoboCssPropertyComputationAnimationComposition,
      ComputedVocabularyAnimationCompositionKnown,
      ComputedVocabularyKeywordMake,
      "not a single animation-composition keyword",
      "not an animation-composition value" },
    /* The container-query vocabulary (18.11) is one more row in the same table. */
    { RoboCssPropertyComputationContainerType,
      ComputedVocabularyContainerTypeKnown, ComputedVocabularyKeywordMake,
      "not a single container-type keyword", "not a container-type value" },
    /*
     * The font vocabularies (18.14) are six more rows, and their make door is the
     * keyword-carrying one for the reason 18.5's five recorded: nothing has a *typed
     * representation* of a kerning or a caps variant yet, so an enumerator in the
     * computed value would be a second copy of a table for a reader that arrives with
     * the text phase (18.15). What the closed set buys is the refusal —
     * `font-kerning: banana` is a mistake rather than a keyword carrying the letters —
     * and what arrives is already the canonical spelling.
     */
    { RoboCssPropertyComputationFontStyle, ComputedVocabularyFontStyleKnown,
      ComputedVocabularyKeywordMake, "not a single font-style keyword",
      "not a font-style value" },
    { RoboCssPropertyComputationFontKerning, ComputedVocabularyFontKerningKnown,
      ComputedVocabularyKeywordMake, "not a single font-kerning keyword",
      "not a font-kerning value" },
    { RoboCssPropertyComputationFontOpticalSizing,
      ComputedVocabularyFontOpticalSizingKnown, ComputedVocabularyKeywordMake,
      "not a single font-optical-sizing keyword",
      "not a font-optical-sizing value" },
    { RoboCssPropertyComputationFontSynthesis,
      ComputedVocabularyFontSynthesisKnown, ComputedVocabularyKeywordMake,
      "not a single font-synthesis keyword", "not a font-synthesis value" },
    { RoboCssPropertyComputationFontVariantCaps,
      ComputedVocabularyFontVariantCapsKnown, ComputedVocabularyKeywordMake,
      "not a single font-variant-caps keyword", "not a font-variant-caps value" },
    { RoboCssPropertyComputationFontVariantPosition,
      ComputedVocabularyFontVariantPositionKnown, ComputedVocabularyKeywordMake,
      "not a single font-variant-position keyword",
      "not a font-variant-position value" },
    /*
     * The text vocabularies (18.15) are ten more rows in the same table, and their
     * make door is the keyword-carrying one for the reason 18.5's rows recorded:
     * nothing has a *typed representation* of a `white-space` or an alignment in the
     * computed value yet, so an enumerator here would be a second copy of a table for
     * a consumer that arrives with the line-breaking and text-painting phases. What
     * the closed set buys is the refusal — `white-space: banana` and
     * `text-align: middle` are mistakes rather than keywords carrying the letters —
     * and what arrives is already the canonical spelling.
     */
    { RoboCssPropertyComputationTextAlign, ComputedVocabularyTextAlignKnown,
      ComputedVocabularyKeywordMake, "not a single text-align keyword",
      "not a text-align value" },
    { RoboCssPropertyComputationTextAlignLast,
      ComputedVocabularyTextAlignLastKnown, ComputedVocabularyKeywordMake,
      "not a single text-align-last keyword", "not a text-align-last value" },
    { RoboCssPropertyComputationTextJustify, ComputedVocabularyTextJustifyKnown,
      ComputedVocabularyKeywordMake, "not a single text-justify keyword",
      "not a text-justify value" },
    { RoboCssPropertyComputationTextTransform,
      ComputedVocabularyTextTransformKnown, ComputedVocabularyKeywordMake,
      "not a single text-transform keyword", "not a text-transform value" },
    { RoboCssPropertyComputationWhiteSpace, ComputedVocabularyWhiteSpaceKnown,
      ComputedVocabularyKeywordMake, "not a single white-space keyword",
      "not a white-space value" },
    { RoboCssPropertyComputationWordBreak, ComputedVocabularyWordBreakKnown,
      ComputedVocabularyKeywordMake, "not a single word-break keyword",
      "not a word-break value" },
    { RoboCssPropertyComputationOverflowWrap,
      ComputedVocabularyOverflowWrapKnown, ComputedVocabularyKeywordMake,
      "not a single overflow-wrap keyword", "not an overflow-wrap value" },
    { RoboCssPropertyComputationLineBreak, ComputedVocabularyLineBreakKnown,
      ComputedVocabularyKeywordMake, "not a single line-break keyword",
      "not a line-break value" },
    { RoboCssPropertyComputationHyphens, ComputedVocabularyHyphensKnown,
      ComputedVocabularyKeywordMake, "not a single hyphens keyword",
      "not a hyphens value" },
    { RoboCssPropertyComputationTextDecorationStyle,
      ComputedVocabularyTextDecorationStyleKnown, ComputedVocabularyKeywordMake,
      "not a single text-decoration-style keyword",
      "not a text-decoration-style value" },
    /*
     * The image vocabularies (18.16) are two more rows in the same table, and their
     * make door is the keyword-carrying one for the reason 18.5's rows recorded:
     * nothing has a *typed representation* of an `image-rendering` or an `object-fit`
     * in the computed value yet, so an enumerator here would be a second copy of a
     * table for a consumer that arrives with the rendering phase. What the closed set
     * buys is the refusal — `image-rendering: banana` and `object-fit: middle` are
     * mistakes rather than keywords carrying the letters — and what arrives is already
     * the canonical spelling.
     */
    { RoboCssPropertyComputationImageRendering,
      ComputedVocabularyImageRenderingKnown, ComputedVocabularyKeywordMake,
      "not a single image-rendering keyword", "not an image-rendering value" },
    { RoboCssPropertyComputationObjectFit,
      ComputedVocabularyObjectFitKnown, ComputedVocabularyKeywordMake,
      "not a single object-fit keyword", "not an object-fit value" },
    /*
     * 18.17's two rows. `background-clip` and `background-origin` share one shape and
     * differ only in their initial value, and `background-blend-mode` is its own set;
     * both carry the keyword rather than a typed enumerator for the reason above.
     */
    { RoboCssPropertyComputationBackgroundBox,
      ComputedVocabularyBackgroundBoxKnown, ComputedVocabularyKeywordMake,
      "not a single background box keyword", "not a background box value" },
    { RoboCssPropertyComputationBlendMode,
      ComputedVocabularyBlendModeKnown, ComputedVocabularyKeywordMake,
      "not a single blend mode keyword", "not a background blend mode value" },
    /* 18.18's row: the border-image repeat set, the border analogue of the background
     * one. */
    { RoboCssPropertyComputationBorderImageRepeat,
      ComputedVocabularyBorderImageRepeatKnown, ComputedVocabularyKeywordMake,
      "not a single border-image repeat keyword",
      "not a border-image repeat value" }
};

/** The vocabulary grammar for @p computation, or NULL when it is not one. */
static const ComputedVocabularyGrammar *ComputedVocabularyGrammarFor(
    CssPropertyComputation computation)
{
    size_t index;

    for (index = 0u;
         index < sizeof(computedVocabularyGrammars) /
                     sizeof(computedVocabularyGrammars[0]);
         index++) {
        if (computedVocabularyGrammars[index].computation == computation) {
            return &computedVocabularyGrammars[index];
        }
    }
    return NULL;
}

/**
 * Compute a closed-vocabulary value: one identifier that must be in the table.
 *
 * The difference from the KEYWORD shape is one branch, and it is the whole point
 * of both vocabularies that have one: KEYWORD accepts any identifier and hands it
 * on, while this asks whether the identifier is one of them and refuses it if not.
 * That is why `display: bogus` and `position: banana` are invalid values with
 * diagnostics rather than computed keywords carrying those letters — a keyword
 * shape is right for a property whose meaning belongs to a phase that has not
 * landed (`cursor: grab`), and wrong for the ones every later phase reads.
 *
 * Three refusals stay three, because they are three different mistakes: a value
 * the grammar produced as a function this build cannot reduce (`var(--d)`) is
 * legal CSS and is therefore *carried*, not refused; something that is not an
 * identifier at all (`20px`, `#fff`, `10%`) says so; and an identifier outside the
 * vocabulary says that instead, which is the sentence an author who wrote
 * `display: flex` or `position: banana` needs, since the first is a value this
 * build has not added and the second is a word it does not know.
 */
static Result ComputedComputeVocabulary(
    const ComputedVocabularyGrammar *grammar,
    CssComputedValue *out,
    const CssValue *specified,
    CssDiag *diag)
{
    const CssToken *token = ComputedSingleToken(specified);
    const CssValue *single = CssValueSingle(specified);
    char keyword[64];
    size_t length = 0u;
    CssSlice text;

    if (!token) {
        /* A reference, and it is carried rather than refused: `display: var(--d)`
         * is a declaration whose value is decided where `--d` is, which is a
         * later stage's question and not a mistake here. The unresolved flag says
         * so, and a consumer that reads the flag will not mistake the tokens for a
         * vocabulary value. */
        if (single && ComputedFunctionIsUnresolved(single)) {
            return ComputedKeep(out, specified, 1, diag);
        }
        return ComputedReject(out, grammar->computation, grammar->notSingleReason,
                               diag);
    }
    if (token->type != RoboCssTokenIdent ||
        !CssTokenKeywordText(token, keyword, sizeof(keyword), &length)) {
        return ComputedReject(out, grammar->computation, "not an identifier", diag);
    }
    text = CssSliceFromBytes((const uint8_t *)keyword, length);
    /* The *identifier* is asked, not the result — and this is the one place both
     * vocabularies have the same trap. `none` is a display value *and*
     * `CssDisplayFromSlice`'s answer for "not one"; `static` is a
     * position value *and* `CssPositioningFromSlice`'s. A check on the
     * result would therefore accept `display: bogus` as `display: none` and
     * `position: banana` as `position: static`, which are the two most damaging
     * confusions their properties have. */
    if (!grammar->IsKnown(text)) {
        return ComputedReject(out, grammar->computation, grammar->unknownReason,
                               diag);
    }
    return grammar->make(out, text, diag);
}

/**
 * Compute a `background-image`: `none`, or a reference this build carries (9.14).
 *
 * Two answers rather than one, and the second is deliberately not a refusal.
 * `none` is a *value* — the property's initial value, and what the `background`
 * shorthand resets the image to — so it is a typed KEYWORD. Everything else that
 * can be an image is a reference: `url("a.png")` and a bare `url(a.png)` token are
 * the same fact, `linear-gradient(...)` is another, and this build can hold every
 * one of them and fetch none of them. So they are carried with the unresolved flag
 * set, exactly as 9.9 carries `oklch()` — legal CSS this build has no
 * representation *for*, which is a different answer from `banana`, and the one an
 * author writing real CSS will meet.
 *
 * The refusal is what is left: an identifier that is not `none` (`banana`), a
 * number, a dimension, a string, or a sequence of more than one component.
 */
static Result ComputedComputeBackgroundImage(
    CssComputedValue *out,
    const CssValue *specified,
    CssDiag *diag)
{
    static const char *const reason = "not a background image";
    const CssPropertyComputation computation =
        RoboCssPropertyComputationBackgroundImage;
    const CssValue *single = CssValueSingle(specified);

    if (single && single->kind == RoboCssValueKindFunction) {
        /* A `var()` is a reference to a reference; a `url()` is a reference to a
         * file; a gradient is a reference to a description of pixels. None is a
         * colour or a length this layer could compute, and all three are carried
         * rather than refused. */
        return ComputedKeep(out, specified, 1, diag);
    }
    if (single && single->kind == RoboCssValueKindToken) {
        CssToken token = CssValueToken(single);

        if (token.type == RoboCssTokenUrl) {
            return ComputedKeep(out, specified, 1, diag);
        }
        if (token.type == RoboCssTokenIdent) {
            char keyword[64];
            size_t length = 0u;

            if (CssTokenKeywordText(&token, keyword, sizeof(keyword), &length) &&
                CssBytesEqualAscii((const uint8_t *)keyword, length, "none")) {
                /* The canonical spelling, not the author's: `NONE` computes to
                 * `none`, so two equal values cannot print differently. */
                return CssComputedValueMakeKeyword(
                    out, CssSliceFromText("none"), diag);
            }
        }
    }
    return ComputedReject(out, computation, reason, diag);
}

/**
 * Compute a `background-position`: a pair of components (9.14).
 *
 * The reading lives in the background module, because it is the one place the
 * grammar of a *pair* is written down and a second copy here is how
 * `background-position: 25% 50%` and `top: 25%` would come to disagree about what
 * a percentage is. What this layer decides is only the outcome: a pair is a typed
 * value, a reference is carried with its tokens and the flag, and anything else is
 * an INVALID answer with the module's reason rather than a failure code.
 */
static Result ComputedComputeBackgroundPosition(
    CssComputedValue *out,
    const CssValue *specified,
    CssDiag *diag)
{
    CssBackgroundPosition position = CssBackgroundPositionNone();

    switch (CssBackgroundPositionFromValue(specified, &position)) {
    case RoboCssBackgroundParsePosition:
        return CssComputedValueMakeBackgroundPosition(out, position, diag);
    case RoboCssBackgroundParseDeferred:
        /* `calc()`, `var()`: legal CSS this build does not evaluate, carried with
         * the flag so a consumer cannot read the tokens as a position. */
        return ComputedKeep(out, specified, 1, diag);
    default:
        return ComputedReject(out, RoboCssPropertyComputationBackgroundPosition,
                               "not a background position", diag);
    }
}

/**
 * Compute a `background-size`: a keyword or a pair (9.14).
 *
 * The same shape of answer as the position above, and the same division of labour.
 * A percentage stays a percentage here for the reason that matters: `background-size:
 * 50% 25%` is 50% *of a box*, and this layer has no box — the phase that lays one
 * out is the phase that can say 50% of what.
 */
static Result ComputedComputeBackgroundSize(
    CssComputedValue *out,
    const CssValue *specified,
    CssDiag *diag)
{
    CssBackgroundSize size = CssBackgroundSizeNone();

    switch (CssBackgroundSizeFromValue(specified, &size)) {
    case RoboCssBackgroundParseSize:
        return CssComputedValueMakeBackgroundSize(out, size, diag);
    case RoboCssBackgroundParseDeferred:
        return ComputedKeep(out, specified, 1, diag);
    default:
        return ComputedReject(out, RoboCssPropertyComputationBackgroundSize,
                               "not a background size", diag);
    }
}

/**
 * Compute a `z-index`: `auto`, or a whole number (9.13).
 *
 * Two shapes in one property and no third: `auto` is the KEYWORD (which is not
 * zero — it says the element takes no part in its parent's stacking decisions, a
 * different statement from "at level zero"), and everything else is a NUMBER with
 * `isInteger` set. The `<integer>` question is asked of the unit module rather
 * than answered here, because CSS's `<integer>` is a *grammar* rather than a value
 * that happens to be whole: `1.0` is a number token, and the module is the one
 * place that distinction is made (`CssNumericIntegerValue`), including its
 * representable-magnitude bound.
 *
 * The refusals are three rather than one because they tell an author three
 * different things: `1.5` is not a whole number at all, `1.0` (and `1e2`) is a
 * whole number written in a spelling `<integer>` does not accept — `z-index: 1` is
 * what they meant — and `10px` is not a number of any kind. A percentage, a
 * dimension and an unknown unit all land in the third, which is the truthful
 * answer for a property whose grammar has no length in it.
 */
static Result ComputedComputeZIndex(CssComputedValue *out,
                                              const CssValue *specified,
                                              CssDiag *diag)
{
    static const char *const reasonNotNumber = "not a whole number or auto";
    CssPropertyComputation computation = RoboCssPropertyComputationZIndex;
    const CssToken *token = ComputedSingleToken(specified);
    CssNumeric numeric = CssNumericNone();
    int64_t integer = 0;

    if (token && token->type == RoboCssTokenIdent) {
        char keyword[64];
        size_t length = 0u;

        if (!CssTokenKeywordText(token, keyword, sizeof(keyword), &length) ||
            !CssBytesEqualAsciiCi((const uint8_t *)keyword, length, "auto")) {
            return ComputedReject(out, computation, reasonNotNumber, diag);
        }
        return CssComputedValueMakeKeyword(
            out, CssSliceFromBytes((const uint8_t *)keyword, length), diag);
    }

    switch (CssNumericFromValue(specified, &numeric)) {
    case RoboCssNumericParseResolved:
        break;
    case RoboCssNumericParseDeferred:
        return ComputedKeep(out, specified, 1, diag);
    case RoboCssNumericParseNonFinite:
        return ComputedReject(out, computation, "not a finite number", diag);
    default:
        return ComputedReject(out, computation,
                               token ? reasonNotNumber
                                     : "not a single z-index component",
                               diag);
    }

    if (numeric.kind != RoboCssNumericInteger) {
        /* A NUMBER that happens to be whole is separated from one that is not,
         * because an author who wrote `1.0` has a near miss and an author who
         * wrote `1.5` has a different one. Everything else — a percentage, a
         * dimension — is only ever "not a whole number or auto". */
        const char *reason = reasonNotNumber;

        if (numeric.kind == RoboCssNumericNumber) {
            /* `1.0` and `1e2` are whole values and are still not integers: the
             * tokenizer's integer flag is a fact about the *spelling*, and CSS's
             * `<integer>` asks for that spelling. `1.5` is the case where the
             * value itself is wrong, and it gets a sentence that says so. */
            reason = CssNumericIsIntegral(numeric)
                         ? "a whole number must be written as an integer"
                         : "not a whole number";
        }
        return ComputedReject(out, computation, reason, diag);
    }
    /* The bound is the module's, and it is asked even though the conversion's
     * result is then discarded in favour of the number the token already carries:
     * what is being asked for is the *refusal* of a magnitude no runtime can hold
     * as an integer, and `(double)integer` is that same value exactly. */
    if (!CssNumericIntegerValue(numeric, &integer)) {
        return ComputedReject(out, computation, "not a whole number", diag);
    }
    return CssComputedValueMakeNumber(out, (double)integer, 1, diag);
}

/**
 * Compute an `opacity`: a number or a percentage, clamped to `[0, 1]` (11.9).
 *
 * Two value kinds in one property, and the second is why the conversion lives in the property
 * module rather than here: CSS's `<alpha-value>` accepts `50%` and `0.5` as the same value,
 * and the division that makes them the same is a fact about *this* property
 * (`CssOpacityFromPercentage`) rather than about numbers in general -- a percentage
 * means a proportion of a box everywhere else in CSS.
 *
 * Three refusals, and the one that is **not** among them is the point: `opacity: 2` is not a
 * mistake. CSS clamps a computed opacity rather than rejecting it, and it does so because an
 * author who wrote `200%` or `1.5` meant "as opaque as it goes" -- so refusing would make a
 * document that every browser renders correctly render here as if the declaration had never
 * been written, which is a worse failure than the one being prevented. The clamp is applied
 * once, here, and to both spellings, so there is one place the range appears on the way in.
 *
 * What *is* refused tells an author three different things: `opacity: banana` is not a number
 * at all, `1e999` is a number the tokenizer could not make finite (arithmetic that went wrong
 * rather than a value anyone writes), and `10px` is a number of the wrong *kind* -- the
 * truthful answer for a property whose grammar has no length in it, and the same answer
 * `ComputedComputeZIndex` gives a dimension. `calc()` and `var()` are neither: they are
 * legal CSS this build does not evaluate, so they are **carried** with the unresolved flag
 * rather than refused.
 */
static Result ComputedComputeOpacity(CssComputedValue *out,
                                              const CssValue *specified,
                                              CssDiag *diag)
{
    static const char *const reasonNotAlpha = "not a number or a percentage";
    CssPropertyComputation computation = RoboCssPropertyComputationOpacity;
    CssNumeric numeric = CssNumericNone();
    double magnitude;

    switch (CssNumericFromValue(specified, &numeric)) {
    case RoboCssNumericParseResolved:
        break;
    case RoboCssNumericParseDeferred:
        return ComputedKeep(out, specified, 1, diag);
    case RoboCssNumericParseNonFinite:
        return ComputedReject(out, computation, "not a finite number", diag);
    default:
        return ComputedReject(out, computation, reasonNotAlpha, diag);
    }

    switch (numeric.kind) {
    case RoboCssNumericInteger:
    case RoboCssNumericNumber:
        /* An integer is a number here, and the two are one case rather than two because
         * nothing about this property distinguishes `opacity: 1` from `opacity: 1.0` -- the
         * spelling is not part of the value, which is what makes `<alpha-value>` unlike
         * `z-index`'s `<integer>` in exactly the place where being unlike it matters. */
        magnitude = numeric.number;
        break;
    case RoboCssNumericPercentage:
        magnitude = CssOpacityFromPercentage(numeric.number);
        break;
    default:
        return ComputedReject(out, computation, reasonNotAlpha, diag);
    }
    return CssComputedValueMakeOpacity(out, CssOpacityClamp(magnitude), diag);
}

/**
 * Compute a font family list: identifiers and strings, comma separated.
 *
 * Two passes over the children, and the reason is the shape check. A family
 * list is a strictly alternating sequence — part, comma, part, comma, part — and
 * a validator that consumed the components while checking would have already
 * built half a string when it found the malformed part. Validating and summing
 * first, then building into exactly the right buffer, means a malformed list
 * leaves nothing behind and there is no growth logic to get wrong.
 *
 * `font-family: ""` is refused: an empty family name is not a family, and
 * accepting it would put an empty entry in a list that later code reads as a
 * name.
 */
static Result ComputedComputeFamily(CssComputedValue *out,
                                             const CssValue *specified,
                                             CssDiag *diag)
{
    size_t families = 0u;
    size_t total = 0u;
    size_t index;
    int expectingPart = 1;
    char *joined;
    size_t position = 0u;
    Result result;

    if (!specified || specified->kind != RoboCssValueKindList) {
        return ComputedReject(out, RoboCssPropertyComputationFontFamily,
                               "not a family list", diag);
    }
    for (index = 0u; index < specified->children.count; index++) {
        const CssValue *child = &specified->children.items[index];
        size_t length;

        if (CssValueIsTrivia(child)) {
            continue;
        }
        if (child->kind != RoboCssValueKindToken) {
            return ComputedReject(out, RoboCssPropertyComputationFontFamily,
                                   "not a family name", diag);
        }
        if (child->token.type == RoboCssTokenComma) {
            if (expectingPart) {
                return ComputedReject(out, RoboCssPropertyComputationFontFamily,
                                       "a family list cannot begin with a comma", diag);
            }
            expectingPart = 1;
            continue;
        }
        if (child->token.type != RoboCssTokenIdent &&
            child->token.type != RoboCssTokenString) {
            return ComputedReject(out, RoboCssPropertyComputationFontFamily,
                                   "not a family name", diag);
        }
        if (!expectingPart) {
            return ComputedReject(out, RoboCssPropertyComputationFontFamily,
                                   "two family names with no comma between them", diag);
        }
        length = CssTokenValueLength(&child->token);
        if (length == 0u) {
            return ComputedReject(out, RoboCssPropertyComputationFontFamily,
                                   "an empty family name is not a family", diag);
        }
        if (total > (size_t)-1 - length) {
            return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                                 "the family list is absurdly long");
        }
        total += length;
        families++;
        expectingPart = 0;
    }
    if (families == 0u || expectingPart) {
        return ComputedReject(out, RoboCssPropertyComputationFontFamily,
                               "not a family list", diag);
    }

    /* `, ` between families, so the value is a list rather than a coincidence of
     * token order. */
    joined = (char *)Malloc(total + (families - 1u) * 2u + 1u);
    if (!joined) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                             "could not build the family list");
    }
    for (index = 0u; index < specified->children.count; index++) {
        const CssValue *child = &specified->children.items[index];
        size_t length = 0u;
        uint8_t scratch[256];
        uint8_t *bytes = scratch;

        if (CssValueIsTrivia(child) || child->token.type == RoboCssTokenComma) {
            if (child->kind == RoboCssValueKindToken &&
                child->token.type == RoboCssTokenComma) {
                joined[position] = ',';
                joined[position + 1u] = ' ';
                position += 2u;
            }
            continue;
        }
        length = CssTokenValueLength(&child->token);
        if (length > sizeof(scratch)) {
            /* The first pass proved a family this long exists; a list long
             * enough to need the heap is rare, so it is handled rather than
             * rejected, but the stack path is the one that runs. */
            bytes = (uint8_t *)Malloc(length);
            if (!bytes) {
                RoboFree(joined);
                return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                                     "could not build the family list");
            }
        }
        /* The *decoded* value, so `\31 a` and `1a` are the same family. */
        (void)CssTokenValueCopy(&child->token, bytes, length, &length, NULL);
        memcpy(joined + position, bytes, length);
        position += length;
        if (bytes != scratch) {
            RoboFree(bytes);
        }
    }
    joined[position] = '\0';
    result = CssComputedValueMakeFamilyList(out, CssSliceFromText(joined),
                                                      diag);
    RoboFree(joined);
    return result;
}

/** Compute `normal`/`bold`/…/100–900. */
static Result ComputedComputeFontWeight(CssComputedValue *out,
                                                  const CssValue *specified,
                                                  CssDiag *diag)
{
    const CssToken *token = ComputedSingleToken(specified);

    if (!token) {
        return ComputedReject(out, RoboCssPropertyComputationFontWeight,
                               "not a font weight", diag);
    }
    if (token->type == RoboCssTokenNumber) {
        if (!CssNumberIsFinite(token->number) || token->number < 1.0 ||
            token->number > 1000.0) {
            return ComputedReject(out, RoboCssPropertyComputationFontWeight,
                                   "a font weight number must be 1 to 1000", diag);
        }
        return CssComputedValueMakeNumber(out, token->number,
                                                   token->isInteger, diag);
    }
    if (token->type == RoboCssTokenIdent) {
        static const char *const weights[] = { "normal", "bold", "bolder", "lighter" };
        size_t index;

        for (index = 0u; index < sizeof(weights) / sizeof(weights[0]); index++) {
            if (CssTokenValueEqualsAsciiCi(token, weights[index])) {
                return CssComputedValueMakeKeyword(
                    out, CssSliceFromText(weights[index]), diag);
            }
        }
    }
    return ComputedReject(out, RoboCssPropertyComputationFontWeight,
                           "not a font weight", diag);
}

/** Compute `normal`, a number, a length or a percentage. */
static Result ComputedComputeLineHeight(CssComputedValue *out,
                                                  const CssValue *specified,
                                                  CssDiag *diag)
{
    const CssToken *token = ComputedSingleToken(specified);

    if (token && token->type == RoboCssTokenIdent &&
        CssTokenValueEqualsAsciiCi(token, "normal")) {
        return CssComputedValueMakeKeyword(out, CssSliceFromText("normal"),
                                                    diag);
    }
    if (token && token->type == RoboCssTokenNumber) {
        if (!CssNumberIsFinite(token->number)) {
            return ComputedReject(out, RoboCssPropertyComputationLineHeight,
                                   "not a finite number", diag);
        }
        /* A bare number is a *multiplier* here, not a length. */
        return CssComputedValueMakeNumber(out, token->number,
                                                   token->isInteger, diag);
    }
    return ComputedComputeLength(out, specified, diag);
}

/**
 * Compute a flex factor: a `<number>`, and it must not be negative (18.3).
 *
 * `flex-grow` and `flex-shrink` share this body because they share the rule, and the
 * rule is where they differ from every other number in this build: a factor has no
 * range, so unlike `opacity` there is nothing to clamp, and a *negative* one is a
 * mistake CSS refuses rather than a value that quietly means zero. Refusing it is
 * what keeps a layout pass from distributing a negative share of the free space.
 *
 * An integer is a number here, as it is for `opacity`: `flex-grow: 2` and
 * `flex-grow: 2.0` are the same value, and the spelling is not part of it. A
 * percentage is not a factor, and neither is a length; both get the one sentence the
 * shape owns. `calc()` and `var()` are carried with the unresolved flag, as always.
 */
static Result ComputedComputeFlexFactor(CssComputedValue *out,
                                                  const CssValue *specified,
                                                  CssDiag *diag)
{
    static const char *const reasonNotFactor = "not a non-negative number";
    CssPropertyComputation computation = RoboCssPropertyComputationFlexFactor;
    CssNumeric numeric = CssNumericNone();

    switch (CssNumericFromValue(specified, &numeric)) {
    case RoboCssNumericParseResolved:
        break;
    case RoboCssNumericParseDeferred:
        return ComputedKeep(out, specified, 1, diag);
    case RoboCssNumericParseNonFinite:
        return ComputedReject(out, computation, "not a finite number", diag);
    default:
        return ComputedReject(out, computation, reasonNotFactor, diag);
    }

    if (numeric.kind != RoboCssNumericInteger &&
        numeric.kind != RoboCssNumericNumber) {
        return ComputedReject(out, computation, reasonNotFactor, diag);
    }
    if (!CssFlexFactorIsValid(numeric.number)) {
        return ComputedReject(out, computation,
                               "a flex factor cannot be negative", diag);
    }
    return CssComputedValueMakeNumber(out, numeric.number,
                                                 numeric.kind == RoboCssNumericInteger,
                                                 diag);
}

/**
 * Compute an `order`: a whole number, and it may be negative (18.3).
 *
 * The same `<integer>` grammar `z-index` reads, and the same two refusals, with one
 * difference that is the property's whole point: an `order` may be negative, since the
 * items before the default are what a negative order is *for*. There is no `auto` —
 * `order`'s initial value is `0` — so an identifier is simply not this shape, which is
 * a different sentence from the one a number of the wrong kind gets.
 */
/**
 * Compute a `grid-auto-flow`: one or two keywords from a closed set (18.4).
 *
 * The reading lives in the grid module, because it is the one place that knows the
 * value is two words and that `dense` alone is `row dense`; a second copy here is how
 * `grid-auto-flow: row dense` and `grid-auto-flow: dense row` would come to disagree
 * about what they are. What this layer decides is only the outcome: a classified flow is
 * a typed value, a reference is carried with its tokens and the flag, and anything else
 * is an INVALID answer rather than a failure code.
 */
static Result ComputedComputeGridAutoFlow(CssComputedValue *out,
                                                     const CssValue *specified,
                                                     CssDiag *diag)
{
    CssGridAutoFlow flow = RoboCssGridAutoFlowRow;

    switch (CssGridAutoFlowFromValue(specified, &flow)) {
    case RoboCssGridParseFlow:
        return CssComputedValueMakeGridAutoFlow(out, flow, diag);
    case RoboCssGridParseDeferred:
        /* `var()`: legal CSS this build does not evaluate, carried with the flag so a
         * consumer cannot read the tokens as a flow. */
        return ComputedKeep(out, specified, 1, diag);
    default:
        return ComputedReject(out, RoboCssPropertyComputationGridAutoFlow,
                               "not a grid-auto-flow value", diag);
    }
}

static Result ComputedComputeOrder(CssComputedValue *out,
                                             const CssValue *specified,
                                             CssDiag *diag)
{
    static const char *const reasonNotInteger = "not a whole number";
    CssPropertyComputation computation = RoboCssPropertyComputationOrder;
    CssNumeric numeric = CssNumericNone();
    int64_t integer = 0;

    switch (CssNumericFromValue(specified, &numeric)) {
    case RoboCssNumericParseResolved:
        break;
    case RoboCssNumericParseDeferred:
        return ComputedKeep(out, specified, 1, diag);
    case RoboCssNumericParseNonFinite:
        return ComputedReject(out, computation, "not a finite number", diag);
    default:
        return ComputedReject(out, computation, reasonNotInteger, diag);
    }

    if (numeric.kind != RoboCssNumericInteger) {
        const char *reason = reasonNotInteger;

        if (numeric.kind == RoboCssNumericNumber) {
            /* `1.0` and `1e2` are whole values and are still not integers: the
             * tokenizer's integer flag is a fact about the *spelling*, exactly as it
             * is for `z-index`. */
            reason = CssNumericIsIntegral(numeric)
                         ? "a whole number must be written as an integer"
                         : "not a whole number";
        }
        return ComputedReject(out, computation, reason, diag);
    }
    if (!CssNumericIntegerValue(numeric, &integer)) {
        return ComputedReject(out, computation, "not a whole number", diag);
    }
    return CssComputedValueMakeNumber(out, (double)integer, 1, diag);
}

/**
 * Compute a `column-count`: `auto`, or a positive whole number (18.6).
 *
 * A shape of its own rather than the NUMBER family, and the difference is the bound
 * rather than the syntax: a count of zero columns is not a smaller count, it is not a
 * column count at all, so `0` and a negative or fractional number are mistakes rather
 * than values to clamp. `auto` is the initial value and means "as many as fit", which
 * is a different statement from one. The keyword is asked first, the way the length
 * grammar asks its own, so `column-count: auto` is a value and `column-count: banana`
 * is told it is not one.
 */
static Result ComputedComputeColumnCount(CssComputedValue *out,
                                         const CssValue *specified,
                                         CssDiag *diag)
{
    static const char *const reason = "not auto or a positive whole number";
    CssPropertyComputation computation = RoboCssPropertyComputationColumnCount;
    const CssToken *token = ComputedSingleToken(specified);
    CssNumeric numeric = CssNumericNone();
    int64_t integer = 0;

    if (token && token->type == RoboCssTokenIdent) {
        char keyword[64];
        size_t length = 0u;

        if (!CssTokenKeywordText(token, keyword, sizeof(keyword), &length) ||
            !CssBytesEqualAsciiCi((const uint8_t *)keyword, length, "auto")) {
            return ComputedReject(out, computation, reason, diag);
        }
        return CssComputedValueMakeKeyword(
            out, CssSliceFromBytes((const uint8_t *)keyword, length), diag);
    }

    switch (CssNumericFromValue(specified, &numeric)) {
    case RoboCssNumericParseResolved:
        break;
    case RoboCssNumericParseDeferred:
        return ComputedKeep(out, specified, 1, diag);
    case RoboCssNumericParseNonFinite:
        return ComputedReject(out, computation, "not a finite number", diag);
    default:
        return ComputedReject(out, computation, reason, diag);
    }

    if (numeric.kind != RoboCssNumericInteger ||
        !CssNumericIntegerValue(numeric, &integer)) {
        return ComputedReject(out, computation,
                              "a column count must be a whole number", diag);
    }
    if (integer < 1) {
        return ComputedReject(out, computation,
                              "a column count must be at least one", diag);
    }
    return CssComputedValueMakeNumber(out, (double)integer, 1, diag);
}

/**
 * Compute an `orphans` or a `widows`: a positive whole number (19.7).
 *
 * One body for two properties, because their grammar *is* the same one -- a positive
 * `<integer>` with the initial value 2 -- and the only thing that differs is the reason
 * a refusal prints, which is the computation the caller handed in. That is the opposite
 * decision from the two `break-*` rows above, and deliberately: those share a
 * *vocabulary* (order-independent, no bound) while these share a *shape*, and a shared
 * shape is one function where a shared vocabulary is one table.
 *
 * The floor is 1 rather than 0, for `column-count`'s reason with a different argument:
 * "keep no lines" is not a smaller floor, it is the absence of one, and CSS says the
 * property must be positive. A fraction is refused outright rather than rounded, because
 * a line is a countable thing.
 */
static Result ComputedComputeLineFloor(CssComputedValue *out,
                                       const CssValue *specified,
                                       CssPropertyComputation computation,
                                       const char *reason,
                                       CssDiag *diag)
{
    CssNumeric numeric = CssNumericNone();
    int64_t integer = 0;

    switch (CssNumericFromValue(specified, &numeric)) {
    case RoboCssNumericParseResolved:
        break;
    case RoboCssNumericParseDeferred:
        return ComputedKeep(out, specified, 1, diag);
    case RoboCssNumericParseNonFinite:
        return ComputedReject(out, computation, "not a finite number", diag);
    default:
        return ComputedReject(out, computation, reason, diag);
    }

    if (numeric.kind != RoboCssNumericInteger ||
        !CssNumericIntegerValue(numeric, &integer)) {
        return ComputedReject(out, computation,
                              "a line floor must be a whole number", diag);
    }
    if (integer < 1) {
        return ComputedReject(out, computation,
                              "a line floor must be at least one", diag);
    }
    return CssComputedValueMakeNumber(out, (double)integer, 1, diag);
}

/**
 * Compute an `animation-iteration-count`: `infinite`, or a non-negative number (18.9).
 *
 * A shape of its own because of the two things at once: the keyword `infinite` is not
 * a number, and the bound is a *floor* rather than CSS's usual whole-number rule — a
 * half iteration is meaningful, so a fraction is a value and only a negative count is
 * a mistake. `infinite` arrives as the keyword it is (the NUMBER kind has no
 * infinity), which is why the two halves of this shape are different kinds and why
 * the keyword is asked first.
 */
static Result ComputedComputeAnimationIterationCount(CssComputedValue *out,
                                                     const CssValue *specified,
                                                     CssDiag *diag)
{
    static const char *const reason =
        "not infinite or a non-negative number";
    CssPropertyComputation computation =
        RoboCssPropertyComputationAnimationIterationCount;
    const CssToken *token = ComputedSingleToken(specified);
    CssNumeric numeric = CssNumericNone();

    if (token && token->type == RoboCssTokenIdent) {
        char keyword[64];
        size_t length = 0u;

        if (!CssTokenKeywordText(token, keyword, sizeof(keyword), &length) ||
            !CssBytesEqualAsciiCi((const uint8_t *)keyword, length, "infinite")) {
            return ComputedReject(out, computation, reason, diag);
        }
        return CssComputedValueMakeKeyword(
            out, CssSliceFromBytes((const uint8_t *)keyword, length), diag);
    }

    switch (CssNumericFromValue(specified, &numeric)) {
    case RoboCssNumericParseResolved:
        break;
    case RoboCssNumericParseDeferred:
        return ComputedKeep(out, specified, 1, diag);
    case RoboCssNumericParseNonFinite:
        return ComputedReject(out, computation, "not a finite number", diag);
    default:
        return ComputedReject(out, computation, reason, diag);
    }

    if (numeric.kind != RoboCssNumericNumber &&
        numeric.kind != RoboCssNumericInteger) {
        return ComputedReject(out, computation, reason, diag);
    }
    if (numeric.number < 0.0) {
        return ComputedReject(out, computation,
                              "an iteration count cannot be negative", diag);
    }
    return CssComputedValueMakeNumber(out, numeric.number, 0, diag);
}

Result CssComputedValueCompute(
    CssPropertyComputation computation,
    const CssValue *specified,
    CssComputedValue *out,
    CssDiag *diag)
{
    if (!out || !specified) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "output or specified value is NULL");
    }
    CssComputedValueInit(out);
    if (!CssPropertyComputationIsKnown(computation)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "not a value-computation classification");
    }
    if (specified->kind == RoboCssValueKindNone) {
        /* Nothing was specified: NONE, and not an invalid value. The entry this
         * belongs to carries the "unresolved default" fact, not the value. */
        return RoboOk;
    }

    switch (computation) {
    case RoboCssPropertyComputationColor:
        return ComputedComputeColor(out, specified, diag);
    case RoboCssPropertyComputationLength:
        return ComputedComputeLength(out, specified, diag);
    case RoboCssPropertyComputationKeyword:
        return ComputedComputeKeyword(out, specified, diag);
    case RoboCssPropertyComputationFontFamily:
        return ComputedComputeFamily(out, specified, diag);
    case RoboCssPropertyComputationFontWeight:
        return ComputedComputeFontWeight(out, specified, diag);
    case RoboCssPropertyComputationLineHeight:
        return ComputedComputeLineHeight(out, specified, diag);
    /* The box model's three length families (9.11) share the length body and
     * differ only in the vocabulary their grammar carries, so they are one case
     * body rather than three — the difference is data, not code. */
    case RoboCssPropertyComputationEdgeOffset:
    case RoboCssPropertyComputationEdgeInset:
    case RoboCssPropertyComputationLineWidth:
    /* `flex-basis` (18.3) joins them: its value is a length, a percentage or one of
     * four keywords, so it is the same body with its own keyword list rather than a
     * shape of its own. */
    case RoboCssPropertyComputationFlexBasis:
    /* And the grid gutters (18.4), which are the same body with the keyword `normal`. */
    case RoboCssPropertyComputationGridGap:
    /* And `column-width` (18.6), which is the same body with the keyword `auto`. */
    case RoboCssPropertyComputationColumnWidth:
        return ComputedComputeLengthGrammar(
            ComputedLengthGrammarFor(computation), out, specified, diag);
    /* The two closed vocabularies (9.12, 9.13) are also one case body for the
     * same reason: the vocabulary, its two doors and its refusals are data in a
     * row, so a third closed vocabulary is a row rather than a third copy of
     * "ask the identifier, then ask the table". */
    case RoboCssPropertyComputationDisplay:
    case RoboCssPropertyComputationPosition:
        return ComputedComputeVocabulary(
            ComputedVocabularyGrammarFor(computation), out, specified, diag);
    /* The two further closed vocabularies (9.14) are rows in the same table as
     * the two above, which is what 9.13's shared body was for. */
    case RoboCssPropertyComputationBackgroundRepeat:
    case RoboCssPropertyComputationBackgroundAttachment:
    /* And the border style vocabulary (9.15) is a row in the same table for the
     * same reason, which is now the fifth vocabulary to arrive that way. */
    case RoboCssPropertyComputationBorderStyle:
    /*
     * And the overflow vocabulary (10.13) is the sixth, and the first whose row was
     * added by a *layout* phase rather than by one of the 9.x value phases: what
     * RoboLayout needed was not a new computation but a typed value it could classify,
     * so the vocabulary table gained a row and this switch gained the case that reads
     * it. Nothing else about the normaliser changed, which is the whole point of the
     * table.
     */
    case RoboCssPropertyComputationOverflow:
    /*
     * The flexbox vocabularies (18.3) are six more rows in the same table, and the
     * alignment family is six rather than two for a reason the table states: the
     * vocabulary is one, but which values each of the four properties may hold is the
     * property's answer, and only the row's predicate knows it.
     */
    case RoboCssPropertyComputationFlexDirection:
    case RoboCssPropertyComputationFlexWrap:
    case RoboCssPropertyComputationJustifyContent:
    case RoboCssPropertyComputationAlignItems:
    case RoboCssPropertyComputationAlignSelf:
    case RoboCssPropertyComputationAlignContent:
    /*
     * The table vocabularies (18.5) are five more rows in the same table, and they
     * are the first whose values are *logical* (`caption-side: block-start`) -- which
     * changes nothing here, because the row still only asks "is this identifier one of
     * ours" and the axis classification lives on the vocabulary.
     */
    case RoboCssPropertyComputationTableLayout:
    case RoboCssPropertyComputationBorderCollapse:
    case RoboCssPropertyComputationCaptionSide:
    case RoboCssPropertyComputationEmptyCells:
    case RoboCssPropertyComputationVisibility:
    /* And the multi-column vocabularies (18.6) are two more rows. */
    case RoboCssPropertyComputationColumnSpan:
    case RoboCssPropertyComputationColumnFill:
    /*
     * And the fragmentation vocabularies (19.7) are three more rows over two
     * vocabularies. They arrive here for the reason the overflow row did: what a
     * fragmentation pass needs is not a new computation but a *typed* value it can
     * classify -- forced, avoided, and which container honours the break -- so the row
     * carries the keyword and RoboCSSFragment.h's table owns the classification.
     */
    case RoboCssPropertyComputationBreakBefore:
    case RoboCssPropertyComputationBreakAfter:
    case RoboCssPropertyComputationBreakInside:
    /* And the writing-mode vocabularies (18.13) are three more rows -- the writing
     * mode is data whose value *is* its keyword here; turning it into a run or a line
     * is RoboText's, and that is a reader this table does not own. */
    case RoboCssPropertyComputationDirection:
    case RoboCssPropertyComputationWritingMode:
    case RoboCssPropertyComputationTextOrientation:
    /* And the transform vocabularies (18.7) are two more rows. */
    case RoboCssPropertyComputationTransformStyle:
    case RoboCssPropertyComputationBackfaceVisibility:
    /* And the transition vocabulary (18.8) is one more row. */
    case RoboCssPropertyComputationTransitionBehavior:
    /* And the animation vocabularies (18.9) are four more rows. */
    case RoboCssPropertyComputationAnimationDirection:
    case RoboCssPropertyComputationAnimationFillMode:
    case RoboCssPropertyComputationAnimationPlayState:
    case RoboCssPropertyComputationAnimationComposition:
    /* And the container-query vocabulary (18.11) is one more row. */
    case RoboCssPropertyComputationContainerType:
    /*
     * And the font vocabularies (18.14) are six more rows — the half of the font
     * family whose grammar *is* a closed set. The other half is not here at all: a
     * `font-stretch` percentage, a `font-variant-numeric` axis set and a
     * `font-feature-settings` tag list are PRESERVED declarations, which is a
     * different *row* in a different table rather than another case in this switch.
     */
    case RoboCssPropertyComputationFontStyle:
    case RoboCssPropertyComputationFontKerning:
    case RoboCssPropertyComputationFontOpticalSizing:
    case RoboCssPropertyComputationFontSynthesis:
    case RoboCssPropertyComputationFontVariantCaps:
    case RoboCssPropertyComputationFontVariantPosition:
    /*
     * And the text vocabularies (18.15) are ten more rows — the half of the text
     * family whose grammar *is* a closed set. The other half is not here at all: a
     * `text-decoration-line` set, a `text-overflow` string and a
     * `text-decoration-thickness` length are PRESERVED declarations, and their own
     * closed sets are reached through RoboCSSText.h rather than through this switch.
     */
    case RoboCssPropertyComputationTextAlign:
    case RoboCssPropertyComputationTextAlignLast:
    case RoboCssPropertyComputationTextJustify:
    case RoboCssPropertyComputationTextTransform:
    case RoboCssPropertyComputationWhiteSpace:
    case RoboCssPropertyComputationWordBreak:
    case RoboCssPropertyComputationOverflowWrap:
    case RoboCssPropertyComputationLineBreak:
    case RoboCssPropertyComputationHyphens:
    case RoboCssPropertyComputationTextDecorationStyle:
    /*
     * And the image vocabularies (18.16) are two more rows -- the half of the image
     * family whose grammar *is* a closed set. The other half is not here at all: an
     * `image-orientation` angle, an `image-resolution` resolution and an
     * `object-position` pair are PRESERVED declarations, and their own closed sets are
     * reached through RoboCSSImage.h rather than through this switch.
     */
    case RoboCssPropertyComputationImageRendering:
    case RoboCssPropertyComputationObjectFit:
    /*
     * And 18.17's two rows, the background family's closed vocabularies.
     */
    case RoboCssPropertyComputationBackgroundBox:
    case RoboCssPropertyComputationBlendMode:
    /* And 18.18's row, the border family's closed set. */
    case RoboCssPropertyComputationBorderImageRepeat:
        return ComputedComputeVocabulary(
            ComputedVocabularyGrammarFor(computation), out, specified, diag);
    case RoboCssPropertyComputationBackgroundImage:
        return ComputedComputeBackgroundImage(out, specified, diag);
    case RoboCssPropertyComputationBackgroundPosition:
        return ComputedComputeBackgroundPosition(out, specified, diag);
    case RoboCssPropertyComputationBackgroundSize:
        return ComputedComputeBackgroundSize(out, specified, diag);
    case RoboCssPropertyComputationZIndex:
        return ComputedComputeZIndex(out, specified, diag);
    /*
     * And the opacity property (11.9), which is the first row here added by a phase of the
     * *paint* stack -- and the first shape in this switch that is not a closed vocabulary: an
     * α-value is every real in a range, so its computation is arithmetic and its refusals are
     * about the kind of number rather than about a spelling.
     */
    case RoboCssPropertyComputationOpacity:
        return ComputedComputeOpacity(out, specified, diag);
    /*
     * And the two numeric shapes flexbox adds (18.3): a flex factor, which is a
     * `<number>` with a lower bound of zero, and `order`, which is an `<integer>` and
     * may be negative. They are two shapes rather than one because theirs are two
     * grammars -- `flex-grow: 2.5` is legal and `order: 2.5` is not -- and because the
     * bound is on the factor and not on the order.
     */
    case RoboCssPropertyComputationFlexFactor:
        return ComputedComputeFlexFactor(out, specified, diag);
    case RoboCssPropertyComputationOrder:
        return ComputedComputeOrder(out, specified, diag);
    /*
     * And the one typed shape grid adds (18.4): `grid-auto-flow`, which is like the
     * vocabularies above in that it is a closed set but unlike them in that three of its
     * four values are two words, so it reads its component value rather than one
     * identifier.
     */
    case RoboCssPropertyComputationGridAutoFlow:
        return ComputedComputeGridAutoFlow(out, specified, diag);
    /*
     * And the one numeric shape multi-column adds (18.6): `column-count`, which is
     * `auto` or a positive whole number, so its bound (at least one) is the reason it
     * is a shape of its own rather than a bare number.
     */
    case RoboCssPropertyComputationColumnCount:
        return ComputedComputeColumnCount(out, specified, diag);
    /*
     * And the two line floors fragmentation adds (19.7): `orphans` and `widows`, which are
     * the same positive-integer shape with two different initial values' worth of meaning.
     * They are numeric rows rather than vocabularies, which is why they arrive in *this*
     * switch rather than beside the three `break-*` rows above.
     */
    case RoboCssPropertyComputationOrphans:
        return ComputedComputeLineFloor(out, specified,
                                       RoboCssPropertyComputationOrphans,
                                       "not a positive whole number", diag);
    case RoboCssPropertyComputationWidows:
        return ComputedComputeLineFloor(out, specified,
                                       RoboCssPropertyComputationWidows,
                                       "not a positive whole number", diag);
    /*
     * And the one numeric shape animations add (18.9): `animation-iteration-count`,
     * which is `infinite` or a non-negative number, so its floor (a half iteration is
     * meaningful) is the reason it is a shape of its own.
     */
    case RoboCssPropertyComputationAnimationIterationCount:
        return ComputedComputeAnimationIterationCount(out, specified, diag);
    default:
        return ComputedKeep(out, specified, ComputedFunctionIsUnresolved(
                                             CssValueSingle(specified)),
                             diag);
    }
}
