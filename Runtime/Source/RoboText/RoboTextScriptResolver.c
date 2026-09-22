/*
 * RoboText's script resolver (Phase 12.6) — see RoboTextUnicode.h.
 *
 * One algorithm, and the whole point of the file is that it exists **once**. The run set
 * (`RoboTextRuns.c`) and the analysis layer (`RoboTextAnalysis.c`) are both consumers: the first asks for
 * the per-scalar resolutions *and* the runs, the second for the runs alone. Phase 12.7 (direction runs)
 * will consume the same answer rather than re-deriving it.
 *
 * ## What changed in 12.6, and why the old rule was not enough
 *
 * 12.5's rule was a single carry: a Common or Inherited scalar took the previous resolved script, and a
 * scalar before the first resolved script took the next one. It was stable and it coalesced well
 * (`"abc 123 def"` was one run), and it was **blind to `Script_Extensions`**: it could not tell a space
 * between two stretches of Latin (which should join them) from a combining acute after a Greek base whose
 * *stated* extensions include Latin — and it could not keep a shared punctuation character neutral when
 * the context genuinely does not decide it.
 *
 * 12.6 resolves a *block* of neutrals against **both** sides at once, and consults the extension set.
 * The rule is stated in the order it is applied in `RoboTextUnicode.h`; the code below is that order and
 * nothing else:
 *
 *   prev == next                     → that script (checked first: no extension lookup, and it is the
 *                                      common case — punctuation and spaces between one script)
 *   mark / Inherited                 → prev, else next (a mark belongs to the base on its left)
 *   extensions list prev **and** next → neutral preserved (ambiguous; do not invent certainty)
 *   extensions list prev, else next  → that script
 *   prev, else next                  → carry-left, else carry-right
 *   nothing                          → Common
 *
 * ## Cost
 *
 * One forward pass over the scalars, with a deferred block of neutrals and **no allocation**: an anchor
 * closes the block before it, so each scalar is looked at twice and each neutral's extension set is
 * consulted at most once. Nothing here grows with the text — a 16 KB paragraph resolves in one pass and
 * one result array (the caller's), which `text.script.performance` asserts by counting allocations.
 *
 * ASCII never reaches a table at all (Phase 12.6 §"ASCII fast path"): `RoboTextUnicodeScript` answers the
 * block in two comparisons, `RoboTextUnicodeIsCombiningMark` declines below U+0300 in one, and
 * `RoboTextUnicodeScriptExtensionsInclude` reduces to equality — so a pure ASCII UI string is letters
 * (anchors) plus spaces and punctuation resolved by the `prev == next` short-circuit, which never asks the
 * extension table a question.
 */

#include "RoboText/RoboTextUnicode.h"

#include "RoboMem/RoboMem.h"
#include "RoboTextInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The rule                                                            */
/* ------------------------------------------------------------------ */

/** A scalar's own `Script` property, or the caller's hint when one is given. */
static RoboTextScript ScalarRawScript(Uchar32 scalar, RoboTextScript hint)
{
    if (hint != RoboTextScriptCommon) {
        return hint;
    }
    /* The public door, not the RoboUnicode one it wraps: this is what gives ASCII its block answer
     * (`RoboTextUnicodeAsciiScript`) instead of a binary search per character. */
    return (RoboTextScript)RoboTextUnicodeScript((RoboTextCodePoint)scalar);
}

/** Non-zero when a scalar attaches to the base on its **left**: an Inherited script, or any mark. */
static int ScalarAttachesLeft(Uchar32 scalar, RoboTextScript raw)
{
    return raw == RoboTextScriptInherited ||
           RoboTextUnicodeIsCombiningMark((RoboTextCodePoint)scalar);
}

/**
 * Resolve one neutral scalar against the anchors on both sides.
 *
 * `havePrev`/`haveNext` distinguish "no anchor on that side" from "the anchor's script happens to be
 * `Common`", which is a distinction the arithmetic would otherwise lose.
 */
