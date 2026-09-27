/*
 * RoboCSS Phase 9.1 foundation suite:
 * module lifecycle, version, shared types, diagnostics, capabilities.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSVersion.h"

static void TestCssVersion(void)
{
    I32 major = 0;
    I32 minor = 0;
    I32 patch = 0;

    RoboCheckStrEq(CssVersionString(), RoboCssVersionString);
    RoboCheckEqInt(RoboCssVersionMajor, 0);
    RoboCheckEqInt(RoboCssVersionMinor, 35);
    RoboCheckEqInt(RoboCssVersionPatch, 0);
    /* Architecture milestone: the surface grows all through Phase 9. */
    RoboCheckEqInt(RoboCssApiFrozen, 0);

    RoboCheckEqInt(CssGetVersion(NULL, &minor, &patch),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssGetVersion(&major, NULL, &patch),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssGetVersion(&major, &minor, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssGetVersion(&major, &minor, &patch), RoboOk);
    RoboCheckEqInt(major, 0);
    RoboCheckEqInt(minor, 35);
    RoboCheckEqInt(patch, 0);
}

static void TestCssModuleInitShutdown(void)
{
    CssFixture();
    RoboCheck(CssIsInitialized());

    /* Double init is refused and changes nothing. */
    RoboCheckEqInt(CssInit(), RoboAlreadyExists);
    RoboCheck(CssIsInitialized());

    /* Live count is about stylesheets, not about the module being up. */
    RoboCheckEqInt(CssLiveStylesheetCount(), 0u);

    CssShutdown();
    RoboCheckEqInt(CssIsInitialized(), 0);

    /* Idempotent, and the count of a down module is 0 rather than stale. */
    CssShutdown();
    RoboCheckEqInt(CssIsInitialized(), 0);
    RoboCheckEqInt(CssLiveStylesheetCount(), 0u);

    RoboCheckEqInt(CssInit(), RoboOk);
    RoboCheck(CssIsInitialized());
}

static void TestCssModuleShutdownReleasesStylesheets(void)
{
    CssStylesheetId first = RoboCssStylesheetIdNone;
    CssStylesheetId second = RoboCssStylesheetIdNone;

    CssFixture();
    RoboCheckEqInt(CssStylesheetCreate(&first, RoboCssOriginAuthor, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStylesheetCreate(&second, RoboCssOriginRuntime, NULL),
                      RoboOk);
    RoboCheckEqInt(CssLiveStylesheetCount(), 2u);
    RoboCheck(CssStylesheetIsLive(first));
    RoboCheck(CssStylesheetIsLive(second));

    CssShutdown();
    RoboCheckEqInt(CssLiveStylesheetCount(), 0u);
    /* A destroyed stylesheet's id is dead, and stays dead. */
    RoboCheckEqInt(CssStylesheetIsLive(first), 0);

    RoboCheckEqInt(CssInit(), RoboOk);
    RoboCheckEqInt(CssStylesheetIsLive(first), 0);
}

static void TestCssStylesheetRequiresModule(void)
{
    CssStylesheetId id = RoboCssStylesheetIdNone;
    CssDiag diag;

    CssFixture();
    CssShutdown();

    CssDiagReset(&diag);
    RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor, &diag),
                      RoboInitError);
    RoboCheckEqInt(id, RoboCssStylesheetIdNone);
    RoboCheckEqInt(diag.code, RoboInitError);
    RoboCheckEqInt(diag.domain, RoboCssDomainStylesheet);
    RoboCheck(diag.message != NULL);

    RoboCheckEqInt(CssInit(), RoboOk);
}

