/*
 * RoboText's vocabulary (Phase 12.4) — see RoboTextTypes.h.
 *
 * The range algebra is the file's centre of gravity, and its two decisions are worth reading in code
 * rather than only in the header.
 *
 * **Every operation clamps rather than wraps.** `RoboTextRangeSlice` and `RoboTextRangeShift` saturate,
 * `RoboTextRangeUnion` ignores an inverted argument, and `RoboTextRangeLength` answers 0 for an
 * inverted range. A range is arithmetic on two `uint32_t`s, which means subtraction that can underflow
 * and addition that can overflow; the alternative to clamping is a range that reads as *inside* a small
 * text because its start wrapped, which is how a selection highlights the wrong bytes.
 *
 * **`Contains` is half-open and `Intersects` says so.** Two ranges that touch end-to-start do **not**
 * intersect — that is what half-open means, and it is the property a run walk relies on when it asks
 * whether a caret sits inside a run.
 */

#include "RoboText/RoboTextTypes.h"

#include "RoboText/RoboTextKerning.h"
#include "RoboText/RoboTextLigature.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Ranges                                                              */
/* ------------------------------------------------------------------ */

RoboTextRange RoboTextRangeAt(RoboTextScalarIndex at)
{
    RoboTextRange range;

    range.start = at;
    range.end = at;
    return range;
}

RoboTextRange RoboTextRangeFull(RoboTextScalarIndex scalars)
{
    return RoboTextRangeMake(0u, scalars);
}

RoboTextRange RoboTextRangeMake(RoboTextScalarIndex first, RoboTextScalarIndex last)
{
    RoboTextRange range;

    range.start = first;
    range.end = last;
    return range;
}

RoboTextScalarIndex RoboTextRangeLength(RoboTextRange range)
{
    if (range.start >= range.end) {
        return 0u;
    }
    return range.end - range.start;
}

int RoboTextRangeIsEmpty(RoboTextRange range)
{
    return range.start >= range.end;
}

int RoboTextRangeIsInverted(RoboTextRange range)
{
    return range.start > range.end;
}

int RoboTextRangeIsWithin(RoboTextRange range, RoboTextScalarIndex scalars)
{
    if (RoboTextRangeIsInverted(range)) {
        return 0;
    }
    return range.start <= scalars && range.end <= scalars;
}

int RoboTextRangeContains(RoboTextRange range, RoboTextScalarIndex index)
{
    return index >= range.start && index < range.end;
}

int RoboTextRangeContainsRange(RoboTextRange range, RoboTextRange other)
{
    if (RoboTextRangeIsInverted(other) || RoboTextRangeIsInverted(range)) {
        return 0;
    }
    return other.start >= range.start && other.end <= range.end;
}

int RoboTextRangeIntersects(RoboTextRange a, RoboTextRange b)
{
    if (RoboTextRangeIsEmpty(a) || RoboTextRangeIsEmpty(b)) {
        /* An empty range is a *position*, and a position is not a span: two carets never "share"
         * scalars, and a caret inside a run is `Contains`'s question rather than this one's. */
        return 0;
    }
    return a.start < b.end && b.start < a.end;
}

RoboTextRange RoboTextRangeUnion(RoboTextRange a, RoboTextRange b)
{
    RoboTextRange out;

    if (RoboTextRangeIsInverted(a)) {
        return b;
    }
    if (RoboTextRangeIsInverted(b)) {
        return a;
    }
    out.start = (a.start < b.start) ? a.start : b.start;
    out.end = (a.end > b.end) ? a.end : b.end;
    return out;
}

RoboTextRange RoboTextRangeSlice(RoboTextRange range, RoboTextScalarIndex from, RoboTextScalarIndex to)
{
    RoboTextScalarIndex length = RoboTextRangeLength(range);
    RoboTextScalarIndex first;
    RoboTextScalarIndex last;

    first = (from < length) ? from : length;
    last = (to < length) ? to : length;
    if (last < first) {
        last = first;
    }
    return RoboTextRangeMake(range.start + first, range.start + last);
}

