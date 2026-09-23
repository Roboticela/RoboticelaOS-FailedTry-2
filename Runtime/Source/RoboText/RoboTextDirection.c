/*
 * RoboText's direction resolver (Phase 12.7) — see RoboTextDirection.h.
 *
 * One algorithm, and the file exists so that it exists **once**. The run set (`RoboTextRuns.c`) and the
 * analysis layer (`RoboTextAnalysis.c`) are both consumers: each asks for the per-scalar directions *and*
 * the runs, and neither re-derives either. Where 12.6's script resolver resolves a *neutral block* against
 * both neighbours and consults `Script_Extensions`, this one resolves the same shape of block against both
 * neighbours and consults the paragraph direction — the family resemblance is deliberate, because a reader
 * who has understood one should be able to read the other.
 *
 * ## The rule, in the order it is applied
 *
 *   1. `L` / `R` / `AL` keep their own direction — the only scalars that have one of their own.
 *
 *   2. `NSM` takes the resolved direction of the character before it: UAX #9's W1, and the reason a
 *      combining mark can never split a run. With nothing before it a mark is neutral, which is W1's own
 *      answer for a mark at the start of a paragraph.
 *
 *   3. A block of number-class scalars with at least one `EN`/`AN` in it is a **number**, and a number is
 *      left-to-right whenever the paragraph direction is known, in *either* base — see the header for the
 *      I1/I2 derivation — and `Unknown` when the paragraph has no strong character and the context
 *      declared none. A block of those classes with no digit (a lone `,`, a lone `.`) falls through to
 *      the neutral rule below, because a separator standing between two words is punctuation.
 *
 *   4. Everything else is one block of neutrals, resolved against the nearest **committed** unit on each
 *      side: the two sides agree → that direction; they differ, or a side is empty → the paragraph
 *      direction (which may be `Unknown`).
 *
 * Rule 4 is UAX #9's N1 read at the level this stage has, and two of its details are what make the runs
 * agree with the algorithm's level runs rather than approximately agree with them:
 *
 *   **A number lends `RTL`, not its own direction.** N1 says so in as many words — "European and Arabic
 *   numbers act as if they were R" — and it is why the space in `ABC אבג 123 DEF` joins the Hebrew run: the
 *   number's own direction is left-to-right, but what it does to the neutral beside it is right-to-left.
 *   A stage that gave the neutral the number's own direction puts that space in the wrong run.
 *
 *   **An empty side is the paragraph direction**, because N1's `sor` and `eor` are the paragraph's. That is
 *   what keeps the `)` in `"(2026)"` left-to-right in an LTR paragraph even though the unit before it is a
 *   number whose influence is RTL: the two sides differ, so the paragraph's answer is used.
 *
 * ## Cost
 *
 * One forward pass, one lookahead per neutral block (`NextUnitInfluence`, which walks until it finds a
 * committed unit), and **no allocation**: the only storage is the caller's two arrays. ASCII never reaches a
 * table: `RoboTextDirectionClassOf` answers `A`–`Z`/`a`–`z` as `L`, `0`–`9` as a number class and every
 * other ASCII scalar as neutral from the block, so `"Hello, world!"` is one strong scalar per letter, one
 * `prevSide == nextSide` neutral block, and one run.
 */

#include "RoboText/RoboTextDirection.h"

#include "RoboMem/RoboMem.h"
#include "RoboTextInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

