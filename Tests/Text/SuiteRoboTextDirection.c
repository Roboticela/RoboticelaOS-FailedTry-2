/*
 * The RoboText direction stage (Phase 12.7) — `text.direction.*`.
 *
 * The phase's whole claim is that direction is *classified here and resolved in 12.8*, so every case below
 * is one of three things: the classification itself (a scalar's `Bidi_Class` projected to a direction class),
 * the paragraph direction (a context's declared mode, or P2/P3's first strong character), or the run set the
 * two produce — and the run cases are written as **boundary lists** (`"ltr 0..6 | rtl 6..11"`) rather than as
 * per-run assertions, because a boundary that moved by one scalar is exactly the defect this stage can have
 * and a run-by-run assertion hides it inside a passing count.
 *
 * Three of the phase's rules are pinned where they cannot be read as accidents:
 *
 *   **a number is not `EN`-means-left-to-right.** `text.direction.classes` asserts the class of a digit is
 *   `Number` and not `Ltr`, and `text.direction.numbers` asserts the direction a number *resolves* to comes
 *   from the paragraph (`LTR` in a known base, `Unknown` without one) — the two facts the phase's §10 and
 *   §16 ask for. `text.direction.numbers` also pins the second half, which is easy to miss: a number lends
 *   `RTL` to the neutral block beside it, so the space in `"ABC אבג 123 DEF"` joins the Hebrew.
 *
 *   **a base does not colour the whole paragraph.** `"123 456"` with a declared RTL base is one left-to-right
 *   run, and the same text with nothing declared is `Unknown` — the honest answer, not an LTR claim.
 *
 *   **a mark keeps its base's context.** A case puts a Latin base and a mark *before* Hebrew text in an RTL
 *   paragraph, so the mark's answer can only come from W1 (the character before it) and not from the block
 *   rules, which would give it the paragraph's direction.
 *
 * Contract: `Documentation/Runtime/Text/DirectionRuns.md`.
 */

#include "RoboTextTestSupport.h"

#include "RoboMem/RoboMemDebug.h"

#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */

/** Room for the widest run set any case below asks for. */
#define DirectionRunMax 64u

/** A resolved run set, a paragraph direction and the status, as one value. */
typedef struct directionResult {
    RoboTextDirectionRun runs[DirectionRunMax];
    size_t count;
    RoboTextDirection base;
    Result status;
} DirectionResult;

