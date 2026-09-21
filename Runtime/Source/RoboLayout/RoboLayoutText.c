/*
 * RoboLayout text layout (Phase 10.5) — see RoboLayoutText.h.
 *
 * Three jobs, in the order the header argues them: the range arithmetic (code
 * points, not bytes), the style resolution (four properties, each with the CSS
 * initial value when the style carries no answer), and the seam driver (a call
 * that produces a run whatever the source says, including when there is no
 * source).
 *
 * Nothing here measures anything. The arithmetic of a glyph advance is RoboFont's
 * and lives in RoboLayoutTextFont.c; this file is the boundary, and keeping the
 * two apart is what lets the whole module be tested with a source that answers in
 * round numbers.
 */

#include "RoboLayout/RoboLayoutText.h"

#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboUnicode/RoboUnicode.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Ranges                                                              */
/* ------------------------------------------------------------------ */

LayoutTextRange LayoutTextRangeAt(uint32_t at)
{
    LayoutTextRange range;

    range.start = at;
    range.end = at;
    return range;
}

LayoutTextRange LayoutTextRangeFull(uint32_t codePoints)
{
    LayoutTextRange range;

    range.start = 0u;
    range.end = codePoints;
    return range;
}

uint32_t LayoutTextRangeLength(LayoutTextRange range)
{
    if (range.end <= range.start) {
        return 0u;
    }
    return range.end - range.start;
}

int LayoutTextRangeIsEmpty(LayoutTextRange range)
{
    return range.start >= range.end;
}

int LayoutTextRangeIsInverted(LayoutTextRange range)
{
    return range.start > range.end;
}

int LayoutTextRangeIsValid(LayoutTextRange range, uint32_t codePoints)
{
    /*
     * An inverted range is refused before anything is compared against the text:
     * `start > end` is not a span of anything, however long the text is, and a
     * caller that built one by subtracting has a bug this check is the first place
     * that can see.
     */
    if (range.start > range.end) {
        return 0;
    }
    return range.end <= codePoints;
}

int LayoutTextRangeContains(LayoutTextRange range, uint32_t index)
{
    return index >= range.start && index < range.end;
}

/* ------------------------------------------------------------------ */
/* Direction                                                           */
/* ------------------------------------------------------------------ */

int LayoutTextDirectionIsKnown(LayoutTextDirection direction)
{
    return direction == RoboLayoutTextDirectionLtr ||
           direction == RoboLayoutTextDirectionRtl;
}