int RoboTextDirectionAsciiClassOf(RoboTextCodePoint codePoint, RoboTextDirectionClass *outClass)
{
    RoboTextDirectionClass asciiClass;

    if (codePoint > 0x7Fu) {
        return 0;
    }
    if ((codePoint >= (RoboTextCodePoint)'A' && codePoint <= (RoboTextCodePoint)'Z') ||
        (codePoint >= (RoboTextCodePoint)'a' && codePoint <= (RoboTextCodePoint)'z')) {
        asciiClass = RoboTextDirectionClassLeftToRight;
    } else if ((codePoint >= (RoboTextCodePoint)'0' && codePoint <= (RoboTextCodePoint)'9') ||
               /*
                * The three weak classes the ASCII block actually contains, spelled out because the shortcut
                * has to be *exact* rather than roughly right: `ET` is `#`, `$` and `%`; `ES` is `+` and `-`;
                * `CS` is `,`, `.`, `/` and `:`. Everything else in the block is `WS`, `ON`, `S` or `B`, which
                * are all neutral. Leaving the separators out would split `1,234` at the comma in an RTL
                * paragraph, which is the defect `text.direction.ascii_fast_path` exists to catch.
                */
               codePoint == (RoboTextCodePoint)'#' || codePoint == (RoboTextCodePoint)'$' ||
               codePoint == (RoboTextCodePoint)'%' || codePoint == (RoboTextCodePoint)'+' ||
               codePoint == (RoboTextCodePoint)'-' || codePoint == (RoboTextCodePoint)',' ||
               codePoint == (RoboTextCodePoint)'.' || codePoint == (RoboTextCodePoint)'/' ||
               codePoint == (RoboTextCodePoint)':') {
        asciiClass = RoboTextDirectionClassNumber;
    } else {
        asciiClass = RoboTextDirectionClassNeutral;
    }
    if (outClass) {
        *outClass = asciiClass;
    }
    return 1;
}

RoboTextDirectionClass RoboTextDirectionClassOf(RoboTextCodePoint codePoint)
{
    /*
     * The **table**, with no shortcut: this is the door the ASCII block's fast path is proved against, so a
     * shortcut inside it would make the proof vacuous. The resolver takes `RoboTextDirectionAsciiClassOf`
     * first and calls this for everything else — see `ScalarClass`.
     */
    switch (RoboTextUnicodeBidiClass(codePoint)) {
        case RoboUBcL:
            return RoboTextDirectionClassLeftToRight;
        case RoboUBcR:
        case RoboUBcAl:
            return RoboTextDirectionClassRightToLeft;
        case RoboUBcEn:
        case RoboUBcAn:
        case RoboUBcEs:
        case RoboUBcEt:
        case RoboUBcCs:
            return RoboTextDirectionClassNumber;
        case RoboUBcNsm:
            return RoboTextDirectionClassAttachLeft;
        default:
            /* `WS`, `ON`, `B`, `S`, `BN`, and the nine explicit formatting controls. The controls are
             * neutral here on purpose: their contents are classified from the contents' own classes, and
             * honouring the embedding they open is 12.8's. */
            return RoboTextDirectionClassNeutral;
    }
}

/** A scalar's direction class, through the ASCII block first. */
static RoboTextDirectionClass ScalarClass(RoboTextCodePoint codePoint)
{
    RoboTextDirectionClass asciiClass;

    if (RoboTextDirectionAsciiClassOf(codePoint, &asciiClass)) {
        return asciiClass;
    }
    return RoboTextDirectionClassOf(codePoint);
}

/** Non-zero when @p directionClass carries a direction of its own. */
static int ClassIsStrong(RoboTextDirectionClass directionClass)
{
    return directionClass == RoboTextDirectionClassLeftToRight ||
           directionClass == RoboTextDirectionClassRightToLeft;
}

/** The direction a strong class names. Not called on a non-strong class. */
static RoboTextDirection ClassDirection(RoboTextDirectionClass directionClass)
{
    return (directionClass == RoboTextDirectionClassLeftToRight) ? RoboTextDirectionLtr
                                                                 : RoboTextDirectionRtl;
}

/** Non-zero when the scalar is `EN` or `AN` — the two classes that make a block of number classes a
 *  number rather than a stretch of punctuation. */