static RoboTextView DirectionView(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

/** Resolve @p text in @p context. */
static DirectionResult Resolve(const char *text, const RoboTextDirectionContext *context)
{
    DirectionResult result;

    memset(&result, 0, sizeof(result));
    result.status = RoboTextDirectionRunsResolveView(DirectionView(text), context, NULL, result.runs,
                                                     DirectionRunMax, &result.count, &result.base, NULL);
    return result;
}

/** `"ltr 0..6 | rtl 6..11"` — the runs, in the form a boundary defect is readable in. */
static const char *RunsText(const DirectionResult *result)
{
    static char buffer[512];
    size_t length = 0u;

    buffer[0] = '\0';
    for (size_t index = 0u; index < result->count; index++) {
        int written = snprintf(buffer + length, sizeof(buffer) - length, "%s%s %u..%u",
                               (index == 0u) ? "" : " | ",
                               RoboTextDirectionName(result->runs[index].direction),
                               (unsigned)result->runs[index].range.start,
                               (unsigned)result->runs[index].range.end);

        if (written < 0 || (size_t)written >= sizeof(buffer) - length) {
            return "<overflow>";
        }
        length += (size_t)written;
    }
    return buffer;
}

/** @p text in @p context resolves to exactly @p expected, and the whole text is covered. */
static void ExpectRuns(const char *text, const RoboTextDirectionContext *context, const char *expected)
{
    DirectionResult result = Resolve(text, context);
    char message[640];

    RoboCheckMsg(result.status == RoboOk, "the resolver refused a case it should have answered");
    snprintf(message, sizeof(message), "\"%s\" resolved to \"%s\", expected \"%s\"", text,
             RunsText(&result), expected);
    RoboCheckMsg(strcmp(RunsText(&result), expected) == 0, message);
}

/** The context a layout declares: first strong character, and left-to-right when there is none. */
static RoboTextDirectionContext LayoutContext(void)
{
    RoboTextDirectionContext context;

    RoboTextDirectionContextInit(&context);
    context.mode = RoboTextDirectionModeAuto;
    context.fallbackDirection = RoboTextDirectionLtr;
    return context;
}

/** The context for a caller that declared @p direction. */
static RoboTextDirectionContext Declared(RoboTextDirection direction)
{
    return RoboTextDirectionContextForBase(direction);
}

/** A selector over @p primary, so the integration case drives the real font policy. */
static RoboTextFontSelector *DirectionSelectorOver(const RoboTextFontContext *primary)
{
    RoboTextFontSelector *selector = NULL;

    RoboCheckEqInt((int)RoboTextFontSelectorCreate(&selector, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFontSelectorSetPrimary(selector, primary, NULL), (int)RoboOk);
    return selector;
}

/** The default context: first strong character, and nothing when there is none. */
static RoboTextDirectionContext Undeclared(void)
{
    RoboTextDirectionContext context;

    RoboTextDirectionContextInit(&context);
    return context;
}

/**
 * The invariants every run set must satisfy, for any input.
 *
 * The phase's §13, and checking them here rather than in one case is the point: they are properties of the
 * *resolver*, and the sweep below runs them over every string in the matrix.
 */
static void CheckInvariants(const char *text, const RoboTextDirectionContext *context)
{
    DirectionResult result = Resolve(text, context);
    RoboTextView view = DirectionView(text);
    RoboTextScalarIndex scalars = 0u;
    char message[512];

    RoboCheckMsg(result.status == RoboOk, "the resolver refused a case it should have answered");
    RoboCheckMsg(RoboTextViewScalarCount(view, &scalars, NULL) == RoboOk, "a view has no scalar count");
    snprintf(message, sizeof(message), "\"%s\" broke a run invariant", text);

    if (scalars == 0u) {
        RoboCheckMsg(result.count == 0u, message);
        return;
    }
    RoboCheckMsg(result.count != 0u, message);
    RoboCheckMsg(result.runs[0].range.start == 0u, message);
    RoboCheckMsg(result.runs[result.count - 1u].range.end == scalars, message);
    for (size_t index = 0u; index < result.count; index++) {
        RoboCheckMsg(!RoboTextRangeIsEmpty(result.runs[index].range), message);
        RoboCheckMsg(!RoboTextRangeIsInverted(result.runs[index].range), message);
        if (index != 0u) {
            /* Contiguous, and no two neighbours carry the same direction — coalescing is by construction. */
            RoboCheckMsg(result.runs[index - 1u].range.end == result.runs[index].range.start, message);
            RoboCheckMsg(result.runs[index - 1u].direction != result.runs[index].direction, message);
        }
    }
}

/** Every string the matrix below is built from, for the invariant sweep. */
static const char *const kDirectionSamples[] = {
    "",
    " ",
    "Hello",
    "123",
    "Hello, world!",
    TextSampleDirectionMixedLtr,
    TextSampleDirectionMixedRtl,
    TextSampleDirectionHebrew,
    TextSampleDirectionPunctuation,
    TextSampleDirectionNumbers,
    TextSampleDirectionNumbersFirst,
    TextSampleDirectionLevels,
    TextSampleDirectionArabicSentence,
    TextSampleDirectionHebrewSentence,
    TextSampleDirectionArabicDigits,
    TextSampleDirectionMarkLatin,
    TextSampleDirectionMarkArabic,
    TextSampleScriptGreek,
    TextSampleScriptDevanagari,
    TextSampleScriptEmoji,
    TextSampleUnicode,
    TextSampleNoncharacter,
};

#define DirectionSampleCount (sizeof(kDirectionSamples) / sizeof(kDirectionSamples[0]))

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

static void TestDirectionClasses(void)
{
    RoboTextDirectionClass directionClass;

    /* Strong: two classes, one direction each. */
    RoboCheckEqInt((int)RoboTextDirectionClassOf('A'), (int)RoboTextDirectionClassLeftToRight);
    RoboCheckEqInt((int)RoboTextDirectionClassOf('z'), (int)RoboTextDirectionClassLeftToRight);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x05E9u), (int)RoboTextDirectionClassRightToLeft);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x0645u), (int)RoboTextDirectionClassRightToLeft);
    /* The two marks that *are* strong, which is how an author steers a paragraph with a zero-width scalar. */
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x200Fu), (int)RoboTextDirectionClassRightToLeft);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x200Eu), (int)RoboTextDirectionClassLeftToRight);

    /*
     * Numbers, and the phase's §10 in one assertion: a digit's class is `Number`, **not** left-to-right.
     * `EN` (ASCII, and the extended Arabic-Indic digits) and `AN` (Arabic-Indic) land on the same class
     * because they do the same thing to a run; they differ in what they do to the *ordering*, which is 12.8's.
     */
    RoboCheckEqInt((int)RoboTextDirectionClassOf('5'), (int)RoboTextDirectionClassNumber);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x0661u), (int)RoboTextDirectionClassNumber);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x06F1u), (int)RoboTextDirectionClassNumber);
    /* …and the separator/terminator classes share it, because they only ever mean "part of a number". */
    RoboCheckEqInt((int)RoboTextDirectionClassOf(','), (int)RoboTextDirectionClassNumber);
    RoboCheckEqInt((int)RoboTextDirectionClassOf('.'), (int)RoboTextDirectionClassNumber);
    RoboCheckEqInt((int)RoboTextDirectionClassOf('+'), (int)RoboTextDirectionClassNumber);
    RoboCheckEqInt((int)RoboTextDirectionClassOf('%'), (int)RoboTextDirectionClassNumber);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x00A0u), (int)RoboTextDirectionClassNumber);

    /* Marks attach; with nothing to attach to they behave as neutrals, which `text.direction.marks` checks. */
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x0301u), (int)RoboTextDirectionClassAttachLeft);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x0651u), (int)RoboTextDirectionClassAttachLeft);

    /* Neutrals: whitespace, punctuation, symbols, emoji, the boundary neutrals, and the explicit controls. */
    RoboCheckEqInt((int)RoboTextDirectionClassOf(' '), (int)RoboTextDirectionClassNeutral);
    RoboCheckEqInt((int)RoboTextDirectionClassOf('\t'), (int)RoboTextDirectionClassNeutral);
    RoboCheckEqInt((int)RoboTextDirectionClassOf('\r'), (int)RoboTextDirectionClassNeutral);
    RoboCheckEqInt((int)RoboTextDirectionClassOf('!'), (int)RoboTextDirectionClassNeutral);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x00A9u), (int)RoboTextDirectionClassNeutral);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x2192u), (int)RoboTextDirectionClassNeutral);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x1F600u), (int)RoboTextDirectionClassNeutral);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x200Du), (int)RoboTextDirectionClassNeutral);
    /* The embeddings are neutral here **on purpose** — their contents are classified by their own classes,
     * and honouring the embedding is 12.8's, which is what makes `capabilities.bidiAlgorithm` a 0. */
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x202Bu), (int)RoboTextDirectionClassNeutral);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x202Cu), (int)RoboTextDirectionClassNeutral);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x2066u), (int)RoboTextDirectionClassNeutral);
    RoboCheckEqInt((int)RoboTextDirectionClassOf(0x2069u), (int)RoboTextDirectionClassNeutral);

    /* Names and the known test. */
    RoboCheckEqInt((int)RoboTextDirectionClassCount, 5);
    for (int value = 0; value < (int)RoboTextDirectionClassCount; value++) {
        RoboCheck(RoboTextDirectionClassIsKnown((RoboTextDirectionClass)value));
        RoboCheck(RoboTextDirectionClassName((RoboTextDirectionClass)value)[0] != '\0');
    }
    RoboCheck(!RoboTextDirectionClassIsKnown((RoboTextDirectionClass)5));
    RoboCheckStrEq(RoboTextDirectionClassName(RoboTextDirectionClassNumber), "number");
    RoboCheckStrEq(RoboTextDirectionClassName(RoboTextDirectionClassAttachLeft), "attach-left");
    RoboCheckStrEq(RoboTextDirectionClassName(RoboTextDirectionClassNeutral), "neutral");
    RoboCheckStrEq(RoboTextDirectionClassName((RoboTextDirectionClass)9), "neutral");

    /* The paragraph vocabulary: three modes, distinct names, and the initial context claims nothing. */
    RoboCheckEqInt((int)RoboTextDirectionModeCount, 3);
    RoboCheck(RoboTextDirectionModeIsKnown(RoboTextDirectionModeAuto));
    RoboCheck(RoboTextDirectionModeIsKnown(RoboTextDirectionModeExplicitLtr));
    RoboCheck(RoboTextDirectionModeIsKnown(RoboTextDirectionModeExplicitRtl));
    RoboCheck(!RoboTextDirectionModeIsKnown((RoboTextDirectionMode)3));
    RoboCheckStrEq(RoboTextDirectionModeName(RoboTextDirectionModeAuto), "auto");
    RoboCheckStrEq(RoboTextDirectionModeName(RoboTextDirectionModeExplicitLtr), "ltr");
    RoboCheckStrEq(RoboTextDirectionModeName(RoboTextDirectionModeExplicitRtl), "rtl");
    RoboCheckStrEq(RoboTextDirectionModeName((RoboTextDirectionMode)7), "auto");

    {
        RoboTextDirectionContext context;

        RoboTextDirectionContextInit(&context);
        RoboCheckEqInt((int)context.mode, (int)RoboTextDirectionModeAuto);
        /* `Unknown`, not `Ltr`: this stage has not run P3, so a text with no strong character is unresolved
         * until the caller says what its default is. That is the whole difference between `Undeclared()` and
         * `LayoutContext()` below. */
        RoboCheckEqInt((int)context.fallbackDirection, (int)RoboTextDirectionUnknown);
        RoboCheck(RoboTextDirectionContextIsValid(&context));

        RoboCheckEqInt((int)RoboTextDirectionContextForBase(RoboTextDirectionLtr).mode,
                       (int)RoboTextDirectionModeExplicitLtr);
        RoboCheckEqInt((int)RoboTextDirectionContextForBase(RoboTextDirectionRtl).mode,
                       (int)RoboTextDirectionModeExplicitRtl);
        RoboCheckEqInt((int)RoboTextDirectionContextForBase(RoboTextDirectionUnknown).mode,
                       (int)RoboTextDirectionModeAuto);
        RoboCheckEqInt((int)RoboTextDirectionContextForBase(RoboTextDirectionRtl).fallbackDirection,
                       (int)RoboTextDirectionRtl);
        RoboCheckEqInt((int)RoboTextDirectionContextExplicit(RoboTextDirectionRtl).mode,
                       (int)RoboTextDirectionModeExplicitRtl);
        RoboCheckEqInt((int)RoboTextDirectionContextExplicit(RoboTextDirectionUnknown).mode,
                       (int)RoboTextDirectionModeAuto);
        RoboCheck(RoboTextDirectionContextEqual(&context, &context));
        RoboCheck(RoboTextDirectionContextEqual(NULL, NULL));
        RoboCheck(!RoboTextDirectionContextEqual(&context, NULL));
        {
            RoboTextDirectionContext other = context;

            other.mode = RoboTextDirectionModeExplicitRtl;
            RoboCheck(!RoboTextDirectionContextEqual(&context, &other));
            other = context;
            other.fallbackDirection = RoboTextDirectionRtl;
            RoboCheck(!RoboTextDirectionContextEqual(&context, &other));
        }

        /* Validation refuses a mode or a fallback that is not one, and NULL is not valid. */
        context.mode = (RoboTextDirectionMode)9;
        RoboCheck(!RoboTextDirectionContextIsValid(&context));
        context.mode = RoboTextDirectionModeAuto;
        context.fallbackDirection = (RoboTextDirection)9;
        RoboCheck(!RoboTextDirectionContextIsValid(&context));
        RoboCheck(!RoboTextDirectionContextIsValid(NULL));
        /* A caller that hands the resolver no context gets the default one, which is a legal call. */
        {
            DirectionResult result = Resolve("Hello", NULL);

            RoboCheckEqInt((int)result.status, (int)RoboOk);
            RoboCheckStrEq(RunsText(&result), "ltr 0..5");
        }
        {
            DirectionResult result = Resolve("Hello", &context);

            RoboCheckEqInt((int)result.status, (int)RoboInvalidArgument);
        }
        directionClass = RoboTextDirectionClassOf('A');
        RoboCheck(RoboTextDirectionClassIsKnown(directionClass));
    }
}

