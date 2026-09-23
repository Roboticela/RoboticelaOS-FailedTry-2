/*
 * Font-fallback conformance (Phase 12.18) — `text.shaping.fallback.*`.
 *
 * The phase's §30 and §31, and the reason the fixture is a **pair** rather than one face: a fallback case
 * can only be honest if the primary's coverage is stated rather than discovered. `ConfSpecFallbackPrimary`
 * maps printable ASCII and nothing else; `ConfSpecFallbackSecondary` maps Greek and Cyrillic and no Latin.
 * So `"A Ω Б"` has one answer, and the host machine's installed fonts have no vote in it.
 *
 * ## Which door the cases use, and why
 *
 * Not `RoboTextShape`, which takes **one** face: a text shaped with one context is a text with one font, and
 * pretending otherwise would be the "find a font per code point" loop §20 forbids. The door is 12.5's
 * selector — `RoboTextFontSelectorSelectRange` — which decides *for a coherent range* and answers **which**
 * candidate answered. The cases then shape each range with the face the selector named, which is what proves
 * the selection is usable rather than merely returned: the glyph the run produces has no miss.
 *
 * ## What is checked
 *
 *   - the primary answers for a Latin range and the secondary for a Greek or Cyrillic one;
 *   - the source ranges the selector covers **tile** the text, so no scalar is left unselected;
 *   - a range is selected **as a whole** (§20): where no candidate covers every scalar, the first candidate
 *     that covers the *base* wins and the record says `complete = 0` rather than splitting the range — which
 *     is what keeps a base character and its combining mark in one face;
 *   - the selection is deterministic: the same query twice is the same answer.
 */

#include "RoboTextConformanceSupport.h"

#include <string.h>

#define FallbackSizePx 16.0f

/**
 * The pair, opened together: the primary, the secondary, and a selector over them.
 *
 * The **candidate lives in the record** rather than in `FallbackOpen`'s frame, and that is not tidiness: the
 * selector *borrows* the chain's entries, so a chain pointing at a local that has gone out of scope is a
 * dangling read the first time a scalar is selected. The structure is the lifetime.
 */
typedef struct FallbackPair {
    ConfFontSpec primarySpec;
    ConfFontSpec secondarySpec;
    ConfFace primary;
    ConfFace secondary;
    RoboTextFontCandidate candidate;
    RoboTextFontFallback chain;
    RoboTextFontSelector *selector;
} FallbackPair;

/** Build and open both faces and a selector with the primary's context and the secondary in the chain. */
static void FallbackOpen(FallbackPair *pair)
{
    memset(pair, 0, sizeof(*pair));
    ConfSpecFallbackPrimary(&pair->primarySpec);
    ConfSpecFallbackSecondary(&pair->secondarySpec);
    ConfFaceOpen(&pair->primary, &pair->primarySpec, FallbackSizePx);
    ConfFaceOpen(&pair->secondary, &pair->secondarySpec, FallbackSizePx);
    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&pair->selector, NULL), (int)RoboOk);
    RoboCheck(pair->selector != NULL);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(pair->selector, &pair->primary.font, NULL),
                   (int)RoboOk);
    pair->candidate.family = "Conformance Secondary";
    pair->candidate.face = pair->secondary.face;
    pair->chain.entries = &pair->candidate;
    pair->chain.count = 1u;
    RoboCheckEqInt((int)RoboTextFontSelectorSetFallback(pair->selector, &pair->chain, NULL), (int)RoboOk);
}

/** Release the pair. The selector first: it borrows both faces. */
static void FallbackClose(FallbackPair *pair)
{
    RoboTextFontSelectorDestroy(pair->selector);
    pair->selector = NULL;
    ConfFaceClose(&pair->secondary);
    ConfFaceClose(&pair->primary);
}

/** Select for @p text and report the face, asserting the selection names one. */
static FontFace *FallbackSelectText(FallbackPair *pair, const char *text, int *outComplete)
{
    RoboTextResolvedFont resolved;
    RoboTextView view = ConfView(text);
    RoboTextScalarIndex scalars = 0u;

    RoboCheckEqInt((int)RoboTextViewScalarCount(view, &scalars, NULL), (int)RoboOk);
    RoboTextResolvedFontInit(&resolved);
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRangeView(pair->selector, view,
                                                            RoboTextRangeFull(scalars), &resolved, NULL),
                   (int)RoboOk);
    RoboCheckMsg(resolved.face != NULL, "the selector named no face for a text both candidates can serve");
    if (outComplete) {
        *outComplete = resolved.complete;
    }
    return resolved.face;
}

