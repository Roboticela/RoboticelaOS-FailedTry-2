/*
 * RoboText's analysis runs (Phase 12.5) — see RoboTextAnalysis.h.
 *
 * The file is one pass that ends the canonical data flow:
 *
 *     decode all  →  script runs (shared resolver)  →  paragraph direction
 *                        ↓
 *                 one font selection per scalar (the selector's memo)
 *                        ↓
 *                 maximal spans where script, direction and font all agree
 *
 * Two details are the phase's requirements made concrete:
 *
 *   **A combining mark copies the font of the scalar before it.** So a mark never begins a run of its own,
 *   whatever coverage the selector would have reported for it — the guarantee that a base and its accent
 *   are one face, which a later `mark` attachment needs. The mark's *script* is the resolver's business and
 *   is already correct (Inherited takes the base's script), so the two layers do not fight.
 *
 *   **The output contains no glyphs.** An analysis run is classification plus a font identity. The bridge to
 *   real glyphs is `RoboTextShapeRequestToRun`, which produces exactly the `RoboTextRun` the 12.4 generator
 *   consumes — one pipeline, reached from the new front door, rather than a second generator beside it.
 */

#include "RoboText/RoboTextAnalysis.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct RoboTextAnalysis {
    RoboTextView view;
    RoboTextAnalysisRun *runs;
    size_t runCount;
    RoboTextScriptRun *scriptRuns;
    size_t scriptRunCount;
    RoboTextScalarIndex scalarCount;
    RoboTextDirection baseDirection;
    RoboTextScript scriptHint;
    RoboTextLanguage language;
    RoboTextFontStyle style;
    RoboTextShapeOptions options;
    size_t combiningMarks;
};

/* ------------------------------------------------------------------ */
/* The shape request                                                   */
/* ------------------------------------------------------------------ */

void RoboTextShapeRequestInit(RoboTextShapeRequest *request)
{
    if (!request) {
        return;
    }
    memset(request, 0, sizeof(*request));
    RoboTextFontContextInit(&request->font);
    RoboTextLanguageInit(&request->language);
}

int RoboTextShapeRequestIsUsable(const RoboTextShapeRequest *request)
{
    if (!request) {
        return 0;
    }
    if (!request->font.face || !RoboTextFontStyleIsValid(&request->font.style)) {
        return 0;
    }
    if (request->text.bytes == NULL && request->text.length != 0u) {
        return 0;
    }
    return !RoboTextRangeIsInverted(request->range);
}

Result RoboTextShapeRequestFromRun(const RoboTextAnalysisRun *run,
                                   RoboTextView text,
                                   const RoboTextFontStyle *style,
                                   const RoboTextLanguage *language,
                                   RoboTextDirection baseDirection,
                                   const RoboTextShapeOptions *options,
                                   RoboTextShapeRequest *out)
{
    RoboTextFontStyle requestStyle;

    if (!run || !style || !out) {
        return RoboInvalidArgument;
    }
    if (RoboTextRangeIsInverted(run->range)) {
        return RoboInvalidArgument;
    }
    RoboTextShapeRequestInit(out);
    out->text = text;
    out->range = run->range;
    out->script = run->script;
    out->direction = run->direction;
    out->baseDirection = baseDirection;
    out->font.face = run->font.face;
    /*
     * The setting is the analysis's own, adjusted to the run's **recorded** weight/style/stretch, so the
     * request agrees with the identity the run carries rather than with what the primary happened to ask
     * for — a fallback candidate that declared its own weight is the case this exists for.
     */
    requestStyle = *style;
    requestStyle.sizePx = run->font.sizePx;
    requestStyle.weight = run->font.weight;
    requestStyle.style = run->font.style;
    requestStyle.stretch = run->font.stretch;
    if (!RoboTextFontStyleIsValid(&requestStyle)) {
        return RoboInvalidArgument;
    }
    out->font.style = requestStyle;
    if (language) {
        out->language = *language;
    }
    if (options) {
        out->featureCount = options->featureCount;
        if (options->featureCount != 0u) {
            memcpy(out->features, options->features,
                   options->featureCount * (size_t)RoboTextFeatureTagBytes);
        }
    }
    return RoboOk;
}