static void TestCssSlotTableIsBounded(void)
{
    size_t i;
    CssStylesheetId id = RoboCssStylesheetIdNone;
    CssStylesheetId liveIds[RoboCssMaxStylesheets];

    CssFixture();
    for (i = 0u; i < (size_t)RoboCssMaxStylesheets; i++) {
        RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor, NULL),
                          RoboOk);
    }
    RoboCheckEqInt(CssLiveStylesheetCount(), (size_t)RoboCssMaxStylesheets);
    /* The table is full: the next create is refused rather than overwriting. */
    RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor, NULL),
                      RoboOutOfMemory);

    /* Snapshot first, destroy second. id_at enumerates the *compacted* live
     * set, so destroying slot 0 shifts every later index down one and a
     * single-pass loop would silently skip every other stylesheet — the loop
     * would end holding half the table and still report success. */
    for (i = 0u; i < (size_t)RoboCssMaxStylesheets; i++) {
        liveIds[i] = CssStylesheetIdAt(i);
        RoboCheck(liveIds[i] != RoboCssStylesheetIdNone);
    }
    /* Beyond the live set there is nothing to name, and it says so. */
    RoboCheckEqInt(CssStylesheetIdAt((size_t)RoboCssMaxStylesheets),
                      RoboCssStylesheetIdNone);
    for (i = 0u; i < (size_t)RoboCssMaxStylesheets; i++) {
        RoboCheckEqInt(CssStylesheetDestroy(liveIds[i], NULL), RoboOk);
    }
    RoboCheckEqInt(CssLiveStylesheetCount(), 0u);
    /* Every id from the snapshot is dead once destroyed, including the last. */
    for (i = 0u; i < (size_t)RoboCssMaxStylesheets; i++) {
        RoboCheckEqInt(CssStylesheetIsLive(liveIds[i]), 0);
    }
    RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStylesheetDestroy(id, NULL), RoboOk);
}

static void TestCssTypesSlices(void)
{
    const uint8_t bytes[] = {'a', 'b', 'c'};
    CssSlice absent = CssSliceEmpty();
    CssSlice three = CssSliceFromBytes(bytes, 3u);
    char text[8];
    uint8_t raw[8];

    RoboCheck(CssSliceIsAbsent(absent));
    RoboCheck(CssSliceIsEmpty(absent));
    RoboCheck(!CssSliceIsAbsent(three));
    RoboCheck(!CssSliceIsEmpty(three));

    /* An absent slice and an empty slice are different facts but compare equal
     * by length, which is what makes a borrowed "nothing" safe to pass around. */
    RoboCheck(CssSliceEquals(absent, CssSliceEmpty()));
    RoboCheck(!CssSliceEquals(absent, three));

    RoboCheck(CssSliceEqualsAscii(three, "abc"));
    RoboCheck(!CssSliceEqualsAscii(three, "ab"));
    RoboCheck(CssSliceEqualsAsciiCi(CssSliceFromText("CaRrOt"),
                                              "carrot"));
    RoboCheck(!CssSliceEqualsAscii(CssSliceFromText("CaRrOt"),
                                            "carrot"));
    RoboCheck(CssSliceStartsWithAscii(three, "ab"));
    RoboCheck(!CssSliceStartsWithAscii(three, "bc"));
    RoboCheck(CssSliceStartsWithAscii(three, ""));

    RoboCheckEqInt(CssSliceCopyBytes(three, raw, sizeof(raw)), RoboOk);
    RoboCheckEqInt(raw[0], 'a');
    RoboCheckEqInt(raw[2], 'c');
    /* Too little capacity is refused rather than overflowing. */
    RoboCheckEqInt(CssSliceCopyBytes(three, raw, 2u), RoboInvalidArgument);
    RoboCheckEqInt(CssSliceCopyString(three, text, sizeof(text)), RoboOk);
    RoboCheckStrEq(text, "abc");
    RoboCheckEqInt(CssSliceCopyString(three, text, 3u), RoboInvalidArgument);
}

static void TestCssTypesSplitWhitespace(void)
{
    CssSlice fields[8];
    int truncated = 0;
    size_t count;

    count = CssSplitAsciiWhitespace(CssSliceFromText("  a\tbb\nccc "),
                                            fields, 8u, &truncated);
    RoboCheckEqInt(count, 3u);
    RoboCheckEqInt(truncated, 0);
    RoboCheck(CssSliceEqualsAscii(fields[0], "a"));
    RoboCheck(CssSliceEqualsAscii(fields[1], "bb"));
    RoboCheck(CssSliceEqualsAscii(fields[2], "ccc"));

    /* Whitespace-only and empty text produce nothing at all. */
    RoboCheckEqInt(CssSplitAsciiWhitespace(CssSliceFromText("  \r\n "),
                                                      fields, 8u, &truncated),
                      0u);
    RoboCheckEqInt(CssSplitAsciiWhitespace(CssSliceEmpty(), fields,
                                                      8u, &truncated),
                      0u);
    RoboCheckEqInt(truncated, 0);

    /* A capacity smaller than the field count writes what fits and says so. */
    count = CssSplitAsciiWhitespace(CssSliceFromText("a b c d"),
                                            fields, 2u, &truncated);
    RoboCheckEqInt(count, 2u);
    RoboCheckEqInt(truncated, 1);
    RoboCheck(CssSliceEqualsAscii(fields[0], "a"));
    RoboCheck(CssSliceEqualsAscii(fields[1], "b"));
}

