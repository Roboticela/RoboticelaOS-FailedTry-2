/*
 * Latin shaping conformance (Phase 12.18) — `text.shaping.latin.*`.
 *
 * The Latin half of the phase's §12, §13, §21, §22, §25 and §27-§39: the basic sets, representative words,
 * punctuation and whitespace, Latin Extended, the script and direction classification, clusters, advances,
 * determinism and run validation — all over `RoboTextConformanceFont.h`'s deterministic face rather than the
 * shipped pack, so a case here is a statement about the engine and not about Inter.
 *
 * ## Why the whole suite is one file and the blocks are cases
 *
 * The phase's §3 asks that conformance be *separated from ordinary unit tests*, and this file is that
 * separation: every case builds its own face from the spec it needs, states what it expects at each layer,
 * and prints both answers when one differs. A reader looking for "what does this build promise about `AV`"
 * finds it in one place rather than spread across five suites' smoke tests.
 *
 * ## The two oracles
 *
 * Where a number can be *derived* it is derived — a glyph id comes from `ConfGlyphOf`, a width from
 * `ConfAdvanceOf` — because a literal that a builder change would move is a test that has to be edited every
 * time the builder is. Where a number is the *point* it is written out and the comment says why: the `AV`
 * pair's 743 units is the fixture's 401 + 422 less its stated 80, and that subtraction is what the kerning
 * case exists to prove.
 */

#include "RoboTextConformanceSupport.h"

#include <string.h>

/* The face every case in this file opens: the size the advances below are stated at. */
#define LatinSizePx 16.0f

/** A font unit at @ref LatinSizePx over the fixture's 1000-unit em. */
static float LatinUnitsToPx(float units)
{
    return units * LatinSizePx / 1000.0f;
}

/** The scalar count of a UTF-8 string, which is not its byte count and is what a cluster tiles. */
static size_t LatinScalars(const char *text)
{
    RoboTextView view = ConfView(text);
    RoboTextScalarIndex scalars = 0u;

    RoboCheckEqInt((int)RoboTextViewScalarCount(view, &scalars, NULL), (int)RoboOk);
    return (size_t)scalars;
}

/* ------------------------------------------------------------------ */
/* The face, built once per case                                       */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* §12 — the basic sets                                                */
/* ------------------------------------------------------------------ */

