/*
 * RoboCSS grid track template suite (Phase 19.4) — css.gridtemplate.*
 *
 * The half of the grid family 18.4 deliberately carried: the **track list** and the
 * **line placements**, read into bounded value records.
 *
 * Two things earn the assertions here, and both are grammar rather than code. A track
 * list is a *repetition* -- `<track-size>+`, with `repeat()` standing for more of them
 * -- so the cases that matter are the ones a single-component reader would get wrong:
 * a repeated list expanded in place, `minmax()` read as the pair it is, `fit-content()`
 * as a clamp, and a list past the bound refused rather than truncated. And `fr` is a
 * unit CSS has and this build's unit table does not, so `1fr` is the one dimension the
 * numeric door cannot answer and the one classification worth pinning.
 *
 * Nothing here lays anything out. A track list at this phase is a *value* with sizes,
 * fractions and content keywords in it, and where the tracks land belongs to 19.4's
 * layout half (RoboLayout's `layout.grid.*`).
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSGridTemplate.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Reading a declaration, the way a layout pass does                   */
/* ------------------------------------------------------------------ */

/**
 * Parse @p text as a declaration and read its value as a track list.
 *
 * The shape every case below uses, and it is deliberately the shape a *consumer* has:
 * the computed value of `grid-template-columns` is a preserved token tree (18.4), so
 * RoboLayout hands exactly this to exactly this door. A literal is used for @p text
 * because the declarations borrow its bytes.
 */
static CssGridTrackListParse GridReadTracks(const char *text,
                                            int noneAllowed,
                                            CssGridTrackList *out)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    const CssDeclaration *declaration;
    CssGridTrackListParse outcome = RoboCssGridTrackListParseInvalid;

    CssGridTrackListInit(out);
    RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    if (declaration) {
        outcome = CssGridTrackListFromValue(CssDeclarationValue(declaration),
                                            noneAllowed, out);
    }
    CssDeclarationListDispose(&list);
    return outcome;
}

/** The same for a line placement. */
static CssGridLineParse GridReadLine(const char *text,
                                     CssGridLinePlacement *out)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    const CssDeclaration *declaration;
    CssGridLineParse outcome = RoboCssGridLineParseInvalid;

    CssGridLinePlacementInit(out);
    RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    if (declaration) {
        outcome = CssGridLinePlacementFromValue(CssDeclarationValue(declaration),
                                                out);
    }
    CssDeclarationListDispose(&list);
    return outcome;
}

/** The canonical text of a track list, for the assertions that read as text. */
static void GridTrackText(const CssGridTrackList *list, char *out,
                          size_t capacity)
{
    size_t length = 0u;

    out[0] = '\0';
    RoboCheckEqInt(CssGridTrackListWrite(list, out, capacity, &length, NULL),
                   RoboOk);
    RoboCheckEqInt((int)length, (int)strlen(out));
}