static void TestCssTypesPositionsAndRanges(void)
{
    CssPosition position;
    CssRange range;

    position = CssPositionNone();
    RoboCheck(!CssPositionHasLine(&position));
    position = CssPositionAt(7u);
    RoboCheckEqInt(position.offset, 7u);
    RoboCheckEqInt(position.line, 0u);
    RoboCheck(!CssPositionHasLine(&position));

    range = CssRangeAt(4u);
    RoboCheck(CssRangeIsEmpty(range));
    RoboCheckEqInt(CssRangeLength(range), 0u);
    RoboCheck(!CssRangeContains(range, 4u));

    range = CssRangeSpan(2u, 3u);
    RoboCheckEqInt(range.begin, 2u);
    RoboCheckEqInt(range.end, 5u);
    RoboCheckEqInt(CssRangeLength(range), 3u);
    RoboCheck(!CssRangeIsEmpty(range));
    RoboCheck(CssRangeContains(range, 2u));
    RoboCheck(CssRangeContains(range, 4u));
    /* Half-open: the end offset itself is not inside. */
    RoboCheck(!CssRangeContains(range, 5u));

    /* A span that would wrap saturates instead of overflowing. */
    range = CssRangeSpan((size_t)-1 - 1u, 8u);
    RoboCheckEqInt(range.end, (size_t)-1);
    /* An inverted range has no length and contains nothing. */
    range = (CssRange){5u, 2u};
    RoboCheckEqInt(CssRangeLength(range), 0u);
    RoboCheck(!CssRangeContains(range, 3u));
}

static void TestCssTypesVocabulary(void)
{
    CssOrigin origin;

    RoboCheck(CssOriginIsKnown(RoboCssOriginNone));
    RoboCheck(CssOriginIsKnown(RoboCssOriginInline));
    RoboCheck(!CssOriginIsKnown((CssOrigin)99));

    /* Inline is provenance without a stylesheet: it is never a sheet origin. */
    for (origin = RoboCssOriginNone; origin < (CssOrigin)RoboCssOriginCount;
         origin = (CssOrigin)(origin + 1)) {
        const char *name = CssOriginName(origin);

        RoboCheck(name != NULL);
        RoboCheck(strcmp(name, "unknown") != 0);
        if (origin == RoboCssOriginInline) {
            RoboCheck(CssOriginIsInline(origin));
            RoboCheck(!CssOriginIsStylesheetOrigin(origin));
        } else if (origin != RoboCssOriginNone) {
            RoboCheck(CssOriginIsStylesheetOrigin(origin));
            RoboCheck(!CssOriginIsInline(origin));
        }
    }
    RoboCheckStrEq(CssOriginName((CssOrigin)99), "unknown");

    RoboCheck(CssStylesheetStateIsKnown(RoboCssStylesheetStateParsed));
    RoboCheck(!CssStylesheetStateIsKnown((CssStylesheetState)42));
    /* NONE is "no stylesheet", not a state a parse can settle into. */
    RoboCheck(!CssStylesheetStateIsSettled(RoboCssStylesheetStateNone));
    RoboCheck(!CssStylesheetStateIsSettled(RoboCssStylesheetStateCreated));
    RoboCheck(CssStylesheetStateIsSettled(RoboCssStylesheetStateParsed));
    RoboCheck(CssStylesheetStateIsSettled(RoboCssStylesheetStateFailed));

    RoboCheck(CssDiagnosticDomainIsKnown(RoboCssDomainParser));
    RoboCheck(!CssDiagnosticDomainIsKnown((CssDiagnosticDomain)99));
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainComputed),
                      "computed");
    RoboCheckStrEq(CssDiagnosticDomainName((CssDiagnosticDomain)99),
                      "unknown");
}