Result RoboTextShapeRequestToRun(const RoboTextShapeRequest *request, RoboTextRun *out)
{
    if (!request || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->range = request->range;
    out->script = request->script;
    out->direction = request->direction;
    out->baseDirection = request->baseDirection;
    out->font = request->font;
    out->language = request->language;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

void RoboTextAnalysisDestroy(RoboTextAnalysis *analysis)
{
    if (!analysis) {
        return;
    }
    RoboFree(analysis->runs);
    RoboFree(analysis->scriptRuns);
    RoboFree(analysis);
}

Result RoboTextAnalysisCreate(RoboTextView view,
                              RoboTextFontSelector *selector,
                              const RoboTextShapeOptions *options,
                              RoboTextAnalysis **out,
                              RoboTextDiag *diag)
{
    RoboTextShapeOptions defaults;
    RoboTextAnalysis *analysis;
    const RoboTextFontContext *primary;
    Uchar32 *scalars = NULL;
    RoboTextScalarIndex count = 0u;
    RoboTextResolvedFont *runFonts = NULL;
    size_t runCount = 0u;
    Result status;

    if (!out || !selector) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a selector and an out pointer are required");
    }
    *out = NULL;
    if (view.bytes == NULL && view.length != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a view with a length needs bytes");
    }
    if (!options) {
        RoboTextShapeOptionsInit(&defaults);
        options = &defaults;
    }
    if (!RoboTextShapeOptionsIsValid(options)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "the shape options are not usable");
    }

    primary = RoboTextFontSelectorPrimary(selector);
    analysis = (RoboTextAnalysis *)RoboCalloc(1u, sizeof(*analysis));
    if (!analysis) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the analysis could not be allocated");
    }
    analysis->view = view;
    analysis->options = *options;
    analysis->scriptHint = options->scriptHint;
    analysis->language = options->language;
    if (primary) {
        analysis->style = primary->style;
    } else {
        RoboTextFontStyleInit(&analysis->style);
    }
    if (!RoboTextFontStyleIsValid(&analysis->style)) {
        RoboTextFontStyleInit(&analysis->style);
    }

    status = RoboTextUnicodeDecodeAll(view, &scalars, &count, diag);
    if (status != RoboOk) {
        RoboTextAnalysisDestroy(analysis);
        return status;
    }
    analysis->scalarCount = count;
    if (count == 0u) {
        analysis->baseDirection = (options->baseDirection == RoboTextDirectionUnknown)
                                      ? RoboTextDirectionUnknown
                                      : options->baseDirection;
        *out = analysis;
        return RoboOk;
    }

    analysis->scriptRuns = (RoboTextScriptRun *)RoboCalloc(count, sizeof(*analysis->scriptRuns));
    analysis->runs = (RoboTextAnalysisRun *)RoboCalloc(count, sizeof(*analysis->runs));
    if (!analysis->scriptRuns || !analysis->runs) {
        RoboTextUnicodeScalarsFree(scalars);
        RoboTextAnalysisDestroy(analysis);
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the analysis arrays could not be allocated");
    }

    /* The script runs come first, because everything after them is *per script run*: the font is chosen for
     * a coherent run rather than once per scalar (Phase 12.6, §"font selection operates on runs"). */
    status = RoboTextScriptRunsResolve(scalars, (size_t)count, options->scriptHint, NULL,
                                       analysis->scriptRuns, (size_t)count, &analysis->scriptRunCount,
                                       diag);
    if (status != RoboOk) {
        RoboTextUnicodeScalarsFree(scalars);
        RoboTextAnalysisDestroy(analysis);
        return status;
    }
    analysis->baseDirection = RoboTextUnicodeBaseDirection(scalars, (size_t)count,
                                                          options->baseDirection);

    for (RoboTextScalarIndex index = 0u; index < count; index++) {
        if (RoboTextUnicodeIsCombiningMark((RoboTextCodePoint)scalars[index])) {
            analysis->combiningMarks++;
        }
    }

    runFonts = (RoboTextResolvedFont *)RoboCalloc((analysis->scriptRunCount != 0u) ? analysis->scriptRunCount : 1u,
                                                  sizeof(*runFonts));
    if (!runFonts) {
        RoboTextUnicodeScalarsFree(scalars);
        RoboTextAnalysisDestroy(analysis);
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the run font array could not be allocated");
    }
    for (size_t scriptIndex = 0u; scriptIndex < analysis->scriptRunCount; scriptIndex++) {
        const RoboTextScriptRun *scriptRun = &analysis->scriptRuns[scriptIndex];

        status = RoboTextFontSelectorSelectRange(selector,
                                                 (const RoboTextCodePoint *)&scalars[scriptRun->range.start],
                                                 (size_t)RoboTextRangeLength(scriptRun->range),
                                                 &runFonts[scriptIndex], diag);
        if (status != RoboOk) {
            RoboFree(runFonts);
            RoboTextUnicodeScalarsFree(scalars);
            RoboTextAnalysisDestroy(analysis);
            return status;
        }
    }

    /*
     * The analysis runs are the script runs cut again by classified direction. There is no third cut for
     * the font, because the font is constant across a script run by construction — which is exactly the
     * property that keeps a base character and its combining mark in one face.
     */
    for (size_t scriptIndex = 0u; scriptIndex < analysis->scriptRunCount; scriptIndex++) {
        const RoboTextScriptRun *scriptRun = &analysis->scriptRuns[scriptIndex];
        RoboTextScalarIndex index = scriptRun->range.start;

        while (index < scriptRun->range.end) {
            RoboTextScalarIndex end = index + 1u;
            RoboTextDirection direction = RoboTextUnicodeClassifyDirection(
                (RoboTextCodePoint)scalars[index], analysis->baseDirection);

            while (end < scriptRun->range.end) {
                RoboTextDirection nextDirection = RoboTextUnicodeClassifyDirection(
                    (RoboTextCodePoint)scalars[end], analysis->baseDirection);

                if (nextDirection != direction) {
                    break;
                }
                end++;
            }
            analysis->runs[runCount].range = RoboTextRangeMake(index, end);
            analysis->runs[runCount].script = scriptRun->script;
            analysis->runs[runCount].direction = direction;
            analysis->runs[runCount].font = runFonts[scriptIndex];
            runCount++;
            index = end;
        }
    }

    analysis->runCount = runCount;
    RoboFree(runFonts);
    RoboTextUnicodeScalarsFree(scalars);
    *out = analysis;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

