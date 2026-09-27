/*
 * RoboCSS text suite (Phase 18.15) — css.text.*
 *
 * Ten closed sets over a family whose central fact is that **six words hide four
 * independent questions**, the shared closed-vocabulary computation body they are read
 * through, and the registry rows all of it is registered by.
 *
 * Three things this suite is really about, and each is a fact no single accessor call
 * could state.
 *
 * First, `white-space`'s four classifications. `normal` and `pre-line` agree on
 * collapsing and on wrapping and differ only on whether a newline survives; `pre-wrap`
 * and `break-spaces` agree on all three of the first columns and differ only on whether
 * a *preserved* space may be broken after. A build with one \"white-space\" flag could
 * not tell either pair apart, and the case walks all six rows rather than the two ends.
 *
 * Second, the classifications that are *not* each other's negation. `text-align`'s
 * `start`/`end` are direction-relative and `left`/`right` are physical, while `center`
 * is neither — so `IsLogical` and `IsPhysical` are both false for it, and a consumer
 * that read \"not logical\" as \"physical\" would map a `center` through a direction.
 * The same shape is asserted for an unknown enumerator: it answers the value that
 * changes least, not the one that looks like a default.
 *
 * Third, the split by *grammar*. Ten properties have a genuinely closed set and take a
 * typed shape, so `white-space: banana` and `text-align: middle` are refused with a
 * sentence. The rest have a grammar wider than any closed set — `text-decoration-line`
 * is a *set*, `text-overflow` may be a `<string>`, `text-decoration-thickness` may be a
 * length — so their declarations are CARRIED and the closed sets inside them are read
 * through RoboCSSText.h.
 *
 * Nothing here wraps a line, distributes slack, finds a hyphenation point or changes a
 * character's case. A value at this phase is a value with classifications in it, and
 * every expectation below is about the value, what it classifies as, or where it came
 * from.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSText.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* text-align                                                          */
/* ------------------------------------------------------------------ */

