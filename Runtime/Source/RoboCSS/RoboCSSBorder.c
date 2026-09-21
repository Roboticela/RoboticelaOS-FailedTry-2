/*
 * RoboCSS border family (Phase 9.15) — see RoboCSSBorder.h.
 *
 * Two tables and one classifier, and everything below reads them:
 *
 *   the style rows        one per value, carrying the one fact a later phase asks
 *   the kind rows         width / style / colour, each naming the box-table row
 *                         whose longhands it owns
 *
 * The classifier is what makes the composite shorthands a third machine beside
 * the box model's positional mapping and `background`'s per-longhand one: a box
 * shorthand's components map to sides *by position*, `background`'s components map
 * to six longhands *by kind*, and a border's components map to *three* kinds whose
 * longhand is then chosen by side as well. Nothing here weighs a level or compares
 * a specificity — which of a shorthand and a longhand wins is the cascade's answer,
 * asked during resolution, not this module's.
 *
 * The twelve longhand names are not spelled here. They are read out of the box
 * table's positional rows, so `border-top-width` appears once in the build; this
 * module spells the three *shorthand* names and pairs each with the kind it
 * carries, and a walk that looks every one of them up is what makes a drift
 * between the two tables visible rather than silent.
 */

#include "RoboCSS/RoboCSSBorder.h"

#include "RoboCSS/RoboCSSColor.h"
#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The style vocabulary                                                */
/* ------------------------------------------------------------------ */

/*
 * `generates` is the value, not a description of it: `none` and `hidden` *are*
 * "this border draws nothing", which is why the question is asked of the row
 * rather than written as `style != NONE && style != HIDDEN` at each call site —
 * a comparison like that has to be revisited by every phase that adds a style,
 * and it is the kind of statement that reads as an optimisation until the day a
 * new value is neither of the two named.
 */
typedef struct borderStyleRow {
    CssBorderStyle value;
    const char *keyword;
    int generates;
} BorderStyleRow;

static const BorderStyleRow borderStyleVocabulary[] = {
    { RoboCssBorderStyleNone,   "none",   0 },
    { RoboCssBorderStyleHidden, "hidden", 0 },
    { RoboCssBorderStyleDotted, "dotted", 1 },
    { RoboCssBorderStyleDashed, "dashed", 1 },
    { RoboCssBorderStyleSolid,  "solid",  1 },
    { RoboCssBorderStyleDouble, "double", 1 },
    { RoboCssBorderStyleGroove, "groove", 1 },
    { RoboCssBorderStyleRidge,  "ridge",  1 },
    { RoboCssBorderStyleInset,  "inset",  1 },
    { RoboCssBorderStyleOutset, "outset", 1 }
};

#define BORDER_STYLE_VOCABULARY_COUNT \
    (sizeof(borderStyleVocabulary) / sizeof(borderStyleVocabulary[0]))

/** The row for @p style, or NULL when it is not one of them. */
static const BorderStyleRow *BorderStyleRowOf(CssBorderStyle style)
{
    size_t index;

    for (index = 0u; index < BORDER_STYLE_VOCABULARY_COUNT; index++) {
        if (borderStyleVocabulary[index].value == style) {
            return &borderStyleVocabulary[index];
        }
    }
    return NULL;
}

int CssBorderStyleIsKnown(CssBorderStyle style)
{
    return BorderStyleRowOf(style) != NULL;
}

const char *CssBorderStyleName(CssBorderStyle style)
{
    const BorderStyleRow *row = BorderStyleRowOf(style);

    return row ? row->keyword : "unknown";
}

size_t CssBorderStyleCount(void)
{
    return BORDER_STYLE_VOCABULARY_COUNT;
}