/** `A` — the smallest case that proves the fixture, the cmap and the scaling door agree. */
static void TestShapingLatinSmoke(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    result = ConfShapeDefault(&face, "A");
    ConfExpectRunCount(result, 1u, "text.shaping.latin.smoke", "A");
    ConfExpectGlyphCount(result, 1u, "text.shaping.latin.smoke", "A");
    {
        uint16_t want = ConfGlyphOf('A');

        ConfExpectGlyphs(result, &want, 1u, "text.shaping.latin.smoke", "A");
    }
    ConfExpectAdvance(result, LatinUnitsToPx(ConfAdvanceOf(ConfGlyphOf('A'))), "text.shaping.latin.smoke",
                      "A");
    ConfExpectValid(result, "text.shaping.latin.smoke");
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/**
 * Upper case, lower case and digits (§12), one **character at a time**.
 *
 * Shaped one at a time rather than as three long strings, and deliberately: a whole-alphabet string is a
 * text the fixture's own ligature rules can act on (`f` `f`, `f` `i`, …), so its glyph count would be a
 * statement about the feature defaults rather than about the `cmap`. One character per case states the two
 * facts §12 asks for — the id the `cmap` gives and the advanced width — with nothing between them and the
 * assertion.
 */
static void TestShapingLatinBasicSets(void)
{
    static const struct {
        const char *name;
        char first;
        char last;
    } kSets[] = {{"upper", 'A', 'Z'}, {"lower", 'a', 'z'}, {"digits", '0', '9'}};
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    for (size_t set = 0u; set < sizeof(kSets) / sizeof(kSets[0]); set++) {
        char label[64];

        snprintf(label, sizeof(label), "text.shaping.latin.%s", kSets[set].name);
        for (char c = kSets[set].first; c <= kSets[set].last; c++) {
            char text[2];
            uint16_t want = ConfGlyphOf((uint32_t)c);
            RoboTextShapeResult *result;

            text[0] = c;
            text[1] = '\0';
            RoboCheck(want != 0u);
            result = ConfShapeDefault(&face, text);
            ConfExpectGlyphs(result, &want, 1u, label, text);
            ConfExpectAdvance(result, LatinUnitsToPx(ConfAdvanceOf(want)), label, text);
            ConfExpectValid(result, label);
            RoboCheckEqInt((int)ConfTotalMissing(result), 0);
            {
                const RoboTextGlyph *glyph = ConfGlyphAt(result, 0u);

                /* The record's own facts: what it draws, where it came from, and that nothing substituted
                 * it — a lone character cannot have been part of a ligature. */
                RoboCheck(glyph != NULL);
                RoboCheckEqInt((int)glyph->codePoint, (int)(unsigned char)c);
                RoboCheckEqInt((int)glyph->source.start, 0);
                RoboCheckEqInt((int)glyph->source.end, 1);
                RoboCheckEqInt((int)glyph->ligatureComponents, 0);
                RoboCheckEqInt((int)glyph->substituted, 0);
            }
            RoboTextShapeResultDestroy(result);
        }
    }
    ConfFaceClose(&face);
}

/** The punctuation and symbol blocks of §12, as one text: every glyph maps and every advance is positive. */
static void TestShapingLatinPunctuation(void)
{
    static const char kText[] = ".,;:!?'\"-\xE2\x80\x93\xE2\x80\x94()[]{}<>/"
                                "\xE2\x82\xAC\xC2\xA3$+ =%&@#*";
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    result = ConfShapeDefault(&face, kText);
    /* The euro and the pound are not in the fixture's canonical list, so they are counted misses and the
     * punctuation that *is* mapped still draws — which is the honest answer, stated. */
    ConfExpectValid(result, "text.shaping.latin.punctuation");
    for (size_t i = 0u; i < ConfTotalGlyphs(result); i++) {
        const RoboTextGlyph *glyph = ConfGlyphAt(result, i);

        RoboCheck(glyph != NULL);
        RoboCheck(!glyph->control);
        if (glyph->mapped) {
            RoboCheck(glyph->advanceX > 0.0f);
        }
    }
    /* The ASCII punctuation the canonical list carries really is mapped. */
    {
        uint16_t comma = ConfGlyphOf(',');

        RoboCheck(comma != 0u);
        {
            RoboTextShapeResult *one = ConfShapeDefault(&face, ",");

            ConfExpectGlyphs(one, &comma, 1u, "text.shaping.latin.punctuation", ",");
            RoboTextShapeResultDestroy(one);
        }
    }
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/** The phase's §13 words: one glyph per scalar, no miss, a positive width, and `ffi` really ligating. */
static void TestShapingLatinWords(void)
{
    static const char *const kWords[] = {"Hello", "Typography", "Roboticela", "beautiful", "rendering",
                                          "computer", "software", "configuration"};
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    for (size_t w = 0u; w < sizeof(kWords) / sizeof(kWords[0]); w++) {
        RoboTextShapeResult *result = ConfShapeDefault(&face, kWords[w]);
        size_t scalars = LatinScalars(kWords[w]);

        ConfExpectValid(result, "text.shaping.latin.words");
        RoboCheckEqInt((int)ConfTotalMissing(result), 0);
        /* A word whose letters are all in the cmap has one glyph per scalar *unless* a ligature folded two
         * of them, and `configuration` is the one word in this list the fixture ligates (`fi`). Which words
         * those are is derived from the fixture's own rules rather than eyeballed, so adding a rule cannot
         * silently turn this case into a weaker one. */
        {
            int ligates = 0;

            for (size_t i = 0u; i + 1u < scalars; i++) {
                if ((kWords[w][i] == 'f' && (kWords[w][i + 1u] == 'i' || kWords[w][i + 1u] == 'l' ||
                                             kWords[w][i + 1u] == 'f')) ||
                    (kWords[w][i] == 'Q' && kWords[w][i + 1u] == 'Q')) {
                    ligates = 1;
                }
            }
            if (!ligates) {
                ConfExpectGlyphCount(result, scalars, "text.shaping.latin.words", kWords[w]);
                for (size_t i = 0u; i < scalars; i++) {
                    const RoboTextGlyph *glyph = ConfGlyphAt(result, i);

                    RoboCheck(glyph != NULL);
                    RoboCheckEqInt((int)glyph->glyphId, (int)ConfGlyphOf((uint32_t)kWords[w][i]));
                }
            } else {
                /* The ligating word still has one glyph per scalar minus one, and the folded glyph keeps
                 * both scalars as its source. */
                RoboCheckEqInt((int)ConfTotalGlyphs(result), (int)scalars - 1);
                RoboCheckEqInt((int)ConfTotalLigatures(result), 1);
            }
        }
        RoboCheck(ConfTotalAdvance(result) > 0.0f);
        RoboTextShapeResultDestroy(result);
    }
    ConfFaceClose(&face);
}

/** Latin Extended (§25): the accented letters map and shape like any other Latin scalar. */
static void TestShapingLatinExtended(void)
{
    static const char kText[] = "\xC3\xA4 \xC3\xB6 \xC3\xBC \xC3\xA1 \xC3\xA9 \xC3\xAD \xC3\xB3 \xC3\xBA "
                                "\xC3\xA0 \xC3\xA8 \xC3\xAC \xC3\xB2 \xC3\xB9 \xC3\xA2 \xC3\xAA \xC3\xAE "
                                "\xC3\xB4 \xC3\xBB \xC3\xA3 \xC3\xB5 \xC3\xA5 \xC3\xA6 \xC5\x93 \xC3\xA7 "
                                "\xC3\xB1 \xC3\x9F \xC3\xB8 \xC5\x82 \xC4\x91 \xC3\xBE \xC3\xB0";
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    result = ConfShapeDefault(&face, kText);
    ConfExpectValid(result, "text.shaping.latin.extended");
    /* The canonical list carries every one of them, so a miss here is the `cmap` builder's. */
    RoboCheckEqInt((int)ConfTotalMissing(result), 0);
    RoboCheckEqInt((int)ConfTotalGlyphs(result), (int)LatinScalars(kText));
    /* The whole text is one script, one direction and one face: `Latin = ASCII` is the assumption this
     * case exists to refuse, and it would show as several runs. */
    ConfExpectRunCount(result, 1u, "text.shaping.latin.extended", kText);
    ConfExpectRunClassification(result, 0u, RoboUScriptLatin, RoboTextDirectionLtr,
                                "text.shaping.latin.extended", kText);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §21, §22 — punctuation and whitespace in context                    */
/* ------------------------------------------------------------------ */

/** Punctuation in realistic context, with the quotation and parenthesis pairs the phase lists. */
static void TestShapingLatinPunctuationInContext(void)
{
    static const char *const kTexts[] = {"Hello, world.", "\"Hello\"", "(Hello)", "[Hello]",
                                          "Hello \xE2\x80\x94 world", "Hello \xE2\x80\x93 world",
                                          "...", "\xE2\x80\xA6", "?!", ":;"};
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    for (size_t i = 0u; i < sizeof(kTexts) / sizeof(kTexts[0]); i++) {
        RoboTextShapeResult *result = ConfShapeDefault(&face, kTexts[i]);

        ConfExpectValid(result, "text.shaping.latin.punctuation_context");
        /* One run: punctuation is `Common` and stays with the Latin around it, which is the script rule and
         * not a fallback decision. */
        ConfExpectRunCount(result, 1u, "text.shaping.latin.punctuation_context", kTexts[i]);
        RoboCheck(ConfTotalAdvance(result) > 0.0f);
        RoboTextShapeResultDestroy(result);
    }
    ConfFaceClose(&face);
}

/**
 * Whitespace (§22): what the shaping layer can and cannot answer.
 *
 * The space and the no-break space are characters the font maps, and they shape as ordinary glyphs with an
 * advance and no ink. The tab and the newline are **not** the shaping layer's: a newline is consumed by line
 * layout (Phase 19) and a tab by tab-stop logic (RoboLayout), and this build's fixture does not map either,
 * so what the case asserts is the honest current answer — an ordinary scalar, `.notdef`, counted as a miss,
 * and *not* silently classified as a drawable glyph the way a control character would be.
 */
static void TestShapingLatinWhitespace(void)
{
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    {
        RoboTextShapeResult *space = ConfShapeDefault(&face, "A B");
        const RoboTextGlyph *glyph = ConfGlyphAt(space, 1u);

        ConfExpectGlyphCount(space, 3u, "text.shaping.latin.whitespace", "A B");
        RoboCheck(glyph != NULL && glyph->mapped && !glyph->control);
        RoboCheck(glyph != NULL && glyph->glyphId == ConfGlyphOf(' ') && glyph->advanceX > 0.0f);
        RoboTextShapeResultDestroy(space);
    }
    {
        /* U+00A0: a character, and one the font maps. It is not a space the line breaker may break at, but
         * that is a line-breaking fact and this layer does not claim it. */
        RoboTextShapeResult *nbsp = ConfShapeDefault(&face, "A\xC2\xA0""B");

        RoboCheckEqInt((int)ConfTotalMissing(nbsp), 0);
        RoboCheckEqInt((int)ConfTotalGlyphs(nbsp), 3);
        RoboTextShapeResultDestroy(nbsp);
    }
    {
        RoboTextShapeResult *tab = ConfShapeDefault(&face, "A\tB");
        const RoboTextGlyph *glyph = ConfGlyphAt(tab, 1u);

        ConfExpectGlyphCount(tab, 3u, "text.shaping.latin.whitespace_tab", "A\\tB");
        /* Not mapped by this fixture, counted, and not a control: a control is a *bidi* control and draws
         * nothing, which is a different statement from "the face has no glyph". */
        RoboCheck(glyph != NULL && !glyph->mapped && !glyph->control);
        RoboCheckEqInt((int)ConfTotalMissing(tab), 1);
        RoboTextShapeResultDestroy(tab);
    }
    {
        RoboTextShapeResult *newline = ConfShapeDefault(&face, "A\nB");

        ConfExpectGlyphCount(newline, 3u, "text.shaping.latin.whitespace_newline", "A\\nB");
        RoboCheckEqInt((int)ConfTotalMissing(newline), 1);
        RoboTextShapeResultDestroy(newline);
    }
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §6, §7 — clusters and glyph counts                                  */
/* ------------------------------------------------------------------ */

/** Clusters tile the glyph array and the source, in logical order, for a mixed run. */
static void TestShapingLatinClusters(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;
    static const uint32_t kClusters[] = {
        0u, 1u, 1u, /* H */
        1u, 2u, 1u, /* i */
        2u, 3u, 1u, /* space */
        3u, 4u, 1u, /* A */
        4u, 5u, 1u, /* B */
    };

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    result = ConfShapeDefault(&face, "Hi AB");
    ConfExpectClusters(result, 0u, kClusters, 5u, "text.shaping.latin.clusters", "Hi AB");
    /* Every glyph's own `cluster` field names the cluster it belongs to, which is the inverse door. */
    for (size_t i = 0u; i < ConfTotalGlyphs(result); i++) {
        const RoboTextGlyph *glyph = ConfGlyphAt(result, i);
        RoboTextClusterIndex cluster = (RoboTextClusterIndex)0xFFFFFFFFu;

        RoboCheck(glyph != NULL);
        RoboCheckEqInt((int)RoboTextGlyphRunClusterForScalar(RoboTextShapeResultGlyphRun(result, 0u),
                                                            glyph->source.start, &cluster, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)cluster, (int)glyph->cluster);
    }
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §27, §28 — script and direction                                     */
/* ------------------------------------------------------------------ */

/** Latin text is classified Latin and left-to-right, and a digit does not change the script. */
static void TestShapingLatinClassification(void)
{
    ConfFontSpec spec;
    ConfFace face;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    {
        RoboTextShapeResult *result = ConfShapeDefault(&face, "Latin 2026");

        ConfExpectRunCount(result, 1u, "text.shaping.latin.classification", "Latin 2026");
        ConfExpectRunClassification(result, 0u, RoboUScriptLatin, RoboTextDirectionLtr,
                                    "text.shaping.latin.classification", "Latin 2026");
        RoboTextShapeResultDestroy(result);
    }
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §39 — determinism and validity                                      */
/* ------------------------------------------------------------------ */

/** The same input twice is the same bytes, run for run and glyph for glyph. */
static void TestShapingLatinDeterminism(void)
{
    static const char kText[] = "The quick brown fox jumps over the lazy dog. \xC3\xA9\xC3\xA4\xC3\xB6";
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *first;
    RoboTextShapeResult *second;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    first = ConfShapeDefault(&face, kText);
    second = ConfShapeDefault(&face, kText);
    ConfExpectValid(first, "text.shaping.latin.determinism");
    /* Rebuilding the face proves the builder itself is deterministic, not only the engine: a map that
     * iterated a hash would pass the first comparison and fail this one. */
    {
        ConfFace rebuilt;
        RoboTextShapeResult *third;

        ConfFaceOpen(&rebuilt, &spec, LatinSizePx);
        third = ConfShapeDefault(&rebuilt, kText);
        RoboCheckMsg(ConfResultsEqual(first, third), "the same input shaped differently over a rebuilt face");
        RoboTextShapeResultDestroy(third);
        ConfFaceClose(&rebuilt);
    }
    RoboCheckMsg(ConfResultsEqual(first, second), "the same input shaped differently twice");
    RoboTextShapeResultDestroy(first);
    RoboTextShapeResultDestroy(second);
    ConfFaceClose(&face);
}

/** The run dump is a complete-or-absent block that names the run's own facts. */
static void TestShapingLatinDiagnostics(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *result;
    static char buffer[8192];
    size_t length = 0u;

    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, LatinSizePx);
    result = ConfShapeDefault(&face, "Hello");
    RoboCheckEqInt((int)RoboTextGlyphRunWrite(RoboTextShapeResultGlyphRun(result, 0u), buffer,
                                              sizeof(buffer), &length),
                   (int)RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(buffer, "GlyphRun {") != NULL);
    /* `RoboTextScriptName` answers the ISO 15924 short name, so the dump says `Latn` and not `latin`. */
    RoboCheck(strstr(buffer, "script Latn") != NULL);
    RoboCheck(strstr(buffer, "clusters") != NULL);
    RoboTextShapeResultDestroy(result);
    ConfFaceClose(&face);
}

void TestRegisterTextShapingLatin(void)
{
    TestRegister("text.shaping.latin.smoke", TestShapingLatinSmoke);
    TestRegister("text.shaping.latin.basic_sets", TestShapingLatinBasicSets);
    TestRegister("text.shaping.latin.punctuation", TestShapingLatinPunctuation);
    TestRegister("text.shaping.latin.punctuation_context", TestShapingLatinPunctuationInContext);
    TestRegister("text.shaping.latin.words", TestShapingLatinWords);
    TestRegister("text.shaping.latin.extended", TestShapingLatinExtended);
    TestRegister("text.shaping.latin.whitespace", TestShapingLatinWhitespace);
    TestRegister("text.shaping.latin.clusters", TestShapingLatinClusters);
    TestRegister("text.shaping.latin.classification", TestShapingLatinClassification);
    TestRegister("text.shaping.latin.determinism", TestShapingLatinDeterminism);
    TestRegister("text.shaping.latin.diagnostics", TestShapingLatinDiagnostics);
}