static void TestCssErrorVocabulary(void)
{
    CssErrorKind kind;
    size_t recoverable = 0u;
    size_t fatal = 0u;

    RoboCheck(CssErrorKindIsKnown(RoboCssErrorKindNone));
    RoboCheck(!CssErrorKindIsKnown((CssErrorKind)99));
    RoboCheckStrEq(CssErrorKindName(RoboCssErrorKindUnsupported),
                      "unsupported");
    RoboCheckStrEq(CssErrorKindName((CssErrorKind)99), "unknown");

    /* The two classes are disjoint and cover every kind except "not an error". */
    for (kind = RoboCssErrorKindNone;
         kind < (CssErrorKind)RoboCssErrorKindCount;
         kind = (CssErrorKind)(kind + 1)) {
        int isRecoverable = CssErrorKindIsRecoverable(kind);
        int isFatal = CssErrorKindIsFatal(kind);

        RoboCheck(!(isRecoverable && isFatal));
        if (kind == RoboCssErrorKindNone) {
            RoboCheck(!CssErrorKindIsError(kind));
            RoboCheck(!isRecoverable);
            RoboCheck(!isFatal);
        } else {
            RoboCheck(CssErrorKindIsError(kind));
            RoboCheck(isRecoverable || isFatal);
        }
        recoverable += (size_t)(isRecoverable ? 1 : 0);
        fatal += (size_t)(isFatal ? 1 : 0);
    }
    RoboCheckEqInt(recoverable + fatal, (size_t)RoboCssErrorKindCount - 1u);

    /* A malformed declaration is something a parser continues past; an
     * unsupported feature is not. That is the distinction this vocabulary
     * exists to make. */
    RoboCheck(CssErrorKindIsRecoverable(
        RoboCssErrorKindMalformedDeclaration));
    RoboCheck(CssErrorKindIsRecoverable(RoboCssErrorKindUnexpectedEof));
    RoboCheck(CssErrorKindIsFatal(RoboCssErrorKindOutOfMemory));
}

static void TestCssDiagRecord(void)
{
    CssDiag diag;

    CssDiagSet(&diag, RoboNotFound, RoboCssDomainSource, "gone", "f.c", 12);
    RoboCheckEqInt(diag.code, RoboNotFound);
    RoboCheckEqInt(diag.domain, RoboCssDomainSource);
    RoboCheckStrEq(diag.message, "gone");
    RoboCheckStrEq(diag.file, "f.c");
    RoboCheckEqInt(diag.line, 12);

    /* RoboOk clears rather than recording a success. */
    RoboCheckEqInt(CssDiagSet(&diag, RoboOk, RoboCssDomainSource, "x",
                                        "f.c", 1),
                      RoboOk);
    RoboCheckEqInt(diag.code, RoboOk);
    RoboCheckEqInt(diag.domain, RoboCssDomainNone);
    RoboCheck(diag.message == NULL);

    /* An optional diagnostic: the macro returns the code either way, and a
     * caller passing NULL gets no diagnostic rather than a crash. */
    RoboCheckEqInt(RoboCssDiag(NULL, RoboCssDomainToken, RoboTimeout, "t"),
                      RoboTimeout);
    RoboCheckEqInt(RoboCssDiag(&diag, RoboCssDomainToken, RoboTimeout, "t"),
                      RoboTimeout);
    RoboCheckEqInt(diag.domain, RoboCssDomainToken);

    /* An unknown domain is recorded as NONE instead of being trusted. */
    CssDiagSet(&diag, RoboError, (CssDiagnosticDomain)99, "m", NULL, 0);
    RoboCheckEqInt(diag.domain, RoboCssDomainNone);
    RoboCheckEqInt(CssDiagSet(NULL, RoboError, RoboCssDomainNone, "m",
                                        NULL, 0),
                      RoboInvalidArgument);
}

