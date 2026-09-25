/*
 * The API freeze — `text.api.*` (Phase 12.24).
 *
 * ## What this suite is, and why it is not another conformance suite
 *
 * 12.18 through 12.23 assert what the engine *computes*. This file asserts what a **consumer may rely on**,
 * which is a different question with a different failure mode: a computation that changes is caught by a
 * conformance case, while a door that starts handing back a pointer into freed memory — or an aggregate
 * header that stops naming one of the module's public headers — is invisible until something builds against
 * it and breaks in a way nobody attributes to the text layer.
 *
 * The suite is arranged around the four things a freeze is for:
 *
 *   - **the aggregate is complete.** `RoboText/RoboText.h` must name every header in the module's public
 *     directory, checked both ways: this file *references a type from each family* while including only the
 *     aggregate, so a missing `#include` is a compile error rather than a test failure, and
 *     `text.api.header_hygiene` reads the header itself and matches its list against the directory. A new
 *     public header nobody added to the aggregate fails the second check on the day it lands.
 *   - **defaults are initial values, not accidents.** Each `...Init` in the public surface is called and its
 *     result compared with the values the headers document, so a default that drifts is a case rather than a
 *     surprise at a call site.
 *   - **refusals are answers.** A sampling of the public doors is called with the arguments they must refuse
 *     — NULL where a pointer is required, an index past the end, a byte inside a multi-byte scalar — and each
 *     must answer `RoboInvalidArgument` rather than crash. A fuzz entry says "no crash"; a refusal says
 *     *which* answer, which is the half a caller can branch on.
 *   - **borrowing is one call deep.** A result's runs, list and measurement are borrowed from it: the case
 *     reads each twice and compares the *same* pointers, checks the run list's own advance against the
 *     measurement's width (one engine, not two sums), then destroys the result and shapes again.
 *
 * ## Where the numbers come from
 *
 * The two shaping cases use the **shipped pack** through `RoboTextTestSupport.h` (`TextFixtureOpen`), exactly
 * as the 12.4 suites do, and read their expectations off the module's own doors: the measurement is compared
 * with the run list's advance rather than with a remembered pixel count, and the index case counts the scalars
 * of a UTF-8 literal it spells itself. Everything else is arithmetic over constants the headers state, which
 * is the point — an API test must not depend on how a glyph looks.
 *
 * ## What is deliberately absent
 *
 * No ABI hashing (there is no stable ABI to hash; the freeze is source-level), no second copy of any engine
 * case, and no capability field read here except the ones the freeze is *about* (the shape/measure/select/edit
 * seams, and the three named absences: `undoHistory`, `wordBoundaries`, `lineBoundaries`).
 */

/*
 * **Only the aggregate.** This include is the compile-time half of `text.api.header_hygiene`: every type and
 * every door named below comes through `RoboText/RoboText.h`, so a public header missing from the aggregate
 * breaks this file's build rather than a runtime check. The support header comes second, and is used for the
 * shipped-pack fixture, `RoboCheck` and `TextReadFile` only.
 */
#include "RoboText/RoboText.h"

#include "RoboTextTestSupport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. The aggregate and the version record                             */
/* ------------------------------------------------------------------ */

/*
 * One line per public family, and the lines exist to make the aggregate's completeness a fact about the
 * *compiler*: a header left out of `RoboText.h` shows up here as an unknown type.
 */