static void TestTextAlignVocabulary(void)
{
    static const char *const keywords[] = {
        "start", "end", "left", "right", "center", "justify", "match-parent"
    };
    static const struct {
        int logical;
        int physical;
        int justified;
    } expected[] = {
        {1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1},
        {1, 0, 0}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssTextAlignCount, 7);
    RoboCheckEqInt((int)CssTextAlignCount(), 7);
    for (index = 0u; index < 7u; index++) {
        CssTextAlign align = (CssTextAlign)index;

        RoboCheckEqInt(CssTextAlignIsKnown(align), 1);
        RoboCheckStrEq(CssTextAlignName(align), keywords[index]);
        RoboCheckEqInt(CssTextAlignFromText(keywords[index]), (int)align);
        RoboCheck(CssTextAlignSliceIsKnown(CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssTextAlignIsLogical(align), expected[index].logical);
        RoboCheckEqInt(CssTextAlignIsPhysical(align), expected[index].physical);
        RoboCheckEqInt(CssTextAlignIsJustified(align), expected[index].justified);
    }
    RoboCheckEqInt(CssTextAlignIsKnown((CssTextAlign)99), 0);
    RoboCheckStrEq(CssTextAlignName((CssTextAlign)99), "");
    RoboCheckEqInt(CssTextAlignFromText("CENTER"), (int)RoboCssTextAlignCenter);
    RoboCheckEqInt(CssTextAlignFromText(NULL), (int)RoboCssTextAlignStart);
    RoboCheckEqInt(CssTextAlignFromText("banana"), (int)RoboCssTextAlignStart);
    RoboCheckEqInt(CssTextAlignSliceIsKnown(CssSliceFromText("banana")), 0);
    RoboCheckEqInt(CssTextAlignSliceIsKnown(CssSliceEmpty()), 0);
    /*
     * The two classifications are not each other's negation, and `center` is the row
     * that proves it: it is neither logical nor physical, so \"not logical\" must not
     * be read as \"physical\".
     */
    RoboCheckEqInt(CssTextAlignIsLogical(RoboCssTextAlignCenter), 0);
    RoboCheckEqInt(CssTextAlignIsPhysical(RoboCssTextAlignCenter), 0);
    /* `left` and `right` are the physical half and are *not* logical. */
    RoboCheckEqInt(CssTextAlignIsLogical(RoboCssTextAlignLeft), 0);
    RoboCheckEqInt(CssTextAlignIsPhysical(RoboCssTextAlignLeft), 1);
    /* `match-parent` is logical, which reads oddly until you see that which physical
     * edge it resolves to depends on the child's direction rather than on the word. */
    RoboCheckEqInt(CssTextAlignIsLogical(RoboCssTextAlignMatchParent), 1);
    RoboCheckEqInt(CssTextAlignIsPhysical(RoboCssTextAlignMatchParent), 0);
    /* An unknown value claims nothing. */
    RoboCheckEqInt(CssTextAlignIsLogical((CssTextAlign)99), 0);
    RoboCheckEqInt(CssTextAlignIsPhysical((CssTextAlign)99), 0);
    RoboCheckEqInt(CssTextAlignIsJustified((CssTextAlign)99), 0);

    {
        char buffer[RoboCssTextAlignTextMax];
        size_t length = 0u;
        char tiny[4];

        RoboCheckEqInt(CssTextAlignWrite(RoboCssTextAlignMatchParent, buffer,
                                              sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "match-parent");
        RoboCheckEqInt((int)length, 12);
        /* A buffer too small reports the length it needed rather than truncating. */
        RoboCheckEqInt(CssTextAlignWrite(RoboCssTextAlignMatchParent, tiny,
                                              sizeof(tiny), &length, NULL),
                          RoboNotSupported);
        RoboCheckEqInt((int)length, 12);
        RoboCheckEqInt(CssTextAlignWrite((CssTextAlign)99, buffer,
                                              sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* text-align-last                                                     */
/* ------------------------------------------------------------------ */

static void TestTextAlignLastVocabulary(void)
{
    static const char *const keywords[] = {
        "auto", "start", "end", "left", "right", "center", "justify"
    };
    static const struct {
        int automatic;
        int logical;
        int physical;
        int justified;
    } expected[] = {
        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0},
        {0, 0, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssTextAlignLastCount, 7);
    RoboCheckEqInt((int)CssTextAlignLastCount(), 7);
    for (index = 0u; index < 7u; index++) {
        CssTextAlignLast align = (CssTextAlignLast)index;

        RoboCheckEqInt(CssTextAlignLastIsKnown(align), 1);
        RoboCheckStrEq(CssTextAlignLastName(align), keywords[index]);
        RoboCheckEqInt(CssTextAlignLastFromText(keywords[index]), (int)align);
        RoboCheck(CssTextAlignLastSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssTextAlignLastIsAutomatic(align), expected[index].automatic);
        RoboCheckEqInt(CssTextAlignLastIsLogical(align), expected[index].logical);
        RoboCheckEqInt(CssTextAlignLastIsPhysical(align), expected[index].physical);
        RoboCheckEqInt(CssTextAlignLastIsJustified(align), expected[index].justified);
    }
    RoboCheckEqInt(CssTextAlignLastIsKnown((CssTextAlignLast)99), 0);
    RoboCheckStrEq(CssTextAlignLastName((CssTextAlignLast)99), "");
    RoboCheckEqInt(CssTextAlignLastFromText("JUSTIFY"),
                      (int)RoboCssTextAlignLastJustify);
    RoboCheckEqInt(CssTextAlignLastFromText(NULL), (int)RoboCssTextAlignLastAuto);
    RoboCheckEqInt(CssTextAlignLastFromText("banana"),
                      (int)RoboCssTextAlignLastAuto);
    RoboCheckEqInt(CssTextAlignLastSliceIsKnown(CssSliceFromText("banana")), 0);
    /*
     * `auto` is deferred, not \"start\": the property means *use whatever `text-align`
     * says*, so a consumer must ask `IsAutomatic` before it reads the other three.
     */
    RoboCheckEqInt(CssTextAlignLastIsAutomatic(RoboCssTextAlignLastAuto), 1);
    RoboCheckEqInt(CssTextAlignLastIsLogical(RoboCssTextAlignLastAuto), 0);
    RoboCheckEqInt(CssTextAlignLastIsPhysical(RoboCssTextAlignLastAuto), 0);
    RoboCheckEqInt(CssTextAlignLastIsAutomatic((CssTextAlignLast)99), 0);

    {
        char buffer[RoboCssTextAlignLastTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTextAlignLastWrite(RoboCssTextAlignLastJustify, buffer,
                                                  sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "justify");
        RoboCheckEqInt((int)length, 7);
    }
}

/* ------------------------------------------------------------------ */
/* text-justify                                                        */
/* ------------------------------------------------------------------ */

static void TestTextJustifyVocabulary(void)
{
    static const char *const keywords[] = {
        "auto", "inter-word", "inter-character", "none"
    };
    static const struct {
        int justifies;
        int expandsCharacters;
    } expected[] = {
        {1, 0}, {1, 0}, {1, 1}, {0, 0}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssTextJustifyCount, 4);
    RoboCheckEqInt((int)CssTextJustifyCount(), 4);
    for (index = 0u; index < 4u; index++) {
        CssTextJustify justify = (CssTextJustify)index;

        RoboCheckEqInt(CssTextJustifyIsKnown(justify), 1);
        RoboCheckStrEq(CssTextJustifyName(justify), keywords[index]);
        RoboCheckEqInt(CssTextJustifyFromText(keywords[index]), (int)justify);
        RoboCheck(CssTextJustifySliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssTextJustifyJustifies(justify), expected[index].justifies);
        RoboCheckEqInt(CssTextJustifyExpandsCharacters(justify),
                          expected[index].expandsCharacters);
    }
    RoboCheckEqInt(CssTextJustifyIsKnown((CssTextJustify)99), 0);
    RoboCheckStrEq(CssTextJustifyName((CssTextJustify)99), "");
    RoboCheckEqInt(CssTextJustifyFromText("INTER-CHARACTER"),
                      (int)RoboCssTextJustifyInterCharacter);
    RoboCheckEqInt(CssTextJustifyFromText(NULL), (int)RoboCssTextJustifyAuto);
    RoboCheckEqInt(CssTextJustifyFromText("banana"), (int)RoboCssTextJustifyAuto);
    RoboCheckEqInt(CssTextJustifySliceIsKnown(CssSliceFromText("banana")), 0);
    /*
     * `none` is the row worth reading twice: it is not a method of justifying, it turns
     * the justification the alignment asked for *off*.
     */
    RoboCheckEqInt(CssTextJustifyJustifies(RoboCssTextJustifyNone), 0);
    RoboCheckEqInt(CssTextJustifyExpandsCharacters(RoboCssTextJustifyNone), 0);
    RoboCheckEqInt(CssTextJustifyJustifies((CssTextJustify)99), 0);

    {
        char buffer[RoboCssTextJustifyTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTextJustifyWrite(RoboCssTextJustifyInterCharacter,
                                                buffer, sizeof(buffer), &length,
                                                NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "inter-character");
        RoboCheckEqInt((int)length, 15);
    }
}

/* ------------------------------------------------------------------ */
/* text-transform                                                      */
/* ------------------------------------------------------------------ */

static void TestTextTransformVocabulary(void)
{
    static const char *const keywords[] = {
        "none", "capitalize", "uppercase", "lowercase", "full-width",
        "full-size-kana"
    };
    static const struct {
        int changesCase;
        int changesWidth;
        int changesSound;
    } expected[] = {
        {0, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssTextTransformCount, 6);
    RoboCheckEqInt((int)CssTextTransformCount(), 6);
    for (index = 0u; index < 6u; index++) {
        CssTextTransform transform = (CssTextTransform)index;

        RoboCheckEqInt(CssTextTransformIsKnown(transform), 1);
        RoboCheckStrEq(CssTextTransformName(transform), keywords[index]);
        RoboCheckEqInt(CssTextTransformFromText(keywords[index]), (int)transform);
        RoboCheck(CssTextTransformSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssTextTransformChangesCase(transform),
                          expected[index].changesCase);
        RoboCheckEqInt(CssTextTransformChangesWidth(transform),
                          expected[index].changesWidth);
        RoboCheckEqInt(CssTextTransformChangesSound(transform),
                          expected[index].changesSound);
    }
    RoboCheckEqInt(CssTextTransformIsKnown((CssTextTransform)99), 0);
    RoboCheckStrEq(CssTextTransformName((CssTextTransform)99), "");
    RoboCheckEqInt(CssTextTransformFromText("FULL-SIZE-KANA"),
                      (int)RoboCssTextTransformFullSizeKana);
    RoboCheckEqInt(CssTextTransformFromText(NULL), (int)RoboCssTextTransformNone);
    RoboCheckEqInt(CssTextTransformFromText("banana"),
                      (int)RoboCssTextTransformNone);
    RoboCheckEqInt(CssTextTransformSliceIsKnown(CssSliceFromText("banana")), 0);
    /*
     * The three classifications tile the six rows rather than leaving one
     * unclassified, and `capitalize` is why \"changes the case\" is not \"transforms the
     * string\": it cases the first letter of each word and leaves the rest alone.
     */
    RoboCheckEqInt(CssTextTransformChangesCase(RoboCssTextTransformCapitalize), 1);
    RoboCheckEqInt(CssTextTransformChangesWidth(RoboCssTextTransformCapitalize), 0);
    RoboCheckEqInt(CssTextTransformChangesSound(RoboCssTextTransformCapitalize), 0);
    RoboCheckEqInt(CssTextTransformChangesWidth(RoboCssTextTransformFullWidth), 1);
    RoboCheckEqInt(CssTextTransformChangesCase(RoboCssTextTransformFullWidth), 0);
    RoboCheckEqInt(CssTextTransformChangesSound(
                       RoboCssTextTransformFullSizeKana), 1);
    RoboCheckEqInt(CssTextTransformChangesCase((CssTextTransform)99), 0);

    {
        char buffer[RoboCssTextTransformTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTextTransformWrite(RoboCssTextTransformFullSizeKana,
                                                   buffer, sizeof(buffer), &length,
                                                   NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "full-size-kana");
        RoboCheckEqInt((int)length, 14);
    }
}

/* ------------------------------------------------------------------ */
/* white-space                                                         */
/* ------------------------------------------------------------------ */

static void TestWhiteSpaceVocabulary(void)
{
    static const char *const keywords[] = {
        "normal", "pre", "nowrap", "pre-wrap", "pre-line", "break-spaces"
    };
    /*
     * collapses, preserves newlines, wraps, breaks after a preserved space.
     *
     *     normal       yes, no,  yes, no
     *     pre          no,  yes, no,  no
     *     nowrap       yes, no,  no,  no
     *     pre-wrap     no,  yes, yes, no
     *     pre-line     yes, yes, yes, no
     *     break-spaces no,  yes, yes, yes
     */
    static const struct {
        int collapses;
        int preservesNewlines;
        int wraps;
        int breaksAfterPreservedSpace;
    } expected[] = {
        {1, 0, 1, 0}, {0, 1, 0, 0}, {1, 0, 0, 0},
        {0, 1, 1, 0}, {1, 1, 1, 0}, {0, 1, 1, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssWhiteSpaceCount, 6);
    RoboCheckEqInt((int)CssWhiteSpaceCount(), 6);
    for (index = 0u; index < 6u; index++) {
        CssWhiteSpace whiteSpace = (CssWhiteSpace)index;

        RoboCheckEqInt(CssWhiteSpaceIsKnown(whiteSpace), 1);
        RoboCheckStrEq(CssWhiteSpaceName(whiteSpace), keywords[index]);
        RoboCheckEqInt(CssWhiteSpaceFromText(keywords[index]), (int)whiteSpace);
        RoboCheck(CssWhiteSpaceSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssWhiteSpaceCollapsesSpaces(whiteSpace),
                          expected[index].collapses);
        RoboCheckEqInt(CssWhiteSpacePreservesNewlines(whiteSpace),
                          expected[index].preservesNewlines);
        RoboCheckEqInt(CssWhiteSpaceWraps(whiteSpace), expected[index].wraps);
        RoboCheckEqInt(CssWhiteSpaceBreaksAfterPreservedSpace(whiteSpace),
                          expected[index].breaksAfterPreservedSpace);
    }
    RoboCheckEqInt(CssWhiteSpaceIsKnown((CssWhiteSpace)99), 0);
    RoboCheckStrEq(CssWhiteSpaceName((CssWhiteSpace)99), "");
    RoboCheckEqInt(CssWhiteSpaceFromText("BREAK-SPACES"),
                      (int)RoboCssWhiteSpaceBreakSpaces);
    RoboCheckEqInt(CssWhiteSpaceFromText(NULL), (int)RoboCssWhiteSpaceNormal);
    RoboCheckEqInt(CssWhiteSpaceFromText("banana"), (int)RoboCssWhiteSpaceNormal);
    RoboCheckEqInt(CssWhiteSpaceSliceIsKnown(CssSliceFromText("banana")), 0);
    /*
     * `normal` and `pre-line` differ *only* in whether a newline survives, which is
     * the pair a three-column table could not tell apart.
     */
    RoboCheckEqInt(CssWhiteSpaceCollapsesSpaces(RoboCssWhiteSpaceNormal),
                      CssWhiteSpaceCollapsesSpaces(RoboCssWhiteSpacePreLine));
    RoboCheckEqInt(CssWhiteSpaceWraps(RoboCssWhiteSpaceNormal),
                      CssWhiteSpaceWraps(RoboCssWhiteSpacePreLine));
    RoboCheckEqInt(CssWhiteSpacePreservesNewlines(RoboCssWhiteSpaceNormal), 0);
    RoboCheckEqInt(CssWhiteSpacePreservesNewlines(RoboCssWhiteSpacePreLine), 1);
    /*
     * `pre-wrap` and `break-spaces` agree on all three of the first columns and differ
     * only on the fourth — the behaviour CSS Text 4 added `break-spaces` for.
     */
    RoboCheckEqInt(CssWhiteSpaceCollapsesSpaces(RoboCssWhiteSpacePreWrap),
                      CssWhiteSpaceCollapsesSpaces(RoboCssWhiteSpaceBreakSpaces));
    RoboCheckEqInt(CssWhiteSpacePreservesNewlines(RoboCssWhiteSpacePreWrap),
                      CssWhiteSpacePreservesNewlines(RoboCssWhiteSpaceBreakSpaces));
    RoboCheckEqInt(CssWhiteSpaceWraps(RoboCssWhiteSpacePreWrap),
                      CssWhiteSpaceWraps(RoboCssWhiteSpaceBreakSpaces));
    RoboCheckEqInt(CssWhiteSpaceBreaksAfterPreservedSpace(
                       RoboCssWhiteSpacePreWrap), 0);
    RoboCheckEqInt(CssWhiteSpaceBreaksAfterPreservedSpace(
                       RoboCssWhiteSpaceBreakSpaces), 1);
    /*
     * An unknown value answers the direction that changes least, not the one that looks
     * like a default: a `white-space` this build cannot read collapses its spaces and
     * wraps, which is the initial value's behaviour rather than \"preserve every space\".
     */
    RoboCheckEqInt(CssWhiteSpaceCollapsesSpaces((CssWhiteSpace)99), 1);
    RoboCheckEqInt(CssWhiteSpacePreservesNewlines((CssWhiteSpace)99), 0);
    RoboCheckEqInt(CssWhiteSpaceWraps((CssWhiteSpace)99), 1);
    RoboCheckEqInt(CssWhiteSpaceBreaksAfterPreservedSpace((CssWhiteSpace)99), 0);

    {
        char buffer[RoboCssWhiteSpaceTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssWhiteSpaceWrite(RoboCssWhiteSpaceBreakSpaces, buffer,
                                                sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "break-spaces");
        RoboCheckEqInt((int)length, 12);
    }
}

/* ------------------------------------------------------------------ */
/* word-break                                                          */
/* ------------------------------------------------------------------ */

static void TestWordBreakVocabulary(void)
{
    static const char *const keywords[] = {
        "normal", "break-all", "keep-all", "break-word"
    };
    static const struct {
        int breaksWithinWords;
        int forbidsScriptBreaks;
    } expected[] = {
        {0, 0}, {1, 0}, {0, 1}, {1, 0}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssWordBreakCount, 4);
    RoboCheckEqInt((int)CssWordBreakCount(), 4);
    for (index = 0u; index < 4u; index++) {
        CssWordBreak wordBreak = (CssWordBreak)index;

        RoboCheckEqInt(CssWordBreakIsKnown(wordBreak), 1);
        RoboCheckStrEq(CssWordBreakName(wordBreak), keywords[index]);
        RoboCheckEqInt(CssWordBreakFromText(keywords[index]), (int)wordBreak);
        RoboCheck(CssWordBreakSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssWordBreakBreaksWithinWords(wordBreak),
                          expected[index].breaksWithinWords);
        RoboCheckEqInt(CssWordBreakForbidsScriptBreaks(wordBreak),
                          expected[index].forbidsScriptBreaks);
    }
    RoboCheckEqInt(CssWordBreakIsKnown((CssWordBreak)99), 0);
    RoboCheckStrEq(CssWordBreakName((CssWordBreak)99), "");
    RoboCheckEqInt(CssWordBreakFromText("KEEP-ALL"),
                      (int)RoboCssWordBreakKeepAll);
    RoboCheckEqInt(CssWordBreakFromText(NULL), (int)RoboCssWordBreakNormal);
    RoboCheckEqInt(CssWordBreakFromText("banana"), (int)RoboCssWordBreakNormal);
    RoboCheckEqInt(CssWordBreakSliceIsKnown(CssSliceFromText("banana")), 0);
    /*
     * The two classifications are separate questions: `normal` already forbids a break
     * inside a Latin word, so `keep-all` is about *disabling* opportunities a script
     * normally has (CJK), not about a stricter `normal`.
     */
    RoboCheckEqInt(CssWordBreakBreaksWithinWords(RoboCssWordBreakNormal), 0);
    RoboCheckEqInt(CssWordBreakBreaksWithinWords(RoboCssWordBreakKeepAll), 0);
    RoboCheckEqInt(CssWordBreakForbidsScriptBreaks(RoboCssWordBreakBreakAll), 0);
    RoboCheckEqInt(CssWordBreakBreaksWithinWords((CssWordBreak)99), 0);
    RoboCheckEqInt(CssWordBreakForbidsScriptBreaks((CssWordBreak)99), 0);

    {
        char buffer[RoboCssWordBreakTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssWordBreakWrite(RoboCssWordBreakBreakWord, buffer,
                                               sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "break-word");
        RoboCheckEqInt((int)length, 10);
    }
}

/* ------------------------------------------------------------------ */
/* overflow-wrap                                                       */
/* ------------------------------------------------------------------ */

static void TestOverflowWrapVocabulary(void)
{
    static const char *const keywords[] = { "normal", "break-word", "anywhere" };
    static const struct {
        int breaksWithinWords;
        int affectsIntrinsicSize;
    } expected[] = {
        {0, 0}, {1, 0}, {1, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssOverflowWrapCount, 3);
    RoboCheckEqInt((int)CssOverflowWrapCount(), 3);
    for (index = 0u; index < 3u; index++) {
        CssOverflowWrap overflowWrap = (CssOverflowWrap)index;

        RoboCheckEqInt(CssOverflowWrapIsKnown(overflowWrap), 1);
        RoboCheckStrEq(CssOverflowWrapName(overflowWrap), keywords[index]);
        RoboCheckEqInt(CssOverflowWrapFromText(keywords[index]),
                          (int)overflowWrap);
        RoboCheck(CssOverflowWrapSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssOverflowWrapBreaksWithinWords(overflowWrap),
                          expected[index].breaksWithinWords);
        RoboCheckEqInt(CssOverflowWrapAffectsIntrinsicSize(overflowWrap),
                          expected[index].affectsIntrinsicSize);
    }
    RoboCheckEqInt(CssOverflowWrapIsKnown((CssOverflowWrap)99), 0);
    RoboCheckStrEq(CssOverflowWrapName((CssOverflowWrap)99), "");
    RoboCheckEqInt(CssOverflowWrapFromText("ANYWHERE"),
                      (int)RoboCssOverflowWrapAnywhere);
    RoboCheckEqInt(CssOverflowWrapFromText(NULL), (int)RoboCssOverflowWrapNormal);
    RoboCheckEqInt(CssOverflowWrapFromText("banana"),
                      (int)RoboCssOverflowWrapNormal);
    RoboCheckEqInt(CssOverflowWrapSliceIsKnown(CssSliceFromText("banana")), 0);
    /*
     * The pair that matters: `break-word` and `anywhere` break *identically* and differ
     * only in whether the break opportunities count towards the element's intrinsic
     * size, so \"does it break\" cannot tell them apart.
     */
    RoboCheckEqInt(
        CssOverflowWrapBreaksWithinWords(RoboCssOverflowWrapBreakWord),
        CssOverflowWrapBreaksWithinWords(RoboCssOverflowWrapAnywhere));
    RoboCheckEqInt(
        CssOverflowWrapAffectsIntrinsicSize(RoboCssOverflowWrapBreakWord), 0);
    RoboCheckEqInt(
        CssOverflowWrapAffectsIntrinsicSize(RoboCssOverflowWrapAnywhere), 1);
    RoboCheckEqInt(CssOverflowWrapBreaksWithinWords((CssOverflowWrap)99), 0);
    RoboCheckEqInt(CssOverflowWrapAffectsIntrinsicSize((CssOverflowWrap)99), 0);

    {
        char buffer[RoboCssOverflowWrapTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssOverflowWrapWrite(RoboCssOverflowWrapAnywhere, buffer,
                                                 sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "anywhere");
        RoboCheckEqInt((int)length, 8);
    }
}

/* ------------------------------------------------------------------ */
/* line-break                                                          */
/* ------------------------------------------------------------------ */

static void TestLineBreakVocabulary(void)
{
    static const char *const keywords[] = {
        "auto", "loose", "normal", "strict", "anywhere"
    };
    static const struct {
        int automatic;
        int strictness;
        int breaksAnywhere;
    } expected[] = {
        {1, 0, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssLineBreakCount, 5);
    RoboCheckEqInt((int)CssLineBreakCount(), 5);
    for (index = 0u; index < 5u; index++) {
        CssLineBreak lineBreak = (CssLineBreak)index;

        RoboCheckEqInt(CssLineBreakIsKnown(lineBreak), 1);
        RoboCheckStrEq(CssLineBreakName(lineBreak), keywords[index]);
        RoboCheckEqInt(CssLineBreakFromText(keywords[index]), (int)lineBreak);
        RoboCheck(CssLineBreakSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssLineBreakIsAutomatic(lineBreak),
                          expected[index].automatic);
        RoboCheckEqInt(CssLineBreakIsStrictness(lineBreak),
                          expected[index].strictness);
        RoboCheckEqInt(CssLineBreakBreaksAnywhere(lineBreak),
                          expected[index].breaksAnywhere);
    }
    RoboCheckEqInt(CssLineBreakIsKnown((CssLineBreak)99), 0);
    RoboCheckStrEq(CssLineBreakName((CssLineBreak)99), "");
    RoboCheckEqInt(CssLineBreakFromText("STRICT"), (int)RoboCssLineBreakStrict);
    RoboCheckEqInt(CssLineBreakFromText(NULL), (int)RoboCssLineBreakAuto);
    RoboCheckEqInt(CssLineBreakFromText("banana"), (int)RoboCssLineBreakAuto);
    RoboCheckEqInt(CssLineBreakSliceIsKnown(CssSliceFromText("banana")), 0);
    /*
     * `anywhere` is the absence of the script's rules rather than a fourth strictness,
     * so a build that treated it as the loosest setting would apply the rules it turns
     * off.
     */
    RoboCheckEqInt(CssLineBreakIsStrictness(RoboCssLineBreakAnywhere), 0);
    RoboCheckEqInt(CssLineBreakBreaksAnywhere(RoboCssLineBreakLoose), 0);
    RoboCheckEqInt(CssLineBreakIsStrictness(RoboCssLineBreakAuto), 0);
    RoboCheckEqInt(CssLineBreakIsAutomatic((CssLineBreak)99), 0);

    {
        char buffer[RoboCssLineBreakTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssLineBreakWrite(RoboCssLineBreakAnywhere, buffer,
                                               sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "anywhere");
        RoboCheckEqInt((int)length, 8);
    }
}

/* ------------------------------------------------------------------ */
/* hyphens                                                             */
/* ------------------------------------------------------------------ */

static void TestHyphensVocabulary(void)
{
    static const char *const keywords[] = { "none", "manual", "auto" };
    static const struct {
        int enables;
        int inserts;
    } expected[] = {
        {0, 0}, {1, 0}, {1, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssHyphensCount, 3);
    RoboCheckEqInt((int)CssHyphensCount(), 3);
    for (index = 0u; index < 3u; index++) {
        CssHyphens hyphens = (CssHyphens)index;

        RoboCheckEqInt(CssHyphensIsKnown(hyphens), 1);
        RoboCheckStrEq(CssHyphensName(hyphens), keywords[index]);
        RoboCheckEqInt(CssHyphensFromText(keywords[index]), (int)hyphens);
        RoboCheck(CssHyphensSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssHyphensEnablesHyphenation(hyphens),
                          expected[index].enables);
        RoboCheckEqInt(CssHyphensInsertsHyphens(hyphens), expected[index].inserts);
    }
    RoboCheckEqInt(CssHyphensIsKnown((CssHyphens)99), 0);
    RoboCheckStrEq(CssHyphensName((CssHyphens)99), "");
    RoboCheckEqInt(CssHyphensFromText("MANUAL"), (int)RoboCssHyphensManual);
    RoboCheckEqInt(CssHyphensFromText(NULL), (int)RoboCssHyphensNone);
    RoboCheckEqInt(CssHyphensFromText("banana"), (int)RoboCssHyphensNone);
    RoboCheckEqInt(CssHyphensSliceIsKnown(CssSliceFromText("banana")), 0);
    /*
     * `manual` is permissive without being generative: it enables hyphenation while
     * inserting nothing, which is exactly why the two questions are separate.
     */
    RoboCheckEqInt(CssHyphensEnablesHyphenation(RoboCssHyphensManual), 1);
    RoboCheckEqInt(CssHyphensInsertsHyphens(RoboCssHyphensManual), 0);
    RoboCheckEqInt(CssHyphensEnablesHyphenation(RoboCssHyphensNone), 0);
    RoboCheckEqInt(CssHyphensEnablesHyphenation((CssHyphens)99), 0);

    {
        char buffer[RoboCssHyphensTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssHyphensWrite(RoboCssHyphensManual, buffer,
                                             sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "manual");
        RoboCheckEqInt((int)length, 6);
    }
}

/* ------------------------------------------------------------------ */
/* text-decoration-style                                               */
/* ------------------------------------------------------------------ */

static void TestTextDecorationStyleVocabulary(void)
{
    static const char *const keywords[] = {
        "solid", "double", "dotted", "dashed", "wavy"
    };
    static const struct {
        int solid;
        int wavy;
        int segmented;
    } expected[] = {
        {1, 0, 0}, {0, 0, 0}, {0, 0, 1}, {0, 0, 1}, {0, 1, 0}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssTextDecorationStyleCount, 5);
    RoboCheckEqInt((int)CssTextDecorationStyleCount(), 5);
    for (index = 0u; index < 5u; index++) {
        CssTextDecorationStyle style = (CssTextDecorationStyle)index;

        RoboCheckEqInt(CssTextDecorationStyleIsKnown(style), 1);
        RoboCheckStrEq(CssTextDecorationStyleName(style), keywords[index]);
        RoboCheckEqInt(CssTextDecorationStyleFromText(keywords[index]),
                          (int)style);
        RoboCheck(CssTextDecorationStyleSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssTextDecorationStyleIsSolid(style), expected[index].solid);
        RoboCheckEqInt(CssTextDecorationStyleIsWavy(style), expected[index].wavy);
        RoboCheckEqInt(CssTextDecorationStyleIsSegmented(style),
                          expected[index].segmented);
    }
    RoboCheckEqInt(CssTextDecorationStyleIsKnown((CssTextDecorationStyle)99), 0);
    RoboCheckStrEq(CssTextDecorationStyleName((CssTextDecorationStyle)99), "");
    RoboCheckEqInt(CssTextDecorationStyleFromText("WAVY"),
                      (int)RoboCssTextDecorationStyleWavy);
    RoboCheckEqInt(CssTextDecorationStyleFromText(NULL),
                      (int)RoboCssTextDecorationStyleSolid);
    RoboCheckEqInt(CssTextDecorationStyleFromText("banana"),
                      (int)RoboCssTextDecorationStyleSolid);
    RoboCheckEqInt(CssTextDecorationStyleSliceIsKnown(
                       CssSliceFromText("banana")), 0);
    /*
     * `double` is why \"segmented\" is not \"not solid\": it is two *unbroken* strokes, so
     * a consumer that read \"not solid\" as \"has gaps\" would dash a double rule.
     */
    RoboCheckEqInt(CssTextDecorationStyleIsSolid(RoboCssTextDecorationStyleDouble),
                      0);
    RoboCheckEqInt(
        CssTextDecorationStyleIsSegmented(RoboCssTextDecorationStyleDouble), 0);
    RoboCheckEqInt(CssTextDecorationStyleIsSegmented(
                       RoboCssTextDecorationStyleDotted), 1);
    RoboCheckEqInt(CssTextDecorationStyleIsSegmented(
                       RoboCssTextDecorationStyleDashed), 1);
    RoboCheckEqInt(CssTextDecorationStyleIsWavy(
                       RoboCssTextDecorationStyleDashed), 0);

    {
        char buffer[RoboCssTextDecorationStyleTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTextDecorationStyleWrite(
                           RoboCssTextDecorationStyleWavy, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "wavy");
        RoboCheckEqInt((int)length, 4);
    }
}

/* ------------------------------------------------------------------ */
/* text-decoration-line                                                */
/* ------------------------------------------------------------------ */

static void TestTextDecorationLineVocabulary(void)
{
    static const char *const keywords[] = {
        "none", "underline", "overline", "line-through", "blink"
    };
    static const struct {
        int drawsInk;
        int blink;
    } expected[] = {
        {0, 0}, {1, 0}, {1, 0}, {1, 0}, {0, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssTextDecorationLineCount, 5);
    RoboCheckEqInt((int)CssTextDecorationLineCount(), 5);
    for (index = 0u; index < 5u; index++) {
        CssTextDecorationLine line = (CssTextDecorationLine)index;

        RoboCheckEqInt(CssTextDecorationLineIsKnown(line), 1);
        RoboCheckStrEq(CssTextDecorationLineName(line), keywords[index]);
        RoboCheckEqInt(CssTextDecorationLineFromText(keywords[index]),
                          (int)line);
        RoboCheck(CssTextDecorationLineSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssTextDecorationLineDrawsInk(line),
                          expected[index].drawsInk);
        RoboCheckEqInt(CssTextDecorationLineIsBlink(line), expected[index].blink);
    }
    RoboCheckEqInt(CssTextDecorationLineIsKnown((CssTextDecorationLine)99), 0);
    RoboCheckStrEq(CssTextDecorationLineName((CssTextDecorationLine)99), "");
    RoboCheckEqInt(CssTextDecorationLineFromText("LINE-THROUGH"),
                      (int)RoboCssTextDecorationLineLineThrough);
    RoboCheckEqInt(CssTextDecorationLineFromText(NULL),
                      (int)RoboCssTextDecorationLineNone);
    RoboCheckEqInt(CssTextDecorationLineSliceIsKnown(CssSliceFromText("banana")),
                      0);
    /*
     * `blink` is why \"draws ink\" is not \"is not none\": a decorative blink with no
     * line has nothing to draw, so both predicates are false for it and for `none`.
     */
    RoboCheckEqInt(CssTextDecorationLineDrawsInk(RoboCssTextDecorationLineBlink),
                      0);
    RoboCheckEqInt(CssTextDecorationLineIsBlink(
                       RoboCssTextDecorationLineUnderline), 0);
    RoboCheckEqInt(CssTextDecorationLineDrawsInk(
                       RoboCssTextDecorationLineUnderline), 1);
    RoboCheckEqInt(CssTextDecorationLineDrawsInk((CssTextDecorationLine)99), 0);

    {
        char buffer[RoboCssTextDecorationLineTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTextDecorationLineWrite(
                           RoboCssTextDecorationLineLineThrough, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "line-through");
        RoboCheckEqInt((int)length, 12);
    }
}

/* ------------------------------------------------------------------ */
/* text-overflow                                                       */
/* ------------------------------------------------------------------ */

static void TestTextOverflowVocabulary(void)
{
    static const char *const keywords[] = { "clip", "ellipsis" };

    CssFixture();
    RoboCheckEqInt((int)RoboCssTextOverflowCount, 2);
    RoboCheckEqInt((int)CssTextOverflowCount(), 2);
    RoboCheckEqInt(CssTextOverflowIsKnown(RoboCssTextOverflowClip), 1);
    RoboCheckEqInt(CssTextOverflowIsKnown(RoboCssTextOverflowEllipsis), 1);
    RoboCheckStrEq(CssTextOverflowName(RoboCssTextOverflowClip), keywords[0]);
    RoboCheckStrEq(CssTextOverflowName(RoboCssTextOverflowEllipsis), keywords[1]);
    RoboCheckEqInt(CssTextOverflowFromText("CLIP"), (int)RoboCssTextOverflowClip);
    RoboCheckEqInt(CssTextOverflowFromText("ellipsis"),
                      (int)RoboCssTextOverflowEllipsis);
    RoboCheckEqInt(CssTextOverflowFromText(NULL), (int)RoboCssTextOverflowClip);
    RoboCheckEqInt(CssTextOverflowFromText("banana"),
                      (int)RoboCssTextOverflowClip);
    RoboCheckEqInt(CssTextOverflowSliceIsKnown(CssSliceFromText("ellipsis")), 1);
    RoboCheckEqInt(CssTextOverflowSliceIsKnown(CssSliceFromText("banana")), 0);
    RoboCheckEqInt(CssTextOverflowIsKnown((CssTextOverflow)99), 0);
    RoboCheckStrEq(CssTextOverflowName((CssTextOverflow)99), "");
    /*
     * Only `ellipsis` substitutes a marker; `clip` cuts the text in place, so the
     * classification is the difference between a visible marker and a cut.
     */
    RoboCheckEqInt(CssTextOverflowAddsMarker(RoboCssTextOverflowEllipsis), 1);
    RoboCheckEqInt(CssTextOverflowAddsMarker(RoboCssTextOverflowClip), 0);
    RoboCheckEqInt(CssTextOverflowAddsMarker((CssTextOverflow)99), 0);

    {
        char buffer[RoboCssTextOverflowTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTextOverflowWrite(RoboCssTextOverflowEllipsis, buffer,
                                                 sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "ellipsis");
        RoboCheckEqInt((int)length, 8);
    }
}

/* ------------------------------------------------------------------ */
/* text-decoration-thickness                                           */
/* ------------------------------------------------------------------ */

static void TestTextDecorationThicknessVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssTextDecorationThicknessCount, 2);
    RoboCheckEqInt((int)CssTextDecorationThicknessCount(), 2);
    RoboCheckEqInt(
        CssTextDecorationThicknessIsKnown(RoboCssTextDecorationThicknessAuto),
        1);
    RoboCheckEqInt(CssTextDecorationThicknessIsKnown(
                       RoboCssTextDecorationThicknessFromFont), 1);
    RoboCheckStrEq(
        CssTextDecorationThicknessName(RoboCssTextDecorationThicknessAuto),
        "auto");
    RoboCheckStrEq(
        CssTextDecorationThicknessName(RoboCssTextDecorationThicknessFromFont),
        "from-font");
    RoboCheckEqInt(CssTextDecorationThicknessFromText("FROM-FONT"),
                      (int)RoboCssTextDecorationThicknessFromFont);
    RoboCheckEqInt(CssTextDecorationThicknessFromText(NULL),
                      (int)RoboCssTextDecorationThicknessAuto);
    RoboCheckEqInt(CssTextDecorationThicknessFromText("banana"),
                      (int)RoboCssTextDecorationThicknessAuto);
    RoboCheckEqInt(CssTextDecorationThicknessSliceIsKnown(
                       CssSliceFromText("from-font")), 1);
    RoboCheckEqInt(CssTextDecorationThicknessSliceIsKnown(
                       CssSliceFromText("banana")), 0);
    RoboCheckEqInt(CssTextDecorationThicknessIsKnown(
                       (CssTextDecorationThickness)99), 0);
    /*
     * The pair is a genuine distinction, not two spellings of \"you decide\": `auto`
     * leaves the thickness to the user agent, while `from-font` reads the *face's own*
     * metric — a fact about the file rather than a choice.
     */
    RoboCheckEqInt(CssTextDecorationThicknessIsAutomatic(
                       RoboCssTextDecorationThicknessAuto), 1);
    RoboCheckEqInt(CssTextDecorationThicknessUsesFontMetric(
                       RoboCssTextDecorationThicknessAuto), 0);
    RoboCheckEqInt(CssTextDecorationThicknessUsesFontMetric(
                       RoboCssTextDecorationThicknessFromFont), 1);
    RoboCheckEqInt(CssTextDecorationThicknessIsAutomatic(
                       RoboCssTextDecorationThicknessFromFont), 0);
    RoboCheckEqInt(CssTextDecorationThicknessIsAutomatic(
                       (CssTextDecorationThickness)99), 0);
    RoboCheckEqInt(CssTextDecorationThicknessUsesFontMetric(
                       (CssTextDecorationThickness)99), 0);

    {
        char buffer[RoboCssTextDecorationThicknessTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTextDecorationThicknessWrite(
                           RoboCssTextDecorationThicknessFromFont, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "from-font");
        RoboCheckEqInt((int)length, 9);
    }
}

/* ------------------------------------------------------------------ */
/* text-underline-position                                             */
/* ------------------------------------------------------------------ */

static void TestTextUnderlinePositionVocabulary(void)
{
    static const char *const keywords[] = { "auto", "under", "left", "right" };

    CssFixture();
    RoboCheckEqInt((int)RoboCssTextUnderlinePositionCount, 4);
    RoboCheckEqInt((int)CssTextUnderlinePositionCount(), 4);
    RoboCheckStrEq(CssTextUnderlinePositionName(RoboCssTextUnderlinePositionAuto),
                      keywords[0]);
    RoboCheckStrEq(CssTextUnderlinePositionName(RoboCssTextUnderlinePositionUnder),
                      keywords[1]);
    RoboCheckStrEq(CssTextUnderlinePositionName(RoboCssTextUnderlinePositionLeft),
                      keywords[2]);
    RoboCheckStrEq(CssTextUnderlinePositionName(RoboCssTextUnderlinePositionRight),
                      keywords[3]);
    RoboCheckEqInt(CssTextUnderlinePositionFromText("UNDER"),
                      (int)RoboCssTextUnderlinePositionUnder);
    RoboCheckEqInt(CssTextUnderlinePositionFromText(NULL),
                      (int)RoboCssTextUnderlinePositionAuto);
    RoboCheckEqInt(CssTextUnderlinePositionFromText("banana"),
                      (int)RoboCssTextUnderlinePositionAuto);
    RoboCheckEqInt(CssTextUnderlinePositionSliceIsKnown(
                       CssSliceFromText("right")), 1);
    RoboCheckEqInt(CssTextUnderlinePositionSliceIsKnown(
                       CssSliceFromText("banana")), 0);
    RoboCheckEqInt(CssTextUnderlinePositionIsKnown(
                       (CssTextUnderlinePosition)99), 0);
    RoboCheckStrEq(CssTextUnderlinePositionName((CssTextUnderlinePosition)99), "");
    /*
     * The four values are two different questions: `under` and `auto` are about where
     * the line sits *vertically* (below the descenders, or the user agent's choice),
     * while `left` and `right` only matter in vertical writing.
     */
    RoboCheckEqInt(CssTextUnderlinePositionIsAutomatic(
                       RoboCssTextUnderlinePositionAuto), 1);
    RoboCheckEqInt(CssTextUnderlinePositionClearsDescenders(
                       RoboCssTextUnderlinePositionAuto), 0);
    RoboCheckEqInt(CssTextUnderlinePositionClearsDescenders(
                       RoboCssTextUnderlinePositionUnder), 1);
    RoboCheckEqInt(CssTextUnderlinePositionIsPhysicalSide(
                       RoboCssTextUnderlinePositionUnder), 0);
    RoboCheckEqInt(CssTextUnderlinePositionIsPhysicalSide(
                       RoboCssTextUnderlinePositionLeft), 1);
    RoboCheckEqInt(CssTextUnderlinePositionIsPhysicalSide(
                       RoboCssTextUnderlinePositionRight), 1);
    RoboCheckEqInt(CssTextUnderlinePositionClearsDescenders(
                       RoboCssTextUnderlinePositionLeft), 0);
    RoboCheckEqInt(CssTextUnderlinePositionIsAutomatic(
                       (CssTextUnderlinePosition)99), 0);

    {
        char buffer[RoboCssTextUnderlinePositionTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssTextUnderlinePositionWrite(
                           RoboCssTextUnderlinePositionUnder, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "under");
        RoboCheckEqInt((int)length, 5);
    }
}

/* ------------------------------------------------------------------ */
/* Computation                                                         */
/* ------------------------------------------------------------------ */

static void TestTextComputedShapes(void)
{
    CssFixture();

    /* The ten typed rows read through the shared closed-vocabulary body, so a value
     * this build can read is a KEYWORD carrying the canonical spelling. */
    CssComputeEquals(RoboCssPropertyComputationTextAlign, "o: center", "center");
    CssComputeEquals(RoboCssPropertyComputationTextAlign, "o: CENTER", "center");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationTextAlign,
                                        "o: center"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationTextAlignLast, "o: auto", "auto");
    CssComputeEquals(RoboCssPropertyComputationTextJustify, "o: inter-word",
                       "inter-word");
    CssComputeEquals(RoboCssPropertyComputationTextTransform, "o: uppercase",
                       "uppercase");
    CssComputeEquals(RoboCssPropertyComputationWhiteSpace, "o: break-spaces",
                       "break-spaces");
    CssComputeEquals(RoboCssPropertyComputationWordBreak, "o: keep-all",
                       "keep-all");
    CssComputeEquals(RoboCssPropertyComputationOverflowWrap, "o: anywhere",
                       "anywhere");
    CssComputeEquals(RoboCssPropertyComputationLineBreak, "o: strict", "strict");
    CssComputeEquals(RoboCssPropertyComputationHyphens, "o: manual", "manual");
    CssComputeEquals(RoboCssPropertyComputationTextDecorationStyle, "o: wavy",
                       "wavy");

    /* A reference is deferred -- legal CSS whose value is decided where the reference
     * is -- and it is carried rather than refused. */
    {
        uint32_t flags = 0u;

        CssComputeEquals(RoboCssPropertyComputationWhiteSpace, "o: var(--w)",
                           "var(--w)");
        CssComputeText(RoboCssPropertyComputationWhiteSpace, "o: var(--w)",
                         NULL, 0u, &flags);
        RoboCheckEqInt(flags, 1u);
    }

    /* The refusals, which is what the closed set buys: a word the vocabulary does not
     * know, something that is not an identifier, and *two* identifiers. */
    CssComputeEquals(RoboCssPropertyComputationWhiteSpace, "o: banana",
                       "not a white-space value");
    CssComputeEquals(RoboCssPropertyComputationWhiteSpace, "o: 3px",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationWhiteSpace, "o: pre wrap",
                       "not a single white-space keyword");
    CssComputeEquals(RoboCssPropertyComputationTextAlign, "o: middle",
                       "not a text-align value");
    CssComputeEquals(RoboCssPropertyComputationTextTransform, "o: banana",
                       "not a text-transform value");
    CssComputeEquals(RoboCssPropertyComputationWordBreak, "o: banana",
                       "not a word-break value");
    CssComputeEquals(RoboCssPropertyComputationOverflowWrap, "o: banana",
                       "not an overflow-wrap value");
    CssComputeEquals(RoboCssPropertyComputationLineBreak, "o: banana",
                       "not a line-break value");
    CssComputeEquals(RoboCssPropertyComputationHyphens, "o: banana",
                       "not a hyphens value");
    CssComputeEquals(RoboCssPropertyComputationTextDecorationStyle, "o: banana",
                       "not a text-decoration-style value");
    CssComputeEquals(RoboCssPropertyComputationTextAlignLast, "o: banana",
                       "not a text-align-last value");
    CssComputeEquals(RoboCssPropertyComputationTextJustify, "o: banana",
                       "not a text-justify value");

    /* The other half of the family is *carried*: the decoration line is a set, the
     * overflow marker may be a string and the thickness may be a length, so those
     * declarations survive intact -- including the word nothing recognises. */
    CssComputeEquals(RoboCssPropertyComputationPreserve,
                       "o: underline overline", "underline overline");
    CssComputeEquals(RoboCssPropertyComputationPreserve,
                       "o: ellipsis '\u2026'", "ellipsis '\u2026'");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: 3px", "3px");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: under left",
                       "under left");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: banana", "banana");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPreserve,
                                        "o: banana"),
                      "preserved");
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestTextRegistry(void)
{
    /*
     * Fourteen inherited rows and six non-inherited ones, and the split is the family's
     * central fact stated as a list: an alignment, a transformation, a white-space rule,
     * a line-breaking control, a shadow and an emphasis mark describe how the **text**
     * is laid out, so a child sits on the same rules, while the decoration's own five
     * rows and `text-overflow` describe the box that draws or clips the text.
     */
    static const struct {
        const char *name;
        const char *shape;
        int inherited;
    } expected[] = {
        {"hyphens", "manual", 1},
        {"line-break", "auto", 1},
        {"overflow-wrap", "normal", 1},
        {"tab-size", "8", 1},
        {"text-align", "start", 1},
        {"text-align-last", "auto", 1},
        {"text-emphasis", "none,_,currentcolor", 1},
        {"text-emphasis-color", "currentcolor", 1},
        {"text-emphasis-position", "over,_,right", 1},
        {"text-emphasis-style", "none", 1},
        {"text-justify", "auto", 1},
        {"text-shadow", "none", 1},
        {"text-transform", "none", 1},
        {"text-underline-offset", "auto", 1},
        {"text-underline-position", "auto", 1},
        {"word-break", "normal", 1},
        {"white-space", "normal", 1},
        {"text-decoration", "none,_,currentcolor,_,solid,_,auto", 0},
        {"text-decoration-color", "currentcolor", 0},
        {"text-decoration-line", "none", 0},
        {"text-decoration-style", "solid", 0},
        {"text-decoration-thickness", "auto", 0},
        {"text-overflow", "clip", 0}
    };
    static const struct {
        const char *name;
        CssPropertyComputation computation;
    } shapes[] = {
        {"text-align", RoboCssPropertyComputationTextAlign},
        {"text-align-last", RoboCssPropertyComputationTextAlignLast},
        {"text-justify", RoboCssPropertyComputationTextJustify},
        {"text-transform", RoboCssPropertyComputationTextTransform},
        {"white-space", RoboCssPropertyComputationWhiteSpace},
        {"word-break", RoboCssPropertyComputationWordBreak},
        {"overflow-wrap", RoboCssPropertyComputationOverflowWrap},
        {"line-break", RoboCssPropertyComputationLineBreak},
        {"hyphens", RoboCssPropertyComputationHyphens},
        {"text-decoration-style", RoboCssPropertyComputationTextDecorationStyle},
        /* The carried half stays PRESERVED, including the five rows whose grammar is
         * wider than any closed set. */
        {"tab-size", RoboCssPropertyComputationPreserve},
        {"text-emphasis", RoboCssPropertyComputationPreserve},
        {"text-emphasis-position", RoboCssPropertyComputationPreserve},
        {"text-emphasis-style", RoboCssPropertyComputationPreserve},
        {"text-shadow", RoboCssPropertyComputationPreserve},
        {"text-underline-offset", RoboCssPropertyComputationPreserve},
        {"text-underline-position", RoboCssPropertyComputationPreserve},
        {"text-decoration", RoboCssPropertyComputationPreserve},
        {"text-decoration-line", RoboCssPropertyComputationPreserve},
        {"text-decoration-thickness", RoboCssPropertyComputationPreserve},
        {"text-overflow", RoboCssPropertyComputationPreserve},
        /* `text-decoration-color` is a colour like every other colour. */
        {"text-decoration-color", RoboCssPropertyComputationColor},
        {"text-emphasis-color", RoboCssPropertyComputationColor}
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, expected[index].name);
        char shape[64];

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition),
                          expected[index].inherited);
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }

    /* The shape is read from the definition rather than inferred from the name, so a
     * row that had drifted onto another family's computation shows here. */
    for (index = 0u; index < sizeof(shapes) / sizeof(shapes[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, shapes[index].name);

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              shapes[index].computation);
        }
    }

    /* The `text-decoration` shorthand is the family's one non-inherited shorthand, and
     * the inherited count on the wide suite would not catch a row that was. */
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "text-decoration");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
            RoboCheck((definition->flags & RoboCssPropertyFlagShorthand) != 0u);
        }
    }
    /* `text-indent` keeps the shared LENGTH shape it has had since 9.11. */
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "text-indent");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              RoboCssPropertyComputationLength);
        }
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestTextCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();

    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.text, 1);
    RoboCheckEqInt((int)RoboCssPropertyComputationCount, 74);
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationTextAlign),
                      1);
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationTextDecorationStyle),
                      1);
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationTextAlign),
                      "text-align");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationTextTransform),
                      "text-transform");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationWhiteSpace),
                      "white-space");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          (CssPropertyComputation)
                              RoboCssPropertyComputationCount),
                      0);
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainText), "text");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainText), 1);
    RoboCheckEqInt(RoboCssDomainCount, 37);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestTextDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: break-spaces", &list, &error),
                      RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationWhiteSpace,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationWhiteSpace,
                          CssDeclarationValue(declaration), &second, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&first, &second), 1);

    {
        CssDeclarationList otherList = RoboCssCleared;
        CssError otherError = RoboCssCleared;
        CssComputedValue other = RoboCssCleared;

        RoboCheckEqInt(CssParseDeclarations("o: pre-wrap", &otherList,
                                                 &otherError), RoboOk);
        declaration = CssDeclarationListAt(&otherList, 0u);
        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationWhiteSpace,
                              CssDeclarationValue(declaration), &other, NULL),
                          RoboOk);
        /* The two rows are different values, not two spellings of one — the same
         * distinction the fourth white-space column exists to make visible. */
        RoboCheckEqInt(CssComputedValueEquals(&first, &other), 0);
        CssComputedValueDispose(&other);
        CssDeclarationListDispose(&otherList);
    }

    CssComputedValueDispose(&first);
    CssComputedValueDispose(&second);
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void TestRegisterCssText(void);