static void TestDirectionAsciiFastPath(void)
{
    RoboTextDirectionClass fast;
    RoboTextCodePoint codePoint;

    /* The fast path answers exactly the ASCII block and declines everything else. */
    for (codePoint = 0u; codePoint <= 0x7Fu; codePoint++) {
        RoboCheck(RoboTextDirectionAsciiClassOf(codePoint, &fast));
    }
    RoboCheck(!RoboTextDirectionAsciiClassOf(0x80u, &fast));
    RoboCheck(!RoboTextDirectionAsciiClassOf(0x0301u, &fast));
    RoboCheck(!RoboTextDirectionAsciiClassOf(0xD800u, &fast));
    RoboCheck(!RoboTextDirectionAsciiClassOf(0x10FFFFu, &fast));

    /*
     * And it is **equivalent** to the tables over the whole block: for every ASCII scalar the shortcut's
     * answer is the one the `Bidi_Class` lookup gives. This is what makes the shortcut a shortcut rather than
     * a guess — a UCD that moved an ASCII scalar's class fails this loop rather than the resolver silently
     * disagreeing with the data it is supposed to be reading.
     */
    for (codePoint = 0u; codePoint <= 0x7Fu; codePoint++) {
        RoboCheck(RoboTextDirectionAsciiClassOf(codePoint, &fast));
        RoboCheckEqInt((int)fast, (int)RoboTextDirectionClassOf(codePoint));
    }

    /* A NULL out pointer is the "is it ASCII" question, and only that. */
    RoboCheck(RoboTextDirectionAsciiClassOf('A', NULL));
    RoboCheck(!RoboTextDirectionAsciiClassOf(0x05E9u, NULL));
}

/* ------------------------------------------------------------------ */
/* The paragraph direction                                             */
/* ------------------------------------------------------------------ */

static void TestDirectionParagraph(void)
{
    Uchar32 scalars[8];
    RoboTextDirectionContext context;

    /* Nothing at all: no text, no context. */
    RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(NULL, 0u, NULL), (int)RoboTextDirectionUnknown);
    /* Scalars are required when a count is given, exactly as the run door requires them. */
    RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(NULL, 4u, NULL), (int)RoboTextDirectionUnknown);

    scalars[0] = (Uchar32)'H';
    scalars[1] = (Uchar32)'i';
    RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(scalars, 2u, NULL), (int)RoboTextDirectionLtr);

    scalars[0] = (Uchar32)0x0645u;
    scalars[1] = (Uchar32)0x0645u;
    RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(scalars, 2u, NULL), (int)RoboTextDirectionRtl);

    /* A first strong scalar decides for the whole paragraph, whichever script it is in. */
    scalars[0] = (Uchar32)0x0645u;
    scalars[1] = (Uchar32)'A';
    RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(scalars, 2u, NULL), (int)RoboTextDirectionRtl);

    /* Digits are not strong, so they decide nothing: the fallback does, and the default fallback says so. */
    scalars[0] = (Uchar32)'1';
    scalars[1] = (Uchar32)'2';
    RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(scalars, 2u, NULL), (int)RoboTextDirectionUnknown);
    {
        RoboTextDirectionContext autoLtr = LayoutContext();
        RoboTextDirectionContext autoRtl = Undeclared();

        autoRtl.fallbackDirection = RoboTextDirectionRtl;
        RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(scalars, 2u, &autoLtr),
                       (int)RoboTextDirectionLtr);
        RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(scalars, 2u, &autoRtl),
                       (int)RoboTextDirectionRtl);
    }

    /* An explicit mode wins over the text, and it is not decoration: it is what a document's `dir` means. */
    context = Declared(RoboTextDirectionRtl);
    scalars[0] = (Uchar32)'A';
    scalars[1] = (Uchar32)'B';
    RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(scalars, 2u, &context),
                   (int)RoboTextDirectionRtl);
    context = Declared(RoboTextDirectionLtr);
    scalars[0] = (Uchar32)0x0645u;
    scalars[1] = (Uchar32)0x0645u;
    RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(scalars, 2u, &context),
                   (int)RoboTextDirectionLtr);

    /* The resolved paragraph is a door of its own, and the run door reports the same one. */
    {
        DirectionResult result = Resolve(TextSampleDirectionMixedLtr, NULL);

        RoboCheckEqInt((int)result.status, (int)RoboOk);
        RoboCheckEqInt((int)result.base, (int)RoboTextDirectionLtr);
    }
    {
        DirectionResult result = Resolve(TextSampleDirectionMixedRtl, NULL);

        RoboCheckEqInt((int)result.base, (int)RoboTextDirectionRtl);
    }
    {
        /* `LayoutContext` is what a desktop surface passes: CSS's initial `direction: ltr`. */
        RoboTextDirectionContext layout = LayoutContext();
        DirectionResult result = Resolve("123", &layout);

        RoboCheckEqInt((int)result.base, (int)RoboTextDirectionLtr);
        RoboCheckStrEq(RunsText(&result), "ltr 0..3");
    }
}

/* ------------------------------------------------------------------ */
/* Runs                                                                */
/* ------------------------------------------------------------------ */

static void TestDirectionPure(void)
{
    RoboTextDirectionContext layout = LayoutContext();
    RoboTextDirectionContext undeclared = Undeclared();
    RoboTextDirectionContext rtl = Declared(RoboTextDirectionRtl);
    DirectionResult result;

    /* Pure Latin: one run, whatever the length, because letters are strong and neutrals join them. */
    ExpectRuns("Hello", &layout, "ltr 0..5");
    ExpectRuns("RoboticelaOS", &layout, "ltr 0..12");
    ExpectRuns("Hello, world!", &layout, "ltr 0..13");
    ExpectRuns("123", &layout, "ltr 0..3");

    /* Pure RTL: Arabic (`AL`) and Hebrew (`R`), one run each, and neither needs a special case. */
    ExpectRuns(TextSampleDirectionArabicSentence, &layout, "rtl 0..13");
    ExpectRuns(TextSampleDirectionHebrewSentence, &layout, "rtl 0..9");
    ExpectRuns(TextSampleDirectionArabicSentence, &undeclared, "rtl 0..13");

    /*
     * Arabic-Indic digits are `AN` and are **not** RTL text even in an RTL paragraph: their level is even in
     * either base (UAX #9 I1/I2), and an even level is left-to-right. With no paragraph direction at all
     * there is no level to be even, so the honest answer is `Unknown` — which is also UAX #9's own answer for
     * a paragraph with no strong character (P3), reached here without claiming a resolved LTR.
     */
    ExpectRuns(TextSampleDirectionArabicDigits, &rtl, "ltr 0..3");
    ExpectRuns(TextSampleDirectionArabicDigits, &undeclared, "unknown 0..3");

    /* Empty text is a fact about a document: no runs at all, and the paragraph is whatever was declared. */
    result = Resolve("", &layout);
    RoboCheckEqInt((int)result.status, (int)RoboOk);
    RoboCheckEqInt((int)result.count, 0);
    RoboCheckEqInt((int)result.base, (int)RoboTextDirectionLtr);
    result = Resolve("", &undeclared);
    RoboCheckEqInt((int)result.count, 0);
    RoboCheckEqInt((int)result.base, (int)RoboTextDirectionUnknown);

    /* A space is not a run of its own in either direction; it follows the context it is in. */
    ExpectRuns("   ", &layout, "ltr 0..3");
    ExpectRuns("   ", &undeclared, "unknown 0..3");
    {
        RoboTextDirectionContext rtlBase = Declared(RoboTextDirectionRtl);

        ExpectRuns("   ", &rtlBase, "rtl 0..3");
    }

    /* Greek, Cyrillic and Devanagari are `L`: the phase's script list is not a direction list. */
    ExpectRuns(TextSampleScriptGreek, &layout, "ltr 0..4");
    ExpectRuns("Latin " "\xD0\x9A\xD0\xB8\xD1\x80\xD0\xB8\xD0\xBB\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB0",
               &layout, "ltr 0..15");
    ExpectRuns(TextSampleScriptDevanagari, &layout, "ltr 0..6");

    /* One run covering the whole text is the assertion the phase asks for by name. */
    result = Resolve("Hello", &layout);
    RoboCheckEqInt((int)result.count, 1);
    RoboCheckEqInt((int)result.runs[0].range.start, 0);
    RoboCheckEqInt((int)result.runs[0].range.end, 5);
    RoboCheckEqInt((int)result.runs[0].direction, (int)RoboTextDirectionLtr);
}