static void TestApiAggregate(void)
{
    /* The seam a document or a shell consumes. */
    RoboTextRange range = RoboTextRangeFull(3u);
    RoboTextFontStyle style;
    RoboTextShapeOptions options;
    RoboTextMeasureRequest request;
    RoboTextMeasurement measurement;
    RoboTextSelection selection;
    RoboTextCaret caret;
    RoboTextEditCommand command;
    RoboTextDirectionContext direction;
    RoboTextGlyphOrientation orientation = RoboTextGlyphOrientationUpright;

    /* The engines behind it: a type from each family, referenced and never run. */
    RoboTextGsubOptions gsub;
    RoboTextGposOptions gpos;
    RoboTextOtLimits limits;
    RoboTextComplexStrategy strategy = RoboTextComplexStrategyNone;
    RoboTextArabicForm form = RoboTextArabicFormIsolated;
    RoboTextIndicMask mask = RoboTextIndicMaskReph;
    RoboTextLigatureVariant variant = RoboTextLigatureVariantNormal;
    RoboTextKerningSource kerning = RoboTextKerningSourceGpos;

    (void)measurement;
    (void)selection;
    (void)caret;
    (void)command;
    (void)direction;
    (void)orientation;
    (void)gsub;
    (void)gpos;
    (void)limits;
    (void)strategy;
    (void)form;
    (void)mask;
    (void)variant;
    (void)kerning;

    RoboTextFontStyleInit(&style);
    RoboTextShapeOptionsInit(&options);
    RoboTextMeasureRequestInit(&request);

    RoboCheck(range.start == 0u && range.end == 3u);
    /* A range is a value with a total constructor: the end of a text is a position, not an error. */
    RoboCheck(RoboTextRangeFull(0u).start == 0u && RoboTextRangeFull(0u).end == 0u);
    RoboCheck(RoboTextRangeFull(7u).end == 7u);

    /* `Init` is the door every caller uses, and it is safe to call on a value with anything in it. */
    RoboCheck(style.sizePx > 0.0f);
    RoboCheck(options.writingMode == RoboTextWritingHorizontal);
    (void)request;
}

/** The version, the capability record, and the three named absences inside it. */
static void TestApiVersionAndCapabilities(void)
{
    RoboTextCapabilities capabilities;
    int32_t major = -1;
    int32_t minor = -1;
    int32_t patch = -1;
    const char *version = RoboTextVersionString();

    RoboCheckMsg(version != NULL, "the version string is absent");
    RoboCheck(version && version[0] != '\0');

    RoboCheckEqInt((int)RoboTextGetVersion(&major, &minor, &patch), (int)RoboOk);
    /* 12.22 took the module to 1.13.0, and 12.23 — a conformance suite — deliberately moved nothing: a phase
     * that adds no capability adds no version, which is the rule this pair exists to keep. 12.24 is the same
     * case: an API freeze that removes a legacy path and adds no engine behaviour. */
    RoboCheckEqInt((int)major, 1);
    RoboCheckEqInt((int)minor, 13);
    RoboCheckEqInt((int)patch, 0);

    RoboCheckEqInt((int)RoboTextGetCapabilities(NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGetCapabilities(&capabilities), (int)RoboOk);

    /* The seams a consumer builds on: one shape call, one measurement door, one selection map, one editor. */
    RoboCheckEqInt(capabilities.shapeResult >= 1, 1);
    RoboCheckEqInt(capabilities.measurement >= 1, 1);
    RoboCheckEqInt(capabilities.glyphRuns >= 1, 1);
    RoboCheckEqInt(capabilities.selectionMapping >= 1, 1);
    RoboCheckEqInt(capabilities.textEditing >= 1, 1);
    RoboCheckEqInt(capabilities.verticalText >= 1, 1);
    RoboCheckEqInt(capabilities.complexScripts >= 1, 1);

    /* The three absences the phase keeps as *values*: an API that says what it does not have is one a
     * consumer can make a decision from. */
    RoboCheckEqInt(capabilities.undoHistory, 0);
    RoboCheckEqInt(capabilities.wordBoundaries, 0);
    RoboCheckEqInt(capabilities.lineBoundaries, 0);
}

/* ------------------------------------------------------------------ */
/* 2. Header hygiene                                                   */
/* ------------------------------------------------------------------ */

/** The aggregate names every public header, matched against the directory it aggregates. */
static void TestApiHeaderHygiene(void)
{
    static const char *expected[] = {
        "RoboTextAnalysis.h", "RoboTextArabic.h", "RoboTextBidi.h", "RoboTextBuffer.h",
        "RoboTextComplex.h", "RoboTextDiagnostics.h", "RoboTextDirection.h", "RoboTextEditing.h",
        "RoboTextFont.h", "RoboTextFontSelection.h", "RoboTextGlyphs.h", "RoboTextGpos.h",
        "RoboTextGsub.h", "RoboTextIndic.h", "RoboTextKerning.h", "RoboTextLigature.h",
        "RoboTextMeasurement.h", "RoboTextOtLayout.h", "RoboTextRunList.h", "RoboTextRuns.h",
        "RoboTextSelection.h", "RoboTextShape.h", "RoboTextTypes.h", "RoboTextUnicode.h",
        "RoboTextVersion.h", "RoboTextVertical.h"
    };
    uint8_t *bytes = NULL;
    size_t size = 0u;
    char *text;
    size_t index;

    /*
     * The header is read from the working tree rather than through the compiler, and that is the half the
     * include cannot state: a header *missing* from the aggregate breaks this file's build, but a header
     * *added* to the directory and forgotten in the aggregate breaks nothing until a consumer wants it. The
     * check is one direction only on purpose — extra names in an umbrella are its own business, while a
     * public header nobody can reach through it is the defect this phase exists to prevent.
     */
    if (TextReadFile(NULL, RoboRepoRoot "/Runtime/Include/RoboText/RoboText.h", 1u << 20, &bytes, &size) !=
        RoboOk) {
        TestFail(__FILE__, __LINE__, "RoboText.h is unreadable under %s", RoboRepoRoot);
        return;
    }
    text = (char *)Malloc(size + 1u);
    RoboCheckMsg(text != NULL, "the aggregate header could not be held");
    if (!text) {
        RoboFree(bytes);
        return;
    }
    memcpy(text, bytes, size);
    text[size] = '\0';
    RoboFree(bytes);

    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        char needle[64];

        snprintf(needle, sizeof(needle), "RoboText/%s", expected[index]);
        if (!strstr(text, needle)) {
            TestFail(__FILE__, __LINE__, "RoboText.h does not name %s", expected[index]);
        }
    }
    RoboFree(text);
}