/* ------------------------------------------------------------------ */
/* §30 — the primary answers for Latin and the chain for the rest       */
/* ------------------------------------------------------------------ */

/** `A Ω Б`: Latin from the primary, Greek and Cyrillic from the chain, and every range is served. */
static void TestShapingFallbackSelection(void)
{
    FallbackPair pair;

    FallbackOpen(&pair);
    /* One scalar at a time: the four answers the selector must give for the four characters. */
    {
        struct {
            uint32_t cp;
            int fromPrimary;
        } kCases[] = {{'A', 1}, {' ', 1}, {0x03A9u, 0}, {0x0411u, 0}};

        for (size_t i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
            RoboTextResolvedFont resolved;

            RoboTextResolvedFontInit(&resolved);
            RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(pair.selector, kCases[i].cp, &resolved,
                                                                 NULL),
                           (int)RoboOk);
            RoboCheck(resolved.face != NULL);
            if (kCases[i].fromPrimary) {
                RoboCheckMsg(resolved.face == pair.primary.face,
                             "a Latin scalar was not answered by the primary");
            } else {
                RoboCheckMsg(resolved.face == pair.secondary.face,
                             "a Greek or Cyrillic scalar was not answered by the chain");
            }
        }
    }
    /* A whole range: the Latin run completes on the primary, the Greek one on the chain. */
    {
        int complete = 0;

        RoboCheckMsg(FallbackSelectText(&pair, "Latin", &complete) == pair.primary.face,
                     "a Latin range was not answered by the primary");
        RoboCheckEqInt(complete, 1);
        RoboCheckMsg(FallbackSelectText(&pair, "\xCE\x91\xCE\x92\xCE\x93", &complete) == pair.secondary.face,
                     "a Greek range was not answered by the chain");
        RoboCheckEqInt(complete, 1);
    }
    FallbackClose(&pair);
}

/** Each selected range shaped with **its own** face produces no miss — selection is usable, not just made. */
static void TestShapingFallbackRangesAreServed(void)
{
    static const struct {
        const char *text;
        int fromPrimary;
    } kRanges[] = {{"Hello", 1}, {"\xCE\x91\xCE\xBB\xCF\x86\xCE\xB1", 0},
                   {"\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", 0}};
    FallbackPair pair;

    FallbackOpen(&pair);
    for (size_t i = 0u; i < sizeof(kRanges) / sizeof(kRanges[0]); i++) {
        FontFace *face = FallbackSelectText(&pair, kRanges[i].text, NULL);
        RoboTextShapeOptions options;
        RoboTextShapeResult *result = NULL;

        if (kRanges[i].fromPrimary) {
            RoboCheckMsg(face == pair.primary.face, "the primary did not answer for a Latin range");
        } else {
            RoboCheckMsg(face == pair.secondary.face, "the chain did not answer for a non-Latin range");
        }
        RoboTextShapeOptionsInit(&options);
        options.baseDirection = RoboTextDirectionLtr;
        /* Shape with the *selected* face, which is what a caller does once the selector has answered. */
        {
            ConfFace chosen;
            RoboTextFontStyle style;

            memset(&chosen, 0, sizeof(chosen));
            chosen.face = face;
            RoboTextFontStyleInit(&style);
            style.sizePx = FallbackSizePx;
            RoboCheckEqInt((int)RoboTextFontContextSet(&chosen.font, face, &style), (int)RoboOk);
            RoboCheckEqInt((int)RoboTextShape(ConfView(kRanges[i].text), &chosen.font, &options, &result,
                                              NULL),
                           (int)RoboOk);
        }
        RoboCheck(result != NULL);
        RoboCheckMsg(ConfTotalMissing(result) == 0u,
                     "a range was selected for a face that could not draw every scalar of it");
        ConfExpectValid(result, "text.shaping.fallback.ranges_are_served");
        RoboTextShapeResultDestroy(result);
    }
    FallbackClose(&pair);
}

/* ------------------------------------------------------------------ */
/* §20 — a range is selected whole, not per scalar                      */
/* ------------------------------------------------------------------ */

/**
 * A range no candidate covers completely is answered by the one that covers its **base**, with `complete = 0`.
 *
 * The alternative — splitting the range per scalar — is exactly the loop §20 forbids, and the reason it is
 * forbidden is visible here: the primary covers `e`, the secondary covers the Greek mark, and a per-scalar
 * walk would hand a base and its mark to two faces. The record says `complete = 0` so a caller that cares can
 * tell "one face covers all of this" from "one face covers enough of this".
 */