static void TestDirectionMixed(void)
{
    RoboTextDirectionContext layout = LayoutContext();

    /*
     * The phase's §1 example. The space after `Hello` is left-to-right — the two sides differ (L, then R) and
     * the paragraph is LTR — which is why the Latin run is six scalars and not five.
     */
    ExpectRuns(TextSampleDirectionMixedLtr, &layout, "ltr 0..6 | rtl 6..11");
    /*
     * The mirror image, and the interesting one: the space now joins the **RTL** run, because the paragraph's
     * first strong character is Arabic. Logical order is untouched; only the neutral's side changed.
     */
    ExpectRuns(TextSampleDirectionMixedRtl, &layout, "rtl 0..6 | ltr 6..11");
    /* Three runs, and the two Latin stretches stay apart — they are not one Latin context across the Hebrew. */
    ExpectRuns(TextSampleDirectionHebrew, &layout, "ltr 0..6 | rtl 6..10 | ltr 10..16");
    /* Latin, Arabic, Devanagari: two boundaries, three runs, and the trailing space goes with the Latin. */
    ExpectRuns("English " "\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9"
               " " "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA6\xE0\xA5\x80",
               &layout, "ltr 0..8 | rtl 8..15 | ltr 15..22");
    /* All-`L` scripts in one paragraph are one run: mixed scripts are not mixed direction. */
    ExpectRuns("Latin " "\xD0\x9A\xD0\xB8\xD1\x80\xD0\xB8\xD0\xBB\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB0"
               " \xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC", &layout, "ltr 0..24");
    /* The declared direction of the *paragraph* does not colour the strong scalars: RTL base, Latin text. */
    {
        RoboTextDirectionContext rtlBase = Declared(RoboTextDirectionRtl);

        ExpectRuns("Hello", &rtlBase, "ltr 0..5");
    }
}

static void TestDirectionNeutrals(void)
{
    RoboTextDirectionContext layout = LayoutContext();
    RoboTextDirectionContext undeclared = Undeclared();
    RoboTextDirectionContext rtl = Declared(RoboTextDirectionRtl);

    /* Punctuation inside one direction never splits it, whichever end of the text it is at. */
    ExpectRuns("Hello, world!", &layout, "ltr 0..13");
    ExpectRuns("Hello, world!", &undeclared, "ltr 0..13");
    /* Leading and trailing neutrals take the context they are in, not the paragraph's *declared* direction. */
    ExpectRuns("  Hello  ", &layout, "ltr 0..9");
    ExpectRuns("  Hello  ", &undeclared, "ltr 0..9");

    /* The phase's §26 line: LTR, RTL, LTR, with the underlying UTF-8 untouched. */
    ExpectRuns(TextSampleDirectionPunctuation, &layout, "ltr 0..7 | rtl 7..11 | ltr 11..19");

    /* Arabic punctuation in an RTL paragraph stays RTL, and so does the ASCII `!` after it. */
    ExpectRuns(TextSampleDirectionArabicSentence "!", &layout, "rtl 0..14");
    ExpectRuns("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7" ", " "\xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84\xD9\x85" "!",
               &layout, "rtl 0..15");

    /* A neutral with no decided side anywhere is the paragraph direction, which may be nothing at all. */
    ExpectRuns("1234", &undeclared, "unknown 0..4");
    ExpectRuns("1234", &layout, "ltr 0..4");
    ExpectRuns("...", &undeclared, "unknown 0..3");
    ExpectRuns("...", &rtl, "rtl 0..3");

    /*
     * Symbols and emoji are `ON`, so they are neutrals: they join the run they sit in rather than making a
     * run of their own. The phase's §13 asks for exactly this — no invented `Emoji` direction.
     */
    ExpectRuns("a \xF0\x9F\x98\x80 b", &layout, "ltr 0..5");
    /* The same shape in Hebrew: the arrows and spaces join the RTL context the text is in. */
    ExpectRuns("\xD7\x90 \xE2\x86\x92 \xD7\x91", &layout, "rtl 0..5");
}

static void TestDirectionNumbers(void)
{
    RoboTextDirectionContext layout = LayoutContext();
    RoboTextDirectionContext undeclared = Undeclared();
    RoboTextDirectionContext rtl = Declared(RoboTextDirectionRtl);

    /*
     * An RTL paragraph with a number in it: the number is its own left-to-right run, and the space before it
     * joins the Arabic — because a number lends `RTL` to the neutral beside it (N1: numbers act as if they
     * were `R`). The phase's §16 is this case: an RTL paragraph is not a paragraph in which everything is RTL.
     */
    ExpectRuns(TextSampleDirectionNumbers, &layout, "rtl 0..6 | ltr 6..9");
    ExpectRuns(TextSampleDirectionNumbersFirst, &layout, "ltr 0..3 | rtl 3..9");
    /* And the phase's §29 diagnostic example, whose four level runs are three direction runs. */
    ExpectRuns(TextSampleDirectionLevels, &layout, "ltr 0..4 | rtl 4..8 | ltr 8..15");
    /* A number with a Latin context on both sides is not a boundary at all — in either direction. */
    ExpectRuns("Hello 123", &layout, "ltr 0..9");
    ExpectRuns("2026/09/22", &layout, "ltr 0..10");
    ExpectRuns("Version 2.0", &layout, "ltr 0..11");
    /* Separators and terminators inside a number are part of it: `1,234.5` is one run, not five. */
    ExpectRuns("1,234.5", &layout, "ltr 0..7");
    ExpectRuns("$1,234.50", &layout, "ltr 0..9");
    /* A lone separator is punctuation, not a number: the block has no digit in it, so it is a neutral. */
    ExpectRuns("Hello, world!", &layout, "ltr 0..13");
    ExpectRuns("a,b", &layout, "ltr 0..3");
    /* A number inside parentheses in an RTL paragraph (N1's `sor`/`eor` are the paragraph direction). */
    ExpectRuns("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7" " (123)", &layout,
               "rtl 0..7 | ltr 7..10 | rtl 10..11");
    /*
     * A declared RTL base makes digits left-to-right — but not the space *between* them: N1 gives a neutral
     * between two numbers the RTL that numbers lend, so the two numbers stay two runs and the space is a
     * right-to-left run of its own. That is the same answer the algorithm's levels give (even, odd, even),
     * and it is the case that shows why a number's own direction and its influence are two facts.
     */
    ExpectRuns("123 456", &rtl, "ltr 0..3 | rtl 3..4 | ltr 4..7");
    ExpectRuns("123 456", &layout, "ltr 0..7");
    ExpectRuns("123 456", &undeclared, "unknown 0..7");
}