static RoboTextScript ResolveNeutral(RoboTextCodePoint codePoint,
                                     int havePrev,
                                     RoboTextScript prev,
                                     int haveNext,
                                     RoboTextScript next,
                                     int attachesLeft)
{
    if (havePrev && haveNext && prev == next) {
        return prev;
    }
    if (attachesLeft) {
        return havePrev ? prev : (haveNext ? next : RoboTextScriptCommon);
    }
    if (!haveNext) {
        return havePrev ? prev : RoboTextScriptCommon;
    }
    {
        int compatPrev = havePrev && RoboTextUnicodeScriptExtensionsInclude(codePoint, prev);
        int compatNext = RoboTextUnicodeScriptExtensionsInclude(codePoint, next);

        if (compatPrev && compatNext) {
            /* Listed by both differing neighbours: the scalar genuinely participates in two scripts. The
             * honest answer is that the context does not decide, so the scalar is preserved neutral. */
            return RoboTextScriptCommon;
        }
        if (compatPrev) {
            return prev;
        }
        if (compatNext) {
            return next;
        }
    }
    /* Carry-left, then carry-right. This is the branch that keeps one space from splitting a mixed-script
     * line into two runs, and the branch that resolves a leading neutral to the script that follows. */
    return havePrev ? prev : next;
}

/* ------------------------------------------------------------------ */
/* Run accumulation                                                    */
/* ------------------------------------------------------------------ */

/** Accumulates the resolved runs as the scalars are resolved, coalescing as it goes. */
typedef struct ScriptRunWriter {
    RoboTextScript *outScripts; /* optional */
    RoboTextScriptRun *outRuns; /* optional */
    size_t runCapacity;
    size_t runCount;
    RoboTextScalarIndex runStart;
    RoboTextScript runScript;
    int runOpen;
} ScriptRunWriter;

static void WriterScalar(ScriptRunWriter *writer, RoboTextScalarIndex index, RoboTextScript script)
{
    if (writer->outScripts) {
        writer->outScripts[index] = script;
    }
    if (!writer->runOpen) {
        writer->runStart = index;
        writer->runScript = script;
        writer->runOpen = 1;
        return;
    }
    if (script != writer->runScript) {
        /* Close the run that ends here. Coalescing is by construction: a run only closes when the resolved
         * script changes, so two adjacent runs can never carry the same script. */
        if (writer->outRuns && writer->runCount < writer->runCapacity) {
            writer->outRuns[writer->runCount].range = RoboTextRangeMake(writer->runStart, index);
            writer->outRuns[writer->runCount].script = writer->runScript;
        }
        writer->runCount++;
        writer->runStart = index;
        writer->runScript = script;
    }
}