const char *LayoutTextDirectionName(LayoutTextDirection direction)
{
    switch (direction) {
    case RoboLayoutTextDirectionLtr:
        return "ltr";
    case RoboLayoutTextDirectionRtl:
        return "rtl";
    default:
        break;
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* Style                                                               */
/* ------------------------------------------------------------------ */

void LayoutTextStyleInit(LayoutTextStyle *style)
{
    if (!style) {
        return;
    }
    style->fontSizePx = RoboLayoutTextInitialFontSizePx;
    style->fontWeight = RoboLayoutTextWeightNormal;
    style->italic = 0;
    style->direction = RoboLayoutTextDirectionLtr;
}

/** One computed value's keyword text, or an absent slice when there is none. */
static CssSlice TextStyleKeyword(const CssComputedStyle *computed,
                                           const char *property)
{
    const CssComputedValue *value;

    if (!computed || !property) {
        return CssSliceEmpty();
    }
    value = CssComputedStyleComputedByName(computed,
                                                     CssSliceFromText(property));
    if (!value || value->kind != RoboCssComputedValueKeyword) {
        return CssSliceEmpty();
    }
    return CssComputedValueText(value);
}

/**
 * The font weight @p computed declares, or the initial weight.
 *
 * A NUMBER is the author's own number, clamped to CSS's own valid range so that a
 * document saying `font-weight: 5000` measures with 1000 rather than with a
 * number no font has a face for. The four keywords are read by name, and `bolder`
 * and `lighter` are answered *relative to the initial weight* -- which is what CSS
 * does when there is no inherited weight to be relative to, and this record has no
 * parent by construction.
 */
static int32_t TextStyleWeight(const CssComputedStyle *computed)
{
    const CssComputedValue *value;
    CssSlice keyword;

    if (!computed) {
        return RoboLayoutTextWeightNormal;
    }
    value = CssComputedStyleComputedByName(computed,
                                                     CssSliceFromText("font-weight"));
    if (!value) {
        return RoboLayoutTextWeightNormal;
    }
    if (value->kind == RoboCssComputedValueNumber) {
        double weight = value->number;

        if (!(weight >= 1.0)) {
            return 1;
        }
        if (weight > 1000.0) {
            return 1000;
        }
        return (int32_t)weight;
    }
    if (value->kind != RoboCssComputedValueKeyword) {
        return RoboLayoutTextWeightNormal;
    }
    keyword = CssComputedValueText(value);
    if (CssSliceEqualsAsciiCi(keyword, "bold")) {
        return RoboLayoutTextWeightBold;
    }
    if (CssSliceEqualsAsciiCi(keyword, "bolder")) {
        /* CSS's relative keyword, resolved against the initial weight because
         * there is no parent weight here (see above). */
        return RoboLayoutTextWeightBold;
    }
    if (CssSliceEqualsAsciiCi(keyword, "lighter")) {
        return 100;
    }
    return RoboLayoutTextWeightNormal;
}

Result LayoutTextStyleResolve(const CssComputedStyle *computed,
                                             LayoutTextStyle *outStyle)
{
    double size = 0.0;
    CssSlice keyword;

    if (!outStyle) {
        return RoboInvalidArgument;
    }
    LayoutTextStyleInit(outStyle);

    /*
     * `font-size` through 10.3's own reader, so that "a declared value this build
     * can resolve is a computed LENGTH in px, finite, non-negative and inside the
     * geometry's range" stays one rule with one implementation. A percentage, a
     * relative unit and a named size all fail that test and take the initial value
     * below, which is the property's own initial value and not a guess.
     */
    if (LayoutStylePxLength(computed, "font-size", &size) && size > 0.0) {
        outStyle->fontSizePx = size;
    }
    outStyle->fontWeight = TextStyleWeight(computed);

    keyword = TextStyleKeyword(computed, "font-style");
    if (CssSliceEqualsAsciiCi(keyword, "italic") ||
        CssSliceEqualsAsciiCi(keyword, "oblique")) {
        outStyle->italic = 1;
    }

    keyword = TextStyleKeyword(computed, "direction");
    if (CssSliceEqualsAsciiCi(keyword, "rtl")) {
        outStyle->direction = RoboLayoutTextDirectionRtl;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Request and run                                                     */
/* ------------------------------------------------------------------ */

void LayoutTextRunInit(LayoutTextRun *run)
{
    if (!run) {
        return;
    }
    memset(run, 0, sizeof(*run));
    /* memset leaves an ill-formed handle, which is what "no source" should look
     * like: `HtmlNodeHandleIsWellformed` answers 0 for it. */
    run->direction = RoboLayoutTextDirectionLtr;
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

void LayoutTextMetricsInit(LayoutTextMetrics *metrics)
{
    if (!metrics) {
        return;
    }
    memset(metrics, 0, sizeof(*metrics));
}

int LayoutTextMetricsIsUsable(const LayoutTextMetrics *metrics)
{
    return metrics && metrics->measure != NULL;
}

Result LayoutTextMeasure(const LayoutTextMetrics *metrics,
                                       const LayoutTextRequest *request,
                                       LayoutTextRun *outRun,
                                       LayoutDiag *diag)
{
    if (!outRun) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "text measurement needs a run to fill");
    }
    LayoutTextRunInit(outRun);

    if (!request) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "text measurement needs a request");
    }
    if (!HtmlNodeHandleIsWellformed(&request->textNode)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "text measurement needs a well-formed text node");
    }
    if (!request->style) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "text measurement needs resolved font inputs");
    }

    /*
     * The source is echoed onto the run before anything measures, so that every
     * answer below -- including the zero one -- names what it is about. A run that
     * did not say which text it measured would be unusable the moment a caller
     * held two of them.
     */
    outRun->source = request->textNode;
    outRun->range = request->range;
    outRun->direction = request->style->direction;

    /*
     * No source is the documented degradation rather than an error: the run stays
     * zero, the range above is echoed, and the pass counts it as *unmeasured*. That
     * is what keeps every 10.4 expectation about a textless container true in this
     * build, and it is why the counters distinguish "nothing measured this" from
     * "this measured to nothing".
     */
    if (!LayoutTextMetricsIsUsable(metrics)) {
        return RoboOk;
    }
    return metrics->measure(metrics->userdata, request, outRun);
}