static void TestDirectionMarks(void)
{
    RoboTextDirectionContext layout = LayoutContext();
    RoboTextDirectionContext undeclared = Undeclared();

    /* A mark takes the direction of the character before it, so it never begins a run of its own. */
    ExpectRuns(TextSampleDirectionMarkLatin, &layout, "ltr 0..2");
    ExpectRuns(TextSampleDirectionMarkArabic, &layout, "rtl 0..2");
    ExpectRuns("e\xCC\x81" "f", &layout, "ltr 0..3");
    /* A leading mark has nothing to attach to, which W1 answers as a neutral — and the paragraph settles it. */
    ExpectRuns("\xCC\x81" "abc", &layout, "ltr 0..4");
    ExpectRuns("\xCC\x81" "abc", &undeclared, "ltr 0..4");

    /*
     * The case that separates W1 from the block rule. Hebrew, then Latin with a mark on it, then Hebrew —
     * in an RTL paragraph, so a mark answered by the *block* rules (which see RTL on one side, LTR on the
     * other, and fall back to the paragraph) would be RTL and split the Latin run. W1 answers LTR, because
     * the character before the mark is a Latin letter, and that is what a `mark` attachment needs.
     */
    ExpectRuns("\xD7\x90\xD7\x91\xD7\x92" " abc\xCC\x81 " "\xD7\x93\xD7\x94\xD7\x95", &layout,
               "rtl 0..4 | ltr 4..8 | rtl 8..12");
    /* The same text without the mark, for the contrast: the Latin run is one scalar shorter. */
    ExpectRuns("\xD7\x90\xD7\x91\xD7\x92" " abc " "\xD7\x93\xD7\x94\xD7\x95", &layout,
               "rtl 0..4 | ltr 4..7 | rtl 7..11");

    /* Marks in Unicode's own combining sequences: Greek with an accent keeps the Greek's direction. */
    ExpectRuns("\xCE\xB1\xCC\x81 " "\xD7\x90", &layout, "ltr 0..3 | rtl 3..4");
}

static void TestDirectionControls(void)
{
    RoboTextDirectionContext layout = LayoutContext();

    /* A zero-width joiner is a boundary neutral: it neither breaks a run nor begins one. */
    ExpectRuns("\xD9\x85\xE2\x80\x8D\xD9\x85", &layout, "rtl 0..3");
    ExpectRuns("a\xC2\xA0" "b", &layout, "ltr 0..3");

    /*
     * The two marks that *are* strong: a right-to-left mark inside Latin text is its own RTL run, and a
     * left-to-right mark inside Hebrew is its own LTR run. That is not a defect — it is what those scalars
     * are for, and the explicit embeddings that use them to steer a paragraph are 12.8's.
     */
    ExpectRuns("abc\xE2\x80\x8F" "abc", &layout, "ltr 0..3 | rtl 3..4 | ltr 4..7");
    {
        RoboTextDirectionContext undeclared = Undeclared();

        ExpectRuns("abc\xE2\x80\x8F" "abc", &undeclared, "ltr 0..3 | rtl 3..4 | ltr 4..7");
    }
    ExpectRuns("\xD7\x90\xD7\x91\xD7\x92\xE2\x80\x8E\xD7\x93\xD7\x94\xD7\x95", &layout,
               "rtl 0..3 | ltr 3..4 | rtl 4..7");

    /*
     * The embeddings themselves are neutral and their *contents* are classified by the contents' own classes,
     * so `RLE` before Latin text does not make that text RTL here — it will, once 12.8 resolves the level the
     * control opens. The boundary this stage reports is the one the scalars' own classes give.
     */
    ExpectRuns("abc\xE2\x80\xAB" "\xD7\x90\xD7\x91\xD7\x92" "\xE2\x80\xAC" "def", &layout,
               "ltr 0..4 | rtl 4..7 | ltr 7..11");
    ExpectRuns("abc\xE2\x80\xAB" "xyz\xE2\x80\xAC" "def", &layout, "ltr 0..11");
}

static void TestDirectionOverride(void)
{
    RoboTextDirectionContext layout = LayoutContext();
    RoboTextDirectionContext undeclared = Undeclared();
    RoboTextDirectionContext rtl = Declared(RoboTextDirectionRtl);

    /*
     * An override is a *context*, and what it decides is the neutral scalars: the same comma is left-to-right
     * in an LTR context, right-to-left in an RTL one, and undecided with nothing declared. That is the seam
     * CSS's `direction` and the document's `dir` attribute will use — a span analysed with its own context,
     * rather than a Unicode property mutated to express an author's wish.
     */
    ExpectRuns(",", &layout, "ltr 0..1");
    ExpectRuns(",", &rtl, "rtl 0..1");
    ExpectRuns(",", &undeclared, "unknown 0..1");

    /* And it cannot override a strong scalar: a declared LTR base does not turn Arabic left-to-right. */
    {
        RoboTextDirectionContext ltr = Declared(RoboTextDirectionLtr);

        ExpectRuns(TextSampleDirectionArabicSentence, &ltr, "rtl 0..13");
    }
    ExpectRuns("Hello", &rtl, "ltr 0..5");

    /* Digits are the case where the override *does* matter without any strong scalar in the text. */
    ExpectRuns("123", &rtl, "ltr 0..3");
    ExpectRuns("123", &layout, "ltr 0..3");
    ExpectRuns("123", &undeclared, "unknown 0..3");

    /* A span's context is the whole input, so a caller that wants a span analysed alone passes its text. */
    {
        RoboTextView span = DirectionView("\xD7\x90\xD7\x91\xD7\x92");
        RoboTextDirectionRun runs[4];
        size_t count = 0u;
        RoboTextDirection base = RoboTextDirectionUnknown;
        RoboTextDirectionContext rtl = Declared(RoboTextDirectionRtl);

        RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(span, &rtl, NULL, runs, 4u, &count, &base,
                                                             NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)count, 1);
        RoboCheckEqInt((int)base, (int)RoboTextDirectionRtl);
    }
}

/* ------------------------------------------------------------------ */
/* Invariants, refusals and cost                                       */
/* ------------------------------------------------------------------ */