CssBorderStyle CssBorderStyleFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword)) {
        return RoboCssBorderStyleNone;
    }
    for (index = 0u; index < BORDER_STYLE_VOCABULARY_COUNT; index++) {
        const char *candidate = borderStyleVocabulary[index].keyword;

        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length, candidate)) {
            return borderStyleVocabulary[index].value;
        }
    }
    /* `none` is both a value and this function's answer for "not one of them",
     * which is why every *check* asks CssBorderStyleSliceIsKnown. */
    return RoboCssBorderStyleNone;
}

CssBorderStyle CssBorderStyleFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssBorderStyleNone;
    }
    return CssBorderStyleFromSlice(CssSliceFromText(keyword));
}

int CssBorderStyleSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword)) {
        return 0;
    }
    for (index = 0u; index < BORDER_STYLE_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                          borderStyleVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

Result CssBorderStyleWrite(CssBorderStyle style,
                                          char *out,
                                          size_t capacity,
                                          size_t *outLength,
                                          CssDiag *diag)
{
    const BorderStyleRow *row = BorderStyleRowOf(style);
    size_t length;

    if (!out || !outLength || !row) {
        return RoboCssDiag(diag, RoboCssDomainBorder, RoboInvalidArgument,
                             "output is NULL or not a border style");
    }
    length = strlen(row->keyword);
    if (capacity < length + 1u) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainBorder, RoboNotSupported,
                             "the border style keyword does not fit");
    }
    memcpy(out, row->keyword, length + 1u);
    *outLength = length;
    return RoboOk;
}

int CssBorderStyleGeneratesBorder(CssBorderStyle style)
{
    const BorderStyleRow *row = BorderStyleRowOf(style);

    return row ? row->generates : 0;
}

/* ------------------------------------------------------------------ */
/* The kinds                                                           */
/* ------------------------------------------------------------------ */

/*
 * The rows, and the whole of the module's spelling of a shorthand name.
 *
 * `index == kind` by construction, which is the point: `border-width` *is* the
 * width row, and a second table relating the two would be a place for them to
 * disagree.
 */
typedef struct borderKindRow {
    CssBorderKind kind;
    const char *shorthand;
} BorderKindRow;

static const BorderKindRow borderKindVocabulary[RoboCssBorderKindCount] = {
    { RoboCssBorderKindWidth, "border-width" },
    { RoboCssBorderKindStyle, "border-style" },
    { RoboCssBorderKindColor, "border-color" }
};

static const char *const borderKindNames[RoboCssBorderKindCount] = {
    "width", "style", "color"
};

static const BorderKindRow *BorderKindRowOf(CssBorderKind kind)
{
    size_t index;

    for (index = 0u; index < RoboCssBorderKindCount; index++) {
        if (borderKindVocabulary[index].kind == kind) {
            return &borderKindVocabulary[index];
        }
    }
    return NULL;
}

int CssBorderKindIsKnown(CssBorderKind kind)
{
    return BorderKindRowOf(kind) != NULL;
}

const char *CssBorderKindName(CssBorderKind kind)
{
    const BorderKindRow *row = BorderKindRowOf(kind);

    return row ? borderKindNames[(size_t)row->kind] : "unknown";
}

size_t CssBorderKindCount(void)
{
    return RoboCssBorderKindCount;
}

size_t CssBorderLonghandCount(void)
{
    return RoboCssBorderLonghandCount;
}

const CssBoxShorthand *CssBorderPositionalRowOfKind(
    CssBorderKind kind)
{
    const BorderKindRow *row = BorderKindRowOf(kind);

    if (!row) {
        return NULL;
    }
    /* Asked rather than restated: the box table is where the four longhands of a
     * positional shorthand are written down, and it is asked for them by name so
     * this module cannot hold a stale copy. */
    return CssBoxShorthandFind(CssSliceFromText(row->shorthand));
}

const char *CssBorderLonghandName(CssBoxSide side,
                                          CssBorderKind kind)
{
    if (!CssBoxSideIsKnown(side)) {
        return NULL;
    }
    return CssBoxShorthandLonghand(CssBorderPositionalRowOfKind(kind),
                                           side);
}

