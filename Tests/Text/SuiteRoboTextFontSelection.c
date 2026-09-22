/*
 * RoboText's font coverage and selection (Phase 12.5) — `text.fontselection.*`.
 *
 * Every case that needs a face uses the **shipped** pack through the fixture, because a suite that tested a
 * substitute could pass while the installed image was broken. The one thing the pack cannot give this suite
 * is a *second* family: `/System/Fonts/` ships Inter alone, so a chain entry that covers what the primary
 * does not cannot be constructed from real faces yet. The suite therefore tests the selection *policy* the
 * way the pack permits it:
 *
 *   - a covering primary wins, and the record says `primary`;
 *   - a primary with **no face** falls through to the chain, and the record says `fallback` with the
 *     candidate's index — the fall-through path, exercised without a second file;
 *   - nothing covering anything is `none`, which is the honest answer and the one a missing CJK face
 *     produces today.
 *
 * When the pack gains a CJK or emoji family, the only change here is to add it to the chain; the policy
 * cases below already assert the ordering they would exercise.
 */

#include "RoboTextTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static RoboTextView ViewOf(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

/** A context with no face at the CSS initial setting: "ask the chain". */
static RoboTextFontContext NoFaceContext(void)
{
    RoboTextFontContext context;
    RoboTextFontStyle style;

    RoboTextFontContextInit(&context);
    RoboTextFontStyleInit(&style);
    RoboCheckEqInt((int)RoboTextFontContextSet(&context, NULL, &style), (int)RoboOk);
    return context;
}

/** Why the selector chose a face, for a readable failure. */
static void CheckSource(const RoboTextResolvedFont *font, RoboTextFontSource source)
{
    RoboCheckMsg(font->source == source, "the selector chose an unexpected source");
}

/* ------------------------------------------------------------------ */
/* Coverage                                                            */
/* ------------------------------------------------------------------ */