/** The canonical text of a line placement. */
static void GridLineText(const CssGridLinePlacement *placement, char *out,
                         size_t capacity)
{
    size_t length = 0u;

    out[0] = '\0';
    RoboCheckEqInt(CssGridLinePlacementWrite(placement, out, capacity, &length,
                                             NULL),
                   RoboOk);
    RoboCheckEqInt((int)length, (int)strlen(out));
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestGridTemplateBreadthKinds(void)
{
    static const char *const names[] = {
        "auto", "length", "percentage", "min-content", "max-content",
        "flex", "fit-content"
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssGridTrackBreadthKindCount, 7);

    /* Walked in enumerator order: a row whose table entry drifted would still answer
     * IsKnown for every value and would show only as a name printing as another's. */
    for (index = 0u; index < (size_t)RoboCssGridTrackBreadthKindCount; index++) {
        CssGridTrackBreadthKind kind = (CssGridTrackBreadthKind)index;

        RoboCheckEqInt(CssGridTrackBreadthKindIsKnown(kind), 1);
        RoboCheckStrEq(CssGridTrackBreadthKindName(kind), names[index]);
    }
    /* AUTO is 0, so a cleared record holds the property's initial value rather than an
     * absence a reader would have to guard. */
    RoboCheckEqInt((int)RoboCssGridBreadthAuto, 0);
    RoboCheckEqInt(CssGridTrackBreadthKindIsKnown((CssGridTrackBreadthKind)99), 0);
    RoboCheckStrEq(CssGridTrackBreadthKindName((CssGridTrackBreadthKind)99),
                   "unknown");

    RoboCheckEqInt((int)RoboCssGridTrackListParseCount, 4);
    RoboCheckStrEq(CssGridTrackListParseName(RoboCssGridTrackListParseInvalid),
                   "invalid");
    RoboCheckStrEq(CssGridTrackListParseName(RoboCssGridTrackListParseTracks),
                   "tracks");
    RoboCheckStrEq(CssGridTrackListParseName(RoboCssGridTrackListParseNone),
                   "none");
    RoboCheckStrEq(CssGridTrackListParseName(RoboCssGridTrackListParseDeferred),
                   "deferred");
    RoboCheckEqInt(CssGridTrackListParseIsKnown(RoboCssGridTrackListParseTracks), 1);
    RoboCheckEqInt(CssGridTrackListParseIsKnown((CssGridTrackListParse)42), 0);

    RoboCheckEqInt((int)RoboCssGridLinePlacementKindCount, 3);
    RoboCheckStrEq(CssGridLinePlacementKindName(RoboCssGridLineAuto), "auto");
    RoboCheckStrEq(CssGridLinePlacementKindName(RoboCssGridLineNumber), "line");
    RoboCheckStrEq(CssGridLinePlacementKindName(RoboCssGridLineSpan), "span");
    RoboCheckEqInt(CssGridLinePlacementKindIsKnown(RoboCssGridLineAuto), 1);
    RoboCheckEqInt(CssGridLinePlacementKindIsKnown((CssGridLinePlacementKind)99),
                   0);
    RoboCheckStrEq(CssGridLinePlacementKindName((CssGridLinePlacementKind)99),
                   "unknown");
}

/* ------------------------------------------------------------------ */
/* Track lists                                                         */
/* ------------------------------------------------------------------ */

static void TestGridTemplateSimpleTracks(void)
{
    CssGridTrackList list;
    char text[RoboCssGridTrackListTextMax];

    CssFixture();

    /* One length: both halves of the track are that length, which is how CSS reads a
     * plain `<track-breadth>` -- `minmax(40px, 40px)` written the short way. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: 40px", 1, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 1);
    RoboCheckEqInt((int)list.tracks[0].minimum.kind,
                   (int)RoboCssGridBreadthLength);
    RoboCheckEqInt((int)list.tracks[0].maximum.kind,
                   (int)RoboCssGridBreadthLength);
    RoboCheckEqInt(CssGridTrackBreadthEqual(&list.tracks[0].minimum,
                                            &list.tracks[0].maximum), 1);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "40px");

    /* Several, in order, and the units are kept as written rather than normalised to
     * px here -- resolving `em` is a layout question and this module has no font. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: 40px 2em 10%", 1, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 3);
    RoboCheckStrEq(CssUnitName(list.tracks[0].minimum.numeric.unit), "px");
    RoboCheckStrEq(CssUnitName(list.tracks[1].minimum.numeric.unit), "em");
    RoboCheckEqInt((int)list.tracks[2].minimum.kind,
                   (int)RoboCssGridBreadthPercentage);
    RoboCheckEqInt(CssGridTrackBreadthIsDefinite(&list.tracks[0].minimum), 1);
    RoboCheckEqInt(CssGridTrackBreadthIsDefinite(&list.tracks[2].minimum), 1);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "40px 2em 10%");

    /* The three keywords, each its own kind. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: auto min-content max-content",
                                  1, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 3);
    RoboCheckEqInt((int)list.tracks[0].minimum.kind, (int)RoboCssGridBreadthAuto);
    RoboCheckEqInt((int)list.tracks[1].minimum.kind,
                   (int)RoboCssGridBreadthMinContent);
    RoboCheckEqInt((int)list.tracks[2].minimum.kind,
                   (int)RoboCssGridBreadthMaxContent);
    /* `auto` is not a *definite* size and `min-content` is not flexible: the two
     * classifications a consumer dispatches on, and the safe answer for both. */
    RoboCheckEqInt(CssGridTrackBreadthIsDefinite(&list.tracks[0].minimum), 0);
    RoboCheckEqInt(CssGridTrackBreadthIsFlexible(&list.tracks[0].minimum), 0);
    RoboCheckEqInt(CssGridTrackBreadthIsFlexible(&list.tracks[1].minimum), 0);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "auto min-content max-content");

    /* Keywords are case-insensitive, as every CSS keyword is. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: AUTO 40PX", 1, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.tracks[0].minimum.kind, (int)RoboCssGridBreadthAuto);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "auto 40px");
}

/**
 * `fr`, the one dimension this build's unit table does not have.
 *
 * `1fr` is a `<dimension>` and its unit is not a length unit, so the numeric door
 * answers UNKNOWN_UNIT for it. The reader asks about the unit's *spelling* first, and
 * these cases are what proves it: a reader that went through `CssNumericFromValue`
 * would refuse every `fr` in this suite and lay out an empty grid.
 */
static void TestGridTemplateFractionTracks(void)
{
    CssGridTrackList list;
    char text[RoboCssGridTrackListTextMax];

    CssFixture();

    RoboCheckEqInt(GridReadTracks("grid-template-columns: 1fr", 1, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 1);
    RoboCheckEqInt((int)list.tracks[0].maximum.kind, (int)RoboCssGridBreadthFraction);
    RoboCheckEqInt(list.tracks[0].maximum.fraction == 1.0, 1);
    RoboCheckEqInt(CssGridTrackBreadthIsFlexible(&list.tracks[0].maximum), 1);
    RoboCheckEqInt(CssGridTrackBreadthIsDefinite(&list.tracks[0].maximum), 0);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "1fr");

    /* A fraction is not always a whole number, and the writer must not round one. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: 1.5fr 0.25fr", 1, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt(list.tracks[0].minimum.fraction == 1.5, 1);
    RoboCheckEqInt(list.tracks[1].minimum.fraction == 0.25, 1);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "1.5fr 0.25fr");

    /* The empty spellings a `<flex>` is not: a zero share and a negative one are not
     * values, because a track cannot be given a fraction of nothing. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: 0fr", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: -1fr", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
}

static void TestGridTemplateMinMax(void)
{
    CssGridTrackList list;
    char text[RoboCssGridTrackListTextMax];

    CssFixture();

    RoboCheckEqInt(GridReadTracks("grid-template-columns: minmax(40px, 1fr)", 1,
                                  &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 1);
    RoboCheckEqInt((int)list.tracks[0].minimum.kind, (int)RoboCssGridBreadthLength);
    RoboCheckEqInt(list.tracks[0].minimum.numeric.number == 40.0, 1);
    RoboCheckEqInt((int)list.tracks[0].maximum.kind,
                   (int)RoboCssGridBreadthFraction);
    RoboCheckEqInt(CssGridTrackBreadthEqual(&list.tracks[0].minimum,
                                            &list.tracks[0].maximum), 0);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "minmax(40px, 1fr)");

    /* `minmax(auto, 200px)`: a content floor under a declared ceiling, which is the
     * shape the pair exists for. */
    RoboCheckEqInt(GridReadTracks("grid-template-rows: minmax(auto, 200px)", 1,
                                  &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.tracks[0].minimum.kind, (int)RoboCssGridBreadthAuto);
    RoboCheckEqInt((int)list.tracks[0].maximum.kind, (int)RoboCssGridBreadthLength);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "minmax(auto, 200px)");

    /* A `<flex>` is refused as the **minimum**, which is CSS's own rule rather than a
     * shortcut: a share of free space is not a floor. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: minmax(1fr, 2fr)", 1,
                                  &list),
                   (int)RoboCssGridTrackListParseInvalid);
    /* One component and three are both not a `minmax()`. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: minmax(40px)", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: minmax(40px, 1fr, auto)",
                                  1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    /* And the halves are track breadths, so a bare number is not one. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: minmax(40, 80)", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
}

static void TestGridTemplateFitContent(void)
{
    CssGridTrackList list;
    char text[RoboCssGridTrackListTextMax];

    CssFixture();

    RoboCheckEqInt(GridReadTracks("grid-template-columns: fit-content(30%)", 1,
                                  &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 1);
    RoboCheckEqInt((int)list.tracks[0].maximum.kind,
                   (int)RoboCssGridBreadthFitContent);
    RoboCheckEqInt((int)list.tracks[0].maximum.numeric.kind,
                   (int)RoboCssNumericPercentage);
    RoboCheckEqInt(CssGridTrackBreadthIsDefinite(&list.tracks[0].maximum), 0);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "fit-content(30%)");

    RoboCheckEqInt(GridReadTracks("grid-template-columns: fit-content(120px)", 1,
                                  &list),
                   (int)RoboCssGridTrackListParseTracks);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "fit-content(120px)");

    /* `fit-content()` takes a length or a percentage and nothing else: `auto` inside
     * it, no argument at all, and two arguments are all refusals. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: fit-content(auto)", 1,
                                  &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: fit-content()", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: fit-content(10px 20px)",
                                  1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
}

/**
 * `repeat()`, expanded **here**.
 *
 * A consumer reads a flat list of tracks and never re-derives a repetition, so the
 * expansion is this reader's job -- and the cases that matter are the two ways it can
 * go wrong: a count of one, which must still expand to its inner list, and a nested
 * `repeat()`, which is invalid CSS and is refused rather than flattened because
 * flattening it would need a depth the grammar does not have.
 */
static void TestGridTemplateRepeat(void)
{
    CssGridTrackList list;
    char text[RoboCssGridTrackListTextMax];

    CssFixture();

    RoboCheckEqInt(GridReadTracks("grid-template-columns: repeat(3, 1fr)", 1, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 3);
    RoboCheckEqInt((int)list.tracks[2].maximum.kind,
                   (int)RoboCssGridBreadthFraction);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "1fr 1fr 1fr");

    /* A repeated *pair*, which is what makes `repeat()` more than a count. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: repeat(2, 40px 80px)", 1,
                                  &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 4);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "40px 80px 40px 80px");

    /* A repeat beside its neighbours, in place. */
    RoboCheckEqInt(GridReadTracks(
                       "grid-template-columns: 10px repeat(2, 1fr) 20px", 1, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 4);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "10px 1fr 1fr 20px");

    /* A count of one is still a repeat, and a named-argument form is not read: the
     * count must be an integer. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: repeat(1, auto)", 1, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 1);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: repeat(2.5, 1fr)", 1,
                                  &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: repeat(0, 1fr)", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: repeat(3)", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    /* A nested repeat is refused rather than flattened. */
    RoboCheckEqInt(GridReadTracks(
                       "grid-template-columns: repeat(2, repeat(2, 1fr))", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);

    /* `auto-fill` and `auto-fit` are **deferred**: their count is the container's used
     * size divided by the track size, which is a layout question this module cannot
     * answer at all. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: repeat(auto-fill, 40px)", 1,
                                  &list),
                   (int)RoboCssGridTrackListParseDeferred);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: repeat(auto-fit, minmax(40px, 1fr))",
                                  1, &list),
                   (int)RoboCssGridTrackListParseDeferred);

    /* And the bound is a **refusal**: a list that would expand past
     * `RoboCssGridMaxTracks` is invalid rather than a prefix of what the author wrote. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: repeat(33, 1fr)", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: repeat(32, 1fr)", 1, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 32);
}

static void TestGridTemplateNoneAndRefusals(void)
{
    CssGridTrackList list;
    char text[RoboCssGridTrackListTextMax];

    CssFixture();

    /* `none` is the templates' initial value and an answer in its own right. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: none", 1, &list),
                   (int)RoboCssGridTrackListParseNone);
    RoboCheckEqInt(list.none, 1);
    RoboCheckEqInt((int)list.count, 0);
    RoboCheckEqInt(CssGridTrackListIsNone(&list), 1);
    GridTrackText(&list, text, sizeof(text));
    RoboCheckStrEq(text, "none");
    RoboCheckEqInt(GridReadTracks("grid-template-columns: NONE", 1, &list),
                   (int)RoboCssGridTrackListParseNone);

    /* ...and it is legal for the *templates* alone: an auto-track list has no `none`,
     * so reading it as "no tracks" would silently make an invalid declaration mean the
     * initial value. */
    RoboCheckEqInt(GridReadTracks("grid-auto-rows: none", 0, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    /* `auto` is the auto-lists' own initial value and reads as one auto track. */
    RoboCheckEqInt(GridReadTracks("grid-auto-rows: auto", 0, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 1);
    RoboCheckEqInt((int)list.tracks[0].minimum.kind, (int)RoboCssGridBreadthAuto);
    /* An auto-track list may be several, and they cycle in the layout pass. */
    RoboCheckEqInt(GridReadTracks("grid-auto-columns: 40px 80px", 0, &list),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt((int)list.count, 2);

    /* The refusals, each a different mistake: a bare number (a value CSS refuses, and
     * reading it as `3px` would guess at a unit the author did not write), a string, a
     * colour function, a bracket (a line name, which this build does not read), the
     * `subgrid` keyword, and an empty value. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: 3", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: \"40px\"", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: rgb(0, 0, 0)", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: [full-start] 40px", 1,
                                  &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: 40px [full-end]", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: subgrid", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: ", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: rotate(3deg)", 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);

    /* A `var()` anywhere is **deferred** rather than refused, because substitution
     * happens before a value is interpreted and can change how many components there
     * are -- `--tracks: 40px 80px` makes one reference two tracks. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: var(--tracks)", 1, &list),
                   (int)RoboCssGridTrackListParseDeferred);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: 40px var(--rest)", 1, &list),
                   (int)RoboCssGridTrackListParseDeferred);

    /* A NULL value is refused rather than answered, and `out` is left a cleared list
     * rather than a half-filled one. */
    RoboCheckEqInt(CssGridTrackListFromValue(NULL, 1, &list),
                   (int)RoboCssGridTrackListParseInvalid);
    RoboCheckEqInt(CssGridTrackListIsNone(&list), 1);
    RoboCheckEqInt(CssGridTrackListFromValue(NULL, 1, NULL),
                   (int)RoboCssGridTrackListParseInvalid);
}

static void TestGridTemplateValueEquality(void)
{
    CssGridTrackList first;
    CssGridTrackList second;
    CssGridTrackBreadth breadth;

    CssFixture();

    RoboCheckEqInt(GridReadTracks("grid-template-columns: 40px 1fr", 1, &first),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt(GridReadTracks("grid-template-columns: 40px 1fr", 1, &second),
                   (int)RoboCssGridTrackListParseTracks);
    /* Structural equality, which is what a determinism assertion is made of. */
    RoboCheckEqInt(CssGridTrackListEqual(&first, &second), 1);

    RoboCheckEqInt(GridReadTracks("grid-template-columns: 40px 2fr", 1, &second),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt(CssGridTrackListEqual(&first, &second), 0);
    /* A track count and a mean nothing (the flag) are both part of it. */
    RoboCheckEqInt(GridReadTracks("grid-template-columns: 40px", 1, &second),
                   (int)RoboCssGridTrackListParseTracks);
    RoboCheckEqInt(CssGridTrackListEqual(&first, &second), 0);

    /* A breadth compares by kind *and* by value: `0` is not `0%`, and `40px` is not
     * `40em`. */
    CssGridTrackBreadthInit(&breadth);
    RoboCheckEqInt(breadth.kind == RoboCssGridBreadthAuto, 1);
    RoboCheckEqInt(CssGridTrackBreadthEqual(&first.tracks[0].minimum,
                                            &first.tracks[0].maximum), 1);
    RoboCheckEqInt(CssGridTrackBreadthEqual(&first.tracks[1].minimum,
                                            &first.tracks[1].maximum), 1);
    /* A cleared breadth is `auto`, which is the property's initial value. */
    RoboCheckEqInt(CssGridTrackBreadthEqual(&breadth, &second.tracks[0].minimum), 0);
    RoboCheckEqInt(CssGridTrackBreadthEqual(NULL, NULL), 1);
    RoboCheckEqInt(CssGridTrackBreadthEqual(NULL, &breadth), 0);

    /* A NULL list is the absence of one, and a track list with no tracks and no `none`
     * is "nothing was declared" rather than `none`. */
    RoboCheckEqInt(CssGridTrackListEqual(NULL, NULL), 1);
    RoboCheckEqInt(CssGridTrackListEqual(NULL, &first), 0);
    CssGridTrackListInit(&second);
    RoboCheckEqInt(second.none, 0);
    RoboCheckEqInt((int)second.count, 0);
    RoboCheckEqInt(CssGridTrackListIsNone(&second), 1);
    RoboCheckEqInt(CssGridTrackListEqual(&first, &second), 0);
    RoboCheckEqInt(CssGridTrackListIsNone(NULL), 1);
}

/* ------------------------------------------------------------------ */
/* Line placements                                                     */
/* ------------------------------------------------------------------ */

static void TestGridTemplateLinePlacements(void)
{
    CssGridLinePlacement placement;
    char text[RoboCssGridLinePlacementTextMax];

    CssFixture();

    /* `auto` is the initial value and the answer for a declaration this build reads
     * and has nothing to place with. */
    RoboCheckEqInt(GridReadLine("grid-column-start: auto", &placement),
                   (int)RoboCssGridLineParsePlacement);
    RoboCheckEqInt((int)placement.kind, (int)RoboCssGridLineAuto);
    RoboCheckEqInt(CssGridLinePlacementIsAuto(&placement), 1);
    GridLineText(&placement, text, sizeof(text));
    RoboCheckStrEq(text, "auto");

    /* Positive line numbers, counted from the first line. */
    RoboCheckEqInt(GridReadLine("grid-column-start: 3", &placement),
                   (int)RoboCssGridLineParsePlacement);
    RoboCheckEqInt((int)placement.kind, (int)RoboCssGridLineNumber);
    RoboCheckEqInt((int)placement.line, 3);
    RoboCheckEqInt(CssGridLinePlacementIsAuto(&placement), 0);
    GridLineText(&placement, text, sizeof(text));
    RoboCheckStrEq(text, "3");

    /* Negative ones, which is what `grid-column: 1 / -1` means -- the last explicit
     * line. The sign is kept rather than resolved here, because "the last line of what"
     * needs the explicit track count and this module has no container. */
    RoboCheckEqInt(GridReadLine("grid-column-end: -1", &placement),
                   (int)RoboCssGridLineParsePlacement);
    RoboCheckEqInt((int)placement.kind, (int)RoboCssGridLineNumber);
    RoboCheckEqInt((int)placement.line, -1);
    GridLineText(&placement, text, sizeof(text));
    RoboCheckStrEq(text, "-1");

    /* `span <integer>`, in that order: `2 span` is not a placement. */
    RoboCheckEqInt(GridReadLine("grid-row-end: span 2", &placement),
                   (int)RoboCssGridLineParsePlacement);
    RoboCheckEqInt((int)placement.kind, (int)RoboCssGridLineSpan);
    RoboCheckEqInt((int)placement.line, 2);
    GridLineText(&placement, text, sizeof(text));
    RoboCheckStrEq(text, "span 2");
    RoboCheckEqInt(GridReadLine("grid-row-end: 2 span", &placement),
                   (int)RoboCssGridLineParseInvalid);

    /* A span counts forward, so zero and a negative count are not placements. */
    RoboCheckEqInt(GridReadLine("grid-row-end: span 0", &placement),
                   (int)RoboCssGridLineParseInvalid);
    RoboCheckEqInt(GridReadLine("grid-row-end: span -1", &placement),
                   (int)RoboCssGridLineParseInvalid);
    /* `span` alone says nothing, and `span 1.5` is not an integer. */
    RoboCheckEqInt(GridReadLine("grid-row-end: span", &placement),
                   (int)RoboCssGridLineParseInvalid);
    RoboCheckEqInt(GridReadLine("grid-row-end: span 1.5", &placement),
                   (int)RoboCssGridLineParseInvalid);

    /* The magnitude bound: a line number nothing could place is refused by the reader
     * that read it rather than clamped three layers down. */
    RoboCheckEqInt(GridReadLine("grid-column-start: 10000", &placement),
                   (int)RoboCssGridLineParsePlacement);
    RoboCheckEqInt(GridReadLine("grid-column-start: 10001", &placement),
                   (int)RoboCssGridLineParseInvalid);
    RoboCheckEqInt(GridReadLine("grid-row-end: span 10001", &placement),
                   (int)RoboCssGridLineParseInvalid);

    /* A **named line** is deferred rather than refused: `[full-start]` and
     * `full-start` are legal CSS whose semantics need a line-name table this build
     * does not keep. */
    RoboCheckEqInt(GridReadLine("grid-column-start: full-start", &placement),
                   (int)RoboCssGridLineParseDeferred);
    RoboCheckEqInt(GridReadLine("grid-column-start: var(--line)", &placement),
                   (int)RoboCssGridLineParseDeferred);

    /* Everything else is refused: a length, an empty value, three components. */
    RoboCheckEqInt(GridReadLine("grid-column-start: 40px", &placement),
                   (int)RoboCssGridLineParseInvalid);
    RoboCheckEqInt(GridReadLine("grid-column-start: ", &placement),
                   (int)RoboCssGridLineParseInvalid);
    RoboCheckEqInt(GridReadLine("grid-column-start: 1 2 3", &placement),
                   (int)RoboCssGridLineParseInvalid);

    /* A cleared record is `auto`, and the door answers for a NULL one rather than
     * dereferencing it. */
    CssGridLinePlacementInit(&placement);
    RoboCheckEqInt((int)placement.kind, (int)RoboCssGridLineAuto);
    RoboCheckEqInt(CssGridLinePlacementIsAuto(NULL), 1);
    RoboCheckEqInt(CssGridLinePlacementEqual(&placement, &placement), 1);
    RoboCheckEqInt(CssGridLinePlacementEqual(NULL, NULL), 1);
    RoboCheckEqInt(CssGridLinePlacementEqual(NULL, &placement), 0);
    RoboCheckEqInt(CssGridLinePlacementFromValue(NULL, &placement),
                   (int)RoboCssGridLineParseInvalid);
    RoboCheckEqInt(CssGridLinePlacementFromValue(NULL, NULL),
                   (int)RoboCssGridLineParseInvalid);
}

/* ------------------------------------------------------------------ */
/* The writers                                                         */
/* ------------------------------------------------------------------ */

/**
 * The buffer discipline every writer in this module shares: a refusal reports the
 * length it *needed* and leaves the buffer empty, so a caller sizes from the refusal
 * rather than from a constant it has to keep in step.
 */
static void TestGridTemplateWriters(void)
{
    CssGridTrackList list;
    char small[RoboCssGridTrackListTextMax];
    size_t length = 0u;

    CssFixture();

    RoboCheckEqInt(GridReadTracks("grid-template-columns: 40px 1fr", 1, &list),
                   (int)RoboCssGridTrackListParseTracks);

    /* Too small for `40px 1fr`, and the length it needed is reported. The refused call
     * is given the capacity the *text* needs anyway -- a writer told it has room and
     * then handed a shorter buffer is the one thing this discipline exists to prevent,
     * and the case below is a check of the arithmetic rather than of a buffer. */
    small[0] = 'x';
    small[1] = '\0';
    RoboCheckEqInt(CssGridTrackListWrite(&list, small, 4u, &length, NULL),
                   RoboNotSupported);
    RoboCheckStrEq(small, "");
    RoboCheckEqInt((int)length, 8);
    /* Exactly big enough is enough: the eight bytes of `40px 1fr` plus the NUL. */
    RoboCheckEqInt(CssGridTrackListWrite(&list, small, 9u, &length, NULL),
                   RoboOk);
    RoboCheckStrEq(small, "40px 1fr");

    /* NULL arguments are refused rather than written through, and an absent list has
     * no text at all. */
    RoboCheckEqInt(CssGridTrackListWrite(&list, NULL, 8u, &length, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(CssGridTrackListWrite(&list, small, 8u, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(CssGridTrackListWrite(&list, small, 0u, &length, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(CssGridTrackListWrite(NULL, small, sizeof(small), &length, NULL),
                   RoboInvalidArgument);
    {
        CssGridTrackList empty;

        CssGridTrackListInit(&empty);
        RoboCheckEqInt(CssGridTrackListWrite(&empty, small, sizeof(small), &length,
                                             NULL),
                       RoboOk);
        RoboCheckStrEq(small, "none");
    }

    /* A line placement's writer, with the same discipline. */
    {
        CssGridLinePlacement placement;
        char tiny[RoboCssGridLinePlacementTextMax];

        RoboCheckEqInt(GridReadLine("grid-row-end: span 10000", &placement),
                       (int)RoboCssGridLineParsePlacement);
        /* The refusal is asked with a capacity that really is too small and a buffer
         * that really is large enough for its own capacity -- the one rule a test of a
         * buffer discipline has to keep, or it proves the overflow rather than the
         * refusal. */
        RoboCheckEqInt(CssGridLinePlacementWrite(&placement, tiny, 4u, &length,
                                                 NULL),
                       RoboNotSupported);
        RoboCheckStrEq(tiny, "");
        RoboCheckEqInt((int)length, 10);
        RoboCheckEqInt(CssGridLinePlacementWrite(&placement, tiny, 11u, &length,
                                                NULL),
                       RoboOk);
        RoboCheckStrEq(tiny, "span 10000");
        RoboCheckEqInt(CssGridLinePlacementWrite(NULL, tiny, sizeof(tiny), &length,
                                                 NULL),
                       RoboInvalidArgument);
        RoboCheckEqInt(CssGridLinePlacementWrite(&placement, NULL, sizeof(tiny),
                                                 &length, NULL),
                       RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */

void TestRegisterCssGridTemplate(void);

void TestRegisterCssGridTemplate(void)
{
    TestRegister("css.gridtemplate.breadth_kinds", TestGridTemplateBreadthKinds);
    TestRegister("css.gridtemplate.tracks", TestGridTemplateSimpleTracks);
    TestRegister("css.gridtemplate.fraction", TestGridTemplateFractionTracks);
    TestRegister("css.gridtemplate.minmax", TestGridTemplateMinMax);
    TestRegister("css.gridtemplate.fit_content", TestGridTemplateFitContent);
    TestRegister("css.gridtemplate.repeat", TestGridTemplateRepeat);
    TestRegister("css.gridtemplate.none", TestGridTemplateNoneAndRefusals);
    TestRegister("css.gridtemplate.equality", TestGridTemplateValueEquality);
    TestRegister("css.gridtemplate.lines", TestGridTemplateLinePlacements);
    TestRegister("css.gridtemplate.writers", TestGridTemplateWriters);
}