int CssBorderIsLonghand(CssSlice name)
{
    const CssBoxShorthand *owner;

    owner = CssBoxShorthandOfLonghand(name, NULL);
    if (!owner) {
        return 0;
    }
    /* A longhand is a *border* longhand when the row that owns it is one of this
     * module's three — which is the same question `distributes_shorthand` asks,
     * asked from the other side, so the two cannot disagree about the family's
     * scope. */
    return CssBorderDistributesShorthand(
        CssSliceFromText(owner->name));
}

/* ------------------------------------------------------------------ */
/* The positional shorthands                                           */
/* ------------------------------------------------------------------ */

size_t CssBorderPositionalCount(void)
{
    return RoboCssBorderPositionalCount;
}

const char *CssBorderPositionalName(size_t index)
{
    return index < RoboCssBorderKindCount
               ? borderKindVocabulary[index].shorthand
               : NULL;
}

CssBorderKind CssBorderPositionalKind(size_t index)
{
    return index < RoboCssBorderKindCount
               ? borderKindVocabulary[index].kind
               : RoboCssBorderKindWidth;
}

const CssBoxShorthand *CssBorderPositionalRow(size_t index)
{
    if (index >= RoboCssBorderKindCount) {
        return NULL;
    }
    return CssBorderPositionalRowOfKind(borderKindVocabulary[index].kind);
}

