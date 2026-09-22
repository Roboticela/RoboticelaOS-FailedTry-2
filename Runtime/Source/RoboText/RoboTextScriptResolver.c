/*
 * RoboText's script resolver (Phase 12.5) — see RoboTextUnicode.h.
 *
 * One algorithm, and the whole point of the file is that it exists **once**. The neutrality rule it
 * implements is subtle enough that two copies of it would eventually disagree about where a run boundary
 * is, and the run set (`RoboTextRuns.c`) and the analysis layer (Phase 12.5's `RoboTextAnalysis.c`) are
 * both consumers: the first asks for the per-scalar resolutions *and* the runs, the second for the runs
 * alone. Phase 12.6 turns script segmentation into a stage of its own; when it does, it extends this file
 * rather than growing a second segmenter.
 *
 * The rule, in code: a scalar whose script is Common or Inherited does not start a run. It takes the
 * **previous** resolved script; a scalar *before* the first resolved script takes the **next** one, which
 * is why the leading-run case is handled by deferring its emission until the first resolved script is seen
 * rather than by a backward pass. One forward walk, and the answer never depends on where the reader is
 * standing.
 */

#include "RoboText/RoboTextUnicode.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The resolver                                                        */
/* ------------------------------------------------------------------ */

/** Record one run, counting it whether or not the caller's array can hold it. */
static void ScriptRunEmit(RoboTextScriptRun *outRuns,
                          size_t runCapacity,
                          size_t *runCount,
                          RoboTextScalarIndex start,
                          RoboTextScalarIndex end,
                          RoboTextScript script)
{
    if (outRuns && *runCount < runCapacity) {
        outRuns[*runCount].range = RoboTextRangeMake(start, end);
        outRuns[*runCount].script = script;
    }
    (*runCount)++;
}

Result RoboTextScriptRunsResolve(const Uchar32 *scalars,
                                 size_t count,
                                 RoboTextScript hint,
                                 RoboTextScript *outScripts,
                                 RoboTextScriptRun *outRuns,
                                 size_t runCapacity,
                                 size_t *outRunCount,
                                 RoboTextDiag *diag)
{
    RoboTextScript carry = RoboTextScriptCommon;
    RoboTextScalarIndex runStart = 0u;
    RoboTextScript runScript = RoboTextScriptCommon;
    size_t runCount = 0u;
    int haveCarry = 0;
    int runOpen = 0;

    if (!outRunCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "an out run count is required");
    }
    *outRunCount = 0u;
    if (!scalars && count != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "scalars are required for a non-empty text");
    }

    for (size_t index = 0u; index < count; index++) {
        RoboTextScript raw = (hint != RoboTextScriptCommon)
                                 ? hint
                                 : (RoboTextScript)MakeUnicodeScript(scalars[index]);
        RoboTextScript resolved;

        if (!RoboTextScriptIsNeutral(raw)) {
            if (!haveCarry) {
                /*
                 * The first resolved script: everything before it was provisionally neutral and belongs to
                 * *this* script. Rewriting the prefix is what makes "the next resolved script" true without
                 * a backward pass, and the open leading run takes the script at the same moment.
                 */
                if (outScripts) {
                    for (size_t back = 0u; back < index; back++) {
                        outScripts[back] = raw;
                    }
                }
                runScript = raw;
            }
            carry = raw;
            haveCarry = 1;
            resolved = raw;
        } else {
            resolved = haveCarry ? carry : RoboTextScriptCommon;
        }

        if (outScripts) {
            outScripts[index] = resolved;
        }
        if (!runOpen) {
            runStart = (RoboTextScalarIndex)index;
            runScript = resolved;
            runOpen = 1;
        } else if (resolved != runScript) {
            ScriptRunEmit(outRuns, runCapacity, &runCount, runStart, (RoboTextScalarIndex)index,
                          runScript);
            runStart = (RoboTextScalarIndex)index;
            runScript = resolved;
        }
    }
    if (runOpen) {
        ScriptRunEmit(outRuns, runCapacity, &runCount, runStart, (RoboTextScalarIndex)count, runScript);
    }

    *outRunCount = runCount;
    if (outRuns && runCount > runCapacity) {
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