static void WriterClose(ScriptRunWriter *writer, RoboTextScalarIndex end)
{
    if (!writer->runOpen) {
        return;
    }
    if (writer->outRuns && writer->runCount < writer->runCapacity) {
        writer->outRuns[writer->runCount].range = RoboTextRangeMake(writer->runStart, end);
        writer->outRuns[writer->runCount].script = writer->runScript;
    }
    writer->runCount++;
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

Result RoboTextScriptRunsResolve(const Uchar32 *scalars,
                                 size_t count,
                                 RoboTextScript hint,
                                 RoboTextScript *outScripts,
                                 RoboTextScriptRun *outRuns,
                                 size_t runCapacity,
                                 size_t *outRunCount,
                                 RoboTextDiag *diag)
{
    ScriptRunWriter writer;
    RoboTextScript prev = RoboTextScriptCommon;
    size_t index = 0u;
    int havePrev = 0;

    if (!outRunCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "an out run count is required");
    }
    *outRunCount = 0u;
    if (!scalars && count != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "scalars are required for a non-empty text");
    }

    memset(&writer, 0, sizeof(writer));
    writer.outScripts = outScripts;
    writer.outRuns = outRuns;
    writer.runCapacity = runCapacity;

    if (hint != RoboTextScriptCommon) {
        /* A hint classifies every scalar as the hinted script; there is nothing to resolve. */
        for (size_t position = 0u; position < count; position++) {
            WriterScalar(&writer, (RoboTextScalarIndex)position, hint);
        }
        WriterClose(&writer, (RoboTextScalarIndex)count);
        *outRunCount = writer.runCount;
        if (outRuns && writer.runCount > runCapacity) {
            return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboNotSupported,
                                   "the caller's array cannot hold the script runs");
        }
        return RoboOk;
    }

    while (index < count) {
        RoboTextScript raw = ScalarRawScript(scalars[index], hint);

        if (RoboTextScriptIsAnchor(raw)) {
            WriterScalar(&writer, (RoboTextScalarIndex)index, raw);
            prev = raw;
            havePrev = 1;
            index++;
            continue;
        }

        /*
         * A block of neutrals: gather it, learn the anchor that closes it (if any), and only then resolve
         * each scalar — which is what lets the rule look at both sides without a backward pass or a stored
         * array.
         */
        {
            size_t blockStart = index;
            size_t blockEnd = index;
            RoboTextScript next = RoboTextScriptCommon;
            int haveNext;

            while (blockEnd < count && !RoboTextScriptIsAnchor(ScalarRawScript(scalars[blockEnd], hint))) {
                blockEnd++;
            }
            haveNext = blockEnd < count;
            if (haveNext) {
                next = ScalarRawScript(scalars[blockEnd], hint);
            }
            for (size_t position = blockStart; position < blockEnd; position++) {
                RoboTextScript blockRaw = ScalarRawScript(scalars[position], hint);
                RoboTextScript resolved = ResolveNeutral((RoboTextCodePoint)scalars[position], havePrev, prev,
                                                         haveNext, next,
                                                         ScalarAttachesLeft(scalars[position], blockRaw));

                WriterScalar(&writer, (RoboTextScalarIndex)position, resolved);
            }
            index = blockEnd;
        }
    }
    WriterClose(&writer, (RoboTextScalarIndex)count);

    *outRunCount = writer.runCount;
    if (outRuns && writer.runCount > runCapacity) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboNotSupported,
                               "the caller's array cannot hold the script runs");
    }
    return RoboOk;
}

Result RoboTextScriptRunsResolveView(RoboTextView view,
                                     RoboTextScript hint,
                                     RoboTextScriptRun *outRuns,
                                     size_t runCapacity,
                                     size_t *outRunCount,
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
    status = RoboTextScriptRunsResolve(scalars, (size_t)count, hint, NULL, outRuns, runCapacity,
                                       outRunCount, diag);
    RoboTextUnicodeScalarsFree(scalars);
    return status;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result RoboTextScriptRunWrite(const RoboTextScriptRun *run,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength)
{
    char line[96];
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
    length = RoboTextWriterText(line, sizeof(line), length, RoboTextScriptName(run->script));
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

/**
 * The byte offset at which scalar @p index begins, for a table whose rows name both index spaces.
 *
 * Walks the view's decoder once per call — the door is a dump, not a per-frame path — and the caller below
 * walks it once for the whole table rather than once per run.
 */
static Result ScriptRunsByteOffsets(RoboTextView view,
                                    size_t count,
                                    RoboTextByteIndex **out,
                                    RoboTextDiag *diag)
{
    RoboTextUnicodeDecoder decoder;
    RoboTextByteIndex *offsets;
    Result status;

    *out = NULL;
    if (count == 0u) {
        return RoboOk;
    }
    offsets = (RoboTextByteIndex *)RoboCalloc(count + 1u, sizeof(*offsets));
    if (!offsets) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the byte offsets could not be allocated");
    }
    status = RoboTextUnicodeDecoderInit(&decoder, view, diag);
    if (status != RoboOk) {
        RoboFree(offsets);
        return status;
    }
    offsets[0] = 0u;
    for (size_t index = 0u; index < count; index++) {
        if (!RoboTextUnicodeDecoderHasNext(&decoder)) {
            /* The runs describe more scalars than the view holds; the table is refused rather than filled
             * with offsets that belong to a different text. */
            RoboFree(offsets);
            return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                                   "the runs cover more scalars than the text holds");
        }
        (void)RoboTextUnicodeDecoderNext(&decoder);
        offsets[index + 1u] = (RoboTextByteIndex)decoder.lastEnd;
    }
    *out = offsets;
    return RoboOk;
}