void TestRegisterCssText(void)
{
    TestRegister("css.text.align_vocabulary", TestTextAlignVocabulary);
    TestRegister("css.text.align_last_vocabulary", TestTextAlignLastVocabulary);
    TestRegister("css.text.justify_vocabulary", TestTextJustifyVocabulary);
    TestRegister("css.text.transform_vocabulary", TestTextTransformVocabulary);
    TestRegister("css.text.white_space_vocabulary", TestWhiteSpaceVocabulary);
    TestRegister("css.text.word_break_vocabulary", TestWordBreakVocabulary);
    TestRegister("css.text.overflow_wrap_vocabulary", TestOverflowWrapVocabulary);
    TestRegister("css.text.line_break_vocabulary", TestLineBreakVocabulary);
    TestRegister("css.text.hyphens_vocabulary", TestHyphensVocabulary);
    TestRegister("css.text.decoration_style_vocabulary",
                 TestTextDecorationStyleVocabulary);
    TestRegister("css.text.decoration_line_vocabulary",
                 TestTextDecorationLineVocabulary);
    TestRegister("css.text.overflow_vocabulary", TestTextOverflowVocabulary);
    TestRegister("css.text.decoration_thickness_vocabulary",
                 TestTextDecorationThicknessVocabulary);
    TestRegister("css.text.underline_position_vocabulary",
                 TestTextUnderlinePositionVocabulary);
    TestRegister("css.text.computed", TestTextComputedShapes);
    TestRegister("css.text.registry", TestTextRegistry);
    TestRegister("css.text.capabilities", TestTextCapabilities);
    TestRegister("css.text.determinism", TestTextDeterminism);
}