int CssBorderDistributesShorthand(CssSlice name)
{
    size_t index;

    if (CssSliceIsAbsent(name)) {
        return 0;
    }
    for (index = 0u; index < RoboCssBorderKindCount; index++) {
        if (CssBytesEqualAsciiCi(
                name.bytes, name.length, borderKindVocabulary[index].shorthand)) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The composite shorthands                                            */
/* ------------------------------------------------------------------ */

/*
 * Index 0 is `border` and 1..4 are the sides in vocabulary order, so the side a
 * row covers is `index - 1` — derived rather than kept in a second parallel array
 * a walk could fall out of step with.
 */
static const char *const borderCompositeVocabulary[RoboCssBorderCompositeCount] = {
    "border", "border-top", "border-right", "border-bottom", "border-left"
};

size_t CssBorderCompositeCount(void)
{
    return RoboCssBorderCompositeCount;
}

const char *CssBorderCompositeName(size_t index)
{
    return index < RoboCssBorderCompositeCount
               ? borderCompositeVocabulary[index]
               : NULL;
}

CssBoxSide CssBorderCompositeSide(size_t index)
{
    if (index == 0u || index >= RoboCssBorderCompositeCount) {
        return RoboCssBoxSideTop;
    }
    return (CssBoxSide)(index - 1u);
}

int CssBorderCompositeIsWhole(size_t index)
{
    return index == 0u;
}

int CssBorderIsComposite(CssSlice name)
{
    size_t index;

    if (CssSliceIsAbsent(name)) {
        return 0;
    }
    for (index = 0u; index < RoboCssBorderCompositeCount; index++) {
        if (CssBytesEqualAsciiCi(
                name.bytes, name.length, borderCompositeVocabulary[index])) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The classifier                                                      */
/* ------------------------------------------------------------------ */

/* The two readers the classifier needs before the section that owns them. */
static const CssValue *BorderComponentAt(const CssValue *value,
                                                   size_t wanted);
static size_t BorderComponentCount(const CssValue *value);

/* Five rows, and the third and fourth are what keep this honest: DEFERRED is
 * "legal CSS whose answer is not written yet" and INVALID is "this means nothing
 * here", and an author who wrote `var(--w)` needs a different sentence from one
 * who wrote `10%`. */
static const char *const borderClassNames[] = {
    "invalid", "width", "style", "color", "deferred"
};

/** Number of classification outcomes. */
#define BORDER_CLASS_COUNT \
    (sizeof(borderClassNames) / sizeof(borderClassNames[0]))

int CssBorderClassIsKnown(CssBorderClass outcome)
{
    return outcome >= RoboCssBorderClassInvalid &&
           (size_t)outcome < BORDER_CLASS_COUNT;
}

const char *CssBorderClassName(CssBorderClass outcome)
{
    return CssBorderClassIsKnown(outcome)
               ? borderClassNames[(size_t)outcome]
               : "unknown";
}

size_t CssBorderClassCount(void)
{
    return BORDER_CLASS_COUNT;
}

/** Non-zero when @p keyword is `thin`, `medium` or `thick`. */
static int BorderKeywordIsLineWidth(const char *keyword, size_t length)
{
    return CssBytesEqualAsciiCi((const uint8_t *)keyword, length, "thin") ||
           CssBytesEqualAsciiCi((const uint8_t *)keyword, length, "medium") ||
           CssBytesEqualAsciiCi((const uint8_t *)keyword, length, "thick");
}

/** Non-zero when @p value is a `var()` or `env()`, which substitution decides. */
static int BorderComponentIsSubstitution(const CssValue *value)
{
    return CssValueFunctionNamed(value, "var") ||
           CssValueFunctionNamed(value, "env");
}

/**
 * Which kind @p component belongs to, with @p outReason naming the refusal.
 *
 * The *order* of the questions is the whole of the logic, and the vocabulary
 * forces it. A style keyword first, because `none` and `hidden` are styles and are
 * also the two words a reader is most likely to place somewhere else. Then a width
 * keyword, because after the styles those three are the only identifiers left that
 * are certainly not colours. Then a colour — because at that point an identifier
 * the 9.9 vocabulary recognises can only be one, and one it does not recognise is a
 * component this grammar has no place for at all. The colour question is asked of
 * 9.9's *value* door rather than of its name table, since `transparent` and
 * `currentcolor` are colours and deliberately not table entries.
 */
static CssBorderClass BorderClassifyComponent(const CssValue *component,
                                                         const char **outReason)
{
    CssToken token;

    if (outReason) {
        *outReason = "not a border value";
    }
    if (!component || component->kind == RoboCssValueKindNone) {
        return RoboCssBorderClassInvalid;
    }
    if (component->kind == RoboCssValueKindList) {
        /* A declaration's value is a list, and for `border-top: solid` that list
         * holds one component. A *component* is what this function describes, so
         * one whose list has exactly one significant child is unwrapped and a
         * longer list is not: `10px 20px` is two components and classifying it as
         * whichever one happened to be first would be answering a question nobody
         * asked. The expansion below passes components rather than lists, so this
         * is for the direct caller. */
        const CssValue *only = BorderComponentAt(component, 0u);

        if (!only || BorderComponentCount(component) != 1u) {
            return RoboCssBorderClassInvalid;
        }
        component = only;
    }
    if (component->kind == RoboCssValueKindFunction) {
        CssColor color = CssColorNone();

        if (BorderComponentIsSubstitution(component)) {
            /* A reference whose substitution can change *which kind* a component
             * is: `--rule: 1px solid red` makes one component three, of two
             * different kinds, so classifying it now would be right for the
             * count and wrong for every part. */
            if (outReason) {
                *outReason =
                    "a reference whose substitution can change which kind a component is";
            }
            return RoboCssBorderClassDeferred;
        }
        /* A colour function *is* the colour — `rgb(10, 20, 30)`, `hsl(0 100% 50%)`
         * — and every other function is not a border value: a border has no
         * image and no position for a `url()` or a `translate()` to go to. */
        if (CssColorFromFunction(component, &color) ==
            RoboCssColorParseResolved) {
            return RoboCssBorderClassColor;
        }
        if (outReason) {
            *outReason = "not a border colour function";
        }
        return RoboCssBorderClassInvalid;
    }
    if (component->kind != RoboCssValueKindToken) {
        return RoboCssBorderClassInvalid;
    }
    token = CssValueToken(component);
    switch (token.type) {
    case RoboCssTokenIdent: {
        char keyword[64];
        size_t keywordLength = 0u;
        CssSlice text;
        CssColor named = CssColorNone();

        if (!CssTokenKeywordText(&token, keyword, sizeof(keyword),
                                         &keywordLength)) {
            return RoboCssBorderClassInvalid;
        }
        text = CssSliceFromBytes((const uint8_t *)keyword, keywordLength);
        if (CssBorderStyleSliceIsKnown(text)) {
            return RoboCssBorderClassStyle;
        }
        if (BorderKeywordIsLineWidth(keyword, keywordLength)) {
            return RoboCssBorderClassWidth;
        }
        /* Asked of the colour *value* rather than of the name table, for the three
         * spellings that are colours and not names: `transparent`, `currentcolor`
         * and `#hex` on the branch below. A name-table lookup would have refused
         * `border: 1px solid transparent`, which is ordinary authoring. */
        switch (CssColorFromValue(component, &named)) {
        case RoboCssColorParseResolved:
        case RoboCssColorParseCurrent:
            return RoboCssBorderClassColor;
        default:
            break;
        }
        if (outReason) {
            *outReason = "not a border value";
        }
        return RoboCssBorderClassInvalid;
    }
    case RoboCssTokenHash: {
        /* The colour spellings that are not a name, and they take the same road
         * as the name does: `#fff` is a colour the 9.9 module already parses, so
         * `border: 1px solid #fff` belongs to `border-color` for the same reason
         * `border: 1px solid white` does. */
        CssColor hex = CssColorNone();

        if (CssColorFromValue(component, &hex) ==
            RoboCssColorParseResolved) {
            return RoboCssBorderClassColor;
        }
        if (outReason) {
            *outReason = "not a border colour";
        }
        return RoboCssBorderClassInvalid;
    }
    case RoboCssTokenDimension:
    case RoboCssTokenPercentage:
    case RoboCssTokenNumber:
        /* A width, all three of them, and the last two carry a boundary this build
         * states rather than enforces: `0` is the one bare number CSS accepts as a
         * length, and a percentage is not legal for `border-width` at all — but
         * the registry admits it (see the header) and the classifier follows the
         * registry rather than overruling it. */
        return RoboCssBorderClassWidth;
    default:
        return RoboCssBorderClassInvalid;
    }
}

CssBorderClass CssBorderClassify(const CssValue *component)
{
    return BorderClassifyComponent(component, NULL);
}

/* ------------------------------------------------------------------ */
/* Reading components out of a value                                   */
/* ------------------------------------------------------------------ */

/** The @p wanted-th significant component of @p value, or NULL. */
static const CssValue *BorderComponentAt(const CssValue *value,
                                                   size_t wanted)
{
    size_t seen = 0u;
    size_t index;

    if (!value || value->kind == RoboCssValueKindNone) {
        return NULL;
    }
    if (value->kind != RoboCssValueKindList) {
        return wanted == 0u && !CssValueIsTrivia(value) ? value : NULL;
    }
    for (index = 0u; index < value->children.count; index++) {
        const CssValue *child = &value->children.items[index];

        if (CssValueIsTrivia(child)) {
            continue;
        }
        if (seen == wanted) {
            return child;
        }
        seen++;
    }
    return NULL;
}

/** Number of significant components in @p value. */
static size_t BorderComponentCount(const CssValue *value)
{
    size_t count = 0u;
    size_t index;

    if (!value || value->kind == RoboCssValueKindNone) {
        return 0u;
    }
    if (value->kind != RoboCssValueKindList) {
        return CssValueIsTrivia(value) ? 0u : 1u;
    }
    for (index = 0u; index < value->children.count; index++) {
        if (!CssValueIsTrivia(&value->children.items[index])) {
            count++;
        }
    }
    return count;
}

/** Build each kind's one-component list, or leave KIND_NONE. */
static Result BorderBuildParts(
    const CssValue *assigned[RoboCssBorderKindCount],
    CssValue outParts[RoboCssBorderKindCount],
    CssDiag *diag)
{
    size_t kindIndex;

    for (kindIndex = 0u; kindIndex < RoboCssBorderKindCount; kindIndex++) {
        CssValue list = RoboCssCleared;
        CssValue copy = RoboCssCleared;
        Result result;

        if (!assigned[kindIndex]) {
            /* KIND_NONE: the shorthand did not name this kind, so that longhand's
             * initial value applies. Deliberately not supplied here — the initial
             * values are the registry's (see the header). */
            continue;
        }
        CssValueInit(&list);
        CssValueInit(&copy);
        result = CssValueMakeList(&list, diag);
        if (result == RoboOk) {
            result = CssValueCopy(&copy, assigned[kindIndex], diag);
        }
        if (result == RoboOk) {
            /* Built as a list rather than as the bare component, so the result has
             * the shape a *declared* longhand's value has — which is what lets it
             * go through the same normalisers with no special case. `append` moves,
             * so `copy` owns nothing afterwards. */
            result = CssValueAppend(&list, &copy, diag);
        }
        if (result != RoboOk) {
            CssValueDispose(&copy);
            CssValueDispose(&list);
            return result;
        }
        outParts[kindIndex] = list;
    }
    return RoboOk;
}

Result CssBorderExpand(const CssValue *value,
                                     CssValue outParts[RoboCssBorderKindCount],
                                     CssDiag *diag)
{
    const CssValue *assigned[RoboCssBorderKindCount];
    size_t count;
    size_t index;
    size_t kindIndex;
    Result result;

    if (!outParts) {
        return RoboCssDiag(diag, RoboCssDomainBorder, RoboInvalidArgument,
                             "the output array is NULL");
    }
    for (kindIndex = 0u; kindIndex < RoboCssBorderKindCount; kindIndex++) {
        CssValueInit(&outParts[kindIndex]);
        assigned[kindIndex] = NULL;
    }
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainBorder, RoboInvalidArgument,
                             "the shorthand value is NULL");
    }
    count = BorderComponentCount(value);
    if (count == 0u) {
        return RoboCssDiag(diag, RoboCssDomainBorder, RoboNotSupported,
                             "a border shorthand has no components");
    }

    for (index = 0u; index < count; index++) {
        const CssValue *component = BorderComponentAt(value, index);
        const char *reason = NULL;
        CssBorderClass outcome = BorderClassifyComponent(component, &reason);
        size_t slot;

        switch (outcome) {
        case RoboCssBorderClassWidth:
            slot = (size_t)RoboCssBorderKindWidth;
            break;
        case RoboCssBorderClassStyle:
            slot = (size_t)RoboCssBorderKindStyle;
            break;
        case RoboCssBorderClassColor:
            slot = (size_t)RoboCssBorderKindColor;
            break;
        case RoboCssBorderClassDeferred:
            return RoboCssDiag(diag, RoboCssDomainBorder, RoboNotSupported,
                                 reason);
        default:
            return RoboCssDiag(diag, RoboCssDomainBorder, RoboNotSupported,
                                 reason ? reason : "not a border value");
        }
        if (assigned[slot]) {
            /* `border: 1px 2px solid` is not a two-value width — a border width is
             * one value, and there is no second width for the first to be the top
             * of. The positional rows are where a pair of lengths means four sides;
             * here it means the author has written something this grammar has no
             * room for. */
            return RoboCssDiag(diag, RoboCssDomainBorder, RoboNotSupported,
                                 "a border shorthand takes one value of each kind");
        }
        assigned[slot] = component;
    }

    result = BorderBuildParts(assigned, outParts, diag);
    if (result != RoboOk) {
        /* All or nothing: a caller that ignored the result code must not be handed
         * two of a shorthand's three kinds. */
        for (kindIndex = 0u; kindIndex < RoboCssBorderKindCount; kindIndex++) {
            CssValueDispose(&outParts[kindIndex]);
            CssValueInit(&outParts[kindIndex]);
        }
        return result;
    }
    return RoboOk;
}