Result LayoutTextMeasureNode(const HtmlNodeHandle *textNode,
                                           const CssComputedStyle *ownerStyle,
                                           const LayoutTextMetrics *metrics,
                                           LayoutTextRun *outRun,
                                           LayoutDiag *diag)
{
    LayoutTextStyle style;
    LayoutTextRequest request;
    uint32_t codePoints = 0u;
    Result status;

    if (!outRun) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "measuring a text node needs a run to fill");
    }
    LayoutTextRunInit(outRun);
    if (!textNode || !HtmlNodeHandleIsWellformed(textNode)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "measuring a text node needs a well-formed handle");
    }

    /*
     * The range first, from the node's own *code points* -- see the header. This
     * is also the call that proves the node is a text node at all, so the two
     * facts arrive together rather than being checked twice.
     */
    status = LayoutTextNodeCodePoints(textNode, &codePoints, diag);
    if (status != RoboOk) {
        return status;
    }

    status = LayoutTextStyleResolve(ownerStyle, &style);
    if (status != RoboOk) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, status,
                                "the font inputs could not be resolved");
    }

    memset(&request, 0, sizeof(request));
    request.textNode = *textNode;
    request.range = LayoutTextRangeFull(codePoints);
    request.style = &style;
    request.computed = ownerStyle;
    return LayoutTextMeasure(metrics, &request, outRun, diag);
}

/* ------------------------------------------------------------------ */
/* What the text direction saw                                         */
/* ------------------------------------------------------------------ */

void LayoutTextStatsInit(LayoutTextStats *stats)
{
    if (!stats) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

int LayoutTextStatsEqual(const LayoutTextStats *a,
                                 const LayoutTextStats *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->runsPlaced == b->runsPlaced && a->measured == b->measured &&
           a->unmeasured == b->unmeasured && a->empty == b->empty &&
           a->codePoints == b->codePoints &&
           a->glyphsMissing == b->glyphsMissing && a->rtl == b->rtl &&
           a->widthMax == b->widthMax && a->heightMax == b->heightMax &&
           a->depthMax == b->depthMax;
}

/* ------------------------------------------------------------------ */
/* Reading a text node                                                 */
/* ------------------------------------------------------------------ */

/** Borrowed text and byte length of @p textNode's payload. */
static Result TextNodePayload(const HtmlNodeHandle *textNode,
                                       const char **outText,
                                       size_t *outLength,
                                       LayoutDiag *diag)
{
    HtmlNodeView view;

    if (!textNode || !outText || !outLength) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "reading a text node needs a handle and two outs");
    }
    if (!HtmlNodeHandleIsWellformed(textNode)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "reading a text node needs a well-formed handle");
    }
    if (HtmlNodeGetView(textNode, &view, NULL) != RoboOk) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboNotFound,
                                "a text node handle did not resolve");
    }
    if (view.kind != RoboHtmlNodeKindText) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "a text run can only be measured inside a text node");
    }
    *outText = view.text ? view.text : "";
    *outLength = view.text ? view.textLength : 0u;
    return RoboOk;
}