static void TestCoverage(void)
{
    TextFixture fixture = TextFixtureOpen();
    int supported = 0;
    size_t missing = 0u;

    /* The Latin letter, the digit and the space a UI face must have. */
    RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, 'A', &supported, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(supported, 1);
    RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, '0', &supported, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(supported, 1);
    RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, ' ', &supported, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(supported, 1);
    RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, 0x00E9u, &supported, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(supported, 1);

    /* A noncharacter no conforming face has a glyph for, and a Han ideograph the UI face does not carry. */
    RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, 0xFDD0u, &supported, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(supported, 0);
    RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, 0x4E2Du, &supported, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(supported, 0);

    /* A value that is not a scalar is not a character: covered by nobody, and not an error. */
    RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, 0xD800u, &supported, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(supported, 0);

    /*
     * The coverage answer agrees with the cmap lookup, scalar by scalar. That is the property that keeps
     * this door from becoming a second, subtly different opinion about the same table: a scalar is covered
     * exactly when the cmap maps it to a non-zero glyph id.
     */
    {
        static const RoboTextCodePoint samples[] = {0x0041u, 0x0061u, 0x0030u, 0x0020u, 0x00E9u,
                                                    0x4E2Du, 0x05E9u, 0xFDD0u};

        for (size_t index = 0u; index < sizeof(samples) / sizeof(samples[0]); index++) {
            FontCmapLookupResult lookup;
            int mapped = 0;

            RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, samples[index], &supported,
                                                                  NULL),
                           (int)RoboOk);
            memset(&lookup, 0, sizeof(lookup));
            RoboCheckEqInt((int)FontFaceLookupGlyph(fixture.face, (uint32_t)samples[index], &lookup),
                           (int)RoboOk);
            mapped = (lookup.status == RoboFontCmapMapped && lookup.glyphId != RoboGlyphIdNone) ? 1 : 0;
            RoboCheckEqInt(supported, mapped);
        }
    }

    /* The run-level door counts what is missing rather than answering yes or no. */
    {
        static const uint32_t allCovered[] = {'A', 'B', 'C'};
        static const uint32_t someMissing[] = {'A', 0x4E2Du, 'C'};

        RoboCheckEqInt((int)RoboTextFontFaceSupportsRange(fixture.face, allCovered, 3u, &missing, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)missing, 0);
        RoboCheckEqInt((int)RoboTextFontFaceSupportsRange(fixture.face, someMissing, 3u, &missing, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)missing, 1);
        RoboCheckEqInt((int)RoboTextFontFaceSupportsRange(fixture.face, NULL, 0u, &missing, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)missing, 0);
    }

    /* Refusals: a NULL face or out is a caller bug and is reported as one. */
    RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(NULL, 'A', &supported, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontFaceSupportsCodePoint(fixture.face, 'A', NULL, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontFaceSupportsRange(fixture.face, NULL, 3u, &missing, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontFaceSupportsRange(NULL, NULL, 0u, &missing, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontFaceSupportsRange(fixture.face, NULL, 0u, NULL, NULL),
                   (int)RoboInvalidArgument);

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Selection                                                           */
/* ------------------------------------------------------------------ */

static void TestSelectionPrimary(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = NULL;
    RoboTextResolvedFont font;

    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheck(selector != NULL);
    RoboCheck(!RoboTextFontSelectorIsUsable(selector));
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, &fixture.font, NULL), (int)RoboOk);
    RoboCheck(RoboTextFontSelectorIsUsable(selector));
    RoboCheck(RoboTextFontSelectorPrimary(selector) != NULL);
    RoboCheckEqInt((int)RoboTextFontSelectorFallback(selector)->count, 0);

    /* A covered scalar: the requested face, recorded as the primary. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 'A', &font, NULL), (int)RoboOk);
    CheckSource(&font, RoboTextFontSourcePrimary);
    RoboCheck(font.face == fixture.face);
    RoboCheck(font.faceId == GetFontFaceId(fixture.face));
    RoboCheckEqInt((int)font.candidateIndex, 0);
    RoboCheckEqInt(font.complete, 1);
    RoboCheck(RoboTextResolvedFontIsSet(&font));
    RoboCheckEqInt((int)font.weight, 400);
    RoboCheck(font.sizePx == 16.0f);
    RoboCheckStrEq(font.family, "");

    /* An uncovered scalar: nothing selected, and that is an answer rather than a failure. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 0x4E2Du, &font, NULL), (int)RoboOk);
    CheckSource(&font, RoboTextFontSourceNone);
    RoboCheck(font.face == NULL);
    RoboCheckEqInt((int)font.faceId, 0);
    RoboCheck(!RoboTextResolvedFontIsSet(&font));
    RoboCheckEqInt((int)font.candidateIndex, (int)RoboTextFontNoCandidate);
    /* The requested setting still travels, so a caller drawing `.notdef` knows the size it was set at. */
    RoboCheck(font.sizePx == 16.0f);

    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 0xFDD0u, &font, NULL), (int)RoboOk);
    CheckSource(&font, RoboTextFontSourceNone);

    /* Refusals. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(NULL, 'A', &font, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 'A', NULL, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(NULL, &fixture.font, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, NULL, NULL), (int)RoboInvalidArgument);
    {
        RoboTextFontStyle broken;
        RoboTextFontContext context;

        RoboTextFontStyleInit(&broken);
        broken.sizePx = 0.0f;
        /* A context cannot hold a setting this build cannot draw, so the selector is handed a record
         * whose style was made invalid after the fact — the one way a caller can smuggle one in. */
        RoboCheckEqInt((int)RoboTextFontContextSet(&context, fixture.face, &broken), (int)RoboInvalidArgument);
        RoboTextFontContextInit(&context);
        context.face = fixture.face;
        context.style.sizePx = 0.0f;
        RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, &context, NULL),
                       (int)RoboInvalidArgument);
    }

    RoboTextFontSelectorDestroy(selector);
    RoboTextFontSelectorDestroy(NULL);
    TextFixtureClose(&fixture);
}