static int ScalarIsDigit(RoboTextCodePoint codePoint)
{
    UnicodeBidiClass bidiClass = RoboTextUnicodeBidiClass(codePoint);

    return bidiClass == RoboUBcEn || bidiClass == RoboUBcAn;
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

RoboTextDirection RoboTextDirectionResolveParagraph(const Uchar32 *scalars,
                                                    size_t count,
                                                    const RoboTextDirectionContext *context)
{
    RoboTextDirectionContext defaults;

    if (!context) {
        RoboTextDirectionContextInit(&defaults);
        context = &defaults;
    }
    switch (context->mode) {
        case RoboTextDirectionModeExplicitLtr:
            return RoboTextDirectionLtr;
        case RoboTextDirectionModeExplicitRtl:
            return RoboTextDirectionRtl;
        case RoboTextDirectionModeAuto:
        default:
            break;
    }
    /*
     * P2/P3 through the 12.5 door: `Unknown` as the declared base asks it purely "what does the text
     * itself say", and the context's fallback answers when the text says nothing. Passing the fallback as
     * the *declared* base instead would answer `Ltr` for an RTL paragraph whose fallback is `Ltr`, because
     * that door short-circuits on a declared base — which is right for a declaration and wrong for a
     * default.
     */
    {
        RoboTextDirection strong = RoboTextUnicodeBaseDirection(scalars, count, RoboTextDirectionUnknown);

        return (strong != RoboTextDirectionUnknown) ? strong : context->fallbackDirection;
    }
}

/**
 * The **influence** the next committed unit at or after @p from has on a neutral block beside it.
 *
 * A committed unit is a strong scalar, or a number-class block with a digit in it; neutrals and marks are
 * looked past, and a number-class block with no digit (a lone separator) is looked past too, because it
 * resolves as a neutral and so commits to nothing. A strong scalar answers its own direction; a number
 * answers @p lastStrong — the direction of the nearest strong scalar **before** it, or @p base when there is
 * none — which is UAX #9's W7 ("search backward from each European number until the first strong type is
 * found; if an L is found the number becomes L"), and the reason `"123 456"` is one left-to-right run in an
 * LTR paragraph and three runs in an RTL one. With no such unit at all the answer is @p base, which is N1's
 * `eor`.
 *
 * @p haveStrong and @p lastStrong describe the direction of the nearest strong scalar *before* @p from. They
 * are the caller's own state, and they are correct to hand in here: a strong scalar between the caller's
 * position and @p from would have been returned by this function before it could reach a number.
 */
static RoboTextDirection NextUnitInfluence(const Uchar32 *scalars,
                                           size_t count,
                                           size_t from,
                                           RoboTextDirection base,
                                           int haveStrong,
                                           RoboTextDirection lastStrong)
{
    size_t index = from;

    while (index < count) {
        RoboTextDirectionClass directionClass = ScalarClass((RoboTextCodePoint)scalars[index]);

        if (ClassIsStrong(directionClass)) {
            return ClassDirection(directionClass);
        }
        if (directionClass == RoboTextDirectionClassNumber) {
            size_t end = index;
            int hasDigit = 0;

            while (end < count) {
                RoboTextDirectionClass blockClass = ScalarClass((RoboTextCodePoint)scalars[end]);

                if (blockClass != RoboTextDirectionClassNumber) {
                    break;
                }
                if (ScalarIsDigit((RoboTextCodePoint)scalars[end])) {
                    hasDigit = 1;
                }
                end++;
            }
            if (hasDigit) {
                /* A number only commits when it has a direction of its own, which needs a paragraph to put it
                 * at an even level; with none, the block is no more a context than the neutrals around it and
                 * the search continues. */
                return (base == RoboTextDirectionUnknown)
                           ? RoboTextDirectionUnknown
                           : (haveStrong ? lastStrong : base);
            }
            index = end;
            continue;
        }
        index++;
    }
    return base;
}

/** Accumulates the resolved runs as the scalars are resolved, coalescing as it goes. */
typedef struct DirectionRunWriter {
    RoboTextDirection *outDirections; /* optional */
    RoboTextDirectionRun *outRuns;    /* optional */
    size_t runCapacity;
    size_t runCount;
    RoboTextScalarIndex runStart;
    RoboTextDirection runDirection;
    int runOpen;
} DirectionRunWriter;

static void WriterScalar(DirectionRunWriter *writer,
                         RoboTextScalarIndex index,
                         RoboTextDirection direction)
{
    if (writer->outDirections) {
        writer->outDirections[index] = direction;
    }
    if (!writer->runOpen) {
        writer->runStart = index;
        writer->runDirection = direction;
        writer->runOpen = 1;
        return;
    }
    if (direction != writer->runDirection) {
        /* Coalescing is by construction: a run closes only when the resolved direction changes, so two
         * adjacent runs can never carry the same direction. */
        if (writer->outRuns && writer->runCount < writer->runCapacity) {
            writer->outRuns[writer->runCount].range = RoboTextRangeMake(writer->runStart, index);
            writer->outRuns[writer->runCount].direction = writer->runDirection;
        }
        writer->runCount++;
        writer->runStart = index;
        writer->runDirection = direction;
    }
}

static void WriterClose(DirectionRunWriter *writer, RoboTextScalarIndex end)
{
    if (!writer->runOpen) {
        return;
    }
    if (writer->outRuns && writer->runCount < writer->runCapacity) {
        writer->outRuns[writer->runCount].range = RoboTextRangeMake(writer->runStart, end);
        writer->outRuns[writer->runCount].direction = writer->runDirection;
    }
    writer->runCount++;
}

Result RoboTextDirectionRunsResolve(const Uchar32 *scalars,
                                    size_t count,
                                    const RoboTextDirectionContext *context,
                                    RoboTextDirection *outDirections,
                                    RoboTextDirectionRun *outRuns,
                                    size_t runCapacity,
                                    size_t *outRunCount,
                                    RoboTextDirection *outBase,
                                    RoboTextDiag *diag)
{
    DirectionRunWriter writer;
    RoboTextDirectionContext defaults;
    RoboTextDirection base;
    RoboTextDirection prev = RoboTextDirectionUnknown;
    RoboTextDirection lastStrong = RoboTextDirectionUnknown;
    int havePrev = 0;
    int haveStrong = 0;
    size_t index = 0u;

    if (!outRunCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "an out run count is required");
    }
    *outRunCount = 0u;
    if (outBase) {
        *outBase = RoboTextDirectionUnknown;
    }
    if (!scalars && count != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "scalars are required for a non-empty text");
    }
    if (!context) {
        RoboTextDirectionContextInit(&defaults);
        context = &defaults;
    }
    if (!RoboTextDirectionContextIsValid(context)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "the direction context is not usable");
    }

    base = RoboTextDirectionResolveParagraph(scalars, count, context);
    if (outBase) {
        *outBase = base;
    }

    memset(&writer, 0, sizeof(writer));
    writer.outDirections = outDirections;
    writer.outRuns = outRuns;
    writer.runCapacity = runCapacity;

    while (index < count) {
        RoboTextDirectionClass directionClass = ScalarClass((RoboTextCodePoint)scalars[index]);

        if (ClassIsStrong(directionClass)) {
            RoboTextDirection direction = ClassDirection(directionClass);

            WriterScalar(&writer, (RoboTextScalarIndex)index, direction);
            prev = direction;
            havePrev = 1;
            /* Kept separately from `prev` because the two differ after a number: `prev` is what the unit
             * *lends* the neutral beside it, `lastStrong` is what W7 looks back for. */
            lastStrong = direction;
            haveStrong = 1;
            index++;
            continue;
        }
        if (directionClass == RoboTextDirectionClassAttachLeft && havePrev) {
            /* W1: the mark takes the type of the character before it, so it can never begin or split a
             * run — which is how a base character and its accent stay in one directional context. */
            WriterScalar(&writer, (RoboTextScalarIndex)index, prev);
            index++;
            continue;
        }

        /* One non-strong unit: a number block, or a block of neutrals (which is also where a mark with
         * nothing before it lands, W1's own answer for it). */
        {
            int numberKind = (directionClass == RoboTextDirectionClassNumber);
            size_t blockEnd = index;
            int hasDigit = 0;

            while (blockEnd < count) {
                RoboTextDirectionClass blockClass = ScalarClass((RoboTextCodePoint)scalars[blockEnd]);

                if (ClassIsStrong(blockClass)) {
                    break;
                }
                if ((blockClass == RoboTextDirectionClassNumber) != numberKind) {
                    break;
                }
                if (numberKind && ScalarIsDigit((RoboTextCodePoint)scalars[blockEnd])) {
                    hasDigit = 1;
                }
                blockEnd++;
            }

            if (numberKind && hasDigit) {
                /* UAX #9 leaves a number's level even in either base, and even is left-to-right. With no
                 * paragraph direction there is no level to be even, so the number stays unresolved. */
                RoboTextDirection direction =
                    (base == RoboTextDirectionUnknown) ? RoboTextDirectionUnknown : RoboTextDirectionLtr;

                for (size_t position = index; position < blockEnd; position++) {
                    WriterScalar(&writer, (RoboTextScalarIndex)position, direction);
                }
                if (direction != RoboTextDirectionUnknown) {
                    /* The **influence** is committed, not the number's own direction: the next neutral block
                     * reads `prev` as "what the unit before me does to me", and W7 makes that the nearest
                     * strong scalar before the number (or the paragraph, when there is none). */
                    prev = haveStrong ? lastStrong : base;
                    havePrev = 1;
                }
            } else {
                /* N1: the block joins the side it agrees with, and takes the paragraph direction where the
                 * sides disagree or one of them is empty. `havePrev` false means the left side is `sor`,
                 * which is the paragraph direction, and `NextUnitInfluence` answers the same for `eor`. */
                RoboTextDirection prevSide = havePrev ? prev : base;
                RoboTextDirection nextSide = NextUnitInfluence(scalars, count, blockEnd, base, haveStrong,
                                                               lastStrong);
                RoboTextDirection direction = (prevSide == nextSide) ? prevSide : base;

                for (size_t position = index; position < blockEnd; position++) {
                    WriterScalar(&writer, (RoboTextScalarIndex)position, direction);
                }
            }
            index = blockEnd;
        }
    }
    WriterClose(&writer, (RoboTextScalarIndex)count);

    *outRunCount = writer.runCount;
    if (outRuns && writer.runCount > runCapacity) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboNotSupported,
                               "the caller's array cannot hold the direction runs");
    }
    return RoboOk;
}

