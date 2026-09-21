/*
 * RoboCSS box model vocabulary (Phase 9.11) — see RoboCSSBox.h.
 *
 * Three things live here and nothing else: the side order, the shorthand table,
 * and the component mapping. No property is classified, no value is interpreted
 * and no precedence is decided — those belong to the registry, the value layer
 * and the cascade respectively, and each is a place this module deliberately
 * does not reach into.
 */

#include "RoboCSS/RoboCSSBox.h"

#include "RoboCSSInternal.h"

/* ------------------------------------------------------------------ */
/* The sides                                                           */
/* ------------------------------------------------------------------ */

int CssBoxSideIsKnown(CssBoxSide side)
{
    return side == RoboCssBoxSideTop || side == RoboCssBoxSideRight ||
           side == RoboCssBoxSideBottom || side == RoboCssBoxSideLeft;
}

const char *CssBoxSideName(CssBoxSide side)
{
    switch (side) {
    case RoboCssBoxSideTop:
        return "top";
    case RoboCssBoxSideRight:
        return "right";
    case RoboCssBoxSideBottom:
        return "bottom";
    case RoboCssBoxSideLeft:
        return "left";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* The shorthands                                                      */
/* ------------------------------------------------------------------ */

/*
 * The shorthands this build expands.
 *
 * Three rows, and the reason there are exactly these three is the phase
 * boundary rather than CSS's full shorthand list. `border` is a shorthand for
 * four widths, four styles and four colours; this build has no border styles and
 * paints no border, so expanding `border` would invent three quarters of a
 * declaration. `font`, `background` and the rest are shorthands for properties
 * this build does not represent at all.
 *
 * `border-width` is here, and it is the interesting row: it is a shorthand whose
 * longhands are lengths, which is exactly the box model's subject, and it is
 * *not* here because anything draws a border.
 *
 * The longhand spellings are written out rather than spliced from the side name
 * at runtime. Building `"margin-" + name` would allocate per call, and a table
 * whose contents a reader can see is a table a test can assert against.
 */
static const CssBoxShorthand boxShorthands[] = {
    { "margin", sizeof("margin") - 1u,
      { "margin-top", "margin-right", "margin-bottom", "margin-left" } },
    { "padding", sizeof("padding") - 1u,
      { "padding-top", "padding-right", "padding-bottom", "padding-left" } },
    { "border-width", sizeof("border-width") - 1u,
      { "border-top-width", "border-right-width", "border-bottom-width",
        "border-left-width" } },
    { "border-style", sizeof("border-style") - 1u,
      { "border-top-style", "border-right-style", "border-bottom-style",
        "border-left-style" } },
    { "border-color", sizeof("border-color") - 1u,
      { "border-top-color", "border-right-color", "border-bottom-color",
        "border-left-color" } }
};

size_t CssBoxShorthandCount(void)
{
    return sizeof(boxShorthands) / sizeof(boxShorthands[0]);
}

const CssBoxShorthand *CssBoxShorthandAt(size_t index)
{
    if (index >= CssBoxShorthandCount()) {
        return NULL;
    }
    return &boxShorthands[index];
}

const CssBoxShorthand *CssBoxShorthandFind(CssSlice name)
{
    size_t index;

    if (CssSliceIsAbsent(name)) {
        return NULL;
    }
    for (index = 0u; index < CssBoxShorthandCount(); index++) {
        const CssBoxShorthand *row = &boxShorthands[index];

        if (CssBytesEqualAsciiCi(name.bytes, name.length, row->name)) {
            return row;
        }
    }
    return NULL;
}

const char *CssBoxShorthandLonghand(const CssBoxShorthand *shorthand,
                                            CssBoxSide side)
{
    if (!shorthand || !CssBoxSideIsKnown(side)) {
        return NULL;
    }
    return shorthand->longhands[(size_t)side];
}

const CssBoxShorthand *CssBoxShorthandOfLonghand(
    CssSlice name,
    CssBoxSide *outSide)
{
    size_t rowIndex;

    if (outSide) {
        *outSide = RoboCssBoxSideTop;
    }
    if (CssSliceIsAbsent(name)) {
        return NULL;
    }
    for (rowIndex = 0u; rowIndex < CssBoxShorthandCount(); rowIndex++) {
        const CssBoxShorthand *row = &boxShorthands[rowIndex];
        size_t sideIndex;

        for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount; sideIndex++) {
            if (CssBytesEqualAsciiCi(name.bytes, name.length,
                                              row->longhands[sideIndex])) {
                if (outSide) {
                    *outSide = (CssBoxSide)sideIndex;
                }
                return row;
            }
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Expansion                                                           */
/* ------------------------------------------------------------------ */

/**
 * Which component supplies @p side when there are @p componentCount of them.
 *
 * Rows are component counts 1..4, columns are sides in side order. The one-value
 * row is the whole spelling for a uniform box, the two-value row is the
 * horizontal/vertical shorthand, and the three-value row is where top differs
 * from bottom and left borrows right — the row a table gets right and a
 * hand-written branch gets wrong.
 */
static const size_t boxComponentMap[RoboCssBoxComponentMax][RoboCssBoxSideCount] = {
    /* 1: a a a a */ { 0u, 0u, 0u, 0u },
    /* 2: a b a b */ { 0u, 1u, 0u, 1u },
    /* 3: a b c b */ { 0u, 1u, 2u, 1u },
    /* 4: a b c d */ { 0u, 1u, 2u, 3u }
};

/**
 * The @p wanted-th significant component of @p value, or NULL.
 *
 * `significant` is what the shorthand means: trivia is skipped, so `10px 20px`
 * is two components rather than three. A value that is a single component rather
 * than a list — a hand-built token, or a function — answers itself for index 0.
 */
static const CssValue *BoxComponentAt(const CssValue *value,
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

size_t CssBoxComponentCount(const CssValue *shorthandValue)
{
    size_t count = 0u;
    size_t index;

    if (!shorthandValue || shorthandValue->kind == RoboCssValueKindNone) {
        return 0u;
    }
    if (shorthandValue->kind != RoboCssValueKindList) {
        return CssValueIsTrivia(shorthandValue) ? 0u : 1u;
    }
    for (index = 0u; index < shorthandValue->children.count; index++) {
        if (!CssValueIsTrivia(&shorthandValue->children.items[index])) {
            count++;
        }
    }
    return count;
}

/**
 * Non-zero when @p value is a reference whose substitution can change how many
 * components a shorthand has: `var()` or `env()`.
 *
 * The two names are listed rather than derived because there are no others — a
 * `calc()` is one component whatever it contains, and a nested `var()` inside a
 * `calc()` is the calc's business, not the shorthand's. Nothing here evaluates
 * or substitutes; it decides only whether splitting now would be premature,
 * which is the one question whose answer can be wrong in a way that looks right.
 */
static int BoxComponentIsSubstitution(const CssValue *value)
{
    return CssValueFunctionNamed(value, "var") ||
           CssValueFunctionNamed(value, "env");
}

int CssBoxIsExpandable(const CssValue *shorthandValue,
                               size_t *outComponentCount)
{
    size_t count;
    size_t index;

    if (outComponentCount) {
        *outComponentCount = 0u;
    }
    count = CssBoxComponentCount(shorthandValue);
    if (count == 0u || count > (size_t)RoboCssBoxComponentMax) {
        return 0;
    }
    for (index = 0u; index < count; index++) {
        if (BoxComponentIsSubstitution(BoxComponentAt(shorthandValue, index))) {
            return 0;
        }
    }
    if (outComponentCount) {
        *outComponentCount = count;
    }
    return 1;
}

Result CssBoxExpandSide(const CssValue *shorthandValue,
                                       size_t componentCount,
                                       CssBoxSide side,
                                       CssValue *out,
                                       CssDiag *diag)
{
    const CssValue *component;
    CssValue list = RoboCssCleared;
    CssValue copy = RoboCssCleared;
    size_t componentIndex;
    Result result;

    if (!out) {
        return RoboCssDiag(diag, RoboCssDomainBox, RoboInvalidArgument,
                             "output is NULL");
    }
    CssValueDispose(out);
    if (!shorthandValue) {
        return RoboCssDiag(diag, RoboCssDomainBox, RoboInvalidArgument,
                             "the shorthand value is NULL");
    }
    if (!CssBoxSideIsKnown(side)) {
        return RoboCssDiag(diag, RoboCssDomainBox, RoboInvalidArgument,
                             "not one of a box's four sides");
    }
    if (componentCount == 0u || componentCount > (size_t)RoboCssBoxComponentMax) {
        return RoboCssDiag(diag, RoboCssDomainBox, RoboNotSupported,
                             "a shorthand expands from one to four components");
    }
    componentIndex = boxComponentMap[componentCount - 1u][(size_t)side];
    component = BoxComponentAt(shorthandValue, componentIndex);
    if (!component) {
        return RoboCssDiag(diag, RoboCssDomainBox, RoboNotSupported,
                             "the shorthand has no component for that side");
    }

    /* Built as a list of one rather than as the bare component, so that the
     * result has the shape a *declared* longhand's value has — which is what
     * lets it go through the same normalisers without a special case. */
    CssValueInit(&list);
    CssValueInit(&copy);
    result = CssValueMakeList(&list, diag);
    if (result != RoboOk) {
        CssValueDispose(&list);
        return result;
    }
    result = CssValueCopy(&copy, component, diag);
    if (result != RoboOk) {
        CssValueDispose(&copy);
        CssValueDispose(&list);
        return result;
    }
    result = CssValueAppend(&list, &copy, diag);
    if (result != RoboOk) {
        CssValueDispose(&copy);
        CssValueDispose(&list);
        return result;
    }
    *out = list;
    return RoboOk;
}

Result CssBoxExpand(const CssValue *shorthandValue,
                                  size_t componentCount,
                                  CssValue outSides[RoboCssBoxSideCount],
                                  CssDiag *diag)
{
    size_t index;
    Result result;

    if (!outSides) {
        return RoboCssDiag(diag, RoboCssDomainBox, RoboInvalidArgument,
                             "the output array is NULL");
    }
    for (index = 0u; index < (size_t)RoboCssBoxSideCount; index++) {
        CssValueInit(&outSides[index]);
    }
    for (index = 0u; index < (size_t)RoboCssBoxSideCount; index++) {
        result = CssBoxExpandSide(shorthandValue, componentCount,
                                          (CssBoxSide)index,
                                          &outSides[index], diag);
        if (result != RoboOk) {
            /* All or nothing: two sides of a four-sided box is not a box, and a
             * caller that ignored the result code must not be handed one. */
            size_t undo;

            for (undo = 0u; undo < (size_t)RoboCssBoxSideCount; undo++) {
                CssValueDispose(&outSides[undo]);
                CssValueInit(&outSides[undo]);
            }
            return result;
        }
    }
    return RoboOk;
}