int RoboTextRangeEqual(const RoboTextRange *a, const RoboTextRange *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->start == b->start && a->end == b->end;
}

RoboTextRange RoboTextRangeShift(RoboTextRange range, int32_t delta)
{
    RoboTextRange out = range;

    if (delta > 0) {
        uint32_t step = (uint32_t)delta;

        out.start = (range.start > RoboTextIndexMax - step) ? RoboTextIndexMax : range.start + step;
        out.end = (range.end > RoboTextIndexMax - step) ? RoboTextIndexMax : range.end + step;
    } else if (delta < 0) {
        uint32_t step = (uint32_t)(-delta);

        out.start = (range.start < step) ? 0u : range.start - step;
        out.end = (range.end < step) ? 0u : range.end - step;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Direction                                                           */
/* ------------------------------------------------------------------ */

int RoboTextDirectionIsKnown(RoboTextDirection direction)
{
    return direction == RoboTextDirectionUnknown || direction == RoboTextDirectionLtr ||
           direction == RoboTextDirectionRtl;
}

const char *RoboTextDirectionName(RoboTextDirection direction)
{
    switch (direction) {
        case RoboTextDirectionLtr:
            return "ltr";
        case RoboTextDirectionRtl:
            return "rtl";
        case RoboTextDirectionUnknown:
            return "unknown";
        default:
            return "unknown";
    }
}

int RoboTextDirectionIsRightToLeft(RoboTextDirection direction)
{
    return direction == RoboTextDirectionRtl;
}

/* ------------------------------------------------------------------ */
/* Writing mode and orientation (Phase 12.20)                          */
/* ------------------------------------------------------------------ */

int RoboTextWritingModeIsKnown(RoboTextWritingMode mode)
{
    return mode == RoboTextWritingHorizontal || mode == RoboTextWritingVerticalRightToLeft ||
           mode == RoboTextWritingVerticalLeftToRight;
}

const char *RoboTextWritingModeName(RoboTextWritingMode mode)
{
    switch (mode) {
        case RoboTextWritingHorizontal:
            return "horizontal-tb";
        case RoboTextWritingVerticalRightToLeft:
            return "vertical-rl";
        case RoboTextWritingVerticalLeftToRight:
            return "vertical-lr";
        default:
            /*
             * A value from beyond the enum is *named* rather than reported as one of the three: a dump that
             * said `horizontal-tb` for an out-of-range word would hide the bug that produced it, and this
             * layer's rule is that it does not answer with a plausible wrong number.
             */
            return "unknown";
    }
}

int RoboTextTextOrientationIsKnown(RoboTextTextOrientation orientation)
{
    return orientation == RoboTextTextOrientationMixed || orientation == RoboTextTextOrientationUpright ||
           orientation == RoboTextTextOrientationSideways;
}

const char *RoboTextTextOrientationName(RoboTextTextOrientation orientation)
{
    switch (orientation) {
        case RoboTextTextOrientationMixed:
            return "mixed";
        case RoboTextTextOrientationUpright:
            return "upright";
        case RoboTextTextOrientationSideways:
            return "sideways";
        default:
            return "unknown";
    }
}

int RoboTextGlyphOrientationIsKnown(RoboTextGlyphOrientation orientation)
{
    return orientation == RoboTextGlyphOrientationUpright || orientation == RoboTextGlyphOrientationSideways;
}

const char *RoboTextGlyphOrientationName(RoboTextGlyphOrientation orientation)
{
    switch (orientation) {
        case RoboTextGlyphOrientationUpright:
            return "upright";
        case RoboTextGlyphOrientationSideways:
            return "sideways";
        default:
            return "unknown";
    }
}

int RoboTextGlyphOrientationIsUpright(RoboTextGlyphOrientation orientation)
{
    return orientation == RoboTextGlyphOrientationUpright;
}

/* ------------------------------------------------------------------ */
/* Direction classes, modes and the paragraph context                  */
/* ------------------------------------------------------------------ */

int RoboTextDirectionClassIsKnown(RoboTextDirectionClass directionClass)
{
    return directionClass == RoboTextDirectionClassNeutral ||
           directionClass == RoboTextDirectionClassLeftToRight ||
           directionClass == RoboTextDirectionClassRightToLeft ||
           directionClass == RoboTextDirectionClassNumber ||
           directionClass == RoboTextDirectionClassAttachLeft;
}

const char *RoboTextDirectionClassName(RoboTextDirectionClass directionClass)
{
    switch (directionClass) {
        case RoboTextDirectionClassLeftToRight:
            return "ltr";
        case RoboTextDirectionClassRightToLeft:
            return "rtl";
        case RoboTextDirectionClassNumber:
            return "number";
        case RoboTextDirectionClassAttachLeft:
            return "attach-left";
        case RoboTextDirectionClassNeutral:
            return "neutral";
        default:
            return "neutral";
    }
}

int RoboTextDirectionModeIsKnown(RoboTextDirectionMode mode)
{
    return mode == RoboTextDirectionModeAuto || mode == RoboTextDirectionModeExplicitLtr ||
           mode == RoboTextDirectionModeExplicitRtl;
}

const char *RoboTextDirectionModeName(RoboTextDirectionMode mode)
{
    switch (mode) {
        case RoboTextDirectionModeExplicitLtr:
            return "ltr";
        case RoboTextDirectionModeExplicitRtl:
            return "rtl";
        case RoboTextDirectionModeAuto:
            return "auto";
        default:
            return "auto";
    }
}

void RoboTextDirectionContextInit(RoboTextDirectionContext *context)
{
    if (!context) {
        return;
    }
    context->mode = RoboTextDirectionModeAuto;
    /* Unknown rather than Ltr, on purpose: this stage has not run P3, so "no strong character and nothing
     * declared" is left unresolved rather than defaulted. A layout context that wants CSS's initial value
     * passes `{ Auto, Ltr }` instead — see the struct's own comment. */
    context->fallbackDirection = RoboTextDirectionUnknown;
}

RoboTextDirectionContext RoboTextDirectionContextForBase(RoboTextDirection declared)
{
    RoboTextDirectionContext context;

    RoboTextDirectionContextInit(&context);
    switch (declared) {
        case RoboTextDirectionLtr:
            context.mode = RoboTextDirectionModeExplicitLtr;
            context.fallbackDirection = RoboTextDirectionLtr;
            break;
        case RoboTextDirectionRtl:
            context.mode = RoboTextDirectionModeExplicitRtl;
            context.fallbackDirection = RoboTextDirectionRtl;
            break;
        case RoboTextDirectionUnknown:
        default:
            context.mode = RoboTextDirectionModeAuto;
            context.fallbackDirection = RoboTextDirectionUnknown;
            break;
    }
    return context;
}

RoboTextDirectionContext RoboTextDirectionContextExplicit(RoboTextDirection direction)
{
    RoboTextDirectionContext context;

    RoboTextDirectionContextInit(&context);
    if (direction == RoboTextDirectionLtr || direction == RoboTextDirectionRtl) {
        context.mode = (direction == RoboTextDirectionLtr) ? RoboTextDirectionModeExplicitLtr
                                                          : RoboTextDirectionModeExplicitRtl;
        context.fallbackDirection = direction;
    }
    return context;
}

int RoboTextDirectionContextIsValid(const RoboTextDirectionContext *context)
{
    if (!context) {
        return 0;
    }
    return RoboTextDirectionModeIsKnown(context->mode) &&
           RoboTextDirectionIsKnown(context->fallbackDirection);
}

int RoboTextDirectionContextEqual(const RoboTextDirectionContext *a, const RoboTextDirectionContext *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->mode == b->mode && a->fallbackDirection == b->fallbackDirection;
}

/* ------------------------------------------------------------------ */
/* Script                                                              */
/* ------------------------------------------------------------------ */

int RoboTextScriptIsNeutral(RoboTextScript script)
{
    /* Common is shared punctuation and digits; Inherited is a mark that takes its base's script. Both
     * are "not a script of their own", which is what a caller merges runs by. */
    return script == RoboTextScriptCommon || script == RoboTextScriptInherited;
}

int RoboTextScriptIsAnchor(RoboTextScript script)
{
    return !RoboTextScriptIsNeutral(script);
}

const char *RoboTextScriptName(RoboTextScript script)
{
    const char *name = UnicodeScriptShortName(script);

    return name ? name : "Zzzz";
}

/* ------------------------------------------------------------------ */
/* Optical size                                                        */
/* ------------------------------------------------------------------ */

int RoboTextOpticalSizeIsKnown(RoboTextOpticalSize size)
{
    return (int)size >= 0 && (int)size < (int)RoboTextOpticalSizeCount;
}

const char *RoboTextOpticalSizeName(RoboTextOpticalSize size)
{
    switch (size) {
        case RoboTextOpticalSmall:
            return "small";
        case RoboTextOpticalBody:
            return "body";
        case RoboTextOpticalUI:
            return "ui";
        case RoboTextOpticalDisplay:
            return "display";
        case RoboTextOpticalLargeDisplay:
            return "large-display";
        default:
            return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Language                                                            */
/* ------------------------------------------------------------------ */

void RoboTextLanguageInit(RoboTextLanguage *language)
{
    if (!language) {
        return;
    }
    memset(language->tag, 0, sizeof(language->tag));
}

Result RoboTextLanguageSet(RoboTextLanguage *language, const char *tag)
{
    size_t length;

    if (!language) {
        return RoboInvalidArgument;
    }
    RoboTextLanguageInit(language);
    if (!tag || tag[0] == '\0') {
        /* The empty tag is "nothing declared", written as emptiness (see the header). Clearing is the
         * documented answer rather than a failure. */
        return RoboOk;
    }

    length = strlen(tag);
    if (length >= (size_t)RoboTextLanguageMaxBytes) {
        return RoboInvalidArgument;
    }
    /*
     * BCP 47's own alphabet: ASCII alphanumerics and `-`. Deliberately *not* a full grammar — a
     * subtag-length check would be a second implementation of a specification this build does not
     * consume yet, and the point of validating here is that a tag which reached a shaping pass with a
     * space or a byte of UTF-8 in it would be a tag nothing could key a feature off.
     */
    for (size_t index = 0u; index < length; index++) {
        char c = tag[index];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-')) {
            return RoboParseError;
        }
    }
    memcpy(language->tag, tag, length + 1u);
    return RoboOk;
}

int RoboTextLanguageIsSet(const RoboTextLanguage *language)
{
    return language && language->tag[0] != '\0';
}

const char *RoboTextLanguageTag(const RoboTextLanguage *language)
{
    if (!language || language->tag[0] == '\0') {
        return "";
    }
    return language->tag;
}

int RoboTextLanguageEqual(const RoboTextLanguage *a, const RoboTextLanguage *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return strcmp(a->tag, b->tag) == 0;
}

/* ------------------------------------------------------------------ */
/* Font style                                                          */
/* ------------------------------------------------------------------ */

void RoboTextFontStyleInit(RoboTextFontStyle *style)
{
    if (!style) {
        return;
    }
    memset(style, 0, sizeof(*style));
    style->sizePx = RoboTextInitialFontSizePx;
    style->weight = (FontWeight)400u;
    style->style = RoboFontStyleNormal;
    style->stretch = RoboFontStretchNormal;
    style->lineHeightPx = RoboTextLineHeightFromFontMetrics;
    style->letterSpacingPx = 0.0f;
    style->opticalSize = RoboTextOpticalBody;
}

int RoboTextFontStyleIsValid(const RoboTextFontStyle *style)
{
    if (!style) {
        return 0;
    }
    /*
     * `!(x > 0.0f)` rather than `x <= 0.0f`: a NaN compares false against everything, so the negative
     * form rejects it while the positive form would let it through. The same trick, for the same
     * reason, as `RoboPaintText.c`'s validation of the same number.
     */
    if (!(style->sizePx > 0.0f)) {
        return 0;
    }
    if (style->sizePx > (float)INT32_MAX) {
        return 0;
    }
    /* Weight 0 is "unset" and is rejected: CSS's `normal` is 400, and a weight of nothing is a scale
     * this module cannot name (`RoboTypographyNearestWeight` says the same about its own vocabulary). */
    if (style->weight < 100u || style->weight > 900u) {
        return 0;
    }
    if (!FontStretchIsValid(style->stretch)) {
        return 0;
    }
    if (style->lineHeightPx < 0.0f || !(style->lineHeightPx == style->lineHeightPx)) {
        return 0;
    }
    if (style->letterSpacingPx != style->letterSpacingPx) {
        return 0;
    }
    if (!RoboTextOpticalSizeIsKnown(style->opticalSize)) {
        return 0;
    }
    return 1;
}

int RoboTextFontStyleEqual(const RoboTextFontStyle *a, const RoboTextFontStyle *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    /* Field by field rather than by bytes: two records built independently must compare equal, and
     * padding between members is not a fact about either of them. */
    return a->sizePx == b->sizePx && a->weight == b->weight && a->style == b->style &&
           a->stretch == b->stretch && a->lineHeightPx == b->lineHeightPx &&
           a->letterSpacingPx == b->letterSpacingPx && a->opticalSize == b->opticalSize;
}

/**
 * Append an unsigned decimal to @p buffer, returning the new length or -1 when it does not fit.
 *
 * A tiny integer writer rather than `snprintf`, because this module is compiled into the guest program,
 * which is freestanding and has no formatted-output machinery (`RoboDesktopText.c` writes its own for
 * the same reason). The `RoboTextMeasurementWrite` door cannot depend on one either — it has no
 * decimals to write.
 */
static int StyleAppendUnsigned(char *buffer, size_t capacity, size_t length, uint32_t value)
{
    char digits[11];
    int count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    if (length + (size_t)count + 1u > capacity) {
        return -1;
    }
    while (count > 0) {
        buffer[length++] = digits[--count];
    }
    buffer[length] = '\0';
    return (int)length;
}

/**
 * Write @p style into a scratch line, then copy it out only if it fits.
 *
 * Building into a local first is what makes "nothing is truncated" true by construction rather than by
 * arithmetic at four call sites: every append is bounds-checked against the *scratch* buffer, which is
 * large enough for the widest line this door can produce, so the only decision left is whether the
 * caller's buffer can hold the finished line.
 */
Result RoboTextFontStyleWrite(const RoboTextFontStyle *style,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength)
{
    char line[96];
    size_t length;
    uint32_t whole;
    uint32_t frac;
    int wrote;

    if (outLength) {
        *outLength = 0u;
    }
    if (!style || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    memcpy(line, "size=", 5u);
    length = 5u;
    /*
     * The size is written as whole pixels and one decimal digit. That is a lossy summary on purpose: the
     * line is for a dump, and printing `13.000000` would claim a precision the setting does not have. A
     * caller that needs the exact value reads the field.
     */
    whole = (uint32_t)style->sizePx;
    frac = (uint32_t)((style->sizePx - (float)whole) * 10.0f + 0.5f);
    wrote = StyleAppendUnsigned(line, sizeof(line), length, whole);
    if (wrote < 0) {
        return RoboNotSupported;
    }
    length = (size_t)wrote;
    line[length++] = '.';
    line[length++] = (char)('0' + (frac % 10u));
    line[length] = '\0';

    memcpy(line + length, " weight=", 8u);
    length += 8u;
    wrote = StyleAppendUnsigned(line, sizeof(line), length, (uint32_t)style->weight);
    if (wrote < 0) {
        return RoboNotSupported;
    }
    length = (size_t)wrote;

    memcpy(line + length, " style=", 7u);
    length += 7u;
    memcpy(line + length, style->style == RoboFontStyleNormal ? "normal" : "italic", 6u);
    length += 6u;
    line[length] = '\0';

    if (strlen(line) + 1u > capacity) {
        /* Nothing was written to the caller's buffer (it holds the empty string): a truncated setting
         * line reads as a *different* setting, which is the one thing a dump must not do. */
        if (outLength) {
            *outLength = strlen(line);
        }
        return RoboNotSupported;
    }
    memcpy(buffer, line, strlen(line) + 1u);
    if (outLength) {
        *outLength = strlen(line);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Shape options                                                       */
/* ------------------------------------------------------------------ */

void RoboTextShapeOptionsInit(RoboTextShapeOptions *options)
{
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->baseDirection = RoboTextDirectionUnknown;
    options->scriptHint = RoboTextScriptCommon;
    RoboTextLanguageInit(&options->language);
    RoboTextFeatureSetInit(&options->features);
    /* The ligature policy a caller gets without asking (Phase 12.12): standard, contextual and required on;
     * discretionary and historical off. Stated as data rather than left implicit, so "the default" is one
     * table in one place and a caller can change it one kind at a time. */
    RoboTextLigaturePolicyDefault(&options->ligatures);
    /* The kerning policy a caller gets without asking (Phase 12.13): `kern` on, no legacy fallback. Stated as
     * data for the same reason the ligature policy is — "kerning is on by default" is a decision, and it
     * belongs where a caller can read and change it. */
    RoboTextKerningPolicyDefault(&options->kerning);
    /* The writing setting a caller gets without asking (Phase 12.20): horizontal, and `mixed` orientation —
     * which is inert while the mode is horizontal, and is stated rather than zeroed because the value is
     * what a vertical caller's `text-orientation` declaration will replace. */
    options->writingMode = RoboTextWritingHorizontal;
    options->textOrientation = RoboTextTextOrientationMixed;
    options->flags = 0u;
}

int RoboTextShapeOptionsIsValid(const RoboTextShapeOptions *options)
{
    if (!options) {
        return 0;
    }
    if (!RoboTextDirectionIsKnown(options->baseDirection)) {
        return 0;
    }
    if (!RoboTextLigaturePolicyIsValid(&options->ligatures)) {
        return 0;
    }
    if (!RoboTextKerningPolicyIsValid(&options->kerning)) {
        return 0;
    }
    if (!RoboTextWritingModeIsKnown(options->writingMode) ||
        !RoboTextTextOrientationIsKnown(options->textOrientation)) {
        return 0;
    }
    if (options->featureRangeCount > RoboTextMaxFeatureRanges) {
        return 0;
    }
    for (size_t index = 0u; index < options->featureRangeCount; index++) {
        /* An empty or inverted range is not a range: it would cover no scalar and silently do nothing, which
         * is the failure a refusal exists to prevent. */
        if (RoboTextRangeIsEmpty(options->featureRanges[index].range) ||
            RoboTextRangeIsInverted(options->featureRanges[index].range)) {
            return 0;
        }
        if (!RoboTextFeatureSetIsValid(&options->featureRanges[index].features)) {
            return 0;
        }
    }
    return RoboTextFeatureSetIsValid(&options->features);
}

/** Non-zero when @p tag is four printable ASCII bytes, which is what an OpenType tag is. */
static int FeatureTagIsValid(const char *tag)
{
    if (!tag || strlen(tag) != 4u) {
        /* An OpenType feature tag is exactly four bytes. A three- or five-character tag would be stored
         * and then looked up by nothing, which is the failure this refusal exists to prevent. */
        return 0;
    }
    for (size_t index = 0u; index < 4u; index++) {
        char c = tag[index];

        if ((unsigned char)c < 0x20u || (unsigned char)c > 0x7Eu) {
            return 0;
        }
    }
    return 1;
}

Result RoboTextShapeOptionsAddFeature(RoboTextShapeOptions *options, const char *tag)
{
    if (!options) {
        return RoboInvalidArgument;
    }
    return RoboTextFeatureSetSet(&options->features, tag, RoboTextFeatureOn, 0u);
}

Result RoboTextShapeOptionsSetFeature(RoboTextShapeOptions *options,
                                      const char *tag,
                                      RoboTextFeatureState state,
                                      uint32_t value)
{
    if (!options) {
        return RoboInvalidArgument;
    }
    return RoboTextFeatureSetSet(&options->features, tag, state, value);
}

Result RoboTextShapeOptionsSetFeatureRange(RoboTextShapeOptions *options,
                                           RoboTextRange range,
                                           const char *tag,
                                           RoboTextFeatureState state,
                                           uint32_t value)
{
    RoboTextFeatureRange *entry = NULL;

    if (!options) {
        return RoboInvalidArgument;
    }
    if (RoboTextRangeIsEmpty(range) || RoboTextRangeIsInverted(range) ||
        !RoboTextFeatureStateIsKnown(state) || !FeatureTagIsValid(tag)) {
        return RoboInvalidArgument;
    }
    /*
     * One entry per range, found by its bounds: two calls for the same range are two settings on one rule, and
     * a second entry for the same scalars would leave the *order* of the request deciding which wins — which is
     * exactly the kind of implicit precedence this record exists to avoid.
     */
    for (size_t index = 0u; index < options->featureRangeCount; index++) {
        if (RoboTextRangeEqual(&options->featureRanges[index].range, &range)) {
            entry = &options->featureRanges[index];
            break;
        }
    }
    if (!entry) {
        if (options->featureRangeCount >= RoboTextMaxFeatureRanges) {
            return RoboNotSupported;
        }
        entry = &options->featureRanges[options->featureRangeCount];
        entry->range = range;
        RoboTextFeatureSetInit(&entry->features);
        options->featureRangeCount++;
    }
    return RoboTextFeatureSetSet(&entry->features, tag, state, value);
}

size_t RoboTextShapeOptionsFeatureRangeCount(const RoboTextShapeOptions *options)
{
    return options ? options->featureRangeCount : 0u;
}

const RoboTextFeatureRange *RoboTextShapeOptionsFeatureRange(const RoboTextShapeOptions *options,
                                                             size_t index)
{
    if (!options || index >= options->featureRangeCount) {
        return NULL;
    }
    return &options->featureRanges[index];
}

void RoboTextFeatureSetInit(RoboTextFeatureSet *set)
{
    if (!set) {
        return;
    }
    memset(set, 0, sizeof(*set));
}

int RoboTextFeatureSetIsValid(const RoboTextFeatureSet *set)
{
    if (!set) {
        return 0;
    }
    if (set->count > (size_t)RoboTextMaxFeatures) {
        return 0;
    }
    for (size_t index = 0u; index < set->count; index++) {
        /* Every stored tag was validated on the way in, so a count within range over a malformed tag
         * means somebody wrote the array directly — which is the one thing this check is for. */
        if (!FeatureTagIsValid(set->settings[index].tag)) {
            return 0;
        }
        if (!RoboTextFeatureStateIsKnown(set->settings[index].state)) {
            return 0;
        }
    }
    return 1;
}

Result RoboTextFeatureSetSet(RoboTextFeatureSet *set,
                             const char *tag,
                             RoboTextFeatureState state,
                             uint32_t value)
{
    if (!set || !RoboTextFeatureStateIsKnown(state)) {
        return RoboInvalidArgument;
    }
    if (!FeatureTagIsValid(tag)) {
        return RoboInvalidArgument;
    }
    for (size_t index = 0u; index < set->count; index++) {
        if (strncmp(set->settings[index].tag, tag, 4u) == 0) {
            /* The last word wins, in place: a caller that says `liga` on and then off holds one answer. */
            set->settings[index].state = state;
            set->settings[index].value = value;
            return RoboOk;
        }
    }
    if (set->count >= (size_t)RoboTextMaxFeatures) {
        return RoboNotSupported;
    }
    memset(set->settings[set->count].tag, 0, RoboTextFeatureTagBytes);
    memcpy(set->settings[set->count].tag, tag, 4u);
    set->settings[set->count].state = state;
    set->settings[set->count].value = value;
    set->count++;
    return RoboOk;
}

size_t RoboTextFeatureSetCount(const RoboTextFeatureSet *set)
{
    return set ? set->count : 0u;
}

const RoboTextFeatureSetting *RoboTextFeatureSetAt(const RoboTextFeatureSet *set, size_t index)
{
    return (set && index < set->count) ? &set->settings[index] : NULL;
}

const RoboTextFeatureSetting *RoboTextFeatureSetFind(const RoboTextFeatureSet *set, const char *tag)
{
    if (!set || !tag) {
        return NULL;
    }
    for (size_t index = 0u; index < set->count; index++) {
        if (strncmp(set->settings[index].tag, tag, 4u) == 0) {
            return &set->settings[index];
        }
    }
    return NULL;
}

int RoboTextFeatureStateIsKnown(RoboTextFeatureState state)
{
    return state == RoboTextFeatureDefault || state == RoboTextFeatureOn || state == RoboTextFeatureOff;
}

const char *RoboTextFeatureStateName(RoboTextFeatureState state)
{
    switch (state) {
        case RoboTextFeatureDefault:
            return "default";
        case RoboTextFeatureOn:
            return "on";
        case RoboTextFeatureOff:
            return "off";
        default:
            return "unknown";
    }
}

int RoboTextFeatureSetEqual(const RoboTextFeatureSet *a, const RoboTextFeatureSet *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b || a->count != b->count) {
        return 0;
    }
    for (size_t index = 0u; index < a->count; index++) {
        const RoboTextFeatureSetting *other =
            RoboTextFeatureSetFind(b, a->settings[index].tag);

        if (!other || other->state != a->settings[index].state ||
            other->value != a->settings[index].value) {
            return 0;
        }
    }
    return 1;
}

Result RoboTextFeatureSetWrite(const RoboTextFeatureSet *set,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength)
{
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!set || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    for (size_t index = 0u; index < set->count; index++) {
        const RoboTextFeatureSetting *setting = &set->settings[index];
        char word[24];
        size_t wordLength = 0u;
        int wrote;

        /* `tag`, then its state: nothing for an enabled feature (the plain form a stylesheet writes),
         * `=0` for one turned off, `=?` for one left to the script's default. A non-zero `value` is the
         * alternate index and is written as `tag=N`. */
        memcpy(word, setting->tag, 4u);
        wordLength = 4u;
        word[wordLength] = '\0';
        if (setting->state == RoboTextFeatureOff) {
            memcpy(word + wordLength, "=0", 3u);
            wordLength += 2u;
        } else if (setting->state == RoboTextFeatureDefault) {
            memcpy(word + wordLength, "=?", 3u);
            wordLength += 2u;
        } else if (setting->value != 0u) {
            word[wordLength++] = '=';
            wrote = StyleAppendUnsigned(word, sizeof(word), wordLength, setting->value);
            if (wrote < 0) {
                return RoboNotSupported;
            }
            wordLength = (size_t)wrote;
        }
        if (length + wordLength + 1u > capacity) {
            /* Complete-or-absent, as every writer here is: a truncated listing reads as other settings. */
            buffer[0] = '\0';
            if (outLength) {
                *outLength = length + wordLength;
            }
            return RoboNotSupported;
        }
        if (index != 0u) {
            buffer[length++] = ' ';
        }
        memcpy(buffer + length, word, wordLength);
        length += wordLength;
        buffer[length] = '\0';
    }
    if (set->count == 0u) {
        static const char kNone[] = "(none)";

        if (sizeof(kNone) > capacity) {
            return RoboNotSupported;
        }
        memcpy(buffer, kNone, sizeof(kNone));
        length = sizeof(kNone) - 1u;
    }
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}