Result LayoutTextNodeCodePoints(const HtmlNodeHandle *textNode,
                                               uint32_t *outCount,
                                               LayoutDiag *diag)
{
    const char *text = NULL;
    size_t length = 0u;
    size_t count = 0u;
    int ok = 0;
    Result status;

    if (!outCount) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "counting code points needs an out");
    }
    *outCount = 0u;
    status = TextNodePayload(textNode, &text, &length, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * REPLACE rather than STRICT: the tokenizer already decided what these bytes
     * mean, so a byte the DOM kept is a byte this count has to account for. STRICT
     * would answer "unmeasurable document" over one truncated sequence, which is a
     * refusal the layer above has no way to act on.
     */
    count = UnicodeUtf8Count(text, length, RoboUnicodeReplace, &ok);
    if (count > (size_t)0xFFFFFFFFu) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "a text node holds more code points than a range can index");
    }
    *outCount = (uint32_t)count;
    return RoboOk;
}

Result LayoutTextRangeBytes(const HtmlNodeHandle *textNode,
                                          LayoutTextRange range,
                                          size_t *outStart,
                                          size_t *outLength,
                                          LayoutDiag *diag)
{
    const char *text = NULL;
    size_t length = 0u;
    size_t offset = 0u;
    size_t index = 0u;
    size_t start = 0u;
    Result status;

    if (!outStart || !outLength) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "a byte span needs two outs");
    }
    *outStart = 0u;
    *outLength = 0u;
    if (LayoutTextRangeIsInverted(range)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "a text range cannot end before it starts");
    }
    status = TextNodePayload(textNode, &text, &length, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * Walk code point by code point rather than counting then seeking, and for a
     * reason the unit test for this function is written around: the walk is what
     * *proves* both endpoints landed on boundaries. A count-then-index
     * implementation would answer for a range that ended mid-sequence by landing
     * on the replacement character's start, which is a silently moved range.
     */
    while (index < range.start) {
        size_t next = offset;
        Uchar32 codePoint = 0;
        UnicodeStatus decoded;

        if (offset >= length) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                    "a text range starts past the end of the text");
        }
        /*
         * REPLACE mode answers OK for a malformed lead byte, consuming one byte
         * as U+FFFD (RoboUnicode.c's own rule), so a document the DOM kept is a
         * document this walk can always advance through. The `next <= offset`
         * guard is therefore not reachable through that path -- it is here
         * because a decode that stopped advancing would turn this loop into a
         * hang, and a hang is the one failure a range check cannot report.
         */
        decoded = UnicodeUtf8Decode(text, length, offset, RoboUnicodeReplace,
                                          &codePoint, &next);
        if (decoded != RoboUnicodeOk) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                    "the text node's own bytes could not be decoded");
        }
        if (next <= offset) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                    "decoding a text node's bytes stopped advancing");
        }
        offset = next;
        index++;
    }
    start = offset;

    while (index < range.end) {
        size_t next = offset;
        Uchar32 codePoint = 0;
        UnicodeStatus decoded;

        if (offset >= length) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                    "a text range ends past the end of the text");
        }
        decoded = UnicodeUtf8Decode(text, length, offset, RoboUnicodeReplace,
                                          &codePoint, &next);
        if (decoded != RoboUnicodeOk) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                    "the text node's own bytes could not be decoded");
        }
        if (next <= offset) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                    "decoding a text node's bytes stopped advancing");
        }
        offset = next;
        index++;
    }

    *outStart = start;
    *outLength = offset - start;
    return RoboOk;
}

int LayoutTextIsCollapsibleWhitespace(const char *text, size_t length)
{
    size_t index;

    if (!text || length == 0u) {
        return 1;
    }
    for (index = 0u; index < length; index++) {
        switch (text[index]) {
        case ' ':
        case '\t':
        case '\n':
        case '\f':
        case '\r':
            break; /* CSS's collapsible whitespace, and the whole of the set. */
        default:
            return 0;
        }
    }
    return 1;
}
