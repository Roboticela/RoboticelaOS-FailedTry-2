/*
 * RoboCSS grid track templates and line placements (Phase 19.4) — see
 * RoboCSSGridTemplate.h.
 *
 * Two readers, and the shape of each is decided by its grammar rather than by a
 * house style.
 *
 * The **track list** is a repetition: `<track-size>+`, where a track size is a
 * breadth, a `minmax()`, or a `repeat()` that expands to more of them. So it is
 * read by a recursion with an accumulator — the only recursion in this file, and it
 * is one level deep because a nested `repeat()` is invalid CSS and is refused
 * rather than flattened. The bound on the accumulator is a *refusal*: a list that
 * would exceed `RoboCssGridMaxTracks` answers INVALID, so a caller is never handed
 * a prefix of the tracks the author wrote.
 *
 * The **line placement** is two components at most, so it is read by counting
 * significant components and classifying them, exactly as `grid-auto-flow` is — but
 * where `grid-auto-flow`'s two words are interchangeable in order, `span 2` is an
 * ordered pair and `2 span` is not legal, so this reader tests the components in
 * place.
 *
 * The one classification that is not a table lookup is `fr`. `1fr` is a
 * `<dimension>` whose unit CSS defines and this build's unit vocabulary does not
 * contain (it is a closed set of *length* units), so `CssNumericFromValue` would
 * answer UNKNOWN_UNIT for it and the one number a grid needs would be lost. Reading
 * the unit's spelling *before* asking the numeric door is the whole reason
 * `CssGridTrackBreadth` is a record of its own rather than a `CssNumeric` with a
 * flag.
 */

#include "RoboCSS/RoboCSSGridTemplate.h"

#include "RoboCSS/RoboCSSUnit.h"

#include "RoboCSSInternal.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Vocabulary tables                                                   */
/* ------------------------------------------------------------------ */

typedef struct gridBreadthRow {
    CssGridTrackBreadthKind kind;
    const char *keyword; /* NULL for the kinds that are not a bare keyword */
} GridBreadthRow;

static const GridBreadthRow gridBreadthVocabulary[] = {
    { RoboCssGridBreadthAuto, "auto" },
    { RoboCssGridBreadthMinContent, "min-content" },
    { RoboCssGridBreadthMaxContent, "max-content" }
};

#define GRID_BREADTH_KEYWORD_COUNT \
    (sizeof(gridBreadthVocabulary) / sizeof(gridBreadthVocabulary[0]))

int CssGridTrackBreadthKindIsKnown(CssGridTrackBreadthKind kind)
{
    return kind >= RoboCssGridBreadthAuto &&
           kind < (CssGridTrackBreadthKind)RoboCssGridTrackBreadthKindCount;
}

const char *CssGridTrackBreadthKindName(CssGridTrackBreadthKind kind)
{
    switch (kind) {
    case RoboCssGridBreadthAuto:
        return "auto";
    case RoboCssGridBreadthLength:
        return "length";
    case RoboCssGridBreadthPercentage:
        return "percentage";
    case RoboCssGridBreadthMinContent:
        return "min-content";
    case RoboCssGridBreadthMaxContent:
        return "max-content";
    case RoboCssGridBreadthFraction:
        return "flex";
    case RoboCssGridBreadthFitContent:
        return "fit-content";
    default:
        return "unknown";
    }
}

int CssGridTrackListParseIsKnown(CssGridTrackListParse outcome)
{
    return outcome >= RoboCssGridTrackListParseInvalid &&
           outcome < (CssGridTrackListParse)RoboCssGridTrackListParseCount;
}

const char *CssGridTrackListParseName(CssGridTrackListParse outcome)
{
    switch (outcome) {
    case RoboCssGridTrackListParseInvalid:
        return "invalid";
    case RoboCssGridTrackListParseTracks:
        return "tracks";
    case RoboCssGridTrackListParseNone:
        return "none";
    case RoboCssGridTrackListParseDeferred:
        return "deferred";
    default:
        return "unknown";
    }
}

int CssGridLinePlacementKindIsKnown(CssGridLinePlacementKind kind)
{
    return kind >= RoboCssGridLineAuto &&
           kind < (CssGridLinePlacementKind)RoboCssGridLinePlacementKindCount;
}

const char *CssGridLinePlacementKindName(CssGridLinePlacementKind kind)
{
    switch (kind) {
    case RoboCssGridLineAuto:
        return "auto";
    case RoboCssGridLineNumber:
        return "line";
    case RoboCssGridLineSpan:
        return "span";
    default:
        return "unknown";
    }
}