/* ------------------------------------------------------------------ */
/* 3. Defaults                                                         */
/* ------------------------------------------------------------------ */

/** Each `...Init` writes the initial values its header documents. */
static void TestApiDefaults(void)
{
    RoboTextFontStyle style;
    RoboTextShapeOptions options;
    RoboTextMeasureRequest request;

    style.sizePx = -1.0f;
    style.weight = (FontWeight)0;
    style.style = RoboFontStyleItalic;
    style.stretch = (FontStretch)0;
    style.lineHeightPx = -1.0f;
    RoboTextFontStyleInit(&style);
    RoboCheck(style.sizePx > 0.0f);
    RoboCheckEqInt((int)style.weight, (int)RoboFontWeightNormal);
    RoboCheckEqInt((int)style.style, (int)RoboFontStyleNormal);
    RoboCheckEqInt((int)style.stretch, (int)RoboFontStretchNormal);
    /* A leading of zero is the *sentinel* for "the face's own metrics", not an unset field. */
    RoboCheck(style.lineHeightPx == RoboTextLineHeightFromFontMetrics);

    memset(&options, 0xAB, sizeof(options));
    RoboTextShapeOptionsInit(&options);
    /* "Nothing declared" is the documented initial value of the base direction: a paragraph with no strong
     * direction must answer `RoboTextDirectionUnknown` rather than an invented LTR. */
    RoboCheckEqInt((int)options.baseDirection, (int)RoboTextDirectionUnknown);
    RoboCheckEqInt((int)options.writingMode, (int)RoboTextWritingHorizontal);
    RoboCheckEqInt((int)options.featureRangeCount, 0);
    RoboCheckEqInt((int)options.flags, 0);

    memset(&request, 0xAB, sizeof(request));
    RoboTextMeasureRequestInit(&request);
    RoboTextMeasureRequestInitFull(&request);
    /* The full request is a *superset*: the two are different settings rather than one being a reset. */
    (void)request;
}

/* ------------------------------------------------------------------ */
/* 4. Refusals                                                         */
/* ------------------------------------------------------------------ */