static void TestDirectionInvariants(void)
{
    RoboTextDirectionContext layout = LayoutContext();
    RoboTextDirectionContext undeclared = Undeclared();
    RoboTextDirectionContext rexplicit = Declared(RoboTextDirectionRtl);
    RoboTextDirection *directions = NULL;
    RoboTextView view = DirectionView(TextSampleDirectionLevels);
    RoboTextScalarIndex scalars = 0u;
    RoboTextDirectionRun runs[DirectionRunMax];
    size_t count = 0u;
    RoboTextDirection base = RoboTextDirectionUnknown;
    Result status;

    for (size_t index = 0u; index < DirectionSampleCount; index++) {
        CheckInvariants(kDirectionSamples[index], &layout);
        CheckInvariants(kDirectionSamples[index], &undeclared);
        CheckInvariants(kDirectionSamples[index], &rexplicit);
        CheckInvariants(kDirectionSamples[index], NULL);
    }

    /* Every prefix of one mixed string, which is where an empty-side defect shows up. */
    {
        char prefix[64];
        size_t length = strlen(TextSampleDirectionLevels);

        for (size_t cut = 0u; cut <= length; cut++) {
            if (cut >= sizeof(prefix)) {
                break;
            }
            memcpy(prefix, TextSampleDirectionLevels, cut);
            prefix[cut] = '\0';
            CheckInvariants(prefix, &layout);
        }
    }

    /*
     * The per-scalar array and the run array are two views of **one** answer, which is the property the
     * analysis layer's run cutting depends on: every scalar inside a run carries that run's direction.
     */
    RoboCheckEqInt((int)RoboTextViewScalarCount(view, &scalars, NULL), (int)RoboOk);
    directions = (RoboTextDirection *)Malloc((size_t)scalars * sizeof(*directions));
    RoboCheckMsg(directions != NULL, "the per-scalar array could not be allocated");
    status = RoboTextDirectionRunsResolveView(view, &layout, directions, runs, DirectionRunMax, &count,
                                              &base, NULL);
    RoboCheckEqInt((int)status, (int)RoboOk);
    for (size_t index = 0u; index < count; index++) {
        for (RoboTextScalarIndex scalar = runs[index].range.start; scalar < runs[index].range.end; scalar++) {
            RoboCheckEqInt((int)directions[scalar], (int)runs[index].direction);
        }
    }
    RoboFree(directions);

    /*
     * A run array that cannot hold the answer is **refused with the requirement**, never truncated — the
     * caller learns how many runs there are and can allocate again. And an out run count is required, since a
     * caller with no count has no way to know whether the array it passed was used.
     */
    {
        RoboTextDirectionRun small[1];
        size_t needed = 0u;

        RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(view, &layout, NULL, small, 1u, &needed, NULL,
                                                             NULL),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)needed, 3);
        RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(view, &layout, NULL, small, 1u, NULL, NULL,
                                                             NULL),
                       (int)RoboInvalidArgument);
        /* Exactly enough: the call succeeds, fills the count, and needs no more than it was given. */
        {
            RoboTextDirectionRun exact[3];
            RoboTextDirection exactBase = RoboTextDirectionUnknown;

            RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(view, &layout, NULL, exact, 3u, &needed,
                                                                 &exactBase, NULL),
                           (int)RoboOk);
            RoboCheckEqInt((int)needed, 3);
            RoboCheckEqInt((int)exactBase, (int)RoboTextDirectionLtr);
        }
    }

    /* Refusals: no scalars, no count, an unusable context, and NULL anything. */
    RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(RoboTextViewMake(NULL, 0u), &layout, NULL, runs,
                                                         DirectionRunMax, &count, &base, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 0);
    RoboCheckEqInt((int)RoboTextDirectionRunsResolve(NULL, 1u, &layout, NULL, runs, DirectionRunMax, &count,
                                                     &base, NULL),
                   (int)RoboInvalidArgument);
    {
        RoboTextDirectionContext invalid;

        RoboTextDirectionContextInit(&invalid);
        invalid.fallbackDirection = (RoboTextDirection)9;
        RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(RoboTextViewMake("Hello", 5u), &invalid,
                                                             NULL, runs, DirectionRunMax, &count, &base,
                                                             NULL),
                       (int)RoboInvalidArgument);
    }
    /* A view with a length and no bytes is refused by the decoder underneath, as it is everywhere else. */
    RoboCheck(RoboTextDirectionRunsResolveView(RoboTextViewMake(NULL, 5u), &layout, NULL, runs,
                                               DirectionRunMax, &count, &base, NULL) != RoboOk);

    /* Bytes that are not UTF-8 still resolve: the one invalid-UTF-8 policy is the decoder's, not this stage's. */
    {
        static const char broken[] = "a\xFF\xFE" "b";
        DirectionResult result = Resolve(broken, &layout);

        RoboCheckEqInt((int)result.status, (int)RoboOk);
        RoboCheck(result.count >= 1u);
    }
}

static void TestDirectionDeterminism(void)
{
    RoboTextDirectionContext layout = LayoutContext();
    RoboTextDirectionRun firstRuns[DirectionRunMax];
    RoboTextDirectionRun secondRuns[DirectionRunMax];
    size_t firstCount = 0u;
    size_t secondCount = 0u;
    RoboTextDirection firstBase = RoboTextDirectionUnknown;
    RoboTextDirection secondBase = RoboTextDirectionUnknown;

    /*
     * Same text, same context, same answer — the phase's §"determinism" item, and the reason it matters here
     * rather than in a comment: the runs are what a later stage reorders and a still later one hit-tests, so
     * a classification that varied between frames would move a caret.
     */
    for (size_t index = 0u; index < DirectionSampleCount; index++) {
        RoboTextView view = DirectionView(kDirectionSamples[index]);

        RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(view, &layout, NULL, firstRuns, DirectionRunMax,
                                                             &firstCount, &firstBase, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(view, &layout, NULL, secondRuns,
                                                             DirectionRunMax, &secondCount, &secondBase, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)firstCount, (int)secondCount);
        RoboCheckEqInt((int)firstBase, (int)secondBase);
        for (size_t run = 0u; run < firstCount; run++) {
            RoboCheckEqInt((int)firstRuns[run].range.start, (int)secondRuns[run].range.start);
            RoboCheckEqInt((int)firstRuns[run].range.end, (int)secondRuns[run].range.end);
            RoboCheckEqInt((int)firstRuns[run].direction, (int)secondRuns[run].direction);
        }
    }
}

static void TestDirectionDiagnostics(void)
{
    RoboTextView view = DirectionView(TextSampleDirectionMixedLtr);
    RoboTextDirectionRun runs[4];
    size_t count = 0u;
    char line[64];
    char table[512];
    size_t length = 0u;
    RoboTextDirectionContext layout = LayoutContext();

    RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(view, &layout, NULL, runs, 4u, &count, NULL, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 2);

    /* One run as a line, and the two refusals a dump has: no room, and nothing to dump. */
    RoboCheckEqInt((int)RoboTextDirectionRunWrite(&runs[0], line, sizeof(line), &length), (int)RoboOk);
    RoboCheckStrEq(line, "[0..6) ltr");
    RoboCheckEqInt((int)length, 10);
    RoboCheckEqInt((int)RoboTextDirectionRunWrite(&runs[1], line, sizeof(line), NULL), (int)RoboOk);
    RoboCheckStrEq(line, "[6..11) rtl");
    RoboCheckEqInt((int)RoboTextDirectionRunWrite(&runs[0], line, 4u, &length), (int)RoboNotSupported);
    RoboCheckEqInt((int)length, 10);
    RoboCheckEqInt((int)RoboTextDirectionRunWrite(NULL, line, sizeof(line), NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextDirectionRunWrite(&runs[0], NULL, sizeof(line), NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextDirectionRunWrite(&runs[0], line, 0u, NULL), (int)RoboInvalidArgument);

    /*
     * The table, which is the phase's §29 diagnostic: a header, then one row per run **in both index
     * spaces**, so a reader can see the UTF-8 byte span the scalars occupy. The byte ends are 6 and 16
     * because the Arabic scalars are two bytes each.
     */
    RoboCheckEqInt((int)RoboTextDirectionRunsWrite(view, runs, count, table, sizeof(table), &length),
                   (int)RoboOk);
    RoboCheckStrEq(table,
                   "Direction Analysis\n"
                   "[0..6) scalars  bytes [0..6)  ltr\n"
                   "[6..11) scalars  bytes [6..16)  rtl\n");
    RoboCheckEqInt((int)length, (int)strlen(table));

    /* Without a view the table carries the scalar ranges only, and there is nothing to check them against. */
    RoboCheckEqInt((int)RoboTextDirectionRunsWrite(RoboTextViewMake(NULL, 0u), runs, count, table,
                                                   sizeof(table), &length),
                   (int)RoboOk);
    RoboCheckStrEq(table, "Direction Analysis\n[0..6) scalars  ltr\n[6..11) scalars  rtl\n");

    /* A buffer that cannot hold the whole table is left empty and told what it needed. */
    {
        char small[16];
        size_t needed = 0u;

        memset(small, 'x', sizeof(small));
        RoboCheckEqInt((int)RoboTextDirectionRunsWrite(view, runs, count, small, sizeof(small), &needed),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)small[0], 0);
        RoboCheck(needed > sizeof(small));
        RoboCheckEqInt((int)RoboTextDirectionRunsWrite(view, NULL, count, small, sizeof(small), &needed),
                       (int)RoboInvalidArgument);
        /* A NULL length is allowed — the caller that only wants the failure does not need one — and the
         * buffer is still left empty rather than half written. */
        RoboCheckEqInt((int)RoboTextDirectionRunsWrite(view, runs, count, small, sizeof(small), NULL),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)small[0], 0);
    }

    /* A run that does not lie in the text is refused rather than printed as if it did. */
    {
        RoboTextDirectionRun outside[1];

        outside[0].range = RoboTextRangeMake(4u, 40u);
        outside[0].direction = RoboTextDirectionLtr;
        RoboCheckEqInt((int)RoboTextDirectionRunsWrite(view, outside, 1u, table, sizeof(table), &length),
                       (int)RoboInvalidArgument);
    }

    /* An empty run set is just the header, which is what an empty text's dump should read as. */
    RoboCheckEqInt((int)RoboTextDirectionRunsWrite(view, runs, 0u, table, sizeof(table), &length),
                   (int)RoboOk);
    RoboCheckStrEq(table, "Direction Analysis\n");
}