int CssGridLineParseIsKnown(CssGridLineParse outcome)
{
    return outcome >= RoboCssGridLineParseInvalid &&
           outcome < (CssGridLineParse)RoboCssGridLineParseCount;
}

const char *CssGridLineParseName(CssGridLineParse outcome)
{
    switch (outcome) {
    case RoboCssGridLineParseInvalid:
        return "invalid";
    case RoboCssGridLineParsePlacement:
        return "placement";
    case RoboCssGridLineParseDeferred:
        return "deferred";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/** Non-zero when @p keyword equals @p spelling, ASCII case-insensitively. */
static int GridKeywordEquals(const char *keyword, size_t length,
                             const char *spelling)
{
    return length > 0u &&
           CssBytesEqualAsciiCi((const uint8_t *)keyword, length, spelling);
}

/** The lowercased keyword text of a token component, or 0 when it is not one. */
static int GridComponentKeyword(const CssValue *component,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength)
{
    if (!component || component->kind != RoboCssValueKindToken) {
        return 0;
    }
    if (component->token.type != RoboCssTokenIdent) {
        return 0;
    }
    return CssTokenKeywordText(&component->token, buffer, capacity, outLength);
}

/**
 * Whether @p component is a `var()`/`env()` reference — the one function form
 * whose *value* is decided elsewhere.
 *
 * A declaration that contains one is carried rather than read, and the policy is
 * this subsystem's own (RoboCSSBox.h states it for the shorthands): substitution
 * happens before a value is interpreted and can change how many components there
 * are, so a reader that classified the reference would be classifying the wrong
 * number of things.
 */
static int GridComponentIsReference(const CssValue *component)
{
    return CssValueFunctionNamed(component, "var") ||
           CssValueFunctionNamed(component, "env");
}

/**
 * Whether the value is, or **contains anywhere**, a reference.
 *
 * The walk is over `CssValueChild`, so it descends into a list, a `minmax()`, a
 * `repeat()` and a block alike: substitution happens before a value is interpreted and
 * can change how many components there are *inside* a function as readily as beside
 * one, so `minmax(40px, var(--rest))` is carried exactly as `40px var(--rest)` is.
 */
static int GridValueHasReference(const CssValue *value)
{
    size_t count;
    size_t index;

    if (!value) {
        return 0;
    }
    if (GridComponentIsReference(value)) {
        return 1;
    }
    count = CssValueChildCount(value);
    for (index = 0u; index < count; index++) {
        if (GridValueHasReference(CssValueChild(value, index))) {
            return 1;
        }
    }
    return 0;
}

/** Whether @p value is the comma that separates two arguments of a function. */
static int GridComponentIsComma(const CssValue *value)
{
    return value && value->kind == RoboCssValueKindToken &&
           value->token.type == RoboCssTokenComma;
}

/**
 * How many comma-separated arguments @p function has, or 0 when its argument list is
 * not one.
 *
 * A function's arguments are its **children**, not the function value's own
 * components: `CssValueSignificantAt()` walks a *list*, and a function is one
 * component, so asking it for "component 0" of `minmax(40px, 1fr)` answers the
 * function itself. Splitting on commas is therefore this reader's job, and a run of
 * significant components between commas counts as one argument -- `repeat(2, 40px 80px)`
 * has two arguments, the second of them two track sizes.
 *
 * An empty argument (`minmax(40px,)`, `repeat(, 1fr)`) and a trailing comma make the
 * whole list malformed rather than a list with fewer arguments, which is CSS's own
 * reading and the reason this returns 0 rather than a count for them.
 */
static size_t GridFunctionArgumentCount(const CssValue *function)
{
    size_t count = 0u;
    size_t index;
    int runHasComponent = 0;

    if (!function || function->kind != RoboCssValueKindFunction) {
        return 0u;
    }
    for (index = 0u; index < CssValueChildCount(function); index++) {
        const CssValue *child = CssValueChild(function, index);

        if (GridComponentIsComma(child)) {
            if (!runHasComponent) {
                return 0u;
            }
            count++;
            runHasComponent = 0;
            continue;
        }
        if (CssValueIsTrivia(child)) {
            continue;
        }
        runHasComponent = 1;
    }
    if (!runHasComponent) {
        return 0u;
    }
    return count + 1u;
}

/**
 * The @p componentIndex-th significant component of @p function's @p argumentIndex-th
 * argument, or NULL when there is no such component.
 *
 * The one door the three function readers share, so that `minmax()`'s two arguments,
 * `fit-content()`'s one and `repeat()`'s count and track-size list are all read the
 * same way and a comma is never mistaken for a track size.
 */
static const CssValue *GridFunctionArgument(const CssValue *function,
                                            size_t argumentIndex,
                                            size_t componentIndex)
{
    size_t index;
    size_t argument = 0u;
    size_t component = 0u;
    int runHasComponent = 0;

    if (!function || function->kind != RoboCssValueKindFunction) {
        return NULL;
    }
    for (index = 0u; index < CssValueChildCount(function); index++) {
        const CssValue *child = CssValueChild(function, index);

        if (GridComponentIsComma(child)) {
            if (!runHasComponent) {
                return NULL;
            }
            argument++;
            component = 0u;
            runHasComponent = 0;
            continue;
        }
        if (CssValueIsTrivia(child)) {
            continue;
        }
        if (argument == argumentIndex && component == componentIndex) {
            return child;
        }
        component++;
        runHasComponent = 1;
    }
    return NULL;
}

/** Whether @p function's @p argumentIndex-th argument is exactly one component. */
static int GridFunctionArgumentIsSingle(const CssValue *function,
                                        size_t argumentIndex)
{
    return GridFunctionArgument(function, argumentIndex, 0u) != NULL &&
           GridFunctionArgument(function, argumentIndex, 1u) == NULL;
}

/* ------------------------------------------------------------------ */
/* Breadths                                                            */
/* ------------------------------------------------------------------ */

void CssGridTrackBreadthInit(CssGridTrackBreadth *breadth)
{
    if (!breadth) {
        return;
    }
    memset(breadth, 0, sizeof(*breadth));
    breadth->kind = RoboCssGridBreadthAuto;
    breadth->numeric = CssNumericNone();
}

int CssGridTrackBreadthEqual(const CssGridTrackBreadth *a,
                             const CssGridTrackBreadth *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    if (a->kind == RoboCssGridBreadthFraction) {
        return a->fraction == b->fraction;
    }
    if (a->kind == RoboCssGridBreadthLength ||
        a->kind == RoboCssGridBreadthPercentage ||
        a->kind == RoboCssGridBreadthFitContent) {
        return CssNumericEquals(a->numeric, b->numeric);
    }
    /* The four keyword kinds carry nothing but their kind. */
    return 1;
}

int CssGridTrackBreadthIsDefinite(const CssGridTrackBreadth *breadth)
{
    if (!breadth) {
        return 0;
    }
    return breadth->kind == RoboCssGridBreadthLength ||
           breadth->kind == RoboCssGridBreadthPercentage;
}

int CssGridTrackBreadthIsFlexible(const CssGridTrackBreadth *breadth)
{
    return breadth ? breadth->kind == RoboCssGridBreadthFraction : 0;
}

/**
 * Read one component as a track breadth.
 *
 * The order of the questions is the logic: `fr` first (because the unit table does
 * not know it), then a function (`fit-content()` is the only one a breadth can be),
 * then the three keyword rows, then the numeric door, which is the one place a
 * `<length>` or a `<percentage>` is normalised by the module that normalises every
 * other length in this build.
 */
static int GridBreadthFromComponent(const CssValue *component,
                                    CssGridTrackBreadth *out)
{
    CssGridTrackBreadth breadth;
    char keyword[32];
    size_t length = 0u;
    size_t index;

    if (!component || !out) {
        return 0;
    }
    CssGridTrackBreadthInit(&breadth);

    /* ---- `fit-content(<length-percentage>)` ---------------------- */
    if (component->kind == RoboCssValueKindFunction) {
        const CssValue *limit;
        CssNumeric numeric;

        if (!CssValueFunctionNamed(component, "fit-content") ||
            GridFunctionArgumentCount(component) != 1u ||
            !GridFunctionArgumentIsSingle(component, 0u)) {
            return 0;
        }
        limit = GridFunctionArgument(component, 0u, 0u);
        if (CssNumericFromValue(limit, &numeric) != RoboCssNumericParseResolved) {
            return 0;
        }
        if (numeric.kind != RoboCssNumericLength &&
            numeric.kind != RoboCssNumericPercentage) {
            return 0;
        }
        breadth.kind = RoboCssGridBreadthFitContent;
        breadth.numeric = numeric;
        *out = breadth;
        return 1;
    }
    if (component->kind != RoboCssValueKindToken) {
        return 0;
    }

    /* ---- `<flex>`: the unit this build's unit table does not have --- */
    if (component->token.type == RoboCssTokenDimension &&
        GridKeywordEquals((const char *)component->token.unit.bytes,
                          component->token.unit.length, "fr")) {
        double fraction = component->token.number;

        /* A `<flex>` is a positive number: `0fr` and `-1fr` are not values, and a
         * non-finite one is not a number a layout can divide by. */
        if (!CssNumberIsFinite(fraction) || fraction <= 0.0) {
            return 0;
        }
        breadth.kind = RoboCssGridBreadthFraction;
        breadth.fraction = fraction;
        *out = breadth;
        return 1;
    }

    /* ---- the three keyword rows ---------------------------------- */
    if (component->token.type == RoboCssTokenIdent) {
        if (!GridComponentKeyword(component, keyword, sizeof(keyword), &length)) {
            return 0;
        }
        for (index = 0u; index < GRID_BREADTH_KEYWORD_COUNT; index++) {
            if (GridKeywordEquals(keyword, length,
                                  gridBreadthVocabulary[index].keyword)) {
                breadth.kind = gridBreadthVocabulary[index].kind;
                *out = breadth;
                return 1;
            }
        }
        /* `none` and `subgrid` are not breadths, and neither is a line name. */
        return 0;
    }

    /* ---- a length or a percentage -------------------------------- */
    {
        CssNumeric numeric;
        CssNumericParse parsed = CssNumericFromValue(component, &numeric);

        if (parsed != RoboCssNumericParseResolved) {
            return 0;
        }
        if (numeric.kind == RoboCssNumericLength) {
            breadth.kind = RoboCssGridBreadthLength;
        } else if (numeric.kind == RoboCssNumericPercentage) {
            breadth.kind = RoboCssGridBreadthPercentage;
        } else {
            /* A bare `<number>` is not a track size: `grid-template-columns: 3` is
             * a declaration CSS refuses, and reading it as `3px` would be a guess
             * at a unit the author did not write. */
            return 0;
        }
        breadth.numeric = numeric;
        *out = breadth;
        return 1;
    }
}

void CssGridTrackSizeInit(CssGridTrackSize *size)
{
    if (!size) {
        return;
    }
    memset(size, 0, sizeof(*size));
    CssGridTrackBreadthInit(&size->minimum);
    CssGridTrackBreadthInit(&size->maximum);
}

int CssGridTrackSizeEqual(const CssGridTrackSize *a,
                          const CssGridTrackSize *b)
{
    if (!a || !b) {
        return a == b;
    }
    return CssGridTrackBreadthEqual(&a->minimum, &b->minimum) &&
           CssGridTrackBreadthEqual(&a->maximum, &b->maximum);
}

/**
 * Read one component as a track size: a breadth, or a `minmax(<breadth>,
 * <breadth>)`.
 *
 * A breadth writes *both* halves, which is how CSS reads `40px` — it is
 * `minmax(40px, 40px)` — and the reason a track record has two fields at all:
 * they differ exactly when `minmax()`'s do. A `<flex>` is refused as the
 * **minimum**, which is CSS's own rule (`minmax(1fr, 2fr)` is invalid) and not a
 * simplification: a share of free space is not a floor.
 *
 * `repeat()` is *not* read here. It is a repetition of track sizes rather than a
 * track size, so refusing it here is what makes a nested `repeat()` invalid without
 * a depth counter.
 */
static int GridTrackSizeFromComponent(const CssValue *component,
                                      CssGridTrackSize *out)
{
    if (!component || !out) {
        return 0;
    }
    if (CssValueFunctionNamed(component, "minmax")) {
        const CssValue *minimum;
        const CssValue *maximum;
        CssGridTrackSize size;

        /* Exactly two arguments, each of them **one** component: a third argument and
         * a two-component argument are both a declaration CSS refuses rather than a
         * `minmax()` this build reads halfway. */
        if (GridFunctionArgumentCount(component) != 2u ||
            !GridFunctionArgumentIsSingle(component, 0u) ||
            !GridFunctionArgumentIsSingle(component, 1u)) {
            return 0;
        }
        minimum = GridFunctionArgument(component, 0u, 0u);
        maximum = GridFunctionArgument(component, 1u, 0u);
        CssGridTrackSizeInit(&size);
        if (!GridBreadthFromComponent(minimum, &size.minimum)) {
            return 0;
        }
        if (!GridBreadthFromComponent(maximum, &size.maximum)) {
            return 0;
        }
        if (size.minimum.kind == RoboCssGridBreadthFraction) {
            return 0;
        }
        *out = size;
        return 1;
    }
    if (CssValueFunctionNamed(component, "repeat") ||
        CssValueFunctionNamed(component, "subgrid")) {
        return 0;
    }
    {
        CssGridTrackSize size;

        CssGridTrackSizeInit(&size);
        if (!GridBreadthFromComponent(component, &size.minimum)) {
            return 0;
        }
        size.maximum = size.minimum;
        *out = size;
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Track lists                                                         */
/* ------------------------------------------------------------------ */

void CssGridTrackListInit(CssGridTrackList *list)
{
    if (!list) {
        return;
    }
    memset(list, 0, sizeof(*list));
    list->none = 0;
    list->count = 0u;
}

int CssGridTrackListEqual(const CssGridTrackList *a,
                          const CssGridTrackList *b)
{
    size_t index;

    if (!a || !b) {
        return a == b;
    }
    if (a->none != b->none || a->count != b->count) {
        return 0;
    }
    for (index = 0u; index < a->count; index++) {
        if (!CssGridTrackSizeEqual(&a->tracks[index], &b->tracks[index])) {
            return 0;
        }
    }
    return 1;
}

int CssGridTrackListIsNone(const CssGridTrackList *list)
{
    if (!list) {
        return 1;
    }
    return list->none || list->count == 0u;
}

/** The reader's internal three-way answer, before it becomes a public outcome. */
typedef enum gridRead {
    GRID_READ_OK = 0,
    GRID_READ_INVALID = 1,
    GRID_READ_DEFERRED = 2
} GridRead;

static GridRead GridListAppend(CssGridTrackList *list,
                               const CssGridTrackSize *size)
{
    if (list->count >= RoboCssGridMaxTracks) {
        /* Refused rather than truncated: a list a caller lays out must be the list
         * the author wrote, and a prefix would place items into tracks that are not
         * the ones their line numbers name. */
        return GRID_READ_INVALID;
    }
    list->tracks[list->count] = *size;
    list->count++;
    return GRID_READ_OK;
}

/**
 * Expand one `repeat(<count>, <track-size>+)` into @p list.
 *
 * `auto-fill` and `auto-fit` are DEFERRED: their count is the container's used
 * size divided by the track's size, which is a layout question and not a value
 * question. The count form is expanded here so that no consumer re-derives a
 * repetition, and the inner list is read by the same track-size reader as the outer
 * one — which is exactly why a *nested* `repeat()` is refused: the inner reader
 * does not know the function.
 */
static GridRead GridListExpandRepeat(const CssValue *repeat,
                                     CssGridTrackList *list)
{
    const CssValue *first;
    CssNumeric numeric;
    int64_t count = 0;
    size_t outer;
    size_t inner;

    /* Exactly two arguments: the count and the track-size list it repeats. A
     * `repeat(3)` repeats nothing and a `repeat(3 4, 1fr)` names two counts, and both
     * are declarations CSS refuses rather than a repetition this build guesses at. */
    if (GridFunctionArgumentCount(repeat) != 2u ||
        !GridFunctionArgumentIsSingle(repeat, 0u)) {
        return GRID_READ_INVALID;
    }
    first = GridFunctionArgument(repeat, 0u, 0u);
    if (!first) {
        return GRID_READ_INVALID;
    }
    if (first->kind == RoboCssValueKindToken &&
        first->token.type == RoboCssTokenIdent) {
        char keyword[32];
        size_t length = 0u;

        if (GridComponentKeyword(first, keyword, sizeof(keyword), &length) &&
            (GridKeywordEquals(keyword, length, "auto-fill") ||
             GridKeywordEquals(keyword, length, "auto-fit"))) {
            return GRID_READ_DEFERRED;
        }
        return GRID_READ_INVALID;
    }
    if (GridComponentIsReference(first)) {
        return GRID_READ_DEFERRED;
    }
    if (CssNumericFromValue(first, &numeric) != RoboCssNumericParseResolved ||
        numeric.kind != RoboCssNumericInteger ||
        !CssNumericIntegerValue(numeric, &count) || count < 1) {
        return GRID_READ_INVALID;
    }
    for (outer = 0u; outer < (size_t)count; outer++) {
        for (inner = 0u; ; inner++) {
            const CssValue *component = GridFunctionArgument(repeat, 1u, inner);
            CssGridTrackSize size;
            GridRead read;

            if (!component) {
                break;
            }
            if (!GridTrackSizeFromComponent(component, &size)) {
                return GRID_READ_INVALID;
            }
            read = GridListAppend(list, &size);
            if (read != GRID_READ_OK) {
                return read;
            }
        }
    }
    return GRID_READ_OK;
}

/** Read the significant components of @p specified, in order, into @p list. */
static GridRead GridListReadComponents(const CssValue *specified,
                                       CssGridTrackList *list)
{
    size_t count = CssValueSignificantCount(specified);
    size_t index;

    if (count == 0u) {
        return GRID_READ_INVALID;
    }
    for (index = 0u; index < count; index++) {
        const CssValue *component = CssValueSignificantAt(specified, index);

        if (GridComponentIsReference(component)) {
            return GRID_READ_DEFERRED;
        }
        if (CssValueFunctionNamed(component, "repeat")) {
            GridRead read = GridListExpandRepeat(component, list);

            if (read != GRID_READ_OK) {
                return read;
            }
            continue;
        }
        {
            CssGridTrackSize size;
            GridRead read;

            if (!GridTrackSizeFromComponent(component, &size)) {
                return GRID_READ_INVALID;
            }
            read = GridListAppend(list, &size);
            if (read != GRID_READ_OK) {
                return read;
            }
        }
    }
    return GRID_READ_OK;
}

CssGridTrackListParse CssGridTrackListFromValue(const CssValue *specified,
                                                int noneAllowed,
                                                CssGridTrackList *out)
{
    CssGridTrackList list;
    GridRead read;
    const CssValue *single;

    if (!out) {
        return RoboCssGridTrackListParseInvalid;
    }
    CssGridTrackListInit(out);
    if (!specified || specified->kind == RoboCssValueKindNone) {
        return RoboCssGridTrackListParseInvalid;
    }

    /* `none` first: it is one identifier and is not a track size, so a reader that
     * asked the component walker first would refuse it as an unknown keyword
     * without ever asking whether this property allows it. */
    single = CssValueSingle(specified);
    if (single && single->kind == RoboCssValueKindToken &&
        single->token.type == RoboCssTokenIdent) {
        char keyword[32];
        size_t length = 0u;

        if (GridComponentKeyword(single, keyword, sizeof(keyword), &length) &&
            GridKeywordEquals(keyword, length, "none")) {
            if (!noneAllowed) {
                /* `grid-auto-rows: none` is not CSS: an auto-track list has no
                 * `none`, and reading it as "no tracks" would silently make an
                 * invalid declaration mean the initial value. */
                return RoboCssGridTrackListParseInvalid;
            }
            out->none = 1;
            return RoboCssGridTrackListParseNone;
        }
    }
    if (GridValueHasReference(specified)) {
        return RoboCssGridTrackListParseDeferred;
    }

    CssGridTrackListInit(&list);
    read = GridListReadComponents(specified, &list);
    switch (read) {
    case GRID_READ_OK:
        *out = list;
        return RoboCssGridTrackListParseTracks;
    case GRID_READ_DEFERRED:
        return RoboCssGridTrackListParseDeferred;
    case GRID_READ_INVALID:
    default:
        return RoboCssGridTrackListParseInvalid;
    }
}

/* ------------------------------------------------------------------ */
/* Writing text, with the module's complete-or-absent discipline       */
/* ------------------------------------------------------------------ */

/**
 * An append-only text builder that keeps counting past the end.
 *
 * The refusal has to report the length a complete line *needs*, and a builder that
 * stopped at the capacity would report the capacity instead — which is why the
 * length is the number of bytes the text *is* and `overflow` is a separate fact
 * about whether they were all copied.
 */
typedef struct gridText {
    char *buffer;
    size_t capacity;
    size_t length;
    int overflow;
} GridText;

static void GridTextInit(GridText *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0u;
    text->overflow = 0;
    if (buffer && capacity > 0u) {
        buffer[0] = '\0';
    }
}

static void GridTextAppend(GridText *text, const char *bytes, size_t length)
{
    if (text->buffer && !text->overflow) {
        /* Room for these bytes *and* the NUL that goes on at the end: a partial
         * copy is not a prefix a caller can use, so the whole append is what is
         * checked. */
        if (text->length + length + 1u > text->capacity) {
            text->overflow = 1;
        } else {
            memcpy(text->buffer + text->length, bytes, length);
            text->buffer[text->length + length] = '\0';
        }
    }
    text->length += length;
}

static void GridTextAppendLiteral(GridText *text, const char *literal)
{
    GridTextAppend(text, literal, strlen(literal));
}

static void GridTextAppendInteger(GridText *text, int64_t value)
{
    char digits[32];
    int written = snprintf(digits, sizeof(digits), "%lld", (long long)value);

    if (written > 0 && (size_t)written < sizeof(digits)) {
        GridTextAppend(text, digits, (size_t)written);
    }
}

/** Write a numeric value through the module's own canonical formatter. */
static void GridTextAppendNumeric(GridText *text, const CssNumeric *numeric)
{
    char digits[RoboCssNumericTextMax];
    size_t length = 0u;

    if (CssNumericWrite(*numeric, digits, sizeof(digits), &length, NULL) ==
        RoboOk) {
        GridTextAppend(text, digits, length);
    }
}

static void GridTextAppendBreadth(GridText *text,
                                  const CssGridTrackBreadth *breadth)
{
    char digits[RoboCssNumericTextMax];

    switch (breadth->kind) {
    case RoboCssGridBreadthLength:
    case RoboCssGridBreadthPercentage:
        GridTextAppendNumeric(text, &breadth->numeric);
        break;
    case RoboCssGridBreadthFraction:
        if (CssMagnitudeRender(breadth->fraction, digits, sizeof(digits))) {
            GridTextAppendLiteral(text, digits);
        }
        GridTextAppendLiteral(text, "fr");
        break;
    case RoboCssGridBreadthFitContent:
        GridTextAppendLiteral(text, "fit-content(");
        GridTextAppendNumeric(text, &breadth->numeric);
        GridTextAppendLiteral(text, ")");
        break;
    case RoboCssGridBreadthMinContent:
        GridTextAppendLiteral(text, "min-content");
        break;
    case RoboCssGridBreadthMaxContent:
        GridTextAppendLiteral(text, "max-content");
        break;
    case RoboCssGridBreadthAuto:
    default:
        GridTextAppendLiteral(text, "auto");
        break;
    }
}

static void GridTextAppendTrackSize(GridText *text,
                                    const CssGridTrackSize *size)
{
    if (CssGridTrackBreadthEqual(&size->minimum, &size->maximum)) {
        GridTextAppendBreadth(text, &size->minimum);
        return;
    }
    GridTextAppendLiteral(text, "minmax(");
    GridTextAppendBreadth(text, &size->minimum);
    GridTextAppendLiteral(text, ", ");
    GridTextAppendBreadth(text, &size->maximum);
    GridTextAppendLiteral(text, ")");
}

Result CssGridTrackListWrite(const CssGridTrackList *list,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag)
{
    GridText text;
    size_t index;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainGrid, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    *outLength = 0u;
    if (!list) {
        return RoboCssDiag(diag, RoboCssDomainGrid, RoboInvalidArgument,
                           "not a track list");
    }
    GridTextInit(&text, buffer, capacity);
    if (CssGridTrackListIsNone(list)) {
        GridTextAppendLiteral(&text, "none");
    } else {
        for (index = 0u; index < list->count; index++) {
            if (index > 0u) {
                GridTextAppendLiteral(&text, " ");
            }
            GridTextAppendTrackSize(&text, &list->tracks[index]);
        }
    }
    if (text.overflow) {
        *outLength = text.length;
        return RoboCssDiag(diag, RoboCssDomainGrid, RoboNotSupported,
                           "the track list's text does not fit");
    }
    *outLength = text.length;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Line placements                                                     */
/* ------------------------------------------------------------------ */

void CssGridLinePlacementInit(CssGridLinePlacement *placement)
{
    if (!placement) {
        return;
    }
    memset(placement, 0, sizeof(*placement));
    placement->kind = RoboCssGridLineAuto;
    placement->line = 0;
}

int CssGridLinePlacementEqual(const CssGridLinePlacement *a,
                              const CssGridLinePlacement *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->kind == b->kind && a->line == b->line;
}

int CssGridLinePlacementIsAuto(const CssGridLinePlacement *placement)
{
    if (!placement) {
        return 1;
    }
    return placement->kind == RoboCssGridLineAuto;
}

/** The integer a component spells, or 0 when it is not an `<integer>`. */
static int GridComponentInteger(const CssValue *component, int64_t *out)
{
    CssNumeric numeric;

    if (CssNumericFromValue(component, &numeric) != RoboCssNumericParseResolved) {
        return 0;
    }
    if (numeric.kind != RoboCssNumericInteger) {
        return 0;
    }
    return CssNumericIntegerValue(numeric, out);
}

CssGridLineParse CssGridLinePlacementFromValue(const CssValue *specified,
                                               CssGridLinePlacement *out)
{
    CssGridLinePlacement placement;
    const CssValue *first;
    size_t count;

    if (!out) {
        return RoboCssGridLineParseInvalid;
    }
    CssGridLinePlacementInit(out);
    if (!specified || specified->kind == RoboCssValueKindNone) {
        return RoboCssGridLineParseInvalid;
    }
    if (GridValueHasReference(specified)) {
        return RoboCssGridLineParseDeferred;
    }
    count = CssValueSignificantCount(specified);
    first = CssValueSignificantAt(specified, 0u);
    if (count == 0u || !first) {
        return RoboCssGridLineParseInvalid;
    }

    /* ---- one component ------------------------------------------- */
    if (count == 1u) {
        char keyword[32];
        size_t length = 0u;
        int64_t value = 0;

        if (first->kind == RoboCssValueKindToken &&
            first->token.type == RoboCssTokenIdent) {
            if (!GridComponentKeyword(first, keyword, sizeof(keyword), &length)) {
                return RoboCssGridLineParseInvalid;
            }
            if (GridKeywordEquals(keyword, length, "auto")) {
                CssGridLinePlacementInit(&placement);
                *out = placement;
                return RoboCssGridLineParsePlacement;
            }
            /* `span` on its own says nothing, and any other ident is a **named
             * line**: legal CSS whose semantics need the line-name table this build
             * does not keep, so it is deferred rather than guessed at. */
            if (GridKeywordEquals(keyword, length, "span")) {
                return RoboCssGridLineParseInvalid;
            }
            return RoboCssGridLineParseDeferred;
        }
        if (!GridComponentInteger(first, &value)) {
            return RoboCssGridLineParseInvalid;
        }
        if (value > RoboCssGridLineLimit || value < -RoboCssGridLineLimit) {
            return RoboCssGridLineParseInvalid;
        }
        CssGridLinePlacementInit(&placement);
        placement.kind = RoboCssGridLineNumber;
        placement.line = (int32_t)value;
        *out = placement;
        return RoboCssGridLineParsePlacement;
    }

    /* ---- `span <integer>` ---------------------------------------- */
    if (count == 2u) {
        const CssValue *second = CssValueSignificantAt(specified, 1u);
        char keyword[32];
        size_t length = 0u;
        int64_t value = 0;

        if (!second) {
            return RoboCssGridLineParseInvalid;
        }
        if (!GridComponentKeyword(first, keyword, sizeof(keyword), &length) ||
            !GridKeywordEquals(keyword, length, "span")) {
            /* A named line beside a number, or two numbers: neither is a placement
             * this build reads. */
            return RoboCssGridLineParseInvalid;
        }
        if (!GridComponentInteger(second, &value)) {
            return RoboCssGridLineParseInvalid;
        }
        /* `span 0` and `span -1` are not CSS: a span counts *forward*. */
        if (value < 1 || value > RoboCssGridLineLimit) {
            return RoboCssGridLineParseInvalid;
        }
        CssGridLinePlacementInit(&placement);
        placement.kind = RoboCssGridLineSpan;
        placement.line = (int32_t)value;
        *out = placement;
        return RoboCssGridLineParsePlacement;
    }

    return RoboCssGridLineParseInvalid;
}

Result CssGridLinePlacementWrite(const CssGridLinePlacement *placement,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 CssDiag *diag)
{
    GridText text;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainGrid, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    *outLength = 0u;
    if (!placement) {
        return RoboCssDiag(diag, RoboCssDomainGrid, RoboInvalidArgument,
                           "not a line placement");
    }
    GridTextInit(&text, buffer, capacity);
    switch (placement->kind) {
    case RoboCssGridLineNumber:
        GridTextAppendInteger(&text, (int64_t)placement->line);
        break;
    case RoboCssGridLineSpan:
        GridTextAppendLiteral(&text, "span ");
        GridTextAppendInteger(&text, (int64_t)placement->line);
        break;
    case RoboCssGridLineAuto:
    default:
        GridTextAppendLiteral(&text, "auto");
        break;
    }
    if (text.overflow) {
        *outLength = text.length;
        return RoboCssDiag(diag, RoboCssDomainGrid, RoboNotSupported,
                           "the line placement's text does not fit");
    }
    *outLength = text.length;
    return RoboOk;
}