static void TestSelectionFallback(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = NULL;
    RoboTextFontContext noFace = NoFaceContext();
    RoboTextFontCandidate candidate;
    RoboTextFontFallback chain;
    RoboTextResolvedFont font;

    memset(&candidate, 0, sizeof(candidate));
    candidate.family = "Inter";
    candidate.face = fixture.face;
    chain.entries = &candidate;
    chain.count = 1u;

    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, &noFace, NULL), (int)RoboOk);
    /* A primary with no face is a supported configuration, but it answers nothing on its own. */
    RoboCheck(!RoboTextFontSelectorIsUsable(selector));
    RoboCheckEqInt((int)RoboTextFontSelectorSetFallback(selector, &chain, NULL), (int)RoboOk);
    RoboCheck(RoboTextFontSelectorIsUsable(selector));
    RoboCheckEqInt((int)RoboTextFontSelectorFallback(selector)->count, 1);
    RoboCheck(RoboTextFontSelectorFallback(selector)->entries == &candidate);

    /* The primary cannot cover anything, so the chain answers — and the record says which entry did. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 'A', &font, NULL), (int)RoboOk);
    CheckSource(&font, RoboTextFontSourceFallback);
    RoboCheck(font.face == fixture.face);
    RoboCheckEqInt((int)font.candidateIndex, 1);
    RoboCheckStrEq(font.family, "Inter");
    RoboCheck(font.complete == 1);

    /* A candidate that cannot cover either: still no answer, and no invented one. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 0x4E2Du, &font, NULL), (int)RoboOk);
    CheckSource(&font, RoboTextFontSourceNone);

    /* Chain bookkeeping refusals. */
    {
        RoboTextFontFallback bad;

        bad.entries = NULL;
        bad.count = 2u;
        RoboCheckEqInt((int)RoboTextFontSelectorSetFallback(selector, &bad, NULL), (int)RoboInvalidArgument);
        bad.entries = &candidate;
        RoboCheckEqInt((int)RoboTextFontSelectorSetFallback(NULL, &bad, NULL), (int)RoboInvalidArgument);
        /* An empty chain is legal: the 12.4 behaviour, expressed as data. */
        bad.entries = NULL;
        bad.count = 0u;
        RoboCheckEqInt((int)RoboTextFontSelectorSetFallback(selector, &bad, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 'A', &font, NULL), (int)RoboOk);
        CheckSource(&font, RoboTextFontSourceNone);
    }

    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