static void TestCssCapabilities(void)
{
    CssCapabilities caps;

    RoboCheckEqInt(CssGetCapabilities(NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);

    /* The model this phase builds is present... */
    RoboCheckEqInt(caps.sourceModel, 1);
    RoboCheckEqInt(caps.tokenModel, 1);
    RoboCheckEqInt(caps.valueModel, 1);
    RoboCheckEqInt(caps.propertyRegistry, 1);
    RoboCheckEqInt(caps.declarationModel, 1);
    RoboCheckEqInt(caps.selectorModel, 1);
    RoboCheckEqInt(caps.ruleModel, 1);
    RoboCheckEqInt(caps.stylesheetModel, 1);
    RoboCheckEqInt(caps.errorModel, 1);

    /* Two capabilities under that model have become real: 9.2 turns bytes into
     * tokens and 9.3 turns tokens into rules, declarations and component values
     * — including an inline `style` attribute's text, which goes through the
     * same declaration-list grammar. Everything above them is still only
     * described. */
    RoboCheckEqInt(caps.tokenizer, 1);
    RoboCheckEqInt(caps.parser, 1);
    RoboCheckEqInt(caps.inlineStyles, 1);

    /* 9.4 turned a prelude into selectors and made them matchable. Both flags
     * are 1 because both halves exist end to end — not because the model gained
     * a field, but because RoboCSSSelectorParser.c and RoboCSSMatcher.c back
     * them. */
    RoboCheckEqInt(caps.selectorParsing, 1);
    RoboCheckEqInt(caps.selectorMatching, 1);

    /* 9.5 collects what applies to an element in cascade order and answers a
     * property whenever a level or the document order decides it. */
    RoboCheckEqInt(caps.cascade, 1);

    /* 9.6 turned that reported-undecided contest between two candidates of one
     * level from different selectors into a decision: the cascade consumes all
     * three keys and holds no specificity arithmetic of its own. */
    RoboCheckEqInt(caps.specificity, 1);

    /* 9.7 answers the question the cascade cannot: for every property an
     * element does not declare, whether it takes the parent's answer or the
     * property's own initial value. */
    RoboCheckEqInt(caps.inheritance, 1);

    /* 9.8 turns that resolved style into a *computed* one — a length with a
     * unit, a percentage, a colour, a keyword — through the property's declared
     * value shape. What it does not do is resolve: `50%` stays a percentage and
     * `var()` stays a reference, which is why nothing here claims a used value. */
    RoboCheckEqInt(caps.computedValues, 1);

    /* 9.9 gave `color` values a real representation: the standard named set, the
     * four hex forms, `rgb()`/`rgba()` and `hsl()`/`hsla()`, and `currentColor`
     * as a contextual colour the computed style resolves. The spaces this build
     * has no representation for are deferred rather than claimed, which is why
     * this is a flag for colours and not for the whole CSS Color specification. */
    RoboCheckEqInt(caps.colors, 1);

    /* 9.10 gave numbers and units a vocabulary of their own: a closed unit set
     * across absolute, font-relative and viewport families, exact conversion of
     * the absolute ones to the reference pixel, and a typed numeric value whose
     * equality distinguishes `0` from `0%` from `0px`. Contextual units are
     * carried rather than resolved, which is why this claims the value system
     * and not the resolution of one. */
    RoboCheckEqInt(caps.units, 1);

    /* 9.11 gave the box model a vocabulary of its own: the four sides in CSS's
     * order, the three shorthands this build distributes and the longhand each
     * side of each stands for, and typed values for the twelve longhands. What
     * it claims is the *value family*, not the geometry — no edge is measured,
     * `50%` stays a percentage and `auto` stays the keyword — and `border-width`
     * is distributed although nothing paints a border, because the four widths
     * are the four values. */
    RoboCheckEqInt(caps.boxModel, 1);

    /* 9.12 gave `display` a vocabulary of its own: a closed set of four values,
     * each a row carrying its keyword and what CSS Display 3 calls its outer and
     * inner halves, with the classifications a layout pass asks. `display: none`
     * is the *absence of a box* rather than zero geometry, which is why this
     * claims a value family and not layout — nothing here removes an element from
     * a tree that does not exist yet. */
    RoboCheckEqInt(caps.display, 1);

    /* 9.13 gave positioning a vocabulary and a family (RoboCSSPosition.h): the
     * five `position` values, each a row carrying whether its box stays in flow
     * and whether it is a positioned box, the four inset properties on the box
     * model's EDGE_OFFSET shape, and `z-index` as `auto` or a whole number. No
     * containing block is computed and no offset is applied, which is why this
     * claims values and their classifications rather than geometry. */
    RoboCheckEqInt(caps.positioning, 1);

    /* ...and everything past that is still 0: no default stylesheet exists, and
     * nothing loads one. */
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    /* Available before the module is up: it describes the build, not the state. */
    CssFixture();
    CssShutdown();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.parser, 1);
    RoboCheckEqInt(CssInit(), RoboOk);

    CssCapabilitiesInit(&caps);
    RoboCheckEqInt(caps.parser, 0);
    RoboCheckEqInt(caps.sourceModel, 0);
    CssCapabilitiesInit(NULL); /* no crash, no state */
}