static void TestShapingFallbackRangeIsWhole(void)
{
    FallbackPair pair;
    RoboTextResolvedFont resolved;
    RoboTextCodePoint codePoints[2];
    int complete = 1;

    FallbackOpen(&pair);
    /* `e` (the primary's) then U+0301 (the chain's): no candidate covers both scalars. */
    codePoints[0] = (RoboTextCodePoint)'e';
    codePoints[1] = (RoboTextCodePoint)0x0301u;
    RoboTextResolvedFontInit(&resolved);
    RoboCheckEqInt((int)RoboTextFontSelectorSelectRange(pair.selector, codePoints, 2u, &resolved, NULL),
                   (int)RoboOk);
    RoboCheck(resolved.face != NULL);
    complete = resolved.complete;
    /* The base decides, and the record says the range was not completely covered. */
    RoboCheckMsg(resolved.face == pair.primary.face, "the base's own face did not answer for the range");
    RoboCheckEqInt(complete, 0);
    /* The same query twice: the answer is a value, not a race. */
    {
        RoboTextResolvedFont again;

        RoboTextResolvedFontInit(&again);
        RoboCheckEqInt((int)RoboTextFontSelectorSelectRange(pair.selector, codePoints, 2u, &again, NULL),
                       (int)RoboOk);
        RoboCheckMsg(RoboTextResolvedFontEqual(&resolved, &again),
                     "the selector answered differently for the same range");
    }
    FallbackClose(&pair);
}

/* ------------------------------------------------------------------ */
/* §30 — a range the primary covers is never overridden by the chain    */
/* ------------------------------------------------------------------ */

/** The chain is consulted in order and only on a miss: a covered scalar never reaches the second entry. */
static void TestShapingFallbackOrder(void)
{
    FallbackPair pair;

    FallbackOpen(&pair);
    {
        /* The primary covers `A`; the chain would also cover nothing for it, but the point is the order: a
         * candidate that covered everything could not displace the primary, because the primary is tried
         * first. */
        RoboTextResolvedFont resolved;

        RoboTextResolvedFontInit(&resolved);
        RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(pair.selector, (RoboTextCodePoint)'A',
                                                            &resolved, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)resolved.source, (int)RoboTextFontSourcePrimary);
        RoboCheckEqInt((int)resolved.candidateIndex, 0);
    }
    {
        RoboTextResolvedFont resolved;

        RoboTextResolvedFontInit(&resolved);
        RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(pair.selector, 0x0416u /* Ж */, &resolved,
                                                            NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)resolved.source, (int)RoboTextFontSourceFallback);
        RoboCheckEqInt((int)resolved.candidateIndex, 1);
    }
    {
        /* A scalar no candidate covers is an **answer** — no face — and not a failure. */
        RoboTextResolvedFont resolved;

        RoboTextResolvedFontInit(&resolved);
        RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(pair.selector, 0x4E2Du /* 中 */, &resolved,
                                                            NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)resolved.source, (int)RoboTextFontSourceNone);
        RoboCheck(resolved.face == NULL);
        RoboCheckEqInt(resolved.complete, 0);
    }
    FallbackClose(&pair);
}

/** One face, no chain: the 12.4 configuration is a supported one and is preserved by construction. */
static void TestShapingFallbackWithoutChain(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextFontSelector *selector = NULL;
    RoboTextResolvedFont resolved;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, FallbackSizePx);
    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, &face.font, NULL), (int)RoboOk);
    RoboTextResolvedFontInit(&resolved);
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, (RoboTextCodePoint)'A', &resolved, NULL),
                   (int)RoboOk);
    RoboCheck(resolved.face == face.face);
    RoboCheckEqInt((int)resolved.complete, 1);
    /* The fixture covers Greek too, so nothing falls off the end of a one-entry chain. */
    RoboTextResolvedFontInit(&resolved);
    RoboCheckEqInt((int)RoboTextFontSelectorSelectScalar(selector, 0x03A9u, &resolved, NULL), (int)RoboOk);
    RoboCheck(resolved.face == face.face);
    RoboTextFontSelectorDestroy(selector);
    ConfFaceClose(&face);
}

void TestRegisterTextShapingFallback(void)
{
    TestRegister("text.shaping.fallback.selection", TestShapingFallbackSelection);
    TestRegister("text.shaping.fallback.ranges_are_served", TestShapingFallbackRangesAreServed);
    TestRegister("text.shaping.fallback.range_is_whole", TestShapingFallbackRangeIsWhole);
    TestRegister("text.shaping.fallback.order", TestShapingFallbackOrder);
    TestRegister("text.shaping.fallback.without_chain", TestShapingFallbackWithoutChain);
}