static void TestSelectionRange(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = NULL;
    RoboTextResolvedFont font;
    static const RoboTextCodePoint covered[] = {'H', 'e', 'l'};
    static const RoboTextCodePoint mixed[] = {'A', 0x4E2Du, 'B'};
    static const RoboTextCodePoint uncovered[] = {0x4E2Du, 0x754Cu};

    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, &fixture.font, NULL), (int)RoboOk);

    /* Every scalar covered: the primary wins and the record says the run is complete. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRange(selector, covered, 3u, &font, NULL), (int)RoboOk);
    CheckSource(&font, RoboTextFontSourcePrimary);
    RoboCheckEqInt(font.complete, 1);

    /*
     * A run whose *base* is covered but whose tail is not: the primary answers with `complete = 0` rather
     * than splitting the run. That is the difference between the scalar door and this one, and it is what
     * keeps a base character and its combining mark in one face.
     */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRange(selector, mixed, 3u, &font, NULL), (int)RoboOk);
    CheckSource(&font, RoboTextFontSourcePrimary);
    RoboCheckEqInt(font.complete, 0);

    /* Nothing covers even the base. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRange(selector, uncovered, 2u, &font, NULL), (int)RoboOk);
    CheckSource(&font, RoboTextFontSourceNone);

    /* An empty run has nothing to cover: the primary is the answer and it is trivially complete. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRange(selector, NULL, 0u, &font, NULL), (int)RoboOk);
    CheckSource(&font, RoboTextFontSourcePrimary);
    RoboCheckEqInt(font.complete, 1);

    /* Over a view: the convenience door decodes the range and agrees with the array door. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRangeView(selector, ViewOf("H\xE4\xB8\xAD" /* H 中 */),
                                                            RoboTextRangeMake(0u, 1u), &font, NULL),
                   (int)RoboOk);
    CheckSource(&font, RoboTextFontSourcePrimary);
    RoboCheckEqInt(font.complete, 1);
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRangeView(selector, ViewOf("H\xE4\xB8\xAD"),
                                                            RoboTextRangeMake(0u, 2u), &font, NULL),
                   (int)RoboOk);
    CheckSource(&font, RoboTextFontSourcePrimary);
    RoboCheckEqInt(font.complete, 0);
    /* A range outside the text is refused rather than clamped. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRangeView(selector, ViewOf("H"), RoboTextRangeMake(0u, 4u),
                                                            &font, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRange(NULL, covered, 3u, &font, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRange(selector, NULL, 3u, &font, NULL),
                   (int)RoboInvalidArgument);

    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

static void TestCoverageCache(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *selector = NULL;
    RoboTextResolvedFont font;
    RoboTextCoverageCacheStats stats;

    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, &fixture.font, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorCacheStats(selector, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.queries, 0);

    /* First query: a miss that reaches the face. Second: a hit that does not. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 'A', &font, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 'A', &font, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorCacheStats(selector, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.queries, 2);
    RoboCheckEqInt((int)stats.misses, 1);
    RoboCheckEqInt((int)stats.hits, 1);
    RoboCheckEqInt((int)stats.entries, 1);

    /* A different scalar that hashes to another slot is its own miss, and the answer is still right. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 0x4E2Du, &font, NULL), (int)RoboOk);
    CheckSource(&font, RoboTextFontSourceNone);
    RoboCheckEqInt((int)RoboTextFontSelectorCacheStats(selector, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.misses, 2);
    RoboCheckEqInt((int)stats.entries, 2);

    /* Clearing empties the memo and the counters, so the next query reaches the face again. */
    RoboTextFontSelectorCacheClear(selector);
    RoboCheckEqInt((int)RoboTextFontSelectorCacheStats(selector, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.queries, 0);
    RoboCheckEqInt((int)stats.entries, 0);
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 'A', &font, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorCacheStats(selector, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.misses, 1);
    RoboCheckEqInt((int)RoboTextFontSelectorCacheStats(NULL, &stats), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextFontSelectorCacheStats(selector, NULL), (int)RoboInvalidArgument);
    RoboTextFontSelectorCacheClear(NULL);

    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

static void TestDeterminismAndVocabulary(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontSelector *first = NULL;
    RoboTextFontSelector *second = NULL;
    RoboTextResolvedFont a;
    RoboTextResolvedFont b;
    char line[192];
    size_t length = 0u;

    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&first, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&second, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(first, &fixture.font, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(second, &fixture.font, NULL), (int)RoboOk);

    /*
     * Same text, same configuration, same database: the same selection, field for field. Two independently
     * created selectors are used on purpose — a determinism check against one object would only prove it
     * remembers its own answer.
     */
    {
        static const RoboTextCodePoint samples[] = {'A', 'x', '0', ' ', 0x00E9u, 0x4E2Du, 0xFDD0u,
                                                    0x10FFFFu};

        for (size_t index = 0u; index < sizeof(samples) / sizeof(samples[0]); index++) {
            RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(first, samples[index], &a, NULL),
                           (int)RoboOk);
            RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(second, samples[index], &b, NULL),
                           (int)RoboOk);
            RoboCheck(RoboTextResolvedFontEqual(&a, &b));
            RoboCheckEqInt(RoboTextResolvedFontCompare(&a, &b), 0);
        }
    }

    /* The identity vocabulary: known sources have distinct names; an unknown source is named. */
    RoboCheck(RoboTextFontSourceIsKnown(RoboTextFontSourceNone));
    RoboCheck(RoboTextFontSourceIsKnown(RoboTextFontSourcePrimary));
    RoboCheck(RoboTextFontSourceIsKnown(RoboTextFontSourceFallback));
    RoboCheck(!RoboTextFontSourceIsKnown((RoboTextFontSource)7));
    RoboCheckStrEq(RoboTextFontSourceName(RoboTextFontSourceNone), "none");
    RoboCheckStrEq(RoboTextFontSourceName(RoboTextFontSourcePrimary), "primary");
    RoboCheckStrEq(RoboTextFontSourceName(RoboTextFontSourceFallback), "fallback");
    RoboCheckStrEq(RoboTextFontSourceName((RoboTextFontSource)7), "none");

    /* Identity equality ignores the borrowed pointer and compares the id, the setting and the provenance. */
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(first, 'A', &a, NULL), (int)RoboOk);
    b = a;
    RoboCheck(RoboTextResolvedFontEqual(&a, &b));
    b.weight = 700;
    RoboCheck(!RoboTextResolvedFontEqual(&a, &b));
    RoboCheck(RoboTextResolvedFontCompare(&a, &b) != 0);
    RoboCheck(RoboTextResolvedFontEqual(NULL, NULL));
    RoboCheck(!RoboTextResolvedFontEqual(&a, NULL));
    {
        RoboTextResolvedFont null;

        RoboTextResolvedFontInit(&null);
        RoboCheck(!RoboTextResolvedFontIsSet(&null));
        RoboCheckStrEq(null.family, "");
        RoboCheckEqInt((int)null.candidateIndex, (int)RoboTextFontNoCandidate);
        RoboTextResolvedFontInit(NULL);
    }

    /* A dump line names the selection, and a buffer too small refuses rather than truncating. */
    RoboCheckEqInt((int)RoboTextResolvedFontWrite(&a, line, sizeof(line), &length), (int)RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(line, "source=primary") != NULL);
    RoboCheck(strstr(line, "complete=1") != NULL);
    {
        char tiny[8];

        RoboCheckEqInt((int)RoboTextResolvedFontWrite(&a, tiny, sizeof(tiny), &length), (int)RoboNotSupported);
        RoboCheckStrEq(tiny, "");
    }
    RoboCheckEqInt((int)RoboTextResolvedFontWrite(NULL, line, sizeof(line), &length),
                   (int)RoboInvalidArgument);

    RoboTextFontSelectorDestroy(first);
    RoboTextFontSelectorDestroy(second);
    TextFixtureClose(&fixture);
}

static void TestFuzz(void)
{
    static const uint32_t codePoints[] = {0x0000u, 0x0041u, 0xD800u, 0x4E2Du, 0x10FFFFu, 0xFFFFFFFFu};

    RoboCheckEqInt((int)RoboTextFuzzFontSelection(codePoints, 6u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFuzzFontSelection(NULL, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFuzzFontSelection(NULL, 2u), (int)RoboInvalidArgument);
}

void TestRegisterTextFontSelection(void)
{
    TestRegister("text.fontselection.coverage", TestCoverage);
    TestRegister("text.fontselection.primary", TestSelectionPrimary);
    TestRegister("text.fontselection.fallback", TestSelectionFallback);
    TestRegister("text.fontselection.range", TestSelectionRange);
    TestRegister("text.fontselection.cache", TestCoverageCache);
    TestRegister("text.fontselection.determinism", TestDeterminismAndVocabulary);
    TestRegister("text.fontselection.fuzz", TestFuzz);
}