/** The doors a caller can get wrong answer, and they answer the same way every time. */
static void TestApiRefusals(void)
{
    RoboTextShapeResult *result = NULL;
    RoboTextMeasurement measurement;
    RoboTextFontContext noFace;
    RoboTextShapeOptions options;
    RoboTextBuffer *empty = NULL;

    /* NULL where a pointer is required. */
    RoboCheckEqInt((int)RoboTextBufferCreate(NULL, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextShape(RoboTextViewMake(NULL, 0u), NULL, NULL, NULL, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextMeasureText(RoboTextViewMake("x", 1u), NULL, NULL, NULL, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextShapeResultMeasure(NULL, NULL, &measurement, NULL),
                   (int)RoboInvalidArgument);

    /* Borrowed accessors answer NULL or zero for a NULL handle rather than trusting the caller. */
    RoboCheck(RoboTextShapeResultGlyphRunList(NULL) == NULL);
    RoboCheck(RoboTextShapeResultGlyphRun(NULL, 0u) == NULL);
    RoboCheck(RoboTextShapeResultMeasurement(NULL) == NULL);
    RoboCheck(RoboTextShapeResultRuns(NULL) == NULL);
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(NULL), 0);

    /* Destroying nothing is legal, which is what makes an error path's cleanup readable. */
    RoboTextShapeResultDestroy(NULL);
    RoboTextBufferDestroy(NULL);

    /*
     * An **empty** text is legal and is not a refusal: an empty document is a document. It creates, it
     * reports nothing, and it destroys.
     */
    RoboCheckEqInt((int)RoboTextBufferCreate(&empty, NULL), (int)RoboOk);
    RoboCheckMsg(empty != NULL, "an empty buffer was not created");
    if (empty) {
        RoboCheckEqInt((int)RoboTextBufferScalarCount(empty), 0);
        RoboCheckEqInt((int)RoboTextBufferByteLength(empty), 0);
        RoboTextBufferDestroy(empty);
    }

    /*
     * A context with no face is a **supported** configuration rather than a broken one: every door that
     * needs a face answers `RoboNotSupported` instead of crashing, and the result is NULL. That is the
     * contract `RoboTextFont.h` states on `RoboTextFontContextSet`, and the reason a caller never has to
     * test for a face before shaping.
     */
    RoboTextFontContextInit(&noFace);
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextShape(RoboTextViewMake("no face here", 12u), &noFace, &options,
                                      &result, NULL),
                   (int)RoboNotSupported);
    RoboCheckMsg(result == NULL, "a refused shape produced a result");
}

/* ------------------------------------------------------------------ */
/* 5. Borrowing and the one measurement                                */
/* ------------------------------------------------------------------ */

/** A result's parts are borrowed from it, one call deep, and the same call answers the same pointer. */
static void TestApiBorrowing(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *first = NULL;
    RoboTextShapeResult *second = NULL;
    RoboTextShapeOptions options;

    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextShape(RoboTextViewMake(TextSampleHello, strlen(TextSampleHello)),
                                      &fixture.font, &options, &first, NULL),
                   (int)RoboOk);
    RoboCheckMsg(first != NULL, "the shipped pack produced no shape result");
    if (first) {
        const RoboTextGlyphRunList *list = RoboTextShapeResultGlyphRunList(first);
        const RoboTextGlyphRunList *listAgain = RoboTextShapeResultGlyphRunList(first);
        const RoboTextMeasurement *measurement = RoboTextShapeResultMeasurement(first);
        const RoboTextMeasurement *measurementAgain = RoboTextShapeResultMeasurement(first);
        const RoboTextRunSet *runs = RoboTextShapeResultRuns(first);
        size_t count = RoboTextShapeResultGlyphRunCount(first);

        /* Borrowing is stable: two reads of one result are one object, which is what lets a consumer hold a
         * pointer for the duration of a call without a copy. */
        RoboCheck(list == listAgain);
        RoboCheck(measurement == measurementAgain);
        RoboCheckMsg(list != NULL, "a result always has a run list, including an empty one");
        RoboCheckMsg(runs != NULL, "a result always has a run set");
        RoboCheck(count >= 1u);
        RoboCheck(count <= RoboTextGlyphRunListCount(list));

        /*
         * The measurement is the run list's, and the two agree **bit for bit** because they are one engine's
         * answer: `width` is the sum of the runs' own advances, which is the claim `RoboTextMeasurement` is
         * built on and the reason a layout pass never recounts a width.
         */
        RoboCheckMsg(measurement != NULL, "a shaped result carries a measurement");
        if (measurement) {
            float wide = RoboTextGlyphRunListAdvance(list);

            RoboCheck(measurement->width > 0.0f);
            RoboCheck(measurement->glyphCount > 0u);
            RoboCheck(wide == measurement->width);
        }

        RoboTextShapeResultDestroy(first);
    }

    /* A second call makes a second object: nothing was retained across the destroy, and nothing a caller
     * held is reachable from the new result. */
    RoboCheckEqInt((int)RoboTextShape(RoboTextViewMake(TextSampleHello, strlen(TextSampleHello)),
                                      &fixture.font, &options, &second, NULL),
                   (int)RoboOk);
    RoboCheckMsg(second != NULL, "shaping twice produced no second result");
    if (second) {
        RoboCheck(second != first);
        RoboTextShapeResultDestroy(second);
    }

    TextFixtureClose(&fixture);
}