static void TestDirectionPerformance(void)
{
    /* A representative UI paragraph of 10–20 KB, built from an ASCII sentence and a mixed one. */
    static char paragraph[20480];
    static char mixed[20480];
    Uchar32 *codePoints = NULL;
    RoboTextScalarIndex scalars = 0u;
    RoboTextDirectionRun runs[DirectionRunMax];
    size_t count = 0u;
    size_t asciiWritten = 0u;
    size_t mixedWritten = 0u;
    size_t mixedRuns = 0u;
    MemStats before;
    MemStats after;
    clock_t started;
    clock_t finished;
    RoboTextDirectionContext layout = LayoutContext();

    while (asciiWritten + sizeof(TextSamplePangram) - 1u < sizeof(paragraph) - 2u) {
        memcpy(paragraph + asciiWritten, TextSamplePangram, sizeof(TextSamplePangram) - 1u);
        asciiWritten += sizeof(TextSamplePangram) - 1u;
        paragraph[asciiWritten++] = ' ';
    }
    paragraph[asciiWritten] = '\0';

    /*
     * Ordinary Latin UI text is **one** run and one pass: letters are strong, spaces are neutrals whose two
     * sides agree, and the run count does not grow with the length of the text.
     */
    memset(runs, 0, sizeof(runs));
    started = clock();
    RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(DirectionView(paragraph), &layout, NULL, runs,
                                                         DirectionRunMax, &count, NULL, NULL),
                   (int)RoboOk);
    finished = clock();
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)runs[0].range.end, (int)asciiWritten);
    RoboCheckEqInt((int)runs[0].direction, (int)RoboTextDirectionLtr);

    /*
     * And the array door over the same scalars allocates **nothing**: the resolver's only storage is the
     * caller's arrays, so its cost is one pass rather than an allocation per scalar. These assertions are the
     * phase's §34 "do not allocate per code point", real in the test build (the debug allocator) and vacuous
     * in a release one, which is why the run count above is asserted either way.
     */
    RoboCheckEqInt((int)RoboTextUnicodeDecodeAll(DirectionView(paragraph), &codePoints, &scalars, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)scalars, (int)asciiWritten);
    MemDebugGetStats(&before);
    RoboCheckEqInt((int)RoboTextDirectionRunsResolve(codePoints, (size_t)scalars, &layout, NULL, runs,
                                                     DirectionRunMax, &count, NULL, NULL),
                   (int)RoboOk);
    MemDebugGetStats(&after);
    RoboCheckEqInt((int)after.totalAllocs - (int)before.totalAllocs, 0);
    RoboCheckEqInt((int)after.totalFrees - (int)before.totalFrees, 0);
    RoboCheckEqInt((int)count, 1);
    /*
     * And the same is true of the paragraph direction, which is the part a reader would expect to be
     * expensive: P2/P3 is a forward scan here, and the one path that allocates — an isolate initiator before
     * any strong scalar, where the skip rule needs RoboUnicode's own state — is not taken by ordinary text.
     */
    {
        RoboTextDirectionContext declaredLtr = Declared(RoboTextDirectionLtr);

        MemDebugGetStats(&before);
        RoboCheckEqInt((int)RoboTextDirectionRunsResolve(codePoints, (size_t)scalars, &declaredLtr, NULL,
                                                         runs, DirectionRunMax, &count, NULL, NULL),
                       (int)RoboOk);
        MemDebugGetStats(&after);
        RoboCheckEqInt((int)after.totalAllocs - (int)before.totalAllocs, 0);
        /* …and the scan is what answers an `Auto` paragraph, with no allocation of its own. */
        MemDebugGetStats(&before);
        RoboCheckEqInt((int)RoboTextDirectionResolveParagraph(codePoints, (size_t)scalars, &layout),
                       (int)RoboTextDirectionLtr);
        MemDebugGetStats(&after);
        RoboCheckEqInt((int)after.totalAllocs - (int)before.totalAllocs, 0);
    }
    RoboTextUnicodeScalarsFree(codePoints);

    /*
     * A mixed 10–20 KB paragraph: the run count grows with the number of *boundaries* — two per repetition,
     * the Arabic stretch and the Latin one — while the allocation count stays at one, for the scalars.
     */
    {
        size_t latinStep = sizeof("RoboticelaOS text ") - 1u;
        size_t arabicStep = sizeof(TextSampleDirectionArabicSentence " ") - 1u;

        while (mixedWritten + latinStep + arabicStep < sizeof(mixed) - 2u) {
            memcpy(mixed + mixedWritten, "RoboticelaOS text ", latinStep);
            mixedWritten += latinStep;
            memcpy(mixed + mixedWritten, TextSampleDirectionArabicSentence " ", arabicStep);
            mixedWritten += arabicStep;
        }
        mixed[mixedWritten] = '\0';

        MemDebugGetStats(&before);
        started = clock();
        RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(DirectionView(mixed), &layout, NULL, runs,
                                                             DirectionRunMax, &count, NULL, NULL),
                       (int)RoboNotSupported);
        finished = clock();
        MemDebugGetStats(&after);
        /* A run array too small for the answer is refused with the requirement, and still allocates only the
         * scalars it had to decode. */
        RoboCheck(count > DirectionRunMax);
        RoboCheckEqInt((int)after.totalAllocs - (int)before.totalAllocs, 1);
        RoboCheckEqInt((int)after.totalFrees - (int)before.totalFrees, 1);
        mixedRuns = count;
    }

    /* The phase asks for cost to be *recorded*, so it is printed: bytes in, runs out, allocations, time. */
    printf("text.direction.performance: ascii %u bytes -> 1 run; mixed %u bytes -> %u runs, "
           "1 allocation each, %ld ms\n",
           (unsigned)asciiWritten, (unsigned)mixedWritten, (unsigned)mixedRuns,
           (long)((finished - started) * 1000L / CLOCKS_PER_SEC));
    RoboCheck(asciiWritten >= 10240u);
    RoboCheck(mixedWritten >= 10240u);
}

/* ------------------------------------------------------------------ */
/* Integration with the rest of the module                             */
/* ------------------------------------------------------------------ */