size_t RoboTextAnalysisRunCount(const RoboTextAnalysis *analysis)
{
    return analysis ? analysis->runCount : 0u;
}

const RoboTextAnalysisRun *RoboTextAnalysisRunAt(const RoboTextAnalysis *analysis, size_t index)
{
    if (!analysis || index >= analysis->runCount) {
        return NULL;
    }
    return &analysis->runs[index];
}

const RoboTextAnalysisRun *RoboTextAnalysisRunAtScalar(const RoboTextAnalysis *analysis,
                                                       RoboTextScalarIndex index)
{
    size_t low;
    size_t high;

    if (!analysis || analysis->runCount == 0u || index >= analysis->scalarCount) {
        return NULL;
    }
    low = 0u;
    high = analysis->runCount - 1u;
    while (low <= high) {
        size_t mid = low + (high - low) / 2u;

        if (RoboTextRangeContains(analysis->runs[mid].range, index)) {
            return &analysis->runs[mid];
        }
        if (index < analysis->runs[mid].range.start) {
            if (mid == 0u) {
                break;
            }
            high = mid - 1u;
        } else {
            low = mid + 1u;
        }
    }
    return NULL;
}

size_t RoboTextAnalysisScriptRunCount(const RoboTextAnalysis *analysis)
{
    return analysis ? analysis->scriptRunCount : 0u;
}