static void TestCssDefaultStylesheetBoundary(void)
{
    size_t length = 123u;
    const char *text;

    CssFixture();
    text = CssDefaultStylesheetText(&length);
    /* The boundary exists and this build ships nothing: NULL with length 0,
     * which a caller must read as "no runtime styles" rather than as failure. */
    RoboCheck(text == NULL);
    RoboCheckEqInt(length, 0u);

    length = 77u;
    RoboCheck(CssDefaultStylesheetText(&length) == NULL);
    RoboCheckEqInt(length, 0u);
    /* The length parameter is optional. */
    RoboCheck(CssDefaultStylesheetText(NULL) == NULL);
}

/*
 * The record-lifetime rule, asserted rather than only documented.
 *
 * RoboCssCleared is what lets a by-value RoboCSS record be declared without a
 * separate init call, and the whole model depends on the cleared state being
 * exactly what the init functions produce — because every make/append/dispose
 * releases what the record already holds. If a cleared record were ever *not*
 * the same as an init'd one, every caller of RoboCssCleared would be releasing
 * fields nobody set. This test pins that equivalence field by field.
 */
static void TestCssRecordsClearedState(void)
{
    CssValue clearedValue = RoboCssCleared;
    CssValue initValue = RoboCssCleared;
    CssToken clearedToken = RoboCssCleared;
    CssSource clearedSource = RoboCssCleared;
    CssSource initSource = RoboCssCleared;
    CssDeclaration clearedDeclaration = RoboCssCleared;
    CssDeclaration initDeclaration = RoboCssCleared;
    CssSimpleSelector clearedSelector = RoboCssCleared;
    CssCompoundSelector clearedCompound = RoboCssCleared;
    CssComplexSelector clearedComplex = RoboCssCleared;
    CssSelectorList clearedList = RoboCssCleared;
    CssRule clearedRule = RoboCssCleared;
    CssRuleList clearedRules = RoboCssCleared;
    CssError clearedError = RoboCssCleared;
    CssParser clearedParser = RoboCssCleared;

    CssFixture();

    CssValueInit(&initValue);
    RoboCheckEqInt(clearedValue.kind, initValue.kind);
    RoboCheckEqInt(clearedValue.kind, RoboCssValueKindNone);
    RoboCheck(CssValueIsEmpty(&clearedValue));
    RoboCheckEqInt(clearedValue.children.count, initValue.children.count);
    RoboCheck(clearedValue.children.items == NULL);
    RoboCheckEqInt(clearedValue.children.capacity, 0u);
    RoboCheck(CssSliceIsAbsent(clearedValue.name));
    RoboCheck(CssRangeIsEmpty(clearedValue.range));
    RoboCheckEqInt(CssValueDepth(&clearedValue), 0u);
    RoboCheckEqInt(CssValueTreeCount(&clearedValue), 0u);
    RoboCheckEqInt(CssValueChildCount(&clearedValue), 0u);
    RoboCheckEqInt(CssValueChild(&clearedValue, 0u) != NULL, 0);

    /* Every cleared record is a usable starting point, not an inert one: each
     * can be configured or released without a trip through its init function. */
    RoboCheckEqInt(CssValueMakeList(&clearedValue, NULL), RoboOk);
    RoboCheckEqInt(clearedValue.kind, RoboCssValueKindList);
    CssValueDispose(&clearedValue);
    RoboCheck(CssValueIsEmpty(&clearedValue));
    CssValueDispose(&clearedValue); /* idempotent */
    RoboCheckEqInt(CssValueMakeToken(&clearedValue, &clearedToken, NULL),
                      RoboOk);
    RoboCheckEqInt(clearedValue.kind, RoboCssValueKindToken);
    RoboCheckEqInt(CssValueToken(&clearedValue).type, RoboCssTokenNone);
    CssValueDispose(&clearedValue);

    RoboCheck(!CssSourceIsConfigured(&clearedSource));
    CssSourceInit(&initSource);
    RoboCheckEqInt(clearedSource.kind, initSource.kind);
    RoboCheckEqInt(clearedSource.kind, RoboCssSourceKindNone);
    RoboCheck(CssSourceIsEmpty(&clearedSource));
    RoboCheck(CssSourceBytes(&clearedSource) == NULL);
    RoboCheckEqInt(CssSourceLength(&clearedSource), 0u);
    RoboCheck(CssSourceName(&clearedSource) != NULL);
    CssSourceDispose(&clearedSource);

    CssDeclarationInit(&initDeclaration);
    RoboCheckEqInt(clearedDeclaration.state, initDeclaration.state);
    RoboCheckEqInt(clearedDeclaration.state, RoboCssDeclarationStateNone);
    RoboCheck(!CssDeclarationIsUsable(&clearedDeclaration));
    RoboCheck(!CssDeclarationHasValue(&clearedDeclaration));
    RoboCheck(CssValueIsEmpty(&clearedDeclaration.value));
    CssDeclarationDispose(&clearedDeclaration);

    RoboCheck(!CssSimpleSelectorIsSet(&clearedSelector));
    RoboCheckEqInt(clearedSelector.kind, RoboCssSimpleSelectorNone);
    RoboCheckEqInt(CssCompoundSelectorCount(&clearedCompound), 0u);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(&clearedComplex), 0u);
    RoboCheckEqInt(CssSelectorListCount(&clearedList), 0u);
    RoboCheck(!CssSelectorListIsWellFormed(&clearedList));

    RoboCheckEqInt(clearedRule.kind, RoboCssRuleKindNone);
    RoboCheckEqInt(CssRuleListCount(&clearedRules), 0u);

    RoboCheck(!CssErrorIsError(&clearedError));
    RoboCheckEqInt(clearedError.kind, RoboCssErrorKindNone);
    RoboCheckStrEq(CssErrorMessage(&clearedError), "");

    RoboCheck(CssParserSource(&clearedParser) == NULL);
    RoboCheckEqInt(CssParserOffset(&clearedParser), 0u);
    RoboCheck(!CssParserHasError(&clearedParser));

    /* Release is safe on every cleared record, including twice. */
    CssValueDispose(&clearedValue);
    CssValueClearChildren(&clearedValue);
    CssSourceDispose(&clearedSource);
    CssDeclarationDispose(&clearedDeclaration);
    CssSimpleSelectorDispose(&clearedSelector);
    CssCompoundSelectorDispose(&clearedCompound);
    CssComplexSelectorDispose(&clearedComplex);
    CssSelectorListDispose(&clearedList);
    CssRuleDispose(&clearedRule);
    CssRuleListDispose(&clearedRules);
    CssParserDispose(&clearedParser);
    CssErrorInit(&clearedError);
}

void TestRegisterCss(void);

void TestRegisterCss(void)
{
    TestRegister("css.version", TestCssVersion);
    TestRegister("css.module.init_shutdown", TestCssModuleInitShutdown);
    TestRegister("css.module.shutdown_releases_stylesheets",
                       TestCssModuleShutdownReleasesStylesheets);
    TestRegister("css.module.requires_init", TestCssStylesheetRequiresModule);
    TestRegister("css.module.slot_table_bounded", TestCssSlotTableIsBounded);
    TestRegister("css.types.slices", TestCssTypesSlices);
    TestRegister("css.types.split_whitespace", TestCssTypesSplitWhitespace);
    TestRegister("css.types.positions_and_ranges",
                       TestCssTypesPositionsAndRanges);
    TestRegister("css.types.vocabulary", TestCssTypesVocabulary);
    TestRegister("css.error.vocabulary", TestCssErrorVocabulary);
    TestRegister("css.diag.record", TestCssDiagRecord);
    TestRegister("css.capabilities", TestCssCapabilities);
    TestRegister("css.default_stylesheet.boundary",
                       TestCssDefaultStylesheetBoundary);
    TestRegister("css.records.cleared_state", TestCssRecordsClearedState);
}