static void TestDirectionAnalysisIntegration(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextDirectionContext declaredLtr = Declared(RoboTextDirectionLtr);
    RoboTextFontSelector *selector = DirectionSelectorOver(&fixture.font);
    RoboTextAnalysis *analysis = NULL;
    RoboTextDirectionContext context;
    RoboTextShapeOptions options;
    RoboTextView view = DirectionView(TextSampleScriptMixed);
    RoboTextDirectionRun resolverRuns[DirectionRunMax];
    size_t resolverCount = 0u;
    RoboTextDirection resolverBase = RoboTextDirectionUnknown;
    RoboTextAnalysisStats stats;

    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    RoboCheckEqInt((int)RoboTextAnalysisCreate(view, selector, &options, &analysis, NULL), (int)RoboOk);

    /*
     * The analysis's direction runs and the resolver's are **one answer**: the analysis asks the resolver and
     * keeps what it produced, so a caller reading the direction runs beside the analysis runs is comparing the
     * boundaries the run walk below used rather than a second computation of them.
     */
    RoboCheckEqInt((int)RoboTextDirectionRunsResolveView(view, &declaredLtr, NULL,
                                                         resolverRuns, DirectionRunMax, &resolverCount,
                                                         &resolverBase, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisDirectionRunCount(analysis), (int)resolverCount);
    RoboCheckEqInt((int)RoboTextAnalysisBaseDirection(analysis), (int)resolverBase);
    for (size_t index = 0u; index < resolverCount; index++) {
        const RoboTextDirectionRun *run = RoboTextAnalysisDirectionRun(analysis, index);

        RoboCheck(run != NULL);
        RoboCheckEqInt((int)run->range.start, (int)resolverRuns[index].range.start);
        RoboCheckEqInt((int)run->range.end, (int)resolverRuns[index].range.end);
        RoboCheckEqInt((int)run->direction, (int)resolverRuns[index].direction);
    }
    RoboCheck(RoboTextAnalysisDirectionRun(analysis, resolverCount) == NULL);
    RoboCheckEqInt((int)RoboTextAnalysisDirectionRunCount(NULL), 0);
    RoboCheck(RoboTextAnalysisDirectionRun(NULL, 0u) == NULL);

    /* The context the analysis classified in is readable, and it is the one the options asked for. */
    RoboCheckEqInt((int)RoboTextAnalysisDirectionContext(analysis, &context), (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)context.mode, (int)RoboTextDirectionModeExplicitLtr);
    RoboCheckEqInt((int)context.fallbackDirection, (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextAnalysisDirectionContext(NULL, &context), (int)RoboTextDirectionUnknown);
    RoboCheckEqInt((int)context.mode, (int)RoboTextDirectionModeAuto);
    RoboCheckEqInt((int)RoboTextAnalysisDirectionContext(analysis, NULL), (int)RoboTextDirectionLtr);

    /* The stats count them, and every analysis run's direction is the direction run covering it. */
    RoboCheckEqInt((int)RoboTextAnalysisGetStats(analysis, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.directionRuns, (int)resolverCount);
    RoboCheckEqInt((int)stats.scriptRuns, (int)RoboTextAnalysisScriptRunCount(analysis));
    /* The phase's mixed sample is Latin, Arabic and Han: two RTL runs are impossible, and at least one is. */
    RoboCheck(stats.rtlRuns >= 1u);
    for (size_t index = 0u; index < RoboTextAnalysisRunCount(analysis); index++) {
        const RoboTextAnalysisRun *run = RoboTextAnalysisRunAt(analysis, index);
        const RoboTextDirectionRun *covering = NULL;

        for (size_t looking = 0u; looking < resolverCount; looking++) {
            const RoboTextDirectionRun *candidate = RoboTextAnalysisDirectionRun(analysis, looking);

            if (RoboTextRangeContains(candidate->range, run->range.start)) {
                covering = candidate;
                break;
            }
        }
        /* Every analysis run lies inside a direction run and carries that run's direction — which is what
         * makes the two arrays comparable rather than merely consistent. */
        RoboCheck(covering != NULL);
        RoboCheckEqInt((int)run->direction, (int)covering->direction);
        RoboCheck(RoboTextRangeContainsRange(covering->range, run->range));
    }

    /*
     * The phase's §32 regression: every shape request the pipeline hands out now carries a real direction.
     *
     * "Real" is what is asserted — a defined direction, and the paragraph direction beside it — rather than
     * *usable*, which additionally needs a face: the shipped pack is Inter alone, so the Arabic and Han runs of
     * this sample have no covering candidate and their request correctly reports `RoboTextFontSourceNone`. That
     * is the pack's limit rather than the direction stage's, and asserting `usable` for every run would make
     * this case pass only in a tree that had a fallback face to find.
     */
    for (size_t index = 0u; index < RoboTextAnalysisRunCount(analysis); index++) {
        const RoboTextAnalysisRun *run = RoboTextAnalysisRunAt(analysis, index);
        RoboTextShapeRequest request;

        RoboCheckEqInt((int)RoboTextAnalysisShapeRequest(analysis, index, &request), (int)RoboOk);
        RoboCheck(RoboTextDirectionIsKnown(request.direction));
        RoboCheckEqInt((int)request.direction, (int)run->direction);
        RoboCheckEqInt((int)request.baseDirection, (int)RoboTextDirectionLtr);
        RoboCheckEqInt((int)request.range.start, (int)run->range.start);
        if (run->font.source != RoboTextFontSourceNone) {
            RoboCheck(RoboTextShapeRequestIsUsable(&request));
        }
    }

    RoboTextAnalysisDestroy(analysis);
    analysis = NULL;

    /*
     * And the same text with nothing declared: the paragraph direction is then the text's own first strong
     * character, which is the Latin of `"Hello "` — so the runs are the same and the *base* differs, which is
     * the distinction a caller that declared a direction needs to be able to see.
     */
    RoboCheckEqInt((int)RoboTextAnalysisCreate(view, selector, NULL, &analysis, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisBaseDirection(analysis), (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)RoboTextAnalysisDirectionContext(analysis, &context), (int)RoboTextDirectionLtr);
    RoboCheckEqInt((int)context.mode, (int)RoboTextDirectionModeAuto);
    RoboCheckEqInt((int)context.fallbackDirection, (int)RoboTextDirectionUnknown);
    RoboTextAnalysisDestroy(analysis);

    /* The run set's direction runs are the resolver's too — one implementation, two consumers. */
    {
        RoboTextRunSet *set = NULL;

        RoboCheckEqInt((int)RoboTextRunSetCreate(view, &fixture.font, NULL, &set, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextRunSetDirectionRunCount(set), (int)resolverCount);
        for (size_t index = 0u; index < resolverCount; index++) {
            const RoboTextDirectionRun *run = RoboTextRunSetDirectionRun(set, index);

            RoboCheckEqInt((int)run->range.start, (int)resolverRuns[index].range.start);
            RoboCheckEqInt((int)run->range.end, (int)resolverRuns[index].range.end);
            RoboCheckEqInt((int)run->direction, (int)resolverRuns[index].direction);
        }
        RoboCheckEqInt((int)RoboTextRunSetBaseDirection(set), (int)RoboTextDirectionLtr);
        RoboTextRunSetDestroy(set);
    }

    /* A degenerate analysis still answers: no text, no runs, and the direction doors return nothing. */
    RoboCheckEqInt((int)RoboTextAnalysisCreate(RoboTextViewMake(NULL, 0u), selector, NULL, &analysis, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextAnalysisDirectionRunCount(analysis), 0);
    RoboCheck(RoboTextAnalysisDirectionRun(analysis, 0u) == NULL);
    RoboCheckEqInt((int)RoboTextAnalysisDirectionContext(analysis, &context), (int)RoboTextDirectionUnknown);
    RoboTextAnalysisDestroy(analysis);

    RoboTextFontSelectorDestroy(selector);
    TextFixtureClose(&fixture);
}

void TestRegisterTextDirection(void)
{
    TestRegister("text.direction.classes", TestDirectionClasses);
    TestRegister("text.direction.ascii_fast_path", TestDirectionAsciiFastPath);
    TestRegister("text.direction.paragraph", TestDirectionParagraph);
    TestRegister("text.direction.pure", TestDirectionPure);
    TestRegister("text.direction.mixed", TestDirectionMixed);
    TestRegister("text.direction.neutrals", TestDirectionNeutrals);
    TestRegister("text.direction.numbers", TestDirectionNumbers);
    TestRegister("text.direction.marks", TestDirectionMarks);
    TestRegister("text.direction.controls", TestDirectionControls);
    TestRegister("text.direction.override", TestDirectionOverride);
    TestRegister("text.direction.invariants", TestDirectionInvariants);
    TestRegister("text.direction.determinism", TestDirectionDeterminism);
    TestRegister("text.direction.diagnostics", TestDirectionDiagnostics);
    TestRegister("text.direction.performance", TestDirectionPerformance);
    TestRegister("text.direction.analysis_integration", TestDirectionAnalysisIntegration);
}