const RoboTextScriptRun *RoboTextAnalysisScriptRun(const RoboTextAnalysis *analysis, size_t index)
{
    if (!analysis || index >= analysis->scriptRunCount) {
        return NULL;
    }
    return &analysis->scriptRuns[index];
}

RoboTextScalarIndex RoboTextAnalysisScalarCount(const RoboTextAnalysis *analysis)
{
    return analysis ? analysis->scalarCount : 0u;
}

RoboTextDirection RoboTextAnalysisBaseDirection(const RoboTextAnalysis *analysis)
{
    return analysis ? analysis->baseDirection : RoboTextDirectionUnknown;
}

RoboTextScript RoboTextAnalysisScriptHint(const RoboTextAnalysis *analysis)
{
    return analysis ? analysis->scriptHint : RoboTextScriptCommon;
}

const RoboTextLanguage *RoboTextAnalysisLanguage(const RoboTextAnalysis *analysis)
{
    static const RoboTextLanguage empty = {{0}};

    return analysis ? &analysis->language : &empty;
}

const RoboTextFontStyle *RoboTextAnalysisStyle(const RoboTextAnalysis *analysis)
{
    static const RoboTextFontStyle initial = {RoboTextInitialFontSizePx,
                                             RoboFontWeightNormal,
                                             RoboFontStyleNormal,
                                             RoboFontStretchNormal,
                                             RoboTextLineHeightFromFontMetrics,
                                             0.0f,
                                             RoboTextOpticalBody};

    return analysis ? &analysis->style : &initial;
}

Result RoboTextAnalysisGetStats(const RoboTextAnalysis *analysis, RoboTextAnalysisStats *out)
{
    if (!analysis || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->runs = analysis->runCount;
    out->scriptRuns = analysis->scriptRunCount;
    out->combiningMarks = analysis->combiningMarks;
    out->scalars = analysis->scalarCount;
    for (size_t index = 0u; index < analysis->runCount; index++) {
        const RoboTextAnalysisRun *run = &analysis->runs[index];

        switch (run->font.source) {
            case RoboTextFontSourcePrimary:
                out->primaryRuns++;
                break;
            case RoboTextFontSourceFallback:
                out->fallbackRuns++;
                break;
            case RoboTextFontSourceNone:
            default:
                out->unresolvedRuns++;
                break;
        }
        if (run->direction == RoboTextDirectionRtl) {
            out->rtlRuns++;
        }
    }
    return RoboOk;
}

Result RoboTextAnalysisShapeRequest(const RoboTextAnalysis *analysis,
                                    size_t index,
                                    RoboTextShapeRequest *out)
{
    const RoboTextAnalysisRun *run;

    if (!analysis || !out) {
        return RoboInvalidArgument;
    }
    run = RoboTextAnalysisRunAt(analysis, index);
    if (!run) {
        return RoboInvalidArgument;
    }
    return RoboTextShapeRequestFromRun(run, analysis->view, &analysis->style, &analysis->language,
                                       analysis->baseDirection, &analysis->options, out);
}

Result RoboTextFuzzAnalysis(const uint8_t *data, size_t size)
{
    RoboTextView view;
    RoboTextFontSelector *selector = NULL;
    RoboTextAnalysis *analysis = NULL;

    if (!data && size != 0u) {
        return RoboInvalidArgument;
    }
    view = RoboTextViewMake((const char *)data, size);
    if (RoboTextFontSelectorCreate(&selector, NULL) != RoboOk) {
        return RoboError;
    }
    if (RoboTextAnalysisCreate(view, selector, NULL, &analysis, NULL) == RoboOk) {
        RoboTextAnalysisDestroy(analysis);
    }
    RoboTextFontSelectorDestroy(selector);
    return RoboOk;
}