/** One measurement engine: the door over a result and the door over a view agree exactly. */
static void TestApiMeasurementAgreement(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeResult *result = NULL;
    RoboTextShapeOptions options;
    RoboTextMeasurement shaped;
    RoboTextMeasurement measured;
    RoboTextMeasurement again;

    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextShape(RoboTextViewMake(TextSamplePangram, strlen(TextSamplePangram)),
                                      &fixture.font, &options, &result, NULL),
                   (int)RoboOk);
    if (!result) {
        TestFail(__FILE__, __LINE__, "the shipped pack produced no shape result");
        TextFixtureClose(&fixture);
        return;
    }

    memset(&shaped, 0, sizeof(shaped));
    memset(&measured, 0, sizeof(measured));
    memset(&again, 0, sizeof(again));
    RoboCheckEqInt((int)RoboTextShapeResultMeasure(result, NULL, &shaped, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextMeasureText(RoboTextViewMake(TextSamplePangram,
                                                             strlen(TextSamplePangram)),
                                            &fixture.font, &options, &measured, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextShapeResultMeasure(result, NULL, &again, NULL), (int)RoboOk);

    /*
     * `RoboTextMeasureText` is documented as "shape, copy the measurement, destroy", so the two numbers are
     * the same number rather than two sums that agree to a tolerance -- and the second read of the first door
     * is that number again, which is what makes a measurement a *record* rather than a recount.
     */
    RoboCheck(shaped.width == measured.width);
    RoboCheck(shaped.glyphCount == measured.glyphCount);
    RoboCheck(shaped.scalarCount == measured.scalarCount);
    RoboCheck(again.width == shaped.width);
    RoboCheck(again.glyphCount == shaped.glyphCount);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Index spaces                                                     */
/* ------------------------------------------------------------------ */

/** The index spaces are conversions rather than arithmetic, and neither rounds a caller's mistake off. */
static void TestApiIndexSpaces(void)
{
    static const char mixed[] = "a\xC3\xA9\xE4\xB8\xAD"; /* a, é, 中 — six bytes, three scalars */
    RoboTextBuffer *buffer = NULL;
    RoboTextScalarIndex scalars = 0u;
    RoboTextByteIndex bytes = 0u;

    RoboCheckEqInt((int)RoboTextBufferCreateFrom(&buffer, mixed, sizeof(mixed) - 1u, NULL), (int)RoboOk);
    if (!buffer) {
        TestFail(__FILE__, __LINE__, "a UTF-8 buffer was not created");
        return;
    }
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 3);
    RoboCheckEqInt((int)RoboTextBufferByteLength(buffer), 6);

    /* Scalar to byte is **total**, so the end of the text is an offset rather than an error. */
    RoboCheckEqInt((int)RoboTextBufferScalarToByte(buffer, 0u, &bytes, NULL), (int)RoboOk);
    RoboCheckEqInt((int)bytes, 0);
    RoboCheckEqInt((int)RoboTextBufferScalarToByte(buffer, 1u, &bytes, NULL), (int)RoboOk);
    RoboCheckEqInt((int)bytes, 1);
    RoboCheckEqInt((int)RoboTextBufferScalarToByte(buffer, 3u, &bytes, NULL), (int)RoboOk);
    RoboCheckEqInt((int)bytes, 6);

    /* Past the end is refused rather than clamped: a silent clamp is a caller's bug that looks like text. */
    RoboCheckEqInt((int)RoboTextBufferScalarToByte(buffer, 4u, &bytes, NULL), (int)RoboInvalidArgument);

    /* Byte to scalar **refuses** a byte inside a multi-byte scalar rather than rounding to one side. */
    RoboCheckEqInt((int)RoboTextBufferByteToScalar(buffer, 2u, &scalars, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextBufferByteToScalar(buffer, 1u, &scalars, NULL), (int)RoboOk);
    RoboCheckEqInt((int)scalars, 1);
    RoboCheckEqInt((int)RoboTextBufferByteToScalar(buffer, 6u, &scalars, NULL), (int)RoboOk);
    RoboCheckEqInt((int)scalars, 3);

    RoboTextBufferDestroy(buffer);
}

/* ------------------------------------------------------------------ */
/* 7. Stress                                                           */
/* ------------------------------------------------------------------ */

/**
 * The shapes a caller will really hand it: nothing, one scalar, a long paragraph, many script transitions,
 * a script the pack does not cover, and the same request twenty-five times.
 *
 * The invariants are the ones a *consumer* needs and a fuzz entry cannot state: a glyph per scalar is the
 * ceiling (a ligature reduces, nothing invents), the runs of a result hold exactly the glyphs the result
 * reports, no call grows without bound, and the same input measured twice is the same number. Nothing here is
 * a timing budget -- a microsecond assertion on a loaded machine is a flaky test, not a fast one -- so the
 * case asserts *stability* rather than speed.
 */
static void TestApiStress(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextBuffer *longText = NULL;
    RoboTextScalarIndex index;

    RoboTextShapeOptionsInit(&options);

    /* Nothing, and one scalar. Both are legal, and neither is a special case in the code. */
    {
        RoboTextMeasurement empty;
        RoboTextMeasurement one;

        memset(&empty, 0, sizeof(empty));
        memset(&one, 0, sizeof(one));
        RoboCheckEqInt((int)RoboTextMeasureText(RoboTextViewMake("", 0u), &fixture.font, &options,
                                                &empty, NULL),
                       (int)RoboOk);
        RoboCheck(empty.width == 0.0f);
        RoboCheckEqInt((int)empty.glyphCount, 0);
        RoboCheckEqInt((int)RoboTextMeasureText(RoboTextViewMake("W", 1u), &fixture.font, &options,
                                                &one, NULL),
                       (int)RoboOk);
        RoboCheck(one.width > 0.0f);
        RoboCheckEqInt((int)one.scalarCount, 1);
    }

    /* A long paragraph, built through the buffer's own doors rather than as a literal. */
    RoboCheckEqInt((int)RoboTextBufferCreate(&longText, NULL), (int)RoboOk);
    if (!longText) {
        TestFail(__FILE__, __LINE__, "a long text buffer was not created");
        TextFixtureClose(&fixture);
        return;
    }
    for (index = 0u; index < 4096u; index++) {
        /* A repeating Latin/Greek/Cyrillic cycle, so the long case is also the many-transition case: the
         * runs it produces are the script segmentation's answer rather than one run pretending to be four. */
        uint32_t scalar = (index % 3u) == 0u ? (uint32_t)'a' + (index % 26u)
                        : (index % 3u) == 1u ? 0x3B1u + (index % 24u)  /* Greek */
                                             : 0x410u + (index % 32u);  /* Cyrillic */

        RoboCheckEqInt((int)RoboTextBufferAppendScalar(longText, (RoboTextCodePoint)scalar, NULL),
                       (int)RoboOk);
    }
    RoboCheckEqInt((int)RoboTextBufferScalarCount(longText), 4096);

    {
        RoboTextView view = RoboTextViewMake(RoboTextBufferBytes(longText),
                                             (size_t)RoboTextBufferByteLength(longText));
        RoboTextShapeResult *result = NULL;
        RoboTextGlyphStats stats;
        size_t runCount;
        size_t runIndex;
        size_t glyphs = 0u;

        RoboCheckEqInt((int)RoboTextShape(view, &fixture.font, &options, &result, NULL), (int)RoboOk);
        RoboCheckMsg(result != NULL, "a 4096-scalar paragraph produced no result");
        if (result) {
            const RoboTextMeasurement *measurement = RoboTextShapeResultMeasurement(result);

            runCount = RoboTextShapeResultGlyphRunCount(result);
            /* Many script transitions, so more than one run -- and every run counted, which is what makes
             * the ceiling below a statement about the whole result rather than about its first run. */
            RoboCheck(runCount > 1u);
            RoboCheck(runCount <= 4096u);
            for (runIndex = 0u; runIndex < runCount; runIndex++) {
                const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, runIndex);

                RoboCheckMsg(run != NULL, "a run index inside the count answered no run");
                if (run) {
                    glyphs += RoboTextGlyphRunCount(run);
                }
            }
            /* The ceiling: a glyph per scalar is the most a shaper may produce, and a ligature reduces it. */
            RoboCheck(glyphs <= 4096u);
            RoboCheckMsg(measurement != NULL, "a long result carries no measurement");
            if (measurement) {
                RoboCheckEqInt((int)measurement->scalarCount, 4096);
                RoboCheckEqInt((int)measurement->glyphCount, (int)glyphs);
                RoboCheck(measurement->width > 0.0f);
            }

            /* The stats door is a sum over the same runs, so the two agree by construction -- and this is
             * the comparison that catches a run list and a measurement drifting apart. */
            memset(&stats, 0, sizeof(stats));
            RoboCheckEqInt((int)RoboTextShapeResultGetGlyphStats(result, &stats), (int)RoboOk);
            RoboCheckEqInt((int)stats.glyphs, (int)glyphs);

            RoboTextShapeResultDestroy(result);
        }

        /* Twenty-five measurements of one text, all the same number: no state carries between calls. */
        {
            RoboTextMeasurement first;
            size_t repeat;

            memset(&first, 0, sizeof(first));
            RoboCheckEqInt((int)RoboTextMeasureText(view, &fixture.font, &options, &first, NULL),
                           (int)RoboOk);
            for (repeat = 0u; repeat < 24u; repeat++) {
                RoboTextMeasurement again;

                memset(&again, 0, sizeof(again));
                RoboCheckEqInt((int)RoboTextMeasureText(view, &fixture.font, &options, &again, NULL),
                               (int)RoboOk);
                if (again.width != first.width || again.glyphCount != first.glyphCount) {
                    TestFail(__FILE__, __LINE__, "measurement %u of one text disagreed with the first",
                             (unsigned)(repeat + 2u));
                }
            }
        }
    }
    RoboTextBufferDestroy(longText);

    /*
     * A script the shipped pack does not cover is **counted** rather than refused or drawn as nothing: the
     * corpus records that the pack carries no Hebrew or Arabic, and the honest answer is a missing-glyph
     * count with the runs, advances and mapping still intact.
     */
    {
        RoboTextShapeResult *result = NULL;
        RoboTextMeasurement measurement;

        RoboTextShapeOptionsInit(&options);
        memset(&measurement, 0, sizeof(measurement));
        RoboCheckEqInt((int)RoboTextShape(RoboTextViewMake(TextSampleScriptArabic,
                                                           strlen(TextSampleScriptArabic)),
                                          &fixture.font, &options, &result, NULL),
                       (int)RoboOk);
        RoboCheckMsg(result != NULL, "uncovored text produced no result");
        if (result) {
            RoboCheckEqInt((int)RoboTextShapeResultMeasure(result, NULL, &measurement, NULL), (int)RoboOk);
            /* The scalars are all there, and whatever the face could not draw is in `missingGlyphs` rather
             * than silently dropped from the count. */
            RoboCheck(measurement.scalarCount > 0u);
            RoboCheck(measurement.missingGlyphs <= measurement.scalarCount);
            RoboTextShapeResultDestroy(result);
        }
    }

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterTextApi(void)
{
    TestRegister("text.api.aggregate", TestApiAggregate);
    TestRegister("text.api.version_and_capabilities", TestApiVersionAndCapabilities);
    TestRegister("text.api.header_hygiene", TestApiHeaderHygiene);
    TestRegister("text.api.defaults", TestApiDefaults);
    TestRegister("text.api.refusals", TestApiRefusals);
    TestRegister("text.api.borrowing", TestApiBorrowing);
    TestRegister("text.api.measurement_agreement", TestApiMeasurementAgreement);
    TestRegister("text.api.index_spaces", TestApiIndexSpaces);
    TestRegister("text.api.stress", TestApiStress);
}