Result RoboTextDirectionRunsResolveView(RoboTextView view,
                                        const RoboTextDirectionContext *context,
                                        RoboTextDirection *outDirections,
                                        RoboTextDirectionRun *outRuns,
                                        size_t runCapacity,
                                        size_t *outRunCount,
                                        RoboTextDirection *outBase,
                                        RoboTextDiag *diag)
{
    Uchar32 *scalars = NULL;
    RoboTextScalarIndex count = 0u;
    Result status;

    if (!outRunCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "an out run count is required");
    }
    *outRunCount = 0u;
    status = RoboTextUnicodeDecodeAll(view, &scalars, &count, diag);
    if (status != RoboOk) {
        return status;
    }
    status = RoboTextDirectionRunsResolve(scalars, (size_t)count, context, outDirections, outRuns,
                                          runCapacity, outRunCount, outBase, diag);
    RoboTextUnicodeScalarsFree(scalars);
    return status;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result RoboTextDirectionRunWrite(const RoboTextDirectionRun *run,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength)
{
    char line[64];
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!run || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    length = RoboTextWriterText(line, sizeof(line), length, "[");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, run->range.start);
    length = RoboTextWriterText(line, sizeof(line), length, "..");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, run->range.end);
    length = RoboTextWriterText(line, sizeof(line), length, ") ");
    length = RoboTextWriterText(line, sizeof(line), length, RoboTextDirectionName(run->direction));
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    if (length + 1u > capacity) {
        if (outLength) {
            *outLength = length;
        }
        return RoboNotSupported;
    }
    memcpy(buffer, line, length + 1u);
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

/** One row's range and its trailing label, for the shared table writer. */
static void DirectionTableRow(const void *runs, size_t index, RoboTextRange *outRange, const char **outLabel)
{
    const RoboTextDirectionRun *run = &((const RoboTextDirectionRun *)runs)[index];

    *outRange = run->range;
    *outLabel = RoboTextDirectionName(run->direction);
}

Result RoboTextDirectionRunsWrite(RoboTextView view,
                                  const RoboTextDirectionRun *runs,
                                  size_t count,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength)
{
    return RoboTextWriterRunTable(view, runs, count, DirectionTableRow, "Direction Analysis\n", buffer,
                                  capacity, outLength);
}