/** One table row, with or without the byte span. Returns the row's length or RoboTextWriterOverflow. */
static size_t ScriptTableRow(char *line,
                             size_t capacity,
                             size_t length,
                             RoboTextRange range,
                             RoboTextScript script,
                             int hasBytes,
                             RoboTextByteIndex byteStart,
                             RoboTextByteIndex byteEnd)
{
    length = RoboTextWriterText(line, capacity, length, "[");
    length = RoboTextWriterUnsigned(line, capacity, length, range.start);
    length = RoboTextWriterText(line, capacity, length, "..");
    length = RoboTextWriterUnsigned(line, capacity, length, range.end);
    length = RoboTextWriterText(line, capacity, length, ") scalars");
    if (hasBytes) {
        length = RoboTextWriterText(line, capacity, length, "  bytes [");
        length = RoboTextWriterUnsigned(line, capacity, length, byteStart);
        length = RoboTextWriterText(line, capacity, length, "..");
        length = RoboTextWriterUnsigned(line, capacity, length, byteEnd);
        length = RoboTextWriterText(line, capacity, length, ")");
    }
    length = RoboTextWriterText(line, capacity, length, "  ");
    length = RoboTextWriterText(line, capacity, length, RoboTextScriptName(script));
    return length;
}

Result RoboTextScriptRunsWrite(RoboTextView view,
                               const RoboTextScriptRun *runs,
                               size_t count,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength)
{
    RoboTextByteIndex *offsets = NULL;
    RoboTextScalarIndex scalarCount = 0u;
    size_t required = 0u;
    int hasBytes = (view.bytes != NULL);

    if (outLength) {
        *outLength = 0u;
    }
    if (!runs || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    if (hasBytes) {
        /* With a view the table carries both index spaces, and the runs are checked against it; without
         * one it carries scalar ranges only, and there is nothing to check them against. */
        Result status = RoboTextViewScalarCount(view, &scalarCount, NULL);

        if (status != RoboOk) {
            return status;
        }
        for (size_t index = 0u; index < count; index++) {
            if (RoboTextRangeIsInverted(runs[index].range) ||
                !RoboTextRangeIsWithin(runs[index].range, scalarCount)) {
                return RoboInvalidArgument;
            }
        }
        status = ScriptRunsByteOffsets(view, (size_t)scalarCount, &offsets, NULL);
        if (status != RoboOk) {
            return status;
        }
    }

    /* Measuring pass: build every row into one reusable line, summing lengths, so a buffer that cannot
     * hold the table is left untouched rather than half written. */
    {
        char line[160];

        required = RoboTextWriterText(line, sizeof(line), 0u, "Script Analysis\n");
        if (required == RoboTextWriterOverflow) {
            RoboFree(offsets);
            return RoboNotSupported;
        }
        for (size_t index = 0u; index < count; index++) {
            size_t row = ScriptTableRow(line, sizeof(line), 0u, runs[index].range, runs[index].script,
                                        hasBytes,
                                        hasBytes ? offsets[runs[index].range.start] : 0u,
                                        hasBytes ? offsets[runs[index].range.end] : 0u);

            if (row == RoboTextWriterOverflow) {
                RoboFree(offsets);
                return RoboNotSupported;
            }
            required += row + 1u; /* + newline */
        }
    }

    if (required + 1u > capacity) {
        if (outLength) {
            *outLength = required;
        }
        RoboFree(offsets);
        return RoboNotSupported;
    }

    {
        char line[160];
        size_t length = 0u;

        memcpy(buffer, "Script Analysis\n", 16u);
        length = 16u;
        for (size_t index = 0u; index < count; index++) {
            size_t row = ScriptTableRow(line, sizeof(line), 0u, runs[index].range, runs[index].script,
                                        hasBytes,
                                        hasBytes ? offsets[runs[index].range.start] : 0u,
                                        hasBytes ? offsets[runs[index].range.end] : 0u);

            memcpy(buffer + length, line, row);
            length += row;
            buffer[length++] = '\n';
        }
        buffer[length] = '\0';
        if (outLength) {
            *outLength = length;
        }
    }
    RoboFree(offsets);
    return RoboOk;
}
